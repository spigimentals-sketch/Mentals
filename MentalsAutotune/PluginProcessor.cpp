#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MentalsAutotuneAudioProcessor::MentalsAutotuneAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    keyParam                 = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("key"));
    scaleParam               = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("scale"));
    retuneSpeedParam         = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("retuneSpeed"));
    amountParam              = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("amount"));
    mixParam                 = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("mix"));
    formantPreservationParam = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("formantPreservation"));
    adaptiveRetuneParam      = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("adaptiveRetune"));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsAutotuneAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "key", "Key",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "scale", "Scale",
        juce::StringArray { "Chromatic", "Major", "Minor" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "retuneSpeed", "Retune Speed",
        juce::NormalisableRange<float> (1.0f, 500.0f, 0.01f, 0.4f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "amount", "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "formantPreservation", "Formant Preservation", true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "adaptiveRetune", "Adaptive Retune", true));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsAutotuneAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    windowSizeSamples = juce::jmax (256, (int) (0.046 * sampleRate));
    hopSizeSamples    = juce::jmax (128, windowSizeSamples / 2);

    analysisRingBuffer.assign ((size_t) windowSizeSamples, 0.0f);
    analysisWorkspace.assign ((size_t) windowSizeSamples, 0.0f);
    analysisWritePos = 0;
    samplesUntilNextHop = hopSizeSamples;

    targetRatio   = 1.0f;
    smoothedRatio = 1.0f;

    for (auto& shifter : pitchShifters)
        shifter.prepare (sampleRate);

    for (auto& corrector : formantCorrectors)
        corrector.prepare();

    // Sized to the formant corrector's fixed latency regardless of whether
    // it's currently enabled, so toggling it on/off at runtime never needs
    // a reallocation on the audio thread.
    for (int ch = 0; ch < maxSupportedChannels; ++ch)
    {
        dryDelayLines[(size_t) ch].assign ((size_t) PitchDSP::FormantCorrector::fftSize, 0.0f);
        dryDelayWritePos[(size_t) ch] = 0;
    }

    stabilityHistory.fill (0.0f);
    stabilityHistoryCount = 0;
    stabilityHistoryPos = 0;

    lastDetectedFreqHz = 0.0f;
    lastTargetFreqHz   = 0.0f;
    lastIsVoiced       = false;

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;

    lastReportedLatencySamples = -1; // force processBlock to (re)announce it on the first block
}

bool MentalsAutotuneAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut.size() >= 1 && mainOut.size() <= maxSupportedChannels;
}

void MentalsAutotuneAudioProcessor::runPitchDetectionAndUpdateTarget()
{
    // Unwrap the ring buffer into chronological order (oldest sample
    // first -- analysisWritePos always points to the oldest remaining
    // sample, the one about to be overwritten next), applying a Hann window.
    for (int i = 0; i < windowSizeSamples; ++i)
    {
        const int idx = (analysisWritePos + i) % windowSizeSamples;
        const float window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (windowSizeSamples - 1));
        analysisWorkspace[(size_t) i] = analysisRingBuffer[(size_t) idx] * window;
    }

    float detectedFreqHz = 0.0f, confidence = 0.0f;
    const bool found = PitchDSP::detectPitch (analysisWorkspace.data(), windowSizeSamples, currentSampleRate,
                                               minDetectableFreqHz, maxDetectableFreqHz, detectedFreqHz, confidence);

    if (found && confidence >= voicedConfidenceThreshold)
    {
        // Map the detected frequency to the nearest note in the selected
        // Key/Scale. Semitone distances are measured from A4 = 440Hz, then
        // re-based so the key's root note sits at an exact multiple of 12
        // before searching for the nearest in-scale semitone.
        const float semitoneFromA4 = 12.0f * std::log2 (detectedFreqHz / 440.0f);
        const int   keyIndex = keyParam->getIndex(); // 0=C..11=B
        const float rootOffsetFromA = (float) (keyIndex - 9); // A is index 9
        const float semitoneFromRoot = semitoneFromA4 - rootOffsetFromA;

        const auto  mask = PitchDSP::getScaleMask (scaleParam->getIndex());
        const int   nearestFromRoot = PitchDSP::nearestScaleSemitone (semitoneFromRoot, mask);
        const float nearestFromA4   = (float) nearestFromRoot + rootOffsetFromA;
        const float targetFreqHz    = 440.0f * std::pow (2.0f, nearestFromA4 / 12.0f);

        const float rawRatio = targetFreqHz / detectedFreqHz;
        const float amount   = juce::jlimit (0.0f, 1.0f, amountParam->get() * 0.01f);
        targetRatio = 1.0f + (rawRatio - 1.0f) * amount;

        lastDetectedFreqHz.store (detectedFreqHz);
        lastTargetFreqHz.store (targetFreqHz);
        lastIsVoiced.store (true);

        // Feed Adaptive Retune's stability tracker -- one push per
        // detection cycle (not per sample).
        stabilityHistory[(size_t) stabilityHistoryPos] = semitoneFromA4;
        stabilityHistoryPos = (stabilityHistoryPos + 1) % stabilityHistoryLength;
        stabilityHistoryCount = juce::jmin (stabilityHistoryCount + 1, stabilityHistoryLength);
    }
    else
    {
        targetRatio = 1.0f; // no confident pitch (silence/unvoiced/noise) -- pass through unshifted
        lastIsVoiced.store (false);
        stabilityHistoryCount = 0; // a gap in voicing resets the stability read, rather than bridging across silence
    }
}

float MentalsAutotuneAudioProcessor::computeStabilityScore() const noexcept
{
    if (stabilityHistoryCount < 2)
        return 0.0f; // not enough history yet -- treat as "unstable" (gentlest correction) until proven otherwise

    float mean = 0.0f;
    for (int i = 0; i < stabilityHistoryCount; ++i)
        mean += stabilityHistory[(size_t) i];
    mean /= (float) stabilityHistoryCount;

    float variance = 0.0f;
    for (int i = 0; i < stabilityHistoryCount; ++i)
    {
        const float diff = stabilityHistory[(size_t) i] - mean;
        variance += diff * diff;
    }
    variance /= (float) stabilityHistoryCount;

    const float stdDevSemitones = std::sqrt (variance);

    // A standard deviation of ~1.5 semitones across the recent window is
    // treated as "fully unstable" (fast run/expressive slide/vibrato);
    // near 0 is "fully stable" (a held, steady note).
    constexpr float maxExpectedMovement = 1.5f;
    const float instability = juce::jlimit (0.0f, 1.0f, stdDevSemitones / maxExpectedMovement);
    return 1.0f - instability;
}

void MentalsAutotuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const int numChannels = juce::jmin (buffer.getNumChannels(), maxSupportedChannels);
    const int numSamples  = buffer.getNumSamples();

    const float mix = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);
    const bool formantPreservationOn = formantPreservationParam->get();
    const bool adaptiveRetuneOn      = adaptiveRetuneParam->get();

    const int desiredLatency = formantPreservationOn ? PitchDSP::FormantCorrector::getLatencySamples() : 0;
    if (desiredLatency != lastReportedLatencySamples)
    {
        lastReportedLatencySamples = desiredLatency;
        setLatencySamples (desiredLatency);
    }

    const float baseRetuneMs = juce::jmax (1.0f, retuneSpeedParam->get());

    for (int n = 0; n < numSamples; ++n)
    {
        float monoSum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            monoSum += buffer.getReadPointer (ch)[n];
        const float mono = numChannels > 0 ? monoSum / (float) numChannels : 0.0f;

        analysisRingBuffer[(size_t) analysisWritePos] = mono;
        analysisWritePos = (analysisWritePos + 1) % windowSizeSamples;

        if (--samplesUntilNextHop <= 0)
        {
            samplesUntilNextHop = hopSizeSamples;
            runPitchDetectionAndUpdateTarget();
        }

        // Adaptive Retune: a steady note (high stability) gets a shorter
        // effective retune time (snappier); a moving pitch (low stability)
        // gets a longer one (gentler, less likely to fight an intentional
        // slide/vibrato). Recomputed every sample is unnecessary given
        // stability itself only updates once per detection cycle, but it's
        // cheap enough not to bother gating further.
        float effectiveRetuneMs = baseRetuneMs;
        if (adaptiveRetuneOn)
        {
            const float stability = computeStabilityScore();
            const float multiplier = juce::jmap (stability, 0.0f, 1.0f, 2.5f, 0.4f);
            effectiveRetuneMs = baseRetuneMs * multiplier;
        }
        const float retuneCoeff = std::exp (-1.0f / (0.001f * effectiveRetuneMs * (float) currentSampleRate));
        smoothedRatio = retuneCoeff * smoothedRatio + (1.0f - retuneCoeff) * targetRatio;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const float dry     = data[n];
            const float shifted = pitchShifters[(size_t) ch].process (dry, smoothedRatio);

            if (formantPreservationOn)
            {
                // The formant corrector's output lags its input by
                // fftSize samples, so the dry signal mixed against it must
                // be delayed by the same amount, or the dry/wet blend would
                // comb-filter against itself.
                auto& delayLine = dryDelayLines[(size_t) ch];
                const int delaySize = (int) delayLine.size();
                int& writePos = dryDelayWritePos[(size_t) ch];

                const float delayedDry = delayLine[(size_t) writePos];
                delayLine[(size_t) writePos] = dry;
                writePos = (writePos + 1) % delaySize;

                const float corrected = formantCorrectors[(size_t) ch].process (dry, shifted);
                data[n] = delayedDry * (1.0f - mix) + corrected * mix;
            }
            else
            {
                data[n] = dry * (1.0f - mix) + shifted * mix;
            }
        }
    }

    updateOutputLevelMeter (buffer);
}

void MentalsAutotuneAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    // ~24dB/second release, so the meter falls smoothly rather than jumping.
    const float releasePerBlock = std::pow (10.0f, -24.0f * ((float) buffer.getNumSamples() / (float) currentSampleRate) / 20.0f);
    const float previous = outputPeakLinear.load();
    outputPeakLinear.store (juce::jmax (peak, previous * releasePerBlock));

    if (peak >= 1.0f)
        clipHoldBlocksRemaining.store ((int) (1.5 * currentSampleRate / juce::jmax (1, buffer.getNumSamples())));
    else if (clipHoldBlocksRemaining.load() > 0)
        clipHoldBlocksRemaining.fetch_sub (1);
}

//==============================================================================
juce::AudioProcessorEditor* MentalsAutotuneAudioProcessor::createEditor()
{
    return new MentalsAutotuneAudioProcessorEditor (*this);
}

void MentalsAutotuneAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsAutotuneAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MentalsAutotuneAudioProcessor();
}
