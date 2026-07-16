#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

//==============================================================================
// ChainRowComponent
//==============================================================================
ChainRowComponent::ChainRowComponent (ChainListComponent& ownerIn, MentalsSuiteAudioProcessor& processorIn, int moduleIdIn)
    : moduleId (moduleIdIn), owner (ownerIn), processor (processorIn)
{
    bypassButton.setToggleState (processor.isModuleBypassed (moduleId), juce::dontSendNotification);
    bypassButton.onClick = [this]
    {
        processor.setModuleBypassed (moduleId, bypassButton.getToggleState());
    };
    addAndMakeVisible (bypassButton);

    removeButton.onClick = [this] { owner.rowRemoveRequested (moduleId); };
    addAndMakeVisible (removeButton);
}

void ChainRowComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (isSelected ? MentalsUI::Colours::slateGray : MentalsUI::Colours::slateGrayDark);
    g.fillRoundedRectangle (bounds.reduced (2.0f), 4.0f);

    if (isSelected)
    {
        g.setColour (MentalsUI::Colours::electricBlue);
        g.drawRoundedRectangle (bounds.reduced (2.0f), 4.0f, 2.0f);
    }

    g.setColour (MentalsUI::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    g.drawText (MentalsSuiteAudioProcessor::getModuleName (moduleId),
                getLocalBounds().reduced (10, 6).removeFromTop (24),
                juce::Justification::centredLeft);

    // Drag handle hint -- three horizontal bars in the top-right corner.
    g.setColour (MentalsUI::Colours::slateGray);
    for (int i = 0; i < 3; ++i)
    {
        const auto y = 8 + i * 5;
        g.fillRect (getWidth() - 26, y, 18, 2);
    }
}

void ChainRowComponent::resized()
{
    auto bottomRow = getLocalBounds().reduced (10, 6).removeFromBottom (22);
    bypassButton.setBounds (bottomRow.removeFromLeft (80));
    removeButton.setBounds (bottomRow.removeFromRight (70));
}

void ChainRowComponent::mouseDown (const juce::MouseEvent& e)
{
    dragStartMouseY = e.getScreenPosition().getY();
    dragStartComponentY = getY();
    isDragging = false;
}

void ChainRowComponent::mouseDrag (const juce::MouseEvent& e)
{
    const auto dy = e.getScreenPosition().getY() - dragStartMouseY;

    if (! isDragging && std::abs (dy) < 4)
        return;

    isDragging = true;
    owner.rowDragged (moduleId, dragStartComponentY + dy);
}

void ChainRowComponent::mouseUp (const juce::MouseEvent&)
{
    if (isDragging)
        owner.rowDragEnded();
    else
        owner.rowClicked (moduleId);

    isDragging = false;
}

//==============================================================================
// ChainListComponent
//==============================================================================
ChainListComponent::ChainListComponent (MentalsSuiteAudioProcessor& proc)
    : processor (proc)
{
    for (int moduleId = 0; moduleId < (int) MentalsSuiteAudioProcessor::numModules; ++moduleId)
    {
        auto row = std::make_unique<ChainRowComponent> (*this, processor, moduleId);
        addChildComponent (*row); // not addAndMakeVisible -- refreshFromProcessor() below decides visibility
        rows[(size_t) moduleId] = std::move (row);
    }

    addButton.onClick = [this] { showAddMenu(); };
    addAndMakeVisible (addButton);

    refreshFromProcessor();
}

void ChainListComponent::resized()
{
    layoutRows();
}

void ChainListComponent::refreshFromProcessor()
{
    const auto order = processor.getChainOrder();
    visualOrder.assign (order.begin(), order.end());

    for (auto& row : rows)
        row->setVisible (std::find (visualOrder.begin(), visualOrder.end(), row->moduleId) != visualOrder.end());

    layoutRows();
}

void ChainListComponent::layoutRows (int excludeModuleId)
{
    for (int slot = 0; slot < (int) visualOrder.size(); ++slot)
    {
        const int moduleId = visualOrder[(size_t) slot];
        if (moduleId == excludeModuleId)
            continue;

        rows[(size_t) moduleId]->setBounds (0, slot * rowHeight, getWidth(), rowHeight - 4);
    }

    addButton.setBounds (10, (int) visualOrder.size() * rowHeight + 4, getWidth() - 20, addButtonHeight);
}

void ChainListComponent::rowDragged (int moduleId, int newScreenY)
{
    const int maxSlot = (int) visualOrder.size() - 1;

    // Let the dragged row follow the mouse directly...
    rows[(size_t) moduleId]->setTopLeftPosition (0, juce::jlimit (0, rowHeight * maxSlot, newScreenY));

    // ...and figure out which slot its centre now falls in, swapping it into
    // that slot in visualOrder if it's moved far enough.
    const int currentSlot = (int) std::distance (visualOrder.begin(),
        std::find (visualOrder.begin(), visualOrder.end(), moduleId));
    const int targetSlot = juce::jlimit (0, maxSlot, (newScreenY + rowHeight / 2) / rowHeight);

    if (targetSlot != currentSlot)
    {
        visualOrder.erase (visualOrder.begin() + currentSlot);
        visualOrder.insert (visualOrder.begin() + targetSlot, moduleId);
    }

    layoutRows (moduleId);
}

void ChainListComponent::rowDragEnded()
{
    layoutRows();
    processor.setChainOrder (visualOrder);
}

void ChainListComponent::rowClicked (int moduleId)
{
    selectedModuleId = moduleId;
    for (auto& row : rows)
        row->isSelected = (row->moduleId == moduleId);
    repaint();

    if (onModuleSelected)
        onModuleSelected (moduleId);
}

void ChainListComponent::rowRemoveRequested (int moduleId)
{
    const bool wasSelected = (selectedModuleId == moduleId);

    processor.removeModuleFromChain (moduleId);
    refreshFromProcessor();

    if (wasSelected)
        rowClicked (visualOrder.empty() ? -1 : visualOrder.front());
}

void ChainListComponent::showAddMenu()
{
    juce::PopupMenu menu;
    for (int moduleId = 0; moduleId < (int) MentalsSuiteAudioProcessor::numModules; ++moduleId)
        if (! processor.isModuleInChain (moduleId))
            menu.addItem (moduleId + 1, MentalsSuiteAudioProcessor::getModuleName (moduleId));

    if (menu.getNumItems() == 0)
        return;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton),
        [this] (int result)
        {
            if (result <= 0)
                return;

            const int moduleId = result - 1;
            processor.addModuleToChain (moduleId);
            refreshFromProcessor();
            rowClicked (moduleId);
        });
}

//==============================================================================
// MentalsSuiteAudioProcessorEditor
//==============================================================================
MentalsSuiteAudioProcessorEditor::MentalsSuiteAudioProcessorEditor (MentalsSuiteAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), chainList (p)
{
    productNameLabel.setText ("MENTALS SUITE", juce::dontSendNotification);
    productNameLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    productNameLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    addAndMakeVisible (productNameLabel);

    addAndMakeVisible (chainList);
    chainList.onModuleSelected = [this] (int moduleId) { showModule (moduleId); };

    moduleViewport.setScrollBarsShown (true, true);
    addAndMakeVisible (moduleViewport);

    emptyStateLabel.setJustificationType (juce::Justification::centred);
    emptyStateLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::slateGray);
    emptyStateLabel.setFont (juce::Font (juce::FontOptions (18.0f)));
    addAndMakeVisible (emptyStateLabel);

    setResizable (true, true);
    setResizeLimits (1000, 650, 1800, 1100);
    setSize (1100, 700);

    const auto initialOrder = processor.getChainOrder();
    showModule (initialOrder.empty() ? -1 : initialOrder.front());
}

MentalsSuiteAudioProcessorEditor::~MentalsSuiteAudioProcessorEditor()
{
    moduleViewport.setViewedComponent (nullptr, false);
}

void MentalsSuiteAudioProcessorEditor::showModule (int moduleId)
{
    if (currentlyShownModuleId == moduleId)
        return;

    currentlyShownModuleId = moduleId;
    chainList.rowClicked (moduleId);

    if (moduleId < 0)
    {
        moduleViewport.setViewedComponent (nullptr, false);
        moduleViewport.setVisible (false);
        emptyStateLabel.setVisible (true);
        return;
    }

    emptyStateLabel.setVisible (false);
    moduleViewport.setVisible (true);

    if (moduleEditors[(size_t) moduleId] == nullptr)
    {
        auto* innerProcessor = processor.getModuleProcessor (moduleId);
        moduleEditors[(size_t) moduleId].reset (innerProcessor->createEditorAndMakeActive());
    }

    moduleViewport.setViewedComponent (moduleEditors[(size_t) moduleId].get(), false);
}

void MentalsSuiteAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);
}

void MentalsSuiteAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    auto topBar = bounds.removeFromTop (topBarHeight);
    productNameLabel.setBounds (topBar.reduced (10, 0));

    chainList.setBounds (bounds.removeFromLeft (chainListWidth));
    moduleViewport.setBounds (bounds);
    emptyStateLabel.setBounds (bounds);
}
