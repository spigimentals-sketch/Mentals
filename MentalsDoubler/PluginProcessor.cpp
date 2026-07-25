#include "PluginProcessor.h"
#include "PluginEditor.h"

const int MentalsDoublerAudioProcessor::voiceCountChoices[2] = { 2, 4 };

//==============================================================================
float MentalsDoublerAudioProcessor::computeVoiceBaseDetuneCents (int voiceIndex, int numVoices, float detuneParamIn) noexcept
{
    if (numVoices <= 1)
        return 0.0f;
    const float normalizedPos = (2.0f * (float) voiceIndex / (float) (numVoices - 1)) - 1.0f; // -1..1, symmetric
    return normalizedPos * detuneParamIn;
}

float MentalsDoublerAudioProcessor::computeVoiceBaseDelayMs (int voiceIndex, int numVoices, float delayParamIn) noexcept
{
    if (numVoices <= 1)
        return delayParamIn;
    const float normalizedPos = (2.0f * (float) voiceIndex / (float) (numVoices - 1)) - 1.0f;
    // A little spread around the base delay (not identical per voice) so
    // voices don't comb-filter against each other when summed.
    return delayParamIn * (1.0f + normalizedPos * 0.25f);
}

float MentalsDoublerAudioProcessor::computeVoicePan (int voiceIndex, int numVoices, float widthParamIn) noexcept
{
    if (numVoices <= 1)
        return 0.0f;
    const float normalizedPos = (2.0f * (float) voiceIndex / (float) (numVoices - 1)) - 1.0f;
    return normalizedPos * (widthParamIn * 0.01f);
}

//==============================================================================
// HumanizeState
//==============================================================================
void MentalsDoublerAudioProcessor::HumanizeState::prepare (double sampleRateIn, juce::int64 seed) noexcept
{
    sampleRate = sampleRateIn;
    random = juce::Random (seed);
    currentCentsOffset = targetCentsOffset = 0.0f;
    currentMsOffset = targetMsOffset = 0.0f;
    samplesUntilNewTarget = 0;
    // ~150ms glide toward each new random target -- smooth, organic wander
    // rather than a stepped jump or (Chorus's job) a clean periodic sweep.
    followCoeff = 1.0f - std::exp (-1.0f / (0.15f * (float) sampleRate));
}

void MentalsDoublerAudioProcessor::HumanizeState::advance (float humanizeAmount01) noexcept
{
    if (--samplesUntilNewTarget <= 0)
    {
        targetCentsOffset = (random.nextFloat() * 2.0f - 1.0f) * MentalsDoublerAudioProcessor::maxHumanizeCents * humanizeAmount01;
        targetMsOffset    = (random.nextFloat() * 2.0f - 1.0f) * MentalsDoublerAudioProcessor::maxHumanizeMs * humanizeAmount01;
        // Next retarget in a randomized ~150-400ms -- independent per voice
        // (each has its own Random instance/seed), so voices drift apart
        // from each other rather than all wandering in lockstep.
        samplesUntilNewTarget = juce::jmax (1, (int) ((0.15 + random.nextDouble() * 0.25) * sampleRate));
    }

    currentCentsOffset += (targetCentsOffset - currentCentsOffset) * followCoeff;
    currentMsOffset    += (targetMsOffset    - currentMsOffset)    * followCoeff;
}

//==============================================================================
MentalsDoublerAudioProcessor::MentalsDoublerAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    voicesParam   = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("voices"));
    detuneParam   = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("detune"));
    delayParam    = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("delay"));
    widthParam    = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("width"));
    humanizeParam = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("humanize"));
    lowCutParam   = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("lowCut"));
    mixParam      = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("mix"));
    stereoParam   = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("stereo"));

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsDoublerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "voices", "Voices", juce::StringArray { "2", "4" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "detune", "Detune",
        juce::NormalisableRange<float> (0.0f, maxDetuneCents, 0.01f), 15.0f,
        juce::AudioParameterFloatAttributes().withLabel ("cents")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delay", "Delay",
        juce::NormalisableRange<float> (0.0f, maxDelayMs, 0.01f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "width", "Width",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 80.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "humanize", "Humanize",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 30.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lowCut", "Low Cut",
        juce::NormalisableRange<float> (20.0f, 500.0f, 0.1f, 0.35f), 20.0f, // 20Hz default is effectively off
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereo", "Stereo", true));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsDoublerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    const int delayLineSize = juce::jmax (8, (int) (0.001 * (maxDelayMs * 1.25f + maxHumanizeMs + 5.0f) * sampleRate));

    for (int i = 0; i < maxVoices; ++i)
    {
        auto& voice = voices[(size_t) i];
        voice.pitchShifter.prepare (sampleRate);
        voice.delayLine.assign ((size_t) delayLineSize, 0.0f);
        voice.delayWritePos = 0;
        // Deterministic-but-distinct seed per voice, so the ensemble's
        // humanize character is stable across reloads rather than
        // reshuffling every time (same rationale as Vox Choir's per-voice
        // vibrato phase seeding).
        voice.humanize.prepare (sampleRate, (juce::int64) (0x5eed0000 + i * 7919));
    }

    for (auto& f : lowCutFilter)
        f.reset();
    lastLowCutFreqHz = -1.0f;

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
}

bool MentalsDoublerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void MentalsDoublerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    const int numVoices = voiceCountChoices[(size_t) juce::jlimit (0, 1, voicesParam->getIndex())];
    const float detune    = detuneParam->get();
    const float delayMs   = delayParam->get();
    // Mono forces every voice dead centre regardless of Width -- a layered
    // but centred double, or a quick check of how it sits before committing
    // to a wide stereo spread.
    const float width     = stereoParam->get() ? widthParam->get() : 0.0f;
    const float humanize  = juce::jlimit (0.0f, 1.0f, humanizeParam->get() * 0.01f);
    const float mix       = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);

    const float hpfFreq = lowCutParam->get();
    if (hpfFreq != lastLowCutFreqHz)
    {
        lastLowCutFreqHz = hpfFreq;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, hpfFreq, lowCutQ);
        for (auto& f : lowCutFilter)
            f.coefficients = coeffs;
    }

    // Precomputed per-voice base detune/delay/pan -- constant across the
    // block (only humanize wanders per-sample), and computed via the exact
    // same functions the editor's voice-spread display calls.
    std::array<float, maxVoices> baseCents {}, baseDelayMs {}, pan {};
    for (int v = 0; v < numVoices; ++v)
    {
        baseCents[(size_t) v]    = computeVoiceBaseDetuneCents (v, numVoices, detune);
        baseDelayMs[(size_t) v]  = computeVoiceBaseDelayMs (v, numVoices, delayMs);
        pan[(size_t) v]          = computeVoicePan (v, numVoices, width);
    }

    const float voiceNormalise = 1.0f / std::sqrt ((float) juce::jmax (1, numVoices));

    for (int n = 0; n < numSamples; ++n)
    {
        float monoSum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            monoSum += buffer.getReadPointer (ch)[n];
        const float dry = numChannels > 0 ? monoSum / (float) numChannels : 0.0f;

        float wetL = 0.0f, wetR = 0.0f;

        for (int v = 0; v < numVoices; ++v)
        {
            auto& voice = voices[(size_t) v];
            voice.humanize.advance (humanize);

            const float totalCents = baseCents[(size_t) v] + voice.humanize.currentCentsOffset;
            const float ratio = std::pow (2.0f, totalCents / 1200.0f);
            const float shifted = voice.pitchShifter.process (dry, ratio);

            auto& delayLine = voice.delayLine;
            const int bufSize = (int) delayLine.size();
            delayLine[(size_t) voice.delayWritePos] = shifted;

            const float delaySamples = juce::jmax (0.0f,
                (baseDelayMs[(size_t) v] + voice.humanize.currentMsOffset) * 0.001f * (float) currentSampleRate);

            float readPos = (float) voice.delayWritePos - delaySamples;
            while (readPos < 0.0f) readPos += (float) bufSize;
            const int i0 = ((int) readPos) % bufSize;
            const int i1 = (i0 + 1) % bufSize;
            const float frac = readPos - std::floor (readPos);
            const float delayed = delayLine[(size_t) i0] * (1.0f - frac) + delayLine[(size_t) i1] * frac;

            voice.delayWritePos = (voice.delayWritePos + 1) % bufSize;

            // Equal-power pan: pan in [-1, 1] -> angle in [0, halfPi].
            const float panAngle = (pan[(size_t) v] * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
            wetL += delayed * std::cos (panAngle);
            wetR += delayed * std::sin (panAngle);
        }

        wetL *= voiceNormalise;
        wetR *= voiceNormalise;

        wetL = lowCutFilter[0].processSample (wetL);
        wetR = lowCutFilter[1].processSample (wetR);

        const float outL = dry * (1.0f - mix) + wetL * mix;
        const float outR = dry * (1.0f - mix) + wetR * mix;

        if (numChannels > 0) buffer.getWritePointer (0)[n] = outL;
        if (numChannels > 1) buffer.getWritePointer (1)[n] = outR;
    }

    updateOutputLevelMeter (buffer);
}

void MentalsDoublerAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
juce::AudioProcessorEditor* MentalsDoublerAudioProcessor::createEditor()
{
    return new MentalsDoublerAudioProcessorEditor (*this);
}

void MentalsDoublerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsDoublerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
// Factory presets: common doubling starting points.
//==============================================================================
void MentalsDoublerAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto applyF = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };
    auto applyChoice = [] (juce::AudioParameterChoice* p, int index) { p->setValueNotifyingHost (p->convertTo0to1 ((float) index)); };

    resetToDefault();
    applyChoice (voicesParam, 0);
    applyF (detuneParam, 8.0f); applyF (delayParam, 15.0f); applyF (widthParam, 60.0f);
    applyF (humanizeParam, 25.0f); applyF (mixParam, 40.0f);
    presetManager.savePreset ("Subtle Vocal Double");

    resetToDefault();
    applyChoice (voicesParam, 1);
    applyF (detuneParam, 18.0f); applyF (delayParam, 22.0f); applyF (widthParam, 90.0f);
    applyF (humanizeParam, 40.0f); applyF (mixParam, 55.0f);
    presetManager.savePreset ("Wide Guitar Double");

    resetToDefault();
    applyChoice (voicesParam, 0);
    applyF (detuneParam, 25.0f); applyF (delayParam, 30.0f); applyF (widthParam, 100.0f);
    applyF (humanizeParam, 60.0f); applyF (mixParam, 65.0f);
    presetManager.savePreset ("Thick 80s ADT");

    resetToDefault();
    applyChoice (voicesParam, 1);
    applyF (detuneParam, 12.0f); applyF (delayParam, 12.0f); applyF (widthParam, 70.0f);
    applyF (humanizeParam, 15.0f); applyF (mixParam, 35.0f);
    presetManager.savePreset ("Tight Unison");

    resetToDefault();
}
