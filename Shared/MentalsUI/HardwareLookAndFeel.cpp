#include "HardwareLookAndFeel.h"
#include "MentalsColours.h"

namespace MentalsUI
{
    namespace
    {
        // Printed tick numbers on real hardware never repeat the unit on
        // every mark (that's what the knob's own label is for) -- just a
        // clean short number, with "k" for round thousands (e.g. a 20000Hz
        // High Cut knob prints "20k" the way a real EQ's frequency knob
        // would).
        juce::String formatTickValue (float v)
        {
            if (std::abs (v) >= 1000.0f)
            {
                const float thousands = v / 1000.0f;
                // juce::String(double, 0) does NOT round to an integer --
                // passing 0 decimal places falls back to default stream
                // precision (6 significant figures), which is not what "0
                // decimals" sounds like it should do. Round explicitly instead.
                return (std::abs (v) >= 10000.0f ? juce::String ((int) std::round (thousands))
                                                  : juce::String (thousands, 1))
                       + "k";
            }
            if (std::abs (v - std::round (v)) < 0.05f)
                return juce::String ((int) std::round (v));
            return juce::String (v, 1);
        }

        const juce::Colour chromeHighlight { 0xfff4f4f6 };
        const juce::Colour chromeMid       { 0xffa6a8ac };
        const juce::Colour chromeShadow    { 0xff4a4c50 };
        const juce::Colour panelNearBlack  { 0xff141416 };
        const juce::Colour panelDark       { 0xff1e1f22 };
    }

    HardwareLookAndFeel::HardwareLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId,    Colours::white);
        setColour (juce::Slider::rotarySliderOutlineColourId, Colours::slateGray);
        setColour (juce::Slider::thumbColourId,               Colours::white);
        setColour (juce::Slider::textBoxTextColourId,         Colours::white);
        setColour (juce::Slider::textBoxBackgroundColourId,   panelNearBlack);
        setColour (juce::Slider::textBoxOutlineColourId,      Colours::slateGray);
        setColour (juce::Slider::trackColourId,               Colours::slateGrayDark);
        setColour (juce::Slider::backgroundColourId,          panelNearBlack);
    }

    void HardwareLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                                 float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                                 juce::Slider& slider)
    {
        // The knob body sits INSIDE the full bounds JUCE hands us -- the ring
        // between knobBounds and the outer edge is reserved for the printed
        // tick numbers, silkscreened onto the panel around a real hardware
        // knob rather than drawn on the knob cap itself.
        auto fullBounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const auto diameter = juce::jmin (fullBounds.getWidth(), fullBounds.getHeight());
        fullBounds = fullBounds.withSizeKeepingCentre (diameter, diameter);

        constexpr float labelMarginFrac = 0.26f;
        auto bounds = fullBounds.reduced (diameter * labelMarginFrac);
        const auto radius = bounds.getWidth() * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Printed tick-mark numbers: the ACTUAL parameter value at each evenly
        // ANGLE-spaced position -- matching how a real potentiometer's
        // silkscreen is printed (evenly spaced in rotation, not in value, so a
        // skewed/log taper shows compressed numbers bunching up at one end,
        // exactly like a real frequency or ratio knob does).
        constexpr int numTicks = 5;
        g.setFont (juce::Font (juce::FontOptions (8.5f)).withExtraKerningFactor (0.02f));
        for (int i = 0; i < numTicks; ++i)
        {
            const float t = (float) i / (float) (numTicks - 1);
            const float tickAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);

            const auto tickOuter = centre.getPointOnCircumference (radius + 4.0f, tickAngle);
            const auto tickInner = centre.getPointOnCircumference (radius - 1.0f, tickAngle);
            g.setColour (Colours::white.withAlpha (0.7f));
            g.drawLine ({ tickInner, tickOuter }, 1.3f);

            const auto labelPoint = centre.getPointOnCircumference (radius + diameter * labelMarginFrac * 0.62f, tickAngle);
            const auto value = slider.proportionOfLengthToValue (t);
            g.setColour (Colours::white.withAlpha (0.85f));
            g.drawText (formatTickValue ((float) value), juce::Rectangle<float> (34.0f, 11.0f).withCentre (labelPoint),
                        juce::Justification::centred);
        }

        // Knob body: polished chrome, not flat plastic -- a bright highlight
        // offset toward the light source (upper-left) fading through a mid
        // steel tone to a darker rim, the read a real machined aluminium/
        // chrome control knob has under studio lighting.
        juce::ColourGradient bodyGradient (chromeHighlight, bounds.getX() + bounds.getWidth() * 0.28f, bounds.getY() + bounds.getHeight() * 0.22f,
                                            chromeShadow, bounds.getRight(), bounds.getBottom(), true);
        bodyGradient.addColour (0.45, chromeMid);
        g.setGradientFill (bodyGradient);
        g.fillEllipse (bounds);

        g.setColour (panelNearBlack);
        g.drawEllipse (bounds, 1.6f);

        // A darker recessed groove near the rim -- reads as a machined edge
        // rather than a flat painted circle.
        g.setColour (chromeShadow.withAlpha (0.6f));
        g.drawEllipse (bounds.reduced (radius * 0.12f), 1.0f);

        // Pointer: the one spot of brand colour on an otherwise neutral-chrome
        // knob -- a thin electric-blue indicator line, the way some hardware
        // units use a single accent colour for their pointers/legends against
        // an otherwise all-metal panel.
        const auto pointerStart = centre.getPointOnCircumference (radius * 0.18f, angle);
        const auto pointerEnd   = centre.getPointOnCircumference (radius * 0.88f, angle);
        g.setColour (Colours::electricBlue);
        g.drawLine ({ pointerStart, pointerEnd }, 2.6f);
    }

    void HardwareLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                 float sliderPos, float minSliderPos, float maxSliderPos,
                                                 juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

        constexpr float trackWidth = 6.0f;
        auto track = bounds.withSizeKeepingCentre (trackWidth, bounds.getHeight()).reduced (0.0f, 4.0f);

        g.setColour (panelNearBlack);
        g.fillRoundedRectangle (track, trackWidth * 0.5f);
        g.setColour (Colours::slateGray.withAlpha (0.5f));
        g.drawRoundedRectangle (track, trackWidth * 0.5f, 1.0f);

        // Printed numbers flanking the groove, same "actual value at this
        // point" idea as the rotary ticks above.
        constexpr int numTicks = 5;
        g.setFont (juce::Font (juce::FontOptions (8.5f)));
        for (int i = 0; i < numTicks; ++i)
        {
            const float t = (float) i / (float) (numTicks - 1);
            const float tickY = bounds.getY() + t * bounds.getHeight();
            g.setColour (Colours::white.withAlpha (0.7f));
            g.drawLine (track.getX() - 6.0f, tickY, track.getX() - 2.0f, tickY, 1.0f);
            g.drawLine (track.getRight() + 2.0f, tickY, track.getRight() + 6.0f, tickY, 1.0f);

            const auto value = slider.proportionOfLengthToValue (1.0 - (double) t);
            g.setColour (Colours::white.withAlpha (0.85f));
            g.drawText (formatTickValue ((float) value),
                         juce::Rectangle<float> (26.0f, 11.0f).withCentre ({ track.getX() - 18.0f, tickY }),
                         juce::Justification::centred);
        }

        // Fader cap: a chrome block (matching the knobs) with a blue-accent
        // grip line through the middle, riding in the recessed groove.
        const float capWidth  = bounds.getWidth() * 0.82f;
        const float capHeight = 18.0f;
        juce::Rectangle<float> cap (bounds.getCentreX() - capWidth * 0.5f, sliderPos - capHeight * 0.5f, capWidth, capHeight);

        juce::ColourGradient capGradient (chromeHighlight, cap.getX(), cap.getY(),
                                           chromeShadow, cap.getX(), cap.getBottom(), false);
        capGradient.addColour (0.5, chromeMid);
        g.setGradientFill (capGradient);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (panelNearBlack);
        g.drawRoundedRectangle (cap, 3.0f, 1.0f);
        g.setColour (Colours::electricBlue);
        g.drawLine (cap.getX() + 3.0f, cap.getCentreY(), cap.getRight() - 3.0f, cap.getCentreY(), 1.6f);
    }

    void HardwareLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                                 bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/)
    {
        // A rocker/slide switch, not a checkbox -- a vertical track recessed
        // into the panel with a chrome tab that sits at the top when on,
        // bottom when off, the way a real hardware unit's toggles work.
        constexpr float trackWidth = 16.0f, trackHeight = 30.0f;
        auto bounds = button.getLocalBounds().toFloat();
        juce::Rectangle<float> track (bounds.getX(), bounds.getCentreY() - trackHeight * 0.5f, trackWidth, trackHeight);

        g.setColour (panelNearBlack);
        g.fillRoundedRectangle (track, trackWidth * 0.5f);
        g.setColour (Colours::slateGray.withAlpha (0.6f));
        g.drawRoundedRectangle (track, trackWidth * 0.5f, 1.0f);

        const bool on = button.getToggleState();
        constexpr float tabSize = 13.0f;
        const float tabY = on ? track.getY() + 2.0f : track.getBottom() - tabSize - 2.0f;
        juce::Rectangle<float> tab (track.getCentreX() - tabSize * 0.5f, tabY, tabSize, tabSize);

        juce::ColourGradient tabGradient (chromeHighlight, tab.getX(), tab.getY(), chromeShadow, tab.getRight(), tab.getBottom(), false);
        tabGradient.addColour (0.5, chromeMid);
        g.setGradientFill (tabGradient);
        g.fillEllipse (tab);
        g.setColour (panelNearBlack);
        g.drawEllipse (tab, 1.0f);

        g.setColour (on ? Colours::electricBlue : Colours::white.withAlpha (0.55f));
        g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
        g.drawText (button.getButtonText(), bounds.withTrimmedLeft (trackWidth + 8.0f), juce::Justification::centredLeft);
    }

    void HardwareLookAndFeel::drawMetalPanel (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        // Near-black brushed aluminium -- a real hardware chassis is much
        // darker and flatter than a "brushed grey" gradient; light barely
        // lifts the top edge.
        juce::ColourGradient gradient (panelDark, bounds.getX(), bounds.getY(),
                                        panelNearBlack, bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (gradient);
        g.fillRect (bounds);

        // Fine brushed-metal grain: faint horizontal lines at very low alpha.
        g.setColour (juce::Colours::white.withAlpha (0.012f));
        for (float yPos = bounds.getY(); yPos < bounds.getBottom(); yPos += 3.0f)
            g.drawHorizontalLine ((int) yPos, bounds.getX(), bounds.getRight());

        g.setColour (juce::Colours::black);
        g.drawRect (bounds, 1.0f);
    }

    void HardwareLookAndFeel::drawScrew (juce::Graphics& g, juce::Point<float> centre)
    {
        constexpr float r = 4.0f;
        juce::ColourGradient gradient (chromeHighlight, centre.x - r, centre.y - r, chromeShadow, centre.x + r, centre.y + r, false);
        g.setGradientFill (gradient);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));

        g.setColour (panelNearBlack);
        g.drawLine (centre.x - r * 0.6f, centre.y, centre.x + r * 0.6f, centre.y, 1.0f);
        g.drawLine (centre.x, centre.y - r * 0.6f, centre.x, centre.y + r * 0.6f, 1.0f);
    }

    void HardwareLookAndFeel::drawRackEar (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        g.setColour (panelDark);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (bounds, 6.0f, 1.2f);

        const float boltInset = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.32f;
        drawScrew (g, { bounds.getCentreX(), bounds.getY() + boltInset });
        drawScrew (g, { bounds.getCentreX(), bounds.getBottom() - boltInset });
    }
}
