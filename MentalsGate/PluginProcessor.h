#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"

//==============================================================================
// Mentals Gate: a downward expander/noise gate (see
// MentalsUI::DynamicsDSP::computeExpanderOutputDb(), the mirror image of
// Compressor's own transfer function) with attack/release, a Range control
// capping how far it can attenuate, dry/wet mix, and an optional external
// sidechain input so it can be triggered by a different signal (e.g.
// gating a guitar bus keyed from a kick drum).
//==============================================================================
class MentalsGateAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsGateAudioProcessor();
    ~MentalsGateAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Gate"; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals Gate" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Gain-reduction meter (see MentalsUI::GainReductionMeterComponent) --
    // the most negative (i.e. most attenuated) value seen during the last
    // processed block.
    float getGainReductionDb() const noexcept { return currentGainReductionDb.load(); }

    // Cached parameter pointers, read directly by the editor's transfer-
    // curve display (which calls MentalsUI::DynamicsDSP::
    // computeExpanderOutputDb() itself so the graph can never disagree
    // with what's actually processed).
    juce::AudioParameterFloat* thresholdParam   = nullptr;
    juce::AudioParameterFloat* ratioParam       = nullptr;
    juce::AudioParameterFloat* attackParam      = nullptr;
    juce::AudioParameterFloat* releaseParam     = nullptr;
    juce::AudioParameterFloat* rangeParam       = nullptr;
    juce::AudioParameterFloat* mixParam         = nullptr;
    juce::AudioParameterBool*  useSidechainParam = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;
    static constexpr float kneeDb = 6.0f; // fixed, not a parameter -- same convention as Mentals De-esser

    MentalsUI::DynamicsDSP::EnvelopeFollower envelopeFollower;

    std::atomic<float> currentGainReductionDb { 0.0f };
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsGateAudioProcessor)
};
