#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>

//==============================================================================
// Mentals Circuit Comp: a compressor with four switchable modeled circuit
// behaviours (VCA/FET/Optical/Tube-Vari-Mu), sidechain (with a detector
// highpass to avoid low-end pumping), a parallel Blend knob for New
// York-style compression, per-mode saturation coloration, switchable
// stereo-linked/independent detection, and an optional 7-band split with
// fully independent per-band Threshold/Ratio/Attack/Release/Makeup plus
// per-band Mute/Solo, editable directly on the spectrum display (drag a
// crossover line to move it, click a band to select and edit it).
//
// What actually differs between modes (see computeModeAdjustedOutputDb()
// and processBandSample()):
//   - VCA:     textbook soft-knee gain computer, knob-mapped attack/release
//              spanning a wide, general-purpose range. The "reference"
//              mode the others are variations on.
//   - FET:     much faster achievable attack (down to 20us), a harder knee
//              (scaled down from the shared Knee knob), and a grittier,
//              asymmetric saturation curve -- modeled after fast, aggressive
//              1176-style behaviour.
//   - Optical: a program-dependent release -- a fast and a slow envelope
//              follower both run continuously, blended by how much gain
//              reduction was applied a moment ago (more reduction leans on
//              the slow stage), mimicking an opto cell's photoresistive
//              memory. Softer knee, warmer/gentler saturation.
//   - Tube:    ratio grows with how far the signal sits above threshold
//              (a vari-mu tube's inherent behaviour, rather than a fixed
//              ratio), plus an extra slow one-pole glide applied to the
//              gain-reduction value itself on top of the envelope
//              follower, modeling a tube circuit's electrical/thermal
//              sluggishness. Rich, biased-asymmetric saturation for
//              2nd-harmonic warmth.
//
// Mode/Knee/Saturation/Blend stay global (apply the same way to every
// band) -- only Threshold/Ratio/Attack/Release/Makeup go per-band, since
// those are the "how much and how fast" controls that actually differ
// meaningfully band to band, while circuit character and dry/wet blend are
// still "one compressor, seven bands" rather than seven unrelated ones.
// The Sidechain HPF is single-band-only: once a 7-band split is already
// shaping detection per band, a further global sidechain filter would just
// fight that (and per-band self-detection is standard for multiband
// compressors, unlike single-band designs which commonly add a sidechain
// filter precisely because they don't otherwise discriminate by
// frequency).
//==============================================================================
class MentalsCircuitCompAudioProcessor : public juce::AudioProcessor
{
public:
    enum Mode { modeVCA = 0, modeFET, modeOptical, modeTube, numModes };
    static constexpr int numBands = 7;
    static constexpr int numCrossovers = numBands - 1;

    MentalsCircuitCompAudioProcessor();
    ~MentalsCircuitCompAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Circuit Comp"; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals Circuit Comp" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    float getInputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (inputPeakLinear.load(), -100.0f); }
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }
    float getGainReductionDb() const noexcept { return currentGainReductionDb.load(); }

    // Read by the editor's spectrum display for the live per-band gain-
    // reduction shading -- 0 when Multiband is off or that band is muted/
    // not currently reducing.
    float getBandGainReductionDb (int bandIndex) const noexcept { return bandGainReductionDb[(size_t) bandIndex].load(); }

    // Shared by processBlock() and the editor's transfer-curve display, so
    // the curve can never show a shape that disagrees with what a given
    // Mode/Threshold/Ratio/Knee setting actually does to the audio.
    static float computeModeAdjustedOutputDb (int mode, float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept;

    juce::AudioParameterChoice* modeParam            = nullptr;
    juce::AudioParameterFloat*  thresholdParam        = nullptr;
    juce::AudioParameterFloat*  ratioParam            = nullptr;
    juce::AudioParameterFloat*  kneeParam             = nullptr;
    juce::AudioParameterFloat*  attackParam           = nullptr;
    juce::AudioParameterFloat*  releaseParam          = nullptr;
    juce::AudioParameterFloat*  makeupGainParam       = nullptr;
    juce::AudioParameterFloat*  saturationParam       = nullptr;
    juce::AudioParameterFloat*  blendParam            = nullptr;
    juce::AudioParameterFloat*  sidechainHpfFreqParam = nullptr;
    juce::AudioParameterBool*   useSidechainParam     = nullptr;
    juce::AudioParameterBool*   multibandEnabledParam = nullptr;
    juce::AudioParameterBool*   stereoLinkParam       = nullptr;

    // When off, the final output is summed to mono (both channels made
    // identical) -- a mono-compatibility check/forcing switch, same control
    // every Mentals plugin now has. Distinct from Stereo Link, which
    // controls whether the two channels' detectors are linked, not whether
    // the final output is collapsed to mono.
    juce::AudioParameterBool* stereoParam = nullptr;

    // Per-band controls, used only when Multiband is on -- indexed 0..6,
    // Low-to-high. The editor rebinds its Threshold/Ratio/Attack/Release/
    // Makeup knobs to whichever of these is currently selected, the same
    // way Mentals Multimode EQ rebinds its knobs to the selected EQ band.
    std::array<juce::AudioParameterFloat*, numBands> bandThresholdParams {};
    std::array<juce::AudioParameterFloat*, numBands> bandRatioParams {};
    std::array<juce::AudioParameterFloat*, numBands> bandAttackParams {};
    std::array<juce::AudioParameterFloat*, numBands> bandReleaseParams {};
    std::array<juce::AudioParameterFloat*, numBands> bandMakeupParams {};
    std::array<juce::AudioParameterBool*,  numBands> bandMuteParams {};
    std::array<juce::AudioParameterBool*,  numBands> bandSoloParams {};

    // The 6 crossover points dividing the 7 bands -- dragged directly on
    // the editor's spectrum display.
    std::array<juce::AudioParameterFloat*, numCrossovers> crossoverFreqParams {};

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateInputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void seedFactoryPresetsIfMissing();

    double currentSampleRate = 44100.0;

    //==========================================================================
    // Per-channel state for one compressor "band" (either the whole signal,
    // when Multiband is off, or one of the seven split bands when it's on).
    //==========================================================================
    struct ChannelBandState
    {
        MentalsUI::DynamicsDSP::EnvelopeFollower primaryFollower;     // VCA/FET/Tube, and Optical's "fast" stage
        MentalsUI::DynamicsDSP::EnvelopeFollower opticalSlowFollower; // Optical's program-dependent "slow" stage
        float tubeSmoothedGainReductionDb = 0.0f; // Tube/Vari-Mu's extra sluggish glide
        float previousGainReductionDb = 0.0f;     // feeds Optical's fast/slow blend depth

        void prepare (double sampleRate)
        {
            primaryFollower.prepare (sampleRate);
            opticalSlowFollower.prepare (sampleRate);
            reset();
        }

        void reset()
        {
            primaryFollower.reset();
            opticalSlowFollower.reset();
            tubeSmoothedGainReductionDb = 0.0f;
            previousGainReductionDb = 0.0f;
        }
    };

    struct Band
    {
        std::array<ChannelBandState, 2> channels;
        void prepare (double sampleRate) { for (auto& c : channels) c.prepare (sampleRate); }
        void reset() { for (auto& c : channels) c.reset(); }
    };

    std::array<Band, numBands> bands;

    //==========================================================================
    // Seven-band split via 6 cascaded crossover points, one instance per
    // channel: each stage peels the low band off the remainder and passes
    // the rest (its complementary highpass) to the next stage, the same
    // cascading generalised to 6 points instead of Mentals 360 Stereo
    // Shaper's 2 -- each point is still a Linkwitz-Riley (LR4) crossover
    // (two identical Butterworth biquads in series per side), so the seven
    // bands still sum back to the original signal with a flat magnitude
    // response.
    //==========================================================================
    struct SevenBandSplitter
    {
        struct CrossoverStage
        {
            juce::dsp::IIR::Filter<float> lpA, lpB, hpA, hpB;
        };

        std::array<CrossoverStage, numCrossovers> stages;
        std::array<float, numCrossovers> lastFreqs { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };

        void reset()
        {
            for (auto& s : stages)
            {
                s.lpA.reset(); s.lpB.reset(); s.hpA.reset(); s.hpB.reset();
            }
        }

        void updateIfNeeded (double sampleRate, const std::array<float, numCrossovers>& freqs)
        {
            constexpr float q = 0.70710678f;
            for (int i = 0; i < numCrossovers; ++i)
            {
                if (freqs[(size_t) i] == lastFreqs[(size_t) i])
                    continue;
                lastFreqs[(size_t) i] = freqs[(size_t) i];

                auto lpC = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sampleRate, freqs[(size_t) i], q);
                auto hpC = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, freqs[(size_t) i], q);
                auto& s = stages[(size_t) i];
                s.lpA.coefficients = lpC; s.lpB.coefficients = lpC;
                s.hpA.coefficients = hpC; s.hpB.coefficients = hpC;
            }
        }

        std::array<float, numBands> process (float x) noexcept
        {
            std::array<float, numBands> out;
            float remainder = x;
            for (int i = 0; i < numCrossovers; ++i)
            {
                auto& s = stages[(size_t) i];
                out[(size_t) i] = s.lpB.processSample (s.lpA.processSample (remainder));
                remainder = s.hpB.processSample (s.hpA.processSample (remainder));
            }
            out[(size_t) numCrossovers] = remainder;
            return out;
        }
    };

    std::array<SevenBandSplitter, 2> multibandSplitters; // one per channel

    // Sidechain detector highpass -- single-band mode only.
    std::array<juce::dsp::IIR::Filter<float>, 2> sidechainHpf;
    float lastSidechainHpfFreq = -1.0f;

    // Runs one band's compressor engine for one channel's sample: advances
    // that channel-band's envelope follower(s) with the already-decided
    // detector level (which may be a stereo-linked, cross-channel value or
    // this channel's own, depending on Stereo Link), computes mode-adjusted
    // gain reduction, and returns the gain-reduced (not yet saturated/
    // made-up) sample.
    float processBandSample (Band& band, int channel, int mode, float x,
                              float thresholdDb, float ratio, float kneeDb,
                              float detectorLevelAbs, float& gainReductionDbOut) noexcept;

    std::atomic<float> currentGainReductionDb { 0.0f };
    std::array<std::atomic<float>, numBands> bandGainReductionDb {};
    std::atomic<float> inputPeakLinear { 0.0f };
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsCircuitCompAudioProcessor)
};
