#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include "PitchDSP.h"
#include <array>
#include <vector>

//==============================================================================
// Mentals Autotune: monophonic pitch correction. See PitchDSP.h for the
// detection/shifting approach and its honestly-disclosed limitations
// (no formant correction, granular rather than PSOLA/phase-vocoder shifting).
//==============================================================================
class MentalsAutotuneAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsAutotuneAudioProcessor();
    ~MentalsAutotuneAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    MentalsUI::PresetManager presetManager { apvts, "Mentals Autotune" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Polled by the editor's pitch-history display -- all updated once per
    // detection cycle (~every 20ish ms), not per sample.
    float getDetectedFrequencyHz() const noexcept { return lastDetectedFreqHz.load(); }
    float getTargetFrequencyHz() const noexcept { return lastTargetFreqHz.load(); }
    bool isVoiced() const noexcept { return lastIsVoiced.load(); }

    juce::AudioParameterChoice* keyParam         = nullptr;
    juce::AudioParameterChoice* scaleParam       = nullptr;
    juce::AudioParameterFloat*  retuneSpeedParam = nullptr;
    juce::AudioParameterFloat*  amountParam      = nullptr;
    juce::AudioParameterFloat*  mixParam         = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void runPitchDetectionAndUpdateTarget();

    double currentSampleRate = 44100.0;

    static constexpr float minDetectableFreqHz = 70.0f;
    static constexpr float maxDetectableFreqHz = 1200.0f;
    static constexpr float voicedConfidenceThreshold = 0.45f;
    static constexpr int maxSupportedChannels = 8;

    int windowSizeSamples = 2048;
    int hopSizeSamples    = 1024;

    // Circular history of mono-summed input, always holding the most recent
    // windowSizeSamples samples; unwrapped into analysisWorkspace (windowed)
    // once per hop for the actual autocorrelation pass.
    std::vector<float> analysisRingBuffer;
    int analysisWritePos = 0;
    int samplesUntilNextHop = 0;
    std::vector<float> analysisWorkspace;

    float targetRatio    = 1.0f; // updated once per detection cycle
    float smoothedRatio  = 1.0f; // glides towards targetRatio every sample, at Retune Speed's rate

    std::array<PitchDSP::PitchShifterChannel, maxSupportedChannels> pitchShifters;

    std::atomic<float> lastDetectedFreqHz { 0.0f };
    std::atomic<float> lastTargetFreqHz   { 0.0f };
    std::atomic<bool>  lastIsVoiced       { false };

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsAutotuneAudioProcessor)
};
