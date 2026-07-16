#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include "SaturatorDSP.h"

//==============================================================================
// SaturatorTransferCurveComponent
//==============================================================================
void SaturatorTransferCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (10.0f);

    const float driveGain  = juce::Decibels::decibelsToGain (processor.driveParam->get());
    const float outputGain = juce::Decibels::decibelsToGain (processor.outputGainParam->get());
    const float mix        = juce::jlimit (0.0f, 1.0f, processor.mixParam->get() * 0.01f);
    const int   type       = processor.typeParam->getIndex();

    // Axis lines.
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());
    g.drawVerticalLine ((int) bounds.getCentreX(), bounds.getY(), bounds.getBottom());

    // Reference diagonal: what "no saturation" (unity, no mix) looks like.
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.drawLine (bounds.getX(), bounds.getBottom(), bounds.getRight(), bounds.getY());

    juce::Path curve;
    constexpr int numPoints = 200;
    bool started = false;

    for (int i = 0; i <= numPoints; ++i)
    {
        const float xNorm  = -1.0f + 2.0f * (float) i / (float) numPoints; // -1..1
        const float driven = xNorm * driveGain;
        const float shaped = SaturatorDSP::waveshape (driven, type);
        const float wet    = shaped * outputGain;
        const float outNorm = juce::jlimit (-1.2f, 1.2f, xNorm * (1.0f - mix) + wet * mix);

        const float px = bounds.getX() + (xNorm * 0.5f + 0.5f) * bounds.getWidth();
        const float py = bounds.getCentreY() - outNorm * (bounds.getHeight() * 0.42f);

        if (! started) { curve.startNewSubPath (px, py); started = true; }
        else            curve.lineTo (px, py);
    }

    g.setColour (MentalsUI::Colours::goldenYellow);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
}

//==============================================================================
// MentalsSaturatorAudioProcessorEditor
//==============================================================================
MentalsSaturatorAudioProcessorEditor::MentalsSaturatorAudioProcessorEditor (MentalsSaturatorAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), transferCurve (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&MentalsUI::MentalsLookAndFeel::getSharedInstance());

    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Saturator", juce::dontSendNotification);
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

    typeLabel.setText ("Type", juce::dontSendNotification);
    typeLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    addAndMakeVisible (typeLabel);

    typeSelector.addItem ("Soft Clip", 1);
    typeSelector.addItem ("Hard Clip", 2);
    typeSelector.addItem ("Tube",      3);
    typeSelector.addItem ("Foldback",  4);
    typeSelector.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    typeSelector.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    typeSelector.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    typeSelector.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (typeSelector);

    driveSlider.addToParent      ("Drive",  *this);
    toneSlider.addToParent       ("Tone",   *this);
    outputGainSlider.addToParent ("Output", *this);
    mixSlider.addToParent        ("Mix",    *this);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    typeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "type", typeSelector);
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "drive", driveSlider.slider);
    toneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "tone", toneSlider.slider);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "outputGain", outputGainSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);

    setResizable (true, true);
    setResizeLimits (560, 420, 1200, 900);
    setSize (760, 560);
}

MentalsSaturatorAudioProcessorEditor::~MentalsSaturatorAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsSaturatorAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsSaturatorAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsSaturatorAudioProcessorEditor::refreshPresetList()
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

void MentalsSaturatorAudioProcessorEditor::promptToSavePreset()
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

void MentalsSaturatorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);

    auto topBarArea = getLocalBounds().removeFromTop (40);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBarArea);
}

void MentalsSaturatorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 170; // taller than Delay/Reverb's 140 to fit the extra Type row above the knobs

    auto topBarArea = area.removeFromTop (topBarHeight);
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        productNameLabel.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (12);
        presetSelector.setBounds (t.removeFromLeft (160));
        t.removeFromLeft (8);
        presetSaveButton.setBounds (t.removeFromLeft (60));
    }

    // Controls are bottom-anchored with a fixed height, and the transfer-
    // curve graph always fills exactly whatever space remains above them.
    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    transferCurve.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);

    auto typeRow = p.removeFromTop (24);
    typeLabel.setBounds (typeRow.removeFromLeft (50));
    typeSelector.setBounds (typeRow.removeFromLeft (150));
    p.removeFromTop (6);

    auto knobArea = p;
    knobArea.removeFromTop (20); // headroom for each knob's attachToComponent label above it

    juce::Array<juce::Component*> knobs { &driveSlider.slider, &toneSlider.slider,
                                           &outputGainSlider.slider, &mixSlider.slider, &outputMeter };
    const int cellWidth = knobArea.getWidth() / knobs.size();
    for (auto* knob : knobs)
        knob->setBounds (knobArea.removeFromLeft (cellWidth).reduced (8, 0));
}
