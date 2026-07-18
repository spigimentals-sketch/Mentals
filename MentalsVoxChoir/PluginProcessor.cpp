#include "PluginProcessor.h"
#include "PluginEditor.h"

const int MentalsVoxChoirAudioProcessor::voiceCountChoices[4] = { 4, 8, 16, 32 };

//==============================================================================
MentalsVoxChoirAudioProcessor::MentalsVoxChoirAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    voicesParam  = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("voices"));
    vibratoParam = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("vibrato"));
    pitchParam   = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("pitch"));
    timingParam  = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("timing"));
    spreadParam  = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("spread"));
    mixParam     = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("mix"));

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsVoxChoirAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "voices", "Voices", juce::StringArray { "4", "8", "16", "32" }, 1)); // default "8"

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "vibrato", "Vibrato",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 40.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "pitch", "Pitch",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 40.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "timing", "Timing",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 40.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "spread", "Spread",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 80.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsVoxChoirAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    const int delayLineSize = juce::jmax (8, (int) (0.001 * (maxTimingMs + 5.0f) * sampleRate));

    for (int v = 0; v < maxVoices; ++v)
    {
        auto& voice = voices[(size_t) v];
        voice.pitchShifter.prepare (sampleRate);
        voice.delayLine.assign ((size_t) delayLineSize, 0.0f);
        voice.delayWritePos = 0;
        voice.vibratoPhase = ChoirVoiceDSP::computeVoiceCharacter (v).vibratoSeedPhaseRad;
    }

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
}

bool MentalsVoxChoirAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Output is always stereo -- the spread across the stereo field is the
    // whole point of this effect -- but the source vocal can be mono or
    // stereo (summed down internally either way).
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto& mainIn = layouts.getMainInputChannelSet();
    return mainIn == juce::AudioChannelSet::mono() || mainIn == juce::AudioChannelSet::stereo();
}

void MentalsVoxChoirAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const int numInputChannels = getMainBusNumInputChannels();
    const int numSamples = buffer.getNumSamples();

    const int numVoices = voiceCountChoices[voicesParam->getIndex()];
    const float vibratoAmount = vibratoParam->get() * 0.01f;
    const float pitchAmount   = pitchParam->get() * 0.01f;
    const float timingAmount  = timingParam->get() * 0.01f;
    const float spreadAmount  = spreadParam->get() * 0.01f;
    const float mix           = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);

    // Per-voice "identity" doesn't change within a block (only the knob
    // amounts scaling it do), so it's computed once here rather than once
    // per sample per voice.
    std::array<ChoirVoiceDSP::VoiceCharacter, maxVoices> characters;
    std::array<float, maxVoices> pans;
    for (int v = 0; v < numVoices; ++v)
    {
        characters[(size_t) v] = ChoirVoiceDSP::computeVoiceCharacter (v);
        pans[(size_t) v] = ChoirVoiceDSP::computeVoicePan (v, numVoices) * spreadAmount;
    }

    const float voiceScale = 1.0f / std::sqrt ((float) juce::jmax (1, numVoices));
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    for (int n = 0; n < numSamples; ++n)
    {
        float monoIn = 0.0f;
        for (int ch = 0; ch < numInputChannels; ++ch)
            monoIn += buffer.getReadPointer (ch)[n];
        monoIn /= (float) juce::jmax (1, numInputChannels);

        float outL = 0.0f, outR = 0.0f;

        for (int v = 0; v < numVoices; ++v)
        {
            auto& voice = voices[(size_t) v];
            const auto& character = characters[(size_t) v];

            const float vibratoCents = character.vibratoDepthScale * maxVibratoCents * vibratoAmount
                                        * std::sin (voice.vibratoPhase);
            voice.vibratoPhase += twoPi * character.vibratoRateHz / (float) currentSampleRate;
            if (voice.vibratoPhase >= twoPi)
                voice.vibratoPhase -= twoPi;

            const float totalCents = character.pitchOffsetSign * maxPitchCents * pitchAmount + vibratoCents;
            const float ratio = std::pow (2.0f, totalCents / 1200.0f);

            const float shifted = voice.pitchShifter.process (monoIn, ratio);

            // Fixed per-voice timing offset -- a plain integer-sample delay
            // (no interpolation) is fine here since, unlike the vibrato
            // above, this doesn't need to glide smoothly moment to moment.
            auto& delayLine = voice.delayLine;
            const int bufSize = (int) delayLine.size();
            const int delaySamples = (int) std::round (character.timingOffset01 * maxTimingMs * timingAmount
                                                          * 0.001f * (float) currentSampleRate);
            int readPos = voice.delayWritePos - juce::jlimit (0, bufSize - 1, delaySamples);
            while (readPos < 0)
                readPos += bufSize;

            const float delayed = delayLine[(size_t) readPos];
            delayLine[(size_t) voice.delayWritePos] = shifted;
            voice.delayWritePos = (voice.delayWritePos + 1) % bufSize;

            // Equal-power pan.
            const float panAngle = (pans[(size_t) v] + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            outL += delayed * std::cos (panAngle);
            outR += delayed * std::sin (panAngle);
        }

        outL *= voiceScale;
        outR *= voiceScale;

        left[n]  = monoIn * (1.0f - mix) + outL * mix;
        right[n] = monoIn * (1.0f - mix) + outR * mix;
    }

    updateOutputLevelMeter (buffer);
}

void MentalsVoxChoirAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
juce::AudioProcessorEditor* MentalsVoxChoirAudioProcessor::createEditor()
{
    return new MentalsVoxChoirAudioProcessorEditor (*this);
}

void MentalsVoxChoirAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsVoxChoirAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
// Factory presets: common ensemble-size/character starting points. voices
// choice indices: 0="4", 1="8", 2="16", 3="32".
//==============================================================================
void MentalsVoxChoirAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto applyF = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };
    auto applyChoice = [] (juce::AudioParameterChoice* p, int index) { p->setValueNotifyingHost (p->convertTo0to1 ((float) index)); };

    resetToDefault();
    applyChoice (voicesParam, 0); applyF (vibratoParam, 30.0f); applyF (pitchParam, 25.0f);
    applyF (timingParam, 25.0f); applyF (spreadParam, 60.0f); applyF (mixParam, 80.0f);
    presetManager.savePreset ("Small Group (4)");

    resetToDefault();
    applyChoice (voicesParam, 1); applyF (vibratoParam, 20.0f); applyF (pitchParam, 20.0f);
    applyF (timingParam, 20.0f); applyF (spreadParam, 50.0f); applyF (mixParam, 70.0f);
    presetManager.savePreset ("Tight Backing Vocals (8)");

    resetToDefault();
    applyChoice (voicesParam, 2); applyF (vibratoParam, 40.0f); applyF (pitchParam, 40.0f);
    applyF (timingParam, 40.0f); applyF (spreadParam, 85.0f); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Choir (16)");

    resetToDefault();
    applyChoice (voicesParam, 2); applyF (vibratoParam, 60.0f); applyF (pitchParam, 60.0f);
    applyF (timingParam, 45.0f); applyF (spreadParam, 100.0f); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Ethereal Wide Pad (16)");

    resetToDefault();
    applyChoice (voicesParam, 3); applyF (vibratoParam, 45.0f); applyF (pitchParam, 50.0f);
    applyF (timingParam, 50.0f); applyF (spreadParam, 100.0f); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Massive Choir (32)");

    resetToDefault();
    applyChoice (voicesParam, 0); applyF (vibratoParam, 15.0f); applyF (pitchParam, 15.0f);
    applyF (timingParam, 15.0f); applyF (spreadParam, 40.0f); applyF (mixParam, 50.0f);
    presetManager.savePreset ("Subtle Double (4)");

    resetToDefault();
}
