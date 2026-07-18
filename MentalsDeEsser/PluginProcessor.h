#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <vector>

//==============================================================================
// Mentals De-esser: a split-band de-esser. The input is split into a Low
// band (below Frequency, passed through untouched) and a High band (above
// Frequency, the sibilance range), using a one-pole low-pass and its
// complementary high-pass (input - lowpassed input, which reconstructs
// exactly when summed -- the same trick Delay's feedback filtering and
// Saturator's Tone control use). The High band is fed through the same
// soft-knee compressor core as Mentals Compressor (see
// MentalsUI::DynamicsDSP), reacting to its OWN envelope -- so it only ducks
// when sibilance is actually loud, not just because the overall mix is loud
// -- then the two bands are recombined.
//==============================================================================
class MentalsDeEsserAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsDeEsserAudioProcessor();
    ~MentalsDeEsserAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals De-esser"; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals De-esser" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Gain-reduction meter (see MentalsUI::GainReductionMeterComponent) --
    // the most negative value seen in the high band during the last block.
    float getGainReductionDb() const noexcept { return currentGainReductionDb.load(); }

    // Fixed rather than exposed as a control -- De-esser already has 8
    // user-facing controls; a soft knee smooths the onset without needing
    // its own knob. Public so the editor's transfer-curve display uses the
    // exact same value processBlock() does.
    static constexpr float kneeDb = 6.0f;

    // Cached parameter pointers, read directly by the editor's transfer-
    // curve display (which calls MentalsUI::DynamicsDSP::computeOutputDb()
    // itself so the graph can never disagree with what's actually processed).
    juce::AudioParameterFloat* frequencyParam    = nullptr;
    juce::AudioParameterFloat* thresholdParam    = nullptr;
    juce::AudioParameterFloat* ratioParam        = nullptr;
    juce::AudioParameterFloat* attackParam       = nullptr;
    juce::AudioParameterFloat* releaseParam      = nullptr;
    juce::AudioParameterFloat* maxReductionParam = nullptr;
    juce::AudioParameterFloat* mixParam          = nullptr;
    juce::AudioParameterBool*  listenParam       = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;

    // One-pole low-pass state per channel, used to derive the Low/High split.
    std::vector<float> splitFilterStates;

    MentalsUI::DynamicsDSP::EnvelopeFollower envelopeFollower;

    std::atomic<float> currentGainReductionDb { 0.0f };
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsDeEsserAudioProcessor)
};
