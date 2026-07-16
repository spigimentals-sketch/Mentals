#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MentalsColours.h"
#include <functional>
#include <utility>

namespace MentalsUI
{
    //==============================================================================
    // Vertical output peak meter: emerald green below -6dB, amber caution zone
    // between -6dB and 0dB, crimson above 0dB, plus a briefly-latched clip LED.
    // Polls its two source callbacks on its own timer, so it has no dependency
    // on any particular plugin's processor type -- just supply how to read the
    // current peak level (dB) and clip flag.
    //==============================================================================
    class LevelMeterComponent : public juce::Component,
                                 private juce::Timer
    {
    public:
        LevelMeterComponent (std::function<float()> getPeakDbIn, std::function<bool()> isClippingIn)
            : getPeakDb (std::move (getPeakDbIn)), isClipping (std::move (isClippingIn))
        {
            startTimerHz (30);
        }

        ~LevelMeterComponent() override { stopTimer(); }

        void paint (juce::Graphics& g) override;

    private:
        void timerCallback() override
        {
            displayedPeakDb = getPeakDb ? getPeakDb() : -100.0f;
            clipping = isClipping && isClipping();
            repaint();
        }

        std::function<float()> getPeakDb;
        std::function<bool()> isClipping;
        float displayedPeakDb = -100.0f;
        bool clipping = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeterComponent)
    };
}
