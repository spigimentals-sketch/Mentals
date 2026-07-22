#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// LFO preview: two cycles of the sine wave currently shaping the delay-time
// modulation, height-scaled by Depth -- computed directly from the current
// parameter values (redrawn on a timer), same "illustrative, not captured
// audio" approach as Mentals Delay's echo-pattern display. A moving dot
// marks the left channel's actual current phase (polled from the
// processor), so it's not purely static.
//==============================================================================
class LfoPreviewComponent : public juce::Component,
                             private juce::Timer
{
public:
    explicit LfoPreviewComponent (MentalsChorusAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (30);
    }

    ~LfoPreviewComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsChorusAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoPreviewComponent)
};

//==============================================================================
class MentalsChorusAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::Button::Listener,
                                           private juce::ComboBox::Listener
{
public:
    explicit MentalsChorusAudioProcessorEditor (MentalsChorusAudioProcessor&);
    ~MentalsChorusAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsChorusAudioProcessor& processor;

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
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    LfoPreviewComponent lfoPreview;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    MentalsUI::LabelledSlider rateSlider, depthSlider, delaySlider, feedbackSlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        rateAttachment, depthAttachment, delayAttachment, feedbackAttachment, mixAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::AnalogVUMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsChorusAudioProcessorEditor)
};
