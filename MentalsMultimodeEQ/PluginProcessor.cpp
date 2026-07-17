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
    }

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

        const BiquadCoefficients activeCoefficients = BiquadCoefficients::lerp (blockStartCoefficients, targetCoefficients, 1.0f);
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

            aiAssistFft.performFrequencyOnlyForwardTransform (aiAssistFftData.data(), true);

            const juce::SpinLock::ScopedLockType lock (aiAssistLock);

            if (! aiAssistCapturing) // cancelAiAssistAnalysis() may have fired while the FFT above ran
                return;

            for (int bin = 0; bin < spectrumNumBins; ++bin)
            {
                const float magnitude = aiAssistFftData[(size_t) bin] / (float) spectrumFftSize;
                aiAssistCapturedSpectrumDb[(size_t) bin] += juce::Decibels::gainToDecibels (magnitude, -100.0f);
            }

            if (++aiAssistCaptureBlocks >= 40) // ~2s of 2048-sample blocks, matching EQ Match's capture length
            {
                for (auto& v : aiAssistCapturedSpectrumDb)
                    v /= (float) aiAssistCaptureBlocks;

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
    {
        const juce::SpinLock::ScopedLockType lock (aiAssistLock);
        if (! aiAssistHasCapture)
            return 0;
        capturedDb = aiAssistCapturedSpectrumDb;
    }

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

    for (size_t m = 0; m < macroBands.size(); ++m)
    {
        if (macroCount[m] == 0)
            continue;

        const float macroAvgDb = (float) (macroSumDb[m] / macroCount[m]);
        const float deviation  = macroAvgDb - overallAvgDb;

        if (std::abs (deviation) < 3.0f)
            continue; // already balanced here

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
    struct ResonancePeak { double freqHz; float prominence; };
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
            resonances.push_back ({ freq, prominence });
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

    for (auto& r : keptResonances)
    {
        // Dynamic Bell: static gain stays at 0dB, and the cut only emerges
        // when the envelope crosses thresholdDb -- the exact same mechanism
        // any manually-configured Dynamic band uses (see EQBand's envelope
        // detector in processBand()), just chosen automatically here rather
        // than by hand.
        const float thresholdDb = juce::jlimit (-50.0f, -6.0f, overallAvgDb - 8.0f);
        suggestions.push_back ({ (float) r.freqHz, 0.0f, 5.0f, true, FilterShape::Bell, thresholdDb, 3.0f, r.prominence });
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

    updateSpectrumAnalyser (mainBuffer);
    updateOutputLevelMeter (mainBuffer);

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
// each a handful of Parametric moves (every band not listed is switched off,
// not just left at 0dB, so the graph shows exactly the shape being applied
// rather than ten flat, cluttering bands). Frequencies/gains/Qs below follow
// standard mixing-engineering starting points (rumble/plosive high-pass,
// mud cut around 200-500Hz, a presence bump somewhere in 2-5kHz, an air
// shelf above 8kHz) adapted per source -- a starting point to dial in
// further, not a finished mix.
//==============================================================================
void MultiModeEQAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! getAvailablePresetNames().isEmpty())
        return;

    struct Move { int band; float freqHz; float gainDb; float q; FilterShape shape; };

    auto applyPreset = [this] (const juce::String& name, std::initializer_list<Move> moves)
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

        savePreset (name);
    };

    // ---- Vocals -----------------------------------------------------------------
    applyPreset ("Vocal - Male", {
        { 0, 90.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 300.0f,   -2.5f, 1.0f, FilterShape::Bell },
        { 2, 3000.0f,   3.0f, 1.0f, FilterShape::Bell },
        { 3, 10000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Vocal - Female", {
        { 0, 110.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 400.0f,   -2.0f, 1.0f, FilterShape::Bell },
        { 2, 5000.0f,   2.5f, 1.2f, FilterShape::Bell },
        { 3, 12000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Vocal - Backing Choir", {
        { 0, 150.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 500.0f,   -2.0f, 1.0f, FilterShape::Bell },
        { 2, 3000.0f,  -1.5f, 1.0f, FilterShape::Bell },
        { 3, 10000.0f,  1.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Vocal - Rap Hip-Hop", {
        { 0, 90.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 150.0f,    1.5f, 1.0f, FilterShape::Bell },
        { 2, 350.0f,   -3.0f, 1.1f, FilterShape::Bell },
        { 3, 3500.0f,   3.5f, 1.0f, FilterShape::Bell },
        { 4, 10000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    // ---- Drums --------------------------------------------------------------------
    applyPreset ("Drums - Kick", {
        { 0, 30.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 60.0f,     4.0f, 1.0f, FilterShape::Bell },
        { 2, 350.0f,   -4.0f, 1.2f, FilterShape::Bell },
        { 3, 3500.0f,   3.0f, 1.0f, FilterShape::Bell },
    });

    applyPreset ("Drums - Snare", {
        { 0, 200.0f,    2.5f, 1.0f, FilterShape::Bell },
        { 1, 450.0f,   -2.5f, 1.1f, FilterShape::Bell },
        { 2, 3500.0f,   3.5f, 1.0f, FilterShape::Bell },
        { 3, 8000.0f,   2.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Drums - Hi-Hat Cymbals", {
        { 0, 400.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 3000.0f,  -1.5f, 1.0f, FilterShape::Bell },
        { 2, 10000.0f,  3.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Drums - Toms", {
        { 0, 50.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 120.0f,    3.0f, 1.0f, FilterShape::Bell },
        { 2, 400.0f,   -2.5f, 1.1f, FilterShape::Bell },
        { 3, 4500.0f,   2.5f, 1.0f, FilterShape::Bell },
    });

    applyPreset ("Drums - Full Kit Overheads", {
        { 0, 60.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 100.0f,    1.5f, 1.0f, FilterShape::Bell },
        { 2, 400.0f,   -2.0f, 1.0f, FilterShape::Bell },
        { 3, 5000.0f,   1.5f, 1.0f, FilterShape::Bell },
        { 4, 10000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    // ---- Bass -----------------------------------------------------------------------
    applyPreset ("Bass - Electric", {
        { 0, 35.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 90.0f,     3.0f, 1.0f, FilterShape::Bell },
        { 2, 300.0f,   -3.0f, 1.2f, FilterShape::Bell },
        { 3, 900.0f,    1.5f, 1.0f, FilterShape::Bell },
        { 4, 2500.0f,   2.0f, 1.0f, FilterShape::Bell },
    });

    applyPreset ("Bass - Upright Double", {
        { 0, 30.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 80.0f,     2.5f, 1.0f, FilterShape::Bell },
        { 2, 220.0f,   -3.0f, 1.3f, FilterShape::Bell },
        { 3, 2000.0f,   1.5f, 1.0f, FilterShape::Bell },
    });

    applyPreset ("Bass - Sub 808", {
        { 0, 25.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 50.0f,     4.0f, 1.0f, FilterShape::Bell },
        { 2, 250.0f,   -3.0f, 1.1f, FilterShape::Bell },
        { 3, 5000.0f,  -3.0f, 0.7f, FilterShape::HighShelf },
    });

    // ---- Guitars --------------------------------------------------------------------
    applyPreset ("Guitar - Acoustic", {
        { 0, 90.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 250.0f,   -2.5f, 1.1f, FilterShape::Bell },
        { 2, 3500.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 3, 11000.0f,  2.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Guitar - Electric Clean", {
        { 0, 100.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 350.0f,   -2.0f, 1.1f, FilterShape::Bell },
        { 2, 3000.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 3, 8000.0f,   1.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Guitar - Electric Distorted", {
        { 0, 120.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 450.0f,   -3.0f, 1.2f, FilterShape::Bell },
        { 2, 1800.0f,   2.5f, 1.0f, FilterShape::Bell },
        { 3, 7000.0f,  -2.0f, 1.0f, FilterShape::Bell },
    });

    // ---- Keys and synths --------------------------------------------------------
    applyPreset ("Piano - Acoustic", {
        { 0, 40.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 250.0f,   -2.0f, 1.0f, FilterShape::Bell },
        { 2, 3000.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 3, 10000.0f,  1.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Keys - Electric Piano Rhodes", {
        { 0, 60.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 180.0f,    1.5f, 1.0f, FilterShape::Bell },
        { 2, 500.0f,   -1.5f, 1.0f, FilterShape::Bell },
        { 3, 3500.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 4, 9000.0f,   1.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Synth - Lead", {
        { 0, 80.0f,     0.0f, 0.7f, FilterShape::HighPass },
        { 1, 350.0f,   -1.5f, 1.0f, FilterShape::Bell },
        { 2, 2500.0f,   2.5f, 1.0f, FilterShape::Bell },
        { 3, 10000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Synth - Pad", {
        { 0, 100.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 900.0f,   -2.0f, 0.8f, FilterShape::Bell },
        { 2, 12000.0f,  2.5f, 0.7f, FilterShape::HighShelf },
    });

    // ---- Orchestral / horns -------------------------------------------------------
    applyPreset ("Strings - Section", {
        { 0, 100.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 350.0f,   -1.5f, 1.0f, FilterShape::Bell },
        { 2, 3500.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 3, 10000.0f,  2.0f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Brass - Section", {
        { 0, 100.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 600.0f,   -2.5f, 1.2f, FilterShape::Bell },
        { 2, 3000.0f,   2.5f, 1.0f, FilterShape::Bell },
        { 3, 8000.0f,   1.5f, 0.7f, FilterShape::HighShelf },
    });

    applyPreset ("Woodwinds", {
        { 0, 120.0f,    0.0f, 0.7f, FilterShape::HighPass },
        { 1, 500.0f,   -1.5f, 1.0f, FilterShape::Bell },
        { 2, 4000.0f,   2.0f, 1.0f, FilterShape::Bell },
        { 3, 9000.0f,   1.5f, 0.7f, FilterShape::HighShelf },
    });

    resetToDefault();
}
