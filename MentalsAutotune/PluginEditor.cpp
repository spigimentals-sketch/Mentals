#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include "PitchDSP.h"
#include <algorithm>

//==============================================================================
// PitchHistoryComponent
//==============================================================================
void PitchHistoryComponent::timerCallback()
{
    // Shift the history left by one and push the newest sample at the end.
    std::rotate (detectedHistory.begin(), detectedHistory.begin() + 1, detectedHistory.end());
    std::rotate (targetHistory.begin(), targetHistory.begin() + 1, targetHistory.end());
    std::rotate (voicedHistory.begin(), voicedHistory.begin() + 1, voicedHistory.end());

    const bool voiced = processor.isVoiced();
    voicedHistory.back() = voiced;

    if (voiced)
    {
        detectedHistory.back() = 12.0f * std::log2 (processor.getDetectedFrequencyHz() / 440.0f);
        targetHistory.back()   = 12.0f * std::log2 (processor.getTargetFrequencyHz() / 440.0f);
    }
    else
    {
        // Hold the last known value visually rather than snapping to 0,
        // which would look like a jump down to a false pitch.
        const size_t prev = (size_t) historyLength - 2;
        detectedHistory.back() = detectedHistory[prev];
        targetHistory.back()   = targetHistory[prev];
    }

    repaint();
}

void PitchHistoryComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (8.0f, 4.0f);

    constexpr float minSemitone = -24.0f, maxSemitone = 24.0f; // 4 octaves centred on A4
    auto yForSemitone = [&] (float semitone)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (semitone - minSemitone) / (maxSemitone - minSemitone));
        return bounds.getBottom() - t * bounds.getHeight();
    };

    // Guide lines at in-scale semitones for the current Key/Scale.
    const int keyIndex = processor.keyParam->getIndex();
    const int rootOffsetFromA = keyIndex - 9; // A is index 9
    const auto mask = PitchDSP::getScaleMask (processor.scaleParam->getIndex());

    for (int semitone = (int) minSemitone; semitone <= (int) maxSemitone; ++semitone)
    {
        const int fromRoot = ((semitone - rootOffsetFromA) % 12 + 12) % 12;
        if (! mask[(size_t) fromRoot])
            continue;

        const float y = yForSemitone ((float) semitone);
        g.setColour (juce::Colours::white.withAlpha (semitone == 0 ? 0.25f : 0.08f)); // A4 line brighter
        g.drawHorizontalLine ((int) y, bounds.getX(), bounds.getRight());
    }

    // History lines -- broken during unvoiced gaps rather than drawn
    // through them.
    const float stepX = bounds.getWidth() / (float) (historyLength - 1);

    auto drawHistory = [&] (const std::vector<float>& history, juce::Colour colour)
    {
        juce::Path path;
        bool started = false;
        for (int i = 0; i < historyLength; ++i)
        {
            if (! voicedHistory[(size_t) i])
            {
                started = false;
                continue;
            }

            const float x = bounds.getX() + (float) i * stepX;
            const float y = yForSemitone (history[(size_t) i]);

            if (! started) { path.startNewSubPath (x, y); started = true; }
            else            path.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    };

    drawHistory (targetHistory, MentalsUI::Colours::electricBlue);
    drawHistory (detectedHistory, MentalsUI::Colours::goldenYellow);

    if (! processor.isVoiced())
    {
        g.setColour (MentalsUI::Colours::slateGray);
        g.setFont (12.0f);
        g.drawText ("Listening...", getLocalBounds(), juce::Justification::centred);
    }
}

//==============================================================================
// MentalsAutotuneAudioProcessorEditor
//==============================================================================
MentalsAutotuneAudioProcessorEditor::MentalsAutotuneAudioProcessorEditor (MentalsAutotuneAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), pitchHistory (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Autotune", juce::dontSendNotification);
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

    addAndMakeVisible (pitchHistory);
    addAndMakeVisible (splitter);

    keyLabel.setText ("Key", juce::dontSendNotification);
    keyLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    addAndMakeVisible (keyLabel);

    keySelector.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    keySelector.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    keySelector.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    keySelector.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    keySelector.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (keySelector);

    scaleLabel.setText ("Scale", juce::dontSendNotification);
    scaleLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    addAndMakeVisible (scaleLabel);

    scaleSelector.addItemList ({ "Chromatic", "Major", "Minor" }, 1);
    scaleSelector.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    scaleSelector.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    scaleSelector.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    scaleSelector.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (scaleSelector);

    retuneSpeedSlider.addToParent ("Retune Speed", *this);
    amountSlider.addToParent      ("Amount",       *this);
    mixSlider.addToParent         ("Mix",          *this);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "key", keySelector);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "scale", scaleSelector);
    retuneSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "retuneSpeed", retuneSpeedSlider.slider);
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "amount", amountSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);

    setResizable (true, true);
    setResizeLimits (620, 460, 1300, 900);
    setSize (820, 580);
}

MentalsAutotuneAudioProcessorEditor::~MentalsAutotuneAudioProcessorEditor()
{
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsAutotuneAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsAutotuneAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsAutotuneAudioProcessorEditor::refreshPresetList()
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

void MentalsAutotuneAudioProcessorEditor::promptToSavePreset()
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

void MentalsAutotuneAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);

    auto topBarArea = getLocalBounds().removeFromTop (40);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBarArea);
}

void MentalsAutotuneAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 170; // Key/Scale row above the knobs, like Saturator's Type row

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

    // Controls are bottom-anchored with a fixed height, and the pitch-
    // history graph always fills exactly whatever space remains above them.
    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    pitchHistory.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);

    auto keyRow = p.removeFromTop (24);
    keyLabel.setBounds (keyRow.removeFromLeft (36));
    keySelector.setBounds (keyRow.removeFromLeft (100));
    keyRow.removeFromLeft (12);
    scaleLabel.setBounds (keyRow.removeFromLeft (44));
    scaleSelector.setBounds (keyRow.removeFromLeft (120));
    p.removeFromTop (6);

    auto knobArea = p;
    knobArea.removeFromTop (20); // headroom for each knob's attachToComponent label above it

    juce::Array<juce::Component*> knobs { &retuneSpeedSlider.slider, &amountSlider.slider,
                                           &mixSlider.slider, &outputMeter };
    const int cellWidth = knobArea.getWidth() / knobs.size();
    for (auto* knob : knobs)
        knob->setBounds (knobArea.removeFromLeft (cellWidth).reduced (8, 0));
}
