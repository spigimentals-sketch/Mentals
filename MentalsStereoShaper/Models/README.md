# AI Placement models

`mix_placement_rotation.onnx` and `mix_placement_width.onnx` are trained
models behind Mentals 360 Stereo Shaper's AI Placement feature (see
`../PlacementModel.h`).

## Training data

Real multitrack songs from [MUSDB18HQ](https://doi.org/10.5281/zenodo.3338373)
(Rafii et al., ISMIR 2017), sampled from the Hugging Face mirror
`danjacobellis/musdb18HQ`. Each song provides four isolated stems (vocals,
drums, bass, other) plus the real, professionally-mixed final "mixture".

Each song was analysed in non-overlapping 8-second windows. For each
(stem, window) pair with the stem not silent:

- **Own features** (6 numbers): band-energy ratios (150Hz/4000Hz split),
  crest factor, RMS, and L/R correlation of that stem alone -- the same six
  numbers `MixRegistry::publish()` computes from live audio at runtime.
- **Context** (7 numbers): an energy-weighted left/centre/right occupancy
  histogram plus average band-energy ratios, aggregated from the *other*
  three stems in that same window -- mirroring exactly what
  `MixRegistry::computeContext()` aggregates from other live plugin
  instances.
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
  training, where it ends up almost entirely explained by the stem's own
  input correlation rather than the mix context. Treat AI Placement's
  Rotation suggestion as the primary, well-supported output and its Width
  suggestion as a rougher secondary one.

## Models

Two separate `RandomForestRegressor` models (80 trees, depth 8), 13
features in, single continuous output each (rotation in degrees, width in
percent) -- kept separate rather than one multi-output regressor since a
joint model's feature-importance numbers blend the two outputs together,
which made it hard to tell whether Rotation's suggestion was really using
the mix context or just riding on Width's much stronger single-feature
relationship.

Both exported via `skl2onnx` (opset 15) and validated to match the
scikit-learn models' own predictions before being committed here.

## Retraining

The training scripts aren't part of this repo (throwaway tooling, not
shipped code). To retrain: download a set of MUSDB18HQ shards from
`danjacobellis/musdb18HQ` on Hugging Face (parquet files, each row one
stem file with `path`/`instrument`/`audio` columns), extract the
own/context features and rotation/width labels as described above for
every (stem, 8-second window) pair across enough complete songs, then fit
and export two `RandomForestRegressor`s via `skl2onnx`.
