#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    //==========================================================================
    // One simple vector silhouette per instrument category, standing in for a
    // real photo/thumbnail (Keyscape's approach) until real sample artwork
    // exists. Each is built in an arbitrary local coordinate space -- drawn
    // via drawCategoryIcon() below, which fits the path's own bounds into
    // whatever rectangle it's given, so the numbers here don't need to share
    // a common canvas size. Order matches
    // MentalsSamplerAudioProcessor::numCategories's kCategoryDefaults table
    // (Brass, Woodwinds, Strings, Mallets, Percussion, Choir, Keys, Bass,
    // Synth, Guitar, Organ, Bells).
    //==========================================================================
    juce::Path makeCategoryIconPath (int categoryIndex)
    {
        juce::Path p;

        switch (categoryIndex)
        {
            case 0: // Brass -- trumpet: tube + flared bell + 3 valves
            {
                p.addRoundedRectangle (8.0f, 44.0f, 46.0f, 12.0f, 4.0f);
                juce::Path bell;
                bell.startNewSubPath (52.0f, 40.0f);
                bell.lineTo (52.0f, 60.0f);
                bell.lineTo (90.0f, 82.0f);
                bell.lineTo (90.0f, 18.0f);
                bell.closeSubPath();
                p.addPath (bell);
                for (int i = 0; i < 3; ++i)
                    p.addRoundedRectangle (16.0f + (float) i * 11.0f, 28.0f, 7.0f, 18.0f, 2.0f);
                break;
            }

            case 1: // Woodwinds -- clarinet: tapered tube + mouthpiece + finger holes
            {
                p.setUsingNonZeroWinding (false);
                p.addRoundedRectangle (42.0f, 6.0f, 16.0f, 88.0f, 5.0f);
                p.addRoundedRectangle (44.0f, 0.0f, 12.0f, 10.0f, 2.0f);
                for (int i = 0; i < 4; ++i)
                    p.addEllipse (46.0f, 26.0f + (float) i * 15.0f, 8.0f, 8.0f);
                break;
            }

            case 2: // Strings -- violin: figure-8 body + neck + scroll + f-holes
            {
                p.setUsingNonZeroWinding (false);
                p.addEllipse (25.0f, 15.0f, 50.0f, 40.0f);
                p.addEllipse (20.0f, 50.0f, 60.0f, 42.0f);
                p.addRoundedRectangle (46.0f, -20.0f, 8.0f, 40.0f, 3.0f);
                p.addEllipse (42.0f, -28.0f, 16.0f, 14.0f);
                p.addEllipse (34.0f, 40.0f, 5.0f, 16.0f);
                p.addEllipse (61.0f, 40.0f, 5.0f, 16.0f);
                break;
            }

            case 3: // Mallets -- two crossed mallets
            {
                juce::PathStrokeType stroke (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

                juce::Path stick1, stroked1;
                stick1.startNewSubPath (15.0f, 90.0f);
                stick1.lineTo (75.0f, 15.0f);
                stroke.createStrokedPath (stroked1, stick1);
                p.addPath (stroked1);

                juce::Path stick2, stroked2;
                stick2.startNewSubPath (85.0f, 90.0f);
                stick2.lineTo (25.0f, 15.0f);
                stroke.createStrokedPath (stroked2, stick2);
                p.addPath (stroked2);

                p.addEllipse (66.0f, 6.0f, 20.0f, 20.0f);
                p.addEllipse (14.0f, 6.0f, 20.0f, 20.0f);
                break;
            }

            case 4: // Percussion -- drum with crossed sticks
            {
                p.addRoundedRectangle (15.0f, 35.0f, 70.0f, 50.0f, 6.0f);
                p.addEllipse (15.0f, 20.0f, 70.0f, 26.0f);

                juce::PathStrokeType stroke (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

                juce::Path stick1, s1;
                stick1.startNewSubPath (20.0f, 15.0f);
                stick1.lineTo (55.0f, -20.0f);
                stroke.createStrokedPath (s1, stick1);
                p.addPath (s1);
                p.addEllipse (50.0f, -26.0f, 12.0f, 12.0f);

                juce::Path stick2, s2;
                stick2.startNewSubPath (80.0f, 15.0f);
                stick2.lineTo (45.0f, -20.0f);
                stroke.createStrokedPath (s2, stick2);
                p.addPath (s2);
                p.addEllipse (38.0f, -26.0f, 12.0f, 12.0f);
                break;
            }

            case 5: // Choir -- open mouth + radiating sound waves
            {
                p.addEllipse (10.0f, 30.0f, 40.0f, 40.0f);

                juce::PathStrokeType stroke (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
                for (int i = 0; i < 3; ++i)
                {
                    juce::Path arc, stroked;
                    const float radius = 20.0f + (float) i * 14.0f;
                    arc.addCentredArc (30.0f, 50.0f, radius, radius, 0.0f,
                                        juce::MathConstants<float>::pi * -0.35f, juce::MathConstants<float>::pi * 0.35f, true);
                    stroke.createStrokedPath (stroked, arc);
                    p.addPath (stroked);
                }
                break;
            }

            case 6: // Keys -- piano keys (black-key notches cut from the top)
            {
                p.setUsingNonZeroWinding (false);
                p.addRoundedRectangle (5.0f, 20.0f, 90.0f, 60.0f, 4.0f);
                for (int i = 0; i < 4; ++i)
                    p.addRectangle (24.0f + (float) i * 18.0f, 20.0f, 10.0f, 36.0f);
                break;
            }

            case 7: // Bass -- bass guitar: body + long neck + headstock
            {
                p.addEllipse (10.0f, 45.0f, 55.0f, 48.0f);
                p.addRoundedRectangle (45.0f, -35.0f, 10.0f, 82.0f, 3.0f);
                p.addRoundedRectangle (38.0f, -46.0f, 24.0f, 16.0f, 3.0f);
                break;
            }

            case 8: // Synth -- oscillator/EQ bars
            {
                constexpr int numBars = 7;
                static const float heights[numBars] = { 0.3f, 0.55f, 0.8f, 1.0f, 0.8f, 0.55f, 0.3f };
                const float spacing = 100.0f / (float) (numBars + 1);
                for (int i = 0; i < numBars; ++i)
                {
                    const float x = spacing * (float) (i + 1);
                    const float h = 90.0f * heights[i];
                    const float y = 50.0f - h * 0.5f;
                    p.addRoundedRectangle (x - 4.0f, y, 8.0f, h, 4.0f);
                }
                break;
            }

            case 9: // Guitar -- acoustic guitar: body + neck + headstock + soundhole
            {
                p.setUsingNonZeroWinding (false);
                p.addEllipse (15.0f, 45.0f, 40.0f, 36.0f);
                p.addEllipse (30.0f, 20.0f, 46.0f, 40.0f);
                p.addRoundedRectangle (58.0f, -35.0f, 9.0f, 60.0f, 3.0f);
                p.addRoundedRectangle (52.0f, -46.0f, 21.0f, 14.0f, 3.0f);
                p.addEllipse (44.0f, 32.0f, 16.0f, 16.0f);
                break;
            }

            case 10: // Organ -- a row of pipes
            {
                static const float pipeHeights[5] = { 55.0f, 75.0f, 95.0f, 75.0f, 55.0f };
                constexpr float pipeWidth = 14.0f;
                constexpr float spacing = 18.0f;
                constexpr float startX = 6.0f;
                for (int i = 0; i < 5; ++i)
                {
                    const float h = pipeHeights[(size_t) i];
                    p.addRoundedRectangle (startX + (float) i * spacing, 95.0f - h, pipeWidth, h, 3.0f);
                }
                break;
            }

            case 11: // Bells
            default:
            {
                p.setUsingNonZeroWinding (false);
                juce::Path dome;
                dome.startNewSubPath (20.0f, 70.0f);
                dome.quadraticTo (20.0f, 10.0f, 50.0f, 10.0f);
                dome.quadraticTo (80.0f, 10.0f, 80.0f, 70.0f);
                dome.lineTo (86.0f, 78.0f);
                dome.lineTo (14.0f, 78.0f);
                dome.closeSubPath();
                p.addPath (dome);
                p.addEllipse (44.0f, 0.0f, 12.0f, 10.0f);
                p.addEllipse (44.0f, 82.0f, 12.0f, 12.0f);
                break;
            }
        }

        return p;
    }

    // Fits a category's icon into `area`, preserving aspect ratio and
    // centring it, regardless of the icon path's own local coordinate range.
    void drawCategoryIcon (juce::Graphics& g, juce::Rectangle<float> area, int categoryIndex, juce::Colour colour)
    {
        auto path = makeCategoryIconPath (categoryIndex);
        const auto pathBounds = path.getBounds();
        if (pathBounds.isEmpty())
            return;

        const float scale = juce::jmin (area.getWidth() / pathBounds.getWidth(), area.getHeight() / pathBounds.getHeight());
        path.applyTransform (juce::AffineTransform::translation (-pathBounds.getX(), -pathBounds.getY())
                                  .scaled (scale)
                                  .translated (area.getCentreX() - pathBounds.getWidth() * scale * 0.5f,
                                               area.getCentreY() - pathBounds.getHeight() * scale * 0.5f));

        g.setColour (colour);
        g.fillPath (path);
    }
}

//==============================================================================
// CategoryRowComponent
//==============================================================================
void CategoryRowComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto& category = processor.getCategories()[(size_t) categoryIndex];

    if (isSelected)
    {
        g.setColour (MentalsUI::Colours::goldenYellow.withAlpha (0.16f));
        g.fillRect (bounds);
        g.setColour (MentalsUI::Colours::goldenYellow);
        g.fillRect (bounds.removeFromLeft (3.0f));
    }
    else
    {
        bounds.removeFromLeft (3.0f);
    }

    auto dotArea = bounds.removeFromLeft (26.0f).reduced (6.0f);
    g.setColour (category.colour);
    g.fillEllipse (dotArea);
    drawCategoryIcon (g, dotArea.reduced (dotArea.getWidth() * 0.22f), categoryIndex, juce::Colours::white);

    g.setColour (isSelected ? MentalsUI::Colours::goldenYellow : MentalsUI::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle (isSelected ? "Bold" : "Regular")));
    g.drawText (category.name, bounds.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
}

//==============================================================================
// CategoryListComponent
//==============================================================================
CategoryListComponent::CategoryListComponent (MentalsSamplerAudioProcessor& proc)
    : processor (proc)
{
    searchBox.setTextToShowWhenEmpty ("Search", MentalsUI::Colours::slateGray);
    searchBox.setColour (juce::TextEditor::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    searchBox.setColour (juce::TextEditor::textColourId,       MentalsUI::Colours::white);
    searchBox.setColour (juce::TextEditor::outlineColourId,    MentalsUI::Colours::slateGray);
    searchBox.setColour (juce::TextEditor::focusedOutlineColourId, MentalsUI::Colours::electricBlue);
    searchBox.addListener (this);
    addAndMakeVisible (searchBox);

    for (int i = 0; i < MentalsSamplerAudioProcessor::numCategories; ++i)
    {
        rows[(size_t) i] = std::make_unique<CategoryRowComponent> (
            i, processor, [this] (int categoryIndex) { selectCategory (categoryIndex); });
        addAndMakeVisible (*rows[(size_t) i]);
    }

    browsingCategory = MentalsSamplerAudioProcessor::categoryIndexForSlot (processor.getSelectedSlot());
    rows[(size_t) browsingCategory]->setSelected (true);

    startTimerHz (10);
}

void CategoryListComponent::resized()
{
    auto area = getLocalBounds();
    searchBox.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);

    constexpr int rowHeight = 25;
    int y = area.getY();
    for (auto& row : rows)
    {
        if (! row->isVisible())
            continue;
        row->setBounds (area.getX(), y, area.getWidth(), rowHeight);
        y += rowHeight;
    }
}

void CategoryListComponent::applyFilter()
{
    const auto filter = searchBox.getText();
    for (int i = 0; i < MentalsSamplerAudioProcessor::numCategories; ++i)
        rows[(size_t) i]->setVisible (filter.isEmpty()
            || processor.getCategories()[(size_t) i].name.containsIgnoreCase (filter));
    resized();
}

void CategoryListComponent::selectCategory (int categoryIndex)
{
    browsingCategory = categoryIndex;
    for (int i = 0; i < MentalsSamplerAudioProcessor::numCategories; ++i)
        rows[(size_t) i]->setSelected (i == categoryIndex);

    if (onCategoryChosen)
        onCategoryChosen (categoryIndex);
}

void CategoryListComponent::timerCallback()
{
    const int selected = processor.getSelectedSlot();
    if (selected == lastKnownSelection)
        return;
    lastKnownSelection = selected;

    const int selectedCategory = MentalsSamplerAudioProcessor::categoryIndexForSlot (selected);
    if (selectedCategory != browsingCategory)
        selectCategory (selectedCategory);
}

//==============================================================================
// SoundRowComponent
//==============================================================================
void SoundRowComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto& slot = processor.getSlots()[(size_t) slotIndex];

    if (isSelected)
    {
        g.setColour (MentalsUI::Colours::goldenYellow.withAlpha (0.16f));
        g.fillRect (bounds);
        g.setColour (MentalsUI::Colours::goldenYellow);
        g.fillRect (bounds.removeFromLeft (3.0f));
    }
    else
    {
        bounds.removeFromLeft (3.0f);
    }

    bounds.removeFromLeft (26.0f); // indent under its category's dot above

    g.setColour (isSelected ? MentalsUI::Colours::goldenYellow : MentalsUI::Colours::white.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (slot.name, bounds.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
}

void SoundRowComponent::mouseDown (const juce::MouseEvent&)
{
    processor.setSelectedSlot (slotIndex);
    processor.triggerPreview (slotIndex);
}

//==============================================================================
// SoundListComponent
//==============================================================================
SoundListComponent::SoundListComponent (MentalsSamplerAudioProcessor& proc)
    : processor (proc)
{
    headerLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::slateGray);
    headerLabel.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    addAndMakeVisible (headerLabel);

    showCategory (MentalsSamplerAudioProcessor::categoryIndexForSlot (processor.getSelectedSlot()));

    startTimerHz (10);
}

void SoundListComponent::showCategory (int categoryIndex)
{
    activeCategoryIndex = categoryIndex;

    rows.clear();
    for (int i = 0; i < MentalsSamplerAudioProcessor::soundsPerCategory; ++i)
    {
        const int slotIndex = categoryIndex * MentalsSamplerAudioProcessor::soundsPerCategory + i;
        auto row = std::make_unique<SoundRowComponent> (slotIndex, processor);
        addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    headerLabel.setText (processor.getCategories()[(size_t) categoryIndex].name.toUpperCase() + " SOUNDS",
                          juce::dontSendNotification);

    resized();
    lastKnownSelection = -1; // force the highlight refresh below to (re)apply
    timerCallback();
}

void SoundListComponent::resized()
{
    auto area = getLocalBounds();
    headerLabel.setBounds (area.removeFromTop (20));

    constexpr int rowHeight = 25;
    int y = area.getY();
    for (auto& row : rows)
    {
        row->setBounds (area.getX(), y, area.getWidth(), rowHeight);
        y += rowHeight;
    }
}

void SoundListComponent::timerCallback()
{
    const int selected = processor.getSelectedSlot();
    if (selected == lastKnownSelection)
        return;
    lastKnownSelection = selected;

    for (int i = 0; i < (int) rows.size(); ++i)
        rows[(size_t) i]->setSelected (
            activeCategoryIndex * MentalsSamplerAudioProcessor::soundsPerCategory + i == selected);
}

//==============================================================================
// HeroDisplayComponent
//==============================================================================
void HeroDisplayComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const int selected = processor.getSelectedSlot();
    const auto& slot = processor.getSlots()[(size_t) selected];
    const auto& category = processor.getCategories()[(size_t) MentalsSamplerAudioProcessor::categoryIndexForSlot (selected)];

    g.setColour (MentalsUI::Colours::slateGrayDark);
    g.fillRoundedRectangle (bounds, 6.0f);

    // Title bar naming the loaded sound, Keyscape-style.
    auto titleArea = bounds.reduced (bounds.getWidth() * 0.15f, 16.0f).removeFromTop (32.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (titleArea, 4.0f);
    g.setColour (MentalsUI::Colours::goldenYellow);
    g.drawRoundedRectangle (titleArea, 4.0f, 1.0f);
    g.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Bold")));
    g.drawText (category.name + "  -  " + slot.name, titleArea, juce::Justification::centred);

    // Big instrument icon standing in for a real thumbnail/photo once
    // samples land -- a coloured disc behind the category's own silhouette
    // (trumpet for Brass, violin for Strings, ...), matching Keyscape's
    // per-instrument photo but drawn rather than photographed.
    auto iconArea = bounds.reduced (bounds.getWidth() * 0.26f, bounds.getHeight() * 0.20f);
    iconArea.removeFromTop (40.0f); // clear the title bar above

    juce::ColourGradient iconGradient (slot.tileColour.brighter (0.4f), iconArea.getTopLeft(),
                                        slot.tileColour.darker (0.35f), iconArea.getBottomRight(), false);
    g.setGradientFill (iconGradient);
    g.fillEllipse (iconArea);

    const int categoryIndex = MentalsSamplerAudioProcessor::categoryIndexForSlot (selected);
    drawCategoryIcon (g, iconArea.reduced (iconArea.getWidth() * 0.22f, iconArea.getHeight() * 0.18f),
                       categoryIndex, juce::Colours::white.withAlpha (0.9f));
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
// MentalsSamplerAudioProcessorEditor
//==============================================================================
MentalsSamplerAudioProcessorEditor::MentalsSamplerAudioProcessorEditor (MentalsSamplerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), categoryList (p), soundList (p), heroDisplay (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&hardwareLookAndFeel);

    addAndMakeVisible (logoImage);

    productNameLabel.setText ("Sampler", juce::dontSendNotification);
    productNameLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    productNameLabel.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    addAndMakeVisible (productNameLabel);

    productSubtitleLabel.setText ("INSTRUMENT LIBRARY", juce::dontSendNotification);
    productSubtitleLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::slateGray);
    productSubtitleLabel.setFont (juce::Font (juce::FontOptions (10.5f)));
    addAndMakeVisible (productSubtitleLabel);

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

    addAndMakeVisible (categoryList);
    addAndMakeVisible (soundList);
    categoryList.onCategoryChosen = [this] (int categoryIndex) { soundList.showCategory (categoryIndex); };
    categoryList.setVisible (sidebarVisible);
    soundList.setVisible (sidebarVisible);

    sidebarToggleButton.setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::slateGrayDark);
    sidebarToggleButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
    sidebarToggleButton.setButtonText (sidebarVisible ? "<" : ">");
    sidebarToggleButton.setTooltip (sidebarVisible ? "Hide sidebar" : "Show sidebar");
    addAndMakeVisible (sidebarToggleButton);
    sidebarToggleButton.addListener (this);

    addAndMakeVisible (heroDisplay);
    addAndMakeVisible (splitter);

    addAndMakeVisible (envelopeSectionHeader);
    addAndMakeVisible (outputSectionHeader);

    attackSlider.addToParent  ("Attack",  *this);
    releaseSlider.addToParent ("Release", *this);
    panSlider.addToParent     ("Pan",     *this);
    volumeSlider.addToParent  ("Volume",  *this);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "attack", attackSlider.slider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "release", releaseSlider.slider);
    panAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "pan", panSlider.slider);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "volume", volumeSlider.slider);

    setResizable (true, true);
    setResizeLimits (760, 500, 1400, 950);
    setSize (980, 640);
}

MentalsSamplerAudioProcessorEditor::~MentalsSamplerAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void MentalsSamplerAudioProcessorEditor::paint (juce::Graphics& g)
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

void MentalsSamplerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto topBar = area.removeFromTop (56).reduced (12, 6);
    logoImage.setBounds (topBar.removeFromLeft (140));
    topBar.removeFromLeft (12);
    auto titleArea = topBar.removeFromLeft (160);
    productNameLabel.setBounds (titleArea.removeFromTop (20));
    productSubtitleLabel.setBounds (titleArea);
    presetSaveButton.setBounds (topBar.removeFromRight (70));
    topBar.removeFromRight (8);
    presetSelector.setBounds (topBar.removeFromRight (140));

    constexpr int earWidth = 22;
    area.removeFromLeft (earWidth);
    area.removeFromRight (earWidth);

    lastPanelBounds = area;
    area.reduce (16, 16);

    // Left sidebar: category list over the sound list, Keyscape-style.
    // Collapsible via sidebarToggleButton, which stays put at the sidebar's
    // right edge (or the far left, once collapsed) so it's always reachable.
    constexpr int sidebarWidth = 220;
    constexpr int toggleWidth = 26;

    if (sidebarVisible)
    {
        auto sidebarArea = area.removeFromLeft (sidebarWidth);
        sidebarToggleButton.setBounds (area.removeFromLeft (toggleWidth).withSizeKeepingCentre (toggleWidth, 50));
        area.removeFromLeft (10);

        categoryList.setBounds (sidebarArea.removeFromTop ((int) (sidebarArea.getHeight() * 0.55f)));
        sidebarArea.removeFromTop (10);
        soundList.setBounds (sidebarArea);
    }
    else
    {
        sidebarToggleButton.setBounds (area.removeFromLeft (toggleWidth).withSizeKeepingCentre (toggleWidth, 50));
        area.removeFromLeft (10);
    }

    // Right side: hero display over the grouped control knobs.
    auto heroArea = area.removeFromTop ((int) (area.getHeight() * 0.55f));
    heroDisplay.setBounds (heroArea);

    splitter.setBounds (area.removeFromTop (10));
    area.removeFromTop (6);

    auto envelopeArea = area.removeFromLeft (area.getWidth() * 2 / 5);
    area.removeFromLeft (16);
    auto outputArea = area;

    envelopeSectionHeader.setBounds (envelopeArea.removeFromTop (18));
    envelopeArea.removeFromTop (26); // room for LabelledSlider labels above each knob

    const int envelopeKnobWidth = envelopeArea.getWidth() / 2;
    attackSlider.slider.setBounds (envelopeArea.removeFromLeft (envelopeKnobWidth).reduced (10));
    releaseSlider.slider.setBounds (envelopeArea.reduced (10));

    outputSectionHeader.setBounds (outputArea.removeFromTop (18));
    outputArea.removeFromTop (26);

    const int meterWidth = 60;
    auto meterArea = outputArea.removeFromRight (meterWidth);
    outputMeter.setBounds (meterArea.reduced (4, 0));

    const int outputKnobWidth = outputArea.getWidth() / 2;
    panSlider.slider.setBounds (outputArea.removeFromLeft (outputKnobWidth).reduced (10));
    volumeSlider.slider.setBounds (outputArea.reduced (10));
}

//==============================================================================
void MentalsSamplerAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
    {
        promptToSavePreset();
    }
    else if (button == &sidebarToggleButton)
    {
        sidebarVisible = ! sidebarVisible;
        categoryList.setVisible (sidebarVisible);
        soundList.setVisible (sidebarVisible);
        sidebarToggleButton.setButtonText (sidebarVisible ? "<" : ">");
        sidebarToggleButton.setTooltip (sidebarVisible ? "Hide sidebar" : "Show sidebar");
        resized();
    }
}

void MentalsSamplerAudioProcessorEditor::comboBoxChanged (juce::ComboBox* comboBox)
{
    if (comboBox == &presetSelector)
    {
        const auto presetName = presetSelector.getText();
        if (presetName.isNotEmpty())
            processor.presetManager.loadPreset (presetName);
    }
}

void MentalsSamplerAudioProcessorEditor::refreshPresetList()
{
    presetSelector.clear (juce::dontSendNotification);
    presetSelector.addItemList (processor.presetManager.getAvailablePresetNames(), 1);
}

void MentalsSamplerAudioProcessorEditor::promptToSavePreset()
{
    auto* dialogWindow = new juce::AlertWindow ("Save Preset", "Enter a name for this preset:",
                                                 juce::MessageBoxIconType::NoIcon);
    dialogWindow->addTextEditor ("presetName", "", "Preset name:");
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
