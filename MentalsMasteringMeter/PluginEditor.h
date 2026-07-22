#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"
#include <vector>

//==============================================================================
// Scrolling loudness history: Momentary (thin, jittery) and Short-Term
// (bold, smoothed) traces over the last ~60 seconds, plus a reference line
// at the current Target -- real metering read straight from the
// processor's ring buffer (see copyRecentLoudnessHistory()), not an
// illustration.
//==============================================================================
class LoudnessHistoryComponent : public juce::Component,
                                  private juce::Timer
{
public:
    explicit LoudnessHistoryComponent (MentalsMasteringMeterAudioProcessor& proc)
        : processor (proc)
    {
        startTimerHz (10);
    }

    ~LoudnessHistoryComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }

    MentalsMasteringMeterAudioProcessor& processor;
    static constexpr int historyPoints = 600; // 60 seconds at 100ms resolution

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessHistoryComponent)
};

//==============================================================================
// One big numeric readout: a title label above a large value display,
// colour-tinted (e.g. red when a peak reading is over 0). Polls its own
// callback on a timer, the same self-contained pattern as
// MentalsUI::LevelMeterComponent.
//==============================================================================
class NumericReadoutComponent : public juce::Component,
                                 private juce::Timer
{
public:
    NumericReadoutComponent (juce::String titleIn, std::function<float()> getValueIn, juce::String suffixIn,
                              std::function<bool (float)> isWarningIn = nullptr)
        : title (std::move (titleIn)), getValue (std::move (getValueIn)), suffix (std::move (suffixIn)),
          isWarning (std::move (isWarningIn))
    {
        startTimerHz (10);
    }

    ~NumericReadoutComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override
    {
        displayedValue = getValue ? getValue() : 0.0f;
        repaint();
    }

    juce::String title;
    std::function<float()> getValue;
    juce::String suffix;
    std::function<bool (float)> isWarning;
    float displayedValue = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NumericReadoutComponent)
};

//==============================================================================
class MentalsMasteringMeterAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                   private juce::Button::Listener,
                                                   private juce::ComboBox::Listener
{
public:
    explicit MentalsMasteringMeterAudioProcessorEditor (MentalsMasteringMeterAudioProcessor&);
    ~MentalsMasteringMeterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override {}
    void parentHierarchyChanged() override { MentalsUI::enableMaximiseButtonIfStandalone (*this); }

    MentalsMasteringMeterAudioProcessor& processor;

    MentalsUI::HardwareLookAndFeel hardwareLookAndFeel;

    // Set in resized(), read back in paint() so the metal-panel texture,
    // corner screws, and rack ears are drawn over exactly the same area the
    // knobs sit in.
    juce::Rectangle<int> lastPanelBounds;

    MentalsUI::MetallicLogoComponent logoImage;
    juce::Label productNameLabel;
    juce::Label targetLabel;
    juce::ComboBox targetSelector;
    juce::TextButton resetButton { "Reset" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment;

    LoudnessHistoryComponent history;
    MentalsUI::SplitterBar splitter;

    NumericReadoutComponent momentaryReadout, shortTermReadout, integratedReadout, lraReadout, truePeakReadout, samplePeakReadout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsMasteringMeterAudioProcessorEditor)
};
