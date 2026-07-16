#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"
#include <vector>

//==============================================================================
// Scrolling pitch-history display: detected pitch (gold) and corrected/
// target pitch (blue) over the last few seconds, in semitones relative to
// A4=440Hz, with faint horizontal guide lines at the in-scale semitones for
// the current Key/Scale. Unlike Delay/Reverb/Saturator/Compressor/De-esser's
// graphs (all pure functions of their parameters), this one is inherently
// live audio-derived data, since pitch is a time-varying property of the
// incoming signal, not something a knob alone determines.
//==============================================================================
class PitchHistoryComponent : public juce::Component,
                               private juce::Timer
{
public:
    explicit PitchHistoryComponent (MentalsAutotuneAudioProcessor& proc)
        : processor (proc)
    {
        detectedHistory.assign ((size_t) historyLength, 0.0f);
        targetHistory.assign ((size_t) historyLength, 0.0f);
        voicedHistory.assign ((size_t) historyLength, false);
        startTimerHz (30);
    }

    ~PitchHistoryComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    static constexpr int historyLength = 150;
    std::vector<float> detectedHistory, targetHistory;
    std::vector<bool> voicedHistory;

    MentalsAutotuneAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchHistoryComponent)
};

//==============================================================================
class MentalsAutotuneAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Button::Listener,
                                             private juce::ComboBox::Listener
{
public:
    explicit MentalsAutotuneAudioProcessorEditor (MentalsAutotuneAudioProcessor&);
    ~MentalsAutotuneAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsAutotuneAudioProcessor& processor;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, preset select/save.
    //==========================================================================
    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    PitchHistoryComponent pitchHistory;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    juce::Label keyLabel, scaleLabel;
    juce::ComboBox keySelector, scaleSelector;
    MentalsUI::LabelledSlider retuneSpeedSlider, amountSlider, mixSlider;

    // Manual overrides for both of Phase 1's automatic/adaptive behaviours --
    // formant preservation is deterministic DSP rather than "AI-driven", but
    // still gets its own bypass since it changes latency; Adaptive Retune
    // is the one behaviour here that reacts to the input rather than just
    // to a knob, so per the "manual override for all AI-driven features"
    // request, it must be fully disable-able.
    juce::ToggleButton formantPreservationToggle { "Formant Preservation" };
    juce::ToggleButton adaptiveRetuneToggle { "Adaptive Retune" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment, scaleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        retuneSpeedAttachment, amountAttachment, mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        formantPreservationAttachment, adaptiveRetuneAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::LevelMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsAutotuneAudioProcessorEditor)
};
