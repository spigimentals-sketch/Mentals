#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>
#include <vector>

//==============================================================================
// Mentals Delay: a stereo delay line with feedback, dry/wet mix, ping-pong
// cross-feed, and low/high cut filtering in the feedback path so repeats
// darken and thin out over time instead of building up unchanged forever.
//==============================================================================
class MentalsDelayAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsDelayAudioProcessor();
    ~MentalsDelayAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; } // repeats continue after input stops

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    MentalsUI::PresetManager presetManager { apvts, "Mentals Delay" };

    // Restores every parameter to the value it was created with -- used by
    // the presets menu's "Default" entry, distinct from a saved/named preset.
    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Cached parameter pointers, read directly by the editor's echo-pattern
    // display (a parametric visualisation of the configured repeats, not a
    // live audio capture -- see PluginEditor's class comment for why).
    juce::AudioParameterFloat* delayTimeMsParam = nullptr;
    juce::AudioParameterFloat* feedbackParam    = nullptr;
    juce::AudioParameterFloat* mixParam         = nullptr;
    juce::AudioParameterBool*  pingPongParam    = nullptr;
    juce::AudioParameterFloat* lowCutParam      = nullptr;
    juce::AudioParameterFloat* highCutParam     = nullptr;

    // When off, the final output is summed to mono (both channels made
    // identical) regardless of Ping-Pong or anything else upstream -- a
    // mono-compatibility check/forcing switch, same control every Mentals
    // plugin now has.
    juce::AudioParameterBool* stereoParam = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;

    // One delay line per channel (up to stereo -- see isBusesLayoutSupported).
    struct DelayChannel
    {
        std::vector<float> buffer;
        int writePos = 0;
        float lowCutState = 0.0f;  // one-pole LP state, used to derive the HP in the feedback path
        float highCutState = 0.0f; // one-pole LP state (feedback path)

        void prepare (int numSamples)
        {
            buffer.assign ((size_t) numSamples, 0.0f);
            writePos = 0;
            lowCutState = 0.0f;
            highCutState = 0.0f;
        }
    };
    std::array<DelayChannel, 2> delayChannels;

    // Ramped so changing delay time/feedback/mix glides rather than clicks.
    // A ramping delay TIME briefly pitch-shifts the repeats while it moves,
    // same as a real tape/analog delay -- an accepted, expected character,
    // not a bug.
    juce::SmoothedValue<float> smoothedDelaySamples, smoothedFeedback, smoothedMix;

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsDelayAudioProcessor)
};
