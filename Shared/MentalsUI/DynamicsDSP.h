#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
// Shared dynamics-processing core: soft-knee gain computation (compressor
// and expander/gate variants) and an envelope follower, used by Mentals
// Compressor, De-esser (fundamentally a frequency-selective compressor),
// and Gate. Kept in one place so a processor's real-time path and its
// editor's transfer-curve display can never disagree about what a given
// Threshold/Ratio/Knee setting actually does.
//==============================================================================
namespace MentalsUI
{
    namespace DynamicsDSP
    {
        // Standard soft-knee compressor transfer function (see e.g. Zolzer's
        // DAFX): below the knee, output follows input 1:1; above it, output
        // is compressed by ratio:1 relative to threshold; within the knee
        // width, a quadratic blends smoothly between the two so there's no
        // audible kink at the threshold.
        inline float computeOutputDb (float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept
        {
            const float knee = juce::jmax (0.01f, kneeDb);
            const float diff = inputDb - thresholdDb;

            if (diff < -knee * 0.5f)
                return inputDb;

            if (diff > knee * 0.5f)
                return thresholdDb + diff / ratio;

            const float x = diff + knee * 0.5f;
            return inputDb + ((1.0f / ratio - 1.0f) * x * x) / (2.0f * knee);
        }

        // Downward expander/gate transfer function -- the mirror image of
        // computeOutputDb() above: ABOVE the knee, output follows input 1:1
        // (unaffected); BELOW it, output falls away ratio:1 relative to
        // threshold (steeper than input, the opposite direction a
        // compressor's ratio bends things), with the same quadratic knee
        // blend so there's no kink. Used by Mentals Gate; the caller is
        // expected to additionally clamp the resulting gain reduction to a
        // "Range" floor so a fully-closed gate attenuates by a bounded
        // amount rather than diving toward silence.
        inline float computeExpanderOutputDb (float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept
        {
            const float knee = juce::jmax (0.01f, kneeDb);
            const float diff = inputDb - thresholdDb;

            if (diff >= knee * 0.5f)
                return inputDb;

            if (diff <= -knee * 0.5f)
                return thresholdDb + diff * ratio;

            const float y = diff - knee * 0.5f;
            return inputDb + ((1.0f - ratio) * y * y) / (2.0f * knee);
        }

        //======================================================================
        // Classic one-pole envelope follower with independent attack/release.
        //======================================================================
        class EnvelopeFollower
        {
        public:
            void prepare (double sampleRate) noexcept
            {
                fs = sampleRate;
                updateCoefficients();
            }

            void setAttackRelease (float attackMs, float releaseMs) noexcept
            {
                attackTimeMs  = juce::jmax (0.01f, attackMs);
                releaseTimeMs = juce::jmax (0.01f, releaseMs);
                updateCoefficients();
            }

            void reset() noexcept { envelope = 0.0f; }

            float process (float rectifiedInput) noexcept
            {
                const float coeff = (rectifiedInput > envelope) ? attackCoeff : releaseCoeff;
                envelope = coeff * envelope + (1.0f - coeff) * rectifiedInput;
                return envelope;
            }

        private:
            void updateCoefficients() noexcept
            {
                attackCoeff  = std::exp (-1.0f / (0.001f * attackTimeMs  * (float) fs));
                releaseCoeff = std::exp (-1.0f / (0.001f * releaseTimeMs * (float) fs));
            }

            double fs = 44100.0;
            float attackTimeMs = 10.0f, releaseTimeMs = 100.0f;
            float attackCoeff = 0.0f, releaseCoeff = 0.0f;
            float envelope = 0.0f;
        };
    }
}
