#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"
#include <array>
#include <memory>

//==============================================================================
// The scrolling TRANSIENTS display: draws the last ~2 seconds of input level
// as a bar-per-column waveform, with a coloured vertical tick + dot at every
// column a hit was detected in (coloured by which layer it picked), and a
// horizontal Threshold line the user can grab and drag to retune Threshold
// visually instead of only via the knob -- matching Addictive Trigger's
// TRANSIENTS panel.
//==============================================================================
class WaveformDisplayComponent : public juce::Component,
                                  private juce::Timer
{
public:
    explicit WaveformDisplayComponent (MentalsTriggerAudioProcessor& proc) : processor (proc) { startTimerHz (30); }
    ~WaveformDisplayComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    float thresholdToY (float thresholdDb) const noexcept;
    bool isNearThresholdLine (float y) const noexcept;

    MentalsTriggerAudioProcessor& processor;
    bool isDraggingThreshold = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplayComponent)
};

//==============================================================================
// The velocity response curve: plots shapeVelocity() across the full 0..1
// input range (so it always shows exactly what the processor will do, never
// an approximation of it), draggable vertically to adjust the Curve
// parameter -- up boosts quiet hits (concave), down suppresses them
// (convex). Matches Addictive Trigger's MIDI Response curve in spirit.
//==============================================================================
class VelocityCurveComponent : public juce::Component,
                                private juce::Timer
{
public:
    explicit VelocityCurveComponent (MentalsTriggerAudioProcessor& proc) : processor (proc) { startTimerHz (20); }
    ~VelocityCurveComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }

    MentalsTriggerAudioProcessor& processor;
    float dragStartCurveValue = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelocityCurveComponent)
};

//==============================================================================
// One velocity layer's card: name, loaded-sample count, and 4 round-robin
// slot buttons (A/B/C/D) -- click one to load a sample into that specific
// slot via a native file chooser. A loaded slot's button is tinted with the
// layer's accent colour; the slot a hit just picked briefly flashes white,
// so watching the UI while playing confirms both that hits are being
// detected and which take got picked.
//==============================================================================
class LayerCardComponent : public juce::Component,
                            private juce::Timer
{
public:
    LayerCardComponent (int layerIndexIn, MentalsTriggerAudioProcessor& proc);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseFile (int roundRobinIndex);

    int layerIndex;
    MentalsTriggerAudioProcessor& processor;
    std::array<juce::TextButton, MentalsTriggerAudioProcessor::numRoundRobins> roundRobinButtons;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Standard mixer-channel semantics: solo takes priority over mute
    // across layers (see MentalsTriggerAudioProcessor::isLayerAudible()).
    // A muted/non-soloed layer still detects hits and flashes here -- only
    // its actual audio output is silenced.
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment, soloAttachment;

    juce::int64 lastSeenHitCounter = 0;
    float flashAlpha = 0.0f;
    int flashingRoundRobin = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerCardComponent)
};

//==============================================================================
// A small underlined section label above a knob cluster.
//==============================================================================
class SectionHeaderComponent : public juce::Component
{
public:
    explicit SectionHeaderComponent (juce::String labelText) : text (std::move (labelText)) {}
    void paint (juce::Graphics& g) override;

private:
    juce::String text;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionHeaderComponent)
};

//==============================================================================
class MentalsTriggerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Button::Listener,
                                            private juce::ComboBox::Listener
{
public:
    explicit MentalsTriggerAudioProcessorEditor (MentalsTriggerAudioProcessor&);
    ~MentalsTriggerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();

    MentalsTriggerAudioProcessor& processor;

    MentalsUI::HardwareLookAndFeel hardwareLookAndFeel;

    juce::Rectangle<int> lastPanelBounds;

    //==========================================================================
    // Top bar -- a wide, prominent kit-name preset field alongside the
    // wordmark, matching the reference's header proportions.
    //==========================================================================
    MentalsUI::MetallicLogoComponent logoImage;
    juce::Label productNameLabel;
    juce::Label productSubtitleLabel;
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    //==========================================================================
    // TRANSIENTS: live waveform + hit markers + draggable threshold, and the
    // 3 velocity-layer cards below it.
    //==========================================================================
    WaveformDisplayComponent waveformDisplay;
    std::array<std::unique_ptr<LayerCardComponent>, MentalsTriggerAudioProcessor::numLayers> layerCards;

    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls: detection (Threshold/Sensitivity/Choke), velocity split
    // (Soft/Med, Med/Hard) + response curve, and output (gain + in/out
    // meters).
    //==========================================================================
    SectionHeaderComponent detectionSectionHeader { "DETECTION" };
    SectionHeaderComponent splitSectionHeader { "VELOCITY SPLIT" };
    SectionHeaderComponent outputSectionHeader { "OUTPUT" };

    MentalsUI::LabelledSlider thresholdSlider, sensitivitySlider, chokeTimeSlider;
    MentalsUI::LabelledSlider softMedSplitSlider, medHardSplitSlider;
    VelocityCurveComponent velocityCurve;
    juce::Label velocityCurveLabel;
    MentalsUI::LabelledFader outputGainSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        thresholdAttachment, sensitivityAttachment, chokeTimeAttachment,
        softMedSplitAttachment, medHardSplitAttachment, outputGainAttachment;

    juce::Label inputMeterLabel, outputMeterLabel;
    MentalsUI::LevelMeterComponent inputMeter;
    MentalsUI::LevelMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsTriggerAudioProcessorEditor)
};
