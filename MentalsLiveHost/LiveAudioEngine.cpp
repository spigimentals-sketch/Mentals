#include "LiveAudioEngine.h"

//==============================================================================
LiveAudioEngine::LiveAudioEngine()
{
    for (auto& strip : channelStrips)
        strip = std::make_unique<LiveChannelStrip>();

    // Opens whatever the system's default audio device is, requesting up to
    // maxChannels each way -- the actual channel count negotiated depends on
    // what that device supports; the app's device selector lets the user
    // pick a different (e.g. Dante/MADI) device and channel count instead.
    deviceManager.initialiseWithDefaultDevices (maxChannels, maxChannels);
    deviceManager.addAudioCallback (this);
}

LiveAudioEngine::~LiveAudioEngine()
{
    deviceManager.removeAudioCallback (this);
}

LiveChannelStrip* LiveAudioEngine::getChannelStrip (int channelIndex) noexcept
{
    if (channelIndex < 0 || channelIndex >= maxChannels)
        return nullptr;
    return channelStrips[(size_t) channelIndex].get();
}

void LiveAudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sampleRate = device->getCurrentSampleRate();
    const int bufferSize = device->getCurrentBufferSizeSamples();

    const int numIn  = device->getActiveInputChannels().countNumberOfSetBits();
    const int numOut = device->getActiveOutputChannels().countNumberOfSetBits();
    numActiveChannels.store (juce::jmin (maxChannels, numIn, numOut));

    scratchMono.setSize (1, bufferSize);

    for (auto& strip : channelStrips)
        strip->prepare (sampleRate, bufferSize);
}

void LiveAudioEngine::audioDeviceStopped()
{
    numActiveChannels.store (0);
}

void LiveAudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                          float* const* outputChannelData, int numOutputChannels,
                                                          int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const int numChannelsToProcess = juce::jmin (numActiveChannels.load(), numInputChannels, numOutputChannels);

    bool anySoloed = false;
    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        if (channelStrips[(size_t) ch]->isSoloed())
        {
            anySoloed = true;
            break;
        }
    }

    for (int ch = 0; ch < numChannelsToProcess; ++ch)
    {
        if (outputChannelData[ch] == nullptr)
            continue;

        if (inputChannelData[ch] == nullptr)
        {
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
            continue;
        }

        scratchMono.setSize (1, numSamples, false, false, true);
        scratchMono.copyFrom (0, 0, inputChannelData[ch], numSamples);

        const bool isAudibleGivenSolo = ! anySoloed || channelStrips[(size_t) ch]->isSoloed();
        channelStrips[(size_t) ch]->process (scratchMono, isAudibleGivenSolo);

        juce::FloatVectorOperations::copy (outputChannelData[ch], scratchMono.getReadPointer (0), numSamples);
    }

    // Silence any output channels beyond what we processed (e.g. the device
    // has more outputs than inputs) rather than leaving stale/garbage data.
    for (int ch = numChannelsToProcess; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
}
