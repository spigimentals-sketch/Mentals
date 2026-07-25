#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>
#include <vector>

//==============================================================================
// Mentals Limiter: a look-ahead brick-wall peak limiter. Input Gain drives
// the signal into the limiter; Ceiling is the absolute output level it will
// never exceed; Release controls how quickly gain recovers after a peak.
//
// How the look-ahead works: the peak-detection envelope (see
// MentalsUI::DynamicsDSP::EnvelopeFollower, reused from Compressor/De-esser
// with an all-but-instant attack) is computed from the UNDELAYED,
// input-gained signal, while gain reduction is applied to a DELAYED copy of
// that same signal (a short ring buffer, ~5ms, same read-before-write
// pattern Mentals Autotune's dry-signal delay uses for Formant
// Preservation). Because the envelope reacts to a loud transient before
// that same transient reaches the delayed output, the gain has already
// started dropping by the time the peak needs catching -- the standard
// look-ahead-limiter trick, not an approximation of one. The look-ahead
// time itself is a fixed 5ms rather than a parameter, matching this
// project's preference for not exposing knobs most users would never want
// to touch; it's what sets the reported plugin latency.
//
// Both channels always share one gain-reduction value (stereo-linked),
// the standard behaviour for a limiter -- letting channels reduce
// independently would shift the stereo image on transients.
//
// True Peak (ITU-R BS.1770 Annex 2, optional, on by default): a plain
// sample-peak detector can miss inter-sample peaks that a D/A converter's
// or a lossy encoder's own reconstruction filter produces between two
// samples that are each individually under the Ceiling -- exactly what
// happens if content is limited right up to 0dBFS and then clips after
// MP3/AAC encoding or D/A conversion. When enabled, the gained signal is
// also oversampled 4x (juce::dsp::Oversampling, the same technique Mentals
// Mastering Meter's true-peak reading uses) purely to see the higher,
// more accurate peak within each sample interval; that's what actually
// drives the envelope follower and gain reduction below, so the limiter
// reacts to peaks a sample-domain-only detector would let through. The
// oversampling filter's own latency never needs compensating or
// reporting: its output is scanned for a peak value and discarded, never
// played back, so it doesn't delay the real signal path at all.
//==============================================================================
class MentalsLimiterAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsLimiterAudioProcessor();
    ~MentalsLimiterAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Limiter"; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals Limiter" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Gain-reduction meter (see MentalsUI::GainReductionMeterComponent) --
    // the most negative (i.e. most reduction) value seen during the last
    // processed block.
    float getGainReductionDb() const noexcept { return currentGainReductionDb.load(); }

    // Cached parameter pointers, read directly by the editor's transfer-
    // curve display (illustrative only -- it shows the static input/output
    // shape, not the look-ahead/envelope smoothing that's actually applied).
    juce::AudioParameterFloat* inputGainParam = nullptr;
    juce::AudioParameterFloat* ceilingParam   = nullptr;
    juce::AudioParameterFloat* releaseParam   = nullptr;
    juce::AudioParameterFloat* mixParam       = nullptr;
    juce::AudioParameterBool*  truePeakParam  = nullptr;

    // When off, the final output is summed to mono (both channels made
    // identical) -- a mono-compatibility check/forcing switch, same control
    // every Mentals plugin now has.
    juce::AudioParameterBool* stereoParam = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    double currentSampleRate = 44100.0;

    static constexpr float lookaheadMs = 5.0f;
    static constexpr int maxSupportedChannels = 8;
    static constexpr float attackMs = 0.1f; // all-but-instant -- look-ahead does the real work, see class comment

    std::array<std::vector<float>, maxSupportedChannels> delayLines;
    std::array<int, maxSupportedChannels> delayWritePos {};

    MentalsUI::DynamicsDSP::EnvelopeFollower envelopeFollower;

    // True Peak detection -- see class comment. gainedBuffer holds the
    // input-gained signal for the whole block (computed once, up front,
    // rather than folded into the main per-sample loop) so it can be
    // handed to the oversampler as one AudioBlock; truePeakLevel then holds
    // one true-peak-aware level per original-rate sample, read by the main
    // loop below in place of a plain per-sample abs().
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> gainedBuffer;
    std::vector<float> truePeakLevel;

    std::atomic<float> currentGainReductionDb { 0.0f };
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsLimiterAudioProcessor)
};
