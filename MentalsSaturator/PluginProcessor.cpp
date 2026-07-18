#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MentalsSaturatorAudioProcessor::MentalsSaturatorAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    driveParam      = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("drive"));
    typeParam       = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("type"));
    toneParam       = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("tone"));
    outputGainParam = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("outputGain"));
    mixParam        = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("mix"));

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsSaturatorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "drive", "Drive",
        juce::NormalisableRange<float> (0.0f, 40.0f, 0.01f), 12.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "type", "Type",
        juce::StringArray { "Soft Clip", "Hard Clip", "Tube", "Foldback" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "tone", "Tone",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 75.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "outputGain", "Output",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsSaturatorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    toneStates.assign ((size_t) juce::jmax (1, getTotalNumOutputChannels()), 0.0f);

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
}

bool MentalsSaturatorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    // No cross-channel interaction in this effect (unlike Delay's ping-pong
    // or Reverb's stereo width), so any reasonable channel count works.
    return mainOut.size() >= 1 && mainOut.size() <= 8;
}

void MentalsSaturatorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const float driveGain  = juce::Decibels::decibelsToGain (driveParam->get());
    const float outputGain = juce::Decibels::decibelsToGain (outputGainParam->get());
    const float mix        = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);
    const int   type       = typeParam->getIndex();

    const float toneHz    = juce::jmap (toneParam->get(), 0.0f, 100.0f, 500.0f, 20000.0f);
    const float toneCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi * toneHz / (float) currentSampleRate);

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) toneStates.size());
    const int numSamples  = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        float& toneState = toneStates[(size_t) ch];

        for (int n = 0; n < numSamples; ++n)
        {
            const float dry     = data[n];
            const float driven  = dry * driveGain;
            const float shaped  = SaturatorDSP::waveshape (driven, type);

            toneState = toneCoeff * toneState + (1.0f - toneCoeff) * shaped;

            const float wet = toneState * outputGain;
            data[n] = dry * (1.0f - mix) + wet * mix;
        }
    }

    updateOutputLevelMeter (buffer);
}

void MentalsSaturatorAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
juce::AudioProcessorEditor* MentalsSaturatorAudioProcessor::createEditor()
{
    return new MentalsSaturatorAudioProcessorEditor (*this);
}

void MentalsSaturatorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsSaturatorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
// Factory presets: common colouration starting points across the four
// circuit types (Soft Clip=0, Hard Clip=1, Tube=2, Foldback=3).
//==============================================================================
void MentalsSaturatorAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto applyF = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };
    auto applyChoice = [] (juce::AudioParameterChoice* p, int index) { p->setValueNotifyingHost (p->convertTo0to1 ((float) index)); };

    resetToDefault();
    applyF (driveParam, 10.0f); applyChoice (typeParam, 2); applyF (toneParam, 65.0f);
    applyF (outputGainParam, 0.0f); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Tube Warmth");

    resetToDefault();
    applyF (driveParam, 8.0f); applyChoice (typeParam, 0); applyF (toneParam, 60.0f);
    applyF (outputGainParam, 0.0f); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Tape-Style Glue");

    resetToDefault();
    applyF (driveParam, 6.0f); applyChoice (typeParam, 2); applyF (toneParam, 75.0f);
    applyF (outputGainParam, 0.0f); applyF (mixParam, 60.0f);
    presetManager.savePreset ("Vocal Presence");

    resetToDefault();
    applyF (driveParam, 18.0f); applyChoice (typeParam, 1); applyF (toneParam, 55.0f);
    applyF (outputGainParam, -1.0f); applyF (mixParam, 45.0f);
    presetManager.savePreset ("Drum Bus Crunch");

    resetToDefault();
    applyF (driveParam, 15.0f); applyChoice (typeParam, 2); applyF (toneParam, 40.0f);
    applyF (outputGainParam, 0.0f); applyF (mixParam, 70.0f);
    presetManager.savePreset ("Bass Grit");

    resetToDefault();
    applyF (driveParam, 30.0f); applyChoice (typeParam, 3); applyF (toneParam, 50.0f);
    applyF (outputGainParam, -3.0f); applyF (mixParam, 50.0f);
    presetManager.savePreset ("Aggressive Foldback FX");

    resetToDefault();
    applyF (driveParam, 4.0f); applyChoice (typeParam, 0); applyF (toneParam, 65.0f);
    applyF (outputGainParam, 0.0f); applyF (mixParam, 35.0f);
    presetManager.savePreset ("Master Bus Subtle");

    resetToDefault();
}
