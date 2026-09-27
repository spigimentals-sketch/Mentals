#pragma once

#include <JuceHeader.h>
#include "../MentalsSuite/PluginProcessor.h"
#include <atomic>

//==============================================================================
// One live mixer input channel: a freely user-configurable insert chain
// (via a private Mentals Suite instance -- add/remove/reorder any of the
// suite's plugin types, exactly like Studio One's channel Inserts slot),
// followed by a fader and mute/solo. Reuses MentalsSuiteAudioProcessor and
// its existing chain-builder editor completely unmodified -- one
// independent instance per physical channel, so each channel's inserts are
// entirely its own.
//
// Mentals Suite is stereo-only internally; mono in/out is handled here the
// same way it would be for any of its individual modules: the incoming
// mono signal is duplicated into both channels of an internal stereo
// scratch buffer, the whole insert chain processes that stereo buffer as
// normal, and channel 0 is read back out as the mono result afterwards.
//==============================================================================
class LiveChannelStrip
{
public:
    LiveChannelStrip();
    ~LiveChannelStrip();

    // Prepares the insert chain in its normal stereo layout -- call once
    // whenever the audio device (re)starts with a new sample rate/buffer
    // size.
    void prepare (double sampleRate, int samplesPerBlock);

    // Processes one mono block in place: the insert chain, then the fader
    // and mute/solo. isAudibleGivenSolo is computed by LiveAudioEngine
    // across all channels (a single channel can't know by itself whether
    // some other channel is soloed); pass true when no channel anywhere is
    // currently soloed.
    void process (juce::AudioBuffer<float>& monoBuffer, bool isAudibleGivenSolo);

    // The channel's insert chain -- add/remove/reorder modules via its own
    // existing chain-builder API (addModuleToChain(), etc.), or just open
    // its own editor (createEditorAndMakeActive()) for the full "+Add
    // Module" UI, same as hosting Mentals Suite directly in a DAW.
    MentalsSuiteAudioProcessor& getInsertChain() noexcept { return insertChain; }

    // For the channel strip's peak meter.
    float getOutputPeakLevel() const noexcept { return outputPeak.load(); }

    // The channel fader, in dB -- applied after the insert chain, same
    // position a real mixer's fader occupies at the bottom of the strip.
    float getFaderGainDb() const noexcept { return faderGainDb.load(); }
    void setFaderGainDb (float db) noexcept { faderGainDb.store (db); }

    bool isMuted() const noexcept { return muted.load(); }
    void setMuted (bool shouldBeMuted) noexcept { muted.store (shouldBeMuted); }

    bool isSoloed() const noexcept { return soloed.load(); }
    void setSoloed (bool shouldBeSoloed) noexcept { soloed.store (shouldBeSoloed); }

    // User-editable label, e.g. "Kick In", "Vocal 1" -- purely cosmetic,
    // shown in the channel strip.
    juce::String channelName;

private:
    MentalsSuiteAudioProcessor insertChain;

    juce::MidiBuffer scratchMidi; // every processBlock() needs one; Mentals Suite only forwards MIDI through unchanged
    juce::AudioBuffer<float> scratchStereo; // L/R duplicated from the mono input; the insert chain processes this

    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> faderGainDb { 0.0f };
    std::atomic<bool> muted { false };
    std::atomic<bool> soloed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveChannelStrip)
};
