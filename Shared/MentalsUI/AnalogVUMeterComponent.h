#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>

namespace MentalsUI
{
    //==============================================================================
    // A classic analog VU meter -- cream/amber face, printed black arc scale, a
    // swinging needle -- for plugins using HardwareLookAndFeel, in place of
    // this suite's usual LED bar meter (LevelMeterComponent/
    // GainReductionMeterComponent), matching the meter every classic hardware
    // compressor/limiter has. The needle is driven by the real level
    // (getLevelDb), not illustrative, but mechanically damped (a slow
    // one-pole follow, not an instant jump) the way a real moving-coil
    // meter's inertia reads, rather than jittering with every sample.
    //
    // Two modes, matching how a real hardware unit's single physical meter is
    // dual-purpose via a switch rather than being two different meter designs:
    //  - VU (default): -20..+3dB, rests near the middle-right for typical
    //    signal levels, red zone above 0dB.
    //  - Gain reduction (isGainReductionMode): 0..-24dB, but READ BACKWARDS --
    //    0 (no reduction) rests at the LEFT, and the needle swings RIGHT as
    //    MORE reduction is applied, red zone below -12dB. Feeding a plain
    //    "more negative = more reduction" value through the VU mode's scale
    //    would rest near the red/hot end even at zero reduction, which reads
    //    backwards for a gain-reduction meter -- this mode exists specifically
    //    to avoid that.
    //==============================================================================
    class AnalogVUMeterComponent : public juce::Component,
                                    private juce::Timer
    {
    public:
        explicit AnalogVUMeterComponent (std::function<float()> getLevelDbIn, bool isGainReductionModeIn = false)
            : getLevelDb (std::move (getLevelDbIn)), isGainReductionMode (isGainReductionModeIn)
        {
            needleDb = isGainReductionMode ? 0.0f : -20.0f;
            startTimerHz (30);
        }

        ~AnalogVUMeterComponent() override { stopTimer(); }

        void paint (juce::Graphics& g) override;

    private:
        void timerCallback() override
        {
            const float restDb = isGainReductionMode ? 0.0f : -20.0f;
            const float target = isGainReductionMode ? juce::jlimit (-24.0f, 0.0f, getLevelDb ? getLevelDb() : restDb)
                                                      : juce::jlimit (-20.0f, 3.0f, getLevelDb ? getLevelDb() : restDb);
            needleDb += (target - needleDb) * 0.28f; // real VU ballistics: damped, not instant
            repaint();
        }

        std::function<float()> getLevelDb;
        const bool isGainReductionMode;
        float needleDb;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalogVUMeterComponent)
    };
}
