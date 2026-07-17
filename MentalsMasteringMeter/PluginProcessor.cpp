#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

//==============================================================================
MentalsMasteringMeterAudioProcessor::MentalsMasteringMeterAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    targetPresetParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("targetPreset"));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsMasteringMeterAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "targetPreset", "Target",
        juce::StringArray { "Spotify -14", "Apple Music -16", "YouTube -14", "Broadcast -23" }, 0));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsMasteringMeterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    lufsMeter.prepare (sampleRate);

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    oversampler->reset();

    truePeakLinear.store (0.0f);
    samplePeakLinear.store (0.0f);

    // Loudness history (subBlockMeanSquare/historyCount/historyWritePos) is
    // deliberately NOT cleared here -- prepareToPlay can fire on a benign
    // engine restart mid-session, and wiping a mastering engineer's
    // integrated-loudness measurement because the audio device hiccuped
    // would be a bad surprise. Only resetMeters() (the Reset button) clears
    // it, matching how a real mastering meter measures "since I last hit
    // Reset", not "since the engine last restarted".
}

bool MentalsMasteringMeterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void MentalsMasteringMeterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    if (resetRequested.exchange (false))
    {
        lufsMeter.resetHistory();
        truePeakLinear.store (0.0f);
        samplePeakLinear.store (0.0f);
    }

    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    // ---- Sample peak (plain, no oversampling) -----------------------------------
    float blockPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        blockPeak = juce::jmax (blockPeak, buffer.getMagnitude (ch, 0, numSamples));
    const float previousSamplePeak = samplePeakLinear.load();
    const float samplePeakRelease = std::pow (10.0f, -12.0f * ((float) numSamples / (float) currentSampleRate) / 20.0f);
    samplePeakLinear.store (juce::jmax (blockPeak, previousSamplePeak * samplePeakRelease));

    // ---- True peak (4x oversampled) ----------------------------------------------
    if (oversampler != nullptr)
    {
        juce::dsp::AudioBlock<const float> inputBlock (buffer);
        auto oversampledBlock = oversampler->processSamplesUp (inputBlock);

        float oversampledPeak = 0.0f;
        for (size_t ch = 0; ch < oversampledBlock.getNumChannels(); ++ch)
        {
            const auto* data = oversampledBlock.getChannelPointer (ch);
            for (size_t n = 0; n < oversampledBlock.getNumSamples(); ++n)
                oversampledPeak = juce::jmax (oversampledPeak, std::abs (data[n]));
        }

        const float previousTruePeak = truePeakLinear.load();
        truePeakLinear.store (juce::jmax (oversampledPeak, previousTruePeak * samplePeakRelease));
    }

    // ---- K-weighted loudness, accumulated into 100ms sub-blocks ------------------
    for (int n = 0; n < numSamples; ++n)
    {
        float samples[2] { buffer.getReadPointer (0)[n], numChannels > 1 ? buffer.getReadPointer (1)[n] : 0.0f };
        lufsMeter.processSample (samples, numChannels);
    }

    // Pure meter -- the buffer passed to the next plugin/output is untouched.
}

void MentalsMasteringMeterAudioProcessor::resetMeters()
{
    resetRequested.store (true);
}

float MentalsMasteringMeterAudioProcessor::getMomentaryLufs() const noexcept { return lufsMeter.getMomentaryLufs(); }
float MentalsMasteringMeterAudioProcessor::getShortTermLufs() const noexcept { return lufsMeter.getShortTermLufs(); }
float MentalsMasteringMeterAudioProcessor::getIntegratedLufs() const noexcept { return lufsMeter.getIntegratedLufs(); }
float MentalsMasteringMeterAudioProcessor::getLoudnessRangeLu() const noexcept { return lufsMeter.getLoudnessRangeLu(); }

void MentalsMasteringMeterAudioProcessor::copyRecentLoudnessHistory (std::vector<float>& outMomentary, std::vector<float>& outShortTerm, int count) const
{
    lufsMeter.copyRecentHistory (outMomentary, outShortTerm, count);
}

//==============================================================================
juce::AudioProcessorEditor* MentalsMasteringMeterAudioProcessor::createEditor()
{
    return new MentalsMasteringMeterAudioProcessorEditor (*this);
}

void MentalsMasteringMeterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsMasteringMeterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
