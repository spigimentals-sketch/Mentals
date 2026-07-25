#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr float sidechainHpfSkew = 0.333f;

    // Skew so each crossover knob's centre position lands near a sensible
    // default rather than the arithmetic midpoint of the full 20Hz-20kHz
    // range -- computed per crossover point below from its own default.
    float skewForDefault (float minV, float maxV, float defaultV) noexcept
    {
        const float t = (defaultV - minV) / (maxV - minV);
        return std::log (0.5f) / std::log (juce::jlimit (0.001f, 0.999f, t));
    }

    // 7 bands (Sub/Low/Low-Mid/Mid/High-Mid/High/Air), 6 crossover points.
    constexpr std::array<float, MentalsCircuitCompAudioProcessor::numCrossovers> defaultCrossoverFreqs {
        80.0f, 250.0f, 800.0f, 2500.0f, 6000.0f, 12000.0f
    };

    // Per-mode attack/release mapping: the knob is the same 0-100% control
    // in every mode ("unified controls"), but what real time constant it
    // maps to differs, since a FET circuit can grab far faster than a vari-
    // mu tube ever could. Optical's "release" additionally has a fixed fast
    // stage and a knob-mapped slow stage -- see processBandSample().
    void mapAttackRelease (int mode, float attackPct, float releasePct,
                            float& attackMsOut, float& releaseMsOut, float& opticalSlowReleaseMsOut) noexcept
    {
        switch (mode)
        {
            case MentalsCircuitCompAudioProcessor::modeFET:
                attackMsOut = juce::jmap (attackPct, 0.0f, 100.0f, 0.02f, 20.0f);
                releaseMsOut = juce::jmap (releasePct, 0.0f, 100.0f, 20.0f, 800.0f);
                opticalSlowReleaseMsOut = releaseMsOut;
                break;
            case MentalsCircuitCompAudioProcessor::modeOptical:
                attackMsOut = juce::jmap (attackPct, 0.0f, 100.0f, 2.0f, 50.0f);
                releaseMsOut = 60.0f; // fixed fast stage
                opticalSlowReleaseMsOut = juce::jmap (releasePct, 0.0f, 100.0f, 200.0f, 3000.0f);
                break;
            case MentalsCircuitCompAudioProcessor::modeTube:
                attackMsOut = juce::jmap (attackPct, 0.0f, 100.0f, 5.0f, 150.0f);
                releaseMsOut = juce::jmap (releasePct, 0.0f, 100.0f, 100.0f, 2000.0f);
                opticalSlowReleaseMsOut = releaseMsOut;
                break;
            case MentalsCircuitCompAudioProcessor::modeVCA:
            default:
                attackMsOut = juce::jmap (attackPct, 0.0f, 100.0f, 0.5f, 100.0f);
                releaseMsOut = juce::jmap (releasePct, 0.0f, 100.0f, 20.0f, 1000.0f);
                opticalSlowReleaseMsOut = releaseMsOut;
                break;
        }
    }

    // Per-mode harmonic coloration, applied once amount > 0. Each mode uses
    // a differently-shaped waveshaper so "Saturation" genuinely sounds like
    // that circuit type, not just a generic drive knob:
    //  - VCA: clean symmetric tanh, low drive range -- subtle, transparent.
    //  - FET: harder-driven, asymmetric tanh (positive/negative half-waves
    //    scaled differently) for a grittier, odd-plus-even harmonic bite.
    //  - Optical: gentle symmetric tanh plus a small, sign-preserving
    //    squared term for warm, low-level even harmonics.
    //  - Tube: richer squared-term bias for pronounced 2nd-harmonic warmth,
    //    the vari-mu "glow".
    float applySaturation (float x, int mode, float amount) noexcept
    {
        if (amount <= 0.0f)
            return x;

        switch (mode)
        {
            case MentalsCircuitCompAudioProcessor::modeFET:
            {
                const float drive = 1.0f + amount * 8.0f;
                const float asymDrive = x >= 0.0f ? drive : drive * 0.65f;
                return std::tanh (x * asymDrive) / std::tanh (drive);
            }
            case MentalsCircuitCompAudioProcessor::modeOptical:
            {
                const float drive = 1.0f + amount * 2.0f;
                const float shaped = std::tanh (x * drive) / std::tanh (drive);
                const float sign = shaped >= 0.0f ? 1.0f : -1.0f;
                return shaped + amount * 0.05f * shaped * shaped * sign;
            }
            case MentalsCircuitCompAudioProcessor::modeTube:
            {
                const float drive = 1.0f + amount * 4.0f;
                const float shaped = std::tanh (x * drive) / std::tanh (drive);
                return shaped + amount * 0.15f * shaped * shaped - amount * 0.04f;
            }
            case MentalsCircuitCompAudioProcessor::modeVCA:
            default:
            {
                const float drive = 1.0f + amount * 3.0f;
                return std::tanh (x * drive) / std::tanh (drive);
            }
        }
    }
}

//==============================================================================
MentalsCircuitCompAudioProcessor::MentalsCircuitCompAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                           .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                           .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    modeParam              = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("mode"));
    thresholdParam         = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("threshold"));
    ratioParam             = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("ratio"));
    kneeParam              = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("knee"));
    attackParam            = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("attack"));
    releaseParam           = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("release"));
    makeupGainParam        = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("makeupGain"));
    saturationParam        = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("saturation"));
    blendParam             = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("blend"));
    sidechainHpfFreqParam  = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("sidechainHpfFreq"));
    useSidechainParam      = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("useSidechain"));
    multibandEnabledParam  = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("multibandEnabled"));
    stereoLinkParam        = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("stereoLink"));
    stereoParam            = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("stereo"));

    for (int i = 0; i < numBands; ++i)
    {
        const auto suffix = juce::String (i);
        bandThresholdParams[(size_t) i] = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("bandThreshold" + suffix));
        bandRatioParams[(size_t) i]     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("bandRatio" + suffix));
        bandAttackParams[(size_t) i]    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("bandAttack" + suffix));
        bandReleaseParams[(size_t) i]   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("bandRelease" + suffix));
        bandMakeupParams[(size_t) i]    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("bandMakeup" + suffix));
        bandMuteParams[(size_t) i]      = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("bandMute" + suffix));
        bandSoloParams[(size_t) i]      = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter ("bandSolo" + suffix));
    }

    for (int i = 0; i < numCrossovers; ++i)
        crossoverFreqParams[(size_t) i] = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("crossoverFreq" + juce::String (i)));

    seedFactoryPresetsIfMissing();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MentalsCircuitCompAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "mode", "Mode", juce::StringArray { "VCA", "FET", "Optical", "Tube" }, modeVCA));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "threshold", "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f), -20.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ratio", "Ratio",
        juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f), 4.0f,
        juce::AudioParameterFloatAttributes().withLabel (":1")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "knee", "Knee",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f), 6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "attack", "Attack",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 30.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 40.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "makeupGain", "Makeup",
        juce::NormalisableRange<float> (-12.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "saturation", "Saturation",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "blend", "Blend",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "sidechainHpfFreq", "SC Filter",
        juce::NormalisableRange<float> (20.0f, 500.0f, 1.0f, sidechainHpfSkew), 80.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "useSidechain", "Use External Sidechain", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "multibandEnabled", "Multiband", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereoLink", "Stereo Link", true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "stereo", "Stereo", true));

    static const juce::StringArray bandNames { "Sub", "Low", "LowMid", "Mid", "HighMid", "High", "Air" };

    for (int i = 0; i < numBands; ++i)
    {
        const auto suffix = juce::String (i);
        const auto niceName = bandNames[i] + " ";

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "bandThreshold" + suffix, niceName + "Threshold",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f), -20.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "bandRatio" + suffix, niceName + "Ratio",
            juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f), 4.0f,
            juce::AudioParameterFloatAttributes().withLabel (":1")));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "bandAttack" + suffix, niceName + "Attack",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 30.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "bandRelease" + suffix, niceName + "Release",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 40.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "bandMakeup" + suffix, niceName + "Makeup",
            juce::NormalisableRange<float> (-12.0f, 24.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            "bandMute" + suffix, niceName + "Mute", false));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            "bandSolo" + suffix, niceName + "Solo", false));
    }

    for (int i = 0; i < numCrossovers; ++i)
    {
        const float defaultFreq = defaultCrossoverFreqs[(size_t) i];
        const float skew = skewForDefault (20.0f, 20000.0f, defaultFreq);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "crossoverFreq" + juce::String (i), "Crossover " + juce::String (i + 1),
            juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, skew), defaultFreq,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    }

    return { params.begin(), params.end() };
}

//==============================================================================
float MentalsCircuitCompAudioProcessor::computeModeAdjustedOutputDb (int mode, float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept
{
    float effectiveRatio = ratio;
    float effectiveKnee = kneeDb;

    switch (mode)
    {
        case modeFET:
            effectiveKnee = kneeDb * 0.4f; // harder knee
            break;
        case modeOptical:
            effectiveKnee = kneeDb * 1.8f + 3.0f; // softer knee
            break;
        case modeTube:
        {
            // Vari-mu: ratio creeps upward the further the signal sits
            // above threshold, rather than staying fixed.
            const float overshoot = juce::jmax (0.0f, inputDb - thresholdDb);
            effectiveRatio = ratio + juce::jmin (6.0f, overshoot * 0.15f);
            effectiveKnee = kneeDb * 1.5f + 2.0f;
            break;
        }
        case modeVCA:
        default:
            break;
    }

    return MentalsUI::DynamicsDSP::computeOutputDb (inputDb, thresholdDb, effectiveRatio, effectiveKnee);
}

//==============================================================================
void MentalsCircuitCompAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    for (auto& band : bands)
        band.prepare (sampleRate);

    for (auto& splitter : multibandSplitters)
    {
        splitter.reset();
        splitter.lastFreqs.fill (-1.0f); // force a coefficient recompute on first block
    }

    for (auto& hpf : sidechainHpf)
        hpf.reset();
    lastSidechainHpfFreq = -1.0f;

    currentGainReductionDb.store (0.0f);
    for (auto& gr : bandGainReductionDb)
        gr.store (0.0f);
    inputPeakLinear = 0.0f;
    outputPeakLinear = 0.0f;
    clipHoldBlocksRemaining = 0;
}

bool MentalsCircuitCompAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != mainIn || mainOut.isDisabled())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sidechain = layouts.getChannelSet (true, 1);
        if (! sidechain.isDisabled() && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

float MentalsCircuitCompAudioProcessor::processBandSample (Band& band, int channel, int mode, float x,
                                                            float thresholdDb, float ratio, float kneeDb,
                                                            float detectorLevelAbs, float& gainReductionDbOut) noexcept
{
    auto& state = band.channels[(size_t) channel];

    float envelopeAbs;
    if (mode == modeOptical)
    {
        const float fast = state.primaryFollower.process (detectorLevelAbs);
        const float slow = state.opticalSlowFollower.process (detectorLevelAbs);
        const float grDepth = juce::jlimit (0.0f, 1.0f, -state.previousGainReductionDb / 20.0f);
        envelopeAbs = juce::jmap (grDepth, fast, slow);
    }
    else
    {
        envelopeAbs = state.primaryFollower.process (detectorLevelAbs);
    }

    const float envelopeDb = juce::Decibels::gainToDecibels (envelopeAbs, -100.0f);
    const float outputDb = computeModeAdjustedOutputDb (mode, envelopeDb, thresholdDb, ratio, kneeDb);
    float gainReductionDb = outputDb - envelopeDb;

    if (mode == modeTube)
    {
        // Extra sluggish glide on top of the envelope follower itself,
        // modeling a vari-mu tube's electrical/thermal response time.
        constexpr float tubeSmoothing = 0.08f;
        state.tubeSmoothedGainReductionDb += (gainReductionDb - state.tubeSmoothedGainReductionDb) * tubeSmoothing;
        gainReductionDb = state.tubeSmoothedGainReductionDb;
    }

    state.previousGainReductionDb = gainReductionDb;
    gainReductionDbOut = gainReductionDb;

    return x * juce::Decibels::decibelsToGain (gainReductionDb);
}

void MentalsCircuitCompAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear(); // not a MIDI effect

    auto mainBuffer = getBusBuffer (buffer, true, 0);
    auto sidechainBuffer = getBusCount (true) > 1 ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const bool useSidechain = useSidechainParam->get() && sidechainBuffer.getNumChannels() > 0;

    // Captured before any of the processing below mutates mainBuffer in place.
    updateInputLevelMeter (mainBuffer);

    const int numChannels = juce::jmin (2, mainBuffer.getNumChannels());
    const int numSamples  = mainBuffer.getNumSamples();

    const int mode           = modeParam->getIndex();
    const float kneeDb       = kneeParam->get();
    const float makeupGain   = juce::Decibels::decibelsToGain (makeupGainParam->get());
    const float saturationAmount = saturationParam->get() * 0.01f;
    const float blend        = juce::jlimit (0.0f, 1.0f, blendParam->get() * 0.01f);
    const bool  multibandOn  = multibandEnabledParam->get();
    const bool  stereoLink   = stereoLinkParam->get();

    if (multibandOn)
    {
        // Per-band settings + solo/mute, monotonically ordered crossovers
        // (each at least 20Hz above the previous one, so no band can
        // collapse to a negative-width slice).
        std::array<float, numBands> bandThresholds, bandRatios, bandMakeups;
        std::array<float, numBands> bandAttackMs, bandReleaseMs, bandOpticalSlowReleaseMs;
        std::array<bool, numBands> bandSoloed {};
        bool anySolo = false;

        for (int b = 0; b < numBands; ++b)
        {
            bandThresholds[(size_t) b] = bandThresholdParams[(size_t) b]->get();
            bandRatios[(size_t) b]     = juce::jmax (1.0f, bandRatioParams[(size_t) b]->get());
            bandMakeups[(size_t) b]    = juce::Decibels::decibelsToGain (bandMakeupParams[(size_t) b]->get());
            bandSoloed[(size_t) b]     = bandSoloParams[(size_t) b]->get();
            anySolo |= bandSoloed[(size_t) b];

            mapAttackRelease (mode, bandAttackParams[(size_t) b]->get(), bandReleaseParams[(size_t) b]->get(),
                               bandAttackMs[(size_t) b], bandReleaseMs[(size_t) b], bandOpticalSlowReleaseMs[(size_t) b]);

            for (auto& state : bands[(size_t) b].channels)
            {
                state.primaryFollower.setAttackRelease (bandAttackMs[(size_t) b], mode == modeOptical ? 60.0f : bandReleaseMs[(size_t) b]);
                state.opticalSlowFollower.setAttackRelease (bandAttackMs[(size_t) b], bandOpticalSlowReleaseMs[(size_t) b]);
            }
        }

        std::array<bool, numBands> bandAudible {};
        for (int b = 0; b < numBands; ++b)
            bandAudible[(size_t) b] = ! bandMuteParams[(size_t) b]->get() && (! anySolo || bandSoloed[(size_t) b]);

        std::array<float, numCrossovers> crossovers;
        float previousFreq = 0.0f;
        for (int i = 0; i < numCrossovers; ++i)
        {
            const float freq = juce::jmax (crossoverFreqParams[(size_t) i]->get(), previousFreq + 20.0f);
            crossovers[(size_t) i] = freq;
            previousFreq = freq;
        }
        for (auto& splitter : multibandSplitters)
            splitter.updateIfNeeded (currentSampleRate, crossovers);

        float blockMinGainReductionDb = 0.0f;
        std::array<float, numBands> blockMinBandGainReductionDb {};
        blockMinBandGainReductionDb.fill (0.0f);

        for (int n = 0; n < numSamples; ++n)
        {
            float dry[2] = { 0.0f, 0.0f };
            for (int ch = 0; ch < numChannels; ++ch)
                dry[ch] = mainBuffer.getReadPointer (ch)[n];

            std::array<std::array<float, numBands>, 2> bandSamples;
            for (int ch = 0; ch < numChannels; ++ch)
                bandSamples[(size_t) ch] = multibandSplitters[(size_t) ch].process (dry[ch]);

            float summed[2] = { 0.0f, 0.0f };

            for (int b = 0; b < numBands; ++b)
            {
                if (! bandAudible[(size_t) b])
                    continue;

                float levelAbs[2] = { 0.0f, 0.0f };
                for (int ch = 0; ch < numChannels; ++ch)
                    levelAbs[ch] = std::abs (bandSamples[(size_t) ch][(size_t) b]);
                const float linkedLevel = juce::jmax (levelAbs[0], levelAbs[1]);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float detLevel = stereoLink ? linkedLevel : levelAbs[ch];
                    float gr = 0.0f;
                    float compressed = processBandSample (bands[(size_t) b], ch, mode, bandSamples[(size_t) ch][(size_t) b],
                                                           bandThresholds[(size_t) b], bandRatios[(size_t) b], kneeDb, detLevel, gr);
                    compressed *= bandMakeups[(size_t) b];
                    summed[ch] += compressed;
                    blockMinGainReductionDb = juce::jmin (blockMinGainReductionDb, gr);
                    blockMinBandGainReductionDb[(size_t) b] = juce::jmin (blockMinBandGainReductionDb[(size_t) b], gr);
                }
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float wet = applySaturation (summed[ch], mode, saturationAmount);
                mainBuffer.getWritePointer (ch)[n] = dry[ch] * (1.0f - blend) + wet * blend;
            }
        }

        currentGainReductionDb.store (blockMinGainReductionDb);
        for (int b = 0; b < numBands; ++b)
            bandGainReductionDb[(size_t) b].store (blockMinBandGainReductionDb[(size_t) b]);
    }
    else
    {
        const float thresholdDb = thresholdParam->get();
        const float ratio       = juce::jmax (1.0f, ratioParam->get());

        float attackMs, releaseMs, opticalSlowReleaseMs;
        mapAttackRelease (mode, attackParam->get(), releaseParam->get(), attackMs, releaseMs, opticalSlowReleaseMs);

        for (auto& state : bands[0].channels)
        {
            state.primaryFollower.setAttackRelease (attackMs, mode == modeOptical ? 60.0f : releaseMs);
            state.opticalSlowFollower.setAttackRelease (attackMs, opticalSlowReleaseMs);
        }

        // Sidechain detector highpass: single-band mode only -- see class comment.
        const float hpfFreq = sidechainHpfFreqParam->get();
        if (hpfFreq != lastSidechainHpfFreq)
        {
            lastSidechainHpfFreq = hpfFreq;
            auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, hpfFreq, 0.70710678f);
            for (auto& hpf : sidechainHpf)
                hpf.coefficients = coeffs;
        }

        float blockMinGainReductionDb = 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            float dry[2] = { 0.0f, 0.0f };
            for (int ch = 0; ch < numChannels; ++ch)
                dry[ch] = mainBuffer.getReadPointer (ch)[n];

            float scRaw[2] = { 0.0f, 0.0f };
            for (int ch = 0; ch < numChannels; ++ch)
                scRaw[ch] = useSidechain && ch < sidechainBuffer.getNumChannels()
                                ? sidechainBuffer.getReadPointer (ch)[n]
                                : dry[ch];

            float levelAbs[2] = { 0.0f, 0.0f };
            for (int ch = 0; ch < numChannels; ++ch)
                levelAbs[ch] = std::abs (sidechainHpf[(size_t) ch].processSample (scRaw[ch]));
            const float linkedLevel = juce::jmax (levelAbs[0], levelAbs[1]);

            float summed[2] = { 0.0f, 0.0f };
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float detLevel = stereoLink ? linkedLevel : levelAbs[ch];
                float gr = 0.0f;
                summed[ch] = processBandSample (bands[0], ch, mode, dry[ch], thresholdDb, ratio, kneeDb, detLevel, gr);
                blockMinGainReductionDb = juce::jmin (blockMinGainReductionDb, gr);
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float wet = summed[ch] * makeupGain;
                wet = applySaturation (wet, mode, saturationAmount);
                mainBuffer.getWritePointer (ch)[n] = dry[ch] * (1.0f - blend) + wet * blend;
            }
        }

        currentGainReductionDb.store (blockMinGainReductionDb);
        for (auto& gr : bandGainReductionDb)
            gr.store (0.0f);
    }

    if (! stereoParam->get() && numChannels > 1)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float avg = 0.5f * (mainBuffer.getReadPointer (0)[n] + mainBuffer.getReadPointer (1)[n]);
            mainBuffer.getWritePointer (0)[n] = avg;
            mainBuffer.getWritePointer (1)[n] = avg;
        }
    }

    updateOutputLevelMeter (mainBuffer);
}

void MentalsCircuitCompAudioProcessor::updateInputLevelMeter (const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    const float releasePerBlock = std::pow (10.0f, -24.0f * ((float) buffer.getNumSamples() / (float) currentSampleRate) / 20.0f);
    const float previous = inputPeakLinear.load();
    inputPeakLinear.store (juce::jmax (peak, previous * releasePerBlock));
}

void MentalsCircuitCompAudioProcessor::updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer)
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
void MentalsCircuitCompAudioProcessor::seedFactoryPresetsIfMissing()
{
    if (! presetManager.getAvailablePresetNames().isEmpty())
        return;

    auto applyF = [] (juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); };
    auto applyChoice = [] (juce::AudioParameterChoice* p, int index) { p->setValueNotifyingHost (p->convertTo0to1 ((float) index)); };

    resetToDefault();
    applyChoice (modeParam, modeVCA);
    applyF (thresholdParam, -18.0f); applyF (ratioParam, 3.0f); applyF (attackParam, 20.0f); applyF (releaseParam, 35.0f);
    applyF (saturationParam, 5.0f);
    presetManager.savePreset ("Clean Bus Glue");

    resetToDefault();
    applyChoice (modeParam, modeFET);
    applyF (thresholdParam, -24.0f); applyF (ratioParam, 8.0f); applyF (attackParam, 5.0f); applyF (releaseParam, 25.0f);
    applyF (saturationParam, 55.0f);
    presetManager.savePreset ("FET Punch");

    resetToDefault();
    applyChoice (modeParam, modeOptical);
    applyF (thresholdParam, -20.0f); applyF (ratioParam, 3.5f); applyF (attackParam, 40.0f); applyF (releaseParam, 60.0f);
    applyF (saturationParam, 15.0f); applyF (blendParam, 80.0f);
    presetManager.savePreset ("Opto Vocal Smooth");

    resetToDefault();
    applyChoice (modeParam, modeTube);
    applyF (thresholdParam, -16.0f); applyF (ratioParam, 2.5f); applyF (attackParam, 45.0f); applyF (releaseParam, 55.0f);
    applyF (saturationParam, 35.0f);
    presetManager.savePreset ("Vari-Mu Warmth");

    resetToDefault();
    applyChoice (modeParam, modeFET);
    applyF (thresholdParam, -30.0f); applyF (ratioParam, 10.0f); applyF (attackParam, 8.0f); applyF (releaseParam, 20.0f);
    applyF (saturationParam, 40.0f); applyF (blendParam, 45.0f);
    presetManager.savePreset ("New York Parallel");

    resetToDefault();
}

//==============================================================================
juce::AudioProcessorEditor* MentalsCircuitCompAudioProcessor::createEditor()
{
    return new MentalsCircuitCompAudioProcessorEditor (*this);
}

void MentalsCircuitCompAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = presetManager.buildStateXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsCircuitCompAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        presetManager.applyStateXml (*xml);
}
