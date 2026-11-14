#include "MainComponent.h"
#include "BinaryData.h"

namespace deckrig
{
using namespace juce;

//==============================================================================
// Click any knob, fader or button, then touch the hardware control to bind it.
class MainComponent::LearnOverlay : public Component
{
public:
    LearnOverlay (MainComponent& o, HostEngine& e) : owner (o), engine (e) {}

    void refresh()
    {
        items.clear();
        collect (owner);
        repaint();
    }

    void paint (Graphics& g) override
    {
        auto& t = themeOf (owner);
        g.fillAll (Colours::black.withAlpha (0.28f));
        const auto learning = engine.getLearnTarget();
        for (auto& it : items)
        {
            auto r = getLocalArea (it.comp, it.comp->getLocalBounds()).toFloat().reduced (1.0f);
            const auto summary = engine.getBindingSummary (it.target);
            const bool active = it.target == learning;
            const bool hover = r.contains (mousePos);
            g.setColour (t.learn.withAlpha (active ? 0.45f : summary.isNotEmpty() ? 0.22f : 0.08f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (t.learn.withAlpha (active || hover ? 1.0f : 0.7f));
            g.drawRoundedRectangle (r, 4.0f, active ? 3.0f : 1.2f);
            if (summary.isNotEmpty() && r.getHeight() > 20)
            {
                g.setFont (t.font (10.0f, true));
                auto tag = r.removeFromBottom (14.0f);
                g.setColour (t.learn);
                g.fillRect (tag);
                g.setColour (t.learn.contrasting (0.9f));
                g.drawFittedText (summary, tag.toNearestInt().reduced (2, 0), Justification::centred, 1, 0.7f);
            }
        }

        auto banner = getLocalBounds().removeFromBottom (34).reduced (120, 4).toFloat();
        g.setColour (t.learn);
        g.fillRoundedRectangle (banner, 6.0f);
        g.setColour (t.learn.contrasting (0.95f));
        g.setFont (t.font (13.0f, true));
        String msg = "MIDI LEARN: click a control here, then move or press the control on your controller.  Right-click clears.  Esc finishes.";
        if (learning.isNotEmpty())
            msg = "Waiting for MIDI for \"" + (findTarget (learning) ? findTarget (learning)->label : learning) + "\". Move a fader, turn a knob or press a button.";
        g.drawFittedText (msg, banner.toNearestInt().reduced (8, 0), Justification::centred, 1);
    }

    void mouseMove (const MouseEvent& e) override { mousePos = e.position; repaint(); }

    void mouseDown (const MouseEvent& e) override
    {
        for (auto& it : items)
        {
            auto r = getLocalArea (it.comp, it.comp->getLocalBounds()).toFloat();
            if (! r.contains (e.position)) continue;
            if (e.mods.isPopupMenu())
            {
                PopupMenu m;
                const auto target = it.target;
                m.addItem ("Clear mapping for " + (findTarget (target) ? findTarget (target)->label : target),
                           engine.getBindingSummary (target).isNotEmpty(), false, [this, target]
                {
                    auto p = engine.getProfile();
                    p.unbindTarget (target);
                    engine.setProfile (p);
                    owner.saveProfile();
                    repaint();
                });
                m.showMenuAsync (PopupMenu::Options());
            }
            else
            {
                engine.beginLearn (it.target);
            }
            repaint();
            return;
        }
        engine.cancelLearn();
        repaint();
    }

private:
    struct Item { Component* comp; String target; };

    void collect (Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (child == this || ! child->isVisible()) continue;
            const auto t = getTargetId (*child);
            if (t.isNotEmpty() && (! dynamic_cast<Knob*> (child) || dynamic_cast<Knob*> (child)->slider.isEnabled() || t.contains ("macro")))
                items.push_back ({ child, t });
            else
                collect (*child);
        }
    }

    MainComponent& owner;
    HostEngine& engine;
    std::vector<Item> items;
    Point<float> mousePos;
};

//==============================================================================
// Steps through every control in the profile and records what the hardware sends.
class MainComponent::Wizard : public Component, private Timer
{
public:
    Wizard (MainComponent& o, HostEngine& e) : owner (o), engine (e), profile (e.getProfile())
    {
        captured.assign (profile.controls.size(), false);
        for (auto* b : { &back, &skip, &finish, &cancel }) addAndMakeVisible (b);
        back.onClick = [this] { if (step > 0) { --step; pending = false; update(); } };
        skip.onClick = [this] { advance(); };
        finish.onClick = [this] { done (true); };
        cancel.onClick = [this] { done (false); };
        engine.onCapture = [this] (const RawMidi& r) { handle (r); };
        update();
    }

    ~Wizard() override { engine.onCapture = nullptr; }

    void paint (Graphics& g) override
    {
        auto& t = themeOf (owner);
        g.fillAll (Colours::black.withAlpha (0.55f));
        auto box = panelBounds().toFloat();
        g.setColour (t.panel.withAlpha (1.0f));
        g.fillRoundedRectangle (box, 10.0f);
        g.setColour (t.learn);
        g.drawRoundedRectangle (box, 10.0f, 2.0f);

        auto r = box.reduced (24).toNearestInt();
        g.setFont (t.font (13.0f, true));
        g.setColour (t.dimText);
        g.drawText ("LEARN CONTROLLER   " + profile.name, r.removeFromTop (20), Justification::centredLeft);
        if (profile.controls.empty())
        {
            g.setColour (t.text);
            g.drawFittedText ("This mapping has no controls to step through.\nLoad the Hercules draft from the Controller menu first, or use MIDI LEARN to build a mapping control by control.",
                              r.removeFromTop (80), Justification::centredLeft, 4);
            return;
        }
        auto& c = profile.controls[(size_t) step];
        g.setFont (t.font (13.0f));
        g.drawText ("Step " + String (step + 1) + " of " + String ((int) profile.controls.size()), r.removeFromTop (22), Justification::centredLeft);
        r.removeFromTop (8);
        g.setColour (t.text);
        g.setFont (t.font (26.0f, true));
        g.drawFittedText (verb (c) + " " + c.label, r.removeFromTop (64), Justification::centredLeft, 2);
        g.setFont (t.font (14.0f));
        g.setColour (captured[(size_t) step] ? t.meterLow : t.dimText);
        g.drawText (captured[(size_t) step] ? "Got it: " + c.address.toString() : (c.learned ? "Currently: " + c.address.toString() + " (verified earlier)"
                                                                                           : "Draft guess: " + c.address.toString()),
                    r.removeFromTop (24), Justification::centredLeft);
        r.removeFromTop (6);
        g.setColour (t.dimText);
        g.setFont (t.font (12.0f));
        g.drawFittedText ("Skip anything your controller doesn't have. Back goes to the previous control. Finish keeps everything captured so far.",
                          r.removeFromTop (40), Justification::topLeft, 2);
    }

    void resized() override
    {
        auto r = panelBounds().reduced (24).removeFromBottom (30);
        cancel.setBounds (r.removeFromLeft (90));
        finish.setBounds (r.removeFromRight (90));
        r.removeFromRight (8);
        skip.setBounds (r.removeFromRight (90));
        r.removeFromRight (8);
        back.setBounds (r.removeFromRight (90));
    }

private:
    Rectangle<int> panelBounds() const { return getLocalBounds().withSizeKeepingCentre (jmin (640, getWidth() - 40), 300); }

    static String verb (const ControlDef& c)
    {
        switch (c.kind)
        {
            case ControlKind::Relative: return "Turn";
            case ControlKind::Button:   return "Press";
            case ControlKind::Pad:      return "Hit";
            case ControlKind::Absolute: break;
        }
        return "Move";
    }

    void handle (const RawMidi& r)
    {
        if (profile.controls.empty()) return;
        if (r.address.type == MsgType::Note && ! r.noteOn) return;
        if (pending)
        {
            // Messages arriving right after a capture (fader still moving, a 14-bit
            // fader's second CC, a jog wheel's touch note) are kept off later steps.
            if (r.address != profile.controls[(size_t) step].address) noise.add (r.address.toString());
            return;
        }
        auto& c = profile.controls[(size_t) step];
        if (c.kind == ControlKind::Relative && r.address.type != MsgType::CC) return;
        if (noise.contains (r.address.toString())) return;
        for (size_t i = 0; i < profile.controls.size(); ++i)
            if (i != (size_t) step && captured[i] && profile.controls[i].address == r.address)
                return;
        c.address = r.address;
        c.learned = true;
        captured[(size_t) step] = true;
        pending = true;
        repaint();
        startTimer (700);
    }

    void timerCallback() override
    {
        stopTimer();
        pending = false;
        advance();
    }

    void advance()
    {
        if (step + 1 >= (int) profile.controls.size()) { done (true); return; }
        ++step;
        update();
    }

    void update() { repaint(); back.setEnabled (step > 0); }

    void done (bool keep)
    {
        stopTimer();
        engine.onCapture = nullptr;
        if (keep)
        {
            const auto dev = engine.getLastMidiDevice();
            if (dev.isNotEmpty()) profile.deviceMatch = dev;
            engine.setProfile (profile);
            owner.saveProfile();
        }
        MessageManager::callAsync ([o = &owner] { o->wizard.reset(); o->updateStatus(); });
    }

    MainComponent& owner;
    HostEngine& engine;
    MappingProfile profile;
    std::vector<bool> captured;
    StringArray noise;
    int step = 0;
    bool pending = false;
    TextButton back { "BACK" }, skip { "SKIP" }, finish { "FINISH" }, cancel { "CANCEL" };
};

//==============================================================================
class MainComponent::MappingList : public Component, private ListBoxModel
{
public:
    explicit MappingList (HostEngine& e) : engine (e)
    {
        list.setModel (this);
        list.setRowHeight (20);
        addAndMakeVisible (list);
        title.setText ("MAPPING", dontSendNotification);
        addAndMakeVisible (title);
        refresh();
    }

    void refresh()
    {
        profile = engine.getProfile();
        list.updateContent();
        list.repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        title.setBounds (r.removeFromTop (18));
        list.setBounds (r);
    }

private:
    int getNumRows() override { return (int) profile.bindings.size(); }

    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (! isPositiveAndBelow (row, (int) profile.bindings.size())) return;
        auto& t = themeOf (*this);
        auto& b = profile.bindings[(size_t) row];
        auto* c = profile.findControl (b.control);
        auto* info = findTarget (b.target);
        if (selected) g.fillAll (t.accent.withAlpha (0.25f));
        g.setFont (t.font (12.0f));
        auto r = Rectangle<int> (0, 0, w, h).reduced (4, 0);
        g.setColour (c != nullptr && c->learned ? t.meterLow : t.meterMid);
        g.drawText (c != nullptr && c->learned ? "OK" : "DRAFT", r.removeFromLeft (44), Justification::centredLeft);
        g.setColour (t.text);
        g.drawText (c != nullptr ? c->label : b.control, r.removeFromLeft (r.getWidth() / 3), Justification::centredLeft, true);
        g.setColour (t.dimText);
        g.drawText (c != nullptr ? c->address.toString() : String(), r.removeFromLeft (r.getWidth() / 2), Justification::centredLeft, true);
        g.setColour (t.text);
        g.drawText (info != nullptr ? info->label : b.target, r, Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() || ! isPositiveAndBelow (row, (int) profile.bindings.size())) return;
        PopupMenu m;
        m.addItem ("Remove this binding", [this, row]
        {
            auto p = engine.getProfile();
            if (isPositiveAndBelow (row, (int) p.bindings.size()))
                p.bindings.erase (p.bindings.begin() + row);
            engine.setProfile (p);
        });
        m.showMenuAsync (PopupMenu::Options());
    }

    HostEngine& engine;
    MappingProfile profile;
    ListBox list;
    Label title;
};

//==============================================================================
class MainComponent::ScannerWindow : public DocumentWindow
{
public:
    ScannerWindow (PluginLibrary& l, std::function<void()> onClose)
        : DocumentWindow ("Plugins", Colours::darkgrey, DocumentWindow::closeButton), lib (l), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        auto* list = new PluginListComponent (lib.formatManager, lib.knownPlugins, lib.getDeadMansPedalFile(), nullptr, false);
        list->setSize (680, 480);
        setContentOwned (list, true);
        setResizable (true, false);
        centreWithSize (680, 480);
        setVisible (true);
    }
    void closeButtonPressed() override
    {
        lib.save();
        MessageManager::callAsync (closed);
    }

private:
    PluginLibrary& lib;
    std::function<void()> closed;
};

//==============================================================================
MappingProfile MainComponent::loadFactoryProfile()
{
    auto p = MappingProfile::fromJson (String::fromUTF8 (BinaryData::herculesdjcontrolmixultra_json, BinaryData::herculesdjcontrolmixultra_jsonSize));
    return p.value_or (MappingProfile {});
}

File MainComponent::getMappingFile()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile (DECKRIG_BRAND_NAME).getChildFile (DECKRIG_PRODUCT_NAME).getChildFile ("Mappings").getChildFile ("current.json");
}

MainComponent::MainComponent (HostEngine& e, PropertiesFile* s) : engine (e), settings (s), mixer (e)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    deckA = std::make_unique<DeckPanel> (engine, *engine.decks[0], [this] { openScanner(); });
    deckB = std::make_unique<DeckPanel> (engine, *engine.decks[1], [this] { openScanner(); });
    addAndMakeVisible (*deckA);
    addAndMakeVisible (*deckB);
    addAndMakeVisible (mixer);

    logo.setText (DECKRIG_PRODUCT_NAME, dontSendNotification);
    addAndMakeVisible (logo);
    status.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (status);

    audioButton.onClick = [this] { showAudioSettings(); };
    pluginsButton.onClick = [this] { openScanner(); };
    controllerButton.onClick = [this] { showControllerMenu(); };
    learnButton.setClickingTogglesState (true);
    learnButton.onClick = [this] { setLearnMode (learnButton.getToggleState()); };
    learnButton.setTooltip ("Click a control on screen, then move the matching control on your hardware");
    monitorButton.setClickingTogglesState (true);
    monitorButton.onClick = [this] { setMonitorVisible (monitorButton.getToggleState()); };
    monitorButton.setTooltip ("Show incoming MIDI and the current mapping");
    for (auto* b : { &audioButton, &pluginsButton, &controllerButton, &learnButton, &monitorButton })
        addAndMakeVisible (b);

    int id = 1;
    for (auto& t : getThemes()) themeBox.addItem (t.name, id++);
    themeBox.onChange = [this] { setTheme (themeBox.getSelectedId() - 1); };
    themeBox.setTooltip ("Look of the app");
    addAndMakeVisible (themeBox);

    monitorText.setMultiLine (true);
    monitorText.setReadOnly (true);
    monitorText.setScrollbarsShown (true);
    monitorText.setCaretVisible (false);
    addChildComponent (monitorText);
    mappingList = std::make_unique<MappingList> (engine);
    addChildComponent (*mappingList);

    overlay = std::make_unique<LearnOverlay> (*this, engine);
    addChildComponent (*overlay);

    engine.onLearned = [this] (const String&, const String&) { saveProfile(); if (overlay->isVisible()) overlay->refresh(); };
    engine.addChangeListener (this);
    engine.deviceManager.addChangeListener (this);

    setTheme (settings != nullptr ? settings->getIntValue ("theme", 0) : 0);
    setMonitorVisible (settings != nullptr && settings->getBoolValue ("monitor", false));
    setSize (1280, 760);
    startTimerHz (30);
    updateStatus();
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.onLearned = nullptr;
    engine.onCapture = nullptr;
    engine.removeChangeListener (this);
    engine.deviceManager.removeChangeListener (this);
    wizard.reset();
    overlay.reset();
    if (scanner != nullptr) { engine.library.save(); scanner.reset(); }
    deckA->chainList.closeAllWindows();
    deckB->chainList.closeAllWindows();
    setLookAndFeel (nullptr);
}

void MainComponent::saveState()
{
    if (settings == nullptr) return;
    settings->setValue ("theme", themeIndex);
    settings->setValue ("monitor", monitorVisible);
    if (auto xml = engine.deviceManager.createStateXml())
        settings->setValue ("audioDevice", xml.get());
    if (auto xml = engine.createSessionXml())
        settings->setValue ("session", xml.get());
    settings->saveIfNeeded();
}

void MainComponent::saveProfile()
{
    auto f = getMappingFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (engine.getProfile().toJson());
    if (mappingList) mappingList->refresh();
    updateStatus();
}

void MainComponent::setTheme (int i)
{
    themeIndex = jlimit (0, (int) getThemes().size() - 1, i);
    lnf.setTheme (getTheme (themeIndex));
    themeBox.setSelectedId (themeIndex + 1, dontSendNotification);
    themeChanged();
}

void MainComponent::themeChanged()
{
    auto& t = getTheme (themeIndex);
    logo.setFont (t.font (22.0f, true));
    logo.setColour (Label::textColourId, t.accent);
    status.setFont (t.font (12.0f));
    status.setColour (Label::textColourId, t.dimText);
    monitorText.setFont (FontOptions (Font::getDefaultMonospacedFontName(), 12.0f, Font::plain));
    monitorText.applyColourToAllText (t.text);
    deckA->themeChanged();
    deckB->themeChanged();
    mixer.themeChanged();
    sendLookAndFeelChange();
    repaint();
}

void MainComponent::setLearnMode (bool on)
{
    learnButton.setToggleState (on, dontSendNotification);
    if (! on) engine.cancelLearn();
    overlay->setVisible (on);
    if (on) { overlay->refresh(); overlay->toFront (false); }
    updateStatus();
}

void MainComponent::setMonitorVisible (bool on)
{
    monitorVisible = on;
    monitorButton.setToggleState (on, dontSendNotification);
    monitorText.setVisible (on);
    mappingList->setVisible (on);
    if (on) mappingList->refresh();
    resized();
}

bool MainComponent::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey && overlay->isVisible()) { setLearnMode (false); return true; }
    return false;
}

void MainComponent::changeListenerCallback (ChangeBroadcaster* src)
{
    if (src == &engine.deviceManager)
    {
        deckA->refreshInputs();
        deckB->refreshInputs();
    }
    else
    {
        if (mappingList) mappingList->refresh();
        if (overlay->isVisible()) overlay->refresh();
        deckA->chainList.rebuild();
        deckB->chainList.rebuild();
    }
    updateStatus();
}

void MainComponent::updateStatus()
{
    const auto p = engine.getProfile();
    int draft = 0;
    for (auto& b : p.bindings)
        if (auto* c = p.findControl (b.control); c != nullptr && ! c->learned) ++draft;
    const auto dev = engine.getLastMidiDevice();
    String s = "Controller: " + (dev.isNotEmpty() ? dev : String ("none heard yet")) + "    Mapping: " + p.name + " (" + String ((int) p.bindings.size()) + " bindings";
    if (draft > 0) s << ", " << draft << " unverified";
    s << ")";
    if (auto* d = engine.deviceManager.getCurrentAudioDevice())
        s << "    Audio: " << d->getName() << " " << (int) d->getCurrentSampleRate() << " Hz / " << d->getCurrentBufferSizeSamples();
    else
        s << "    Audio: no device";
    status.setText (s, dontSendNotification);
}

void MainComponent::timerCallback()
{
    deckA->tick();
    deckB->tick();
    mixer.tick();
    if (overlay->isVisible()) overlay->repaint();

    static int slow = 0;
    if (++slow % 30 == 0) updateStatus();

    if (monitorVisible)
    {
        auto lines = engine.getMonitorLines();
        const auto tail = lines.isEmpty() ? String() : lines[lines.size() - 1];
        if (lines.size() != lastMonitorCount || tail != lastMonitorTail)
        {
            lastMonitorCount = lines.size();
            lastMonitorTail = tail;
            monitorText.setText (lines.isEmpty() ? String ("No MIDI received yet. Move something on your controller.") : lines.joinIntoString ("\n"), false);
            monitorText.moveCaretToEnd();
        }
    }
}

void MainComponent::showControllerMenu()
{
    PopupMenu m;
    const auto p = engine.getProfile();
    m.addSectionHeader ("Mapping: " + p.name);
    m.addItem ("Learn controller (step through every control)...", [this] { startWizard(); });
    m.addItem ("MIDI learn single controls", [this] { setLearnMode (true); });
    m.addSeparator();
    m.addItem ("Load Hercules DJControl Mix Ultra (draft)", [this]
    {
        engine.setProfile (loadFactoryProfile());
        saveProfile();
    });
    m.addItem ("New empty mapping", [this]
    {
        MappingProfile p;
        p.name = "My controller";
        engine.setProfile (p);
        saveProfile();
    });
    m.addSeparator();
    m.addItem ("Open mapping file...", [this]
    {
        chooser = std::make_unique<FileChooser> ("Open mapping", getMappingFile().getParentDirectory(), "*.json");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            String err;
            if (auto p = MappingProfile::fromJson (f.loadFileAsString(), &err))
            {
                engine.setProfile (*p);
                saveProfile();
            }
            else
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Could not open mapping", err);
        });
    });
    m.addItem ("Save mapping file as...", [this]
    {
        chooser = std::make_unique<FileChooser> ("Save mapping", getMappingFile().getParentDirectory().getChildFile (File::createLegalFileName (engine.getProfile().name) + ".json"), "*.json");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [this] (const FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f != File()) f.withFileExtension ("json").replaceWithText (engine.getProfile().toJson());
        });
    });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&controllerButton));
}

void MainComponent::startWizard()
{
    setLearnMode (false);
    wizard = std::make_unique<Wizard> (*this, engine);
    addAndMakeVisible (*wizard);
    resized();
}

void MainComponent::showAudioSettings()
{
    auto* sel = new AudioDeviceSelectorComponent (engine.deviceManager, 0, 8, 2, 8, true, true, true, false);
    sel->setSize (560, 520);
    DialogWindow::LaunchOptions o;
    o.content.setOwned (sel);
    o.dialogTitle = "Audio and MIDI settings";
    o.dialogBackgroundColour = getTheme (themeIndex).panel.withAlpha (1.0f);
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::openScanner()
{
    if (scanner != nullptr) { scanner->toFront (true); return; }
    scanner = std::make_unique<ScannerWindow> (engine.library, [this] { scanner.reset(); });
}

void MainComponent::paint (Graphics& g)
{
    auto& t = getTheme (themeIndex);
    g.fillAll (t.bg);
    if (t.grid)
    {
        // Synthwave floor grid behind the panels.
        auto r = getLocalBounds().toFloat();
        const float horizon = r.getHeight() * 0.42f;
        g.setGradientFill (ColourGradient (t.accent.withAlpha (0.18f), 0, horizon, t.bg.withAlpha (0.0f), 0, 0, false));
        g.fillRect (r.withBottom (horizon));
        g.setColour (t.accent.withAlpha (0.18f));
        for (int i = -20; i <= 20; ++i)
            g.drawLine (r.getCentreX(), horizon, r.getCentreX() + (float) i * r.getWidth() * 0.12f, r.getBottom(), 1.0f);
        for (float y = 4; horizon + y < r.getBottom(); y *= 1.35f)
            g.drawHorizontalLine ((int) (horizon + y), 0, r.getRight());
    }
    g.setColour (t.panelEdge);
    g.fillRect (0, 44, getWidth(), 1);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (44).reduced (10, 8);
    logo.setBounds (top.removeFromLeft (110));
    themeBox.setBounds (top.removeFromRight (150));
    top.removeFromRight (8);
    for (auto* b : { &monitorButton, &learnButton, &controllerButton, &pluginsButton, &audioButton })
    {
        b->setBounds (top.removeFromRight (b == &audioButton ? 110 : 100));
        top.removeFromRight (6);
    }
    status.setBounds (top);

    auto body = r.reduced (10);
    if (overlay) overlay->setBounds (r);
    if (wizard) wizard->setBounds (r);
    if (monitorVisible)
    {
        auto bottom = body.removeFromBottom (170);
        body.removeFromBottom (8);
        mappingList->setBounds (bottom.removeFromRight (bottom.getWidth() / 2).withTrimmedLeft (4));
        monitorText.setBounds (bottom.withTrimmedRight (4));
    }
    const int mixW = jlimit (170, 230, body.getWidth() / 6);
    const int deckW = (body.getWidth() - mixW - 16) / 2;
    deckA->setBounds (body.removeFromLeft (deckW));
    deckB->setBounds (body.removeFromRight (deckW));
    mixer.setBounds (body.reduced (8, 0));
    if (overlay && overlay->isVisible()) overlay->refresh();
}
} // namespace deckrig
