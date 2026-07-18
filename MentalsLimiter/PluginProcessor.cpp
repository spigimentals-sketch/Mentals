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
    truePeakParam  = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("truePeak"));

    seedFactoryPresetsIfMissing();
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

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "truePeak", "True Peak", true));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsLimiterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
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

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    oversampler->reset();

    gainedBuffer.setSize (2, samplesPerBlock);
    truePeakLevel.assign ((size_t) samplesPerBlock, 0.0f);

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
    const int oversamplingChannels = juce::jmin (numChannels, gainedBuffer.getNumChannels());

    const float inputGain  = juce::Decibels::decibelsToGain (inputGainParam->get());
    const float ceilingLin = juce::Decibels::decibelsToGain (ceilingParam->get());
    const float mix        = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);
    const bool  truePeakOn = truePeakParam->get();

    envelopeFollower.setAttackRelease (attackMs, releaseParam->get());

    // ---- Pass 1: apply Input Gain up front, into gainedBuffer -- needed as
    // a whole block (not interleaved sample-by-sample like the loop below)
    // so it can be handed to the oversampler in one call.
    gainedBuffer.setSize (gainedBuffer.getNumChannels(), numSamples, false, false, true);
    for (int ch = 0; ch < oversamplingChannels; ++ch)
    {
        auto* dst = gainedBuffer.getWritePointer (ch);
        const auto* src = buffer.getReadPointer (ch);
        for (int n = 0; n < numSamples; ++n)
            dst[n] = src[n] * inputGain;
    }

    // ---- True-peak scan (see class comment) --------------------------------------
    if ((int) truePeakLevel.size() < numSamples)
        truePeakLevel.resize ((size_t) numSamples);

    if (truePeakOn && oversampler != nullptr)
    {
        juce::dsp::AudioBlock<float> gainedBlock (gainedBuffer);
        auto trimmedBlock = gainedBlock.getSubsetChannelBlock (0, (size_t) oversamplingChannels).getSubBlock (0, (size_t) numSamples);
        auto oversampled = oversampler->processSamplesUp (juce::dsp::AudioBlock<const float> (trimmedBlock));

        const size_t factor = oversampled.getNumSamples() / (size_t) juce::jmax (1, numSamples);
        for (int n = 0; n < numSamples; ++n)
        {
            float peak = 0.0f;
            for (size_t ch = 0; ch < oversampled.getNumChannels(); ++ch)
            {
                const auto* data = oversampled.getChannelPointer (ch);
                for (size_t k = 0; k < factor; ++k)
                {
                    const size_t idx = (size_t) n * factor + k;
                    if (idx < oversampled.getNumSamples())
                        peak = juce::jmax (peak, std::abs (data[idx]));
                }
            }
            truePeakLevel[(size_t) n] = peak;
        }
    }
    else
    {
        for (int n = 0; n < numSamples; ++n)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < oversamplingChannels; ++ch)
                peak = juce::jmax (peak, std::abs (gainedBuffer.getReadPointer (ch)[n]));
            truePeakLevel[(size_t) n] = peak;
        }
    }

    // ---- Pass 2: envelope + look-ahead + gain application ------------------------
    float blockMinGainReductionDb = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        // Envelope reacts to the UNDELAYED, true-peak-aware level; gain is
        // applied to the DELAYED signal below -- see class comment for why
        // that's what makes this a genuine look-ahead limiter rather than a
        // plain one.
        const float envelope = envelopeFollower.process (truePeakLevel[(size_t) n]);
        const float gain = envelope > ceilingLin ? ceilingLin / envelope : 1.0f;
        const float gainReductionDb = juce::Decibels::gainToDecibels (gain);
        blockMinGainReductionDb = juce::jmin (blockMinGainReductionDb, gainReductionDb);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& delayLine = delayLines[(size_t) ch];
            const int delaySize = (int) delayLine.size();
            int& writePos = delayWritePos[(size_t) ch];

            const float gainedSample = ch < oversamplingChannels
                ? gainedBuffer.getReadPointer (ch)[n]
                : buffer.getReadPointer (ch)[n] * inputGain;

            const float delayed = delayLine[(size_t) writePos];
            delayLine[(size_t) writePos] = gainedSample;
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

//==============================================================================
// Factory presets: common mastering/bus-limiting starting points. True Peak
// is left on for every one of these -- see the class comment on why that's
// this plugin's own recommended default.
//==============================================================================
void MentalsLimiterAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto applyF = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };
    auto applyB = [] (juce::AudioParameterBool* p, bool value) { p->setValueNotifyingHost (value ? 1.0f : 0.0f); };

    resetToDefault();
    applyF (inputGainParam, 2.0f); applyF (ceilingParam, -0.3f); applyF (releaseParam, 150.0f);
    applyF (mixParam, 100.0f); applyB (truePeakParam, true);
    presetManager.savePreset ("Mastering Transparent");

    resetToDefault();
    applyF (inputGainParam, 6.0f); applyF (ceilingParam, -0.1f); applyF (releaseParam, 80.0f);
    applyF (mixParam, 100.0f); applyB (truePeakParam, true);
    presetManager.savePreset ("Loud and Safe");

    resetToDefault();
    applyF (inputGainParam, 4.0f); applyF (ceilingParam, -1.0f); applyF (releaseParam, 100.0f);
    applyF (mixParam, 100.0f); applyB (truePeakParam, true);
    presetManager.savePreset ("Streaming Loudness");

    resetToDefault();
    applyF (inputGainParam, 3.0f); applyF (ceilingParam, -0.3f); applyF (releaseParam, 50.0f);
    applyF (mixParam, 90.0f); applyB (truePeakParam, true);
    presetManager.savePreset ("Drum Bus Punch");

    resetToDefault();
    applyF (inputGainParam, 0.0f); applyF (ceilingParam, -0.3f); applyF (releaseParam, 200.0f);
    applyF (mixParam, 100.0f); applyB (truePeakParam, true);
    presetManager.savePreset ("Gentle Safety Ceiling");

    resetToDefault();
    applyF (inputGainParam, 3.0f); applyF (ceilingParam, -2.0f); applyF (releaseParam, 120.0f);
    applyF (mixParam, 100.0f); applyB (truePeakParam, true);
    presetManager.savePreset ("Broadcast Safe");

    resetToDefault();
}
