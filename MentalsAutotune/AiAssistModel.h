#pragma once

#include <onnxruntime_cxx_api.h>

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

    Suggestion predict (float avgAbsDelta, float pitchRange, float stdDevSemitone);

private:
    Ort::Env env;
    Ort::Session regressorSession;
    Ort::Session classifierSession;
};
