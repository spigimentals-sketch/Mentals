#include "EqAssistModel.h"
#include "EqAssistBinaryData.h"

namespace
{
    std::unique_ptr<Ort::Session> makeSession (Ort::Env& env, const void* data, size_t size)
    {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads (1);
        options.SetGraphOptimizationLevel (ORT_ENABLE_ALL);
        return std::make_unique<Ort::Session> (env, data, size, options);
    }

    // Runs a single-row model and returns its first output as a float
    // (regressors) -- shared by the two frequency regressors.
    float runSingleOutputRegressor (Ort::Session& session, const float* input, size_t numFeatures)
    {
        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 2> inputShape { 1, (int64_t) numFeatures };

        Ort::Value inputTensor = Ort::Value::CreateTensor<float> (
            memoryInfo, const_cast<float*> (input), numFeatures, inputShape.data(), inputShape.size());

        const char* inputNames[]  { "input" };
        const char* outputNames[] { "variable" };

        auto outputs = session.Run (Ort::RunOptions { nullptr }, inputNames, &inputTensor, 1, outputNames, 1);
        return outputs[0].GetTensorData<float>()[0];
    }

    // Runs a single-row "classifier" and returns its predicted label (0 or
    // 1) -- shared by the two Low Cut/High Cut decision models. These are
    // RandomForestRegressors trained directly on 0/1 labels and thresholded
    // here at 0.5, not RandomForestClassifiers -- see Models/README.md for
    // why (a real ONNX export bug in this project's sklearn/skl2onnx
    // version pair, specific to binary RandomForestClassifier).
    bool runThresholdedRegressor (Ort::Session& session, const float* input, size_t numFeatures)
    {
        return runSingleOutputRegressor (session, input, numFeatures) >= 0.5f;
    }
}

EqAssistModel::EqAssistModel()
{
    // See the class comment: any failure here (bad model bytes, a missing
    // or blocked runtime, etc.) must leave this object in a well-defined
    // "not ready" state rather than let the exception escape -- this runs
    // unconditionally from MultiModeEQAudioProcessor's own constructor.
    try
    {
        env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "MentalsMultimodeEqAssist");
        resonanceSession        = makeSession (*env, EqAssistBinaryData::eq_assist_resonance_onnx,         (size_t) EqAssistBinaryData::eq_assist_resonance_onnxSize);
        lowCutClassifierSession = makeSession (*env, EqAssistBinaryData::eq_assist_lowcut_classifier_onnx, (size_t) EqAssistBinaryData::eq_assist_lowcut_classifier_onnxSize);
        highCutClassifierSession= makeSession (*env, EqAssistBinaryData::eq_assist_highcut_classifier_onnx,(size_t) EqAssistBinaryData::eq_assist_highcut_classifier_onnxSize);
        lowCutFreqSession       = makeSession (*env, EqAssistBinaryData::eq_assist_lowcut_freq_onnx,        (size_t) EqAssistBinaryData::eq_assist_lowcut_freq_onnxSize);
        highCutFreqSession      = makeSession (*env, EqAssistBinaryData::eq_assist_highcut_freq_onnx,       (size_t) EqAssistBinaryData::eq_assist_highcut_freq_onnxSize);
        categorySession         = makeSession (*env, EqAssistBinaryData::eq_assist_category_onnx,           (size_t) EqAssistBinaryData::eq_assist_category_onnxSize);
    }
    catch (const std::exception&)
    {
        env.reset();
        resonanceSession.reset();
        lowCutClassifierSession.reset();
        highCutClassifierSession.reset();
        lowCutFreqSession.reset();
        highCutFreqSession.reset();
        categorySession.reset();
    }
}

std::optional<EqAssistModel::ResonanceSuggestion> EqAssistModel::predictResonance (const std::array<float, 7>& features)
{
    if (! isReady())
        return std::nullopt;

    try
    {
        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 2> inputShape { 1, (int64_t) features.size() };

        Ort::Value inputTensor = Ort::Value::CreateTensor<float> (
            memoryInfo, const_cast<float*> (features.data()), features.size(), inputShape.data(), inputShape.size());

        const char* inputNames[]  { "input" };
        const char* outputNames[] { "variable" };

        auto outputs = resonanceSession->Run (Ort::RunOptions { nullptr }, inputNames, &inputTensor, 1, outputNames, 1);
        const float* out = outputs[0].GetTensorData<float>();

        ResonanceSuggestion suggestion {};
        suggestion.gainDb = out[0];
        suggestion.q      = out[1];
        return suggestion;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

std::optional<EqAssistModel::CutSuggestion> EqAssistModel::predictCut (const std::array<float, 8>& features)
{
    if (! isReady())
        return std::nullopt;

    try
    {
        CutSuggestion suggestion;

        suggestion.needsLowCut = runThresholdedRegressor (*lowCutClassifierSession, features.data(), features.size());
        if (suggestion.needsLowCut)
            suggestion.lowCutFreqHz = runSingleOutputRegressor (*lowCutFreqSession, features.data(), features.size());

        suggestion.needsHighCut = runThresholdedRegressor (*highCutClassifierSession, features.data(), features.size());
        if (suggestion.needsHighCut)
            suggestion.highCutFreqHz = runSingleOutputRegressor (*highCutFreqSession, features.data(), features.size());

        return suggestion;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

std::optional<int> EqAssistModel::predictCategory (const std::array<float, 8>& features)
{
    if (! isReady())
        return std::nullopt;

    try
    {
        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 2> inputShape { 1, (int64_t) features.size() };

        Ort::Value inputTensor = Ort::Value::CreateTensor<float> (
            memoryInfo, const_cast<float*> (features.data()), features.size(), inputShape.data(), inputShape.size());

        const char* inputNames[]  { "input" };
        const char* outputNames[] { "label" };

        auto outputs = categorySession->Run (Ort::RunOptions { nullptr }, inputNames, &inputTensor, 1, outputNames, 1);
        return (int) outputs[0].GetTensorData<int64_t>()[0];
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}
