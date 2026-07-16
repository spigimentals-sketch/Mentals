#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include "SaturatorDSP.h"
#include <vector>

//==============================================================================
// Mentals Saturator: drive into one of four waveshaping curves (see
// SaturatorDSP.h), a post-saturation Tone filter, output makeup gain, and a
// dry/wet Mix for parallel saturation.
//==============================================================================
class MentalsSaturatorAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsSaturatorAudioProcessor();
    ~MentalsSaturatorAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Saturator"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    MentalsUI::PresetManager presetManager { apvts, "Mentals Saturator" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Cached parameter pointers, read directly by the editor's transfer-
    // curve display (which calls SaturatorDSP::waveshape() itself so the
    // graph can never disagree with what's actually processed).
    juce::AudioParameterFloat*  driveParam      = nullptr;
    juce::AudioParameterChoice* typeParam       = nullptr;
    juce::AudioParameterFloat*  toneParam       = nullptr;
    juce::AudioParameterFloat*  outputGainParam = nullptr;
    juce::AudioParameterFloat*  mixParam        = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;

    // One-pole low-pass state per channel for the Tone control.
    std::vector<float> toneStates;

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsSaturatorAudioProcessor)
};
