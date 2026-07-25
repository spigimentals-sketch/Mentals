#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <functional>

// Note divisions offered for tempo-synced Pre-Delay -- see class comment.
const juce::StringArray MentalsReverbAudioProcessor::preDelayDivisionNames
{
    "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1/1"
};
const float MentalsReverbAudioProcessor::preDelayDivisionBeats[MentalsReverbAudioProcessor::numPreDelayDivisions]
{
    0.125f, 0.16667f, 0.25f, 0.33333f, 0.5f, 0.66667f, 1.0f, 2.0f, 4.0f
};

// Early reflections: a fixed, hand-placed tapped-delay pattern (not derived
// from any real room measurement) -- alternating taps are panned mostly to
// one side then the other (see processBlock()) purely so the pattern reads
// as spatial rather than a single mono slap, decreasing in gain with delay
// the way real reflections lose energy the further/later they arrive.
const float MentalsReverbAudioProcessor::earlyTapDelaysMs[MentalsReverbAudioProcessor::numEarlyTaps]
{
    7.0f, 13.0f, 19.0f, 27.0f, 34.0f, 43.0f, 53.0f, 67.0f
};
const float MentalsReverbAudioProcessor::earlyTapGains[MentalsReverbAudioProcessor::numEarlyTaps]
{
    0.60f, 0.50f, 0.42f, 0.35f, 0.28f, 0.22f, 0.17f, 0.12f
};

//==============================================================================
MentalsReverbAudioProcessor::MentalsReverbAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    roomSizeParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("roomSize"));
    dampingParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("damping"));
    widthParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("width"));
    mixParam         = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("mix"));
    preDelayMsParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("preDelayMs"));
    freezeParam      = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("freeze"));
    shimmerAmountParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("shimmerAmount"));
    lowCutParam      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("lowCut"));
    highCutParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("highCut"));
    earlyReflectionsParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("earlyReflections"));
    modDepthParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("modDepth"));
    modRateHzParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("modRateHz"));
    duckingParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("ducking"));
    duckingAttackMsParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("duckingAttackMs"));
    duckingReleaseMsParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("duckingReleaseMs"));
    tempoSyncParam   = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("tempoSync"));
    preDelayDivisionParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("preDelayDivision"));
    stereoParam      = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("stereo"));

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsReverbAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "roomSize", "Room Size",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "damping", "Damping",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "width", "Width",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 30.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "preDelayMs", "Pre-Delay",
        juce::NormalisableRange<float> (0.0f, 200.0f, 0.01f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "freeze", "Freeze", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "shimmerAmount", "Shimmer",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lowCut", "Low Cut",
        juce::NormalisableRange<float> (20.0f, 1000.0f, 0.1f, 0.3f), 20.0f, // 20Hz default is effectively off
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "highCut", "High Cut",
        juce::NormalisableRange<float> (1000.0f, 20000.0f, 1.0f, 0.4f), 20000.0f, // 20kHz default is effectively off
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "earlyReflections", "Early Refl.",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "modDepth", "Mod Depth",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "modRateHz", "Mod Rate",
        juce::NormalisableRange<float> (0.05f, 5.0f, 0.001f, 0.5f), 0.4f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ducking", "Ducking",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "duckingAttackMs", "Duck Attack",
        juce::NormalisableRange<float> (1.0f, 200.0f, 0.01f, 0.4f), 8.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "duckingReleaseMs", "Duck Release",
        juce::NormalisableRange<float> (20.0f, 2000.0f, 0.1f, 0.4f), 250.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "tempoSync", "Tempo Sync", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "preDelayDivision", "Pre-Delay Division", preDelayDivisionNames, 4)); // default "1/8"

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereo", "Stereo", true));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsReverbAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock,
                                         (juce::uint32) juce::jmax (1, getMainBusNumOutputChannels()) };
    reverb.prepare (spec);
    reverb.reset();

    constexpr float maxPreDelayMs = 200.0f;
    const int bufSize = (int) (maxPreDelayMs * 0.001 * sampleRate) + 4;
    for (auto& buf : preDelayBuffers)
        buf.assign ((size_t) bufSize, 0.0f);
    preDelayWritePos = { 0, 0 };

    constexpr float shimmerFeedbackMs = 50.0f;
    const int shimmerBufSize = (int) (shimmerFeedbackMs * 0.001 * sampleRate) + 4;
    for (int ch = 0; ch < 2; ++ch)
    {
        shimmerPitchShifters[(size_t) ch].prepare (sampleRate);
        shimmerFeedbackBuffers[(size_t) ch].assign ((size_t) shimmerBufSize, 0.0f);
    }
    shimmerFeedbackWritePos = { 0, 0 };
    dryCopyBuffer.setSize (2, samplesPerBlock);

    for (auto& filter : lowCutFilter)
        filter.reset();
    for (auto& filter : highCutFilter)
        filter.reset();
    lastLowCutFreqHz = -1.0f;  // forces coefficients to be set on the first block
    lastHighCutFreqHz = -1.0f;

    for (int t = 0; t < numEarlyTaps; ++t)
        earlyTapSamples[(size_t) t] = (int) (earlyTapDelaysMs[t] * 0.001 * sampleRate);
    const float maxEarlyTapMs = earlyTapDelaysMs[numEarlyTaps - 1];
    const int earlyBufSize = (int) (maxEarlyTapMs * 0.001 * sampleRate) + 4;
    earlyReflectionBuffer.assign ((size_t) earlyBufSize, 0.0f);
    earlyReflectionWritePos = 0;

    const int modBufSize = (int) ((modBaseDelayMs + modMaxDepthMs) * 0.001 * sampleRate) + 4;
    for (auto& buf : modDelayBuffers)
        buf.assign ((size_t) modBufSize, 0.0f);
    modWritePos = { 0, 0 };
    modPhase = { 0.0f, modPhaseOffsetRad }; // channels start 90 degrees apart and stay that way (same rate)

    duckingEnvelope = { 0.0f, 0.0f };
    currentDuckingGainReductionDb = 0.0f;

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
}

bool MentalsReverbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void MentalsReverbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    // ---- Pre-delay ------------------------------------------------------------
    const float preDelayMs = tempoSyncParam->get() ? computeTempoSyncedPreDelayMs() : preDelayMsParam->get();
    const int preDelaySamples = (int) (preDelayMs * 0.001 * currentSampleRate);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& pdBuf = preDelayBuffers[(size_t) ch];
        const int bufSize = (int) pdBuf.size();
        auto* data = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            const int writeIdx = preDelayWritePos[(size_t) ch];
            const int readIdx  = ((writeIdx - preDelaySamples) % bufSize + bufSize) % bufSize;

            const float input = data[n];
            pdBuf[(size_t) writeIdx] = input;
            data[n] = pdBuf[(size_t) readIdx];

            preDelayWritePos[(size_t) ch] = (writeIdx + 1) % bufSize;
        }
    }

    // ---- Shimmer feedback injection (pre-reverb) --------------------------------
    // Keep a clean copy of the dry (pre-delayed) signal for the external
    // mix below, then sum in last block's pitch-shifted wet tail before the
    // signal reaches the reverb, so it re-reverberates and re-shifts each
    // cycle -- see class comment.
    const float shimmerAmount = juce::jlimit (0.0f, 1.0f, shimmerAmountParam->get() * 0.01f);
    constexpr float maxShimmerFeedbackGain = 0.85f; // keeps the octave-up feedback loop from building up without bound
    constexpr float shimmerRatio = 2.0f; // one octave up

    dryCopyBuffer.setSize (juce::jmax (2, numChannels), numSamples, false, false, true);
    for (int ch = 0; ch < numChannels; ++ch)
        dryCopyBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // A fixed ~50ms delay implemented as "read-before-write at the same
    // index": each slot holds whatever the post-reverb tap loop below wrote
    // exactly fbSize samples ago, so reading it here (before that loop
    // overwrites the same slots with this block's new tap output) recovers
    // a clean, constant-length delay across the reverb call in between.
    // startPos is shared with the tap loop below so both traverse the exact
    // same sequence of slots; shimmerFeedbackWritePos itself only advances
    // once, after both loops have run.
    if (shimmerAmount > 0.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& fbBuf = shimmerFeedbackBuffers[(size_t) ch];
            const int fbSize = (int) fbBuf.size();
            auto* data = buffer.getWritePointer (ch);

            int pos = shimmerFeedbackWritePos[(size_t) ch];
            for (int n = 0; n < numSamples; ++n)
            {
                data[n] += fbBuf[(size_t) pos] * shimmerAmount * maxShimmerFeedbackGain;
                pos = (pos + 1) % fbSize;
            }
        }
    }

    // ---- Reverb -----------------------------------------------------------------
    juce::dsp::Reverb::Parameters params;
    params.roomSize   = juce::jlimit (0.0f, 1.0f, roomSizeParam->get() * 0.01f);
    params.damping    = juce::jlimit (0.0f, 1.0f, dampingParam->get() * 0.01f);
    params.width      = juce::jlimit (0.0f, 1.0f, widthParam->get() * 0.01f);
    const float mix   = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);
    params.wetLevel   = 1.0f; // internal mix is bypassed -- dry/wet blend is done externally below
    params.dryLevel   = 0.0f;
    params.freezeMode = freezeParam->get() ? 1.0f : 0.0f;
    reverb.setParameters (params);

    // juce::dsp::Reverb::process() dispatches to mono or stereo internally
    // based on the block's channel count -- no separate mono call needed.
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context);

    // ---- Shimmer pitch-shift tap (post-reverb) ----------------------------------
    // buffer now holds the 100%-wet reverb output. Shift it up an octave,
    // stash it for next block's feedback injection above, and layer it into
    // the audible wet signal now.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& fbBuf = shimmerFeedbackBuffers[(size_t) ch];
        const int fbSize = (int) fbBuf.size();
        auto* data = buffer.getWritePointer (ch);
        auto& shifter = shimmerPitchShifters[(size_t) ch];

        int pos = shimmerFeedbackWritePos[(size_t) ch]; // same starting slot the injection loop above read from
        for (int n = 0; n < numSamples; ++n)
        {
            const float shifted = shifter.process (data[n], shimmerRatio);
            fbBuf[(size_t) pos] = shifted;
            pos = (pos + 1) % fbSize;

            data[n] += shifted * shimmerAmount;
        }
        shimmerFeedbackWritePos[(size_t) ch] = pos; // advance for real, once, after both passes over this block
    }

    // ---- Early reflections (from the dry pre-delayed signal, parallel to
    // the diffuse tail -- see class comment) --------------------------------------
    const float earlyReflectionsAmount = juce::jlimit (0.0f, 1.0f, earlyReflectionsParam->get() * 0.01f);
    if (earlyReflectionsAmount > 0.0f)
    {
        auto* dry0 = dryCopyBuffer.getReadPointer (0);
        auto* dry1 = numChannels > 1 ? dryCopyBuffer.getReadPointer (1) : dry0;
        auto* out0 = buffer.getWritePointer (0);
        auto* out1 = numChannels > 1 ? buffer.getWritePointer (1) : out0;

        const int erBufSize = (int) earlyReflectionBuffer.size();
        int pos = earlyReflectionWritePos;

        for (int n = 0; n < numSamples; ++n)
        {
            earlyReflectionBuffer[(size_t) pos] = (dry0[n] + dry1[n]) * 0.5f;

            // Alternating taps lean left/right (with a smaller cross-feed to
            // the other side) so the pattern reads as spatial rather than a
            // single mono slap -- see earlyTapDelaysMs/earlyTapGains.
            float erL = 0.0f, erR = 0.0f;
            for (int t = 0; t < numEarlyTaps; ++t)
            {
                const int idx = ((pos - earlyTapSamples[(size_t) t]) % erBufSize + erBufSize) % erBufSize;
                const float tap = earlyReflectionBuffer[(size_t) idx] * earlyTapGains[t];
                if ((t & 1) == 0) { erL += tap; erR += tap * 0.3f; }
                else              { erR += tap; erL += tap * 0.3f; }
            }

            out0[n] += erL * earlyReflectionsAmount;
            out1[n] += erR * earlyReflectionsAmount;

            pos = (pos + 1) % erBufSize;
        }
        earlyReflectionWritePos = pos;
    }

    // ---- Modulation (wet signal pitch/comb drift -- see class comment) ----------
    const float modDepthAmount = juce::jlimit (0.0f, 1.0f, modDepthParam->get() * 0.01f);
    if (modDepthAmount > 0.0f)
    {
        const float phaseIncrement = juce::MathConstants<float>::twoPi * modRateHzParam->get() / (float) currentSampleRate;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& modBuf = modDelayBuffers[(size_t) ch];
            const int modBufSize = (int) modBuf.size();
            auto* data = buffer.getWritePointer (ch);
            float phase = modPhase[(size_t) ch];
            int pos = modWritePos[(size_t) ch];

            for (int n = 0; n < numSamples; ++n)
            {
                modBuf[(size_t) pos] = data[n];

                const float delaySamples = (modBaseDelayMs + modMaxDepthMs * modDepthAmount * std::sin (phase))
                                            * 0.001f * (float) currentSampleRate;

                // Linear-interpolated fractional read -- delay time moves
                // continuously with the LFO, so an integer-only read would
                // produce audible zipper noise stepping between samples.
                const float readPosF = (float) pos - delaySamples;
                int readIdx0 = (int) std::floor (readPosF);
                const float frac = readPosF - (float) readIdx0;
                readIdx0 = ((readIdx0 % modBufSize) + modBufSize) % modBufSize;
                const int readIdx1 = (readIdx0 + 1) % modBufSize;

                data[n] = modBuf[(size_t) readIdx0] * (1.0f - frac) + modBuf[(size_t) readIdx1] * frac;

                phase += phaseIncrement;
                if (phase >= juce::MathConstants<float>::twoPi)
                    phase -= juce::MathConstants<float>::twoPi;

                pos = (pos + 1) % modBufSize;
            }

            modPhase[(size_t) ch] = phase;
            modWritePos[(size_t) ch] = pos;
        }
    }

    // ---- Low Cut / High Cut (wet tone shaping) -----------------------------------
    // Filters the wet signal only (reverb + shimmer, already summed above),
    // leaving the dry signal and the shimmer feedback loop's own tone alone
    // -- see class comment for why this is kept separate from Damping.
    const float lowCutFreqHz = lowCutParam->get();
    if (lowCutFreqHz != lastLowCutFreqHz)
    {
        lastLowCutFreqHz = lowCutFreqHz;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, lowCutFreqHz, lowHighCutQ);
        for (auto& filter : lowCutFilter)
            filter.coefficients = coeffs;
    }

    const float highCutFreqHz = highCutParam->get();
    if (highCutFreqHz != lastHighCutFreqHz)
    {
        lastHighCutFreqHz = highCutFreqHz;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, highCutFreqHz, lowHighCutQ);
        for (auto& filter : highCutFilter)
            filter.coefficients = coeffs;
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int n = 0; n < numSamples; ++n)
            data[n] = highCutFilter[(size_t) ch].processSample (lowCutFilter[(size_t) ch].processSample (data[n]));
    }

    // ---- Ducking (wet gain follows the dry envelope -- see class comment) -------
    const float duckingAmount = juce::jlimit (0.0f, 1.0f, duckingParam->get() * 0.01f);
    float blockMinDuckGainDb = 0.0f;
    if (duckingAmount > 0.0f)
    {
        // Recomputed every block straight from the live knobs -- see the
        // member comment on why this doesn't need the "only recompute when
        // changed" caching used for the filter coefficients above.
        const float duckingAttackCoeff  = 1.0f - std::exp (-1.0f / (duckingAttackMsParam->get()  * 0.001f * (float) currentSampleRate));
        const float duckingReleaseCoeff = 1.0f - std::exp (-1.0f / (duckingReleaseMsParam->get() * 0.001f * (float) currentSampleRate));
        constexpr float duckingSensitivity = 4.0f; // scales typical program levels into a usable 0..1 duck range

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const auto* dry = dryCopyBuffer.getReadPointer (ch);
            float env = duckingEnvelope[(size_t) ch];

            for (int n = 0; n < numSamples; ++n)
            {
                const float rectified = std::abs (dry[n]);
                env += (rectified - env) * (rectified > env ? duckingAttackCoeff : duckingReleaseCoeff);

                const float duckGain = 1.0f - duckingAmount * juce::jlimit (0.0f, 1.0f, env * duckingSensitivity);
                data[n] *= duckGain;

                blockMinDuckGainDb = juce::jmin (blockMinDuckGainDb, juce::Decibels::gainToDecibels (duckGain, -60.0f));
            }

            duckingEnvelope[(size_t) ch] = env;
        }
    }
    currentDuckingGainReductionDb.store (blockMinDuckGainDb);

    // ---- External dry/wet mix ---------------------------------------------------
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        const auto* dry = dryCopyBuffer.getReadPointer (ch);
        for (int n = 0; n < numSamples; ++n)
            data[n] = dry[n] * (1.0f - mix) + data[n] * mix;
    }

    if (! stereoParam->get() && numChannels > 1)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float avg = 0.5f * (buffer.getReadPointer (0)[n] + buffer.getReadPointer (1)[n]);
            buffer.getWritePointer (0)[n] = avg;
            buffer.getWritePointer (1)[n] = avg;
        }
    }

    updateOutputLevelMeter (buffer);
}

float MentalsReverbAudioProcessor::computeTempoSyncedPreDelayMs() const
{
    double bpm = 120.0; // fallback when the host provides no transport/tempo (e.g. standalone with nothing playing)

    if (auto* ph = getPlayHead())
        if (auto position = ph->getPosition())
            if (auto bpmFromHost = position->getBpm())
                bpm = *bpmFromHost;

    const int divisionIndex = preDelayDivisionParam != nullptr ? preDelayDivisionParam->getIndex() : 4;
    const float beats = preDelayDivisionBeats[(size_t) juce::jlimit (0, numPreDelayDivisions - 1, divisionIndex)];
    const float msPerBeat = (float) (60000.0 / bpm);

    // Clamped to the pre-delay buffer's own 0-200ms range (see
    // prepareToPlay()) -- slow tempos on long divisions (e.g. 1/1 at
    // 60bpm = 4 seconds) would ask for a pre-delay far beyond anything
    // musically sensible for a reverb anyway, so hitting the ceiling here
    // is the correct outcome, not a bug to work around.
    return juce::jlimit (0.0f, 200.0f, beats * msPerBeat);
}

void MentalsReverbAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
// Per-preset-name existence check (not "is the whole library empty") so
// this can keep adding newly-introduced factory presets on top of an
// older install without ever touching a preset (factory or user-saved)
// that's already on disk under that name -- e.g. "Shimmer" was this
// plugin's only factory preset before the rest below were added.
void MentalsReverbAudioProcessor::seedFactoryPresetsIfMissing()
{
    const auto presetsDir = presetManager.getPresetsDirectory();

    struct Settings
    {
        float roomSize = 50.0f, damping = 50.0f, width = 100.0f, mix = 30.0f, preDelayMs = 20.0f, shimmerAmount = 0.0f;
        float lowCut = 20.0f, highCut = 20000.0f; // off by default -- matches this plugin's behaviour before these existed
        float earlyReflections = 0.0f, modDepth = 0.0f, modRateHz = 0.4f, ducking = 0.0f; // all off by default, same reasoning
        float duckingAttackMs = 8.0f, duckingReleaseMs = 250.0f; // only audible once ducking > 0 anyway
    };

    auto applyPreset = [this, &presetsDir] (const juce::String& name, std::function<void (Settings&)> configure)
    {
        if (presetsDir.getChildFile (name + ".xml").existsAsFile())
            return; // never overwrite a preset (factory or user-saved) already on disk under this name

        Settings s;
        configure (s);

        resetToDefault();
        auto apply = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };
        apply (roomSizeParam, s.roomSize); apply (dampingParam, s.damping); apply (widthParam, s.width);
        apply (mixParam, s.mix); apply (preDelayMsParam, s.preDelayMs); apply (shimmerAmountParam, s.shimmerAmount);
        apply (lowCutParam, s.lowCut); apply (highCutParam, s.highCut);
        apply (earlyReflectionsParam, s.earlyReflections); apply (modDepthParam, s.modDepth);
        apply (modRateHzParam, s.modRateHz); apply (duckingParam, s.ducking);
        apply (duckingAttackMsParam, s.duckingAttackMs); apply (duckingReleaseMsParam, s.duckingReleaseMs);
        presetManager.savePreset (name);
    };

    applyPreset ("Shimmer", [] (Settings& s)
    {
        s.roomSize = 85.0f; s.damping = 20.0f; s.width = 100.0f;
        s.mix = 45.0f; s.preDelayMs = 30.0f; s.shimmerAmount = 65.0f;
    });

    // ---- Classic reverb types -----------------------------------------------------
    applyPreset ("Hall", [] (Settings& s)
    {
        s.roomSize = 80.0f; s.damping = 35.0f; s.width = 100.0f; s.mix = 35.0f; s.preDelayMs = 25.0f;
    });

    applyPreset ("Chamber", [] (Settings& s)
    {
        s.roomSize = 55.0f; s.damping = 45.0f; s.width = 90.0f; s.mix = 30.0f; s.preDelayMs = 15.0f;
    });

    applyPreset ("Room", [] (Settings& s)
    {
        s.roomSize = 30.0f; s.damping = 50.0f; s.width = 80.0f; s.mix = 25.0f; s.preDelayMs = 8.0f;
    });

    applyPreset ("Plate", [] (Settings& s)
    {
        s.roomSize = 45.0f; s.damping = 15.0f; s.width = 100.0f; s.mix = 28.0f; s.preDelayMs = 5.0f;
    });

    applyPreset ("Spring", [] (Settings& s)
    {
        s.roomSize = 20.0f; s.damping = 60.0f; s.width = 60.0f; s.mix = 30.0f; s.preDelayMs = 0.0f;
    });

    // ---- Vocal-specific ------------------------------------------------------------
    applyPreset ("Lead Vox", [] (Settings& s)
    {
        s.roomSize = 40.0f; s.damping = 45.0f; s.width = 85.0f; s.mix = 20.0f; s.preDelayMs = 35.0f;
    });

    applyPreset ("BGV", [] (Settings& s)
    {
        s.roomSize = 60.0f; s.damping = 40.0f; s.width = 100.0f; s.mix = 40.0f; s.preDelayMs = 15.0f; s.shimmerAmount = 5.0f;
    });

    applyPreset ("Afro Vox", [] (Settings& s)
    {
        // Bright, present, vibrant space with a touch of shimmer for air --
        // the spacious-but-forward vocal reverb character common in
        // Afrobeats/Amapiano vocal chains, rather than a dark, washy hall.
        s.roomSize = 55.0f; s.damping = 20.0f; s.width = 100.0f; s.mix = 35.0f; s.preDelayMs = 20.0f; s.shimmerAmount = 15.0f;
    });

    // ---- Large venues ---------------------------------------------------------------
    applyPreset ("Concert", [] (Settings& s)
    {
        s.roomSize = 95.0f; s.damping = 30.0f; s.width = 100.0f; s.mix = 40.0f; s.preDelayMs = 40.0f;
    });

    applyPreset ("Church", [] (Settings& s)
    {
        s.roomSize = 90.0f; s.damping = 20.0f; s.width = 100.0f; s.mix = 45.0f; s.preDelayMs = 45.0f;
    });

    applyPreset ("Cathedral", [] (Settings& s)
    {
        s.roomSize = 100.0f; s.damping = 15.0f; s.width = 100.0f; s.mix = 55.0f; s.preDelayMs = 60.0f; s.shimmerAmount = 10.0f;
    });

    // ---- Other practical starting points --------------------------------------------
    applyPreset ("Vocal Booth", [] (Settings& s)
    {
        s.roomSize = 10.0f; s.damping = 60.0f; s.width = 50.0f; s.mix = 12.0f; s.preDelayMs = 0.0f;
    });

    applyPreset ("Drum Room", [] (Settings& s)
    {
        s.roomSize = 35.0f; s.damping = 55.0f; s.width = 90.0f; s.mix = 20.0f; s.preDelayMs = 5.0f;
    });

    applyPreset ("Studio Live Room", [] (Settings& s)
    {
        s.roomSize = 45.0f; s.damping = 40.0f; s.width = 90.0f; s.mix = 25.0f; s.preDelayMs = 10.0f;
    });

    applyPreset ("Podcast Voice", [] (Settings& s)
    {
        s.roomSize = 15.0f; s.damping = 55.0f; s.width = 60.0f; s.mix = 8.0f; s.preDelayMs = 0.0f;
    });

    applyPreset ("Master Bus Glue", [] (Settings& s)
    {
        s.roomSize = 25.0f; s.damping = 50.0f; s.width = 100.0f; s.mix = 6.0f; s.preDelayMs = 0.0f;
    });

    applyPreset ("Guitar Amp Spring", [] (Settings& s)
    {
        s.roomSize = 15.0f; s.damping = 65.0f; s.width = 40.0f; s.mix = 25.0f; s.preDelayMs = 0.0f;
    });

    applyPreset ("Ethereal Wash", [] (Settings& s)
    {
        s.roomSize = 100.0f; s.damping = 10.0f; s.width = 100.0f; s.mix = 60.0f; s.preDelayMs = 30.0f; s.shimmerAmount = 80.0f;
    });

    resetToDefault();
}

//==============================================================================
juce::AudioProcessorEditor* MentalsReverbAudioProcessor::createEditor()
{
    return new MentalsReverbAudioProcessorEditor (*this);
}

void MentalsReverbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsReverbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

