#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

//==============================================================================
// The actual waveshaping functions, kept in one place so the processor's
// real-time path and the editor's transfer-curve display can never disagree
// about what a given Drive/Type setting actually does.
//==============================================================================
namespace SaturatorDSP
{
    enum class Type
    {
        SoftClip = 0, // tanh -- smooth, symmetric, classic analog-style saturation
        HardClip,     // brick-wall clip -- harsh, adds strong odd harmonics
        Tube,         // asymmetric tanh -- adds even harmonics like a tube stage
        Foldback      // reflects back into range instead of clipping -- metallic/glitchy
    };

    inline float waveshape (float x, int typeIndex) noexcept
    {
        switch (static_cast<Type> (typeIndex))
        {
            case Type::HardClip:
                return juce::jlimit (-1.0f, 1.0f, x);

            case Type::Tube:
                // Different curve above/below zero produces even harmonics,
                // the hallmark of tube-style saturation.
                return x >= 0.0f ? std::tanh (x) : std::tanh (x * 1.3f) * 0.9f;

            case Type::Foldback:
            {
                constexpr float threshold = 1.0f;
                int iterations = 0;
                // Bounded rather than a plain while() -- a very hot, heavily
                // driven signal could otherwise take many reflections to
                // settle back in range on the audio thread.
                while ((x > threshold || x < -threshold) && iterations < 8)
                {
                    if (x > threshold)  x = 2.0f * threshold - x;
                    if (x < -threshold) x = -2.0f * threshold - x;
                    ++iterations;
                }
                return x;
            }

            case Type::SoftClip:
            default:
                return std::tanh (x);
        }
    }
}
