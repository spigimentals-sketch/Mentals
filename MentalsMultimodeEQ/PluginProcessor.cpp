#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Spreads the default per-band frequencies roughly logarithmically so a
    // freshly-loaded instance already looks like a usable multiband EQ.
    float defaultFreqForBand (int bandIndex)
    {
        static constexpr std::array<float, MultiModeEQAudioProcessor::numBands> defaults
            { 60.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 12000.0f, 16000.0f };
        return defaults[(size_t) juce::jlimit (0, MultiModeEQAudioProcessor::numBands - 1, bandIndex)];
    }

    // Only High Pass/Low Pass reach steeper slopes by cascading identical
    // sections; Bell and the shelves are always a single section (see
    // FilterShape's class comment for why shelves don't cascade).
    bool shapeUsesCascade (FilterShape shape)
    {
        return shape == FilterShape::HighPass || shape == FilterShape::LowPass;
    }

    //==========================================================================
    // EqAssistModel feature extraction. Must match extract_features.py's
    // macro_energy_db()/cut_features() exactly (same bin ranges, same
    // power-domain averaging) -- see Models/README.md.
    //==========================================================================
    float macroEnergyDb (const std::vector<float>& spectrumDb, double sampleRate, int fftSize, float loHz, float hiHz)
    {
        const double binHz = sampleRate / (double) fftSize;
        const int numBins = (int) spectrumDb.size();
        const int loBin = juce::jmax (0, (int) (loHz / binHz));
        const int hiBin = juce::jmin (numBins, (int) (hiHz / binHz) + 1);
        if (hiBin <= loBin)
            return -100.0f;

        double sumLinear = 0.0;
        for (int b = loBin; b < hiBin; ++b)
            sumLinear += std::pow (10.0, (double) spectrumDb[(size_t) b] / 10.0);

        return (float) (10.0 * std::log10 (sumLinear / (double) (hiBin - loBin) + 1.0e-12));
    }

    std::array<float, 8> eqAssistCutFeatures (const std::vector<float>& spectrumDb, double sampleRate, int fftSize)
    {
        const float overall = macroEnergyDb (spectrumDb, sampleRate, fftSize, (float) (sampleRate / fftSize), (float) (sampleRate * 0.5));
        const float subBass = macroEnergyDb (spectrumDb, sampleRate, fftSize, 10.0f, 30.0f) - overall;
        const float bass    = macroEnergyDb (spectrumDb, sampleRate, fftSize, 30.0f, 60.0f) - overall;
        const float lowMid  = macroEnergyDb (spectrumDb, sampleRate, fftSize, 150.0f, 400.0f) - overall;
        const float high    = macroEnergyDb (spectrumDb, sampleRate, fftSize, 12000.0f, 16000.0f) - overall;
        const float air     = macroEnergyDb (spectrumDb, sampleRate, fftSize, 16000.0f, 20000.0f) - overall;
        const float lowSlope  = macroEnergyDb (spectrumDb, sampleRate, fftSize, 60.0f, 100.0f)
                               - macroEnergyDb (spectrumDb, sampleRate, fftSize, 10.0f, 30.0f);
        const float highSlope = macroEnergyDb (spectrumDb, sampleRate, fftSize, 18000.0f, 20000.0f)
                               - macroEnergyDb (spectrumDb, sampleRate, fftSize, 12000.0f, 14000.0f);
        return { subBass, bass, lowMid, high, air, overall, lowSlope, highSlope };
    }

    // The 7 macro-bands used by both the spectral-balance heuristic above
    // and (here) the instrument-category classifier/reference curves --
    // must match extract_features_v2.py's MACRO_BANDS exactly.
    constexpr std::array<std::pair<float, float>, 7> categoryMacroBands {{
        { 20.0f, 80.0f }, { 80.0f, 250.0f }, { 250.0f, 800.0f }, { 800.0f, 2500.0f },
        { 2500.0f, 6000.0f }, { 6000.0f, 12000.0f }, { 12000.0f, 20000.0f }
    }};

    // Reference curves: each category's average real-audio macro-band
    // shape (relative to its own overall level), computed once from
    // MUSDB18HQ (see Models/README.md) -- indices match
    // extract_features_v2.py's CATEGORY_NAMES. Instrument-aware target
    // curves use these instead of flattening every track towards its own
    // average, which doesn't distinguish a kick drum's naturally bass-
    // heavy shape from a hi-hat's naturally treble-heavy one.
    constexpr const char* categoryNames[] = { "Vocals", "Drums", "Bass", "Other", "Mixture" };
    constexpr std::array<std::array<float, 7>, 5> categoryReferenceCurves {{
        { -10.394078f,   4.222233f,  12.252428f,   2.100836f,  -5.785967f, -14.819640f, -23.227668f }, // Vocals
        {  16.808410f,  16.871366f,   4.047982f,  -5.258839f,  -5.426491f, -10.048739f, -18.773783f }, // Drums
        {  16.943207f,  19.836208f,  -4.045785f, -25.294087f, -30.295610f, -31.779457f, -31.978904f }, // Bass
        {  -5.832837f,  13.990394f,  12.007932f,   0.731897f, -10.342810f, -26.614864f, -30.820102f }, // Other
        {  12.673898f,  17.695000f,   9.337998f,  -1.730836f, -10.121452f, -20.106730f, -29.008213f }, // Mixture
    }};

    // 7 relative macro-band levels + the window's overall level -- must
    // match extract_features_v2.py's macro_band_profile() exactly.
    std::array<float, 8> eqAssistCategoryFeatures (const std::vector<float>& spectrumDb, double sampleRate, int fftSize)
    {
        const float overall = macroEnergyDb (spectrumDb, sampleRate, fftSize, (float) (sampleRate / fftSize), (float) (sampleRate * 0.5));
        std::array<float, 8> features {};
        for (size_t i = 0; i < categoryMacroBands.size(); ++i)
            features[i] = macroEnergyDb (spectrumDb, sampleRate, fftSize, categoryMacroBands[i].first, categoryMacroBands[i].second) - overall;
        features[7] = overall;
        return features;
    }

    // Absolute-dB profile across EqMixRegistry's 24 log-spaced bands, for
    // publish() -- band edges sit at the geometric midpoint between
    // adjacent centre frequencies (0/Nyquist at the outer edges).
    std::array<float, EqMixRegistry::numBands> computeMixRegistryProfile (const std::vector<float>& spectrumDb, double sampleRate, int fftSize)
    {
        std::array<float, EqMixRegistry::numBands> profile {};
        for (int i = 0; i < EqMixRegistry::numBands; ++i)
        {
            const float centre = EqMixRegistry::bandCentreHz (i);
            const float loEdge = (i == 0) ? 0.0f : std::sqrt (EqMixRegistry::bandCentreHz (i - 1) * centre);
            const float hiEdge = (i == EqMixRegistry::numBands - 1) ? (float) (sampleRate * 0.5)
                                                                     : std::sqrt (centre * EqMixRegistry::bandCentreHz (i + 1));
            profile[(size_t) i] = macroEnergyDb (spectrumDb, sampleRate, fftSize, loEdge, hiEdge);
        }
        return profile;
    }

    //==========================================================================
    // Harmonicity: this plugin's own independent copy of the octave-error-
    // resistant autocorrelation pitch detector (same algorithm as
    // MentalsAutotune/PitchDSP.h's detectPitch() -- kept as a separate copy
    // rather than a shared header, consistent with how each plugin already
    // owns its DSP). Only used by AI Assist's harmonicity feature, never
    // the main EQ signal path.
    //==========================================================================
    bool detectPitchForHarmonicity (const float* windowedSamples, int windowSize, double sampleRate,
                                     float minFreqHz, float maxFreqHz, float& outFreqHz, float& outConfidence) noexcept
    {
        const int minLag = juce::jmax (1, (int) (sampleRate / maxFreqHz));
        const int overlapFloorLag = windowSize / 2;
        const int maxLag = juce::jmin (overlapFloorLag, (int) (sampleRate / minFreqHz));
        if (maxLag <= minLag)
            return false;

        auto scoreAt = [&] (int lag)
        {
            double crossSum = 0.0, energyA = 0.0, energyB = 0.0;
            const int overlap = windowSize - lag;
            for (int i = 0; i < overlap; ++i)
            {
                const float a = windowedSamples[i];
                const float b = windowedSamples[i + lag];
                crossSum += (double) a * (double) b;
                energyA  += (double) a * (double) a;
                energyB  += (double) b * (double) b;
            }
            const double denom = std::sqrt (energyA * energyB);
            return denom > 1.0e-9 ? (float) (crossSum / denom) : 0.0f;
        };

        constexpr int maxLagSpan = 4096;
        const int lagCount = juce::jmin (maxLag - minLag + 1, maxLagSpan);
        std::array<float, maxLagSpan> scores {};
        for (int i = 0; i < lagCount; ++i)
            scores[(size_t) i] = scoreAt (minLag + i);

        constexpr float strongPeakThreshold = 0.6f;
        int bestLag = -1;
        float bestScore = 0.0f;
        for (int i = 0; i < lagCount; ++i)
        {
            const float score = scores[(size_t) i];
            const bool isLocalPeak = (i == 0 || score >= scores[(size_t) (i - 1)])
                                   && (i == lagCount - 1 || score >= scores[(size_t) (i + 1)]);
            if (isLocalPeak && score >= strongPeakThreshold)
            {
                bestLag = minLag + i;
                bestScore = score;
                break;
            }
            if (score > bestScore)
            {
                bestScore = score;
                bestLag = minLag + i;
            }
        }

        if (bestLag < 0 || bestScore < 0.05f)
            return false;

        float interpolatedLag = (float) bestLag;
        if (bestLag > minLag && bestLag < maxLag)
        {
            const float sPrev = scoreAt (bestLag - 1);
            const float sNext = scoreAt (bestLag + 1);
            const float denom = sPrev - 2.0f * bestScore + sNext;
            if (std::abs (denom) > 1.0e-9f)
                interpolatedLag += 0.5f * (sPrev - sNext) / denom;
        }

        outFreqHz = (float) (sampleRate / (double) interpolatedLag);
        outConfidence = bestScore;
        return true;
    }
}

//==============================================================================
MultiModeEQAudioProcessor::MultiModeEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                           .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                           .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (int i = 0; i < numBands; ++i)
    {
        const juce::String prefix = "band" + juce::String (i) + "_";
        auto& band = bands[(size_t) i];

        band.enabledParam     = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter (prefix + "enabled"));
        band.freqParam        = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter (prefix + "freq"));
        band.gainParam        = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter (prefix + "gain"));
        band.qParam           = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter (prefix + "q"));
        band.modeParam        = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (prefix + "mode"));
        band.filterShapeParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (prefix + "filterShape"));
        band.slopeParam       = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (prefix + "slope"));
        band.channelParam     = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (prefix + "channel"));
        band.thresholdParam   = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter (prefix + "threshold"));
        band.ratioParam       = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter (prefix + "ratio"));
        band.attackParam      = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter (prefix + "attack"));
        band.releaseParam     = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter (prefix + "release"));
        band.sidechainParam   = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter (prefix + "sidechain"));
    }

    autoGainParam  = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("autoGain"));
    phaseModeParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("phaseMode"));
    stereoParam    = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("stereo"));

    // Allocated once, here, and never resized again for the processor's
    // lifetime -- see the comment on visualiserFifoCapacity below for why
    // (bandFifoBuffers is read by the editor's message-thread timer while the
    // audio thread writes it, so reallocating it later could race).
    for (auto& buf : bandFifoBuffers)
        buf.assign ((size_t) visualiserFifoCapacity, 0.0f);

    static constexpr int maxExpectedBlockSize = 8192;
    monoScratchBuffer.assign ((size_t) maxExpectedBlockSize, 0.0f);
    for (auto& buf : bandScratchBuffers)
        buf.assign ((size_t) maxExpectedBlockSize, 0.0f);

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MultiModeEQAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int i = 0; i < numBands; ++i)
    {
        const juce::String prefix = "band" + juce::String (i) + "_";
        const juce::String suffix = " " + juce::String (i + 1);

        // All bands start enabled and flat (0dB gain, Bell shape) so the plugin
        // launches with the full band set visible on the graph at zero
        // amplitude, ready to adjust -- rather than just one active band.
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            prefix + "enabled", "Band" + suffix + " Enabled", true));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            prefix + "freq", "Band" + suffix + " Frequency",
            juce::NormalisableRange<float> (20.0f, 20000.0f, 0.01f, 0.3f), defaultFreqForBand (i),
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            prefix + "gain", "Band" + suffix + " Gain",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            prefix + "q", "Band" + suffix + " Q",
            juce::NormalisableRange<float> (0.1f, 10.0f, 0.001f, 0.5f), 1.0f));

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            prefix + "mode", "Band" + suffix + " Mode",
            juce::StringArray { "Parametric", "Dynamic" }, 0));

        // Filter shape only applies in Parametric mode (Dynamic bands always
        // behave as Bell); "High Pass"/"Low Cut" are the same filter, as are
        // "Low Pass"/"High Cut", so each shape's label covers both names.
        // Item order must match the FilterShape enum. The lowest-frequency
        // band defaults to Low Shelf and the highest-frequency band defaults
        // to High Shelf (a sensible starting point for the two edge bands);
        // every other band defaults to Bell. These are just starting values --
        // the user can freely change any band's shape afterwards.
        const int defaultFilterShapeIndex = (i == 0) ? (int) FilterShape::LowShelf
                                           : (i == numBands - 1) ? (int) FilterShape::HighShelf
                                           : (int) FilterShape::Bell;
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            prefix + "filterShape", "Band" + suffix + " Filter Shape",
            juce::StringArray { "Bell", "High Pass (Low Cut)", "Low Pass (High Cut)", "Low Shelf", "High Shelf" },
            defaultFilterShapeIndex));

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            prefix + "slope", "Band" + suffix + " Slope",
            juce::StringArray { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 0));

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            prefix + "channel", "Band" + suffix + " Channel",
            juce::StringArray { "Stereo", "Mid", "Side", "Left", "Right" }, 0));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            prefix + "threshold", "Band" + suffix + " Threshold",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f), -20.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            prefix + "ratio", "Band" + suffix + " Ratio",
            juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f), 2.0f,
            juce::AudioParameterFloatAttributes().withLabel (":1")));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            prefix + "attack", "Band" + suffix + " Attack",
            juce::NormalisableRange<float> (0.1f, 100.0f, 0.01f, 0.4f), 10.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            prefix + "release", "Band" + suffix + " Release",
            juce::NormalisableRange<float> (10.0f, 1000.0f, 0.01f, 0.4f), 100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            prefix + "sidechain", "Band" + suffix + " Use Sidechain", false));
    }

    params.push_back (std::make_unique<juce::AudioParameterBool> ("autoGain", "Auto Gain", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "phaseMode", "Phase Mode", juce::StringArray { "Zero Latency", "Natural Phase" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereo", "Stereo", true));

    return { params.begin(), params.end() };
}

//==============================================================================
void MultiModeEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;

    const int numMainChannels = juce::jmax (1, getMainBusNumOutputChannels());

    for (auto& band : bands)
        band.prepare (sampleRate, numMainChannels);

    // ---- Natural Phase FIR ---------------------------------------------------
    // juce::dsp::FIR::Filter isn't copy-assignable (its HeapBlock member
    // deletes operator=), so vector::assign() can't be used here -- rebuild
    // via reserve()+emplace_back() instead, which only default-constructs new
    // elements in place rather than assigning/copying into existing ones.
    {
        const juce::SpinLock::ScopedLockType lock (naturalPhaseLock);
        naturalPhaseFirFilters.clear();
        naturalPhaseFirFilters.reserve ((size_t) numMainChannels);
        for (int i = 0; i < numMainChannels; ++i)
            naturalPhaseFirFilters.emplace_back();
    }
    naturalPhaseLatencySamples = firLength / 2;
    naturalPhaseFirDirty = true;
    samplesSinceLastFirDesign = 0;
    lastReportedLatencySamples = -1;

    // ---- Auto Gain ------------------------------------------------------------
    autoGainInputRms = autoGainOutputRms = 0.0f;
    autoGainCurrentDb = 0.0f;

    // ---- Spectrum analyser (also feeds EQ Match's "current" curve) ----------
    {
        const juce::SpinLock::ScopedLockType lock (spectrumLock);
        spectrumFifo.assign ((size_t) spectrumFftSize, 0.0f);
        spectrumFftData.assign ((size_t) spectrumFftSize * 2, 0.0f);
        spectrumMagnitudesDb.assign ((size_t) spectrumNumBins, -100.0f);
        spectrumFifoIndex = 0;
    }

    // ---- AI Assist -------------------------------------------------------------
    {
        const juce::SpinLock::ScopedLockType lock (aiAssistLock);
        aiAssistFifo.assign ((size_t) spectrumFftSize, 0.0f);
        aiAssistFftData.assign ((size_t) spectrumFftSize * 2, 0.0f);
        aiAssistCapturedSpectrumDb.assign ((size_t) spectrumNumBins, -100.0f);
        aiAssistFifoIndex = 0;
        aiAssistCapturing = false;
        aiAssistHasCapture = false;
        aiAssistPitchSemitones.clear();
        aiAssistPitchFreqHz = 0.0f;
        aiAssistPitchStability = 0.0f;
    }

    mixRegistryPublishScratch.assign ((size_t) spectrumNumBins, -100.0f);
    samplesSinceLastMixRegistryPublish = 0;

    // ---- Split-band visualiser filters (unchanged from before) ---------------
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    const juce::dsp::ProcessSpec visualiserSpec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    {
        const juce::SpinLock::ScopedLockType lock (bandVisualiserFilterLock);

        bandVisualiserFilters[0].coefficients = Coeffs::makeLowPass  (sampleRate, 80.0f);
        bandVisualiserFilters[1].coefficients = Coeffs::makeBandPass (sampleRate, 250.0f,  1.0f);
        bandVisualiserFilters[2].coefficients = Coeffs::makeBandPass (sampleRate, 1000.0f, 1.0f);
        bandVisualiserFilters[3].coefficients = Coeffs::makeBandPass (sampleRate, 4000.0f, 1.0f);
        bandVisualiserFilters[4].coefficients = Coeffs::makeHighPass (sampleRate, 9000.0f);

        for (auto& f : bandVisualiserFilters)
        {
            f.prepare (visualiserSpec);
            f.reset();
        }
    }

    for (auto& fifo : bandFifos)
        fifo.reset();
}

bool MultiModeEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    // Discrete channel counts up to 16 (a 9.1.6-sized layout) are accepted so
    // surround/immersive beds can be processed channel-by-channel. This is
    // NOT certified Dolby Atmos object rendering -- that requires Dolby's own
    // licensed renderer/SDK, which this project has no access to. What's
    // supported here is independent per-channel filtering across up to 16
    // discrete channels.
    const int numMainChannels = mainOut.size();
    if (numMainChannels < 1 || numMainChannels > 16)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sidechain = layouts.getChannelSet (true, 1);
        if (! sidechain.isDisabled() && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================
void MultiModeEQAudioProcessor::processBand (EQBand& band, juce::AudioBuffer<float>& buffer,
                                              const juce::AudioBuffer<float>* sidechainBuffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();
    const auto mode   = band.getMode();
    const auto target = band.getChannelTarget();
    const bool isDynamic = (mode == EQMode::Dynamic);

    // Filter shape only applies in Parametric mode; Dynamic bands always
    // behave as Bell (a dynamic High/Low Pass doesn't have an obvious
    // sensible meaning, so it's deliberately out of scope). Steeper slopes
    // for High Pass/Low Pass are reached by cascading numStages identical
    // biquad sections in series.
    const auto shape = isDynamic ? FilterShape::Bell : band.getFilterShape();
    const int numStages = shapeUsesCascade (shape) ? band.getSlopeStages() : 1;
    band.numActiveFilterStages = numStages;

    band.smoothedFreq.setTargetValue (band.freqParam->get());
    band.smoothedGain.setTargetValue (band.gainParam->get());
    band.smoothedQ.setTargetValue (band.qParam->get());
    band.smoothedThreshold.setTargetValue (band.thresholdParam->get());
    band.smoothedRatio.setTargetValue (band.ratioParam->get());
    band.envelopeFollower.setAttackRelease (band.attackParam->get(), band.releaseParam->get());

    const bool useSidechain = isDynamic && band.usesSidechain()
                               && sidechainBuffer != nullptr && sidechainBuffer->getNumChannels() > 0;

    // Runs one sample through however many cascaded stages this shape/slope
    // needs (1 for Bell, 1-4 for High/Low Pass).
    auto applyCascade = [&] (FilterChain& chain, float x, const BiquadCoefficients& c) -> float
    {
        for (int s = 0; s < numStages; ++s)
            x = chain.stages[(size_t) s].processSample (x, c);
        return x;
    };

    BiquadCoefficients blockStartCoefficients = band.previousCoefficients;

    // Extra-fast (5ms) smoothing applied to the actual computed
    // coefficients, on top of the slower (20ms) SmoothedValue ramps on
    // freq/gain/Q/threshold/ratio above: those glide the numeric
    // PARAMETERS smoothly, but changing Filter Shape or Mode (Dynamic
    // forces Bell) swaps which coefficient FORMULA is used altogether --
    // an instantaneous jump to a very different coefficient set,
    // independent of any parameter smoothing, which can click/pop
    // (especially for a resonant High Pass/Low Pass) because the filter's
    // existing internal state suddenly gets driven by coefficients it
    // wasn't built up under. Blending the coefficients themselves over a
    // short, effectively-inaudible time constant catches that case without
    // perceptibly slowing down ordinary parameter moves.
    constexpr float coefficientSmoothingMs = 5.0f;
    const float coefficientSmoothingCoeff = std::exp (-1.0f / (0.001f * coefficientSmoothingMs * (float) currentSampleRate));

    for (int n = 0; n < numSamples; ++n)
    {
        // These smoothers must each advance exactly once per sample regardless
        // of how many channels this band actually touches, so they live in one
        // outer per-sample loop shared by every ChannelTarget case below.
        const float freq = band.smoothedFreq.getNextValue();
        const float q    = band.smoothedQ.getNextValue();
        float gain       = band.smoothedGain.getNextValue();
        const float thresholdDb = band.smoothedThreshold.getNextValue();
        const float ratio       = juce::jmax (1.0f, band.smoothedRatio.getNextValue());

        if (isDynamic)
        {
            float levelAbs = 0.0f;

            if (useSidechain)
            {
                for (int ch = 0; ch < sidechainBuffer->getNumChannels(); ++ch)
                    levelAbs = juce::jmax (levelAbs, std::abs (sidechainBuffer->getReadPointer (ch)[n]));
            }
            else
            {
                switch (target)
                {
                    case ChannelTarget::Left:
                        levelAbs = numChannels > 0 ? std::abs (buffer.getReadPointer (0)[n]) : 0.0f;
                        break;
                    case ChannelTarget::Right:
                        levelAbs = numChannels > 1 ? std::abs (buffer.getReadPointer (1)[n]) : 0.0f;
                        break;
                    case ChannelTarget::Mid:
                    case ChannelTarget::Side:
                        if (numChannels > 1)
                        {
                            const float l = buffer.getReadPointer (0)[n];
                            const float r = buffer.getReadPointer (1)[n];
                            levelAbs = target == ChannelTarget::Mid ? std::abs (0.5f * (l + r))
                                                                     : std::abs (0.5f * (l - r));
                        }
                        break;
                    case ChannelTarget::StereoOrAll:
                    default:
                        for (int ch = 0; ch < numChannels; ++ch)
                            levelAbs = juce::jmax (levelAbs, std::abs (buffer.getReadPointer (ch)[n]));
                        break;
                }
            }

            const float envelopeLevel = band.envelopeFollower.process (levelAbs);
            const float envelopeDb    = juce::Decibels::gainToDecibels (envelopeLevel, -100.0f);

            if (envelopeDb > thresholdDb)
            {
                const float excessDb        = envelopeDb - thresholdDb;
                const float gainReductionDb = excessDb * (1.0f - 1.0f / ratio);
                gain -= gainReductionDb;
            }
        }

        BiquadCoefficients targetCoefficients;
        switch (shape)
        {
            case FilterShape::HighPass:  targetCoefficients = BiquadCoefficients::makeHighPass  (currentSampleRate, freq, q); break;
            case FilterShape::LowPass:   targetCoefficients = BiquadCoefficients::makeLowPass   (currentSampleRate, freq, q); break;
            case FilterShape::LowShelf:  targetCoefficients = BiquadCoefficients::makeLowShelf  (currentSampleRate, freq, q, gain); break;
            case FilterShape::HighShelf: targetCoefficients = BiquadCoefficients::makeHighShelf (currentSampleRate, freq, q, gain); break;
            case FilterShape::Bell:
            default:                    targetCoefficients = BiquadCoefficients::makePeaking   (currentSampleRate, freq, q, gain); break;
        }

        const BiquadCoefficients activeCoefficients = BiquadCoefficients::lerp (blockStartCoefficients, targetCoefficients, 1.0f - coefficientSmoothingCoeff);
        blockStartCoefficients = activeCoefficients;

        switch (target)
        {
            case ChannelTarget::Left:
                if (numChannels > 0)
                {
                    auto* data = buffer.getWritePointer (0);
                    data[n] = applyCascade (band.filtersPerChannel[0], data[n], activeCoefficients);
                }
                break;

            case ChannelTarget::Right:
                if (numChannels > 1)
                {
                    auto* data = buffer.getWritePointer (1);
                    data[n] = applyCascade (band.filtersPerChannel[0], data[n], activeCoefficients);
                }
                break;

            case ChannelTarget::Mid:
            case ChannelTarget::Side:
                if (numChannels > 1)
                {
                    auto* l = buffer.getWritePointer (0);
                    auto* r = buffer.getWritePointer (1);
                    const float mid  = 0.5f * (l[n] + r[n]);
                    const float side = 0.5f * (l[n] - r[n]);

                    if (target == ChannelTarget::Mid)
                    {
                        const float filteredMid = applyCascade (band.filtersPerChannel[0], mid, activeCoefficients);
                        l[n] = filteredMid + side;
                        r[n] = filteredMid - side;
                    }
                    else
                    {
                        const float filteredSide = applyCascade (band.filtersPerChannel[0], side, activeCoefficients);
                        l[n] = mid + filteredSide;
                        r[n] = mid - filteredSide;
                    }
                }
                break;

            case ChannelTarget::StereoOrAll:
            default:
                for (int ch = 0; ch < numChannels && ch < (int) band.filtersPerChannel.size(); ++ch)
                {
                    auto* data = buffer.getWritePointer (ch);
                    data[n] = applyCascade (band.filtersPerChannel[(size_t) ch], data[n], activeCoefficients);
                }
                break;
        }
    }

    band.previousCoefficients = blockStartCoefficients;
}

void MultiModeEQAudioProcessor::updateBandCoefficientsOnly (EQBand& band)
{
    const bool isDynamic = (band.getMode() == EQMode::Dynamic);
    const auto shape = isDynamic ? FilterShape::Bell : band.getFilterShape();

    band.numActiveFilterStages = shapeUsesCascade (shape) ? band.getSlopeStages() : 1;

    const float freq = band.freqParam->get();
    const float q     = band.qParam->get();

    switch (shape)
    {
        case FilterShape::HighPass:
            band.previousCoefficients = BiquadCoefficients::makeHighPass (currentSampleRate, freq, q);
            break;
        case FilterShape::LowPass:
            band.previousCoefficients = BiquadCoefficients::makeLowPass (currentSampleRate, freq, q);
            break;
        case FilterShape::LowShelf:
            band.previousCoefficients = BiquadCoefficients::makeLowShelf (currentSampleRate, freq, q, band.gainParam->get());
            break;
        case FilterShape::HighShelf:
            band.previousCoefficients = BiquadCoefficients::makeHighShelf (currentSampleRate, freq, q, band.gainParam->get());
            break;
        case FilterShape::Bell:
        default:
            band.previousCoefficients = BiquadCoefficients::makePeaking (currentSampleRate, freq, q, band.gainParam->get());
            break;
    }
}

//==============================================================================
void MultiModeEQAudioProcessor::updateNaturalPhaseFirIfNeeded()
{
    if (! naturalPhaseFirDirty.exchange (false))
        return;

    // ---- Frequency-sampling linear-phase FIR design --------------------------
    // Combine the magnitude response of every enabled, non-dynamic,
    // StereoOrAll-target band (biquads in series multiply in the frequency
    // domain), sample it across 0..Nyquist, mirror it for the negative
    // frequencies (the spectrum is zero-phase, i.e. purely real and even, so
    // this just reuses the same magnitude value), inverse-FFT to get a
    // zero-phase impulse response, circularly shift by half the kernel length
    // to make it causal, and taper with a Hann window. Dynamic bands and
    // Mid/Side/Left/Right-targeted bands are deliberately excluded and always
    // run through their own minimum-phase IIR regardless of phase mode -- a
    // fully linear-phase dynamic/M-S engine is out of scope here.
    std::vector<float> fftData ((size_t) firLength * 2, 0.0f); // interleaved real/imag, JUCE Complex<float> layout

    for (int bin = 0; bin <= firLength / 2; ++bin)
    {
        const double freqHz = (double) bin * currentSampleRate / (double) firLength;
        double magnitude = 1.0;

        for (auto& band : bands)
        {
            if (! band.isEnabled() || band.getMode() == EQMode::Dynamic
                || band.getChannelTarget() != ChannelTarget::StereoOrAll)
                continue;

            magnitude *= band.getMagnitudeForFrequency (freqHz, currentSampleRate);
        }

        fftData[(size_t) bin * 2] = (float) magnitude;
        if (bin > 0 && bin < firLength / 2)
            fftData[(size_t) (firLength - bin) * 2] = (float) magnitude;
    }

    firDesignFft.perform (reinterpret_cast<const juce::dsp::Complex<float>*> (fftData.data()),
                          reinterpret_cast<juce::dsp::Complex<float>*> (fftData.data()), true);

    std::vector<float> kernel ((size_t) firLength);
    for (int i = 0; i < firLength; ++i)
    {
        const int shifted = (i + firLength / 2) % firLength;
        const float window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (firLength - 1));
        kernel[(size_t) i] = fftData[(size_t) shifted * 2] * window;
    }

    juce::dsp::FIR::Coefficients<float>::Ptr newCoefficients (
        new juce::dsp::FIR::Coefficients<float> (kernel.data(), (size_t) firLength));

    const juce::SpinLock::ScopedLockType lock (naturalPhaseLock);
    firCoefficients = newCoefficients;
    for (auto& f : naturalPhaseFirFilters)
    {
        f.coefficients = firCoefficients;
        f.reset();
    }
}

void MultiModeEQAudioProcessor::applyNaturalPhaseFir (juce::AudioBuffer<float>& buffer)
{
    const juce::SpinLock::ScopedTryLockType lock (naturalPhaseLock);
    if (! lock.isLocked())
        return; // kernel is being rebuilt; skip this block's linear-phase shaping rather than block the audio thread

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels && ch < (int) naturalPhaseFirFilters.size(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& filter = naturalPhaseFirFilters[(size_t) ch];
        for (int n = 0; n < numSamples; ++n)
            data[n] = filter.processSample (data[n]);
    }
}

//==============================================================================
void MultiModeEQAudioProcessor::updateAutoGain (const juce::AudioBuffer<float>& dryBuffer, juce::AudioBuffer<float>& wetBuffer)
{
    const int numSamples = wetBuffer.getNumSamples();
    if (numSamples <= 0)
        return;

    auto rmsOf = [] (const juce::AudioBuffer<float>& b)
    {
        double sumSquares = 0.0;
        const int numCh = b.getNumChannels();
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float rms = b.getRMSLevel (ch, 0, b.getNumSamples());
            sumSquares += (double) rms * (double) rms;
        }
        return numCh > 0 ? (float) std::sqrt (sumSquares / numCh) : 0.0f;
    };

    const float inputRms  = rmsOf (dryBuffer);
    const float outputRms = rmsOf (wetBuffer);

    // Slow one-pole smoothing towards the instantaneous ratio, so the makeup
    // gain doesn't jump abruptly block-to-block.
    constexpr float smoothingCoeff = 0.995f;
    autoGainInputRms  = smoothingCoeff * autoGainInputRms  + (1.0f - smoothingCoeff) * inputRms;
    autoGainOutputRms = smoothingCoeff * autoGainOutputRms + (1.0f - smoothingCoeff) * outputRms;

    if (autoGainOutputRms > 1.0e-6f && autoGainInputRms > 1.0e-6f)
    {
        const float targetDb  = juce::Decibels::gainToDecibels (autoGainInputRms / autoGainOutputRms, -24.0f);
        const float clampedDb = juce::jlimit (-24.0f, 24.0f, targetDb);
        autoGainCurrentDb = smoothingCoeff * autoGainCurrentDb + (1.0f - smoothingCoeff) * clampedDb;
    }

    wetBuffer.applyGain (juce::Decibels::decibelsToGain (autoGainCurrentDb));
}

//==============================================================================
void MultiModeEQAudioProcessor::updateSpectrumAnalyser (const juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int n = 0; n < numSamples; ++n)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sum += buffer.getReadPointer (ch)[n];

        if (spectrumFifoIndex >= (int) spectrumFifo.size())
            break; // guards against a prepareToPlay resize race; see spectrumLock usage below

        spectrumFifo[(size_t) spectrumFifoIndex++] = numChannels > 0 ? sum / (float) numChannels : 0.0f;

        if (spectrumFifoIndex == spectrumFftSize)
        {
            spectrumFifoIndex = 0;

            const juce::SpinLock::ScopedLockType lock (spectrumLock);

            for (int i = 0; i < spectrumFftSize; ++i)
            {
                const float window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (spectrumFftSize - 1));
                spectrumFftData[(size_t) i] = spectrumFifo[(size_t) i] * window;
            }

            spectrumFft.performFrequencyOnlyForwardTransform (spectrumFftData.data(), true);

            for (int bin = 0; bin < spectrumNumBins; ++bin)
            {
                const float magnitude = spectrumFftData[(size_t) bin] / (float) spectrumFftSize;
                const float db = juce::Decibels::gainToDecibels (magnitude, -100.0f);
                spectrumMagnitudesDb[(size_t) bin] = spectrumMagnitudesDb[(size_t) bin] * 0.7f + db * 0.3f;
            }

            if (eqMatchCapturing && eqMatchCurrentSpectrumDb.size() == (size_t) spectrumNumBins)
            {
                for (int bin = 0; bin < spectrumNumBins; ++bin)
                    eqMatchCurrentSpectrumDb[(size_t) bin] += spectrumMagnitudesDb[(size_t) bin];

                if (++eqMatchCaptureBlocks >= 40) // ~a few seconds' worth of 2048-sample blocks
                {
                    for (auto& v : eqMatchCurrentSpectrumDb)
                        v /= (float) eqMatchCaptureBlocks;
                    eqMatchCapturing = false;
                    eqMatchCurrentReady = true;
                }
            }
        }
    }
}

void MultiModeEQAudioProcessor::getSpectrumMagnitudesDb (float* dest) const
{
    const juce::SpinLock::ScopedLockType lock (spectrumLock);
    if (spectrumMagnitudesDb.size() == (size_t) spectrumNumBins)
        std::copy (spectrumMagnitudesDb.begin(), spectrumMagnitudesDb.end(), dest);
    else
        std::fill (dest, dest + spectrumNumBins, -100.0f);
}

//==============================================================================
void MultiModeEQAudioProcessor::beginEqMatchCapture()
{
    const juce::SpinLock::ScopedLockType lock (spectrumLock);
    eqMatchCurrentSpectrumDb.assign ((size_t) spectrumNumBins, 0.0f);
    eqMatchCaptureBlocks = 0;
    eqMatchCurrentReady = false;
    eqMatchCapturing = true;
}

void MultiModeEQAudioProcessor::cancelEqMatch()
{
    const juce::SpinLock::ScopedLockType lock (spectrumLock);
    eqMatchCapturing = false;
}

bool MultiModeEQAudioProcessor::isEqMatchCapturing() const noexcept
{
    const juce::SpinLock::ScopedLockType lock (spectrumLock);
    return eqMatchCapturing;
}

bool MultiModeEQAudioProcessor::isEqMatchCurrentReady() const noexcept
{
    const juce::SpinLock::ScopedLockType lock (spectrumLock);
    return eqMatchCurrentReady;
}

bool MultiModeEQAudioProcessor::hasEqMatchReference() const noexcept
{
    const juce::SpinLock::ScopedLockType lock (spectrumLock);
    return eqMatchHasTarget;
}

bool MultiModeEQAudioProcessor::loadEqMatchReferenceFile (const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    std::vector<float> accumulated ((size_t) spectrumNumBins, 0.0f);
    int numBlocksAveraged = 0;

    std::vector<float> fftWorkspace ((size_t) spectrumFftSize * 2, 0.0f);
    juce::AudioBuffer<float> chunk ((int) juce::jmax (1, (int) reader->numChannels), spectrumFftSize);

    juce::int64 position = 0;
    while (position + spectrumFftSize <= reader->lengthInSamples)
    {
        chunk.clear();
        reader->read (&chunk, 0, spectrumFftSize, position, true, true);

        for (int n = 0; n < spectrumFftSize; ++n)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < chunk.getNumChannels(); ++ch)
                sum += chunk.getReadPointer (ch)[n];

            const float mono = chunk.getNumChannels() > 0 ? sum / (float) chunk.getNumChannels() : 0.0f;
            const float window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) (spectrumFftSize - 1));
            fftWorkspace[(size_t) n] = mono * window;
        }

        eqMatchFft.performFrequencyOnlyForwardTransform (fftWorkspace.data(), true);

        for (int bin = 0; bin < spectrumNumBins; ++bin)
        {
            const float magnitude = fftWorkspace[(size_t) bin] / (float) spectrumFftSize;
            accumulated[(size_t) bin] += juce::Decibels::gainToDecibels (magnitude, -100.0f);
        }

        ++numBlocksAveraged;
        position += spectrumFftSize;
    }

    if (numBlocksAveraged > 0)
    {
        for (auto& v : accumulated)
            v /= (float) numBlocksAveraged;

        const juce::SpinLock::ScopedLockType lock (spectrumLock);
        eqMatchTargetSpectrumDb = std::move (accumulated);
        eqMatchHasTarget = true;
    }

    return numBlocksAveraged > 0;
}

void MultiModeEQAudioProcessor::applyEqMatch()
{
    const juce::SpinLock::ScopedLockType lock (spectrumLock);
    if (! eqMatchHasTarget || ! eqMatchCurrentReady || eqMatchCurrentSpectrumDb.size() != (size_t) spectrumNumBins)
        return;

    for (auto& band : bands)
    {
        if (! band.isEnabled() || band.getMode() == EQMode::Dynamic)
            continue;

        const double freq = band.freqParam->get();
        const int bin = juce::jlimit (0, spectrumNumBins - 1,
                                       (int) std::round (freq * (double) spectrumFftSize / currentSampleRate));

        const float diffDb    = eqMatchTargetSpectrumDb[(size_t) bin] - eqMatchCurrentSpectrumDb[(size_t) bin];
        const float newGainDb = juce::jlimit (-24.0f, 24.0f, band.gainParam->get() + diffDb);

        band.gainParam->setValueNotifyingHost (band.gainParam->convertTo0to1 (newGainDb));
    }
}

//==============================================================================
// AI Assist
//==============================================================================
void MultiModeEQAudioProcessor::updateAiAssistCapture (const juce::AudioBuffer<float>& buffer)
{
    if (! aiAssistCapturing) // fast unlocked check so idle blocks do no extra work
        return;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int n = 0; n < numSamples; ++n)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sum += buffer.getReadPointer (ch)[n];

        if (aiAssistFifoIndex >= (int) aiAssistFifo.size())
            break;

        aiAssistFifo[(size_t) aiAssistFifoIndex++] = numChannels > 0 ? sum / (float) numChannels : 0.0f;

        if (aiAssistFifoIndex == spectrumFftSize)
        {
            aiAssistFifoIndex = 0;

            for (int i = 0; i < spectrumFftSize; ++i)
            {
                const float window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (spectrumFftSize - 1));
                aiAssistFftData[(size_t) i] = aiAssistFifo[(size_t) i] * window;
            }

            // Pitch detection reads the Hann-windowed time-domain frame,
            // so it must run before the FFT below overwrites aiAssistFftData
            // in place with frequency-domain magnitudes.
            float pitchFreqHz = 0.0f, pitchConfidence = 0.0f;
            const bool pitchFound = detectPitchForHarmonicity (aiAssistFftData.data(), spectrumFftSize, currentSampleRate,
                                                                60.0f, 1500.0f, pitchFreqHz, pitchConfidence);

            aiAssistFft.performFrequencyOnlyForwardTransform (aiAssistFftData.data(), true);

            const juce::SpinLock::ScopedLockType lock (aiAssistLock);

            if (! aiAssistCapturing) // cancelAiAssistAnalysis() may have fired while the FFT above ran
                return;

            if (pitchFound && pitchConfidence >= 0.5f)
                aiAssistPitchSemitones.push_back (12.0f * std::log2 (pitchFreqHz / 440.0f));

            for (int bin = 0; bin < spectrumNumBins; ++bin)
            {
                const float magnitude = aiAssistFftData[(size_t) bin] / (float) spectrumFftSize;
                aiAssistCapturedSpectrumDb[(size_t) bin] += juce::Decibels::gainToDecibels (magnitude, -100.0f);
            }

            if (++aiAssistCaptureBlocks >= 40) // ~2s of 2048-sample blocks, matching EQ Match's capture length
            {
                for (auto& v : aiAssistCapturedSpectrumDb)
                    v /= (float) aiAssistCaptureBlocks;

                // Mirrors extract_features_v2.py's analyse_pitch_stability():
                // need a healthy fraction of confidently-pitched frames to
                // trust a representative pitch at all.
                if ((int) aiAssistPitchSemitones.size() >= juce::jmax (4, (aiAssistCaptureBlocks * 3) / 10))
                {
                    float mean = 0.0f;
                    for (float s : aiAssistPitchSemitones) mean += s;
                    mean /= (float) aiAssistPitchSemitones.size();

                    float variance = 0.0f;
                    for (float s : aiAssistPitchSemitones) variance += (s - mean) * (s - mean);
                    variance /= (float) aiAssistPitchSemitones.size();

                    aiAssistPitchStability = juce::jlimit (0.0f, 1.0f, 1.0f - std::sqrt (variance) / 1.5f);
                    aiAssistPitchFreqHz = 440.0f * std::pow (2.0f, mean / 12.0f);
                }
                else
                {
                    aiAssistPitchStability = 0.0f;
                    aiAssistPitchFreqHz = 0.0f;
                }

                aiAssistCapturing = false;
                aiAssistHasCapture = true;
            }
        }
    }
}

void MultiModeEQAudioProcessor::beginAiAssistAnalysis()
{
    const juce::SpinLock::ScopedLockType lock (aiAssistLock);
    aiAssistCapturedSpectrumDb.assign ((size_t) spectrumNumBins, 0.0f);
    aiAssistFifoIndex = 0;
    aiAssistCaptureBlocks = 0;
    aiAssistHasCapture = false;
    aiAssistCapturing = true;
    aiAssistPitchSemitones.clear();
    aiAssistPitchFreqHz = 0.0f;
    aiAssistPitchStability = 0.0f;
}

void MultiModeEQAudioProcessor::cancelAiAssistAnalysis()
{
    const juce::SpinLock::ScopedLockType lock (aiAssistLock);
    aiAssistCapturing = false;
}

bool MultiModeEQAudioProcessor::isAiAssistCapturing() const noexcept
{
    const juce::SpinLock::ScopedLockType lock (aiAssistLock);
    return aiAssistCapturing;
}

int MultiModeEQAudioProcessor::applyAiAssistSuggestions()
{
    std::vector<float> capturedDb;
    float pitchFreqHz = 0.0f, pitchStability = 0.0f;
    {
        const juce::SpinLock::ScopedLockType lock (aiAssistLock);
        if (! aiAssistHasCapture)
            return 0;
        capturedDb = aiAssistCapturedSpectrumDb;
        pitchFreqHz = aiAssistPitchFreqHz;
        pitchStability = aiAssistPitchStability;
    }

    // Masking-aware AI Assist: a snapshot of what OTHER currently-active
    // Multimode EQ instances are occupying right now (see EqMixRegistry.h).
    // othersCount == 0 just means no other instance is currently detected
    // (or none exists) -- levelAtFrequency() handles that by reporting
    // -100dB, giving a harmless ~0 masking-pressure feature below.
    const auto maskingAggregate = eqMixRegistry.computeAggregate();

    // ---- Bands available to claim: still at their untouched mid-band
    // default state (Parametric, Bell, ~0dB), regardless of their Enabled
    // toggle. The two bookend bands (index 0 / numBands-1) are never
    // claimed, even if untouched, since they're the designated Low/High
    // Shelf bookends. Any band the user has actually customised (different
    // shape, non-zero gain, Dynamic mode) is left completely alone.
    std::vector<int> availableBandIndices;
    for (int i = 1; i < numBands - 1; ++i)
    {
        auto& band = bands[(size_t) i];
        if (band.getMode() == EQMode::Parametric && band.getFilterShape() == FilterShape::Bell
            && std::abs (band.gainParam->get()) < 0.05f)
            availableBandIndices.push_back (i);
    }

    if (availableBandIndices.empty())
        return 0;

    struct Suggestion
    {
        float freqHz, gainDb, q;
        bool dynamic;
        FilterShape shape;
        float thresholdDb, ratio;
        float severity; // priority only, not applied
    };
    std::vector<Suggestion> suggestions;

    // ---- Spectral balance: split into macro bands, compare each one's
    // average level against the overall average, and suggest a gentle
    // corrective shelf/bell for anything that sticks out by more than 3dB.
    struct MacroBand { float loHz, hiHz; FilterShape shape; bool isEdge; };
    static const std::array<MacroBand, 7> macroBands {{
        { 20.0f,    80.0f,    FilterShape::LowShelf,  true  },
        { 80.0f,    250.0f,   FilterShape::Bell,       false },
        { 250.0f,   800.0f,   FilterShape::Bell,       false },
        { 800.0f,   2500.0f,  FilterShape::Bell,       false },
        { 2500.0f,  6000.0f,  FilterShape::Bell,       false },
        { 6000.0f,  12000.0f, FilterShape::Bell,       false },
        { 12000.0f, 20000.0f, FilterShape::HighShelf,  true  }
    }};

    double overallSumDb = 0.0;
    int overallCount = 0;
    std::array<double, macroBands.size()> macroSumDb {};
    std::array<int, macroBands.size()> macroCount {};

    for (int bin = 1; bin < spectrumNumBins; ++bin)
    {
        const double freq = getSpectrumBinFrequency (bin);
        if (freq < 20.0 || freq > 20000.0)
            continue;

        const float db = capturedDb[(size_t) bin];
        overallSumDb += db;
        ++overallCount;

        for (size_t m = 0; m < macroBands.size(); ++m)
        {
            if (freq >= macroBands[m].loHz && freq < macroBands[m].hiHz)
            {
                macroSumDb[m] += db;
                ++macroCount[m];
                break;
            }
        }
    }

    if (overallCount == 0)
        return 0;

    const float overallAvgDb = (float) (overallSumDb / overallCount);

    // ---- Instrument-aware target: classify the likely source category
    // (see Models/README.md) and use its reference curve as the target
    // shape below, instead of just flattening every track toward its own
    // average -- a kick drum and a hi-hat have very different natural
    // spectra, so "deviation from this track's own average" was a crude
    // one-size-fits-all measure. Falls back to the exact previous
    // behaviour (referenceCurve == nullptr -> targetRelative 0.0f below)
    // if the model isn't available on this machine.
    const auto categoryFeatures = eqAssistCategoryFeatures (capturedDb, currentSampleRate, spectrumFftSize);
    const auto categoryIndex = eqAssistModel.predictCategory (categoryFeatures);
    const std::array<float, 7>* referenceCurve =
        (categoryIndex.has_value() && *categoryIndex >= 0 && *categoryIndex < (int) categoryReferenceCurves.size())
            ? &categoryReferenceCurves[(size_t) *categoryIndex] : nullptr;

    for (size_t m = 0; m < macroBands.size(); ++m)
    {
        if (macroCount[m] == 0)
            continue;

        const float macroAvgDb = (float) (macroSumDb[m] / macroCount[m]);
        const float ownRelativeDb = macroAvgDb - overallAvgDb;
        const float targetRelativeDb = referenceCurve != nullptr ? (*referenceCurve)[m] : 0.0f;
        const float deviation = ownRelativeDb - targetRelativeDb;

        if (std::abs (deviation) < 3.0f)
            continue; // already balanced here (relative to the instrument-aware target, if any)

        const float correctionDb = (deviation > 0.0f ? -1.0f : 1.0f) * juce::jlimit (2.0f, 6.0f, std::abs (deviation) * 0.6f);
        const float centreFreq   = macroBands[m].isEdge
            ? (m == 0 ? macroBands[m].hiHz : macroBands[m].loHz)
            : std::sqrt (macroBands[m].loHz * macroBands[m].hiHz);

        suggestions.push_back ({ centreFreq, correctionDb, macroBands[m].isEdge ? 0.7f : 0.8f,
                                  false, macroBands[m].shape, -20.0f, 2.0f, std::abs (deviation) });
    }

    // ---- Resonances: bins that are local maxima and sit well above their
    // own local (roughly +/-1/3-octave) baseline. Kept candidates within
    // ~12% of each other in frequency are merged so one spectral "hump"
    // doesn't produce several overlapping suggestions.
    struct ResonancePeak { double freqHz; float prominence; int bin, loBin, hiBin; float db, baseline; };
    std::vector<ResonancePeak> resonances;

    for (int bin = 3; bin < spectrumNumBins - 3; ++bin)
    {
        const double freq = getSpectrumBinFrequency (bin);
        if (freq < 40.0 || freq > 16000.0)
            continue; // leave the extreme edges to the shelf bands above

        const float db = capturedDb[(size_t) bin];
        if (db < -55.0f)
            continue;
        if (db < capturedDb[(size_t) bin - 1] || db < capturedDb[(size_t) bin + 1])
            continue; // local maximum only

        int loBin = bin, hiBin = bin;
        while (loBin > 0 && getSpectrumBinFrequency (loBin) > freq * 0.79)
            --loBin;
        while (hiBin < spectrumNumBins - 1 && getSpectrumBinFrequency (hiBin) < freq * 1.26)
            ++hiBin;

        double baselineSum = 0.0;
        int baselineCount = 0;
        for (int b = loBin; b <= hiBin; ++b)
        {
            if (std::abs (b - bin) <= 2)
                continue; // exclude the peak itself and its immediate shoulder
            baselineSum += capturedDb[(size_t) b];
            ++baselineCount;
        }
        if (baselineCount == 0)
            continue;

        const float baseline   = (float) (baselineSum / baselineCount);
        const float prominence = db - baseline;

        if (prominence > 5.0f)
            resonances.push_back ({ freq, prominence, bin, loBin, hiBin, db, baseline });
    }

    std::sort (resonances.begin(), resonances.end(),
               [] (const ResonancePeak& a, const ResonancePeak& b) { return a.prominence > b.prominence; });

    std::vector<ResonancePeak> keptResonances;
    for (auto& r : resonances)
    {
        bool tooClose = false;
        for (auto& k : keptResonances)
        {
            if (std::abs (std::log10 (r.freqHz) - std::log10 (k.freqHz)) < 0.05)
            {
                tooClose = true;
                break;
            }
        }

        if (! tooClose)
        {
            keptResonances.push_back (r);
            if (keptResonances.size() >= 3)
                break;
        }
    }

    // Full-spectrum mean (every bin, unrestricted) -- loudness context for
    // EqAssistModel::predictResonance(), distinct from overallAvgDb above
    // (which only covers 20-20000Hz and feeds the older spectral-balance
    // heuristic). Must match extract_features.py's resonance feature #5
    // (plain np.mean(spectrum_db) over the whole array) exactly.
    const float fullSpectrumMeanDb = (float) (std::accumulate (capturedDb.begin(), capturedDb.end(), 0.0) / (double) capturedDb.size());

    for (auto& r : keptResonances)
    {
        // Tilt: upper-context mean minus lower-context mean within the same
        // +-~1/3-octave window used for baseline above -- must match
        // extract_features.py's resonance_features_and_labels() exactly.
        double loSum = 0.0; int loCount = 0;
        for (int b = r.loBin; b < juce::jmax (r.bin - 2, r.loBin + 1); ++b) { loSum += capturedDb[(size_t) b]; ++loCount; }
        double hiSum = 0.0; int hiCount = 0;
        for (int b = juce::jmin (r.bin + 3, r.hiBin); b <= r.hiBin; ++b) { hiSum += capturedDb[(size_t) b]; ++hiCount; }
        const float loCtx = loCount > 0 ? (float) (loSum / loCount) : r.baseline;
        const float hiCtx = hiCount > 0 ? (float) (hiSum / hiCount) : r.baseline;
        const float tilt = hiCtx - loCtx;

        // Masking pressure: how much OTHER currently-active instances
        // occupy this exact frequency right now, relative to this peak's
        // own level -- positive when something else already dominates
        // here, in which case the trained model learned to cut harder
        // (see Models/README.md). 0 (from levelAtFrequency's -100dB
        // default) when no other instance is currently detected.
        const float othersLevelDb = EqMixRegistry::levelAtFrequency (maskingAggregate, (float) r.freqHz);
        const float maskingPressureDb = juce::jlimit (-24.0f, 24.0f, othersLevelDb - r.db);

        // Harmonicity: how closely this peak aligns with a harmonic of a
        // stable pitch actually detected in this track's own audio during
        // the capture -- high harmonicity damps the suggested cut, since a
        // prominent, harmonically-aligned peak is more likely the note
        // being played than a problem resonance (see Models/README.md).
        // 0 if no stable pitch was found in the capture at all.
        float harmonicity = 0.0f;
        if (pitchFreqHz > 0.0f && pitchStability > 0.0f)
        {
            const int harmonicNumber = juce::jmax (1, (int) std::round (r.freqHz / pitchFreqHz));
            const float centsOff = std::abs (1200.0f * std::log2 ((float) r.freqHz / (pitchFreqHz * (float) harmonicNumber)));
            harmonicity = pitchStability * juce::jmax (0.0f, 1.0f - centsOff / 50.0f);
        }

        const std::array<float, 7> features { std::log10 ((float) r.freqHz), r.prominence, r.db, tilt, fullSpectrumMeanDb,
                                               maskingPressureDb, harmonicity };
        const auto trained = eqAssistModel.predictResonance (features);

        if (trained.has_value())
        {
            // Static Bell cut at the model's learned (gainDb, Q) -- a
            // genuinely trained correction (see Models/README.md), not the
            // fixed Q=5/dynamic-threshold guess this used to hardcode.
            suggestions.push_back ({ (float) r.freqHz, trained->gainDb, trained->q,
                                      false, FilterShape::Bell, -20.0f, 2.0f, r.prominence });
        }
        else
        {
            // Model unavailable on this machine -- fall back to the
            // original dynamic-threshold heuristic rather than dropping
            // the suggestion entirely.
            const float thresholdDb = juce::jlimit (-50.0f, -6.0f, overallAvgDb - 8.0f);
            suggestions.push_back ({ (float) r.freqHz, 0.0f, 5.0f, true, FilterShape::Bell, thresholdDb, 3.0f, r.prominence });
        }
    }

    // ---- Low Cut / High Cut: a genuinely trained decision (see
    // Models/README.md) about whether the source has actual rumble or
    // hiss/harshness needing a real cut, not just a gentle shelf -- the
    // spectral-balance pass above never suggests an actual High Pass/Low
    // Pass, only shelf boosts/cuts on the two bookend bands.
    {
        const auto cutFeatures = eqAssistCutFeatures (capturedDb, currentSampleRate, spectrumFftSize);
        const auto cutSuggestion = eqAssistModel.predictCut (cutFeatures);

        if (cutSuggestion.has_value())
        {
            if (cutSuggestion->needsLowCut)
                suggestions.push_back ({ cutSuggestion->lowCutFreqHz, 0.0f, 0.7f,
                                          false, FilterShape::HighPass, -20.0f, 2.0f, 6.0f });
            if (cutSuggestion->needsHighCut)
                suggestions.push_back ({ cutSuggestion->highCutFreqHz, 0.0f, 0.7f,
                                          false, FilterShape::LowPass, -20.0f, 2.0f, 6.0f });
        }
    }

    if (suggestions.empty())
        return 0;

    std::sort (suggestions.begin(), suggestions.end(),
               [] (const Suggestion& a, const Suggestion& b) { return a.severity > b.severity; });

    const int numToApply = juce::jmin ((int) suggestions.size(), (int) availableBandIndices.size());

    aiAssistLastAppliedBands.clear();

    for (int i = 0; i < numToApply; ++i)
    {
        const int bandIndex = availableBandIndices[(size_t) i];
        auto& band = bands[(size_t) bandIndex];
        const auto& s = suggestions[(size_t) i];

        aiAssistLastAppliedBands.push_back ({ bandIndex, band.isEnabled() });

        setBandEnabled (bandIndex, true);
        band.freqParam->setValueNotifyingHost (band.freqParam->convertTo0to1 (juce::jlimit (20.0f, 20000.0f, s.freqHz)));
        band.qParam->setValueNotifyingHost (band.qParam->convertTo0to1 (s.q));
        band.gainParam->setValueNotifyingHost (band.gainParam->convertTo0to1 (s.gainDb));
        *band.filterShapeParam = (int) s.shape;
        *band.modeParam = s.dynamic ? 1 : 0;

        if (s.dynamic)
        {
            band.thresholdParam->setValueNotifyingHost (band.thresholdParam->convertTo0to1 (s.thresholdDb));
            band.ratioParam->setValueNotifyingHost (band.ratioParam->convertTo0to1 (s.ratio));
        }
    }

    return numToApply;
}

void MultiModeEQAudioProcessor::undoLastAiAssist()
{
    for (auto& touched : aiAssistLastAppliedBands)
    {
        auto& band = bands[(size_t) touched.bandIndex];

        setBandEnabled (touched.bandIndex, touched.wasEnabled);
        band.freqParam->setValueNotifyingHost (band.freqParam->convertTo0to1 (defaultFreqForBand (touched.bandIndex)));
        band.gainParam->setValueNotifyingHost (band.gainParam->convertTo0to1 (0.0f));
        band.qParam->setValueNotifyingHost (band.qParam->convertTo0to1 (1.0f));
        *band.filterShapeParam = (int) FilterShape::Bell;
        *band.modeParam = 0;
        band.thresholdParam->setValueNotifyingHost (band.thresholdParam->convertTo0to1 (-20.0f));
        band.ratioParam->setValueNotifyingHost (band.ratioParam->convertTo0to1 (2.0f));
    }

    aiAssistLastAppliedBands.clear();
}

//==============================================================================
void MultiModeEQAudioProcessor::spectrumGrab (int bandIndex, float freqHz, float gainDb)
{
    if (bandIndex < 0 || bandIndex >= numBands)
        return;

    auto& band = bands[(size_t) bandIndex];
    band.freqParam->setValueNotifyingHost (band.freqParam->convertTo0to1 (juce::jlimit (20.0f, 20000.0f, freqHz)));
    band.gainParam->setValueNotifyingHost (band.gainParam->convertTo0to1 (juce::jlimit (-24.0f, 24.0f, gainDb)));
}

void MultiModeEQAudioProcessor::adjustBandBandwidth (int bandIndex, float wheelDeltaY)
{
    if (bandIndex < 0 || bandIndex >= numBands)
        return;

    auto& band = bands[(size_t) bandIndex];
    const float currentQ = band.qParam->get();

    // ~1 octave of Q change per full scroll "click" (deltaY of 1.0).
    const float factor = std::pow (2.0f, wheelDeltaY);
    const float newQ   = juce::jlimit (0.1f, 10.0f, currentQ * factor);

    band.qParam->setValueNotifyingHost (band.qParam->convertTo0to1 (newQ));
}

void MultiModeEQAudioProcessor::setBandEnabled (int bandIndex, bool shouldBeEnabled)
{
    if (bandIndex < 0 || bandIndex >= numBands)
        return;

    bands[(size_t) bandIndex].enabledParam->setValueNotifyingHost (shouldBeEnabled ? 1.0f : 0.0f);
}

//==============================================================================
void MultiModeEQAudioProcessor::armMidiLearn (const juce::String& paramID)
{
    const juce::ScopedLock lock (midiLearnLock);
    learnArmedParamID = paramID;
}

void MultiModeEQAudioProcessor::clearMidiLearn (const juce::String& paramID)
{
    const juce::ScopedLock lock (midiLearnLock);
    for (auto it = ccToParamId.begin(); it != ccToParamId.end(); )
    {
        if (it->second == paramID) it = ccToParamId.erase (it);
        else ++it;
    }
}

void MultiModeEQAudioProcessor::clearAllMidiLearn()
{
    const juce::ScopedLock lock (midiLearnLock);
    ccToParamId.clear();
    learnArmedParamID.clear();
}

juce::String MultiModeEQAudioProcessor::getMidiLearnArmedParam() const
{
    const juce::ScopedLock lock (midiLearnLock);
    return learnArmedParamID;
}

juce::String MultiModeEQAudioProcessor::getCcMappedParam (int ccNumber) const
{
    const juce::ScopedLock lock (midiLearnLock);
    const auto it = ccToParamId.find (ccNumber);
    return it != ccToParamId.end() ? it->second : juce::String();
}

void MultiModeEQAudioProcessor::processIncomingMidi (juce::MidiBuffer& midi)
{
    if (midi.isEmpty())
        return;

    // A blocking CriticalSection on the audio thread is not ideal real-time
    // practice, but MIDI CC messages are infrequent and the lock is only ever
    // held briefly (by the editor arming/clearing a mapping), so contention
    // is rare -- the same pattern used by many JUCE plugins' MIDI Learn.
    const juce::ScopedLock lock (midiLearnLock);

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (! message.isController())
            continue;

        const int ccNumber = message.getControllerNumber();

        if (learnArmedParamID.isNotEmpty())
        {
            ccToParamId[ccNumber] = learnArmedParamID;
            learnArmedParamID.clear();
            continue;
        }

        const auto it = ccToParamId.find (ccNumber);
        if (it != ccToParamId.end())
            if (auto* param = apvts.getParameter (it->second))
                param->setValueNotifyingHost (message.getControllerValue() / 127.0f);
    }
}

//==============================================================================
void MultiModeEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    processIncomingMidi (midi);

    auto mainBuffer = getBusBuffer (buffer, true, 0);
    auto sidechainBuffer = getBusCount (true) > 1 ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const bool hasSidechain = sidechainBuffer.getNumChannels() > 0;

    // Must run before any band processing below -- AI Assist analyses the
    // true incoming signal, not this plugin's own output.
    updateAiAssistCapture (mainBuffer);

    const int numChannels = mainBuffer.getNumChannels();
    const int numSamples  = mainBuffer.getNumSamples();

    const bool autoGainOn = autoGainParam != nullptr && autoGainParam->get();
    juce::AudioBuffer<float> dryBuffer;
    if (autoGainOn)
        dryBuffer.makeCopyOf (mainBuffer, true);

    const auto phaseMode = phaseModeParam != nullptr
                              ? static_cast<PhaseMode> (phaseModeParam->getIndex())
                              : PhaseMode::ZeroLatency;

    for (auto& band : bands)
    {
        if (! band.isEnabled())
            continue;

        if (phaseMode == PhaseMode::NaturalPhase
            && band.getMode() != EQMode::Dynamic
            && band.getChannelTarget() == ChannelTarget::StereoOrAll)
        {
            // Handled by the shared linear-phase FIR below instead of this
            // band's own IIR chain -- still refresh its stored coefficients
            // so a live shape/freq/gain/Q change (e.g. switching back to
            // Bell) reaches the FIR design and the analyser's drawn curve.
            updateBandCoefficientsOnly (band);
            continue;
        }

        processBand (band, mainBuffer, hasSidechain ? &sidechainBuffer : nullptr);
    }

    if (phaseMode == PhaseMode::NaturalPhase)
    {
        samplesSinceLastFirDesign += numSamples;
        if (samplesSinceLastFirDesign >= (int) currentSampleRate / 10) // redesign ~10x/sec while active, not every block
        {
            samplesSinceLastFirDesign = 0;
            naturalPhaseFirDirty = true;
        }

        updateNaturalPhaseFirIfNeeded();
        applyNaturalPhaseFir (mainBuffer);
    }

    const int desiredLatency = (phaseMode == PhaseMode::NaturalPhase) ? naturalPhaseLatencySamples : 0;
    if (desiredLatency != lastReportedLatencySamples)
    {
        lastReportedLatencySamples = desiredLatency;
        setLatencySamples (desiredLatency);
    }

    if (autoGainOn)
        updateAutoGain (dryBuffer, mainBuffer);

    if (! stereoParam->get() && mainBuffer.getNumChannels() > 1)
    {
        auto* monoL = mainBuffer.getWritePointer (0);
        auto* monoR = mainBuffer.getWritePointer (1);
        for (int n = 0; n < mainBuffer.getNumSamples(); ++n)
        {
            const float avg = 0.5f * (monoL[n] + monoR[n]);
            monoL[n] = avg;
            monoR[n] = avg;
        }
    }

    updateSpectrumAnalyser (mainBuffer);
    updateOutputLevelMeter (mainBuffer);

    // ---- Masking-aware AI Assist: publish this instance's own spectrum --------
    // into the cross-process mix registry periodically (not every block --
    // other instances only need a fresh-ish read, and copying 1024 bins
    // under spectrumLock is cheap but not free).
    samplesSinceLastMixRegistryPublish += numSamples;
    if (samplesSinceLastMixRegistryPublish >= (int) currentSampleRate / 4) // ~4x/sec
    {
        samplesSinceLastMixRegistryPublish = 0;
        bool haveSnapshot = false;
        {
            const juce::SpinLock::ScopedLockType lock (spectrumLock);
            if (spectrumMagnitudesDb.size() == mixRegistryPublishScratch.size())
            {
                std::copy (spectrumMagnitudesDb.begin(), spectrumMagnitudesDb.end(), mixRegistryPublishScratch.begin());
                haveSnapshot = true;
            }
        }
        if (haveSnapshot)
            eqMixRegistry.publish (computeMixRegistryProfile (mixRegistryPublishScratch, currentSampleRate, spectrumFftSize));
    }

    // ---- Feed the split-band oscilloscope ------------------------------------
    // Mono-sum the (already EQ'd) output, then push it through each of the
    // five fixed band filters and queue the results for the editor to draw.
    if ((int) monoScratchBuffer.size() < numSamples)
        monoScratchBuffer.resize ((size_t) numSamples);

    for (int n = 0; n < numSamples; ++n)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sum += mainBuffer.getReadPointer (ch)[n];
        monoScratchBuffer[(size_t) n] = numChannels > 0 ? sum / (float) numChannels : 0.0f;
    }

    const juce::SpinLock::ScopedTryLockType visualiserLock (bandVisualiserFilterLock);
    if (visualiserLock.isLocked())
    {
        for (int band = 0; band < numVisualiserBands; ++band)
        {
            auto& filter  = bandVisualiserFilters[(size_t) band];
            auto& scratch = bandScratchBuffers[(size_t) band];

            if ((int) scratch.size() < numSamples)
                scratch.resize ((size_t) numSamples);

            for (int n = 0; n < numSamples; ++n)
                scratch[(size_t) n] = filter.processSample (monoScratchBuffer[(size_t) n]);

            pushBandVisualiserSamples (band, scratch.data(), numSamples);
        }
    }
}

//==============================================================================
void MultiModeEQAudioProcessor::pushBandVisualiserSamples (int bandIndex, const float* samples, int numSamples) noexcept
{
    auto& fifo = bandFifos[(size_t) bandIndex];
    auto& buf  = bandFifoBuffers[(size_t) bandIndex];

    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    if (size1 > 0) juce::FloatVectorOperations::copy (buf.data() + start1, samples, size1);
    if (size2 > 0) juce::FloatVectorOperations::copy (buf.data() + start2, samples + size1, size2);

    fifo.finishedWrite (size1 + size2);
}

void MultiModeEQAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
{
    float blockPeak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        blockPeak = juce::jmax (blockPeak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    float currentPeak = outputPeakLinear.load();

    if (blockPeak >= currentPeak)
    {
        currentPeak = blockPeak;
    }
    else
    {
        // ~24dB/second release, so the meter falls smoothly rather than
        // snapping straight down to the next (lower) block's peak.
        const double blockSeconds = (double) buffer.getNumSamples() / currentSampleRate;
        const float decayFactor = std::pow (10.0f, -24.0f * (float) blockSeconds / 20.0f);
        currentPeak *= decayFactor;
    }

    outputPeakLinear.store (currentPeak);

    if (blockPeak > 1.0f)
        clipHoldBlocksRemaining.store ((int) juce::jmax (1.0, currentSampleRate * 1.5 / (double) juce::jmax (1, buffer.getNumSamples())));
    else if (clipHoldBlocksRemaining.load() > 0)
        clipHoldBlocksRemaining.fetch_sub (1);
}

int MultiModeEQAudioProcessor::readBandVisualiserSamples (int bandIndex, float* dest, int maxSamplesToRead) noexcept
{
    auto& fifo = bandFifos[(size_t) bandIndex];
    auto& buf  = bandFifoBuffers[(size_t) bandIndex];

    int start1, size1, start2, size2;
    fifo.prepareToRead (maxSamplesToRead, start1, size1, start2, size2);

    if (size1 > 0) juce::FloatVectorOperations::copy (dest, buf.data() + start1, size1);
    if (size2 > 0) juce::FloatVectorOperations::copy (dest + size1, buf.data() + start2, size2);

    fifo.finishedRead (size1 + size2);
    return size1 + size2;
}

//==============================================================================
juce::AudioProcessorEditor* MultiModeEQAudioProcessor::createEditor()
{
    return new MultiModeEQAudioProcessorEditor (*this);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> MultiModeEQAudioProcessor::buildStateXml()
{
    auto state = apvts.copyState();
    if (! state.isValid())
        return nullptr;

    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    // Persist MIDI Learn mappings alongside the normal parameter state.
    {
        const juce::ScopedLock lock (midiLearnLock);
        auto* midiLearnXml = xml->createNewChildElement ("MIDI_LEARN");
        for (const auto& [cc, paramId] : ccToParamId)
        {
            auto* entry = midiLearnXml->createNewChildElement ("MAP");
            entry->setAttribute ("cc", cc);
            entry->setAttribute ("param", paramId);
        }
    }

    return xml;
}

void MultiModeEQAudioProcessor::applyStateXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (xml));

    if (auto* midiLearnXml = xml.getChildByName ("MIDI_LEARN"))
    {
        const juce::ScopedLock lock (midiLearnLock);
        ccToParamId.clear();
        for (auto* entry : midiLearnXml->getChildIterator())
            ccToParamId[entry->getIntAttribute ("cc")] = entry->getStringAttribute ("param");
    }
}

void MultiModeEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MultiModeEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr)
        applyStateXml (*xml);
}

//==============================================================================
// Presets: named .xml snapshots of the same state buildStateXml()/
// applyStateXml() use for the host's own save/restore, stored per-user so
// they survive across projects/DAWs.
//==============================================================================
juce::File MultiModeEQAudioProcessor::getPresetsDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Mentals Multimode EQ")
                   .getChildFile ("Presets");

    if (! dir.isDirectory())
        dir.createDirectory();

    return dir;
}

juce::StringArray MultiModeEQAudioProcessor::getAvailablePresetNames() const
{
    juce::StringArray names;

    for (const auto& file : getPresetsDirectory().findChildFiles (juce::File::findFiles, false, "*.xml"))
        names.add (file.getFileNameWithoutExtension());

    names.sort (true);
    return names;
}

std::vector<MultiModeEQAudioProcessor::PresetCategory> MultiModeEQAudioProcessor::getFactoryPresetCategories() const
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

    static constexpr std::array<const char*, 7> canonicalOrder
        { "DRUMS", "BASS", "GUITARS", "STRINGS", "VOCALS", "KEYS", "SYNTHS" };

    std::sort (categories.begin(), categories.end(), [] (const PresetCategory& a, const PresetCategory& b)
    {
        const auto rank = [] (const juce::String& name) -> int
        {
            for (int i = 0; i < (int) canonicalOrder.size(); ++i)
                if (name == canonicalOrder[(size_t) i])
                    return i;
            return (int) canonicalOrder.size(); // unrecognised categories sort after the canonical ones
        };

        const int rankA = rank (a.name), rankB = rank (b.name);
        return rankA != rankB ? rankA < rankB : a.name < b.name;
    });

    return categories;
}

void MultiModeEQAudioProcessor::savePreset (const juce::String& presetName)
{
    if (presetName.isEmpty())
        return;

    if (auto xml = buildStateXml())
        xml->writeTo (getPresetsDirectory().getChildFile (presetName + ".xml"));
}

void MultiModeEQAudioProcessor::loadPreset (const juce::String& presetName)
{
    auto file = getPresetsDirectory().getChildFile (presetName + ".xml");
    if (! file.existsAsFile())
        return;

    if (auto xml = juce::XmlDocument::parse (file))
        applyStateXml (*xml);
}

void MultiModeEQAudioProcessor::resetToDefault()
{
    for (auto* param : getParameters())
        param->setValueNotifyingHost (param->getDefaultValue());

    clearAllMidiLearn();
}

//==============================================================================
// Factory presets: one tonal starting point per major instrument/vocal type,
// organised into category folders (DRUMS/BASS/GUITARS/STRINGS/VOCALS/KEYS/
// SYNTHS, plus ORCHESTRAL for the two orchestral sections that don't fit
// any of those seven) -- see getFactoryPresetCategories(), which just scans
// getPresetsDirectory()'s subfolders, so adding a preset here is the only
// thing that ever needs to change to make it show up grouped correctly.
//
// Each preset is a handful of Parametric moves (every band not listed is
// switched off, not just left at 0dB, so the graph shows exactly the shape
// being applied rather than ten flat, cluttering bands). Frequencies/gains/
// Qs follow standard mixing-engineering starting points (rumble/plosive
// high-pass, a mud cut around 200-500Hz, a presence bump somewhere in
// 2-5kHz, an air shelf above 8kHz) adapted per source -- a starting point
// to dial in further, not a finished mix. The DRUMS folder's per-mic
// presets (Kick In/Out/Sub, Snare Top/Bottom/Rim, Rack/Floor Tom) follow
// the same standard multi-mic drum-recording conventions: a sub/inside
// kick mic pushes deep thump and cuts everything above the target band, an
// outside kick mic is a gentler, roomier version of the same shape, a
// snare bottom mic leans on wire buzz rather than body, a rim/cross-stick
// mic is almost all transient with very little low end kept, and so on.
//==============================================================================
void MultiModeEQAudioProcessor::seedFactoryPresetsIfMissing()
{
    // DRUMS is always seeded alongside every other category, so its
    // presence alone is a reliable "have factory presets already been
    // written" marker -- getAvailablePresetNames() (root-level only) can't
    // be used for this any more now that factory presets live one folder
    // deeper.
    if (getPresetsDirectory().getChildFile ("DRUMS").isDirectory())
        return;

    struct Move { int band; float freqHz; float gainDb; float q; FilterShape shape; };

    auto applyPreset = [this] (const juce::String& category, const juce::String& name, std::initializer_list<Move> moves)
    {
        resetToDefault();
        for (auto& band : bands)
            band.enabledParam->setValueNotifyingHost (0.0f);

        for (auto& m : moves)
        {
            auto& band = bands[(size_t) m.band];
            band.enabledParam->setValueNotifyingHost (1.0f);
            band.freqParam->setValueNotifyingHost (band.freqParam->convertTo0to1 (m.freqHz));
            band.gainParam->setValueNotifyingHost (band.gainParam->convertTo0to1 (m.gainDb));
            band.qParam->setValueNotifyingHost (band.qParam->convertTo0to1 (m.q));
            band.filterShapeParam->setValueNotifyingHost (band.filterShapeParam->convertTo0to1 ((float) (int) m.shape));
        }

        const auto file = getPresetsDirectory().getChildFile (category).getChildFile (name + ".xml");
        file.getParentDirectory().createDirectory();
        if (auto xml = buildStateXml())
            xml->writeTo (file);
    };

    // ---- VOCALS -------------------------------------------------------------------
    applyPreset ("VOCALS", "Male", {
        { 0, 90.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 300.0f,   -2.5f, 1.0f, FilterShape::Bell },
        { 2, 3000.0f,   3.0f, 1.0f, FilterShape::Bell },
        { 3, 10000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("VOCALS", "Female", {
        { 0, 110.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 400.0f,   -2.0f, 1.0f, FilterShape::Bell },
        { 2, 5000.0f,   2.5f, 1.2f, FilterShape::Bell },
        { 3, 12000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("VOCALS", "Backing Choir", {
        { 0, 150.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 500.0f,   -2.0f, 1.0f, FilterShape::Bell },
        { 2, 3000.0f,  -1.5f, 1.0f, FilterShape::Bell },
        { 3, 10000.0f,  1.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("VOCALS", "Rap Hip-Hop", {
        { 0, 90.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 150.0f,    1.5f, 1.0f, FilterShape::Bell },
        { 2, 350.0f,   -3.0f, 1.1f, FilterShape::Bell },
        { 3, 3500.0f,   3.5f, 1.0f, FilterShape::Bell },
        { 4, 10000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    // ---- DRUMS: whole-kit starting points -------------------------------------------
    applyPreset ("DRUMS", "Kick", {
        { 0, 30.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 60.0f,     4.0f, 1.0f, FilterShape::Bell },
        { 2, 350.0f,   -4.0f, 1.2f, FilterShape::Bell },
        { 3, 3500.0f,   3.0f, 1.0f, FilterShape::Bell },
    });

    applyPreset ("DRUMS", "Snare", {
        { 0, 200.0f,    2.5f, 1.0f, FilterShape::Bell },
        { 1, 450.0f,   -2.5f, 1.1f, FilterShape::Bell },
        { 2, 3500.0f,   3.5f, 1.0f, FilterShape::Bell },
        { 3, 8000.0f,   2.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("DRUMS", "Hi-Hat Cymbals", {
        { 0, 400.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 3000.0f,  -1.5f, 1.0f, FilterShape::Bell },
        { 2, 10000.0f,  3.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("DRUMS", "Toms", {
        { 0, 50.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 120.0f,    3.0f, 1.0f, FilterShape::Bell },
        { 2, 400.0f,   -2.5f, 1.1f, FilterShape::Bell },
        { 3, 4500.0f,   2.5f, 1.0f, FilterShape::Bell },
    });

    applyPreset ("DRUMS", "Full Kit Overheads", {
        { 0, 60.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 100.0f,    1.5f, 1.0f, FilterShape::Bell },
        { 2, 400.0f,   -2.0f, 1.0f, FilterShape::Bell },
        { 3, 5000.0f,   1.5f, 1.0f, FilterShape::Bell },
        { 4, 10000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    // ---- DRUMS: individual mic positions --------------------------------------------
    // Sub/inside-kick mic: almost pure fundamental thump -- boosted hard,
    // then everything above the target band low-passed away, since a sub
    // mic's own diffuse pickup above a few hundred Hz is just boom to
    // filter out, not useful signal.
    applyPreset ("DRUMS", "Kick Sub", {
        { 0, 20.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 50.0f,     4.0f, 1.0f, FilterShape::Bell },
        { 2, 500.0f,    0.0f, 0.7f, FilterShape::LowPass },
    });

    applyPreset ("DRUMS", "Kick In", {
        { 0, 30.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 80.0f,     3.0f, 1.0f, FilterShape::Bell },
        { 2, 400.0f,   -3.5f, 1.2f, FilterShape::Bell },
        { 3, 4000.0f,   4.0f, 1.0f, FilterShape::Bell },
    });

    applyPreset ("DRUMS", "Kick Out", {
        { 0, 30.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 80.0f,     2.5f, 1.0f, FilterShape::Bell },
        { 2, 350.0f,   -3.0f, 1.2f, FilterShape::Bell },
        { 3, 3000.0f,   1.5f, 1.0f, FilterShape::Bell },
    });

    applyPreset ("DRUMS", "Snare Top", {
        { 0, 180.0f,    2.5f, 1.0f, FilterShape::Bell },
        { 1, 450.0f,   -2.5f, 1.1f, FilterShape::Bell },
        { 2, 4000.0f,   3.5f, 1.0f, FilterShape::Bell },
        { 3, 9000.0f,   2.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("DRUMS", "Snare Bottom", {
        { 0, 200.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 400.0f,   -2.5f, 1.1f, FilterShape::Bell },
        { 2, 5000.0f,   3.0f, 1.0f, FilterShape::Bell },
        { 3, 9000.0f,   1.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("DRUMS", "Snare Rim", {
        { 0, 250.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 3500.0f,   2.5f, 1.0f, FilterShape::Bell },
        { 2, 9000.0f,   2.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("DRUMS", "Rack Tom", {
        { 0, 60.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 180.0f,    3.0f, 1.0f, FilterShape::Bell },
        { 2, 500.0f,   -2.5f, 1.1f, FilterShape::Bell },
        { 3, 4500.0f,   2.5f, 1.0f, FilterShape::Bell },
    });

    applyPreset ("DRUMS", "Floor Tom", {
        { 0, 40.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 100.0f,    3.5f, 1.0f, FilterShape::Bell },
        { 2, 400.0f,   -2.5f, 1.1f, FilterShape::Bell },
        { 3, 3800.0f,   2.0f, 1.0f, FilterShape::Bell },
    });

    // ---- BASS -----------------------------------------------------------------------
    applyPreset ("BASS", "Electric", {
        { 0, 35.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 90.0f,     3.0f, 1.0f, FilterShape::Bell },
        { 2, 300.0f,   -3.0f, 1.2f, FilterShape::Bell },
        { 3, 900.0f,    1.5f, 1.0f, FilterShape::Bell },
        { 4, 2500.0f,   2.0f, 1.0f, FilterShape::Bell },
    });

    applyPreset ("BASS", "Upright Double", {
        { 0, 30.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 80.0f,     2.5f, 1.0f, FilterShape::Bell },
        { 2, 220.0f,   -3.0f, 1.3f, FilterShape::Bell },
        { 3, 2000.0f,   1.5f, 1.0f, FilterShape::Bell },
    });

    applyPreset ("BASS", "Sub 808", {
        { 0, 25.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 50.0f,     4.0f, 1.0f, FilterShape::Bell },
        { 2, 250.0f,   -3.0f, 1.1f, FilterShape::Bell },
        { 3, 5000.0f,  -3.0f, 0.7f, FilterShape::HighShelf },
    });

    // ---- GUITARS --------------------------------------------------------------------
    applyPreset ("GUITARS", "Acoustic", {
        { 0, 90.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 250.0f,   -2.5f, 1.1f, FilterShape::Bell },
        { 2, 3500.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 3, 11000.0f,  2.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("GUITARS", "Electric Clean", {
        { 0, 100.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 350.0f,   -2.0f, 1.1f, FilterShape::Bell },
        { 2, 3000.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 3, 8000.0f,   1.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("GUITARS", "Electric Distorted", {
        { 0, 120.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 450.0f,   -3.0f, 1.2f, FilterShape::Bell },
        { 2, 1800.0f,   2.5f, 1.0f, FilterShape::Bell },
        { 3, 7000.0f,  -2.0f, 1.0f, FilterShape::Bell },
    });

    // ---- KEYS -------------------------------------------------------------------------
    applyPreset ("KEYS", "Piano Acoustic", {
        { 0, 40.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 250.0f,   -2.0f, 1.0f, FilterShape::Bell },
        { 2, 3000.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 3, 10000.0f,  1.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("KEYS", "Electric Piano Rhodes", {
        { 0, 60.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 180.0f,    1.5f, 1.0f, FilterShape::Bell },
        { 2, 500.0f,   -1.5f, 1.0f, FilterShape::Bell },
        { 3, 3500.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 4, 9000.0f,   1.5f, 0.7f, FilterShape::HighShelf },
    });

    // ---- SYNTHS -------------------------------------------------------------------------
    applyPreset ("SYNTHS", "Lead", {
        { 0, 80.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 350.0f,   -1.5f, 1.0f, FilterShape::Bell },
        { 2, 2500.0f,   2.5f, 1.0f, FilterShape::Bell },
        { 3, 10000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("SYNTHS", "Pad", {
        { 0, 100.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 900.0f,   -2.0f, 0.8f, FilterShape::Bell },
        { 2, 12000.0f,  2.5f, 0.7f, FilterShape::HighShelf },
    });

    // ---- STRINGS ------------------------------------------------------------------------
    applyPreset ("STRINGS", "Section", {
        { 0, 100.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 350.0f,   -1.5f, 1.0f, FilterShape::Bell },
        { 2, 3500.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 3, 10000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    // ---- ORCHESTRAL: horns/winds, not covered by the seven requested folders ---------
    applyPreset ("ORCHESTRAL", "Brass Section", {
        { 0, 100.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 600.0f,   -2.5f, 1.2f, FilterShape::Bell },
        { 2, 3000.0f,   2.5f, 1.0f, FilterShape::Bell },
        { 3, 8000.0f,   1.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("ORCHESTRAL", "Woodwinds", {
        { 0, 120.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 500.0f,   -1.5f, 1.0f, FilterShape::Bell },
        { 2, 4000.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 3, 9000.0f,   1.5f, 0.7f, FilterShape::HighShelf },
    });

    resetToDefault();
}
