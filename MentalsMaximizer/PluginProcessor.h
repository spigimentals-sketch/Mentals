#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>
#include <vector>

//==============================================================================
// Mentals Maximizer: a mastering-grade loudness maximizer in the spirit of
// iZotope Ozone's own Maximizer module -- not a clone of its exact
// (proprietary, undocumented) IRC algorithms, but the same idea: push a
// signal up against a true-peak-safe ceiling via a look-ahead limiter,
// pick a "character" of algorithm rather than exposing raw attack/release
// knobs, and show input/output loudness alongside gain reduction so a
// mastering engineer can see the effect on perceived loudness, not just
// on peak level.
//
// Threshold vs Ceiling: unlike Mentals Limiter's "Input Gain", this uses
// Ozone's own mental model -- Threshold is how far below full scale the
// limiter starts pulling the signal down from (lower Threshold = more
// gain driven into the limiter = louder, denser result); Ceiling is the
// absolute true-peak output level that's never crossed. Internally,
// Threshold just drives an input gain of -Threshold dB, then the same
// look-ahead brick-wall math Mentals Limiter uses (undelayed envelope,
// gain applied to a delayed copy) targets the Ceiling.
//
// Algorithm + Character: four selectable algorithms (Modern/Classic/Warm/
// Aggressive) set a release-time range and a knee width; Character (0-100%)
// picks where in that range the release sits (0% = fastest/tightest,
// tracking transients closely for a transparent result; 100% = slowest,
// gluing the level down for a denser, louder-feeling result) and how much
// of that algorithm's own saturation curve is blended in on top of the
// limited signal -- Modern stays clean at any Character setting; Classic/
// Warm/Aggressive each add a different flavour of harmonic density as
// Character rises, the same "louder needs colour to still sound good"
// idea real loudness maximizers lean on.
//
// True Peak (ITU-R BS.1770 Annex 2) is always on here, not a toggle --
// see Mentals Limiter's own class comment for the oversampling technique;
// a "Maximizer" whose ceiling isn't true-peak-safe would defeat its own
// purpose. Stereo Unlink lets each channel's gain reduction run
// independently instead of sharing one linked value (Ozone's own toggle
// of the same name) -- linked is the default, correct choice for almost
// all mastering use.
//==============================================================================
class MentalsMaximizerAudioProcessor : public juce::AudioProcessor
{
public:
    enum Algorithm { algModern = 0, algClassic, algWarm, algAggressive, numAlgorithms };

    MentalsMaximizerAudioProcessor();
    ~MentalsMaximizerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Maximizer"; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals Maximizer" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Gain-reduction meter (see MentalsUI::GainReductionMeterComponent).
    float getGainReductionDb() const noexcept { return currentGainReductionDb.load(); }

    // Live input/output loudness (see MentalsUI::LoudnessDSP::LufsMeter) --
    // Momentary (400ms) is the most responsive reading of the three that
    // meter offers, matching what a live "how loud is this right now"
    // readout on a maximizer should show.
    float getInputLufs() const noexcept { return inputLufsMeter.getMomentaryLufs(); }
    float getOutputLufs() const noexcept { return outputLufsMeter.getMomentaryLufs(); }

    // Shared by processBlock() and the editor's transfer-curve display.
    static void getAlgorithmReleaseRangeMs (int algorithm, float& minReleaseMs, float& maxReleaseMs, float& kneeDb) noexcept;
    static float shapeSaturation (int algorithm, float x, float driveAmount) noexcept;

    juce::AudioParameterFloat*  thresholdParam    = nullptr;
    juce::AudioParameterFloat*  ceilingParam      = nullptr;
    juce::AudioParameterChoice* algorithmParam    = nullptr;
    juce::AudioParameterFloat*  characterParam    = nullptr;
    juce::AudioParameterBool*   stereoUnlinkParam = nullptr;
    juce::AudioParameterFloat*  mixParam          = nullptr;

    // When off, the final output is summed to mono (both channels made
    // identical) -- a mono-compatibility check/forcing switch, same control
    // every Mentals plugin now has. Distinct from Stereo Unlink, which
    // controls whether the two channels' detectors are independent, not
    // whether the final output is collapsed to mono.
    juce::AudioParameterBool* stereoParam = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;

    static constexpr float lookaheadMs = 5.0f;
    static constexpr int maxSupportedChannels = 2;
    static constexpr float attackMs = 0.1f; // all-but-instant -- look-ahead does the real work, see class comment
    static constexpr float limiterRatio = 500.0f; // effectively brick-wall, via the shared soft-knee compressor transfer function

    std::array<std::vector<float>, maxSupportedChannels> delayLines;
    std::array<int, maxSupportedChannels> delayWritePos {};

    // One envelope follower per channel -- always run independently, but
    // fed the SAME (max-across-channels) detector value when Stereo
    // Unlink is off, so linked mode still ends up with one shared gain.
    std::array<MentalsUI::DynamicsDSP::EnvelopeFollower, maxSupportedChannels> envelopeFollowers;

    // True Peak detection (always on -- see class comment), per channel so
    // Stereo Unlink can react to each channel's own peaks independently.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> gainedBuffer;
    std::array<std::vector<float>, maxSupportedChannels> truePeakLevel;

    MentalsUI::LoudnessDSP::LufsMeter inputLufsMeter, outputLufsMeter;

    std::atomic<float> currentGainReductionDb { 0.0f };
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsMaximizerAudioProcessor)
};
