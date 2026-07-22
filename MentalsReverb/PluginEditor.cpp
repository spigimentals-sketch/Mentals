#include "PluginProcessor.h"
#include "PluginEditor.h"

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
      outputMeter ([&p] { return p.getOutputPeakDb(); }),
      duckingMeter ([&p] { return p.getDuckingGainReductionDb(); }, true /* gain-reduction mode -- see MentalsUI::AnalogVUMeterComponent */)
{
    setLookAndFeel (&hardwareLookAndFeel);

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

    roomSizeSlider.addToParent  ("Room Size",  *this);
    dampingSlider.addToParent   ("Damping",    *this);
    widthSlider.addToParent     ("Width",      *this);
    mixSlider.addToParent       ("Mix",        *this);
    preDelaySlider.addToParent  ("Pre-Delay",  *this);
    shimmerSlider.addToParent   ("Shimmer",    *this);
    lowCutSlider.addToParent    ("Low Cut",    *this);
    highCutSlider.addToParent   ("High Cut",   *this);
    earlyReflectionsSlider.addToParent ("Early Refl.", *this);
    modDepthSlider.addToParent  ("Mod Depth",  *this);
    modRateSlider.addToParent   ("Mod Rate",   *this);
    duckingSlider.addToParent   ("Ducking",    *this);
    duckingAttackSlider.addToParent  ("Duck Attack",  *this);
    duckingReleaseSlider.addToParent ("Duck Release", *this);

    // Every knob stays the same neutral chrome (see
    // MentalsUI::HardwareLookAndFeel::drawRotarySlider) -- the reference unit's
    // knobs aren't colour-coded by section, so an earlier per-row colour
    // scheme was dropped in favour of matching that exactly.
    addAndMakeVisible (freezeToggle);

    addAndMakeVisible (tempoSyncToggle);
    tempoSyncToggle.addListener (this);

    preDelayDivisionCombo.addItemList (MentalsReverbAudioProcessor::preDelayDivisionNames, 1);
    preDelayDivisionCombo.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    preDelayDivisionCombo.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    preDelayDivisionCombo.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    preDelayDivisionCombo.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (preDelayDivisionCombo);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    duckingMeterLabel.setText ("Duck GR", juce::dontSendNotification);
    duckingMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    duckingMeterLabel.setJustificationType (juce::Justification::centred);
    duckingMeterLabel.attachToComponent (&duckingMeter, false);
    addAndMakeVisible (duckingMeterLabel);
    addAndMakeVisible (duckingMeter);

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
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "lowCut", lowCutSlider.slider);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "highCut", highCutSlider.slider);
    earlyReflectionsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "earlyReflections", earlyReflectionsSlider.slider);
    modDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "modDepth", modDepthSlider.slider);
    modRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "modRateHz", modRateSlider.slider);
    duckingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ducking", duckingSlider.slider);
    duckingAttackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "duckingAttackMs", duckingAttackSlider.slider);
    duckingReleaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "duckingReleaseMs", duckingReleaseSlider.slider);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "freeze", freezeToggle);
    tempoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "tempoSync", tempoSyncToggle);
    preDelayDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "preDelayDivision", preDelayDivisionCombo);

    updatePreDelayEnablement();

    setResizable (true, true);
    setResizeLimits (760, 560, 1300, 1000);
    setSize (980, 740);
}

MentalsReverbAudioProcessorEditor::~MentalsReverbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
    tempoSyncToggle.removeListener (this);
}

void MentalsReverbAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
    else if (button == &tempoSyncToggle)
        updatePreDelayEnablement();
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

    updatePreDelayEnablement(); // the loaded preset/default may have its own Tempo Sync state
}

void MentalsReverbAudioProcessorEditor::updatePreDelayEnablement()
{
    const bool synced = processor.tempoSyncParam->get();
    preDelaySlider.slider.setEnabled (! synced);
    preDelayDivisionCombo.setEnabled (synced);
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

    // Bolted rack ears running the full height of the unit -- the detail
    // that most reads as "this is a rack-mounted hardware box" rather than
    // just a dark rectangle (see the reference image).
    constexpr float earWidth = 22.0f;
    auto fullBounds = getLocalBounds().toFloat();
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromLeft (earWidth).reduced (2.0f));
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromRight (earWidth).reduced (2.0f));
}

void MentalsReverbAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Keep all content clear of the rack ears paint() draws at the very
    // left/right edges (see there).
    area.removeFromLeft (24);
    area.removeFromRight (24);

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 380;

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

    lastPanelBounds = panelArea;

    decayEnvelope.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);
    const int rowHeight = p.getHeight() / 3;
    auto row1 = p.removeFromTop (rowHeight);
    auto row2 = p.removeFromTop (rowHeight);
    auto row3 = p;

    auto layoutKnobRow = [] (juce::Rectangle<int> row, juce::Array<juce::Component*> knobs)
    {
        row.removeFromTop (20); // headroom for each knob's attachToComponent label above it
        const int cellWidth = row.getWidth() / knobs.size();
        for (auto* knob : knobs)
            knob->setBounds (row.removeFromLeft (cellWidth).reduced (4, 0));
    };

    // Row 1: core reverb shape, plus BOTH meters sitting next to each other
    // (Out and Duck GR are the same analog meter design, so they read as a
    // pair rather than one being oddly separated down in Row 3 with the
    // Ducking knobs).
    constexpr int freezeCellWidth = 90, syncCellWidth = 130, meterCellWidth = 200;
    auto meterCell1 = row1.removeFromRight (meterCellWidth);
    auto meterCell2 = row1.removeFromRight (meterCellWidth);

    layoutKnobRow (row1, { &roomSizeSlider.slider, &dampingSlider.slider, &widthSlider.slider,
                           &preDelaySlider.slider, &mixSlider.slider });

    meterCell1.removeFromTop (20); // line up with the slider row's label headroom
    outputMeter.setBounds (meterCell1.reduced (6, 0));

    meterCell2.removeFromTop (20);
    duckingMeter.setBounds (meterCell2.reduced (6, 0));

    // Row 2: character/space-shaping controls.
    layoutKnobRow (row2, { &shimmerSlider.slider, &lowCutSlider.slider, &highCutSlider.slider,
                           &earlyReflectionsSlider.slider, &modDepthSlider.slider, &modRateSlider.slider });

    // Row 3: Ducking, plus Freeze and the Tempo Sync group -- carried down
    // here from Row 1 now that both meters have moved up to sit together.
    auto freezeCell = row3.removeFromRight (freezeCellWidth);
    auto syncCell   = row3.removeFromRight (syncCellWidth);

    layoutKnobRow (row3, { &duckingSlider.slider, &duckingAttackSlider.slider, &duckingReleaseSlider.slider });

    freezeToggle.setBounds (freezeCell.reduced (8, 0).withHeight (32).withY (freezeCell.getY() + freezeCell.getHeight() / 2 - 16));

    syncCell.removeFromTop (20); // line up with the slider row's label headroom
    tempoSyncToggle.setBounds (syncCell.removeFromTop (32));
    syncCell.removeFromTop (4);
    preDelayDivisionCombo.setBounds (syncCell.removeFromTop (24));
}
