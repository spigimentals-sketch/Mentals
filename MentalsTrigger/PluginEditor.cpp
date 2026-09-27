#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

namespace
{
    const juce::Colour layerAccentColours[MentalsTriggerAudioProcessor::numLayers] = {
        MentalsUI::Colours::emeraldGreen, MentalsUI::Colours::goldenYellow, MentalsUI::Colours::crimsonRed
    };
}

//==============================================================================
// WaveformDisplayComponent
//==============================================================================
float WaveformDisplayComponent::thresholdToY (float thresholdDb) const noexcept
{
    const float linear = juce::jlimit (0.0f, 1.0f, juce::Decibels::decibelsToGain (thresholdDb));
    const auto plotArea = getLocalBounds().toFloat().reduced (4.0f);
    return plotArea.getBottom() - linear * plotArea.getHeight();
}

bool WaveformDisplayComponent::isNearThresholdLine (float y) const noexcept
{
    return std::abs (y - thresholdToY (processor.thresholdParam->get())) < 8.0f;
}

void WaveformDisplayComponent::mouseDown (const juce::MouseEvent& e)
{
    isDraggingThreshold = isNearThresholdLine (e.position.y);
}

void WaveformDisplayComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! isDraggingThreshold)
        return;

    const auto plotArea = getLocalBounds().toFloat().reduced (4.0f);
    const float linear = juce::jlimit (0.0001f, 1.0f, (plotArea.getBottom() - e.position.y) / plotArea.getHeight());
    const float db = juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (linear));

    auto* param = processor.thresholdParam;
    param->setValueNotifyingHost (param->convertTo0to1 (db));
}

void WaveformDisplayComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRoundedRectangle (bounds, 6.0f);

    std::array<MentalsTriggerAudioProcessor::WaveformColumn, MentalsTriggerAudioProcessor::waveformColumns> columns;
    processor.getWaveformSnapshot (columns);

    auto plotArea = bounds.reduced (4.0f);
    const float columnWidth = plotArea.getWidth() / (float) MentalsTriggerAudioProcessor::waveformColumns;

    g.setColour (MentalsUI::Colours::electricBlue.withAlpha (0.75f));
    for (int i = 0; i < MentalsTriggerAudioProcessor::waveformColumns; ++i)
    {
        const float peak = juce::jlimit (0.0f, 1.0f, columns[(size_t) i].peak);
        const float barHeight = peak * plotArea.getHeight();
        const float x = plotArea.getX() + (float) i * columnWidth;
        g.fillRect (x, plotArea.getBottom() - barHeight, juce::jmax (1.0f, columnWidth - 0.5f), barHeight);
    }

    for (int i = 0; i < MentalsTriggerAudioProcessor::waveformColumns; ++i)
    {
        const auto& column = columns[(size_t) i];
        if (column.hitVelocity < 0.0f)
            continue;

        const int layer = juce::jlimit (0, MentalsTriggerAudioProcessor::numLayers - 1, column.hitLayer);
        const float x = plotArea.getX() + (float) i * columnWidth;
        g.setColour (layerAccentColours[layer]);
        g.fillRect (x, plotArea.getY(), juce::jmax (1.5f, columnWidth), plotArea.getHeight());
        g.setColour (layerAccentColours[layer].brighter());
        g.fillEllipse (x - 2.5f, plotArea.getY() - 2.0f, 7.0f, 7.0f);
    }

    const float thresholdY = thresholdToY (processor.thresholdParam->get());
    g.setColour (MentalsUI::Colours::crimsonRed.withAlpha (0.85f));
    g.drawHorizontalLine ((int) thresholdY, plotArea.getX(), plotArea.getRight());
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("Threshold", juce::Rectangle<float> (plotArea.getRight() - 70.0f, thresholdY - 14.0f, 66.0f, 12.0f),
                juce::Justification::centredRight);

    g.setColour (MentalsUI::Colours::slateGray);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
}

//==============================================================================
// VelocityCurveComponent
//==============================================================================
void VelocityCurveComponent::mouseDown (const juce::MouseEvent&)
{
    dragStartCurveValue = processor.curveParam->get();
}

void VelocityCurveComponent::mouseDrag (const juce::MouseEvent& e)
{
    const float deltaY = (float) e.getDistanceFromDragStartY();
    const float newValue = juce::jlimit (-100.0f, 100.0f, dragStartCurveValue - deltaY * 0.6f);
    auto* param = processor.curveParam;
    param->setValueNotifyingHost (param->convertTo0to1 (newValue));
}

void VelocityCurveComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto plotArea = bounds.reduced (8.0f);
    g.setColour (MentalsUI::Colours::slateGray.withAlpha (0.4f));
    g.drawRect (plotArea, 1.0f);
    g.drawLine (plotArea.getX(), plotArea.getBottom(), plotArea.getRight(), plotArea.getY(), 0.6f); // linear reference

    juce::Path curve;
    constexpr int numPoints = 40;
    for (int i = 0; i <= numPoints; ++i)
    {
        const float x01 = (float) i / (float) numPoints;
        const float y01 = processor.shapeVelocity (x01);
        const float x = plotArea.getX() + x01 * plotArea.getWidth();
        const float y = plotArea.getBottom() - y01 * plotArea.getHeight();
        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }
    g.setColour (MentalsUI::Colours::electricBlue);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    g.setColour (MentalsUI::Colours::slateGray);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
}

//==============================================================================
// LayerCardComponent
//==============================================================================
LayerCardComponent::LayerCardComponent (int layerIndexIn, MentalsTriggerAudioProcessor& proc)
    : layerIndex (layerIndexIn), processor (proc)
{
    for (int i = 0; i < MentalsTriggerAudioProcessor::numRoundRobins; ++i)
    {
        auto& button = roundRobinButtons[(size_t) i];
        button.setButtonText (MentalsTriggerAudioProcessor::roundRobinNames[i]);
        button.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
        button.onClick = [this, i] { chooseFile (i); };
        addAndMakeVisible (button);
    }

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
    muteButton.setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::crimsonRed);
    muteButton.setColour (juce::TextButton::textColourOffId,  MentalsUI::Colours::white);
    muteButton.setColour (juce::TextButton::textColourOnId,   MentalsUI::Colours::white);
    muteButton.setTooltip ("Mute this layer");
    addAndMakeVisible (muteButton);

    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
    soloButton.setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::goldenYellow);
    soloButton.setColour (juce::TextButton::textColourOffId,  MentalsUI::Colours::white);
    soloButton.setColour (juce::TextButton::textColourOnId,   MentalsUI::Colours::charcoalBlack);
    soloButton.setTooltip ("Solo this layer");
    addAndMakeVisible (soloButton);

    muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, juce::String ("mute") + MentalsTriggerAudioProcessor::layerNames[layerIndex], muteButton);
    soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, juce::String ("solo") + MentalsTriggerAudioProcessor::layerNames[layerIndex], soloButton);

    startTimerHz (20);
}

void LayerCardComponent::resized()
{
    auto area = getLocalBounds().reduced (8);

    auto topRow = area.removeFromTop (22);
    topRow.removeFromLeft (topRow.getWidth() - 48); // push M/S to the top-right corner
    soloButton.setBounds (topRow.removeFromRight (22));
    muteButton.setBounds (topRow.removeFromRight (22));

    auto buttonRow = area.removeFromBottom (24);
    const int buttonWidth = buttonRow.getWidth() / MentalsTriggerAudioProcessor::numRoundRobins;
    for (auto& button : roundRobinButtons)
        button.setBounds (buttonRow.removeFromLeft (buttonWidth).reduced (2, 0));
}

void LayerCardComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    const auto accent = layerAccentColours[layerIndex];

    juce::ColourGradient cardGradient (MentalsUI::Colours::slateGray.brighter (0.05f), bounds.getTopLeft(),
                                        MentalsUI::Colours::slateGrayDark, bounds.getBottomLeft(), false);
    g.setGradientFill (cardGradient);
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (accent);
    g.drawRoundedRectangle (bounds, 8.0f, 2.0f);

    auto textArea = bounds.reduced (10.0f, 8.0f);
    textArea.removeFromBottom (32.0f); // room for the A/B/C/D row below

    auto titleRow = textArea.removeFromTop (22.0f);
    titleRow.removeFromRight (48.0f); // room for the M/S buttons in the top-right corner
    g.setColour (MentalsUI::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));
    g.drawText (MentalsTriggerAudioProcessor::layerNames[layerIndex], titleRow, juce::Justification::centred);

    const auto& layer = processor.getLayers()[(size_t) layerIndex];
    const int loadedCount = (int) std::count_if (layer.roundRobins.begin(), layer.roundRobins.end(),
                                                   [] (const auto& rr) { return rr.hasAudio(); });

    g.setColour (loadedCount > 0 ? MentalsUI::Colours::white.withAlpha (0.7f) : MentalsUI::Colours::slateGray);
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawText (loadedCount == 0 ? "No samples loaded"
                                 : (juce::String (loadedCount) + " sample" + (loadedCount > 1 ? "s" : "") + " loaded"),
                textArea, juce::Justification::centred);
}

void LayerCardComponent::timerCallback()
{
    const auto hitCounter = processor.getLastHitCounter();
    if (hitCounter != lastSeenHitCounter)
    {
        lastSeenHitCounter = hitCounter;
        if (processor.getLastTriggeredLayer() == layerIndex)
        {
            flashAlpha = 1.0f;
            flashingRoundRobin = processor.getLastTriggeredRoundRobin();
        }
    }

    if (flashAlpha > 0.0f)
        flashAlpha = juce::jmax (0.0f, flashAlpha - 0.06f);

    const auto accent = layerAccentColours[layerIndex];
    const auto& layer = processor.getLayers()[(size_t) layerIndex];

    for (int i = 0; i < MentalsTriggerAudioProcessor::numRoundRobins; ++i)
    {
        const bool loaded = layer.roundRobins[(size_t) i].hasAudio();
        auto colour = loaded ? accent.withAlpha (0.55f) : MentalsUI::Colours::slateGrayDark;
        if (i == flashingRoundRobin && flashAlpha > 0.0f)
            colour = colour.interpolatedWith (juce::Colours::white, flashAlpha);

        roundRobinButtons[(size_t) i].setColour (juce::TextButton::buttonColourId, colour);
        roundRobinButtons[(size_t) i].setTooltip (loaded ? layer.roundRobins[(size_t) i].fileName
                                                          : "Empty -- click to load a sample");
    }
}

void LayerCardComponent::chooseFile (int roundRobinIndex)
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load " + juce::String (MentalsTriggerAudioProcessor::layerNames[layerIndex]) + " "
            + juce::String (MentalsTriggerAudioProcessor::roundRobinNames[roundRobinIndex]),
        juce::File(), "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");

    constexpr auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (chooserFlags, [this, roundRobinIndex] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File{})
            return;

        if (! processor.loadSampleForSlot (layerIndex, roundRobinIndex, file))
        {
            juce::NativeMessageBox::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Couldn't Load Sample",
                "\"" + file.getFileName() + "\" couldn't be loaded. Make sure it's a valid "
                    "WAV, AIFF, FLAC, MP3, or OGG file.");
        }
    });
}

//==============================================================================
// SectionHeaderComponent
//==============================================================================
void SectionHeaderComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    const auto font = juce::Font (juce::FontOptions (11.0f).withStyle ("Bold"));
    g.setColour (MentalsUI::Colours::goldenYellow);
    g.setFont (font);
    const float textWidth = juce::GlyphArrangement::getStringWidth (font, text) + 10.0f;
    g.drawText (text, bounds.removeFromLeft (textWidth), juce::Justification::centredLeft);

    g.setColour (MentalsUI::Colours::slateGray);
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());
}

//==============================================================================
// MentalsTriggerAudioProcessorEditor
//==============================================================================
MentalsTriggerAudioProcessorEditor::MentalsTriggerAudioProcessorEditor (MentalsTriggerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), waveformDisplay (p), velocityCurve (p),
      inputMeter ([&p] { return p.getInputLevelDb(); }, [] { return false; }),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&hardwareLookAndFeel);

    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Trigger", juce::dontSendNotification);
    productNameLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    productNameLabel.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    addAndMakeVisible (productNameLabel);

    productSubtitleLabel.setText ("DRUM REPLACER", juce::dontSendNotification);
    productSubtitleLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::slateGray);
    productSubtitleLabel.setFont (juce::Font (juce::FontOptions (10.5f)));
    addAndMakeVisible (productSubtitleLabel);

    presetSelector.setTextWhenNothingSelected ("Default Kit");
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

    addAndMakeVisible (waveformDisplay);

    for (int i = 0; i < MentalsTriggerAudioProcessor::numLayers; ++i)
    {
        layerCards[(size_t) i] = std::make_unique<LayerCardComponent> (i, processor);
        addAndMakeVisible (*layerCards[(size_t) i]);
    }

    addAndMakeVisible (splitter);

    addAndMakeVisible (detectionSectionHeader);
    addAndMakeVisible (splitSectionHeader);
    addAndMakeVisible (outputSectionHeader);

    thresholdSlider.addToParent    ("Threshold",   *this);
    sensitivitySlider.addToParent  ("Sensitivity", *this);
    chokeTimeSlider.addToParent    ("Choke",       *this);
    softMedSplitSlider.addToParent ("Soft/Med",    *this);
    medHardSplitSlider.addToParent ("Med/Hard",    *this);
    outputGainSlider.addToParent   ("Output",      *this);

    addAndMakeVisible (velocityCurve);
    velocityCurveLabel.setText ("Curve", juce::dontSendNotification);
    velocityCurveLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    velocityCurveLabel.setJustificationType (juce::Justification::centred);
    velocityCurveLabel.attachToComponent (&velocityCurve, false);
    addAndMakeVisible (velocityCurveLabel);

    inputMeterLabel.setText ("In", juce::dontSendNotification);
    inputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    inputMeterLabel.setJustificationType (juce::Justification::centred);
    inputMeterLabel.attachToComponent (&inputMeter, false);
    addAndMakeVisible (inputMeterLabel);
    addAndMakeVisible (inputMeter);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "threshold", thresholdSlider.slider);
    sensitivityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "sensitivity", sensitivitySlider.slider);
    chokeTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "chokeTime", chokeTimeSlider.slider);
    softMedSplitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "softMedSplit", softMedSplitSlider.slider);
    medHardSplitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "medHardSplit", medHardSplitSlider.slider);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "outputGain", outputGainSlider.slider);

    setResizable (true, true);
    setResizeLimits (820, 560, 1400, 950);
    setSize (1000, 700);
}

MentalsTriggerAudioProcessorEditor::~MentalsTriggerAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void MentalsTriggerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0c0c0d));

    auto fullBounds = getLocalBounds();
    auto topBarArea = fullBounds.removeFromTop (56);
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

    constexpr int earWidth = 22;
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromLeft (earWidth).reduced (2).toFloat());
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromRight (earWidth).reduced (2).toFloat());
}

void MentalsTriggerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto topBar = area.removeFromTop (56).reduced (12, 6);
    logoImage.setBounds (topBar.removeFromLeft (140));
    topBar.removeFromLeft (12);
    auto titleArea = topBar.removeFromLeft (110);
    productNameLabel.setBounds (titleArea.removeFromTop (20));
    productSubtitleLabel.setBounds (titleArea);
    topBar.removeFromLeft (12);
    presetSaveButton.setBounds (topBar.removeFromRight (70));
    topBar.removeFromRight (8);
    // The kit-name preset field takes all remaining top-bar width, matching
    // the reference's wide central name field.
    presetSelector.setBounds (topBar);

    constexpr int earWidth = 22;
    area.removeFromLeft (earWidth);
    area.removeFromRight (earWidth);

    lastPanelBounds = area;
    area.reduce (16, 16);

    // TRANSIENTS waveform display, then the 3 layer cards below it.
    auto waveformArea = area.removeFromTop ((int) (area.getHeight() * 0.30f));
    waveformDisplay.setBounds (waveformArea);
    area.removeFromTop (10);

    auto cardsArea = area.removeFromTop ((int) (area.getHeight() * 0.34f));
    const int cardWidth = cardsArea.getWidth() / MentalsTriggerAudioProcessor::numLayers;
    for (int i = 0; i < MentalsTriggerAudioProcessor::numLayers; ++i)
        layerCards[(size_t) i]->setBounds (cardsArea.removeFromLeft (cardWidth).reduced (6));

    splitter.setBounds (area.removeFromTop (10));
    area.removeFromTop (6);

    // Remaining space: three columns -- Detection, Velocity Split (+ curve), Output.
    auto detectionArea = area.removeFromLeft (area.getWidth() * 3 / 10);
    area.removeFromLeft (16);
    auto splitArea = area.removeFromLeft (area.getWidth() * 4 / 7);
    area.removeFromLeft (16);
    auto outputArea = area;

    detectionSectionHeader.setBounds (detectionArea.removeFromTop (18));
    detectionArea.removeFromTop (26);
    const int detectionKnobWidth = detectionArea.getWidth() / 3;
    thresholdSlider.slider.setBounds   (detectionArea.removeFromLeft (detectionKnobWidth).reduced (8));
    sensitivitySlider.slider.setBounds (detectionArea.removeFromLeft (detectionKnobWidth).reduced (8));
    chokeTimeSlider.slider.setBounds   (detectionArea.reduced (8));

    splitSectionHeader.setBounds (splitArea.removeFromTop (18));
    splitArea.removeFromTop (26);
    auto curveArea = splitArea.removeFromRight (splitArea.getWidth() * 2 / 5);
    curveArea.removeFromTop (16); // room for the "Curve" label above it
    velocityCurve.setBounds (curveArea.reduced (8));
    const int splitKnobWidth = splitArea.getWidth() / 2;
    softMedSplitSlider.slider.setBounds (splitArea.removeFromLeft (splitKnobWidth).reduced (8));
    medHardSplitSlider.slider.setBounds (splitArea.reduced (8));

    outputSectionHeader.setBounds (outputArea.removeFromTop (18));
    outputArea.removeFromTop (26);
    const int meterWidth = 44;
    outputMeter.setBounds (outputArea.removeFromRight (meterWidth).reduced (4, 0));
    inputMeter.setBounds (outputArea.removeFromRight (meterWidth).reduced (4, 0));
    outputGainSlider.slider.setBounds (outputArea.reduced (8));
}

//==============================================================================
void MentalsTriggerAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsTriggerAudioProcessorEditor::comboBoxChanged (juce::ComboBox* comboBox)
{
    if (comboBox == &presetSelector)
    {
        const auto presetName = presetSelector.getText();
        if (presetName.isNotEmpty())
            processor.presetManager.loadPreset (presetName);
    }
}

void MentalsTriggerAudioProcessorEditor::refreshPresetList()
{
    presetSelector.clear (juce::dontSendNotification);
    presetSelector.addItemList (processor.presetManager.getAvailablePresetNames(), 1);
}

void MentalsTriggerAudioProcessorEditor::promptToSavePreset()
{
    auto* dialogWindow = new juce::AlertWindow ("Save Kit", "Enter a name for this kit:",
                                                 juce::MessageBoxIconType::NoIcon);
    dialogWindow->addTextEditor ("presetName", "", "Kit name:");
    dialogWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialogWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    dialogWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this, dialogWindow] (int result)
    {
        if (result == 1)
        {
            const auto name = dialogWindow->getTextEditorContents ("presetName");
            processor.presetManager.savePreset (name);
            refreshPresetList();
        }
        delete dialogWindow;
    }));
}
