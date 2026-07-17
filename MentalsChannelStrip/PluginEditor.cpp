#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// ChannelStripCurveComponent
//==============================================================================
void ChannelStripCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (10.0f);
    constexpr float minDb = -18.0f, maxDb = 18.0f;
    constexpr float minFreq = 20.0f, maxFreq = 20000.0f;

    auto xForFreq = [&] (float f) { return bounds.getX() + std::log10 (f / minFreq) / std::log10 (maxFreq / minFreq) * bounds.getWidth(); };
    auto yForDb   = [&] (float db) { return bounds.getBottom() - (db - minDb) / (maxDb - minDb) * bounds.getHeight(); };

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine ((int) xForFreq (f), bounds.getY(), bounds.getBottom());
    g.drawHorizontalLine ((int) yForDb (0.0f), bounds.getX(), bounds.getRight());

    using Shape = MentalsChannelStripAudioProcessor::BandShape;
    const double sampleRate = 44100.0; // curve is sample-rate-independent enough for display purposes

    const bool lfBell = processor.lfBellParam->get();
    const bool hfBell = processor.hfBellParam->get();
    const float lfFreq = processor.lfFreqParam->get(), lfGain = processor.lfGainParam->get();
    const float lmfFreq = processor.lmfFreqParam->get(), lmfGain = processor.lmfGainParam->get(), lmfQ = processor.lmfQParam->get();
    const float hmfFreq = processor.hmfFreqParam->get(), hmfGain = processor.hmfGainParam->get(), hmfQ = processor.hmfQParam->get();
    const float hfFreq = processor.hfFreqParam->get(), hfGain = processor.hfGainParam->get();
    const bool eqIn = processor.eqInParam->get();

    juce::Path path;
    bool started = false;
    constexpr int numPoints = 200;
    for (int i = 0; i <= numPoints; ++i)
    {
        const float t = (float) i / (float) numPoints;
        const float freq = minFreq * std::pow (maxFreq / minFreq, t);

        float totalDb = 0.0f;
        if (eqIn)
        {
            const double lfMag  = MentalsChannelStripAudioProcessor::getMagnitudeForFrequency (
                lfBell ? Shape::Bell : Shape::LowShelf, freq, lfFreq, lfBell ? 1.0f : 0.7071f, lfGain, sampleRate);
            const double lmfMag = MentalsChannelStripAudioProcessor::getMagnitudeForFrequency (Shape::Bell, freq, lmfFreq, lmfQ, lmfGain, sampleRate);
            const double hmfMag = MentalsChannelStripAudioProcessor::getMagnitudeForFrequency (Shape::Bell, freq, hmfFreq, hmfQ, hmfGain, sampleRate);
            const double hfMag  = MentalsChannelStripAudioProcessor::getMagnitudeForFrequency (
                hfBell ? Shape::Bell : Shape::HighShelf, freq, hfFreq, hfBell ? 1.0f : 0.7071f, hfGain, sampleRate);

            totalDb = (float) juce::Decibels::gainToDecibels (lfMag * lmfMag * hmfMag * hfMag, -100.0);
        }

        const float x = xForFreq (freq);
        const float y = yForDb (juce::jlimit (minDb, maxDb, totalDb));

        if (! started) { path.startNewSubPath (x, y); started = true; }
        else            path.lineTo (x, y);
    }

    g.setColour (eqIn ? MentalsUI::Colours::electricBlue : MentalsUI::Colours::slateGray);
    g.strokePath (path, juce::PathStrokeType (2.0f));
}

//==============================================================================
// MentalsChannelStripAudioProcessorEditor
//==============================================================================
MentalsChannelStripAudioProcessorEditor::MentalsChannelStripAudioProcessorEditor (MentalsChannelStripAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), curve (p),
      compGrMeter ([&p] { return p.getCompGainReductionDb(); }),
      gateGrMeter ([&p] { return p.getGateGainReductionDb(); }),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&MentalsUI::MentalsLookAndFeel::getSharedInstance());

    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
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

    addAndMakeVisible (curve);
    addAndMakeVisible (splitter);

    // ---- Section labels ---------------------------------------------------------
    for (auto* l : { &filtersLabel, &compLabel, &gateLabel, &lfLabel, &lmfLabel, &hmfLabel, &hfLabel, &outputLabel })
    {
        l->setColour (juce::Label::textColourId, MentalsUI::Colours::goldenYellow);
        l->setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
    }

    // ---- Filters ------------------------------------------------------------------
    hpfFreqSlider.addToParent ("HPF", *this);
    lpfFreqSlider.addToParent ("LPF", *this);
    for (auto* b : { &filterSplitToggle, &filtersInToggle })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
        b->setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::electricBlue);
        addAndMakeVisible (*b);
    }

    // ---- Dynamics -------------------------------------------------------------------
    compThresholdSlider.addToParent ("Thresh", *this);
    compRatioSlider.addToParent     ("Ratio",  *this);
    compAttackSlider.addToParent    ("Attack", *this);
    compReleaseSlider.addToParent   ("Release",*this);
    compMakeupSlider.addToParent    ("Makeup", *this);
    addAndMakeVisible (compGrMeter);

    gateThresholdSlider.addToParent ("Thresh", *this);
    gateRatioSlider.addToParent     ("Ratio",  *this);
    gateAttackSlider.addToParent    ("Attack", *this);
    gateReleaseSlider.addToParent   ("Release",*this);
    gateRangeSlider.addToParent     ("Range",  *this);
    addAndMakeVisible (gateGrMeter);

    for (auto* b : { &dynamicsInToggle, &dynamicsBeforeEqToggle })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
        b->setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::electricBlue);
        addAndMakeVisible (*b);
    }

    // ---- EQ ---------------------------------------------------------------------------
    lfFreqSlider.addToParent ("Freq", *this);
    lfGainSlider.addToParent ("Gain", *this);
    lmfFreqSlider.addToParent ("Freq", *this);
    lmfGainSlider.addToParent ("Gain", *this);
    lmfQSlider.addToParent    ("Q",    *this);
    hmfFreqSlider.addToParent ("Freq", *this);
    hmfGainSlider.addToParent ("Gain", *this);
    hmfQSlider.addToParent    ("Q",    *this);
    hfFreqSlider.addToParent ("Freq", *this);
    hfGainSlider.addToParent ("Gain", *this);

    for (auto* b : { &lfBellToggle, &hfBellToggle, &eqInToggle })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
        b->setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::electricBlue);
        addAndMakeVisible (*b);
    }

    // ---- Output -----------------------------------------------------------------------
    outputGainSlider.addToParent ("Gain", *this);
    addAndMakeVisible (outputMeter);

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

    setResizable (true, true);
    setResizeLimits (1200, 780, 1900, 1200);
    setSize (1350, 860);
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
    g.fillAll (MentalsUI::Colours::charcoalBlack);

    auto topBar = getLocalBounds().removeFromTop (40);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBar);
}

void MentalsChannelStripAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 6;
    constexpr int curveHeight    = 150;

    auto topBarArea = area.removeFromTop (topBarHeight);
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        productNameLabel.setBounds (t.removeFromLeft (110));
        presetSaveButton.setBounds (t.removeFromRight (60));
        t.removeFromRight (8);
        presetsButton.setBounds (t.removeFromRight (140));
    }

    curve.setBounds (area.removeFromTop (curveHeight).reduced (8));
    splitter.setBounds (area.removeFromTop (splitterHeight));

    auto panel = area.reduced (10);
    const int rowHeight = panel.getHeight() / 2;

    // ---- Row 1: Filters | Compressor | Gate/Expander | Output --------------------
    auto row1 = panel.removeFromTop (rowHeight);
    {
        auto filtersArea = row1.removeFromLeft (200);
        filtersLabel.setBounds (filtersArea.removeFromTop (20));
        filtersArea.removeFromTop (20); // headroom for knob labels
        auto knobRow = filtersArea.removeFromTop (90);
        const int kw = knobRow.getWidth() / 2;
        hpfFreqSlider.slider.setBounds (knobRow.removeFromLeft (kw).reduced (4, 0));
        lpfFreqSlider.slider.setBounds (knobRow.reduced (4, 0));
        auto toggleRow = filtersArea.removeFromTop (28);
        filtersInToggle.setBounds (toggleRow.removeFromLeft (95).reduced (2));
        filterSplitToggle.setBounds (toggleRow.removeFromLeft (95).reduced (2));

        row1.removeFromLeft (10);

        auto compArea = row1.removeFromLeft (420);
        compLabel.setBounds (compArea.removeFromTop (20));
        compArea.removeFromTop (20);
        auto compKnobRow = compArea.removeFromTop (90);
        MentalsUI::LabelledSlider* compKnobs[] { &compThresholdSlider, &compRatioSlider, &compAttackSlider, &compReleaseSlider, &compMakeupSlider };
        const int compKw = compKnobRow.getWidth() * 3 / 4 / 5;
        for (auto* k : compKnobs)
            k->slider.setBounds (compKnobRow.removeFromLeft (compKw).reduced (4, 0));
        compGrMeter.setBounds (compKnobRow.reduced (4, 0));

        row1.removeFromLeft (10);

        auto gateArea = row1.removeFromLeft (420);
        gateLabel.setBounds (gateArea.removeFromTop (20));
        gateArea.removeFromTop (20);
        auto gateKnobRow = gateArea.removeFromTop (90);
        MentalsUI::LabelledSlider* gateKnobs[] { &gateThresholdSlider, &gateRatioSlider, &gateAttackSlider, &gateReleaseSlider, &gateRangeSlider };
        const int gateKw = gateKnobRow.getWidth() * 3 / 4 / 5;
        for (auto* k : gateKnobs)
            k->slider.setBounds (gateKnobRow.removeFromLeft (gateKw).reduced (4, 0));
        gateGrMeter.setBounds (gateKnobRow.reduced (4, 0));
        auto dynToggleRow = gateArea.removeFromTop (28);
        dynamicsInToggle.setBounds (dynToggleRow.removeFromLeft (95).reduced (2));
        dynamicsBeforeEqToggle.setBounds (dynToggleRow.removeFromLeft (95).reduced (2));

        row1.removeFromLeft (10);

        auto outputArea = row1;
        outputLabel.setBounds (outputArea.removeFromTop (20));
        outputArea.removeFromTop (20);
        auto outputKnobRow = outputArea.removeFromTop (90);
        outputGainSlider.slider.setBounds (outputKnobRow.removeFromLeft (outputKnobRow.getWidth() / 2).reduced (4, 0));
        outputMeter.setBounds (outputKnobRow.reduced (4, 0));
    }

    panel.removeFromTop (10);

    // ---- Row 2: LF | LMF | HMF | HF, with the single EQ In toggle reserved
    // in its own strip at the top-right before the four bands are laid out.
    auto row2 = panel;
    auto eqInRow = row2.removeFromTop (26);
    eqInToggle.setBounds (eqInRow.removeFromRight (90).reduced (2));

    const int eqBandWidth = row2.getWidth() / 4;

    auto layoutEqBand = [&] (juce::Rectangle<int> b, juce::Label& label,
                              MentalsUI::LabelledSlider& freqSlider, MentalsUI::LabelledSlider& gainSlider,
                              MentalsUI::LabelledSlider* qSlider, juce::TextButton* bellToggle)
    {
        label.setBounds (b.removeFromTop (20));
        b.removeFromTop (20);
        auto knobRow = b.removeFromTop (90);
        const int numKnobs = qSlider != nullptr ? 3 : 2;
        const int kw = knobRow.getWidth() / juce::jmax (1, numKnobs);
        freqSlider.slider.setBounds (knobRow.removeFromLeft (kw).reduced (4, 0));
        gainSlider.slider.setBounds (knobRow.removeFromLeft (kw).reduced (4, 0));
        if (qSlider != nullptr)
            qSlider->slider.setBounds (knobRow.removeFromLeft (kw).reduced (4, 0));
        if (bellToggle != nullptr)
            bellToggle->setBounds (b.removeFromTop (26).removeFromLeft (90).reduced (2));
    };

    layoutEqBand (row2.removeFromLeft (eqBandWidth).reduced (6, 0), lfLabel, lfFreqSlider, lfGainSlider, nullptr, &lfBellToggle);
    layoutEqBand (row2.removeFromLeft (eqBandWidth).reduced (6, 0), lmfLabel, lmfFreqSlider, lmfGainSlider, &lmfQSlider, nullptr);
    layoutEqBand (row2.removeFromLeft (eqBandWidth).reduced (6, 0), hmfLabel, hmfFreqSlider, hmfGainSlider, &hmfQSlider, nullptr);
    layoutEqBand (row2.reduced (6, 0), hfLabel, hfFreqSlider, hfGainSlider, nullptr, &hfBellToggle);
}
