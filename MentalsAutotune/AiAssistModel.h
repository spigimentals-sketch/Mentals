#pragma once

#include <onnxruntime_cxx_api.h>
#include <optional>
#include <memory>

//==============================================================================
// Wraps the two small ONNX models behind AI Assist: a regressor predicting
// [retuneMs, amount] and a classifier predicting the 4-way style label
// (see PluginProcessor.cpp's old applySuggestedVocalSettings() comment for
// what those four labels are). Both are trained on real VocalSet singing
// audio (Wilkins & Seetharaman, ISMIR 2018), with training labels generated
// from that same original heuristic formula applied to real extracted
// pitch-movement features -- so the model has learned data-driven decision
// boundaries and a data-driven speed/amount curve fitted to how real voices
// actually move, rather than the four hand-picked thresholds and linear
// map the heuristic used. See Models/README.md for the training pipeline.
//
// Runtime input is the same three numbers the original heuristic computed
// from a capture: avgAbsDelta (mean semitone-to-semitone movement),
// pitchRange (max-min semitone), and stdDevSemitone (spread around the
// mean, the same measure computeStabilityScore() already uses elsewhere).
// No new DSP capture is needed -- applySuggestedVocalSettings() already
// has the semitone buffer these are computed from.
//
// Loading and running the model can both throw (a corrupt/incompatible
// embedded model, a missing or blocked onnxruntime runtime library, an
// execution-provider failure, etc. -- exactly the kind of thing that can
// differ across machines and can't be ruled out here), so both the
// constructor and predict() swallow any exception rather than let it
// propagate: this class is a member of MentalsAutotuneAudioProcessor,
// constructed unconditionally whenever an Autotune instance is created --
// including reactively, in-process, when Mentals Suite loads one into its
// chain -- so an uncaught exception here would silently take down
// whatever hosts it, not just this one feature. isReady()/predict()
// returning no suggestion just means "AI Assist unavailable"; the rest of
// the plugin works normally either way.
//==============================================================================
class AiAssistModel
{
public:
    struct Suggestion
    {
        float retuneMs;
        float amount;
        int labelIndex;
    };

    AiAssistModel();

    bool isReady() const noexcept { return regressorSession != nullptr && classifierSession != nullptr; }

    std::optional<Suggestion> predict (float avgAbsDelta, float pitchRange, float stdDevSemitone);

private:
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> regressorSession;
    std::unique_ptr<Ort::Session> classifierSession;
};
