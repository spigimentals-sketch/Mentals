#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Transfer-curve display: input amplitude (-1..1, scaled by Drive) on the X
// axis, output amplitude on the Y axis, with a faint diagonal reference line
// showing what "no saturation" would look like. Calls SaturatorDSP::
// waveshape() directly -- the exact same function processBlock() uses -- so
// this can never show a curve that doesn't match what's actually happening
// to the audio, unlike Delay/Reverb's illustrative parametric visualisations.
//==============================================================================
class TransferCurveComponent : public juce::Component,
                                private juce::Timer
{
public:
    explicit TransferCurveComponent (MentalsSaturatorAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~TransferCurveComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsSaturatorAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransferCurveComponent)
};

//==============================================================================
class MentalsSaturatorAudioProcessorEditor : public juce::AudioProcessorEditor,
                                              private juce::Button::Listener,
                                              private juce::ComboBox::Listener
{
public:
    explicit MentalsSaturatorAudioProcessorEditor (MentalsSaturatorAudioProcessor&);
    ~MentalsSaturatorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsSaturatorAudioProcessor& processor;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, preset select/save.
    //==========================================================================
    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    TransferCurveComponent transferCurve;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    juce::Label typeLabel;
    juce::ComboBox typeSelector;
    MentalsUI::LabelledSlider driveSlider, toneSlider, outputGainSlider, mixSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        driveAttachment, toneAttachment, outputGainAttachment, mixAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::LevelMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsSaturatorAudioProcessorEditor)
};
