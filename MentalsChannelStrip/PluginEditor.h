#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Round-bulb gain-reduction meter -- the SSL E-Channel's GR meter is a column
// of individual round LED bulbs (not the suite's usual rounded-rect LED
// segments, see MentalsUI::GainReductionMeterComponent), so this is a
// bespoke, Channel-Strip-only variant rather than a change to the shared
// component (which 0 other plugins use yet, but rectangular segments are
// still the right shared default -- this specific plugin is the one
// deliberately chasing an exact SSL look). Same fill-from-the-top,
// jump-to-more-reduction/slow-decay ballistics as the shared component.
//==============================================================================
class BulbGrMeterComponent : public juce::Component,
                              private juce::Timer
{
public:
    explicit BulbGrMeterComponent (std::function<float()> getGainReductionDbIn)
        : getGainReductionDb (std::move (getGainReductionDbIn))
    {
        startTimerHz (30);
    }

    ~BulbGrMeterComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override
    {
        const float target = getGainReductionDb ? getGainReductionDb() : 0.0f;

        if (target < displayedGainReductionDb)
            displayedGainReductionDb = target;
        else
            displayedGainReductionDb = juce::jmin (target, displayedGainReductionDb + 1.5f);

        repaint();
    }

    std::function<float()> getGainReductionDb;
    float displayedGainReductionDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BulbGrMeterComponent)
};

//==============================================================================
// Round-bulb output level meter -- same green/amber/red zone convention and
// clip LED as MentalsUI::LevelMeterComponent, just rendered as bulbs instead
// of rectangular LED segments (see BulbGrMeterComponent's comment above for
// why this is a local, Channel-Strip-only variant rather than a change to
// the shared component other plugins already rely on looking a certain way).
//==============================================================================
class BulbLevelMeterComponent : public juce::Component,
                                 private juce::Timer
{
public:
    BulbLevelMeterComponent (std::function<float()> getPeakDbIn, std::function<bool()> isClippingIn)
        : getPeakDb (std::move (getPeakDbIn)), isClipping (std::move (isClippingIn))
    {
        startTimerHz (30);
    }

    ~BulbLevelMeterComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override
    {
        displayedPeakDb = getPeakDb ? getPeakDb() : -100.0f;
        clipping = isClipping && isClipping();
        repaint();
    }

    std::function<float()> getPeakDb;
    std::function<bool()> isClipping;
    float displayedPeakDb = -100.0f;
    bool clipping = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BulbLevelMeterComponent)
};

//==============================================================================
// Two-column arrangement modelled on a classic SSL E-Channel strip: EQ
// (Filters/HF/HMF/LMF/LF) down the left column, Dynamics (Compressor/Gate)
// and the final Gain/output stage down the right column, each knob cluster
// colour-coded by band the same way the hardware's knob caps are (HF red,
// HMF green, LMF blue, LF gold). Meters are the round-bulb "lighting" style
// above rather than a swinging analog needle, matching the SSL's own
// bulb-style GR/output meters exactly. Two columns side by side keeps the
// whole strip short enough to fit on an ordinary laptop screen, unlike a
// single tall vertical stack.
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

    MentalsUI::HardwareLookAndFeel hardwareLookAndFeel;

    // Set in resized(), read back in paint() so the metal-panel texture,
    // corner screws, and rack ears are drawn over exactly the same area the
    // knobs sit in.
    juce::Rectangle<int> lastPanelBounds;

    // Set in resized(), read back in paint() to draw a grouping box around
    // the whole Dynamics block (Compressor + Gate together) -- addresses the
    // "dynamics section isn't arranged" note: a visible boundary makes it
    // read as one deliberate unit instead of two knob rows floating loose.
    juce::Rectangle<int> lastDynamicsBounds;

    //==========================================================================
    // Top bar.
    //==========================================================================
    MentalsUI::MetallicLogoComponent logoImage;
    juce::Label productNameLabel;
    juce::TextButton presetsButton { "Presets" };
    juce::TextButton presetSaveButton { "Save" };
    juce::ToggleButton stereoToggle { "Stereo" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoAttachment;

    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // LEFT COLUMN -- Filters + EQ.
    //==========================================================================
    juce::Label filtersLabel { {}, "FILTERS" };
    MentalsUI::LabelledSlider hpfFreqSlider, lpfFreqSlider;
    juce::TextButton filterSplitToggle { "Split" };
    juce::TextButton filtersInToggle   { "In" };

    juce::Label eqLabel { {}, "EQ" };
    juce::TextButton eqInToggle { "EQ In" };

    juce::Label hfLabel  { {}, "HF" };
    MentalsUI::LabelledSlider hfFreqSlider, hfGainSlider;
    juce::TextButton hfBellToggle { "Bell" };

    juce::Label hmfLabel { {}, "HMF" };
    MentalsUI::LabelledSlider hmfFreqSlider, hmfGainSlider, hmfQSlider;

    juce::Label lmfLabel { {}, "LMF" };
    MentalsUI::LabelledSlider lmfFreqSlider, lmfGainSlider, lmfQSlider;

    juce::Label lfLabel  { {}, "LF" };
    MentalsUI::LabelledSlider lfFreqSlider, lfGainSlider;
    juce::TextButton lfBellToggle { "Bell" };

    //==========================================================================
    // RIGHT COLUMN -- Dynamics + Output.
    //==========================================================================
    juce::Label dynamicsLabel { {}, "DYNAMICS" };

    juce::Label compLabel { {}, "COMPRESSOR" };
    MentalsUI::LabelledSlider compThresholdSlider, compRatioSlider, compAttackSlider, compReleaseSlider, compMakeupSlider;
    BulbGrMeterComponent compGrMeter;

    juce::Label gateLabel { {}, "GATE / EXPANDER" };
    MentalsUI::LabelledSlider gateThresholdSlider, gateRatioSlider, gateAttackSlider, gateReleaseSlider, gateRangeSlider;
    BulbGrMeterComponent gateGrMeter;

    juce::TextButton dynamicsInToggle       { "Dyn In" };
    juce::TextButton dynamicsBeforeEqToggle { "Dyn > EQ" };

    juce::Label outputLabel { {}, "GAIN" };
    MentalsUI::LabelledSlider outputGainSlider;
    BulbLevelMeterComponent outputMeter;

    // LabelledSlider knobs above own their attachments internally (see
    // rebind()); only the plain TextButton toggles need one here.
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        filterSplitAttachment, filtersInAttachment, dynamicsInAttachment, dynamicsBeforeEqAttachment,
        lfBellAttachment, hfBellAttachment, eqInAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsChannelStripAudioProcessorEditor)
};
