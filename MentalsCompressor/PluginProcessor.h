#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"

//==============================================================================
// Mentals Compressor: a feed-forward soft-knee compressor (see
// MentalsUI::DynamicsDSP) with attack/release, makeup gain, dry/wet mix, and
// an optional external sidechain input for ducking-style use.
//==============================================================================
class MentalsCompressorAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsCompressorAudioProcessor();
    ~MentalsCompressorAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Compressor"; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals Compressor" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Gain-reduction meter (see MentalsUI::GainReductionMeterComponent) --
    // the most negative (i.e. most reduction) value seen during the last
    // processed block.
    float getGainReductionDb() const noexcept { return currentGainReductionDb.load(); }

    // Cached parameter pointers, read directly by the editor's transfer-
    // curve display (which calls MentalsUI::DynamicsDSP::computeOutputDb()
    // itself so the graph can never disagree with what's actually processed).
    juce::AudioParameterFloat* thresholdParam   = nullptr;
    juce::AudioParameterFloat* ratioParam       = nullptr;
    juce::AudioParameterFloat* kneeParam        = nullptr;
    juce::AudioParameterFloat* attackParam      = nullptr;
    juce::AudioParameterFloat* releaseParam     = nullptr;
    juce::AudioParameterFloat* makeupGainParam  = nullptr;
    juce::AudioParameterFloat* mixParam         = nullptr;
    juce::AudioParameterBool*  useSidechainParam = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;

    MentalsUI::DynamicsDSP::EnvelopeFollower envelopeFollower;

    std::atomic<float> currentGainReductionDb { 0.0f };
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsCompressorAudioProcessor)
};
