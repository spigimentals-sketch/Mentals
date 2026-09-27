#include "ChannelStripComponent.h"

namespace
{
    // Plain juce::DocumentWindow has no click callback for its close button
    // (unlike TextButton's onClick) -- this tiny subclass adds one so we
    // can just reset our own unique_ptr when the user closes a channel's
    // Inserts window.
    class CallbackDocumentWindow : public juce::DocumentWindow
    {
    public:
        using juce::DocumentWindow::DocumentWindow;
        std::function<void()> onCloseButtonPressed;
        void closeButtonPressed() override { if (onCloseButtonPressed) onCloseButtonPressed(); }
    };
}

ChannelStripComponent::ChannelStripComponent (int channelIndexIn, LiveAudioEngine& engineIn)
    : channelIndex (channelIndexIn), engine (engineIn),
      meter ([this]
             {
                 auto* strip = engine.getChannelStrip (channelIndex);
                 return strip != nullptr ? juce::Decibels::gainToDecibels (strip->getOutputPeakLevel(), -100.0f) : -100.0f;
             },
             [] { return false; })
{
    numberLabel.setText ("Ch " + juce::String (channelIndex + 1), juce::dontSendNotification);
    numberLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    numberLabel.setJustificationType (juce::Justification::centred);
    numberLabel.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    addAndMakeVisible (numberLabel);

    insertsButton.setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::slateGrayDark);
    insertsButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::electricBlue);
    insertsButton.onClick = [this] { openInsertsWindow(); };
    addAndMakeVisible (insertsButton);

    insertsCountLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::slateGray);
    insertsCountLabel.setJustificationType (juce::Justification::centred);
    insertsCountLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
    addAndMakeVisible (insertsCountLabel);

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
    muteButton.setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::crimsonRed);
    muteButton.setColour (juce::TextButton::textColourOffId,  MentalsUI::Colours::white);
    muteButton.setColour (juce::TextButton::textColourOnId,   MentalsUI::Colours::white);
    muteButton.onClick = [this]
    {
        if (auto* strip = engine.getChannelStrip (channelIndex))
            strip->setMuted (muteButton.getToggleState());
    };
    addAndMakeVisible (muteButton);

    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
    soloButton.setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::goldenYellow);
    soloButton.setColour (juce::TextButton::textColourOffId,  MentalsUI::Colours::white);
    soloButton.setColour (juce::TextButton::textColourOnId,   MentalsUI::Colours::charcoalBlack);
    soloButton.onClick = [this]
    {
        if (auto* strip = engine.getChannelStrip (channelIndex))
            strip->setSoloed (soloButton.getToggleState());
    };
    addAndMakeVisible (soloButton);

    addAndMakeVisible (meter);

    // Not APVTS-bound (the channel fader is a plain LiveChannelStrip field,
    // not a plugin parameter), so wired by hand rather than via
    // SliderAttachment: a musical +12/-60dB range with a 0dB (unity) mid-
    // point taper, same shape a real fader's law approximates.
    fader.addToParent ("", *this);
    fader.slider.setRange (-60.0, 12.0, 0.1);
    fader.slider.setSkewFactorFromMidPoint (0.0);
    if (auto* strip = engine.getChannelStrip (channelIndex))
        fader.slider.setValue (strip->getFaderGainDb(), juce::dontSendNotification);
    fader.slider.onValueChange = [this]
    {
        if (auto* strip = engine.getChannelStrip (channelIndex))
            strip->setFaderGainDb ((float) fader.slider.getValue());
    };

    startTimerHz (6);
}

ChannelStripComponent::~ChannelStripComponent()
{
    stopTimer();
}

void ChannelStripComponent::timerCallback()
{
    refreshFromState();
}

void ChannelStripComponent::refreshFromState()
{
    auto* strip = engine.getChannelStrip (channelIndex);
    if (strip == nullptr)
        return;

    if (muteButton.getToggleState() != strip->isMuted())
        muteButton.setToggleState (strip->isMuted(), juce::dontSendNotification);
    if (soloButton.getToggleState() != strip->isSoloed())
        soloButton.setToggleState (strip->isSoloed(), juce::dontSendNotification);

    const int numSlots = (int) strip->getInsertChain().getChainSlots().size();
    insertsCountLabel.setText (numSlots == 0 ? "Empty" : (juce::String (numSlots) + (numSlots == 1 ? " FX" : " FX")),
                                juce::dontSendNotification);
}

void ChannelStripComponent::openInsertsWindow()
{
    if (insertsWindow != nullptr)
    {
        insertsWindow->toFront (true);
        return;
    }

    auto* strip = engine.getChannelStrip (channelIndex);
    if (strip == nullptr)
        return;

    auto* processor = &strip->getInsertChain();
    auto* editor = processor->getActiveEditor();
    if (editor == nullptr)
        editor = processor->createEditorAndMakeActive();
    if (editor == nullptr)
        return;

    auto callbackWindow = std::make_unique<CallbackDocumentWindow> (
        "Channel " + juce::String (channelIndex + 1) + " -- Inserts", MentalsUI::Colours::charcoalBlack,
        juce::DocumentWindow::closeButton);
    callbackWindow->setUsingNativeTitleBar (true);
    callbackWindow->setContentOwned (editor, true);
    callbackWindow->setResizable (true, false);
    callbackWindow->centreWithSize (editor->getWidth(), editor->getHeight());
    callbackWindow->setVisible (true);
    callbackWindow->onCloseButtonPressed = [this] { insertsWindow.reset(); };
    insertsWindow = std::move (callbackWindow);
}

void ChannelStripComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    juce::ColourGradient gradient (MentalsUI::Colours::slateGray.brighter (0.05f), bounds.getTopLeft(),
                                    MentalsUI::Colours::slateGrayDark, bounds.getBottomLeft(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (MentalsUI::Colours::slateGray);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void ChannelStripComponent::resized()
{
    auto area = getLocalBounds().reduced (4);

    numberLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (2);
    insertsButton.setBounds (area.removeFromTop (20));
    insertsCountLabel.setBounds (area.removeFromTop (14));
    area.removeFromTop (4);

    auto msRow = area.removeFromTop (20);
    muteButton.setBounds (msRow.removeFromLeft (msRow.getWidth() / 2).reduced (1, 0));
    soloButton.setBounds (msRow.reduced (1, 0));
    area.removeFromTop (4);

    area.removeFromBottom (32); // room for the fader's own value textbox below it

    auto meterArea = area.removeFromRight (12);
    meter.setBounds (meterArea);
    area.removeFromRight (2);
    fader.slider.setBounds (area);
}
