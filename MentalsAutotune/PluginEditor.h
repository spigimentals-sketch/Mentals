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
                                             private juce::ComboBox::Listener,
                                             private juce::Timer
{
public:
    explicit MentalsAutotuneAudioProcessorEditor (MentalsAutotuneAudioProcessor&);
    ~MentalsAutotuneAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void timerCallback() override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();
    void showSettingsPanel();
    void layoutSettingsPanelContent();
    void showAiAssistPanel();
    void layoutAiAssistPanelContent();
    void updateAiAssistStatusLabel();
    void showHarmonyPanel();
    void layoutHarmonyPanelContent();

    MentalsAutotuneAudioProcessor& processor;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, preset select/save,
    // Settings (opens a popup -- see showSettingsPanel()) holding every
    // toggle-style mode/behaviour, keeping the main panel focused on the
    // controls used every session (Key/Scale/Retune Speed/Amount/Mix).
    //==========================================================================
    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };
    juce::TextButton settingsButton { "Settings" };
    juce::Component settingsPanelContent;
    juce::TextButton aiAssistButton { "AI Assist" };
    juce::Component aiAssistPanelContent;
    juce::TextButton harmonyButton { "Harmony" };
    juce::Component harmonyPanelContent;

    PitchHistoryComponent pitchHistory;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    juce::Label keyLabel, scaleLabel;
    juce::ComboBox keySelector, scaleSelector;
    MentalsUI::LabelledSlider retuneSpeedSlider, amountSlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment, scaleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        retuneSpeedAttachment, amountAttachment, mixAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::LevelMeterComponent outputMeter;

    //==========================================================================
    // Settings popup content: every toggle-style mode/behaviour, all with
    // their own manual override per the "manual override for all AI-driven
    // features" request (formant preservation is deterministic DSP rather
    // than "AI-driven", but still gets a bypass since it changes latency).
    // Children of settingsPanelContent, not of the editor directly.
    //==========================================================================
    juce::ToggleButton formantPreservationToggle { "Formant Preservation" };
    juce::ToggleButton adaptiveRetuneToggle { "Adaptive Retune" };
    juce::ToggleButton midiControlToggle { "MIDI Control" };
    juce::ToggleButton sidechainTuningToggle { "Sidechain Tuning" };
    juce::ToggleButton lowLatencyModeToggle { "Low-Latency Mode" };
    juce::ToggleButton flexTuneToggle { "Flex-Tune" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        formantPreservationAttachment, adaptiveRetuneAttachment,
        midiControlAttachment, sidechainTuningAttachment, lowLatencyModeAttachment,
        flexTuneAttachment;

    //==========================================================================
    // AI Assist popup content: measures the input's own recently-detected
    // pitch movement, then runs that through a small trained model (see
    // AiAssistModel.h and MentalsAutotuneAudioProcessor::
    // applySuggestedVocalSettings()) to get the suggested Retune Speed/
    // Amount and style label. Unlike Mentals Multimode EQ's AI Assist
    // (still a disclosed rule-based heuristic), this one is a genuinely
    // trained model.
    //==========================================================================
    juce::Label aiAssistLabel, aiAssistStatusLabel;
    juce::TextButton aiAssistAnalyseButton { "Analyze" };
    juce::TextButton aiAssistApplyButton { "Apply Suggestion" };
    bool aiAssistWasCapturing = false; // edge-detects capture-just-finished, to flip the status label once

    //==========================================================================
    // Harmony popup content: two independent harmony voices, each shifting
    // the dry signal by a configurable number of scale degrees (not fixed
    // semitones -- see PitchDSP::nearestScaleDegreeCents()) from the
    // detected pitch. Only engage during ordinary scale-snapping (see
    // MentalsAutotuneAudioProcessor's class comment for that scope
    // boundary) and don't go through Formant Preservation.
    //==========================================================================
    juce::Label harmony1Label, harmony2Label;
    juce::ToggleButton harmony1EnabledToggle { "Enabled" }, harmony2EnabledToggle { "Enabled" };
    MentalsUI::LabelledSlider harmony1DegreeSlider, harmony1LevelSlider;
    MentalsUI::LabelledSlider harmony2DegreeSlider, harmony2LevelSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> harmony1EnabledAttachment, harmony2EnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        harmony1DegreeAttachment, harmony1LevelAttachment, harmony2DegreeAttachment, harmony2LevelAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsAutotuneAudioProcessorEditor)
};
