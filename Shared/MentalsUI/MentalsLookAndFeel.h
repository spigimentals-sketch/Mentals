#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace MentalsUI
{
    //==============================================================================
    // Hardware-rack styling for every Mentals plugin's knobs and faders: a
    // metal-disc rotary knob with tick marks and a pointer instead of JUCE's
    // flat default, and a real fader cap running in a recessed groove for
    // LinearVertical sliders (see LabelledFader). Meters (LevelMeterComponent,
    // GainReductionMeterComponent) draw themselves directly rather than going
    // through a LookAndFeel, so their LED-segment look lives in their own
    // paint() methods instead of here.
    //
    // One shared instance (getSharedInstance()) rather than one per editor --
    // Mentals Suite hosts several plugins' editors in the same process at
    // once, so each editor calls setLookAndFeel() on itself (not the global
    // default) and clears it in its destructor; sharing one LookAndFeel
    // instance across them is safe since none of them own or mutate it.
    //==============================================================================
    class MentalsLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        MentalsLookAndFeel();

        static MentalsLookAndFeel& getSharedInstance();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                               juce::Slider&) override;

        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
    };
}
