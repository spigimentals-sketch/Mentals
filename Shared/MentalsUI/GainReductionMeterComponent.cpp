#include "GainReductionMeterComponent.h"
#include <cmath>

namespace MentalsUI
{
    void GainReductionMeterComponent::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        // Recessed bezel behind the LED column.
        g.setColour (Colours::charcoalBlack);
        g.fillRoundedRectangle (bounds, 3.0f);

        constexpr float maxReductionDb = 24.0f; // full-scale range shown
        const float amount = juce::jlimit (0.0f, 1.0f, -displayedGainReductionDb / maxReductionDb);

        // Discrete LED segments, filling from the TOP down (0dB reduction is
        // empty; more reduction lights more segments), matching this meter's
        // fills-from-the-top convention.
        constexpr int numSegments = 16;
        constexpr float gap = 1.5f;
        const float segmentHeight = (bounds.getHeight() - gap * (numSegments - 1)) / (float) numSegments;
        const int numLit = (int) std::round (amount * (float) numSegments);

        for (int i = 0; i < numSegments; ++i)
        {
            const float segTop = bounds.getY() + (float) i * (segmentHeight + gap);
            juce::Rectangle<float> segment (bounds.getX(), segTop, bounds.getWidth(), segmentHeight);

            const bool lit = i < numLit;
            g.setColour (lit ? Colours::electricBlue : Colours::electricBlue.withAlpha (0.12f));
            g.fillRoundedRectangle (segment, 1.5f);
        }

        g.setColour (Colours::slateGray);
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    }
}
