#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include "../MentalsAutotune/PitchDSP.h" // reusing the granular pitch shifter unmodified -- see class comment
#include <array>
#include <vector>

//==============================================================================
// Mentals Reverb: an algorithmic reverb built on juce::dsp::Reverb (a
// well-tested Freeverb-derived comb/allpass network) rather than a
// hand-rolled DSP algorithm -- reimplementing that from scratch would just
// be re-solving an already-solved problem. Adds a pre-delay stage (which
// juce::dsp::Reverb doesn't offer on its own) ahead of it, plus the usual
// room size/damping/width/mix/freeze controls.
//
// Shimmer: juce::dsp::Reverb is run internally at 100% wet (dry/wet mixing
// is done externally instead, see processBlock()) so there's a clean wet
// signal to tap. Each block, that wet output is pitch-shifted up an octave
// (PitchDSP::PitchShifterChannel, the same granular shifter Mentals
// Autotune and Vox Choir use) and fed into a short delay line; next block,
// that delayed, shifted signal is summed back into the reverb's own input
// alongside the dry pre-delayed signal, so it re-reverberates and
// re-shifts, building the cascading, ascending texture shimmer reverbs are
// known for. The one-block-old feedback timing (rather than a same-block
// tap) is the same causal, lookahead-free tradeoff Mentals 360 Stereo
// Shaper's Phase Align safety net makes -- imperceptible against a reverb
// tail lasting hundreds of milliseconds to seconds, and it avoids a
// same-sample feedback loop through juce::dsp::Reverb entirely.
//==============================================================================
class MentalsReverbAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsReverbAudioProcessor();
    ~MentalsReverbAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Reverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; } // long room sizes decay slowly

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    MentalsUI::PresetManager presetManager { apvts, "Mentals Reverb" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Cached parameter pointers, read directly by the editor's decay-envelope
    // display (a parametric visualisation of the configured tail, not a live
    // audio capture -- see PluginEditor's class comment for why).
    juce::AudioParameterFloat* roomSizeParam   = nullptr;
    juce::AudioParameterFloat* dampingParam    = nullptr;
    juce::AudioParameterFloat* widthParam      = nullptr;
    juce::AudioParameterFloat* mixParam        = nullptr;
    juce::AudioParameterFloat* preDelayMsParam   = nullptr;
    juce::AudioParameterBool*  freezeParam       = nullptr;
    juce::AudioParameterFloat* shimmerAmountParam = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void seedFactoryPresetsIfMissing();

    double currentSampleRate = 44100.0;

    juce::dsp::Reverb reverb;

    // Simple integer-sample circular buffer per channel, applied before the
    // signal reaches the reverb -- separates the dry attack from the wet
    // onset, a standard reverb feature juce::dsp::Reverb doesn't provide.
    std::array<std::vector<float>, 2> preDelayBuffers;
    std::array<int, 2> preDelayWritePos { 0, 0 };

    // Shimmer's octave-up feedback path -- see class comment.
    std::array<PitchDSP::PitchShifterChannel, 2> shimmerPitchShifters;
    std::array<std::vector<float>, 2> shimmerFeedbackBuffers;
    std::array<int, 2> shimmerFeedbackWritePos { 0, 0 };
    juce::AudioBuffer<float> dryCopyBuffer; // holds the pre-reverb (dry) signal for the external mix

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsReverbAudioProcessor)
};
