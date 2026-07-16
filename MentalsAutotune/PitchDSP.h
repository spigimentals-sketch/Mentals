#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

//==============================================================================
// Pitch detection (autocorrelation) and scale-snapping logic, plus the
// grain-based pitch shifter, all kept in one place so the processor's
// real-time path and the editor's pitch-history display agree on exactly
// how a detected pitch maps to a corrected one.
//
// Honest scope note: this is a monophonic pitch corrector using normalised
// autocorrelation for detection and a simple two-tap crossfaded delay-line
// ("granular") pitch shifter -- not the proprietary, phase-vocoder/PSOLA-
// refined algorithm real commercial Auto-Tune uses, and it does not do
// formant correction (shifting pitch without formant correction can make
// large shifts sound "chipmunked"/unnatural -- a known, disclosed
// limitation, not a bug). It works well on a single melodic voice or
// instrument; it isn't designed for polyphonic material.
//==============================================================================
namespace PitchDSP
{
    //==========================================================================
    // Normalised autocorrelation pitch detection with parabolic interpolation
    // for sub-sample lag accuracy. windowedSamples should already have a
    // window function (e.g. Hann) applied. Returns false (leaving outFreqHz/
    // outConfidence untouched) if no candidate lag scores above a sane
    // minimum, which the caller should treat as "no pitch detected".
    //==========================================================================
    inline bool detectPitch (const float* windowedSamples, int windowSize, double sampleRate,
                              float minFreqHz, float maxFreqHz,
                              float& outFreqHz, float& outConfidence) noexcept
    {
        const int minLag = juce::jmax (1, (int) (sampleRate / maxFreqHz));
        const int maxLag = juce::jmin (windowSize - 1, (int) (sampleRate / minFreqHz));

        if (maxLag <= minLag)
            return false;

        int bestLag = -1;
        float bestScore = 0.0f;

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double crossSum = 0.0, energyA = 0.0, energyB = 0.0;
            const int overlap = windowSize - lag;

            for (int i = 0; i < overlap; ++i)
            {
                const float a = windowedSamples[i];
                const float b = windowedSamples[i + lag];
                crossSum += (double) a * (double) b;
                energyA  += (double) a * (double) a;
                energyB  += (double) b * (double) b;
            }

            const double denom = std::sqrt (energyA * energyB);
            const float score = denom > 1.0e-9 ? (float) (crossSum / denom) : 0.0f;

            if (score > bestScore)
            {
                bestScore = score;
                bestLag = lag;
            }
        }

        if (bestLag < 0 || bestScore < 0.05f)
            return false;

        // Parabolic interpolation around the best lag using its neighbours'
        // scores, for sub-sample accuracy (recomputing just those two).
        float interpolatedLag = (float) bestLag;
        if (bestLag > minLag && bestLag < maxLag)
        {
            auto scoreAt = [&] (int lag)
            {
                double crossSum = 0.0, energyA = 0.0, energyB = 0.0;
                const int overlap = windowSize - lag;
                for (int i = 0; i < overlap; ++i)
                {
                    const float a = windowedSamples[i];
                    const float b = windowedSamples[i + lag];
                    crossSum += (double) a * (double) b;
                    energyA  += (double) a * (double) a;
                    energyB  += (double) b * (double) b;
                }
                const double denom = std::sqrt (energyA * energyB);
                return denom > 1.0e-9 ? (float) (crossSum / denom) : 0.0f;
            };

            const float sPrev = scoreAt (bestLag - 1);
            const float sNext = scoreAt (bestLag + 1);
            const float denom = sPrev - 2.0f * bestScore + sNext;
            if (std::abs (denom) > 1.0e-9f)
                interpolatedLag += 0.5f * (sPrev - sNext) / denom;
        }

        outFreqHz = (float) (sampleRate / (double) interpolatedLag);
        outConfidence = bestScore;
        return true;
    }

    //==========================================================================
    // Scale masks (semitone offsets from the key's root that are valid
    // correction targets) and the nearest-in-scale-semitone search.
    //==========================================================================
    enum class Scale { Chromatic = 0, Major, Minor };

    inline std::array<bool, 12> getScaleMask (int scaleIndex) noexcept
    {
        switch (static_cast<Scale> (scaleIndex))
        {
            case Scale::Major:
                return { true, false, true, false, true, true, false, true, false, true, false, true };
            case Scale::Minor:
                return { true, false, true, true, false, true, false, true, true, false, true, false };
            case Scale::Chromatic:
            default:
                return { true, true, true, true, true, true, true, true, true, true, true, true };
        }
    }

    // semitoneFromRoot can be any real (fractional, any octave) semitone
    // distance from the selected key's root note. Scans two octaves either
    // side to guarantee finding the true nearest in-scale semitone.
    inline int nearestScaleSemitone (float semitoneFromRoot, const std::array<bool, 12>& scaleMask) noexcept
    {
        const int baseSemitone = (int) std::floor (semitoneFromRoot);
        float bestDistance = 1.0e9f;
        int best = baseSemitone;

        for (int candidate = baseSemitone - 12; candidate <= baseSemitone + 12; ++candidate)
        {
            const int mod = ((candidate % 12) + 12) % 12;
            if (! scaleMask[(size_t) mod])
                continue;

            const float distance = std::abs ((float) candidate - semitoneFromRoot);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = candidate;
            }
        }

        return best;
    }

    //==========================================================================
    // Two-tap crossfaded delay-line ("granular") pitch shifter. Each tap
    // tracks how far behind the write pointer it's reading from
    // (delayDistance); advancing it by (1 - ratio) each sample means a
    // ratio > 1 (shift up) continuously shrinks that distance and a
    // ratio < 1 (shift down) grows it, so it's wrapped into [0, grainSize)
    // and windowed with a Hann envelope whose gain is ~0 right at the wrap
    // points -- masking the otherwise-audible jump. The two taps are offset
    // by half a grain so their Hann gains sum to exactly 1 at every instant
    // (0.5-0.5cos(x) + 0.5-0.5cos(x+pi) == 1), giving continuous,
    // constant-amplitude coverage across the crossfade.
    //==========================================================================
    class PitchShifterChannel
    {
    public:
        void prepare (double sampleRate)
        {
            grainSizeSamples = juce::jmax (64.0f, (float) (0.04 * sampleRate)); // ~40ms grains
            const int bufferSize = (int) (grainSizeSamples * 2.0f) + 8;
            buffer.assign ((size_t) bufferSize, 0.0f);
            writePos = 0;
            tap1Distance = 0.0f;
            tap2Distance = grainSizeSamples * 0.5f;
        }

        float process (float input, float ratio) noexcept
        {
            const int bufSize = (int) buffer.size();
            buffer[(size_t) writePos] = input;

            const float gain1 = advanceTap (tap1Distance, ratio);
            const float gain2 = advanceTap (tap2Distance, ratio);

            const float sample1 = readInterpolated (tap1Distance, bufSize);
            const float sample2 = readInterpolated (tap2Distance, bufSize);

            writePos = (writePos + 1) % bufSize;
            return sample1 * gain1 + sample2 * gain2;
        }

    private:
        float advanceTap (float& delayDistance, float ratio) noexcept
        {
            delayDistance += (1.0f - ratio);

            while (delayDistance < 0.0f)               delayDistance += grainSizeSamples;
            while (delayDistance >= grainSizeSamples)   delayDistance -= grainSizeSamples;

            const float phase = delayDistance / grainSizeSamples;
            return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phase);
        }

        float readInterpolated (float delayDistance, int bufSize) const noexcept
        {
            float readPos = (float) writePos - delayDistance;
            while (readPos < 0.0f)            readPos += (float) bufSize;
            while (readPos >= (float) bufSize) readPos -= (float) bufSize;

            const int i0 = (int) readPos;
            const int i1 = (i0 + 1) % bufSize;
            const float frac = readPos - (float) i0;
            return buffer[(size_t) i0] * (1.0f - frac) + buffer[(size_t) i1] * frac;
        }

        std::vector<float> buffer;
        int writePos = 0;
        float grainSizeSamples = 1764.0f; // recomputed in prepare()
        float tap1Distance = 0.0f, tap2Distance = 0.0f;
    };
}
