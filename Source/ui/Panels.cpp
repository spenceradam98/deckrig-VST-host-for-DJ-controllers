#include "Panels.h"

namespace deckrig
{
using namespace juce;

void paintPanel (Graphics& g, const Component& c, Colour tint)
{
    auto& t = themeOf (c);
    auto r = c.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, t.radius);
    if (t.brushed)
    {
        Random rng (42);
        for (int y = (int) r.getY(); y < (int) r.getBottom(); y += 2)
        {
            g.setColour (Colours::white.withAlpha (0.03f + 0.05f * rng.nextFloat()));
            g.drawHorizontalLine (y, r.getX() + 2, r.getRight() - 2);
        }
    }
    g.setColour (t.panelEdge);
    g.drawRoundedRectangle (r, t.radius, 1.0f);
    g.setColour (tint);
    g.fillRoundedRectangle (r.withHeight (3.0f).reduced (t.radius, 0), 1.5f);
}

//==============================================================================
DeckPanel::DeckPanel (HostEngine& e, Deck& d, std::function<void()> scanner)
    : chainList (e, d, std::move (scanner)), engine (e), deck (d),
      gain (e, deckTarget (d.index, "gain"), "GAIN", true),
      high (e, deckTarget (d.index, "eqHigh"), "HIGH", true),
      mid (e, deckTarget (d.index, "eqMid"), "MID", true),
      low (e, deckTarget (d.index, "eqLow"), "LOW", true),
      filter (e, deckTarget (d.index, "filter"), "FILTER", true),
      volume (e, deckTarget (d.index, "volume"), true, 0.8f),
      pfl (e, deckTarget (d.index, "pfl"), "CUE"),
      fxBypass (e, deckTarget (d.index, "chainBypass"), "FX OFF"),
      meter (d.meterL, d.meterR)
{
    title.setText ("DECK " + deckLetter (d.index), dontSendNotification);
    title.setFont (FontOptions (20.0f, Font::bold));
    addAndMakeVisible (title);

    inputLabel.setText ("INPUT", dontSendNotification);
    inputLabel.setFont (FontOptions (11.0f));
    addAndMakeVisible (inputLabel);
    input.setTooltip ("Live audio into this deck, before its plugins (a turntable, a DJ app, a mic)");
    input.onChange = [this] { deck.params.inputPair = input.getSelectedId() - 2; };
    addAndMakeVisible (input);
    refreshInputs();

    addAndMakeVisible (chainList);

    macroTitle.setText ("MACROS  (right-click to assign)", dontSendNotification);
    macroTitle.setFont (FontOptions (11.0f));
    addAndMakeVisible (macroTitle);
    for (int m = 0; m < numMacros; ++m)
    {
        auto* k = macros.add (new Knob (e, deckTarget (d.index, "macro." + String (m + 1)), "M" + String (m + 1)));
        k->onRightClick = [this, m] (const MouseEvent&) { showMacroMenu (m); };
        k->slider.onDragStart = [this, m] { deck.selectedMacro = m; };
        addAndMakeVisible (k);
    }

    for (auto* k : { &gain, &high, &mid, &low, &filter })
        addAndMakeVisible (k);
    filter.slider.setTooltip ("Left = low-pass, right = high-pass, centre = off");
    addAndMakeVisible (volume);
    pfl.setTooltip ("Send this deck to the headphone output (outputs 3/4)");
    addAndMakeVisible (pfl);
    fxBypass.setTooltip ("Bypass every plugin on this deck");
    addAndMakeVisible (fxBypass);
    addAndMakeVisible (meter);
    themeChanged();
}

void DeckPanel::refreshInputs()
{
    input.clear (dontSendNotification);
    input.addItem ("None (instrument only)", 1);
    int numIns = 0;
    if (auto* dev = engine.deviceManager.getCurrentAudioDevice())
        numIns = dev->getActiveInputChannels().countNumberOfSetBits();
    for (int p = 0; p < jmax (2, (numIns + 1) / 2); ++p)
        input.addItem ("Inputs " + String (p * 2 + 1) + "/" + String (p * 2 + 2) + (p * 2 >= numIns ? "  (not active)" : ""), p + 2);
    input.setSelectedId (deck.params.inputPair.load() + 2, dontSendNotification);
}

void DeckPanel::themeChanged()
{
    auto& t = themeOf (*this);
    const auto c = t.deckColour (deck.index);
    title.setColour (Label::textColourId, c);
    for (auto* k : macros) k->setRingColour (c);
    for (auto* k : { &gain, &high, &mid, &low, &filter }) k->setRingColour (c);
    volume.setColour (c);
    pfl.setLed (c);
    fxBypass.setLed (t.meterHigh);
    chainList.rebuild();
    for (auto* l : { &title, &inputLabel, &macroTitle })
        l->setFont (t.font (l == &title ? 20.0f : 11.0f, l == &title));
    repaint();
}

void DeckPanel::showMacroMenu (int m)
{
    PopupMenu menu;
    menu.addSectionHeader ("Macro " + String (m + 1) + " controls...");
    for (int s = 0; s < deck.chain.size(); ++s)
    {
        auto* p = deck.chain.getPlugin (s);
        if (p == nullptr) continue;
        PopupMenu sub;
        auto& params = p->getParameters();
        for (int i = 0; i < params.size(); ++i)
        {
            const bool ticked = deck.macros[(size_t) m].slot == s && deck.macros[(size_t) m].param == i;
            sub.addItem (params[i]->getName (60), true, ticked, [this, m, s, i] { deck.assignMacro (m, s, i); deck.selectedMacro = m; });
            if (i >= 255) { sub.addItem ("(more parameters not shown)", false, false, nullptr); break; }
        }
        menu.addSubMenu (String (s + 1) + ". " + p->getName(), sub, params.size() > 0);
    }
    if (deck.chain.size() == 0)
        menu.addItem ("Add a plugin to this deck first", false, false, nullptr);
    menu.addSeparator();
    menu.addItem ("Clear", deck.macros[(size_t) m].isAssigned(), false, [this, m] { deck.assignMacro (m, -1, -1); });
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (macros[m]));
}

void DeckPanel::tick()
{
    auto& t = themeOf (*this);
    const int sel = deck.selectedMacro.load();
    for (int m = 0; m < numMacros; ++m)
    {
        auto* k = macros[m];
        auto& a = deck.macros[(size_t) m];
        k->slider.setEnabled (a.isAssigned());
        k->setCaption (a.isAssigned() ? a.label.fromFirstOccurrenceOf (": ", false, false).substring (0, 12) : "M" + String (m + 1));
        k->slider.setTooltip (a.isAssigned() ? a.label : "Unassigned. Right-click to pick a plugin parameter.");
        k->label.setColour (Label::textColourId, m == sel ? t.deckColour (deck.index) : t.dimText);
        k->syncFromEngine();
    }
    for (auto* k : { &gain, &high, &mid, &low, &filter }) k->syncFromEngine();
    volume.syncFromEngine();
    pfl.syncFromEngine();
    fxBypass.syncFromEngine();
    for (auto* b : chainList.getChildren())
        if (auto* tk = dynamic_cast<ToggleKey*> (b)) tk->syncFromEngine();
    meter.tick();
}

void DeckPanel::paint (Graphics& g)
{
    paintPanel (g, *this, themeOf (*this).deckColour (deck.index));
}

void DeckPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto top = r.removeFromTop (28);
    title.setBounds (top.removeFromLeft (110));
    input.setBounds (top.removeFromRight (jmin (180, top.getWidth() / 2)));
    inputLabel.setBounds (top.removeFromRight (44));
    r.removeFromTop (8);

    // Right column: EQ knobs, fader and meter (the "mixer channel" of this deck).
    // Deck A has it on the inside edge (right), deck B mirrored (left).
    const bool mirrored = deck.index == 1;
    auto channel = mirrored ? r.removeFromLeft (150) : r.removeFromRight (150);
    mirrored ? r.removeFromLeft (10) : r.removeFromRight (10);

    auto knobCol = channel.removeFromLeft (70);
    const int kh = jmin (70, (knobCol.getHeight() - 40) / 5);
    for (auto* k : { &gain, &high, &mid, &low, &filter })
        k->setBounds (knobCol.removeFromTop (kh).reduced (2));
    channel.removeFromLeft (6);
    auto buttons = channel.removeFromBottom (58);
    pfl.setBounds (buttons.removeFromTop (26));
    buttons.removeFromTop (6);
    fxBypass.setBounds (buttons.removeFromTop (26));
    channel.removeFromBottom (8);
    meter.setBounds (channel.removeFromRight (14));
    channel.removeFromRight (4);
    volume.setBounds (channel);

    // Left: plugin chain then macros.
    auto macroArea = r.removeFromBottom (jmin (200, r.getHeight() / 2));
    macroTitle.setBounds (macroArea.removeFromTop (16));
    const int cols = 4, rows = 2;
    const int mw = macroArea.getWidth() / cols, mh = macroArea.getHeight() / rows;
    for (int m = 0; m < numMacros; ++m)
        macros[m]->setBounds (macroArea.getX() + (m % cols) * mw, macroArea.getY() + (m / cols) * mh, mw, mh);
    r.removeFromBottom (8);
    chainList.setBounds (r);
}

//==============================================================================
MixerPanel::MixerPanel (HostEngine& e)
    : engine (e),
      master (e, "mixer.master", "MASTER", false, 0.8f),
      cueMix (e, "mixer.cueMix", "CUE/MIX", false, 0.0f),
      cueLevel (e, "mixer.cueLevel", "PHONES", false, 0.8f),
      crossfader (e, "mixer.crossfader", false, 0.5f),
      meter (e.mixer.meterL, e.mixer.meterR)
{
    title.setText ("MIXER", dontSendNotification);
    title.setJustificationType (Justification::centred);
    addAndMakeVisible (title);
    for (auto* k : { &master, &cueMix, &cueLevel }) addAndMakeVisible (k);
    cueMix.slider.setTooltip ("Headphones: left = cue only, right = master only");
    addAndMakeVisible (crossfader);
    xfLabel.setText ("A          CROSSFADER          B", dontSendNotification);
    xfLabel.setJustificationType (Justification::centred);
    addAndMakeVisible (xfLabel);
    curve.addItem ("Smooth curve (blend)", 1);
    curve.addItem ("Sharp curve (scratch)", 2);
    curve.addItem ("Linear curve", 3);
    curve.setSelectedId (engine.mixer.curve.load() + 1, dontSendNotification);
    curve.onChange = [this] { engine.mixer.curve = curve.getSelectedId() - 1; };
    addAndMakeVisible (curve);
    addAndMakeVisible (meter);
    themeChanged();
}

void MixerPanel::themeChanged()
{
    auto& t = themeOf (*this);
    title.setFont (t.font (16.0f, true));
    xfLabel.setFont (t.font (11.0f));
    xfLabel.setColour (Label::textColourId, t.dimText);
    crossfader.setColour (t.accent);
    repaint();
}

void MixerPanel::tick()
{
    for (auto* k : { &master, &cueMix, &cueLevel }) k->syncFromEngine();
    crossfader.syncFromEngine();
    curve.setSelectedId (engine.mixer.curve.load() + 1, dontSendNotification);
    meter.tick();
}

void MixerPanel::paint (Graphics& g)
{
    paintPanel (g, *this, themeOf (*this).accent);
}

void MixerPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    title.setBounds (r.removeFromTop (24));
    auto bottom = r.removeFromBottom (96);
    curve.setBounds (bottom.removeFromBottom (24));
    bottom.removeFromBottom (4);
    xfLabel.setBounds (bottom.removeFromBottom (16));
    crossfader.setBounds (bottom.reduced (4, 0));

    auto meterArea = r.removeFromRight (22);
    meter.setBounds (meterArea.reduced (2, 6));
    const int kh = jmin (90, r.getHeight() / 3);
    master.setBounds (r.removeFromTop (kh).reduced (6));
    cueLevel.setBounds (r.removeFromTop (kh).reduced (6));
    cueMix.setBounds (r.removeFromTop (kh).reduced (6));
}
} // namespace deckrig
