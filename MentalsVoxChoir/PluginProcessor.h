#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include "ChoirVoiceDSP.h"
#include "../MentalsAutotune/PitchDSP.h" // reusing the granular pitch shifter unmodified -- see class comment
#include <array>
#include <vector>

//==============================================================================
// Mentals Vox Choir: turns one vocal into an ensemble of up to 32 voices.
// The input (mono or stereo, summed down internally) is fed through
// `voices` independent copies, each built from:
//   - PitchDSP::PitchShifterChannel (the same granular shifter Mentals
//     Autotune uses) detuned by a fixed per-voice cents offset (Pitch) plus
//     a per-voice sine vibrato (Vibrato), each voice's own rate/depth/phase
//     seeded deterministically by its index (see ChoirVoiceDSP.h) so the
//     ensemble's character is stable rather than reshuffling every reload;
//   - a short fixed per-voice delay (Timing) simulating slightly staggered
//     entrances, the way real singers never attack a note in perfect
//     unison;
//   - equal-power panning spread evenly across the stereo field (Spread).
// All voices are summed and normalised by 1/sqrt(voices) so overall
// loudness stays roughly constant regardless of how many voices are
// active, then blended against the dry (centred, unprocessed) signal via
// Mix. Output is always stereo -- the whole point of this effect is the
// stereo spread, so unlike this project's other effects, input and output
// channel counts don't have to match.
//
// Softness (applied last, to the final stereo output, dry+wet together --
// see applySoftness() in PluginProcessor.cpp) is what actually rounds off
// a "hard"/edgy result: a high-shelf de-harshening filter tames excess
// top-end energy, and a fast/slow envelope comparison tames consonant and
// pick/attack transients specifically (the part of "hardness" a static EQ
// shelf alone can't reach), both scaling together with one knob.
//
// Low Cut is a high-pass filter on the WET ensemble only, applied before
// the dry/wet blend -- up to 32 detuned copies of the same source stack
// their low-mid energy on top of each other and each other's, which reads
// as mud/thickness rather than the dry source's own (untouched) low end.
// Sweeping it up doesn't touch the dry signal at all, only how much low
// end the ensemble itself is allowed to contribute.
//
// Runs well after a pitch-correction stage in a chain (e.g. Mentals
// Suite): correcting pitch first gives every voice a clean, in-tune
// reference to detune/vibrato around, rather than compounding onto
// whatever pitch inaccuracies were already in the source.
//==============================================================================
class MentalsVoxChoirAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsVoxChoirAudioProcessor();
    ~MentalsVoxChoirAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Vox Choir"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    MentalsUI::PresetManager presetManager { apvts, "Mentals Vox Choir" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Cached parameter pointers, read directly by the editor's choir-spread
    // visualisation (which calls ChoirVoiceDSP::computeVoiceCharacter()/
    // computeVoicePan() itself so the picture can never disagree with what
    // the DSP is actually doing).
    juce::AudioParameterChoice* voicesParam   = nullptr;
    juce::AudioParameterFloat*  vibratoParam  = nullptr;
    juce::AudioParameterFloat*  pitchParam    = nullptr;
    juce::AudioParameterFloat*  timingParam   = nullptr;
    juce::AudioParameterFloat*  spreadParam   = nullptr;
    juce::AudioParameterFloat*  mixParam      = nullptr;
    juce::AudioParameterFloat*  softnessParam = nullptr;
    juce::AudioParameterFloat*  lowCutParam   = nullptr;

    // When off, the final output is summed to mono (both channels made
    // identical) regardless of Spread or anything else upstream -- a mono-
    // compatibility check/forcing switch, same control every Mentals plugin
    // now has.
    juce::AudioParameterBool* stereoParam = nullptr;

    static constexpr int maxVoices = 32;
    static const int voiceCountChoices[4];

    static constexpr float maxPitchCents   = 25.0f; // practical calibration -- subtle, not a full semitone
    static constexpr float maxVibratoCents = 15.0f;
    static constexpr float maxTimingMs     = 30.0f;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void applySoftness (float* left, float* right, int numSamples, float softnessAmount) noexcept;

    double currentSampleRate = 44100.0;

    //==========================================================================
    // Low Cut: a plain Butterworth high-pass on the wet ensemble sum
    // (outL/outR), recomputed only when the frequency actually changes.
    //==========================================================================
    static constexpr float lowCutQ = 0.70710678f;
    std::array<juce::dsp::IIR::Filter<float>, 2> lowCutFilter;
    float lastLowCutFreqHz = -1.0f;

    //==========================================================================
    // Softness's de-harsh shelf: fixed frequency/Q, only the gain (0 at
    // Softness=0, up to softnessMaxShelfCutDb at 100%) changes, so recomputing
    // every block (not per-sample) is plenty responsive to knob moves.
    //==========================================================================
    static constexpr float softnessShelfFreqHz    = 3000.0f;
    static constexpr float softnessShelfQ         = 0.70710678f;
    static constexpr float softnessMaxShelfCutDb  = -12.0f;
    std::array<juce::dsp::IIR::Filter<float>, 2> softnessShelf;
    float lastSoftnessShelfGainDb = 1.0f; // deliberately not a valid gain, forces the first updateIfNeeded() to set coefficients

    // Softness's transient tamer: compares a fast envelope (catches
    // consonants/pick attacks) against a slow one (the sustained body) and
    // pulls down just the excess, so it can round off "bite" without
    // dulling sustained tone the way lowering Mix or the shelf alone would.
    static constexpr float softnessFastAttackSeconds  = 0.0005f;
    static constexpr float softnessFastReleaseSeconds = 0.015f;
    static constexpr float softnessSlowAttackSeconds  = 0.030f;
    static constexpr float softnessSlowReleaseSeconds = 0.150f;
    static constexpr float softnessMaxTransientCut    = 0.85f; // never fully guts a transient, even at Softness=100%
    struct SoftnessEnvelopes { float fast = 0.0f, slow = 0.0f; };
    std::array<SoftnessEnvelopes, 2> softnessEnvelopes;
    float softnessFastAttackCoeff = 0.0f, softnessFastReleaseCoeff = 0.0f;
    float softnessSlowAttackCoeff = 0.0f, softnessSlowReleaseCoeff = 0.0f;

    struct Voice
    {
        PitchDSP::PitchShifterChannel pitchShifter;
        std::vector<float> delayLine;
        int delayWritePos = 0;
        float vibratoPhase = 0.0f; // running phase, radians -- persists across blocks
    };

    std::array<Voice, maxVoices> voices;

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsVoxChoirAudioProcessor)
};
