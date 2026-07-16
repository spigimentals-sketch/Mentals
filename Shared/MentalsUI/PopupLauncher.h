#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace MentalsUI
{
    //==============================================================================
    // Launches a non-owning CallOutBox showing contentComponent, anchored to
    // anchorButton, self-deleting on dismissal. Used for every "click a top-bar
    // button, show a small popup panel" pattern (Tools, AI Assist, EQ Match,
    // etc.) so each plugin doesn't reimplement the CallOutBox setup by hand.
    //
    // contentComponent must be a plain (non-heap-owned) member that outlives
    // the popup and can be reused across openings -- this does NOT take
    // ownership of it, unlike CallOutBox::launchAsynchronously.
    //==============================================================================
    inline void launchPopup (juce::Component& contentComponent, juce::Component& anchorButton)
    {
        auto* callOut = new juce::CallOutBox (contentComponent, anchorButton.getScreenBounds(), nullptr);
        callOut->setDismissalMouseClicksAreAlwaysConsumed (true);
        callOut->setVisible (true);
        callOut->enterModalState (true, nullptr, true);
    }
}
