#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MentalsColours.h"

namespace MentalsUI
{
    //==============================================================================
    // Plain visual divider between two stacked sections of an editor. Purely
    // decorative -- whichever section above it is expected to fill exactly the
    // space available (see each plugin's resized()), so this never needs to be
    // draggable.
    //==============================================================================
    class SplitterBar : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Colours::slateGrayDark);

            g.setColour (Colours::slateGray);
            const auto bounds = getLocalBounds();
            const int cy = bounds.getCentreY();
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse ((float) bounds.getCentreX() + (float) i * 10.0f - 2.0f, (float) cy - 2.0f, 4.0f, 4.0f);
        }
    };
}
