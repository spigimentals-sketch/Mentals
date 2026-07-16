#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int maxSupportedChannels = 8;
}

//==============================================================================
MentalsDeEsserAudioProcessor::MentalsDeEsserAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    frequencyParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("frequency"));
    thresholdParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("threshold"));
    ratioParam        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("ratio"));
    attackParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("attack"));
    releaseParam      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("release"));
    maxReductionParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("maxReduction"));
    mixParam          = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("mix"));
    listenParam       = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("listen"));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsDeEsserAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "frequency", "Frequency",
        juce::NormalisableRange<float> (2000.0f, 12000.0f, 0.01f, 0.4f), 6000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "threshold", "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f), -24.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ratio", "Ratio",
        juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f), 4.0f,
        juce::AudioParameterFloatAttributes().withLabel (":1")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "attack", "Attack",
        juce::NormalisableRange<float> (0.1f, 30.0f, 0.01f, 0.5f), 1.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release",
        juce::NormalisableRange<float> (10.0f, 300.0f, 0.01f, 0.5f), 60.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "maxReduction", "Max Reduction",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f), 12.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "listen", "Listen", false));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsDeEsserAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    splitFilterStates.assign ((size_t) juce::jmax (1, getTotalNumOutputChannels()), 0.0f);

    envelopeFollower.prepare (sampleRate);
    envelopeFollower.setAttackRelease (attackParam->get(), releaseParam->get());
    envelopeFollower.reset();

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
    currentGainReductionDb = 0.0f;
}

bool MentalsDeEsserAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut.size() >= 1 && mainOut.size() <= maxSupportedChannels;
}

void MentalsDeEsserAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const float freqHz     = frequencyParam->get();
    const float splitCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi * freqHz / (float) currentSampleRate);

    const float thresholdDb    = thresholdParam->get();
    const float ratio          = juce::jmax (1.0f, ratioParam->get());
    const float maxReductionDb = maxReductionParam->get();
    const float mix            = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);
    const bool  listen         = listenParam->get();

    envelopeFollower.setAttackRelease (attackParam->get(), releaseParam->get());

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) splitFilterStates.size(), maxSupportedChannels);
    const int numSamples  = buffer.getNumSamples();

    float blockMinGainReductionDb = 0.0f;
    float highBandValues[maxSupportedChannels];

    for (int n = 0; n < numSamples; ++n)
    {
        // Split each channel into Low (below Frequency) and High (the
        // complementary high-pass, i.e. dry - lowpassed dry -- guarantees
        // Low + High reconstructs the original exactly). The envelope
        // follower is driven by the loudest channel's High band, so the
        // gain reduction applied to every channel stays linked/coherent.
        float maxHighAbs = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = buffer.getReadPointer (ch)[n];
            float& lpState = splitFilterStates[(size_t) ch];
            lpState = splitCoeff * lpState + (1.0f - splitCoeff) * dry;

            const float highBand = dry - lpState;
            highBandValues[ch] = highBand;
            maxHighAbs = juce::jmax (maxHighAbs, std::abs (highBand));
        }

        const float envelope   = envelopeFollower.process (maxHighAbs);
        const float envelopeDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
        const float outputDb   = MentalsUI::DynamicsDSP::computeOutputDb (envelopeDb, thresholdDb, ratio, kneeDb);

        float gainReductionDb = outputDb - envelopeDb;
        gainReductionDb = juce::jmax (gainReductionDb, -maxReductionDb); // floor: never duck more than this
        blockMinGainReductionDb = juce::jmin (blockMinGainReductionDb, gainReductionDb);

        const float gainLinear = juce::Decibels::decibelsToGain (gainReductionDb);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const float dry      = data[n];
            const float highBand = highBandValues[ch];
            const float lowBand  = dry - highBand;

            if (listen)
            {
                // Monitoring utility for tuning Frequency/Threshold -- hear
                // only the (unprocessed) band being detected, not the
                // normal dry/wet output.
                data[n] = highBand;
            }
            else
            {
                const float wet = lowBand + highBand * gainLinear;
                data[n] = dry * (1.0f - mix) + wet * mix;
            }
        }
    }

    currentGainReductionDb.store (blockMinGainReductionDb);

    updateOutputLevelMeter (buffer);
}

void MentalsDeEsserAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
juce::AudioProcessorEditor* MentalsDeEsserAudioProcessor::createEditor()
{
    return new MentalsDeEsserAudioProcessorEditor (*this);
}

void MentalsDeEsserAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsDeEsserAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MentalsDeEsserAudioProcessor();
}
