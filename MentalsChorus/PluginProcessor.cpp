#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MentalsChorusAudioProcessor::MentalsChorusAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    rateParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("rate"));
    depthParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("depth"));
    delayParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("delay"));
    feedbackParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("feedback"));
    mixParam      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("mix"));
    stereoParam   = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("stereo"));

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsChorusAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "rate", "Rate",
        juce::NormalisableRange<float> (0.05f, 5.0f, 0.001f, 0.5f), 0.5f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "depth", "Depth",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 3.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "delay", "Delay",
        juce::NormalisableRange<float> (1.0f, 30.0f, 0.01f), 15.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "feedback", "Feedback",
        juce::NormalisableRange<float> (0.0f, 90.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereo", "Stereo", true));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsChorusAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    // Max possible delay (Delay + Depth, both at their parameter maxima)
    // plus a little headroom, rounded up to whole samples.
    const int delayLineSize = juce::jmax (8, (int) (0.001 * (30.0f + 10.0f + 5.0f) * sampleRate));

    for (int ch = 0; ch < maxSupportedChannels; ++ch)
    {
        delayLines[(size_t) ch].assign ((size_t) delayLineSize, 0.0f);
        writePos[(size_t) ch] = 0;
        lastWet[(size_t) ch] = 0.0f;
    }

    // Channel 1 (and any beyond) start 90 degrees ahead of channel 0 for
    // stereo width -- see class comment.
    lfoPhase[0] = 0.0f;
    for (int ch = 1; ch < maxSupportedChannels; ++ch)
        lfoPhase[(size_t) ch] = juce::MathConstants<float>::halfPi;

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
}

bool MentalsChorusAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

float MentalsChorusAudioProcessor::readInterpolated (const std::vector<float>& buffer, int writePosIn, float delaySamples)
{
    const int bufSize = (int) buffer.size();
    float readPos = (float) writePosIn - delaySamples;
    while (readPos < 0.0f)
        readPos += (float) bufSize;

    const int i0 = (int) readPos;
    const int i1 = (i0 + 1) % bufSize;
    const float frac = readPos - (float) i0;
    return buffer[(size_t) i0] * (1.0f - frac) + buffer[(size_t) i1] * frac;
}

void MentalsChorusAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const int numChannels = juce::jmin (buffer.getNumChannels(), maxSupportedChannels);
    const int numSamples  = buffer.getNumSamples();

    const float rateHz     = rateParam->get();
    const float depthMs    = depthParam->get();
    const float centreMs   = delayParam->get();
    const float feedback   = juce::jlimit (0.0f, 0.9f, feedbackParam->get() * 0.01f);
    const float mix        = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);
    const float phaseInc   = juce::MathConstants<float>::twoPi * rateHz / (float) currentSampleRate;

    for (int n = 0; n < numSamples; ++n)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const float dry = data[n];

            auto& delayLine = delayLines[(size_t) ch];
            const int bufSize = (int) delayLine.size();
            int& wp = writePos[(size_t) ch];

            delayLine[(size_t) wp] = dry + feedback * lastWet[(size_t) ch];

            const float lfoValue = std::sin (lfoPhase[(size_t) ch]);
            const float delayMs = juce::jlimit (0.5f, (float) bufSize * 1000.0f / (float) currentSampleRate - 1.0f,
                                                 centreMs + depthMs * lfoValue);
            const float delaySamples = delayMs * 0.001f * (float) currentSampleRate;

            const float wet = readInterpolated (delayLine, wp, delaySamples);
            lastWet[(size_t) ch] = wet;

            wp = (wp + 1) % bufSize;

            lfoPhase[(size_t) ch] += phaseInc;
            if (lfoPhase[(size_t) ch] >= juce::MathConstants<float>::twoPi)
                lfoPhase[(size_t) ch] -= juce::MathConstants<float>::twoPi;

            data[n] = dry * (1.0f - mix) + wet * mix;
        }
    }

    lastLfoPhase01.store (lfoPhase[0] / juce::MathConstants<float>::twoPi);

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

void MentalsChorusAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
juce::AudioProcessorEditor* MentalsChorusAudioProcessor::createEditor()
{
    return new MentalsChorusAudioProcessorEditor (*this);
}

void MentalsChorusAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsChorusAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
// Factory presets: common chorus-character starting points.
//==============================================================================
void MentalsChorusAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto applyF = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };

    resetToDefault();
    applyF (rateParam, 0.3f); applyF (depthParam, 2.0f); applyF (delayParam, 12.0f);
    applyF (feedbackParam, 0.0f); applyF (mixParam, 25.0f);
    presetManager.savePreset ("Subtle Shimmer");

    resetToDefault();
    applyF (rateParam, 0.8f); applyF (depthParam, 5.0f); applyF (delayParam, 15.0f);
    applyF (feedbackParam, 10.0f); applyF (mixParam, 50.0f);
    presetManager.savePreset ("Classic Chorus");

    resetToDefault();
    applyF (rateParam, 0.6f); applyF (depthParam, 4.0f); applyF (delayParam, 18.0f);
    applyF (feedbackParam, 15.0f); applyF (mixParam, 40.0f);
    presetManager.savePreset ("Guitar Ensemble");

    resetToDefault();
    applyF (rateParam, 0.4f); applyF (depthParam, 3.0f); applyF (delayParam, 20.0f);
    applyF (feedbackParam, 5.0f); applyF (mixParam, 35.0f);
    presetManager.savePreset ("Wide Vocal Double");

    resetToDefault();
    applyF (rateParam, 1.5f); applyF (depthParam, 8.0f); applyF (delayParam, 10.0f);
    applyF (feedbackParam, 25.0f); applyF (mixParam, 55.0f);
    presetManager.savePreset ("Deep Sweep");

    resetToDefault();
    applyF (rateParam, 0.25f); applyF (depthParam, 6.0f); applyF (delayParam, 22.0f);
    applyF (feedbackParam, 20.0f); applyF (mixParam, 60.0f);
    presetManager.savePreset ("Synth Pad Motion");

    resetToDefault();
}
