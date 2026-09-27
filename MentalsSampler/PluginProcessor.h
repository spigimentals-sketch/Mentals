#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>

//==============================================================================
// Mentals Sampler: a one-shot/sustained sample player with 12 embedded
// instrument categories (Brass, Woodwinds, Strings, ...), each holding a
// handful of individual sounds -- no user file loading, the whole point is a
// curated, built-in sound set browsed by clicking a category tile, then a
// sound within it, on the UI rather than browsing files. MIDI note-on plays
// the currently selected sound, pitch-shifted by semitone distance from
// middle C, shaped by a shared ADSR, and mixed to stereo through Volume/Pan.
//
// Sample audio itself isn't wired in yet -- each SampleSlot below holds an
// empty buffer and a display name/colour only, so the UI (category grid,
// drilling down into a sound list per category) can be built and tested
// before the embedded audio data lands. Loading real audio into these slots
// (from BinaryData, once samples are embedded) is a follow-up change
// localised to loadEmbeddedSamples() in the .cpp.
//==============================================================================
class MentalsSamplerAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsSamplerAudioProcessor();
    ~MentalsSamplerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    MentalsUI::PresetManager presetManager { apvts, "Mentals Sampler" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    static constexpr int numCategories = 12;
    static constexpr int soundsPerCategory = 4;
    static constexpr int numSlots = numCategories * soundsPerCategory;

    // Which category a flat slot index (0..numSlots-1) belongs to -- e.g.
    // slot 5 (0-based) is category 5/4 = 1 (Woodwinds).
    static constexpr int categoryIndexForSlot (int slotIndex) noexcept { return slotIndex / soundsPerCategory; }

    //==========================================================================
    // One embedded sound. audioData/sourceSampleRate are populated by
    // loadEmbeddedSamples() once real samples are embedded via BinaryData --
    // until then every slot is a named placeholder with an empty buffer, so
    // selecting it and pressing a key is silent but otherwise fully wired.
    //==========================================================================
    struct SampleSlot
    {
        juce::String name;
        juce::Colour tileColour;
        juce::AudioBuffer<float> audioData;
        double sourceSampleRate = 44100.0;

        bool hasAudio() const noexcept { return audioData.getNumSamples() > 0; }
    };

    // One tile in the home-screen category grid -- Brass, Woodwinds, etc.
    // Clicking one drills down into that category's soundsPerCategory sounds
    // (see getSlots(), sliced by categoryIndexForSlot()).
    struct CategoryInfo
    {
        juce::String name;
        juce::Colour colour;
    };

    const std::array<SampleSlot, numSlots>& getSlots() const noexcept { return slots; }
    const std::array<CategoryInfo, numCategories>& getCategories() const noexcept { return categories; }

    int getSelectedSlot() const noexcept { return selectedSlotParam->getIndex(); }
    void setSelectedSlot (int index) { selectedSlotParam->setValueNotifyingHost (selectedSlotParam->convertTo0to1 ((float) index)); }

    // Called by the editor whenever a tile is clicked, so a quick preview
    // plays even with no MIDI input connected (e.g. auditioning sounds in a
    // DAW browser before dropping the plugin on a track).
    void triggerPreview (int slotIndex);

    juce::AudioParameterChoice* selectedSlotParam = nullptr;
    juce::AudioParameterFloat* attackParam  = nullptr;
    juce::AudioParameterFloat* releaseParam = nullptr;
    juce::AudioParameterFloat* volumeParam  = nullptr;
    juce::AudioParameterFloat* panParam     = nullptr;

    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void loadEmbeddedSamples();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;

    //==========================================================================
    // A single playing instance of a slot's sample -- multiple can overlap
    // (e.g. releasing one note while another starts) since each carries its
    // own read position and envelope state independently.
    //==========================================================================
    struct ActiveVoice
    {
        bool active = false;
        int slotIndex = 0;
        int midiNote = 60;
        double playbackRatio = 1.0;
        double readPosition = 0.0;
        juce::ADSR envelope;
        bool isInPreviewMode = false; // ignores note-off; used for tile-click auditioning
    };

    static constexpr int maxActiveVoices = 16;
    std::array<ActiveVoice, maxActiveVoices> activeVoices;

    void startVoice (int slotIndex, int midiNote, float velocity, bool preview);
    void stopVoice (int midiNote);
    void renderVoices (juce::AudioBuffer<float>& outputBuffer);

    std::array<SampleSlot, numSlots> slots;
    std::array<CategoryInfo, numCategories> categories;

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsSamplerAudioProcessor)
};
