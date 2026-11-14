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
      info (e, d), wave (d, false), overview (d, true),
      gain (e, deckTarget (d.index, "gain"), "GAIN", true),
      high (e, deckTarget (d.index, "eqHigh"), "HIGH", true),
      mid (e, deckTarget (d.index, "eqMid"), "MID", true),
      low (e, deckTarget (d.index, "eqLow"), "LOW", true),
      filter (e, deckTarget (d.index, "filter"), "FILTER", true),
      volume (e, deckTarget (d.index, "volume"), true, 0.8f),
      tempo (e, deckTarget (d.index, "tempo"), true, 0.5f),
      pfl (e, deckTarget (d.index, "pfl"), "CUE"),
      fxBypass (e, deckTarget (d.index, "chainBypass"), "FX OFF"),
      meter (d.meterL, d.meterR)
{
    title.setText ("DECK " + deckLetter (d.index), dontSendNotification);
    addAndMakeVisible (title);

    rigButton.setTooltip ("Save or load this deck's plugins and macros as a rig preset");
    rigButton.onClick = [this] { showRigMenu(); };
    addAndMakeVisible (rigButton);

    inputLabel.setText ("INPUT", dontSendNotification);
    addAndMakeVisible (inputLabel);
    input.setTooltip ("Live audio into this deck, before its plugins (a turntable, a mic, another app)");
    input.onChange = [this] { deck.params.inputPair = input.getSelectedId() - 2; };
    addAndMakeVisible (input);
    refreshInputs();

    addAndMakeVisible (info);
    wave.setTooltip ("Drag to scratch");
    addAndMakeVisible (wave);
    overview.setTooltip ("Click to jump");
    addAndMakeVisible (overview);

    // Transport
    setTargetId (play, deckTarget (d.index, "play"));
    play.onClick = [this] { engine.trigger (deckTarget (deck.index, "play")); };
    setTargetId (cue, deckTarget (d.index, "cue"));
    cue.onHold = [this] (bool down) { deck.player.push ({ down ? TrackPlayer::Cmd::CueDown : TrackPlayer::Cmd::CueUp }); };
    cue.isLit = [this] { return deck.player.loaded && ! deck.player.playing; };
    setTargetId (sync, deckTarget (d.index, "sync"));
    sync.onClick = [this] { engine.trigger (deckTarget (deck.index, "sync")); };
    sync.setTooltip ("Match tempo and beat phase to the other deck");
    for (auto* b : { (Button*) &play, (Button*) &cue, (Button*) &sync })
        addAndMakeVisible (b);

    // Tempo fader: up = slower, like a CDJ.
    tempo.slider.setTooltip ("Tempo. Double-click to reset to 0 %.");
    tempo.slider.setRange (0.0, 1.0);
    tempo.slider.setSliderStyle (Slider::LinearVertical);
    addAndMakeVisible (tempo);
    tempoRange.onClick = [this] { deck.params.tempoRange = (deck.params.tempoRange.load() + 1) % 3; };
    tempoRange.setTooltip ("Tempo fader range: click to switch between 8, 16 and 50 %");
    addAndMakeVisible (tempoRange);

    // Pad modes and pads
    const char* modeNames[] = { "HOT CUE", "LOOP", "FX", "INSTR" };
    const char* modeIds[] = { "hotcue", "loop", "fx", "instrument" };
    for (int m = 0; m < 4; ++m)
    {
        auto* b = modeButtons.add (new TextButton (modeNames[m]));
        setTargetId (*b, deckTarget (d.index, String ("padMode.") + modeIds[m]));
        b->onClick = [this, m] { deck.params.padMode = m; };
        addAndMakeVisible (b);
    }
    modeButtons[0]->setTooltip ("Pads set and jump to hot cues. Shift + pad deletes.");
    modeButtons[1]->setTooltip ("Pads 1-6 set 1/2 to 16 beat loops, 7 halves, 8 doubles. Shift + pad beat-jumps.");
    modeButtons[2]->setTooltip ("Pads switch this deck's plugins 1-8 on and off");
    modeButtons[3]->setTooltip ("Pads play the deck's instrument plugin (notes C2 to G2)");
    for (int p = 0; p < 8; ++p)
    {
        auto* b = pads.add (new HoldButton (String (p + 1)));
        setTargetId (*b, deckTarget (d.index, "pad." + String (p + 1)));
        b->onHold = [this, p] (bool down) { engine.padPress (deck.index, p, down, 100, ModifierKeys::currentModifiers.isShiftDown()); };
        b->isLit = [this, p] { return engine.getLed (deckTarget (deck.index, "pad." + String (p + 1))); };
        addAndMakeVisible (b);
    }

    addAndMakeVisible (chainList);

    macroTitle.setText ("MACROS (right-click to assign)", dontSendNotification);
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

DeckPanel::~DeckPanel() = default;

void DeckPanel::refreshInputs()
{
    input.clear (dontSendNotification);
    input.addItem ("None", 1);
    int numIns = 0;
    if (auto* dev = engine.deviceManager.getCurrentAudioDevice())
        numIns = dev->getActiveInputChannels().countNumberOfSetBits();
    for (int p = 0; p < jmax (2, (numIns + 1) / 2); ++p)
        input.addItem ("Inputs " + String (p * 2 + 1) + "/" + String (p * 2 + 2) + (p * 2 >= numIns ? " (off)" : ""), p + 2);
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
    tempo.setColour (c);
    pfl.setLed (c);
    fxBypass.setLed (t.meterHigh);
    play.getProperties().set ("ledColour", (int64) t.meterLow.getARGB());
    cue.getProperties().set ("ledColour", (int64) t.meterMid.getARGB());
    sync.getProperties().set ("ledColour", (int64) c.getARGB());
    for (auto* p : pads) p->getProperties().set ("ledColour", (int64) c.withAlpha (0.85f).getARGB());
    for (auto* b : modeButtons) b->getProperties().set ("ledColour", (int64) c.getARGB());
    chainList.rebuild();
    title.setFont (t.font (20.0f, true));
    for (auto* l : { &inputLabel, &macroTitle })
    {
        l->setFont (t.font (11.0f));
        l->setColour (Label::textColourId, t.dimText);
    }
    repaint();
}

void DeckPanel::showRigMenu()
{
    PopupMenu m;
    auto dir = File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile (DECKRIG_BRAND_NAME).getChildFile (DECKRIG_PRODUCT_NAME).getChildFile ("Rigs");
    dir.createDirectory();
    m.addItem ("Save rig preset...", [this, dir]
    {
        chooser = std::make_unique<FileChooser> ("Save rig preset", dir.getChildFile ("My rig.deckrig"), "*.deckrig");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [this] (const FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == File()) return;
            if (auto xml = deck.createRigXml())
                xml->writeTo (f.withFileExtension ("deckrig"));
        });
    });
    m.addItem ("Load rig preset...", [this, dir]
    {
        chooser = std::make_unique<FileChooser> ("Load rig preset", dir, "*.deckrig");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (auto xml = XmlDocument::parse (fc.getResult()))
            {
                chainList.closeAllWindows();
                deck.restoreRigXml (*xml);
                chainList.rebuild();
            }
        });
    });
    // Quick list of saved rigs.
    auto files = dir.findChildFiles (File::findFiles, false, "*.deckrig");
    files.sort();
    if (! files.isEmpty())
    {
        m.addSeparator();
        for (auto& f : files)
            m.addItem (f.getFileNameWithoutExtension(), [this, f]
            {
                if (auto xml = XmlDocument::parse (f))
                {
                    chainList.closeAllWindows();
                    deck.restoreRigXml (*xml);
                    chainList.rebuild();
                }
            });
    }
    m.addSeparator();
    m.addItem ("Clear all plugins", deck.chain.size() > 0, false, [this]
    {
        chainList.closeAllWindows();
        for (auto& a : deck.macros) a = {};
        deck.chain.clear();
    });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&rigButton));
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

void DeckPanel::updatePadLabels()
{
    const int mode = deck.params.padMode.load();
    static const char* loopNames[] = { "1/2", "1", "2", "4", "8", "16", "1/2 X", "2 X" };
    for (int p = 0; p < 8; ++p)
    {
        String text;
        switch (mode)
        {
            case padHotcue: text = String (p + 1); break;
            case padLoop: text = loopNames[p]; break;
            case padFx:
            {
                auto* pl = deck.chain.getPlugin (p);
                text = pl != nullptr ? pl->getName().substring (0, 8) : String ("-");
                break;
            }
            default: text = MidiMessage::getMidiNoteName (36 + p, true, true, 3); break;
        }
        pads[p]->setButtonText (text);
    }
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
    tempo.syncFromEngine();
    pfl.syncFromEngine();
    fxBypass.syncFromEngine();
    for (auto* b : chainList.getChildren())
        if (auto* tk = dynamic_cast<ToggleKey*> (b)) tk->syncFromEngine();

    play.setToggleState (deck.player.playing.load(), dontSendNotification);
    sync.setToggleState (deck.player.synced.load(), dontSendNotification);
    cue.sync();
    const int mode = deck.params.padMode.load();
    for (int m = 0; m < 4; ++m) modeButtons[m]->setToggleState (m == mode, dontSendNotification);
    if (mode != lastPadMode || deck.chain.size() != lastChainSize)
    {
        lastPadMode = mode;
        lastChainSize = deck.chain.size();
        updatePadLabels();
    }
    for (auto* p : pads) p->sync();
    {
        static const char* names[] = { "8%", "16%", "50%" };
        const String txt = names[jlimit (0, 2, deck.params.tempoRange.load())];
        if (tempoRange.getButtonText() != txt) tempoRange.setButtonText (txt);
    }
    info.repaint();
    wave.repaint();
    overview.repaint();
    meter.tick();
}

bool DeckPanel::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (engine.formats.findFormatForFileExtension (File (f).getFileExtension()) != nullptr) return true;
    return false;
}

void DeckPanel::filesDropped (const StringArray& files, int, int)
{
    for (auto& f : files)
        if (engine.formats.findFormatForFileExtension (File (f).getFileExtension()) != nullptr)
        {
            engine.loadTrack (deck.index, File (f));
            return;
        }
}

bool DeckPanel::isInterestedInDragSource (const SourceDetails& s)
{
    return s.description.toString().startsWith ("track:");
}

void DeckPanel::itemDropped (const SourceDetails& s)
{
    engine.loadTrack (deck.index, File (s.description.toString().fromFirstOccurrenceOf ("track:", false, false)));
}

void DeckPanel::paint (Graphics& g)
{
    paintPanel (g, *this, themeOf (*this).deckColour (deck.index));
}

void DeckPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    const bool mirrored = deck.index == 1;

    auto top = r.removeFromTop (26);
    title.setBounds (top.removeFromLeft (100));
    rigButton.setBounds (top.removeFromLeft (52).reduced (0, 1));
    input.setBounds (top.removeFromRight (jmin (130, top.getWidth() / 3)));
    inputLabel.setBounds (top.removeFromRight (40));
    r.removeFromTop (4);
    info.setBounds (r.removeFromTop (38));
    r.removeFromTop (2);
    wave.setBounds (r.removeFromTop (72));
    r.removeFromTop (3);
    overview.setBounds (r.removeFromTop (20));
    r.removeFromTop (8);

    // Channel strip on the inside edge (towards the mixer), tempo on the outside.
    auto channel = mirrored ? r.removeFromLeft (136) : r.removeFromRight (136);
    mirrored ? r.removeFromLeft (8) : r.removeFromRight (8);
    auto tempoCol = mirrored ? r.removeFromRight (44) : r.removeFromLeft (44);
    mirrored ? r.removeFromRight (8) : r.removeFromLeft (8);

    tempoRange.setBounds (tempoCol.removeFromBottom (22));
    tempoCol.removeFromBottom (4);
    tempo.setBounds (tempoCol);

    auto knobCol = channel.removeFromLeft (62);
    const int kh = jmin (64, knobCol.getHeight() / 5);
    for (auto* k : { &gain, &high, &mid, &low, &filter })
        k->setBounds (knobCol.removeFromTop (kh).reduced (1));
    channel.removeFromLeft (4);
    auto buttons = channel.removeFromBottom (54);
    pfl.setBounds (buttons.removeFromTop (24));
    buttons.removeFromTop (6);
    fxBypass.setBounds (buttons.removeFromTop (24));
    channel.removeFromBottom (6);
    meter.setBounds (channel.removeFromRight (12));
    channel.removeFromRight (4);
    volume.setBounds (channel);

    // Middle: transport, pads, plugins, macros.
    auto transport = r.removeFromTop (34);
    const int tw = (transport.getWidth() - 12) / 3;
    play.setBounds (transport.removeFromLeft (tw)); transport.removeFromLeft (6);
    cue.setBounds (transport.removeFromLeft (tw)); transport.removeFromLeft (6);
    sync.setBounds (transport);
    r.removeFromTop (6);

    auto modes = r.removeFromTop (22);
    const int mw = (modes.getWidth() - 9) / 4;
    for (auto* b : modeButtons) { b->setBounds (modes.removeFromLeft (mw)); modes.removeFromLeft (3); }
    r.removeFromTop (4);
    auto padArea = r.removeFromTop (76);
    const int pw = (padArea.getWidth() - 3 * 4) / 4, ph = (padArea.getHeight() - 4) / 2;
    for (int p = 0; p < 8; ++p)
        pads[p]->setBounds (padArea.getX() + (p % 4) * (pw + 4), padArea.getY() + (p / 4) * (ph + 4), pw, ph);
    r.removeFromTop (8);

    auto macroArea = r.removeFromBottom (jmin (150, r.getHeight() / 2));
    macroTitle.setBounds (macroArea.removeFromTop (14));
    const int cw = macroArea.getWidth() / 4, ch = macroArea.getHeight() / 2;
    for (int m = 0; m < numMacros; ++m)
        macros[m]->setBounds (macroArea.getX() + (m % 4) * cw, macroArea.getY() + (m / 4) * ch, cw, ch);
    r.removeFromBottom (4);
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
