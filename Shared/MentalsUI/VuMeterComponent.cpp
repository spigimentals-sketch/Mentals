#include "VuMeterComponent.h"
#include <cmath>

namespace MentalsUI
{
    void VuMeterComponent::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Colours::charcoalBlack);
        g.fillRoundedRectangle (bounds, 4.0f);

        auto face = bounds.reduced (4.0f);
        const juce::Point<float> pivot (face.getCentreX(), face.getBottom() - 2.0f);
        const float radius = juce::jmin (face.getWidth() * 0.5f, face.getHeight()) - 6.0f;

        constexpr float maxReductionDb = 24.0f;
        // 0dB reduction rests toward the right, swinging left as more
        // reduction is applied -- the classic hardware-compressor VU look.
        constexpr float startAngle = -50.0f * juce::MathConstants<float>::pi / 180.0f;
        constexpr float endAngle   =  50.0f * juce::MathConstants<float>::pi / 180.0f;

        g.setColour (Colours::slateGray);
        juce::Path scaleArc;
        scaleArc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.strokePath (scaleArc, juce::PathStrokeType (1.5f));

        constexpr int numTicks = 7;
        for (int i = 0; i < numTicks; ++i)
        {
            const float t = (float) i / (float) (numTicks - 1);
            const float angle = startAngle + t * (endAngle - startAngle);
            const auto outer = pivot.getPointOnCircumference (radius + 3.0f, angle);
            const auto inner = pivot.getPointOnCircumference (radius - 4.0f, angle);
            g.setColour (t > 0.7f ? Colours::crimsonRed.withAlpha (0.8f) : Colours::slateGray);
            g.drawLine ({ inner, outer }, 1.5f);
        }

        const float amount = juce::jlimit (0.0f, 1.0f, -displayedGainReductionDb / maxReductionDb);
        const float needleAngle = startAngle + amount * (endAngle - startAngle);
        const auto needleTip = pivot.getPointOnCircumference (radius - 2.0f, needleAngle);

        g.setColour (Colours::goldenYellow);
        g.drawLine ({ pivot, needleTip }, 2.5f);
        g.fillEllipse (pivot.x - 3.0f, pivot.y - 3.0f, 6.0f, 6.0f);

        g.setColour (Colours::slateGray);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }
}
