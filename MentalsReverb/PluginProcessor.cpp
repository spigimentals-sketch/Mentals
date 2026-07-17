#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    const int preDelaySamples = (int) (preDelayMsParam->get() * 0.001 * currentSampleRate);
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

    // ---- External dry/wet mix ---------------------------------------------------
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        const auto* dry = dryCopyBuffer.getReadPointer (ch);
        for (int n = 0; n < numSamples; ++n)
            data[n] = dry[n] * (1.0f - mix) + data[n] * mix;
    }

    updateOutputLevelMeter (buffer);
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
void MentalsReverbAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto apply = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };

    resetToDefault();
    apply (roomSizeParam, 85.0f); apply (dampingParam, 20.0f); apply (widthParam, 100.0f);
    apply (mixParam, 45.0f); apply (preDelayMsParam, 30.0f); apply (shimmerAmountParam, 65.0f);
    presetManager.savePreset ("Shimmer");

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

