#include "PluginProcessor.h"

// The actual plugin-format entry point JUCE's wrapper code calls -- kept
// out of PluginProcessor.cpp (which lives in MentalsTriggerCore, shared
// with Mentals Suite) since every one of these plugins' Core libraries
// would otherwise each define this exact free function, colliding once
// Suite links them all into one binary.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MentalsTriggerAudioProcessor();
}
