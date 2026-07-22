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
//
// Low Cut/High Cut shape the WET signal only (reverb tail + shimmer +
// early reflections, applied after Modulation but before Ducking/the
// external dry/wet mix -- see processBlock()), leaving the dry signal and
// the shimmer feedback loop's own internal tone untouched. This is
// deliberately separate from Damping: Damping shapes juce::dsp::Reverb's
// own feedback path (how the tail darkens as it decays, a dynamic/
// spectral-over-time effect), while Low Cut/High Cut are a static tone
// control on the tail as a whole -- e.g. removing boomy low end a big room
// accumulates, or dulling a bright plate's harshness, independent of how
// quickly it decays.
//
// Early Reflections: a small fixed tapped-delay pattern (see
// earlyTapDelaysMs/earlyTapGains in PluginProcessor.cpp) read from the dry,
// pre-delayed signal (dryCopyBuffer) and added directly to the wet buffer
// AFTER reverb.process() rather than fed back into it -- real early
// reflections are discrete, direction-carrying slap-back, and feeding them
// through the same diffuse algorithm would just blur them into more tail
// rather than keeping the distinct "room slap" that gives a space its
// sense of size ahead of the wash.
//
// Modulation: a short (~12ms) per-channel delay line on the wet signal
// whose length is wobbled by a slow LFO (Mod Rate), phase-offset 90
// degrees between channels for stereo movement -- the subtle pitch/comb
// drift real physical spaces have from mechanical and acoustic
// instability, which a static digital algorithm's tail otherwise lacks
// (most noticeable as a faint metallic/glassy quality on long, high-mix
// tails). Applied to the wet signal only, same rationale as Low Cut/High
// Cut.
//
// Ducking: an envelope follower on the DRY (pre-reverb) signal gain-
// reduces the finished wet signal -- fast Attack so the tail steps back as
// soon as a new dry transient arrives, slower Release so it blooms back in
// naturally during gaps, rather than choppy on/off pumping. This is the
// last stage before the external dry/wet mix. Attack/Release are their own
// knobs (not just Ducking's amount) since how QUICKLY it ducks and
// recovers changes the character as much as how MUCH it ducks -- fast/fast
// reads as an obvious pump, slow/slow as a gentle, barely-noticed lift.
// getDuckingGainReductionDb() reports the most reduction seen in the last
// block (in dB, 0 = none) for the editor's real GR meter -- read from the
// audio thread's own atomic, not estimated/illustrative.
//
// Tempo sync: when enabled, Pre-Delay is recomputed every block from the
// host's current tempo (juce::AudioPlayHead) and a chosen note division
// instead of the Pre-Delay knob's millisecond value, clamped to the same
// 0-200ms range the pre-delay buffer is sized for -- see
// computeTempoSyncedPreDelayMs() in PluginProcessor.cpp. Falls back to
// 120bpm if no host tempo is available (e.g. a standalone with no
// transport), the same convention as every other tempo-aware plugin.
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

    // Ducking's gain-reduction meter (see MentalsUI::GainReductionMeterComponent).
    float getDuckingGainReductionDb() const noexcept { return currentDuckingGainReductionDb.load(); }

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
    juce::AudioParameterFloat* lowCutParam       = nullptr;
    juce::AudioParameterFloat* highCutParam      = nullptr;
    juce::AudioParameterFloat* earlyReflectionsParam = nullptr;
    juce::AudioParameterFloat* modDepthParam     = nullptr;
    juce::AudioParameterFloat* modRateHzParam    = nullptr;
    juce::AudioParameterFloat* duckingParam      = nullptr;
    juce::AudioParameterFloat* duckingAttackMsParam  = nullptr;
    juce::AudioParameterFloat* duckingReleaseMsParam = nullptr;
    juce::AudioParameterBool*  tempoSyncParam    = nullptr;
    juce::AudioParameterChoice* preDelayDivisionParam = nullptr;

    // Note divisions offered for tempo-synced Pre-Delay, in quarter-note
    // (beat) units -- e.g. a 1/16 note is 0.25 of a beat, a 1/8 triplet is
    // 1/3 of a 1/8 note (0.5 beats * 2/3). Shared between the processor
    // (to compute the synced ms value) and the editor (to populate the
    // ComboBox), so the two can never disagree about what "1/8T" means.
    static constexpr int numPreDelayDivisions = 9;
    static const juce::StringArray preDelayDivisionNames;
    static const float preDelayDivisionBeats[numPreDelayDivisions];

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void seedFactoryPresetsIfMissing();
    float computeTempoSyncedPreDelayMs() const;

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

    // Low Cut/High Cut: plain Butterworth filters on the wet signal (see
    // class comment), coefficients only recomputed when a frequency
    // actually changes.
    static constexpr float lowHighCutQ = 0.70710678f;
    std::array<juce::dsp::IIR::Filter<float>, 2> lowCutFilter, highCutFilter;
    float lastLowCutFreqHz = -1.0f, lastHighCutFreqHz = -1.0f;

    // Early reflections: one mono tapped-delay buffer fed from the dry,
    // pre-delayed signal (see class comment). earlyTapSamples is computed
    // from earlyTapDelaysMs in prepareToPlay() once the sample rate is
    // known.
    static constexpr int numEarlyTaps = 8;
    static const float earlyTapDelaysMs[numEarlyTaps];
    static const float earlyTapGains[numEarlyTaps];
    std::array<int, numEarlyTaps> earlyTapSamples {};
    std::vector<float> earlyReflectionBuffer;
    int earlyReflectionWritePos = 0;

    // Modulation: a short per-channel delay line on the wet signal, length
    // wobbled by an LFO (see class comment). modPhase is offset by
    // modPhaseOffsetRad between channels so the two sides drift
    // independently rather than moving in lockstep.
    static constexpr float modBaseDelayMs = 12.0f, modMaxDepthMs = 8.0f;
    static constexpr float modPhaseOffsetRad = juce::MathConstants<float>::halfPi;
    std::array<std::vector<float>, 2> modDelayBuffers;
    std::array<int, 2> modWritePos { 0, 0 };
    std::array<float, 2> modPhase { 0.0f, 0.0f };

    // Ducking: an envelope follower on the dry signal (see class comment).
    // Attack/Release coefficients are recomputed every block straight from
    // the live knob values (two exp() calls -- cheap enough that the
    // "only recompute when changed" caching used for filter coefficients
    // elsewhere in this file isn't worth the extra state here).
    std::array<float, 2> duckingEnvelope { 0.0f, 0.0f };
    std::atomic<float> currentDuckingGainReductionDb { 0.0f };

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsReverbAudioProcessor)
};
