#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"
#include <array>
#include <memory>
#include <vector>

//==============================================================================
// One row in the chain list: shows a module's name and bypass toggle, acts as
// a tab selector (click to view that module) and a drag handle (drag to move
// it earlier/later in the signal chain). A row's ModuleId never changes --
// only its on-screen position does, as the chain is reordered.
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
    int dragStartMouseY = 0, dragStartComponentY = 0;
    bool isDragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainRowComponent)
};

//==============================================================================
// Vertical list of the seven ChainRowComponents, laid out in the processor's
// current chain order. Dragging a row past a neighbour reorders the
// underlying chain live (see MentalsSuiteAudioProcessor::setChainOrder());
// clicking one without dragging selects it for viewing.
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

    std::function<void (int)> onModuleSelected;
    int selectedModuleId = -1;

    static constexpr int rowHeight = 56;

private:
    void layoutRows (int excludeModuleId = -1);

    MentalsSuiteAudioProcessor& processor;
    std::array<std::unique_ptr<ChainRowComponent>, (size_t) MentalsSuiteAudioProcessor::numModules> rows;
    std::vector<int> visualOrder; // needs erase/insert while dragging -- std::array can't do that

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainListComponent)
};

//==============================================================================
// Mentals Suite's editor: the chain list on the left (reorder + bypass +
// module selection), and the currently-selected module's own, unmodified
// editor filling the rest of the window -- each module's UI is exactly what
// its standalone plugin shows, just hosted here instead.
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

    std::array<std::unique_ptr<juce::AudioProcessorEditor>, (size_t) MentalsSuiteAudioProcessor::numModules> moduleEditors;
    int currentlyShownModuleId = -1;

    static constexpr int chainListWidth = 220;
    static constexpr int topBarHeight = 40;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsSuiteAudioProcessorEditor)
};
