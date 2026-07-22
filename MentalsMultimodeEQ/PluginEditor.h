#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MentalsUI.h"

//==============================================================================
// Colour palette: charcoal/slate core with electric-blue selection, golden
// curve accents, and a dedicated green/amber/red scale reserved for meters.
//==============================================================================
namespace EditorColours
{
    // ---- Core palette --------------------------------------------------------
    const juce::Colour charcoalBlack { 0xff1a1a1a }; // dominant background
    const juce::Colour slateGray     { 0xff44494f }; // panels, borders, inactive controls
    const juce::Colour slateGrayDark { 0xff26292d }; // recessed panels (graph/text-box backgrounds)
    const juce::Colour white         { 0xfff2f2f2 }; // text labels and values

    // ---- Accent colours --------------------------------------------------------
    const juce::Colour electricBlue  { 0xff2e9bff }; // active EQ bands / module selection
    const juce::Colour goldenYellow  { 0xffe6b800 }; // frequency markers / curve accents
    const juce::Colour emeraldGreen  { 0xff2ecc71 }; // safe levels in meters
    const juce::Colour amberOrange   { 0xffff9f1a }; // caution zone in meters
    const juce::Colour crimsonRed    { 0xffdc143c }; // clipping indicator
}

//==============================================================================
// Vertical output peak meter: emerald green below -6dB, amber caution zone
// between -6dB and 0dB, crimson above 0dB, plus a briefly-latched clip LED.
// Polls the processor on its own timer, same self-contained pattern as
// SpectrumAnalyserComponent.
//==============================================================================
class LevelMeterComponent : public juce::Component,
                             private juce::Timer
{
public:
    explicit LevelMeterComponent (MultiModeEQAudioProcessor& proc);
    ~LevelMeterComponent() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    MultiModeEQAudioProcessor& processor;
    float displayedPeakDb = -100.0f;
    bool clipping = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeterComponent)
};

//==============================================================================
// Plain visual divider between the spectrum graph and the controls below it.
// The graph always fills exactly whatever space isn't taken by the
// fixed-height sections below it (so there's never a leftover gap), which
// means this divider's position is fully determined by the window size --
// it's decorative, not draggable.
//==============================================================================
class SplitterBar : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        g.fillAll (EditorColours::slateGrayDark);

        g.setColour (EditorColours::slateGray);
        const auto bounds = getLocalBounds();
        const int cy = bounds.getCentreY();
        for (int i = -1; i <= 1; ++i)
            g.fillEllipse ((float) bounds.getCentreX() + (float) i * 10.0f - 2.0f, (float) cy - 2.0f, 4.0f, 4.0f);
    }
};

//==============================================================================
// Visual confirmation that EQ Match's "Load Reference..." actually loaded a
// file: draws its waveform (via JUCE's AudioThumbnail, which generates the
// thumbnail on a background thread and notifies via ChangeListener as more
// of it becomes available), or a placeholder message if nothing's loaded.
// Owns its own AudioFormatManager/AudioThumbnailCache -- this is a display
// concern only, entirely separate from the processor's own spectral
// analysis of the same file in loadEqMatchReferenceFile().
//==============================================================================
class ReferenceWaveformComponent : public juce::Component,
                                    private juce::ChangeListener
{
public:
    ReferenceWaveformComponent()
        : thumbnail (512, formatManager, thumbnailCache)
    {
        formatManager.registerBasicFormats();
        thumbnail.addChangeListener (this);
    }

    ~ReferenceWaveformComponent() override
    {
        thumbnail.removeChangeListener (this);
    }

    void setFile (const juce::File& file)
    {
        thumbnail.setSource (new juce::FileInputSource (file));
        repaint();
    }

    void clear()
    {
        thumbnail.clear();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (EditorColours::slateGrayDark);
        g.setColour (EditorColours::slateGray);
        g.drawRect (getLocalBounds(), 1);

        if (thumbnail.getTotalLength() > 0.0)
        {
            g.setColour (EditorColours::goldenYellow);
            thumbnail.drawChannels (g, getLocalBounds().reduced (2), 0.0, thumbnail.getTotalLength(), 1.0f);
        }
        else
        {
            g.setColour (EditorColours::slateGray);
            g.setFont (11.0f);
            g.drawText ("No reference loaded", getLocalBounds(), juce::Justification::centred);
        }
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 1 };
    juce::AudioThumbnail thumbnail;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceWaveformComponent)
};

//==============================================================================
// Live spectrum display + combined static EQ curve + per-band markers, all of
// which are always shown together on the one graph -- every band's marker is
// drawn (disabled bands dimmed), not just the enabled ones.
//
// Doubles as the "Spectrum Grab" surface:
//   - Clicking near an enabled band's marker grabs it and starts dragging
//     that band's frequency (x axis) and gain (y axis) to follow the mouse --
//     no need to select a band via a separate tab/selector first.
//   - Clicking anywhere else (not near an existing enabled band) "adds" a
//     band: it enables the nearest still-disabled band slot and drags it
//     into position, up to MultiModeEQAudioProcessor::numBands total.
// The band picked at mouseDown stays "held" for the rest of that drag
// gesture, even if the mouse ends up passing closer to a different band's
// marker along the way.
//
// Scrolling the mouse wheel anywhere on the graph adjusts the bandwidth (Q)
// of whichever enabled band's marker is nearest the cursor -- scroll up
// narrows the band (higher Q), scroll down widens it (lower Q) -- and
// selects that band the same way clicking does.
//==============================================================================
class SpectrumAnalyserComponent : public juce::Component,
                                   private juce::Timer
{
public:
    explicit SpectrumAnalyserComponent (MultiModeEQAudioProcessor& proc);
    ~SpectrumAnalyserComponent() override;

    void setSelectedBand (int bandIndex) noexcept { selectedBand = bandIndex; }

    // Called whenever a mouseDown grabs a (possibly different) band, so the
    // editor can keep its band tabs/detail panel in sync with whichever band
    // is now being dragged.
    std::function<void (int)> onBandGrabbed;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    float  frequencyToX (double freqHz) const;
    double xToFrequency (float x) const;
    float  gainToY (float gainDb) const;
    float  yToGain (float y) const;
    void   grabAt (const juce::MouseEvent& e);
    int    findNearestBand (juce::Point<float> position, bool enabledOnly) const;

    MultiModeEQAudioProcessor& processor;
    std::vector<float> magnitudesDb;
    int selectedBand = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyserComponent)
};

//==============================================================================
// One knob + label + APVTS attachment. The fill/thumb use electric blue
// since a knob is always editing the currently active/selected band.
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
    // see MultiModeEQAudioProcessorEditor's constructor for why this matters).
    void addToParent (const juce::String& labelText, juce::Component& parent)
    {
        label.setText (labelText, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, EditorColours::white);
        label.attachToComponent (&slider, false);

        slider.setColour (juce::Slider::rotarySliderFillColourId,    EditorColours::electricBlue);
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, EditorColours::slateGray);
        slider.setColour (juce::Slider::thumbColourId,               EditorColours::electricBlue);
        slider.setColour (juce::Slider::textBoxTextColourId,         EditorColours::white);
        slider.setColour (juce::Slider::textBoxBackgroundColourId,   EditorColours::slateGrayDark);
        slider.setColour (juce::Slider::textBoxOutlineColourId,      EditorColours::slateGray);

        parent.addAndMakeVisible (slider);
        parent.addAndMakeVisible (label);
    }

    // (Re)binds this knob to a parameter -- used both for the first bind and
    // whenever the selected-band tab changes to a different band.
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

//==============================================================================
class MultiModeEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Button::Listener,
                                         private juce::ComboBox::Listener,
                                         private juce::Timer
{
public:
    explicit MultiModeEQAudioProcessorEditor (MultiModeEQAudioProcessor&);
    ~MultiModeEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void timerCallback() override;

    // JUCE's Standalone app window deliberately omits the maximize button
    // (it only requests minimise + close). Once this editor is attached to
    // that window, add maximize back in ourselves -- has no effect when
    // hosted as a plugin inside a DAW, since there the window chrome
    // (including whether a maximize button exists at all) is entirely
    // host-controlled, not something a plugin can add to.
    void parentHierarchyChanged() override;

    void selectBand (int bandIndex);
    void rebuildAttachmentsForSelectedBand();
    void updateBandControlVisibility();
    void updateMidiLearnStatusLabel();
    void updateAiAssistStatusLabel();
    void updateEqMatchStatusLabel();
    void showPresetsMenu();
    void promptToSavePreset();
    void showSettingsPanel();
    void layoutSettingsPanelContent();
    void showAiAssistPanel();
    void layoutAiAssistPanelContent();
    void showEqMatchPanel();
    void layoutEqMatchPanelContent();

    MultiModeEQAudioProcessor& processor;
    int selectedBandIndex = 0;

    MentalsUI::HardwareLookAndFeel hardwareLookAndFeel;

    // Set in resized(), read back in paint() so the metal-panel texture,
    // corner screws, and rack ears are drawn over exactly the same area the
    // knobs sit in.
    juce::Rectangle<int> lastPanelBounds;

    //==========================================================================
    // Top bar: compact logo (top left), and preset select/save to its right.
    //==========================================================================
    MentalsUI::MetallicLogoComponent logoImage;

    // Opens a folder-style PopupMenu (see showPresetsMenu()) rather than a
    // flat ComboBox list -- factory presets are grouped into category
    // submenus (DRUMS, BASS, ...), rebuilt fresh from
    // MultiModeEQAudioProcessor::getFactoryPresetCategories() every time
    // it's opened, so there's no separate list to keep in sync.
    juce::TextButton presetsButton { "Presets" };
    juce::TextButton presetSaveButton { "Save" };

    // Auto Gain / Phase Mode / MIDI Learn used to be a whole extra row at the
    // bottom of the window; they now live in this panel, shown as a popup
    // (see showSettingsPanel()) anchored to settingsButton -- freeing that
    // space lets the graph/tabs/band panel below fill the window properly
    // instead of leaving a gap.
    juce::TextButton settingsButton { "Tools" };
    juce::Component settingsPanelContent;

    // AI Assist gets its own dedicated top-bar button (right of Tools)
    // rather than living inside the Tools popup, since it's the plugin's
    // headline feature -- shown as its own popup (see showAiAssistPanel()).
    juce::TextButton aiAssistButton { "AI Assist" };
    juce::Component aiAssistPanelContent;

    // EQ Match also gets its own top-bar button (right of AI Assist) rather
    // than living inside the Tools popup -- shown as its own popup (see
    // showEqMatchPanel()).
    juce::TextButton eqMatchButton { "EQ Match" };
    juce::Component eqMatchPanelContent;

    SpectrumAnalyserComponent analyser;

    // Purely decorative now -- the graph always fills exactly whatever space
    // isn't taken by the fixed-height sections below it, so there's never a
    // leftover gap (see SplitterBar's class comment).
    SplitterBar analyserSplitter;

    //==========================================================================
    // Band tabs (one toggle button per band, "1".."numBands").
    //==========================================================================
    std::array<juce::TextButton, MultiModeEQAudioProcessor::numBands> bandTabs;

    //==========================================================================
    // Selected-band detail panel. Attachments are rebuilt (not recreated as
    // new components) whenever the selected band tab changes.
    //==========================================================================
    juce::ToggleButton enabledToggle { "Enabled" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment;

    juce::Label modeLabel, channelLabel, filterShapeLabel, slopeLabel;
    juce::ComboBox modeSelector, channelSelector, filterShapeSelector, slopeSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, channelAttachment,
        filterShapeAttachment, slopeAttachment;

    LabelledSlider freqSlider, gainSlider, qSlider;
    juce::Label outputMeterLabel;
    LevelMeterComponent outputMeter; // sits right after Q in the knob row
    LabelledSlider thresholdSlider, ratioSlider, attackSlider, releaseSlider;

    juce::ToggleButton sidechainToggle { "Use Sidechain" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sidechainAttachment;

    //==========================================================================
    // Global controls: Auto Gain, Phase Mode, MIDI Learn. Children of
    // settingsPanelContent (shown in the Tools popup), not of the editor
    // directly.
    //==========================================================================
    juce::ToggleButton autoGainToggle { "Auto Gain" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment;

    juce::Label phaseModeLabel;
    juce::ComboBox phaseModeSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> phaseModeAttachment;

    juce::Label midiLearnLabel, midiLearnStatusLabel;
    juce::ComboBox midiLearnTargetSelector;
    juce::TextButton midiLearnButton { "Learn" };
    juce::TextButton midiLearnClearAllButton { "Clear All" };

    // AI Assist: rule-based spectral-balance/resonance analysis of the live
    // input, offering to configure a handful of currently-unused bands (see
    // MultiModeEQAudioProcessor::applyAiAssistSuggestions()'s comment for why
    // this isn't a neural-network model). Children of aiAssistPanelContent
    // (shown in its own popup), not of settingsPanelContent.
    juce::Label aiAssistLabel, aiAssistStatusLabel;
    juce::TextButton aiAssistAnalyseButton { "Analyze" };
    juce::TextButton aiAssistApplyButton   { "Apply Suggestions" };
    juce::TextButton aiAssistUndoButton    { "Undo" };
    bool aiAssistWasCapturing = false; // edge-detects capture-just-finished, to flip the status label once

    // EQ Match: analyse a reference file's spectrum and adjust band gains to
    // approach it. Children of eqMatchPanelContent (shown in its own popup),
    // not of settingsPanelContent.
    juce::Label eqMatchLabel, eqMatchStatusLabel;
    juce::TextButton eqMatchCaptureButton  { "Capture Current" };
    juce::TextButton eqMatchLoadRefButton  { "Load Reference..." };
    juce::TextButton eqMatchApplyButton    { "Apply Match" };
    juce::TextButton eqMatchCancelButton   { "Cancel" };
    std::unique_ptr<juce::FileChooser> activeFileChooser;
    ReferenceWaveformComponent eqMatchWaveform;
    juce::String lastLoadedReferenceFileName;
    bool lastEqMatchLoadFailed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiModeEQAudioProcessorEditor)
};
