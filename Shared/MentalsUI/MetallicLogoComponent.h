#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace MentalsUI
{
    //==============================================================================
    // The shared MENTALS wordmark (BinaryData::mentals_logo_png), recoloured
    // from its baked-in flat blue to a polished-chrome gradient matching
    // HardwareLookAndFeel's knobs -- reusing the image's own letterforms as a
    // clip mask (see the .cpp) rather than needing a second, separately
    // maintained logo asset for plugins using the hardware look.
    //==============================================================================
    class MetallicLogoComponent : public juce::Component
    {
    public:
        MetallicLogoComponent() = default;

        void paint (juce::Graphics& g) override;

    private:
        juce::Image logoImage; // loaded lazily on first paint -- keeps BinaryData.h out of this header

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetallicLogoComponent)
    };
}
