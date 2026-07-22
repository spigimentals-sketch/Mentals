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

        // A lag needs a reasonable number of overlapping samples for its
        // correlation score to mean anything. Without this floor, a lag
        // forced right up against the window boundary (whenever the window
        // is too short to comfortably fit the requested minFreqHz -- Low-
        // Latency Mode's short window is the common case) leaves only a
        // handful of overlapping samples, or even one, whose correlation
        // trivially reaches +-1.0 purely by chance -- a spuriously "perfect"
        // score that looks like a rock-solid detection but is really just
        // noise. Capping lag at half the window guarantees at least half
        // the window always participates, which does mean the true
        // achievable frequency floor gets silently raised whenever the
        // window is too short to support the requested minFreqHz -- but
        // that's honest: a short window fundamentally cannot resolve a very
        // low pitch, no matter what range was asked for.
        const int overlapFloorLag = windowSize / 2;
        const int maxLag = juce::jmin (overlapFloorLag, (int) (sampleRate / minFreqHz));

        if (maxLag <= minLag)
            return false;

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

        // Score storage capped to a generous fixed size so this stays
        // allocation-free on the audio thread; windowSize (a few thousand
        // samples at most, in practice) never approaches this span.
        constexpr int maxLagSpan = 4096;
        const int lagCount = juce::jmin (maxLag - minLag + 1, maxLagSpan);
        std::array<float, maxLagSpan> scores {};
        for (int i = 0; i < lagCount; ++i)
            scores[(size_t) i] = scoreAt (minLag + i);

        // Octave-error mitigation: picking the single highest-scoring lag
        // across the whole range regularly locks onto a subharmonic (an
        // octave or more BELOW the true pitch), because a strong, harmonic-
        // rich singing voice makes integer multiples of the true period
        // score nearly as well as -- and sometimes marginally better than --
        // the true period itself. That's the single biggest cause of a
        // pitch corrector suddenly "helping" a note an octave away from
        // what was actually sung, which reads as broken/weird rather than
        // just imprecise. Scanning from the shortest lag (highest
        // frequency) upward and taking the FIRST strong local peak, instead
        // of the global maximum, prefers the true fundamental over its
        // subharmonics -- the same principle behind YIN's absolute-
        // threshold search. Falls back to the previous "pick whatever
        // scored highest" behaviour if nothing clears the strong-peak bar,
        // so a quiet/ambiguous frame isn't handled any worse than before.
        constexpr float strongPeakThreshold = 0.6f;
        int bestLag = -1;
        float bestScore = 0.0f;

        for (int i = 0; i < lagCount; ++i)
        {
            const float score = scores[(size_t) i];
            const bool isLocalPeak = (i == 0 || score >= scores[(size_t) (i - 1)])
                                   && (i == lagCount - 1 || score >= scores[(size_t) (i + 1)]);

            if (isLocalPeak && score >= strongPeakThreshold)
            {
                bestLag = minLag + i;
                bestScore = score;
                break;
            }

            if (score > bestScore)
            {
                bestScore = score;
                bestLag = minLag + i;
            }
        }

        if (bestLag < 0 || bestScore < 0.05f)
            return false;

        // Parabolic interpolation around the best lag using its neighbours'
        // scores, for sub-sample accuracy (reusing the shared scoreAt()
        // rather than recomputing the whole scan).
        float interpolatedLag = (float) bestLag;
        if (bestLag > minLag && bestLag < maxLag)
        {
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

    inline float midiNoteToFrequencyHz (int midiNote) noexcept
    {
        return 440.0f * std::pow (2.0f, (float) (midiNote - 69) / 12.0f);
    }

    // Scientific pitch notation (e.g. "A4", "C#5") for a detected/target
    // frequency -- what the editor's note display shows for "the key the
    // vocal is hitting". Returns an empty string for a non-positive/silent
    // frequency rather than a nonsense note name.
    inline juce::String frequencyToNoteName (float freqHz) noexcept
    {
        if (freqHz <= 0.0f)
            return {};

        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        const float midiFloat = 69.0f + 12.0f * std::log2 (freqHz / 440.0f);
        const int midiNote = (int) std::floor (midiFloat + 0.5f);
        const int noteIndex = ((midiNote % 12) + 12) % 12;
        const int octave = midiNote / 12 - 1; // MIDI note 60 == C4, note 0 == C-1

        return juce::String (names[noteIndex]) + juce::String (octave);
    }

    //==========================================================================
    // Voice-type detection ranges: narrows the pitch detector's search band
    // to a real singer's actual range instead of always scanning the full
    // bass-to-soprano span. Two benefits, not one -- a tighter band also
    // makes octave errors less likely in the first place, since fewer
    // candidate lags (fewer harmonics/subharmonics) fall inside it at all.
    // "Auto (Wide Range)" keeps today's original full-range behaviour for
    // anyone who'd rather not pick a voice type.
    //==========================================================================
    struct VoiceTypeRange
    {
        const char* name;
        float minFreqHz, maxFreqHz;
    };

    inline const std::vector<VoiceTypeRange>& getVoiceTypeRanges()
    {
        static const std::vector<VoiceTypeRange> ranges
        {
            { "Auto (Wide Range)", 70.0f,  1200.0f },
            { "Bass",              65.0f,  330.0f  },
            { "Baritone",          80.0f,  400.0f  },
            { "Tenor",             95.0f,  520.0f  },
            { "Alto",              140.0f, 700.0f  },
            { "Soprano",           200.0f, 1100.0f },
        };
        return ranges;
    }

    //==========================================================================
    // Self-contained streaming wrapper around detectPitch(): owns the ring
    // buffer/hop counter/window so a second, independent detector (e.g. for
    // Sidechain Tuning) can run alongside the main one without duplicating
    // that bookkeeping. The main processor's own detector predates this
    // class and isn't migrated onto it, to avoid touching already-verified,
    // working code for a pure refactor with no behaviour change.
    //==========================================================================
    class StreamingPitchDetector
    {
    public:
        void prepare (double sampleRateIn, double windowSeconds)
        {
            sampleRate = sampleRateIn;
            windowSize = juce::jmax (256, (int) (windowSeconds * sampleRate));
            hopSize    = juce::jmax (128, windowSize / 2);

            ringBuffer.assign ((size_t) windowSize, 0.0f);
            workspace.assign ((size_t) windowSize, 0.0f);
            writePos = 0;
            samplesUntilNextHop = hopSize;
            lastFreqHz = 0.0f;
            lastVoiced = false;
        }

        // Feeds one sample in; returns true if a new detection cycle just ran.
        bool pushSample (float sample, float minFreqHz, float maxFreqHz, float confidenceThreshold) noexcept
        {
            ringBuffer[(size_t) writePos] = sample;
            writePos = (writePos + 1) % windowSize;

            if (--samplesUntilNextHop > 0)
                return false;

            samplesUntilNextHop = hopSize;

            for (int i = 0; i < windowSize; ++i)
            {
                const int idx = (writePos + i) % windowSize;
                const float window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (windowSize - 1));
                workspace[(size_t) i] = ringBuffer[(size_t) idx] * window;
            }

            float freq = 0.0f, confidence = 0.0f;
            const bool found = detectPitch (workspace.data(), windowSize, sampleRate, minFreqHz, maxFreqHz, freq, confidence);

            lastVoiced = found && confidence >= confidenceThreshold;
            if (lastVoiced)
                lastFreqHz = freq;

            return true;
        }

        float getLastFreqHz() const noexcept { return lastFreqHz; }
        bool isLastVoiced() const noexcept { return lastVoiced; }

    private:
        double sampleRate = 44100.0;
        int windowSize = 2048, hopSize = 1024;
        std::vector<float> ringBuffer, workspace;
        int writePos = 0;
        int samplesUntilNextHop = 0;
        float lastFreqHz = 0.0f;
        bool lastVoiced = false;
    };

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

    // Like nearestScaleCents(), but then steps degreeOffset scale degrees up
    // (positive) or down (negative) from whichever degree was nearest,
    // wrapping across octave boundaries as needed -- used by the
    // harmonizer to generate harmonies that respect the current Key/Scale
    // (including microtonal ones) rather than fixed semitone intervals,
    // which wouldn't make sense for a non-12-TET scale.
    inline float nearestScaleDegreeCents (float centsFromRoot, const std::vector<float>& scaleCents, int degreeOffset) noexcept
    {
        if (scaleCents.empty())
            return centsFromRoot;

        constexpr float octave = 1200.0f;
        const float wrapped = std::fmod (centsFromRoot, octave);
        const float wrappedPositive = wrapped >= 0.0f ? wrapped : wrapped + octave;
        const float baseOctaveOffset = centsFromRoot - wrappedPositive;

        float bestDistance = 1.0e9f;
        int bestOctaveShift = 0;
        int bestDegreeIndex = 0;

        for (int octaveShift = -1; octaveShift <= 1; ++octaveShift)
        {
            for (int degreeIndex = 0; degreeIndex < (int) scaleCents.size(); ++degreeIndex)
            {
                const float candidate = scaleCents[(size_t) degreeIndex] + baseOctaveOffset + (float) octaveShift * octave;
                const float distance = std::abs (candidate - centsFromRoot);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestOctaveShift = octaveShift;
                    bestDegreeIndex = degreeIndex;
                }
            }
        }

        const int numDegrees = (int) scaleCents.size();
        const int totalStepIndex = bestOctaveShift * numDegrees + bestDegreeIndex + degreeOffset;

        // Floor division (not C++'s truncating integer division), so a
        // negative totalStepIndex wraps to the correct lower octave rather
        // than rounding towards zero.
        const int finalOctaveShift  = (totalStepIndex >= 0) ? (totalStepIndex / numDegrees)
                                                             : -(((-totalStepIndex) + numDegrees - 1) / numDegrees);
        const int finalDegreeIndex  = totalStepIndex - finalOctaveShift * numDegrees;

        return scaleCents[(size_t) finalDegreeIndex] + baseOctaveOffset + (float) finalOctaveShift * octave;
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
