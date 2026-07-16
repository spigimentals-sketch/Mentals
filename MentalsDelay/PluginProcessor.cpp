#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MentalsDelayAudioProcessor::MentalsDelayAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    delayTimeMsParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("delayTimeMs"));
    feedbackParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("feedback"));
    mixParam         = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("mix"));
    pingPongParam    = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("pingPong"));
    lowCutParam      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("lowCut"));
    highCutParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("highCut"));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsDelayAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delayTimeMs", "Delay Time",
        juce::NormalisableRange<float> (1.0f, 2000.0f, 0.01f, 0.35f), 350.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "feedback", "Feedback",
        juce::NormalisableRange<float> (0.0f, 95.0f, 0.01f), 35.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 35.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "pingPong", "Ping-Pong", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lowCut", "Feedback Low Cut",
        juce::NormalisableRange<float> (20.0f, 2000.0f, 0.01f, 0.3f), 150.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "highCut", "Feedback High Cut",
        juce::NormalisableRange<float> (1000.0f, 20000.0f, 0.01f, 0.3f), 8000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsDelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    constexpr float maxDelayMs = 2000.0f;
    const int bufSize = (int) (maxDelayMs * 0.001 * sampleRate) + 4; // +4 headroom for interpolation wraparound
    for (auto& dc : delayChannels)
        dc.prepare (bufSize);

    constexpr double rampSeconds = 0.05;
    smoothedDelaySamples.reset (sampleRate, rampSeconds);
    smoothedFeedback.reset     (sampleRate, rampSeconds);
    smoothedMix.reset          (sampleRate, 0.02);

    smoothedDelaySamples.setCurrentAndTargetValue (delayTimeMsParam->get() * 0.001f * (float) sampleRate);
    smoothedFeedback.setCurrentAndTargetValue     (feedbackParam->get() * 0.01f);
    smoothedMix.setCurrentAndTargetValue          (mixParam->get() * 0.01f);

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
}

bool MentalsDelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void MentalsDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    smoothedDelaySamples.setTargetValue (juce::jlimit (1.0f, (float) (delayChannels[0].buffer.size() - 1),
        delayTimeMsParam->get() * 0.001f * (float) currentSampleRate));
    smoothedFeedback.setTargetValue (feedbackParam->get() * 0.01f);
    smoothedMix.setTargetValue (mixParam->get() * 0.01f);

    const bool pingPong = pingPongParam->get();

    // One-pole coefficients recomputed once per block -- feedback tone
    // shaping doesn't need per-sample modulation.
    const float lowCutCoeff  = std::exp (-2.0f * juce::MathConstants<float>::pi * lowCutParam->get()  / (float) currentSampleRate);
    const float highCutCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi * highCutParam->get() / (float) currentSampleRate);

    for (int n = 0; n < numSamples; ++n)
    {
        const float delaySamples = smoothedDelaySamples.getNextValue();
        const float feedback     = smoothedFeedback.getNextValue();
        const float mix          = smoothedMix.getNextValue();

        std::array<float, 2> dry { 0.0f, 0.0f };
        std::array<float, 2> delayedRead { 0.0f, 0.0f };
        std::array<float, 2> feedbackSignal { 0.0f, 0.0f };

        for (int ch = 0; ch < numChannels; ++ch)
            dry[(size_t) ch] = buffer.getReadPointer (ch)[n];

        // Pass 1: read each channel's delay line and derive its (tone-shaped)
        // feedback signal. Both channels must be read before either is
        // written, since ping-pong crosses them in pass 2 below.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& dc = delayChannels[(size_t) ch];
            const int bufSize = (int) dc.buffer.size();

            const float readPos = (float) dc.writePos - delaySamples;
            const float wrapped = readPos >= 0.0f ? readPos : readPos + (float) bufSize;
            const int i0 = ((int) wrapped) % bufSize;
            const int i1 = (i0 + 1) % bufSize;
            const float frac = wrapped - std::floor (wrapped);

            const float delayed = dc.buffer[(size_t) i0] * (1.0f - frac) + dc.buffer[(size_t) i1] * frac;
            delayedRead[(size_t) ch] = delayed;

            // One-pole low-pass gives highCutState directly; the high-pass
            // is derived as (signal - its own low-passed version), the
            // standard one-pole HP-from-LP trick.
            dc.lowCutState = lowCutCoeff * dc.lowCutState + (1.0f - lowCutCoeff) * delayed;
            const float afterHighPass = delayed - dc.lowCutState;

            dc.highCutState = highCutCoeff * dc.highCutState + (1.0f - highCutCoeff) * afterHighPass;
            feedbackSignal[(size_t) ch] = dc.highCutState;
        }

        // Pass 2: write dry + (possibly cross-channel) feedback into each
        // delay line, and output the dry/wet mix.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& dc = delayChannels[(size_t) ch];
            const int bufSize = (int) dc.buffer.size();

            const int feedSourceCh = (pingPong && numChannels > 1) ? (1 - ch) : ch;
            const float toWrite = dry[(size_t) ch] + feedbackSignal[(size_t) feedSourceCh] * feedback;

            dc.buffer[(size_t) dc.writePos] = toWrite;
            dc.writePos = (dc.writePos + 1) % bufSize;

            const float wet = delayedRead[(size_t) ch];
            buffer.getWritePointer (ch)[n] = dry[(size_t) ch] * (1.0f - mix) + wet * mix;
        }
    }

    updateOutputLevelMeter (buffer);
}

void MentalsDelayAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
juce::AudioProcessorEditor* MentalsDelayAudioProcessor::createEditor()
{
    return new MentalsDelayAudioProcessorEditor (*this);
}

void MentalsDelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsDelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

