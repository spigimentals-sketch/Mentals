#pragma once

#include <JuceHeader.h>
#include "LiveAudioEngine.h"
#include "MentalsUI.h"
#include <functional>
#include <memory>

//==============================================================================
// One vertical hardware-mixer-style channel strip: an Inserts button (opens
// this channel's own Mentals Suite chain-builder editor -- add/remove/
// reorder any plugin, exactly like a DAW mixer channel's insert slots, e.g.
// Studio One's), a live count of how many modules are loaded, Mute/Solo, a
// peak meter, and a fader. Shown side by side in a horizontally-scrolling
// row for up to 64 channels, modelled on a real digital mixing console's
// channel strip.
//==============================================================================
class ChannelStripComponent : public juce::Component,
                               private juce::Timer
{
public:
    ChannelStripComponent (int channelIndexIn, LiveAudioEngine& engineIn);
    ~ChannelStripComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshFromState();
    void openInsertsWindow();

    int channelIndex;
    LiveAudioEngine& engine;

    juce::Label numberLabel;
    juce::TextButton insertsButton { "Inserts" };
    juce::Label insertsCountLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    MentalsUI::LevelMeterComponent meter;
    MentalsUI::LabelledFader fader;

    std::unique_ptr<juce::DocumentWindow> insertsWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripComponent)
};
