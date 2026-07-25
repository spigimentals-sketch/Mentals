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
                                               private juce::ComboBox::Listener,
                                               private juce::Timer
{
public:
    explicit MentalsCompressorAudioProcessorEditor (MentalsCompressorAudioProcessor&);
    ~MentalsCompressorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void timerCallback() override; // polls AI Assist's capturing state
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();
    void showAiAssistPanel();
    void layoutAiAssistPanelContent();

    MentalsCompressorAudioProcessor& processor;

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
    juce::TextButton aiAssistButton { "AI Assist" };
    juce::Component aiAssistPanelContent;
    juce::ToggleButton stereoToggle { "Stereo" };
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoAttachment;

    // AI Assist: listens to the detector signal for a few seconds and
    // suggests Threshold/Ratio/Attack/Release/Makeup (see
    // MentalsCompressorAudioProcessor::applySuggestedCompressorSettings()).
    // Shown as its own popup (see showAiAssistPanel()), the same pattern
    // Autotune/Multimode EQ's own AI Assist popups use.
    juce::Label aiAssistLabel, aiAssistStatusLabel;
    juce::TextButton aiAssistAnalyseButton { "Analyze" };
    juce::TextButton aiAssistApplyButton   { "Apply Suggestion" };
    bool aiAssistWasCapturing = false; // edge-detects capture-just-finished, to flip the status label once

    CompressorTransferCurveComponent transferCurve;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls -- two rows: threshold/ratio/knee/attack/release on top,
    // makeup/mix/sidechain toggle + meters below.
    //==========================================================================
    MentalsUI::LabelledSlider thresholdSlider, ratioSlider, kneeSlider, attackSlider, releaseSlider;
    MentalsUI::LabelledSlider makeupGainSlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob
    juce::ToggleButton sidechainToggle { "Use Sidechain" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        thresholdAttachment, ratioAttachment, kneeAttachment, attackAttachment, releaseAttachment,
        makeupGainAttachment, mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sidechainAttachment;

    juce::Label gainReductionMeterLabel, outputMeterLabel;
    MentalsUI::AnalogVUMeterComponent gainReductionMeter;
    MentalsUI::AnalogVUMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsCompressorAudioProcessorEditor)
};
