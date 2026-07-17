#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// ExciterCurveComponent
//==============================================================================
float ExciterCurveComponent::xForFrequency (double freqHz) const
{
    constexpr double minFreq = 20.0, maxFreq = 20000.0;
    const double logMin = std::log10 (minFreq), logMax = std::log10 (maxFreq);
    const double t = (std::log10 (juce::jlimit (minFreq, maxFreq, freqHz)) - logMin) / (logMax - logMin);
    return (float) (t * getWidth());
}

void ExciterCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (4.0f, 14.0f);
    const float zeroDbY = bounds.getY() + bounds.getHeight() * 0.65f;
    const float dbToPixels = bounds.getHeight() * 0.35f / 12.0f; // +/-12dB fits comfortably

    const float airGainDb = processor.airGainParam->get();
    const float exciterFreq = processor.frequencyParam->get();
    const float driveAmount = processor.driveParam->get() * 0.01f;

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine ((int) zeroDbY, bounds.getX(), bounds.getRight());

    // High-shelf silhouette: a smooth logistic transition centred on the
    // fixed Air frequency, approaching 0dB well below it and airGainDb well
    // above -- illustrative, not literally the biquad's own response curve.
    juce::Path shelfPath;
    bool started = false;
    for (float x = 0.0f; x <= bounds.getWidth(); x += 2.0f)
    {
        const float freq = (float) std::pow (10.0, std::log10 (20.0) + (x / bounds.getWidth()) * (std::log10 (20000.0) - std::log10 (20.0)));
        const float t = std::log2 (freq / MentalsExciterEQAudioProcessor::airShelfFreq);
        const float responseDb = airGainDb / (1.0f + std::exp (-2.5f * t));

        const float px = bounds.getX() + x;
        const float py = zeroDbY - responseDb * dbToPixels;
        if (! started) { shelfPath.startNewSubPath (px, py); started = true; }
        else            shelfPath.lineTo (px, py);
    }

    g.setColour (MentalsUI::Colours::goldenYellow);
    g.strokePath (shelfPath, juce::PathStrokeType (2.0f));

    // Exciter crossover marker -- brighter/hotter the harder Drive is pushed.
    const float markerX = bounds.getX() + xForFrequency (exciterFreq);
    const auto markerColour = MentalsUI::Colours::electricBlue.interpolatedWith (MentalsUI::Colours::crimsonRed, driveAmount);
    g.setColour (markerColour.withAlpha (0.3f + 0.5f * driveAmount));
    g.drawVerticalLine ((int) markerX, bounds.getY(), bounds.getBottom());
    g.setColour (markerColour);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("Excite", juce::Rectangle<float> (markerX + 4.0f, bounds.getY(), 60.0f, 14.0f), juce::Justification::left);

    g.setColour (MentalsUI::Colours::slateGray);
    for (double f : { 100.0, 1000.0, 10000.0 })
    {
        const float x = bounds.getX() + xForFrequency (f);
        g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 20.0f, bounds.getBottom() - 2.0f, 40.0f, 14.0f), juce::Justification::centred);
    }
}

//==============================================================================
// MentalsExciterEQAudioProcessorEditor
//==============================================================================
MentalsExciterEQAudioProcessorEditor::MentalsExciterEQAudioProcessorEditor (MentalsExciterEQAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), curve (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&MentalsUI::MentalsLookAndFeel::getSharedInstance());

    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Exciter EQ", juce::dontSendNotification);
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

    addAndMakeVisible (curve);
    addAndMakeVisible (splitter);

    frequencySlider.addToParent ("Frequency", *this);
    driveSlider.addToParent     ("Drive",     *this);
    airGainSlider.addToParent   ("Air",       *this);
    mixSlider.addToParent       ("Mix",       *this);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    frequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "frequency", frequencySlider.slider);
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "drive", driveSlider.slider);
    airGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "airGain", airGainSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);

    setResizable (true, true);
    setResizeLimits (560, 420, 1200, 900);
    setSize (760, 560);
}

MentalsExciterEQAudioProcessorEditor::~MentalsExciterEQAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsExciterEQAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
}

void MentalsExciterEQAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsExciterEQAudioProcessorEditor::refreshPresetList()
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

void MentalsExciterEQAudioProcessorEditor::promptToSavePreset()
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

void MentalsExciterEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);

    auto topBarArea = getLocalBounds().removeFromTop (40);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBarArea);
}

void MentalsExciterEQAudioProcessorEditor::resized()
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
        productNameLabel.setBounds (t.removeFromLeft (100));
        t.removeFromLeft (12);
        presetSelector.setBounds (t.removeFromLeft (160));
        t.removeFromLeft (8);
        presetSaveButton.setBounds (t.removeFromLeft (60));
    }

    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    curve.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);
    p.removeFromTop (20); // headroom for each knob's attachToComponent label above it

    juce::Array<juce::Component*> knobs { &frequencySlider.slider, &driveSlider.slider,
                                           &airGainSlider.slider, &mixSlider.slider, &outputMeter };
    const int cellWidth = p.getWidth() / knobs.size();
    for (auto* knob : knobs)
        knob->setBounds (p.removeFromLeft (cellWidth).reduced (4, 0));
}
