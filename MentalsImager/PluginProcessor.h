#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>
#include <atomic>

//==============================================================================
// Mentals Imager: a direct, Ozone-Imager-style multiband stereo widener --
// narrower in scope than Mentals 360 Stereo Shaper (no rotation, no dynamics-
// driven width, no cross-instance AI Placement), the same trade Doubler makes
// against Chorus/Vox Choir: pick one job and do exactly that job the way the
// well-known reference tool does it.
//
// Bands (1-4, selectable) split the Side (L-R) signal only via cascaded
// Linkwitz-Riley (LR4) crossovers, same technique/filter topology as Stereo
// Shaper's ThreeBandSplitter, generalised to up to 3 crossover points here.
// Each band gets its own independent Width knob (0-200%, 100% = unprocessed);
// Mid (L+R) is left full-band and untouched, since only the difference signal
// needs per-band treatment for width to change -- narrowing/widening the sum
// signal by band is an EQ job, not an imaging one.
//
// Visual feedback is a classic 2D vectorscope/goniometer (ImagerGoniometer-
// Component in the editor) plus a correlation meter, both fed from the
// REAL final output samples via a lock-free ring buffer -- not illustrative,
// the same guarantee Stereo Shaper's analyzer makes.
//==============================================================================
class MentalsImagerAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsImagerAudioProcessor();
    ~MentalsImagerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Imager"; }
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
    MentalsUI::PresetManager presetManager { apvts, "Mentals Imager" };

    void resetToDefault()
    {
        for (auto* param : getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    // Output level meter (see MentalsUI::AnalogVUMeterComponent).
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    // Read by the editor's goniometer + correlation meter. Same lock-free
    // single-writer/single-reader ring buffer design as Stereo Shaper: one
    // atomic<float> per slot, so a torn read can only ever show a slightly
    // stale point, never a crash.
    static constexpr int goniometerSize = 2048;
    const std::atomic<float>& getGoniometerL (int i) const noexcept { return goniometerL[(size_t) i]; }
    const std::atomic<float>& getGoniometerR (int i) const noexcept { return goniometerR[(size_t) i]; }
    int getGoniometerWritePos() const noexcept { return goniometerWritePos.load (std::memory_order_relaxed); }
    float getCorrelation() const noexcept { return smoothedCorrelation.load(); }

    static constexpr int maxBands = 4;
    static const int bandCountChoices[4]; // { 1, 2, 3, 4 }

    juce::AudioParameterChoice* bandsParam      = nullptr;
    juce::AudioParameterFloat*  crossover1Param  = nullptr;
    juce::AudioParameterFloat*  crossover2Param  = nullptr;
    juce::AudioParameterFloat*  crossover3Param  = nullptr;
    juce::AudioParameterFloat*  width1Param      = nullptr;
    juce::AudioParameterFloat*  width2Param      = nullptr;
    juce::AudioParameterFloat*  width3Param      = nullptr;
    juce::AudioParameterFloat*  width4Param      = nullptr;
    juce::AudioParameterFloat*  mixParam         = nullptr;

    // When off, the final output is summed to mono (both channels made
    // identical) regardless of every per-band Width knob above -- same
    // mono-compatibility override every Mentals plugin has.
    juce::AudioParameterBool* stereoParam = nullptr;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void seedFactoryPresetsIfMissing();

    double currentSampleRate = 44100.0;

    //==========================================================================
    // Up to 3 cascaded LR4 (-24dB/oct) crossovers splitting the Side channel
    // into up to 4 bands -- see class comment. Only as many stages as the
    // current band count needs are ever fed real coefficients; the rest sit
    // idle at zero.
    //==========================================================================
    struct FourBandSplitter
    {
        juce::dsp::IIR::Filter<float> lp1a, lp1b, hp1a, hp1b;
        juce::dsp::IIR::Filter<float> lp2a, lp2b, hp2a, hp2b;
        juce::dsp::IIR::Filter<float> lp3a, lp3b, hp3a, hp3b;
        float lastFreq1 = -1.0f, lastFreq2 = -1.0f, lastFreq3 = -1.0f;
        int lastNumBands = -1;

        void reset()
        {
            lp1a.reset(); lp1b.reset(); hp1a.reset(); hp1b.reset();
            lp2a.reset(); lp2b.reset(); hp2a.reset(); hp2b.reset();
            lp3a.reset(); lp3b.reset(); hp3a.reset(); hp3b.reset();
        }

        void updateIfNeeded (double sampleRate, int numBands, float freq1, float freq2, float freq3)
        {
            if (numBands == lastNumBands && freq1 == lastFreq1 && freq2 == lastFreq2 && freq3 == lastFreq3)
                return;
            lastNumBands = numBands; lastFreq1 = freq1; lastFreq2 = freq2; lastFreq3 = freq3;

            constexpr float butterworthQ = 0.70710678f;

            if (numBands >= 2)
            {
                auto lpC = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sampleRate, freq1, butterworthQ);
                auto hpC = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, freq1, butterworthQ);
                lp1a.coefficients = lpC; lp1b.coefficients = lpC;
                hp1a.coefficients = hpC; hp1b.coefficients = hpC;
            }
            if (numBands >= 3)
            {
                auto lpC = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sampleRate, freq2, butterworthQ);
                auto hpC = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, freq2, butterworthQ);
                lp2a.coefficients = lpC; lp2b.coefficients = lpC;
                hp2a.coefficients = hpC; hp2b.coefficients = hpC;
            }
            if (numBands >= 4)
            {
                auto lpC = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sampleRate, freq3, butterworthQ);
                auto hpC = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, freq3, butterworthQ);
                lp3a.coefficients = lpC; lp3b.coefficients = lpC;
                hp3a.coefficients = hpC; hp3b.coefficients = hpC;
            }
        }

        // Returns up to 4 bands; entries beyond numBands are left at 0.
        std::array<float, 4> process (float x, int numBands) noexcept
        {
            std::array<float, 4> out { 0.0f, 0.0f, 0.0f, 0.0f };

            if (numBands <= 1)
            {
                out[0] = x;
                return out;
            }

            const float low   = lp1b.processSample (lp1a.processSample (x));
            const float rest1 = hp1b.processSample (hp1a.processSample (x));

            if (numBands == 2)
            {
                out[0] = low; out[1] = rest1;
                return out;
            }

            const float mid   = lp2b.processSample (lp2a.processSample (rest1));
            const float rest2 = hp2b.processSample (hp2a.processSample (rest1));

            if (numBands == 3)
            {
                out[0] = low; out[1] = mid; out[2] = rest2;
                return out;
            }

            const float midHigh = lp3b.processSample (lp3a.processSample (rest2));
            const float high    = hp3b.processSample (hp3a.processSample (rest2));
            out[0] = low; out[1] = mid; out[2] = midHigh; out[3] = high;
            return out;
        }
    };

    FourBandSplitter sideSplitter;
    int lastNumBandsUsed = -1; // detects band-count changes so the splitter can reset (avoids a stale-filter click)

    std::array<std::atomic<float>, goniometerSize> goniometerL, goniometerR;
    std::atomic<int> goniometerWritePos { 0 };
    std::atomic<float> smoothedCorrelation { 1.0f };

    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsImagerAudioProcessor)
};
