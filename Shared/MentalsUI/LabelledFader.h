#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "MentalsColours.h"

namespace MentalsUI
{
    //==============================================================================
    // One vertical fader + label + APVTS attachment -- the fader-cap-in-a-groove
    // rendering used for the same real-hardware-style controls as
    // LabelledSlider's knobs (see MentalsLookAndFeel::drawLinearSlider()), for
    // parameters that read more naturally as a fader than a rotary pot (Mix/
    // dry-wet blend, output level). Same API shape as LabelledSlider
    // deliberately, so call sites look the same either way.
    //==============================================================================
    struct LabelledFader
    {
        juce::Slider slider { juce::Slider::LinearVertical, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        void addToParent (const juce::String& labelText, juce::Component& parent)
        {
            label.setText (labelText, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setColour (juce::Label::textColourId, Colours::white);
            label.attachToComponent (&slider, false);

            slider.setColour (juce::Slider::thumbColourId,             Colours::electricBlue);
            slider.setColour (juce::Slider::trackColourId,             Colours::slateGrayDark);
            slider.setColour (juce::Slider::backgroundColourId,        Colours::slateGrayDark);
            slider.setColour (juce::Slider::textBoxTextColourId,       Colours::white);
            slider.setColour (juce::Slider::textBoxBackgroundColourId, Colours::slateGrayDark);
            slider.setColour (juce::Slider::textBoxOutlineColourId,    Colours::slateGray);

            parent.addAndMakeVisible (slider);
            parent.addAndMakeVisible (label);
        }

        void rebind (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
        {
            attachment.reset();
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
        }

        void setVisible (bool shouldBeVisible)
        {
            slider.setVisible (shouldBeVisible);
            label.setVisible (shouldBeVisible);
        }
    };
}
