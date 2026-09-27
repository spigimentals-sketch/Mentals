#pragma once

#include <JuceHeader.h>
#include "LiveChannelStrip.h"
#include <array>
#include <memory>

//==============================================================================
// Owns the audio device connection and routes each input channel through its
// own LiveChannelStrip to the matching output channel. The selected device
// can be anything JUCE's AudioDeviceManager can open -- a Dante Virtual
// Soundcard, a MADI interface, or a mixer's own built-in USB multichannel
// I/O all present themselves as an ordinary ASIO/WASAPI device, so this
// class needs no protocol-specific code of its own; whatever channel count
// the chosen device actually offers (up to maxChannels) is what gets used.
//==============================================================================
class LiveAudioEngine : private juce::AudioIODeviceCallback
{
public:
    static constexpr int maxChannels = 64;

    LiveAudioEngine();
    ~LiveAudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    // How many channels are actually live right now -- the smaller of the
    // device's active input/output channel counts, capped at maxChannels.
    // 0 until a device has actually started.
    int getNumActiveChannels() const noexcept { return numActiveChannels; }

    LiveChannelStrip* getChannelStrip (int channelIndex) noexcept;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                            float* const* outputChannelData, int numOutputChannels,
                                            int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager deviceManager;
    std::array<std::unique_ptr<LiveChannelStrip>, maxChannels> channelStrips;
    std::atomic<int> numActiveChannels { 0 };
    juce::AudioBuffer<float> scratchMono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAudioEngine)
};
