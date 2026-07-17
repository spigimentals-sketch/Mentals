#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// LoudnessHistoryComponent
//==============================================================================
void LoudnessHistoryComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (10.0f, 14.0f);

    constexpr float minLufs = -40.0f, maxLufs = 3.0f;
    auto yForLufs = [&] (float lufs) { return bounds.getBottom() - (juce::jlimit (minLufs, maxLufs, lufs) - minLufs) / (maxLufs - minLufs) * bounds.getHeight(); };

    for (float gridLufs : { 0.0f, -9.0f, -18.0f, -27.0f, -36.0f })
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        const float y = yForLufs (gridLufs);
        g.drawHorizontalLine ((int) y, bounds.getX(), bounds.getRight());
        g.setColour (MentalsUI::Colours::slateGray);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (juce::String ((int) gridLufs), juce::Rectangle<float> (bounds.getRight() - 30.0f, y - 12.0f, 28.0f, 12.0f), juce::Justification::right);
    }

    const float target = processor.getTargetLufs();
    g.setColour (MentalsUI::Colours::goldenYellow.withAlpha (0.6f));
    {
        const float y = yForLufs (target);
        g.drawDashedLine (juce::Line<float> (bounds.getX(), y, bounds.getRight(), y), std::array<float, 2> { 4.0f, 3.0f }.data(), 2, 1.5f);
    }

    std::vector<float> momentary, shortTerm;
    processor.copyRecentLoudnessHistory (momentary, shortTerm, historyPoints);

    auto drawTrace = [&] (const std::vector<float>& values, juce::Colour colour, float thickness)
    {
        juce::Path path;
        bool started = false;
        for (int i = 0; i < (int) values.size(); ++i)
        {
            if (values[(size_t) i] <= -99.0f)
                continue; // no data yet at this point
            const float x = bounds.getX() + (float) i / (float) (historyPoints - 1) * bounds.getWidth();
            const float y = yForLufs (values[(size_t) i]);
            if (! started) { path.startNewSubPath (x, y); started = true; }
            else            path.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness));
    };

    drawTrace (momentary, MentalsUI::Colours::electricBlue.withAlpha (0.35f), 1.0f);
    drawTrace (shortTerm, MentalsUI::Colours::electricBlue, 2.5f);

    g.setColour (MentalsUI::Colours::white.withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("Momentary (thin) / Short-Term (bold)  --  dashed line = Target", bounds, juce::Justification::bottomLeft);
}

//==============================================================================
// NumericReadoutComponent
//==============================================================================
void NumericReadoutComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto bounds = getLocalBounds().toFloat().reduced (4.0f);

    g.setColour (MentalsUI::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (title, bounds.removeFromTop (18.0f), juce::Justification::centred);

    const bool warning = isWarning && isWarning (displayedValue);
    g.setColour (warning ? MentalsUI::Colours::crimsonRed : MentalsUI::Colours::electricBlue);
    g.setFont (juce::Font (juce::FontOptions (26.0f).withStyle ("Bold")));
    g.drawText (juce::String (displayedValue, 1) + suffix, bounds, juce::Justification::centred);

    g.setColour (MentalsUI::Colours::slateGray);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 3.0f, 1.0f);
}

//==============================================================================
// MentalsMasteringMeterAudioProcessorEditor
//==============================================================================
MentalsMasteringMeterAudioProcessorEditor::MentalsMasteringMeterAudioProcessorEditor (MentalsMasteringMeterAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), history (p),
      momentaryReadout ("Momentary", [&p] { return p.getMomentaryLufs(); }, " LUFS"),
      shortTermReadout ("Short-Term", [&p] { return p.getShortTermLufs(); }, " LUFS"),
      integratedReadout ("Integrated", [&p] { return p.getIntegratedLufs(); }, " LUFS"),
      lraReadout ("Loudness Range", [&p] { return p.getLoudnessRangeLu(); }, " LU"),
      truePeakReadout ("True Peak", [&p] { return p.getTruePeakDb(); }, " dBTP", [] (float v) { return v > -1.0f; }),
      samplePeakReadout ("Sample Peak", [&p] { return p.getSamplePeakDb(); }, " dBFS", [] (float v) { return v > -0.1f; })
{
    setLookAndFeel (&MentalsUI::MentalsLookAndFeel::getSharedInstance());

    logoImage.setImage (juce::ImageFileFormat::loadFrom (BinaryData::mentals_logo_png, (size_t) BinaryData::mentals_logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Mastering Meter", juce::dontSendNotification);
    productNameLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    productNameLabel.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    addAndMakeVisible (productNameLabel);

    targetLabel.setText ("Target", juce::dontSendNotification);
    targetLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    targetLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (targetLabel);

    targetSelector.addItemList ({ "Spotify -14", "Apple Music -16", "YouTube -14", "Broadcast -23" }, 1);
    targetSelector.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    targetSelector.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    targetSelector.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    targetSelector.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (targetSelector);

    resetButton.setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::slateGrayDark);
    resetButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
    addAndMakeVisible (resetButton);
    resetButton.addListener (this);

    addAndMakeVisible (history);
    addAndMakeVisible (splitter);

    addAndMakeVisible (momentaryReadout);
    addAndMakeVisible (shortTermReadout);
    addAndMakeVisible (integratedReadout);
    addAndMakeVisible (lraReadout);
    addAndMakeVisible (truePeakReadout);
    addAndMakeVisible (samplePeakReadout);

    targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "targetPreset", targetSelector);

    setResizable (true, true);
    setResizeLimits (760, 500, 1300, 850);
    setSize (900, 620);
}

MentalsMasteringMeterAudioProcessorEditor::~MentalsMasteringMeterAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    resetButton.removeListener (this);
}

void MentalsMasteringMeterAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &resetButton)
        processor.resetMeters();
}

void MentalsMasteringMeterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::charcoalBlack);

    auto topBarArea = getLocalBounds().removeFromTop (40);
    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRect (topBarArea);
}

void MentalsMasteringMeterAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 130;

    auto topBarArea = area.removeFromTop (topBarHeight);
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        productNameLabel.setBounds (t.removeFromLeft (140));

        resetButton.setBounds (t.removeFromRight (70));
        t.removeFromRight (12);
        targetSelector.setBounds (t.removeFromRight (150));
        t.removeFromRight (4);
        targetLabel.setBounds (t.removeFromRight (50));
    }

    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    history.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);
    juce::Array<juce::Component*> readouts { &momentaryReadout, &shortTermReadout, &integratedReadout,
                                              &lraReadout, &truePeakReadout, &samplePeakReadout };
    const int cellWidth = p.getWidth() / readouts.size();
    for (auto* readout : readouts)
        readout->setBounds (p.removeFromLeft (cellWidth).reduced (4, 0));
}
