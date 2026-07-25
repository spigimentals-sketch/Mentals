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
    voiceTypeParam           = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("voiceType"));
    retuneSpeedParam         = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("retuneSpeed"));
    amountParam              = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("amount"));
    mixParam                 = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("mix"));
    formantPreservationParam = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("formantPreservation"));
    adaptiveRetuneParam      = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("adaptiveRetune"));
    midiControlParam         = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("midiControl"));
    sidechainTuningParam     = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("sidechainTuning"));
    lowLatencyModeParam      = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("lowLatencyMode"));
    flexAmountParam          = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("flexAmount"));

    harmony1EnabledParam = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("harmony1Enabled"));
    harmony1DegreeParam  = dynamic_cast<juce::AudioParameterInt*>   (apvts.getParameter ("harmony1Degree"));
    harmony1LevelParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("harmony1Level"));
    harmony2EnabledParam = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("harmony2Enabled"));
    harmony2DegreeParam  = dynamic_cast<juce::AudioParameterInt*>   (apvts.getParameter ("harmony2Degree"));
    harmony2LevelParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("harmony2Level"));
    stereoParam          = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("stereo"));

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

    // Built from PitchDSP::getVoiceTypeRanges() for the same reason as
    // "scale" above -- the parameter's choices and the actual min/max
    // detection range used at runtime can never drift out of sync.
    juce::StringArray voiceTypeNames;
    for (auto& range : PitchDSP::getVoiceTypeRanges())
        voiceTypeNames.add (range.name);

    params.push_back (std::make_unique<juce::AudioParameterChoice> ("voiceType", "Voice Type", voiceTypeNames, 0));

    // Defaults match the built-in "Natural" style preset (see
    // ensureFactoryPresetsExist()) rather than the previous out-of-the-box
    // defaults of 100% Amount / 50ms Retune Speed, which correct hard and
    // fast enough to sound close to a robotic hard-tune -- a jarring first
    // impression for anyone loading the plugin fresh and expecting ordinary
    // pitch correction. Robotic/Trap are still one preset click away for
    // anyone who wants that sound deliberately.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "retuneSpeed", "Retune Speed",
        juce::NormalisableRange<float> (1.0f, 500.0f, 0.01f, 0.4f), 120.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "amount", "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 60.0f,
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

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "flexAmount", "Flex-Tune",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "harmony1Enabled", "Harmony 1", false));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "harmony1Degree", "Harmony 1 Degree", -12, 12, 2)); // +2 scale degrees = a "3rd above" on a 7-note scale
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmony1Level", "Harmony 1 Level",
        juce::NormalisableRange<float> (-24.0f, 0.0f, 0.01f), -6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "harmony2Enabled", "Harmony 2", false));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "harmony2Degree", "Harmony 2 Degree", -12, 12, -2)); // a "3rd below" by default
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmony2Level", "Harmony 2 Level",
        juce::NormalisableRange<float> (-24.0f, 0.0f, 0.01f), -6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereo", "Stereo", true));

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

    // Same forced-refresh trick for Voice Type's min/max detection range.
    lastVoiceTypeIndexApplied = -1;
    reconfigureVoiceRangeIfNeeded();

    sidechainDetector.prepare (sampleRate, normalWindowSeconds);
    heldMidiNotes.clear();

    targetRatio   = 1.0f;
    targetCents   = 0.0f;
    smoothedCents = 0.0f;
    targetHarmony1Ratio = 1.0f; targetHarmony1Cents = 0.0f; smoothedHarmony1Cents = 0.0f;
    targetHarmony2Ratio = 1.0f; targetHarmony2Cents = 0.0f; smoothedHarmony2Cents = 0.0f;

    for (auto& shifter : pitchShifters)
        shifter.prepare (sampleRate);
    for (auto& shifter : harmony1Shifters)
        shifter.prepare (sampleRate);
    for (auto& shifter : harmony2Shifters)
        shifter.prepare (sampleRate);

    for (auto& corrector : formantCorrectors)
        corrector.prepare();

    vocalAnalysisSemitones.assign ((size_t) vocalAnalysisCaptureCount, 0.0f);
    vocalAnalysisCollected = 0;
    vocalAnalysisCapturing = false;
    vocalAnalysisReady = false;

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

void MentalsAutotuneAudioProcessor::reconfigureVoiceRangeIfNeeded()
{
    const int voiceTypeIndex = voiceTypeParam->getIndex();
    if (voiceTypeIndex == lastVoiceTypeIndexApplied)
        return;

    lastVoiceTypeIndexApplied = voiceTypeIndex;

    const auto& ranges = PitchDSP::getVoiceTypeRanges();
    const auto& range  = ranges[(size_t) juce::jlimit (0, (int) ranges.size() - 1, voiceTypeIndex)];
    currentMinFreqHz = range.minFreqHz;
    currentMaxFreqHz = range.maxFreqHz;
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
                                               currentMinFreqHz, currentMaxFreqHz, detectedFreqHz, confidence);
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

        // AI Assist capture: only ever appends, never resizes, while
        // capturing -- see the member comment for why that makes reading it
        // from the message thread (once vocalAnalysisReady is observed)
        // safe without a lock.
        if (vocalAnalysisCapturing.load() && vocalAnalysisCollected < vocalAnalysisCaptureCount)
        {
            vocalAnalysisSemitones[(size_t) vocalAnalysisCollected++] = centsFromA4 / 100.0f;
            if (vocalAnalysisCollected >= vocalAnalysisCaptureCount)
            {
                vocalAnalysisCapturing.store (false);
                vocalAnalysisReady.store (true);
            }
        }
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
    float flexCentsOff = 0.0f; // only set during ordinary scale-snapping -- see Flex-Tune below

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

        flexCentsOff = std::abs (nearestFromA4 - centsFromA4);

        targetFreqHz = 440.0f * std::pow (2.0f, nearestFromA4 / 1200.0f);
        haveTarget = true;

        // Harmonizer: only engages here, during ordinary scale-snapping
        // (see class comment) -- each voice shifts the DRY signal from the
        // detected pitch to a target N scale degrees away from the note
        // just matched above.
        if (harmony1EnabledParam->get())
        {
            const float harmony1FromRoot = PitchDSP::nearestScaleDegreeCents (centsFromRoot, scale.centsFromRoot, harmony1DegreeParam->get());
            const float harmony1FreqHz   = 440.0f * std::pow (2.0f, (harmony1FromRoot + rootOffsetFromA) / 1200.0f);
            targetHarmony1Ratio = harmony1FreqHz / detectedFreqHz;
        }
        if (harmony2EnabledParam->get())
        {
            const float harmony2FromRoot = PitchDSP::nearestScaleDegreeCents (centsFromRoot, scale.centsFromRoot, harmony2DegreeParam->get());
            const float harmony2FreqHz   = 440.0f * std::pow (2.0f, (harmony2FromRoot + rootOffsetFromA) / 1200.0f);
            targetHarmony2Ratio = harmony2FreqHz / detectedFreqHz;
        }
    }

    if (haveTarget)
    {
        const float rawRatio = targetFreqHz / referenceFreqHz;
        float amount = juce::jlimit (0.0f, 1.0f, amountParam->get() * 0.01f);

        const float flexAmount = juce::jlimit (0.0f, 1.0f, flexAmountParam->get() * 0.01f);
        if (flexAmount > 0.0f)
        {
            // Flex-Tune: scale the correction down while still far from the
            // target, ramping back up to full Amount as the pitch closes in,
            // blended in by the Flex-Tune knob (0% = no effect, a hard snap;
            // 100% = the full easing curve below) -- 100 cents (one
            // semitone) as the "fully far" reference point and a 35% floor
            // there are practical calibration choices, not measured from a
            // corpus of real vocal recordings.
            constexpr float flexRangeCents = 100.0f;
            constexpr float flexFloorScale = 0.35f;
            const float flexT = juce::jlimit (0.0f, 1.0f, flexCentsOff / flexRangeCents);
            const float fullFlexScale = juce::jmap (flexT, 1.0f, flexFloorScale);
            amount *= juce::jmap (flexAmount, 1.0f, fullFlexScale);
        }

        targetRatio = 1.0f + (rawRatio - 1.0f) * amount;
        lastTargetFreqHz.store (targetFreqHz);
    }
    else
    {
        targetRatio = 1.0f; // no confident pitch and no override active -- pass through unshifted
    }

    // Mirror the ratio targets in cents -- see the class/member comments
    // for why the per-sample glide smooths in cents rather than linear
    // ratio space. Recomputed unconditionally here (covering both branches
    // above, and whether or not either harmony voice is currently enabled)
    // is simpler and cheap enough at hop rate (tens of Hz) not to bother
    // gating further.
    targetCents         = 1200.0f * std::log2 (juce::jmax (1.0e-6f, targetRatio));
    targetHarmony1Cents = 1200.0f * std::log2 (juce::jmax (1.0e-6f, targetHarmony1Ratio));
    targetHarmony2Cents = 1200.0f * std::log2 (juce::jmax (1.0e-6f, targetHarmony2Ratio));
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

void MentalsAutotuneAudioProcessor::beginVocalAnalysis()
{
    vocalAnalysisCollected = 0;
    vocalAnalysisReady.store (false);
    vocalAnalysisCapturing.store (true);
}

bool MentalsAutotuneAudioProcessor::applySuggestedVocalSettings()
{
    if (! vocalAnalysisReady.load())
        return false;

    // Copy out before clearing the ready flag -- a fresh beginVocalAnalysis()
    // call could otherwise start overwriting the buffer again soon after.
    const std::vector<float> semitones = vocalAnalysisSemitones;
    vocalAnalysisReady.store (false);

    if (semitones.size() < 2)
        return false;

    float sumAbsDelta = 0.0f;
    float sumSemitone = 0.0f;
    float minSemitone = semitones[0], maxSemitone = semitones[0];
    for (size_t i = 0; i < semitones.size(); ++i)
    {
        minSemitone = juce::jmin (minSemitone, semitones[i]);
        maxSemitone = juce::jmax (maxSemitone, semitones[i]);
        sumSemitone += semitones[i];
        if (i > 0)
            sumAbsDelta += std::abs (semitones[i] - semitones[i - 1]);
    }
    const float avgAbsDelta = sumAbsDelta / (float) (semitones.size() - 1); // semitones of movement per hop, on average
    const float pitchRange  = maxSemitone - minSemitone;

    const float meanSemitone = sumSemitone / (float) semitones.size();
    float sumSquaredDiff = 0.0f;
    for (float s : semitones)
        sumSquaredDiff += (s - meanSemitone) * (s - meanSemitone);
    const float stdDevSemitone = std::sqrt (sumSquaredDiff / (float) semitones.size());

    // See AiAssistModel.h: a regressor (Retune Speed/Amount) and classifier
    // (style label) trained on real VocalSet singing audio, run on the same
    // three numbers -- avgAbsDelta, pitchRange, stdDevSemitone -- that this
    // feature's original heuristic formula used.
    const auto suggestion = aiAssistModel.predict (avgAbsDelta, pitchRange, stdDevSemitone);
    if (! suggestion.has_value())
        return false; // AI Assist model unavailable on this machine -- nothing to apply

    retuneSpeedParam->setValueNotifyingHost (retuneSpeedParam->convertTo0to1 (suggestion->retuneMs));
    amountParam->setValueNotifyingHost (amountParam->convertTo0to1 (suggestion->amount));

    lastSuggestedRetuneMs.store (suggestion->retuneMs);
    lastSuggestedAmount.store (suggestion->amount);
    lastAnalysisLabelIndex.store (suggestion->labelIndex);

    return true;
}

void MentalsAutotuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    processIncomingMidi (midi);
    reconfigureAnalysisWindowIfNeeded();
    reconfigureVoiceRangeIfNeeded();

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

    const bool harmony1On = harmony1EnabledParam->get();
    const bool harmony2On = harmony2EnabledParam->get();
    const float harmony1Gain = juce::Decibels::decibelsToGain (harmony1LevelParam->get());
    const float harmony2Gain = juce::Decibels::decibelsToGain (harmony2LevelParam->get());

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
            sidechainDetector.pushSample (sidechainMono, currentMinFreqHz, currentMaxFreqHz, voicedConfidenceThreshold);
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
        smoothedCents = retuneCoeff * smoothedCents + (1.0f - retuneCoeff) * targetCents;
        smoothedHarmony1Cents = retuneCoeff * smoothedHarmony1Cents + (1.0f - retuneCoeff) * targetHarmony1Cents;
        smoothedHarmony2Cents = retuneCoeff * smoothedHarmony2Cents + (1.0f - retuneCoeff) * targetHarmony2Cents;

        // Converted back to ratios once per sample (not per channel) --
        // this is what the pitch shifters actually need to run.
        const float smoothedRatio         = std::pow (2.0f, smoothedCents / 1200.0f);
        const float smoothedHarmony1Ratio = std::pow (2.0f, smoothedHarmony1Cents / 1200.0f);
        const float smoothedHarmony2Ratio = std::pow (2.0f, smoothedHarmony2Cents / 1200.0f);

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

            // Harmonizer: added on top of whatever the main voice's output
            // is, each at its own level -- these voices have no "dry"
            // equivalent of their own, so they aren't part of the Mix
            // blend above, just extra material layered in.
            if (harmony1On)
                data[n] += harmony1Shifters[(size_t) ch].process (dry, smoothedHarmony1Ratio) * harmony1Gain;
            if (harmony2On)
                data[n] += harmony2Shifters[(size_t) ch].process (dry, smoothedHarmony2Ratio) * harmony2Gain;
        }
    }

    if (! stereoParam->get() && numChannels > 1)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float avg = 0.5f * (mainBuffer.getReadPointer (0)[n] + mainBuffer.getReadPointer (1)[n]);
            mainBuffer.getWritePointer (0)[n] = avg;
            mainBuffer.getWritePointer (1)[n] = avg;
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
