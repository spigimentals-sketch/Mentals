#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr double absoluteGateLufs = -70.0;
    constexpr double integratedRelativeGateLu = 10.0;
    constexpr double lraRelativeGateLu = 20.0;
    constexpr double lraLowerPercentile = 0.10;
    constexpr double lraUpperPercentile = 0.95;

    double meanSquareToLufs (double meanSquare) noexcept
    {
        return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare) : -100.0;
    }
}

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
    samplesPerSubBlock = juce::jmax (1, (int) std::round (0.1 * sampleRate));
    subBlockSampleCounter = 0;
    subBlockSumSquares.fill (0.0);

    for (auto& kw : kWeighting)
        kw.prepare (sampleRate);

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
        historyCount.store (0);
        historyWritePos.store (0);
        subBlockSampleCounter = 0;
        subBlockSumSquares.fill (0.0);
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
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float weighted = kWeighting[(size_t) ch].process (buffer.getReadPointer (ch)[n]);
            subBlockSumSquares[(size_t) ch] += (double) weighted * (double) weighted;
        }

        if (++subBlockSampleCounter >= samplesPerSubBlock)
        {
            double combined = 0.0;
            for (int ch = 0; ch < numChannels; ++ch)
                combined += subBlockSumSquares[(size_t) ch] / (double) subBlockSampleCounter; // per-channel mean square, G=1.0 each

            const int pos = historyWritePos.load (std::memory_order_relaxed);
            subBlockMeanSquare[(size_t) pos].store ((float) combined, std::memory_order_relaxed);
            historyWritePos.store ((pos + 1) % historyCapacity, std::memory_order_relaxed);
            historyCount.store (juce::jmin (historyCapacity, historyCount.load (std::memory_order_relaxed) + 1), std::memory_order_relaxed);

            subBlockSampleCounter = 0;
            subBlockSumSquares.fill (0.0);
        }
    }

    // Pure meter -- the buffer passed to the next plugin/output is untouched.
}

void MentalsMasteringMeterAudioProcessor::resetMeters()
{
    resetRequested.store (true);
}

float MentalsMasteringMeterAudioProcessor::getMomentaryLufs() const noexcept
{
    const int count = juce::jmin (historyCount.load(), 4);
    if (count == 0)
        return -100.0f;

    const int writePos = historyWritePos.load();
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const int idx = ((writePos - 1 - i) % historyCapacity + historyCapacity) % historyCapacity;
        sum += subBlockMeanSquare[(size_t) idx].load (std::memory_order_relaxed);
    }
    return (float) meanSquareToLufs (sum / (double) count);
}

float MentalsMasteringMeterAudioProcessor::getShortTermLufs() const noexcept
{
    const int count = juce::jmin (historyCount.load(), 30);
    if (count == 0)
        return -100.0f;

    const int writePos = historyWritePos.load();
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const int idx = ((writePos - 1 - i) % historyCapacity + historyCapacity) % historyCapacity;
        sum += subBlockMeanSquare[(size_t) idx].load (std::memory_order_relaxed);
    }
    return (float) meanSquareToLufs (sum / (double) count);
}

float MentalsMasteringMeterAudioProcessor::getIntegratedLufs() const noexcept
{
    const int count = historyCount.load();
    if (count < 4)
        return -100.0f;

    const int writePos = historyWritePos.load();
    std::vector<double> subBlocks ((size_t) count);
    for (int i = 0; i < count; ++i)
    {
        const int idx = ((writePos - 1 - i) % historyCapacity + historyCapacity) % historyCapacity;
        subBlocks[(size_t) (count - 1 - i)] = subBlockMeanSquare[(size_t) idx].load (std::memory_order_relaxed); // oldest first
    }

    // 400ms gating blocks, stepped by one 100ms sub-block (75% overlap).
    std::vector<double> blockMeanSquares;
    blockMeanSquares.reserve ((size_t) count);
    for (int i = 0; i + 4 <= count; ++i)
    {
        double sum = 0.0;
        for (int k = 0; k < 4; ++k)
            sum += subBlocks[(size_t) (i + k)];
        blockMeanSquares.push_back (sum / 4.0);
    }
    if (blockMeanSquares.empty())
        return -100.0f;

    // Absolute gate.
    std::vector<double> absoluteGated;
    for (double ms : blockMeanSquares)
        if (meanSquareToLufs (ms) >= absoluteGateLufs)
            absoluteGated.push_back (ms);
    if (absoluteGated.empty())
        return -100.0f;

    double ungatedSum = 0.0;
    for (double ms : absoluteGated) ungatedSum += ms;
    const double ungatedLoudness = meanSquareToLufs (ungatedSum / (double) absoluteGated.size());

    // Relative gate.
    double relativeSum = 0.0;
    int relativeCount = 0;
    for (double ms : absoluteGated)
    {
        if (meanSquareToLufs (ms) >= ungatedLoudness - integratedRelativeGateLu)
        {
            relativeSum += ms;
            ++relativeCount;
        }
    }
    if (relativeCount == 0)
        return (float) ungatedLoudness;

    return (float) meanSquareToLufs (relativeSum / (double) relativeCount);
}

float MentalsMasteringMeterAudioProcessor::getLoudnessRangeLu() const noexcept
{
    const int count = historyCount.load();
    if (count < 30)
        return 0.0f;

    const int writePos = historyWritePos.load();
    std::vector<double> subBlocks ((size_t) count);
    for (int i = 0; i < count; ++i)
    {
        const int idx = ((writePos - 1 - i) % historyCapacity + historyCapacity) % historyCapacity;
        subBlocks[(size_t) (count - 1 - i)] = subBlockMeanSquare[(size_t) idx].load (std::memory_order_relaxed);
    }

    // 3s short-term blocks, stepped by one 100ms sub-block.
    std::vector<double> blockMeanSquares;
    blockMeanSquares.reserve ((size_t) count);
    for (int i = 0; i + 30 <= count; ++i)
    {
        double sum = 0.0;
        for (int k = 0; k < 30; ++k)
            sum += subBlocks[(size_t) (i + k)];
        blockMeanSquares.push_back (sum / 30.0);
    }
    if (blockMeanSquares.empty())
        return 0.0f;

    std::vector<double> absoluteGated;
    for (double ms : blockMeanSquares)
        if (meanSquareToLufs (ms) >= absoluteGateLufs)
            absoluteGated.push_back (ms);
    if (absoluteGated.empty())
        return 0.0f;

    double ungatedSum = 0.0;
    for (double ms : absoluteGated) ungatedSum += ms;
    const double ungatedLoudness = meanSquareToLufs (ungatedSum / (double) absoluteGated.size());

    std::vector<double> relativelyGatedLoudness;
    for (double ms : absoluteGated)
    {
        const double loudness = meanSquareToLufs (ms);
        if (loudness >= ungatedLoudness - lraRelativeGateLu)
            relativelyGatedLoudness.push_back (loudness);
    }
    if (relativelyGatedLoudness.size() < 2)
        return 0.0f;

    std::sort (relativelyGatedLoudness.begin(), relativelyGatedLoudness.end());
    const auto pick = [&] (double percentile)
    {
        const double pos = percentile * (double) (relativelyGatedLoudness.size() - 1);
        const size_t lower = (size_t) std::floor (pos);
        const size_t upper = juce::jmin (relativelyGatedLoudness.size() - 1, lower + 1);
        const double frac = pos - (double) lower;
        return relativelyGatedLoudness[lower] * (1.0 - frac) + relativelyGatedLoudness[upper] * frac;
    };

    return (float) (pick (lraUpperPercentile) - pick (lraLowerPercentile));
}

void MentalsMasteringMeterAudioProcessor::copyRecentLoudnessHistory (std::vector<float>& outMomentary, std::vector<float>& outShortTerm, int count) const
{
    const int available = historyCount.load();
    const int n = juce::jmin (count, available);
    outMomentary.assign ((size_t) count, -100.0f);
    outShortTerm.assign ((size_t) count, -100.0f);
    if (n == 0)
        return;

    const int writePos = historyWritePos.load();
    std::vector<double> subBlocks ((size_t) available);
    for (int i = 0; i < available; ++i)
    {
        const int idx = ((writePos - 1 - i) % historyCapacity + historyCapacity) % historyCapacity;
        subBlocks[(size_t) (available - 1 - i)] = subBlockMeanSquare[(size_t) idx].load (std::memory_order_relaxed); // oldest first
    }

    // For each of the last n sub-block positions, compute the windowed
    // momentary (4-wide) / short-term (30-wide) average ending at that point.
    for (int i = 0; i < n; ++i)
    {
        const int posFromEnd = n - 1 - i; // 0 = most recent
        const int endIdx = available - 1 - posFromEnd; // inclusive index into subBlocks

        double momSum = 0.0; int momCount = 0;
        for (int k = juce::jmax (0, endIdx - 3); k <= endIdx; ++k) { momSum += subBlocks[(size_t) k]; ++momCount; }

        double stSum = 0.0; int stCount = 0;
        for (int k = juce::jmax (0, endIdx - 29); k <= endIdx; ++k) { stSum += subBlocks[(size_t) k]; ++stCount; }

        const int outIdx = count - n + i;
        outMomentary[(size_t) outIdx] = (float) meanSquareToLufs (momSum / (double) juce::jmax (1, momCount));
        outShortTerm[(size_t) outIdx] = (float) meanSquareToLufs (stSum / (double) juce::jmax (1, stCount));
    }
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
