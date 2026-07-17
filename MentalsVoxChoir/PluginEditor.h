#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Choir-spread visualisation: one dot per active voice, X = its pan
// position (after the Spread knob), Y = its pitch offset (after the Pitch
// knob), dot size hinting at its vibrato depth (after the Vibrato knob).
// Calls ChoirVoiceDSP::computeVoiceCharacter()/computeVoicePan() directly --
// the exact same functions processBlock() uses -- so this can never show an
// arrangement that doesn't match what's actually playing. Illustrative/
// parameter-driven (redrawn on a timer), not captured from live audio, the
// same approach as Mentals Delay's echo-pattern display.
//==============================================================================
class ChoirSpreadComponent : public juce::Component,
                              private juce::Timer
{
public:
    explicit ChoirSpreadComponent (MentalsVoxChoirAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~ChoirSpreadComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsVoxChoirAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoirSpreadComponent)
};

//==============================================================================
class MentalsVoxChoirAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Button::Listener,
                                             private juce::ComboBox::Listener
{
public:
    explicit MentalsVoxChoirAudioProcessorEditor (MentalsVoxChoirAudioProcessor&);
    ~MentalsVoxChoirAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsVoxChoirAudioProcessor& processor;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, preset select/save.
    //==========================================================================
    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    ChoirSpreadComponent choirSpread;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    juce::Label voicesLabel;
    juce::ComboBox voicesSelector;
    MentalsUI::LabelledSlider vibratoSlider, pitchSlider, timingSlider, spreadSlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> voicesAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        vibratoAttachment, pitchAttachment, timingAttachment, spreadAttachment, mixAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::LevelMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsVoxChoirAudioProcessorEditor)
};
