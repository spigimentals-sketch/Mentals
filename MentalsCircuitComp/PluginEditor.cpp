#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// CircuitCompCurveComponent
//==============================================================================
void CircuitCompCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (14.0f);

    constexpr float minDb = -60.0f, maxDb = 6.0f;
    auto xForDb = [&] (float db) { return bounds.getX() + (db - minDb) / (maxDb - minDb) * bounds.getWidth(); };
    auto yForDb = [&] (float db) { return bounds.getBottom() - (db - minDb) / (maxDb - minDb) * bounds.getHeight(); };

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    juce::Path unity;
    unity.startNewSubPath (xForDb (minDb), yForDb (minDb));
    unity.lineTo (xForDb (maxDb), yForDb (maxDb));
    g.strokePath (unity, juce::PathStrokeType (1.0f));

    const int mode = processor.modeParam->getIndex();
    const float thresholdDb = processor.thresholdParam->get();
    const float ratio = juce::jmax (1.0f, processor.ratioParam->get());
    const float kneeDb = processor.kneeParam->get();

    juce::Path curvePath;
    bool started = false;
    for (float db = minDb; db <= maxDb; db += 0.5f)
    {
        const float outDb = MentalsCircuitCompAudioProcessor::computeModeAdjustedOutputDb (mode, db, thresholdDb, ratio, kneeDb);
        const float x = xForDb (db), y = yForDb (outDb);
        if (! started) { curvePath.startNewSubPath (x, y); started = true; }
        else            curvePath.lineTo (x, y);
    }

    g.setColour (MentalsUI::Colours::goldenYellow);
    g.strokePath (curvePath, juce::PathStrokeType (2.5f));

    g.setColour (MentalsUI::Colours::electricBlue.withAlpha (0.4f));
    g.drawVerticalLine ((int) xForDb (thresholdDb), bounds.getY(), bounds.getBottom());

    static const juce::StringArray modeNames { "VCA", "FET", "Optical", "Tube" };
    const auto modeName = modeNames[juce::jlimit (0, modeNames.size() - 1, mode)];

    g.setColour (MentalsUI::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
    g.drawText (modeName + " Mode   GR " + juce::String (processor.getGainReductionDb(), 1) + " dB",
                juce::Rectangle<float> (bounds.getX(), bounds.getY(), bounds.getWidth(), 18.0f),
                juce::Justification::left);
}

//==============================================================================
// MultibandSpectrumComponent
//==============================================================================
float MultibandSpectrumComponent::frequencyToX (double freqHz) const
{
    constexpr double minFreq = 20.0, maxFreq = 20000.0;
    const double logMin = std::log10 (minFreq), logMax = std::log10 (maxFreq);
    const double t = (std::log10 (juce::jlimit (minFreq, maxFreq, freqHz)) - logMin) / (logMax - logMin);
    return (float) (t * getWidth());
}

double MultibandSpectrumComponent::xToFrequency (float x) const
{
    constexpr double minFreq = 20.0, maxFreq = 20000.0;
    const double logMin = std::log10 (minFreq), logMax = std::log10 (maxFreq);
    const double t = juce::jlimit (0.0, 1.0, (double) x / (double) juce::jmax (1, getWidth()));
    return std::pow (10.0, logMin + t * (logMax - logMin));
}

int MultibandSpectrumComponent::findNearestCrossover (float x) const
{
    constexpr float grabRadius = 10.0f;
    int nearest = -1;
    float nearestDist = grabRadius;

    for (int i = 0; i < MentalsCircuitCompAudioProcessor::numCrossovers; ++i)
    {
        const float cx = frequencyToX (processor.crossoverFreqParams[(size_t) i]->get());
        const float dist = std::abs (cx - x);
        if (dist < nearestDist)
        {
            nearestDist = dist;
            nearest = i;
        }
    }
    return nearest;
}

int MultibandSpectrumComponent::findBandAt (float x) const
{
    const float freq = (float) xToFrequency (x);
    for (int i = 0; i < MentalsCircuitCompAudioProcessor::numCrossovers; ++i)
        if (freq < processor.crossoverFreqParams[(size_t) i]->get())
            return i;
    return MentalsCircuitCompAudioProcessor::numCrossovers; // top band
}

void MultibandSpectrumComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);
    auto bounds = getLocalBounds().toFloat();

    static const juce::StringArray bandNames { "Sub", "Low", "L-Mid", "Mid", "H-Mid", "High", "Air" };

    float previousX = 0.0f;
    for (int b = 0; b < MentalsCircuitCompAudioProcessor::numBands; ++b)
    {
        const float nextX = (b < MentalsCircuitCompAudioProcessor::numCrossovers)
                                 ? frequencyToX (processor.crossoverFreqParams[(size_t) b]->get())
                                 : bounds.getWidth();
        juce::Rectangle<float> zone (previousX, 0.0f, nextX - previousX, bounds.getHeight());

        const bool muted = processor.bandMuteParams[(size_t) b]->get();
        const bool soloed = processor.bandSoloParams[(size_t) b]->get();
        const bool selected = (b == selectedBand);

        auto zoneColour = selected ? MentalsUI::Colours::electricBlue.withAlpha (0.12f)
                                    : juce::Colours::transparentBlack;
        g.setColour (zoneColour);
        g.fillRect (zone);

        // Live gain-reduction shading: a bar rising from the bottom of the
        // zone, taller/brighter the more that band is currently reducing --
        // real metering, not illustrative (see class comment).
        const float grDb = processor.getBandGainReductionDb (b);
        const float grAmount = juce::jlimit (0.0f, 1.0f, -grDb / 24.0f);
        if (grAmount > 0.0f && ! muted)
        {
            auto grBar = zone.withTop (zone.getBottom() - zone.getHeight() * grAmount * 0.9f).reduced (2.0f, 0.0f);
            g.setColour (MentalsUI::Colours::electricBlue.withAlpha (0.35f));
            g.fillRect (grBar);
        }

        if (muted)
        {
            g.setColour (MentalsUI::Colours::crimsonRed.withAlpha (0.10f));
            g.fillRect (zone);
        }
        if (soloed)
        {
            g.setColour (MentalsUI::Colours::goldenYellow.withAlpha (0.65f));
            g.drawRect (zone.reduced (1.5f), 2.0f);
        }

        g.setColour (muted ? MentalsUI::Colours::slateGray : MentalsUI::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle (selected ? "Bold" : "Regular")));
        g.drawText (bandNames[b], zone.reduced (2.0f), juce::Justification::centredTop);

        previousX = nextX;
    }

    // Crossover dividers.
    for (int i = 0; i < MentalsCircuitCompAudioProcessor::numCrossovers; ++i)
    {
        const float x = frequencyToX (processor.crossoverFreqParams[(size_t) i]->get());
        g.setColour (i == draggingCrossover ? MentalsUI::Colours::goldenYellow : MentalsUI::Colours::slateGray);
        g.drawVerticalLine ((int) x, bounds.getY(), bounds.getBottom());
    }

    g.setColour (MentalsUI::Colours::white.withAlpha (0.8f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("Click a band to edit  |  drag a divider to move it  |  GR " + juce::String (processor.getGainReductionDb(), 1) + " dB",
                bounds.reduced (6.0f), juce::Justification::bottomLeft);
}

void MultibandSpectrumComponent::mouseDown (const juce::MouseEvent& e)
{
    const float x = e.position.x;
    const int nearCrossover = findNearestCrossover (x);
    if (nearCrossover >= 0)
    {
        draggingCrossover = nearCrossover;
        return;
    }

    const int band = findBandAt (x);
    selectedBand = band;
    if (onBandSelected)
        onBandSelected (band);
    repaint();
}

void MultibandSpectrumComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingCrossover < 0)
        return;

    constexpr float minGapHz = 20.0f;
    float newFreq = (float) xToFrequency (e.position.x);

    // Keep crossovers monotonically ordered with a minimum gap either side.
    const float lowerBound = draggingCrossover > 0
        ? processor.crossoverFreqParams[(size_t) (draggingCrossover - 1)]->get() + minGapHz
        : 20.0f;
    const float upperBound = draggingCrossover < MentalsCircuitCompAudioProcessor::numCrossovers - 1
        ? processor.crossoverFreqParams[(size_t) (draggingCrossover + 1)]->get() - minGapHz
        : 20000.0f;

    newFreq = juce::jlimit (lowerBound, juce::jmax (lowerBound, upperBound), newFreq);

    auto* param = processor.crossoverFreqParams[(size_t) draggingCrossover];
    param->setValueNotifyingHost (param->convertTo0to1 (newFreq));
}

void MultibandSpectrumComponent::mouseUp (const juce::MouseEvent&)
{
    draggingCrossover = -1;
}

//==============================================================================
// MentalsCircuitCompAudioProcessorEditor
//==============================================================================
MentalsCircuitCompAudioProcessorEditor::MentalsCircuitCompAudioProcessorEditor (MentalsCircuitCompAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), curve (p), spectrum (p),
      gainReductionMeter ([&p] { return p.getGainReductionDb(); }, true),
      vuMeter ([&p] { return p.getInputPeakDb(); }),
      outputMeter ([&p] { return p.getOutputPeakDb(); })
{
    setLookAndFeel (&hardwareLookAndFeel);

    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Circuit Comp", juce::dontSendNotification);
    productNameLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    productNameLabel.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    addAndMakeVisible (productNameLabel);

    modeLabel.setText ("Mode", juce::dontSendNotification);
    modeLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    modeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (modeLabel);

    modeSelector.addItemList ({ "VCA", "FET", "Optical", "Tube" }, 1);
    modeSelector.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    modeSelector.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    modeSelector.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    modeSelector.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (modeSelector);

    auto setupToggle = [this] (juce::TextButton& button)
    {
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
        button.setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::electricBlue);
        button.setColour (juce::TextButton::textColourOffId,  MentalsUI::Colours::white);
        button.setColour (juce::TextButton::textColourOnId,   MentalsUI::Colours::charcoalBlack);
        addAndMakeVisible (button);
    };
    setupToggle (sidechainToggle);
    setupToggle (multibandToggle);
    setupToggle (linkToggle);
    setupToggle (bandMuteToggle);
    setupToggle (bandSoloToggle);

    stereoToggle.setColour (juce::ToggleButton::textColourId, MentalsUI::Colours::white);
    stereoToggle.setColour (juce::ToggleButton::tickColourId, MentalsUI::Colours::white);
    addAndMakeVisible (stereoToggle);

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

    addAndMakeVisible (curve);
    addAndMakeVisible (spectrum);
    addAndMakeVisible (splitter);

    spectrum.onBandSelected = [this] (int band) { selectBand (band); };

    for (int i = 0; i < MentalsCircuitCompAudioProcessor::numBands; ++i)
    {
        auto& tab = bandTabs[(size_t) i];
        tab.setButtonText (juce::String (i + 1));
        tab.setClickingTogglesState (false);
        tab.setColour (juce::TextButton::buttonColourId, MentalsUI::Colours::slateGrayDark);
        tab.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
        tab.onClick = [this, i] { selectBand (i); };
        addAndMakeVisible (tab);
    }

    thresholdSlider.addToParent ("Threshold", *this);
    ratioSlider.addToParent     ("Ratio",     *this);
    kneeSlider.addToParent      ("Knee",      *this);
    attackSlider.addToParent    ("Attack",    *this);
    releaseSlider.addToParent   ("Release",   *this);

    makeupGainSlider.addToParent ("Makeup",     *this);
    saturationSlider.addToParent ("Saturation", *this);
    blendSlider.addToParent      ("Blend",      *this);

    sidechainHpfFreqSlider.addToParent ("SC Filter", *this);

    gainReductionMeterLabel.setText ("GR", juce::dontSendNotification);
    gainReductionMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    gainReductionMeterLabel.setJustificationType (juce::Justification::centred);
    gainReductionMeterLabel.attachToComponent (&gainReductionMeter, false);
    addAndMakeVisible (gainReductionMeterLabel);
    addAndMakeVisible (gainReductionMeter);

    vuMeterLabel.setText ("In", juce::dontSendNotification);
    vuMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    vuMeterLabel.setJustificationType (juce::Justification::centred);
    vuMeterLabel.attachToComponent (&vuMeter, false);
    addAndMakeVisible (vuMeterLabel);
    addAndMakeVisible (vuMeter);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "mode", modeSelector);
    sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "useSidechain", sidechainToggle);
    multibandAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "multibandEnabled", multibandToggle);
    linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "stereoLink", linkToggle);
    stereoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "stereo", stereoToggle);

    kneeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "knee", kneeSlider.slider);
    saturationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "saturation", saturationSlider.slider);
    blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "blend", blendSlider.slider);
    sidechainHpfFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "sidechainHpfFreq", sidechainHpfFreqSlider.slider);

    rebuildKnobAttachments();
    lastMultibandState = processor.multibandEnabledParam->get();
    updateMultibandVisibility();

    multibandToggle.onClick = [this] { updateMultibandVisibility(); };

    setResizable (true, true);
    setResizeLimits (900, 650, 1500, 1050);
    setSize (1000, 760);

    startTimerHz (10);
}

MentalsCircuitCompAudioProcessorEditor::~MentalsCircuitCompAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsCircuitCompAudioProcessorEditor::selectBand (int bandIndex)
{
    selectedBand = bandIndex;
    spectrum.setSelectedBand (bandIndex);

    for (int i = 0; i < MentalsCircuitCompAudioProcessor::numBands; ++i)
    {
        auto& tab = bandTabs[(size_t) i];
        const bool soloed = processor.bandSoloParams[(size_t) i]->get();
        const bool muted  = processor.bandMuteParams[(size_t) i]->get();
        const auto colour = i == selectedBand ? MentalsUI::Colours::electricBlue
                           : soloed            ? MentalsUI::Colours::goldenYellow.withAlpha (0.5f)
                           : muted             ? MentalsUI::Colours::crimsonRed.withAlpha (0.3f)
                                               : MentalsUI::Colours::slateGrayDark;
        tab.setColour (juce::TextButton::buttonColourId, colour);
    }

    rebuildKnobAttachments();
}

void MentalsCircuitCompAudioProcessorEditor::rebuildKnobAttachments()
{
    const bool multiband = processor.multibandEnabledParam->get();
    const auto suffix = juce::String (selectedBand);

    thresholdAttachment.reset();
    ratioAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();
    makeupGainAttachment.reset();
    bandMuteAttachment.reset();
    bandSoloAttachment.reset();

    const juce::String thresholdId = multiband ? "bandThreshold" + suffix : juce::String ("threshold");
    const juce::String ratioId     = multiband ? "bandRatio" + suffix     : juce::String ("ratio");
    const juce::String attackId    = multiband ? "bandAttack" + suffix    : juce::String ("attack");
    const juce::String releaseId   = multiband ? "bandRelease" + suffix   : juce::String ("release");
    const juce::String makeupId    = multiband ? "bandMakeup" + suffix    : juce::String ("makeupGain");

    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, thresholdId, thresholdSlider.slider);
    ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, ratioId, ratioSlider.slider);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, attackId, attackSlider.slider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, releaseId, releaseSlider.slider);
    makeupGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, makeupId, makeupGainSlider.slider);

    if (multiband)
    {
        bandMuteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.apvts, "bandMute" + suffix, bandMuteToggle);
        bandSoloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.apvts, "bandSolo" + suffix, bandSoloToggle);
    }
}

void MentalsCircuitCompAudioProcessorEditor::updateMultibandVisibility()
{
    const bool multiband = processor.multibandEnabledParam->get();

    curve.setVisible (! multiband);
    spectrum.setVisible (multiband);

    for (auto& tab : bandTabs)
        tab.setVisible (multiband);

    bandMuteToggle.setVisible (multiband);
    bandSoloToggle.setVisible (multiband);
    sidechainHpfFreqSlider.setVisible (! multiband);

    rebuildKnobAttachments();
}

void MentalsCircuitCompAudioProcessorEditor::timerCallback()
{
    const bool multiband = processor.multibandEnabledParam->get();
    if (multiband != lastMultibandState)
    {
        lastMultibandState = multiband;
        updateMultibandVisibility();
        resized();
    }

    if (multiband)
        for (int i = 0; i < MentalsCircuitCompAudioProcessor::numBands; ++i)
        {
            auto& tab = bandTabs[(size_t) i];
            const bool soloed = processor.bandSoloParams[(size_t) i]->get();
            const bool muted  = processor.bandMuteParams[(size_t) i]->get();
            const auto colour = i == selectedBand ? MentalsUI::Colours::electricBlue
                               : soloed            ? MentalsUI::Colours::goldenYellow.withAlpha (0.5f)
                               : muted             ? MentalsUI::Colours::crimsonRed.withAlpha (0.3f)
                                                   : MentalsUI::Colours::slateGrayDark;
            tab.setColour (juce::TextButton::buttonColourId, colour);
        }
}

void MentalsCircuitCompAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsCircuitCompAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsCircuitCompAudioProcessorEditor::refreshPresetList()
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

void MentalsCircuitCompAudioProcessorEditor::promptToSavePreset()
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

void MentalsCircuitCompAudioProcessorEditor::paint (juce::Graphics& g)
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

void MentalsCircuitCompAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (24);
    area.removeFromRight (24);

    constexpr int topBarHeight   = 40;
    constexpr int tabsHeight     = 28;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 460; // three knob rows

    auto topBarArea = area.removeFromTop (topBarHeight);
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        productNameLabel.setBounds (t.removeFromLeft (110));
        t.removeFromLeft (12);
        modeLabel.setBounds (t.removeFromLeft (40));
        t.removeFromLeft (4);
        modeSelector.setBounds (t.removeFromLeft (110));

        presetSaveButton.setBounds (t.removeFromRight (60));
        t.removeFromRight (8);
        presetSelector.setBounds (t.removeFromRight (140));
        t.removeFromRight (12);
        linkToggle.setBounds (t.removeFromRight (60));
        t.removeFromRight (8);
        multibandToggle.setBounds (t.removeFromRight (90));
        t.removeFromRight (8);
        sidechainToggle.setBounds (t.removeFromRight (90));
        t.removeFromRight (8);
        stereoToggle.setBounds (t.removeFromRight (80));
    }

    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto tabsArea     = area.removeFromBottom (tabsHeight);
    auto graphArea    = area;

    lastPanelBounds = panelArea;

    curve.setBounds (graphArea.reduced (8));
    spectrum.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    {
        auto row = tabsArea.reduced (8, 2);
        const int tabWidth = row.getWidth() / MentalsCircuitCompAudioProcessor::numBands;
        for (auto& tab : bandTabs)
            tab.setBounds (row.removeFromLeft (tabWidth).reduced (2, 0));
    }

    auto p = panelArea.reduced (10);

    auto layoutKnobRow = [] (juce::Rectangle<int> row, juce::Array<juce::Component*> knobs)
    {
        row.removeFromTop (20); // headroom for each knob's attachToComponent label above it
        const int cellWidth = row.getWidth() / knobs.size();
        for (auto* knob : knobs)
            knob->setBounds (row.removeFromLeft (cellWidth).reduced (4, 0));
    };

    const int rowHeight = p.getHeight() / 3;
    auto row1 = p.removeFromTop (rowHeight);
    auto row2 = p.removeFromTop (rowHeight);
    auto row3 = p;

    layoutKnobRow (row1, { &thresholdSlider.slider, &ratioSlider.slider, &kneeSlider.slider,
                           &attackSlider.slider, &releaseSlider.slider });
    layoutKnobRow (row2, { &makeupGainSlider.slider, &saturationSlider.slider, &blendSlider.slider,
                           &gainReductionMeter, &vuMeter, &outputMeter });

    row3.removeFromTop (20);
    auto scCell = row3.removeFromLeft (row3.getWidth() / 4).reduced (4, 0);
    sidechainHpfFreqSlider.slider.setBounds (scCell);

    auto toggleArea = row3.reduced (8, 0);
    auto muteCell = toggleArea.removeFromLeft (110);
    bandMuteToggle.setBounds (muteCell.withSizeKeepingCentre (muteCell.getWidth(), 28));
    toggleArea.removeFromLeft (10);
    auto soloCell = toggleArea.removeFromLeft (110);
    bandSoloToggle.setBounds (soloCell.withSizeKeepingCentre (soloCell.getWidth(), 28));
}
