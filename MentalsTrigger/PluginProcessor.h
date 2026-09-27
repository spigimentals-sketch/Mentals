#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>

//==============================================================================
// Mentals Trigger: a drum-trigger/replacer -- an envelope follower watches
// the incoming audio for hits, and each detected hit plays back a sample
// (loaded from disk) instead of the original signal, the same job units
// like Addictive Trigger, Slate Trigger, or Drumagog do.
//
// Detection: the input (summed to mono) is rectified and smoothed by a fast-
// attack/slower-release envelope follower. A hit fires when that envelope
// rises above Threshold while the retrigger lockout (Choke Time) has fully
// elapsed since the last hit; the envelope's value at that instant is the
// hit's raw velocity (0..1).
//
// That raw velocity is reshaped by the Curve parameter (see shapeVelocity())
// before it's used for anything else, matching Addictive Trigger's MIDI
// Response curve -- Curve > 0 boosts quiet hits (concave), < 0 suppresses
// them (convex), 0 is linear. The shaped velocity both selects a layer (via
// the two split boundaries) and scales that hit's own playback level, so
// harder hits are audibly louder within a layer, not just switching which
// layer plays.
//
// Each of the 3 velocity layers (Soft/Medium/Hard) holds up to 4 round-robin
// sample slots (A/B/C/D); a layer that has more than one loaded cycles
// through them in order on successive hits (skipping empty slots) so
// repeated hits at a similar velocity don't sound identically repetitive.
//
// Only one voice plays at a time -- a new hit always restarts playback
// (whichever layer/round-robin it selects), matching how hardware/software
// drum triggers behave: the previous one-shot is simply cut off, never
// layered with the new one.
//==============================================================================
class MentalsTriggerAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsTriggerAudioProcessor();
    ~MentalsTriggerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Trigger"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // Reflects the longest currently-loaded sample across all layers/round-
    // robins (with a small floor), rather than a fixed guess -- a host uses
    // this to decide how long to keep processing after playback stops, so a
    // long-loaded sample (e.g. a cymbal swell) previously risked getting cut
    // off early against a hardcoded short tail.
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    MentalsUI::PresetManager presetManager { apvts, "Mentals Trigger" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    static constexpr int numLayers = 3; // Soft, Medium, Hard
    static constexpr int numRoundRobins = 4; // A, B, C, D
    static const char* const layerNames[numLayers];
    static const char* const roundRobinNames[numRoundRobins];

    //==========================================================================
    // One round-robin slot's loaded sample -- empty (no audio, name "Empty")
    // until the user picks a file for it via the editor.
    //==========================================================================
    struct RoundRobinSlot
    {
        juce::String fileName { "Empty" };
        juce::AudioBuffer<float> audioData;
        double sourceSampleRate = 44100.0;

        bool hasAudio() const noexcept { return audioData.getNumSamples() > 0; }
    };

    //==========================================================================
    // One velocity layer: up to numRoundRobins alternate takes, cycled on
    // successive hits (see pickRoundRobinIndex() in the .cpp).
    //==========================================================================
    struct SampleLayer
    {
        std::array<RoundRobinSlot, numRoundRobins> roundRobins;
        int nextRoundRobinIndex = 0;

        bool hasAnyAudio() const noexcept
        {
            for (auto& rr : roundRobins)
                if (rr.hasAudio())
                    return true;
            return false;
        }
    };

    const std::array<SampleLayer, numLayers>& getLayers() const noexcept { return layers; }

    // Loads (and format-converts as needed) an audio file from disk into the
    // given layer/round-robin slot. Safe to call from the message thread
    // only (it isn't called from processBlock); the resulting buffer is only
    // ever read from the audio thread afterwards, never written to
    // concurrently (see layersLock).
    bool loadSampleForSlot (int layerIndex, int roundRobinIndex, const juce::File& file);

    // UI feedback: which layer/round-robin fired most recently and at what
    // (shaped) velocity, so the editor can flash indicators without
    // duplicating the detection logic. -1 means "no hit yet".
    int getLastTriggeredLayer() const noexcept { return lastTriggeredLayer.load(); }
    int getLastTriggeredRoundRobin() const noexcept { return lastTriggeredRoundRobin.load(); }
    float getLastHitVelocity() const noexcept { return lastHitVelocity.load(); }
    juce::int64 getLastHitCounter() const noexcept { return lastHitCounter.load(); } // increments per hit, so the UI can detect a *new* hit even if it repeats the same layer/velocity

    float getInputLevelDb() const noexcept { return juce::Decibels::gainToDecibels (inputPeakLinear.load(), -100.0f); }
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Applies the Curve parameter's response shaping to a raw 0..1 velocity
    // -- exposed so the editor's curve display can preview the shape
    // without duplicating the maths.
    float shapeVelocity (float raw01) const noexcept;

    // Picks a layer index for a (shaped) velocity, using the two
    // split-boundary parameters -- exposed so the editor's curve/velocity
    // displays can show which layer a given point would land in.
    int pickLayerForVelocity (float velocity01) const noexcept;

    //==========================================================================
    // Scrolling waveform + transient markers, for the editor's TRANSIENTS
    // display. Each column summarises columnLengthSamples worth of the
    // input signal's peak amplitude (0..1) plus, if a hit landed in that
    // column, its shaped velocity and which layer it picked (hitVelocity
    // < 0 means "no hit in this column"). Written once per column from
    // processBlock, read by the editor on a timer -- see waveformLock.
    //==========================================================================
    struct WaveformColumn
    {
        float peak = 0.0f;
        float hitVelocity = -1.0f;
        int hitLayer = -1;
    };
    static constexpr int waveformColumns = 400;

    // Copies the ring buffer out in chronological order (oldest first) so
    // the editor can draw it directly left-to-right.
    void getWaveformSnapshot (std::array<WaveformColumn, waveformColumns>& outColumns) const;

    juce::AudioParameterFloat* thresholdParam    = nullptr;
    juce::AudioParameterFloat* sensitivityParam  = nullptr;
    juce::AudioParameterFloat* chokeTimeParam    = nullptr;
    juce::AudioParameterFloat* softMedSplitParam = nullptr;
    juce::AudioParameterFloat* medHardSplitParam = nullptr;
    juce::AudioParameterFloat* curveParam        = nullptr;
    juce::AudioParameterFloat* outputGainParam   = nullptr;

    // Per-layer Mute/Solo, standard mixer-channel semantics: if any layer is
    // soloed, only soloed layers are audible; otherwise every layer is
    // audible except muted ones. A hit still fires, still selects a layer,
    // and still flashes that layer's card in the UI even when silenced this
    // way -- only the actual audio output is affected (see isLayerAudible()
    // and its use in triggerHit()).
    std::array<juce::AudioParameterBool*, numLayers> muteParams { nullptr, nullptr, nullptr };
    std::array<juce::AudioParameterBool*, numLayers> soloParams { nullptr, nullptr, nullptr };

    bool isLayerAudible (int layerIndex) const noexcept;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void updateLevelMeters (const juce::AudioBuffer<float>& buffer, float inputPeak);
    void pushWaveformColumn (float peak, float hitVelocity, int hitLayer);

    // Picks which of a layer's loaded round-robin slots to use next
    // (skipping empty ones), returns -1 if none are loaded.
    int pickRoundRobinIndex (SampleLayer& layer) const noexcept;

    // Returns the layer index it picked, so callers (and the waveform
    // ring buffer) can record it against this hit.
    int triggerHit (float rawVelocity01);

    double currentSampleRate = 44100.0;

    //==========================================================================
    // Envelope-follower transient detector state.
    //==========================================================================
    float envelopeFollower = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    bool wasAboveThreshold = false; // tracks the RISING edge, so a hit fires once per crossing, not every sample spent above threshold
    int lockoutSamplesRemaining = 0;

    //==========================================================================
    // The single playing voice -- retriggering restarts it from sample 0,
    // cutting off whatever was previously playing. velocityGain applies the
    // shaped hit velocity to this voice's playback level.
    //==========================================================================
    struct Voice
    {
        bool active = false;
        int layerIndex = 0;
        int roundRobinIndex = 0;
        double readPosition = 0.0;
        double playbackRatio = 1.0;
        float velocityGain = 1.0f;
    };
    Voice voice;

    // Guards `layers` -- loadSampleForSlot() swaps a slot's buffer in from
    // the message thread while processBlock() reads it on the audio thread.
    // The slow part (decoding the file) happens before the lock is taken, so
    // this is only ever held for a fast field swap-in on the loading side,
    // and for one whole block's worth of hit-detection + voice rendering on
    // the audio side -- cheap and uncontended in practice since loads only
    // happen on user action.
    juce::CriticalSection layersLock;
    std::array<SampleLayer, numLayers> layers;

    std::atomic<int> lastTriggeredLayer { -1 };
    std::atomic<int> lastTriggeredRoundRobin { -1 };
    std::atomic<float> lastHitVelocity { 0.0f };
    std::atomic<juce::int64> lastHitCounter { 0 };

    std::atomic<float> inputPeakLinear { 0.0f };
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    //==========================================================================
    // Waveform ring buffer -- see the WaveformColumn comment above.
    //==========================================================================
    juce::SpinLock waveformLock;
    std::array<WaveformColumn, waveformColumns> waveformRing;
    int waveformWriteIndex = 0;
    int columnLengthSamples = 220;
    int samplesInCurrentColumn = 0;
    float currentColumnPeak = 0.0f;
    float currentColumnHitVelocity = -1.0f;
    int currentColumnHitLayer = -1;

    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsTriggerAudioProcessor)
};
