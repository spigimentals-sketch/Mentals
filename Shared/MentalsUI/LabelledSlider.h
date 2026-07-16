#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "MentalsColours.h"

namespace MentalsUI
{
    //==============================================================================
    // One knob + label + APVTS attachment, styled consistently across every
    // Mentals plugin. The fill/thumb use electric blue since a knob is always
    // editing whatever module/band is currently selected.
    //==============================================================================
    struct LabelledSlider
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        // Visual setup only -- no attachment yet. Attachments are created
        // separately via rebind() once every referenced component in the editor
        // exists (an APVTS attachment's constructor synchronously fires
        // sendInitialUpdate(), which for combo boxes can cascade into resized();
        // see each plugin editor's constructor for why this matters there).
        void addToParent (const juce::String& labelText, juce::Component& parent)
        {
            label.setText (labelText, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setColour (juce::Label::textColourId, Colours::white);
            label.attachToComponent (&slider, false);

            slider.setColour (juce::Slider::rotarySliderFillColourId,    Colours::electricBlue);
            slider.setColour (juce::Slider::rotarySliderOutlineColourId, Colours::slateGray);
            slider.setColour (juce::Slider::thumbColourId,               Colours::electricBlue);
            slider.setColour (juce::Slider::textBoxTextColourId,         Colours::white);
            slider.setColour (juce::Slider::textBoxBackgroundColourId,   Colours::slateGrayDark);
            slider.setColour (juce::Slider::textBoxOutlineColourId,      Colours::slateGray);

            parent.addAndMakeVisible (slider);
            parent.addAndMakeVisible (label);
        }

        // (Re)binds this knob to a parameter -- used both for the first bind and
        // whenever the selected module/band changes to a different one.
        void rebind (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
        {
            attachment.reset(); // destroy the old attachment before the new one attaches
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
        }

        void setVisible (bool shouldBeVisible)
        {
            slider.setVisible (shouldBeVisible);
            label.setVisible (shouldBeVisible);
        }
    };
}
