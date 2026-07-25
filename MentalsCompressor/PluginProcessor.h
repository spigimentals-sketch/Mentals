#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"

//==============================================================================
// Mentals Compressor: a feed-forward soft-knee compressor (see
// MentalsUI::DynamicsDSP) with attack/release, makeup gain, dry/wet mix, and
// an optional external sidechain input for ducking-style use.
//==============================================================================
class MentalsCompressorAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsCompressorAudioProcessor();
    ~MentalsCompressorAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Compressor"; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals Compressor" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Gain-reduction meter (see MentalsUI::GainReductionMeterComponent) --
    // the most negative (i.e. most reduction) value seen during the last
    // processed block.
    float getGainReductionDb() const noexcept { return currentGainReductionDb.load(); }

    //==========================================================================
    // AI Assist: listens to whatever's coming into the detector (the same
    // signal processBlock() itself compresses -- main input, or the
    // sidechain if Use Sidechain is on) for a few seconds, measuring its
    // average level, peak level, and how often transients hit, then maps
    // those to a starting Threshold/Ratio/Attack/Release/Makeup -- a
    // disclosed rule-based heuristic (see applySuggestedCompressorSettings()
    // in the .cpp), not a trained model.
    //==========================================================================
    void beginAiAssistAnalysis();
    bool isAiAssistCapturing() const noexcept { return aiAssistCapturing.load(); }
    bool applySuggestedCompressorSettings();

    // Cached parameter pointers, read directly by the editor's transfer-
    // curve display (which calls MentalsUI::DynamicsDSP::computeOutputDb()
    // itself so the graph can never disagree with what's actually processed).
    juce::AudioParameterFloat* thresholdParam   = nullptr;
    juce::AudioParameterFloat* ratioParam       = nullptr;
    juce::AudioParameterFloat* kneeParam        = nullptr;
    juce::AudioParameterFloat* attackParam      = nullptr;
    juce::AudioParameterFloat* releaseParam     = nullptr;
    juce::AudioParameterFloat* makeupGainParam  = nullptr;
    juce::AudioParameterFloat* mixParam         = nullptr;
    juce::AudioParameterBool*  useSidechainParam = nullptr;

    // When off, the final output is summed to mono (both channels made
    // identical) -- a mono-compatibility check/forcing switch, same control
    // every Mentals plugin now has.
    juce::AudioParameterBool* stereoParam = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void captureAiAssistSample (float levelAbs) noexcept;

    double currentSampleRate = 44100.0;

    MentalsUI::DynamicsDSP::EnvelopeFollower envelopeFollower;

    std::atomic<float> currentGainReductionDb { 0.0f };
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    //==========================================================================
    // AI Assist capture state -- only ever touched on the audio thread while
    // aiAssistCapturing is true, except for the atomics below which the
    // message thread reads for status/results.
    //==========================================================================
    static constexpr float aiAssistCaptureSeconds = 3.0f;
    int aiAssistSamplesRemaining = 0;
    double aiAssistSumSquares = 0.0;
    juce::int64 aiAssistSampleCount = 0;
    float aiAssistPeakLinear = 0.0f;
    int aiAssistTransientCount = 0;

    // Onset/transient detection works on a peak-per-chunk basis (~5ms
    // chunks), not raw per-sample level -- a per-sample fast/slow envelope
    // would mistake the natural rise-and-fall of every single cycle of a
    // sustained tone (its full-wave-rectified |sample| ripples up and down
    // every half-period) for a stream of transients. Holding the peak over
    // a whole chunk before comparing it to a slower-following floor means a
    // steady tone's chunk-to-chunk peak stays flat (no false onsets), while
    // a real transient still shows up as one chunk's peak jumping well
    // above the floor.
    int aiAssistChunkSizeSamples = 1;
    int aiAssistChunkSamplesRemaining = 0;
    float aiAssistChunkPeakLinear = 0.0f;
    float aiAssistFloorLinear = 0.0f;
    float aiAssistFloorFollowCoeff = 0.0f;

    std::atomic<bool> aiAssistCapturing { false };
    std::atomic<bool> aiAssistReady { false };
    std::atomic<float> aiAssistCapturedRmsDb { -100.0f };
    std::atomic<float> aiAssistCapturedPeakDb { -100.0f };
    std::atomic<float> aiAssistCapturedTransientRate { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsCompressorAudioProcessor)
};
