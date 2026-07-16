#pragma once

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

//==============================================================================
// Pitch detection (autocorrelation), microtonal scale-snapping, the grain-
// based pitch shifter, and the formant corrector, all kept in one place so
// the processor's real-time path and the editor's pitch-history display
// agree on exactly how a detected pitch maps to a corrected one.
//
// Honest scope note: this is a monophonic pitch corrector using normalised
// autocorrelation for detection and a simple two-tap crossfaded delay-line
// ("granular") pitch shifter -- not the proprietary, phase-vocoder/PSOLA-
// refined algorithm real commercial Auto-Tune uses. Formant preservation
// (see FormantCorrector below) uses spectral-envelope smoothing rather than
// cepstral liftering/LPC -- simpler, lower risk, less precise. It works well
// on a single melodic voice or instrument; it isn't designed for polyphonic
// material.
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
    // Scales/tunings, expressed as a list of cent-offsets from the root
    // (0 = root, 1200 = an octave above) rather than a fixed 12-tone-equal-
    // temperament semitone mask -- this is what lets non-12-TET systems
    // (quarter-tone maqam scales, just-intonation raga gamuts) be
    // represented at all, alongside the ordinary 12-TET scales.
    //
    // Honest scope note: "Maqam Rast"/"Maqam Bayati"/"Raga Bhairav" here are
    // fixed-pitch-set approximations of those systems for the purpose of a
    // pitch-correction target grid, using commonly-cited representative
    // cent values -- real maqam/raga performance practice involves far more
    // microtonal nuance, ornamentation, and (for ragas) different ascending
    // vs. descending note choices than a fixed scale can capture. The
    // 22-Shruti scale is the fuller just-intonation gamut ragas draw their
    // specific note selections from, included as a more open-ended option.
    //==========================================================================
    struct MicrotonalScale
    {
        const char* name;
        std::vector<float> centsFromRoot; // ascending; first entry should be 0
    };

    inline const std::vector<MicrotonalScale>& getBuiltInScales()
    {
        static const std::vector<MicrotonalScale> scales
        {
            { "Chromatic",                  { 0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100 } },
            { "Major",                      { 0, 200, 400, 500, 700, 900, 1100 } },
            { "Minor",                      { 0, 200, 300, 500, 700, 800, 1000 } },
            { "Maqam Rast",                 { 0, 200, 350, 500, 700, 900, 1050 } },
            { "Maqam Bayati",               { 0, 150, 300, 500, 700, 800, 1000 } },
            { "Raga Bhairav",               { 0, 90, 386, 498, 702, 792, 1088 } },
            { "22-Shruti (Just Intonation)", { 0, 90, 112, 182, 204, 294, 316, 386, 408, 498, 520,
                                                590, 612, 702, 792, 814, 884, 906, 996, 1018, 1088, 1110 } }
        };
        return scales;
    }

    // centsFromRoot can be any real value, any octave, relative to the
    // selected key's root note. Scans the octave above and below to
    // guarantee finding the true nearest scale degree even near an octave
    // boundary.
    inline float nearestScaleCents (float centsFromRoot, const std::vector<float>& scaleCents) noexcept
    {
        constexpr float octave = 1200.0f;
        const float wrapped = std::fmod (centsFromRoot, octave);
        const float wrappedPositive = wrapped >= 0.0f ? wrapped : wrapped + octave;
        const float baseOctaveOffset = centsFromRoot - wrappedPositive; // nearest multiple of 1200 at or below centsFromRoot

        float bestDistance = 1.0e9f;
        float best = scaleCents.empty() ? 0.0f : scaleCents[0] + baseOctaveOffset;

        for (int octaveShift = -1; octaveShift <= 1; ++octaveShift)
        {
            for (float degree : scaleCents)
            {
                const float candidate = degree + baseOctaveOffset + (float) octaveShift * octave;
                const float distance = std::abs (candidate - centsFromRoot);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = candidate;
                }
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

    //==========================================================================
    // Formant preservation: matches the pitch-shifted signal's coarse
    // spectral envelope (formants/timbre) back onto the original (dry)
    // signal's envelope, via a windowed STFT overlap-add stage running
    // after the pitch shifter.
    //
    // Envelope estimation here uses a moving average over the log-magnitude
    // spectrum, not cepstral liftering or LPC -- a simpler, lower-risk
    // technique than the "textbook" homomorphic approach, at the cost of
    // less precise resolution of individual formant peaks. It genuinely
    // corrects the coarse spectral tilt/timbre a plain pitch shift distorts,
    // which is the audible problem formant preservation exists to solve;
    // it just isn't the state-of-the-art version of that fix.
    //
    // This stage adds latency (see getLatencySamples()) because an STFT
    // frame can't be analysed until enough samples exist to fill it --
    // the processor must delay its own dry signal by the same amount
    // before mixing, or the dry/wet blend will comb-filter against itself.
    //==========================================================================
    class FormantCorrector
    {
    public:
        static constexpr int fftOrder = 10;
        static constexpr int fftSize  = 1 << fftOrder; // 1024
        static constexpr int hopSize  = fftSize / 4;   // 256 -- 75% overlap, Hann-squared COLA

        void prepare()
        {
            fft = std::make_unique<juce::dsp::FFT> (fftOrder);

            hann.assign ((size_t) fftSize, 0.0f);
            for (int i = 0; i < fftSize; ++i)
                hann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));

            dryRing.assign ((size_t) fftSize, 0.0f);
            shiftedRing.assign ((size_t) fftSize, 0.0f);
            ringWritePos = 0;
            samplesUntilNextFrame = hopSize;

            dryWindowed.assign ((size_t) fftSize, {});
            shiftedWindowed.assign ((size_t) fftSize, {});
            dryComplex.assign ((size_t) fftSize, {});
            shiftedComplex.assign ((size_t) fftSize, {});
            correctedComplex.assign ((size_t) fftSize, {});
            ifftResult.assign ((size_t) fftSize, {});

            logMagDry.assign ((size_t) fftSize / 2 + 1, 0.0f);
            logMagShifted.assign ((size_t) fftSize / 2 + 1, 0.0f);
            envelopeDry.assign ((size_t) fftSize / 2 + 1, 0.0f);
            envelopeShifted.assign ((size_t) fftSize / 2 + 1, 0.0f);

            outputAccumulator.assign ((size_t) fftSize, 0.0f);

            outputFifo.assign ((size_t) fftSize * 2, 0.0f);
            outputFifoReadPos = 0;
            outputFifoWritePos = 0;
            outputFifoAvailable = 0;

            // Pre-load fftSize samples of silence -- this IS the stage's
            // reported latency, giving the first real frame time to arrive
            // before anything is read back out.
            for (int i = 0; i < fftSize; ++i)
                pushOutputSample (0.0f);

            computeColaNormalisation();
        }

        static constexpr int getLatencySamples() noexcept { return fftSize; }

        // Feeds one dry+shifted sample pair in, returns one (latency-
        // delayed) formant-corrected sample out.
        float process (float dry, float shifted) noexcept
        {
            dryRing[(size_t) ringWritePos] = dry;
            shiftedRing[(size_t) ringWritePos] = shifted;
            ringWritePos = (ringWritePos + 1) % fftSize;

            if (--samplesUntilNextFrame <= 0)
            {
                samplesUntilNextFrame = hopSize;
                processFrame();
            }

            return popOutputSample();
        }

    private:
        void pushOutputSample (float s) noexcept
        {
            const int size = (int) outputFifo.size();
            outputFifo[(size_t) outputFifoWritePos] = s;
            outputFifoWritePos = (outputFifoWritePos + 1) % size;
            ++outputFifoAvailable;
        }

        float popOutputSample() noexcept
        {
            if (outputFifoAvailable <= 0)
                return 0.0f; // shouldn't happen -- production/consumption rates match by construction

            const int size = (int) outputFifo.size();
            const float s = outputFifo[(size_t) outputFifoReadPos];
            outputFifoReadPos = (outputFifoReadPos + 1) % size;
            --outputFifoAvailable;
            return s;
        }

        void computeColaNormalisation() noexcept
        {
            std::vector<float> sumBuf ((size_t) (fftSize + hopSize * 8), 0.0f);
            const int numFrames = fftSize / hopSize + 4;
            for (int frame = 0; frame < numFrames; ++frame)
            {
                const int offset = frame * hopSize;
                for (int i = 0; i < fftSize; ++i)
                    sumBuf[(size_t) (offset + i)] += hann[(size_t) i] * hann[(size_t) i];
            }
            // Probe well inside the steady-state region, away from the
            // startup edge.
            const int probeIndex = (numFrames / 2) * hopSize + fftSize / 2;
            colaNormalisation = juce::jmax (1.0e-6f, sumBuf[(size_t) probeIndex]);
        }

        void processFrame() noexcept
        {
            // Unwrap the rings into chronological order (oldest sample
            // first -- ringWritePos always points at the oldest remaining
            // sample) with a Hann analysis window applied.
            for (int i = 0; i < fftSize; ++i)
            {
                const int idx = (ringWritePos + i) % fftSize;
                const float w = hann[(size_t) i];
                dryWindowed[(size_t) i]     = { dryRing[(size_t) idx] * w, 0.0f };
                shiftedWindowed[(size_t) i] = { shiftedRing[(size_t) idx] * w, 0.0f };
            }

            fft->perform (dryWindowed.data(), dryComplex.data(), false);
            fft->perform (shiftedWindowed.data(), shiftedComplex.data(), false);

            for (int k = 0; k <= fftSize / 2; ++k)
            {
                logMagDry[(size_t) k]     = std::log (juce::jmax (std::abs (dryComplex[(size_t) k]), 1.0e-8f));
                logMagShifted[(size_t) k] = std::log (juce::jmax (std::abs (shiftedComplex[(size_t) k]), 1.0e-8f));
            }

            // Moving-average spectral envelope (see class comment).
            constexpr int smoothRadius = 8;
            for (int k = 0; k <= fftSize / 2; ++k)
            {
                float sumDry = 0.0f, sumShifted = 0.0f;
                int count = 0;
                for (int j = juce::jmax (0, k - smoothRadius); j <= juce::jmin (fftSize / 2, k + smoothRadius); ++j)
                {
                    sumDry     += logMagDry[(size_t) j];
                    sumShifted += logMagShifted[(size_t) j];
                    ++count;
                }
                envelopeDry[(size_t) k]     = sumDry / (float) count;
                envelopeShifted[(size_t) k] = sumShifted / (float) count;
            }

            // Scale each bin by the desired/actual envelope ratio -- this
            // rescales magnitude while leaving phase untouched (multiplying
            // a complex number by a positive real scalar only changes its
            // magnitude), so there's no need to separately track phase.
            // Clamped to roughly +-18dB so a silent/noisy frame can't
            // produce an extreme, unstable correction.
            for (int k = 0; k <= fftSize / 2; ++k)
            {
                const float correctionNats = juce::jlimit (-2.07f, 2.07f, envelopeDry[(size_t) k] - envelopeShifted[(size_t) k]);
                const float ratio = std::exp (correctionNats);
                correctedComplex[(size_t) k] = shiftedComplex[(size_t) k] * ratio;
            }
            for (int k = fftSize / 2 + 1; k < fftSize; ++k)
                correctedComplex[(size_t) k] = std::conj (correctedComplex[(size_t) (fftSize - k)]);

            fft->perform (correctedComplex.data(), ifftResult.data(), true);

            for (int i = 0; i < fftSize; ++i)
                outputAccumulator[(size_t) i] += ifftResult[(size_t) i].real() * hann[(size_t) i] / colaNormalisation;

            for (int i = 0; i < hopSize; ++i)
                pushOutputSample (outputAccumulator[(size_t) i]);

            // Shift the accumulator left by hopSize and zero-pad the newly-
            // exposed tail, ready for the next frame's overlap-add.
            for (int i = 0; i < fftSize - hopSize; ++i)
                outputAccumulator[(size_t) i] = outputAccumulator[(size_t) (i + hopSize)];
            for (int i = fftSize - hopSize; i < fftSize; ++i)
                outputAccumulator[(size_t) i] = 0.0f;
        }

        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> hann;

        std::vector<float> dryRing, shiftedRing;
        int ringWritePos = 0;
        int samplesUntilNextFrame = hopSize;

        std::vector<std::complex<float>> dryWindowed, shiftedWindowed, dryComplex, shiftedComplex, correctedComplex, ifftResult;
        std::vector<float> logMagDry, logMagShifted, envelopeDry, envelopeShifted;

        std::vector<float> outputAccumulator;
        float colaNormalisation = 1.0f;

        std::vector<float> outputFifo;
        int outputFifoReadPos = 0, outputFifoWritePos = 0, outputFifoAvailable = 0;
    };
}
