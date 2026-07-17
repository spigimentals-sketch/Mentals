#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Goniometer (Side on X, Mid on Y, the classic 45-degree-rotated vectorscope
// orientation) plus a correlation bar -- both read straight from the
// processor's actual output samples (see MentalsStereoShaperAudioProcessor's
// goniometer ring buffer/smoothedCorrelation), unlike e.g. Mentals Vox
// Choir's parameter-driven illustration.
//==============================================================================
class StereoAnalyzerComponent : public juce::Component,
                                 private juce::Timer
{
public:
    explicit StereoAnalyzerComponent (MentalsStereoShaperAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (30);
    }

    ~StereoAnalyzerComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsStereoShaperAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoAnalyzerComponent)
};

//==============================================================================
class MentalsStereoShaperAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                 private juce::Button::Listener,
                                                 private juce::ComboBox::Listener
{
public:
    explicit MentalsStereoShaperAudioProcessorEditor (MentalsStereoShaperAudioProcessor&);
    ~MentalsStereoShaperAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsStereoShaperAudioProcessor& processor;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, Phase Align / AI
    // Placement (both global, not tied to any one band, hence living here
    // rather than in the knob panel), preset select/save.
    //==========================================================================
    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::TextButton phaseAlignButton { "Phase Align" };
    juce::TextButton aiAssistButton   { "AI Placement" };
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> phaseAlignAttachment;

    StereoAnalyzerComponent analyzer;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls: row 1 is the stereo-field/rotation/dynamics macro controls,
    // row 2 is the frequency-dependent shaping controls.
    //==========================================================================
    MentalsUI::LabelledSlider widthSlider, midGainSlider, rotationSlider, autoRotateSlider, dynamicsSlider;
    MentalsUI::LabelledSlider lowFreqSlider, highFreqSlider, lowWidthSlider, midWidthSlider, highWidthSlider;
    MentalsUI::LabelledFader mixSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        widthAttachment, midGainAttachment, rotationAttachment, autoRotateAttachment, dynamicsAttachment,
        lowFreqAttachment, highFreqAttachment, lowWidthAttachment, midWidthAttachment, highWidthAttachment,
        mixAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::LevelMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsStereoShaperAudioProcessorEditor)
};
