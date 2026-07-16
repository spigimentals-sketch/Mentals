#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include "PitchDSP.h"
#include "AiAssistModel.h"
#include <algorithm>
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
//
// The correction target comes from one of three sources, in priority order:
// a held MIDI note (MIDI Control), the sidechain bus's own detected pitch
// (Sidechain Tuning), or ordinary scale-snapping -- see
// runPitchDetectionAndUpdateTarget()'s comment for exactly how they combine.
//
// The Harmonizer (2 voices) only engages during ordinary scale-snapping --
// harmonising "whatever MIDI note is held" or a sidechain target isn't
// something this generates, a deliberate scope boundary. Each voice shifts
// the DRY signal by a configurable number of scale degrees (not fixed
// semitones, so it stays correct on non-12-TET scales) from the detected
// pitch, and does not go through Formant Preservation (that would need up
// to 3x the FormantCorrector instances for a feature that's already
// disclosed as a simplified spectral-smoothing approximation, not worth
// tripling the CPU cost of).
//
// AI Assist (see beginVocalAnalysis()) analyses the input's own recently-
// detected pitch movement (avgAbsDelta/pitchRange/stdDev, computed in
// applySuggestedVocalSettings()) and runs those three numbers through a
// small trained model (see AiAssistModel.h) to get its suggested Retune
// Speed/Amount and style label -- a regressor and classifier trained on
// real VocalSet singing audio, with training labels generated from what
// was originally a hand-written heuristic formula. Unlike Mentals
// Multimode EQ's AI Assist (still the disclosed rule-based heuristic),
// this is a genuinely trained model, not just an if/else on the same
// numbers -- see Models/README.md for how it was trained.
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

    const juce::String getName() const override { return "Mentals Autotune"; }
    bool acceptsMidi() const override { return true; } // MIDI Control mode: a held note can drive the correction target
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
    juce::AudioParameterBool*   midiControlParam         = nullptr;
    juce::AudioParameterBool*   sidechainTuningParam     = nullptr;
    juce::AudioParameterBool*   lowLatencyModeParam      = nullptr;

    juce::AudioParameterBool*  harmony1EnabledParam = nullptr;
    juce::AudioParameterInt*   harmony1DegreeParam  = nullptr;
    juce::AudioParameterFloat* harmony1LevelParam   = nullptr;
    juce::AudioParameterBool*  harmony2EnabledParam = nullptr;
    juce::AudioParameterInt*   harmony2DegreeParam  = nullptr;
    juce::AudioParameterFloat* harmony2LevelParam   = nullptr;

    //==========================================================================
    // AI Assist: listens to a few seconds of live input, then suggests a
    // Retune Speed/Amount pairing based on how much the detected pitch
    // actually moved during that capture (steadier singing -> gentler,
    // slower suggestion; more movement -> snappier, stronger suggestion),
    // plus a rough "closest character" label purely as feedback text. This
    // is rule-based analysis of real, measured pitch behaviour, not a
    // classifier trained to recognise "pop" vs. "rap" vs. "opera" by name --
    // see applySuggestedVocalSettings()'s comment for exactly what's
    // measured and why the label should be read as a rough approximation.
    //==========================================================================
    void beginVocalAnalysis();
    bool isVocalAnalysisCapturing() const noexcept { return vocalAnalysisCapturing.load(); }
    bool isVocalAnalysisReady() const noexcept { return vocalAnalysisReady.load(); }

    // Reads the finished capture, computes and applies the suggested
    // Retune Speed/Amount, and updates the character-label guess. Returns
    // false (no-op) if the capture isn't ready yet.
    bool applySuggestedVocalSettings();

    // -1 = no analysis run yet; otherwise an index into the fixed label set
    // documented in applySuggestedVocalSettings()'s definition.
    int getVocalAnalysisLabelIndex() const noexcept { return lastAnalysisLabelIndex.load(); }
    float getSuggestedRetuneSpeedMs() const noexcept { return lastSuggestedRetuneMs.load(); }
    float getSuggestedAmount() const noexcept { return lastSuggestedAmount.load(); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void runPitchDetectionAndUpdateTarget();
    float computeStabilityScore() const noexcept;
    void processIncomingMidi (juce::MidiBuffer& midi);

    // Low-Latency Mode shrinks the main detector's analysis window/hop for
    // faster reaction at the cost of reduced low-frequency accuracy. Checked
    // once per block; reallocates the (tiny) analysis buffers only on the
    // rare occasion the toggle actually changes, not continuously.
    void reconfigureAnalysisWindowIfNeeded();

    // Writes out the built-in Natural/Robotic/Trap/Choral style presets the
    // first time this plugin runs (skipping any name the user has already
    // saved over), so they show up in the presets dropdown like any other
    // saved preset without this class needing special-cased UI for them.
    void ensureFactoryPresetsExist();

    double currentSampleRate = 44100.0;

    static constexpr float minDetectableFreqHz = 70.0f;
    static constexpr float maxDetectableFreqHz = 1200.0f;
    static constexpr float voicedConfidenceThreshold = 0.45f;
    static constexpr int maxSupportedChannels = 8;

    static constexpr double normalWindowSeconds     = 0.046;
    static constexpr double lowLatencyWindowSeconds = 0.012;
    bool lastLowLatencyModeApplied = false; // forces a reconfigure on the first block

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

    // MIDI Control: notes currently held, most-recently-pressed last (used
    // as the correction target with top priority over Sidechain Tuning and
    // ordinary scale-snapping whenever at least one note is held).
    std::vector<int> heldMidiNotes;

    // Sidechain Tuning: a second, independent pitch detector analysing the
    // optional sidechain bus, whose detected pitch becomes the correction
    // target (second priority, below MIDI Control) whenever it's confident.
    PitchDSP::StreamingPitchDetector sidechainDetector;

    // Harmonizer: each voice shifts the dry signal (not the corrected
    // signal) from the detected pitch to a target N scale degrees away,
    // using its own PitchShifterChannel per audio channel. Ratios are only
    // updated during ordinary scale-snapping (see class comment); the
    // "smoothed" values glide at the same rate as the main voice.
    std::array<PitchDSP::PitchShifterChannel, maxSupportedChannels> harmony1Shifters, harmony2Shifters;
    float targetHarmony1Ratio = 1.0f, smoothedHarmony1Ratio = 1.0f;
    float targetHarmony2Ratio = 1.0f, smoothedHarmony2Ratio = 1.0f;

    // AI Assist capture: a plain buffer of detected semitone values filled
    // by the audio thread (runPitchDetectionAndUpdateTarget(), only ever
    // appending, never resized while capturing) and read only after
    // vocalAnalysisReady is observed true from the message thread -- no
    // lock needed given that ordering.
    static constexpr int vocalAnalysisCaptureCount = 100; // ~2.3s of hops at the normal window size
    std::vector<float> vocalAnalysisSemitones;
    int vocalAnalysisCollected = 0;
    std::atomic<bool> vocalAnalysisCapturing { false };
    std::atomic<bool> vocalAnalysisReady { false };
    std::atomic<int> lastAnalysisLabelIndex { -1 };
    std::atomic<float> lastSuggestedRetuneMs { 0.0f };
    std::atomic<float> lastSuggestedAmount   { 0.0f };

    // AI Assist's trained model (see AiAssistModel.h) -- constructed once
    // here rather than lazily, since its embedded ONNX data is always
    // present and loading it is a one-off cost paid at plugin startup.
    AiAssistModel aiAssistModel;

    std::atomic<float> lastDetectedFreqHz { 0.0f };
    std::atomic<float> lastTargetFreqHz   { 0.0f };
    std::atomic<bool>  lastIsVoiced       { false };

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsAutotuneAudioProcessor)
};
