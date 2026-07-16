#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr const char* moduleNames[] = { "EQ", "De-esser", "Compressor", "Saturator", "Autotune", "Delay", "Reverb" };
}

const char* MentalsSuiteAudioProcessor::getModuleName (int moduleId) noexcept
{
    return moduleNames[(size_t) moduleId];
}

MentalsSuiteAudioProcessor::MentalsSuiteAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    moduleNodes[(size_t) moduleEQ]         = graph.addNode (std::make_unique<MultiModeEQAudioProcessor>());
    moduleNodes[(size_t) moduleDeEsser]    = graph.addNode (std::make_unique<MentalsDeEsserAudioProcessor>());
    moduleNodes[(size_t) moduleCompressor] = graph.addNode (std::make_unique<MentalsCompressorAudioProcessor>());
    moduleNodes[(size_t) moduleSaturator]  = graph.addNode (std::make_unique<MentalsSaturatorAudioProcessor>());
    moduleNodes[(size_t) moduleAutotune]   = graph.addNode (std::make_unique<MentalsAutotuneAudioProcessor>());
    moduleNodes[(size_t) moduleDelay]      = graph.addNode (std::make_unique<MentalsDelayAudioProcessor>());
    moduleNodes[(size_t) moduleReverb]     = graph.addNode (std::make_unique<MentalsReverbAudioProcessor>());

    audioInputNode  = graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    audioOutputNode = graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    midiInputNode   = graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
        juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));

    // MIDI fan-out: only the EQ (MIDI Learn) and Autotune (MIDI Control) ever
    // read incoming MIDI, and that doesn't depend on where they sit in the
    // reorderable audio chain, so it's wired once here and left alone.
    graph.addConnection ({ { midiInputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                            { moduleNodes[(size_t) moduleEQ]->nodeID, juce::AudioProcessorGraph::midiChannelIndex } });
    graph.addConnection ({ { midiInputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                            { moduleNodes[(size_t) moduleAutotune]->nodeID, juce::AudioProcessorGraph::midiChannelIndex } });
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

juce::AudioProcessor* MentalsSuiteAudioProcessor::getModuleProcessor (int moduleId) const noexcept
{
    return moduleNodes[(size_t) moduleId]->getProcessor();
}

void MentalsSuiteAudioProcessor::setChainOrder (const std::array<int, (size_t) numModules>& newOrder)
{
    chainOrder = newOrder;
    rebuildConnections();
}

bool MentalsSuiteAudioProcessor::isModuleBypassed (int moduleId) const noexcept
{
    return moduleNodes[(size_t) moduleId]->isBypassed();
}

void MentalsSuiteAudioProcessor::setModuleBypassed (int moduleId, bool shouldBeBypassed)
{
    moduleNodes[(size_t) moduleId]->setBypassed (shouldBeBypassed);
}

void MentalsSuiteAudioProcessor::rebuildConnections()
{
    // Guards against the audio thread running processBlock() (which reads
    // the graph's current topology) while this rewires it -- the same lock
    // JUCE hosts hold around calling processBlock().
    const juce::ScopedLock sl (getCallbackLock());

    // Clear only the AUDIO connections (channelIndex != midiChannelIndex);
    // the fixed MIDI fan-out set up in the constructor is left alone. Copy
    // the connection list first since removeConnection() mutates the same
    // list this would otherwise be iterating.
    const auto existingConnections = graph.getConnections();
    for (const auto& connection : existingConnections)
        if (connection.source.channelIndex != juce::AudioProcessorGraph::midiChannelIndex)
            graph.removeConnection (connection);

    auto connectStereo = [this] (juce::AudioProcessorGraph::NodeID from, juce::AudioProcessorGraph::NodeID to)
    {
        for (int ch = 0; ch < 2; ++ch) // stereo only -- see class comment
            graph.addConnection ({ { from, ch }, { to, ch } });
    };

    auto previous = audioInputNode->nodeID;
    for (int moduleId : chainOrder)
    {
        connectStereo (previous, moduleNodes[(size_t) moduleId]->nodeID);
        previous = moduleNodes[(size_t) moduleId]->nodeID;
    }
    connectStereo (previous, audioOutputNode->nodeID);

    // The graph handles latency-compensation delay lines internally based on
    // each node's own reported latency (Multimode EQ's Natural Phase mode
    // and Autotune's Formant Preservation both report latency only when
    // that toggle is on); the total is only re-read here, at topology-change
    // time, not continuously -- toggling one of those live mid-playback
    // without also changing the chain order won't retrigger this.
    setLatencySamples (graph.getLatencySamples());
}

void MentalsSuiteAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("MentalsSuiteState");

    juce::Array<juce::var> orderArray;
    for (int moduleId : chainOrder)
        orderArray.add (moduleId);
    state.setProperty ("chainOrder", orderArray, nullptr);

    for (int moduleId = 0; moduleId < (int) numModules; ++moduleId)
    {
        juce::ValueTree moduleState ("Module");
        moduleState.setProperty ("id", moduleId, nullptr);
        moduleState.setProperty ("bypassed", isModuleBypassed (moduleId), nullptr);

        juce::MemoryBlock innerBlock;
        getModuleProcessor (moduleId)->getStateInformation (innerBlock);
        moduleState.setProperty ("state", innerBlock.toBase64Encoding(), nullptr);

        state.appendChild (moduleState, nullptr);
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

    std::array<int, (size_t) numModules> newOrder = chainOrder;
    const auto orderVar = state.getProperty ("chainOrder");
    if (orderVar.isArray())
    {
        auto& arr = *orderVar.getArray();
        for (int i = 0; i < juce::jmin ((int) numModules, arr.size()); ++i)
            newOrder[(size_t) i] = (int) arr.getUnchecked (i);
    }

    for (const auto& moduleState : state)
    {
        const int moduleId = moduleState.getProperty ("id", -1);
        if (moduleId < 0 || moduleId >= (int) numModules)
            continue;

        setModuleBypassed (moduleId, moduleState.getProperty ("bypassed", false));

        juce::MemoryBlock innerBlock;
        innerBlock.fromBase64Encoding (moduleState.getProperty ("state").toString());
        if (innerBlock.getSize() > 0)
            getModuleProcessor (moduleId)->setStateInformation (innerBlock.getData(), (int) innerBlock.getSize());
    }

    setChainOrder (newOrder);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MentalsSuiteAudioProcessor();
}
