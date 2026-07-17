#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <map>

//==============================================================================
// Per-band operating mode selectable from the GUI dropdown.
//==============================================================================
enum class EQMode
{
    Parametric = 0,
    Dynamic
};

// Which channel(s) a band's filter is applied to. Mid/Side only make sense
// for a stereo channel pair, so they always act on channels 0/1 (the main
// stereo pair) regardless of how many channels the current bus layout has --
// see the class comment on surround support below.
enum class ChannelTarget
{
    StereoOrAll = 0,
    Mid,
    Side,
    Left,
    Right
};

enum class PhaseMode
{
    ZeroLatency = 0, // minimum-phase IIR (the biquad chain), no added latency
    NaturalPhase     // linear-phase FIR match of the same magnitude response, at the cost of added latency
};

// A band's filter shape, only selectable/meaningful in Parametric mode (see
// EQMode) -- Dynamic-mode bands always behave as Bell. "High Pass" and
// "Low Cut" are the exact same filter in standard audio terminology (likewise
// "Low Pass" and "High Cut"), so there are only two cut shapes here; the GUI
// labels each to cover both names rather than offering four redundant
// duplicates. Low Shelf/High Shelf use the band's gain (like Bell) and are
// always a single section -- unlike High Pass/Low Pass, cascading identical
// shelf stages would multiply the total gain rather than just steepen the
// transition, so they don't participate in the slope/cascade mechanism.
enum class FilterShape
{
    Bell = 0,
    HighPass, // a.k.a. Low Cut
    LowPass,  // a.k.a. High Cut
    LowShelf,
    HighShelf
};

//==============================================================================
// Plain set of normalised biquad (peaking-filter) coefficients.
// Direct Form II Transposed is used in BiquadFilter below because it needs
// only two state variables per channel and is numerically well behaved for
// the moderate coefficient modulation rates used here.
//==============================================================================
struct BiquadCoefficients
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f; // a0 has already been normalised to 1

    // Linear interpolation between two coefficient sets. Used to glide
    // smoothly from the previous sample's coefficients to the new target,
    // which avoids the audible "zipper"/clicking artefacts that occur when
    // filter coefficients are changed abruptly.
    static BiquadCoefficients lerp (const BiquadCoefficients& a, const BiquadCoefficients& b, float t)
    {
        BiquadCoefficients out;
        out.b0 = a.b0 + (b.b0 - a.b0) * t;
        out.b1 = a.b1 + (b.b1 - a.b1) * t;
        out.b2 = a.b2 + (b.b2 - a.b2) * t;
        out.a1 = a.a1 + (b.a1 - a.a1) * t;
        out.a2 = a.a2 + (b.a2 - a.a2) * t;
        return out;
    }

    //==========================================================================
    // RBJ "Audio EQ Cookbook" peaking-EQ (bell curve) design equations.
    //==========================================================================
    static BiquadCoefficients makePeaking (double sampleRate, float freqHz, float q, float gainDb)
    {
        freqHz = juce::jlimit (20.0f, static_cast<float> (sampleRate) * 0.49f, freqHz);
        q      = juce::jmax (0.05f, q);

        const double w0    = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
        const double cosW0 = std::cos (w0);
        const double sinW0 = std::sin (w0);
        const double alpha = sinW0 / (2.0 * q);
        const double A     = std::pow (10.0, gainDb / 40.0);

        const double b0 = 1.0 + alpha * A;
        const double b1 = -2.0 * cosW0;
        const double b2 = 1.0 - alpha * A;
        const double a0 = 1.0 + alpha / A;
        const double a1 = -2.0 * cosW0;
        const double a2 = 1.0 - alpha / A;

        BiquadCoefficients c;
        c.b0 = static_cast<float> (b0 / a0);
        c.b1 = static_cast<float> (b1 / a0);
        c.b2 = static_cast<float> (b2 / a0);
        c.a1 = static_cast<float> (a1 / a0);
        c.a2 = static_cast<float> (a2 / a0);
        return c;
    }

    //==========================================================================
    // RBJ "Audio EQ Cookbook" high-pass / low-pass design equations. Q shapes
    // the resonance at the cutoff (0.707 is the flat/Butterworth response).
    // Steeper slopes are built by cascading several of these identical
    // sections in series (see EQBand::numActiveFilterStages), not by a
    // higher-order formula here.
    //==========================================================================
    static BiquadCoefficients makeHighPass (double sampleRate, float freqHz, float q)
    {
        freqHz = juce::jlimit (20.0f, static_cast<float> (sampleRate) * 0.49f, freqHz);
        q      = juce::jmax (0.05f, q);

        const double w0    = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
        const double cosW0 = std::cos (w0);
        const double sinW0 = std::sin (w0);
        const double alpha = sinW0 / (2.0 * q);

        const double b0 =  (1.0 + cosW0) / 2.0;
        const double b1 = -(1.0 + cosW0);
        const double b2 =  (1.0 + cosW0) / 2.0;
        const double a0 =  1.0 + alpha;
        const double a1 = -2.0 * cosW0;
        const double a2 =  1.0 - alpha;

        BiquadCoefficients c;
        c.b0 = static_cast<float> (b0 / a0);
        c.b1 = static_cast<float> (b1 / a0);
        c.b2 = static_cast<float> (b2 / a0);
        c.a1 = static_cast<float> (a1 / a0);
        c.a2 = static_cast<float> (a2 / a0);
        return c;
    }

    static BiquadCoefficients makeLowPass (double sampleRate, float freqHz, float q)
    {
        freqHz = juce::jlimit (20.0f, static_cast<float> (sampleRate) * 0.49f, freqHz);
        q      = juce::jmax (0.05f, q);

        const double w0    = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
        const double cosW0 = std::cos (w0);
        const double sinW0 = std::sin (w0);
        const double alpha = sinW0 / (2.0 * q);

        const double b0 =  (1.0 - cosW0) / 2.0;
        const double b1 =   1.0 - cosW0;
        const double b2 =  (1.0 - cosW0) / 2.0;
        const double a0 =  1.0 + alpha;
        const double a1 = -2.0 * cosW0;
        const double a2 =  1.0 - alpha;

        BiquadCoefficients c;
        c.b0 = static_cast<float> (b0 / a0);
        c.b1 = static_cast<float> (b1 / a0);
        c.b2 = static_cast<float> (b2 / a0);
        c.a1 = static_cast<float> (a1 / a0);
        c.a2 = static_cast<float> (a2 / a0);
        return c;
    }

    //==========================================================================
    // RBJ "Audio EQ Cookbook" low-shelf / high-shelf design equations. Q
    // shapes how sharp the transition into the shelf is (0.707 is a smooth,
    // non-resonant transition); gainDb is the final shelf gain reached well
    // past the transition, same convention as Bell.
    //==========================================================================
    static BiquadCoefficients makeLowShelf (double sampleRate, float freqHz, float q, float gainDb)
    {
        freqHz = juce::jlimit (20.0f, static_cast<float> (sampleRate) * 0.49f, freqHz);
        q      = juce::jmax (0.05f, q);

        const double w0    = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
        const double cosW0 = std::cos (w0);
        const double sinW0 = std::sin (w0);
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double beta  = sinW0 * std::sqrt (A) / q;

        const double aMinus1 = A - 1.0;
        const double aPlus1  = A + 1.0;
        const double aMinus1TimesCos = aMinus1 * cosW0;

        const double b0 =        A * (aPlus1 - aMinus1TimesCos + beta);
        const double b1 =  A * 2.0 * (aMinus1 - aPlus1 * cosW0);
        const double b2 =        A * (aPlus1 - aMinus1TimesCos - beta);
        const double a0 =            aPlus1 + aMinus1TimesCos + beta;
        const double a1 = -2.0 *     (aMinus1 + aPlus1 * cosW0);
        const double a2 =            aPlus1 + aMinus1TimesCos - beta;

        BiquadCoefficients c;
        c.b0 = static_cast<float> (b0 / a0);
        c.b1 = static_cast<float> (b1 / a0);
        c.b2 = static_cast<float> (b2 / a0);
        c.a1 = static_cast<float> (a1 / a0);
        c.a2 = static_cast<float> (a2 / a0);
        return c;
    }

    static BiquadCoefficients makeHighShelf (double sampleRate, float freqHz, float q, float gainDb)
    {
        freqHz = juce::jlimit (20.0f, static_cast<float> (sampleRate) * 0.49f, freqHz);
        q      = juce::jmax (0.05f, q);

        const double w0    = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
        const double cosW0 = std::cos (w0);
        const double sinW0 = std::sin (w0);
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double beta  = sinW0 * std::sqrt (A) / q;

        const double aMinus1 = A - 1.0;
        const double aPlus1  = A + 1.0;
        const double aMinus1TimesCos = aMinus1 * cosW0;

        const double b0 =       A * (aPlus1 + aMinus1TimesCos + beta);
        const double b1 = -A * 2.0 * (aMinus1 + aPlus1 * cosW0);
        const double b2 =       A * (aPlus1 + aMinus1TimesCos - beta);
        const double a0 =           aPlus1 - aMinus1TimesCos + beta;
        const double a1 =  2.0 *   (aMinus1 - aPlus1 * cosW0);
        const double a2 =           aPlus1 - aMinus1TimesCos - beta;

        BiquadCoefficients c;
        c.b0 = static_cast<float> (b0 / a0);
        c.b1 = static_cast<float> (b1 / a0);
        c.b2 = static_cast<float> (b2 / a0);
        c.a1 = static_cast<float> (a1 / a0);
        c.a2 = static_cast<float> (a2 / a0);
        return c;
    }

    // Magnitude response |H(f)| of this biquad at freqHz, used by the
    // spectrum analyser (to draw the EQ curve) and by the Natural Phase
    // linear-phase FIR designer (to sample the target magnitude response).
    double getMagnitudeForFrequency (double freqHz, double sampleRate) const noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
        const std::complex<double> j (0.0, 1.0);
        const std::complex<double> z  = std::exp (-j * w);
        const std::complex<double> z2 = z * z;
        const std::complex<double> num = (double) b0 + (double) b1 * z + (double) b2 * z2;
        const std::complex<double> den = 1.0        + (double) a1 * z + (double) a2 * z2;
        return std::abs (num / den);
    }
};

//==============================================================================
// Single-channel biquad, Direct Form II Transposed.
//==============================================================================
class BiquadFilter
{
public:
    void reset() noexcept { z1 = z2 = 0.0f; }

    float processSample (float x, const BiquadCoefficients& c) noexcept
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

private:
    float z1 = 0.0f, z2 = 0.0f;
};

//==============================================================================
// Classic one-pole envelope follower with independent attack/release times.
// Used by Dynamic EQ bands to derive a control signal from either the band's
// own signal or an external sidechain input.
//==============================================================================
class EnvelopeFollower
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        updateCoefficients();
    }

    void setAttackRelease (float attackMs, float releaseMs) noexcept
    {
        attackTimeMs  = juce::jmax (0.01f, attackMs);
        releaseTimeMs = juce::jmax (0.01f, releaseMs);
        updateCoefficients();
    }

    void reset() noexcept { envelope = 0.0f; }

    float process (float rectifiedInput) noexcept
    {
        const float coeff = (rectifiedInput > envelope) ? attackCoeff : releaseCoeff;
        envelope = coeff * envelope + (1.0f - coeff) * rectifiedInput;
        return envelope;
    }

private:
    void updateCoefficients() noexcept
    {
        attackCoeff  = std::exp (-1.0f / (0.001f * attackTimeMs  * static_cast<float> (fs)));
        releaseCoeff = std::exp (-1.0f / (0.001f * releaseTimeMs * static_cast<float> (fs)));
    }

    double fs = 44100.0;
    float attackTimeMs = 10.0f, releaseTimeMs = 100.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    float envelope = 0.0f;
};

// A band's filter, cascaded up to 4 identical biquad sections deep to reach
// steeper slopes for High Pass / Low Pass shapes (1 section = 12dB/oct, 2 =
// 24dB/oct, 3 = 36dB/oct, 4 = 48dB/oct). Bell shape only ever uses 1 section.
static constexpr int maxFilterStages = 4;

struct FilterChain
{
    std::array<BiquadFilter, maxFilterStages> stages;
    void reset() noexcept { for (auto& s : stages) s.reset(); }
};

//==============================================================================
// One EQ band: Bell/High-Pass/Low-Pass filter (Parametric mode) or a dynamic
// Bell (Dynamic mode), targeting stereo/mid/side/left/right, with its own
// dynamics detector (fed from either its own signal or the sidechain input).
//==============================================================================
struct EQBand
{
    // Cached APVTS parameter pointers (owned by the tree; not by this struct).
    juce::AudioParameterBool*   enabledParam     = nullptr;
    juce::AudioParameterFloat*  freqParam        = nullptr;
    juce::AudioParameterFloat*  gainParam        = nullptr;
    juce::AudioParameterFloat*  qParam           = nullptr;
    juce::AudioParameterChoice* modeParam        = nullptr;
    juce::AudioParameterChoice* filterShapeParam = nullptr;
    juce::AudioParameterChoice* slopeParam       = nullptr;
    juce::AudioParameterChoice* channelParam     = nullptr;
    juce::AudioParameterFloat*  thresholdParam   = nullptr;
    juce::AudioParameterFloat*  ratioParam       = nullptr;
    juce::AudioParameterFloat*  attackParam      = nullptr;
    juce::AudioParameterFloat*  releaseParam     = nullptr;
    juce::AudioParameterBool*   sidechainParam   = nullptr;

    juce::SmoothedValue<float> smoothedFreq, smoothedGain, smoothedQ, smoothedThreshold, smoothedRatio;

    // Used when channelTarget == StereoOrAll: one filter chain per channel of
    // the current bus (so a surround bed gets independently-filtered
    // channels). For Mid/Side/Left/Right targeting, only
    // filtersPerChannel[0] is used (applied to the encoded M, S, L or R
    // signal respectively).
    std::vector<FilterChain> filtersPerChannel;
    BiquadCoefficients previousCoefficients;

    // How many of each chain's stages are actually active for the current
    // filter shape/slope -- updated every block by processBand, and read
    // externally (spectrum analyser curve, Natural Phase FIR designer) via
    // getMagnitudeForFrequency() below rather than computed independently.
    int numActiveFilterStages = 1;

    // Single shared dynamics detector for the whole band (Dynamic mode only).
    EnvelopeFollower envelopeFollower;

    void prepare (double sampleRate, int numChannelsForFilters)
    {
        const double rampSeconds = 0.02;
        smoothedFreq.reset      (sampleRate, rampSeconds);
        smoothedGain.reset      (sampleRate, rampSeconds);
        smoothedQ.reset         (sampleRate, rampSeconds);
        smoothedThreshold.reset (sampleRate, rampSeconds);
        smoothedRatio.reset     (sampleRate, rampSeconds);

        smoothedFreq.setCurrentAndTargetValue      (freqParam->get());
        smoothedGain.setCurrentAndTargetValue      (gainParam->get());
        smoothedQ.setCurrentAndTargetValue         (qParam->get());
        smoothedThreshold.setCurrentAndTargetValue (thresholdParam->get());
        smoothedRatio.setCurrentAndTargetValue     (ratioParam->get());

        filtersPerChannel.assign ((size_t) juce::jmax (1, numChannelsForFilters), FilterChain());
        for (auto& f : filtersPerChannel)
            f.reset();

        envelopeFollower.prepare (sampleRate);
        envelopeFollower.setAttackRelease (attackParam->get(), releaseParam->get());
        envelopeFollower.reset();

        previousCoefficients = BiquadCoefficients::makePeaking (
            sampleRate, freqParam->get(), qParam->get(), gainParam->get());
    }

    EQMode getMode() const noexcept          { return static_cast<EQMode> (modeParam->getIndex()); }
    FilterShape getFilterShape() const noexcept { return static_cast<FilterShape> (filterShapeParam->getIndex()); }
    int getSlopeStages() const noexcept      { return slopeParam->getIndex() + 1; } // 0..3 -> 1..4 cascaded stages
    ChannelTarget getChannelTarget() const noexcept { return static_cast<ChannelTarget> (channelParam->getIndex()); }
    bool isEnabled() const noexcept          { return enabledParam->get(); }

    // Magnitude response accounting for cascaded stages -- use this (not
    // previousCoefficients.getMagnitudeForFrequency() directly) anywhere
    // outside the audio thread that needs this band's response.
    double getMagnitudeForFrequency (double freqHz, double sampleRate) const noexcept
    {
        return std::pow (previousCoefficients.getMagnitudeForFrequency (freqHz, sampleRate), numActiveFilterStages);
    }
    bool usesSidechain() const noexcept      { return sidechainParam->get(); }
};

//==============================================================================
// MultiModeEQAudioProcessor
//
// An N-band parametric/dynamic EQ. Each band can:
//   - run in Parametric or Dynamic mode (see EQMode)
//   - target the full signal, or just its Mid, Side, Left or Right component
//     (ChannelTarget) -- Mid/Side/Left/Right always act on the main stereo
//     pair (channels 0/1), which is the only pairing that has a well-defined
//     meaning; a "StereoOrAll" band applies independently to every channel
//     of the current bus, which is what gives surround beds full coverage.
//   - in Dynamic mode, react to its own signal or to the external sidechain
//     input bus.
//
// Surround note: true Dolby Atmos is object-based audio with bed + object
// metadata rendered by Dolby's own licensed renderer/SDK, which this project
// has no access to. What's implemented here is discrete-channel processing
// for bus layouts up to 16 channels (a 9.1.6-sized channel count), which
// covers the "process this many independent channels" part of surround
// support without claiming actual Dolby Atmos certification.
//==============================================================================
class MultiModeEQAudioProcessor : public juce::AudioProcessor
{
public:
    MultiModeEQAudioProcessor();
    ~MultiModeEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Multimode EQ"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Presets: named snapshots of the same state getStateInformation() saves,
    // stored per-user so they're available across projects/DAWs.
    //
    // Two tiers: factory presets live one folder deeper, under a category
    // subdirectory (DRUMS/Kick.xml, VOCALS/Male.xml, ...) -- see
    // getFactoryPresetCategories() -- while a preset the user saves by name
    // through the UI stays flat at the presets directory's root (unchanged
    // behaviour), so the manual save flow never needs the user to pick a
    // category. loadPreset()/presetName can name either kind: "DRUMS/Kick"
    // for a factory preset, or a plain name for one of the user's own.
    //==========================================================================
    juce::File getPresetsDirectory() const;
    juce::StringArray getAvailablePresetNames() const; // user (root-level) presets only
    void savePreset (const juce::String& presetName);
    void loadPreset (const juce::String& presetName);

    struct PresetCategory { juce::String name; juce::StringArray presetNames; };

    // One entry per factory category folder, in a fixed display order
    // (DRUMS, BASS, GUITARS, STRINGS, VOCALS, KEYS, SYNTHS, then anything
    // else alphabetically), each with its preset names sorted alphabetically.
    std::vector<PresetCategory> getFactoryPresetCategories() const;

    // Restores every parameter to the value it was created with in
    // createParameterLayout(), and clears any MIDI Learn mappings -- used by
    // the presets menu's "Default" entry, distinct from a saved/named preset.
    void resetToDefault();

    juce::AudioProcessorValueTreeState apvts;

    static constexpr int numBands = 10;
    std::array<EQBand, numBands> bands;

    // Enables/disables a band without touching its frequency/gain -- used by
    // the editor's "click empty space on the graph to add a band" gesture,
    // which enables the nearest free (disabled) slot and then spectrumGrab()s
    // it into position.
    void setBandEnabled (int bandIndex, bool shouldBeEnabled);

    //==========================================================================
    // Auto Gain: compensates output level as the aggregate EQ curve boosts or
    // cuts, by comparing a slow RMS estimate of input vs output and adapting
    // an inverse makeup gain towards their ratio.
    //==========================================================================
    juce::AudioParameterBool* autoGainParam = nullptr;

    //==========================================================================
    // Output level meter: a decaying peak-hold reading (in dB) of the final
    // output, plus a briefly-latched clip flag, both safe to poll from the
    // editor's own timer (plain atomics -- a single float/bool read/write
    // needs no additional locking).
    //==========================================================================
    float getOutputPeakDb() const noexcept { return juce::Decibels::gainToDecibels (outputPeakLinear.load(), -100.0f); }
    bool isOutputClipping() const noexcept { return clipHoldBlocksRemaining.load() > 0; }

    //==========================================================================
    // Zero Latency / Natural Phase.
    //==========================================================================
    juce::AudioParameterChoice* phaseModeParam = nullptr;
    int getReportedLatencySamples() const noexcept { return naturalPhaseLatencySamples; }

    //==========================================================================
    // Interactive MIDI Learn.
    //
    // learnArmedParamID is set by the editor when the user clicks a band's
    // "Learn" button; the next incoming MIDI CC message on any channel is
    // then bound to that parameter. Both are guarded by midiLearnLock since
    // the editor (message thread) arms/clears them while the audio thread
    // reads/updates them while scanning incoming MIDI.
    //==========================================================================
    void armMidiLearn (const juce::String& paramID);
    void clearMidiLearn (const juce::String& paramID);
    void clearAllMidiLearn();
    juce::String getMidiLearnArmedParam() const;
    juce::String getCcMappedParam (int ccNumber) const;

    //==========================================================================
    // EQ Match: analyse a reference audio file's averaged spectrum and adjust
    // enabled bands' gains so the plugin's own output spectrum approaches it.
    //==========================================================================
    void beginEqMatchCapture();      // start accumulating the live (post-EQ) output spectrum as the "current" curve
    bool loadEqMatchReferenceFile (const juce::File& file); // analyse a reference file's spectrum as the "target" curve; false if the file couldn't be read
    void applyEqMatch();             // adjust band gains so current approaches target
    void cancelEqMatch();

    // Status for the editor to poll (e.g. to show "Capturing...", "Ready").
    bool isEqMatchCapturing() const noexcept;
    bool isEqMatchCurrentReady() const noexcept; // a captured "current" average is ready to match against
    bool hasEqMatchReference() const noexcept;   // a reference file has been loaded

    //==========================================================================
    // AI Assist: analyses a short capture of the live INPUT spectrum (before
    // any of this plugin's own EQ is applied) for spectral-balance imbalance
    // and narrow resonances, then configures a handful of currently-untouched
    // bands with static Parametric moves for broad tonal correction and
    // Dynamic moves for resonance suppression.
    //
    // This is a rule-based DSP analysis built on the same FFT machinery as
    // the spectrum analyser/EQ Match above, not a neural-network model: no
    // ONNX Runtime/TensorFlow Lite dependency or trained model file is
    // bundled, since no real trained "spectral balance -> EQ curve" model was
    // available to source or train for this project. It targets the same
    // three things a model-based approach would -- spectral-balance
    // analysis, resonance detection, and adaptive (parametric + dynamic) EQ
    // moves -- while never touching a band the user has already customised
    // (see the "available band" check in applyAiAssistSuggestions()'s
    // definition) and never claiming the two bookend shelf bands.
    //==========================================================================
    void beginAiAssistAnalysis();   // start a ~2s capture of the live input spectrum
    void cancelAiAssistAnalysis();
    bool isAiAssistCapturing() const noexcept;

    // Runs the analysis on the finished capture and configures free bands
    // accordingly. Returns how many bands were touched (0 if the capture
    // isn't ready yet, or no free bands / no issues were found).
    int applyAiAssistSuggestions();

    // Reverts whatever bands the last applyAiAssistSuggestions() call
    // touched back to their prior state. Safe to call even if there's
    // nothing to undo.
    void undoLastAiAssist();

    //==========================================================================
    // Spectrum Grab: live analyser magnitude spectrum for the editor to draw,
    // plus a "grab" action that snaps a band to a clicked frequency/gain.
    //==========================================================================
    static constexpr int spectrumFftOrder = 11; // 2048-point FFT
    static constexpr int spectrumFftSize   = 1 << spectrumFftOrder;
    static constexpr int spectrumNumBins   = spectrumFftSize / 2;

    // Copies the latest smoothed magnitude spectrum (in dB) into dest[0..spectrumNumBins).
    void getSpectrumMagnitudesDb (float* dest) const;
    double getSpectrumBinFrequency (int bin) const noexcept { return (double) bin * currentSampleRate / (double) spectrumFftSize; }

    // "Grabs" the spectrum at approximately freqHz/gainDb into the given band:
    // sets that band's frequency to freqHz and gain to gainDb.
    void spectrumGrab (int bandIndex, float freqHz, float gainDb);

    // Adjusts a band's bandwidth (Q) by a mouse-wheel step -- a multiplicative
    // (log-scale) change since Q spans a wide range (0.1-10); positive
    // wheelDeltaY narrows the band (raises Q), negative widens it (lowers Q).
    void adjustBandBandwidth (int bandIndex, float wheelDeltaY);

    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    //==========================================================================
    // Split-band oscilloscope feed (unchanged visual aid from before).
    //==========================================================================
    static constexpr int numVisualiserBands = 5;
    static constexpr std::array<const char*, numVisualiserBands> bandNames { "Sub", "Low", "Mid", "High", "Air" };

    int readBandVisualiserSamples (int bandIndex, float* dest, int maxSamplesToRead) noexcept;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Ships a starting-point tonal preset for every major instrument/vocal
    // type (see the .cpp for the full list) the first time the plugin ever
    // runs -- writes them straight into getPresetsDirectory() via
    // savePreset() (same path a user's own saved preset takes), so they
    // show up in the preset dropdown alongside anything the user saves
    // afterwards. Never overwrites an existing preset directory (checked
    // via getAvailablePresetNames()), so a user who's already saved
    // anything of their own is left alone.
    void seedFactoryPresetsIfMissing();

    // Shared by getStateInformation()/setStateInformation() and the preset
    // save/load methods, so both write and read the exact same XML shape.
    std::unique_ptr<juce::XmlElement> buildStateXml();
    void applyStateXml (const juce::XmlElement& xml);

    void processBand (EQBand& band, juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechainBuffer);

    // Recomputes just band.previousCoefficients/numActiveFilterStages from
    // its current parameters, without touching any audio. Needed for bands
    // that processBand() skips this block (Natural Phase mode diverts
    // Parametric+Stereo bands to the shared linear-phase FIR instead of their
    // own IIR chain) -- otherwise those fields freeze at whatever they were
    // before the skip started, so a shape/freq/gain/Q change made while
    // skipped (e.g. switching Low Pass back to Bell) would never reach the
    // Natural Phase FIR design or the spectrum analyser's drawn curve.
    void updateBandCoefficientsOnly (EQBand& band);

    void updateNaturalPhaseFirIfNeeded();
    void applyNaturalPhaseFir (juce::AudioBuffer<float>& buffer);
    void updateAutoGain (const juce::AudioBuffer<float>& dryBuffer, juce::AudioBuffer<float>& wetBuffer);
    void updateSpectrumAnalyser (const juce::AudioBuffer<float>& buffer);
    void updateOutputLevelMeter (const juce::AudioBuffer<float>& buffer);
    void processIncomingMidi (juce::MidiBuffer& midi);

    // Output level meter state (see getOutputPeakDb()/isOutputClipping()).
    std::atomic<float> outputPeakLinear { 0.0f };
    std::atomic<int> clipHoldBlocksRemaining { 0 };

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    //==========================================================================
    // Natural Phase (linear-phase FIR) engine. Rebuilt whenever band
    // parameters change significantly; applied via FFT overlap-save.
    //==========================================================================
    static constexpr int firOrder = 10;             // 1024-point FFT used to design the kernel via frequency sampling
    static constexpr int firLength = 1 << firOrder; // 1024-tap symmetric linear-phase kernel; latency = firLength/2
    juce::dsp::FFT firDesignFft { firOrder };
    juce::dsp::FIR::Coefficients<float>::Ptr firCoefficients;
    std::atomic<bool> naturalPhaseFirDirty { true };
    juce::SpinLock naturalPhaseLock;
    std::vector<juce::dsp::FIR::Filter<float>> naturalPhaseFirFilters; // one per main-bus channel, sharing firCoefficients
    int naturalPhaseLatencySamples = 0;
    int lastReportedLatencySamples = -1;
    int samplesSinceLastFirDesign = 0; // kernel is redesigned periodically (not every block) while active

    //==========================================================================
    // Auto Gain state.
    //==========================================================================
    float autoGainInputRms = 0.0f, autoGainOutputRms = 0.0f, autoGainCurrentDb = 0.0f;

    //==========================================================================
    // MIDI Learn state.
    //==========================================================================
    mutable juce::CriticalSection midiLearnLock;
    juce::String learnArmedParamID;
    std::map<int, juce::String> ccToParamId;

    //==========================================================================
    // EQ Match state.
    //==========================================================================
    juce::dsp::FFT eqMatchFft { spectrumFftOrder };
    std::vector<float> eqMatchCurrentSpectrumDb;  // averaged from live output while capturing
    std::vector<float> eqMatchTargetSpectrumDb;   // averaged from a loaded reference file
    bool eqMatchCapturing = false;
    int eqMatchCaptureBlocks = 0;
    bool eqMatchHasTarget = false;

    // True only once a capture has actually finished averaging -- distinct
    // from eqMatchCurrentSpectrumDb's size, which is already correct the
    // instant beginEqMatchCapture() runs, well before it holds anything
    // other than partial (un-averaged) running sums. applyEqMatch() must
    // check this, not just the vector's size, or clicking Apply before the
    // ~2s capture finishes silently matches against garbage.
    bool eqMatchCurrentReady = false;

    //==========================================================================
    // AI Assist state. Uses its own FFT/fifo (rather than sharing
    // spectrumFft's) so a capture can run independently of the always-on
    // live display and EQ Match, both of which analyse post-EQ output --
    // this one deliberately captures pre-EQ input instead (see processBlock).
    //==========================================================================
    juce::dsp::FFT aiAssistFft { spectrumFftOrder };
    std::vector<float> aiAssistFifo;
    std::vector<float> aiAssistFftData;
    std::vector<float> aiAssistCapturedSpectrumDb;
    int aiAssistFifoIndex = 0;
    bool aiAssistCapturing = false;
    int aiAssistCaptureBlocks = 0;
    bool aiAssistHasCapture = false;
    mutable juce::SpinLock aiAssistLock;

    // Bands the last applyAiAssistSuggestions() call touched, so
    // undoLastAiAssist() can put them back -- message-thread only (both
    // methods are only ever called from editor button clicks), so this
    // doesn't need aiAssistLock.
    struct AiAssistTouchedBand { int bandIndex; bool wasEnabled; };
    std::vector<AiAssistTouchedBand> aiAssistLastAppliedBands;

    void updateAiAssistCapture (const juce::AudioBuffer<float>& buffer);

    //==========================================================================
    // Spectrum analyser state (feeds both the GUI display and EQ Match).
    //==========================================================================
    juce::dsp::FFT spectrumFft { spectrumFftOrder };
    std::vector<float> spectrumFifo;
    std::vector<float> spectrumFftData;
    std::vector<float> spectrumMagnitudesDb;
    int spectrumFifoIndex = 0;
    mutable juce::SpinLock spectrumLock;

    // Writes numSamples band-filtered samples into that band's ring buffer.
    void pushBandVisualiserSamples (int bandIndex, const float* samples, int numSamples) noexcept;

    static constexpr int visualiserFifoCapacity = 1 << 15;

    juce::SpinLock bandVisualiserFilterLock;
    std::array<juce::dsp::IIR::Filter<float>, numVisualiserBands> bandVisualiserFilters;
    std::array<juce::AbstractFifo, numVisualiserBands> bandFifos { juce::AbstractFifo (visualiserFifoCapacity),
                                                                    juce::AbstractFifo (visualiserFifoCapacity),
                                                                    juce::AbstractFifo (visualiserFifoCapacity),
                                                                    juce::AbstractFifo (visualiserFifoCapacity),
                                                                    juce::AbstractFifo (visualiserFifoCapacity) };
    std::array<std::vector<float>, numVisualiserBands> bandFifoBuffers;

    std::vector<float> monoScratchBuffer;
    std::array<std::vector<float>, numVisualiserBands> bandScratchBuffers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiModeEQAudioProcessor)
};
