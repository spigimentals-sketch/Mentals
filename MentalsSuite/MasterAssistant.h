#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <atomic>

class MentalsSuiteAudioProcessor; // full definition only needed in MasterAssistant.cpp

//==============================================================================
// Mentals Suite's "Master Assistant": Ozone-style mastering-by-reference,
// built as an orchestrator over the chain's EXISTING modules rather than a
// reimplementation of any of their DSP --
//
//   Tonal balance -- driven straight through to Mentals Multimode EQ's own
//   pre-existing EQ Match feature (loadEqMatchReferenceFile/
//   beginEqMatchCapture/applyEqMatch on MultiModeEQAudioProcessor): that
//   already solves spectral matching correctly, so Master Assistant's own
//   job is just making sure an EQ instance exists in the chain and driving
//   its capture/apply calls at the right moments, not re-measuring spectra
//   itself.
//
//   Loudness -- measured with the same MentalsUI::LoudnessDSP::LufsMeter
//   Mentals Mastering Meter uses (one live instance here, one offline pass
//   over the reference file), so "how many dB off is my mix" is the exact
//   same BS.1770 Integrated-loudness number the Mastering Meter module
//   would show.
//
//   Dynamics and stereo width -- crest factor (peak/RMS) and L/R
//   correlation aren't covered by any existing module's public API, so
//   this class measures both itself (see processOutputBlock()), the same
//   simple proxies already used elsewhere in this project (Mentals 360
//   Stereo Shaper's correlation meter, the AI Placement feature's crest-
//   factor fingerprint) -- then nudges Mentals Circuit Comp's global
//   Threshold/Ratio and Mentals 360 Stereo Shaper's Width from the
//   difference between "mine" and "reference".
//
// Two explicit steps, mirroring how a mastering engineer actually A/Bs a
// reference: load a reference file (instant, offline analysis), then
// Capture -- a few seconds of the CURRENT chain's live output, played by
// the user, analysed the same way. Only once both are ready does Apply do
// anything; Apply itself only ever nudges parameters (or creates a missing
// module and inserts it into a sensible spot in the chain -- see
// ensureModuleInChain()), never deletes or reorders anything the user
// already placed.
//
// Explicitly a starting point, not a finished master: every adjustment is
// a modest, clamped nudge toward the reference's numbers, not an attempt
// to replicate it exactly -- matching how every other "smart" feature in
// this project (EQ Match, AI Placement, AI Assist) works, since blindly
// forcing an exact match onto unrelated program material can sound worse,
// not better, than a human's own judgement applied on top of a sane
// starting point.
//==============================================================================
class MasterAssistant
{
public:
    explicit MasterAssistant (MentalsSuiteAudioProcessor& ownerSuite) : suite (ownerSuite) {}

    void prepare (double sampleRate);

    // Called every block from Suite's processBlock, fed the chain's final
    // output (post-graph, right before it's handed back to the host) --
    // the live LUFS meter always runs so Integrated/Momentary/Short-Term
    // are available the instant a capture window ends, and the
    // crest/correlation accumulators only run while capturing (see
    // beginCapture()).
    void processOutputBlock (const juce::AudioBuffer<float>& buffer);

    // Offline analysis of a loaded reference file: Integrated LUFS (via
    // MentalsUI::LoudnessDSP), crest factor, and L/R correlation across the
    // whole file. Message-thread only (does file I/O); false if the file
    // couldn't be read. Doesn't touch the chain -- see class comment.
    bool loadReferenceFile (const juce::File& file);
    bool hasReference() const noexcept { return referenceReady.load(); }
    juce::String getReferenceFileName() const;
    float getReferenceLufs() const noexcept { return referenceLufs.load(); }
    float getReferenceCrestDb() const noexcept { return referenceCrestDb.load(); }
    float getReferenceCorrelation() const noexcept { return referenceCorrelation.load(); }

    // Starts a ~3 second capture of the chain's live output -- ensures an
    // EQ slot exists and kicks off its own EQ-Match capture at the same
    // moment, so tonal/loudness/dynamics/width all measure the same few
    // seconds of the user's playback.
    void beginCapture();
    bool isCapturing() const noexcept { return capturing.load(); }
    float getCaptureProgress() const noexcept; // 0..1
    bool isCaptureReady() const noexcept { return captureReady.load(); }

    float getCapturedLufs() const noexcept { return capturedLufs.load(); }
    float getCapturedCrestDb() const noexcept { return capturedCrestDb.load(); }
    float getCapturedCorrelation() const noexcept { return capturedCorrelation.load(); }

    // Only available once both a reference and a capture are ready.
    bool canApply() const noexcept { return referenceReady.load() && captureReady.load(); }

    // Ensures EQ/Circuit Comp/Stereo Shaper/Limiter/Mastering Meter all
    // exist somewhere in the chain (see ensureModuleInChain()), then drives
    // EQ's applyEqMatch() plus heuristic nudges to the other four.
    void applyToChain();

private:
    int ensureModuleInChain (int moduleType);

    MentalsSuiteAudioProcessor& suite;

    MentalsUI::LoudnessDSP::LufsMeter liveLufsMeter;
    double currentSampleRate = 44100.0;

    static constexpr double captureSeconds = 3.0;
    std::atomic<bool> capturing { false };
    std::atomic<juce::int64> captureSamplesRemaining { 0 };
    juce::int64 captureTotalSamples = 1;

    // Audio-thread-only accumulators, valid only while capturing == true;
    // finalised into the atomics below on the audio thread the instant the
    // window ends, before captureReady is set -- the message thread only
    // ever reads the atomics, never these.
    double captureSumLL = 0.0, captureSumRR = 0.0, captureSumLR = 0.0;
    float capturePeak = 0.0f;
    juce::int64 captureSampleCount = 0;

    std::atomic<bool> captureReady { false };
    std::atomic<float> capturedLufs { -100.0f };
    std::atomic<float> capturedCrestDb { 0.0f };
    std::atomic<float> capturedCorrelation { 1.0f };

    juce::File referenceFile;
    std::atomic<bool> referenceReady { false };
    std::atomic<float> referenceLufs { -100.0f };
    std::atomic<float> referenceCrestDb { 0.0f };
    std::atomic<float> referenceCorrelation { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterAssistant)
};
