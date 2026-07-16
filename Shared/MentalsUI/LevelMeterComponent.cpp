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

        g.setColour (Colours::slateGrayDark);
        g.fillRoundedRectangle (bounds, 3.0f);

        constexpr float minDb = -48.0f, maxDb = 6.0f;   // a little headroom above 0dB to show clipping clearly
        constexpr float safeCeilingDb = -6.0f;          // emerald below this
        constexpr float cautionCeilingDb = 0.0f;        // amber between safeCeiling and 0dB; crimson above

        auto dbToY = [&] (float db)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
            return bounds.getBottom() - t * bounds.getHeight();
        };

        const float peakY    = dbToY (juce::jlimit (minDb, maxDb, displayedPeakDb));
        const float safeY    = dbToY (safeCeilingDb);
        const float cautionY = dbToY (cautionCeilingDb);

        // Only the portion of the bar from the current peak down to the bottom
        // is "lit"; clipping to that region and drawing the three full-height
        // colour zones inside it gives the classic segmented meter look.
        juce::Rectangle<float> lit (bounds.getX(), peakY, bounds.getWidth(), bounds.getBottom() - peakY);

        g.saveState();
        g.reduceClipRegion (lit.getSmallestIntegerContainer());

        g.setColour (Colours::emeraldGreen);
        g.fillRect (juce::Rectangle<float> (bounds.getX(), safeY, bounds.getWidth(), bounds.getBottom() - safeY));

        g.setColour (Colours::amberOrange);
        g.fillRect (juce::Rectangle<float> (bounds.getX(), cautionY, bounds.getWidth(), safeY - cautionY));

        g.setColour (Colours::crimsonRed);
        g.fillRect (juce::Rectangle<float> (bounds.getX(), bounds.getY(), bounds.getWidth(), cautionY - bounds.getY()));

        g.restoreState();

        g.setColour (Colours::slateGray);
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    }
}
