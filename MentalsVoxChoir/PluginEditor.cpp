#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// ChoirSpreadComponent
//==============================================================================
void ChoirSpreadComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (14.0f);

    // Centre reference line (no pan / no pitch offset).
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawVerticalLine ((int) bounds.getCentreX(), bounds.getY(), bounds.getBottom());
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());

    const int numVoices = MentalsVoxChoirAudioProcessor::voiceCountChoices[processor.voicesParam->getIndex()];
    const float pitchAmount   = processor.pitchParam->get() * 0.01f;
    const float spreadAmount  = processor.spreadParam->get() * 0.01f;
    const float vibratoAmount = processor.vibratoParam->get() * 0.01f;

    for (int v = 0; v < numVoices; ++v)
    {
        const auto character = ChoirVoiceDSP::computeVoiceCharacter (v);
        const float pan = ChoirVoiceDSP::computeVoicePan (v, numVoices) * spreadAmount;
        const float pitchOffsetCents = character.pitchOffsetSign * MentalsVoxChoirAudioProcessor::maxPitchCents * pitchAmount;

        const float x = bounds.getCentreX() + pan * bounds.getWidth() * 0.5f;
        const float y = bounds.getCentreY() - (pitchOffsetCents / MentalsVoxChoirAudioProcessor::maxPitchCents) * bounds.getHeight() * 0.45f;

        const float dotSize = 6.0f + character.vibratoDepthScale * vibratoAmount * 8.0f;

        g.setColour (MentalsUI::Colours::electricBlue.withAlpha (0.85f));
        g.fillEllipse (juce::Rectangle<float> (dotSize, dotSize).withCentre ({ x, y }));
    }

    g.setColour (MentalsUI::Colours::slateGray);
    g.drawText ("L", bounds.removeFromLeft (20), juce::Justification::centredLeft);
    g.drawText ("R", getLocalBounds().toFloat().reduced (14.0f).removeFromRight (20), juce::Justification::centredRight);
}

//==============================================================================
// MentalsVoxChoirAudioProcessorEditor
//==============================================================================
MentalsVoxChoirAudioProcessorEditor::MentalsVoxChoirAudioProcessorEditor (MentalsVoxChoirAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), choirSpread (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&MentalsUI::MentalsLookAndFeel::getSharedInstance());

    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Vox Choir", juce::dontSendNotification);
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

    addAndMakeVisible (choirSpread);
    addAndMakeVisible (splitter);

    voicesLabel.setText ("Voices", juce::dontSendNotification);
    voicesLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    voicesLabel.setJustificationType (juce::Justification::centred);
    voicesLabel.attachToComponent (&voicesSelector, false);
    addAndMakeVisible (voicesLabel);

    voicesSelector.addItemList ({ "4", "8", "16", "32" }, 1);
    voicesSelector.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    voicesSelector.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    voicesSelector.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    voicesSelector.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (voicesSelector);

    vibratoSlider.addToParent ("Vibrato", *this);
    pitchSlider.addToParent   ("Pitch",   *this);
    timingSlider.addToParent  ("Timing",  *this);
    spreadSlider.addToParent  ("Spread",  *this);
    mixSlider.addToParent     ("Mix",     *this);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    voicesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "voices", voicesSelector);
    vibratoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "vibrato", vibratoSlider.slider);
    pitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "pitch", pitchSlider.slider);
    timingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "timing", timingSlider.slider);
    spreadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "spread", spreadSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);

    setResizable (true, true);
    setResizeLimits (620, 460, 1300, 900);
    setSize (860, 600);
}

MentalsVoxChoirAudioProcessorEditor::~MentalsVoxChoirAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsVoxChoirAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsVoxChoirAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsVoxChoirAudioProcessorEditor::refreshPresetList()
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

void MentalsVoxChoirAudioProcessorEditor::promptToSavePreset()
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

void MentalsVoxChoirAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);

    auto topBarArea = getLocalBounds().removeFromTop (40);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBarArea);
}

void MentalsVoxChoirAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 140;

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

    choirSpread.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);
    p.removeFromTop (20); // headroom for each knob's attachToComponent label above it

    juce::Array<juce::Component*> knobs { &voicesSelector, &vibratoSlider.slider, &pitchSlider.slider,
                                           &timingSlider.slider, &spreadSlider.slider, &mixSlider.slider, &outputMeter };
    const int cellWidth = p.getWidth() / knobs.size();
    for (auto* knob : knobs)
    {
        auto cell = p.removeFromLeft (cellWidth).reduced (8, 0);
        if (knob == &voicesSelector)
            cell = cell.withSizeKeepingCentre (cell.getWidth(), 24).withY (cell.getY() + cell.getHeight() / 2 - 12);
        knob->setBounds (cell);
    }
}
