#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

namespace
{
    constexpr const char* moduleTypeNames[] = { "EQ", "De-esser", "Compressor", "Saturator", "Autotune", "Delay", "Reverb", "Limiter", "Gate", "Chorus", "Vox Choir", "360 Stereo Shaper", "Exciter EQ", "Circuit Comp", "Mastering Meter" };
}

const char* MentalsSuiteAudioProcessor::getModuleTypeName (int moduleType) noexcept
{
    return moduleTypeNames[(size_t) moduleType];
}

std::unique_ptr<juce::AudioProcessor> MentalsSuiteAudioProcessor::createModuleProcessor (int moduleType)
{
    switch (moduleType)
    {
        case moduleEQ:         return std::make_unique<MultiModeEQAudioProcessor>();
        case moduleDeEsser:    return std::make_unique<MentalsDeEsserAudioProcessor>();
        case moduleCompressor: return std::make_unique<MentalsCompressorAudioProcessor>();
        case moduleSaturator:  return std::make_unique<MentalsSaturatorAudioProcessor>();
        case moduleAutotune:   return std::make_unique<MentalsAutotuneAudioProcessor>();
        case moduleDelay:      return std::make_unique<MentalsDelayAudioProcessor>();
        case moduleReverb:     return std::make_unique<MentalsReverbAudioProcessor>();
        case moduleLimiter:    return std::make_unique<MentalsLimiterAudioProcessor>();
        case moduleGate:       return std::make_unique<MentalsGateAudioProcessor>();
        case moduleChorus:     return std::make_unique<MentalsChorusAudioProcessor>();
        case moduleVoxChoir:   return std::make_unique<MentalsVoxChoirAudioProcessor>();
        case moduleStereoShaper: return std::make_unique<MentalsStereoShaperAudioProcessor>();
        case moduleExciterEQ:  return std::make_unique<MentalsExciterEQAudioProcessor>();
        case moduleCircuitComp: return std::make_unique<MentalsCircuitCompAudioProcessor>();
        case moduleMasteringMeter: return std::make_unique<MentalsMasteringMeterAudioProcessor>();
        default:               jassertfalse; return nullptr;
    }
}

MentalsSuiteAudioProcessor::MentalsSuiteAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    audioInputNode  = graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    audioOutputNode = graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    midiInputNode   = graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
        juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));
}

void MentalsSuiteAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    graph.setPlayConfigDetails (getMainBusNumInputChannels(), getMainBusNumOutputChannels(), sampleRate, samplesPerBlock);
    graph.prepareToPlay (sampleRate, samplesPerBlock);
    rebuildConnections();
}

void MentalsSuiteAudioProcessor::releaseResources()
{
    graph.releaseResources();
}

bool MentalsSuiteAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MentalsSuiteAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    graph.processBlock (buffer, midi);
}

juce::AudioProcessorEditor* MentalsSuiteAudioProcessor::createEditor()
{
    return new MentalsSuiteAudioProcessorEditor (*this);
}

std::vector<MentalsSuiteAudioProcessor::ChainSlot> MentalsSuiteAudioProcessor::getChainSlots() const
{
    std::vector<ChainSlot> result;
    result.reserve (slots.size());
    for (auto& slot : slots)
        result.push_back ({ slot.slotId, slot.moduleType });
    return result;
}

MentalsSuiteAudioProcessor::Slot* MentalsSuiteAudioProcessor::findSlot (int slotId) noexcept
{
    for (auto& slot : slots)
        if (slot.slotId == slotId)
            return &slot;
    return nullptr;
}

juce::AudioProcessor* MentalsSuiteAudioProcessor::getSlotProcessor (int slotId) const noexcept
{
    for (auto& slot : slots)
        if (slot.slotId == slotId)
            return slot.node->getProcessor();
    return nullptr;
}

void MentalsSuiteAudioProcessor::setChainOrder (const std::vector<int>& newSlotOrder)
{
    std::vector<Slot> reordered;
    reordered.reserve (newSlotOrder.size());
    for (int slotId : newSlotOrder)
        if (auto* slot = findSlot (slotId))
            reordered.push_back (*slot);

    slots = std::move (reordered);
    rebuildConnections();
}

int MentalsSuiteAudioProcessor::addModuleToChain (int moduleType)
{
    auto node = graph.addNode (createModuleProcessor (moduleType));
    const int slotId = nextSlotId++;
    slots.push_back ({ slotId, moduleType, node });
    rebuildConnections();
    return slotId;
}

void MentalsSuiteAudioProcessor::removeModuleFromChain (int slotId)
{
    auto it = std::find_if (slots.begin(), slots.end(), [slotId] (const Slot& s) { return s.slotId == slotId; });
    if (it == slots.end())
        return;

    graph.removeNode (it->node->nodeID);
    slots.erase (it);
    rebuildConnections();
}

bool MentalsSuiteAudioProcessor::isSlotBypassed (int slotId) const noexcept
{
    for (auto& slot : slots)
        if (slot.slotId == slotId)
            return slot.node->isBypassed();
    return false;
}

void MentalsSuiteAudioProcessor::setSlotBypassed (int slotId, bool shouldBeBypassed)
{
    for (auto& slot : slots)
        if (slot.slotId == slotId)
            slot.node->setBypassed (shouldBeBypassed);
}

void MentalsSuiteAudioProcessor::rebuildConnections()
{
    // Guards against the audio thread running processBlock() (which reads
    // the graph's current topology) while this rewires it -- the same lock
    // JUCE hosts hold around calling processBlock().
    const juce::ScopedLock sl (getCallbackLock());

    // Clear every existing connection (audio AND MIDI) and rebuild both from
    // scratch below -- simpler than trying to patch around whichever slots
    // changed, and cheap enough given how rarely this runs (only on
    // add/remove/reorder, never per-block).
    const auto existingConnections = graph.getConnections();
    for (const auto& connection : existingConnections)
        graph.removeConnection (connection);

    auto connectStereo = [this] (juce::AudioProcessorGraph::NodeID from, juce::AudioProcessorGraph::NodeID to)
    {
        for (int ch = 0; ch < 2; ++ch) // stereo only -- see class comment
            graph.addConnection ({ { from, ch }, { to, ch } });
    };

    auto previous = audioInputNode->nodeID;
    for (auto& slot : slots)
    {
        connectStereo (previous, slot.node->nodeID);
        previous = slot.node->nodeID;
    }
    connectStereo (previous, audioOutputNode->nodeID);

    // MIDI fan-out: every currently-present EQ (MIDI Learn) or Autotune
    // (MIDI Control) instance reads incoming MIDI directly, regardless of
    // its position in the audio chain -- recomputed here since which slots
    // exist can change at any time now that instances can be added/removed.
    for (auto& slot : slots)
        if (slot.moduleType == moduleEQ || slot.moduleType == moduleAutotune)
            graph.addConnection ({ { midiInputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                                    { slot.node->nodeID, juce::AudioProcessorGraph::midiChannelIndex } });

    // The graph handles latency-compensation delay lines internally based on
    // each node's own reported latency (Multimode EQ's Natural Phase mode
    // and Autotune's Formant Preservation both report latency only when
    // that toggle is on); the total is only re-read here, at topology-change
    // time, not continuously -- toggling one of those live mid-playback
    // without also changing the chain doesn't retrigger this.
    setLatencySamples (graph.getLatencySamples());
}

void MentalsSuiteAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("MentalsSuiteState");

    for (auto& slot : slots)
    {
        juce::ValueTree slotState ("Slot");
        slotState.setProperty ("moduleType", slot.moduleType, nullptr);
        slotState.setProperty ("bypassed", slot.node->isBypassed(), nullptr);

        juce::MemoryBlock innerBlock;
        slot.node->getProcessor()->getStateInformation (innerBlock);
        slotState.setProperty ("state", innerBlock.toBase64Encoding(), nullptr);

        state.appendChild (slotState, nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MentalsSuiteAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid())
        return;

    for (auto& slot : slots)
        graph.removeNode (slot.node->nodeID);
    slots.clear();

    for (const auto& slotState : state)
    {
        const int moduleType = slotState.getProperty ("moduleType", -1);
        if (moduleType < 0 || moduleType >= (int) numModuleTypes)
            continue;

        auto node = graph.addNode (createModuleProcessor (moduleType));
        node->setBypassed (slotState.getProperty ("bypassed", false));

        juce::MemoryBlock innerBlock;
        innerBlock.fromBase64Encoding (slotState.getProperty ("state").toString());
        if (innerBlock.getSize() > 0)
            node->getProcessor()->setStateInformation (innerBlock.getData(), (int) innerBlock.getSize());

        slots.push_back ({ nextSlotId++, moduleType, node });
    }

    rebuildConnections();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MentalsSuiteAudioProcessor();
}
