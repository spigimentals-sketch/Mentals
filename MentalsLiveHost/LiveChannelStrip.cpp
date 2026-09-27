#include "LiveChannelStrip.h"

//==============================================================================
LiveChannelStrip::LiveChannelStrip() = default;
LiveChannelStrip::~LiveChannelStrip() = default;

void LiveChannelStrip::prepare (double sampleRate, int samplesPerBlock)
{
    // Left in the insert chain's normal (stereo) default bus layout -- no
    // setBusesLayout() call, matching exactly how Mentals Suite already
    // runs successfully hosted inside a DAW.
    insertChain.prepareToPlay (sampleRate, samplesPerBlock);
    scratchStereo.setSize (2, samplesPerBlock);
}

void LiveChannelStrip::process (juce::AudioBuffer<float>& monoBuffer, bool isAudibleGivenSolo)
{
    const int numSamples = monoBuffer.getNumSamples();
    scratchStereo.setSize (2, numSamples, false, false, true);
    scratchStereo.copyFrom (0, 0, monoBuffer, 0, 0, numSamples);
    scratchStereo.copyFrom (1, 0, monoBuffer, 0, 0, numSamples);

    scratchMidi.clear();
    insertChain.processBlock (scratchStereo, scratchMidi);

    monoBuffer.copyFrom (0, 0, scratchStereo, 0, 0, numSamples);
    monoBuffer.applyGain (juce::Decibels::decibelsToGain (faderGainDb.load()));

    // Metering reflects the fader (a real mixer's channel meter usually
    // sits post-fader), but stays live even while muted/not-soloed -- only
    // the buffer itself gets silenced below -- so cueing a muted channel's
    // meter still shows real activity rather than looking dead/broken.
    outputPeak.store (monoBuffer.getMagnitude (0, 0, numSamples));

    if (muted.load() || ! isAudibleGivenSolo)
        monoBuffer.clear();
}
