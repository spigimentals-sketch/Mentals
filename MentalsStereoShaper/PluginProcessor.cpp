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

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsStereoShaperAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    sideSplitter.reset();
    sideSplitter.lastFreq1 = sideSplitter.lastFreq2 = -1.0f; // force a coefficient recompute on first block

    rotationAngleRad = 0.0f;
    envelopeFollowerState = 0.0f;
    phaseSafetyScale = 1.0f;

    attackCoeff  = 1.0f - std::exp (-1.0f / (envelopeAttackSeconds  * (float) sampleRate));
    releaseCoeff = 1.0f - std::exp (-1.0f / (envelopeReleaseSeconds * (float) sampleRate));

    smoothedCorrelation.store (1.0f);
    lowSideEnergy.store (0.0f);
    midSideEnergy.store (0.0f);
    highSideEnergy.store (0.0f);

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
    double sumLow2 = 0.0, sumMid2 = 0.0, sumHigh2 = 0.0;
    int gPos = goniometerWritePos.load (std::memory_order_relaxed);

    for (int n = 0; n < numSamples; ++n)
    {
        const float L = left[n];
        const float R = right[n];

        const float mid  = 0.5f * (L + R);
        const float side = 0.5f * (L - R);

        const auto bands = sideSplitter.process (side);
        sumLow2  += (double) bands[0] * bands[0];
        sumMid2  += (double) bands[1] * bands[1];
        sumHigh2 += (double) bands[2] * bands[2];

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

    lowSideEnergy.store (lowSideEnergy.load() * 0.7f + (float) sumLow2 * 0.3f);
    midSideEnergy.store (midSideEnergy.load() * 0.7f + (float) sumMid2 * 0.3f);
    highSideEnergy.store (highSideEnergy.load() * 0.7f + (float) sumHigh2 * 0.3f);

    if (phaseAlignOn && newCorrelation < correlationSafetyThreshold)
    {
        const float target = juce::jmap (newCorrelation, -1.0f, correlationSafetyThreshold, minSafetyScale, 1.0f);
        phaseSafetyScale += (target - phaseSafetyScale) * 0.2f;
    }
    else
    {
        phaseSafetyScale += (1.0f - phaseSafetyScale) * 0.2f;
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
void MentalsStereoShaperAudioProcessor::runMixAnalysisAssist()
{
    const float correlation = smoothedCorrelation.load();
    const float low = lowSideEnergy.load(), mid = midSideEnergy.load(), high = highSideEnergy.load();
    const float total = low + mid + high + 1.0e-9f;
    const float lowFrac = low / total;
    const float highFrac = high / total;

    const float suggestedLowWidth  = lowFrac > 0.35f ? 20.0f : 60.0f;
    const float suggestedMidWidth  = 100.0f;
    const float suggestedHighWidth = highFrac > 0.20f ? 150.0f : 110.0f;
    const float suggestedWidth     = correlation > 0.7f ? 130.0f : (correlation < 0.2f ? 90.0f : 110.0f);

    auto apply = [] (juce::AudioParameterFloat* p, float value)
    {
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    apply (lowWidthParam,  suggestedLowWidth);
    apply (midWidthParam,  suggestedMidWidth);
    apply (highWidthParam, suggestedHighWidth);
    apply (widthParam,     suggestedWidth);
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
