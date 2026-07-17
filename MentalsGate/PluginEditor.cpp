#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// GateTransferCurveComponent
//==============================================================================
void GateTransferCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (10.0f);

    constexpr float minDb = -80.0f, maxDb = 0.0f;
    const float threshold = processor.thresholdParam->get();
    const float ratio     = juce::jmax (1.0f, processor.ratioParam->get());
    const float rangeDb   = juce::jmax (0.0f, -processor.rangeParam->get());
    constexpr float kneeDb = 6.0f; // matches the fixed constant processBlock() uses

    auto xForDb = [&] (float db) { return bounds.getX() + (db - minDb) / (maxDb - minDb) * bounds.getWidth(); };
    auto yForDb = [&] (float db) { return bounds.getBottom() - (db - minDb) / (maxDb - minDb) * bounds.getHeight(); };

    // Reference diagonal (unity, gate fully open).
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.drawLine (xForDb (minDb), yForDb (minDb), xForDb (maxDb), yForDb (maxDb));

    // Threshold marker.
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawVerticalLine ((int) xForDb (threshold), bounds.getY(), bounds.getBottom());

    juce::Path curve;
    bool started = false;
    for (float inDb = minDb; inDb <= maxDb; inDb += 0.5f)
    {
        const float outDb = juce::jmax (MentalsUI::DynamicsDSP::computeExpanderOutputDb (inDb, threshold, ratio, kneeDb),
                                         inDb - rangeDb);
        const float x = xForDb (inDb);
        const float y = yForDb (juce::jlimit (minDb, maxDb, outDb));

        if (! started) { curve.startNewSubPath (x, y); started = true; }
        else            curve.lineTo (x, y);
    }

    g.setColour (MentalsUI::Colours::goldenYellow);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
}

//==============================================================================
// MentalsGateAudioProcessorEditor
//==============================================================================
MentalsGateAudioProcessorEditor::MentalsGateAudioProcessorEditor (MentalsGateAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), transferCurve (p),
      gainReductionMeter ([&p] { return p.getGainReductionDb(); }),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&MentalsUI::MentalsLookAndFeel::getSharedInstance());

    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Gate", juce::dontSendNotification);
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

    addAndMakeVisible (transferCurve);
    addAndMakeVisible (splitter);

    thresholdSlider.addToParent ("Threshold", *this);
    ratioSlider.addToParent     ("Ratio",     *this);
    attackSlider.addToParent    ("Attack",    *this);
    releaseSlider.addToParent   ("Release",   *this);
    rangeSlider.addToParent     ("Range",     *this);
    mixSlider.addToParent       ("Mix",       *this);

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
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "attack", attackSlider.slider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "release", releaseSlider.slider);
    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "range", rangeSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);
    sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "useSidechain", sidechainToggle);

    setResizable (true, true);
    setResizeLimits (620, 560, 1300, 900);
    setSize (860, 600);
}

MentalsGateAudioProcessorEditor::~MentalsGateAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsGateAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsGateAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsGateAudioProcessorEditor::refreshPresetList()
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

void MentalsGateAudioProcessorEditor::promptToSavePreset()
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

void MentalsGateAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);

    auto topBarArea = getLocalBounds().removeFromTop (40);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBarArea);
}

void MentalsGateAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 380; // two knob rows, like Compressor

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
    }

    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    transferCurve.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);

    auto row1 = p.removeFromTop ((p.getHeight() - 10) / 2);
    p.removeFromTop (10);
    auto row2 = p;

    row1.removeFromTop (20); // headroom for each knob's attachToComponent label above it
    juce::Array<juce::Component*> row1Knobs { &thresholdSlider.slider, &ratioSlider.slider,
                                               &attackSlider.slider, &releaseSlider.slider };
    const int cellWidth1 = row1.getWidth() / row1Knobs.size();
    for (auto* knob : row1Knobs)
        knob->setBounds (row1.removeFromLeft (cellWidth1).reduced (4, 0));

    row2.removeFromTop (20);
    juce::Array<juce::Component*> row2Knobs { &rangeSlider.slider, &mixSlider.slider,
                                               &gainReductionMeter, &outputMeter };
    const int cellWidth2 = row2.getWidth() / (row2Knobs.size() + 1); // +1 reserves a cell for the sidechain toggle
    for (auto* knob : row2Knobs)
        knob->setBounds (row2.removeFromLeft (cellWidth2).reduced (4, 0));

    sidechainToggle.setBounds (row2.reduced (8, 0).withHeight (26).withY (row2.getY() + row2.getHeight() / 2 - 13));
}
