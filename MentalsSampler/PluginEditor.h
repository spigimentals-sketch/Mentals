#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"
#include <array>
#include <functional>
#include <vector>

//==============================================================================
// Keyscape-style sound browser: a left sidebar with two always-visible lists
// (instrument categories on top, the sounds within whichever category is
// being browsed below) instead of the earlier click-to-drill-down tile grid.
// Picking a category only changes what the bottom list shows; picking a
// sound is what actually loads/auditions it -- same split as Spectrasonics
// Keyscape's category list vs. its "MODEL" patch list.
//==============================================================================

//==============================================================================
// One row in the category list (Brass, Woodwinds, ...). Clicking doesn't
// change the loaded sound -- it just tells the sidebar which category's
// sounds the list below should show.
//==============================================================================
class CategoryRowComponent : public juce::Component
{
public:
    CategoryRowComponent (int categoryIndexIn, MentalsSamplerAudioProcessor& proc, std::function<void (int)> onClickIn)
        : categoryIndex (categoryIndexIn), processor (proc), onClick (std::move (onClickIn))
    {
    }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick (categoryIndex); }

    void setSelected (bool shouldBeSelected)
    {
        if (isSelected != shouldBeSelected)
        {
            isSelected = shouldBeSelected;
            repaint();
        }
    }

private:
    int categoryIndex;
    MentalsSamplerAudioProcessor& processor;
    std::function<void (int)> onClick;
    bool isSelected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CategoryRowComponent)
};

//==============================================================================
// The category list itself: a search box that filters rows by substring
// (rows that don't match are hidden and skipped in layout, not just
// greyed out), plus the 12 always-constructed CategoryRowComponents.
// Tracks which category is being "browsed" (independent of which sound is
// actually loaded) and fires onCategoryChosen() whenever that changes --
// either from a row click, or automatically when the loaded sound changes
// externally (host automation, preset recall) to a different category.
//==============================================================================
class CategoryListComponent : public juce::Component,
                               private juce::Timer,
                               private juce::TextEditor::Listener
{
public:
    explicit CategoryListComponent (MentalsSamplerAudioProcessor& proc);
    ~CategoryListComponent() override { stopTimer(); }

    void resized() override;

    std::function<void (int)> onCategoryChosen;

private:
    void timerCallback() override;
    void textEditorTextChanged (juce::TextEditor&) override { applyFilter(); }
    void applyFilter();
    void selectCategory (int categoryIndex);

    MentalsSamplerAudioProcessor& processor;
    juce::TextEditor searchBox;
    std::array<std::unique_ptr<CategoryRowComponent>, MentalsSamplerAudioProcessor::numCategories> rows;
    int browsingCategory = 0;
    int lastKnownSelection = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CategoryListComponent)
};

//==============================================================================
// One row in the sound list (e.g. "Brass 2") -- clicking this is what
// actually loads + auditions that sound.
//==============================================================================
class SoundRowComponent : public juce::Component
{
public:
    SoundRowComponent (int slotIndexIn, MentalsSamplerAudioProcessor& proc)
        : slotIndex (slotIndexIn), processor (proc)
    {
    }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;

    void setSelected (bool shouldBeSelected)
    {
        if (isSelected != shouldBeSelected)
        {
            isSelected = shouldBeSelected;
            repaint();
        }
    }

private:
    int slotIndex;
    MentalsSamplerAudioProcessor& processor;
    bool isSelected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundRowComponent)
};

//==============================================================================
// The sound list for whichever category is currently being browsed --
// rebuilt (soundsPerCategory rows) each time showCategory() is called.
//==============================================================================
class SoundListComponent : public juce::Component,
                            private juce::Timer
{
public:
    explicit SoundListComponent (MentalsSamplerAudioProcessor& proc);
    ~SoundListComponent() override { stopTimer(); }

    void resized() override;
    void showCategory (int categoryIndex);

private:
    void timerCallback() override;

    MentalsSamplerAudioProcessor& processor;
    juce::Label headerLabel;
    std::vector<std::unique_ptr<SoundRowComponent>> rows;
    int activeCategoryIndex = -1;
    int lastKnownSelection = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundListComponent)
};

//==============================================================================
// The big hero panel on the right, standing in for Keyscape's instrument
// photo: a title bar naming the currently loaded sound ("Brass -- Brass 2")
// and a large version of the coloured icon/waveform-glyph look used
// throughout this suite's tiles. Polls the processor's selected slot on a
// timer so it always reflects whatever is actually loaded, however it got
// there (sidebar click, host automation, or preset recall).
//==============================================================================
class HeroDisplayComponent : public juce::Component,
                              private juce::Timer
{
public:
    explicit HeroDisplayComponent (MentalsSamplerAudioProcessor& proc) : processor (proc) { startTimerHz (10); }
    ~HeroDisplayComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override
    {
        const int selected = processor.getSelectedSlot();
        if (selected != lastKnownSelection)
        {
            lastKnownSelection = selected;
            repaint();
        }
    }

    MentalsSamplerAudioProcessor& processor;
    int lastKnownSelection = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeroDisplayComponent)
};

//==============================================================================
// A small underlined section label above a knob cluster (ENVELOPE, OUTPUT),
// matching Keyscape's "MIX / REVERB / PERFORMANCE" group headers: the label
// text in the accent colour, then a thin rule filling the rest of the width.
//==============================================================================
class SectionHeaderComponent : public juce::Component
{
public:
    explicit SectionHeaderComponent (juce::String labelText) : text (std::move (labelText)) {}
    void paint (juce::Graphics& g) override;

private:
    juce::String text;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionHeaderComponent)
};

//==============================================================================
class MentalsSamplerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Button::Listener,
                                            private juce::ComboBox::Listener
{
public:
    explicit MentalsSamplerAudioProcessorEditor (MentalsSamplerAudioProcessor&);
    ~MentalsSamplerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsSamplerAudioProcessor& processor;

    MentalsUI::HardwareLookAndFeel hardwareLookAndFeel;

    juce::Rectangle<int> lastPanelBounds;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name/subtitle, preset
    // select/save.
    //==========================================================================
    MentalsUI::MetallicLogoComponent logoImage;
    juce::Label productNameLabel;
    juce::Label productSubtitleLabel;
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    //==========================================================================
    // Left sidebar: category list over the sound list for whichever category
    // is currently being browsed. Collapsible via sidebarToggleButton -- when
    // hidden, the hero display and controls expand to fill the freed width.
    //==========================================================================
    CategoryListComponent categoryList;
    SoundListComponent soundList;
    juce::TextButton sidebarToggleButton { "<" };
    bool sidebarVisible = true;

    //==========================================================================
    // Right side: the big hero display for the loaded sound, then the
    // grouped control knobs below.
    //==========================================================================
    HeroDisplayComponent heroDisplay;
    MentalsUI::SplitterBar splitter;

    SectionHeaderComponent envelopeSectionHeader { "ENVELOPE" };
    SectionHeaderComponent outputSectionHeader { "OUTPUT" };

    MentalsUI::LabelledSlider attackSlider, releaseSlider, panSlider;
    MentalsUI::LabelledFader volumeSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attackAttachment, releaseAttachment, volumeAttachment, panAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::LevelMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsSamplerAudioProcessorEditor)
};
