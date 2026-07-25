#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Transfer-curve display: input level (dB) on the X axis, output level (dB)
// on the Y axis, with a faint diagonal reference line (unity, gate fully
// open) and a threshold marker. Calls MentalsUI::DynamicsDSP::
// computeExpanderOutputDb() directly (clamped to Range, exactly like
// processBlock() does) so this can never show a curve that doesn't match
// what's actually happening to the audio.
//==============================================================================
class GateTransferCurveComponent : public juce::Component,
                                    private juce::Timer
{
public:
    explicit GateTransferCurveComponent (MentalsGateAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~GateTransferCurveComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsGateAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateTransferCurveComponent)
};

//==============================================================================
class MentalsGateAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Button::Listener,
                                         private juce::ComboBox::Listener
{
public:
    explicit MentalsGateAudioProcessorEditor (MentalsGateAudioProcessor&);
    ~MentalsGateAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsGateAudioProcessor& processor;

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

    GateTransferCurveComponent transferCurve;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls -- two rows: threshold/ratio/attack/release on top, range/
    // mix/sidechain toggle + meters below.
    //==========================================================================
    MentalsUI::LabelledSlider thresholdSlider, ratioSlider, attackSlider, releaseSlider;
    MentalsUI::LabelledSlider rangeSlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob
    juce::ToggleButton sidechainToggle { "Use Sidechain" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        thresholdAttachment, ratioAttachment, attackAttachment, releaseAttachment,
        rangeAttachment, mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sidechainAttachment;

    juce::Label gainReductionMeterLabel, outputMeterLabel;
    MentalsUI::AnalogVUMeterComponent gainReductionMeter;
    MentalsUI::AnalogVUMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsGateAudioProcessorEditor)
};
