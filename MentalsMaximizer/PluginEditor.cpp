#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// MaximizerCurveComponent
//==============================================================================
void MaximizerCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (10.0f);

    constexpr float minDb = -30.0f, maxDb = 0.0f;
    const float thresholdDb = processor.thresholdParam->get();
    const float ceilingDb   = processor.ceilingParam->get();
    const float inputGainDb = -thresholdDb;

    auto xForDb = [&] (float db) { return bounds.getX() + (db - minDb) / (maxDb - minDb) * bounds.getWidth(); };
    auto yForDb = [&] (float db) { return bounds.getBottom() - (db - minDb) / (maxDb - minDb) * bounds.getHeight(); };

    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.drawLine (xForDb (minDb), yForDb (minDb), xForDb (maxDb), yForDb (maxDb));

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

    static constexpr const char* algorithmNames[] = { "Modern", "Classic", "Warm", "Aggressive" };
    const int algorithmIndex = juce::jlimit (0, 3, processor.algorithmParam->getIndex());
    g.setColour (MentalsUI::Colours::electricBlue);
    g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    g.drawText (juce::String (algorithmNames[algorithmIndex]) + " Algorithm",
                juce::Rectangle<float> (bounds.getX(), bounds.getY(), bounds.getWidth(), 18.0f), juce::Justification::left);
}

//==============================================================================
// MentalsMaximizerAudioProcessorEditor
//==============================================================================
MentalsMaximizerAudioProcessorEditor::MentalsMaximizerAudioProcessorEditor (MentalsMaximizerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), transferCurve (p),
      gainReductionMeter ([&p] { return p.getGainReductionDb(); }, true),
      outputMeter ([&p] { return p.getOutputPeakDb(); })
{
    setLookAndFeel (&hardwareLookAndFeel);

    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Maximizer", juce::dontSendNotification);
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

    for (auto* l : { &inputLufsLabel, &outputLufsLabel })
    {
        l->setColour (juce::Label::textColourId, MentalsUI::Colours::white);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
        addAndMakeVisible (*l);
    }

    thresholdSlider.addToParent ("Threshold", *this);
    ceilingSlider.addToParent   ("Ceiling",   *this);
    characterSlider.addToParent ("Character",*this);
    mixSlider.addToParent       ("Mix",       *this);

    algorithmLabel.setText ("Algorithm", juce::dontSendNotification);
    algorithmLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    algorithmLabel.setJustificationType (juce::Justification::centred);
    algorithmLabel.attachToComponent (&algorithmSelector, false);
    addAndMakeVisible (algorithmLabel);

    algorithmSelector.addItemList ({ "Modern", "Classic", "Warm", "Aggressive" }, 1);
    algorithmSelector.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    algorithmSelector.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    algorithmSelector.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    algorithmSelector.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (algorithmSelector);

    stereoUnlinkToggle.setClickingTogglesState (true);
    stereoUnlinkToggle.setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
    stereoUnlinkToggle.setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::electricBlue);
    stereoUnlinkToggle.setColour (juce::TextButton::textColourOffId,  MentalsUI::Colours::white);
    addAndMakeVisible (stereoUnlinkToggle);

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
    ceilingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ceiling", ceilingSlider.slider);
    characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "character", characterSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);
    algorithmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "algorithm", algorithmSelector);
    stereoUnlinkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "stereoUnlink", stereoUnlinkToggle);
    stereoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "stereo", stereoToggle);

    setResizable (true, true);
    setResizeLimits (720, 520, 1300, 900);
    setSize (940, 640);

    startTimerHz (10);
}

MentalsMaximizerAudioProcessorEditor::~MentalsMaximizerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsMaximizerAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsMaximizerAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsMaximizerAudioProcessorEditor::timerCallback()
{
    inputLufsLabel.setText ("In: " + juce::String (processor.getInputLufs(), 1) + " LUFS", juce::dontSendNotification);
    outputLufsLabel.setText ("Out: " + juce::String (processor.getOutputLufs(), 1) + " LUFS", juce::dontSendNotification);
}

void MentalsMaximizerAudioProcessorEditor::refreshPresetList()
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

void MentalsMaximizerAudioProcessorEditor::promptToSavePreset()
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

void MentalsMaximizerAudioProcessorEditor::paint (juce::Graphics& g)
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

void MentalsMaximizerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (24);
    area.removeFromRight (24);

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 220;
    constexpr int lufsRowHeight  = 22;

    auto topBarArea = area.removeFromTop (topBarHeight);
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        productNameLabel.setBounds (t.removeFromLeft (110));

        presetSaveButton.setBounds (t.removeFromRight (60));
        t.removeFromRight (8);
        presetSelector.setBounds (t.removeFromRight (160));
        t.removeFromRight (12);
        stereoToggle.setBounds (t.removeFromRight (80));
    }

    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto lufsRow      = area.removeFromBottom (lufsRowHeight);
    auto graphArea    = area;

    {
        auto l = lufsRow.reduced (10, 0);
        inputLufsLabel.setBounds (l.removeFromLeft (l.getWidth() / 2));
        outputLufsLabel.setBounds (l);
    }

    lastPanelBounds = panelArea;

    transferCurve.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);
    p.removeFromTop (20); // headroom for each knob's attachToComponent label above it

    auto algorithmArea = p.removeFromLeft (p.getWidth() / 7);
    algorithmSelector.setBounds (algorithmArea.removeFromTop (26).reduced (4, 0));
    stereoUnlinkToggle.setBounds (algorithmArea.removeFromTop (30).reduced (4, 4));

    juce::Array<juce::Component*> knobs { &thresholdSlider.slider, &ceilingSlider.slider, &characterSlider.slider,
                                           &mixSlider.slider, &gainReductionMeter, &outputMeter };
    const int cellWidth = p.getWidth() / knobs.size();
    for (auto* knob : knobs)
        knob->setBounds (p.removeFromLeft (cellWidth).reduced (4, 0));
}
