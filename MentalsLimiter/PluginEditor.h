#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Transfer-curve display: input level (dB, before Input Gain) on the X axis,
// output level (dB) on the Y axis. Shows the STATIC brick-wall shape only
// (unity slope up to the Ceiling, then flat) -- illustrative of what the
// knobs mean, not a literal picture of the actual algorithm, which also
// applies look-ahead and envelope smoothing (see
// MentalsLimiterAudioProcessor's class comment) that a static curve can't
// show.
//==============================================================================
class LimiterTransferCurveComponent : public juce::Component,
                                       private juce::Timer
{
public:
    explicit LimiterTransferCurveComponent (MentalsLimiterAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~LimiterTransferCurveComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsLimiterAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LimiterTransferCurveComponent)
};

//==============================================================================
class MentalsLimiterAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Button::Listener,
                                            private juce::ComboBox::Listener
{
public:
    explicit MentalsLimiterAudioProcessorEditor (MentalsLimiterAudioProcessor&);
    ~MentalsLimiterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsLimiterAudioProcessor& processor;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, preset select/save.
    //==========================================================================
    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::TextButton truePeakToggle { "True Peak" };
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> truePeakAttachment;

    LimiterTransferCurveComponent transferCurve;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    MentalsUI::LabelledSlider inputGainSlider, ceilingSlider, releaseSlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        inputGainAttachment, ceilingAttachment, releaseAttachment, mixAttachment;

    juce::Label gainReductionMeterLabel, outputMeterLabel;
    MentalsUI::GainReductionMeterComponent gainReductionMeter;
    MentalsUI::LevelMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsLimiterAudioProcessorEditor)
};
