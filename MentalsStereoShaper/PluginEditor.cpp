#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// StereoAnalyzerComponent
//==============================================================================
juce::Point<float> StereoAnalyzerComponent::projectStagePoint (float worldX, float worldZ, float worldY, juce::Rectangle<float> bounds) const noexcept
{
    // Fixed camera: the listener stands just outside the stage's near edge
    // (world Z=-1), looking into a circular floor of radius 1 centred at
    // the world origin, slightly above floor height so the view reads as
    // "looking down into a stage" rather than a flat side-on view. Nothing
    // here depends on the audio -- only where a given world point ends up
    // on screen.
    constexpr float cameraSetback = 2.3f;
    constexpr float cameraHeight  = 0.55f;
    constexpr float focalLength   = 2.0f;
    constexpr float horizonYFrac  = 0.22f;

    const float depth = juce::jmax (0.3f, worldZ + cameraSetback);
    const float scale = focalLength / depth;

    const float screenX = bounds.getCentreX() + worldX * scale * (bounds.getWidth() * 0.42f);
    const float horizonY = bounds.getY() + bounds.getHeight() * horizonYFrac;
    const float screenY = horizonY + (cameraHeight - worldY) * scale * (bounds.getHeight() * 0.5f);

    return { screenX, screenY };
}

void StereoAnalyzerComponent::paint (juce::Graphics& g)
{
    g.fillAll (MentalsUI::Colours::slateGrayDark);

    auto fullBounds = getLocalBounds().toFloat().reduced (14.0f);

    // ---- Correlation bar (bottom strip) ------------------------------------
    auto corrArea = fullBounds.removeFromBottom (26.0f);
    fullBounds.removeFromBottom (10.0f); // gap

    const float correlation = processor.getCorrelation();

    g.setColour (MentalsUI::Colours::slateGray);
    g.fillRoundedRectangle (corrArea, 3.0f);

    const float corrCentreX = corrArea.getCentreX();
    const float corrFillX = corrArea.getX() + (correlation * 0.5f + 0.5f) * corrArea.getWidth();
    auto fillRect = juce::Rectangle<float> (juce::jmin (corrCentreX, corrFillX), corrArea.getY(),
                                             std::abs (corrFillX - corrCentreX), corrArea.getHeight());

    const auto fillColour = correlation < -0.1f ? MentalsUI::Colours::crimsonRed
                           : correlation <  0.5f ? MentalsUI::Colours::amberOrange
                                                  : MentalsUI::Colours::emeraldGreen;
    g.setColour (fillColour);
    g.fillRoundedRectangle (fillRect, 3.0f);

    g.setColour (MentalsUI::Colours::white);
    g.drawText ("Correlation " + juce::String (correlation, 2), corrArea, juce::Justification::centred);

    // ---- 3D stereo stage ------------------------------------------------------
    // Backdrop: a soft vertical gradient suggesting depth/atmosphere above
    // the stage floor's horizon, rather than a flat fill.
    const float horizonY = fullBounds.getY() + fullBounds.getHeight() * 0.22f;
    g.setGradientFill (juce::ColourGradient (MentalsUI::Colours::charcoalBlack, fullBounds.getX(), fullBounds.getY(),
                                              MentalsUI::Colours::slateGrayDark, fullBounds.getX(), horizonY, false));
    g.fillRect (fullBounds.withBottom (horizonY));

    auto project = [&] (float wx, float wz, float wy = 0.0f)
    {
        return projectStagePoint (wx, wz, wy, fullBounds);
    };

    // Floor grid: concentric rings at fixed pan-distance-from-centre
    // fractions, plus radial spokes every 45 degrees -- purely a fixed
    // reference grid, not audio-driven.
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    constexpr int ringSegments = 48;
    for (float ringRadius : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        juce::Path ring;
        for (int i = 0; i <= ringSegments; ++i)
        {
            const float angle = juce::MathConstants<float>::twoPi * (float) i / (float) ringSegments;
            const auto p = project (ringRadius * std::sin (angle), ringRadius * std::cos (angle));
            if (i == 0) ring.startNewSubPath (p); else ring.lineTo (p);
        }
        g.strokePath (ring, juce::PathStrokeType (ringRadius >= 1.0f ? 1.4f : 0.8f));
    }
    for (int spoke = 0; spoke < 8; ++spoke)
    {
        const float angle = juce::MathConstants<float>::twoPi * (float) spoke / 8.0f;
        g.drawLine (juce::Line<float> (project (0.0f, 0.0f), project (std::sin (angle), std::cos (angle))), 0.8f);
    }

    // Speaker markers flanking the listener at the stage's near edge, and a
    // small listener marker in front of the camera -- fixed reference
    // points that ground the perspective, same idea as the old diamond
    // guide's L/M/R labels.
    for (float side : { -1.0f, 1.0f })
    {
        const auto speaker = project (side * 0.75f, -0.55f, 0.32f);
        const auto speakerBase = project (side * 0.75f, -0.55f, 0.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawLine (juce::Line<float> (speakerBase, speaker), 2.5f);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (speaker));
        g.setColour (MentalsUI::Colours::slateGray);
        g.drawText (side < 0.0f ? "L" : "R",
                     juce::Rectangle<float> (26.0f, 16.0f).withCentre (speaker.translated (0.0f, -14.0f)),
                     juce::Justification::centred);
    }
    {
        const auto listener = project (0.0f, -1.25f);
        g.setColour (MentalsUI::Colours::white.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (listener));
    }

    // Every OTHER currently-open Stereo Shaper instance's last-published
    // placement (see MixRegistry::getOthersSnapshot()) -- used both for the
    // dim static markers just below and for the gap finder further down.
    // rotationDeg is the Rotation *control*'s value (-180..180, a plain
    // rotation of the L/R pair); the world-space angle everywhere else on
    // this stage is atan2(side, mid) of the actual samples, with
    // side = (R - L) * diag (see the trail loop below for why that sign,
    // not (L - R)). Dropping a centred (mostly-mid) source's outL=outR=m
    // through processBlock()'s rotation matrix at angle A gives
    // side' = 2*diag*m*sinA, mid' = 2*diag*m*cosA, i.e. atan2(side',mid')
    // == A exactly -- so a track's Rotation-knob degrees and its stage
    // angle are the SAME number, verified against a concrete case too:
    // a hard-left source (L=1,R=0) rotated by +90 degrees becomes hard
    // right (finalL=0,finalR=1) in processBlock(), and stage angle +90
    // degrees is exactly where hard-right plots on this floor.
    const auto others = processor.getOtherInstancesSnapshot();
    auto stageAngleForRotationDeg = [] (float rotationDeg) { return juce::degreesToRadians (rotationDeg); };

    for (const auto& other : others)
    {
        const float otherAngleRad = stageAngleForRotationDeg (other.rotationDeg);
        constexpr float otherRadius = 0.55f;
        const auto otherPoint = project (otherRadius * std::sin (otherAngleRad), otherRadius * std::cos (otherAngleRad));

        const float otherWidthFrac = juce::jlimit (0.0f, 2.0f, other.widthPercent / 100.0f);
        const float markerSize = 6.0f + otherWidthFrac * 6.0f;

        g.setColour (MentalsUI::Colours::slateGray.withAlpha (0.65f));
        g.fillEllipse (juce::Rectangle<float> (markerSize, markerSize).withCentre (otherPoint));
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawEllipse (juce::Rectangle<float> (markerSize, markerSize).withCentre (otherPoint), 1.0f);
    }

    constexpr float diag = 0.70710678f;
    const int writePos = processor.getGoniometerWritePos();
    constexpr int step = 4;
    constexpr int numPoints = MentalsStereoShaperAudioProcessor::goniometerSize / step;

    // ---- This track's occupancy zone: a weighted circular mean (centroid
    // direction) and spread of the SAME live output samples used for the
    // trail below, so "where it's sitting" and "how much space it's using"
    // reflect genuine recent audio rather than just the Rotation/Width knob
    // values -- an automated Rotation sweep or a naturally wide source
    // shows up here even if the knobs themselves are static. Weighting each
    // sample by its own magnitude means near-silent samples (which have an
    // essentially random angle) don't drag the centroid around.
    float sumSin = 0.0f, sumCos = 0.0f, sumMagnitude = 0.0f, sumMagnitudeSq = 0.0f, sumWeight = 0.0f;
    for (int i = 0; i < numPoints; ++i)
    {
        const int age = numPoints - 1 - i;
        const int idx = ((writePos - age * step) % MentalsStereoShaperAudioProcessor::goniometerSize
                          + MentalsStereoShaperAudioProcessor::goniometerSize) % MentalsStereoShaperAudioProcessor::goniometerSize;
        const float L = processor.getGoniometerL (idx).load (std::memory_order_relaxed);
        const float R = processor.getGoniometerR (idx).load (std::memory_order_relaxed);
        // (R - L), not (L - R): worldX = sin(angle) must come out positive
        // (renders to the right, matching the R speaker marker) for
        // right-channel-dominant audio, and negative for left-dominant --
        // verified against the L/R-panned test case documented on
        // stageAngleForRotationDeg below.
        const float side = (R - L) * diag;
        const float mid  = (L + R) * diag;
        const float magnitude = juce::jlimit (0.0f, 1.0f, std::sqrt (side * side + mid * mid));
        const float angle = std::atan2 (side, mid);

        sumSin += magnitude * std::sin (angle);
        sumCos += magnitude * std::cos (angle);
        sumMagnitude += magnitude * magnitude;
        sumMagnitudeSq += magnitude * magnitude * magnitude;
        sumWeight += magnitude;
    }

    const bool hasSignal = sumWeight > 0.0001f;
    if (hasSignal)
    {
        const float centroidAngle = std::atan2 (sumSin, sumCos);
        // Resultant length: 1 == every sample pointed the same direction
        // (tight cluster), 0 == directions cancel out (spread all around).
        const float concentration = juce::jlimit (0.0f, 1.0f, std::sqrt (sumSin * sumSin + sumCos * sumCos) / sumWeight);
        const float meanMagnitude = sumMagnitude / sumWeight;
        const float meanMagnitudeSq = sumMagnitudeSq / sumWeight;
        const float magnitudeSpread = std::sqrt (juce::jmax (0.0f, meanMagnitudeSq - meanMagnitude * meanMagnitude));

        // Concentration -> angular half-width is a readability heuristic
        // (a tight cluster reads as a narrow wedge, a spread-out source
        // reads as a broad one), not the formal circular-statistics
        // conversion (circular SD = sqrt(-2 ln R)), which diverges as R
        // approaches 0 and isn't what a mixing engineer needs from a
        // glance at this display.
        const float halfWidthRad = (1.0f - concentration) * juce::MathConstants<float>::halfPi * 0.85f
                                    + juce::degreesToRadians (6.0f);
        const float innerRadius = juce::jlimit (0.0f, 1.0f, meanMagnitude - magnitudeSpread);
        const float outerRadius = juce::jlimit (0.0f, 1.0f, meanMagnitude + magnitudeSpread + 0.05f);

        juce::Path wedge;
        constexpr int wedgeSegments = 20;
        for (int i = 0; i <= wedgeSegments; ++i)
        {
            const float a = centroidAngle - halfWidthRad + 2.0f * halfWidthRad * (float) i / (float) wedgeSegments;
            const auto p = project (outerRadius * std::sin (a), outerRadius * std::cos (a));
            if (i == 0) wedge.startNewSubPath (p); else wedge.lineTo (p);
        }
        for (int i = wedgeSegments; i >= 0; --i)
        {
            const float a = centroidAngle - halfWidthRad + 2.0f * halfWidthRad * (float) i / (float) wedgeSegments;
            wedge.lineTo (project (innerRadius * std::sin (a), innerRadius * std::cos (a)));
        }
        wedge.closeSubPath();

        g.setColour (MentalsUI::Colours::electricBlue.withAlpha (0.16f));
        g.fillPath (wedge);
        g.setColour (MentalsUI::Colours::electricBlue.withAlpha (0.5f));
        g.strokePath (wedge, juce::PathStrokeType (1.2f));

        // Centroid marker plus a plain-language position readout (degrees
        // off centre, or "C" near dead-centre) anchored where the wedge
        // sits on the floor -- the explicit "which spot" answer, rather
        // than leaving the user to read it off the wedge's position alone.
        const auto centroidPoint = project (meanMagnitude * std::sin (centroidAngle), meanMagnitude * std::cos (centroidAngle));
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (centroidPoint), 1.6f);

        const float centroidDeg = juce::radiansToDegrees (centroidAngle);
        const juce::String degreeSign = juce::String::fromUTF8 ("\xc2\xb0");
        const juce::String posLabel = std::abs (centroidDeg) < 4.0f
                                     ? juce::String ("C")
                                     : juce::String (juce::roundToInt (std::abs (centroidDeg))) + degreeSign
                                           + (centroidDeg < 0.0f ? "L" : "R");

        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
        g.drawText (posLabel, juce::Rectangle<float> (60.0f, 18.0f).withCentre (centroidPoint.translated (0.0f, -20.0f)),
                    juce::Justification::centred);
    }

    // ---- Live sample trail: each point is a genuine 3D position derived
    // from actual recent output samples (not illustrative) -- angle around
    // the stage floor is this sample's real instantaneous stereo placement
    // (atan2 of Side against Mid), so Rotation/Auto Rotate visibly sweeps a
    // sound source around the floor exactly as the plugin's processing
    // does; distance from stage-centre is how far from dead-centre that
    // sample sits. Older samples fade out, giving a comet-trail that reads
    // as motion for a moving (auto-rotating, or just wide/dynamic) source
    // and a tight cluster for a stable, centred one.
    for (int i = 0; i < numPoints; ++i)
    {
        // i == 0 is the OLDEST sample still in the trail, i == numPoints-1
        // is the most recent -- walking backwards from the write position.
        const int age = numPoints - 1 - i;
        const int idx = ((writePos - age * step) % MentalsStereoShaperAudioProcessor::goniometerSize
                          + MentalsStereoShaperAudioProcessor::goniometerSize) % MentalsStereoShaperAudioProcessor::goniometerSize;

        const float L = processor.getGoniometerL (idx).load (std::memory_order_relaxed);
        const float R = processor.getGoniometerR (idx).load (std::memory_order_relaxed);

        // (R - L), not (L - R): worldX = sin(angle) must come out positive
        // (renders to the right, matching the R speaker marker) for
        // right-channel-dominant audio, and negative for left-dominant --
        // verified against the L/R-panned test case documented on
        // stageAngleForRotationDeg below.
        const float side = (R - L) * diag;
        const float mid  = (L + R) * diag;
        const float magnitude = juce::jlimit (0.0f, 1.0f, std::sqrt (side * side + mid * mid));
        const float angle = std::atan2 (side, mid);

        const auto p = project (magnitude * std::sin (angle), magnitude * std::cos (angle));

        const float ageFrac = (float) i / (float) juce::jmax (1, numPoints - 1); // 0 = oldest, 1 = newest
        const bool isHead = i == numPoints - 1;

        g.setColour ((isHead ? MentalsUI::Colours::goldenYellow : MentalsUI::Colours::electricBlue)
                         .withAlpha (isHead ? 0.95f : 0.12f + 0.5f * ageFrac));
        const float dotSize = isHead ? 5.5f : 2.2f;
        g.fillEllipse (juce::Rectangle<float> (dotSize, dotSize).withCentre (p));
    }

    // ---- Gap finder: a continuous, always-on suggestion for where in the
    // stereo field is currently LEAST occupied by every other open Stereo
    // Shaper instance, rather than only surfacing mix-awareness once the
    // user presses AI Placement. Each other instance is modelled as a
    // loudness-weighted bump centred on its own stage angle, with an
    // angular width that grows with its own Width setting (a wide source
    // realistically occupies more of the field than a narrow one) -- the
    // gap is just the angle where the sum of every bump is smallest.
    //
    // Rotation by theta and by theta+180 degrees produce exactly opposite-
    // polarity but otherwise identical L/R magnitudes (drop outL=outR=mid
    // through processBlock()'s rotation matrix at theta+180 and it's just
    // -rotL(theta)/-rotR(theta)) -- i.e. they're the SAME pan position, not
    // opposite ones. Comparing raw angles the way the live trail does would
    // have called that "maximally different" and pointed straight at a
    // pure-polarity-inversion of whatever's already there (confirmed by an
    // early test of this feature: two centred tracks produced "Gap 180",
    // which is not a usefully different spot at all). Both the candidates
    // scanned and each other's contribution are therefore folded into a
    // half turn via the standard doubled-angle trick for axial data before
    // comparing, so the search only needs to (and only ever does) suggest
    // within +/-90 degrees.
    if (! others.empty())
    {
        constexpr int numGapSamples = 36;
        float bestAngle = 0.0f, bestOccupancy = 1.0e9f;

        for (int s = 0; s < numGapSamples; ++s)
        {
            const float candidate = -juce::MathConstants<float>::halfPi
                                     + juce::MathConstants<float>::pi * (float) s / (float) numGapSamples;
            float occupancy = 0.0f;

            for (const auto& other : others)
            {
                const float otherAngle = stageAngleForRotationDeg (other.rotationDeg);

                // Doubled-angle signed difference, wrapped into [-pi, pi):
                // makes the comparison periodic every 180 degrees instead
                // of every 360, so theta and theta+180 collapse to "same".
                float diff = std::fmod (2.0f * (candidate - otherAngle) + juce::MathConstants<float>::pi,
                                         juce::MathConstants<float>::twoPi);
                if (diff < 0.0f) diff += juce::MathConstants<float>::twoPi;
                diff -= juce::MathConstants<float>::pi;

                const float widthFrac = juce::jlimit (0.0f, 2.0f, other.widthPercent / 100.0f);
                const float sigma = juce::degreesToRadians (15.0f + widthFrac * 15.0f);
                const float sigmaDoubled = 2.0f * sigma;
                const float weight = juce::jmin (4.0f, std::pow (10.0f, other.rmsDb / 20.0f));

                occupancy += weight * std::exp (-(diff * diff) / (2.0f * sigmaDoubled * sigmaDoubled));
            }

            if (occupancy < bestOccupancy)
            {
                bestOccupancy = occupancy;
                bestAngle = candidate;
            }
        }

        // Smoothed via the sin/cos components (not the angle directly, which
        // would jump discontinuously across the +/-180 degree wrap) so the
        // suggestion drifts calmly rather than jittering as other tracks'
        // loudness fluctuates moment to moment.
        if (! gapEstimateValid)
        {
            gapSmoothedSin = std::sin (bestAngle);
            gapSmoothedCos = std::cos (bestAngle);
            gapEstimateValid = true;
        }
        else
        {
            constexpr float smoothing = 0.06f;
            gapSmoothedSin += (std::sin (bestAngle) - gapSmoothedSin) * smoothing;
            gapSmoothedCos += (std::cos (bestAngle) - gapSmoothedCos) * smoothing;
        }

        const float gapAngle = std::atan2 (gapSmoothedSin, gapSmoothedCos);
        const float gapHalfWidthRad = juce::degreesToRadians (12.0f);
        constexpr float gapRadius = 0.97f;

        juce::Path gapArc;
        constexpr int gapSegments = 16;
        for (int i = 0; i <= gapSegments; ++i)
        {
            const float a = gapAngle - gapHalfWidthRad + 2.0f * gapHalfWidthRad * (float) i / (float) gapSegments;
            const auto p = project (gapRadius * std::sin (a), gapRadius * std::cos (a));
            if (i == 0) gapArc.startNewSubPath (p); else gapArc.lineTo (p);
        }
        g.setColour (MentalsUI::Colours::emeraldGreen.withAlpha (0.8f));
        g.strokePath (gapArc, juce::PathStrokeType (3.0f));

        const auto gapMarker = project (gapRadius * std::sin (gapAngle), gapRadius * std::cos (gapAngle));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (gapMarker));

        // The actual Rotation-knob degrees the user would dial in to sit in
        // this gap -- the inverse of stageAngleForRotationDeg() above
        // (currently the identity, since stage angle == Rotation degrees).
        const float suggestedRotationDeg = juce::radiansToDegrees (gapAngle);
        const juce::String degreeSign = juce::String::fromUTF8 ("\xc2\xb0");
        const juce::String gapPosLabel = std::abs (suggestedRotationDeg) < 4.0f
                                        ? juce::String ("C")
                                        : juce::String (juce::roundToInt (std::abs (suggestedRotationDeg))) + degreeSign
                                              + (suggestedRotationDeg < 0.0f ? "L" : "R");

        g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
        g.drawText ("Gap " + gapPosLabel, juce::Rectangle<float> (90.0f, 16.0f).withCentre (gapMarker.translated (0.0f, -16.0f)),
                    juce::Justification::centred);
    }
}

//==============================================================================
// MentalsStereoShaperAudioProcessorEditor
//==============================================================================
MentalsStereoShaperAudioProcessorEditor::MentalsStereoShaperAudioProcessorEditor (MentalsStereoShaperAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), analyzer (p),
      outputMeter ([&p] { return p.getOutputPeakDb(); }, [&p] { return p.isOutputClipping(); })
{
    setLookAndFeel (&hardwareLookAndFeel);

    addAndMakeVisible (logoImage);

    productNameLabel.setText ("360 Stereo Shaper", juce::dontSendNotification);
    productNameLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    productNameLabel.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    addAndMakeVisible (productNameLabel);

    phaseAlignButton.setClickingTogglesState (true);
    phaseAlignButton.setColour (juce::TextButton::buttonColourId,   MentalsUI::Colours::slateGrayDark);
    phaseAlignButton.setColour (juce::TextButton::buttonOnColourId, MentalsUI::Colours::electricBlue);
    phaseAlignButton.setColour (juce::TextButton::textColourOffId,  MentalsUI::Colours::white);
    phaseAlignButton.setColour (juce::TextButton::textColourOnId,   MentalsUI::Colours::charcoalBlack);
    addAndMakeVisible (phaseAlignButton);
    phaseAlignAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "phaseAlign", phaseAlignButton);

    aiAssistButton.setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::slateGrayDark);
    aiAssistButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
    addAndMakeVisible (aiAssistButton);
    aiAssistButton.addListener (this);

    presetSelector.setTextWhenNothingSelected ("Presets");
    presetSelector.setColour (juce::ComboBox::backgroundColourId, MentalsUI::Colours::slateGrayDark);
    presetSelector.setColour (juce::ComboBox::textColourId,       MentalsUI::Colours::white);
    presetSelector.setColour (juce::ComboBox::outlineColourId,    MentalsUI::Colours::slateGray);
    presetSelector.setColour (juce::ComboBox::arrowColourId,      MentalsUI::Colours::white);
    addAndMakeVisible (presetSelector);
    presetSelector.addListener (this);
    refreshPresetList();

    presetSaveButton.setColour (juce::TextButton::buttonColourId,  MentalsUI::Colours::slateGrayDark);
    presetSaveButton.setColour (juce::TextButton::textColourOffId, MentalsUI::Colours::white);
    addAndMakeVisible (presetSaveButton);
    presetSaveButton.addListener (this);

    stereoToggle.setColour (juce::ToggleButton::textColourId, MentalsUI::Colours::white);
    stereoToggle.setColour (juce::ToggleButton::tickColourId, MentalsUI::Colours::white);
    addAndMakeVisible (stereoToggle);

    addAndMakeVisible (analyzer);
    addAndMakeVisible (splitter);

    widthSlider.addToParent      ("Width",       *this);
    midGainSlider.addToParent    ("Mid Gain",    *this);
    rotationSlider.addToParent   ("Rotation",    *this);
    autoRotateSlider.addToParent ("Auto Rotate", *this);
    dynamicsSlider.addToParent   ("Dynamics",    *this);

    lowFreqSlider.addToParent   ("Low Freq",   *this);
    highFreqSlider.addToParent  ("High Freq",  *this);
    lowWidthSlider.addToParent  ("Low Width",  *this);
    midWidthSlider.addToParent  ("Mid Width",  *this);
    highWidthSlider.addToParent ("High Width", *this);

    mixSlider.addToParent ("Mix", *this);

    outputMeterLabel.setText ("Out", juce::dontSendNotification);
    outputMeterLabel.setColour (juce::Label::textColourId, MentalsUI::Colours::white);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.attachToComponent (&outputMeter, false);
    addAndMakeVisible (outputMeterLabel);
    addAndMakeVisible (outputMeter);

    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "width", widthSlider.slider);
    midGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "midGain", midGainSlider.slider);
    rotationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "rotation", rotationSlider.slider);
    autoRotateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "autoRotateRate", autoRotateSlider.slider);
    dynamicsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "dynamicAmount", dynamicsSlider.slider);
    lowFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "lowFreq", lowFreqSlider.slider);
    highFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "highFreq", highFreqSlider.slider);
    lowWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "lowWidth", lowWidthSlider.slider);
    midWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "midWidth", midWidthSlider.slider);
    highWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "highWidth", highWidthSlider.slider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mix", mixSlider.slider);
    stereoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "stereo", stereoToggle);

    setResizable (true, true);
    setResizeLimits (760, 560, 1400, 1000);
    setSize (960, 700);
}

MentalsStereoShaperAudioProcessorEditor::~MentalsStereoShaperAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    aiAssistButton.removeListener (this);
    presetSelector.removeListener (this);
    presetSaveButton.removeListener (this);
}

void MentalsStereoShaperAudioProcessorEditor::buttonClicked (juce::Button* button)
{
    if (button == &presetSaveButton)
        promptToSavePreset();
    else if (button == &aiAssistButton)
        processor.runAiPlacement();
}

void MentalsStereoShaperAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetSelector)
        return;

    const auto name = presetSelector.getText();
    if (name == "Default")
        processor.resetToDefault();
    else if (name.isNotEmpty())
        processor.presetManager.loadPreset (name);
}

void MentalsStereoShaperAudioProcessorEditor::refreshPresetList()
{
    const auto currentText = presetSelector.getText();

    presetSelector.clear (juce::dontSendNotification);
    presetSelector.addItem ("Default", 1);

    const auto presetNames = processor.presetManager.getAvailablePresetNames();
    if (! presetNames.isEmpty())
    {
        presetSelector.addSeparator();
        int itemId = 2;
        for (const auto& name : presetNames)
            presetSelector.addItem (name, itemId++);
    }

    presetSelector.setText (currentText, juce::dontSendNotification);
}

void MentalsStereoShaperAudioProcessorEditor::promptToSavePreset()
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
            if (name.isNotEmpty() && name != "Default")
            {
                processor.presetManager.savePreset (name);
                refreshPresetList();
                presetSelector.setText (name, juce::dontSendNotification);
            }
        }
    }), true /* deleteWhenDismissed */);
}

void MentalsStereoShaperAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0c0c0d));

    auto topBarArea = getLocalBounds().removeFromTop (40);
    MentalsUI::HardwareLookAndFeel::drawMetalPanel (g, topBarArea.toFloat());

    if (! lastPanelBounds.isEmpty())
    {
        auto panelBoundsF = lastPanelBounds.toFloat();
        MentalsUI::HardwareLookAndFeel::drawMetalPanel (g, panelBoundsF);

        constexpr float inset = 10.0f;
        MentalsUI::HardwareLookAndFeel::drawScrew (g, { panelBoundsF.getX() + inset, panelBoundsF.getY() + inset });
        MentalsUI::HardwareLookAndFeel::drawScrew (g, { panelBoundsF.getRight() - inset, panelBoundsF.getY() + inset });
        MentalsUI::HardwareLookAndFeel::drawScrew (g, { panelBoundsF.getX() + inset, panelBoundsF.getBottom() - inset });
        MentalsUI::HardwareLookAndFeel::drawScrew (g, { panelBoundsF.getRight() - inset, panelBoundsF.getBottom() - inset });
    }

    constexpr float earWidth = 22.0f;
    auto fullBounds = getLocalBounds().toFloat();
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromLeft (earWidth).reduced (2.0f));
    MentalsUI::HardwareLookAndFeel::drawRackEar (g, fullBounds.removeFromRight (earWidth).reduced (2.0f));
}

void MentalsStereoShaperAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (24);
    area.removeFromRight (24);

    constexpr int topBarHeight   = 40;
    constexpr int splitterHeight = 8;
    constexpr int panelHeight    = 240;

    auto topBarArea = area.removeFromTop (topBarHeight);
    {
        auto t = topBarArea.reduced (8, 4);
        logoImage.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (8);
        productNameLabel.setBounds (t.removeFromLeft (130));

        presetSaveButton.setBounds (t.removeFromRight (60));
        t.removeFromRight (8);
        presetSelector.setBounds (t.removeFromRight (140));
        t.removeFromRight (12);
        aiAssistButton.setBounds (t.removeFromRight (110));
        t.removeFromRight (8);
        phaseAlignButton.setBounds (t.removeFromRight (110));
        t.removeFromRight (8);
        stereoToggle.setBounds (t.removeFromRight (80));
    }

    auto panelArea    = area.removeFromBottom (panelHeight);
    auto splitterArea = area.removeFromBottom (splitterHeight);
    auto graphArea    = area;

    lastPanelBounds = panelArea;

    analyzer.setBounds (graphArea.reduced (8));
    splitter.setBounds (splitterArea);

    auto p = panelArea.reduced (10);

    constexpr int rightStripWidth = 150;
    auto rightStrip = p.removeFromRight (rightStripWidth);
    p.removeFromRight (10);

    auto layoutKnobRow = [] (juce::Rectangle<int> row, juce::Array<juce::Component*> knobs)
    {
        row.removeFromTop (20); // headroom for each knob's attachToComponent label above it
        const int cellWidth = row.getWidth() / knobs.size();
        for (auto* knob : knobs)
            knob->setBounds (row.removeFromLeft (cellWidth).reduced (6, 0));
    };

    const int rowHeight = p.getHeight() / 2;
    auto row1 = p.removeFromTop (rowHeight);
    auto row2 = p;

    layoutKnobRow (row1, { &widthSlider.slider, &midGainSlider.slider, &rotationSlider.slider,
                           &autoRotateSlider.slider, &dynamicsSlider.slider });
    layoutKnobRow (row2, { &lowFreqSlider.slider, &highFreqSlider.slider, &lowWidthSlider.slider,
                           &midWidthSlider.slider, &highWidthSlider.slider });

    rightStrip.removeFromTop (20); // headroom to line up with the knob rows' labels
    auto meterArea = rightStrip.removeFromRight (44);
    outputMeter.setBounds (meterArea.reduced (4, 0));
    mixSlider.slider.setBounds (rightStrip.reduced (10, 0));
}
