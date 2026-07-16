#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MentalsAutotuneAudioProcessor::MentalsAutotuneAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                           .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                           .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    keyParam                 = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("key"));
    scaleParam               = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("scale"));
    retuneSpeedParam         = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("retuneSpeed"));
    amountParam              = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("amount"));
    mixParam                 = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("mix"));
    formantPreservationParam = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("formantPreservation"));
    adaptiveRetuneParam      = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("adaptiveRetune"));
    midiControlParam         = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("midiControl"));
    sidechainTuningParam     = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("sidechainTuning"));
    lowLatencyModeParam      = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("lowLatencyMode"));

    ensureFactoryPresetsExist();
}

//==============================================================================
void MentalsAutotuneAudioProcessor::ensureFactoryPresetsExist()
{
    struct ParamValue { const char* id; float value; };
    struct StylePreset { const char* name; std::vector<ParamValue> values; };

    // "key" is deliberately never included below, so loading a style preset
    // never overrides whatever key the user already has dialled in -- these
    // are correction-character presets, not song presets. Robotic/Trap lean
    // into the artificial timbre a plain (formant-uncorrected) shift gives,
    // which is part of their character rather than an oversight.
    const std::vector<StylePreset> stylePresets
    {
        { "Natural", { { "retuneSpeed", 120.0f }, { "amount", 60.0f }, { "mix", 100.0f },
                       { "formantPreservation", 1.0f }, { "adaptiveRetune", 1.0f } } },

        { "Robotic", { { "retuneSpeed", 1.0f }, { "amount", 100.0f }, { "mix", 100.0f },
                       { "formantPreservation", 0.0f }, { "adaptiveRetune", 0.0f } } },

        { "Trap",    { { "retuneSpeed", 10.0f }, { "amount", 100.0f }, { "mix", 100.0f },
                       { "formantPreservation", 0.0f }, { "adaptiveRetune", 0.0f }, { "scale", 0.0f } } },

        { "Choral",  { { "retuneSpeed", 200.0f }, { "amount", 70.0f }, { "mix", 90.0f },
                       { "formantPreservation", 1.0f }, { "adaptiveRetune", 1.0f } } },
    };

    const auto presetsDir = presetManager.getPresetsDirectory();
    const auto parametersTypeName = apvts.state.getType().toString();

    for (auto& style : stylePresets)
    {
        const auto file = presetsDir.getChildFile (juce::String (style.name) + ".xml");
        if (file.existsAsFile())
            continue; // never overwrite a preset the user has already saved under this name

        juce::XmlElement root ("MENTALS_STATE");
        auto* parametersXml = root.createNewChildElement (parametersTypeName);

        for (auto& pv : style.values)
        {
            auto* paramXml = parametersXml->createNewChildElement ("PARAM");
            paramXml->setAttribute ("id", pv.id);
            paramXml->setAttribute ("value", (double) pv.value);
        }

        root.writeTo (file);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsAutotuneAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "key", "Key",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));

    // Built from PitchDSP::getBuiltInScales() rather than a hand-duplicated
    // list, so the parameter's choices and the actual scale data used at
    // runtime can never drift out of sync with each other.
    juce::StringArray scaleNames;
    for (auto& scale : PitchDSP::getBuiltInScales())
        scaleNames.add (scale.name);

    params.push_back (std::make_unique<juce::AudioParameterChoice> ("scale", "Scale", scaleNames, 0));

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

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "midiControl", "MIDI Control", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "sidechainTuning", "Sidechain Tuning", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "lowLatencyMode", "Low-Latency Mode", false));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsAutotuneAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    // Force reconfigureAnalysisWindowIfNeeded() to (re)size the main
    // detector's buffers for the current sample rate and Low-Latency Mode
    // setting right now, rather than waiting for the first block.
    lastLowLatencyModeApplied = ! lowLatencyModeParam->get();
    reconfigureAnalysisWindowIfNeeded();

    sidechainDetector.prepare (sampleRate, normalWindowSeconds);
    heldMidiNotes.clear();

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

void MentalsAutotuneAudioProcessor::reconfigureAnalysisWindowIfNeeded()
{
    const bool lowLatencyNow = lowLatencyModeParam->get();
    if (lowLatencyNow == lastLowLatencyModeApplied)
        return;

    lastLowLatencyModeApplied = lowLatencyNow;

    const double windowSeconds = lowLatencyNow ? lowLatencyWindowSeconds : normalWindowSeconds;
    windowSizeSamples = juce::jmax (256, (int) (windowSeconds * currentSampleRate));
    hopSizeSamples    = juce::jmax (128, windowSizeSamples / 2);

    analysisRingBuffer.assign ((size_t) windowSizeSamples, 0.0f);
    analysisWorkspace.assign ((size_t) windowSizeSamples, 0.0f);
    analysisWritePos = 0;
    samplesUntilNextHop = hopSizeSamples;
}

bool MentalsAutotuneAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sidechain = layouts.getChannelSet (true, 1);
        if (! sidechain.isDisabled() && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return mainOut.size() >= 1 && mainOut.size() <= maxSupportedChannels;
}

void MentalsAutotuneAudioProcessor::processIncomingMidi (juce::MidiBuffer& midi)
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            const int note = message.getNoteNumber();
            // Re-pressing an already-held note (unlikely, but a host could
            // send a duplicate) shouldn't create a duplicate stack entry.
            heldMidiNotes.erase (std::remove (heldMidiNotes.begin(), heldMidiNotes.end(), note), heldMidiNotes.end());
            heldMidiNotes.push_back (note);
        }
        else if (message.isNoteOff())
        {
            const int note = message.getNoteNumber();
            heldMidiNotes.erase (std::remove (heldMidiNotes.begin(), heldMidiNotes.end(), note), heldMidiNotes.end());
        }
    }

    midi.clear(); // not a MIDI effect -- inspected for note tracking only, not passed through
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
    const bool voicedNow = found && confidence >= voicedConfidenceThreshold;

    if (voicedNow)
    {
        lastDetectedFreqHz.store (detectedFreqHz);
        lastIsVoiced.store (true);

        // Feed Adaptive Retune's stability tracker -- one push per
        // detection cycle (not per sample), in semitones (not cents) since
        // that's the unit computeStabilityScore()'s threshold uses.
        const float centsFromA4 = 1200.0f * std::log2 (detectedFreqHz / 440.0f);
        stabilityHistory[(size_t) stabilityHistoryPos] = centsFromA4 / 100.0f;
        stabilityHistoryPos = (stabilityHistoryPos + 1) % stabilityHistoryLength;
        stabilityHistoryCount = juce::jmin (stabilityHistoryCount + 1, stabilityHistoryLength);
    }
    else
    {
        lastIsVoiced.store (false);
        stabilityHistoryCount = 0; // a gap in voicing resets the stability read, rather than bridging across silence
    }

    // The frequency to correct FROM: the input's own detected pitch right
    // now, or (during a brief unvoiced gap, e.g. a consonant) the last
    // known-good one, so MIDI Control/Sidechain Tuning can still produce a
    // sensible ratio instead of momentarily doing nothing.
    const float referenceFreqHz = voicedNow ? detectedFreqHz : lastDetectedFreqHz.load();

    float targetFreqHz = 0.0f;
    bool haveTarget = false;

    if (midiControlParam->get() && ! heldMidiNotes.empty() && referenceFreqHz > 0.0f)
    {
        // Highest priority: a live-held MIDI note drives the target
        // directly, bypassing scale-snapping entirely.
        targetFreqHz = PitchDSP::midiNoteToFrequencyHz (heldMidiNotes.back());
        haveTarget = true;
    }
    else if (sidechainTuningParam->get() && sidechainDetector.isLastVoiced() && referenceFreqHz > 0.0f)
    {
        // Second priority: match whatever pitch the sidechain bus is
        // currently singing/playing.
        targetFreqHz = sidechainDetector.getLastFreqHz();
        haveTarget = true;
    }
    else if (voicedNow)
    {
        // Ordinary scale-snapping: map the detected frequency to the
        // nearest scale degree in the selected Key/Scale, working in cents
        // (not semitones) so microtonal scales -- whose degrees don't all
        // land on integer semitones -- are represented exactly. Cent
        // distances are measured from A4 = 440Hz, then re-based so the
        // key's root note sits at an exact multiple of 1200 cents before
        // searching for the nearest scale degree.
        const float centsFromA4 = 1200.0f * std::log2 (detectedFreqHz / 440.0f);
        const int   keyIndex = keyParam->getIndex(); // 0=C..11=B
        const float rootOffsetFromA = (float) (keyIndex - 9) * 100.0f; // A is index 9
        const float centsFromRoot = centsFromA4 - rootOffsetFromA;

        const auto& scales = PitchDSP::getBuiltInScales();
        const auto& scale  = scales[(size_t) juce::jlimit (0, (int) scales.size() - 1, scaleParam->getIndex())];
        const float nearestFromRoot = PitchDSP::nearestScaleCents (centsFromRoot, scale.centsFromRoot);
        const float nearestFromA4   = nearestFromRoot + rootOffsetFromA;

        targetFreqHz = 440.0f * std::pow (2.0f, nearestFromA4 / 1200.0f);
        haveTarget = true;
    }

    if (haveTarget)
    {
        const float rawRatio = targetFreqHz / referenceFreqHz;
        const float amount   = juce::jlimit (0.0f, 1.0f, amountParam->get() * 0.01f);
        targetRatio = 1.0f + (rawRatio - 1.0f) * amount;
        lastTargetFreqHz.store (targetFreqHz);
    }
    else
    {
        targetRatio = 1.0f; // no confident pitch and no override active -- pass through unshifted
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
    processIncomingMidi (midi);
    reconfigureAnalysisWindowIfNeeded();

    auto mainBuffer = getBusBuffer (buffer, true, 0);
    auto sidechainBuffer = getBusCount (true) > 1 ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const bool sidechainTuningOn = sidechainTuningParam->get() && sidechainBuffer.getNumChannels() > 0;

    const int numChannels = juce::jmin (mainBuffer.getNumChannels(), maxSupportedChannels);
    const int numSamples  = mainBuffer.getNumSamples();

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
            monoSum += mainBuffer.getReadPointer (ch)[n];
        const float mono = numChannels > 0 ? monoSum / (float) numChannels : 0.0f;

        analysisRingBuffer[(size_t) analysisWritePos] = mono;
        analysisWritePos = (analysisWritePos + 1) % windowSizeSamples;

        if (sidechainTuningOn)
        {
            float sidechainSum = 0.0f;
            for (int ch = 0; ch < sidechainBuffer.getNumChannels(); ++ch)
                sidechainSum += sidechainBuffer.getReadPointer (ch)[n];
            const float sidechainMono = sidechainSum / (float) sidechainBuffer.getNumChannels();
            sidechainDetector.pushSample (sidechainMono, minDetectableFreqHz, maxDetectableFreqHz, voicedConfidenceThreshold);
        }

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
            auto* data = mainBuffer.getWritePointer (ch);
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

    updateOutputLevelMeter (mainBuffer);
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
