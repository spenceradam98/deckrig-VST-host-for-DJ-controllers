#include "Deck.h"

namespace deckrig
{
Deck::Deck (int i, PluginLibrary& lib) : index (i), chain (lib) {}

void Deck::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    buffer.setSize (2, maxBlock, false, true, false);
    midi.ensureSize (2048);
    chain.prepare (sr, maxBlock, 2);
    player.prepare (sr);
    midiIn.reset (sr);
    low.reset(); mid.reset(); high.reset(); filt.reset();
    lastLow = lastMid = lastHigh = lastFilter = -1;
    trim.reset (sr, 0.02);
    trim.setCurrentAndTargetValue (dbToGain (gainKnobToDb (params.gain.load())));
}

void Deck::release()
{
    chain.release();
}

void Deck::updateFilters()
{
    const float l = params.eqLow.load(), m = params.eqMid.load(), h = params.eqHigh.load(), f = params.filter.load();
    // DJ-style isolator-ish EQ: shelves at 200 Hz / 4 kHz, broad mid at 1 kHz.
    if (l != lastLow)  { low.lowShelf (sampleRate, 200.0, std::max (-60.0f, eqKnobToDb (l))); lastLow = l; }
    if (m != lastMid)  { mid.peak (sampleRate, 1000.0, std::max (-60.0f, eqKnobToDb (m)), 0.6); lastMid = m; }
    if (h != lastHigh) { high.highShelf (sampleRate, 4000.0, std::max (-60.0f, eqKnobToDb (h))); lastHigh = h; }
    if (f != lastFilter)
    {
        bool lp = false, active = false;
        const double fc = std::min (filterCutoff (f, lp, active), sampleRate * 0.45);
        if (active)
        {
            if (lp) filt.lowPass (sampleRate, fc, 0.9); else filt.highPass (sampleRate, fc, 0.9);
        }
        if (active && ! filterOn) filt.reset();
        filterOn = active;
        lastFilter = f;
    }
}

void Deck::process (const float* const* inputs, int numInputs, int n, const Deck* other)
{
    jassert (n <= buffer.getNumSamples());
    buffer.clear (0, n);

    const int pair = params.inputPair.load();
    if (pair >= 0 && inputs != nullptr)
    {
        const int l = pair * 2, r = l + 1;
        if (l < numInputs && inputs[l] != nullptr)
        {
            buffer.copyFrom (0, 0, inputs[l], n);
            buffer.copyFrom (1, 0, (r < numInputs && inputs[r] != nullptr) ? inputs[r] : inputs[l], n);
        }
    }

    {
        float tempo = params.tempo.load();
        const float range = tempoRanges[juce::jlimit (0, 2, params.tempoRange.load())];
        player.render (buffer, n, tempo, range, other != nullptr ? &other->player : nullptr);
        if (player.synced) params.tempo = tempo;
    }

    midi.clear();
    midiIn.removeNextBlockOfMessages (midi, n);

    if (! params.chainBypass.load())
    {
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
        chain.process (view, midi);
    }

    updateFilters();
    trim.setTargetValue (dbToGain (gainKnobToDb (params.gain.load())));
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float g = trim.getNextValue();
        float x[2] = { L[i] * g, R[i] * g };
        for (int c = 0; c < 2; ++c)
        {
            float y = low.process (x[c], c);
            y = mid.process (y, c);
            y = high.process (y, c);
            if (filterOn) y = filt.process (y, c);
            x[c] = y;
        }
        L[i] = x[0];
        R[i] = x[1];
    }
}

juce::AudioProcessorParameter* Deck::getMacroParameter (int m) const
{
    if (! juce::isPositiveAndBelow (m, numMacros) || ! macros[(size_t) m].isAssigned())
        return nullptr;
    auto& a = macros[(size_t) m];
    if (auto* p = chain.getPlugin (a.slot))
    {
        auto& ps = p->getParameters();
        if (juce::isPositiveAndBelow (a.param, ps.size()))
            return ps[a.param];
    }
    return nullptr;
}

float Deck::getMacroValue (int m) const
{
    auto* p = getMacroParameter (m);
    return p != nullptr ? p->getValue() : 0.0f;
}

void Deck::setMacroValue (int m, float v)
{
    if (auto* p = getMacroParameter (m))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}

void Deck::assignMacro (int m, int slot, int param)
{
    if (! juce::isPositiveAndBelow (m, numMacros))
        return;
    auto& a = macros[(size_t) m];
    a.slot = slot; a.param = param; a.label = {};
    if (auto* p = getMacroParameter (m))
        a.label = chain.getPlugin (slot)->getName() + ": " + p->getName (40);
    else
        a.slot = a.param = -1;
}

void Deck::validateMacros()
{
    for (int m = 0; m < numMacros; ++m)
        if (macros[(size_t) m].isAssigned())
            assignMacro (m, macros[(size_t) m].slot, macros[(size_t) m].param);
}

std::unique_ptr<juce::XmlElement> Deck::createRigXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("DECKRIG_RIG");
    xml->setAttribute ("version", 1);
    xml->addChildElement (chain.toXml().release());
    for (int m = 0; m < numMacros; ++m)
    {
        auto& a = macros[(size_t) m];
        if (! a.isAssigned()) continue;
        auto* me = xml->createNewChildElement ("MACRO");
        me->setAttribute ("index", m);
        me->setAttribute ("slot", a.slot);
        me->setAttribute ("param", a.param);
    }
    return xml;
}

void Deck::restoreRigXml (const juce::XmlElement& xml)
{
    for (int m = 0; m < numMacros; ++m)
        macros[(size_t) m] = {};
    if (auto* c = xml.getChildByName ("CHAIN"))
        chain.fromXml (*c);
    for (auto* me : xml.getChildWithTagNameIterator ("MACRO"))
        assignMacro (me->getIntAttribute ("index"), me->getIntAttribute ("slot", -1), me->getIntAttribute ("param", -1));
}
} // namespace deckrig
