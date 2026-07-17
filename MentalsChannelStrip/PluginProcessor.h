#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>
#include <cmath>

//==============================================================================
// Mentals Channel Strip: a single-unit take on the classic British console
// channel strip workflow (the module chain a channel of an SSL-style 4000/
// 9000-series desk uses on every input) -- Filters, then Dynamics
// (Compressor + Expander/Gate sharing one "Dyn In" switch), then a 4-band
// EQ (LF/LMF/HMF/HF), then Output trim, all in a fixed order that can be
// reordered exactly one way: Dynamics can swap to after the EQ instead of
// before it (dynamicsBeforeEqParam), the one signature routing option the
// real hardware exposes.
//
// Filters: HPF and LPF always shape the main signal; "Filter Split" (see
// filterSplitParam) additionally routes the filtered signal into the
// Dynamics section's detector (instead of the raw, unfiltered input), so
// heavy low end doesn't dominate gain-reduction decisions once it's
// already been filtered out of what's actually heard.
//
// Dynamics: Compressor (soft-knee, see MentalsUI::DynamicsDSP::
// computeOutputDb -- the same shared transfer function Mentals Compressor
// uses) and Expander/Gate (MentalsUI::DynamicsDSP::computeExpanderOutputDb,
// the same one Mentals Gate uses, with a Range floor) run back to back on
// one detector signal, both gated by a single "Dyn In" switch -- exactly
// the real hardware's single-button dynamics bypass, not two independent
// ones.
//
// EQ: LF and HF are shelf-by-default with a Bell switch (fixed Q per
// shape -- the real hardware's shelf slope wasn't independently
// adjustable either); LMF and HMF are full parametric bells with their
// own sweepable Frequency, Gain, and Q, the two "sweep the mud/presence
// out" bands a console EQ lives or dies on. All four share one EQ In
// switch.
//==============================================================================
class MentalsChannelStripAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsChannelStripAudioProcessor();
    ~MentalsChannelStripAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Channel Strip"; }
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

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    //==========================================================================
    // Presets: same two-tier, folder-per-category scheme as Mentals
    // Multimode EQ (see that plugin's PluginProcessor.h for the full
    // rationale) -- factory presets live under a category subfolder
    // (DRUMS/Kick Bus.xml, VOCALS/Male.xml, ...), a user's own saved preset
    // stays flat at the root.
    //==========================================================================
    juce::File getPresetsDirectory() const;
    juce::StringArray getAvailablePresetNames() const;
    void savePreset (const juce::String& presetName);
    void loadPreset (const juce::String& presetName);

    struct PresetCategory { juce::String name; juce::StringArray presetNames; };
    std::vector<PresetCategory> getFactoryPresetCategories() const;

    // Output level meter (see MentalsUI::LevelMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Gain-reduction meters (see MentalsUI::GainReductionMeterComponent) --
    // most-negative value seen during the last processed block, one each
    // for the Compressor and the Expander/Gate stages.
    float getCompGainReductionDb() const noexcept { return currentCompGainReductionDb.load(); }
    float getGateGainReductionDb() const noexcept { return currentGateGainReductionDb.load(); }

    // Shared by processBlock() and the editor's EQ curve display, so the
    // curve can never show a shape that disagrees with what's actually
    // applied. shape: 0 = Bell, 1 = Shelf (low), 2 = Shelf (high), matching
    // how each band's own fixed role calls it.
    enum class BandShape { Bell, LowShelf, HighShelf, HighPass, LowPass };
    static double getMagnitudeForFrequency (BandShape shape, double freqHz, double bandFreq, double q,
                                             double gainDb, double sampleRate) noexcept;

    juce::AudioParameterFloat* hpfFreqParam   = nullptr;
    juce::AudioParameterFloat* lpfFreqParam   = nullptr;
    juce::AudioParameterBool*  filterSplitParam = nullptr;
    juce::AudioParameterBool*  filtersInParam  = nullptr;

    juce::AudioParameterFloat* compThresholdParam = nullptr;
    juce::AudioParameterFloat* compRatioParam     = nullptr;
    juce::AudioParameterFloat* compAttackParam    = nullptr;
    juce::AudioParameterFloat* compReleaseParam   = nullptr;
    juce::AudioParameterFloat* compMakeupParam    = nullptr;

    juce::AudioParameterFloat* gateThresholdParam = nullptr;
    juce::AudioParameterFloat* gateRatioParam     = nullptr;
    juce::AudioParameterFloat* gateAttackParam    = nullptr;
    juce::AudioParameterFloat* gateReleaseParam   = nullptr;
    juce::AudioParameterFloat* gateRangeParam     = nullptr;

    juce::AudioParameterBool*  dynamicsInParam       = nullptr;
    juce::AudioParameterBool*  dynamicsBeforeEqParam = nullptr;

    juce::AudioParameterFloat* lfFreqParam  = nullptr;
    juce::AudioParameterFloat* lfGainParam  = nullptr;
    juce::AudioParameterBool*  lfBellParam  = nullptr;

    juce::AudioParameterFloat* lmfFreqParam = nullptr;
    juce::AudioParameterFloat* lmfGainParam = nullptr;
    juce::AudioParameterFloat* lmfQParam    = nullptr;

    juce::AudioParameterFloat* hmfFreqParam = nullptr;
    juce::AudioParameterFloat* hmfGainParam = nullptr;
    juce::AudioParameterFloat* hmfQParam    = nullptr;

    juce::AudioParameterFloat* hfFreqParam  = nullptr;
    juce::AudioParameterFloat* hfGainParam  = nullptr;
    juce::AudioParameterBool*  hfBellParam  = nullptr;

    juce::AudioParameterBool*  eqInParam     = nullptr;
    juce::AudioParameterFloat* outputGainParam = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void seedFactoryPresetsIfMissing();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);

    //==========================================================================
    // Plain single-stage biquad (Direct Form II Transposed) plus the RBJ
    // "Audio EQ Cookbook" design equations it needs -- the filters section
    // (HPF/LPF) and each EQ band all use this same shape; nothing here
    // cascades multiple stages the way Multimode EQ's steep-slope High
    // Pass/Low Pass do, since a console channel strip's filters are a
    // fixed, gentle 12dB/oct.
    //==========================================================================
    struct Coeffs
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

        static Coeffs bell (double sampleRate, float freqHz, float q, float gainDb) noexcept
        {
            freqHz = juce::jlimit (20.0f, (float) sampleRate * 0.49f, freqHz);
            q = juce::jmax (0.05f, q);
            const double w0 = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
            const double cosW0 = std::cos (w0), sinW0 = std::sin (w0);
            const double alpha = sinW0 / (2.0 * q);
            const double A = std::pow (10.0, gainDb / 40.0);
            const double b0 = 1.0 + alpha * A, b1 = -2.0 * cosW0, b2 = 1.0 - alpha * A;
            const double a0 = 1.0 + alpha / A, a1 = -2.0 * cosW0, a2 = 1.0 - alpha / A;
            return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
        }

        static Coeffs lowShelf (double sampleRate, float freqHz, float q, float gainDb) noexcept
        {
            freqHz = juce::jlimit (20.0f, (float) sampleRate * 0.49f, freqHz);
            q = juce::jmax (0.05f, q);
            const double w0 = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
            const double cosW0 = std::cos (w0), sinW0 = std::sin (w0);
            const double A = std::pow (10.0, gainDb / 40.0);
            const double beta = sinW0 * std::sqrt (A) / q;
            const double aM1 = A - 1.0, aP1 = A + 1.0, aM1cos = aM1 * cosW0;
            const double b0 = A * (aP1 - aM1cos + beta), b1 = 2.0 * A * (aM1 - aP1 * cosW0), b2 = A * (aP1 - aM1cos - beta);
            const double a0 = aP1 + aM1cos + beta, a1 = -2.0 * (aM1 + aP1 * cosW0), a2 = aP1 + aM1cos - beta;
            return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
        }

        static Coeffs highShelf (double sampleRate, float freqHz, float q, float gainDb) noexcept
        {
            freqHz = juce::jlimit (20.0f, (float) sampleRate * 0.49f, freqHz);
            q = juce::jmax (0.05f, q);
            const double w0 = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
            const double cosW0 = std::cos (w0), sinW0 = std::sin (w0);
            const double A = std::pow (10.0, gainDb / 40.0);
            const double beta = sinW0 * std::sqrt (A) / q;
            const double aM1 = A - 1.0, aP1 = A + 1.0, aM1cos = aM1 * cosW0;
            const double b0 = A * (aP1 + aM1cos + beta), b1 = -2.0 * A * (aM1 + aP1 * cosW0), b2 = A * (aP1 + aM1cos - beta);
            const double a0 = aP1 - aM1cos + beta, a1 = 2.0 * (aM1 - aP1 * cosW0), a2 = aP1 - aM1cos - beta;
            return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
        }

        static Coeffs highPass (double sampleRate, float freqHz, float q) noexcept
        {
            freqHz = juce::jlimit (20.0f, (float) sampleRate * 0.49f, freqHz);
            q = juce::jmax (0.05f, q);
            const double w0 = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
            const double cosW0 = std::cos (w0), sinW0 = std::sin (w0);
            const double alpha = sinW0 / (2.0 * q);
            const double b0 = (1.0 + cosW0) / 2.0, b1 = -(1.0 + cosW0), b2 = (1.0 + cosW0) / 2.0;
            const double a0 = 1.0 + alpha, a1 = -2.0 * cosW0, a2 = 1.0 - alpha;
            return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
        }

        static Coeffs lowPass (double sampleRate, float freqHz, float q) noexcept
        {
            freqHz = juce::jlimit (20.0f, (float) sampleRate * 0.49f, freqHz);
            q = juce::jmax (0.05f, q);
            const double w0 = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
            const double cosW0 = std::cos (w0), sinW0 = std::sin (w0);
            const double alpha = sinW0 / (2.0 * q);
            const double b0 = (1.0 - cosW0) / 2.0, b1 = 1.0 - cosW0, b2 = (1.0 - cosW0) / 2.0;
            const double a0 = 1.0 + alpha, a1 = -2.0 * cosW0, a2 = 1.0 - alpha;
            return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
        }

        double getMagnitudeForFrequency (double freqHz, double sampleRate) const noexcept
        {
            const double w = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
            const std::complex<double> j (0.0, 1.0);
            const std::complex<double> z = std::exp (-j * w), z2 = z * z;
            const std::complex<double> num = (double) b0 + (double) b1 * z + (double) b2 * z2;
            const std::complex<double> den = 1.0 + (double) a1 * z + (double) a2 * z2;
            return std::abs (num / den);
        }
    };

    struct BiquadState
    {
        float z1 = 0.0f, z2 = 0.0f;
        void reset() noexcept { z1 = z2 = 0.0f; }
        float process (float x, const Coeffs& c) noexcept
        {
            const float y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };

    static constexpr int maxChannels = 2;

    std::array<BiquadState, maxChannels> hpfState, lpfState;
    std::array<BiquadState, maxChannels> lfState, lmfState, hmfState, hfState;

    // Smoothed so a knob move mid-playback glides rather than clicking --
    // coefficients are recomputed from these every sample, same reasoning
    // as Multimode EQ's own per-band smoothing.
    juce::SmoothedValue<float> smoothedHpfFreq, smoothedLpfFreq;
    juce::SmoothedValue<float> smoothedLfFreq, smoothedLfGain;
    juce::SmoothedValue<float> smoothedLmfFreq, smoothedLmfGain, smoothedLmfQ;
    juce::SmoothedValue<float> smoothedHmfFreq, smoothedHmfGain, smoothedHmfQ;
    juce::SmoothedValue<float> smoothedHfFreq, smoothedHfGain;

    MentalsUI::DynamicsDSP::EnvelopeFollower compEnvelopeFollower, gateEnvelopeFollower;

    double currentSampleRate = 44100.0;
    static constexpr float compKneeDb = 6.0f;
    static constexpr float gateKneeDb = 6.0f;
    static constexpr float shelfQ = 0.7071f; // standard smooth (Butterworth-like) shelf transition

    std::atomic<float> currentCompGainReductionDb { 0.0f };
    std::atomic<float> currentGateGainReductionDb { 0.0f };
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsChannelStripAudioProcessor)
};
