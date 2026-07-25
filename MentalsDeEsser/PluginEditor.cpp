#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// DeEsserTransferCurveComponent
//==============================================================================
void DeEsserTransferCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (10.0f);

    constexpr float minDb = -60.0f, maxDb = 0.0f;
    const float threshold    = processor.thresholdParam->get();
    const float ratio        = juce::jmax (1.0f, processor.ratioParam->get());
    const float maxReduction = processor.maxReductionParam->get();

    auto xForDb = [&] (float db) { return bounds.getX() + (db - minDb) / (maxDb - minDb) * bounds.getWidth(); };
    auto yForDb = [&] (float db) { return bounds.getBottom() - (db - minDb) / (maxDb - minDb) * bounds.getHeight(); };

    // Reference diagonal (unity, no de-essing).
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.drawLine (xForDb (minDb), yForDb (minDb), xForDb (maxDb), yForDb (maxDb));

    // Threshold marker.
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawVerticalLine ((int) xForDb (threshold), bounds.getY(), bounds.getBottom());

    juce::Path curve;
    bool started = false;
    for (float inDb = minDb; inDb <= maxDb; inDb += 0.5f)
    {
        const float outDbUnclamped = MentalsUI::DynamicsDSP::computeOutputDb (
            inDb, threshold, ratio, MentalsDeEsserAudioProcessor::kneeDb);

        // Mirrors processBlock()'s Max Reduction floor exactly.
        const float gainReductionDb = juce::jmax (outDbUnclamped - inDb, -maxReduction);
        const float outDb = inDb + gainReductionDb;

        const float x = xForDb (inDb);
        const float y = yForDb (juce::jlimit (minDb, maxDb, outDb));

        if (! started) { curve.startNewSubPath (x, y); started = true; }
        else            curve.lineTo (x, y);
    }

    g.setColour (MentalsUI::Colours::goldenYellow);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
}

//==============================================================================
// MentalsDeEsserAudioProcessorEditor
//==============================================================================
MentalsDeEsserAudioProcessorEditor::MentalsDeEsserAudioProcessorEditor (MentalsDeEsserAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), transferCurve (p),
      gainReductionMeter ([&p] { return p.getGainReductionDb(); }, true /* gain-reduction mode -- see MentalsUI::AnalogVUMeterComponent */),
      outputMeter ([&p] { return p.getOutputPeakDb(); })
{
    setLookAndFeel (&hardwareLookAndFeel);

    addAndMakeVisible (logoImage);

    productNameLabel.setText ("De-esser", juce::dontSendNotification);
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

    stereoToggle.setColour (juce::ToggleButton::textColourId, MentalsUI::Colours::white);
    stereoToggle.setColour (juce::ToggleButton::tickColourId, MentalsUI::Colours::white);
    addAndMakeVisible (stereoToggle);

    addAndMakeVisible (transferCurve);
    addAndMakeVisible (splitter);

    frequencySlider.addToParent ("Frequency", *this);
    thresholdSlider.addToParent ("Threshold", *this);
    ratioSlider.addToParent     ("Ratio",     *this);
    attackSlider.addToParent    ("Attack",    *this);
    releaseSlider.addToParent   ("Release",   *this);
    maxReductionSlider.addToParent ("Max Reduction", *this);
    mixSlider.addToParent          ("Mix",           *this);

    addAndMakeVisible (listenToggle);

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

    frequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "frequency", frequencySlider.slider);
    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "threshold", thresholdSlider.slider);
    ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ratio", ratioSlider.slider);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "attack", attackSlider.slider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "release", releaseSlider.slider);
    maxReductionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "maxReduction", maxReductionSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);
    listenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "listen", listenToggle);
    stereoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "stereo", stereoToggle);

    setResizable (true, true);
    setResizeLimits (760, 560, 1300, 900);
    setSize (980, 620);
}

MentalsDeEsserAudioProcessorEditor::~MentalsDeEsserAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsDeEsserAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsDeEsserAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsDeEsserAudioProcessorEditor::refreshPresetList()
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

void MentalsDeEsserAudioProcessorEditor::promptToSavePreset()
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

void MentalsDeEsserAudioProcessorEditor::paint (juce::Graphics& g)
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

    // Bolted rack ears running the full height of the unit, same as Reverb.
    constexpr float earWidth = 22.0f;
    auto fullBounds = getLocalBounds().toFloat();
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromLeft (earWidth).reduced (2.0f));
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromRight (earWidth).reduced (2.0f));
}

void MentalsDeEsserAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Keep all content clear of the rack ears paint() draws at the very
    // left/right edges (see there).
    area.removeFromLeft (24);
    area.removeFromRight (24);

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 380; // two knob rows, like Compressor's layout

    auto topBarArea = area.removeFromTop (topBarHeight);
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        productNameLabel.setBounds (t.removeFromLeft (100));
        t.removeFromLeft (12);
        presetSelector.setBounds (t.removeFromLeft (160));
        t.removeFromLeft (8);
        presetSaveButton.setBounds (t.removeFromLeft (60));
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
    juce::Array<juce::Component*> row1Knobs { &frequencySlider.slider, &thresholdSlider.slider, &ratioSlider.slider,
                                               &attackSlider.slider, &releaseSlider.slider };
    const int cellWidth1 = row1.getWidth() / row1Knobs.size();
    for (auto* knob : row1Knobs)
        knob->setBounds (row1.removeFromLeft (cellWidth1).reduced (4, 0));

    // Row 2: Max Reduction, Mix, and Listen share whatever width remains
    // after both meters (kept adjacent, same "one pair of meters" idea as
    // Reverb's Out/Duck GR) claim their own wider cells.
    constexpr int meterCellWidth = 200, listenCellWidth = 100;
    auto meterCell1 = row2.removeFromRight (meterCellWidth);
    auto meterCell2 = row2.removeFromRight (meterCellWidth);
    auto listenCell = row2.removeFromRight (listenCellWidth);

    row2.removeFromTop (20);
    juce::Array<juce::Component*> row2Knobs { &maxReductionSlider.slider, &mixSlider.slider };
    const int cellWidth2 = row2.getWidth() / row2Knobs.size();
    for (auto* knob : row2Knobs)
        knob->setBounds (row2.removeFromLeft (cellWidth2).reduced (4, 0));

    // meterCell1 is the rightmost cell (removeFromRight was called on it
    // first) -- Out goes there and GR just to its left, matching Reverb's
    // "GR ... Out" left-to-right meter order.
    meterCell1.removeFromTop (20);
    outputMeter.setBounds (meterCell1.reduced (6, 0));

    meterCell2.removeFromTop (20);
    gainReductionMeter.setBounds (meterCell2.reduced (6, 0));

    listenToggle.setBounds (listenCell.reduced (8, 0).withHeight (32).withY (listenCell.getY() + listenCell.getHeight() / 2 - 16));
}
