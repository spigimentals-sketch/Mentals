#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// LimiterTransferCurveComponent
//==============================================================================
void LimiterTransferCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (10.0f);

    constexpr float minDb = -60.0f, maxDb = 0.0f;
    const float inputGainDb = processor.inputGainParam->get();
    const float ceilingDb   = processor.ceilingParam->get();

    auto xForDb = [&] (float db) { return bounds.getX() + (db - minDb) / (maxDb - minDb) * bounds.getWidth(); };
    auto yForDb = [&] (float db) { return bounds.getBottom() - (db - minDb) / (maxDb - minDb) * bounds.getHeight(); };

    // Reference diagonal (unity, no gain or limiting).
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.drawLine (xForDb (minDb), yForDb (minDb), xForDb (maxDb), yForDb (maxDb));

    // Ceiling marker.
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine ((int) yForDb (ceilingDb), bounds.getX(), bounds.getRight());

    juce::Path curve;
    bool started = false;
    for (float inDb = minDb; inDb <= maxDb; inDb += 0.5f)
    {
        const float outDb = juce::jmin (inDb + inputGainDb, ceilingDb);
        const float x = xForDb (inDb);
        const float y = yForDb (juce::jlimit (minDb, maxDb, outDb));

        if (! started) { curve.startNewSubPath (x, y); started = true; }
        else            curve.lineTo (x, y);
    }

    g.setColour (MentalsUI::Colours::goldenYellow);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
}

//==============================================================================
// MentalsLimiterAudioProcessorEditor
//==============================================================================
MentalsLimiterAudioProcessorEditor::MentalsLimiterAudioProcessorEditor (MentalsLimiterAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), transferCurve (p),
      gainReductionMeter ([&p] { return p.getGainReductionDb(); }),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&MentalsUI::MentalsLookAndFeel::getSharedInstance());

    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Limiter", juce::dontSendNotification);
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

    inputGainSlider.addToParent ("Input Gain", *this);
    ceilingSlider.addToParent   ("Ceiling",    *this);
    releaseSlider.addToParent   ("Release",    *this);
    mixSlider.addToParent       ("Mix",        *this);

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

    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "inputGain", inputGainSlider.slider);
    ceilingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ceiling", ceilingSlider.slider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "release", releaseSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);

    setResizable (true, true);
    setResizeLimits (620, 460, 1300, 900);
    setSize (860, 600);
}

MentalsLimiterAudioProcessorEditor::~MentalsLimiterAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsLimiterAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsLimiterAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsLimiterAudioProcessorEditor::refreshPresetList()
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

void MentalsLimiterAudioProcessorEditor::promptToSavePreset()
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

void MentalsLimiterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);

    auto topBarArea = getLocalBounds().removeFromTop (40);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBarArea);
}

void MentalsLimiterAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 220;

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
    p.removeFromTop (20); // headroom for each knob's attachToComponent label above it

    juce::Array<juce::Component*> knobs { &inputGainSlider.slider, &ceilingSlider.slider, &releaseSlider.slider,
                                           &mixSlider.slider, &gainReductionMeter, &outputMeter };
    const int cellWidth = p.getWidth() / knobs.size();
    for (auto* knob : knobs)
        knob->setBounds (p.removeFromLeft (cellWidth).reduced (4, 0));
}
