#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Illustrative (parameter-driven, redrawn on a timer, not a live audio
// capture -- same approach as Mentals Reverb's decay-envelope graph) curve:
// a smooth high-shelf silhouette for Air, plus a marker at the exciter's
// Frequency crossover whose glow intensity tracks Drive, so you can see
// roughly where and how hard the harmonic exciter is working.
//==============================================================================
class ExciterCurveComponent : public juce::Component,
                               private juce::Timer
{
public:
    explicit ExciterCurveComponent (MentalsExciterEQAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~ExciterCurveComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }
    float xForFrequency (double freqHz) const;

    MentalsExciterEQAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExciterCurveComponent)
};

//==============================================================================
class MentalsExciterEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                                              private juce::Button::Listener,
                                              private juce::ComboBox::Listener
{
public:
    explicit MentalsExciterEQAudioProcessorEditor (MentalsExciterEQAudioProcessor&);
    ~MentalsExciterEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsExciterEQAudioProcessor& processor;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, preset select/save.
    //==========================================================================
    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    ExciterCurveComponent curve;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    MentalsUI::LabelledSlider frequencySlider, driveSlider, airGainSlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        frequencyAttachment, driveAttachment, airGainAttachment, mixAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::LevelMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsExciterEQAudioProcessorEditor)
};
