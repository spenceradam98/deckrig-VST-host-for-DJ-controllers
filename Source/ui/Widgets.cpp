#include "Widgets.h"

namespace deckrig
{
using namespace juce;

//==============================================================================
Knob::Knob (HostEngine& e, const String& t, const String& caption, bool bipolar, float def) : engine (e), target (t)
{
    setTargetId (*this, t);
    slider.setRange (0.0, 1.0);
    slider.setDoubleClickReturnValue (true, def);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.getProperties().set ("bipolar", bipolar);
    slider.onValueChange = [this] { engine.setValue (target, (float) slider.getValue()); };
    slider.setInterceptsMouseClicks (true, true);
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
    label.setText (caption, dontSendNotification);
    label.setJustificationType (Justification::centred);
    label.setFont (FontOptions (11.0f));
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
    syncFromEngine();
}

void Knob::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick)
        onRightClick (e);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromBottom (14));
    slider.setBounds (r);
}

void Knob::syncFromEngine()
{
    if (slider.isMouseButtonDown()) return;
    slider.setValue (engine.getValue (target), dontSendNotification);
}

//==============================================================================
Fader::Fader (HostEngine& e, const String& t, bool vertical, float def) : engine (e), target (t)
{
    setTargetId (*this, t);
    slider.setSliderStyle (vertical ? Slider::LinearVertical : Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRange (0.0, 1.0);
    slider.setDoubleClickReturnValue (true, def);
    slider.onValueChange = [this] { engine.setValue (target, (float) slider.getValue()); };
    addAndMakeVisible (slider);
    syncFromEngine();
}

void Fader::syncFromEngine()
{
    if (slider.isMouseButtonDown()) return;
    slider.setValue (engine.getValue (target), dontSendNotification);
}

//==============================================================================
ToggleKey::ToggleKey (HostEngine& e, const String& t, const String& text) : TextButton (text), engine (e), target (t)
{
    setTargetId (*this, t);
    onClick = [this] { engine.setToggle (target, ! engine.getToggle (target)); syncFromEngine(); };
    syncFromEngine();
}

void ToggleKey::syncFromEngine()
{
    const bool on = engine.getToggle (target);
    if (on != getToggleState())
        setToggleState (on, dontSendNotification);
}

//==============================================================================
void StereoMeter::paint (Graphics& g)
{
    auto& t = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    const float w = (r.getWidth() - 2.0f) * 0.5f;
    const int segs = jmax (8, (int) (r.getHeight() / 5.0f));
    const float segH = r.getHeight() / (float) segs;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float level = (ch == 0 ? left : right).load();
        const float db = level > 0 ? 20.0f * std::log10 (level) : -100.0f;
        const float norm = jlimit (0.0f, 1.0f, (db + 48.0f) / 54.0f);  // -48..+6 dBFS
        const int lit = roundToInt (norm * (float) segs);
        const float x = r.getX() + (float) ch * (w + 2.0f);
        for (int s = 0; s < segs; ++s)
        {
            const float frac = (float) s / (float) segs;
            Colour c = frac > 0.89f ? t.meterHigh : frac > 0.72f ? t.meterMid : t.meterLow;
            if (s >= lit) c = t.meterOff;
            g.setColour (c);
            g.fillRect (x, r.getBottom() - (float) (s + 1) * segH + 1.0f, w, segH - 1.5f);
        }
    }
}

//==============================================================================
class ChainList::PluginWindow : public DocumentWindow
{
public:
    PluginWindow (AudioProcessor& p, std::function<void (AudioProcessor*)> onClose)
        : DocumentWindow (p.getName(), Colours::darkgrey, DocumentWindow::closeButton), processor (p), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        if (auto* ed = p.createEditorIfNeeded())
            setContentNonOwned (ed, true);
        else
            setContentOwned (new GenericAudioProcessorEditor (p), true);
        auto* ed = dynamic_cast<AudioProcessorEditor*> (getContentComponent());
        setResizable (ed != nullptr && ed->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~PluginWindow() override
    {
        if (auto* ed = dynamic_cast<AudioProcessorEditor*> (getContentComponent()))
        {
            processor.editorBeingDeleted (ed);
            clearContentComponent();
            delete ed;
        }
    }

    void closeButtonPressed() override { MessageManager::callAsync ([c = closed, p = &processor] { c (p); }); }

    AudioProcessor& processor;

private:
    std::function<void (AudioProcessor*)> closed;
};

ChainList::ChainList (HostEngine& e, Deck& d, std::function<void()> scanner) : engine (e), deck (d), openScanner (std::move (scanner))
{
    addButton.onClick = [this] { showAddMenu(); };
    addButton.setTooltip ("Add a VST3 effect or instrument to this deck");
    addAndMakeVisible (addButton);
    deck.chain.addChangeListener (this);
    rebuild();
}

ChainList::~ChainList()
{
    deck.chain.removeChangeListener (this);
    windows.clear();
}

void ChainList::closeAllWindows() { windows.clear(); }

void ChainList::rebuild()
{
    windows.erase (std::remove_if (windows.begin(), windows.end(), [this] (auto& w)
    {
        for (int i = 0; i < deck.chain.size(); ++i)
            if (deck.chain.getPlugin (i) == &w->processor) return false;
        return true;
    }), windows.end());

    deck.validateMacros();
    slotButtons.clear();
    for (int i = 0; i < deck.chain.size(); ++i)
    {
        auto* p = deck.chain.getPlugin (i);
        const auto target = deckTarget (deck.index, "slot." + String (i + 1) + ".bypass");
        auto* b = slotButtons.add (new ToggleKey (engine, target, String (i + 1) + "  " + (p != nullptr ? p->getName() : String ("?"))));
        b->setTooltip ("Click to open the plugin, right-click for options. Lit = active.");
        b->setLed (themeOf (*this).deckColour (deck.index).withAlpha (0.55f));
        b->onClick = [this, i]
        {
            if (std::exchange (suppressClick, false)) return;
            openEditor (i);
        };
        b->addMouseListener (this, false);
        addAndMakeVisible (b);
    }
    addButton.setEnabled (deck.chain.size() < maxSlots);
    resized();
    repaint();
}

void ChainList::mouseDown (const MouseEvent& e)
{
    const int idx = slotButtons.indexOf (dynamic_cast<ToggleKey*> (e.eventComponent));
    if (idx >= 0 && e.mods.isPopupMenu())
    {
        suppressClick = true;
        showSlotMenu (idx);
    }
}

void ChainList::showAddMenu()
{
    auto types = engine.library.knownPlugins.getTypes();
    PopupMenu m;
    if (types.isEmpty())
        m.addItem ("No plugins found yet. Scan for plugins...", [this] { if (openScanner) openScanner(); });
    else
    {
        KnownPluginList::addToMenu (m, types, KnownPluginList::sortByManufacturer);
        m.addSeparator();
        m.addItem ("Scan for plugins...", [this] { if (openScanner) openScanner(); });
    }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&addButton), [this, types] (int result)
    {
        const int idx = KnownPluginList::getIndexChosenByMenu (types, result);
        if (! isPositiveAndBelow (idx, types.size())) return;
        String error;
        if (! deck.chain.addPlugin (types.getReference (idx), error))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Could not load plugin", error);
        else
            openEditor (deck.chain.size() - 1);
    });
}

void ChainList::showSlotMenu (int index)
{
    auto& chain = deck.chain;
    PopupMenu m;
    m.addItem ("Open editor", [this, index] { openEditor (index); });
    m.addItem ("Active", true, ! chain.isBypassed (index), [this, index] { deck.chain.setBypassed (index, ! deck.chain.isBypassed (index)); });
    m.addSeparator();
    auto move = [this] (int from, int to)
    {
        for (auto& a : deck.macros)  // macros follow their plugin
            if (a.slot == from) a.slot = to; else if (a.slot == to) a.slot = from;
        deck.chain.movePlugin (from, to);
    };
    m.addItem ("Move up", index > 0, false, [move, index] { move (index, index - 1); });
    m.addItem ("Move down", index < chain.size() - 1, false, [move, index] { move (index, index + 1); });
    m.addSeparator();
    m.addItem ("Remove", [this, index]
    {
        if (auto* p = deck.chain.getPlugin (index)) closeEditor (p);
        // Macros pointing at this or later slots shift with it.
        for (auto& a : deck.macros)
        {
            if (a.slot == index) a.slot = a.param = -1;
            else if (a.slot > index) --a.slot;
        }
        deck.chain.removePlugin (index);
    });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (slotButtons[index]));
}

void ChainList::openEditor (int index)
{
    auto* p = deck.chain.getPlugin (index);
    if (p == nullptr) return;
    for (auto& w : windows)
        if (&w->processor == p) { w->toFront (true); return; }
    windows.push_back (std::make_unique<PluginWindow> (*p, [this] (AudioProcessor* proc) { closeEditor (proc); }));
}

void ChainList::closeEditor (AudioProcessor* p)
{
    windows.erase (std::remove_if (windows.begin(), windows.end(), [p] (auto& w) { return &w->processor == p; }), windows.end());
}

void ChainList::resized()
{
    auto r = getLocalBounds().reduced (6);
    const int rowH = jlimit (18, 26, (r.getHeight() - 4 * 7) / (maxSlots + 1));
    for (auto* b : slotButtons)
    {
        b->setBounds (r.removeFromTop (rowH));
        r.removeFromTop (4);
    }
    addButton.setBounds (r.removeFromTop (rowH));
}

void ChainList::paint (Graphics& g)
{
    auto& t = themeOf (*this);
    g.setColour (t.faderSlot.withAlpha (0.6f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), t.radius);
    if (deck.chain.size() == 0)
    {
        g.setColour (t.dimText);
        g.setFont (t.font (12.0f));
        g.drawFittedText ("No plugins on this deck yet.\nAdd an instrument to play it from the pads,\nor effects to process the deck's input.",
                          getLocalBounds().reduced (10).withTrimmedTop (40), Justification::centredTop, 4);
    }
}
} // namespace deckrig
