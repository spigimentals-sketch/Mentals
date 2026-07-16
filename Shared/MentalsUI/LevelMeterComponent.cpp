#include "LevelMeterComponent.h"

namespace MentalsUI
{
    void LevelMeterComponent::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        // Reserve a small square at the top for the clip LED, the rest is the bar.
        auto clipLedArea = bounds.removeFromTop (juce::jmin (14.0f, bounds.getHeight() * 0.15f)).reduced (2.0f);
        bounds.removeFromTop (2.0f);

        g.setColour (clipping ? Colours::crimsonRed : Colours::slateGrayDark);
        g.fillRoundedRectangle (clipLedArea, 2.0f);

        // Recessed bezel behind the LED column.
        g.setColour (Colours::charcoalBlack);
        g.fillRoundedRectangle (bounds, 3.0f);

        constexpr float minDb = -48.0f, maxDb = 6.0f;   // a little headroom above 0dB to show clipping clearly
        constexpr float safeCeilingDb = -6.0f;          // emerald below this
        constexpr float cautionCeilingDb = 0.0f;        // amber between safeCeiling and 0dB; crimson above

        // Discrete LED segments rather than a smooth gradient bar -- the
        // classic hardware bargraph look. Each segment is either fully lit
        // (its bottom edge is at or below the current peak) or fully dark.
        constexpr int numSegments = 20;
        constexpr float gap = 1.5f;
        const float segmentHeight = (bounds.getHeight() - gap * (numSegments - 1)) / (float) numSegments;

        for (int i = 0; i < numSegments; ++i)
        {
            // Segment 0 is at the bottom of the column.
            const float segTop = bounds.getBottom() - (float) (i + 1) * (segmentHeight + gap) + gap;
            juce::Rectangle<float> segment (bounds.getX(), segTop, bounds.getWidth(), segmentHeight);

            const float segBottomDb = minDb + ((float) i / (float) numSegments) * (maxDb - minDb);
            const bool lit = displayedPeakDb >= segBottomDb;

            juce::Colour zoneColour = Colours::emeraldGreen;
            if (segBottomDb >= cautionCeilingDb)       zoneColour = Colours::crimsonRed;
            else if (segBottomDb >= safeCeilingDb)      zoneColour = Colours::amberOrange;

            g.setColour (lit ? zoneColour : zoneColour.withAlpha (0.12f));
            g.fillRoundedRectangle (segment, 1.5f);
        }

        g.setColour (Colours::slateGray);
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    }
}
