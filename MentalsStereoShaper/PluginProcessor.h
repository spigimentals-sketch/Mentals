#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include "MixRegistry.h"
#include "PlacementModel.h"
#include <array>
#include <atomic>

//==============================================================================
// Mentals 360 Stereo Shaper: mid/side width shaping, 360-degree stereo-field
// rotation, frequency-dependent width (3-band), signal-dynamics-driven width,
// and a correlation-based mono-safety net -- plus a goniometer + correlation
// meter fed straight from the actual output samples (not illustrative, unlike
// e.g. Mentals Vox Choir's choir-spread graph).
//
// Signal flow per sample:
//   L/R -> Mid/Side encode -> Side split into Low/Mid/High via two cascaded
//   Linkwitz-Riley (LR4) crossovers -> each band scaled by its own Width knob
//   -> recombined -> scaled by the global Width, the dynamics-follower
//   modulation, and (if Phase Align is on) a correlation-safety gain computed
//   from the *previous* block's output correlation -> Mid gain trim applied
//   to Mid -> Mid/Side decode -> stereo-field rotation (manual angle plus an
//   optional continuously-running auto-rotate LFO) -> dry/wet Mix.
//
// AI Placement (see PlacementModel.h/MixRegistry.h): every instance
// publishes a slowly-smoothed fingerprint of its own raw input (band-energy
// ratios, crest factor, RMS, L/R correlation) into a cross-process shared
// registry every block, and can read every OTHER instance's fingerprint on
// demand. Pressing the button runs a RandomForest (trained on real
// MUSDB18HQ multitrack songs -- see Models/README.md) on this track's own
// fingerprint plus that aggregate context, suggesting a Rotation/Width that
// accounts for what else is already occupying the stereo field.
//==============================================================================
class MentalsStereoShaperAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsStereoShaperAudioProcessor();
    ~MentalsStereoShaperAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals 360 Stereo Shaper"; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals 360 Stereo Shaper" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // AI Placement: a trained model (see PlacementModel.h), unlike Mentals
    // Autotune's single-track AI Assist, this one is mix-aware -- every
    // Stereo Shaper instance publishes its own track's audio fingerprint
    // into a shared cross-process registry every block (see MixRegistry.h)
    // and reads every OTHER instance's published fingerprint back, so the
    // model sees not just this track but what's already occupying the
    // stereo field elsewhere in the session.
    //
    // The model itself, however, empirically learned ~zero sensitivity to
    // which SIDE (left/right) is more occupied specifically -- confirmed
    // against 2,808 real MUSDB18HQ examples, not just a hunch (see
    // Models/README.md) -- so runAiPlacement() layers an explicit,
    // deterministic nudge away from the more-crowded side on top of the
    // model's own rotation suggestion, to make sure "mix-aware" is actually
    // true in practice rather than just in the feature's name. Safe to call
    // from the message thread at any time.
    void runAiPlacement();

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Read by the editor's analyzer (goniometer + correlation meter). The
    // goniometer buffer is a plain lock-free ring: one audio-thread writer,
    // one UI-thread reader, each element a self-contained atomic<float> so a
    // torn read can only ever show a slightly-stale point, never a crash.
    static constexpr int goniometerSize = 2048;
    const std::atomic<float>& getGoniometerL (int i) const noexcept { return goniometerL[(size_t) i]; }
    const std::atomic<float>& getGoniometerR (int i) const noexcept { return goniometerR[(size_t) i]; }
    int getGoniometerWritePos() const noexcept { return goniometerWritePos.load (std::memory_order_relaxed); }
    float getCorrelation() const noexcept { return smoothedCorrelation.load(); }

    // Every OTHER currently-active Stereo Shaper instance's last-published
    // placement, for the 3D stage to draw a whole-session view (see
    // MixRegistry::getOthersSnapshot()). Not real-time safe -- only ever
    // called from the editor's display timer.
    std::vector<MixRegistry::OtherInstance> getOtherInstancesSnapshot() const { return mixRegistry.getOthersSnapshot(); }

    juce::AudioParameterFloat* widthParam          = nullptr;
    juce::AudioParameterFloat* midGainParam        = nullptr;
    juce::AudioParameterFloat* rotationParam       = nullptr;
    juce::AudioParameterFloat* autoRotateRateParam = nullptr;
    juce::AudioParameterFloat* dynamicAmountParam  = nullptr;
    juce::AudioParameterFloat* lowFreqParam        = nullptr;
    juce::AudioParameterFloat* highFreqParam       = nullptr;
    juce::AudioParameterFloat* lowWidthParam       = nullptr;
    juce::AudioParameterFloat* midWidthParam       = nullptr;
    juce::AudioParameterFloat* highWidthParam      = nullptr;
    juce::AudioParameterBool*  phaseAlignParam     = nullptr;
    juce::AudioParameterFloat* mixParam            = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void seedFactoryPresetsIfMissing();

    double currentSampleRate = 44100.0;

    //==========================================================================
    // Two cascaded (LR4, -24dB/oct) crossover points split the Side channel
    // into Low/Mid/High. LR4 is built from two identical Butterworth
    // biquads in series per side, chosen because the three bands sum back to
    // the original signal with a flat magnitude response and no extra phase
    // flip needed -- unlike a plain LR2 crossover.
    //==========================================================================
    struct ThreeBandSplitter
    {
        juce::dsp::IIR::Filter<float> lp1a, lp1b, hp1a, hp1b;
        juce::dsp::IIR::Filter<float> lp2a, lp2b, hp2a, hp2b;
        float lastFreq1 = -1.0f, lastFreq2 = -1.0f;

        void reset()
        {
            lp1a.reset(); lp1b.reset(); hp1a.reset(); hp1b.reset();
            lp2a.reset(); lp2b.reset(); hp2a.reset(); hp2b.reset();
        }

        void updateIfNeeded (double sampleRate, float freq1, float freq2)
        {
            if (freq1 == lastFreq1 && freq2 == lastFreq2)
                return;
            lastFreq1 = freq1; lastFreq2 = freq2;

            constexpr float butterworthQ = 0.70710678f;
            auto lpCoeffs1 = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sampleRate, freq1, butterworthQ);
            auto hpCoeffs1 = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, freq1, butterworthQ);
            auto lpCoeffs2 = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sampleRate, freq2, butterworthQ);
            auto hpCoeffs2 = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, freq2, butterworthQ);

            lp1a.coefficients = lpCoeffs1; lp1b.coefficients = lpCoeffs1;
            hp1a.coefficients = hpCoeffs1; hp1b.coefficients = hpCoeffs1;
            lp2a.coefficients = lpCoeffs2; lp2b.coefficients = lpCoeffs2;
            hp2a.coefficients = hpCoeffs2; hp2b.coefficients = hpCoeffs2;
        }

        // Returns { low, mid, high }.
        std::array<float, 3> process (float x) noexcept
        {
            const float low  = lp1b.processSample (lp1a.processSample (x));
            const float highPassed1 = hp1b.processSample (hp1a.processSample (x));
            const float mid  = lp2b.processSample (lp2a.processSample (highPassed1));
            const float high = hp2b.processSample (hp2a.processSample (highPassed1));
            return { low, mid, high };
        }
    };

    ThreeBandSplitter sideSplitter;

    // A second splitter, fixed at the same 150Hz/4000Hz points the Python
    // training pipeline used (independent of the user-adjustable Low/High
    // Freq width-shaping knobs above), analysing the raw MONO input so the
    // published "own fingerprint" matches what the model was trained on.
    ThreeBandSplitter fingerprintSplitter;
    static constexpr float fingerprintLowFreq = 150.0f, fingerprintHighFreq = 4000.0f;

    float rotationAngleRad = 0.0f; // running phase for the auto-rotate LFO, persists across blocks
    float envelopeFollowerState = 0.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    float phaseSafetyScale = 1.0f; // applied to Side this block, derived from last block's correlation

    std::atomic<float> smoothedCorrelation { 1.0f };

    // Slowly-smoothed (multi-second) "own fingerprint" state, published to
    // mixRegistry every block and fed to the AI Placement model -- smoothed
    // much more heavily than the analyzer/meter figures above since this is
    // meant to characterise the track's overall character (matching the
    // 8-second analysis windows the model was trained on), not respond
    // quickly like a meter.
    float fingerprintLowEnergy = 0.0f, fingerprintMidEnergy = 0.0f, fingerprintHighEnergy = 0.0f;
    float fingerprintPeak = 0.0f, fingerprintRmsSquared = 0.0f, fingerprintCorrelation = 0.0f;

    // Mirrors what's published to mixRegistry each block, so runAiPlacement()
    // (called from the message thread, when the user presses the button) can
    // read the latest values without recomputing anything.
    std::array<std::atomic<float>, MixRegistry::numOwnFeatures> ownFeatureAtomics {};

    MixRegistry mixRegistry;
    PlacementModel placementModel;

    std::array<std::atomic<float>, goniometerSize> goniometerL, goniometerR;
    std::atomic<int> goniometerWritePos { 0 };

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsStereoShaperAudioProcessor)
};
