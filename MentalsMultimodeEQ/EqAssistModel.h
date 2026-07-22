#pragma once

#include <onnxruntime_cxx_api.h>
#include <optional>
#include <memory>
#include <array>

//==============================================================================
// Wraps the trained models behind Mentals Multimode EQ's AI Assist "harsh
// frequency" and "Low Cut / High Cut" suggestions -- unlike the plugin's
// spectral-balance-shelf suggestions (still a disclosed rule-based
// heuristic, see PluginProcessor.cpp's applyAiAssistSuggestions()), these
// are genuinely trained models, the same kind of upgrade Autotune's AI
// Assist and Stereo Shaper's AI Placement already are. See Models/README.md
// for the full training pipeline.
//
// Four tasks, six small RandomForestRegressor/Classifier models:
//
// 1. Resonance cut (frequency, gain, adjusted bandwidth): for a candidate
//    harsh peak already found by applyAiAssistSuggestions()'s existing
//    peak-detection loop, predictResonance() replaces the old fixed
//    Q=5/dynamic-threshold guess with a learned (gainDb, Q) pair -- Q's
//    training label is a genuine measured half-power bandwidth from real
//    audio, not a guess, so the model has learned how wide or narrow a
//    real resonance's correction should be from its actual shape.
//
// 2. Masking-aware cutting: folded into the same resonance model above
//    via two extra input features rather than a separate model --
//    maskingPressureDb (how much OTHER currently-active instances occupy
//    this exact frequency right now, from EqMixRegistry) and
//    harmonicityScore (see below) both directly change the learned gainDb/
//    Q, since both are really answering the same question ("how hard
//    should this specific peak be cut") as the base resonance model.
//
// 3. Distinguishing a problem resonance from a wanted note: harmonicityScore
//    (0-1) measures whether a peak aligns with a harmonic of a stable pitch
//    actually detected in the track during the AI Assist capture -- high
//    harmonicity damps the suggested cut, since a prominent, harmonically-
//    aligned peak is more likely the note being played than a problem.
//
// 4. Instrument-aware target curves: predictCategory() classifies the
//    captured spectrum's likely source type (Vocals/Drums/Bass/Other
//    instruments/Full Mix -- the categories MUSDB18HQ's own stem labels
//    provide), so the spectral-balance heuristic can target a real
//    category-appropriate reference curve instead of just flattening
//    towards the track's own average (a kick drum and a hi-hat have very
//    different natural spectra; "deviation from this track's own average"
//    was a crude one-size-fits-all measure). The reference curves
//    themselves are plain averaged-real-audio data, not a model -- see
//    referenceCurveForCategory() in PluginProcessor.cpp.
//
// 5. Low Cut / High Cut: predictCut() looks at the whole captured
//    spectrum's low/high-end energy and decides whether an actual High
//    Pass (rumble) or Low Pass (hiss/harshness) is warranted -- something
//    the old heuristic never did at all (it only ever suggested gentle
//    shelf boosts/cuts, never a hard cut). Each side is an independent
//    classifier (a track can need both, or neither, or just one) with its
//    own frequency regressor, only meaningful when that side's classifier
//    says yes.
//
// Loading and running any of these can throw (a corrupt/incompatible
// embedded model, a missing or blocked onnxruntime runtime library, an
// execution-provider failure, etc.), so both construction and prediction
// swallow any exception rather than let it propagate: this class is a
// member of MultiModeEQAudioProcessor, constructed unconditionally
// whenever an EQ instance is created -- including reactively, in-process,
// when Mentals Suite loads one into its chain -- so an uncaught exception
// here would silently take down whatever hosts it, not just this one
// feature. isReady()/predict* returning no suggestion just means "AI
// Assist unavailable"; the rest of the plugin works normally either way.
//==============================================================================
class EqAssistModel
{
public:
    struct ResonanceSuggestion
    {
        float gainDb;
        float q;
    };

    struct CutSuggestion
    {
        bool needsLowCut = false;
        float lowCutFreqHz = 20.0f;
        bool needsHighCut = false;
        float highCutFreqHz = 20000.0f;
    };

    EqAssistModel();

    bool isReady() const noexcept
    {
        return resonanceSession != nullptr && lowCutClassifierSession != nullptr && highCutClassifierSession != nullptr
            && lowCutFreqSession != nullptr && highCutFreqSession != nullptr && categorySession != nullptr;
    }

    // features: { log10(freqHz), prominenceDb, peakDb, spectralTiltDb,
    //             overallLevelDb, maskingPressureDb, harmonicityScore } --
    // must match extract_features_v2.py's resonance feature order exactly.
    std::optional<ResonanceSuggestion> predictResonance (const std::array<float, 7>& features);

    // features: { subBassDb, bassDb, lowMidRefDb, highDb, airDb, overallLevelDb,
    //             lowSlopeDb, highSlopeDb } -- must match extract_features.py's
    // cut_features() order exactly.
    std::optional<CutSuggestion> predictCut (const std::array<float, 8>& features);

    // features: { 7 relative macro-band levels, overallLevelDb } -- must
    // match extract_features_v2.py's macro_band_profile() order exactly.
    // Returns an index into { Vocals, Drums, Bass, Other, Mixture } (see
    // PluginProcessor.cpp's categoryNames), or nullopt if unavailable.
    std::optional<int> predictCategory (const std::array<float, 8>& features);

private:
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> resonanceSession;
    std::unique_ptr<Ort::Session> lowCutClassifierSession;
    std::unique_ptr<Ort::Session> highCutClassifierSession;
    std::unique_ptr<Ort::Session> lowCutFreqSession;
    std::unique_ptr<Ort::Session> highCutFreqSession;
    std::unique_ptr<Ort::Session> categorySession;
};
