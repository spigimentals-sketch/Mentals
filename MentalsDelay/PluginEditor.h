#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Parametric visualisation of the configured echo pattern: a dry tap at t=0,
// then repeats every delayTime, each scaled by feedback^n, out to a capped
// visible window. This is computed directly from the current parameter
// values (redrawn on a timer), not captured from live audio -- a delay's
// time span is usually hundreds to thousands of milliseconds, far too long
// a window for a live scrolling waveform to usefully show more than one or
// two repeats at once, whereas this shows the whole configured pattern at a
// glance and updates instantly as a knob turns.
//==============================================================================
class EchoPatternComponent : public juce::Component,
                              private juce::Timer
{
public:
    explicit EchoPatternComponent (MentalsDelayAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~EchoPatternComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsDelayAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoPatternComponent)
};

//==============================================================================
class MentalsDelayAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          private juce::Button::Listener,
                                          private juce::ComboBox::Listener
{
public:
    explicit MentalsDelayAudioProcessorEditor (MentalsDelayAudioProcessor&);
    ~MentalsDelayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsDelayAudioProcessor& processor;

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

    EchoPatternComponent echoPattern;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    MentalsUI::LabelledSlider delayTimeSlider, feedbackSlider, lowCutSlider, highCutSlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob
    juce::ToggleButton pingPongToggle { "Ping-Pong" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        delayTimeAttachment, feedbackAttachment, mixAttachment, lowCutAttachment, highCutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pingPongAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::AnalogVUMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsDelayAudioProcessorEditor)
};
