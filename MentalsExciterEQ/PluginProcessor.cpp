#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    // Skew so the Frequency knob's centre position lands near the default
    // (5kHz) rather than the arithmetic midpoint of its range.
    constexpr float exciterFreqSkew = 0.35f;

    // Fixed, not exposed as a knob -- a classic aural exciter generates new
    // high-frequency harmonic content via saturation, but left unfiltered
    // that content reads as harsh fizz rather than airy smoothness. A
    // gentle lowpass right after the saturation stage keeps the excited
    // band silky regardless of how hard Drive is pushed, which is the
    // whole point of this plugin over just cranking a treble shelf.
    constexpr float smoothingLowpassFreq = 15000.0f;
}

//==============================================================================
MentalsExciterEQAudioProcessor::MentalsExciterEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    frequencyParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("frequency"));
    driveParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("drive"));
    airGainParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("airGain"));
    mixParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("mix"));
    stereoParam    = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("stereo"));

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsExciterEQAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "frequency", "Frequency",
        juce::NormalisableRange<float> (1000.0f, 10000.0f, 1.0f, exciterFreqSkew), 5000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "drive", "Drive",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 30.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "airGain", "Air",
        juce::NormalisableRange<float> (-6.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereo", "Stereo", true));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsExciterEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    for (auto& ch : channels)
        ch.reset();

    lastExciterFreq = -1.0f;
    lastAirGainDb = 1.0e9f;

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
}

bool MentalsExciterEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void MentalsExciterEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    const float exciterFreq = frequencyParam->get();
    const float driveAmount = driveParam->get() * 0.01f;
    const float airGainDb   = airGainParam->get();
    const float mix         = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);

    if (exciterFreq != lastExciterFreq)
    {
        lastExciterFreq = exciterFreq;
        constexpr float q = 0.70710678f;
        auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, exciterFreq, q);
        auto lpCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, smoothingLowpassFreq, q);
        for (auto& ch : channels)
        {
            ch.exciterHighpass.coefficients = hpCoeffs;
            ch.smoothingLowpass.coefficients = lpCoeffs;
        }
    }

    if (airGainDb != lastAirGainDb)
    {
        lastAirGainDb = airGainDb;
        auto shelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            currentSampleRate, airShelfFreq, 0.70710678f, juce::Decibels::decibelsToGain (airGainDb));
        for (auto& ch : channels)
            ch.airShelf.coefficients = shelfCoeffs;
    }

    // Driving the tanh harder both saturates more heavily and generates
    // proportionally louder harmonics; the 1/tanh(driveGain) compensation
    // keeps the excited band from just getting louder as Drive increases
    // when the input is already near full scale, so Drive mostly changes
    // harmonic character/amount rather than doubling as a volume knob.
    const float driveGain = 1.0f + driveAmount * 9.0f;
    const float driveCompensation = 1.0f / std::tanh (driveGain);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& state = channels[(size_t) ch];
        auto* data = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            const float dry = data[n];

            const float highBand = state.exciterHighpass.processSample (dry);
            const float saturated = std::tanh (highBand * driveGain) * driveCompensation;
            const float excited = state.smoothingLowpass.processSample (saturated) - highBand;

            const float combined = dry + excited * driveAmount;
            const float shelved = state.airShelf.processSample (combined);

            data[n] = dry * (1.0f - mix) + shelved * mix;
        }
    }

    if (! stereoParam->get() && numChannels > 1)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float avg = 0.5f * (buffer.getReadPointer (0)[n] + buffer.getReadPointer (1)[n]);
            buffer.getWritePointer (0)[n] = avg;
            buffer.getWritePointer (1)[n] = avg;
        }
    }

    updateOutputLevelMeter (buffer);
}

void MentalsExciterEQAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    const float releasePerBlock = std::pow (10.0f, -24.0f * ((float) buffer.getNumSamples() / (float) currentSampleRate) / 20.0f);
    const float previous = outputPeakLinear.load();
    outputPeakLinear.store (juce::jmax (peak, previous * releasePerBlock));

    if (peak >= 1.0f)
        clipHoldBlocksRemaining.store ((int) (1.5 * currentSampleRate / juce::jmax (1, buffer.getNumSamples())));
    else if (clipHoldBlocksRemaining.load() > 0)
        clipHoldBlocksRemaining.fetch_sub (1);
}

//==============================================================================
void MentalsExciterEQAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto apply = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };

    // Air: tuned for vocal smoothness -- a higher crossover (so only the
    // very top end gets excited, not sibilance-adjacent midrange), modest
    // drive, a gentle air shelf lift, and a partial mix so it reads as
    // "brighter and softer" rather than an obviously processed effect.
    resetToDefault();
    apply (frequencyParam, 7000.0f);
    apply (driveParam, 25.0f);
    apply (airGainParam, 4.0f);
    apply (mixParam, 55.0f);
    presetManager.savePreset ("Air");

    resetToDefault();
}

//==============================================================================
juce::AudioProcessorEditor* MentalsExciterEQAudioProcessor::createEditor()
{
    return new MentalsExciterEQAudioProcessorEditor (*this);
}

void MentalsExciterEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsExciterEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}
