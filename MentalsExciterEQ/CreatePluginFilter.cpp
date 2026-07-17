#include "PluginProcessor.h"

// Isolated into the plugin-wrapper target only (never MentalsExciterEQCore)
// so Mentals Suite can link this Core library alongside every other
// plugin's without colliding createPluginFilter() symbols.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MentalsExciterEQAudioProcessor();
}
