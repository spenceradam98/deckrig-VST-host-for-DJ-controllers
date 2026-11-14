#include "PluginChain.h"

namespace deckrig
{
using Graph = juce::AudioProcessorGraph;
using IOProc = Graph::AudioGraphIOProcessor;

PluginLibrary::PluginLibrary()
{
    formatManager.addDefaultFormats();
    if (auto xml = juce::XmlDocument::parse (getPluginListFile()))
        knownPlugins.recreateFromXml (*xml);
}

PluginChain::PluginChain (PluginLibrary& lib) : library (lib)
{

    graph.setPlayConfigDetails (2, 2, currentRate, currentBlock);
    inputNode = graph.addNode (std::make_unique<IOProc> (IOProc::audioInputNode));
    outputNode = graph.addNode (std::make_unique<IOProc> (IOProc::audioOutputNode));
    midiInNode = graph.addNode (std::make_unique<IOProc> (IOProc::midiInputNode));
    rewire();
}

PluginChain::~PluginChain()
{
    graph.clear();
}

juce::File PluginLibrary::getPluginListFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (DECKRIG_BRAND_NAME).getChildFile (DECKRIG_PRODUCT_NAME).getChildFile ("KnownPlugins.xml");
}

juce::File PluginLibrary::getDeadMansPedalFile() const
{
    return getPluginListFile().getSiblingFile ("ScanInProgress.txt");
}

void PluginLibrary::save() const
{
    if (auto xml = knownPlugins.createXml())
    {
        getPluginListFile().getParentDirectory().createDirectory();
        xml->writeTo (getPluginListFile());
    }
}

void PluginChain::prepare (double sampleRate, int blockSize, int numChannels)
{
    currentRate = sampleRate;
    currentBlock = blockSize;
    currentChannels = juce::jmax (1, numChannels);
    graph.setPlayConfigDetails (currentChannels, currentChannels, sampleRate, blockSize);
    graph.prepareToPlay (sampleRate, blockSize);
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstance()->isThisTheMessageThread())
        rewire();
}

void PluginChain::release()
{
    graph.releaseResources();
}

void PluginChain::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    graph.processBlock (buffer, midi);
}

void PluginChain::configureBuses (juce::AudioPluginInstance& p) const
{
    // Prefer a layout matching ours on the main buses; leave sidechains alone.
    auto layout = p.getBusesLayout();
    const auto set = juce::AudioChannelSet::canonicalChannelSet (currentChannels);
    if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0) = set;
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = set;
    if (p.checkBusesLayoutSupported (layout))
        p.setBusesLayout (layout);
}

bool PluginChain::addPlugin (const juce::PluginDescription& desc, juce::String& error)
{
    auto instance = library.formatManager.createPluginInstance (desc, currentRate, currentBlock, error);
    if (instance == nullptr)
    {
        if (error.isEmpty()) error = "Could not load " + desc.name;
        return false;
    }

    configureBuses (*instance);
    auto node = graph.addNode (std::move (instance));
    if (node == nullptr)
    {
        error = "Could not add " + desc.name + " to the chain";
        return false;
    }

    slots.push_back ({ node });
    rewire();
    sendChangeMessage();
    return true;
}

void PluginChain::removePlugin (int index)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return;
    auto id = slots[(size_t) index].node->nodeID;
    slots.erase (slots.begin() + index);
    graph.removeNode (id);
    rewire();
    sendChangeMessage();
}

void PluginChain::movePlugin (int from, int to)
{
    if (! juce::isPositiveAndBelow (from, size()) || ! juce::isPositiveAndBelow (to, size()) || from == to)
        return;
    auto s = slots[(size_t) from];
    slots.erase (slots.begin() + from);
    slots.insert (slots.begin() + to, s);
    rewire();
    sendChangeMessage();
}

void PluginChain::setBypassed (int index, bool bypassed)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return;
    slots[(size_t) index].node->setBypassed (bypassed);
    sendChangeMessage();
}

bool PluginChain::isBypassed (int index) const
{
    return juce::isPositiveAndBelow (index, size()) && slots[(size_t) index].node->isBypassed();
}

void PluginChain::clear()
{
    for (auto& s : slots)
        graph.removeNode (s.node->nodeID);
    slots.clear();
    rewire();
    sendChangeMessage();
}

juce::AudioPluginInstance* PluginChain::getPlugin (int index) const
{
    if (! juce::isPositiveAndBelow (index, size()))
        return nullptr;
    return dynamic_cast<juce::AudioPluginInstance*> (slots[(size_t) index].node->getProcessor());
}

void PluginChain::connect (Graph::NodeID src, int srcChans, Graph::NodeID dst, int dstChans)
{
    if (srcChans <= 0 || dstChans <= 0)
        return;
    // Fan out mono to stereo, sum stereo into mono.
    for (int i = 0; i < juce::jmax (srcChans, dstChans); ++i)
        graph.addConnection ({ { src, i % srcChans }, { dst, i % dstChans } });
}

void PluginChain::rewire()
{
    for (auto& c : graph.getConnections())
        graph.removeConnection (c);

    auto prevId = inputNode->nodeID;
    int prevChans = inputNode->getProcessor()->getTotalNumOutputChannels();
    bool havePrev = true;

    for (auto& s : slots)
    {
        auto* p = s.node->getProcessor();
        const int ins = p->getMainBusNumInputChannels();
        const int outs = p->getMainBusNumOutputChannels();

        // Pads and other deck MIDI go to every plugin that wants MIDI.
        if (p->acceptsMidi())
            graph.addConnection ({ { midiInNode->nodeID, Graph::midiChannelIndex }, { s.node->nodeID, Graph::midiChannelIndex } });

        if (outs == 0)
            continue; // MIDI-only plugin: no audio to pass on.
        if (ins == 0)
        {
            // Instrument: it becomes the deck's sound source. The deck's live input
            // is mixed in alongside it, so a synth and a line input can share a deck.
            if (havePrev && prevId != inputNode->nodeID)
                connect (prevId, prevChans, outputNode->nodeID, outputNode->getProcessor()->getTotalNumInputChannels());
            connect (inputNode->nodeID, inputNode->getProcessor()->getTotalNumOutputChannels(),
                     outputNode->nodeID, outputNode->getProcessor()->getTotalNumInputChannels());
            prevId = s.node->nodeID;
            prevChans = outs;
            havePrev = true;
            continue;
        }
        connect (prevId, prevChans, s.node->nodeID, ins);
        prevId = s.node->nodeID;
        prevChans = outs;
    }

    connect (prevId, prevChans, outputNode->nodeID, outputNode->getProcessor()->getTotalNumInputChannels());
}

std::unique_ptr<juce::XmlElement> PluginChain::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("CHAIN");
    for (auto& s : slots)
    {
        auto* p = dynamic_cast<juce::AudioPluginInstance*> (s.node->getProcessor());
        if (p == nullptr)
            continue;
        auto* e = xml->createNewChildElement ("SLOT");
        e->setAttribute ("bypassed", s.node->isBypassed());
        e->addChildElement (p->getPluginDescription().createXml().release());
        juce::MemoryBlock state;
        p->getStateInformation (state);
        e->createNewChildElement ("STATE")->addTextElement (state.toBase64Encoding());
    }
    return xml;
}

void PluginChain::fromXml (const juce::XmlElement& xml)
{
    clear();
    for (auto* e : xml.getChildWithTagNameIterator ("SLOT"))
    {
        juce::PluginDescription desc;
        auto* descXml = e->getChildByName ("PLUGIN");
        if (descXml == nullptr || ! desc.loadFromXml (*descXml))
            continue;

        juce::String error;
        if (! addPlugin (desc, error))
        {
            DBG ("Chain restore: " << error);
            continue;
        }

        if (auto* stateXml = e->getChildByName ("STATE"))
        {
            juce::MemoryBlock state;
            if (state.fromBase64Encoding (stateXml->getAllSubText()))
                getPlugin (size() - 1)->setStateInformation (state.getData(), (int) state.getSize());
        }
        setBypassed (size() - 1, e->getBoolAttribute ("bypassed"));
    }
}
} // namespace deckrig
