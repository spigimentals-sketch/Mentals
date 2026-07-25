#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// CompressorTransferCurveComponent
//==============================================================================
void CompressorTransferCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (10.0f);

    constexpr float minDb = -60.0f, maxDb = 0.0f;
    const float threshold = processor.thresholdParam->get();
    const float ratio     = juce::jmax (1.0f, processor.ratioParam->get());
    const float knee      = processor.kneeParam->get();

    auto xForDb = [&] (float db) { return bounds.getX() + (db - minDb) / (maxDb - minDb) * bounds.getWidth(); };
    auto yForDb = [&] (float db) { return bounds.getBottom() - (db - minDb) / (maxDb - minDb) * bounds.getHeight(); };

    // Reference diagonal (unity, no compression).
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.drawLine (xForDb (minDb), yForDb (minDb), xForDb (maxDb), yForDb (maxDb));

    // Threshold marker.
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawVerticalLine ((int) xForDb (threshold), bounds.getY(), bounds.getBottom());

    juce::Path curve;
    bool started = false;
    for (float inDb = minDb; inDb <= maxDb; inDb += 0.5f)
    {
        const float outDb = MentalsUI::DynamicsDSP::computeOutputDb (inDb, threshold, ratio, knee);
        const float x = xForDb (inDb);
        const float y = yForDb (juce::jlimit (minDb, maxDb, outDb));

        if (! started) { curve.startNewSubPath (x, y); started = true; }
        else            curve.lineTo (x, y);
    }

    g.setColour (MentalsUI::Colours::goldenYellow);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
}

//==============================================================================
// MentalsCompressorAudioProcessorEditor
//==============================================================================
MentalsCompressorAudioProcessorEditor::MentalsCompressorAudioProcessorEditor (MentalsCompressorAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), transferCurve (p),
      gainReductionMeter ([&p] { return p.getGainReductionDb(); }, true),
      outputMeter ([&p] { return p.getOutputPeakDb(); })
{
    setLookAndFeel (&hardwareLookAndFeel);

    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Compressor", juce::dontSendNotification);
    productNameLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    productNameLabel.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    addAndMakeVisible (productNameLabel);

    presetSelector.setTextWhenNothingSelected ("Presets");
    presetSelector.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    presetSelector.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    presetSelector.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    presetSelector.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (presetSelector);
    presetSelector.addListener (this);
    refreshPresetList();

    presetSaveButton.setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::slateGrayDark);
    presetSaveButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
    addAndMakeVisible (presetSaveButton);
    presetSaveButton.addListener (this);

    aiAssistButton.setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::electricBlue);
    aiAssistButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
    addAndMakeVisible (aiAssistButton);
    aiAssistButton.addListener (this);

    aiAssistLabel.setText ("AI Assist", juce::dontSendNotification);
    aiAssistLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    aiAssistPanelContent.addAndMakeVisible (aiAssistLabel);

    for (auto* b : { &aiAssistAnalyseButton, &aiAssistApplyButton })
    {
        b->setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::slateGrayDark);
        b->setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
        aiAssistPanelContent.addAndMakeVisible (*b);
        b->addListener (this);
    }

    aiAssistStatusLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::slateGray);
    aiAssistStatusLabel.setText ("Not analysed", juce::dontSendNotification);
    aiAssistPanelContent.addAndMakeVisible (aiAssistStatusLabel);

    aiAssistPanelContent.setSize (300, 100);
    layoutAiAssistPanelContent();

    stereoToggle.setColour (juce::ToggleButton::textColourId, MentalsUI::Colours::white);
    stereoToggle.setColour (juce::ToggleButton::tickColourId, MentalsUI::Colours::white);
    addAndMakeVisible (stereoToggle);

    addAndMakeVisible (transferCurve);
    addAndMakeVisible (splitter);

    thresholdSlider.addToParent ("Threshold", *this);
    ratioSlider.addToParent     ("Ratio",     *this);
    kneeSlider.addToParent      ("Knee",      *this);
    attackSlider.addToParent    ("Attack",    *this);
    releaseSlider.addToParent   ("Release",   *this);
    makeupGainSlider.addToParent ("Makeup",   *this);
    mixSlider.addToParent        ("Mix",      *this);

    sidechainToggle.setColour (juce::ToggleButton::textColourId, MentalsUI::Colours::white);
    sidechainToggle.setColour (juce::ToggleButton::tickColourId, MentalsUI::Colours::white);
    addAndMakeVisible (sidechainToggle);

    gainReductionMeterLabel.setText ("GR", juce::dontSendNotification);
    gainReductionMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    gainReductionMeterLabel.setJustificationType (juce::Justification::centred);
    gainReductionMeterLabel.attachToComponent (&gainReductionMeter, false);
    addAndMakeVisible (gainReductionMeterLabel);
    addAndMakeVisible (gainReductionMeter);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "threshold", thresholdSlider.slider);
    ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ratio", ratioSlider.slider);
    kneeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "knee", kneeSlider.slider);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "attack", attackSlider.slider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "release", releaseSlider.slider);
    makeupGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "makeupGain", makeupGainSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);
    sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "useSidechain", sidechainToggle);
    stereoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "stereo", stereoToggle);

    setResizable (true, true);
    setResizeLimits (620, 560, 1300, 900);
    setSize (860, 600);

    startTimer (300);
}

MentalsCompressorAudioProcessorEditor::~MentalsCompressorAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
    aiAssistButton.removeListener (this);
    aiAssistAnalyseButton.removeListener (this);
    aiAssistApplyButton.removeListener (this);
}

void MentalsCompressorAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
    {
        promptToSavePreset();
        return;
    }

    if (button == &aiAssistButton)
    {
        showAiAssistPanel();
        return;
    }

    if (button == &aiAssistAnalyseButton)
    {
        processor.beginAiAssistAnalysis();
        aiAssistStatusLabel.setText ("Listening to input...", juce::dontSendNotification);
        aiAssistWasCapturing = true;
        return;
    }

    if (button == &aiAssistApplyButton)
    {
        const bool applied = processor.applySuggestedCompressorSettings();
        if (applied)
        {
            aiAssistStatusLabel.setText (
                "Applied -- Thresh " + juce::String (processor.thresholdParam->get(), 1)
                    + "dB, Ratio " + juce::String (processor.ratioParam->get(), 1) + ":1",
                juce::dontSendNotification);
        }
        else
        {
            aiAssistStatusLabel.setText ("Nothing to apply -- analyze first", juce::dontSendNotification);
        }
        return;
    }
}

void MentalsCompressorAudioProcessorEditor::timerCallback()
{
    const bool capturing = processor.isAiAssistCapturing();

    if (capturing)
    {
        aiAssistStatusLabel.setText ("Listening to input...", juce::dontSendNotification);
        aiAssistWasCapturing = true;
    }
    else if (aiAssistWasCapturing)
    {
        aiAssistWasCapturing = false;
        aiAssistStatusLabel.setText ("Ready -- click Apply Suggestion", juce::dontSendNotification);
    }
}

void MentalsCompressorAudioProcessorEditor::layoutAiAssistPanelContent()
{
    auto g = aiAssistPanelContent.getLocalBounds().reduced (10);

    aiAssistLabel.setBounds (g.removeFromTop (18));
    g.removeFromTop (4);
    auto row = g.removeFromTop (26);
    aiAssistAnalyseButton.setBounds (row.removeFromLeft (90));
    row.removeFromLeft (6);
    aiAssistApplyButton.setBounds (row);
    g.removeFromTop (6);
    aiAssistStatusLabel.setBounds (g.removeFromTop (40));
}

void MentalsCompressorAudioProcessorEditor::showAiAssistPanel()
{
    layoutAiAssistPanelContent();
    MentalsUI::launchPopup (aiAssistPanelContent, aiAssistButton);
}

void MentalsCompressorAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsCompressorAudioProcessorEditor::refreshPresetList()
{
    const auto currentText = presetSelector.getText();

    presetSelector.clear (juce::dontSendNotification);
    presetSelector.addItem ("Default", 1);

    const auto presetNames = processor.presetManager.getAvailablePresetNames();
    if (! presetNames.isEmpty())
    {
        presetSelector.addSeparator();
        int itemId = 2;
        for (const auto& name : presetNames)
            presetSelector.addItem (name, itemId++);
    }

    presetSelector.setText (currentText, juce::dontSendNotification);
}

void MentalsCompressorAudioProcessorEditor::promptToSavePreset()
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
                processor.presetManager.savePreset (name);
                refreshPresetList();
                presetSelector.setText (name, juce::dontSendNotification);
            }
        }
    }), true /* deleteWhenDismissed */);
}

void MentalsCompressorAudioProcessorEditor::paint (juce::Graphics& g)
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
    }

    constexpr float earWidth = 22.0f;
    auto fullBounds = getLocalBounds().toFloat();
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromLeft (earWidth).reduced (2.0f));
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromRight (earWidth).reduced (2.0f));
}

void MentalsCompressorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (24);
    area.removeFromRight (24);

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 380; // two knob rows, unlike the other plugins' single row

    auto topBarArea = area.removeFromTop (topBarHeight);
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        productNameLabel.setBounds (t.removeFromLeft (110));
        t.removeFromLeft (12);
        presetSelector.setBounds (t.removeFromLeft (160));
        t.removeFromLeft (8);
        presetSaveButton.setBounds (t.removeFromLeft (60));
        t.removeFromLeft (12);
        aiAssistButton.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (12);
        stereoToggle.setBounds (t.removeFromLeft (80));
    }

    // Controls are bottom-anchored with a fixed height, and the transfer-
    // curve graph always fills exactly whatever space remains above them.
    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    lastPanelBounds = panelArea;

    transferCurve.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);

    auto row1 = p.removeFromTop ((p.getHeight() - 10) / 2);
    p.removeFromTop (10);
    auto row2 = p;

    row1.removeFromTop (20); // headroom for each knob's attachToComponent label above it
    juce::Array<juce::Component*> row1Knobs { &thresholdSlider.slider, &ratioSlider.slider, &kneeSlider.slider,
                                               &attackSlider.slider, &releaseSlider.slider };
    const int cellWidth1 = row1.getWidth() / row1Knobs.size();
    for (auto* knob : row1Knobs)
        knob->setBounds (row1.removeFromLeft (cellWidth1).reduced (4, 0));

    row2.removeFromTop (20);

    // Meters get their own fixed-width cells (matching Reverb/De-esser's
    // convention) rather than sharing an equal division with the knobs --
    // dividing evenly with a tall row made the meter cell nearly square,
    // when a real analog VU meter reads as a wide rectangle. Out sits
    // rightmost, GR to its left.
    constexpr int meterCellWidth = 200;
    auto outMeterCell = row2.removeFromRight (meterCellWidth);
    auto grMeterCell  = row2.removeFromRight (meterCellWidth);

    juce::Array<juce::Component*> row2Knobs { &makeupGainSlider.slider, &mixSlider.slider };
    const int cellWidth2 = row2.getWidth() / (row2Knobs.size() + 1); // +1 reserves a cell for the sidechain toggle
    for (auto* knob : row2Knobs)
        knob->setBounds (row2.removeFromLeft (cellWidth2).reduced (4, 0));

    sidechainToggle.setBounds (row2.reduced (8, 0).withHeight (26).withY (row2.getY() + row2.getHeight() / 2 - 13));

    // Meters are vertically centred within a fixed height band, rather than
    // stretched to the row's full height, so they render as a landscape
    // rectangle instead of filling a tall square cell.
    constexpr int meterHeight = 100;
    gainReductionMeter.setBounds (grMeterCell.withSizeKeepingCentre (grMeterCell.getWidth() - 12, meterHeight));
    outputMeter.setBounds (outMeterCell.withSizeKeepingCentre (outMeterCell.getWidth() - 12, meterHeight));
}
