#include "MainComponent.h"

MainComponent::MainComponent()
{
    setLookAndFeel (&hardwareLookAndFeel);

    titleLabel.setText ("Mentals Live Host", juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
    addAndMakeVisible (titleLabel);

    deviceSelectorButton.setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::slateGrayDark);
    deviceSelectorButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
    deviceSelectorButton.onClick = [this] { toggleDeviceSelector(); };
    addAndMakeVisible (deviceSelectorButton);

    channelStripViewport.setViewedComponent (&channelStripContent, false);
    addAndMakeVisible (channelStripViewport);

    engine.getDeviceManager().addChangeListener (this);
    rebuildChannelStrips();

    setSize (1100, 700);
}

MainComponent::~MainComponent()
{
    engine.getDeviceManager().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildChannelStrips();
}

void MainComponent::rebuildChannelStrips()
{
    const int numChannels = juce::jmax (1, engine.getNumActiveChannels());
    if (numChannels == lastKnownChannelCount)
        return;
    lastKnownChannelCount = numChannels;

    channelStrips.clear();
    channelStripContent.removeAllChildren();

    constexpr int stripWidth = 90;
    const int stripHeight = channelStripViewport.getHeight() > 0 ? channelStripViewport.getHeight() : 500;
    for (int i = 0; i < numChannels; ++i)
    {
        auto strip = std::make_unique<ChannelStripComponent> (i, engine);
        channelStripContent.addAndMakeVisible (*strip);
        strip->setBounds (i * stripWidth, 0, stripWidth - 4, stripHeight);
        channelStrips.push_back (std::move (strip));
    }
    channelStripContent.setSize (numChannels * stripWidth, stripHeight);
}

void MainComponent::toggleDeviceSelector()
{
    deviceSelectorVisible = ! deviceSelectorVisible;

    if (deviceSelectorVisible && deviceSelector == nullptr)
    {
        deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
            engine.getDeviceManager(), 0, LiveAudioEngine::maxChannels, 0, LiveAudioEngine::maxChannels,
            false, false, true, false);
        addAndMakeVisible (*deviceSelector);
    }

    if (deviceSelector != nullptr)
        deviceSelector->setVisible (deviceSelectorVisible);

    resized();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0c0c0d));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto topBar = area.removeFromTop (44).reduced (8, 6);
    titleLabel.setBounds (topBar.removeFromLeft (260));
    deviceSelectorButton.setBounds (topBar.removeFromRight (180));

    if (deviceSelectorVisible && deviceSelector != nullptr)
        deviceSelector->setBounds (area.removeFromTop (280).reduced (8));

    // The horizontally-scrolling fader bank fills the rest of the window,
    // same position a real mixer's channel strips occupy across its
    // control surface.
    auto stripArea = area.reduced (8);
    channelStripViewport.setBounds (stripArea);

    constexpr int stripWidth = 90;
    const int numStrips = (int) channelStrips.size();
    channelStripContent.setSize (numStrips * stripWidth, stripArea.getHeight());
    for (int i = 0; i < numStrips; ++i)
        channelStrips[(size_t) i]->setBounds (i * stripWidth, 0, stripWidth - 4, stripArea.getHeight());
}
