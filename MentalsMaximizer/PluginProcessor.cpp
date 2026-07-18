#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

//==============================================================================
MentalsMaximizerAudioProcessor::MentalsMaximizerAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    thresholdParam    = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("threshold"));
    ceilingParam      = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("ceiling"));
    algorithmParam    = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("algorithm"));
    characterParam    = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("character"));
    stereoUnlinkParam = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("stereoUnlink"));
    mixParam          = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("mix"));

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsMaximizerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "threshold", "Threshold",
        juce::NormalisableRange<float> (-30.0f, 0.0f, 0.01f), -6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ceiling", "Ceiling",
        juce::NormalisableRange<float> (-6.0f, 0.0f, 0.01f), -0.3f,
        juce::AudioParameterFloatAttributes().withLabel ("dBTP")));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "algorithm", "Algorithm",
        juce::StringArray { "Modern", "Classic", "Warm", "Aggressive" }, algModern));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "character", "Character",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereoUnlink", "Stereo Unlink", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsMaximizerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    const int lookaheadSamples = juce::jmax (1, (int) (lookaheadMs * 0.001 * sampleRate));
    for (int ch = 0; ch < maxSupportedChannels; ++ch)
    {
        delayLines[(size_t) ch].assign ((size_t) lookaheadSamples, 0.0f);
        delayWritePos[(size_t) ch] = 0;
    }
    setLatencySamples (lookaheadSamples);

    for (auto& ef : envelopeFollowers)
    {
        ef.prepare (sampleRate);
        ef.setAttackRelease (attackMs, 150.0f);
        ef.reset();
    }

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    oversampler->reset();

    gainedBuffer.setSize (maxSupportedChannels, samplesPerBlock);
    for (auto& v : truePeakLevel)
        v.assign ((size_t) samplesPerBlock, 0.0f);

    inputLufsMeter.prepare (sampleRate);
    outputLufsMeter.prepare (sampleRate);

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
    currentGainReductionDb = 0.0f;
}

bool MentalsMaximizerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void MentalsMaximizerAudioProcessor::getAlgorithmReleaseRangeMs (int algorithm, float& minReleaseMs, float& maxReleaseMs, float& kneeDb) noexcept
{
    switch (algorithm)
    {
        case algModern:     minReleaseMs = 30.0f;  maxReleaseMs = 150.0f; kneeDb = 3.0f; break;
        case algClassic:    minReleaseMs = 60.0f;  maxReleaseMs = 250.0f; kneeDb = 2.0f; break;
        case algWarm:       minReleaseMs = 100.0f; maxReleaseMs = 400.0f; kneeDb = 4.0f; break;
        case algAggressive: minReleaseMs = 20.0f;  maxReleaseMs = 100.0f; kneeDb = 0.5f; break;
        default:            minReleaseMs = 30.0f;  maxReleaseMs = 150.0f; kneeDb = 3.0f; break;
    }
}

float MentalsMaximizerAudioProcessor::shapeSaturation (int algorithm, float x, float driveAmount) noexcept
{
    if (driveAmount <= 0.0f || algorithm == algModern)
        return x;

    float shaped = x;
    switch (algorithm)
    {
        case algClassic:
            shaped = std::tanh (x * 1.5f) / 1.5f;
            break;
        case algWarm:
        {
            const float asym = x + 0.15f * x * x * (x >= 0.0f ? 1.0f : -1.0f);
            shaped = std::tanh (asym * 1.3f) / 1.3f;
            break;
        }
        case algAggressive:
            shaped = std::tanh (x * 2.5f) / 1.6f; // steeper, doesn't fully normalise back down -- adds density
            break;
        default:
            break;
    }
    return x + (shaped - x) * driveAmount;
}

void MentalsMaximizerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const int numChannels = juce::jmin (buffer.getNumChannels(), maxSupportedChannels);
    const int numSamples  = buffer.getNumSamples();
    const int oversamplingChannels = juce::jmin (numChannels, gainedBuffer.getNumChannels());

    const float inputGain  = juce::Decibels::decibelsToGain (-thresholdParam->get());
    const float ceilingLin = juce::Decibels::decibelsToGain (ceilingParam->get());
    const float ceilingDb  = ceilingParam->get();
    const int   algorithm  = algorithmParam->getIndex();
    const float character01 = juce::jlimit (0.0f, 1.0f, characterParam->get() * 0.01f);
    const bool  stereoUnlink = stereoUnlinkParam->get();
    const float mix = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);

    float minReleaseMs, maxReleaseMs, kneeDb;
    getAlgorithmReleaseRangeMs (algorithm, minReleaseMs, maxReleaseMs, kneeDb);
    const float releaseMs = minReleaseMs + (maxReleaseMs - minReleaseMs) * character01;
    for (auto& ef : envelopeFollowers)
        ef.setAttackRelease (attackMs, releaseMs);

    // ---- Pass 1: apply input gain (driven by Threshold) up front, into
    // gainedBuffer -- needed as a whole block so it can be handed to the
    // oversampler in one call. The input loudness meter reads the RAW
    // (pre-gain) signal, since "Input" should mean what came in, not what
    // Threshold already did to it.
    gainedBuffer.setSize (gainedBuffer.getNumChannels(), numSamples, false, false, true);
    for (int ch = 0; ch < oversamplingChannels; ++ch)
    {
        auto* dst = gainedBuffer.getWritePointer (ch);
        const auto* src = buffer.getReadPointer (ch);
        for (int n = 0; n < numSamples; ++n)
            dst[n] = src[n] * inputGain;
    }
    for (int n = 0; n < numSamples; ++n)
    {
        float samples[2] { buffer.getReadPointer (0)[n], numChannels > 1 ? buffer.getReadPointer (1)[n] : 0.0f };
        inputLufsMeter.processSample (samples, numChannels);
    }

    // ---- True-peak scan (always on -- see class comment), per channel so
    // Stereo Unlink can react to each channel's own peaks independently.
    for (auto& v : truePeakLevel)
        if ((int) v.size() < numSamples)
            v.resize ((size_t) numSamples);

    if (oversampler != nullptr)
    {
        juce::dsp::AudioBlock<float> gainedBlock (gainedBuffer);
        auto trimmedBlock = gainedBlock.getSubsetChannelBlock (0, (size_t) oversamplingChannels).getSubBlock (0, (size_t) numSamples);
        auto oversampled = oversampler->processSamplesUp (juce::dsp::AudioBlock<const float> (trimmedBlock));
        const size_t factor = oversampled.getNumSamples() / (size_t) juce::jmax (1, numSamples);

        for (int ch = 0; ch < oversamplingChannels; ++ch)
        {
            const auto* data = oversampled.getChannelPointer ((size_t) ch);
            for (int n = 0; n < numSamples; ++n)
            {
                float peak = 0.0f;
                for (size_t k = 0; k < factor; ++k)
                {
                    const size_t idx = (size_t) n * factor + k;
                    if (idx < oversampled.getNumSamples())
                        peak = juce::jmax (peak, std::abs (data[idx]));
                }
                truePeakLevel[(size_t) ch][(size_t) n] = peak;
            }
        }
    }

    // ---- Pass 2: envelope + look-ahead + gain + saturation + ceiling clamp ------
    float blockMinGainReductionDb = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const float detectorL = truePeakLevel[0][(size_t) n];
        const float detectorR = oversamplingChannels > 1 ? truePeakLevel[1][(size_t) n] : detectorL;
        const float linkedDetector = juce::jmax (detectorL, detectorR);

        const float envL = envelopeFollowers[0].process (stereoUnlink ? detectorL : linkedDetector);
        const float envR = envelopeFollowers[1].process (stereoUnlink ? detectorR : linkedDetector);

        const auto computeGainReductionDb = [&] (float envelope) noexcept
        {
            const float envelopeDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
            const float outputDb = MentalsUI::DynamicsDSP::computeOutputDb (envelopeDb, ceilingDb, limiterRatio, kneeDb);
            return outputDb - envelopeDb;
        };

        const float grL = computeGainReductionDb (envL);
        const float grR = stereoUnlink ? computeGainReductionDb (envR) : grL;
        blockMinGainReductionDb = juce::jmin (blockMinGainReductionDb, juce::jmin (grL, grR));

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& delayLine = delayLines[(size_t) ch];
            const int delaySize = (int) delayLine.size();
            int& writePos = delayWritePos[(size_t) ch];

            const float gainedSample = ch < oversamplingChannels
                ? gainedBuffer.getReadPointer (ch)[n]
                : buffer.getReadPointer (ch)[n] * inputGain;

            const float delayed = delayLine[(size_t) writePos];
            delayLine[(size_t) writePos] = gainedSample;
            writePos = (writePos + 1) % delaySize;

            const float gainDb = (ch == 0) ? grL : grR;
            float limited = delayed * juce::Decibels::decibelsToGain (gainDb);
            limited = shapeSaturation (algorithm, limited, character01);
            limited = juce::jlimit (-ceilingLin, ceilingLin, limited); // final true-peak-safe hard clamp

            buffer.getWritePointer (ch)[n] = delayed * (1.0f - mix) + limited * mix;
        }
    }

    currentGainReductionDb.store (blockMinGainReductionDb);

    for (int n = 0; n < numSamples; ++n)
    {
        float samples[2] { buffer.getReadPointer (0)[n], numChannels > 1 ? buffer.getReadPointer (1)[n] : 0.0f };
        outputLufsMeter.processSample (samples, numChannels);
    }

    updateOutputLevelMeter (buffer);
}

void MentalsMaximizerAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
juce::AudioProcessorEditor* MentalsMaximizerAudioProcessor::createEditor()
{
    return new MentalsMaximizerAudioProcessorEditor (*this);
}

void MentalsMaximizerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsMaximizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
// Factory presets: mastering-loudness starting points spanning transparent
// to club-loud, in the spirit of the target-loudness preset menus
// mastering-maximizer plugins (Ozone's Maximizer included) ship -- not a
// copy of any specific product's exact values.
//==============================================================================
void MentalsMaximizerAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto applyF = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };
    auto applyChoice = [] (juce::AudioParameterChoice* p, int index) { p->setValueNotifyingHost (p->convertTo0to1 ((float) index)); };
    auto applyB = [] (juce::AudioParameterBool* p, bool value) { p->setValueNotifyingHost (value ? 1.0f : 0.0f); };

    resetToDefault();
    applyF (thresholdParam, -3.0f); applyF (ceilingParam, -1.0f); applyChoice (algorithmParam, algModern);
    applyF (characterParam, 20.0f); applyB (stereoUnlinkParam, false); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Transparent Master");

    resetToDefault();
    applyF (thresholdParam, -6.0f); applyF (ceilingParam, -1.0f); applyChoice (algorithmParam, algModern);
    applyF (characterParam, 35.0f); applyB (stereoUnlinkParam, false); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Streaming Loud");

    resetToDefault();
    applyF (thresholdParam, -8.0f); applyF (ceilingParam, -0.3f); applyChoice (algorithmParam, algClassic);
    applyF (characterParam, 55.0f); applyB (stereoUnlinkParam, false); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Classic Loud");

    resetToDefault();
    applyF (thresholdParam, -7.0f); applyF (ceilingParam, -0.5f); applyChoice (algorithmParam, algWarm);
    applyF (characterParam, 60.0f); applyB (stereoUnlinkParam, false); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Warm Analog Master");

    resetToDefault();
    applyF (thresholdParam, -14.0f); applyF (ceilingParam, -0.1f); applyChoice (algorithmParam, algAggressive);
    applyF (characterParam, 80.0f); applyB (stereoUnlinkParam, false); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Club EDM Loud");

    resetToDefault();
    applyF (thresholdParam, -18.0f); applyF (ceilingParam, -0.1f); applyChoice (algorithmParam, algAggressive);
    applyF (characterParam, 100.0f); applyB (stereoUnlinkParam, false); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Maximum Loudness");

    resetToDefault();
    applyF (thresholdParam, -4.0f); applyF (ceilingParam, -2.0f); applyChoice (algorithmParam, algModern);
    applyF (characterParam, 15.0f); applyB (stereoUnlinkParam, false); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Broadcast Safe");

    resetToDefault();
    applyF (thresholdParam, -2.0f); applyF (ceilingParam, -1.0f); applyChoice (algorithmParam, algModern);
    applyF (characterParam, 10.0f); applyB (stereoUnlinkParam, false); applyF (mixParam, 100.0f);
    presetManager.savePreset ("Podcast Voice Master");

    resetToDefault();
}
