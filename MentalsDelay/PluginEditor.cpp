#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// EchoPatternComponent
//==============================================================================
void EchoPatternComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    const float delayMs  = processor.delayTimeMsParam->get();
    const float feedback = juce::jlimit (0.0f, 0.95f, processor.feedbackParam->get() * 0.01f);
    const float mix      = juce::jlimit (0.0f, 1.0f, processor.mixParam->get() * 0.01f);
    const bool  pingPong = processor.pingPongParam->get();

    // Enough window to show several repeats, capped so a short delay time
    // doesn't zoom in to an unreadably tiny window and a long one doesn't
    // force showing an absurd time span.
    const float windowMs = juce::jlimit (800.0f, 6000.0f, delayMs * 5.0f);

    auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    for (float gridMs = delayMs; gridMs <= windowMs; gridMs += delayMs)
        g.drawVerticalLine ((int) (bounds.getX() + 10.0f + (gridMs / windowMs) * (bounds.getWidth() - 20.0f)),
                             0.0f, bounds.getHeight());

    const float originX      = bounds.getX() + 10.0f;
    const float usableWidth  = bounds.getWidth() - 20.0f;
    const float baselineY    = bounds.getBottom() - 10.0f;
    const float maxBarHeight = bounds.getHeight() - 24.0f;

    auto xForMs = [&] (float ms) { return originX + (ms / windowMs) * usableWidth; };

    // Dry tap at t=0.
    {
        const float h = maxBarHeight * juce::jmax (0.08f, 1.0f - mix);
        g.setColour (MentalsUI::Colours::white);
        g.fillRoundedRectangle (xForMs (0.0f) - 3.0f, baselineY - h, 6.0f, h, 2.0f);
    }

    // Repeats: amplitude decays by `feedback` each repeat. Ping-pong
    // alternates the bar colour to suggest left/right bounce.
    float amplitude = mix;
    int tapIndex = 1;
    for (float t = delayMs; t <= windowMs && amplitude > 0.02f && tapIndex < 500; t += delayMs, ++tapIndex)
    {
        amplitude *= feedback;
        const float h = maxBarHeight * amplitude;
        const bool rightSide = pingPong && (tapIndex % 2 == 0);

        g.setColour (rightSide ? MentalsUI::Colours::electricBlue : MentalsUI::Colours::goldenYellow);
        g.fillRoundedRectangle (xForMs (t) - 2.5f, baselineY - h, 5.0f, h, 2.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine ((int) baselineY, bounds.getX(), bounds.getRight());
}

//==============================================================================
// MentalsDelayAudioProcessorEditor
//==============================================================================
MentalsDelayAudioProcessorEditor::MentalsDelayAudioProcessorEditor (MentalsDelayAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), echoPattern (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); })
{
    setLookAndFeel (&hardwareLookAndFeel);

    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Delay", juce::dontSendNotification);
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

    addAndMakeVisible (echoPattern);
    addAndMakeVisible (splitter);

    delayTimeSlider.addToParent ("Delay Time", *this);
    feedbackSlider.addToParent  ("Feedback",   *this);
    mixSlider.addToParent       ("Mix",        *this);
    lowCutSlider.addToParent    ("Low Cut",    *this);
    highCutSlider.addToParent   ("High Cut",   *this);

    pingPongToggle.setColour (juce::ToggleButton::textColourId, MentalsUI::Colours::white);
    pingPongToggle.setColour (juce::ToggleButton::tickColourId, MentalsUI::Colours::white);
    addAndMakeVisible (pingPongToggle);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    delayTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "delayTimeMs", delayTimeSlider.slider);
    feedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "feedback", feedbackSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "lowCut", lowCutSlider.slider);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "highCut", highCutSlider.slider);
    pingPongAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "pingPong", pingPongToggle);

    setResizable (true, true);
    setResizeLimits (560, 420, 1200, 900);
    setSize (760, 560);
}

MentalsDelayAudioProcessorEditor::~MentalsDelayAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsDelayAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsDelayAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsDelayAudioProcessorEditor::refreshPresetList()
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

void MentalsDelayAudioProcessorEditor::promptToSavePreset()
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

void MentalsDelayAudioProcessorEditor::paint (juce::Graphics& g)
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

void MentalsDelayAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (24);
    area.removeFromRight (24);

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 220;

    auto topBarArea = area.removeFromTop (topBarHeight);
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        productNameLabel.setBounds (t.removeFromLeft (70));
        t.removeFromLeft (12);
        presetSelector.setBounds (t.removeFromLeft (160));
        t.removeFromLeft (8);
        presetSaveButton.setBounds (t.removeFromLeft (60));
    }

    // Controls are bottom-anchored with a fixed height, and the echo-pattern
    // graph always fills exactly whatever space remains above them.
    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    lastPanelBounds = panelArea;

    echoPattern.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);
    p.removeFromTop (20); // headroom for each knob's attachToComponent label above it

    juce::Array<juce::Component*> knobs { &delayTimeSlider.slider, &feedbackSlider.slider, &mixSlider.slider,
                                           &lowCutSlider.slider, &highCutSlider.slider, &outputMeter };
    const int cellWidth = p.getWidth() / (knobs.size() + 1); // +1 reserves a cell for the Ping-Pong toggle
    for (auto* knob : knobs)
        knob->setBounds (p.removeFromLeft (cellWidth).reduced (4, 0));

    pingPongToggle.setBounds (p.reduced (8, 0).withHeight (26).withY (p.getY() + p.getHeight() / 2 - 13));
}
