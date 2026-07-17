#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"
#include <array>
#include <vector>
#include <mutex>

//==============================================================================
// Mentals Mastering Meter: a pure analysis plugin -- audio passes through
// completely unmodified (see processBlock()), only measured. Fills the gap
// every other Mentals plugin leaves for the actual final-polish stage:
// loudness (LUFS) and true-peak metering, the two numbers streaming
// platforms and broadcast specs actually gate on, which nothing else in
// this project reports.
//
// Loudness (ITU-R BS.1770-4 / EBU R128): each channel is K-weighted (a
// shelving filter approximating the outer/middle ear's response above
// ~2kHz, cascaded with a highpass approximating the ear's reduced
// sensitivity below ~100Hz), then measured in 100ms sub-blocks. Momentary
// (400ms) and Short-Term (3s) loudness are plain sliding averages of those
// sub-blocks; Integrated loudness and Loudness Range (LRA) additionally
// apply BS.1770's two-stage gating (an absolute -70 LUFS gate, then a
// relative gate 10 LU -- or for LRA, 20 LU -- below the ungated mean) so a
// quiet intro or a silent outro can't drag the overall reading down. All
// of this only needs to run on the message thread (see getIntegratedLufs()/
// getLoudnessRangeLu()): the audio thread just keeps appending one more
// 100ms sub-block measurement to a lock-free ring buffer.
//
// True Peak (ITU-R BS.1770 Annex 2): a sample-peak meter alone misses
// inter-sample peaks that a D/A converter or a lossy encoder's own
// reconstruction filter can produce between two adjacent samples that are
// each individually under 0dBFS. Oversampling 4x (juce::dsp::Oversampling)
// and scanning the upsampled signal catches those -- this analysis path
// never gets played back or written to the output buffer, so its filter
// latency doesn't need compensating or reporting anywhere.
//==============================================================================
class MentalsMasteringMeterAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsMasteringMeterAudioProcessor();
    ~MentalsMasteringMeterAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Mastering Meter"; }
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

    juce::AudioParameterChoice* targetPresetParam = nullptr;
    static constexpr std::array<float, 4> targetLufsValues { -14.0f, -16.0f, -14.0f, -23.0f }; // Spotify/Apple Music/YouTube/Broadcast
    float getTargetLufs() const noexcept { return targetLufsValues[(size_t) juce::jlimit (0, 3, targetPresetParam->getIndex())]; }

    // Clears all loudness/peak history -- the normal way to start measuring
    // a fresh playthrough on a mastering meter (integrated loudness/LRA are
    // measured "since last reset", not per-block).
    void resetMeters();

    //==========================================================================
    // Momentary/Short-Term read straight off the ring buffer (cheap, O(1)-ish
    // -- last 4 or last 30 entries). Integrated/LRA re-run BS.1770's full
    // gating pass over whatever history exists so far; both are safe to call
    // from the message thread only (a UI timer), never the audio thread.
    //==========================================================================
    float getMomentaryLufs() const noexcept;
    float getShortTermLufs() const noexcept;
    float getIntegratedLufs() const noexcept;
    float getLoudnessRangeLu() const noexcept;

    float getTruePeakDb() const noexcept { return juce::Decibels::gainToDecibels (truePeakLinear.load(), -100.0f); }
    float getSamplePeakDb() const noexcept { return juce::Decibels::gainToDecibels (samplePeakLinear.load(), -100.0f); }

    // For the editor's scrolling history graph: reads the last `count`
    // 100ms sub-block loudness values (already converted to LUFS, -100 for
    // silence/empty slots), oldest first.
    void copyRecentLoudnessHistory (std::vector<float>& outMomentary, std::vector<float>& outShortTerm, int count) const;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    double currentSampleRate = 44100.0;

    //==========================================================================
    // K-weighting: a high-shelf ("pre-filter", approximating head diffraction
    // above ~2kHz) cascaded with a highpass ("RLB", approximating reduced
    // low-frequency sensitivity) -- coefficients recomputed per sample rate
    // from BS.1770's analog-prototype design equations (not the commonly-
    // copied 48kHz-only coefficient table), so this measures correctly
    // whatever rate the host actually runs at.
    //==========================================================================
    struct KWeightingFilter
    {
        juce::dsp::IIR::Filter<float> shelf, highpass;

        void prepare (double sampleRate)
        {
            updateCoefficients (sampleRate);
            reset();
        }

        void reset()
        {
            shelf.reset();
            highpass.reset();
        }

        void updateCoefficients (double sampleRate)
        {
            {
                constexpr double f0 = 1681.9744509555319, gainDb = 3.99984385397, q = 0.7071752369554193;
                const double k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
                const double vh = std::pow (10.0, gainDb / 20.0);
                const double vb = std::pow (vh, 0.4996667741545416);
                const double a0 = 1.0 + k / q + k * k;
                juce::dsp::IIR::Coefficients<float>::Ptr coeffs = new juce::dsp::IIR::Coefficients<float> (
                    (float) ((vh + vb * k / q + k * k) / a0),
                    (float) (2.0 * (k * k - vh) / a0),
                    (float) ((vh - vb * k / q + k * k) / a0),
                    1.0f,
                    (float) (2.0 * (k * k - 1.0) / a0),
                    (float) ((1.0 - k / q + k * k) / a0));
                shelf.coefficients = coeffs;
            }
            {
                constexpr double f0 = 38.13547087613982, q = 0.5003270373238773;
                const double k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
                const double a0 = 1.0 + k / q + k * k;
                juce::dsp::IIR::Coefficients<float>::Ptr coeffs = new juce::dsp::IIR::Coefficients<float> (
                    (float) (1.0 / a0), (float) (-2.0 / a0), (float) (1.0 / a0),
                    1.0f,
                    (float) (2.0 * (k * k - 1.0) / a0),
                    (float) ((1.0 - k / q + k * k) / a0));
                highpass.coefficients = coeffs;
            }
        }

        float process (float x) noexcept { return highpass.processSample (shelf.processSample (x)); }
    };

    std::array<KWeightingFilter, 2> kWeighting;

    int samplesPerSubBlock = 4800; // 100ms, recomputed from sample rate in prepareToPlay
    int subBlockSampleCounter = 0;
    std::array<double, 2> subBlockSumSquares { 0.0, 0.0 }; // per channel -- BS.1770 averages each channel separately before summing

    // Lock-free ring buffer of 100ms sub-block mean squares (linear, not dB
    // yet) -- single audio-thread writer, message-thread-only readers.
    static constexpr int historyCapacity = 18000; // 30 minutes at 100ms resolution
    std::array<std::atomic<float>, historyCapacity> subBlockMeanSquare {};
    std::atomic<int> historyWritePos { 0 };
    std::atomic<int> historyCount { 0 }; // number of valid entries so far, capped at historyCapacity

    std::atomic<bool> resetRequested { false };

    //==========================================================================
    // True-peak (4x oversampled) and plain sample peak, each a standard
    // instant-attack/slow-release peak-hold like every other Mentals meter.
    //==========================================================================
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::atomic<float> truePeakLinear { 0.0f };
    std::atomic<float> samplePeakLinear { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsMasteringMeterAudioProcessor)
};
