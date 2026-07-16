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
#include <vector>

//==============================================================================
// Mentals Suite: hosts all seven Mentals plugins, available to be added into
// one chained instance. Each module is the SAME AudioProcessor/Editor class
// its own standalone plugin uses -- not a reimplementation or duplication of
// any of their DSP -- wired in series through a juce::AudioProcessorGraph,
// which also gives latency-compensated audio routing that can be rewired
// live as the user adds, removes, or reorders modules.
//
// The chain starts EMPTY (pure passthrough) -- the user builds it up by
// adding modules one at a time via the editor's "Add" selector (see
// PluginEditor.h's ChainListComponent), rather than all seven being present
// up front. A module not currently in the chain still exists (its
// AudioProcessor node and parameters are always alive, so re-adding it
// later restores whatever state it was left in) but isn't connected into
// the audio path.
//
// Scope note: stereo only (no mono, no sidechain routing to Compressor's or
// Autotune's own optional sidechain bus -- both are left unconnected/silent
// here). Each module keeps its own full parameter set and preset system
// exactly as in its standalone plugin; Mentals Suite's own state save/load
// just wraps all seven modules' states plus the current chain membership/
// order and bypass flags into one blob (see getStateInformation()).
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
    // The seven available modules, identified by a fixed ModuleId (their
    // identity, not whether/where they currently sit in the chain -- see
    // getChainOrder() for which modules are actually in the signal path and
    // in what order).
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

    // The modules currently IN the chain, front-to-back in signal-flow order.
    // Modules not listed here exist but aren't connected into the audio path.
    std::vector<int> getChainOrder() const noexcept { return chainOrder; }

    // Reorders the chain -- newOrder must contain exactly the same set of
    // module IDs already in the chain, just in a new sequence (use
    // addModuleToChain()/removeModuleFromChain() to change membership).
    void setChainOrder (const std::vector<int>& newOrder);

    bool isModuleInChain (int moduleId) const noexcept;
    void addModuleToChain (int moduleId);
    void removeModuleFromChain (int moduleId);

    bool isModuleBypassed (int moduleId) const noexcept;
    void setModuleBypassed (int moduleId, bool shouldBeBypassed);

private:
    using Node = juce::AudioProcessorGraph::Node;

    void rebuildConnections();

    juce::AudioProcessorGraph graph;
    Node::Ptr audioInputNode, audioOutputNode, midiInputNode;
    std::array<Node::Ptr, (size_t) numModules> moduleNodes;

    std::vector<int> chainOrder; // empty until the user adds modules

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsSuiteAudioProcessor)
};
