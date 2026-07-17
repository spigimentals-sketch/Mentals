#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MentalsColours.h"
#include <functional>
#include <utility>

namespace MentalsUI
{
    //==============================================================================
    // Analog-style VU needle gauge showing gain reduction: a swept arc with a
    // needle that kicks left as more reduction is applied, the classic look
    // of a hardware compressor's built-in meter (e.g. an 1176 or LA-2A).
    // Deliberately slow, symmetric ballistics (~300ms one-pole smoothing on
    // both rise and fall) rather than the fast-attack/slow-release
    // convention GainReductionMeterComponent's LED column uses -- real VU
    // meters are slow on both directions because of the mechanical inertia
    // of an actual moving coil, and that sluggish, rounded-off motion is
    // exactly the "vibe" this component exists to add alongside the LED
    // meter's fast, precise digital reading.
    //==============================================================================
    class VuMeterComponent : public juce::Component,
                              private juce::Timer
    {
    public:
        explicit VuMeterComponent (std::function<float()> getGainReductionDbIn)
            : getGainReductionDb (std::move (getGainReductionDbIn))
        {
            startTimerHz (30);
        }

        ~VuMeterComponent() override { stopTimer(); }

        void paint (juce::Graphics& g) override;

    private:
        void timerCallback() override
        {
            const float target = getGainReductionDb ? getGainReductionDb() : 0.0f;
            constexpr float smoothing = 0.20f; // ~300ms-ish integration time at 30Hz
            displayedGainReductionDb += (target - displayedGainReductionDb) * smoothing;
            repaint();
        }

        std::function<float()> getGainReductionDb;
        float displayedGainReductionDb = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuMeterComponent)
    };
}
