#include "AnalogVUMeterComponent.h"
#include "MentalsColours.h"

namespace MentalsUI
{
    namespace
    {
        // juce::Point::getPointOnCircumference(radius, angle) already treats
        // angle 0 as "straight up" and positive as clockwise (see its
        // definition: x + r*sin(angle), y - r*cos(angle)) -- no extra rotation
        // needed to get "swings right as the meter reads hotter/more reduced".
        //
        // VU mode: 0 rests near the middle-right for typical levels -- db and
        // angle move the same direction. Gain-reduction mode: reversed, since
        // 0 (no reduction) should rest at the LEFT and the needle should swing
        // RIGHT as MORE reduction (more negative dB) is applied -- feeding a
        // plain "more negative = more reduction" value through VU mode's
        // mapping unchanged would rest near the hot/red end even at zero
        // reduction, which reads backwards for a GR meter.
        float angleForDb (float db, float sweepDegrees, bool isGainReductionMode)
        {
            const float minDb = isGainReductionMode ? -24.0f : -20.0f;
            const float maxDb = isGainReductionMode ?   0.0f :  3.0f;
            const float t = isGainReductionMode
                                ? juce::jlimit (0.0f, 1.0f, (maxDb - db) / (maxDb - minDb))
                                : juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
            return juce::degreesToRadians (-sweepDegrees + t * (2.0f * sweepDegrees));
        }
    }

    void AnalogVUMeterComponent::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        // Recessed dark bezel, rounded like a real meter window.
        g.setColour (juce::Colour (0xff0c0c0d));
        g.fillRoundedRectangle (bounds, 6.0f);

        auto face = bounds.reduced (bounds.getWidth() * 0.05f, bounds.getHeight() * 0.09f);

        // Cream/amber lit face.
        juce::ColourGradient faceGradient (juce::Colour (0xfff2d9a0), face.getX(), face.getY(),
                                            juce::Colour (0xffcf9a4e), face.getX(), face.getBottom(), false);
        g.setGradientFill (faceGradient);
        g.fillRoundedRectangle (face, 3.0f);

        // A real VU meter's case is much wider than its printed arc alone --
        // the needle sweep widens with the face's aspect ratio so a wide meter
        // actually uses that extra width instead of leaving empty amber space
        // either side of a narrow, unchanged arc.
        const float aspect = face.getWidth() / juce::jmax (1.0f, face.getHeight());
        const float sweepDegrees = juce::jlimit (30.0f, 58.0f, 18.0f + aspect * 16.0f);

        // Everything below (arc scale + needle) is clipped to the visible
        // face -- the pivot and most of the needle's shaft sit just below it,
        // exactly like a real meter's mechanism hidden behind its bezel.
        g.saveState();
        g.reduceClipRegion (face.getSmallestIntegerContainer());

        const auto pivot = juce::Point<float> (face.getCentreX(), face.getBottom() + face.getHeight() * 0.14f);

        // Needle length is capped by BOTH how far it can reach before poking
        // out the top of the face, and how far it can swing sideways at full
        // deflection before poking out the sides -- whichever is tighter wins,
        // so a wide-but-short meter still keeps its whole arc on-screen.
        const float lengthForHeight = face.getHeight() * 1.05f;
        const float lengthForWidth  = (face.getWidth() * 0.48f) / std::sin (juce::degreesToRadians (sweepDegrees));
        const float needleLength = juce::jmin (lengthForHeight, lengthForWidth);

        // Printed arc scale: VU mode picks out the top end (0..+3) in red, the
        // way a real VU's overload zone is; gain-reduction mode picks out the
        // heavy-reduction end (<=-12) in red instead.
        const auto tickValues = isGainReductionMode ? std::initializer_list<float> { 0.0f, -3.0f, -6.0f, -12.0f, -24.0f }
                                                     : std::initializer_list<float> { -20.0f, -10.0f, -3.0f, 0.0f, 3.0f };
        g.setFont (juce::Font (juce::FontOptions (8.0f).withStyle ("Bold")));
        for (float db : tickValues)
        {
            const float a = angleForDb (db, sweepDegrees, isGainReductionMode);
            const auto outer = pivot.getPointOnCircumference (needleLength * 0.90f, a);
            const auto inner = pivot.getPointOnCircumference (needleLength * 0.78f, a);

            const bool hot = isGainReductionMode ? db <= -12.0f : db >= 0.0f;
            g.setColour (hot ? Colours::crimsonRed : juce::Colours::black.withAlpha (0.8f));
            g.drawLine ({ inner, outer }, 1.4f);

            const auto labelPoint = pivot.getPointOnCircumference (needleLength * 0.66f, a);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (24.0f, 11.0f).withCentre (labelPoint),
                        juce::Justification::centred);
        }

        // The needle itself.
        const auto needleTip = pivot.getPointOnCircumference (needleLength * 0.84f, angleForDb (needleDb, sweepDegrees, isGainReductionMode));
        g.setColour (juce::Colours::black);
        g.drawLine ({ pivot, needleTip }, 2.0f);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (pivot));

        g.restoreState();

        // Brand text, where a real hardware meter prints its manufacturer.
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.setFont (juce::Font (juce::FontOptions (8.5f).withStyle ("Bold")));
        g.drawText ("MENTALS", face.removeFromTop (face.getHeight() * 0.28f), juce::Justification::centred);

        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (bounds.reduced (1.0f), 6.0f, 1.2f);
    }
}
