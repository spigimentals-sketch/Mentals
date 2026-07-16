#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Transfer-curve display: input level (dB) on the X axis, output level (dB)
// on the Y axis, with a faint diagonal reference line (unity/no compression)
// and a threshold marker. Calls MentalsUI::DynamicsDSP::computeOutputDb()
// directly -- the exact same function processBlock() uses -- so this can
// never show a curve that doesn't match what's actually happening to the
// audio.
//==============================================================================
class CompressorTransferCurveComponent : public juce::Component,
                                private juce::Timer
{
public:
    explicit CompressorTransferCurveComponent (MentalsCompressorAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~CompressorTransferCurveComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsCompressorAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorTransferCurveComponent)
};

//==============================================================================
class MentalsCompressorAudioProcessorEditor : public juce::AudioProcessorEditor,
                                               private juce::Button::Listener,
                                               private juce::ComboBox::Listener
{
public:
    explicit MentalsCompressorAudioProcessorEditor (MentalsCompressorAudioProcessor&);
    ~MentalsCompressorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsCompressorAudioProcessor& processor;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, preset select/save.
    //==========================================================================
    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    CompressorTransferCurveComponent transferCurve;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls -- two rows: threshold/ratio/knee/attack/release on top,
    // makeup/mix/sidechain toggle + meters below.
    //==========================================================================
    MentalsUI::LabelledSlider thresholdSlider, ratioSlider, kneeSlider, attackSlider, releaseSlider;
    MentalsUI::LabelledSlider makeupGainSlider, mixSlider;
    juce::ToggleButton sidechainToggle { "Use Sidechain" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        thresholdAttachment, ratioAttachment, kneeAttachment, attackAttachment, releaseAttachment,
        makeupGainAttachment, mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sidechainAttachment;

    juce::Label gainReductionMeterLabel, outputMeterLabel;
    MentalsUI::GainReductionMeterComponent gainReductionMeter;
    MentalsUI::LevelMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsCompressorAudioProcessorEditor)
};
