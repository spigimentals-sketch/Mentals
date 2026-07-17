#pragma once

#include <onnxruntime_cxx_api.h>
#include "MixRegistry.h"

//==============================================================================
// Wraps the trained ONNX model behind Stereo Shaper's "AI Placement" --
// unlike the plugin's older, rule-based "Mix Analysis" heuristic, this one
// is a RandomForestRegressor trained on real MUSDB18HQ multitrack songs:
// for each stem in each song, its real placement (rotation angle) was
// reverse-engineered from the actual, professionally-mixed "mixture" file
// by projecting the stem's mono content onto the mixture's L/R channels
// (a standard matched-filter/oracle-gain technique), so the model has
// learned data-driven placement decisions from real mixing engineers rather
// than a hand-written rule. Width is a proxy derived from each stem's own
// inherent mid/side energy ratio -- separating "how much width the engineer
// added" from "how much width the source already had" isn't cleanly
// recoverable from stems + final mixture alone, so that output is weaker
// evidence than rotation. See Models/README.md for the full pipeline.
//
// Runtime input is 13 numbers: this track's own 6-feature audio fingerprint
// (see MixRegistry::FeatureIndex) plus the 7-number aggregate context
// MixRegistry::computeContext() reports about every OTHER Stereo Shaper
// instance currently active anywhere on the machine -- the same numbers
// the Python training script computed from a song's other real stems, so
// the live input distribution matches what the model was trained on.
//==============================================================================
class PlacementModel
{
public:
    struct Suggestion
    {
        float rotationDeg;
        float widthPercent;
    };

    PlacementModel();

    Suggestion predict (const std::array<float, MixRegistry::numOwnFeatures>& ownFeatures,
                         const MixRegistry::AggregateContext& context);

private:
    Ort::Env env;
    Ort::Session rotationSession;
    Ort::Session widthSession;
};
