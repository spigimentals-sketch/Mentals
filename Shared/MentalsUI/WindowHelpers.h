#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace MentalsUI
{
    // JUCE's Standalone app window deliberately omits the maximize button
    // (it only requests minimise + close). Call this from an editor's
    // parentHierarchyChanged() override to add it back once attached to that
    // window -- has no effect when hosted as a plugin inside a DAW, since
    // there the window chrome (including whether a maximize button exists at
    // all) is entirely host-controlled, not something a plugin can add to.
    inline void enableMaximiseButtonIfStandalone (juce::Component& editor)
    {
        if (auto* documentWindow = dynamic_cast<juce::DocumentWindow*> (editor.getTopLevelComponent()))
            documentWindow->setTitleBarButtonsRequired (juce::DocumentWindow::minimiseButton
                                                       | juce::DocumentWindow::maximiseButton
                                                       | juce::DocumentWindow::closeButton, false);
    }
}
