#include "PluginProcessor.h"

// The actual plugin-format entry point JUCE's wrapper code calls -- kept
// out of PluginProcessor.cpp (which lives in MentalsSuiteCore, shared with
// Mentals Live Host, which hosts one MentalsSuiteAudioProcessor per live
// mixer channel as that channel's insert chain) since every one of these
// plugins' Core libraries would otherwise each define this exact free
// function, colliding once something links more than one of them together.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MentalsSuiteAudioProcessor();
}
