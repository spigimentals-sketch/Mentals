#include "PluginProcessor.h"
#include "PluginEditor.h"

const int MentalsImagerAudioProcessor::bandCountChoices[4] = { 1, 2, 3, 4 };

//==============================================================================
MentalsImagerAudioProcessor::MentalsImagerAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    bandsParam      = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("bands"));
    crossover1Param = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("crossover1"));
    crossover2Param = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("crossover2"));
    crossover3Param = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("crossover3"));
    width1Param     = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("width1"));
    width2Param     = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("width2"));
    width3Param     = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("width3"));
    width4Param     = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("width4"));
    mixParam        = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("mix"));
    stereoParam     = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("stereo"));

    for (auto& a : goniometerL) a.store (0.0f);
    for (auto& a : goniometerR) a.store (0.0f);

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsImagerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "bands", "Bands", juce::StringArray { "1", "2", "3", "4" }, 1)); // default 2-band, Low/High

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "crossover1", "Crossover 1",
        juce::NormalisableRange<float> (20.0f, 2000.0f, 1.0f, 0.35f), 200.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "crossover2", "Crossover 2",
        juce::NormalisableRange<float> (200.0f, 8000.0f, 1.0f, 0.35f), 1500.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "crossover3", "Crossover 3",
        juce::NormalisableRange<float> (1000.0f, 18000.0f, 1.0f, 0.35f), 6000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    auto widthRange = juce::NormalisableRange<float> (0.0f, 200.0f, 0.01f);
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "width1", "Width 1", widthRange, 100.0f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "width2", "Width 2", widthRange, 100.0f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "width3", "Width 3", widthRange, 100.0f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "width4", "Width 4", widthRange, 100.0f, juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereo", "Stereo", true));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsImagerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    sideSplitter.reset();
    sideSplitter.lastNumBands = -1;
    sideSplitter.lastFreq1 = sideSplitter.lastFreq2 = sideSplitter.lastFreq3 = -1.0f;
    lastNumBandsUsed = -1;

    for (auto& a : goniometerL) a.store (0.0f);
    for (auto& a : goniometerR) a.store (0.0f);
    goniometerWritePos.store (0);
    smoothedCorrelation.store (1.0f);

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
}

bool MentalsImagerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MentalsImagerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    const int numSamples = buffer.getNumSamples();

    const int numBands = bandCountChoices[(size_t) juce::jlimit (0, 3, bandsParam->getIndex())];

    // Enforce increasing crossover points with a minimum gap, same rationale
    // as Stereo Shaper's two-crossover clamp: no band can ever collapse to a
    // negative-width bandpass.
    const float freq1 = crossover1Param->get();
    const float freq2 = juce::jmax (crossover2Param->get(), freq1 + 20.0f);
    const float freq3 = juce::jmax (crossover3Param->get(), freq2 + 20.0f);
    sideSplitter.updateIfNeeded (currentSampleRate, numBands, freq1, freq2, freq3);

    if (numBands != lastNumBandsUsed)
    {
        sideSplitter.reset(); // avoids a click from stale filter state on the newly (in)active stages
        lastNumBandsUsed = numBands;
    }

    const std::array<float, 4> widthScale {
        width1Param->get() * 0.01f, width2Param->get() * 0.01f,
        width3Param->get() * 0.01f, width4Param->get() * 0.01f
    };

    const float mix = juce::jlimit (0.0f, 1.0f, mixParam->get() * 0.01f);

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
    int gPos = goniometerWritePos.load (std::memory_order_relaxed);

    for (int n = 0; n < numSamples; ++n)
    {
        const float L = left[n];
        const float R = right[n];

        const float mid  = 0.5f * (L + R);
        const float side = 0.5f * (L - R);

        const auto bands = sideSplitter.process (side, numBands);
        float shapedSide = 0.0f;
        for (int b = 0; b < numBands; ++b)
            shapedSide += bands[(size_t) b] * widthScale[(size_t) b];

        const float wetL = mid + shapedSide;
        const float wetR = mid - shapedSide;

        const float finalL = L * (1.0f - mix) + wetL * mix;
        const float finalR = R * (1.0f - mix) + wetR * mix;

        left[n]  = finalL;
        right[n] = finalR;

        sumLR += (double) finalL * finalR;
        sumLL += (double) finalL * finalL;
        sumRR += (double) finalR * finalR;

        goniometerL[(size_t) gPos].store (finalL, std::memory_order_relaxed);
        goniometerR[(size_t) gPos].store (finalR, std::memory_order_relaxed);
        gPos = (gPos + 1) % goniometerSize;
    }

    goniometerWritePos.store (gPos, std::memory_order_relaxed);

    const double denom = std::sqrt (sumLL * sumRR) + 1.0e-9;
    const float blockCorrelation = (float) juce::jlimit (-1.0, 1.0, sumLR / denom);
    const float previousCorrelation = smoothedCorrelation.load();
    smoothedCorrelation.store (previousCorrelation * 0.7f + blockCorrelation * 0.3f);

    // Mono-compatibility override: always collapses fully to mono no matter
    // what the per-band Width knobs are dialled in at -- same convention
    // every Mentals plugin now has.
    if (! stereoParam->get())
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float avg = 0.5f * (left[n] + right[n]);
            left[n] = avg;
            right[n] = avg;
        }
    }

    updateOutputLevelMeter (buffer);
}

void MentalsImagerAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
juce::AudioProcessorEditor* MentalsImagerAudioProcessor::createEditor()
{
    return new MentalsImagerAudioProcessorEditor (*this);
}

void MentalsImagerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsImagerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}

//==============================================================================
// Factory presets: common imaging starting points.
//==============================================================================
void MentalsImagerAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto applyF = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };
    auto applyChoice = [] (juce::AudioParameterChoice* p, int index) { p->setValueNotifyingHost (p->convertTo0to1 ((float) index)); };

    resetToDefault();
    applyChoice (bandsParam, 1); // 2 bands
    applyF (crossover1Param, 150.0f);
    applyF (width1Param, 0.0f); applyF (width2Param, 130.0f);
    presetManager.savePreset ("Bass Mono Safe");

    resetToDefault();
    applyChoice (bandsParam, 2); // 3 bands
    applyF (crossover1Param, 200.0f); applyF (crossover2Param, 3000.0f);
    applyF (width1Param, 60.0f); applyF (width2Param, 110.0f); applyF (width3Param, 150.0f);
    presetManager.savePreset ("Wide Mix Bus");

    resetToDefault();
    applyChoice (bandsParam, 3); // 4 bands
    applyF (crossover1Param, 120.0f); applyF (crossover2Param, 1000.0f); applyF (crossover3Param, 6000.0f);
    applyF (width1Param, 20.0f); applyF (width2Param, 90.0f); applyF (width3Param, 140.0f); applyF (width4Param, 170.0f);
    presetManager.savePreset ("Precision 4-Band");

    resetToDefault();
    applyChoice (bandsParam, 0); // 1 band, simple mode
    applyF (width1Param, 130.0f);
    presetManager.savePreset ("Simple Widen");

    resetToDefault();
}
