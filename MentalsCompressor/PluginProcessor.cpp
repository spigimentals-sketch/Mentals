#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MentalsCompressorAudioProcessor::MentalsCompressorAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                           .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                           .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    thresholdParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("threshold"));
    ratioParam        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("ratio"));
    kneeParam         = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("knee"));
    attackParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("attack"));
    releaseParam      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("release"));
    makeupGainParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("makeupGain"));
    mixParam          = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("mix"));
    useSidechainParam = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("useSidechain"));
    stereoParam       = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("stereo"));

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsCompressorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "threshold", "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f), -18.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ratio", "Ratio",
        juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f), 4.0f,
        juce::AudioParameterFloatAttributes().withLabel (":1")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "knee", "Knee",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f), 6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "attack", "Attack",
        juce::NormalisableRange<float> (0.1f, 100.0f, 0.01f, 0.4f), 10.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release",
        juce::NormalisableRange<float> (10.0f, 1000.0f, 0.01f, 0.4f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "makeupGain", "Makeup",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "useSidechain", "Use External Sidechain", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereo", "Stereo", true));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsCompressorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    envelopeFollower.prepare (sampleRate);
    envelopeFollower.setAttackRelease (attackParam->get(), releaseParam->get());
    envelopeFollower.reset();

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
    currentGainReductionDb = 0.0f;

    // AI Assist's onset detector: ~20ms peak-hold chunks compared against a
    // slower-following floor (see the member comment in the header). 20ms
    // is longer than half the period of any real bass content (down to
    // ~25Hz), so a sustained low tone's own full-wave-rectified ripple
    // always lands inside a single chunk rather than aliasing against the
    // chunk boundaries and reading as a string of false transients.
    aiAssistChunkSizeSamples = juce::jmax (1, (int) (0.02 * sampleRate));
    aiAssistFloorFollowCoeff = 1.0f - std::exp (-(float) aiAssistChunkSizeSamples / (0.25f * (float) sampleRate));
    aiAssistCapturing.store (false);
}

bool MentalsCompressorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void MentalsCompressorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    auto mainBuffer = getBusBuffer (buffer, true, 0);
    auto sidechainBuffer = getBusCount (true) > 1 ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const bool useSidechain = useSidechainParam->get() && sidechainBuffer.getNumChannels() > 0;

    const int numChannels = mainBuffer.getNumChannels();
    const int numSamples  = mainBuffer.getNumSamples();

    const float thresholdDb  = thresholdParam->get();
    const float ratio        = juce::jmax (1.0f, ratioParam->get());
    const float kneeDb       = kneeParam->get();
    const float makeupGain   = juce::Decibels::decibelsToGain (makeupGainParam->get());
    const float mix          = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);

    envelopeFollower.setAttackRelease (attackParam->get(), releaseParam->get());

    float blockMinGainReductionDb = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        float levelAbs = 0.0f;

        if (useSidechain)
        {
            for (int ch = 0; ch < sidechainBuffer.getNumChannels(); ++ch)
                levelAbs = juce::jmax (levelAbs, std::abs (sidechainBuffer.getReadPointer (ch)[n]));
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
                levelAbs = juce::jmax (levelAbs, std::abs (mainBuffer.getReadPointer (ch)[n]));
        }

        if (aiAssistCapturing.load (std::memory_order_relaxed))
            captureAiAssistSample (levelAbs);

        const float envelope   = envelopeFollower.process (levelAbs);
        const float envelopeDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
        const float outputDb   = MentalsUI::DynamicsDSP::computeOutputDb (envelopeDb, thresholdDb, ratio, kneeDb);
        const float gainReductionDb = outputDb - envelopeDb;
        const float gainLinear = juce::Decibels::decibelsToGain (gainReductionDb);

        blockMinGainReductionDb = juce::jmin (blockMinGainReductionDb, gainReductionDb);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = mainBuffer.getWritePointer (ch);
            const float dry = data[n];
            const float wet = dry * gainLinear * makeupGain;
            data[n] = dry * (1.0f - mix) + wet * mix;
        }
    }

    currentGainReductionDb.store (blockMinGainReductionDb);

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

void MentalsCompressorAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
// AI Assist
//==============================================================================
void MentalsCompressorAudioProcessor::beginAiAssistAnalysis()
{
    aiAssistSamplesRemaining = (int) (aiAssistCaptureSeconds * currentSampleRate);
    aiAssistSumSquares = 0.0;
    aiAssistSampleCount = 0;
    aiAssistPeakLinear = 0.0f;
    aiAssistTransientCount = 0;
    aiAssistChunkSamplesRemaining = aiAssistChunkSizeSamples;
    aiAssistChunkPeakLinear = 0.0f;
    aiAssistFloorLinear = 0.0f;
    aiAssistReady.store (false);
    aiAssistCapturing.store (true);
}

void MentalsCompressorAudioProcessor::captureAiAssistSample (float levelAbs) noexcept
{
    aiAssistSumSquares += (double) levelAbs * (double) levelAbs;
    ++aiAssistSampleCount;
    aiAssistPeakLinear = juce::jmax (aiAssistPeakLinear, levelAbs);

    aiAssistChunkPeakLinear = juce::jmax (aiAssistChunkPeakLinear, levelAbs);

    if (--aiAssistChunkSamplesRemaining <= 0)
    {
        constexpr float onsetThresholdDb = 6.0f;
        const float chunkPeakDb = juce::Decibels::gainToDecibels (aiAssistChunkPeakLinear, -100.0f);
        const float floorDb = juce::Decibels::gainToDecibels (aiAssistFloorLinear, -100.0f);

        if (chunkPeakDb - floorDb > onsetThresholdDb)
            ++aiAssistTransientCount;

        // Floor update happens AFTER the comparison above, using the same
        // chunk that was just judged -- so a genuine transient is compared
        // against the still-lagging pre-transient floor, not one already
        // dragged up by itself.
        aiAssistFloorLinear += (aiAssistChunkPeakLinear - aiAssistFloorLinear) * aiAssistFloorFollowCoeff;

        aiAssistChunkPeakLinear = 0.0f;
        aiAssistChunkSamplesRemaining = aiAssistChunkSizeSamples;
    }

    if (--aiAssistSamplesRemaining <= 0)
    {
        const float meanSquare = aiAssistSampleCount > 0 ? (float) (aiAssistSumSquares / (double) aiAssistSampleCount) : 0.0f;
        const float rmsDb = juce::Decibels::gainToDecibels (std::sqrt (meanSquare), -100.0f);
        const float peakDb = juce::Decibels::gainToDecibels (aiAssistPeakLinear, -100.0f);
        const float transientRate = (float) aiAssistTransientCount / aiAssistCaptureSeconds;

        aiAssistCapturedRmsDb.store (rmsDb);
        aiAssistCapturedPeakDb.store (peakDb);
        aiAssistCapturedTransientRate.store (transientRate);

        aiAssistCapturing.store (false);
        aiAssistReady.store (true);
    }
}

bool MentalsCompressorAudioProcessor::applySuggestedCompressorSettings()
{
    if (! aiAssistReady.load())
        return false;

    const float rmsDb = aiAssistCapturedRmsDb.load();
    const float peakDb = aiAssistCapturedPeakDb.load();
    const float transientRate = aiAssistCapturedTransientRate.load();
    aiAssistReady.store (false); // consumed

    if (rmsDb <= -60.0f)
        return false; // captured window was effectively silent -- nothing meaningful to suggest

    const float crestDb = juce::jlimit (0.0f, 30.0f, peakDb - rmsDb);

    // Threshold: a few dB above the measured average level, so the
    // compressor engages on louder-than-average passages/transients while
    // leaving quieter material alone.
    const float suggestedThreshold = juce::jlimit (-60.0f, 0.0f, rmsDb + 3.0f);

    // Ratio: a wide peak-to-average gap (drums, percussive sources) gets a
    // gentler ratio so transients still poke through; already-dense
    // material (bass, sustained vocals) gets squeezed harder since there's
    // less transient content worth protecting. crestDb is clamped to the
    // mapping's own 4-18dB design range BEFORE jmap -- real material can
    // sit well outside that (a sparse drum hit against silence can measure
    // a crest factor of 40dB+), and jmap() extrapolates rather than clamps,
    // so an unclamped input here could push the ratio to a nonsensical
    // extreme (including below 1:1, i.e. no compression at all).
    const float suggestedRatio = juce::jlimit (1.0f, 20.0f,
        juce::jmap (juce::jlimit (4.0f, 18.0f, crestDb), 4.0f, 18.0f, 5.0f, 2.5f));

    // Attack: frequent transients need a slower attack so each hit's punch
    // gets through before gain reduction clamps down; sparse transients
    // (sustained pads/vocals) can take a fast attack safely.
    const float suggestedAttack = juce::jlimit (0.1f, 100.0f, juce::jmap (transientRate, 0.0f, 8.0f, 3.0f, 30.0f));

    // Release: the mirror image -- percussive material wants a short
    // release so the compressor recovers before the next hit; sustained
    // material wants a long, smooth release to avoid audible pumping.
    const float suggestedRelease = juce::jlimit (10.0f, 1000.0f, juce::jmap (transientRate, 0.0f, 8.0f, 350.0f, 90.0f));

    // Makeup: reuses the exact same gain-computer math processBlock() and
    // the transfer-curve display already call, so the compensation can
    // never disagree with what these settings will actually do to a signal
    // sitting at the captured RMS level. Only 80% compensated back rather
    // than fully matched -- a deliberately conservative starting point,
    // not a precise loudness match (which would need the real envelope
    // follower's response, not just a static level).
    const float outputAtRms = MentalsUI::DynamicsDSP::computeOutputDb (rmsDb, suggestedThreshold, suggestedRatio, kneeParam->get());
    const float expectedGrDb = rmsDb - outputAtRms;
    const float suggestedMakeup = juce::jlimit (0.0f, 24.0f, expectedGrDb * 0.8f);

    thresholdParam->setValueNotifyingHost (thresholdParam->convertTo0to1 (suggestedThreshold));
    ratioParam->setValueNotifyingHost (ratioParam->convertTo0to1 (suggestedRatio));
    attackParam->setValueNotifyingHost (attackParam->convertTo0to1 (suggestedAttack));
    releaseParam->setValueNotifyingHost (releaseParam->convertTo0to1 (suggestedRelease));
    makeupGainParam->setValueNotifyingHost (makeupGainParam->convertTo0to1 (suggestedMakeup));

    return true;
}

//==============================================================================
juce::AudioProcessorEditor* MentalsCompressorAudioProcessor::createEditor()
{
    return new MentalsCompressorAudioProcessorEditor (*this);
}

void MentalsCompressorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsCompressorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
// Factory presets: common per-source compression starting points, in the
// spirit of the source-based preset menus character plugins like Abbey
// Road's own compressor/bus-glue tools ship -- not a copy of any specific
// product's exact values, just the same "pick your source, get a sane
// starting point" idea applied with this compressor's own soft-knee
// transfer function.
//==============================================================================
void MentalsCompressorAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto applyF = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };

    resetToDefault();
    applyF (thresholdParam, -20.0f); applyF (ratioParam, 3.0f); applyF (kneeParam, 8.0f);
    applyF (attackParam, 15.0f); applyF (releaseParam, 120.0f); applyF (makeupGainParam, 3.0f);
    presetManager.savePreset ("Vocal Leveler");

    resetToDefault();
    applyF (thresholdParam, -12.0f); applyF (ratioParam, 4.0f); applyF (kneeParam, 4.0f);
    applyF (attackParam, 20.0f); applyF (releaseParam, 200.0f); applyF (makeupGainParam, 2.0f);
    presetManager.savePreset ("Drum Bus Glue");

    resetToDefault();
    applyF (thresholdParam, -18.0f); applyF (ratioParam, 5.0f); applyF (kneeParam, 3.0f);
    applyF (attackParam, 5.0f); applyF (releaseParam, 100.0f); applyF (makeupGainParam, 3.0f);
    presetManager.savePreset ("Bass Tighten");

    resetToDefault();
    applyF (thresholdParam, -8.0f); applyF (ratioParam, 2.0f); applyF (kneeParam, 10.0f);
    applyF (attackParam, 30.0f); applyF (releaseParam, 300.0f); applyF (makeupGainParam, 1.0f);
    presetManager.savePreset ("Master Bus Glue");

    resetToDefault();
    applyF (thresholdParam, -30.0f); applyF (ratioParam, 8.0f); applyF (kneeParam, 2.0f);
    applyF (attackParam, 1.0f); applyF (releaseParam, 60.0f); applyF (makeupGainParam, 6.0f);
    presetManager.savePreset ("Punch Parallel");

    resetToDefault();
    applyF (thresholdParam, -16.0f); applyF (ratioParam, 3.0f); applyF (kneeParam, 8.0f);
    applyF (attackParam, 10.0f); applyF (releaseParam, 150.0f); applyF (makeupGainParam, 2.0f);
    presetManager.savePreset ("Acoustic Guitar Smooth");

    resetToDefault();
    applyF (thresholdParam, -14.0f); applyF (ratioParam, 4.0f); applyF (kneeParam, 3.0f);
    applyF (attackParam, 2.0f); applyF (releaseParam, 90.0f); applyF (makeupGainParam, 3.0f);
    presetManager.savePreset ("Snare Snap");

    resetToDefault();
}
