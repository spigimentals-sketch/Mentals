#pragma once

#include <JuceHeader.h>
#include "MentalsUI.h"

// Reusing each plugin's own AudioProcessor unmodified -- relative includes
// (not bare "PluginProcessor.h") since every one of these files shares that
// same filename; a bare include would be ambiguous once all of their
// folders are on this target's include path.
#include "../MentalsMultimodeEQ/PluginProcessor.h"
#include "../MentalsDeEsser/PluginProcessor.h"
#include "../MentalsCompressor/PluginProcessor.h"
#include "../MentalsSaturator/PluginProcessor.h"
#include "../MentalsAutotune/PluginProcessor.h"
#include "../MentalsDelay/PluginProcessor.h"
#include "../MentalsReverb/PluginProcessor.h"
#include "../MentalsLimiter/PluginProcessor.h"
#include "../MentalsGate/PluginProcessor.h"
#include "../MentalsChorus/PluginProcessor.h"
#include "../MentalsVoxChoir/PluginProcessor.h"
#include "../MentalsStereoShaper/PluginProcessor.h"
#include "../MentalsExciterEQ/PluginProcessor.h"
#include "../MentalsCircuitComp/PluginProcessor.h"
#include "../MentalsMasteringMeter/PluginProcessor.h"
#include "../MentalsChannelStrip/PluginProcessor.h"
#include "../MentalsMaximizer/PluginProcessor.h"
#include "../MentalsDoubler/PluginProcessor.h"
#include "../MentalsImager/PluginProcessor.h"
#include "MasterAssistant.h"

#include <memory>
#include <vector>

//==============================================================================
// Mentals Suite: hosts any number of Mentals plugin instances, chained
// together in one instance. Each is the SAME AudioProcessor/Editor class its
// own standalone plugin uses -- not a reimplementation or duplication of any
// of their DSP -- wired in series through a juce::AudioProcessorGraph, which
// also gives latency-compensated audio routing that can be rewired live as
// the user adds, removes, or reorders modules.
//
// The chain starts EMPTY (pure passthrough) -- the user builds it up by
// adding modules one at a time via the editor's "+ Add Module" selector (see
// PluginEditor.h's ChainListComponent). Any plugin type can be added more
// than once (e.g. two EQ instances at different points in the
// chain) -- each addModuleToChain() call creates a genuinely new
// AudioProcessor instance with its own independent parameters/state, not a
// shared one. Each instance in the chain is identified by a "slot": a
// small, stable integer handle assigned when it's added, used everywhere
// (selection, reordering, removal) instead of the plugin type, since a type
// alone no longer uniquely identifies which instance is meant once
// duplicates exist. Removing a slot destroys that specific instance and its
// state for good (unlike a plain bypass, which leaves it in the chain,
// silently passed through).
//
// Scope note: stereo only (no mono, no sidechain routing to Compressor's or
// Autotune's own optional sidechain bus -- both are left unconnected/silent
// here). Each module keeps its own full parameter set and preset system
// exactly as in its standalone plugin; Mentals Suite's own state save/load
// just wraps every slot's module type, state, and bypass flag, in chain
// order, into one blob (see getStateInformation()).
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
    // The pluggable module types. Any of them can appear zero, one, or
    // several times in the chain -- see the class comment.
    //==========================================================================
    enum ModuleType
    {
        moduleEQ = 0,
        moduleDeEsser,
        moduleCompressor,
        moduleSaturator,
        moduleAutotune,
        moduleDelay,
        moduleReverb,
        moduleLimiter,
        moduleGate,
        moduleChorus,
        moduleVoxChoir,
        moduleStereoShaper,
        moduleExciterEQ,
        moduleCircuitComp,
        moduleMasteringMeter,
        moduleChannelStrip,
        moduleMaximizer,
        moduleDoubler,
        moduleImager,
        numModuleTypes
    };

    static const char* getModuleTypeName (int moduleType) noexcept;

    // One instance currently in the chain: a stable slotId (identity, for
    // selection/reordering/removal) plus which ModuleType it is.
    struct ChainSlot
    {
        int slotId;
        int moduleType;
    };

    // The chain's current contents, front-to-back in signal-flow order.
    std::vector<ChainSlot> getChainSlots() const;

    // The actual AudioProcessor behind a slot, so the editor can host its
    // existing UI directly (see PluginEditor.h). nullptr if slotId doesn't
    // (or no longer) exists.
    juce::AudioProcessor* getSlotProcessor (int slotId) const noexcept;

    // Reorders the chain -- newSlotOrder must contain exactly the slot IDs
    // already in the chain, just in a new sequence.
    void setChainOrder (const std::vector<int>& newSlotOrder);

    // Appends a brand-new instance of moduleType to the end of the chain and
    // returns its freshly assigned slotId.
    int addModuleToChain (int moduleType);

    // Removes and destroys the instance at slotId (and its state) for good.
    void removeModuleFromChain (int slotId);

    bool isSlotBypassed (int slotId) const noexcept;
    void setSlotBypassed (int slotId, bool shouldBeBypassed);

    // Ozone-style mastering-by-reference, orchestrating the chain's own
    // modules -- see MasterAssistant.h's class comment.
    MasterAssistant& getMasterAssistant() noexcept { return masterAssistant; }

private:
    using Node = juce::AudioProcessorGraph::Node;

    struct Slot
    {
        int slotId;
        int moduleType;
        Node::Ptr node;
    };

    static std::unique_ptr<juce::AudioProcessor> createModuleProcessor (int moduleType);
    Slot* findSlot (int slotId) noexcept;
    void rebuildConnections();

    juce::AudioProcessorGraph graph;
    Node::Ptr audioInputNode, audioOutputNode, midiInputNode;

    std::vector<Slot> slots; // empty until the user adds modules; front-to-back signal order
    int nextSlotId = 0;

    MasterAssistant masterAssistant { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalsSuiteAudioProcessor)
};
