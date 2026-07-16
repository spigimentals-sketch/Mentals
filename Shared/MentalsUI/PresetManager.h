#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace MentalsUI
{
    //==============================================================================
    // Named-preset save/load for any plugin's AudioProcessorValueTreeState,
    // stored per-user under the plugin's product name so presets are available
    // across projects/DAWs. Also doubles as the shared buildStateXml()/
    // applyStateXml() implementation for a plugin's own
    // getStateInformation()/setStateInformation(), so both paths write and
    // read the exact same XML shape.
    //
    // onBuildExtraState/onApplyExtraState let a plugin fold in state beyond
    // the plain parameter tree (e.g. MIDI Learn mappings) without this class
    // needing to know anything about it.
    //==============================================================================
    class PresetManager
    {
    public:
        PresetManager (juce::AudioProcessorValueTreeState& apvtsIn, juce::String productNameIn)
            : apvts (apvtsIn), productName (std::move (productNameIn))
        {
        }

        std::function<void (juce::XmlElement&)> onBuildExtraState;
        std::function<void (const juce::XmlElement&)> onApplyExtraState;

        juce::File getPresetsDirectory() const
        {
            auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                           .getChildFile (productName)
                           .getChildFile ("Presets");
            if (! dir.exists())
                dir.createDirectory();
            return dir;
        }

        juce::StringArray getAvailablePresetNames() const
        {
            juce::StringArray names;
            for (const auto& f : getPresetsDirectory().findChildFiles (juce::File::findFiles, false, "*.xml"))
                names.add (f.getFileNameWithoutExtension());
            names.sort (true);
            return names;
        }

        std::unique_ptr<juce::XmlElement> buildStateXml()
        {
            auto xml = std::make_unique<juce::XmlElement> ("MENTALS_STATE");
            xml->addChildElement (apvts.copyState().createXml().release());

            if (onBuildExtraState)
                onBuildExtraState (*xml);

            return xml;
        }

        void applyStateXml (const juce::XmlElement& xml)
        {
            if (auto* paramsXml = xml.getChildByName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*paramsXml));

            if (onApplyExtraState)
                onApplyExtraState (xml);
        }

        void savePreset (const juce::String& presetName)
        {
            if (presetName.isEmpty())
                return;

            if (auto xml = buildStateXml())
                xml->writeTo (getPresetsDirectory().getChildFile (presetName + ".xml"));
        }

        void loadPreset (const juce::String& presetName)
        {
            auto file = getPresetsDirectory().getChildFile (presetName + ".xml");
            if (! file.existsAsFile())
                return;

            if (auto xml = juce::XmlDocument::parse (file))
                applyStateXml (*xml);
        }

    private:
        juce::AudioProcessorValueTreeState& apvts;
        juce::String productName;
    };
}
