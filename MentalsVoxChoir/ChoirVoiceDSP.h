#pragma once

#include <cmath>
#include <cstdint>

//==============================================================================
// Per-voice "character": deterministic pseudo-random offsets seeded purely
// by voice index, so the same voice always gets the same underlying
// pitch/timing/vibrato identity -- turning a knob changes how MUCH
// variation is applied, not which voice does what, so the ensemble's basic
// arrangement stays stable across reloads and while a knob is moving. Kept
// in its own header (not folded into PluginProcessor.cpp) so
// PluginEditor.cpp's choir-spread visualisation can call the exact same
// function the DSP does and never show an arrangement that disagrees with
// what's actually playing.
//==============================================================================
namespace ChoirVoiceDSP
{
    // A small, fast, deterministic hash (PCG-style) rather than a real PRNG
    // with state -- there's nothing to seed/carry between calls, and the
    // same (voiceIndex, salt) pair always produces the same value.
    inline float pseudoRandom01 (uint32_t seed) noexcept
    {
        uint32_t x = seed * 747796405u + 2891336453u;
        x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
        x = (x >> 22u) ^ x;
        return (float) x / (float) 0xffffffffu;
    }

    struct VoiceCharacter
    {
        float pitchOffsetSign;    // -1..1, this voice's share of the Pitch knob's max detune
        float timingOffset01;     // 0..1, this voice's share of the Timing knob's max delay
        float vibratoSeedPhaseRad;
        float vibratoRateHz;
        float vibratoDepthScale;  // 0..1, this voice's share of the Vibrato knob's max depth
    };

    inline VoiceCharacter computeVoiceCharacter (int voiceIndex) noexcept
    {
        const auto seed = (uint32_t) voiceIndex * 5u;
        VoiceCharacter c;
        c.pitchOffsetSign   = pseudoRandom01 (seed + 0u) * 2.0f - 1.0f;
        c.timingOffset01    = pseudoRandom01 (seed + 1u);
        c.vibratoSeedPhaseRad = pseudoRandom01 (seed + 2u) * 6.28318530718f;
        c.vibratoRateHz     = 0.8f + pseudoRandom01 (seed + 3u) * 0.4f; // ~0.8-1.2Hz spread around a human vibrato-ish rate
        c.vibratoDepthScale = 0.5f + pseudoRandom01 (seed + 4u) * 0.5f;
        return c;
    }

    // Pan (-1 left .. +1 right), evenly spread across however many voices
    // are currently active -- separate from VoiceCharacter since it depends
    // on the current voice COUNT, not just this voice's own identity.
    inline float computeVoicePan (int voiceIndex, int numVoices) noexcept
    {
        return numVoices > 1 ? ((float) voiceIndex / (float) (numVoices - 1) * 2.0f - 1.0f) : 0.0f;
    }
}
