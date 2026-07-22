#include "MetallicLogoComponent.h"
#include "BinaryData.h"

namespace MentalsUI
{
    void MetallicLogoComponent::paint (juce::Graphics& g)
    {
        if (! logoImage.isValid())
            logoImage = juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize);

        if (! logoImage.isValid())
            return;

        auto bounds = getLocalBounds().toFloat();
        const auto transform = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                    .getTransformToFit (logoImage.getBounds().toFloat(), bounds);

        // Clip to the logo's own letterforms (its alpha shape) rather than
        // maintaining a second, white/metallic logo asset -- same wordmark
        // every other Mentals plugin uses, just recoloured.
        g.saveState();
        g.reduceClipRegion (logoImage, transform);

        juce::ColourGradient gradient (juce::Colour (0xfff4f4f6), bounds.getX(), bounds.getY(),
                                        juce::Colour (0xff4a4c50), bounds.getX(), bounds.getBottom(), false);
        gradient.addColour (0.5, juce::Colour (0xffa6a8ac));
        g.setGradientFill (gradient);
        g.fillRect (bounds);

        g.restoreState();
    }
}
