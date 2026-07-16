#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>
#include <vector>

//==============================================================================
// Mentals Limiter: a look-ahead brick-wall peak limiter. Input Gain drives
// the signal into the limiter; Ceiling is the absolute output level it will
// never exceed; Release controls how quickly gain recovers after a peak.
//
// How the look-ahead works: the peak-detection envelope (see
// MentalsUI::DynamicsDSP::EnvelopeFollower, reused from Compressor/De-esser
// with an all-but-instant attack) is computed from the UNDELAYED,
// input-gained signal, while gain reduction is applied to a DELAYED copy of
// that same signal (a short ring buffer, ~5ms, same read-before-write
// pattern Mentals Autotune's dry-signal delay uses for Formant
// Preservation). Because the envelope reacts to a loud transient before
// that same transient reaches the delayed output, the gain has already
// started dropping by the time the peak needs catching -- the standard
// look-ahead-limiter trick, not an approximation of one. The look-ahead
// time itself is a fixed 5ms rather than a parameter, matching this
// project's preference for not exposing knobs most users would never want
// to touch; it's what sets the reported plugin latency.
//
// Both channels always share one gain-reduction value (stereo-linked),
// the standard behaviour for a limiter -- letting channels reduce
// independently would shift the stereo image on transients.
//==============================================================================
class MentalsLimiterAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsLimiterAudioProcessor();
    ~MentalsLimiterAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Limiter"; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals Limiter" };

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
    // curve display (illustrative only -- it shows the static input/output
    // shape, not the look-ahead/envelope smoothing that's actually applied).
    juce::AudioParameterFloat* inputGainParam = nullptr;
    juce::AudioParameterFloat* ceilingParam   = nullptr;
    juce::AudioParameterFloat* releaseParam   = nullptr;
    juce::AudioParameterFloat* mixParam       = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;

    static constexpr float lookaheadMs = 5.0f;
    static constexpr int maxSupportedChannels = 8;
    static constexpr float attackMs = 0.1f; // all-but-instant -- look-ahead does the real work, see class comment

    std::array<std::vector<float>, maxSupportedChannels> delayLines;
    std::array<int, maxSupportedChannels> delayWritePos {};

    MentalsUI::DynamicsDSP::EnvelopeFollower envelopeFollower;

    std::atomic<float> currentGainReductionDb { 0.0f };
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsLimiterAudioProcessor)
};
