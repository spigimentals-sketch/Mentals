#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <array>

//==============================================================================
// ChainRowComponent
//==============================================================================
ChainRowComponent::ChainRowComponent (ChainListComponent& ownerIn, MentalsSuiteAudioProcessor& processorIn, int slotIdIn, int moduleTypeIn)
    : slotId (slotIdIn), moduleType (moduleTypeIn), owner (ownerIn), processor (processorIn),
      displayName (MentalsSuiteAudioProcessor::getModuleTypeName (moduleTypeIn))
{
    bypassButton.setToggleState (processor.isSlotBypassed (slotId), juce::dontSendNotification);
    bypassButton.onClick = [this]
    {
        processor.setSlotBypassed (slotId, bypassButton.getToggleState());
    };
    addAndMakeVisible (bypassButton);

    removeButton.onClick = [this] { owner.rowRemoveRequested (slotId); };
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
    g.drawText (displayName,
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
    owner.rowDragged (slotId, dragStartComponentY + dy);
}

void ChainRowComponent::mouseUp (const juce::MouseEvent&)
{
    if (isDragging)
        owner.rowDragEnded();
    else
        owner.rowClicked (slotId);

    isDragging = false;
}

//==============================================================================
// ChainListComponent
//==============================================================================
ChainListComponent::ChainListComponent (MentalsSuiteAudioProcessor& proc)
    : processor (proc)
{
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
    // Full teardown/rebuild rather than diffing -- only ever called after an
    // add/remove or a state load, never during a drag (which just repositions
    // the existing row components), so the cost of recreating them is a
    // non-issue.
    rows.clear();
    visualOrder.clear();

    for (auto& slot : processor.getChainSlots())
    {
        visualOrder.push_back (slot.slotId);
        auto row = std::make_unique<ChainRowComponent> (*this, processor, slot.slotId, slot.moduleType);
        row->isSelected = (slot.slotId == selectedSlotId);
        addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    updateDisplayNames();
    layoutRows();
}

void ChainListComponent::updateDisplayNames()
{
    // Number duplicate module types ("EQ 1", "EQ 2", ...) so instances of
    // the same type are distinguishable; a type that appears only once is
    // shown plainly.
    std::array<int, (size_t) MentalsSuiteAudioProcessor::numModuleTypes> totalCount {};
    for (auto& row : rows)
        ++totalCount[(size_t) row->moduleType];

    std::array<int, (size_t) MentalsSuiteAudioProcessor::numModuleTypes> seenSoFar {};
    for (auto& row : rows)
    {
        const int type = row->moduleType;
        const juce::String baseName = MentalsSuiteAudioProcessor::getModuleTypeName (type);
        if (totalCount[(size_t) type] > 1)
            row->setDisplayName (baseName + " " + juce::String (++seenSoFar[(size_t) type]));
        else
            row->setDisplayName (baseName);
    }
}

void ChainListComponent::layoutRows (int excludeSlotId)
{
    for (int slot = 0; slot < (int) visualOrder.size(); ++slot)
    {
        if (visualOrder[(size_t) slot] == excludeSlotId)
            continue;

        rows[(size_t) slot]->setBounds (0, slot * rowHeight, getWidth(), rowHeight - 4);
    }

    addButton.setBounds (10, (int) visualOrder.size() * rowHeight + 4, getWidth() - 20, addButtonHeight);
}

void ChainListComponent::rowDragged (int slotId, int newScreenY)
{
    const int maxSlot = (int) visualOrder.size() - 1;
    const int currentSlot = (int) std::distance (visualOrder.begin(),
        std::find (visualOrder.begin(), visualOrder.end(), slotId));

    // Let the dragged row follow the mouse directly...
    rows[(size_t) currentSlot]->setTopLeftPosition (0, juce::jlimit (0, rowHeight * maxSlot, newScreenY));

    // ...and figure out which slot its centre now falls in, moving both the
    // row and its slot ID into that slot together if it's moved far enough.
    const int targetSlot = juce::jlimit (0, maxSlot, (newScreenY + rowHeight / 2) / rowHeight);

    if (targetSlot != currentSlot)
    {
        auto rowPtr = std::move (rows[(size_t) currentSlot]);
        rows.erase (rows.begin() + currentSlot);
        rows.insert (rows.begin() + targetSlot, std::move (rowPtr));

        visualOrder.erase (visualOrder.begin() + currentSlot);
        visualOrder.insert (visualOrder.begin() + targetSlot, slotId);
    }

    layoutRows (slotId);
}

void ChainListComponent::rowDragEnded()
{
    layoutRows();
    processor.setChainOrder (visualOrder);
}

void ChainListComponent::rowClicked (int slotId)
{
    selectedSlotId = slotId;
    for (auto& row : rows)
        row->isSelected = (row->slotId == slotId);
    repaint();

    if (onModuleSelected)
        onModuleSelected (slotId);
}

void ChainListComponent::rowRemoveRequested (int slotId)
{
    if (onModuleRemoveRequested)
        onModuleRemoveRequested (slotId);
}

void ChainListComponent::showAddMenu()
{
    // Always every module type -- unlike before, a type already in the chain isn't
    // filtered out, since adding another instance of it is exactly the point.
    juce::PopupMenu menu;
    for (int moduleType = 0; moduleType < (int) MentalsSuiteAudioProcessor::numModuleTypes; ++moduleType)
        menu.addItem (moduleType + 1, MentalsSuiteAudioProcessor::getModuleTypeName (moduleType));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton),
        [this] (int result)
        {
            if (result <= 0)
                return;

            const int moduleType = result - 1;
            const int newSlotId = processor.addModuleToChain (moduleType);
            refreshFromProcessor();
            rowClicked (newSlotId);
        });
}

//==============================================================================
// MasterAssistantPanel
//==============================================================================
MasterAssistantPanel::MasterAssistantPanel (MentalsSuiteAudioProcessor& proc)
    : processor (proc)
{
    titleLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    addAndMakeVisible (titleLabel);

    hintLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    hintLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::slateGray);
    hintLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hintLabel);

    for (auto* b : { &loadRefButton, &captureButton, &applyButton })
    {
        b->addListener (this);
        addAndMakeVisible (*b);
    }

    for (auto* l : { &referenceStatusLabel, &captureStatusLabel })
    {
        l->setFont (juce::Font (juce::FontOptions (12.0f)));
        l->setColour (juce::Label::textColourId, MentalsUI::Colours::white);
        addAndMakeVisible (*l);
    }

    applyHintLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    applyHintLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::slateGray);
    addAndMakeVisible (applyHintLabel);

    setSize (360, 250);
    updateStatusLabels();
}

MasterAssistantPanel::~MasterAssistantPanel()
{
    for (auto* b : { &loadRefButton, &captureButton, &applyButton })
        b->removeListener (this);
}

void MasterAssistantPanel::resized()
{
    auto g = getLocalBounds().reduced (10);

    titleLabel.setBounds (g.removeFromTop (20));
    g.removeFromTop (4);
    hintLabel.setBounds (g.removeFromTop (32));
    g.removeFromTop (8);

    loadRefButton.setBounds (g.removeFromTop (26));
    g.removeFromTop (4);
    referenceStatusLabel.setBounds (g.removeFromTop (18));
    g.removeFromTop (10);

    captureButton.setBounds (g.removeFromTop (26));
    g.removeFromTop (4);
    captureStatusLabel.setBounds (g.removeFromTop (18));
    g.removeFromTop (10);

    applyButton.setBounds (g.removeFromTop (26));
    g.removeFromTop (4);
    applyHintLabel.setBounds (g.removeFromTop (18));
}

void MasterAssistantPanel::refresh()
{
    updateStatusLabels();
}

void MasterAssistantPanel::buttonClicked (juce::Button* button)
{
    auto& assistant = processor.getMasterAssistant();

    if (button == &loadRefButton)
    {
        activeFileChooser = std::make_unique<juce::FileChooser> (
            "Select a reference audio file...", juce::File(), "*.wav;*.aiff;*.mp3;*.flac;*.ogg");

        activeFileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (! file.existsAsFile())
                    return; // user cancelled -- leave whatever was already loaded alone

                lastLoadFailed = ! processor.getMasterAssistant().loadReferenceFile (file);
                updateStatusLabels();
            });
        return;
    }

    if (button == &captureButton)
    {
        assistant.beginCapture();
        updateStatusLabels();
        return;
    }

    if (button == &applyButton)
    {
        assistant.applyToChain();
        updateStatusLabels();
        if (onApplied)
            onApplied(); // chain composition may have changed -- see MasterAssistant::ensureModuleInChain
        return;
    }
}

void MasterAssistantPanel::updateStatusLabels()
{
    auto& assistant = processor.getMasterAssistant();

    if (! assistant.hasReference())
        referenceStatusLabel.setText (lastLoadFailed ? "Failed to load -- unsupported or unreadable file"
                                                       : "No reference loaded yet",
                                       juce::dontSendNotification);
    else
        referenceStatusLabel.setText (
            "\"" + assistant.getReferenceFileName() + "\"  " + juce::String (assistant.getReferenceLufs(), 1)
                + " LUFS, crest " + juce::String (assistant.getReferenceCrestDb(), 1)
                + " dB, corr " + juce::String (assistant.getReferenceCorrelation(), 2),
            juce::dontSendNotification);

    if (assistant.isCapturing())
        captureStatusLabel.setText (
            "Capturing... " + juce::String ((int) (assistant.getCaptureProgress() * 100.0f)) + "% -- keep it playing",
            juce::dontSendNotification);
    else if (! assistant.isCaptureReady())
        captureStatusLabel.setText ("Not captured yet -- play your mix, then click Capture",
                                     juce::dontSendNotification);
    else
        captureStatusLabel.setText (
            "My mix: " + juce::String (assistant.getCapturedLufs(), 1) + " LUFS, crest "
                + juce::String (assistant.getCapturedCrestDb(), 1) + " dB, corr "
                + juce::String (assistant.getCapturedCorrelation(), 2),
            juce::dontSendNotification);

    applyButton.setEnabled (assistant.canApply());
    applyHintLabel.setText (assistant.canApply()
                                 ? "Nudges EQ/Comp/Width/Limiter toward the reference"
                                 : "Load a reference and capture your mix first",
                             juce::dontSendNotification);
}

//==============================================================================
// MentalsSuiteAudioProcessorEditor
//==============================================================================
MentalsSuiteAudioProcessorEditor::MentalsSuiteAudioProcessorEditor (MentalsSuiteAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), masterAssistantPanel (p), chainList (p)
{
    productNameLabel.setText ("MENTALS SUITE", juce::dontSendNotification);
    productNameLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    productNameLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    addAndMakeVisible (productNameLabel);

    masterAssistantButton.setColour (juce::TextButton::buttonColourId, MentalsUI::Colours::slateGrayDark);
    masterAssistantButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
    masterAssistantButton.addListener (this);
    addAndMakeVisible (masterAssistantButton);
    masterAssistantPanel.onApplied = [this] { chainList.refreshFromProcessor(); };

    addAndMakeVisible (chainList);
    chainList.onModuleSelected = [this] (int slotId) { showModule (slotId); };
    chainList.onModuleRemoveRequested = [this] (int slotId) { removeModule (slotId); };

    moduleViewport.setScrollBarsShown (true, true);
    addAndMakeVisible (moduleViewport);

    emptyStateLabel.setJustificationType (juce::Justification::centred);
    emptyStateLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::slateGray);
    emptyStateLabel.setFont (juce::Font (juce::FontOptions (18.0f)));
    addAndMakeVisible (emptyStateLabel);

    setResizable (true, true);
    setResizeLimits (1000, 650, 1800, 1100);
    setSize (1100, 700);

    const auto initialSlots = processor.getChainSlots();
    showModule (initialSlots.empty() ? -1 : initialSlots.front().slotId);

    startTimerHz (10); // refreshes Master Assistant's capture-progress readout while its popup is open
}

MentalsSuiteAudioProcessorEditor::~MentalsSuiteAudioProcessorEditor()
{
    stopTimer();
    masterAssistantButton.removeListener (this);
    moduleViewport.setViewedComponent (nullptr, false);
}

void MentalsSuiteAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &masterAssistantButton)
    {
        masterAssistantPanel.refresh();
        MentalsUI::launchPopup (masterAssistantPanel, masterAssistantButton);
    }
}

void MentalsSuiteAudioProcessorEditor::timerCallback()
{
    masterAssistantPanel.refresh();
}

void MentalsSuiteAudioProcessorEditor::showModule (int slotId)
{
    if (currentlyShownSlotId == slotId)
        return;

    currentlyShownSlotId = slotId;
    chainList.rowClicked (slotId);

    if (slotId < 0)
    {
        moduleViewport.setViewedComponent (nullptr, false);
        moduleViewport.setVisible (false);
        emptyStateLabel.setVisible (true);
        return;
    }

    emptyStateLabel.setVisible (false);
    moduleViewport.setVisible (true);

    if (moduleEditors.find (slotId) == moduleEditors.end())
    {
        auto* innerProcessor = processor.getSlotProcessor (slotId);
        if (innerProcessor == nullptr)
            return; // slot vanished between selection and this call -- nothing to show

        moduleEditors[slotId].reset (innerProcessor->createEditorAndMakeActive());
    }

    moduleViewport.setViewedComponent (moduleEditors[slotId].get(), false);
}

void MentalsSuiteAudioProcessorEditor::removeModule (int slotId)
{
    const bool wasShowing = (currentlyShownSlotId == slotId);

    // Release the viewport's reference before the editor it points to gets
    // destroyed just below -- currentlyShownSlotId is deliberately left
    // untouched here (still equal to the slot being removed) so the
    // showModule() call at the end doesn't no-op against its own new target.
    if (wasShowing)
        moduleViewport.setViewedComponent (nullptr, false);

    moduleEditors.erase (slotId); // destroy the editor before its underlying processor goes away
    processor.removeModuleFromChain (slotId);
    chainList.refreshFromProcessor();

    if (wasShowing)
    {
        const auto remaining = processor.getChainSlots();
        showModule (remaining.empty() ? -1 : remaining.front().slotId);
    }
}

void MentalsSuiteAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);
}

void MentalsSuiteAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    auto topBar = bounds.removeFromTop (topBarHeight);
    topBar = topBar.reduced (10, 4);
    masterAssistantButton.setBounds (topBar.removeFromRight (150));
    productNameLabel.setBounds (topBar);

    chainList.setBounds (bounds.removeFromLeft (chainListWidth));
    moduleViewport.setBounds (bounds);
    emptyStateLabel.setBounds (bounds);
}
