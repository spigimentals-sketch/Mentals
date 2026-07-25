#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Illustrative (parameter-driven, redrawn on a timer, not a live audio
// capture) transfer curve -- calls
// MentalsCircuitCompAudioProcessor::computeModeAdjustedOutputDb() directly,
// the exact same function processBlock() uses, so the curve's shape can
// never disagree with what the current Mode/Threshold/Ratio/Knee setting
// actually does to the audio. Shown when Multiband is off; replaced by
// MultibandSpectrumComponent when it's on.
//==============================================================================
class CircuitCompCurveComponent : public juce::Component,
                                   private juce::Timer
{
public:
    explicit CircuitCompCurveComponent (MentalsCircuitCompAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~CircuitCompCurveComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsCircuitCompAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CircuitCompCurveComponent)
};

//==============================================================================
// The 7-band spectrum display: one zone per band (log-frequency-mapped,
// 20Hz-20kHz), shaded live by that band's current gain reduction (reading
// MentalsCircuitCompAudioProcessor::getBandGainReductionDb() -- real
// metering, not illustrative) so you can actually see which bands are
// doing the work. Click a zone to select that band for editing below;
// drag one of the 6 divider lines to move that crossover frequency.
// Muted bands dim, soloed bands get a highlighted border -- mirroring
// Mentals Multimode EQ's click-to-grab spectrum interaction, generalised
// from gain/frequency band points to frequency-only crossover dividers.
//==============================================================================
class MultibandSpectrumComponent : public juce::Component,
                                    private juce::Timer
{
public:
    explicit MultibandSpectrumComponent (MentalsCircuitCompAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (20);
    }

    ~MultibandSpectrumComponent() override { stopTimer(); }

    void setSelectedBand (int bandIndex) noexcept { selectedBand = bandIndex; }

    // Called when a click selects a (possibly different) band, so the
    // editor can keep its tabs/detail panel in sync.
    std::function<void (int)> onBandSelected;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }

    float frequencyToX (double freqHz) const;
    double xToFrequency (float x) const;
    int findNearestCrossover (float x) const;
    int findBandAt (float x) const;

    MentalsCircuitCompAudioProcessor& processor;
    int selectedBand = 0;
    int draggingCrossover = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandSpectrumComponent)
};

//==============================================================================
class MentalsCircuitCompAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                private juce::Button::Listener,
                                                private juce::ComboBox::Listener,
                                                private juce::Timer
{
public:
    explicit MentalsCircuitCompAudioProcessorEditor (MentalsCircuitCompAudioProcessor&);
    ~MentalsCircuitCompAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void timerCallback() override; // polls Multiband on/off to swap the graph and rebind knobs
    void refreshPresetList();
    void promptToSavePreset();
    void selectBand (int bandIndex);
    void rebuildKnobAttachments(); // (re)binds Threshold/Ratio/Attack/Release/Makeup to global or per-band params
    void updateMultibandVisibility();

    MentalsCircuitCompAudioProcessor& processor;
    int selectedBand = 0;
    bool lastMultibandState = false;

    MentalsUI::HardwareLookAndFeel hardwareLookAndFeel;

    // Set in resized(), read back in paint() so the metal-panel texture,
    // corner screws, and rack ears are drawn over exactly the same area the
    // knobs sit in.
    juce::Rectangle<int> lastPanelBounds;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, Mode selector (the
    // headline feature, so it sits in the top bar rather than the knob
    // panel), Use Sidechain / Multiband / Stereo Link toggles, preset
    // select/save.
    //==========================================================================
    MentalsUI::MetallicLogoComponent logoImage;
    juce::Label productNameLabel;
    juce::Label modeLabel;
    juce::ComboBox modeSelector;
    juce::TextButton sidechainToggle { "Sidechain" };
    juce::TextButton multibandToggle { "Multiband" };
    juce::TextButton linkToggle      { "Link" };
    juce::ToggleButton stereoToggle { "Stereo" };
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        sidechainAttachment, multibandAttachment, linkAttachment, stereoAttachment;

    CircuitCompCurveComponent curve;
    MultibandSpectrumComponent spectrum;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Band tabs (one per band, "1".."7", shown only when Multiband is on).
    //==========================================================================
    std::array<juce::TextButton, MentalsCircuitCompAudioProcessor::numBands> bandTabs;

    //==========================================================================
    // Controls: row 1 is the core dynamics (Threshold/Ratio/Attack/Release
    // rebind between global and the selected band's own params depending on
    // Multiband; Knee always stays global), row 2 is coloration/output plus
    // metering (Makeup rebinds the same way; Saturation/Blend stay global),
    // row 3 is the sidechain filter (single-band only) plus the selected
    // band's Mute/Solo (multiband only).
    //==========================================================================
    MentalsUI::LabelledSlider thresholdSlider, ratioSlider, kneeSlider, attackSlider, releaseSlider;
    MentalsUI::LabelledSlider makeupGainSlider, saturationSlider, blendSlider;
    MentalsUI::LabelledSlider sidechainHpfFreqSlider;
    juce::TextButton bandMuteToggle { "Mute" }, bandSoloToggle { "Solo" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        thresholdAttachment, ratioAttachment, kneeAttachment, attackAttachment, releaseAttachment,
        makeupGainAttachment, saturationAttachment, blendAttachment, sidechainHpfFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bandMuteAttachment, bandSoloAttachment;

    juce::Label gainReductionMeterLabel, vuMeterLabel, outputMeterLabel;
    MentalsUI::AnalogVUMeterComponent gainReductionMeter;
    MentalsUI::AnalogVUMeterComponent vuMeter;
    MentalsUI::AnalogVUMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsCircuitCompAudioProcessorEditor)
};
