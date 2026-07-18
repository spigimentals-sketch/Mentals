#include "PlacementModel.h"
#include "PlacementBinaryData.h"
#include <array>

namespace
{
    std::unique_ptr<Ort::Session> makeSession (Ort::Env& env, const void* data, size_t size)
    {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads (1);
        options.SetGraphOptimizationLevel (ORT_ENABLE_ALL);
        return std::make_unique<Ort::Session> (env, data, size, options);
    }

    float runSingleOutputRegressor (Ort::Session& session, const std::array<float, 13>& input)
    {
        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 2> inputShape { 1, (int64_t) input.size() };

        Ort::Value inputTensor = Ort::Value::CreateTensor<float> (
            memoryInfo, const_cast<float*> (input.data()), input.size(), inputShape.data(), inputShape.size());

        const char* inputNames[]  { "X" };
        const char* outputNames[] { "variable" };

        auto outputs = session.Run (Ort::RunOptions { nullptr }, inputNames, &inputTensor, 1, outputNames, 1);
        return outputs[0].GetTensorData<float>()[0];
    }
}

PlacementModel::PlacementModel()
{
    // See the class comment: any failure here (bad model bytes, a missing
    // or blocked runtime, etc.) must leave this object in a well-defined
    // "not ready" state rather than let the exception escape -- this runs
    // unconditionally from MentalsStereoShaperAudioProcessor's own
    // constructor.
    try
    {
        env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "MentalsStereoShaperPlacement");
        rotationSession = makeSession (*env, PlacementBinaryData::mix_placement_rotation_onnx, (size_t) PlacementBinaryData::mix_placement_rotation_onnxSize);
        widthSession    = makeSession (*env, PlacementBinaryData::mix_placement_width_onnx,    (size_t) PlacementBinaryData::mix_placement_width_onnxSize);
    }
    catch (const std::exception&)
    {
        env.reset();
        rotationSession.reset();
        widthSession.reset();
    }
}

std::optional<PlacementModel::Suggestion> PlacementModel::predict (const std::array<float, MixRegistry::numOwnFeatures>& ownFeatures,
                                                                    const MixRegistry::AggregateContext& context)
{
    if (! isReady())
        return std::nullopt;

    try
    {
        // Must match FEATURE_COLUMNS' order in extract_features.py/train_model.py exactly.
        std::array<float, 13> input {
            ownFeatures[0], ownFeatures[1], ownFeatures[2], ownFeatures[3], ownFeatures[4], ownFeatures[5],
            context.leftOccupancy, context.centreOccupancy, context.rightOccupancy,
            context.othersAvgLowRatio, context.othersAvgMidRatio, context.othersAvgHighRatio,
            (float) context.othersCount,
        };

        Suggestion suggestion {};
        suggestion.rotationDeg  = runSingleOutputRegressor (*rotationSession, input);
        suggestion.widthPercent = runSingleOutputRegressor (*widthSession, input);
        return suggestion;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}
