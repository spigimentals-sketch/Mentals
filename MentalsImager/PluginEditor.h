#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Classic 2D vectorscope/goniometer, rotated 45 degrees the same way Ozone
// Imager's (and most hardware goniometers') display reads: a purely mono
// signal draws a vertical line up the centre, a fully out-of-phase signal
// draws a horizontal line, and stereo width shows as how far the trail
// spreads sideways from that centre line. Deliberately a different visual
// language from Mentals 360 Stereo Shaper's 3D perspective stage -- this one
// reads instantly to anyone who has used a real goniometer.
//
// Fed straight from the processor's ring buffer of actual OUTPUT samples
// (see getGoniometerL()/getGoniometerR()), not illustrative -- same
// guarantee Stereo Shaper's analyzer makes. Correlation bar underneath uses
// the identical layout/colour logic as Stereo Shaper's StereoAnalyzerComponent.
//==============================================================================
class ImagerGoniometerComponent : public juce::Component,
                                   private juce::Timer
{
public:
    explicit ImagerGoniometerComponent (MentalsImagerAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (30);
    }

    ~ImagerGoniometerComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsImagerAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImagerGoniometerComponent)
};

//==============================================================================
class MentalsImagerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::Button::Listener,
                                           private juce::ComboBox::Listener
{
public:
    explicit MentalsImagerAudioProcessorEditor (MentalsImagerAudioProcessor&);
    ~MentalsImagerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }
    void refreshPresetList();
    void promptToSavePreset();
    void updateBandEnablement();

    MentalsImagerAudioProcessor& processor;

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
    juce::ToggleButton stereoToggle { "Stereo" };
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoAttachment;

    ImagerGoniometerComponent goniometer;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    juce::Label bandsLabel;
    juce::ComboBox bandsSelector;
    MentalsUI::LabelledSlider crossover1Slider, crossover2Slider, crossover3Slider;
    MentalsUI::LabelledSlider width1Slider, width2Slider, width3Slider, width4Slider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bandsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        crossover1Attachment, crossover2Attachment, crossover3Attachment,
        width1Attachment, width2Attachment, width3Attachment, width4Attachment, mixAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::AnalogVUMeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsImagerAudioProcessorEditor)
};
