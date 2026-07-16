#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MentalsColours.h"
#include <functional>
#include <utility>

namespace MentalsUI
{
    //==============================================================================
    // Vertical gain-reduction meter: fills from the top down as more
    // reduction is applied (0dB = empty, more negative = more filled), the
    // conventional look for a compressor/dynamics-processor GR meter.
    // Ballistics: jumps immediately to MORE reduction (so transients are
    // never missed), decays slowly back towards 0dB for readability --
    // standard GR meter behaviour, the opposite of a peak level meter's
    // "hold and slowly fall" ballistics.
    //==============================================================================
    class GainReductionMeterComponent : public juce::Component,
                                         private juce::Timer
    {
    public:
        explicit GainReductionMeterComponent (std::function<float()> getGainReductionDbIn)
            : getGainReductionDb (std::move (getGainReductionDbIn))
        {
            startTimerHz (30);
        }

        ~GainReductionMeterComponent() override { stopTimer(); }

        void paint (juce::Graphics& g) override;

    private:
        void timerCallback() override
        {
            const float target = getGainReductionDb ? getGainReductionDb() : 0.0f;

            if (target < displayedGainReductionDb)
                displayedGainReductionDb = target; // immediate jump to more reduction
            else
                displayedGainReductionDb = juce::jmin (target, displayedGainReductionDb + 1.5f); // slow climb back to 0

            repaint();
        }

        std::function<float()> getGainReductionDb;
        float displayedGainReductionDb = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainReductionMeterComponent)
    };
}
