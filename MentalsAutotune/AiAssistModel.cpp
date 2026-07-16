#include "AiAssistModel.h"
#include "AiAssistBinaryData.h"
#include <array>

namespace
{
    Ort::Session makeSession (Ort::Env& env, const void* data, size_t size)
    {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads (1);
        options.SetGraphOptimizationLevel (ORT_ENABLE_ALL);
        return Ort::Session (env, data, size, options);
    }
}

AiAssistModel::AiAssistModel()
    : env (ORT_LOGGING_LEVEL_WARNING, "MentalsAutotuneAiAssist"),
      regressorSession (makeSession (env, AiAssistBinaryData::ai_assist_regressor_onnx, (size_t) AiAssistBinaryData::ai_assist_regressor_onnxSize)),
      classifierSession (makeSession (env, AiAssistBinaryData::ai_assist_classifier_onnx, (size_t) AiAssistBinaryData::ai_assist_classifier_onnxSize))
{
}

AiAssistModel::Suggestion AiAssistModel::predict (float avgAbsDelta, float pitchRange, float stdDevSemitone)
{
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

    std::array<float, 3> inputValues { avgAbsDelta, pitchRange, stdDevSemitone };
    std::array<int64_t, 2> inputShape { 1, 3 };

    const char* inputNames[]  { "input" };

    // Regressor: single "variable" output, 2 columns (retuneMs, amount) for
    // this one input row -- read its actual runtime shape rather than
    // assuming, since the model's own declared metadata undercounts it.
    {
        Ort::Value inputTensor = Ort::Value::CreateTensor<float> (
            memoryInfo, inputValues.data(), inputValues.size(), inputShape.data(), inputShape.size());
        const char* outputNames[] { "variable" };

        auto outputs = regressorSession.Run (Ort::RunOptions { nullptr }, inputNames, &inputTensor, 1, outputNames, 1);
        const float* out = outputs[0].GetTensorData<float>();

        Suggestion suggestion {};
        suggestion.retuneMs = out[0];
        suggestion.amount   = out[1];

        // Classifier: "label" output is a single int64 predicted class.
        Ort::Value classifierInput = Ort::Value::CreateTensor<float> (
            memoryInfo, inputValues.data(), inputValues.size(), inputShape.data(), inputShape.size());
        const char* classifierOutputNames[] { "label" };
        auto classifierOutputs = classifierSession.Run (Ort::RunOptions { nullptr }, inputNames, &classifierInput, 1, classifierOutputNames, 1);
        const int64_t* label = classifierOutputs[0].GetTensorData<int64_t>();
        suggestion.labelIndex = (int) label[0];

        return suggestion;
    }
}
