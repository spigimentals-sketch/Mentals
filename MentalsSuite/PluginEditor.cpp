#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include <algorithm>
#include <array>

namespace
{
    //==========================================================================
    // Small representative glyph per module type, drawn in a single accent
    // colour -- deliberately simple line-art rather than literal icons, so
    // every card reads as one family (matching this project's one-accent-
    // colour convention, see MentalsUI::Colours) while still being tellable
    // apart from across the rack at a glance.
    //==========================================================================
    void drawModuleGlyph (juce::Graphics& g, juce::Rectangle<float> b, int moduleType, juce::Colour colour)
    {
        using Module = MentalsSuiteAudioProcessor;
        g.setColour (colour);
        const juce::PathStrokeType stroke (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        const float x0 = b.getX(), y0 = b.getY(), x1 = b.getRight(), y1 = b.getBottom();
        const float w = b.getWidth(), h = b.getHeight();
        const float midY = b.getCentreY();
        juce::Path p;

        switch (moduleType)
        {
            case Module::moduleEQ:
            case Module::moduleExciterEQ:
            {
                p.startNewSubPath (x0, midY + h * 0.18f);
                p.cubicTo (x0 + w * 0.25f, midY - h * 0.35f, x0 + w * 0.35f, midY - h * 0.35f, x0 + w * 0.5f, midY);
                p.cubicTo (x0 + w * 0.65f, midY + h * 0.3f, x0 + w * 0.75f, midY + h * 0.3f, x1, midY - h * 0.1f);
                g.strokePath (p, stroke);
                break;
            }
            case Module::moduleCompressor:
            case Module::moduleCircuitComp:
            {
                const float barW = w * 0.16f;
                const float heights[3] = { h * 0.75f, h * 0.5f, h * 0.32f };
                for (int i = 0; i < 3; ++i)
                {
                    const float bx = x0 + w * (0.14f + i * 0.3f);
                    g.fillRoundedRectangle (bx, y1 - heights[(size_t) i], barW, heights[(size_t) i], 1.0f);
                }
                g.setColour (colour.withAlpha (0.55f));
                g.drawHorizontalLine ((int) midY, x0, x1);
                break;
            }
            case Module::moduleSaturator:
            {
                p.startNewSubPath (x0, midY);
                p.lineTo (x0 + w * 0.28f, midY);
                p.lineTo (x0 + w * 0.28f, y0 + h * 0.18f);
                p.lineTo (x0 + w * 0.5f, y0 + h * 0.18f);
                p.lineTo (x0 + w * 0.5f, y1 - h * 0.18f);
                p.lineTo (x0 + w * 0.72f, y1 - h * 0.18f);
                p.lineTo (x0 + w * 0.72f, midY);
                p.lineTo (x1, midY);
                g.strokePath (p, stroke);
                break;
            }
            case Module::moduleAutotune:
            {
                const float r = h * 0.26f;
                g.fillEllipse (x0 + w * 0.14f, y1 - r * 1.9f, r * 1.5f, r);
                g.fillRect (x0 + w * 0.14f + r * 1.1f, y0 + h * 0.12f, w * 0.05f, h * 0.66f);
                g.fillRect (x0 + w * 0.14f + r * 1.1f, y0 + h * 0.12f, w * 0.34f, h * 0.09f);
                break;
            }
            case Module::moduleDelay:
            {
                for (int i = 0; i < 3; ++i)
                {
                    const float d = w * 0.22f - (float) i * w * 0.05f;
                    const float cx = x0 + w * (0.22f + (float) i * 0.28f);
                    g.setColour (colour.withAlpha (1.0f - (float) i * 0.3f));
                    g.drawEllipse (cx - d * 0.5f, midY - d * 0.5f, d, d, 1.6f);
                }
                break;
            }
            case Module::moduleReverb:
            {
                for (int i = 0; i < 3; ++i)
                {
                    const float r = w * (0.16f + (float) i * 0.17f);
                    juce::Path arc;
                    arc.addCentredArc (x0 + w * 0.12f, midY, r, r,
                                        0.0f, juce::MathConstants<float>::pi * -0.45f, juce::MathConstants<float>::pi * 0.45f, true);
                    g.setColour (colour.withAlpha (1.0f - (float) i * 0.28f));
                    g.strokePath (arc, stroke);
                }
                break;
            }
            case Module::moduleLimiter:
            {
                const float ceilingY = y0 + h * 0.24f;
                p.startNewSubPath (x0, y1 - h * 0.12f);
                p.cubicTo (x0 + w * 0.18f, y0 + h * 0.1f, x0 + w * 0.3f, ceilingY, x0 + w * 0.42f, ceilingY);
                p.lineTo (x0 + w * 0.58f, ceilingY);
                p.cubicTo (x0 + w * 0.7f, ceilingY, x0 + w * 0.8f, y1 - h * 0.12f, x1, y1 - h * 0.3f);
                g.strokePath (p, stroke);
                g.setColour (colour.withAlpha (0.5f));
                g.drawHorizontalLine ((int) ceilingY, x0, x1);
                break;
            }
            case Module::moduleGate:
            {
                g.fillRoundedRectangle (x0 + w * 0.16f, y0 + h * 0.15f, w * 0.09f, h * 0.7f, 1.0f);
                g.fillRoundedRectangle (x1 - w * 0.25f, y0 + h * 0.15f, w * 0.09f, h * 0.7f, 1.0f);
                juce::Path pulse;
                pulse.startNewSubPath (x0 + w * 0.32f, y1 - h * 0.2f);
                pulse.lineTo (x0 + w * 0.32f, midY - h * 0.12f);
                pulse.lineTo (x0 + w * 0.5f, midY - h * 0.12f);
                pulse.lineTo (x0 + w * 0.5f, y1 - h * 0.2f);
                pulse.lineTo (x0 + w * 0.68f, y1 - h * 0.2f);
                pulse.lineTo (x0 + w * 0.68f, midY - h * 0.12f);
                pulse.lineTo (x1 - w * 0.25f, midY - h * 0.12f);
                g.strokePath (pulse, stroke);
                break;
            }
            case Module::moduleChorus:
            {
                for (int i = 0; i < 2; ++i)
                {
                    const float off = (float) i * h * 0.16f;
                    juce::Path wave;
                    wave.startNewSubPath (x0, midY + off);
                    wave.cubicTo (x0 + w * 0.25f, midY - h * 0.3f + off, x0 + w * 0.25f, midY - h * 0.3f + off, x0 + w * 0.5f, midY + off);
                    wave.cubicTo (x0 + w * 0.75f, midY + h * 0.3f + off, x0 + w * 0.75f, midY + h * 0.3f + off, x1, midY + off);
                    g.setColour (colour.withAlpha (i == 0 ? 1.0f : 0.5f));
                    g.strokePath (wave, stroke);
                }
                break;
            }
            case Module::moduleVoxChoir:
            {
                const float r = w * 0.24f;
                g.setColour (colour.withAlpha (0.5f));
                g.fillEllipse (x0 + w * 0.06f, midY - r * 0.35f, r, r);
                g.fillEllipse (x1 - w * 0.06f - r, midY - r * 0.35f, r, r);
                g.setColour (colour);
                g.fillEllipse (b.getCentreX() - r * 0.5f, y0 + h * 0.16f, r, r);
                break;
            }
            case Module::moduleStereoShaper:
            {
                const float r = w * 0.3f;
                g.setColour (colour.withAlpha (0.6f));
                g.drawEllipse (b.getCentreX() - r - w * 0.1f, midY - r * 0.5f, r, r, 1.6f);
                g.setColour (colour);
                g.drawEllipse (b.getCentreX() - r * 0.5f + w * 0.1f, midY - r * 0.5f, r, r, 1.6f);
                break;
            }
            case Module::moduleMasteringMeter:
            {
                juce::Path arc;
                const float cy = y1 - h * 0.18f;
                arc.addCentredArc (b.getCentreX(), cy, w * 0.36f, w * 0.36f, 0.0f,
                                    juce::MathConstants<float>::pi * -0.85f, juce::MathConstants<float>::pi * -0.15f, true);
                g.strokePath (arc, stroke);
                juce::Path needle;
                needle.startNewSubPath (b.getCentreX(), cy);
                needle.lineTo (b.getCentreX() + w * 0.2f, cy - h * 0.4f);
                g.strokePath (needle, stroke);
                break;
            }
            case Module::moduleChannelStrip:
            {
                // Three small fader strips at different heights -- the one
                // glyph that doesn't reduce to "an EQ curve" or "a dynamics
                // bar chart" alone, since this module is both at once.
                for (int i = 0; i < 3; ++i)
                {
                    const float fx = x0 + w * (0.22f + (float) i * 0.28f);
                    g.drawVerticalLine ((int) fx, y0 + h * 0.1f, y1 - h * 0.1f);
                    const float capY = y1 - h * (0.25f + (float) i * 0.22f);
                    g.fillRoundedRectangle (fx - w * 0.09f, capY - h * 0.06f, w * 0.18f, h * 0.12f, 1.0f);
                }
                break;
            }
            case Module::moduleDeEsser:
            default:
            {
                p.startNewSubPath (x0, y1 - h * 0.1f);
                p.lineTo (x0 + w * 0.4f, y0 + h * 0.15f);
                p.lineTo (x0 + w * 0.5f, y0 + h * 0.35f);
                p.lineTo (x0 + w * 0.6f, y0 + h * 0.15f);
                p.lineTo (x1, y1 - h * 0.1f);
                g.strokePath (p, stroke);
                g.setColour (colour.withAlpha (0.5f));
                g.drawHorizontalLine ((int) (y0 + h * 0.35f), x0 + w * 0.3f, x1 - w * 0.3f);
                break;
            }
        }
    }
}

//==============================================================================
// ChainRowComponent
//==============================================================================
ChainRowComponent::ChainRowComponent (ChainListComponent& ownerIn, MentalsSuiteAudioProcessor& processorIn, int slotIdIn, int moduleTypeIn)
    : slotId (slotIdIn), moduleType (moduleTypeIn), owner (ownerIn), processor (processorIn),
      displayName (MentalsSuiteAudioProcessor::getModuleTypeName (moduleTypeIn))
{
    powerToggle.setClickingTogglesState (true);
    powerToggle.setToggleState (! processor.isSlotBypassed (slotId), juce::dontSendNotification);
    powerToggle.setColour (juce::TextButton::buttonColourId, MentalsUI::Colours::slateGrayDark);
    powerToggle.setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::electricBlue);
    powerToggle.onClick = [this]
    {
        processor.setSlotBypassed (slotId, ! powerToggle.getToggleState());
    };
    addAndMakeVisible (powerToggle);

    removeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    removeButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::slateGray);
    removeButton.onClick = [this] { owner.rowRemoveRequested (slotId); };
    addAndMakeVisible (removeButton);
}

void ChainRowComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (3.0f);

    g.setColour (isSelected ? MentalsUI::Colours::slateGray : MentalsUI::Colours::slateGrayDark);
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (isSelected ? MentalsUI::Colours::electricBlue : MentalsUI::Colours::slateGray.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds, 8.0f, isSelected ? 2.0f : 1.0f);

    auto iconArea = bounds.withY (bounds.getY() + 10.0f).withHeight (28.0f).reduced (bounds.getWidth() * 0.22f, 0.0f);
    drawModuleGlyph (g, iconArea, moduleType,
                      isSelected ? MentalsUI::Colours::electricBlue : MentalsUI::Colours::slateGray.brighter (0.5f));

    g.setColour (MentalsUI::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    auto textArea = bounds.reduced (5.0f, 0.0f).withY (bounds.getY() + 44.0f).withHeight (32.0f);
    g.drawFittedText (displayName, textArea.toNearestInt(), juce::Justification::centred, 2);

    // Drag-handle hint -- three small dots at the bottom, echoing the
    // vertical-list version's bar hint but oriented for left/right dragging.
    g.setColour (MentalsUI::Colours::slateGray);
    for (int i = 0; i < 3; ++i)
        g.fillEllipse (bounds.getCentreX() - 9.0f + (float) i * 8.0f, bounds.getBottom() - 11.0f, 3.0f, 3.0f);
}

void ChainRowComponent::resized()
{
    auto b = getLocalBounds().reduced (3);
    powerToggle.setBounds (b.getX() + 5, b.getY() + 5, 16, 16);
    removeButton.setBounds (b.getRight() - 19, b.getY() + 5, 14, 14);
}

void ChainRowComponent::mouseDown (const juce::MouseEvent& e)
{
    dragStartMouseX = e.getScreenPosition().getX();
    dragStartComponentX = getX();
    isDragging = false;
}

void ChainRowComponent::mouseDrag (const juce::MouseEvent& e)
{
    const auto dx = e.getScreenPosition().getX() - dragStartMouseX;

    if (! isDragging && std::abs (dx) < 4)
        return;

    isDragging = true;
    owner.rowDragged (slotId, dragStartComponentX + dx);
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
    addButton.setColour (juce::TextButton::buttonColourId, MentalsUI::Colours::slateGrayDark);
    addButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::electricBlue);
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
    // the existing card components), so the cost of recreating them is a
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

    // Grows to fit its own content -- hosted inside the outer editor's
    // horizontally-scrolling rackViewport, so there's no need to fit within
    // any particular width here.
    const int contentWidth = (int) visualOrder.size() * cardWidth + addCardWidth + 12;
    setSize (contentWidth, cardHeight); // triggers resized() -> layoutRows()
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

        rows[(size_t) slot]->setBounds (slot * cardWidth + 4, 0, cardWidth - 6, cardHeight);
    }

    addButton.setBounds ((int) visualOrder.size() * cardWidth + 4, 0, addCardWidth - 6, cardHeight);
}

void ChainListComponent::rowDragged (int slotId, int newScreenX)
{
    const int maxSlot = (int) visualOrder.size() - 1;
    const int currentSlot = (int) std::distance (visualOrder.begin(),
        std::find (visualOrder.begin(), visualOrder.end(), slotId));

    // Let the dragged card follow the mouse directly...
    rows[(size_t) currentSlot]->setTopLeftPosition (juce::jlimit (0, cardWidth * maxSlot, newScreenX), 0);

    // ...and figure out which slot its centre now falls in, moving both the
    // card and its slot ID into that slot together if it's moved far enough.
    const int targetSlot = juce::jlimit (0, maxSlot, (newScreenX + cardWidth / 2) / cardWidth);

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
    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    productNameLabel.setText ("SUITE", juce::dontSendNotification);
    productNameLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    productNameLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    addAndMakeVisible (productNameLabel);

    chainSummaryLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    chainSummaryLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::slateGray);
    addAndMakeVisible (chainSummaryLabel);

    masterAssistantButton.setColour (juce::TextButton::buttonColourId, MentalsUI::Colours::slateGrayDark);
    masterAssistantButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::electricBlue);
    masterAssistantButton.addListener (this);
    addAndMakeVisible (masterAssistantButton);
    masterAssistantPanel.onApplied = [this] { chainList.refreshFromProcessor(); };

    rackViewport.setViewedComponent (&chainList, false);
    rackViewport.setScrollBarsShown (false, true); // horizontal only -- the rack never needs to scroll vertically
    addAndMakeVisible (rackViewport);
    chainList.onModuleSelected = [this] (int slotId) { showModule (slotId); };
    chainList.onModuleRemoveRequested = [this] (int slotId) { removeModule (slotId); };

    addAndMakeVisible (splitter);

    moduleViewport.setScrollBarsShown (true, true);
    addAndMakeVisible (moduleViewport);

    emptyStateLabel.setJustificationType (juce::Justification::centred);
    emptyStateLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::slateGray);
    emptyStateLabel.setFont (juce::Font (juce::FontOptions (18.0f)));
    addAndMakeVisible (emptyStateLabel);

    setResizable (true, true);
    setResizeLimits (1000, 650, 3840, 2160); // comfortably covers a maximized window on any real display, up to 4K
    setSize (1100, 700);

    const auto initialSlots = processor.getChainSlots();
    showModule (initialSlots.empty() ? -1 : initialSlots.front().slotId);

    startTimerHz (10); // refreshes Master Assistant's capture-progress readout and the chain summary label
}

MentalsSuiteAudioProcessorEditor::~MentalsSuiteAudioProcessorEditor()
{
    stopTimer();
    masterAssistantButton.removeListener (this);
    rackViewport.setViewedComponent (nullptr, false);
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

    const int count = (int) processor.getChainSlots().size();
    chainSummaryLabel.setText (count == 1 ? "1 module" : juce::String (count) + " modules", juce::dontSendNotification);
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
    fitModuleEditorToViewport();
}

void MentalsSuiteAudioProcessorEditor::fitModuleEditorToViewport()
{
    if (currentlyShownSlotId < 0)
        return;

    const auto it = moduleEditors.find (currentlyShownSlotId);
    if (it == moduleEditors.end())
        return;

    auto* editor = it->second.get();

    // getMaximumVisibleWidth/Height already account for whichever scrollbars
    // are about to be needed, so this converges instead of oscillating
    // between "fits" and "needs a scrollbar" every call.
    int targetW = moduleViewport.getMaximumVisibleWidth();
    int targetH = moduleViewport.getMaximumVisibleHeight();

    if (auto* editorConstrainer = editor->getConstrainer())
    {
        targetW = juce::jlimit ((int) editorConstrainer->getMinimumWidth(), (int) editorConstrainer->getMaximumWidth(), targetW);
        targetH = juce::jlimit ((int) editorConstrainer->getMinimumHeight(), (int) editorConstrainer->getMaximumHeight(), targetH);
    }

    editor->setSize (targetW, targetH);
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

    auto topBar = getLocalBounds().removeFromTop (topBarHeight);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBar);

    auto rackArea = getLocalBounds().withTrimmedTop (topBarHeight).removeFromTop (rackHeight);
    g.setColour (MentalsUI::Colours::charcoalBlack.brighter (0.02f));
    g.fillRect (rackArea);
}

void MentalsSuiteAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    auto topBar = bounds.removeFromTop (topBarHeight).reduced (10, 6);
    logoImage.setBounds (topBar.removeFromLeft (90));
    topBar.removeFromLeft (10);
    masterAssistantButton.setBounds (topBar.removeFromRight (150));
    topBar.removeFromRight (10);

    auto titleArea = topBar;
    productNameLabel.setBounds (titleArea.removeFromTop (titleArea.getHeight() / 2 + 4));
    chainSummaryLabel.setBounds (titleArea);

    rackViewport.setBounds (bounds.removeFromTop (rackHeight));

    splitter.setBounds (bounds.removeFromTop (splitterHeight));

    moduleViewport.setBounds (bounds);
    emptyStateLabel.setBounds (bounds);

    fitModuleEditorToViewport();
}
