#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// DecayEnvelopeComponent
//==============================================================================
void DecayEnvelopeComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    const float roomSize   = juce::jlimit (0.0f, 1.0f, processor.roomSizeParam->get() * 0.01f);
    const float damping    = juce::jlimit (0.0f, 1.0f, processor.dampingParam->get() * 0.01f);
    const float mix        = juce::jlimit (0.0f, 1.0f, processor.mixParam->get() * 0.01f);
    const float preDelayMs = processor.preDelayMsParam->get();
    const bool  freeze     = processor.freezeParam->get();

    auto bounds = getLocalBounds().toFloat();
    const float originX     = bounds.getX() + 10.0f;
    const float usableWidth = bounds.getWidth() - 20.0f;
    const float baselineY   = bounds.getBottom() - 10.0f;
    const float maxHeight   = bounds.getHeight() - 20.0f;

    constexpr float windowSeconds = 4.0f;
    const float preDelaySeconds = preDelayMs * 0.001f;

    // Bigger room -> longer tail; more damping -> shorter perceived tail.
    // Freeze sustains indefinitely, drawn as a decay so slow it looks flat
    // across the visible window.
    const float decayTimeConstant = freeze ? 1.0e6f
        : juce::jmap (roomSize, 0.0f, 1.0f, 0.25f, 3.0f) * (1.0f - 0.5f * damping);

    auto xForSeconds = [&] (float s) { return originX + (s / windowSeconds) * usableWidth; };

    // Dry level indicator: a short flat segment at the pre-reverb level.
    {
        const float dryY = baselineY - maxHeight * (1.0f - mix) * 0.6f;
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawHorizontalLine ((int) dryY, originX, originX + usableWidth * 0.12f);
    }

    // Wet decay envelope.
    juce::Path wetPath;
    bool started = false;
    for (float x = 0.0f; x <= usableWidth; x += 2.0f)
    {
        const float s = (x / usableWidth) * windowSeconds;
        float amp = 0.0f;
        if (s >= preDelaySeconds)
            amp = mix * std::exp (-(s - preDelaySeconds) / decayTimeConstant);

        const float y = baselineY - amp * maxHeight;
        if (! started) { wetPath.startNewSubPath (originX + x, y); started = true; }
        else            wetPath.lineTo (originX + x, y);
    }

    juce::Path filledWave (wetPath);
    filledWave.lineTo (originX + usableWidth, baselineY);
    filledWave.lineTo (originX, baselineY);
    filledWave.closeSubPath();

    juce::ColourGradient gradient (MentalsUI::Colours::goldenYellow.withAlpha (0.35f), 0.0f, bounds.getY(),
                                    MentalsUI::Colours::goldenYellow.withAlpha (0.02f), 0.0f, baselineY, false);
    g.setGradientFill (gradient);
    g.fillPath (filledWave);

    g.setColour (MentalsUI::Colours::goldenYellow);
    g.strokePath (wetPath, juce::PathStrokeType (2.0f));

    // Pre-delay marker.
    if (preDelaySeconds > 0.0f)
    {
        g.setColour (MentalsUI::Colours::electricBlue.withAlpha (0.6f));
        g.drawVerticalLine ((int) xForSeconds (preDelaySeconds), 0.0f, bounds.getHeight());
    }

    if (freeze)
    {
        g.setColour (MentalsUI::Colours::electricBlue);
        g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
        g.drawText ("FREEZE", getLocalBounds().reduced (8), juce::Justification::topRight);
    }

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine ((int) baselineY, bounds.getX(), bounds.getRight());
}

//==============================================================================
// MentalsReverbAudioProcessorEditor
//==============================================================================
MentalsReverbAudioProcessorEditor::MentalsReverbAudioProcessorEditor (MentalsReverbAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), decayEnvelope (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&MentalsUI::MentalsLookAndFeel::getSharedInstance());

    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Reverb", juce::dontSendNotification);
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

    addAndMakeVisible (decayEnvelope);
    addAndMakeVisible (splitter);

    roomSizeSlider.addToParent ("Room Size",  *this);
    dampingSlider.addToParent  ("Damping",    *this);
    widthSlider.addToParent    ("Width",      *this);
    mixSlider.addToParent      ("Mix",        *this);
    preDelaySlider.addToParent ("Pre-Delay",  *this);
    shimmerSlider.addToParent  ("Shimmer",    *this);

    freezeToggle.setColour (juce::ToggleButton::textColourId, MentalsUI::Colours::white);
    freezeToggle.setColour (juce::ToggleButton::tickColourId, MentalsUI::Colours::white);
    addAndMakeVisible (freezeToggle);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    roomSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "roomSize", roomSizeSlider.slider);
    dampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "damping", dampingSlider.slider);
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "width", widthSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);
    preDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "preDelayMs", preDelaySlider.slider);
    shimmerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "shimmerAmount", shimmerSlider.slider);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "freeze", freezeToggle);

    setResizable (true, true);
    setResizeLimits (560, 420, 1200, 900);
    setSize (760, 560);
}

MentalsReverbAudioProcessorEditor::~MentalsReverbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsReverbAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsReverbAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsReverbAudioProcessorEditor::refreshPresetList()
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

void MentalsReverbAudioProcessorEditor::promptToSavePreset()
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

void MentalsReverbAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);

    auto topBarArea = getLocalBounds().removeFromTop (40);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBarArea);
}

void MentalsReverbAudioProcessorEditor::resized()
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
        productNameLabel.setBounds (t.removeFromLeft (80));
        t.removeFromLeft (12);
        presetSelector.setBounds (t.removeFromLeft (160));
        t.removeFromLeft (8);
        presetSaveButton.setBounds (t.removeFromLeft (60));
    }

    // Controls are bottom-anchored with a fixed height, and the decay-
    // envelope graph always fills exactly whatever space remains above them.
    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    decayEnvelope.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);
    p.removeFromTop (20); // headroom for each knob's attachToComponent label above it

    juce::Array<juce::Component*> knobs { &roomSizeSlider.slider, &dampingSlider.slider, &widthSlider.slider,
                                           &mixSlider.slider, &preDelaySlider.slider, &shimmerSlider.slider, &outputMeter };
    const int cellWidth = p.getWidth() / (knobs.size() + 1); // +1 reserves a cell for the Freeze toggle
    for (auto* knob : knobs)
        knob->setBounds (p.removeFromLeft (cellWidth).reduced (8, 0));

    freezeToggle.setBounds (p.reduced (8, 0).withHeight (26).withY (p.getY() + p.getHeight() / 2 - 13));
}
