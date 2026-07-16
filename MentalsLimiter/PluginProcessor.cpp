#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MentalsLimiterAudioProcessor::MentalsLimiterAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    inputGainParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("inputGain"));
    ceilingParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("ceiling"));
    releaseParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("release"));
    mixParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("mix"));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsLimiterAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "inputGain", "Input Gain",
        juce::NormalisableRange<float> (-12.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ceiling", "Ceiling",
        juce::NormalisableRange<float> (-6.0f, 0.0f, 0.01f), -0.3f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release",
        juce::NormalisableRange<float> (1.0f, 1000.0f, 0.01f, 0.4f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsLimiterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    const int lookaheadSamples = juce::jmax (1, (int) (lookaheadMs * 0.001 * sampleRate));
    for (int ch = 0; ch < maxSupportedChannels; ++ch)
    {
        delayLines[(size_t) ch].assign ((size_t) lookaheadSamples, 0.0f);
        delayWritePos[(size_t) ch] = 0;
    }
    setLatencySamples (lookaheadSamples);

    envelopeFollower.prepare (sampleRate);
    envelopeFollower.setAttackRelease (attackMs, releaseParam->get());
    envelopeFollower.reset();

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
    currentGainReductionDb = 0.0f;
}

bool MentalsLimiterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void MentalsLimiterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const int numChannels = juce::jmin (buffer.getNumChannels(), maxSupportedChannels);
    const int numSamples  = buffer.getNumSamples();

    const float inputGain  = juce::Decibels::decibelsToGain (inputGainParam->get());
    const float ceilingLin = juce::Decibels::decibelsToGain (ceilingParam->get());
    const float mix        = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);

    envelopeFollower.setAttackRelease (attackMs, releaseParam->get());

    float blockMinGainReductionDb = 0.0f;
    std::array<float, maxSupportedChannels> gained {};

    for (int n = 0; n < numSamples; ++n)
    {
        float levelAbs = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            gained[(size_t) ch] = buffer.getReadPointer (ch)[n] * inputGain;
            levelAbs = juce::jmax (levelAbs, std::abs (gained[(size_t) ch]));
        }

        // Envelope reacts to the UNDELAYED level; gain is applied to the
        // DELAYED signal below -- see class comment for why that's what
        // makes this a genuine look-ahead limiter rather than a plain one.
        const float envelope = envelopeFollower.process (levelAbs);
        const float gain = envelope > ceilingLin ? ceilingLin / envelope : 1.0f;
        const float gainReductionDb = juce::Decibels::gainToDecibels (gain);
        blockMinGainReductionDb = juce::jmin (blockMinGainReductionDb, gainReductionDb);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& delayLine = delayLines[(size_t) ch];
            const int delaySize = (int) delayLine.size();
            int& writePos = delayWritePos[(size_t) ch];

            const float delayed = delayLine[(size_t) writePos];
            delayLine[(size_t) writePos] = gained[(size_t) ch];
            writePos = (writePos + 1) % delaySize;

            const float limited = delayed * gain;
            buffer.getWritePointer (ch)[n] = delayed * (1.0f - mix) + limited * mix;
        }
    }

    currentGainReductionDb.store (blockMinGainReductionDb);
    updateOutputLevelMeter (buffer);
}

void MentalsLimiterAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    // ~24dB/second release, so the meter falls smoothly rather than jumping.
    const float releasePerBlock = std::pow (10.0f, -24.0f * ((float) buffer.getNumSamples() / (float) currentSampleRate) / 20.0f);
    const float previous = outputPeakLinear.load();
    outputPeakLinear.store (juce::jmax (peak, previous * releasePerBlock));

    if (peak >= 1.0f)
        clipHoldBlocksRemaining.store ((int) (1.5 * currentSampleRate / juce::jmax (1, buffer.getNumSamples())));
    else if (clipHoldBlocksRemaining.load() > 0)
        clipHoldBlocksRemaining.fetch_sub (1);
}

//==============================================================================
juce::AudioProcessorEditor* MentalsLimiterAudioProcessor::createEditor()
{
    return new MentalsLimiterAudioProcessorEditor (*this);
}

void MentalsLimiterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsLimiterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}
