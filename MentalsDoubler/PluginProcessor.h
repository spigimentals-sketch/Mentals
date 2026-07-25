#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include "../MentalsAutotune/PitchDSP.h" // reusing the granular pitch shifter unmodified -- see class comment
#include <array>
#include <vector>

//==============================================================================
// Mentals Doubler: a classic ADT-style doubling effect -- not a continuous
// chorus/flanger (Mentals Chorus) and not a massed choir/ensemble (Mentals
// Vox Choir), but exactly 2 or 4 extra "doubled" performances of the source,
// each independently detuned, delayed, and panned, that sit alongside the
// dry signal to add width and thickness to a single lead vocal/guitar/
// instrument -- the same job a doubler pedal or "double-track this" studio
// trick does.
//
// Each voice is built from:
//   - PitchDSP::PitchShifterChannel (the same granular shifter Autotune and
//     Vox Choir use), detuned by a fixed per-voice cents offset (Detune)
//     spread symmetrically across the voices;
//   - a short per-voice delay line (Delay), also spread slightly per voice
//     so identical-delay voices don't comb-filter against each other;
//   - equal-power panning spread across the stereo field (Width).
//
// Humanize adds a slow, independently-random (NOT a clean sine LFO -- that
// again is Chorus's job) wander to each voice's pitch and delay, mimicking
// the natural inconsistency of a real second performance rather than a
// perfectly static double. See HumanizeState::advance() in the .cpp.
//
// All voices are summed and normalised by 1/sqrt(voices) so overall loudness
// stays roughly constant regardless of voice count, Low Cut trims mud from
// the wet sum only (the dry signal's own low end is untouched), then Mix
// blends the dry (centred) signal against the wet (spread) voices. Output is
// always stereo, the same as Vox Choir, since the whole point is the stereo
// spread.
//==============================================================================
class MentalsDoublerAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsDoublerAudioProcessor();
    ~MentalsDoublerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Doubler"; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals Doubler" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    static constexpr int maxVoices = 4;
    static const int voiceCountChoices[2]; // { 2, 4 }

    static constexpr float maxDetuneCents    = 40.0f;
    static constexpr float maxDelayMs        = 40.0f;
    static constexpr float maxHumanizeCents  = 8.0f;
    static constexpr float maxHumanizeMs     = 3.0f;

    // Shared by processBlock() and the editor's voice-spread display, so the
    // picture can never disagree with what the DSP is actually doing.
    static float computeVoiceBaseDetuneCents (int voiceIndex, int numVoices, float detuneParam) noexcept;
    static float computeVoiceBaseDelayMs (int voiceIndex, int numVoices, float delayParam) noexcept;
    static float computeVoicePan (int voiceIndex, int numVoices, float widthParam) noexcept;

    // Cached parameter pointers, read directly by the editor's voice-spread
    // display.
    juce::AudioParameterChoice* voicesParam   = nullptr;
    juce::AudioParameterFloat*  detuneParam   = nullptr;
    juce::AudioParameterFloat*  delayParam    = nullptr;
    juce::AudioParameterFloat*  widthParam    = nullptr;
    juce::AudioParameterFloat*  humanizeParam = nullptr;
    juce::AudioParameterFloat*  lowCutParam   = nullptr;
    juce::AudioParameterFloat*  mixParam      = nullptr;

    // Stereo (default) spreads voices across the field per Width, same as
    // ever; Mono forces every voice dead centre regardless of Width, for a
    // layered-but-centred double on a mono source/output, or just to check
    // how the double sits before committing to a wide stereo spread. Read
    // directly by the editor's voice-spread display so the picture never
    // shows spread that isn't actually happening.
    juce::AudioParameterBool* stereoParam = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;

    //==========================================================================
    // Low Cut: a plain Butterworth high-pass on the wet voice sum only,
    // recomputed only when the frequency actually changes.
    //==========================================================================
    static constexpr float lowCutQ = 0.70710678f;
    std::array<juce::dsp::IIR::Filter<float>, 2> lowCutFilter;
    float lastLowCutFreqHz = -1.0f;

    //==========================================================================
    // Slow, independently-random (not periodic) per-voice wander applied to
    // pitch and delay -- see the class comment and advance()'s definition in
    // the .cpp for why this reads as "human", not "chorus".
    //==========================================================================
    struct HumanizeState
    {
        float currentCentsOffset = 0.0f, targetCentsOffset = 0.0f;
        float currentMsOffset = 0.0f, targetMsOffset = 0.0f;
        int samplesUntilNewTarget = 0;
        float followCoeff = 0.0f;
        double sampleRate = 44100.0;
        juce::Random random;

        void prepare (double sampleRateIn, juce::int64 seed) noexcept;
        void advance (float humanizeAmount01) noexcept;
    };

    struct Voice
    {
        PitchDSP::PitchShifterChannel pitchShifter;
        std::vector<float> delayLine;
        int delayWritePos = 0;
        HumanizeState humanize;
    };

    std::array<Voice, maxVoices> voices;

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsDoublerAudioProcessor)
};
