#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Voice-spread visualisation: one dot per active voice, X = its pan position
// (after Width), Y = its detune offset (after Detune), dot glow modulated by
// Humanize. Calls MentalsDoublerAudioProcessor::computeVoiceBaseDetuneCents()/
// computeVoicePan() directly -- the exact same functions processBlock() uses
// -- so this can never show an arrangement that doesn't match what's
// actually playing. Illustrative/parameter-driven (redrawn on a timer), not
// captured from live audio, the same approach as Mentals Delay's echo-
// pattern display.
//==============================================================================
class DoublerVoicesComponent : public juce::Component,
                                private juce::Timer
{
public:
    explicit DoublerVoicesComponent (MentalsDoublerAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~DoublerVoicesComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsDoublerAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DoublerVoicesComponent)
};

//==============================================================================
class MentalsDoublerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Button::Listener,
                                            private juce::ComboBox::Listener
{
public:
    explicit MentalsDoublerAudioProcessorEditor (MentalsDoublerAudioProcessor&);
    ~MentalsDoublerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsDoublerAudioProcessor& processor;

    MentalsUI::HardwareLookAndFeel hardwareLookAndFeel;

    // Set in resized(), read back in paint() so the metal-panel texture,
    // corner screws, and rack ears are drawn over exactly the same area the
    // knobs sit in.
    juce::Rectangle<int> lastPanelBounds;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, preset select/save.
    //==========================================================================
    MentalsUI::MetallicLogoComponent logoImage;
    juce::Label productNameLabel;
    juce::ToggleButton stereoToggle { "Stereo" };
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoAttachment;

    DoublerVoicesComponent voicesGraph;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    juce::Label voicesLabel;
    juce::ComboBox voicesSelector;
    MentalsUI::LabelledSlider detuneSlider, delaySlider, widthSlider, humanizeSlider, lowCutSlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> voicesAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        detuneAttachment, delayAttachment, widthAttachment, humanizeAttachment, lowCutAttachment, mixAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::AnalogVUMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsDoublerAudioProcessorEditor)
};
