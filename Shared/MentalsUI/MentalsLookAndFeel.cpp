#include "MentalsLookAndFeel.h"
#include "MentalsColours.h"

namespace MentalsUI
{
    MentalsLookAndFeel::MentalsLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId,    Colours::electricBlue);
        setColour (juce::Slider::rotarySliderOutlineColourId, Colours::slateGray);
        setColour (juce::Slider::thumbColourId,               Colours::electricBlue);
        setColour (juce::Slider::textBoxTextColourId,         Colours::white);
        setColour (juce::Slider::textBoxBackgroundColourId,   Colours::slateGrayDark);
        setColour (juce::Slider::textBoxOutlineColourId,      Colours::slateGray);
        setColour (juce::Slider::trackColourId,               Colours::slateGrayDark);
        setColour (juce::Slider::backgroundColourId,           Colours::slateGrayDark);
    }

    MentalsLookAndFeel& MentalsLookAndFeel::getSharedInstance()
    {
        static MentalsLookAndFeel instance;
        return instance;
    }

    void MentalsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                                float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                                juce::Slider&)
    {
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        bounds = bounds.withSizeKeepingCentre (diameter, diameter);
        const auto radius = diameter * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Tick marks around the sweep.
        constexpr int numTicks = 11;
        for (int i = 0; i < numTicks; ++i)
        {
            const float t = (float) i / (float) (numTicks - 1);
            const float tickAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            const auto tickOuter = centre.getPointOnCircumference (radius + 2.0f, tickAngle);
            const auto tickInner = centre.getPointOnCircumference (radius - 3.0f, tickAngle);
            g.setColour (Colours::slateGray);
            g.drawLine ({ tickInner, tickOuter }, 1.5f);
        }

        // Value arc: dark track behind, electric-blue fill up to the current value.
        const float trackRadius = radius - 8.0f;
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (Colours::slateGrayDark);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path fillArc;
        fillArc.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour (Colours::electricBlue);
        g.strokePath (fillArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Knob body: a metal disc, lit from the top-left, with a thin rim.
        const auto knobBounds = bounds.reduced (radius * 0.34f);
        juce::ColourGradient bodyGradient (Colours::slateGray.brighter (0.3f), knobBounds.getX(), knobBounds.getY(),
                                            Colours::charcoalBlack, knobBounds.getRight(), knobBounds.getBottom(), false);
        g.setGradientFill (bodyGradient);
        g.fillEllipse (knobBounds);
        g.setColour (Colours::charcoalBlack.withAlpha (0.8f));
        g.drawEllipse (knobBounds, 1.2f);

        // Glossy highlight, upper-left.
        auto highlight = knobBounds.reduced (knobBounds.getWidth() * 0.2f)
                                    .translated (-knobBounds.getWidth() * 0.06f, -knobBounds.getHeight() * 0.1f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillEllipse (highlight.withHeight (highlight.getHeight() * 0.5f));

        // Pointer, from just off-centre out to near the rim, with a dot tip.
        const auto pointerStart = centre.getPointOnCircumference (knobBounds.getWidth() * 0.14f, angle);
        const auto pointerEnd   = centre.getPointOnCircumference (knobBounds.getWidth() * 0.44f, angle);
        g.setColour (Colours::electricBlue);
        g.drawLine ({ pointerStart, pointerEnd }, 2.5f);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (pointerEnd));
    }

    void MentalsLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
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

        // Recessed groove.
        g.setColour (Colours::slateGrayDark);
        g.fillRoundedRectangle (track, trackWidth * 0.5f);
        g.setColour (Colours::charcoalBlack);
        g.drawRoundedRectangle (track, trackWidth * 0.5f, 1.0f);

        // Tick marks flanking the groove.
        constexpr int numTicks = 9;
        for (int i = 0; i < numTicks; ++i)
        {
            const float t = (float) i / (float) (numTicks - 1);
            const float tickY = bounds.getY() + t * bounds.getHeight();
            g.setColour (Colours::slateGray);
            g.drawLine (track.getX() - 6.0f, tickY, track.getX() - 2.0f, tickY, 1.0f);
            g.drawLine (track.getRight() + 2.0f, tickY, track.getRight() + 6.0f, tickY, 1.0f);
        }

        // Lit fill from the bottom of the groove up to the current position.
        juce::Rectangle<float> lit (track.getX(), sliderPos, track.getWidth(), track.getBottom() - sliderPos);
        g.setColour (Colours::electricBlue.withAlpha (0.85f));
        g.fillRoundedRectangle (lit, trackWidth * 0.5f);

        // Fader cap: a metal block with a coloured grip stripe through the middle.
        const float capWidth  = bounds.getWidth() * 0.82f;
        const float capHeight = 18.0f;
        juce::Rectangle<float> cap (bounds.getCentreX() - capWidth * 0.5f, sliderPos - capHeight * 0.5f, capWidth, capHeight);

        juce::ColourGradient capGradient (Colours::slateGray.brighter (0.3f), cap.getX(), cap.getY(),
                                           Colours::charcoalBlack, cap.getX(), cap.getBottom(), false);
        g.setGradientFill (capGradient);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (Colours::charcoalBlack);
        g.drawRoundedRectangle (cap, 3.0f, 1.0f);

        g.setColour (Colours::electricBlue);
        g.fillRect (cap.reduced (cap.getWidth() * 0.15f, cap.getHeight() * 0.42f));
    }
}
