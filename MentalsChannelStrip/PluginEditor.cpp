#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Renders one bulb with a glossy, 3D "sphere" shading -- a radial
    // gradient with the highlight offset up-left (a fixed, plausible light
    // source) fading through the zone colour to a darker rim, plus a small
    // specular highlight dot, rather than a flat coloured disc.
    void drawBulb3D (juce::Graphics& g, juce::Point<float> centre, float diameter, juce::Colour zoneColour, bool lit)
    {
        if (lit)
        {
            g.setColour (zoneColour.withAlpha (0.32f));
            g.fillEllipse (juce::Rectangle<float> (diameter * 1.7f, diameter * 1.7f).withCentre (centre));
        }

        const juce::Colour base      = lit ? zoneColour : zoneColour.withAlpha (0.16f);
        const juce::Colour highlight = lit ? base.brighter (1.3f) : base.brighter (0.4f);
        const juce::Colour shadow    = lit ? base.darker (0.75f)  : base.darker (0.3f);

        auto bulbRect = juce::Rectangle<float> (diameter, diameter).withCentre (centre);
        juce::ColourGradient sphere (highlight, centre.x - diameter * 0.3f, centre.y - diameter * 0.34f,
                                      shadow,    centre.x + diameter * 0.55f, centre.y + diameter * 0.55f, true);
        sphere.addColour (0.55, base);
        g.setGradientFill (sphere);
        g.fillEllipse (bulbRect);

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawEllipse (bulbRect, 1.0f);

        if (lit)
        {
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.fillEllipse (juce::Rectangle<float> (diameter * 0.24f, diameter * 0.24f)
                                .withCentre ({ centre.x - diameter * 0.22f, centre.y - diameter * 0.26f }));
        }
    }
}

//==============================================================================
// BulbGrMeterComponent
//==============================================================================
void BulbGrMeterComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (MentalsUI::Colours::charcoalBlack);
    g.fillRoundedRectangle (bounds, 5.0f);
    bounds.reduce (4.0f, 4.0f);

    constexpr float maxReductionDb = 24.0f;
    const float amount = juce::jlimit (0.0f, 1.0f, -displayedGainReductionDb / maxReductionDb);

    constexpr int numBulbs = 8; // fewer, bigger bulbs than before
    const float cellHeight = bounds.getHeight() / (float) numBulbs;
    const float bulbDiameter = juce::jmin (bounds.getWidth(), cellHeight * 0.86f);
    const int numLit = (int) std::round (amount * (float) numBulbs);

    for (int i = 0; i < numBulbs; ++i)
    {
        // Bulb 0 is at the TOP (least reduction); fills downward as more
        // reduction is applied, same convention as the shared segmented meter.
        const float cy = bounds.getY() + ((float) i + 0.5f) * cellHeight;
        const float cx = bounds.getCentreX();
        const bool lit = i < numLit;

        // Zone colour by position: shallow reduction reads green, moderate
        // amber, heavy (bottom of the column) red -- a standard GR-meter
        // convention.
        const float posFrac = (float) i / (float) (numBulbs - 1);
        juce::Colour zoneColour = MentalsUI::Colours::emeraldGreen;
        if (posFrac >= 0.66f)      zoneColour = MentalsUI::Colours::crimsonRed;
        else if (posFrac >= 0.33f) zoneColour = MentalsUI::Colours::amberOrange;

        drawBulb3D (g, { cx, cy }, bulbDiameter, zoneColour, lit);
    }

    g.setColour (MentalsUI::Colours::slateGray);
    g.drawRoundedRectangle (getLocalBounds().toFloat(), 5.0f, 1.0f);
}

//==============================================================================
// BulbLevelMeterComponent
//==============================================================================
void BulbLevelMeterComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    auto clipLedArea = bounds.removeFromTop (juce::jmin (18.0f, bounds.getHeight() * 0.1f)).reduced (2.0f);
    bounds.removeFromTop (3.0f);

    {
        const auto diameter = juce::jmin (clipLedArea.getWidth(), clipLedArea.getHeight());
        drawBulb3D (g, clipLedArea.getCentre(), diameter, MentalsUI::Colours::crimsonRed, clipping);
    }

    g.setColour (MentalsUI::Colours::charcoalBlack);
    g.fillRoundedRectangle (bounds, 5.0f);
    bounds.reduce (4.0f, 4.0f);

    constexpr float minDb = -48.0f, maxDb = 6.0f;
    constexpr float safeCeilingDb = -6.0f;
    constexpr float cautionCeilingDb = 0.0f;

    constexpr int numBulbs = 11; // fewer, bigger bulbs than before
    const float cellHeight = bounds.getHeight() / (float) numBulbs;
    const float bulbDiameter = juce::jmin (bounds.getWidth(), cellHeight * 0.86f);

    for (int i = 0; i < numBulbs; ++i)
    {
        // Bulb 0 is at the BOTTOM of the column, filling upward with level --
        // the classic bargraph convention.
        const float cy = bounds.getBottom() - ((float) i + 0.5f) * cellHeight;
        const float cx = bounds.getCentreX();

        const float segBottomDb = minDb + ((float) i / (float) numBulbs) * (maxDb - minDb);
        const bool lit = displayedPeakDb >= segBottomDb;

        juce::Colour zoneColour = MentalsUI::Colours::emeraldGreen;
        if (segBottomDb >= cautionCeilingDb)  zoneColour = MentalsUI::Colours::crimsonRed;
        else if (segBottomDb >= safeCeilingDb) zoneColour = MentalsUI::Colours::amberOrange;

        drawBulb3D (g, { cx, cy }, bulbDiameter, zoneColour, lit);
    }

    g.setColour (MentalsUI::Colours::slateGray);
    g.drawRoundedRectangle (getLocalBounds().toFloat().withTrimmedTop (clipLedArea.getHeight() + 5.0f), 5.0f, 1.0f);
}

namespace
{
    // Colours a knob's fill/thumb to match this strip's section colour
    // coding: green for Filters, blue for every EQ band, orange for
    // Dynamics, grey for Input/Output -- one colour per SECTION (not one
    // per band) is the convention here.
    void colourKnob (MentalsUI::LabelledSlider& knob, juce::Colour colour)
    {
        knob.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        knob.slider.setColour (juce::Slider::thumbColourId, colour);
    }
}

//==============================================================================
// MentalsChannelStripAudioProcessorEditor
//==============================================================================
MentalsChannelStripAudioProcessorEditor::MentalsChannelStripAudioProcessorEditor (MentalsChannelStripAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      compGrMeter ([&p] { return p.getCompGainReductionDb(); }),
      gateGrMeter ([&p] { return p.getGateGainReductionDb(); }),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&hardwareLookAndFeel);

    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Channel Strip", juce::dontSendNotification);
    productNameLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    productNameLabel.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    addAndMakeVisible (productNameLabel);

    presetsButton.setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::slateGrayDark);
    presetsButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
    presetsButton.onClick = [this] { showPresetsMenu(); };
    addAndMakeVisible (presetsButton);

    presetSaveButton.setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::slateGrayDark);
    presetSaveButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
    presetSaveButton.addListener (this);
    addAndMakeVisible (presetSaveButton);

    stereoToggle.setColour (juce::ToggleButton::textColourId, MentalsUI::Colours::white);
    stereoToggle.setColour (juce::ToggleButton::tickColourId, MentalsUI::Colours::white);
    addAndMakeVisible (stereoToggle);

    addAndMakeVisible (splitter);

    // ---- Section labels ---------------------------------------------------------
    for (auto* l : { &filtersLabel, &eqLabel, &hfLabel, &hmfLabel, &lmfLabel, &lfLabel,
                      &dynamicsLabel, &compLabel, &gateLabel, &outputLabel })
    {
        l->setColour (juce::Label::textColourId, MentalsUI::Colours::goldenYellow);
        l->setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
    }
    dynamicsLabel.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));

    // ---- Filters (green) ------------------------------------------------------------
    hpfFreqSlider.addToParent ("HPF", *this);
    lpfFreqSlider.addToParent ("LPF", *this);
    colourKnob (hpfFreqSlider, MentalsUI::Colours::emeraldGreen);
    colourKnob (lpfFreqSlider, MentalsUI::Colours::emeraldGreen);
    for (auto* b : { &filterSplitToggle, &filtersInToggle })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
        b->setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::electricBlue);
        addAndMakeVisible (*b);
    }

    // ---- EQ -- every band is the SAME blue (see colourKnob() above) ----------------
    hfFreqSlider.addToParent ("Freq", *this);
    hfGainSlider.addToParent ("Gain", *this);
    colourKnob (hfFreqSlider, MentalsUI::Colours::electricBlue);
    colourKnob (hfGainSlider, MentalsUI::Colours::electricBlue);

    hmfFreqSlider.addToParent ("Freq", *this);
    hmfGainSlider.addToParent ("Gain", *this);
    hmfQSlider.addToParent    ("Q",    *this);
    colourKnob (hmfFreqSlider, MentalsUI::Colours::electricBlue);
    colourKnob (hmfGainSlider, MentalsUI::Colours::electricBlue);
    colourKnob (hmfQSlider, MentalsUI::Colours::electricBlue);

    lmfFreqSlider.addToParent ("Freq", *this);
    lmfGainSlider.addToParent ("Gain", *this);
    lmfQSlider.addToParent    ("Q",    *this);
    colourKnob (lmfFreqSlider, MentalsUI::Colours::electricBlue);
    colourKnob (lmfGainSlider, MentalsUI::Colours::electricBlue);
    colourKnob (lmfQSlider, MentalsUI::Colours::electricBlue);

    lfFreqSlider.addToParent ("Freq", *this);
    lfGainSlider.addToParent ("Gain", *this);
    colourKnob (lfFreqSlider, MentalsUI::Colours::electricBlue);
    colourKnob (lfGainSlider, MentalsUI::Colours::electricBlue);

    for (auto* b : { &lfBellToggle, &hfBellToggle, &eqInToggle })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
        b->setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::electricBlue);
        addAndMakeVisible (*b);
    }

    // ---- Dynamics (orange) ------------------------------------------------------------
    compThresholdSlider.addToParent ("Thresh", *this);
    compRatioSlider.addToParent     ("Ratio",  *this);
    compAttackSlider.addToParent    ("Attack", *this);
    compReleaseSlider.addToParent   ("Release",*this);
    compMakeupSlider.addToParent    ("Makeup", *this);
    addAndMakeVisible (compGrMeter);
    for (auto* k : { &compThresholdSlider, &compRatioSlider, &compAttackSlider, &compReleaseSlider, &compMakeupSlider })
        colourKnob (*k, MentalsUI::Colours::amberOrange);

    gateThresholdSlider.addToParent ("Thresh", *this);
    gateRatioSlider.addToParent     ("Ratio",  *this);
    gateAttackSlider.addToParent    ("Attack", *this);
    gateReleaseSlider.addToParent   ("Release",*this);
    gateRangeSlider.addToParent     ("Range",  *this);
    addAndMakeVisible (gateGrMeter);
    for (auto* k : { &gateThresholdSlider, &gateRatioSlider, &gateAttackSlider, &gateReleaseSlider, &gateRangeSlider })
        colourKnob (*k, MentalsUI::Colours::amberOrange);

    for (auto* b : { &dynamicsInToggle, &dynamicsBeforeEqToggle })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
        b->setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::electricBlue);
        addAndMakeVisible (*b);
    }

    // ---- Output (grey/black) -----------------------------------------------------------
    outputGainSlider.addToParent ("Gain", *this);
    addAndMakeVisible (outputMeter);
    colourKnob (outputGainSlider, MentalsUI::Colours::slateGray);

    // No value read-out below any knob -- only the range numbers the
    // hardware LookAndFeel prints AROUND the knob itself stay (see
    // HardwareLookAndFeel::drawRotarySlider()); the name label ABOVE each
    // knob (added via addToParent()'s attachToComponent call above) stays
    // too, since only "labels below" were the ask.
    for (auto* knob : { &hpfFreqSlider, &lpfFreqSlider,
                         &hfFreqSlider, &hfGainSlider,
                         &hmfFreqSlider, &hmfGainSlider, &hmfQSlider,
                         &lmfFreqSlider, &lmfGainSlider, &lmfQSlider,
                         &lfFreqSlider, &lfGainSlider,
                         &compThresholdSlider, &compRatioSlider, &compAttackSlider, &compReleaseSlider, &compMakeupSlider,
                         &gateThresholdSlider, &gateRatioSlider, &gateAttackSlider, &gateReleaseSlider, &gateRangeSlider,
                         &outputGainSlider })
    {
        knob->slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    }

    // ---- Attachments --------------------------------------------------------------
    auto& apvts = processor.apvts;
    hpfFreqSlider.rebind (apvts, "hpfFreq");
    lpfFreqSlider.rebind (apvts, "lpfFreq");
    compThresholdSlider.rebind (apvts, "compThreshold");
    compRatioSlider.rebind     (apvts, "compRatio");
    compAttackSlider.rebind    (apvts, "compAttack");
    compReleaseSlider.rebind   (apvts, "compRelease");
    compMakeupSlider.rebind    (apvts, "compMakeup");
    gateThresholdSlider.rebind (apvts, "gateThreshold");
    gateRatioSlider.rebind     (apvts, "gateRatio");
    gateAttackSlider.rebind    (apvts, "gateAttack");
    gateReleaseSlider.rebind   (apvts, "gateRelease");
    gateRangeSlider.rebind     (apvts, "gateRange");
    lfFreqSlider.rebind  (apvts, "lfFreq");
    lfGainSlider.rebind  (apvts, "lfGain");
    lmfFreqSlider.rebind (apvts, "lmfFreq");
    lmfGainSlider.rebind (apvts, "lmfGain");
    lmfQSlider.rebind    (apvts, "lmfQ");
    hmfFreqSlider.rebind (apvts, "hmfFreq");
    hmfGainSlider.rebind (apvts, "hmfGain");
    hmfQSlider.rebind    (apvts, "hmfQ");
    hfFreqSlider.rebind  (apvts, "hfFreq");
    hfGainSlider.rebind  (apvts, "hfGain");
    outputGainSlider.rebind (apvts, "outputGain");

    filterSplitAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "filterSplit", filterSplitToggle);
    filtersInAttachment        = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "filtersIn", filtersInToggle);
    dynamicsInAttachment       = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "dynamicsIn", dynamicsInToggle);
    dynamicsBeforeEqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "dynamicsBeforeEq", dynamicsBeforeEqToggle);
    lfBellAttachment           = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "lfBell", lfBellToggle);
    hfBellAttachment           = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "hfBell", hfBellToggle);
    eqInAttachment             = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "eqIn", eqInToggle);
    stereoAttachment           = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "stereo", stereoToggle);

    setResizable (true, true);
    // Minimum == default, deliberately: whatever size the standalone host
    // window/DAW actually opens this editor at, it can never be smaller
    // than exactly the size that shows every control with nothing clipped
    // (verified by screenshot) -- no smaller "natural"/remembered size can
    // ever slip through.
    setResizeLimits (1400, 700, 1650, 950);
    setSize (1400, 700);
}

MentalsChannelStripAudioProcessorEditor::~MentalsChannelStripAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSaveButton.removeListener (this);
}

void MentalsChannelStripAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsChannelStripAudioProcessorEditor::showPresetsMenu()
{
    juce::PopupMenu menu;

    menu.addItem ("Default", [this]
    {
        processor.resetToDefault();
        presetsButton.setButtonText ("Default");
    });

    const auto categories = processor.getFactoryPresetCategories();
    if (! categories.empty())
    {
        menu.addSeparator();
        for (const auto& category : categories)
        {
            juce::PopupMenu submenu;
            for (const auto& name : category.presetNames)
            {
                const auto relativePath = category.name + "/" + name;
                submenu.addItem (name, [this, relativePath, name]
                {
                    processor.loadPreset (relativePath);
                    presetsButton.setButtonText (name);
                });
            }
            menu.addSubMenu (category.name, submenu);
        }
    }

    const auto userPresetNames = processor.getAvailablePresetNames();
    if (! userPresetNames.isEmpty())
    {
        juce::PopupMenu userMenu;
        for (const auto& name : userPresetNames)
        {
            userMenu.addItem (name, [this, name]
            {
                processor.loadPreset (name);
                presetsButton.setButtonText (name);
            });
        }
        menu.addSeparator();
        menu.addSubMenu ("MY PRESETS", userMenu);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetsButton));
}

void MentalsChannelStripAudioProcessorEditor::promptToSavePreset()
{
    auto* window = new juce::AlertWindow ("Save Preset", "Enter a name for this preset:",
                                           juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", "", "Preset name");
    window->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        if (result == 1)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            if (name.isNotEmpty() && name != "Default")
            {
                processor.savePreset (name);
                presetsButton.setButtonText (name);
            }
        }
    }), true);
}

void MentalsChannelStripAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0c0c0d));

    auto topBarArea = getLocalBounds().removeFromTop (40);
    MentalsUI::HardwareLookAndFeel::drawMetalPanel (g, topBarArea.toFloat());

    if (! lastPanelBounds.isEmpty())
    {
        auto panelBoundsF = lastPanelBounds.toFloat();
        MentalsUI::HardwareLookAndFeel::drawMetalPanel (g, panelBoundsF);

        constexpr float inset = 10.0f;
        MentalsUI::HardwareLookAndFeel::drawScrew (g, { panelBoundsF.getX() + inset, panelBoundsF.getY() + inset });
        MentalsUI::HardwareLookAndFeel::drawScrew (g, { panelBoundsF.getRight() - inset, panelBoundsF.getY() + inset });
        MentalsUI::HardwareLookAndFeel::drawScrew (g, { panelBoundsF.getX() + inset, panelBoundsF.getBottom() - inset });
        MentalsUI::HardwareLookAndFeel::drawScrew (g, { panelBoundsF.getRight() - inset, panelBoundsF.getBottom() - inset });

        // Thin vertical divider between the EQ (left) and Dynamics (right)
        // columns, matching the SSL reference's own hard split down the middle.
        g.setColour (MentalsUI::Colours::slateGray);
        g.drawVerticalLine ((int) panelBoundsF.getCentreX(), panelBoundsF.getY() + 12.0f, panelBoundsF.getBottom() - 12.0f);
    }

    // Grouping box around the whole Dynamics block (Compressor + Gate
    // together) so it reads as one deliberate unit, matching the SSL
    // reference's own bordered Dynamics section.
    if (! lastDynamicsBounds.isEmpty())
    {
        g.setColour (MentalsUI::Colours::slateGray);
        g.drawRoundedRectangle (lastDynamicsBounds.toFloat(), 4.0f, 1.2f);
    }

    constexpr float earWidth = 22.0f;
    auto fullBounds = getLocalBounds().toFloat();
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromLeft (earWidth).reduced (2.0f));
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromRight (earWidth).reduced (2.0f));
}

//==============================================================================
// Two-column SSL-style layout: EQ down the left, Dynamics + Gain down the
// right (see class comment in the header) -- short enough top to bottom to
// fit an ordinary laptop screen without resizing, unlike a single tall
// vertical stack.
//==============================================================================
void MentalsChannelStripAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (12);
    area.removeFromRight (12);

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 6;

    auto topBarArea = area.removeFromTop (topBarHeight);
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        productNameLabel.setBounds (t.removeFromLeft (110));
        presetSaveButton.setBounds (t.removeFromRight (60));
        t.removeFromRight (8);
        presetsButton.setBounds (t.removeFromRight (140));
        t.removeFromRight (12);
        stereoToggle.setBounds (t.removeFromRight (80));
    }

    splitter.setBounds (area.removeFromTop (splitterHeight));

    lastPanelBounds = area;

    auto panel = area.reduced (8, 8);

    // EQ column is wider than Dynamics (it packs HF/HMF/LMF/LF two-across --
    // see below -- trading the screen's spare WIDTH for less HEIGHT, so the
    // whole strip fits on an ordinary screen at its default size with no
    // scrolling or resizing needed).
    constexpr int columnGap = 14;
    auto leftColumn  = panel.removeFromLeft ((panel.getWidth() * 50) / 100);
    panel.removeFromLeft (columnGap);
    auto rightColumn = panel;

    constexpr int labelHeight   = 14;
    constexpr int headroom      = 16; // space for each knob's own attachToComponent label above it
    constexpr int knobSize      = 120; // every knob on this strip is this size -- matches the Gain knob
    constexpr int toggleHeight  = 18;
    constexpr int sectionGap    = 8;

    // Every knob gets the SAME fixed square size (knobSize), centred in an
    // evenly-divided cell -- so a 2-knob row and a 5-knob row both use
    // identically-sized knobs, just with more or less breathing room
    // between them, rather than knobs shrinking to fit more of them in.
    auto layoutKnobRow = [knobSize] (juce::Rectangle<int> row, std::initializer_list<MentalsUI::LabelledSlider*> knobs)
    {
        const int cellWidth = row.getWidth() / (int) knobs.size();
        for (auto* k : knobs)
        {
            auto cell = row.removeFromLeft (cellWidth);
            k->slider.setBounds (cell.withSizeKeepingCentre (knobSize, row.getHeight()));
        }
    };

    //==========================================================================
    // LEFT COLUMN -- Filters (full width), then the 4 EQ bands two-across
    // (HF+HMF | LMF+LF) instead of all stacked in one lane -- same knobSize
    // knobs, but using the screen's spare width instead of piling up height.
    //==========================================================================
    {
        auto col = leftColumn;

        auto filtersHeaderRow = col.removeFromTop (labelHeight);
        filtersLabel.setBounds (filtersHeaderRow);
        col.removeFromTop (headroom);
        layoutKnobRow (col.removeFromTop (knobSize), { &hpfFreqSlider, &lpfFreqSlider });
        col.removeFromTop (4);
        {
            auto toggleRow = col.removeFromTop (toggleHeight);
            filtersInToggle.setBounds (toggleRow.removeFromLeft (90).reduced (2, 0));
            toggleRow.removeFromLeft (6);
            filterSplitToggle.setBounds (toggleRow.removeFromLeft (90).reduced (2, 0));
        }
        col.removeFromTop (sectionGap);

        {
            auto eqHeaderRow = col.removeFromTop (labelHeight);
            eqInToggle.setBounds (eqHeaderRow.removeFromRight (80).reduced (0, 1));
            eqLabel.setBounds (eqHeaderRow);
        }
        col.removeFromTop (4);

        constexpr int laneGap = 10;
        auto laneWidth = (col.getWidth() - laneGap) / 2;
        auto laneA = col.removeFromLeft (laneWidth);
        col.removeFromLeft (laneGap);
        auto laneB = col;

        // Lane A: HF, then HMF.
        hfLabel.setBounds (laneA.removeFromTop (labelHeight));
        laneA.removeFromTop (headroom);
        layoutKnobRow (laneA.removeFromTop (knobSize), { &hfFreqSlider, &hfGainSlider });
        laneA.removeFromTop (4);
        hfBellToggle.setBounds (laneA.removeFromTop (toggleHeight).removeFromLeft (80).reduced (2, 0));
        laneA.removeFromTop (sectionGap);

        hmfLabel.setBounds (laneA.removeFromTop (labelHeight));
        laneA.removeFromTop (headroom);
        layoutKnobRow (laneA.removeFromTop (knobSize), { &hmfFreqSlider, &hmfGainSlider, &hmfQSlider });

        // Lane B: LMF, then LF.
        lmfLabel.setBounds (laneB.removeFromTop (labelHeight));
        laneB.removeFromTop (headroom);
        layoutKnobRow (laneB.removeFromTop (knobSize), { &lmfFreqSlider, &lmfGainSlider, &lmfQSlider });
        laneB.removeFromTop (sectionGap);

        lfLabel.setBounds (laneB.removeFromTop (labelHeight));
        laneB.removeFromTop (headroom);
        layoutKnobRow (laneB.removeFromTop (knobSize), { &lfFreqSlider, &lfGainSlider });
        laneB.removeFromTop (4);
        lfBellToggle.setBounds (laneB.removeFromTop (toggleHeight).removeFromLeft (80).reduced (2, 0));
    }

    //==========================================================================
    // RIGHT COLUMN -- Dynamics (Compressor/Gate, each with its own bulb-style
    // GR meter beside the knobs, the whole block boxed together -- see
    // lastDynamicsBounds/paint()), then Gain/Output.
    //==========================================================================
    {
        auto col = rightColumn;

        auto dynamicsBlockStart = col;

        dynamicsLabel.setBounds (col.removeFromTop (labelHeight + 4));
        col.removeFromTop (10); // extra breathing room under the DYNAMICS header, not just the usual 4px

        constexpr int meterWidth = 55;

        // Each dynamics section is one row of all 5 knobs -- there's plenty
        // of spare width in this column (see leftColumn/rightColumn split
        // above) to give every knob its full knobSize without needing a
        // second row.
        compLabel.setBounds (col.removeFromTop (labelHeight));
        col.removeFromTop (headroom);
        {
            auto row = col.removeFromTop (knobSize);
            compGrMeter.setBounds (row.removeFromRight (meterWidth));
            row.removeFromRight (10);
            layoutKnobRow (row, { &compThresholdSlider, &compRatioSlider, &compAttackSlider, &compReleaseSlider, &compMakeupSlider });
        }
        col.removeFromTop (sectionGap + 4);

        gateLabel.setBounds (col.removeFromTop (labelHeight));
        col.removeFromTop (headroom);
        {
            auto row = col.removeFromTop (knobSize);
            gateGrMeter.setBounds (row.removeFromRight (meterWidth));
            row.removeFromRight (10);
            layoutKnobRow (row, { &gateThresholdSlider, &gateRatioSlider, &gateAttackSlider, &gateReleaseSlider, &gateRangeSlider });
        }
        col.removeFromTop (8);
        {
            auto toggleRow = col.removeFromTop (toggleHeight);
            dynamicsInToggle.setBounds (toggleRow.removeFromLeft (90).reduced (2, 0));
            toggleRow.removeFromLeft (6);
            dynamicsBeforeEqToggle.setBounds (toggleRow.removeFromLeft (90).reduced (2, 0));
        }
        col.removeFromTop (10);

        // Everything from the DYNAMICS header down to just under the Dyn In /
        // Dyn > EQ toggles gets boxed together as one visual unit.
        lastDynamicsBounds = dynamicsBlockStart.withBottom (col.getY() - 6);

        col.removeFromTop (sectionGap * 2);

        outputLabel.setBounds (col.removeFromTop (labelHeight));
        col.removeFromTop (headroom);
        {
            auto row = col.removeFromTop (knobSize);
            outputMeter.setBounds (row.removeFromRight (meterWidth));
            row.removeFromRight (18);
            layoutKnobRow (row, { &outputGainSlider });
        }
    }
}
