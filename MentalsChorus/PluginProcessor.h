#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>
#include <vector>

//==============================================================================
// Mentals Chorus: a modulated-delay chorus. A sine LFO (Rate) swings each
// channel's delay time around a centre point (Delay) by up to Depth
// milliseconds; reading the delay line at that continuously-changing,
// fractional-sample position (linearly interpolated, the same technique
// Mentals Autotune's granular pitch shifter uses) is what creates the
// characteristic shimmer/thickening, since the read position's rate of
// change is itself a slight, continuously-varying pitch shift. Feedback
// feeds a fraction of the wet signal back into the delay line for a
// richer, more resonant character.
//
// The two channels' LFOs are always 90 degrees out of phase with each
// other for stereo width -- a fixed internal choice (not a parameter) in
// line with this project's preference for not exposing knobs most users
// would never want to touch.
//==============================================================================
class MentalsChorusAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsChorusAudioProcessor();
    ~MentalsChorusAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Chorus"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    MentalsUI::PresetManager presetManager { apvts, "Mentals Chorus" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // The left channel's current LFO phase (0-1), polled by the editor's
    // LFO preview to draw a moving playhead -- purely cosmetic, updated
    // once per block rather than per sample.
    float getLfoPhase01() const noexcept { return lastLfoPhase01.load(); }

    juce::AudioParameterFloat* rateParam     = nullptr;
    juce::AudioParameterFloat* depthParam    = nullptr;
    juce::AudioParameterFloat* delayParam    = nullptr;
    juce::AudioParameterFloat* feedbackParam = nullptr;
    juce::AudioParameterFloat* mixParam      = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    static float readInterpolated (const std::vector<float>& buffer, int writePos, float delaySamples);

    double currentSampleRate = 44100.0;
    static constexpr int maxSupportedChannels = 8;

    std::array<std::vector<float>, maxSupportedChannels> delayLines;
    std::array<int, maxSupportedChannels> writePos {};
    std::array<float, maxSupportedChannels> lfoPhase {}; // radians; channel 1 is offset +90 degrees from channel 0
    std::array<float, maxSupportedChannels> lastWet {};

    std::atomic<float> lastLfoPhase01 { 0.0f };
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsChorusAudioProcessor)
};
