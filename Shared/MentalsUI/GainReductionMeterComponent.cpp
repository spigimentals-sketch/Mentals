#include "GainReductionMeterComponent.h"

namespace MentalsUI
{
    void GainReductionMeterComponent::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (Colours::slateGrayDark);
        g.fillRoundedRectangle (bounds, 3.0f);

        constexpr float maxReductionDb = 24.0f; // full-scale range shown
        const float amount = juce::jlimit (0.0f, 1.0f, -displayedGainReductionDb / maxReductionDb);

        juce::Rectangle<float> lit (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight() * amount);
        g.setColour (Colours::electricBlue);
        g.fillRoundedRectangle (lit, 3.0f);

        g.setColour (Colours::slateGray);
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    }
}
