#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"

// Reusing each plugin's own AudioProcessor unmodified -- relative includes
// (not bare "PluginProcessor.h") since every one of these files shares that
// same filename; a bare include would be ambiguous once all seven of their
// folders are on this target's include path.
#include "../MentalsMultimodeEQ/PluginProcessor.h"
#include "../MentalsDeEsser/PluginProcessor.h"
#include "../MentalsCompressor/PluginProcessor.h"
#include "../MentalsSaturator/PluginProcessor.h"
#include "../MentalsAutotune/PluginProcessor.h"
#include "../MentalsDelay/PluginProcessor.h"
#include "../MentalsReverb/PluginProcessor.h"

#include <array>

//==============================================================================
// Mentals Suite: hosts all seven Mentals plugins chained together in one
// instance. Each module is the SAME AudioProcessor/Editor class its own
// standalone plugin uses -- not a reimplementation or duplication of any of
// their DSP -- wired in series through a juce::AudioProcessorGraph, which
// also gives latency-compensated audio routing that can be rewired live
// when the user reorders the chain.
//
// Scope note: stereo only (no mono, no sidechain routing to Compressor's or
// Autotune's own optional sidechain bus -- both are left unconnected/silent
// here). Each module keeps its own full parameter set and preset system
// exactly as in its standalone plugin; Mentals Suite's own state save/load
// just wraps all seven modules' states plus the chain order and bypass
// flags into one blob (see getStateInformation()).
//==============================================================================
class MentalsSuiteAudioProcessor : public juce::AudioProcessor
{
public:
    MentalsSuiteAudioProcessor();
    ~MentalsSuiteAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mentals Suite"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // The seven chain modules, identified by a fixed ModuleId (their identity,
    // NOT their position in the signal chain -- see getChainOrder()/
    // setChainOrder() for the actual, user-rearrangeable signal-flow order).
    //==========================================================================
    enum ModuleId
    {
        moduleEQ = 0,
        moduleDeEsser,
        moduleCompressor,
        moduleSaturator,
        moduleAutotune,
        moduleDelay,
        moduleReverb,
        numModules
    };

    static const char* getModuleName (int moduleId) noexcept;

    // The actual AudioProcessor behind a module, so the editor can host its
    // existing UI directly (see PluginEditor.h).
    juce::AudioProcessor* getModuleProcessor (int moduleId) const noexcept;

    std::array<int, (size_t) numModules> getChainOrder() const noexcept { return chainOrder; }
    void setChainOrder (const std::array<int, (size_t) numModules>& newOrder);

    bool isModuleBypassed (int moduleId) const noexcept;
    void setModuleBypassed (int moduleId, bool shouldBeBypassed);

private:
    using Node = juce::AudioProcessorGraph::Node;

    void rebuildConnections();

    juce::AudioProcessorGraph graph;
    Node::Ptr audioInputNode, audioOutputNode, midiInputNode;
    std::array<Node::Ptr, (size_t) numModules> moduleNodes;

    std::array<int, (size_t) numModules> chainOrder
        { moduleEQ, moduleDeEsser, moduleCompressor, moduleSaturator, moduleAutotune, moduleDelay, moduleReverb };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsSuiteAudioProcessor)
};
