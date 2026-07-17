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

    static constexpr int maxVoices = 32;
    static const int voiceCountChoices[4];

    static constexpr float maxPitchCents   = 25.0f; // practical calibration -- subtle, not a full semitone
    static constexpr float maxVibratoCents = 15.0f;
    static constexpr float maxTimingMs     = 30.0f;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;

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
