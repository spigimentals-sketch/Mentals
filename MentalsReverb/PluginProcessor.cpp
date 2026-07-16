#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MentalsReverbAudioProcessor::MentalsReverbAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    roomSizeParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("roomSize"));
    dampingParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("damping"));
    widthParam      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("width"));
    mixParam        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("mix"));
    preDelayMsParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("preDelayMs"));
    freezeParam     = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("freeze"));
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

    // ---- Reverb -----------------------------------------------------------------
    juce::dsp::Reverb::Parameters params;
    params.roomSize   = juce::jlimit (0.0f, 1.0f, roomSizeParam->get() * 0.01f);
    params.damping    = juce::jlimit (0.0f, 1.0f, dampingParam->get() * 0.01f);
    params.width      = juce::jlimit (0.0f, 1.0f, widthParam->get() * 0.01f);
    const float mix   = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);
    params.wetLevel   = mix;
    params.dryLevel   = 1.0f - mix;
    params.freezeMode = freezeParam->get() ? 1.0f : 0.0f;
    reverb.setParameters (params);

    // juce::dsp::Reverb::process() dispatches to mono or stereo internally
    // based on the block's channel count -- no separate mono call needed.
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context);

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

