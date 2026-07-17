#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>

//==============================================================================
// Mentals Exciter EQ: a harmonic exciter (adds gentle, musical high-frequency
// harmonics the way a classic "aural exciter" does) plus a high-shelf "Air"
// EQ band, combined into one plugin since they're both reached for to make a
// source (often vocals) sound brighter and more present without just turning
// up a treble EQ, which tends to sound harsh/thin rather than airy.
//
// Signal flow per sample:
//   Input -> highpassed at Frequency (isolating the band to excite) -> soft
//   (tanh) saturation scaled by Drive, generating new harmonic content ->
//   a fixed gentle lowpass smooths the freshly-generated harmonics so they
//   read as silky "air" rather than harsh fizz (this smoothing is baked in,
//   not a separate knob -- see class comment on why) -> that excited band is
//   added back on top of (not instead of) the original signal -> a
//   high-shelf EQ ("Air", centred at a fixed 12kHz) applied to the combined
//   signal -> dry/wet Mix.
//==============================================================================
class MentalsExciterEQAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsExciterEQAudioProcessor();
    ~MentalsExciterEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Exciter EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.02; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    MentalsUI::PresetManager presetManager { apvts, "Mentals Exciter EQ" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    juce::AudioParameterFloat* frequencyParam = nullptr;
    juce::AudioParameterFloat* driveParam     = nullptr;
    juce::AudioParameterFloat* airGainParam   = nullptr;
    juce::AudioParameterFloat* mixParam       = nullptr;

    static constexpr float airShelfFreq = 12000.0f; // fixed -- see class comment

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void seedFactoryPresetsIfMissing();

    double currentSampleRate = 44100.0;

    struct ChannelState
    {
        juce::dsp::IIR::Filter<float> exciterHighpass;
        juce::dsp::IIR::Filter<float> smoothingLowpass; // fixed, gentle -- tames freshly-generated harmonics
        juce::dsp::IIR::Filter<float> airShelf;

        void reset()
        {
            exciterHighpass.reset();
            smoothingLowpass.reset();
            airShelf.reset();
        }
    };

    std::array<ChannelState, 2> channels;
    float lastExciterFreq = -1.0f;
    float lastAirGainDb = 1.0e9f; // force a recompute on first block

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsExciterEQAudioProcessor)
};
