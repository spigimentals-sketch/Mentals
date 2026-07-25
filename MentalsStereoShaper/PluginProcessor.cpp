#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    // Skew factors chosen so each crossover knob's centre position lands near
    // its default frequency (150Hz / 4000Hz) rather than the arithmetic
    // midpoint of its range.
    constexpr float lowFreqSkew  = 0.343f;
    constexpr float highFreqSkew = 0.450f;

    constexpr float envelopeAttackSeconds  = 0.010f;
    constexpr float envelopeReleaseSeconds = 0.250f;

    // Below this correlation, Phase Align starts pulling Side back in; fully
    // clamped down to minSafetyScale by the time correlation reaches -1.
    constexpr float correlationSafetyThreshold = -0.1f;
    constexpr float minSafetyScale = 0.4f;
}

//==============================================================================
MentalsStereoShaperAudioProcessor::MentalsStereoShaperAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    widthParam          = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("width"));
    midGainParam        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("midGain"));
    rotationParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("rotation"));
    autoRotateRateParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("autoRotateRate"));
    dynamicAmountParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("dynamicAmount"));
    lowFreqParam        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("lowFreq"));
    highFreqParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("highFreq"));
    lowWidthParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("lowWidth"));
    midWidthParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("midWidth"));
    highWidthParam      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("highWidth"));
    phaseAlignParam     = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("phaseAlign"));
    mixParam            = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("mix"));
    stereoParam         = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("stereo"));

    for (auto& a : goniometerL) a.store (0.0f);
    for (auto& a : goniometerR) a.store (0.0f);

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsStereoShaperAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "width", "Width",
        juce::NormalisableRange<float> (0.0f, 200.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "midGain", "Mid Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "rotation", "Rotation",
        juce::NormalisableRange<float> (-180.0f, 180.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("deg")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "autoRotateRate", "Auto Rotate",
        juce::NormalisableRange<float> (0.0f, 5.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "dynamicAmount", "Dynamics",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lowFreq", "Low Freq",
        juce::NormalisableRange<float> (20.0f, 1000.0f, 1.0f, lowFreqSkew), 150.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "highFreq", "High Freq",
        juce::NormalisableRange<float> (1000.0f, 15000.0f, 1.0f, highFreqSkew), 4000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lowWidth", "Low Width",
        juce::NormalisableRange<float> (0.0f, 200.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "midWidth", "Mid Width",
        juce::NormalisableRange<float> (0.0f, 200.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "highWidth", "High Width",
        juce::NormalisableRange<float> (0.0f, 200.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "phaseAlign", "Phase Align", true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereo", "Stereo", true));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsStereoShaperAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    sideSplitter.reset();
    sideSplitter.lastFreq1 = sideSplitter.lastFreq2 = -1.0f; // force a coefficient recompute on first block

    fingerprintSplitter.reset();
    fingerprintSplitter.lastFreq1 = fingerprintSplitter.lastFreq2 = -1.0f; // force a coefficient recompute even if the sample rate changed
    fingerprintSplitter.updateIfNeeded (sampleRate, fingerprintLowFreq, fingerprintHighFreq);
    fingerprintLowEnergy = fingerprintMidEnergy = fingerprintHighEnergy = 0.0f;
    fingerprintPeak = fingerprintRmsSquared = 0.0f;
    fingerprintCorrelation = 0.0f;

    rotationAngleRad = 0.0f;
    envelopeFollowerState = 0.0f;
    phaseSafetyScale = 1.0f;

    attackCoeff  = 1.0f - std::exp (-1.0f / (envelopeAttackSeconds  * (float) sampleRate));
    releaseCoeff = 1.0f - std::exp (-1.0f / (envelopeReleaseSeconds * (float) sampleRate));

    smoothedCorrelation.store (1.0f);

    for (auto& a : goniometerL) a.store (0.0f);
    for (auto& a : goniometerR) a.store (0.0f);
    goniometerWritePos.store (0);

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
}

bool MentalsStereoShaperAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MentalsStereoShaperAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const int numSamples = buffer.getNumSamples();

    const float widthScale   = widthParam->get() * 0.01f;
    const float midGainLin   = juce::Decibels::decibelsToGain (midGainParam->get());
    const float rotationBase = rotationParam->get() * juce::MathConstants<float>::pi / 180.0f;
    const float autoRateHz   = autoRotateRateParam->get();
    const float dynamicNorm  = dynamicAmountParam->get() * 0.01f;
    const float lowWidthScale  = lowWidthParam->get()  * 0.01f;
    const float midWidthScale  = midWidthParam->get()  * 0.01f;
    const float highWidthScale = highWidthParam->get() * 0.01f;
    const bool  phaseAlignOn  = phaseAlignParam->get();
    const float mix = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);

    // Enforce a minimum gap between the two crossover points so the mid band
    // (between them) can never collapse to a negative-width bandpass.
    const float freq1 = lowFreqParam->get();
    const float freq2 = juce::jmax (highFreqParam->get(), freq1 + 20.0f);
    sideSplitter.updateIfNeeded (currentSampleRate, freq1, freq2);

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    const float twoPi = juce::MathConstants<float>::twoPi;
    const float autoRateIncrement = twoPi * autoRateHz / (float) currentSampleRate;

    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
    int gPos = goniometerWritePos.load (std::memory_order_relaxed);

    // "Own fingerprint" accumulators -- describe this track's raw INPUT
    // (before any width/rotation processing), matching what
    // extract_features.py computed from each real stem for training.
    double sumInLR = 0.0, sumInLL = 0.0, sumInRR = 0.0;
    double sumFpLow2 = 0.0, sumFpMid2 = 0.0, sumFpHigh2 = 0.0, sumFpMono2 = 0.0;
    float fpPeak = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const float L = left[n];
        const float R = right[n];

        sumInLR += (double) L * R;
        sumInLL += (double) L * L;
        sumInRR += (double) R * R;

        const float fpMono = 0.5f * (L + R);
        const auto fpBands = fingerprintSplitter.process (fpMono);
        sumFpLow2  += (double) fpBands[0] * fpBands[0];
        sumFpMid2  += (double) fpBands[1] * fpBands[1];
        sumFpHigh2 += (double) fpBands[2] * fpBands[2];
        sumFpMono2 += (double) fpMono * fpMono;
        fpPeak = juce::jmax (fpPeak, std::abs (fpMono));

        const float mid  = 0.5f * (L + R);
        const float side = 0.5f * (L - R);

        const auto bands = sideSplitter.process (side);

        float shapedSide = bands[0] * lowWidthScale + bands[1] * midWidthScale + bands[2] * highWidthScale;

        // Dynamics-driven width: envelope follows the input loudness, then
        // pushes Width up (positive Dynamics) or down (negative Dynamics) in
        // proportion to how loud the signal currently is.
        const float rectified = std::abs (mid);
        const float coeff = rectified > envelopeFollowerState ? attackCoeff : releaseCoeff;
        envelopeFollowerState += (rectified - envelopeFollowerState) * coeff;
        const float dynamicMod = juce::jlimit (0.0f, 2.5f, 1.0f + dynamicNorm * envelopeFollowerState);

        shapedSide *= widthScale * dynamicMod * phaseSafetyScale;

        const float shapedMid = mid * midGainLin;

        float outL = shapedMid + shapedSide;
        float outR = shapedMid - shapedSide;

        // Stereo-field rotation: a plain 2D rotation of the L/R pair, so a
        // fixed angle is a creative pan/rotate and a nonzero Auto Rotate rate
        // sweeps continuously through 360 degrees for the "360 Sweep" effect.
        const float angle = rotationBase + rotationAngleRad;
        const float cosA = std::cos (angle);
        const float sinA = std::sin (angle);
        const float rotL = outL * cosA - outR * sinA;
        const float rotR = outL * sinA + outR * cosA;

        rotationAngleRad += autoRateIncrement;
        if (rotationAngleRad >= twoPi)
            rotationAngleRad -= twoPi;

        const float finalL = L * (1.0f - mix) + rotL * mix;
        const float finalR = R * (1.0f - mix) + rotR * mix;

        left[n]  = finalL;
        right[n] = finalR;

        sumLR += (double) finalL * finalR;
        sumLL += (double) finalL * finalL;
        sumRR += (double) finalR * finalR;

        goniometerL[(size_t) gPos].store (finalL, std::memory_order_relaxed);
        goniometerR[(size_t) gPos].store (finalR, std::memory_order_relaxed);
        gPos = (gPos + 1) % goniometerSize;
    }

    goniometerWritePos.store (gPos, std::memory_order_relaxed);

    // Correlation and band-energy figures feed both the analyzer display and
    // (one block later) the Phase Align safety scale and the Mix Analysis
    // Assist heuristic -- a one-block-old measurement is an accepted,
    // standard tradeoff for a causal, lookahead-free safety net.
    const double denom = std::sqrt (sumLL * sumRR) + 1.0e-9;
    const float blockCorrelation = (float) juce::jlimit (-1.0, 1.0, sumLR / denom);
    const float previousCorrelation = smoothedCorrelation.load();
    const float newCorrelation = previousCorrelation * 0.7f + blockCorrelation * 0.3f;
    smoothedCorrelation.store (newCorrelation);

    if (phaseAlignOn && newCorrelation < correlationSafetyThreshold)
    {
        const float target = juce::jmap (newCorrelation, -1.0f, correlationSafetyThreshold, minSafetyScale, 1.0f);
        phaseSafetyScale += (target - phaseSafetyScale) * 0.2f;
    }
    else
    {
        phaseSafetyScale += (1.0f - phaseSafetyScale) * 0.2f;
    }

    // Heavily-smoothed (multi-second) "own fingerprint", published to
    // mixRegistry for every OTHER Stereo Shaper instance to read, and read
    // back by runAiPlacement() for this instance's own AI Placement call.
    {
        const float blockLow  = (float) (sumFpLow2  / numSamples);
        const float blockMid  = (float) (sumFpMid2  / numSamples);
        const float blockHigh = (float) (sumFpHigh2 / numSamples);
        const float blockRms2 = (float) (sumFpMono2 / numSamples);
        constexpr float fpSmooth = 0.05f; // slow blend -- a multi-second characterisation, not a fast meter

        fingerprintLowEnergy  += (blockLow  - fingerprintLowEnergy)  * fpSmooth;
        fingerprintMidEnergy  += (blockMid  - fingerprintMidEnergy)  * fpSmooth;
        fingerprintHighEnergy += (blockHigh - fingerprintHighEnergy) * fpSmooth;
        fingerprintRmsSquared += (blockRms2 - fingerprintRmsSquared) * fpSmooth;
        fingerprintPeak       += (fpPeak    - fingerprintPeak)       * fpSmooth;

        const double inDenom = std::sqrt (sumInLL * sumInRR) + 1.0e-9;
        const float blockInCorrelation = (float) juce::jlimit (-1.0, 1.0, sumInLR / inDenom);
        fingerprintCorrelation += (blockInCorrelation - fingerprintCorrelation) * fpSmooth;

        const float fpTotal = fingerprintLowEnergy + fingerprintMidEnergy + fingerprintHighEnergy + 1.0e-12f;
        const float rms = std::sqrt (fingerprintRmsSquared) + 1.0e-9f;

        std::array<float, MixRegistry::numOwnFeatures> ownFeatures;
        ownFeatures[MixRegistry::featureLowRatio]         = fingerprintLowEnergy / fpTotal;
        ownFeatures[MixRegistry::featureMidRatio]         = fingerprintMidEnergy / fpTotal;
        ownFeatures[MixRegistry::featureHighRatio]        = fingerprintHighEnergy / fpTotal;
        ownFeatures[MixRegistry::featureCrestFactorDb]    = 20.0f * std::log10 (fingerprintPeak / rms + 1.0e-9f);
        ownFeatures[MixRegistry::featureRmsDb]             = 20.0f * std::log10 (rms);
        ownFeatures[MixRegistry::featureInputCorrelation] = fingerprintCorrelation;

        for (int i = 0; i < MixRegistry::numOwnFeatures; ++i)
            ownFeatureAtomics[(size_t) i].store (ownFeatures[(size_t) i], std::memory_order_relaxed);

        mixRegistry.publish (ownFeatures, rotationParam->get(), widthParam->get());
    }

    if (! stereoParam->get() && buffer.getNumChannels() > 1)
    {
        auto* monoL = buffer.getWritePointer (0);
        auto* monoR = buffer.getWritePointer (1);
        for (int n = 0; n < numSamples; ++n)
        {
            const float avg = 0.5f * (monoL[n] + monoR[n]);
            monoL[n] = avg;
            monoR[n] = avg;
        }
    }

    updateOutputLevelMeter (buffer);
}

void MentalsStereoShaperAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    const float releasePerBlock = std::pow (10.0f, -24.0f * ((float) buffer.getNumSamples() / (float) currentSampleRate) / 20.0f);
    const float previous = outputPeakLinear.load();
    outputPeakLinear.store (juce::jmax (peak, previous * releasePerBlock));

    if (peak >= 1.0f)
        clipHoldBlocksRemaining.store ((int) (1.5 * currentSampleRate / juce::jmax (1, buffer.getNumSamples())));
    else if (clipHoldBlocksRemaining.load() > 0)
        clipHoldBlocksRemaining.fetch_sub (1);
}

//==============================================================================
void MentalsStereoShaperAudioProcessor::runAiPlacement()
{
    std::array<float, MixRegistry::numOwnFeatures> ownFeatures;
    for (int i = 0; i < MixRegistry::numOwnFeatures; ++i)
        ownFeatures[(size_t) i] = ownFeatureAtomics[(size_t) i].load (std::memory_order_relaxed);

    const auto context = mixRegistry.computeContext();
    const auto suggestion = placementModel.predict (ownFeatures, context);
    if (! suggestion.has_value())
        return; // AI Placement model unavailable on this machine -- nothing to apply

    // Explicit occupancy-avoidance nudge, layered on top of the trained
    // model's own rotation suggestion. Empirically (retrained and checked
    // against 2,808 real MUSDB18HQ examples across 26 songs -- see
    // Models/README.md), the model shows ~zero learned sensitivity to
    // leftOccupancy/rightOccupancy specifically: RandomForest feature
    // importance of exactly 0.0 for both, and a raw correlation of -0.009
    // between occupancy imbalance and the real placement label. That's not
    // a bug or a data-volume problem -- real mixing engineers' left/right
    // placement choices just aren't well-predicted by a simple aggregate
    // occupancy measure (instrument role/convention dominates instead,
    // which the model does pick up on). Rather than ship a "mix-aware"
    // feature that silently ignores the one thing it's meant to be aware
    // of, a deterministic nudge away from whichever side is currently more
    // crowded is applied here -- 0 when nothing else is detected
    // (leftOccupancy == rightOccupancy == 0), growing towards
    // maxOccupancyNudgeDeg as the imbalance grows.
    constexpr float maxOccupancyNudgeDeg = 45.0f;
    constexpr float occupancyNudgeDegPerUnit = 12.0f;
    const float occupancyImbalance = context.rightOccupancy - context.leftOccupancy;
    const float occupancyNudge = juce::jlimit (-maxOccupancyNudgeDeg, maxOccupancyNudgeDeg,
                                                -occupancyImbalance * occupancyNudgeDegPerUnit);
    const float adjustedRotationDeg = suggestion->rotationDeg + occupancyNudge;

    auto apply = [] (juce::AudioParameterFloat* p, float value)
    {
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    apply (rotationParam, juce::jlimit (-180.0f, 180.0f, adjustedRotationDeg));
    apply (widthParam,    juce::jlimit (0.0f, 200.0f, suggestion->widthPercent));
}

//==============================================================================
void MentalsStereoShaperAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto apply = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };

    resetToDefault();
    apply (widthParam, 140.0f); apply (midGainParam, 2.0f);
    apply (lowWidthParam, 60.0f); apply (midWidthParam, 110.0f); apply (highWidthParam, 150.0f);
    presetManager.savePreset ("Wide Vocals");

    resetToDefault();
    apply (widthParam, 110.0f);
    apply (lowWidthParam, 0.0f); apply (midWidthParam, 100.0f); apply (highWidthParam, 120.0f);
    presetManager.savePreset ("Mono Bass");

    resetToDefault();
    apply (widthParam, 130.0f); apply (autoRotateRateParam, 0.5f);
    presetManager.savePreset ("360 Sweep");

    resetToDefault();
    apply (widthParam, 170.0f);
    apply (lowWidthParam, 60.0f); apply (midWidthParam, 140.0f); apply (highWidthParam, 180.0f);
    apply (dynamicAmountParam, 40.0f); apply (autoRotateRateParam, 0.1f);
    presetManager.savePreset ("Ambient Pad");

    resetToDefault();
}

//==============================================================================
juce::AudioProcessorEditor* MentalsStereoShaperAudioProcessor::createEditor()
{
    return new MentalsStereoShaperAudioProcessorEditor (*this);
}

void MentalsStereoShaperAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsStereoShaperAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}
