#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace deckrig
{
enum class KnobStyle { Arc, Cap, Pointer };

struct Theme
{
    juce::String name, blurb;
    juce::Colour bg, panel, panelEdge, text, dimText, accent, deckA, deckB;
    juce::Colour knobBody, knobTrack, faderSlot, faderCap, button, buttonOn, buttonText;
    juce::Colour meterLow, meterMid, meterHigh, meterOff, learn;
    float radius = 6.0f;
    float glow = 0.0f;       // LED/arc bloom
    bool mono = false;       // monospaced lettering
    bool brushed = false;    // brushed-metal panel texture
    bool grid = false;       // perspective grid backdrop
    KnobStyle knob = KnobStyle::Arc;

    juce::Font font (float h, bool bold = false) const
    {
        auto o = juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain);
        if (mono) o = o.withName (juce::Font::getDefaultMonospacedFontName());
        return juce::Font (o);
    }
    juce::Colour deckColour (int d) const { return d == 0 ? deckA : deckB; }
};

inline juce::Colour rgb (juce::uint32 v, float a = 1.0f) { return juce::Colour (0xff000000 | v).withAlpha (a); }

inline const std::vector<Theme>& getThemes()
{
    static const std::vector<Theme> themes = []
    {
        std::vector<Theme> t;
        { Theme x; x.name = "Club Night"; x.blurb = "Matte black booth gear, cyan and magenta deck lights";
          x.bg = rgb (0x0b0c10); x.panel = rgb (0x16181f); x.panelEdge = rgb (0x2a2e3a); x.text = rgb (0xe8ecf4); x.dimText = rgb (0x8a93a6);
          x.accent = rgb (0x39d6ff); x.deckA = rgb (0x39d6ff); x.deckB = rgb (0xff4fb4);
          x.knobBody = rgb (0x23262f); x.knobTrack = rgb (0x30343f); x.faderSlot = rgb (0x07080b); x.faderCap = rgb (0xd9dde6);
          x.button = rgb (0x252832); x.buttonOn = rgb (0x39d6ff); x.buttonText = rgb (0xe8ecf4);
          x.meterLow = rgb (0x33e08a); x.meterMid = rgb (0xffd23f); x.meterHigh = rgb (0xff4a3d); x.meterOff = rgb (0x1d2027); x.learn = rgb (0xffb020);
          x.radius = 6; x.glow = 0.6f; t.push_back (x); }
        { Theme x; x.name = "Silver Booth"; x.blurb = "Brushed aluminium mixer with orange LEDs";
          x.bg = rgb (0x9ea3a8); x.panel = rgb (0xc9cdd1); x.panelEdge = rgb (0x7d8287); x.text = rgb (0x16181a); x.dimText = rgb (0x4a4f55);
          x.accent = rgb (0xff7a1a); x.deckA = rgb (0xff7a1a); x.deckB = rgb (0x1f7ae0);
          x.knobBody = rgb (0x2b2d30); x.knobTrack = rgb (0x8d9297); x.faderSlot = rgb (0x2a2c2f); x.faderCap = rgb (0x1b1c1e);
          x.button = rgb (0xb3b8bd); x.buttonOn = rgb (0xff7a1a); x.buttonText = rgb (0x16181a);
          x.meterLow = rgb (0x2fbf5a); x.meterMid = rgb (0xf2b81d); x.meterHigh = rgb (0xe0352a); x.meterOff = rgb (0x5a5f64); x.learn = rgb (0xd01fe0);
          x.radius = 3; x.brushed = true; x.knob = KnobStyle::Cap; t.push_back (x); }
        { Theme x; x.name = "Neon Grid"; x.blurb = "Synthwave sunset: purple haze, hot pink and teal";
          x.bg = rgb (0x120726); x.panel = rgb (0x1d0d3a, 0.92f); x.panelEdge = rgb (0x5b2a9c); x.text = rgb (0xf6e9ff); x.dimText = rgb (0xa98bd0);
          x.accent = rgb (0xff3fa4); x.deckA = rgb (0x22f0d8); x.deckB = rgb (0xff3fa4);
          x.knobBody = rgb (0x2a1450); x.knobTrack = rgb (0x3d2070); x.faderSlot = rgb (0x0a0318); x.faderCap = rgb (0xffd36e);
          x.button = rgb (0x2a1450); x.buttonOn = rgb (0xff3fa4); x.buttonText = rgb (0xf6e9ff);
          x.meterLow = rgb (0x22f0d8); x.meterMid = rgb (0xffd36e); x.meterHigh = rgb (0xff3f6a); x.meterOff = rgb (0x22103f); x.learn = rgb (0xffe14a);
          x.radius = 10; x.glow = 1.0f; x.grid = true; t.push_back (x); }
        { Theme x; x.name = "Walnut Rotary"; x.blurb = "Warm wood and cream, like a valve-era rotary mixer";
          x.bg = rgb (0x2e1c12); x.panel = rgb (0x4a2f1f); x.panelEdge = rgb (0x1c110a); x.text = rgb (0xf6e6c8); x.dimText = rgb (0xc7a77d);
          x.accent = rgb (0xffb347); x.deckA = rgb (0xffb347); x.deckB = rgb (0xe8d6a8);
          x.knobBody = rgb (0xefe2c4); x.knobTrack = rgb (0x2e1c12); x.faderSlot = rgb (0x1c110a); x.faderCap = rgb (0xefe2c4);
          x.button = rgb (0x5e3d29); x.buttonOn = rgb (0xffb347); x.buttonText = rgb (0xf6e6c8);
          x.meterLow = rgb (0xffcf73); x.meterMid = rgb (0xffa040); x.meterHigh = rgb (0xff5a2e); x.meterOff = rgb (0x2b1a10); x.learn = rgb (0x7fe0ff);
          x.radius = 14; x.knob = KnobStyle::Pointer; t.push_back (x); }
        { Theme x; x.name = "Terminal"; x.blurb = "Green-on-black monospace, for low-light booths";
          x.bg = rgb (0x000000); x.panel = rgb (0x050d07); x.panelEdge = rgb (0x1e5a2c); x.text = rgb (0x7dff9c); x.dimText = rgb (0x3c9a55);
          x.accent = rgb (0x7dff9c); x.deckA = rgb (0x7dff9c); x.deckB = rgb (0xd4ff6a);
          x.knobBody = rgb (0x07160b); x.knobTrack = rgb (0x10301a); x.faderSlot = rgb (0x020603); x.faderCap = rgb (0x7dff9c);
          x.button = rgb (0x07160b); x.buttonOn = rgb (0x2fa850); x.buttonText = rgb (0x7dff9c);
          x.meterLow = rgb (0x5dff86); x.meterMid = rgb (0xd4ff6a); x.meterHigh = rgb (0xff6a4a); x.meterOff = rgb (0x0a1f10); x.learn = rgb (0xffe14a);
          x.radius = 0; x.glow = 0.8f; x.mono = true; t.push_back (x); }
        { Theme x; x.name = "Daylight"; x.blurb = "Light, high-contrast layout for outdoor and daytime sets";
          x.bg = rgb (0xeef0f3); x.panel = rgb (0xffffff); x.panelEdge = rgb (0xc9ced6); x.text = rgb (0x111418); x.dimText = rgb (0x5b6370);
          x.accent = rgb (0x0057d9); x.deckA = rgb (0x0057d9); x.deckB = rgb (0xd93a00);
          x.knobBody = rgb (0xe3e6eb); x.knobTrack = rgb (0xc9ced6); x.faderSlot = rgb (0xc9ced6); x.faderCap = rgb (0x111418);
          x.button = rgb (0xe3e6eb); x.buttonOn = rgb (0x0057d9); x.buttonText = rgb (0x111418);
          x.meterLow = rgb (0x1a9c4a); x.meterMid = rgb (0xd99a00); x.meterHigh = rgb (0xd92a1a); x.meterOff = rgb (0xdde1e6); x.learn = rgb (0xc000c0);
          x.radius = 8; t.push_back (x); }
        return t;
    }();
    return themes;
}

inline const Theme& getTheme (int i)
{
    auto& t = getThemes();
    return t[(size_t) juce::jlimit (0, (int) t.size() - 1, i)];
}

// Draws knobs, faders and buttons in the current theme.
class ThemedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void setTheme (const Theme& t)
    {
        theme = &t;
        setColour (juce::ResizableWindow::backgroundColourId, t.bg);
        setColour (juce::Label::textColourId, t.text);
        setColour (juce::TextButton::buttonColourId, t.button);
        setColour (juce::TextButton::buttonOnColourId, t.buttonOn);
        setColour (juce::TextButton::textColourOffId, t.buttonText);
        setColour (juce::TextButton::textColourOnId, t.buttonOn.contrasting (0.9f));
        setColour (juce::ComboBox::backgroundColourId, t.button);
        setColour (juce::ComboBox::textColourId, t.buttonText);
        setColour (juce::ComboBox::outlineColourId, t.panelEdge);
        setColour (juce::ComboBox::arrowColourId, t.dimText);
        setColour (juce::PopupMenu::backgroundColourId, t.panel.withAlpha (1.0f));
        setColour (juce::PopupMenu::textColourId, t.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent);
        setColour (juce::PopupMenu::highlightedTextColourId, t.accent.contrasting (0.9f));
        setColour (juce::TextEditor::backgroundColourId, t.faderSlot);
        setColour (juce::TextEditor::textColourId, t.text);
        setColour (juce::TextEditor::outlineColourId, t.panelEdge);
        setColour (juce::Slider::textBoxTextColourId, t.text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ListBox::backgroundColourId, t.panel);
        setColour (juce::ToggleButton::textColourId, t.text);
        setColour (juce::ToggleButton::tickColourId, t.accent);
        setColour (juce::ScrollBar::thumbColourId, t.dimText);
        setColour (juce::AlertWindow::backgroundColourId, t.panel.withAlpha (1.0f));
        setColour (juce::AlertWindow::textColourId, t.text);
        setColour (juce::DocumentWindow::textColourId, t.text);
        getCurrentColourScheme().setUIColour (ColourScheme::UIColour::windowBackground, t.bg);
        getCurrentColourScheme().setUIColour (ColourScheme::UIColour::widgetBackground, t.panel.withAlpha (1.0f));
        getCurrentColourScheme().setUIColour (ColourScheme::UIColour::defaultText, t.text);
        getCurrentColourScheme().setUIColour (ColourScheme::UIColour::highlightedFill, t.accent);
    }

    const Theme& getTheme() const { return *theme; }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return theme->font (juce::jmin (13.0f, h * 0.55f), true); }
    juce::Font getLabelFont (juce::Label& l) override { return theme->font (l.getFont().getHeight()); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme->font (13.0f); }
    juce::Font getPopupMenuFont() override { return theme->font (14.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto c = on ? (b.getProperties().contains ("ledColour") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["ledColour"]) : theme->buttonOn)
                    : theme->button;
        if (down) c = c.darker (0.2f); else if (over) c = c.brighter (0.08f);
        const float rad = juce::jmin (theme->radius, r.getHeight() * 0.5f);
        if (on && theme->glow > 0)
        {
            g.setColour (c.withAlpha (0.25f * theme->glow));
            g.fillRoundedRectangle (r.expanded (2.5f), rad + 2.5f);
        }
        g.setColour (c);
        g.fillRoundedRectangle (r, rad);
        g.setColour (theme->panelEdge);
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
        const float rad = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = start + pos * (end - start);
        const auto col = s.getProperties().contains ("ring") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["ring"]) : theme->accent;
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const bool enabled = s.isEnabled();

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, rad - 2, rad - 2, 0, start, end, true);
        g.setColour (theme->knobTrack);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float from = bipolar ? (start + end) * 0.5f : start;
        if (enabled && std::abs (angle - from) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, rad - 2, rad - 2, 0, juce::jmin (from, angle), juce::jmax (from, angle), true);
            if (theme->glow > 0)
            {
                g.setColour (col.withAlpha (0.25f * theme->glow));
                g.strokePath (arc, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (col);
            g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const float bodyR = rad * (theme->knob == KnobStyle::Arc ? 0.68f : 0.74f);
        auto body = juce::Rectangle<float> (bodyR * 2, bodyR * 2).withCentre (centre);
        if (theme->knob == KnobStyle::Cap)
        {
            g.setGradientFill (juce::ColourGradient (theme->knobBody.brighter (0.35f), body.getX(), body.getY(), theme->knobBody.darker (0.3f), body.getRight(), body.getBottom(), false));
            g.fillEllipse (body);
            g.setColour (theme->knobBody.brighter (0.6f).withAlpha (0.4f));
            g.drawEllipse (body.reduced (bodyR * 0.25f), 1.0f);
        }
        else
        {
            g.setColour (theme->knobBody);
            g.fillEllipse (body);
        }
        g.setColour (theme->panelEdge);
        g.drawEllipse (body, 1.0f);

        const auto pointerCol = theme->knob == KnobStyle::Pointer ? theme->knobTrack : (enabled ? col.brighter (0.2f) : theme->dimText);
        juce::Path p;
        const float pl = bodyR * 0.85f;
        p.addRoundedRectangle (-1.6f, -pl, 3.2f, pl * 0.55f, 1.5f);
        g.setColour (theme->knob == KnobStyle::Cap ? juce::Colours::white : pointerCol);
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (centre));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        const bool vertical = style == juce::Slider::LinearVertical;
        auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
        const auto col = s.getProperties().contains ("ring") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["ring"]) : theme->accent;

        auto slot = vertical ? juce::Rectangle<float> (5.0f, r.getHeight()).withCentre (r.getCentre())
                             : juce::Rectangle<float> (r.getWidth(), 5.0f).withCentre (r.getCentre());
        g.setColour (theme->faderSlot);
        g.fillRoundedRectangle (slot, 2.5f);

        // Tick marks
        g.setColour (theme->dimText.withAlpha (0.5f));
        for (int i = 0; i <= 10; ++i)
        {
            if (vertical)
            {
                const float yy = r.getY() + r.getHeight() * (float) i / 10.0f;
                g.fillRect (slot.getRight() + 5.0f, yy - 0.5f, i % 5 == 0 ? 8.0f : 4.0f, 1.0f);
                g.fillRect (slot.getX() - (i % 5 == 0 ? 13.0f : 9.0f), yy - 0.5f, i % 5 == 0 ? 8.0f : 4.0f, 1.0f);
            }
            else
            {
                const float xx = r.getX() + r.getWidth() * (float) i / 10.0f;
                g.fillRect (xx - 0.5f, slot.getBottom() + 5.0f, 1.0f, i % 5 == 0 ? 7.0f : 3.0f);
            }
        }

        if (vertical)
        {
            g.setColour (col.withAlpha (0.8f));
            g.fillRoundedRectangle (slot.withTop (pos), 2.5f);
            auto cap = juce::Rectangle<float> (juce::jmin (w - 4.0f, 34.0f), 16.0f).withCentre ({ r.getCentreX(), pos });
            drawCap (g, cap, col);
        }
        else
        {
            auto cap = juce::Rectangle<float> (16.0f, juce::jmin (h - 4.0f, 30.0f)).withCentre ({ pos, r.getCentreY() });
            drawCap (g, cap, col);
        }
    }

    void drawCap (juce::Graphics& g, juce::Rectangle<float> cap, juce::Colour col)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (cap.translated (0, 2), 3.0f);
        g.setColour (theme->faderCap);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (col);
        if (cap.getWidth() > cap.getHeight())
            g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 6, 2.0f));
        else
            g.fillRect (cap.withSizeKeepingCentre (2.0f, cap.getHeight() - 6));
    }

private:
    const Theme* theme = &getThemes().front();
};
} // namespace deckrig
