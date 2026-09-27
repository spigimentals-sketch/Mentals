#pragma once

#include <JuceHeader.h>
#include "LiveAudioEngine.h"
#include "ChannelStripComponent.h"
#include "MentalsUI.h"
#include <memory>
#include <vector>

//==============================================================================
// Top-level content: a device-setup bar, and a horizontally-scrolling row
// of ChannelStripComponents along the rest of the window -- one per active
// channel, each fully self-contained (its own Inserts chain, fader,
// mute/solo), matching a real digital mixer's control surface. There's no
// separate "selected channel" detail panel -- every channel manages its own
// insert chain directly via its own Inserts button.
//==============================================================================
class MainComponent : public juce::Component,
                       private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override; // fires on audio device changes
    void rebuildChannelStrips();
    void toggleDeviceSelector();

    LiveAudioEngine engine;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    juce::TextButton deviceSelectorButton { "Audio Device Setup..." };
    bool deviceSelectorVisible = false;

    juce::Label titleLabel;

    juce::Viewport channelStripViewport;
    juce::Component channelStripContent;
    std::vector<std::unique_ptr<ChannelStripComponent>> channelStrips;
    int lastKnownChannelCount = -1;

    MentalsUI::HardwareLookAndFeel hardwareLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
