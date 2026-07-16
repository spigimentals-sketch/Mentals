#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MultiModeEQBinaryData.h"
#include <limits>
#include <algorithm>
#include <cmath>

namespace
{
    juce::String midiLearnTargetParamSuffix (int itemId)
    {
        switch (itemId)
        {
            case 1: return "freq";
            case 2: return "gain";
            case 3: return "q";
            case 4: return "threshold";
            case 5: return "ratio";
            case 6: return "attack";
            case 7: return "release";
            case 8: return "enabled";
            default: return "gain";
        }
    }

    struct SpectrumPeak { double freqHz; float db; };

    // Finds the most prominent local maxima in the (already-smoothed) live
    // spectrum, for labelling on the graph. Requires a small local-maximum
    // window (not just immediate neighbours) to avoid single noisy bins
    // being picked, and merges candidates that are close in log-frequency so
    // one spectral "hump" doesn't produce several overlapping labels.
    std::vector<SpectrumPeak> findSpectrumPeaks (const std::vector<float>& magnitudesDb, double sampleRate,
                                                  int fftSize, int maxPeaks)
    {
        constexpr float minDb = -60.0f;
        std::vector<SpectrumPeak> candidates;

        for (int bin = 2; bin < (int) magnitudesDb.size() - 2; ++bin)
        {
            const float v = magnitudesDb[(size_t) bin];
            if (v < minDb)
                continue;
            if (v < magnitudesDb[(size_t) bin - 1] || v < magnitudesDb[(size_t) bin + 1]
                || v < magnitudesDb[(size_t) bin - 2] || v < magnitudesDb[(size_t) bin + 2])
                continue;

            const double freq = (double) bin * sampleRate / (double) fftSize;
            if (freq < 20.0 || freq > 20000.0)
                continue;

            candidates.push_back ({ freq, v });
        }

        std::sort (candidates.begin(), candidates.end(), [] (auto& a, auto& b) { return a.db > b.db; });

        std::vector<SpectrumPeak> kept;
        for (auto& p : candidates)
        {
            bool tooClose = false;
            for (auto& k : kept)
            {
                if (std::abs (std::log10 (p.freqHz) - std::log10 (k.freqHz)) < 0.05) // ~12% frequency ratio apart
                {
                    tooClose = true;
                    break;
                }
            }

            if (! tooClose)
            {
                kept.push_back (p);
                if ((int) kept.size() >= maxPeaks)
                    break;
            }
        }

        return kept;
    }

    juce::String formatFrequencyLabel (double freqHz)
    {
        if (freqHz >= 1000.0)
            return juce::String (freqHz / 1000.0, freqHz >= 10000.0 ? 1 : 2) + " kHz";
        return juce::String ((int) std::round (freqHz)) + " Hz";
    }
}

//==============================================================================
// LevelMeterComponent
//==============================================================================
LevelMeterComponent::LevelMeterComponent (MultiModeEQAudioProcessor& proc)
    : processor (proc)
{
    startTimerHz (30);
}

LevelMeterComponent::~LevelMeterComponent()
{
    stopTimer();
}

void LevelMeterComponent::timerCallback()
{
    displayedPeakDb = processor.getOutputPeakDb();
    clipping = processor.isOutputClipping();
    repaint();
}

void LevelMeterComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Reserve a small square at the top for the clip LED, the rest is the bar.
    auto clipLedArea = bounds.removeFromTop (juce::jmin (14.0f, bounds.getHeight() * 0.15f)).reduced (2.0f);
    bounds.removeFromTop (2.0f);

    g.setColour (clipping ? EditorColours::crimsonRed : EditorColours::slateGrayDark);
    g.fillRoundedRectangle (clipLedArea, 2.0f);

    g.setColour (EditorColours::slateGrayDark);
    g.fillRoundedRectangle (bounds, 3.0f);

    constexpr float minDb = -48.0f, maxDb = 6.0f;   // a little headroom above 0dB to show clipping clearly
    constexpr float safeCeilingDb = -6.0f;          // emerald below this
    constexpr float cautionCeilingDb = 0.0f;        // amber between safeCeiling and 0dB; crimson above

    auto dbToY = [&] (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
        return bounds.getBottom() - t * bounds.getHeight();
    };

    const float peakY    = dbToY (juce::jlimit (minDb, maxDb, displayedPeakDb));
    const float safeY    = dbToY (safeCeilingDb);
    const float cautionY = dbToY (cautionCeilingDb);

    // Only the portion of the bar from the current peak down to the bottom
    // is "lit"; clipping to that region and drawing the three full-height
    // colour zones inside it gives the classic segmented meter look.
    juce::Rectangle<float> lit (bounds.getX(), peakY, bounds.getWidth(), bounds.getBottom() - peakY);

    g.saveState();
    g.reduceClipRegion (lit.getSmallestIntegerContainer());

    g.setColour (EditorColours::emeraldGreen);
    g.fillRect (juce::Rectangle<float> (bounds.getX(), safeY, bounds.getWidth(), bounds.getBottom() - safeY));

    g.setColour (EditorColours::amberOrange);
    g.fillRect (juce::Rectangle<float> (bounds.getX(), cautionY, bounds.getWidth(), safeY - cautionY));

    g.setColour (EditorColours::crimsonRed);
    g.fillRect (juce::Rectangle<float> (bounds.getX(), bounds.getY(), bounds.getWidth(), cautionY - bounds.getY()));

    g.restoreState();

    g.setColour (EditorColours::slateGray);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
}

//==============================================================================
// SpectrumAnalyserComponent
//==============================================================================
SpectrumAnalyserComponent::SpectrumAnalyserComponent (MultiModeEQAudioProcessor& proc)
    : processor (proc)
{
    magnitudesDb.assign ((size_t) MultiModeEQAudioProcessor::spectrumNumBins, -100.0f);
    startTimerHz (30);
}

SpectrumAnalyserComponent::~SpectrumAnalyserComponent()
{
    stopTimer();
}

void SpectrumAnalyserComponent::timerCallback()
{
    processor.getSpectrumMagnitudesDb (magnitudesDb.data());
    repaint();
}

float SpectrumAnalyserComponent::frequencyToX (double freqHz) const
{
    constexpr double minFreq = 20.0, maxFreq = 20000.0;
    const double logMin = std::log10 (minFreq), logMax = std::log10 (maxFreq);
    const double t = (std::log10 (juce::jlimit (minFreq, maxFreq, freqHz)) - logMin) / (logMax - logMin);
    return (float) (t * getWidth());
}

double SpectrumAnalyserComponent::xToFrequency (float x) const
{
    constexpr double minFreq = 20.0, maxFreq = 20000.0;
    const double logMin = std::log10 (minFreq), logMax = std::log10 (maxFreq);
    const double t = juce::jlimit (0.0, 1.0, (double) x / (double) juce::jmax (1, getWidth()));
    return std::pow (10.0, logMin + t * (logMax - logMin));
}

float SpectrumAnalyserComponent::gainToY (float gainDb) const
{
    constexpr float minDb = -24.0f, maxDb = 24.0f;
    const float t = (juce::jlimit (minDb, maxDb, gainDb) - minDb) / (maxDb - minDb);
    return (float) getHeight() * (1.0f - t);
}

float SpectrumAnalyserComponent::yToGain (float y) const
{
    constexpr float minDb = -24.0f, maxDb = 24.0f;
    const float t = 1.0f - juce::jlimit (0.0f, 1.0f, y / (float) juce::jmax (1, getHeight()));
    return minDb + t * (maxDb - minDb);
}

int SpectrumAnalyserComponent::findNearestBand (juce::Point<float> position, bool enabledOnly) const
{
    int best = -1;
    float bestDistance = std::numeric_limits<float>::max();

    for (int i = 0; i < MultiModeEQAudioProcessor::numBands; ++i)
    {
        auto& band = processor.bands[(size_t) i];
        if (enabledOnly && ! band.isEnabled())
            continue;
        if (! enabledOnly && band.isEnabled())
            continue;

        const juce::Point<float> markerPos { frequencyToX (band.freqParam->get()), gainToY (band.gainParam->get()) };
        const float distance = position.getDistanceFrom (markerPos);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

void SpectrumAnalyserComponent::mouseDown (const juce::MouseEvent& e)
{
    // Grabbing an existing band only counts if the click actually landed
    // near its marker; anything further away is treated as "empty space" and
    // adds a new band there instead (up to numBands total).
    constexpr float grabRadiusPixels = 26.0f;

    const int nearestEnabled = findNearestBand (e.position, true);
    const bool clickedNearExistingBand = nearestEnabled >= 0
        && e.position.getDistanceFrom ({ frequencyToX (processor.bands[(size_t) nearestEnabled].freqParam->get()),
                                          gainToY (processor.bands[(size_t) nearestEnabled].gainParam->get()) })
           <= grabRadiusPixels;

    if (clickedNearExistingBand)
    {
        selectedBand = nearestEnabled;
    }
    else
    {
        const int nearestDisabled = findNearestBand (e.position, false);
        if (nearestDisabled >= 0)
        {
            // "Add a band": enable the nearest free slot and drop it exactly
            // where the user clicked.
            processor.setBandEnabled (nearestDisabled, true);
            selectedBand = nearestDisabled;
        }
        else if (nearestEnabled >= 0)
        {
            selectedBand = nearestEnabled; // every band is already enabled -- fall back to nearest
        }
    }

    if (onBandGrabbed)
        onBandGrabbed (selectedBand);

    grabAt (e);
}

void SpectrumAnalyserComponent::mouseDrag (const juce::MouseEvent& e) { grabAt (e); }

void SpectrumAnalyserComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int nearest = findNearestBand (e.position, true);
    if (nearest < 0)
        return;

    if (nearest != selectedBand)
    {
        selectedBand = nearest;
        if (onBandGrabbed)
            onBandGrabbed (selectedBand);
    }

    processor.adjustBandBandwidth (selectedBand, wheel.deltaY);
}

void SpectrumAnalyserComponent::grabAt (const juce::MouseEvent& e)
{
    const double freq = xToFrequency (e.position.x);
    const float  gain = yToGain (e.position.y);
    processor.spectrumGrab (selectedBand, (float) freq, gain);
}

void SpectrumAnalyserComponent::paint (juce::Graphics& g)
{
    g.fillAll (EditorColours::slateGrayDark);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    for (double f : { 100.0, 1000.0, 10000.0 })
        g.drawVerticalLine ((int) frequencyToX (f), 0.0f, (float) getHeight());

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine ((int) gainToY (0.0f), 0.0f, (float) getWidth());

    // ---- Live spectrum, drawn as a filled waveform (its own internal
    // ~-100..0dB scale, independent of the +/-24dB EQ curve below) -----------
    if (! magnitudesDb.empty())
    {
        const double sampleRate = processor.getCurrentSampleRate();

        juce::Path spectrumLine;
        bool started = false;
        float firstX = 0.0f, lastX = 0.0f;

        for (int bin = 1; bin < (int) magnitudesDb.size(); ++bin)
        {
            const double freq = (double) bin * sampleRate / (double) MultiModeEQAudioProcessor::spectrumFftSize;
            if (freq < 20.0 || freq > 20000.0)
                continue;

            const float x = frequencyToX (freq);
            const float normalised = juce::jlimit (0.0f, 1.0f, (magnitudesDb[(size_t) bin] + 100.0f) / 100.0f);
            const float y = (float) getHeight() * (1.0f - normalised);

            if (! started) { spectrumLine.startNewSubPath (x, y); started = true; firstX = x; }
            else            spectrumLine.lineTo (x, y);

            lastX = x;
        }

        if (started)
        {
            // Filled area under the curve gives the "audio wave" mountain
            // look, fading out towards the bottom rather than a flat fill.
            juce::Path filledWave (spectrumLine);
            filledWave.lineTo (lastX, (float) getHeight());
            filledWave.lineTo (firstX, (float) getHeight());
            filledWave.closeSubPath();

            juce::ColourGradient gradient (EditorColours::goldenYellow.withAlpha (0.45f), 0.0f, 0.0f,
                                            EditorColours::goldenYellow.withAlpha (0.02f), 0.0f, (float) getHeight(), false);
            g.setGradientFill (gradient);
            g.fillPath (filledWave);

            g.setColour (EditorColours::goldenYellow.withAlpha (0.85f));
            g.strokePath (spectrumLine, juce::PathStrokeType (1.5f));
        }

        // ---- Peak labels: frequency + level of the most prominent peaks ----
        const auto peaks = findSpectrumPeaks (magnitudesDb, sampleRate, MultiModeEQAudioProcessor::spectrumFftSize, 5);
        g.setFont (11.0f);

        for (auto& peak : peaks)
        {
            const float x = frequencyToX (peak.freqHz);
            const float normalised = juce::jlimit (0.0f, 1.0f, (peak.db + 100.0f) / 100.0f);
            const float y = (float) getHeight() * (1.0f - normalised);

            g.setColour (juce::Colours::white);
            g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);

            const juce::String label = formatFrequencyLabel (peak.freqHz) + "  " + juce::String (peak.db, 1) + " dB";
            constexpr int labelWidth = 100;
            const float labelX = juce::jlimit (0.0f, (float) getWidth() - labelWidth, x - labelWidth * 0.5f);
            const bool roomAbove = y > 16.0f;
            const float labelY = roomAbove ? y - 16.0f : y + 4.0f;

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (labelX, labelY, (float) labelWidth, 13.0f, 3.0f);
            g.setColour (juce::Colours::white);
            g.drawText (label, (int) labelX, (int) labelY, labelWidth, 13, juce::Justification::centred);
        }
    }

    // ---- Combined static EQ curve (StereoOrAll, non-dynamic bands) ---------
    // Reads each band's previousCoefficients directly from the audio thread's
    // last-computed value without a lock; worst case is one visually stale
    // frame of the curve, which is an accepted, common trade-off for a
    // display-only read like this.
    {
        juce::Path curvePath;
        const double sampleRate = processor.getCurrentSampleRate();
        const int numPoints = juce::jmax (2, getWidth());

        for (int i = 0; i < numPoints; ++i)
        {
            const double freq = xToFrequency ((float) i);

            double totalMagnitude = 1.0;
            for (auto& band : processor.bands)
            {
                if (! band.isEnabled())
                    continue;
                totalMagnitude *= band.getMagnitudeForFrequency (freq, sampleRate);
            }

            const float gainDb = (float) (20.0 * std::log10 (juce::jmax (1.0e-6, totalMagnitude)));
            const float y = gainToY (gainDb);

            if (i == 0) curvePath.startNewSubPath ((float) i, y);
            else        curvePath.lineTo ((float) i, y);
        }

        g.setColour (EditorColours::goldenYellow);
        g.strokePath (curvePath, juce::PathStrokeType (2.0f));
    }

    // ---- Per-band markers -----------------------------------------------------
    // Every band slot gets a marker, not just the enabled ones, so a
    // still-unused band can be seen (dimly) and clicked on to add it -- see
    // mouseDown()'s "click empty space to add a band" behaviour.
    for (int i = 0; i < MultiModeEQAudioProcessor::numBands; ++i)
    {
        auto& band = processor.bands[(size_t) i];
        const bool enabled    = band.isEnabled();
        const bool isSelected = (i == selectedBand);

        const float x = frequencyToX (band.freqParam->get());
        const float y = gainToY (band.gainParam->get());
        const float r = isSelected ? 9.0f : (enabled ? 7.0f : 5.0f);

        if (enabled)
        {
            // A white outline behind the fill keeps the dot readable
            // regardless of what part of the curve/spectrum it's sitting on.
            // Selected (the active module) is electric blue; other enabled
            // bands are golden yellow, matching their role as frequency
            // markers on the curve.
            g.setColour (juce::Colours::white);
            g.fillEllipse (x - r - 1.5f, y - r - 1.5f, (r + 1.5f) * 2.0f, (r + 1.5f) * 2.0f);

            g.setColour (isSelected ? EditorColours::electricBlue : EditorColours::goldenYellow);
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);

            g.setColour (juce::Colours::black);
            g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
            g.drawText (juce::String (i + 1), (int) x - 10, (int) y - 9, 20, 18, juce::Justification::centred);
        }
        else
        {
            // Hollow ring for an unused slot -- clickable to add a band there.
            g.setColour (EditorColours::slateGray.withAlpha (0.8f));
            g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 2.0f);
        }
    }
}

//==============================================================================
// MultiModeEQAudioProcessorEditor
//==============================================================================
MultiModeEQAudioProcessorEditor::MultiModeEQAudioProcessorEditor (MultiModeEQAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), analyser (p), outputMeter (p)
{
    // ---- Top bar: logo (left) + preset select/save (right of logo) -----------
    logoImage.setImage (juce::ImageFileFormat::loadFrom (MultiModeEQBinaryData::logo_png, (size_t) MultiModeEQBinaryData::logo_pngSize));
    logoImage.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logoImage);

    presetSelector.setTextWhenNothingSelected ("Presets");
    presetSelector.setColour (juce::ComboBox::backgroundColourId, EditorColours::slateGrayDark);
    presetSelector.setColour (juce::ComboBox::textColourId,       EditorColours::white);
    presetSelector.setColour (juce::ComboBox::outlineColourId,    EditorColours::slateGray);
    presetSelector.setColour (juce::ComboBox::arrowColourId,      EditorColours::white);
    addAndMakeVisible (presetSelector);
    presetSelector.addListener (this);
    refreshPresetList();

    presetSaveButton.setColour (juce::TextButton::buttonColourId,  EditorColours::slateGrayDark);
    presetSaveButton.setColour (juce::TextButton::textColourOffId, EditorColours::white);
    addAndMakeVisible (presetSaveButton);
    presetSaveButton.addListener (this);

    settingsButton.setColour (juce::TextButton::buttonColourId,  EditorColours::slateGrayDark);
    settingsButton.setColour (juce::TextButton::textColourOffId, EditorColours::white);
    addAndMakeVisible (settingsButton);
    settingsButton.addListener (this);

    aiAssistButton.setColour (juce::TextButton::buttonColourId,  EditorColours::electricBlue);
    aiAssistButton.setColour (juce::TextButton::textColourOffId, EditorColours::white);
    addAndMakeVisible (aiAssistButton);
    aiAssistButton.addListener (this);

    eqMatchButton.setColour (juce::TextButton::buttonColourId,  EditorColours::slateGrayDark);
    eqMatchButton.setColour (juce::TextButton::textColourOffId, EditorColours::white);
    addAndMakeVisible (eqMatchButton);
    eqMatchButton.addListener (this);

    addAndMakeVisible (analyser);
    // Grabbing a band directly on the graph (see SpectrumAnalyserComponent's
    // class comment) should keep the tabs/detail panel in sync with whichever
    // band just got grabbed, without requiring the user to click its tab
    // first. This only assigns the callback -- it isn't invoked until an
    // actual mouseDown happens later, well after construction finishes.
    analyser.onBandGrabbed = [this] (int bandIndex) { selectBand (bandIndex); };

    addAndMakeVisible (analyserSplitter);

    // ---- Band tabs ------------------------------------------------------------
    for (int i = 0; i < MultiModeEQAudioProcessor::numBands; ++i)
    {
        auto& tab = bandTabs[(size_t) i];
        tab.setButtonText (juce::String (i + 1));
        tab.setColour (juce::TextButton::buttonColourId, EditorColours::slateGrayDark);
        tab.setColour (juce::TextButton::textColourOffId, EditorColours::white);
        addAndMakeVisible (tab);
        tab.addListener (this);
    }

    // ---- Selected-band detail panel (visual setup only; no attachments yet) --
    enabledToggle.setColour (juce::ToggleButton::textColourId, EditorColours::white);
    enabledToggle.setColour (juce::ToggleButton::tickColourId, EditorColours::white);
    addAndMakeVisible (enabledToggle);

    modeLabel.setText ("Mode", juce::dontSendNotification);
    modeLabel.setColour (juce::Label::textColourId, EditorColours::white);
    addAndMakeVisible (modeLabel);
    modeSelector.addItem ("Parametric", 1);
    modeSelector.addItem ("Dynamic",    2);
    modeSelector.setColour (juce::ComboBox::backgroundColourId, EditorColours::slateGrayDark);
    modeSelector.setColour (juce::ComboBox::textColourId,       EditorColours::white);
    modeSelector.setColour (juce::ComboBox::outlineColourId,    EditorColours::slateGray);
    modeSelector.setColour (juce::ComboBox::arrowColourId,      EditorColours::white);
    addAndMakeVisible (modeSelector);
    modeSelector.addListener (this);

    channelLabel.setText ("Channel", juce::dontSendNotification);
    channelLabel.setColour (juce::Label::textColourId, EditorColours::white);
    addAndMakeVisible (channelLabel);
    channelSelector.addItem ("Stereo", 1);
    channelSelector.addItem ("Mid",    2);
    channelSelector.addItem ("Side",   3);
    channelSelector.addItem ("Left",   4);
    channelSelector.addItem ("Right",  5);
    channelSelector.setColour (juce::ComboBox::backgroundColourId, EditorColours::slateGrayDark);
    channelSelector.setColour (juce::ComboBox::textColourId,       EditorColours::white);
    channelSelector.setColour (juce::ComboBox::outlineColourId,    EditorColours::slateGray);
    channelSelector.setColour (juce::ComboBox::arrowColourId,      EditorColours::white);
    addAndMakeVisible (channelSelector);

    filterShapeLabel.setText ("Shape", juce::dontSendNotification);
    filterShapeLabel.setColour (juce::Label::textColourId, EditorColours::white);
    addAndMakeVisible (filterShapeLabel);
    filterShapeSelector.addItem ("Bell",                    1);
    filterShapeSelector.addItem ("High Pass (Low Cut)",     2);
    filterShapeSelector.addItem ("Low Pass (High Cut)",     3);
    filterShapeSelector.addItem ("Low Shelf",               4);
    filterShapeSelector.addItem ("High Shelf",              5);
    filterShapeSelector.setColour (juce::ComboBox::backgroundColourId, EditorColours::slateGrayDark);
    filterShapeSelector.setColour (juce::ComboBox::textColourId,       EditorColours::white);
    filterShapeSelector.setColour (juce::ComboBox::outlineColourId,    EditorColours::slateGray);
    filterShapeSelector.setColour (juce::ComboBox::arrowColourId,      EditorColours::white);
    addAndMakeVisible (filterShapeSelector);
    filterShapeSelector.addListener (this);

    slopeLabel.setText ("Slope", juce::dontSendNotification);
    slopeLabel.setColour (juce::Label::textColourId, EditorColours::white);
    addAndMakeVisible (slopeLabel);
    slopeSelector.addItem ("12 dB/oct", 1);
    slopeSelector.addItem ("24 dB/oct", 2);
    slopeSelector.addItem ("36 dB/oct", 3);
    slopeSelector.addItem ("48 dB/oct", 4);
    slopeSelector.setColour (juce::ComboBox::backgroundColourId, EditorColours::slateGrayDark);
    slopeSelector.setColour (juce::ComboBox::textColourId,       EditorColours::white);
    slopeSelector.setColour (juce::ComboBox::outlineColourId,    EditorColours::slateGray);
    slopeSelector.setColour (juce::ComboBox::arrowColourId,      EditorColours::white);
    addAndMakeVisible (slopeSelector);

    freqSlider.addToParent      ("Frequency", *this);
    gainSlider.addToParent      ("Gain",      *this);
    qSlider.addToParent         ("Q",         *this);

    // Output meter sits right after Q in the knob row.
    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, EditorColours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    thresholdSlider.addToParent ("Threshold", *this);
    ratioSlider.addToParent     ("Ratio",     *this);
    attackSlider.addToParent    ("Attack",    *this);
    releaseSlider.addToParent   ("Release",   *this);
    freqSlider.slider.setSkewFactorFromMidPoint (1000.0);

    sidechainToggle.setColour (juce::ToggleButton::textColourId, EditorColours::white);
    sidechainToggle.setColour (juce::ToggleButton::tickColourId, EditorColours::white);
    addAndMakeVisible (sidechainToggle);

    // ---- Settings popup content (Auto Gain / Phase Mode / EQ Match / MIDI
    // Learn) -- these are children of settingsPanelContent, not of the editor
    // directly, since they're only ever shown inside the Settings popup.
    autoGainToggle.setColour (juce::ToggleButton::textColourId, EditorColours::white);
    autoGainToggle.setColour (juce::ToggleButton::tickColourId, EditorColours::white);
    settingsPanelContent.addAndMakeVisible (autoGainToggle);

    phaseModeLabel.setText ("Phase", juce::dontSendNotification);
    phaseModeLabel.setColour (juce::Label::textColourId, EditorColours::white);
    settingsPanelContent.addAndMakeVisible (phaseModeLabel);
    phaseModeSelector.addItem ("Zero Latency",  1);
    phaseModeSelector.addItem ("Natural Phase", 2);
    phaseModeSelector.setColour (juce::ComboBox::backgroundColourId, EditorColours::slateGrayDark);
    phaseModeSelector.setColour (juce::ComboBox::textColourId,       EditorColours::white);
    phaseModeSelector.setColour (juce::ComboBox::outlineColourId,    EditorColours::slateGray);
    phaseModeSelector.setColour (juce::ComboBox::arrowColourId,      EditorColours::white);
    settingsPanelContent.addAndMakeVisible (phaseModeSelector);

    midiLearnLabel.setText ("MIDI Learn", juce::dontSendNotification);
    midiLearnLabel.setColour (juce::Label::textColourId, EditorColours::white);
    settingsPanelContent.addAndMakeVisible (midiLearnLabel);

    midiLearnTargetSelector.addItem ("Frequency", 1);
    midiLearnTargetSelector.addItem ("Gain",      2);
    midiLearnTargetSelector.addItem ("Q",         3);
    midiLearnTargetSelector.addItem ("Threshold", 4);
    midiLearnTargetSelector.addItem ("Ratio",     5);
    midiLearnTargetSelector.addItem ("Attack",    6);
    midiLearnTargetSelector.addItem ("Release",   7);
    midiLearnTargetSelector.addItem ("Enabled",   8);
    midiLearnTargetSelector.setSelectedId (1, juce::dontSendNotification);
    midiLearnTargetSelector.setColour (juce::ComboBox::backgroundColourId, EditorColours::slateGrayDark);
    midiLearnTargetSelector.setColour (juce::ComboBox::textColourId,       EditorColours::white);
    midiLearnTargetSelector.setColour (juce::ComboBox::outlineColourId,    EditorColours::slateGray);
    midiLearnTargetSelector.setColour (juce::ComboBox::arrowColourId,      EditorColours::white);
    settingsPanelContent.addAndMakeVisible (midiLearnTargetSelector);

    for (auto* b : { &midiLearnButton, &midiLearnClearAllButton })
    {
        b->setColour (juce::TextButton::buttonColourId,  EditorColours::slateGrayDark);
        b->setColour (juce::TextButton::textColourOffId, EditorColours::white);
        settingsPanelContent.addAndMakeVisible (*b);
        b->addListener (this);
    }

    midiLearnStatusLabel.setColour (juce::Label::textColourId, EditorColours::slateGray);
    midiLearnStatusLabel.setText ("Not learning", juce::dontSendNotification);
    settingsPanelContent.addAndMakeVisible (midiLearnStatusLabel);

    settingsPanelContent.setSize (340, 270);
    layoutSettingsPanelContent();

    // ---- AI Assist popup content (its own popup, not part of Tools) -----------
    aiAssistLabel.setText ("AI Assist", juce::dontSendNotification);
    aiAssistLabel.setColour (juce::Label::textColourId, EditorColours::white);
    aiAssistPanelContent.addAndMakeVisible (aiAssistLabel);

    for (auto* b : { &aiAssistAnalyseButton, &aiAssistApplyButton, &aiAssistUndoButton })
    {
        b->setColour (juce::TextButton::buttonColourId,  EditorColours::slateGrayDark);
        b->setColour (juce::TextButton::textColourOffId, EditorColours::white);
        aiAssistPanelContent.addAndMakeVisible (*b);
        b->addListener (this);
    }

    aiAssistStatusLabel.setColour (juce::Label::textColourId, EditorColours::slateGray);
    aiAssistStatusLabel.setText ("Not analysed", juce::dontSendNotification);
    aiAssistPanelContent.addAndMakeVisible (aiAssistStatusLabel);

    aiAssistPanelContent.setSize (340, 100);
    layoutAiAssistPanelContent();

    // ---- EQ Match popup content (its own popup, not part of Tools) ------------
    eqMatchLabel.setText ("EQ Match", juce::dontSendNotification);
    eqMatchLabel.setColour (juce::Label::textColourId, EditorColours::white);
    eqMatchPanelContent.addAndMakeVisible (eqMatchLabel);

    for (auto* b : { &eqMatchCaptureButton, &eqMatchLoadRefButton, &eqMatchApplyButton, &eqMatchCancelButton })
    {
        b->setColour (juce::TextButton::buttonColourId,   EditorColours::slateGrayDark);
        b->setColour (juce::TextButton::textColourOffId,  EditorColours::white);
        eqMatchPanelContent.addAndMakeVisible (*b);
        b->addListener (this);
    }

    eqMatchStatusLabel.setColour (juce::Label::textColourId, EditorColours::slateGray);
    eqMatchStatusLabel.setText ("Load a reference file to begin", juce::dontSendNotification);
    eqMatchPanelContent.addAndMakeVisible (eqMatchStatusLabel);

    eqMatchPanelContent.addAndMakeVisible (eqMatchWaveform);

    eqMatchPanelContent.setSize (340, 182);
    layoutEqMatchPanelContent();

    // ---- Only now, with every component resized()/updateBandControlVisibility()
    // could touch already constructed, is it safe to create APVTS attachments:
    // their constructors synchronously fire sendInitialUpdate(), which for
    // modeSelector cascades into updateBandControlVisibility() -> resized().
    selectBand (0);

    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "autoGain", autoGainToggle);
    phaseModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "phaseMode", phaseModeSelector);

    startTimer (300);

    setResizable (true, true);
    // Fixed sections (top bar + splitter + tabs + band panel) total 288px;
    // the rest is always the spectrum graph, so these limits just guarantee
    // it never gets squeezed to nothing.
    setResizeLimits (760, 480, 1600, 1000);
    setSize (1000, 700);
}

MultiModeEQAudioProcessorEditor::~MultiModeEQAudioProcessorEditor()
{
    stopTimer();

    for (auto& tab : bandTabs)
        tab.removeListener (this);
    modeSelector.removeListener (this);
    filterShapeSelector.removeListener (this);
    presetSelector.removeListener (this);

    for (auto* b : { &eqMatchCaptureButton, &eqMatchLoadRefButton, &eqMatchApplyButton, &eqMatchCancelButton,
                     &midiLearnButton, &midiLearnClearAllButton, &presetSaveButton, &settingsButton,
                     &aiAssistButton, &aiAssistAnalyseButton, &aiAssistApplyButton, &aiAssistUndoButton,
                     &eqMatchButton })
        b->removeListener (this);
}

void MultiModeEQAudioProcessorEditor::timerCallback()
{
    updateMidiLearnStatusLabel();
    updateAiAssistStatusLabel();
    updateEqMatchStatusLabel();
}

void MultiModeEQAudioProcessorEditor::parentHierarchyChanged()
{
    if (auto* documentWindow = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
        documentWindow->setTitleBarButtonsRequired (juce::DocumentWindow::minimiseButton
                                                   | juce::DocumentWindow::maximiseButton
                                                   | juce::DocumentWindow::closeButton, false);
}

void MultiModeEQAudioProcessorEditor::selectBand (int bandIndex)
{
    selectedBandIndex = bandIndex;

    for (int i = 0; i < MultiModeEQAudioProcessor::numBands; ++i)
        bandTabs[(size_t) i].setColour (juce::TextButton::buttonColourId,
                                         i == bandIndex ? EditorColours::electricBlue : EditorColours::slateGrayDark);

    analyser.setSelectedBand (bandIndex);
    rebuildAttachmentsForSelectedBand();
}

void MultiModeEQAudioProcessorEditor::rebuildAttachmentsForSelectedBand()
{
    const juce::String prefix = "band" + juce::String (selectedBandIndex) + "_";

    enabledAttachment.reset();
    enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, prefix + "enabled", enabledToggle);

    modeAttachment.reset();
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, prefix + "mode", modeSelector);

    channelAttachment.reset();
    channelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, prefix + "channel", channelSelector);

    filterShapeAttachment.reset();
    filterShapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, prefix + "filterShape", filterShapeSelector);

    slopeAttachment.reset();
    slopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, prefix + "slope", slopeSelector);

    freqSlider.rebind      (processor.apvts, prefix + "freq");
    gainSlider.rebind      (processor.apvts, prefix + "gain");
    qSlider.rebind         (processor.apvts, prefix + "q");
    thresholdSlider.rebind (processor.apvts, prefix + "threshold");
    ratioSlider.rebind     (processor.apvts, prefix + "ratio");
    attackSlider.rebind    (processor.apvts, prefix + "attack");
    releaseSlider.rebind   (processor.apvts, prefix + "release");

    sidechainAttachment.reset();
    sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, prefix + "sidechain", sidechainToggle);

    updateBandControlVisibility();
}

void MultiModeEQAudioProcessorEditor::updateBandControlVisibility()
{
    const auto mode = static_cast<EQMode> (modeSelector.getSelectedItemIndex());
    const bool isParametric = (mode == EQMode::Parametric);

    // Q is user-adjustable in both remaining modes (Parametric and Dynamic) --
    // it shapes the Bell's width in either mode, and the resonance at the
    // cutoff for High Pass/Low Pass shapes.
    qSlider.setVisible (true);

    // Filter shape only applies in Parametric mode -- Dynamic bands always
    // behave as Bell (see EQBand::getFilterShape()'s comment).
    filterShapeLabel.setVisible (isParametric);
    filterShapeSelector.setVisible (isParametric);

    const auto shape = static_cast<FilterShape> (filterShapeSelector.getSelectedItemIndex());
    const bool isPassShape = isParametric && (shape == FilterShape::HighPass || shape == FilterShape::LowPass);

    // Slope only means something for High Pass/Low Pass (steeper cascaded
    // cuts). Gain applies to everything except those two -- Bell and both
    // shelves all use it, a pure cut filter has no separate gain control.
    slopeLabel.setVisible (isPassShape);
    slopeSelector.setVisible (isPassShape);
    gainSlider.setVisible (! isPassShape);

    const bool dynamicVisible = (mode == EQMode::Dynamic);
    thresholdSlider.setVisible (dynamicVisible);
    ratioSlider.setVisible     (dynamicVisible);
    attackSlider.setVisible    (dynamicVisible);
    releaseSlider.setVisible   (dynamicVisible);
    sidechainToggle.setVisible (dynamicVisible);

    resized();
}

void MultiModeEQAudioProcessorEditor::updateMidiLearnStatusLabel()
{
    const auto armed = processor.getMidiLearnArmedParam();
    midiLearnStatusLabel.setText (armed.isNotEmpty() ? ("Move a MIDI CC to map " + armed) : "Not learning",
                                   juce::dontSendNotification);
}

void MultiModeEQAudioProcessorEditor::updateAiAssistStatusLabel()
{
    // Only touches the label while actively capturing, or right at the
    // moment capture finishes -- otherwise leaves whatever buttonClicked()
    // last set there (e.g. "Applied N moves"), rather than stomping on it
    // every 300ms.
    const bool capturing = processor.isAiAssistCapturing();

    if (capturing)
    {
        aiAssistStatusLabel.setText ("Listening to input...", juce::dontSendNotification);
        aiAssistWasCapturing = true;
    }
    else if (aiAssistWasCapturing)
    {
        aiAssistWasCapturing = false;
        aiAssistStatusLabel.setText ("Ready -- click Apply Suggestions", juce::dontSendNotification);
    }
}

void MultiModeEQAudioProcessorEditor::updateEqMatchStatusLabel()
{
    juce::String status;

    if (processor.isEqMatchCapturing())
        status = "Capturing current mix...";
    else if (! processor.hasEqMatchReference())
        status = lastEqMatchLoadFailed ? "Failed to load -- unsupported or unreadable file"
                                        : "Load a reference file to begin";
    else if (! processor.isEqMatchCurrentReady())
        status = "\"" + lastLoadedReferenceFileName + "\" loaded -- click Capture Current";
    else
        status = "Ready -- click Apply Match (\"" + lastLoadedReferenceFileName + "\")";

    eqMatchStatusLabel.setText (status, juce::dontSendNotification);
}

void MultiModeEQAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    for (int i = 0; i < MultiModeEQAudioProcessor::numBands; ++i)
        if (button == &bandTabs[(size_t) i]) { selectBand (i); return; }

    if (button == &eqMatchCaptureButton) { processor.beginEqMatchCapture(); return; }
    if (button == &eqMatchApplyButton)   { processor.applyEqMatch();       return; }
    if (button == &eqMatchCancelButton)  { processor.cancelEqMatch();      return; }

    if (button == &eqMatchLoadRefButton)
    {
        activeFileChooser = std::make_unique<juce::FileChooser> (
            "Select a reference audio file...", juce::File(), "*.wav;*.aiff;*.mp3;*.flac;*.ogg");

        activeFileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (! file.existsAsFile())
                    return; // user cancelled -- leave whatever was already loaded alone

                const bool loaded = processor.loadEqMatchReferenceFile (file);
                lastEqMatchLoadFailed = ! loaded;
                lastLoadedReferenceFileName = loaded ? file.getFileName() : juce::String();

                if (loaded)
                    eqMatchWaveform.setFile (file);
                else
                    eqMatchWaveform.clear();

                updateEqMatchStatusLabel();
            });
        return;
    }

    if (button == &midiLearnButton)
    {
        const auto suffix = midiLearnTargetParamSuffix (midiLearnTargetSelector.getSelectedId());
        processor.armMidiLearn ("band" + juce::String (selectedBandIndex) + "_" + suffix);
        updateMidiLearnStatusLabel();
        return;
    }

    if (button == &midiLearnClearAllButton)
    {
        processor.clearAllMidiLearn();
        updateMidiLearnStatusLabel();
        return;
    }

    if (button == &presetSaveButton)
    {
        promptToSavePreset();
        return;
    }

    if (button == &settingsButton)
    {
        showSettingsPanel();
        return;
    }

    if (button == &aiAssistButton)
    {
        showAiAssistPanel();
        return;
    }

    if (button == &eqMatchButton)
    {
        showEqMatchPanel();
        return;
    }

    if (button == &aiAssistAnalyseButton)
    {
        processor.beginAiAssistAnalysis();
        aiAssistStatusLabel.setText ("Listening to input...", juce::dontSendNotification);
        aiAssistWasCapturing = true;
        return;
    }

    if (button == &aiAssistApplyButton)
    {
        const int numApplied = processor.applyAiAssistSuggestions();
        aiAssistStatusLabel.setText (numApplied > 0
            ? ("Applied " + juce::String (numApplied) + " move" + (numApplied == 1 ? "" : "s"))
            : "Nothing to apply -- analyze first, or no free bands/issues found",
            juce::dontSendNotification);
        return;
    }

    if (button == &aiAssistUndoButton)
    {
        processor.undoLastAiAssist();
        aiAssistStatusLabel.setText ("Reverted", juce::dontSendNotification);
        return;
    }
}

void MultiModeEQAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box == &modeSelector || box == &filterShapeSelector)
        updateBandControlVisibility();

    if (box == &presetSelector)
    {
        const auto name = presetSelector.getText();
        if (name == "Default")
            processor.resetToDefault();
        else if (name.isNotEmpty())
            processor.loadPreset (name);
    }
}

void MultiModeEQAudioProcessorEditor::refreshPresetList()
{
    const auto currentText = presetSelector.getText();

    presetSelector.clear (juce::dontSendNotification);
    presetSelector.addItem ("Default", 1);

    const auto presetNames = processor.getAvailablePresetNames();
    if (! presetNames.isEmpty())
    {
        presetSelector.addSeparator();
        int itemId = 2;
        for (const auto& name : presetNames)
            presetSelector.addItem (name, itemId++);
    }

    presetSelector.setText (currentText, juce::dontSendNotification);
}

void MultiModeEQAudioProcessorEditor::promptToSavePreset()
{
    auto* window = new juce::AlertWindow ("Save Preset", "Enter a name for this preset:",
                                           juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", "", "Preset name");
    window->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        if (result == 1)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            if (name.isNotEmpty() && name != "Default") // "Default" is reserved for resetToDefault()
            {
                processor.savePreset (name);
                refreshPresetList();
                presetSelector.setText (name, juce::dontSendNotification);
            }
        }
    }), true /* deleteWhenDismissed */);
}

void MultiModeEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (EditorColours::charcoalBlack);

    auto topBarArea = getLocalBounds().removeFromTop (40);
    g.setColour (EditorColours::slateGrayDark);
    g.fillRect (topBarArea);
}

void MultiModeEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int tabsHeight     = 30;
    constexpr int panelHeight    = 210;

    auto topBarArea = area.removeFromTop (topBarHeight); // background painted in paint()

    // ---- Top bar: compact logo (left) + preset select/save/settings/AI/EQ Match (right) ---
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (110));
        t.removeFromLeft (12);
        presetSelector.setBounds (t.removeFromLeft (160));
        t.removeFromLeft (8);
        presetSaveButton.setBounds (t.removeFromLeft (60));
        t.removeFromLeft (8);
        settingsButton.setBounds (t.removeFromLeft (70));
        t.removeFromLeft (8);
        aiAssistButton.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        eqMatchButton.setBounds (t.removeFromLeft (90));
    }

    // Tabs + detail panel are bottom-anchored with fixed heights, and the
    // graph always fills exactly whatever space remains above them -- so
    // there's never a leftover gap regardless of window size.
    auto panelArea     = area.removeFromBottom (panelHeight);
    auto tabsArea      = area.removeFromBottom (tabsHeight);
    auto splitterArea  = area.removeFromBottom (splitterHeight);
    auto analyserArea  = area; // everything left above the splitter

    analyser.setBounds (analyserArea.reduced (8));
    analyserSplitter.setBounds (splitterArea);

    // ---- Band tabs ------------------------------------------------------------
    {
        auto row = tabsArea.reduced (8, 2);
        const int tabWidth = row.getWidth() / MultiModeEQAudioProcessor::numBands;
        for (auto& tab : bandTabs)
            tab.setBounds (row.removeFromLeft (tabWidth).reduced (2, 0));
    }

    // ---- Selected-band detail panel --------------------------------------------
    {
        auto p = panelArea.reduced (10);
        auto leftCol = p.removeFromLeft (190);

        enabledToggle.setBounds (leftCol.removeFromTop (26));
        leftCol.removeFromTop (6);

        auto row2 = leftCol.removeFromTop (24);
        modeLabel.setBounds (row2.removeFromLeft (60));
        modeSelector.setBounds (row2);
        leftCol.removeFromTop (6);

        auto row3 = leftCol.removeFromTop (24);
        channelLabel.setBounds (row3.removeFromLeft (60));
        channelSelector.setBounds (row3);
        leftCol.removeFromTop (6);

        if (filterShapeSelector.isVisible())
        {
            auto row4 = leftCol.removeFromTop (24);
            filterShapeLabel.setBounds (row4.removeFromLeft (60));
            filterShapeSelector.setBounds (row4);
            leftCol.removeFromTop (6);
        }

        if (slopeSelector.isVisible())
        {
            auto row5 = leftCol.removeFromTop (24);
            slopeLabel.setBounds (row5.removeFromLeft (60));
            slopeSelector.setBounds (row5);
            leftCol.removeFromTop (6);
        }

        sidechainToggle.setBounds (leftCol.removeFromTop (24));

        p.removeFromLeft (14);

        juce::Array<juce::Component*> knobs { &freqSlider.slider, &qSlider.slider };
        if (gainSlider.slider.isVisible())
            knobs.insert (1, &gainSlider.slider);

        // Output meter sits right after Q, wherever that ends up depending on
        // whether Gain is currently shown.
        knobs.insert (knobs.indexOf (&qSlider.slider) + 1, &outputMeter);

        if (thresholdSlider.slider.isVisible())
        {
            knobs.add (&thresholdSlider.slider);
            knobs.add (&ratioSlider.slider);
            knobs.add (&attackSlider.slider);
            knobs.add (&releaseSlider.slider);
        }

        auto knobArea = p;
        knobArea.removeFromTop (20); // headroom for each knob's attachToComponent label above it
        if (! knobs.isEmpty())
        {
            const int cellWidth = knobArea.getWidth() / knobs.size();
            for (auto* knob : knobs)
                knob->setBounds (knobArea.removeFromLeft (cellWidth).reduced (8, 0));
        }
    }

}

void MultiModeEQAudioProcessorEditor::layoutSettingsPanelContent()
{
    // Lays out Auto Gain / Phase Mode / MIDI Learn as a single vertical stack
    // inside settingsPanelContent, sized to match whatever was set via
    // settingsPanelContent.setSize() in the constructor.
    auto g = settingsPanelContent.getLocalBounds().reduced (10);

    autoGainToggle.setBounds (g.removeFromTop (26));
    g.removeFromTop (8);

    auto pm = g.removeFromTop (24);
    phaseModeLabel.setBounds (pm.removeFromLeft (50));
    phaseModeSelector.setBounds (pm);
    g.removeFromTop (12);

    midiLearnLabel.setBounds (g.removeFromTop (18));
    g.removeFromTop (4);
    auto mlRow1 = g.removeFromTop (26);
    midiLearnTargetSelector.setBounds (mlRow1.removeFromLeft (140));
    mlRow1.removeFromLeft (6);
    midiLearnButton.setBounds (mlRow1.removeFromLeft (70));
    mlRow1.removeFromLeft (6);
    midiLearnClearAllButton.setBounds (mlRow1);
    g.removeFromTop (6);
    midiLearnStatusLabel.setBounds (g.removeFromTop (20));
}

void MultiModeEQAudioProcessorEditor::layoutAiAssistPanelContent()
{
    auto g = aiAssistPanelContent.getLocalBounds().reduced (10);

    aiAssistLabel.setBounds (g.removeFromTop (18));
    g.removeFromTop (4);
    auto aiRow = g.removeFromTop (26);
    aiAssistAnalyseButton.setBounds (aiRow.removeFromLeft (90));
    aiRow.removeFromLeft (6);
    aiAssistApplyButton.setBounds (aiRow.removeFromLeft (130));
    aiRow.removeFromLeft (6);
    aiAssistUndoButton.setBounds (aiRow);
    g.removeFromTop (6);
    aiAssistStatusLabel.setBounds (g.removeFromTop (20));
}

void MultiModeEQAudioProcessorEditor::layoutEqMatchPanelContent()
{
    auto g = eqMatchPanelContent.getLocalBounds().reduced (10);

    eqMatchLabel.setBounds (g.removeFromTop (18));
    g.removeFromTop (4);
    auto eqRow1 = g.removeFromTop (26);
    eqMatchCaptureButton.setBounds (eqRow1.removeFromLeft (140));
    eqRow1.removeFromLeft (6);
    eqMatchLoadRefButton.setBounds (eqRow1);
    g.removeFromTop (6);
    auto eqRow2 = g.removeFromTop (26);
    eqMatchApplyButton.setBounds (eqRow2.removeFromLeft (140));
    eqRow2.removeFromLeft (6);
    eqMatchCancelButton.setBounds (eqRow2);
    g.removeFromTop (6);
    eqMatchStatusLabel.setBounds (g.removeFromTop (20));
    g.removeFromTop (6);
    eqMatchWaveform.setBounds (g.removeFromTop (50));
}

void MultiModeEQAudioProcessorEditor::showSettingsPanel()
{
    layoutSettingsPanelContent();

    // settingsPanelContent is a plain (non-heap-owned) member that outlives
    // the popup and is reused across openings, so this uses CallOutBox's
    // non-owning constructor directly rather than launchAsynchronously
    // (which takes ownership of a freshly-created content component). The
    // CallOutBox itself is still heap-allocated and self-deletes on
    // dismissal via deleteWhenDismissed.
    auto* callOut = new juce::CallOutBox (settingsPanelContent, settingsButton.getScreenBounds(), nullptr);
    callOut->setDismissalMouseClicksAreAlwaysConsumed (true);
    callOut->setVisible (true);
    callOut->enterModalState (true, nullptr, true);
}

void MultiModeEQAudioProcessorEditor::showAiAssistPanel()
{
    layoutAiAssistPanelContent();

    auto* callOut = new juce::CallOutBox (aiAssistPanelContent, aiAssistButton.getScreenBounds(), nullptr);
    callOut->setDismissalMouseClicksAreAlwaysConsumed (true);
    callOut->setVisible (true);
    callOut->enterModalState (true, nullptr, true);
}

void MultiModeEQAudioProcessorEditor::showEqMatchPanel()
{
    layoutEqMatchPanelContent();

    auto* callOut = new juce::CallOutBox (eqMatchPanelContent, eqMatchButton.getScreenBounds(), nullptr);
    callOut->setDismissalMouseClicksAreAlwaysConsumed (true);
    callOut->setVisible (true);
    callOut->enterModalState (true, nullptr, true);
}
