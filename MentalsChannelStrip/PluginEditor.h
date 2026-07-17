#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Combined 4-band EQ curve: sums the LF/LMF/HMF/HF magnitude responses (via
// MentalsChannelStripAudioProcessor::getMagnitudeForFrequency(), the exact
// same math processBlock() uses) into one dB curve across 20Hz-20kHz.
// Read-only -- unlike Multimode EQ's graph, there's nothing to drag here,
// just a shape to confirm the four knob clusters below are doing what's
// expected.
//==============================================================================
class ChannelStripCurveComponent : public juce::Component,
                                    private juce::Timer
{
public:
    explicit ChannelStripCurveComponent (MentalsChannelStripAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (15);
    }

    ~ChannelStripCurveComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsChannelStripAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripCurveComponent)
};

//==============================================================================
class MentalsChannelStripAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                 private juce::Button::Listener
{
public:
    explicit MentalsChannelStripAudioProcessorEditor (MentalsChannelStripAudioProcessor&);
    ~MentalsChannelStripAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void showPresetsMenu();
    void promptToSavePreset();
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }

    MentalsChannelStripAudioProcessor& processor;

    //==========================================================================
    // Top bar.
    //==========================================================================
    juce::ImageComponent logoImage;
    juce::Label productNameLabel;
    juce::TextButton presetsButton { "Presets" };
    juce::TextButton presetSaveButton { "Save" };

    ChannelStripCurveComponent curve;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Filters.
    //==========================================================================
    juce::Label filtersLabel { {}, "FILTERS" };
    MentalsUI::LabelledSlider hpfFreqSlider, lpfFreqSlider;
    juce::TextButton filterSplitToggle { "Split" };
    juce::TextButton filtersInToggle   { "In" };

    //==========================================================================
    // Dynamics: Compressor + Expander/Gate, sharing one Dyn In / Dyn Order
    // pair of toggles.
    //==========================================================================
    juce::Label compLabel { {}, "COMPRESSOR" };
    MentalsUI::LabelledSlider compThresholdSlider, compRatioSlider, compAttackSlider, compReleaseSlider, compMakeupSlider;
    MentalsUI::GainReductionMeterComponent compGrMeter;

    juce::Label gateLabel { {}, "GATE / EXPANDER" };
    MentalsUI::LabelledSlider gateThresholdSlider, gateRatioSlider, gateAttackSlider, gateReleaseSlider, gateRangeSlider;
    MentalsUI::GainReductionMeterComponent gateGrMeter;

    juce::TextButton dynamicsInToggle       { "Dyn In" };
    juce::TextButton dynamicsBeforeEqToggle { "Dyn > EQ" };

    //==========================================================================
    // EQ: LF/HF are shelf-with-Bell-option; LMF/HMF are full parametric bells.
    //==========================================================================
    juce::Label lfLabel  { {}, "LF" };
    MentalsUI::LabelledSlider lfFreqSlider, lfGainSlider;
    juce::TextButton lfBellToggle { "Bell" };

    juce::Label lmfLabel { {}, "LMF" };
    MentalsUI::LabelledSlider lmfFreqSlider, lmfGainSlider, lmfQSlider;

    juce::Label hmfLabel { {}, "HMF" };
    MentalsUI::LabelledSlider hmfFreqSlider, hmfGainSlider, hmfQSlider;

    juce::Label hfLabel  { {}, "HF" };
    MentalsUI::LabelledSlider hfFreqSlider, hfGainSlider;
    juce::TextButton hfBellToggle { "Bell" };

    juce::TextButton eqInToggle { "EQ In" };

    //==========================================================================
    // Output.
    //==========================================================================
    juce::Label outputLabel { {}, "OUTPUT" };
    MentalsUI::LabelledSlider outputGainSlider;
    MentalsUI::LevelMeterComponent outputMeter;

    // LabelledSlider knobs above own their attachments internally (see
    // rebind()); only the plain TextButton toggles need one here.
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        filterSplitAttachment, filtersInAttachment, dynamicsInAttachment, dynamicsBeforeEqAttachment,
        lfBellAttachment, hfBellAttachment, eqInAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsChannelStripAudioProcessorEditor)
};
