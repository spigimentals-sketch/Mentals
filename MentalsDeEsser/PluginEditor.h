#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Transfer-curve display for the High (sibilance) band: input level (dB) on
// the X axis, output level (dB) on the Y axis, with a faint diagonal
// reference line, a threshold marker, and the Max Reduction floor. Calls
// MentalsUI::DynamicsDSP::computeOutputDb() directly -- the exact same
// function processBlock() uses -- so this can never show a curve that
// doesn't match what's actually happening to the audio.
//==============================================================================
class DeEsserTransferCurveComponent : public juce::Component,
                                private juce::Timer
{
public:
    explicit DeEsserTransferCurveComponent (MentalsDeEsserAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~DeEsserTransferCurveComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsDeEsserAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeEsserTransferCurveComponent)
};

//==============================================================================
class MentalsDeEsserAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Button::Listener,
                                            private juce::ComboBox::Listener
{
public:
    explicit MentalsDeEsserAudioProcessorEditor (MentalsDeEsserAudioProcessor&);
    ~MentalsDeEsserAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsDeEsserAudioProcessor& processor;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, preset select/save.
    //==========================================================================
    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    DeEsserTransferCurveComponent transferCurve;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls -- two rows: frequency/threshold/ratio/attack/release on top,
    // max reduction/mix/listen toggle + meters below.
    //==========================================================================
    MentalsUI::LabelledSlider frequencySlider, thresholdSlider, ratioSlider, attackSlider, releaseSlider;
    MentalsUI::LabelledSlider maxReductionSlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob
    juce::ToggleButton listenToggle { "Listen" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        frequencyAttachment, thresholdAttachment, ratioAttachment, attackAttachment, releaseAttachment,
        maxReductionAttachment, mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttachment;

    juce::Label gainReductionMeterLabel, outputMeterLabel;
    MentalsUI::GainReductionMeterComponent gainReductionMeter;
    MentalsUI::LevelMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsDeEsserAudioProcessorEditor)
};
