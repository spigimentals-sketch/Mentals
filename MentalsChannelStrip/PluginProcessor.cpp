#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MentalsChannelStripAudioProcessor::MentalsChannelStripAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    hpfFreqParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("hpfFreq"));
    lpfFreqParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("lpfFreq"));
    filterSplitParam = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("filterSplit"));
    filtersInParam   = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("filtersIn"));

    compThresholdParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("compThreshold"));
    compRatioParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("compRatio"));
    compAttackParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("compAttack"));
    compReleaseParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("compRelease"));
    compMakeupParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("compMakeup"));

    gateThresholdParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("gateThreshold"));
    gateRatioParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("gateRatio"));
    gateAttackParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("gateAttack"));
    gateReleaseParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("gateRelease"));
    gateRangeParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("gateRange"));

    dynamicsInParam       = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("dynamicsIn"));
    dynamicsBeforeEqParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("dynamicsBeforeEq"));

    lfFreqParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("lfFreq"));
    lfGainParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("lfGain"));
    lfBellParam  = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("lfBell"));

    lmfFreqParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("lmfFreq"));
    lmfGainParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("lmfGain"));
    lmfQParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("lmfQ"));

    hmfFreqParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("hmfFreq"));
    hmfGainParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("hmfGain"));
    hmfQParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("hmfQ"));

    hfFreqParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("hfFreq"));
    hfGainParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("hfGain"));
    hfBellParam  = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("hfBell"));

    eqInParam       = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("eqIn"));
    outputGainParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("outputGain"));

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsChannelStripAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto freqParam = [] (const juce::String& id, const juce::String& name, float lo, float hi, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (lo, hi, 0.01f, 0.3f), def,
            juce::AudioParameterFloatAttributes().withLabel ("Hz"));
    };

    params.push_back (freqParam ("hpfFreq", "HPF Freq", 20.0f, 500.0f, 20.0f));
    params.push_back (freqParam ("lpfFreq", "LPF Freq", 3000.0f, 22000.0f, 22000.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("filterSplit", "Filter Split", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("filtersIn", "Filters In", true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "compThreshold", "Comp Threshold", juce::NormalisableRange<float> (-30.0f, 0.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "compRatio", "Comp Ratio", juce::NormalisableRange<float> (1.0f, 10.0f, 0.01f), 2.0f,
        juce::AudioParameterFloatAttributes().withLabel (":1")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "compAttack", "Comp Attack", juce::NormalisableRange<float> (0.1f, 100.0f, 0.01f, 0.4f), 10.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "compRelease", "Comp Release", juce::NormalisableRange<float> (10.0f, 1200.0f, 0.01f, 0.4f), 150.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "compMakeup", "Comp Makeup", juce::NormalisableRange<float> (-12.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "gateThreshold", "Gate Threshold", juce::NormalisableRange<float> (-80.0f, 0.0f, 0.01f), -80.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "gateRatio", "Gate Ratio", juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f), 1.0f,
        juce::AudioParameterFloatAttributes().withLabel (":1")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "gateAttack", "Gate Attack", juce::NormalisableRange<float> (0.1f, 100.0f, 0.01f, 0.4f), 1.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "gateRelease", "Gate Release", juce::NormalisableRange<float> (10.0f, 1200.0f, 0.01f, 0.4f), 150.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "gateRange", "Gate Range", juce::NormalisableRange<float> (-80.0f, 0.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterBool> ("dynamicsIn", "Dynamics In", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("dynamicsBeforeEq", "Dynamics Before EQ", true));

    params.push_back (freqParam ("lfFreq", "LF Freq", 30.0f, 450.0f, 80.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lfGain", "LF Gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("lfBell", "LF Bell", false));

    params.push_back (freqParam ("lmfFreq", "LMF Freq", 200.0f, 2500.0f, 600.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lmfGain", "LMF Gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lmfQ", "LMF Q", juce::NormalisableRange<float> (0.3f, 3.0f, 0.001f, 0.6f), 1.0f));

    params.push_back (freqParam ("hmfFreq", "HMF Freq", 600.0f, 7000.0f, 3000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "hmfGain", "HMF Gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "hmfQ", "HMF Q", juce::NormalisableRange<float> (0.3f, 3.0f, 0.001f, 0.6f), 1.0f));

    params.push_back (freqParam ("hfFreq", "HF Freq", 1500.0f, 16000.0f, 8000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "hfGain", "HF Gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("hfBell", "HF Bell", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> ("eqIn", "EQ In", true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "outputGain", "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return { params.begin(), params.end() };
}

//==============================================================================
void MentalsChannelStripAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    for (auto& s : hpfState) s.reset();
    for (auto& s : lpfState) s.reset();
    for (auto& s : lfState)  s.reset();
    for (auto& s : lmfState) s.reset();
    for (auto& s : hmfState) s.reset();
    for (auto& s : hfState)  s.reset();

    constexpr double rampSeconds = 0.02;
    smoothedHpfFreq.reset (sampleRate, rampSeconds); smoothedHpfFreq.setCurrentAndTargetValue (hpfFreqParam->get());
    smoothedLpfFreq.reset (sampleRate, rampSeconds); smoothedLpfFreq.setCurrentAndTargetValue (lpfFreqParam->get());
    smoothedLfFreq.reset  (sampleRate, rampSeconds); smoothedLfFreq.setCurrentAndTargetValue  (lfFreqParam->get());
    smoothedLfGain.reset  (sampleRate, rampSeconds); smoothedLfGain.setCurrentAndTargetValue  (lfGainParam->get());
    smoothedLmfFreq.reset (sampleRate, rampSeconds); smoothedLmfFreq.setCurrentAndTargetValue (lmfFreqParam->get());
    smoothedLmfGain.reset (sampleRate, rampSeconds); smoothedLmfGain.setCurrentAndTargetValue (lmfGainParam->get());
    smoothedLmfQ.reset    (sampleRate, rampSeconds); smoothedLmfQ.setCurrentAndTargetValue    (lmfQParam->get());
    smoothedHmfFreq.reset (sampleRate, rampSeconds); smoothedHmfFreq.setCurrentAndTargetValue (hmfFreqParam->get());
    smoothedHmfGain.reset (sampleRate, rampSeconds); smoothedHmfGain.setCurrentAndTargetValue (hmfGainParam->get());
    smoothedHmfQ.reset    (sampleRate, rampSeconds); smoothedHmfQ.setCurrentAndTargetValue    (hmfQParam->get());
    smoothedHfFreq.reset  (sampleRate, rampSeconds); smoothedHfFreq.setCurrentAndTargetValue  (hfFreqParam->get());
    smoothedHfGain.reset  (sampleRate, rampSeconds); smoothedHfGain.setCurrentAndTargetValue  (hfGainParam->get());

    compEnvelopeFollower.prepare (sampleRate);
    compEnvelopeFollower.setAttackRelease (compAttackParam->get(), compReleaseParam->get());
    compEnvelopeFollower.reset();

    gateEnvelopeFollower.prepare (sampleRate);
    gateEnvelopeFollower.setAttackRelease (gateAttackParam->get(), gateReleaseParam->get());
    gateEnvelopeFollower.reset();

    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
    currentCompGainReductionDb = 0.0f;
    currentGateGainReductionDb = 0.0f;
}

bool MentalsChannelStripAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void MentalsChannelStripAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int numChannels = juce::jmin (buffer.getNumChannels(), maxChannels);
    const int numSamples  = buffer.getNumSamples();

    smoothedHpfFreq.setTargetValue (hpfFreqParam->get());
    smoothedLpfFreq.setTargetValue (lpfFreqParam->get());
    smoothedLfFreq.setTargetValue  (lfFreqParam->get());
    smoothedLfGain.setTargetValue  (lfGainParam->get());
    smoothedLmfFreq.setTargetValue (lmfFreqParam->get());
    smoothedLmfGain.setTargetValue (lmfGainParam->get());
    smoothedLmfQ.setTargetValue    (lmfQParam->get());
    smoothedHmfFreq.setTargetValue (hmfFreqParam->get());
    smoothedHmfGain.setTargetValue (hmfGainParam->get());
    smoothedHmfQ.setTargetValue    (hmfQParam->get());
    smoothedHfFreq.setTargetValue  (hfFreqParam->get());
    smoothedHfGain.setTargetValue  (hfGainParam->get());

    compEnvelopeFollower.setAttackRelease (compAttackParam->get(), compReleaseParam->get());
    gateEnvelopeFollower.setAttackRelease (gateAttackParam->get(), gateReleaseParam->get());

    const bool filtersIn    = filtersInParam->get();
    const bool filterSplit  = filterSplitParam->get();
    const bool dynamicsIn   = dynamicsInParam->get();
    const bool dynBeforeEq  = dynamicsBeforeEqParam->get();
    const bool eqIn         = eqInParam->get();
    const bool lfBell       = lfBellParam->get();
    const bool hfBell       = hfBellParam->get();

    const float compThresholdDb = compThresholdParam->get();
    const float compRatio       = juce::jmax (1.0f, compRatioParam->get());
    const float compMakeupLin   = juce::Decibels::decibelsToGain (compMakeupParam->get());

    const float gateThresholdDb = gateThresholdParam->get();
    const float gateRatio       = juce::jmax (1.0f, gateRatioParam->get());
    const float gateRangeDbMag  = juce::jmax (0.0f, -gateRangeParam->get()); // stored <=0; used here as a positive magnitude

    const float outputGainLin = juce::Decibels::decibelsToGain (outputGainParam->get());

    float blockMinCompGrDb = 0.0f;
    float blockMinGateGrDb = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const float hpfFreq = smoothedHpfFreq.getNextValue();
        const float lpfFreq = smoothedLpfFreq.getNextValue();
        const float lfFreq  = smoothedLfFreq.getNextValue();
        const float lfGain  = smoothedLfGain.getNextValue();
        const float lmfFreq = smoothedLmfFreq.getNextValue();
        const float lmfGain = smoothedLmfGain.getNextValue();
        const float lmfQ    = smoothedLmfQ.getNextValue();
        const float hmfFreq = smoothedHmfFreq.getNextValue();
        const float hmfGain = smoothedHmfGain.getNextValue();
        const float hmfQ    = smoothedHmfQ.getNextValue();
        const float hfFreq  = smoothedHfFreq.getNextValue();
        const float hfGain  = smoothedHfGain.getNextValue();

        const auto hpfC = Coeffs::highPass (currentSampleRate, hpfFreq, 0.7071f);
        const auto lpfC = Coeffs::lowPass  (currentSampleRate, lpfFreq, 0.7071f);
        const auto lfC  = lfBell ? Coeffs::bell (currentSampleRate, lfFreq, 1.0f, lfGain)
                                 : Coeffs::lowShelf (currentSampleRate, lfFreq, shelfQ, lfGain);
        const auto lmfC = Coeffs::bell (currentSampleRate, lmfFreq, lmfQ, lmfGain);
        const auto hmfC = Coeffs::bell (currentSampleRate, hmfFreq, hmfQ, hmfGain);
        const auto hfC  = hfBell ? Coeffs::bell (currentSampleRate, hfFreq, 1.0f, hfGain)
                                 : Coeffs::highShelf (currentSampleRate, hfFreq, shelfQ, hfGain);

        std::array<float, maxChannels> raw {}, filtered {}, mainSample {};

        for (int ch = 0; ch < numChannels; ++ch)
        {
            raw[(size_t) ch] = buffer.getReadPointer (ch)[n];
            float f = hpfState[(size_t) ch].process (raw[(size_t) ch], hpfC);
            f = lpfState[(size_t) ch].process (f, lpfC);
            filtered[(size_t) ch] = f;
        }

        float detectorLevelAbs = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            detectorLevelAbs = juce::jmax (detectorLevelAbs, std::abs (filterSplit ? filtered[(size_t) ch] : raw[(size_t) ch]));

        // ---- Dynamics: one shared detector drives both stages, gated by a
        // single Dyn In switch, exactly the real hardware's one-button
        // dynamics bypass.
        float dynamicsGainLinear = 1.0f;
        if (dynamicsIn)
        {
            const float compEnvelope   = compEnvelopeFollower.process (detectorLevelAbs);
            const float compEnvelopeDb = juce::Decibels::gainToDecibels (compEnvelope, -100.0f);
            const float compOutputDb   = MentalsUI::DynamicsDSP::computeOutputDb (compEnvelopeDb, compThresholdDb, compRatio, compKneeDb);
            const float compGainReductionDb = compOutputDb - compEnvelopeDb;

            const float gateEnvelope   = gateEnvelopeFollower.process (detectorLevelAbs);
            const float gateEnvelopeDb = juce::Decibels::gainToDecibels (gateEnvelope, -100.0f);
            const float gateOutputDb   = MentalsUI::DynamicsDSP::computeExpanderOutputDb (gateEnvelopeDb, gateThresholdDb, gateRatio, gateKneeDb);
            const float gateGainReductionDb = juce::jmax (gateOutputDb - gateEnvelopeDb, -gateRangeDbMag);

            blockMinCompGrDb = juce::jmin (blockMinCompGrDb, compGainReductionDb);
            blockMinGateGrDb = juce::jmin (blockMinGateGrDb, gateGainReductionDb);

            dynamicsGainLinear = juce::Decibels::decibelsToGain (compGainReductionDb) * compMakeupLin
                               * juce::Decibels::decibelsToGain (gateGainReductionDb);
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            mainSample[(size_t) ch] = filtersIn ? filtered[(size_t) ch] : raw[(size_t) ch];

            const auto applyEq = [&] (float x) noexcept
            {
                if (! eqIn)
                    return x;
                float y = x;
                y = lfState[(size_t) ch].process (y, lfC);
                y = lmfState[(size_t) ch].process (y, lmfC);
                y = hmfState[(size_t) ch].process (y, hmfC);
                y = hfState[(size_t) ch].process (y, hfC);
                return y;
            };

            if (dynBeforeEq)
            {
                mainSample[(size_t) ch] *= dynamicsGainLinear;
                mainSample[(size_t) ch] = applyEq (mainSample[(size_t) ch]);
            }
            else
            {
                mainSample[(size_t) ch] = applyEq (mainSample[(size_t) ch]);
                mainSample[(size_t) ch] *= dynamicsGainLinear;
            }

            buffer.getWritePointer (ch)[n] = mainSample[(size_t) ch] * outputGainLin;
        }
    }

    currentCompGainReductionDb.store (blockMinCompGrDb);
    currentGateGainReductionDb.store (blockMinGateGrDb);

    updateOutputLevelMeter (buffer);
}

void MentalsChannelStripAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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

double MentalsChannelStripAudioProcessor::getMagnitudeForFrequency (BandShape shape, double freqHz, double bandFreq, double q,
                                                                     double gainDb, double sampleRate) noexcept
{
    Coeffs c;
    switch (shape)
    {
        case BandShape::Bell:      c = Coeffs::bell      (sampleRate, (float) bandFreq, (float) q, (float) gainDb); break;
        case BandShape::LowShelf:  c = Coeffs::lowShelf   (sampleRate, (float) bandFreq, (float) q, (float) gainDb); break;
        case BandShape::HighShelf: c = Coeffs::highShelf  (sampleRate, (float) bandFreq, (float) q, (float) gainDb); break;
        case BandShape::HighPass:  c = Coeffs::highPass   (sampleRate, (float) bandFreq, (float) q); break;
        case BandShape::LowPass:   c = Coeffs::lowPass    (sampleRate, (float) bandFreq, (float) q); break;
    }
    return c.getMagnitudeForFrequency (freqHz, sampleRate);
}

//==============================================================================
juce::AudioProcessorEditor* MentalsChannelStripAudioProcessor::createEditor()
{
    return new MentalsChannelStripAudioProcessorEditor (*this);
}

void MentalsChannelStripAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsChannelStripAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// Presets: same two-tier, folder-per-category scheme as Mentals Multimode
// EQ -- see that plugin's PluginProcessor.cpp for the full rationale.
//==============================================================================
juce::File MentalsChannelStripAudioProcessor::getPresetsDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Mentals Channel Strip")
                   .getChildFile ("Presets");
    if (! dir.isDirectory())
        dir.createDirectory();
    return dir;
}

juce::StringArray MentalsChannelStripAudioProcessor::getAvailablePresetNames() const
{
    juce::StringArray names;
    for (const auto& file : getPresetsDirectory().findChildFiles (juce::File::findFiles, false, "*.xml"))
        names.add (file.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

std::vector<MentalsChannelStripAudioProcessor::PresetCategory> MentalsChannelStripAudioProcessor::getFactoryPresetCategories() const
{
    std::vector<PresetCategory> categories;

    for (const auto& dir : getPresetsDirectory().findChildFiles (juce::File::findDirectories, false))
    {
        PresetCategory category;
        category.name = dir.getFileName();
        for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
            category.presetNames.add (file.getFileNameWithoutExtension());
        category.presetNames.sort (true);
        categories.push_back (std::move (category));
    }

    static constexpr std::array<const char*, 8> canonicalOrder
        { "DRUMS", "BASS", "GUITARS", "STRINGS", "VOCALS", "KEYS", "SYNTHS", "MASTER BUS" };

    std::sort (categories.begin(), categories.end(), [] (const PresetCategory& a, const PresetCategory& b)
    {
        const auto rank = [] (const juce::String& name) -> int
        {
            for (int i = 0; i < (int) canonicalOrder.size(); ++i)
                if (name == canonicalOrder[(size_t) i])
                    return i;
            return (int) canonicalOrder.size();
        };
        const int rankA = rank (a.name), rankB = rank (b.name);
        return rankA != rankB ? rankA < rankB : a.name < b.name;
    });

    return categories;
}

void MentalsChannelStripAudioProcessor::savePreset (const juce::String& presetName)
{
    if (presetName.isEmpty())
        return;
    if (auto xml = apvts.copyState().createXml())
        xml->writeTo (getPresetsDirectory().getChildFile (presetName + ".xml"));
}

void MentalsChannelStripAudioProcessor::loadPreset (const juce::String& presetName)
{
    auto file = getPresetsDirectory().getChildFile (presetName + ".xml");
    if (! file.existsAsFile())
        return;
    if (auto xml = juce::XmlDocument::parse (file))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// Factory presets: typical channel-strip starting points per source, each
// dialling in whichever of Filters/Dynamics/EQ actually matters for that
// source rather than touching every section -- see getFactoryPresetCategories()
// for how these are grouped into folders. MASTER BUS covers this plugin's
// most iconic single use case (slow-attack, moderate-ratio "glue"
// compression across a mix bus) since it doesn't fit any of the seven
// per-instrument folders.
//==============================================================================
void MentalsChannelStripAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (getPresetsDirectory().getChildFile ("DRUMS").isDirectory())
        return;

    struct Settings
    {
        float hpfFreq = 20.0f, lpfFreq = 22000.0f;
        bool filterSplit = false, filtersIn = true;
        float compThreshold = 0.0f, compRatio = 2.0f, compAttack = 10.0f, compRelease = 150.0f, compMakeup = 0.0f;
        float gateThreshold = -80.0f, gateRatio = 1.0f, gateAttack = 1.0f, gateRelease = 150.0f, gateRange = 0.0f;
        bool dynamicsIn = true, dynamicsBeforeEq = true;
        float lfFreq = 80.0f, lfGain = 0.0f; bool lfBell = false;
        float lmfFreq = 600.0f, lmfGain = 0.0f, lmfQ = 1.0f;
        float hmfFreq = 3000.0f, hmfGain = 0.0f, hmfQ = 1.0f;
        float hfFreq = 8000.0f, hfGain = 0.0f; bool hfBell = false;
        bool eqIn = true;
        float outputGain = 0.0f;
    };

    auto applyPreset = [this] (const juce::String& category, const juce::String& name, std::function<void (Settings&)> configure)
    {
        Settings s;
        configure (s);

        resetToDefault();
        auto setF = [] (juce::AudioParameterFloat* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        auto setB = [] (juce::AudioParameterBool* p, bool v)   { p->setValueNotifyingHost (v ? 1.0f : 0.0f); };

        setF (hpfFreqParam, s.hpfFreq); setF (lpfFreqParam, s.lpfFreq);
        setB (filterSplitParam, s.filterSplit); setB (filtersInParam, s.filtersIn);

        setF (compThresholdParam, s.compThreshold); setF (compRatioParam, s.compRatio);
        setF (compAttackParam, s.compAttack); setF (compReleaseParam, s.compRelease); setF (compMakeupParam, s.compMakeup);

        setF (gateThresholdParam, s.gateThreshold); setF (gateRatioParam, s.gateRatio);
        setF (gateAttackParam, s.gateAttack); setF (gateReleaseParam, s.gateRelease); setF (gateRangeParam, s.gateRange);

        setB (dynamicsInParam, s.dynamicsIn); setB (dynamicsBeforeEqParam, s.dynamicsBeforeEq);

        setF (lfFreqParam, s.lfFreq); setF (lfGainParam, s.lfGain); setB (lfBellParam, s.lfBell);
        setF (lmfFreqParam, s.lmfFreq); setF (lmfGainParam, s.lmfGain); setF (lmfQParam, s.lmfQ);
        setF (hmfFreqParam, s.hmfFreq); setF (hmfGainParam, s.hmfGain); setF (hmfQParam, s.hmfQ);
        setF (hfFreqParam, s.hfFreq); setF (hfGainParam, s.hfGain); setB (hfBellParam, s.hfBell);
        setB (eqInParam, s.eqIn); setF (outputGainParam, s.outputGain);

        const auto file = getPresetsDirectory().getChildFile (category).getChildFile (name + ".xml");
        file.getParentDirectory().createDirectory();
        if (auto xml = apvts.copyState().createXml())
            xml->writeTo (file);
    };

    // ---- VOCALS ---------------------------------------------------------------------
    applyPreset ("VOCALS", "Lead Vocal Bus", [] (Settings& s)
    {
        s.hpfFreq = 90.0f;
        s.compThreshold = -18.0f; s.compRatio = 2.5f; s.compAttack = 15.0f; s.compRelease = 200.0f; s.compMakeup = 3.0f;
        s.lmfFreq = 350.0f; s.lmfGain = -2.0f; s.lmfQ = 1.0f;
        s.hmfFreq = 4000.0f; s.hmfGain = 2.5f; s.hmfQ = 1.0f;
        s.hfFreq = 10000.0f; s.hfGain = 2.0f;
    });

    applyPreset ("VOCALS", "Vocal Punch", [] (Settings& s)
    {
        s.hpfFreq = 100.0f;
        s.compThreshold = -22.0f; s.compRatio = 4.0f; s.compAttack = 3.0f; s.compRelease = 90.0f; s.compMakeup = 4.0f;
        s.gateThreshold = -45.0f; s.gateRatio = 3.0f; s.gateRelease = 120.0f; s.gateRange = -18.0f;
        s.hmfFreq = 3500.0f; s.hmfGain = 3.0f; s.hmfQ = 1.0f;
    });

    // ---- DRUMS ----------------------------------------------------------------------
    applyPreset ("DRUMS", "Kick Punch", [] (Settings& s)
    {
        s.hpfFreq = 35.0f;
        s.compThreshold = -16.0f; s.compRatio = 4.0f; s.compAttack = 3.0f; s.compRelease = 100.0f; s.compMakeup = 3.0f;
        s.lfFreq = 70.0f; s.lfGain = 3.5f;
        s.lmfFreq = 400.0f; s.lmfGain = -3.5f; s.lmfQ = 1.2f;
        s.hmfFreq = 4000.0f; s.hmfGain = 3.0f; s.hmfQ = 1.0f;
    });

    applyPreset ("DRUMS", "Snare Punch", [] (Settings& s)
    {
        s.compThreshold = -18.0f; s.compRatio = 4.0f; s.compAttack = 2.0f; s.compRelease = 90.0f; s.compMakeup = 3.5f;
        s.lmfFreq = 450.0f; s.lmfGain = -2.5f; s.lmfQ = 1.1f;
        s.hmfFreq = 3500.0f; s.hmfGain = 3.5f; s.hmfQ = 1.0f;
        s.hfFreq = 8000.0f; s.hfGain = 2.0f;
    });

    applyPreset ("DRUMS", "Drum Bus Glue", [] (Settings& s)
    {
        s.hpfFreq = 40.0f;
        s.compThreshold = -15.0f; s.compRatio = 4.0f; s.compAttack = 15.0f; s.compRelease = 250.0f; s.compMakeup = 2.5f;
        s.hfFreq = 9000.0f; s.hfGain = 1.5f;
    });

    // ---- BASS -----------------------------------------------------------------------
    applyPreset ("BASS", "Bass DI Glue", [] (Settings& s)
    {
        s.hpfFreq = 30.0f;
        s.compThreshold = -20.0f; s.compRatio = 3.0f; s.compAttack = 10.0f; s.compRelease = 150.0f; s.compMakeup = 3.0f;
        s.lfFreq = 90.0f; s.lfGain = 2.5f;
        s.lmfFreq = 300.0f; s.lmfGain = -3.0f; s.lmfQ = 1.2f;
        s.hmfFreq = 900.0f; s.hmfGain = 1.5f; s.hmfQ = 1.0f;
    });

    // ---- GUITARS --------------------------------------------------------------------
    applyPreset ("GUITARS", "Guitar Bus", [] (Settings& s)
    {
        s.hpfFreq = 90.0f;
        s.compThreshold = -18.0f; s.compRatio = 2.5f; s.compAttack = 12.0f; s.compRelease = 180.0f; s.compMakeup = 2.0f;
        s.lmfFreq = 400.0f; s.lmfGain = -2.0f; s.lmfQ = 1.0f;
        s.hmfFreq = 3000.0f; s.hmfGain = 2.0f; s.hmfQ = 1.0f;
    });

    // ---- KEYS -------------------------------------------------------------------------
    applyPreset ("KEYS", "Piano Bus", [] (Settings& s)
    {
        s.hpfFreq = 40.0f;
        s.compThreshold = -20.0f; s.compRatio = 2.0f; s.compAttack = 15.0f; s.compRelease = 200.0f; s.compMakeup = 2.0f;
        s.lmfFreq = 300.0f; s.lmfGain = -1.5f; s.lmfQ = 1.0f;
        s.hfFreq = 10000.0f; s.hfGain = 1.5f;
    });

    // ---- SYNTHS -------------------------------------------------------------------------
    applyPreset ("SYNTHS", "Synth Bus", [] (Settings& s)
    {
        s.hpfFreq = 60.0f;
        s.compThreshold = -18.0f; s.compRatio = 2.5f; s.compAttack = 12.0f; s.compRelease = 180.0f; s.compMakeup = 2.0f;
        s.hmfFreq = 2500.0f; s.hmfGain = 2.0f; s.hmfQ = 1.0f;
        s.hfFreq = 12000.0f; s.hfGain = 2.0f;
    });

    // ---- STRINGS ------------------------------------------------------------------------
    applyPreset ("STRINGS", "Strings Bus", [] (Settings& s)
    {
        s.hpfFreq = 90.0f;
        s.compThreshold = -20.0f; s.compRatio = 2.0f; s.compAttack = 20.0f; s.compRelease = 250.0f; s.compMakeup = 1.5f;
        s.hmfFreq = 3500.0f; s.hmfGain = 1.5f; s.hmfQ = 1.0f;
        s.hfFreq = 10000.0f; s.hfGain = 2.0f;
    });

    // ---- MASTER BUS: this plugin's signature use case, classic slow-attack,
    // moderate-ratio "glue" bus compression, applied with barely any EQ.
    applyPreset ("MASTER BUS", "Glue Light", [] (Settings& s)
    {
        s.compThreshold = -10.0f; s.compRatio = 2.0f; s.compAttack = 30.0f; s.compRelease = 300.0f; s.compMakeup = 1.0f;
    });

    applyPreset ("MASTER BUS", "Glue Medium", [] (Settings& s)
    {
        s.compThreshold = -14.0f; s.compRatio = 4.0f; s.compAttack = 20.0f; s.compRelease = 250.0f; s.compMakeup = 2.5f;
        s.hfFreq = 12000.0f; s.hfGain = 1.0f;
    });

    resetToDefault();
}
