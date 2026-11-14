#include "DeckWidgets.h"

namespace deckrig
{
using namespace juce;

static String formatTime (double s)
{
    s = jmax (0.0, s);
    const int m = (int) (s / 60.0);
    return String (m) + ":" + String (s - m * 60.0, 1).paddedLeft ('0', 4);
}

//==============================================================================
void WaveformView::paint (Graphics& g)
{
    auto& t = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    g.setColour (t.faderSlot);
    g.fillRoundedRectangle (r, jmin (4.0f, t.radius));

    auto track = deck.player.getTrack();
    const auto col = t.deckColour (deck.index);
    if (track == nullptr)
    {
        if (! isOverview)
        {
            g.setColour (t.dimText);
            g.setFont (t.font (12.0f));
            g.drawText (deck.player.loaded ? "" : "Drop a track here, or pick one in the library and press LOAD",
                        getLocalBounds(), Justification::centred);
        }
        return;
    }

    const auto& wave = track->wave;
    const double sr = track->sampleRate;
    const double len = track->lengthSeconds();
    const double pos = deck.player.position.load();
    const double bpm = deck.player.bpm.load(), fb = deck.player.firstBeat.load();
    const float mid = r.getCentreY(), half = r.getHeight() * 0.46f;

    double t0, t1;
    if (isOverview) { t0 = 0; t1 = len; }
    else { const double vis = secondsVisible / jmax (0.25, deck.player.tempoFactor.load()); t0 = pos - vis * 0.5; t1 = pos + vis * 0.5; }
    auto xOf = [&] (double s) { return r.getX() + (float) ((s - t0) / (t1 - t0)) * r.getWidth(); };

    // Loop region.
    if (deck.player.looping || deck.player.loopEnd > deck.player.loopStart)
    {
        const float x0 = xOf (deck.player.loopStart), x1 = xOf (deck.player.loopEnd);
        g.setColour (t.learn.withAlpha (deck.player.looping ? 0.22f : 0.08f));
        g.fillRect (Rectangle<float> (x0, r.getY(), jmax (1.0f, x1 - x0), r.getHeight()).getIntersection (r));
    }

    // Beat grid (close-up only).
    if (! isOverview && bpm > 0)
    {
        const double beat = 60.0 / bpm;
        double b = fb + std::floor ((t0 - fb) / beat) * beat;
        int idx = (int) std::floor ((t0 - fb) / beat);
        for (; b <= t1; b += beat, ++idx)
        {
            if (b < 0) continue;
            g.setColour (t.dimText.withAlpha (idx % 4 == 0 ? 0.55f : 0.22f));
            g.fillRect (xOf (b), r.getY(), idx % 4 == 0 ? 1.5f : 1.0f, r.getHeight());
        }
    }

    // Waveform columns.
    const int w = (int) r.getWidth();
    const double secPerPx = (t1 - t0) / jmax (1, w);
    for (int x = 0; x < w; ++x)
    {
        const double s0 = t0 + x * secPerPx, s1 = s0 + secPerPx;
        if (s1 < 0 || s0 > len) continue;
        const int b0 = jlimit (0, (int) wave.peak.size() - 1, (int) (s0 * sr / wave.block));
        const int b1 = jlimit (b0, (int) wave.peak.size() - 1, (int) (s1 * sr / wave.block));
        float pk = 0, lo = 0;
        for (int b = b0; b <= b1; ++b) { pk = jmax (pk, wave.peak[(size_t) b]); lo = jmax (lo, wave.low[(size_t) b]); }
        const float h = pk * half;
        const bool played = s0 < pos;
        auto c = col.interpolatedWith (t.text, 0.5f * (1.0f - lo));
        if (isOverview && played) c = c.withMultipliedAlpha (0.45f);
        g.setColour (c);
        g.fillRect ((float) x + r.getX(), mid - h, 1.0f, h * 2.0f);
    }

    // Cue and hot cue markers.
    g.setColour (t.meterMid);
    g.fillRect (xOf (deck.player.cuePoint) - 1.0f, r.getY(), 2.0f, r.getHeight());
    for (int i = 0; i < numHotcues; ++i)
    {
        const double h = deck.player.hotcues[(size_t) i];
        if (h < 0) continue;
        const float x = xOf (h);
        g.setColour (t.learn);
        g.fillRect (x - 0.5f, r.getY(), 1.0f, r.getHeight());
        if (! isOverview)
        {
            g.fillRect (x, r.getY(), 12.0f, 12.0f);
            g.setColour (t.learn.contrasting (0.9f));
            g.setFont (t.font (10.0f, true));
            g.drawText (String (i + 1), Rectangle<float> (x, r.getY(), 12.0f, 12.0f), Justification::centred);
        }
    }

    // Playhead.
    g.setColour (t.text);
    g.fillRect (xOf (pos) - 1.0f, r.getY(), 2.0f, r.getHeight());
}

void WaveformView::seekTo (float x)
{
    if (auto track = deck.player.getTrack())
        deck.player.push ({ TrackPlayer::Cmd::Seek, jlimit (0.0, 1.0, (double) (x / (float) getWidth())) });
}

void WaveformView::mouseDown (const MouseEvent& e)
{
    lastDragX = e.position.x;
    if (isOverview) seekTo (e.position.x);
    else deck.player.push ({ TrackPlayer::Cmd::JogTouch, 1.0 });
}

void WaveformView::mouseDrag (const MouseEvent& e)
{
    if (isOverview) { seekTo (e.position.x); return; }
    // Dragging the close-up scratches: one platter tick per few pixels.
    const float dx = e.position.x - lastDragX;
    lastDragX = e.position.x;
    deck.player.push ({ TrackPlayer::Cmd::Jog, -dx * 1.2 });
}

void WaveformView::mouseUp (const MouseEvent&)
{
    if (! isOverview) deck.player.push ({ TrackPlayer::Cmd::JogTouch, 0.0 });
}

//==============================================================================
void TrackInfo::paint (Graphics& g)
{
    auto& t = themeOf (*this);
    auto r = getLocalBounds().reduced (2, 0);
    auto track = deck.player.getTrack();
    auto right = r.removeFromRight (200);

    g.setColour (t.text);
    g.setFont (t.font (15.0f, true));
    String title = engine.isLoading (deck.index) ? "Loading..." : track != nullptr ? track->title : "No track";
    g.drawText (title, r.removeFromTop (r.getHeight() / 2 + 2), Justification::bottomLeft, true);
    g.setFont (t.font (12.0f));
    g.setColour (t.dimText);
    if (track != nullptr)
    {
        const double pos = deck.player.position, len = track->lengthSeconds();
        g.drawText (formatTime (pos) + "   -" + formatTime (len - pos) + "   " + (deck.player.looping ? "LOOP " + String (deck.player.loopBeats.load(), 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " beats" : String()),
                    r, Justification::centredLeft, true);
    }

    const double bpm = deck.player.effectiveBpm.load();
    const double pct = (deck.player.tempoFactor.load() - 1.0) * 100.0;
    g.setColour (deck.player.synced ? t.deckColour (deck.index) : t.text);
    g.setFont (t.font (20.0f, true));
    g.drawText (bpm > 0 ? String (bpm, 1) : String ("--.-"), right.removeFromTop (right.getHeight() / 2 + 4), Justification::bottomRight);
    g.setFont (t.font (11.0f));
    g.setColour (t.dimText);
    g.drawText ((pct >= 0 ? "+" : "") + String (pct, 1) + "%  of " + (deck.player.bpm > 0 ? String (deck.player.bpm.load(), 1) : String ("?")) + " BPM" + (deck.player.synced ? "  SYNC" : ""),
                right, Justification::centredRight);
}

void TrackInfo::mouseDown (const MouseEvent& e)
{
    if (! deck.player.loaded) return;
    PopupMenu m;
    m.addSectionHeader ("Detected tempo " + String (deck.player.bpm.load(), 2) + " BPM");
    m.addItem ("Double tempo (x2)", [this] { deck.player.push ({ TrackPlayer::Cmd::ScaleBpm, 2.0 }); });
    m.addItem ("Halve tempo (/2)", [this] { deck.player.push ({ TrackPlayer::Cmd::ScaleBpm, 0.5 }); });
    m.addSeparator();
    m.addItem ("Vinyl mode (jog touch scratches)", true, deck.player.vinylMode.load(), [this] { deck.player.vinylMode = ! deck.player.vinylMode.load(); });
    m.addItem ("Eject track", [this] { deck.player.setTrack (nullptr); });
    ignoreUnused (e);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
}
} // namespace deckrig
