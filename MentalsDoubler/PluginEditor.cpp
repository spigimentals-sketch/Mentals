#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// DoublerVoicesComponent
//==============================================================================
void DoublerVoicesComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (16.0f, 14.0f);

    const int numVoices = MentalsDoublerAudioProcessor::voiceCountChoices[
        (size_t) juce::jlimit (0, 1, processor.voicesParam->getIndex())];
    const float detune   = processor.detuneParam->get();
    const float width    = processor.stereoParam->get() ? processor.widthParam->get() : 0.0f;
    const float humanize = juce::jlimit (0.0f, 100.0f, processor.humanizeParam->get());

    // Centre reference line (the dry signal's own position -- always dead
    // centre, no detune).
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawVerticalLine ((int) bounds.getCentreX(), bounds.getY(), bounds.getBottom());
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());

    const float maxCents = MentalsDoublerAudioProcessor::maxDetuneCents;

    for (int v = 0; v < numVoices; ++v)
    {
        const float cents = MentalsDoublerAudioProcessor::computeVoiceBaseDetuneCents (v, numVoices, detune);
        const float pan   = MentalsDoublerAudioProcessor::computeVoicePan (v, numVoices, width);

        const float x = bounds.getCentreX() + pan * bounds.getWidth() * 0.5f;
        const float y = bounds.getCentreY() - (cents / juce::jmax (1.0f, maxCents)) * bounds.getHeight() * 0.42f;

        // Humanize shows as a soft halo around each voice's dot -- the more
        // humanize, the more that voice wanders around its base position.
        const float haloRadius = 6.0f + (humanize * 0.01f) * 14.0f;
        g.setColour (MentalsUI::Colours::electricBlue.withAlpha (0.12f + humanize * 0.0015f));
        g.fillEllipse (juce::Rectangle<float> (haloRadius * 2.0f, haloRadius * 2.0f).withCentre ({ x, y }));

        g.setColour (MentalsUI::Colours::goldenYellow);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ x, y }));

        g.setColour (MentalsUI::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (juce::String (v + 1), juce::Rectangle<float> (18.0f, 14.0f).withCentre ({ x, y - 14.0f }),
                    juce::Justification::centred);
    }

    // Dry marker, dead centre.
    g.setColour (MentalsUI::Colours::white);
    g.drawEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (bounds.getCentre()), 1.5f);

    g.setColour (MentalsUI::Colours::slateGray);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("Pan (Width) / Detune (cents)  --  ring = Humanize", bounds, juce::Justification::bottomLeft);
}

//==============================================================================
// MentalsDoublerAudioProcessorEditor
//==============================================================================
MentalsDoublerAudioProcessorEditor::MentalsDoublerAudioProcessorEditor (MentalsDoublerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), voicesGraph (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); })
{
    setLookAndFeel (&hardwareLookAndFeel);

    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Doubler", juce::dontSendNotification);
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

    addAndMakeVisible (voicesGraph);
    addAndMakeVisible (splitter);

    voicesLabel.setText ("Voices", juce::dontSendNotification);
    voicesLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    addAndMakeVisible (voicesLabel);

    voicesSelector.addItemList ({ "2", "4" }, 1);
    voicesSelector.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    voicesSelector.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    voicesSelector.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    voicesSelector.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (voicesSelector);

    detuneSlider.addToParent    ("Detune",   *this);
    delaySlider.addToParent     ("Delay",    *this);
    widthSlider.addToParent     ("Width",    *this);
    humanizeSlider.addToParent  ("Humanize", *this);
    lowCutSlider.addToParent    ("Low Cut",  *this);
    mixSlider.addToParent       ("Mix",      *this);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    voicesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "voices", voicesSelector);
    detuneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "detune", detuneSlider.slider);
    delayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "delay", delaySlider.slider);
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "width", widthSlider.slider);
    humanizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "humanize", humanizeSlider.slider);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "lowCut", lowCutSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);
    stereoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "stereo", stereoToggle);

    setResizable (true, true);
    setResizeLimits (700, 480, 1300, 900);
    setSize (900, 600);
}

MentalsDoublerAudioProcessorEditor::~MentalsDoublerAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsDoublerAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsDoublerAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsDoublerAudioProcessorEditor::refreshPresetList()
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

void MentalsDoublerAudioProcessorEditor::promptToSavePreset()
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

void MentalsDoublerAudioProcessorEditor::paint (juce::Graphics& g)
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

void MentalsDoublerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (24);
    area.removeFromRight (24);

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 250; // Voices row above the knobs, like Saturator's Type row

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
        t.removeFromLeft (12);
        stereoToggle.setBounds (t.removeFromLeft (80));
    }

    // Controls are bottom-anchored with a fixed height, and the voice-spread
    // graph always fills exactly whatever space remains above them.
    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    lastPanelBounds = panelArea;

    voicesGraph.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);

    auto voicesRow = p.removeFromTop (24);
    voicesLabel.setBounds (voicesRow.removeFromLeft (50));
    voicesSelector.setBounds (voicesRow.removeFromLeft (80));
    p.removeFromTop (6);

    auto knobArea = p;
    knobArea.removeFromTop (20); // headroom for each knob's attachToComponent label above it

    // Out gets its own fixed-width cell (matching Reverb/Compressor's
    // convention) rather than sharing an equal division with the knobs --
    // a real analog VU meter reads as a wide rectangle, not a square.
    constexpr int meterCellWidth = 180;
    auto meterCell = knobArea.removeFromRight (meterCellWidth);

    juce::Array<juce::Component*> knobs { &detuneSlider.slider, &delaySlider.slider, &widthSlider.slider,
                                           &humanizeSlider.slider, &lowCutSlider.slider, &mixSlider.slider };
    const int cellWidth = knobArea.getWidth() / knobs.size();
    for (auto* knob : knobs)
        knob->setBounds (knobArea.removeFromLeft (cellWidth).reduced (4, 0));

    constexpr int meterHeight = 100;
    outputMeter.setBounds (meterCell.withSizeKeepingCentre (meterCell.getWidth() - 12, meterHeight));
}
