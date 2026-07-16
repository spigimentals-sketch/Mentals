# AI Assist models

`ai_assist_regressor.onnx` and `ai_assist_classifier.onnx` are trained models
behind Mentals Autotune's AI Assist feature (see `../AiAssistModel.h`).

## Training data

Real singing audio from [VocalSet](https://doi.org/10.5281/zenodo.1203819)
(Wilkins & Seetharaman, ISMIR 2018, CC-BY 4.0), sampled from the Hugging
Face mirror `Bill13579/vocalset-mirror`. Pitch was detected with a Python
port of `PitchDSP::detectPitch()` (same normalised-autocorrelation +
parabolic interpolation math, vectorised via FFT for speed), run over the
same 100-voiced-hop capture window `applySuggestedVocalSettings()` uses.

Each 100-hop capture produces three features -- `avgAbsDelta`, `pitchRange`,
`stdDevSemitone` -- and labels generated from what was originally this
feature's hand-written heuristic formula, applied to those same real,
extracted features. Real audio makes the *feature* distributions (how
these three numbers actually vary together for real singing) realistic;
the *labels* still trace back to the same subjective judgement calls the
heuristic encoded, since there's no objective "correct" Retune Speed/Amount
for a given voice -- there was no way around that, and the model was
trained this way with that tradeoff made explicit.

## Models

- Regressor: `RandomForestRegressor` (60 trees, depth 6), 3 features in,
  2 outputs (`retuneMs`, `amount`).
- Classifier: `RandomForestClassifier` (60 trees, depth 6), 3 features in,
  4-way style label out.

Both exported via `skl2onnx` (opset 15) and validated to match the
scikit-learn models' own predictions before being committed here.

## Retraining

The training scripts aren't part of this repo (they're throwaway tooling,
not shipped code). To retrain: extract the same three features from a
VocalSet sample using a script that ports `PitchDSP::detectPitch()` and
`applySuggestedVocalSettings()`'s capture-window math to Python, generate
labels via the same heuristic formula, then fit and export
`RandomForestRegressor`/`RandomForestClassifier` via `skl2onnx`.
