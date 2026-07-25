#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// ImagerGoniometerComponent
//==============================================================================
void ImagerGoniometerComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto fullBounds = getLocalBounds().toFloat().reduced (14.0f);

    // ---- Correlation bar (bottom strip) -- identical layout/colour logic to
    // Mentals 360 Stereo Shaper's StereoAnalyzerComponent. ---------------------
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

    // ---- Goniometer -----------------------------------------------------------
    // Square area, centred in whatever's left above the correlation bar.
    const float side = juce::jmin (fullBounds.getWidth(), fullBounds.getHeight());
    auto scopeArea = juce::Rectangle<float> (side, side).withCentre (fullBounds.getCentre());

    g.setColour (MentalsUI::Colours::charcoalBlack);
    g.fillEllipse (scopeArea);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (scopeArea, 1.0f);

    // Reference cross: vertical = mono (L==R), horizontal = fully out of
    // phase (L==-R) -- the classic rotated-45-degree goniometer convention.
    g.drawVerticalLine ((int) scopeArea.getCentreX(), scopeArea.getY(), scopeArea.getBottom());
    g.drawHorizontalLine ((int) scopeArea.getCentreY(), scopeArea.getX(), scopeArea.getRight());

    const float scale = side * 0.47f;
    const auto centre = scopeArea.getCentre();

    constexpr int pointsToShow = 1024;
    const int writePos = processor.getGoniometerWritePos();

    for (int i = 0; i < pointsToShow; ++i)
    {
        const int idx = (writePos - 1 - i + MentalsImagerAudioProcessor::goniometerSize * 2) % MentalsImagerAudioProcessor::goniometerSize;
        const float L = processor.getGoniometerL (idx).load (std::memory_order_relaxed);
        const float R = processor.getGoniometerR (idx).load (std::memory_order_relaxed);

        const float x = centre.x + (R - L) * scale;
        const float y = centre.y - (L + R) * scale;

        const float alpha = juce::jmax (0.0f, 1.0f - (float) i / (float) pointsToShow);
        g.setColour (MentalsUI::Colours::emeraldGreen.withAlpha (alpha * 0.55f));
        g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre ({ x, y }));
    }

    g.setColour (MentalsUI::Colours::slateGray);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("M", juce::Rectangle<float> (20.0f, 16.0f).withCentre ({ centre.x, scopeArea.getY() + 10.0f }), juce::Justification::centred);
    g.drawText ("S", juce::Rectangle<float> (20.0f, 16.0f).withCentre ({ scopeArea.getRight() - 12.0f, centre.y }), juce::Justification::centred);
}

//==============================================================================
// MentalsImagerAudioProcessorEditor
//==============================================================================
MentalsImagerAudioProcessorEditor::MentalsImagerAudioProcessorEditor (MentalsImagerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), goniometer (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); })
{
    setLookAndFeel (&hardwareLookAndFeel);

    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Imager", juce::dontSendNotification);
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

    addAndMakeVisible (goniometer);
    addAndMakeVisible (splitter);

    bandsLabel.setText ("Bands", juce::dontSendNotification);
    bandsLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    addAndMakeVisible (bandsLabel);

    bandsSelector.addItemList ({ "1", "2", "3", "4" }, 1);
    bandsSelector.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    bandsSelector.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    bandsSelector.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    bandsSelector.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (bandsSelector);
    bandsSelector.addListener (this);

    crossover1Slider.addToParent ("Xover 1", *this);
    crossover2Slider.addToParent ("Xover 2", *this);
    crossover3Slider.addToParent ("Xover 3", *this);
    width1Slider.addToParent ("Width 1", *this);
    width2Slider.addToParent ("Width 2", *this);
    width3Slider.addToParent ("Width 3", *this);
    width4Slider.addToParent ("Width 4", *this);
    mixSlider.addToParent ("Mix", *this);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    bandsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "bands", bandsSelector);
    crossover1Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "crossover1", crossover1Slider.slider);
    crossover2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "crossover2", crossover2Slider.slider);
    crossover3Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "crossover3", crossover3Slider.slider);
    width1Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "width1", width1Slider.slider);
    width2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "width2", width2Slider.slider);
    width3Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "width3", width3Slider.slider);
    width4Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "width4", width4Slider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);
    stereoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "stereo", stereoToggle);

    updateBandEnablement();

    setResizable (true, true);
    setResizeLimits (760, 520, 1300, 900);
    setSize (940, 640);
}

MentalsImagerAudioProcessorEditor::~MentalsImagerAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
    bandsSelector.removeListener (this);
}

void MentalsImagerAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsImagerAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box == &bandsSelector)
    {
        updateBandEnablement();
        return;
    }

    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);

    updateBandEnablement();
}

void MentalsImagerAudioProcessorEditor::updateBandEnablement()
{
    const int numBands = MentalsImagerAudioProcessor::bandCountChoices[
        (size_t) juce::jlimit (0, 3, bandsSelector.getSelectedItemIndex())];

    crossover1Slider.slider.setEnabled (numBands >= 2);
    crossover1Slider.label.setEnabled  (numBands >= 2);
    crossover2Slider.slider.setEnabled (numBands >= 3);
    crossover2Slider.label.setEnabled  (numBands >= 3);
    crossover3Slider.slider.setEnabled (numBands >= 4);
    crossover3Slider.label.setEnabled  (numBands >= 4);

    width2Slider.slider.setEnabled (numBands >= 2);
    width2Slider.label.setEnabled  (numBands >= 2);
    width3Slider.slider.setEnabled (numBands >= 3);
    width3Slider.label.setEnabled  (numBands >= 3);
    width4Slider.slider.setEnabled (numBands >= 4);
    width4Slider.label.setEnabled  (numBands >= 4);
}

void MentalsImagerAudioProcessorEditor::refreshPresetList()
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

void MentalsImagerAudioProcessorEditor::promptToSavePreset()
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

void MentalsImagerAudioProcessorEditor::paint (juce::Graphics& g)
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

void MentalsImagerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (24);
    area.removeFromRight (24);

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 300; // Bands row + 2 knob rows (crossovers, widths)

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

    // Controls are bottom-anchored with a fixed height, and the goniometer
    // always fills exactly whatever space remains above them -- same
    // convention as Doubler's voice-spread graph/Stereo Shaper's stage.
    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    lastPanelBounds = panelArea;

    goniometer.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);

    // Out gets its own fixed-width cell spanning the whole panel height
    // (matching Reverb/Compressor/Doubler's convention) rather than sharing
    // an equal division with the knobs -- a real analog VU meter reads as a
    // wide rectangle, not a square.
    constexpr int meterCellWidth = 180;
    auto meterCell = p.removeFromRight (meterCellWidth);

    auto bandsRow = p.removeFromTop (26);
    bandsLabel.setBounds (bandsRow.removeFromLeft (50));
    bandsSelector.setBounds (bandsRow.removeFromLeft (70));
    p.removeFromTop (6);

    auto crossoverRow = p.removeFromTop (p.getHeight() / 2);
    auto widthRow = p;

    crossoverRow.removeFromTop (20); // headroom for each knob's attachToComponent label above it
    widthRow.removeFromTop (20);

    juce::Array<juce::Component*> crossoverKnobs { &crossover1Slider.slider, &crossover2Slider.slider, &crossover3Slider.slider };
    const int crossoverCellWidth = crossoverRow.getWidth() / crossoverKnobs.size();
    for (auto* knob : crossoverKnobs)
        knob->setBounds (crossoverRow.removeFromLeft (crossoverCellWidth).reduced (4, 0));

    juce::Array<juce::Component*> widthKnobs { &width1Slider.slider, &width2Slider.slider,
                                                &width3Slider.slider, &width4Slider.slider, &mixSlider.slider };
    const int widthCellWidth = widthRow.getWidth() / widthKnobs.size();
    for (auto* knob : widthKnobs)
        knob->setBounds (widthRow.removeFromLeft (widthCellWidth).reduced (4, 0));

    constexpr int meterHeight = 100;
    outputMeter.setBounds (meterCell.withSizeKeepingCentre (meterCell.getWidth() - 12, meterHeight));
}
