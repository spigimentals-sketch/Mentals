#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include "PitchDSP.h"
#include <array>
#include <vector>

//==============================================================================
// Mentals Autotune: monophonic pitch correction. See PitchDSP.h for the
// detection/shifting/formant-correction approach and its honestly-disclosed
// limitations (granular rather than PSOLA/phase-vocoder shifting; formant
// envelope matching via spectral smoothing rather than cepstral/LPC methods).
//
// Adaptive Retune reacts to how STABLE the recently detected pitch has been
// (see computeStabilityScore()): a sustained, steady note gets snappier
// correction, while a fast run or expressive slide gets a gentler, slower
// correction so it doesn't fight the performer's own pitch movement -- one
// mechanism serving what would otherwise be three separate asks ("dynamic
// pitch tracking for sustained notes vs. fast runs", "emotion-aware tuning",
// and "adaptive retune speed").
//==============================================================================
class MentalsAutotuneAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsAutotuneAudioProcessor();
    ~MentalsAutotuneAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals Autotune" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Polled by the editor's pitch-history display -- all updated once per
    // detection cycle (~every 20ish ms), not per sample.
    float getDetectedFrequencyHz() const noexcept { return lastDetectedFreqHz.load(); }
    float getTargetFrequencyHz() const noexcept { return lastTargetFreqHz.load(); }
    bool isVoiced() const noexcept { return lastIsVoiced.load(); }

    juce::AudioParameterChoice* keyParam               = nullptr;
    juce::AudioParameterChoice* scaleParam             = nullptr;
    juce::AudioParameterFloat*  retuneSpeedParam       = nullptr;
    juce::AudioParameterFloat*  amountParam            = nullptr;
    juce::AudioParameterFloat*  mixParam               = nullptr;
    juce::AudioParameterBool*   formantPreservationParam = nullptr;
    juce::AudioParameterBool*   adaptiveRetuneParam      = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void runPitchDetectionAndUpdateTarget();
    float computeStabilityScore() const noexcept;

    double currentSampleRate = 44100.0;

    static constexpr float minDetectableFreqHz = 70.0f;
    static constexpr float maxDetectableFreqHz = 1200.0f;
    static constexpr float voicedConfidenceThreshold = 0.45f;
    static constexpr int maxSupportedChannels = 8;

    int windowSizeSamples = 2048;
    int hopSizeSamples    = 1024;

    // Circular history of mono-summed input, always holding the most recent
    // windowSizeSamples samples; unwrapped into analysisWorkspace (windowed)
    // once per hop for the actual autocorrelation pass.
    std::vector<float> analysisRingBuffer;
    int analysisWritePos = 0;
    int samplesUntilNextHop = 0;
    std::vector<float> analysisWorkspace;

    float targetRatio    = 1.0f; // updated once per detection cycle
    float smoothedRatio  = 1.0f; // glides towards targetRatio every sample, at Retune Speed's rate

    std::array<PitchDSP::PitchShifterChannel, maxSupportedChannels> pitchShifters;

    // Formant preservation stage (see PitchDSP::FormantCorrector) plus the
    // matching dry-signal delay it requires: mixing an undelayed dry signal
    // against the formant corrector's necessarily-delayed output would
    // comb-filter the two against each other, so the dry path is run
    // through its own plain delay line of the same length whenever the
    // stage is active.
    std::array<PitchDSP::FormantCorrector, maxSupportedChannels> formantCorrectors;
    std::array<std::vector<float>, maxSupportedChannels> dryDelayLines;
    std::array<int, maxSupportedChannels> dryDelayWritePos {};
    int lastReportedLatencySamples = -1;

    // Adaptive Retune's note-stability tracking: a short history of recently
    // detected semitone values (one push per detection cycle, not per
    // sample), whose recent spread determines how "steady" the pitch has
    // been.
    static constexpr int stabilityHistoryLength = 6;
    std::array<float, stabilityHistoryLength> stabilityHistory {};
    int stabilityHistoryCount = 0;
    int stabilityHistoryPos = 0;

    std::atomic<float> lastDetectedFreqHz { 0.0f };
    std::atomic<float> lastTargetFreqHz   { 0.0f };
    std::atomic<bool>  lastIsVoiced       { false };

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsAutotuneAudioProcessor)
};
