#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"
#include <map>
#include <memory>
#include <vector>

//==============================================================================
// One card in the module rack -- a horizontal strip of these across the top
// of the window, Ozone-style, one per instance currently in the chain
// (several cards can show the same module type, e.g. "EQ 1"/"EQ 2"): a
// small glyph identifying what kind of module it is, its display name, a
// power toggle (lit electric-blue = active; dim = bypassed, staying in the
// chain but passing through unprocessed), and a Remove button (takes it out
// of the chain and destroys it for good -- see ChainListComponent's "+" card
// for adding a fresh instance of any type back in). Acts as a tab selector
// (click to view that instance) and a drag handle (drag left/right to move
// it earlier/later in the signal chain). A card's slotId never changes --
// only its on-screen position does, as the chain is reordered.
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
    juce::TextButton powerToggle { {} };
    juce::TextButton removeButton { "x" };
    int dragStartMouseX = 0, dragStartComponentX = 0;
    bool isDragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainRowComponent)
};

//==============================================================================
// Horizontal rack of cards, one per instance currently in the chain
// (possibly none, possibly several of the same module type), laid out
// left-to-right in the processor's current chain order, plus a "+" card
// that opens a menu of every module type -- always all of them, since any
// type can be added more than once. Sized to fit its own content (grows
// wider as modules are added) and hosted inside the outer editor's
// horizontally-scrolling rackViewport. Dragging a card past a neighbour
// reorders the underlying chain live (see
// MentalsSuiteAudioProcessor::setChainOrder()); clicking one without
// dragging selects it for viewing.
//==============================================================================
class ChainListComponent : public juce::Component
{
public:
    explicit ChainListComponent (MentalsSuiteAudioProcessor& proc);

    void resized() override;

    // Called by a card on drag/click -- slotId identifies which card.
    void rowDragged (int slotId, int newScreenX);
    void rowDragEnded();
    void rowClicked (int slotId);
    void rowRemoveRequested (int slotId);

    // Re-reads the processor's current chain membership/order (e.g. after
    // an add/remove, or after loading saved state) and rebuilds the cards.
    // NOT called mid-drag -- dragging only reorders the existing cards.
    void refreshFromProcessor();

    // Called with the slot that should now be shown, or -1 if the chain is
    // now empty. Actually removing a slot's processor/editor is the outer
    // editor's job (see onModuleRemoveRequested), not this component's --
    // it owns the editor cache that must be torn down in the right order.
    std::function<void (int)> onModuleSelected;
    std::function<void (int)> onModuleRemoveRequested;
    int selectedSlotId = -1;

    static constexpr int cardWidth = 132;
    static constexpr int cardHeight = 96;
    static constexpr int addCardWidth = 72;

private:
    void layoutRows (int excludeSlotId = -1);
    void showAddMenu();
    void updateDisplayNames();

    MentalsSuiteAudioProcessor& processor;
    std::vector<std::unique_ptr<ChainRowComponent>> rows; // parallel to visualOrder
    std::vector<int> visualOrder; // slot IDs, in chain order
    juce::TextButton addButton { "+" };

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
// Mentals Suite's editor: an Ozone-style horizontal module rack across the
// top (add/remove/reorder/bypass/select, scrolling sideways once the chain
// outgrows the window) below a dark title bar, and the currently-selected
// instance's own, unmodified editor filling the rest of the window -- each
// module's UI is exactly what its standalone plugin shows, just hosted here
// instead. Shows a placeholder message when the chain is empty (nothing
// added yet).
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

    // Grows (or shrinks) the currently-shown module's own editor to fill
    // moduleViewport's visible area, clamped to that editor's own
    // setResizeLimits() -- otherwise a maximized Suite window would just
    // leave the hosted editor at whatever size it last was, surrounded by
    // dead space (or forcing scrollbars) instead of actually using the
    // room a bigger window gives it.
    void fitModuleEditorToViewport();

    MentalsSuiteAudioProcessor& processor;

    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::Label chainSummaryLabel; // "N modules" subtitle under the wordmark, Ozone-title-bar-style
    juce::TextButton masterAssistantButton { "Master Assistant" };
    MasterAssistantPanel masterAssistantPanel;

    ChainListComponent chainList;
    juce::Viewport rackViewport; // horizontal-only -- hosts chainList, scrolls once the rack outgrows the window
    MentalsUI::SplitterBar splitter;
    juce::Viewport moduleViewport;
    juce::Label emptyStateLabel { {}, "No plugins in the chain yet -- click \"+\" to begin." };

    // Keyed by slotId rather than module type, since several instances of
    // the same type can now coexist, each with its own independent editor.
    std::map<int, std::unique_ptr<juce::AudioProcessorEditor>> moduleEditors;
    int currentlyShownSlotId = -1;

    static constexpr int topBarHeight = 56;
    static constexpr int rackHeight = ChainListComponent::cardHeight + 16;
    static constexpr int splitterHeight = 6;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsSuiteAudioProcessorEditor)
};
