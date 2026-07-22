# AI Assist models

Six trained models behind Mentals Multimode EQ's AI Assist (see
`../EqAssistModel.h` and `../EqMixRegistry.h`):

- `eq_assist_resonance.onnx` -- harsh-frequency cuts, masking-aware and
  harmonic-aware (below)
- `eq_assist_lowcut_classifier.onnx` / `eq_assist_lowcut_freq.onnx` --
  Low Cut (High Pass) decision + frequency
- `eq_assist_highcut_classifier.onnx` / `eq_assist_highcut_freq.onnx` --
  High Cut (Low Pass) decision + frequency
- `eq_assist_category.onnx` -- instrument-category classifier, for
  instrument-aware target curves

The spectral-balance shelf suggestions in `applyAiAssistSuggestions()` are
still a disclosed rule-based heuristic for the actual macro-band deviation
math (see that function's own comment) -- these six models are the
genuinely trained parts, the same kind of upgrade Autotune's AI Assist and
Stereo Shaper's AI Placement already are.

## Training data

Real audio from [MUSDB18HQ](https://doi.org/10.5281/zenodo.3338373) (Rafii
et al., ISMIR 2017), sampled from the Hugging Face mirror
`danjacobellis/musdb18HQ`: 10 shards, 26 complete songs (mixture + vocals/
drums/bass/other stems, decoded and analysed together per song rather than
independently -- see "Masking-aware cutting" below for why). Each stem was
analysed in ~1.86s windows (40 non-overlapping 2048-sample Hann-windowed
FFTs, averaged -- the same capture `updateAiAssistCapture()` performs at
runtime), hopping every 4 seconds.

### Resonance cut (gainDb, Q, masking-aware, harmonic-aware)

For every genuine local-maximum spectral peak found by the same
peak-detection loop `applyAiAssistSuggestions()` already used (local
maxima within 40Hz-16kHz, prominence measured against a +/-~1/3-octave
baseline excluding the peak's immediate neighbours), seven features are
extracted: `log10(freqHz)`, `prominenceDb`, `peakDb`, a local spectral-tilt
measure, the window's full-spectrum mean level, **maskingPressureDb**, and
**harmonicityScore** (the last two are new -- see below). Two labels:

- **gainDb**: `-clamp(damped_cut + masking_extra, 0, 18)`, where
  `damped_cut = clamp(prominence * 0.9, 0, 15) * (1 - harmonicity * 0.85)`
  and `masking_extra = clamp(maskingPressureDb * 0.3, 0, 6)`. Same
  disclosed tradeoff as before (there's no single objective "correct" cut
  amount for a resonance), now also pulling back when the peak is likely a
  wanted note and pushing harder when something else already occupies the
  same frequency.
- **Q**: unchanged -- a genuine measurement (the peak's real half-power
  bandwidth converted to Q), not a formula.

**Masking-aware cutting.** A single track analysed alone can't tell "this
frequency is crowded" from "this frequency is fine" -- that needs to know
what else is playing at the same time. Training mines this from
MUSDB18HQ's real stems: for a candidate peak found in one stem (say,
vocals), the *other* stems present for that song (drums/bass/other) are
read at the exact same time window, and their combined level at that exact
frequency becomes `maskingPressureDb = othersDb - peakDb` (positive when
something else already dominates there). At runtime there's no MUSDB-style
"other stems" available -- instead, `EqMixRegistry` (a cross-process
shared-memory blackboard, the same design as Stereo Shaper's `MixRegistry`)
lets every currently-loaded Multimode EQ instance publish a coarse 24-band
log-spaced energy profile a few times a second, and read every other
instance's published profile on demand. `EqMixRegistry::levelAtFrequency()`
log-interpolates between the two nearest published bands to approximate
what the fine per-bin training-time lookup measured -- necessarily a
coarser live approximation of the training-time computation (24 published
bands vs. exact per-bin occupancy from real simultaneous stems), the same
kind of live-vs-training gap Stereo Shaper's own context aggregation has.
If no other instance is currently detected, masking pressure is just 0 --
the same as running on a single, unaccompanied track.

**Distinguishing a problem resonance from a wanted note.** The resonance
detector previously had no idea whether a prominent peak was an unwanted
room mode/harshness or just the fundamental of the bass note or vocal
pitch actually being sung -- a strong, sustained musical note is also, by
definition, a peak that stands out from its surroundings. `harmonicityScore`
(0-1) fixes this: during the AI Assist capture, a pitch detector runs on
each frame (this plugin's own copy of the same octave-error-resistant
autocorrelation algorithm as `MentalsAutotune/PitchDSP.h`'s `detectPitch()`
-- see `PluginProcessor.cpp`'s `detectPitchForHarmonicity()`), and if a
stable pitch is found across enough frames, each candidate peak's distance
(in cents) from the nearest harmonic of that pitch, weighted by how stable
the pitch was, becomes its harmonicity score. High harmonicity damps the
suggested cut in the gainDb formula above. At training time the "stable
pitch" comes from running the same detector on the real stem's own audio
in the same window; at runtime it comes from `MultiModeEQAudioProcessor`'s
own capture (`aiAssistPitchFreqHz`/`aiAssistPitchStability`).

18,769 examples, an 80/20 train/test split. `RandomForestRegressor` (80
trees, depth 8), 7 features in, 2 outputs (gainDb, Q). Held-out MAE: gainDb
0.123dB, Q 0.82. Feature importance: prominenceDb 0.69, log10(freq) 0.28,
harmonicityScore 0.013, maskingPressureDb 0.003, everything else <0.01 --
prominence still dominates (gainDb's base label is directly a function of
it), with harmonicity and masking pressure picked up as smaller but real
modulating factors, exactly as intended.

### Low Cut / High Cut

Unchanged from the previous training round. Real, professionally mastered
MUSDB18HQ tracks mostly don't have the kind of pathological mic rumble or
hiss/hum a Low Cut or High Cut exists to fix, so positive examples are
built by synthetically injecting shaped noise into real spectra at
randomised levels/cutoffs (disclosed in full detail in this file's git
history / the original training round). 8 global features per window.

- **needsLowCut / needsHighCut**: `RandomForestRegressor` trained directly
  on 0/1 labels, thresholded at 0.5 in C++ -- *not* `RandomForestClassifier`,
  because this project's sklearn/skl2onnx version pair (1.9.0/1.20.0)
  produces a broken ONNX export specifically for a binary
  `RandomForestClassifier` (confirmed with a synthetic reproduction: the
  exported model's probabilities came back negative/inverted and its label
  output predicted the same class for every row, while a 5-class
  multiclass classifier -- see the category classifier below -- exported
  and validated correctly). Held-out accuracy/F1: needsLowCut 83.4% / 0.80,
  needsHighCut 99.6% / 0.995.
- **lowCutFreqHz / highCutFreqHz**: `RandomForestRegressor`, trained only
  on each side's positive examples. Held-out MAE: lowCutFreqHz 25.4Hz,
  highCutFreqHz 306.5Hz.

### Instrument-category classifier (for instrument-aware target curves)

The spectral-balance heuristic used to flatten every track toward *its
own* average level -- a crude one-size-fits-all target, since a kick drum
and a hi-hat have very different natural spectra. `eq_assist_category.onnx`
classifies the captured spectrum into one of MUSDB18HQ's own stem labels
-- Vocals, Drums, Bass, Other (instruments), or Mixture (full mix) --
using 8 features (7 relative macro-band levels + overall level, the same
macro bands the spectral-balance heuristic already uses). Each category's
*reference curve* (baked into `PluginProcessor.cpp` as
`categoryReferenceCurves`, not a model -- just the average real-audio
macro-band shape for that category) then becomes the spectral-balance
heuristic's target instead of a flat 0: `deviation = ownRelativeDb -
referenceCurve[band]`. If the classifier is unavailable, `referenceCurve`
is `nullptr` and the target falls back to 0 -- the exact previous
behaviour.

5,824 examples (729 vocals / 934 drums / 1,255 bass / 1,400 other / 1,506
mixture). `RandomForestClassifier` (80 trees, depth 8) -- a genuine
multiclass classifier, unlike the binary Low Cut/High Cut decisions above,
exported and validated with no issues. Held-out accuracy 92.4%, macro F1
0.921.

All six exported via `skl2onnx` (opset 15) and validated to match the
scikit-learn models' own predictions before being committed here.

## Retraining

The training scripts aren't part of this repo (throwaway tooling, not
shipped code). To retrain: download a set of complete MUSDB18HQ songs
(mixture + all 4 stems, not independent stem sampling, since masking
pressure needs real simultaneous stems) from `danjacobellis/musdb18HQ` on
Hugging Face; for each stem's candidate resonance peaks, extract the
original 5 features plus masking pressure (from the other real stems in
the same window) and harmonicity (from a ported `PitchDSP::detectPitch()`
run on the same stem's own audio); for the category classifier, use each
stem's `instrument` label directly; for the reference curves, average each
category's macro-band profile across all its examples; for Low Cut/High
Cut, keep the synthetic rumble/hiss augmentation from the original round.
Fit and export all six via `skl2onnx` -- using `RandomForestRegressor`
thresholded at 0.5 rather than `RandomForestClassifier` for the two cut
decisions (see above), and a genuine `RandomForestClassifier` for the
5-way category model.
