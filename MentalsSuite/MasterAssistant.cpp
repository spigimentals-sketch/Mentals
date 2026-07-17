#include "MasterAssistant.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <limits>

void MasterAssistant::prepare (double sampleRate)
{
    currentSampleRate = sampleRate;
    liveLufsMeter.prepare (sampleRate);
}

void MasterAssistant::processOutputBlock (const juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    // The live LUFS meter always runs -- Integrated/Momentary/Short-Term
    // are available on demand, not just during a capture window.
    for (int n = 0; n < numSamples; ++n)
    {
        float samples[2] { buffer.getReadPointer (0)[n], numChannels > 1 ? buffer.getReadPointer (1)[n] : 0.0f };
        liveLufsMeter.processSample (samples, numChannels);
    }

    if (! capturing.load (std::memory_order_relaxed))
        return;

    const auto* l = buffer.getReadPointer (0);
    const auto* r = numChannels > 1 ? buffer.getReadPointer (1) : l;
    for (int n = 0; n < numSamples; ++n)
    {
        captureSumLL += (double) l[n] * (double) l[n];
        captureSumRR += (double) r[n] * (double) r[n];
        captureSumLR += (double) l[n] * (double) r[n];
        capturePeak = juce::jmax (capturePeak, std::abs (l[n]), std::abs (r[n]));
    }
    captureSampleCount += numSamples;

    const juce::int64 remaining = captureSamplesRemaining.fetch_sub ((juce::int64) numSamples, std::memory_order_relaxed) - (juce::int64) numSamples;
    if (remaining <= 0)
    {
        capturing.store (false);

        const double meanSquare = (captureSumLL + captureSumRR) / (double) juce::jmax ((juce::int64) 1, captureSampleCount * 2);
        const double rms = std::sqrt (juce::jmax (1.0e-9, meanSquare));
        capturedCrestDb.store ((float) juce::Decibels::gainToDecibels ((double) capturePeak / rms));

        const double denom = std::sqrt (juce::jmax (1.0e-9, captureSumLL) * juce::jmax (1.0e-9, captureSumRR));
        capturedCorrelation.store ((float) juce::jlimit (-1.0, 1.0, captureSumLR / denom));

        capturedLufs.store (liveLufsMeter.getIntegratedLufs());

        captureReady.store (true);
    }
}

bool MasterAssistant::loadReferenceFile (const juce::File& file)
{
    float lufs = -100.0f;
    if (! MentalsUI::LoudnessDSP::analyseFileIntegratedLufs (file, currentSampleRate, lufs))
        return false;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const int numChannels = juce::jmin (2, (int) reader->numChannels);
    double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;
    float peak = 0.0f;

    constexpr int chunkSize = 8192;
    juce::AudioBuffer<float> chunk (juce::jmax (1, numChannels), chunkSize);

    juce::int64 position = 0;
    while (position < reader->lengthInSamples)
    {
        const int samplesThisChunk = (int) juce::jmin ((juce::int64) chunkSize, reader->lengthInSamples - position);
        chunk.clear();
        reader->read (&chunk, 0, samplesThisChunk, position, true, true);

        const auto* l = chunk.getReadPointer (0);
        const auto* r = numChannels > 1 ? chunk.getReadPointer (1) : l;
        for (int n = 0; n < samplesThisChunk; ++n)
        {
            sumLL += (double) l[n] * (double) l[n];
            sumRR += (double) r[n] * (double) r[n];
            sumLR += (double) l[n] * (double) r[n];
            peak = juce::jmax (peak, std::abs (l[n]), std::abs (r[n]));
        }

        position += samplesThisChunk;
    }

    const double meanSquare = (sumLL + sumRR) / (double) juce::jmax ((juce::int64) 1, reader->lengthInSamples * 2);
    const double rms = std::sqrt (juce::jmax (1.0e-9, meanSquare));
    const double denom = std::sqrt (juce::jmax (1.0e-9, sumLL) * juce::jmax (1.0e-9, sumRR));

    referenceLufs.store (lufs);
    referenceCrestDb.store ((float) juce::Decibels::gainToDecibels ((double) peak / rms));
    referenceCorrelation.store ((float) juce::jlimit (-1.0, 1.0, sumLR / denom));
    referenceFile = file;
    referenceReady.store (true);
    return true;
}

juce::String MasterAssistant::getReferenceFileName() const
{
    return referenceReady.load() ? referenceFile.getFileNameWithoutExtension() : juce::String();
}

void MasterAssistant::beginCapture()
{
    // Feed Multimode EQ's own EQ Match engine the same reference file and
    // start its live capture alongside ours, so tonal matching rides on
    // the same few seconds of playback as the loudness/dynamics/width
    // measurements below.
    if (auto* eq = dynamic_cast<MultiModeEQAudioProcessor*> (suite.getSlotProcessor (ensureModuleInChain (MentalsSuiteAudioProcessor::moduleEQ))))
    {
        if (referenceReady.load())
            eq->loadEqMatchReferenceFile (referenceFile);
        eq->beginEqMatchCapture();
    }

    captureSumLL = captureSumRR = captureSumLR = 0.0;
    capturePeak = 0.0f;
    captureSampleCount = 0;
    captureReady.store (false);
    liveLufsMeter.resetHistory();

    captureTotalSamples = juce::jmax ((juce::int64) 1, (juce::int64) (captureSeconds * currentSampleRate));
    captureSamplesRemaining.store (captureTotalSamples);
    capturing.store (true);
}

float MasterAssistant::getCaptureProgress() const noexcept
{
    if (! capturing.load())
        return captureReady.load() ? 1.0f : 0.0f;

    const double remaining = (double) captureSamplesRemaining.load();
    return (float) juce::jlimit (0.0, 1.0, 1.0 - remaining / (double) juce::jmax ((juce::int64) 1, captureTotalSamples));
}

int MasterAssistant::ensureModuleInChain (int moduleType)
{
    auto slots = suite.getChainSlots();
    for (auto& s : slots)
        if (s.moduleType == moduleType)
            return s.slotId;

    const int newSlotId = suite.addModuleToChain (moduleType); // always appended at the end

    // Canonical mastering-chain order: tonal balance, then dynamics, then
    // stereo width, then loudness/limiting, with the meter last as a
    // monitor. Move the just-added slot to just before the first
    // already-existing slot that comes later in this order -- so an empty
    // chain ends up built in this order, but a slot the user already
    // placed elsewhere is never moved.
    static constexpr std::array<int, 5> canonicalOrder {
        (int) MentalsSuiteAudioProcessor::moduleEQ,
        (int) MentalsSuiteAudioProcessor::moduleCircuitComp,
        (int) MentalsSuiteAudioProcessor::moduleStereoShaper,
        (int) MentalsSuiteAudioProcessor::moduleLimiter,
        (int) MentalsSuiteAudioProcessor::moduleMasteringMeter
    };

    const auto canonicalIt = std::find (canonicalOrder.begin(), canonicalOrder.end(), moduleType);
    if (canonicalIt == canonicalOrder.end())
        return newSlotId;

    auto refreshed = suite.getChainSlots();
    std::vector<int> newOrder;
    newOrder.reserve (refreshed.size());
    bool inserted = false;
    for (auto& s : refreshed)
    {
        if (s.slotId == newSlotId)
            continue;

        if (! inserted)
        {
            const auto otherIt = std::find (canonicalOrder.begin(), canonicalOrder.end(), s.moduleType);
            if (otherIt != canonicalOrder.end() && otherIt > canonicalIt)
            {
                newOrder.push_back (newSlotId);
                inserted = true;
            }
        }
        newOrder.push_back (s.slotId);
    }
    if (! inserted)
        newOrder.push_back (newSlotId);

    suite.setChainOrder (newOrder);
    return newSlotId;
}

void MasterAssistant::applyToChain()
{
    if (! canApply())
        return;

    // ---- Tonal balance: hand off entirely to EQ Match's own logic. ---------------
    if (auto* eq = dynamic_cast<MultiModeEQAudioProcessor*> (suite.getSlotProcessor (ensureModuleInChain (MentalsSuiteAudioProcessor::moduleEQ))))
    {
        if (eq->isEqMatchCurrentReady() && eq->hasEqMatchReference())
            eq->applyEqMatch();
    }

    // ---- Dynamics: nudge Circuit Comp's global Threshold/Ratio from the
    // crest-factor gap. Global-only -- if that instance has Multiband
    // switched on, these params are dormant and this nudge is a no-op,
    // which is an accepted limitation: matching a single crest-factor
    // number to 7 independently-set bands isn't a sound heuristic anyway.
    const float crestDeltaDb = referenceCrestDb.load() - capturedCrestDb.load();
    if (auto* comp = dynamic_cast<MentalsCircuitCompAudioProcessor*> (suite.getSlotProcessor (ensureModuleInChain (MentalsSuiteAudioProcessor::moduleCircuitComp))))
    {
        const float thresholdNudge = juce::jlimit (-12.0f, 12.0f, crestDeltaDb * 1.5f);
        const float newThreshold = juce::jlimit (comp->thresholdParam->range.start, comp->thresholdParam->range.end,
                                                  comp->thresholdParam->get() + thresholdNudge);
        comp->thresholdParam->setValueNotifyingHost (comp->thresholdParam->convertTo0to1 (newThreshold));

        const float ratioNudge = juce::jlimit (-2.0f, 2.0f, -crestDeltaDb * 0.15f);
        const float newRatio = juce::jlimit (comp->ratioParam->range.start, comp->ratioParam->range.end,
                                              comp->ratioParam->get() + ratioNudge);
        comp->ratioParam->setValueNotifyingHost (comp->ratioParam->convertTo0to1 (newRatio));
    }

    // ---- Stereo width: nudge Width from the correlation gap -- higher
    // correlation means narrower/more mono, so if the current mix is
    // narrower than the reference (higher correlation), widen, and vice
    // versa.
    const float correlationDelta = capturedCorrelation.load() - referenceCorrelation.load();
    if (auto* shaper = dynamic_cast<MentalsStereoShaperAudioProcessor*> (suite.getSlotProcessor (ensureModuleInChain (MentalsSuiteAudioProcessor::moduleStereoShaper))))
    {
        const float widthNudgePercent = juce::jlimit (-60.0f, 60.0f, correlationDelta * 120.0f);
        const float newWidth = juce::jlimit (shaper->widthParam->range.start, shaper->widthParam->range.end,
                                              shaper->widthParam->get() + widthNudgePercent);
        shaper->widthParam->setValueNotifyingHost (shaper->widthParam->convertTo0to1 (newWidth));
    }

    // ---- Loudness: nudge the Limiter's Input Gain from the LUFS gap, and
    // make sure True Peak protection is on since we're deliberately driving
    // the signal closer to the reference's level.
    const float lufsDelta = referenceLufs.load() - capturedLufs.load();
    if (auto* limiter = dynamic_cast<MentalsLimiterAudioProcessor*> (suite.getSlotProcessor (ensureModuleInChain (MentalsSuiteAudioProcessor::moduleLimiter))))
    {
        const float gainNudge = juce::jlimit (-12.0f, 12.0f, lufsDelta);
        const float newGain = juce::jlimit (limiter->inputGainParam->range.start, limiter->inputGainParam->range.end,
                                             limiter->inputGainParam->get() + gainNudge);
        limiter->inputGainParam->setValueNotifyingHost (limiter->inputGainParam->convertTo0to1 (newGain));
        limiter->truePeakParam->setValueNotifyingHost (1.0f);
    }

    // ---- Mastering Meter: snap its Target readout to whichever preset is
    // closest to the reference's own loudness, purely so the meter's own
    // display reads consistently with what Master Assistant just matched
    // to.
    if (auto* meter = dynamic_cast<MentalsMasteringMeterAudioProcessor*> (suite.getSlotProcessor (ensureModuleInChain (MentalsSuiteAudioProcessor::moduleMasteringMeter))))
    {
        int bestIndex = 0;
        float bestDiff = std::numeric_limits<float>::max();
        for (int i = 0; i < (int) MentalsMasteringMeterAudioProcessor::targetLufsValues.size(); ++i)
        {
            const float diff = std::abs (MentalsMasteringMeterAudioProcessor::targetLufsValues[(size_t) i] - referenceLufs.load());
            if (diff < bestDiff) { bestDiff = diff; bestIndex = i; }
        }
        meter->targetPresetParam->setValueNotifyingHost (meter->targetPresetParam->convertTo0to1 ((float) bestIndex));
    }
}
