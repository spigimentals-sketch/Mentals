#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>
#include <vector>

//==============================================================================
// Mentals Reverb: an algorithmic reverb built on juce::dsp::Reverb (a
// well-tested Freeverb-derived comb/allpass network) rather than a
// hand-rolled DSP algorithm -- reimplementing that from scratch would just
// be re-solving an already-solved problem. Adds a pre-delay stage (which
// juce::dsp::Reverb doesn't offer on its own) ahead of it, plus the usual
// room size/damping/width/mix/freeze controls.
//==============================================================================
class MentalsReverbAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsReverbAudioProcessor();
    ~MentalsReverbAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Reverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; } // long room sizes decay slowly

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    MentalsUI::PresetManager presetManager { apvts, "Mentals Reverb" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Cached parameter pointers, read directly by the editor's decay-envelope
    // display (a parametric visualisation of the configured tail, not a live
    // audio capture -- see PluginEditor's class comment for why).
    juce::AudioParameterFloat* roomSizeParam   = nullptr;
    juce::AudioParameterFloat* dampingParam    = nullptr;
    juce::AudioParameterFloat* widthParam      = nullptr;
    juce::AudioParameterFloat* mixParam        = nullptr;
    juce::AudioParameterFloat* preDelayMsParam = nullptr;
    juce::AudioParameterBool*  freezeParam     = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;

    juce::dsp::Reverb reverb;

    // Simple integer-sample circular buffer per channel, applied before the
    // signal reaches the reverb -- separates the dry attack from the wet
    // onset, a standard reverb feature juce::dsp::Reverb doesn't provide.
    std::array<std::vector<float>, 2> preDelayBuffers;
    std::array<int, 2> preDelayWritePos { 0, 0 };

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsReverbAudioProcessor)
};
