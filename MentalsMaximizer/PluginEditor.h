#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Transfer-curve display: input level (dB, before Threshold-driven gain) on
// the X axis, output level (dB) on the Y axis. Shows the STATIC brick-wall
// shape only (unity slope up to the Ceiling, then flat) -- illustrative of
// what Threshold/Ceiling mean, not a literal picture of the actual
// algorithm, which also applies look-ahead, mode-dependent release, and
// saturation that a static curve can't show (see the processor's class
// comment).
//==============================================================================
class MaximizerCurveComponent : public juce::Component,
                                 private juce::Timer
{
public:
    explicit MaximizerCurveComponent (MentalsMaximizerAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~MaximizerCurveComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsMaximizerAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaximizerCurveComponent)
};

//==============================================================================
class MentalsMaximizerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                              private juce::Button::Listener,
                                              private juce::ComboBox::Listener,
                                              private juce::Timer
{
public:
    explicit MentalsMaximizerAudioProcessorEditor (MentalsMaximizerAudioProcessor&);
    ~MentalsMaximizerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void timerCallback() override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsMaximizerAudioProcessor& processor;

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

    MaximizerCurveComponent transferCurve;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Input/output loudness readout -- updated on the same timer that
    // drives the transfer curve's repaint.
    //==========================================================================
    juce::Label inputLufsLabel, outputLufsLabel;

    //==========================================================================
    // Controls.
    //==========================================================================
    MentalsUI::LabelledSlider thresholdSlider, ceilingSlider, characterSlider;
    juce::Label algorithmLabel;
    juce::ComboBox algorithmSelector;
    juce::TextButton stereoUnlinkToggle { "Unlink" };
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        thresholdAttachment, ceilingAttachment, characterAttachment, mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> algorithmAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoUnlinkAttachment;

    juce::Label gainReductionMeterLabel, outputMeterLabel;
    MentalsUI::AnalogVUMeterComponent gainReductionMeter;
    MentalsUI::AnalogVUMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsMaximizerAudioProcessorEditor)
};
