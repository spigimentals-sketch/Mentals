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
// a menu of every module type -- always all of them, since any type
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
// Master Assistant's popup panel content -- Load Reference / Capture My Mix
// / Apply, plus a status readout of each side's measured LUFS/crest/
// correlation once available. A plain (non-heap-owned) member reused across
// openings, following the same pattern as Mentals Multimode EQ's own
// EQ Match / AI Assist / Settings popups (see MentalsUI::launchPopup).
//==============================================================================
class MasterAssistantPanel : public juce::Component,
                              private juce::Button::Listener
{
public:
    explicit MasterAssistantPanel (MentalsSuiteAudioProcessor& proc);
    ~MasterAssistantPanel() override;

    void resized() override;
    void refresh(); // re-reads MasterAssistant's current state into the labels/buttons

    // Called after Apply -- the chain's composition may have changed (see
    // MasterAssistant::ensureModuleInChain), so the outer editor needs to
    // refresh its chain list.
    std::function<void()> onApplied;

private:
    void buttonClicked (juce::Button*) override;
    void updateStatusLabels();

    MentalsSuiteAudioProcessor& processor;

    juce::Label titleLabel { {}, "Master Assistant" };
    juce::Label hintLabel { {}, "Load a reference track, then Capture a few\nseconds of your own mix playing to compare." };
    juce::TextButton loadRefButton { "Load Reference..." };
    juce::Label referenceStatusLabel;
    juce::TextButton captureButton { "Capture My Mix" };
    juce::Label captureStatusLabel;
    juce::TextButton applyButton { "Apply" };
    juce::Label applyHintLabel;

    std::unique_ptr<juce::FileChooser> activeFileChooser;
    bool lastLoadFailed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterAssistantPanel)
};

//==============================================================================
// Mentals Suite's editor: the chain list on the left (add/remove/reorder/
// bypass/select), and the currently-selected instance's own, unmodified
// editor filling the rest of the window -- each module's UI is exactly what
// its standalone plugin shows, just hosted here instead. Shows a
// placeholder message when the chain is empty (nothing added yet).
//==============================================================================
class MentalsSuiteAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          private juce::Button::Listener,
                                          private juce::Timer
{
public:
    explicit MentalsSuiteAudioProcessorEditor (MentalsSuiteAudioProcessor&);
    ~MentalsSuiteAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showModule (int slotId);
    void removeModule (int slotId);
    void buttonClicked (juce::Button*) override;
    void timerCallback() override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }

    MentalsSuiteAudioProcessor& processor;

    juce::Label productNameLabel;
    juce::TextButton masterAssistantButton { "Master Assistant" };
    MasterAssistantPanel masterAssistantPanel;
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
