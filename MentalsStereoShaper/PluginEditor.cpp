#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// StereoAnalyzerComponent
//==============================================================================
void StereoAnalyzerComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto fullBounds = getLocalBounds().toFloat().reduced (14.0f);

    // ---- Correlation bar (bottom strip) ------------------------------------
    auto corrArea = fullBounds.removeFromBottom (26.0f);
    fullBounds.removeFromBottom (10.0f); // gap

    const float correlation = processor.getCorrelation();

    g.setColour (MentalsUI::Colours::slateGray);
    g.fillRoundedRectangle (corrArea, 3.0f);

    const float corrCentreX = corrArea.getCentreX();
    const float corrFillX = corrArea.getX() + (correlation * 0.5f + 0.5f) * corrArea.getWidth();
    auto fillRect = juce::Rectangle<float> (juce::jmin (corrCentreX, corrFillX), corrArea.getY(),
                                             std::abs (corrFillX - corrCentreX), corrArea.getHeight());

    const auto fillColour = correlation < -0.1f ? MentalsUI::Colours::crimsonRed
                           : correlation <  0.5f ? MentalsUI::Colours::amberOrange
                                                  : MentalsUI::Colours::emeraldGreen;
    g.setColour (fillColour);
    g.fillRoundedRectangle (fillRect, 3.0f);

    g.setColour (MentalsUI::Colours::white);
    g.drawText ("Correlation " + juce::String (correlation, 2), corrArea, juce::Justification::centred);

    // ---- Goniometer ---------------------------------------------------------
    const auto centre = fullBounds.getCentre();
    const float radius = juce::jmin (fullBounds.getWidth(), fullBounds.getHeight()) * 0.45f;

    // Diamond guide: its four edges run along the pure-L and pure-R
    // directions (since Side = (L-R)/root2 is the X axis and Mid = (L+R)/
    // root2 is the Y axis here, a channel with only L or only R energy
    // plots at 45 degrees, i.e. along one of these diagonals).
    juce::Path diamond;
    diamond.startNewSubPath (centre.x, centre.y - radius);
    diamond.lineTo (centre.x + radius, centre.y);
    diamond.lineTo (centre.x, centre.y + radius);
    diamond.lineTo (centre.x - radius, centre.y);
    diamond.closeSubPath();
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.strokePath (diamond, juce::PathStrokeType (1.0f));
    g.drawVerticalLine ((int) centre.x, centre.y - radius, centre.y + radius);
    g.drawHorizontalLine ((int) centre.y, centre.x - radius, centre.x + radius);

    g.setColour (MentalsUI::Colours::slateGray);
    constexpr float diag = 0.70710678f;
    g.drawText ("M", juce::Rectangle<float> (centre.x - 10, centre.y - radius - 16, 20, 14), juce::Justification::centred);
    g.drawText ("L", juce::Rectangle<float> (centre.x + radius * diag - 8, centre.y - radius * diag - 16, 20, 14), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (centre.x - radius * diag - 12, centre.y - radius * diag - 16, 20, 14), juce::Justification::centred);

    const int writePos = processor.getGoniometerWritePos();
    g.setColour (MentalsUI::Colours::electricBlue.withAlpha (0.55f));
    for (int i = 0; i < MentalsStereoShaperAudioProcessor::goniometerSize; i += 4)
    {
        const int idx = (writePos + i) % MentalsStereoShaperAudioProcessor::goniometerSize;
        const float L = processor.getGoniometerL (idx).load (std::memory_order_relaxed);
        const float R = processor.getGoniometerR (idx).load (std::memory_order_relaxed);

        const float side = (L - R) * diag;
        const float mid  = (L + R) * diag;
        const float x = centre.x + side * radius;
        const float y = centre.y - mid * radius;

        g.fillEllipse (x - 1.2f, y - 1.2f, 2.4f, 2.4f);
    }
}

//==============================================================================
// MentalsStereoShaperAudioProcessorEditor
//==============================================================================
MentalsStereoShaperAudioProcessorEditor::MentalsStereoShaperAudioProcessorEditor (MentalsStereoShaperAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), analyzer (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&MentalsUI::MentalsLookAndFeel::getSharedInstance());

    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    productNameLabel.setText ("360 Stereo Shaper", juce::dontSendNotification);
    productNameLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    productNameLabel.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    addAndMakeVisible (productNameLabel);

    phaseAlignButton.setClickingTogglesState (true);
    phaseAlignButton.setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
    phaseAlignButton.setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::electricBlue);
    phaseAlignButton.setColour (juce::TextButton::textColourOffId,  MentalsUI::Colours::white);
    phaseAlignButton.setColour (juce::TextButton::textColourOnId,   MentalsUI::Colours::charcoalBlack);
    addAndMakeVisible (phaseAlignButton);
    phaseAlignAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "phaseAlign", phaseAlignButton);

    aiAssistButton.setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::slateGrayDark);
    aiAssistButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
    addAndMakeVisible (aiAssistButton);
    aiAssistButton.addListener (this);

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

    addAndMakeVisible (analyzer);
    addAndMakeVisible (splitter);

    widthSlider.addToParent      ("Width",       *this);
    midGainSlider.addToParent    ("Mid Gain",    *this);
    rotationSlider.addToParent   ("Rotation",    *this);
    autoRotateSlider.addToParent ("Auto Rotate", *this);
    dynamicsSlider.addToParent   ("Dynamics",    *this);

    lowFreqSlider.addToParent   ("Low Freq",   *this);
    highFreqSlider.addToParent  ("High Freq",  *this);
    lowWidthSlider.addToParent  ("Low Width",  *this);
    midWidthSlider.addToParent  ("Mid Width",  *this);
    highWidthSlider.addToParent ("High Width", *this);

    mixSlider.addToParent ("Mix", *this);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "width", widthSlider.slider);
    midGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "midGain", midGainSlider.slider);
    rotationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "rotation", rotationSlider.slider);
    autoRotateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "autoRotateRate", autoRotateSlider.slider);
    dynamicsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "dynamicAmount", dynamicsSlider.slider);
    lowFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "lowFreq", lowFreqSlider.slider);
    highFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "highFreq", highFreqSlider.slider);
    lowWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "lowWidth", lowWidthSlider.slider);
    midWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "midWidth", midWidthSlider.slider);
    highWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "highWidth", highWidthSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);

    setResizable (true, true);
    setResizeLimits (760, 560, 1400, 1000);
    setSize (960, 700);
}

MentalsStereoShaperAudioProcessorEditor::~MentalsStereoShaperAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    aiAssistButton.removeListener (this);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsStereoShaperAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
    else if (button == &aiAssistButton)
        processor.runMixAnalysisAssist();
}

void MentalsStereoShaperAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsStereoShaperAudioProcessorEditor::refreshPresetList()
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

void MentalsStereoShaperAudioProcessorEditor::promptToSavePreset()
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

void MentalsStereoShaperAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);

    auto topBarArea = getLocalBounds().removeFromTop (40);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBarArea);
}

void MentalsStereoShaperAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 240;

    auto topBarArea = area.removeFromTop (topBarHeight);
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        productNameLabel.setBounds (t.removeFromLeft (130));

        presetSaveButton.setBounds (t.removeFromRight (60));
        t.removeFromRight (8);
        presetSelector.setBounds (t.removeFromRight (140));
        t.removeFromRight (12);
        aiAssistButton.setBounds (t.removeFromRight (110));
        t.removeFromRight (8);
        phaseAlignButton.setBounds (t.removeFromRight (110));
    }

    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    analyzer.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);

    constexpr int rightStripWidth = 150;
    auto rightStrip = p.removeFromRight (rightStripWidth);
    p.removeFromRight (10);

    auto layoutKnobRow = [] (juce::Rectangle<int> row, juce::Array<juce::Component*> knobs)
    {
        row.removeFromTop (20); // headroom for each knob's attachToComponent label above it
        const int cellWidth = row.getWidth() / knobs.size();
        for (auto* knob : knobs)
            knob->setBounds (row.removeFromLeft (cellWidth).reduced (6, 0));
    };

    const int rowHeight = p.getHeight() / 2;
    auto row1 = p.removeFromTop (rowHeight);
    auto row2 = p;

    layoutKnobRow (row1, { &widthSlider.slider, &midGainSlider.slider, &rotationSlider.slider,
                           &autoRotateSlider.slider, &dynamicsSlider.slider });
    layoutKnobRow (row2, { &lowFreqSlider.slider, &highFreqSlider.slider, &lowWidthSlider.slider,
                           &midWidthSlider.slider, &highWidthSlider.slider });

    rightStrip.removeFromTop (20); // headroom to line up with the knob rows' labels
    auto meterArea = rightStrip.removeFromRight (44);
    outputMeter.setBounds (meterArea.reduced (4, 0));
    mixSlider.slider.setBounds (rightStrip.reduced (10, 0));
}
