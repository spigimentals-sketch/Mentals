# AI Placement models

`mix_placement_rotation.onnx` and `mix_placement_width.onnx` are trained
models behind Mentals 360 Stereo Shaper's AI Placement feature (see
`../PlacementModel.h`).

## Training data

Real multitrack songs from [MUSDB18HQ](https://doi.org/10.5281/zenodo.3338373)
(Rafii et al., ISMIR 2017), sampled from the Hugging Face mirror
`danjacobellis/musdb18HQ`: 26 complete songs (mixture + all available real
stems), non-overlapping 8-second windows, 2,808 (stem, window) examples.
Each song provides four isolated stems (vocals, drums, bass, other) plus
the real, professionally-mixed final "mixture".

For each (stem, window) pair with the stem not silent:

- **Own features** (6 numbers): band-energy ratios (150Hz/4000Hz split),
  crest factor, RMS, and L/R correlation of that stem alone -- the same six
  numbers `MixRegistry::publish()` computes from live audio at runtime.
- **Context** (7 numbers): an energy-weighted left/centre/right occupancy
  histogram plus average band-energy ratios, aggregated from the *other*
  real stems in that same window (using each one's own recovered rotation
  label to decide which occupancy bucket it falls into) -- mirroring
  exactly what `MixRegistry::computeContext()` aggregates from other live
  plugin instances.
- **Rotation label**: the stem's real placement as actually used in the
  professional mixture, recovered by projecting the stem's mono content
  onto the mixture's L and R channels (a standard matched-filter/oracle-gain
  technique) and inverting `MentalsStereoShaperAudioProcessor::processBlock()`'s
  rotation matrix. This is a genuine, directly-measured quantity -- the
  model has learned actual placement decisions made by professional mixing
  engineers on real songs, not a hand-written heuristic.
- **Width label**: a proxy derived from the stem's own inherent mid/side
  energy ratio. Unlike rotation, isolating "how much width the mixing
  engineer added" from "how much width the source already had" isn't
  cleanly recoverable from stems + final mixture alone, so this output is
  weaker evidence than rotation -- confirmed by feature importance during
  training, where it ends up almost entirely explained (99.1%) by the
  stem's own input correlation rather than the mix context. Treat AI
  Placement's Rotation suggestion as the primary, well-supported output
  and its Width suggestion as a rougher secondary one.

## Known limitation: left/right occupancy, and how it's handled

This was retrained (previously on an undisclosed, likely smaller sample)
specifically to check a user report that "the AI seems not to work well."
Direct ONNX probing of the *previously shipped* model found its rotation
output completely unchanged across leftOccupancy/rightOccupancy's entire
0-4 range -- bit-identical predictions regardless of which side (or
neither) was "crowded." Retraining with 26 real songs (2,808 examples, up
from whatever the original count was) did not fix this:
`RandomForestRegressor.feature_importances_` for leftOccupancy and
rightOccupancy came back at *exactly* 0.0, and the raw Pearson correlation
between occupancy imbalance (`rightOccupancy - leftOccupancy`) and the real
placement label across all 2,808 examples was -0.009 -- essentially zero.

This isn't a bug or a data-volume problem: 90.3% of real stem placements in
this dataset sit within +/-10 degrees of dead centre (kick, bass, and lead
vocals are placed centre by near-universal convention, not by real-time
spatial negotiation with whatever else is playing), and the actual
left-vs-right creative choice for the remaining, genuinely-panned stems
appears to be driven by instrument role and arrangement convention rather
than a measurable "avoid where others already are" pattern a simple
aggregate occupancy feature can capture. The model *does* use centre
occupancy meaningfully (17.4% feature importance -- "don't add another
centred sound when centre is already crowded" is a learnable, real
pattern); it's specifically the left/right direction that isn't.

Given that, `MentalsStereoShaperAudioProcessor::runAiPlacement()` applies
an explicit, deterministic nudge on top of the model's own rotation
suggestion: `nudge = clamp(-(rightOccupancy - leftOccupancy) * 12, -45, 45)`
degrees, zero when nothing else is detected. This is a plain rule, not
something squeezed out of the training data -- disclosed here the same way
every other tradeoff in this project is, so "mix-aware" placement actually
reflects the mix rather than silently not using the one feature it's named
for.

## Models

Two separate `RandomForestRegressor` models (80 trees, depth 8), 13
features in, single continuous output each (rotation in degrees, width in
percent) -- kept separate rather than one multi-output regressor since a
joint model's feature-importance numbers blend the two outputs together,
which made it hard to tell whether Rotation's suggestion was really using
the mix context or just riding on Width's much stronger single-feature
relationship. Held-out MAE: rotation 4.67 degrees, width 0.90 percent.

Both exported via `skl2onnx` (opset 15) and validated to match the
scikit-learn models' own predictions before being committed here.

## Retraining

The training scripts aren't part of this repo (throwaway tooling, not
shipped code). To retrain: download a set of complete MUSDB18HQ songs
(mixture + all available real stems, not independent per-stem sampling,
since context aggregation needs every other real stem's own recovered
rotation label in the same window) from `danjacobellis/musdb18HQ` on
Hugging Face, extract the own/context features and rotation/width labels
as described above for every (stem, 8-second window) pair across enough
complete songs, then fit and export two `RandomForestRegressor`s via
`skl2onnx`. **Before shipping a retrain**, explicitly check occupancy
sensitivity the way this round did (sweep leftOccupancy/rightOccupancy
across their range at fixed other features and confirm the prediction
actually moves, and/or check `feature_importances_` directly) rather than
just trusting held-out MAE -- a model can look perfectly good on MAE while
still silently ignoring the specific feature the whole point of "mix-
aware" placement depends on.
