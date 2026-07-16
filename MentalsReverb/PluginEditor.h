#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Parametric visualisation of the configured decay: a flat dry-level segment,
// a pre-delay gap, then an exponential decay envelope whose time constant is
// derived from Room Size/Damping. This is an illustrative mapping, not a
// literal simulation of juce::dsp::Reverb's internal comb-filter maths (which
// isn't simply reducible to one time constant anyway) -- like Delay's echo
// pattern, the point is an instant, knob-driven picture of the shape you're
// dialling in, not a live audio capture.
//==============================================================================
class DecayEnvelopeComponent : public juce::Component,
                                private juce::Timer
{
public:
    explicit DecayEnvelopeComponent (MentalsReverbAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~DecayEnvelopeComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsReverbAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DecayEnvelopeComponent)
};

//==============================================================================
class MentalsReverbAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::Button::Listener,
                                           private juce::ComboBox::Listener
{
public:
    explicit MentalsReverbAudioProcessorEditor (MentalsReverbAudioProcessor&);
    ~MentalsReverbAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsReverbAudioProcessor& processor;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, preset select/save.
    //==========================================================================
    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    DecayEnvelopeComponent decayEnvelope;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    MentalsUI::LabelledSlider roomSizeSlider, dampingSlider, widthSlider, preDelaySlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob
    juce::ToggleButton freezeToggle { "Freeze" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        roomSizeAttachment, dampingAttachment, widthAttachment, mixAttachment, preDelayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::LevelMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsReverbAudioProcessorEditor)
};
