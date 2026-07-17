#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// LfoPreviewComponent
//==============================================================================
void LfoPreviewComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (10.0f);
    const float depthMs = processor.depthParam->get();

    // Centre reference line.
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());

    // Two full cycles across the width, height-scaled by Depth relative to
    // its own maximum (so the shape is always visible even at low Depth).
    constexpr float maxDepthMs = 10.0f;
    const float amplitude = (depthMs / maxDepthMs) * bounds.getHeight() * 0.45f;

    juce::Path curve;
    constexpr int numPoints = 200;
    for (int i = 0; i <= numPoints; ++i)
    {
        const float t = (float) i / (float) numPoints;
        const float x = bounds.getX() + t * bounds.getWidth();
        const float y = bounds.getCentreY() - amplitude * std::sin (t * juce::MathConstants<float>::twoPi * 2.0f);

        if (i == 0) curve.startNewSubPath (x, y);
        else        curve.lineTo (x, y);
    }

    g.setColour (MentalsUI::Colours::goldenYellow);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    // Playhead: the left channel's actual current phase, wrapped into the
    // same two-cycle window.
    const float phase01 = processor.getLfoPhase01();
    const float playheadT = std::fmod (phase01 * 2.0f, 1.0f);
    const float playheadX = bounds.getX() + playheadT * bounds.getWidth();
    const float playheadY = bounds.getCentreY() - amplitude * std::sin (playheadT * juce::MathConstants<float>::twoPi * 2.0f);

    g.setColour (MentalsUI::Colours::electricBlue);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ playheadX, playheadY }));
}

//==============================================================================
// MentalsChorusAudioProcessorEditor
//==============================================================================
MentalsChorusAudioProcessorEditor::MentalsChorusAudioProcessorEditor (MentalsChorusAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), lfoPreview (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&MentalsUI::MentalsLookAndFeel::getSharedInstance());

    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Chorus", juce::dontSendNotification);
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

    addAndMakeVisible (lfoPreview);
    addAndMakeVisible (splitter);

    rateSlider.addToParent     ("Rate",     *this);
    depthSlider.addToParent    ("Depth",    *this);
    delaySlider.addToParent    ("Delay",    *this);
    feedbackSlider.addToParent ("Feedback", *this);
    mixSlider.addToParent      ("Mix",      *this);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "rate", rateSlider.slider);
    depthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "depth", depthSlider.slider);
    delayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "delay", delaySlider.slider);
    feedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "feedback", feedbackSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);

    setResizable (true, true);
    setResizeLimits (620, 460, 1300, 900);
    setSize (860, 600);
}

MentalsChorusAudioProcessorEditor::~MentalsChorusAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsChorusAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsChorusAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsChorusAudioProcessorEditor::refreshPresetList()
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

void MentalsChorusAudioProcessorEditor::promptToSavePreset()
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

void MentalsChorusAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);

    auto topBarArea = getLocalBounds().removeFromTop (40);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBarArea);
}

void MentalsChorusAudioProcessorEditor::resized()
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

    lfoPreview.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);
    p.removeFromTop (20); // headroom for each knob's attachToComponent label above it

    juce::Array<juce::Component*> knobs { &rateSlider.slider, &depthSlider.slider, &delaySlider.slider,
                                           &feedbackSlider.slider, &mixSlider.slider, &outputMeter };
    const int cellWidth = p.getWidth() / knobs.size();
    for (auto* knob : knobs)
        knob->setBounds (p.removeFromLeft (cellWidth).reduced (4, 0));
}
