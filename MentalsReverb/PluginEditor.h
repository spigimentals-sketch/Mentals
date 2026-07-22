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
// Rack-hardware-unit styling, modelled directly on a classic 1970s-style
// hardware limiting amplifier's front panel (a near-black brushed chassis
// with bolted rack ears, polished-chrome knobs with panel-printed tick
// numbers, rocker toggle switches, and a cream-faced analog VU meter) --
// see MentalsUI::HardwareLookAndFeel/MentalsUI::AnalogVUMeterComponent for
// the actual drawing code (shared with Mentals De-esser, the second plugin
// to use this look).
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

    // Pre-Delay's own ms value is ignored while Tempo Sync is on (see
    // MentalsReverbAudioProcessor::computeTempoSyncedPreDelayMs()) -- greyed
    // out rather than hidden, so it's obvious why turning the knob does
    // nothing audible until Sync is switched back off.
    void updatePreDelayEnablement();

    MentalsReverbAudioProcessor& processor;

    // A plain member (not a shared singleton) since only one Reverb editor
    // instance needs it at a time here.
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
    juce::ComboBox presetSelector;
    juce::TextButton presetSaveButton { "Save" };

    DecayEnvelopeComponent decayEnvelope;
    MentalsUI::SplitterBar splitter;

    //==========================================================================
    // Controls.
    //==========================================================================
    MentalsUI::LabelledSlider roomSizeSlider, dampingSlider, widthSlider, preDelaySlider, shimmerSlider,
                               lowCutSlider, highCutSlider, earlyReflectionsSlider, modDepthSlider,
                               modRateSlider, duckingSlider, duckingAttackSlider, duckingReleaseSlider;
    MentalsUI::LabelledFader mixSlider; // dry/wet blend reads more naturally as a fader than a knob
    juce::ToggleButton freezeToggle { "Freeze" };
    juce::ToggleButton tempoSyncToggle { "Tempo Sync" };
    juce::ComboBox preDelayDivisionCombo;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        roomSizeAttachment, dampingAttachment, widthAttachment, mixAttachment, preDelayAttachment, shimmerAttachment,
        lowCutAttachment, highCutAttachment, earlyReflectionsAttachment, modDepthAttachment, modRateAttachment,
        duckingAttachment, duckingAttackAttachment, duckingReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment, tempoSyncAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> preDelayDivisionAttachment;

    juce::Label outputMeterLabel;
    MentalsUI::AnalogVUMeterComponent outputMeter;

    // Ducking's meter -- the same analog VU face as Out, just fed gain
    // reduction instead of output level (see class comment: the reference
    // unit's own meter is dual-purpose the same way, just switched between
    // modes, rather than being two different meter types).
    juce::Label duckingMeterLabel;
    MentalsUI::AnalogVUMeterComponent duckingMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsReverbAudioProcessorEditor)
};
