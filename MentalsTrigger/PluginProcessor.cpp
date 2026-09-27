#include "PluginProcessor.h"
#include "PluginEditor.h"

const char* const MentalsTriggerAudioProcessor::layerNames[MentalsTriggerAudioProcessor::numLayers] = {
    "Soft", "Medium", "Hard"
};

const char* const MentalsTriggerAudioProcessor::roundRobinNames[MentalsTriggerAudioProcessor::numRoundRobins] = {
    "A", "B", "C", "D"
};

//==============================================================================
MentalsTriggerAudioProcessor::MentalsTriggerAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    formatManager.registerBasicFormats();

    thresholdParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("threshold"));
    sensitivityParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("sensitivity"));
    chokeTimeParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("chokeTime"));
    softMedSplitParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("softMedSplit"));
    medHardSplitParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("medHardSplit"));
    curveParam        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("curve"));
    outputGainParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("outputGain"));

    for (int i = 0; i < numLayers; ++i)
    {
        muteParams[(size_t) i] = dynamic_cast<juce::AudioParameterBool*> (
            apvts.getParameter (juce::String ("mute") + layerNames[i]));
        soloParams[(size_t) i] = dynamic_cast<juce::AudioParameterBool*> (
            apvts.getParameter (juce::String ("solo") + layerNames[i]));
    }

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsTriggerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "threshold", "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "sensitivity", "Sensitivity",
        juce::NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "chokeTime", "Choke Time",
        juce::NormalisableRange<float> (10.0f, 500.0f, 1.0f, 0.5f), 60.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "softMedSplit", "Soft/Med Split",
        juce::NormalisableRange<float> (0.01f, 0.98f, 0.01f), 0.33f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "medHardSplit", "Med/Hard Split",
        juce::NormalisableRange<float> (0.02f, 0.99f, 0.01f), 0.66f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "curve", "Curve",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "outputGain", "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    for (int i = 0; i < numLayers; ++i)
    {
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::String ("mute") + layerNames[i], juce::String (layerNames[i]) + " Mute", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::String ("solo") + layerNames[i], juce::String (layerNames[i]) + " Solo", false));
    }

    return { params.begin(), params.end() };
}

//==============================================================================
bool MentalsTriggerAudioProcessor::loadSampleForSlot (int layerIndex, int roundRobinIndex, const juce::File& file)
{
    if (layerIndex < 0 || layerIndex >= numLayers || roundRobinIndex < 0 || roundRobinIndex >= numRoundRobins)
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    juce::AudioBuffer<float> newBuffer ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&newBuffer, 0, (int) reader->lengthInSamples, 0, true, true);
    const double sourceSampleRate = reader->sampleRate;
    const auto displayName = file.getFileNameWithoutExtension();

    auto& slot = layers[(size_t) layerIndex].roundRobins[(size_t) roundRobinIndex];
    const juce::ScopedLock lock (layersLock);
    slot.audioData = std::move (newBuffer);
    slot.sourceSampleRate = sourceSampleRate;
    slot.fileName = displayName;
    return true;
}

//==============================================================================
float MentalsTriggerAudioProcessor::shapeVelocity (float raw01) const noexcept
{
    // curve: -100..100, 0 = linear. Maps to a power-curve exponent: +100%
    // gives ~0.32 (concave -- boosts quiet hits), -100% gives ~3.16 (convex
    // -- suppresses quiet hits), matching Addictive Trigger's MIDI Response
    // curve in spirit if not in exact bezier shape.
    const float curvePercent = curveParam->get();
    const float exponent = std::pow (10.0f, -curvePercent / 100.0f * 0.5f);
    return std::pow (juce::jlimit (0.0f, 1.0f, raw01), exponent);
}

int MentalsTriggerAudioProcessor::pickLayerForVelocity (float velocity01) const noexcept
{
    if (velocity01 < softMedSplitParam->get())
        return 0; // Soft
    if (velocity01 < medHardSplitParam->get())
        return 1; // Medium
    return 2; // Hard
}

bool MentalsTriggerAudioProcessor::isLayerAudible (int layerIndex) const noexcept
{
    bool anySoloed = false;
    for (int i = 0; i < numLayers; ++i)
    {
        if (soloParams[(size_t) i]->get())
        {
            anySoloed = true;
            break;
        }
    }

    if (anySoloed)
        return soloParams[(size_t) layerIndex]->get();

    return ! muteParams[(size_t) layerIndex]->get();
}

int MentalsTriggerAudioProcessor::pickRoundRobinIndex (SampleLayer& layer) const noexcept
{
    for (int attempt = 0; attempt < numRoundRobins; ++attempt)
    {
        const int idx = (layer.nextRoundRobinIndex + attempt) % numRoundRobins;
        if (layer.roundRobins[(size_t) idx].hasAudio())
        {
            layer.nextRoundRobinIndex = (idx + 1) % numRoundRobins;
            return idx;
        }
    }
    return -1;
}

int MentalsTriggerAudioProcessor::triggerHit (float rawVelocity01)
{
    const float shapedVelocity = shapeVelocity (rawVelocity01);
    const int layerIndex = pickLayerForVelocity (shapedVelocity);
    auto& layer = layers[(size_t) layerIndex];
    const int roundRobinIndex = pickRoundRobinIndex (layer);

    lastTriggeredLayer.store (layerIndex);
    lastTriggeredRoundRobin.store (roundRobinIndex);
    lastHitVelocity.store (shapedVelocity);
    lastHitCounter.fetch_add (1);

    if (roundRobinIndex < 0 || ! isLayerAudible (layerIndex))
    {
        voice.active = false;
        return layerIndex;
    }

    const auto& slot = layer.roundRobins[(size_t) roundRobinIndex];
    voice.active = true;
    voice.layerIndex = layerIndex;
    voice.roundRobinIndex = roundRobinIndex;
    voice.readPosition = 0.0;
    voice.playbackRatio = slot.sourceSampleRate / currentSampleRate;
    // Quiet hits still play clearly audible (floor at 30%) rather than
    // fading to silence at the bottom of the velocity range.
    voice.velocityGain = 0.3f + 0.7f * shapedVelocity;

    return layerIndex;
}

//==============================================================================
double MentalsTriggerAudioProcessor::getTailLengthSeconds() const
{
    const juce::ScopedLock lock (layersLock);

    double longestSeconds = 0.1;
    for (const auto& layer : layers)
        for (const auto& rr : layer.roundRobins)
            if (rr.hasAudio())
                longestSeconds = juce::jmax (longestSeconds, (double) rr.audioData.getNumSamples() / rr.sourceSampleRate);

    return longestSeconds;
}

//==============================================================================
bool MentalsTriggerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void MentalsTriggerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    constexpr float attackMs = 1.0f;
    constexpr float releaseMs = 60.0f;
    attackCoeff  = 1.0f - std::exp (-1.0f / (0.001f * attackMs  * (float) sampleRate));
    releaseCoeff = 1.0f - std::exp (-1.0f / (0.001f * releaseMs * (float) sampleRate));

    envelopeFollower = 0.0f;
    wasAboveThreshold = false;
    lockoutSamplesRemaining = 0;
    voice.active = false;

    // ~5ms per waveform column, giving a ~2 second visible window across
    // waveformColumns columns -- independent of host sample rate.
    columnLengthSamples = juce::jmax (1, (int) (sampleRate * 0.005));
    samplesInCurrentColumn = 0;
    currentColumnPeak = 0.0f;
    currentColumnHitVelocity = -1.0f;
    currentColumnHitLayer = -1;
}

//==============================================================================
void MentalsTriggerAudioProcessor::pushWaveformColumn (float peak, float hitVelocity, int hitLayer)
{
    const juce::SpinLock::ScopedLockType lock (waveformLock);
    waveformRing[(size_t) waveformWriteIndex] = { peak, hitVelocity, hitLayer };
    waveformWriteIndex = (waveformWriteIndex + 1) % waveformColumns;
}

void MentalsTriggerAudioProcessor::getWaveformSnapshot (std::array<WaveformColumn, waveformColumns>& outColumns) const
{
    const juce::SpinLock::ScopedLockType lock (waveformLock);
    for (int i = 0; i < waveformColumns; ++i)
        outColumns[(size_t) i] = waveformRing[(size_t) ((waveformWriteIndex + i) % waveformColumns)];
}

//==============================================================================
void MentalsTriggerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedLock lock (layersLock);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    const float thresholdLinear = juce::Decibels::decibelsToGain (thresholdParam->get());
    const float sensitivityGain = juce::Decibels::decibelsToGain (sensitivityParam->get());
    const float outputGain = juce::Decibels::decibelsToGain (outputGainParam->get());

    float blockInputPeak = 0.0f;

    // Scan the input for hits before overwriting the buffer with whatever
    // voice ends up playing -- detection always looks at the real input,
    // never at the replaced output. Also feeds the scrolling waveform
    // display, one column at a time.
    for (int i = 0; i < numSamples; ++i)
    {
        float inputSample = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            inputSample = juce::jmax (inputSample, std::abs (buffer.getSample (ch, i)));
        inputSample *= sensitivityGain;
        blockInputPeak = juce::jmax (blockInputPeak, inputSample);

        const float coeff = (inputSample > envelopeFollower) ? attackCoeff : releaseCoeff;
        envelopeFollower += (inputSample - envelopeFollower) * coeff;

        if (lockoutSamplesRemaining > 0)
        {
            --lockoutSamplesRemaining;
            wasAboveThreshold = envelopeFollower > thresholdLinear;
        }
        else
        {
            const bool isAboveThreshold = envelopeFollower > thresholdLinear;
            if (isAboveThreshold && ! wasAboveThreshold)
            {
                const float rawVelocity = juce::jlimit (0.0f, 1.0f, envelopeFollower);
                const int firedLayer = triggerHit (rawVelocity);
                currentColumnHitVelocity = lastHitVelocity.load();
                currentColumnHitLayer = firedLayer;
                lockoutSamplesRemaining = (int) (chokeTimeParam->get() * 0.001 * currentSampleRate);
            }
            wasAboveThreshold = isAboveThreshold;
        }

        currentColumnPeak = juce::jmax (currentColumnPeak, envelopeFollower);
        if (++samplesInCurrentColumn >= columnLengthSamples)
        {
            pushWaveformColumn (currentColumnPeak, currentColumnHitVelocity, currentColumnHitLayer);
            currentColumnPeak = 0.0f;
            currentColumnHitVelocity = -1.0f;
            currentColumnHitLayer = -1;
            samplesInCurrentColumn = 0;
        }
    }

    // Replace the signal entirely with whichever voice is playing (or
    // silence, if none is / the selected layer has no sample loaded).
    buffer.clear();
    if (voice.active)
    {
        const auto& slot = layers[(size_t) voice.layerIndex].roundRobins[(size_t) voice.roundRobinIndex];
        const int sourceLength = slot.audioData.getNumSamples();
        const int sourceChannels = slot.audioData.getNumChannels();

        for (int i = 0; i < numSamples; ++i)
        {
            const int readIndex = (int) voice.readPosition;
            if (readIndex >= sourceLength)
            {
                voice.active = false;
                break;
            }

            // Linear interpolation between readIndex and the next sample --
            // playbackRatio is rarely exactly 1.0 (it tracks the loaded
            // file's own sample rate against the session's), so without
            // this the nearest-neighbour step introduces audible aliasing/
            // graininess on anything but a perfectly matched sample rate.
            const float frac = (float) (voice.readPosition - (double) readIndex);
            const int nextIndex = juce::jmin (readIndex + 1, sourceLength - 1);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const int sourceChannel = juce::jmin (ch, sourceChannels - 1);
                const float s0 = slot.audioData.getSample (sourceChannel, readIndex);
                const float s1 = slot.audioData.getSample (sourceChannel, nextIndex);
                const float interpolated = s0 + frac * (s1 - s0);
                buffer.setSample (ch, i, interpolated * outputGain * voice.velocityGain);
            }

            voice.readPosition += voice.playbackRatio;
        }
    }

    updateLevelMeters (buffer, blockInputPeak);
}

void MentalsTriggerAudioProcessor::updateLevelMeters (const juce::AudioBuffer<float>& buffer, float inputPeak)
{
    inputPeakLinear.store (inputPeak);

    float outPeak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        outPeak = juce::jmax (outPeak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    const float releasePerBlock = std::pow (10.0f, -24.0f * ((float) buffer.getNumSamples() / (float) currentSampleRate) / 20.0f);
    const float previous = outputPeakLinear.load();
    outputPeakLinear.store (juce::jmax (outPeak, previous * releasePerBlock));

    if (outPeak >= 1.0f)
        clipHoldBlocksRemaining.store ((int) (1.5 * currentSampleRate / juce::jmax (1, buffer.getNumSamples())));
    else if (clipHoldBlocksRemaining.load() > 0)
        clipHoldBlocksRemaining.fetch_sub (1);
}

//==============================================================================
juce::AudioProcessorEditor* MentalsTriggerAudioProcessor::createEditor()
{
    return new MentalsTriggerAudioProcessorEditor (*this);
}

//==============================================================================
void MentalsTriggerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsTriggerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
namespace
{
    // Starting points for each mic position in a typically-miked drum kit,
    // tuned by how clean vs. bleed-prone that source usually is: close/
    // isolated mics (Kick In, Snare Top) get a lower Threshold and shorter
    // Choke Time since their hits are clean and well-separated; bleed-heavy
    // sources (Overheads, Room, Snare Bottom) get a higher Threshold, more
    // negative Sensitivity, a longer Choke Time, and a negative Curve (so
    // only genuinely hard transients reach the Hard layer, rather than
    // spill from other drums). Hi-Hat gets a short Choke Time specifically
    // so fast repeated 16th-note patterns can all still trigger. These are
    // deliberately just sensible starting points, same as any factory
    // preset -- Threshold/Sensitivity/Choke still need tuning against the
    // actual recording.
    struct DrumPresetDefaults
    {
        const char* name;
        float thresholdDb, sensitivityDb, chokeMs, softMedSplit, medHardSplit, curvePercent;
    };

    const DrumPresetDefaults kDrumPresets[] = {
        { "Kick In",      -20.0f,  0.0f,  80.0f, 0.30f, 0.65f,   0.0f },
        { "Kick Out",     -18.0f,  0.0f, 120.0f, 0.30f, 0.65f,   0.0f },
        { "Sub Kick",     -26.0f,  3.0f, 150.0f, 0.35f, 0.70f,  10.0f },
        { "Snare Top",    -22.0f,  0.0f,  60.0f, 0.30f, 0.65f,   0.0f },
        { "Snare Bottom", -16.0f, -3.0f,  90.0f, 0.35f, 0.70f, -10.0f },
        // Rim clicks/cross-stick hits run quieter and shorter than full
        // snare hits, and are often played in fast patterns -- a lower
        // Threshold catches the quieter clicks, and a short Choke Time
        // (like Hi-Hat's) lets rapid rim patterns keep triggering.
        { "Snare Rim",    -28.0f,  0.0f,  40.0f, 0.30f, 0.60f,   0.0f },
        { "Hi-Hat",       -24.0f,  0.0f,  30.0f, 0.30f, 0.60f,   0.0f },
        // Three separate toms, highest-pitched to lowest -- Choke Time
        // lengthens as toms get bigger/lower since they ring out longer.
        { "Tom 1",        -22.0f,  0.0f,  90.0f, 0.30f, 0.65f,   0.0f },
        { "Tom 2",        -22.0f,  0.0f, 105.0f, 0.30f, 0.65f,   0.0f },
        { "Low Tom",      -22.0f,  0.0f, 130.0f, 0.30f, 0.65f,   0.0f },
        { "OH",           -12.0f, -6.0f, 200.0f, 0.40f, 0.75f, -20.0f },
        { "Room",         -10.0f, -8.0f, 250.0f, 0.40f, 0.75f, -20.0f },
    };
}

void MentalsTriggerAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    resetToDefault();
    presetManager.savePreset ("Default");

    auto applyF = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };

    for (const auto& preset : kDrumPresets)
    {
        resetToDefault();
        applyF (thresholdParam,    preset.thresholdDb);
        applyF (sensitivityParam,  preset.sensitivityDb);
        applyF (chokeTimeParam,    preset.chokeMs);
        applyF (softMedSplitParam, preset.softMedSplit);
        applyF (medHardSplitParam, preset.medHardSplit);
        applyF (curveParam,        preset.curvePercent);
        presetManager.savePreset (preset.name);
    }

    resetToDefault();
}
