#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MentalsCompressorAudioProcessor::MentalsCompressorAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                           .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                           .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    thresholdParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("threshold"));
    ratioParam        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("ratio"));
    kneeParam         = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("knee"));
    attackParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("attack"));
    releaseParam      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("release"));
    makeupGainParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("makeupGain"));
    mixParam          = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("mix"));
    useSidechainParam = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("useSidechain"));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsCompressorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "threshold", "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f), -18.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ratio", "Ratio",
        juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f), 4.0f,
        juce::AudioParameterFloatAttributes().withLabel (":1")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "knee", "Knee",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f), 6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "attack", "Attack",
        juce::NormalisableRange<float> (0.1f, 100.0f, 0.01f, 0.4f), 10.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release",
        juce::NormalisableRange<float> (10.0f, 1000.0f, 0.01f, 0.4f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "makeupGain", "Makeup",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "useSidechain", "Use External Sidechain", false));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsCompressorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    envelopeFollower.prepare (sampleRate);
    envelopeFollower.setAttackRelease (attackParam->get(), releaseParam->get());
    envelopeFollower.reset();

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
    currentGainReductionDb = 0.0f;
}

bool MentalsCompressorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sidechain = layouts.getChannelSet (true, 1);
        if (! sidechain.isDisabled() && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void MentalsCompressorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    auto mainBuffer = getBusBuffer (buffer, true, 0);
    auto sidechainBuffer = getBusCount (true) > 1 ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const bool useSidechain = useSidechainParam->get() && sidechainBuffer.getNumChannels() > 0;

    const int numChannels = mainBuffer.getNumChannels();
    const int numSamples  = mainBuffer.getNumSamples();

    const float thresholdDb  = thresholdParam->get();
    const float ratio        = juce::jmax (1.0f, ratioParam->get());
    const float kneeDb       = kneeParam->get();
    const float makeupGain   = juce::Decibels::decibelsToGain (makeupGainParam->get());
    const float mix          = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);

    envelopeFollower.setAttackRelease (attackParam->get(), releaseParam->get());

    float blockMinGainReductionDb = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        float levelAbs = 0.0f;

        if (useSidechain)
        {
            for (int ch = 0; ch < sidechainBuffer.getNumChannels(); ++ch)
                levelAbs = juce::jmax (levelAbs, std::abs (sidechainBuffer.getReadPointer (ch)[n]));
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
                levelAbs = juce::jmax (levelAbs, std::abs (mainBuffer.getReadPointer (ch)[n]));
        }

        const float envelope   = envelopeFollower.process (levelAbs);
        const float envelopeDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
        const float outputDb   = MentalsUI::DynamicsDSP::computeOutputDb (envelopeDb, thresholdDb, ratio, kneeDb);
        const float gainReductionDb = outputDb - envelopeDb;
        const float gainLinear = juce::Decibels::decibelsToGain (gainReductionDb);

        blockMinGainReductionDb = juce::jmin (blockMinGainReductionDb, gainReductionDb);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = mainBuffer.getWritePointer (ch);
            const float dry = data[n];
            const float wet = dry * gainLinear * makeupGain;
            data[n] = dry * (1.0f - mix) + wet * mix;
        }
    }

    currentGainReductionDb.store (blockMinGainReductionDb);

    updateOutputLevelMeter (mainBuffer);
}

void MentalsCompressorAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
juce::AudioProcessorEditor* MentalsCompressorAudioProcessor::createEditor()
{
    return new MentalsCompressorAudioProcessorEditor (*this);
}

void MentalsCompressorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsCompressorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MentalsCompressorAudioProcessor();
}
