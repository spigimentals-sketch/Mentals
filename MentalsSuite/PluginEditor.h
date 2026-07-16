#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"
#include <array>
#include <memory>
#include <vector>

//==============================================================================
// One row in the chain list, shown only while its module is actually in the
// chain: name, a Bypass toggle (stays in the chain but passes through
// unprocessed), and a Remove button (takes it out of the chain entirely --
// see ChainListComponent's "+ Add" button for putting it back). Acts as a
// tab selector (click to view that module) and a drag handle (drag to move
// it earlier/later in the signal chain). A row's ModuleId never changes --
// only its on-screen position/visibility does.
//==============================================================================
class ChainListComponent;

class ChainRowComponent : public juce::Component
{
public:
    ChainRowComponent (ChainListComponent& ownerIn, MentalsSuiteAudioProcessor& processorIn, int moduleIdIn);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    int moduleId;
    bool isSelected = false;

private:
    ChainListComponent& owner;
    MentalsSuiteAudioProcessor& processor;
    juce::ToggleButton bypassButton { "Bypass" };
    juce::TextButton removeButton { "Remove" };
    int dragStartMouseY = 0, dragStartComponentY = 0;
    bool isDragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainRowComponent)
};

//==============================================================================
// Vertical list of the modules currently IN the chain (a subset of all seven,
// possibly empty), laid out in the processor's current chain order, plus an
// "+ Add Module" button that opens a menu of whichever modules aren't in the
// chain yet. Dragging a row past a neighbour reorders the underlying chain
// live (see MentalsSuiteAudioProcessor::setChainOrder()); clicking one
// without dragging selects it for viewing.
//==============================================================================
class ChainListComponent : public juce::Component
{
public:
    explicit ChainListComponent (MentalsSuiteAudioProcessor& proc);

    void resized() override;

    // Called by a row on drag/click -- moduleId identifies which row.
    void rowDragged (int moduleId, int newScreenY);
    void rowDragEnded();
    void rowClicked (int moduleId);
    void rowRemoveRequested (int moduleId);

    // Re-reads the processor's current chain membership/order (e.g. after
    // loading saved state) and refreshes which rows are shown.
    void refreshFromProcessor();

    // Called with the module that should now be shown, or -1 if the chain
    // is now empty (e.g. the module being viewed was just removed).
    std::function<void (int)> onModuleSelected;
    int selectedModuleId = -1;

    static constexpr int rowHeight = 56;
    static constexpr int addButtonHeight = 32;

private:
    void layoutRows (int excludeModuleId = -1);
    void showAddMenu();

    MentalsSuiteAudioProcessor& processor;
    std::array<std::unique_ptr<ChainRowComponent>, (size_t) MentalsSuiteAudioProcessor::numModules> rows;
    std::vector<int> visualOrder; // the modules currently in the chain, in order
    juce::TextButton addButton { "+ Add Module" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainListComponent)
};

//==============================================================================
// Mentals Suite's editor: the chain list on the left (add/remove/reorder/
// bypass/select), and the currently-selected module's own, unmodified editor
// filling the rest of the window -- each module's UI is exactly what its
// standalone plugin shows, just hosted here instead. Shows a placeholder
// message when the chain is empty (nothing added yet).
//==============================================================================
class MentalsSuiteAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit MentalsSuiteAudioProcessorEditor (MentalsSuiteAudioProcessor&);
    ~MentalsSuiteAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showModule (int moduleId);
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }

    MentalsSuiteAudioProcessor& processor;

    juce::Label productNameLabel;
    ChainListComponent chainList;
    juce::Viewport moduleViewport;
    juce::Label emptyStateLabel { {}, "No plugins in the chain yet -- click \"+ Add Module\" to begin." };

    std::array<std::unique_ptr<juce::AudioProcessorEditor>, (size_t) MentalsSuiteAudioProcessor::numModules> moduleEditors;
    int currentlyShownModuleId = -1;

    static constexpr int chainListWidth = 220;
    static constexpr int topBarHeight = 40;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsSuiteAudioProcessorEditor)
};
