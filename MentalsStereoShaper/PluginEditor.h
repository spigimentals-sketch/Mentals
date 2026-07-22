#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// 3D stereo stage (a perspective-projected circular floor, viewed from the
// listener's position looking into it) plus a correlation bar -- both read
// straight from the processor's actual output samples (see
// MentalsStereoShaperAudioProcessor's goniometer ring buffer/
// smoothedCorrelation), unlike e.g. Mentals Vox Choir's parameter-driven
// illustration. Each recent output sample's Mid/Side becomes a genuine 3D
// position on the stage floor -- angle around the circle is the actual
// instantaneous stereo placement (so rotation/auto-rotate visibly sweeps a
// sound source around the stage exactly as the "360" in this plugin's name
// promises), radius is how far from dead-centre that sample sits, and a
// real (not illustrative) perspective camera transform projects the whole
// floor -- grid rings, radial spokes, speaker markers, and the live
// sample trail -- down to 2D with correct foreshortening (nearer/louder
// content reads bigger and lower, further/quieter content reads smaller
// and closer to the horizon).
//
// On top of the live trail, a weighted circular mean of those same samples
// (see paint()) gives this track's own occupancy zone -- a filled wedge
// showing WHERE it's centred (a degrees-off-centre label at the wedge) and
// HOW MUCH of the stage it's spread across (the wedge's angular width and
// radial extent), rather than leaving the user to eyeball the scatter.
// Every other currently-open Stereo Shaper instance's last published
// placement (see MixRegistry::getOthersSnapshot()) is drawn as a smaller,
// dimmer static marker, so the whole session's spatial layout -- not just
// this one track -- is visible at a glance.
//
// On top of that, the "gap finder" (see paint()) is a continuous, always-on
// suggestion -- not a one-shot action like AI Placement -- for the
// currently least-crowded angle given every other instance's own placement
// and width, drawn as a green arc plus a degrees-off-centre readout of the
// Rotation value that would sit a new source there.
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

    // World-space point on the (flat, y=0) stage floor, in the range
    // [-1, 1] on both x (left/right) and z (0 = centre of the stage,
    // 1 = the far/outer edge) -- projected to a 2D screen point by a fixed
    // perspective camera looking into the stage from the listener's
    // position at the near edge.
    juce::Point<float> projectStagePoint (float worldX, float worldZ, float worldY, juce::Rectangle<float> bounds) const noexcept;

    MentalsStereoShaperAudioProcessor& processor;

    // Gap finder smoothing state (see paint()) -- persisted across frames
    // so the suggested angle drifts calmly rather than jittering as other
    // tracks' loudness fluctuates from block to block.
    float gapSmoothedSin = 0.0f, gapSmoothedCos = 1.0f;
    bool gapEstimateValid = false;

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

    MentalsUI::HardwareLookAndFeel hardwareLookAndFeel;

    // Set in resized(), read back in paint() so the metal-panel texture,
    // corner screws, and rack ears are drawn over exactly the same area the
    // knobs sit in.
    juce::Rectangle<int> lastPanelBounds;

    //==========================================================================
    // Top bar: shared MENTALS wordmark + product name, Phase Align / AI
    // Placement (both global, not tied to any one band, hence living here
    // rather than in the knob panel), preset select/save.
    //==========================================================================
    MentalsUI::MetallicLogoComponent logoImage;
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
