#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"
#include <map>
#include <memory>
#include <vector>

//==============================================================================
// One row in the chain list, one per instance currently in the chain
// (several rows can show the same module type, e.g. "EQ 1"/"EQ 2"): its
// display name, a Bypass toggle (stays in the chain but passes through
// unprocessed), and a Remove button (takes it out of the chain and destroys
// it for good -- see ChainListComponent's "+ Add" button for adding a fresh
// instance of any type back in). Acts as a tab selector (click to view that
// instance) and a drag handle (drag to move it earlier/later in the signal
// chain). A row's slotId never changes -- only its on-screen position does,
// as the chain is reordered.
//==============================================================================
class ChainListComponent;

class ChainRowComponent : public juce::Component
{
public:
    ChainRowComponent (ChainListComponent& ownerIn, MentalsSuiteAudioProcessor& processorIn, int slotIdIn, int moduleTypeIn);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void setDisplayName (const juce::String& name) { displayName = name; repaint(); }

    int slotId;
    int moduleType;
    bool isSelected = false;

private:
    ChainListComponent& owner;
    MentalsSuiteAudioProcessor& processor;
    juce::String displayName;
    juce::ToggleButton bypassButton { "Bypass" };
    juce::TextButton removeButton { "Remove" };
    int dragStartMouseY = 0, dragStartComponentY = 0;
    bool isDragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainRowComponent)
};

//==============================================================================
// Vertical list of rows, one per instance currently in the chain (possibly
// none, possibly several of the same module type), laid out in the
// processor's current chain order, plus an "+ Add Module" button that opens
// a menu of all seven module types -- always all seven, since any of them
// can be added more than once. Dragging a row past a neighbour reorders the
// underlying chain live (see MentalsSuiteAudioProcessor::setChainOrder());
// clicking one without dragging selects it for viewing.
//==============================================================================
class ChainListComponent : public juce::Component
{
public:
    explicit ChainListComponent (MentalsSuiteAudioProcessor& proc);

    void resized() override;

    // Called by a row on drag/click -- slotId identifies which row.
    void rowDragged (int slotId, int newScreenY);
    void rowDragEnded();
    void rowClicked (int slotId);
    void rowRemoveRequested (int slotId);

    // Re-reads the processor's current chain membership/order (e.g. after
    // an add/remove, or after loading saved state) and rebuilds the rows.
    // NOT called mid-drag -- dragging only reorders the existing rows.
    void refreshFromProcessor();

    // Called with the slot that should now be shown, or -1 if the chain is
    // now empty. Actually removing a slot's processor/editor is the outer
    // editor's job (see onModuleRemoveRequested), not this component's --
    // it owns the editor cache that must be torn down in the right order.
    std::function<void (int)> onModuleSelected;
    std::function<void (int)> onModuleRemoveRequested;
    int selectedSlotId = -1;

    static constexpr int rowHeight = 56;
    static constexpr int addButtonHeight = 32;

private:
    void layoutRows (int excludeSlotId = -1);
    void showAddMenu();
    void updateDisplayNames();

    MentalsSuiteAudioProcessor& processor;
    std::vector<std::unique_ptr<ChainRowComponent>> rows; // parallel to visualOrder
    std::vector<int> visualOrder; // slot IDs, in chain order
    juce::TextButton addButton { "+ Add Module" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainListComponent)
};

//==============================================================================
// Mentals Suite's editor: the chain list on the left (add/remove/reorder/
// bypass/select), and the currently-selected instance's own, unmodified
// editor filling the rest of the window -- each module's UI is exactly what
// its standalone plugin shows, just hosted here instead. Shows a
// placeholder message when the chain is empty (nothing added yet).
//==============================================================================
class MentalsSuiteAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit MentalsSuiteAudioProcessorEditor (MentalsSuiteAudioProcessor&);
    ~MentalsSuiteAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showModule (int slotId);
    void removeModule (int slotId);
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }

    MentalsSuiteAudioProcessor& processor;

    juce::Label productNameLabel;
    ChainListComponent chainList;
    juce::Viewport moduleViewport;
    juce::Label emptyStateLabel { {}, "No plugins in the chain yet -- click \"+ Add Module\" to begin." };

    // Keyed by slotId rather than module type, since several instances of
    // the same type can now coexist, each with its own independent editor.
    std::map<int, std::unique_ptr<juce::AudioProcessorEditor>> moduleEditors;
    int currentlyShownSlotId = -1;

    static constexpr int chainListWidth = 220;
    static constexpr int topBarHeight = 40;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsSuiteAudioProcessorEditor)
};
