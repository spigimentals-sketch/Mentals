#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

//==============================================================================
// Shared ITU-R BS.1770-4 / EBU R128 loudness core: K-weighting, the two-stage
// gating algorithm behind Integrated loudness and Loudness Range, and a
// real-time-safe live meter built on both -- used by Mentals Mastering
// Meter's own readouts AND Mentals Suite's Master Assistant (which needs the
// exact same measurement to compare a live mix against a loaded reference
// file). Kept in one place so there is only ever one BS.1770 implementation
// in this project to get right, not two that could quietly drift apart.
//==============================================================================
namespace MentalsUI
{
    namespace LoudnessDSP
    {
        constexpr double absoluteGateLufs = -70.0;
        constexpr double integratedRelativeGateLu = 10.0;
        constexpr double lraRelativeGateLu = 20.0;
        constexpr double lraLowerPercentile = 0.10;
        constexpr double lraUpperPercentile = 0.95;

        inline double meanSquareToLufs (double meanSquare) noexcept
        {
            return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare) : -100.0;
        }

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

        // Runs BS.1770's two-stage gating over a list of 400ms-equivalent block
        // mean squares (each already averaged from N consecutive 100ms
        // sub-blocks -- caller decides N: 4 for Integrated's 400ms blocks, 30
        // for LRA's 3s short-term blocks). Returns the gated mean loudness in
        // LUFS, or -100 if nothing survives the absolute gate.
        inline double runTwoStageGating (const std::vector<double>& blockMeanSquares, double relativeGateLu) noexcept
        {
            std::vector<double> absoluteGated;
            absoluteGated.reserve (blockMeanSquares.size());
            for (double ms : blockMeanSquares)
                if (meanSquareToLufs (ms) >= absoluteGateLufs)
                    absoluteGated.push_back (ms);
            if (absoluteGated.empty())
                return -100.0;

            double ungatedSum = 0.0;
            for (double ms : absoluteGated) ungatedSum += ms;
            const double ungatedLoudness = meanSquareToLufs (ungatedSum / (double) absoluteGated.size());

            double relativeSum = 0.0;
            int relativeCount = 0;
            for (double ms : absoluteGated)
            {
                if (meanSquareToLufs (ms) >= ungatedLoudness - relativeGateLu)
                {
                    relativeSum += ms;
                    ++relativeCount;
                }
            }
            return relativeCount > 0 ? meanSquareToLufs (relativeSum / (double) relativeCount) : ungatedLoudness;
        }

        //==========================================================================
        // Real-time-safe live LUFS meter: processSample() runs on the audio
        // thread (K-weights each channel and accumulates 100ms sub-blocks into a
        // lock-free ring buffer); every other method re-derives its answer from
        // that ring buffer and is safe to call from the message thread only
        // (a UI timer, or Master Assistant's analysis step), never the audio
        // thread -- Integrated/LRA's gating pass is O(history size), cheap at
        // UI polling rates but not something to run per-sample.
        //==========================================================================
        class LufsMeter
        {
        public:
            // Recomputes filter coefficients for a (possibly new) sample rate
            // and resets the in-flight sub-block accumulation, but
            // deliberately leaves loudness history untouched: prepareToPlay
            // can fire on a benign engine restart mid-session, and wiping a
            // mastering engineer's integrated-loudness measurement because
            // the audio device hiccuped would be a bad surprise. Call
            // resetHistory() too for a genuinely fresh start (e.g. the meter's
            // own Reset button, or the start of a new reference-matching
            // capture).
            void prepare (double sampleRate)
            {
                samplesPerSubBlock = juce::jmax (1, (int) std::round (0.1 * sampleRate));
                for (auto& kw : kWeighting)
                    kw.prepare (sampleRate);
                subBlockSampleCounter = 0;
                subBlockSumSquares.fill (0.0);
            }

            void resetHistory()
            {
                historyCount.store (0);
                historyWritePos.store (0);
            }

            void reset()
            {
                subBlockSampleCounter = 0;
                subBlockSumSquares.fill (0.0);
                for (auto& kw : kWeighting)
                    kw.reset();
                resetHistory();
            }

            // Call once per sample, per block, from the audio thread.
            void processSample (const float* channelSamples, int numChannels) noexcept
            {
                numChannels = juce::jmin (numChannels, 2);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float weighted = kWeighting[(size_t) ch].process (channelSamples[ch]);
                    subBlockSumSquares[(size_t) ch] += (double) weighted * (double) weighted;
                }

                if (++subBlockSampleCounter >= samplesPerSubBlock)
                {
                    double combined = 0.0;
                    for (int ch = 0; ch < numChannels; ++ch)
                        combined += subBlockSumSquares[(size_t) ch] / (double) subBlockSampleCounter;

                    const int pos = historyWritePos.load (std::memory_order_relaxed);
                    subBlockMeanSquare[(size_t) pos].store ((float) combined, std::memory_order_relaxed);
                    historyWritePos.store ((pos + 1) % historyCapacity, std::memory_order_relaxed);
                    historyCount.store (juce::jmin (historyCapacity, historyCount.load (std::memory_order_relaxed) + 1), std::memory_order_relaxed);

                    subBlockSampleCounter = 0;
                    subBlockSumSquares.fill (0.0);
                }
            }

            float getMomentaryLufs() const noexcept { return windowedLufs (4); }
            float getShortTermLufs() const noexcept { return windowedLufs (30); }

            float getIntegratedLufs() const noexcept
            {
                auto subBlocks = copyHistoryOldestFirst();
                if (subBlocks.size() < 4)
                    return -100.0f;

                std::vector<double> blockMeanSquares;
                blockMeanSquares.reserve (subBlocks.size());
                for (size_t i = 0; i + 4 <= subBlocks.size(); ++i)
                {
                    double sum = 0.0;
                    for (int k = 0; k < 4; ++k) sum += subBlocks[i + (size_t) k];
                    blockMeanSquares.push_back (sum / 4.0);
                }
                return (float) runTwoStageGating (blockMeanSquares, integratedRelativeGateLu);
            }

            float getLoudnessRangeLu() const noexcept
            {
                auto subBlocks = copyHistoryOldestFirst();
                if (subBlocks.size() < 30)
                    return 0.0f;

                std::vector<double> blockMeanSquares;
                blockMeanSquares.reserve (subBlocks.size());
                for (size_t i = 0; i + 30 <= subBlocks.size(); ++i)
                {
                    double sum = 0.0;
                    for (int k = 0; k < 30; ++k) sum += subBlocks[i + (size_t) k];
                    blockMeanSquares.push_back (sum / 30.0);
                }

                std::vector<double> absoluteGated;
                for (double ms : blockMeanSquares)
                    if (meanSquareToLufs (ms) >= absoluteGateLufs)
                        absoluteGated.push_back (ms);
                if (absoluteGated.empty())
                    return 0.0f;

                double ungatedSum = 0.0;
                for (double ms : absoluteGated) ungatedSum += ms;
                const double ungatedLoudness = meanSquareToLufs (ungatedSum / (double) absoluteGated.size());

                std::vector<double> relativelyGatedLoudness;
                for (double ms : absoluteGated)
                {
                    const double loudness = meanSquareToLufs (ms);
                    if (loudness >= ungatedLoudness - lraRelativeGateLu)
                        relativelyGatedLoudness.push_back (loudness);
                }
                if (relativelyGatedLoudness.size() < 2)
                    return 0.0f;

                std::sort (relativelyGatedLoudness.begin(), relativelyGatedLoudness.end());
                const auto pick = [&] (double percentile)
                {
                    const double pos = percentile * (double) (relativelyGatedLoudness.size() - 1);
                    const size_t lower = (size_t) std::floor (pos);
                    const size_t upper = juce::jmin (relativelyGatedLoudness.size() - 1, lower + 1);
                    const double frac = pos - (double) lower;
                    return relativelyGatedLoudness[lower] * (1.0 - frac) + relativelyGatedLoudness[upper] * frac;
                };

                return (float) (pick (lraUpperPercentile) - pick (lraLowerPercentile));
            }

            void copyRecentHistory (std::vector<float>& outMomentary, std::vector<float>& outShortTerm, int count) const
            {
                auto subBlocks = copyHistoryOldestFirst();
                const int available = (int) subBlocks.size();
                const int n = juce::jmin (count, available);
                outMomentary.assign ((size_t) count, -100.0f);
                outShortTerm.assign ((size_t) count, -100.0f);
                if (n == 0)
                    return;

                for (int i = 0; i < n; ++i)
                {
                    const int posFromEnd = n - 1 - i;
                    const int endIdx = available - 1 - posFromEnd;

                    double momSum = 0.0; int momCount = 0;
                    for (int k = juce::jmax (0, endIdx - 3); k <= endIdx; ++k) { momSum += subBlocks[(size_t) k]; ++momCount; }

                    double stSum = 0.0; int stCount = 0;
                    for (int k = juce::jmax (0, endIdx - 29); k <= endIdx; ++k) { stSum += subBlocks[(size_t) k]; ++stCount; }

                    const int outIdx = count - n + i;
                    outMomentary[(size_t) outIdx] = (float) meanSquareToLufs (momSum / (double) juce::jmax (1, momCount));
                    outShortTerm[(size_t) outIdx] = (float) meanSquareToLufs (stSum / (double) juce::jmax (1, stCount));
                }
            }

            int getHistoryCount() const noexcept { return historyCount.load(); }

        private:
            float windowedLufs (int windowSubBlocks) const noexcept
            {
                const int count = juce::jmin (historyCount.load(), windowSubBlocks);
                if (count == 0)
                    return -100.0f;

                const int writePos = historyWritePos.load();
                double sum = 0.0;
                for (int i = 0; i < count; ++i)
                {
                    const int idx = ((writePos - 1 - i) % historyCapacity + historyCapacity) % historyCapacity;
                    sum += subBlockMeanSquare[(size_t) idx].load (std::memory_order_relaxed);
                }
                return (float) meanSquareToLufs (sum / (double) count);
            }

            std::vector<double> copyHistoryOldestFirst() const
            {
                const int count = historyCount.load();
                const int writePos = historyWritePos.load();
                std::vector<double> out ((size_t) count);
                for (int i = 0; i < count; ++i)
                {
                    const int idx = ((writePos - 1 - i) % historyCapacity + historyCapacity) % historyCapacity;
                    out[(size_t) (count - 1 - i)] = subBlockMeanSquare[(size_t) idx].load (std::memory_order_relaxed);
                }
                return out;
            }

            std::array<KWeightingFilter, 2> kWeighting;
            int samplesPerSubBlock = 4800;
            int subBlockSampleCounter = 0;
            std::array<double, 2> subBlockSumSquares { 0.0, 0.0 };

            static constexpr int historyCapacity = 18000; // 30 minutes at 100ms resolution
            std::array<std::atomic<float>, historyCapacity> subBlockMeanSquare {};
            std::atomic<int> historyWritePos { 0 };
            std::atomic<int> historyCount { 0 };
        };

        //==========================================================================
        // One-shot OFFLINE analysis of a whole audio file's integrated loudness
        // -- the same K-weighting and gating as LufsMeter, just fed by an
        // AudioFormatReader instead of live audio. Returns false if the file
        // couldn't be read. Message-thread only (does file I/O); mirrors the
        // same block-by-block reading approach Mentals Multimode EQ's EQ Match
        // uses for its own reference-file spectrum analysis.
        //==========================================================================
        inline bool analyseFileIntegratedLufs (const juce::File& file, double sampleRateForFilters, float& outIntegratedLufs) noexcept
        {
            juce::AudioFormatManager formatManager;
            formatManager.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
            if (reader == nullptr)
                return false;

            std::array<KWeightingFilter, 2> kWeighting;
            for (auto& kw : kWeighting)
                kw.prepare (reader->sampleRate > 0 ? reader->sampleRate : sampleRateForFilters);

            const int samplesPerSubBlock = juce::jmax (1, (int) std::round (0.1 * reader->sampleRate));
            const int numChannels = juce::jmin (2, (int) reader->numChannels);

            std::vector<double> subBlockMeanSquares;
            std::array<double, 2> subBlockSumSquares { 0.0, 0.0 };
            int subBlockSampleCounter = 0;

            constexpr int chunkSize = 8192;
            juce::AudioBuffer<float> chunk (juce::jmax (1, numChannels), chunkSize);

            juce::int64 position = 0;
            while (position < reader->lengthInSamples)
            {
                const int samplesThisChunk = (int) juce::jmin ((juce::int64) chunkSize, reader->lengthInSamples - position);
                chunk.clear();
                reader->read (&chunk, 0, samplesThisChunk, position, true, true);

                for (int n = 0; n < samplesThisChunk; ++n)
                {
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        const float weighted = kWeighting[(size_t) ch].process (chunk.getReadPointer (ch)[n]);
                        subBlockSumSquares[(size_t) ch] += (double) weighted * (double) weighted;
                    }

                    if (++subBlockSampleCounter >= samplesPerSubBlock)
                    {
                        double combined = 0.0;
                        for (int ch = 0; ch < numChannels; ++ch)
                            combined += subBlockSumSquares[(size_t) ch] / (double) subBlockSampleCounter;
                        subBlockMeanSquares.push_back (combined);

                        subBlockSampleCounter = 0;
                        subBlockSumSquares.fill (0.0);
                    }
                }

                position += samplesThisChunk;
            }

            if (subBlockMeanSquares.size() < 4)
            {
                outIntegratedLufs = -100.0f;
                return true;
            }

            std::vector<double> blockMeanSquares;
            blockMeanSquares.reserve (subBlockMeanSquares.size());
            for (size_t i = 0; i + 4 <= subBlockMeanSquares.size(); ++i)
            {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k) sum += subBlockMeanSquares[i + (size_t) k];
                blockMeanSquares.push_back (sum / 4.0);
            }

            outIntegratedLufs = (float) runTwoStageGating (blockMeanSquares, integratedRelativeGateLu);
            return true;
        }
    }
}
