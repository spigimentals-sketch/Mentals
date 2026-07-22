#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace MentalsUI
{
    //==============================================================================
    // Rack-unit styling, opt-in per plugin (call setLookAndFeel(&instance) the
    // same way MentalsLookAndFeel is used) rather than the suite-wide default --
    // modelled directly on a classic hardware rack compressor's front panel:
    // polished-chrome knobs with printed panel tick numbers (the actual
    // parameter value at each angle, not just blank lines), a near-black
    // brushed chassis with bolted rack ears, and rocker-style toggle switches,
    // with a single electric-blue accent (knob pointers, fader grip lines, a
    // switch's "on" state) rather than this suite's usual per-control colour
    // coding. First built for Mentals Reverb, promoted here once a second
    // plugin (De-esser) wanted the same look -- keeps one canonical
    // definition rather than copy-pasted per-plugin drawing code (which would
    // also risk duplicate-symbol linker errors, since Mentals Suite links
    // every plugin's core library into one executable).
    //==============================================================================
    class HardwareLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        HardwareLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                               juce::Slider&) override;

        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

        // Shared texture helpers -- not LookAndFeel overrides, just called
        // directly from a plugin editor's paint() so the panel background and
        // the knobs read as the same physical material.
        static void drawMetalPanel (juce::Graphics&, juce::Rectangle<float> bounds);
        static void drawScrew (juce::Graphics&, juce::Point<float> centre);

        // A bolted, rounded rack-mount ear -- drawn at the outer left/right
        // edges of the panel, the detail that most reads as "this is a 1U/2U
        // rack unit" rather than just a dark rectangle.
        static void drawRackEar (juce::Graphics&, juce::Rectangle<float> bounds);
    };
}
