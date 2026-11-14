// Renders the UI in every theme to PNG, without opening an audio device.
// Usage: DeckRigSnapshots <output-dir>   (on headless Linux: xvfb-run -a ...)
#include "ui/MainComponent.h"

using namespace juce;
using namespace deckrig;

static void save (Component& c, const File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    FileOutputStream out (f);
    PNGImageFormat().writeImageToStream (img, out);
    std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    File dir = argc > 1 ? File::getCurrentWorkingDirectory().getChildFile (argv[1]) : File::getCurrentWorkingDirectory();
    dir.createDirectory();

    HostEngine engine;
    engine.setProfile (MainComponent::loadFactoryProfile());
    // Some movement so the screenshots aren't all at rest.
    engine.setValue ("deck.A.volume", 0.9f); engine.setValue ("deck.B.volume", 0.65f);
    engine.setValue ("deck.A.eqLow", 0.25f); engine.setValue ("deck.B.filter", 0.72f);
    engine.setValue ("deck.A.eqHigh", 0.62f); engine.setValue ("mixer.crossfader", 0.38f);
    engine.setToggle ("deck.A.pfl", true);
    engine.decks[0]->meterL = 0.5f; engine.decks[0]->meterR = 0.42f;
    engine.decks[1]->meterL = 0.2f; engine.decks[1]->meterR = 0.25f;
    engine.mixer.meterL = 0.6f; engine.mixer.meterR = 0.55f;

    // Tracks: argv[2] / argv[3] if given (loaded synchronously for the screenshot).
    for (int d = 0; d < numDecks; ++d)
    {
        if (argc <= 2 + d) break;
        String err;
        File f = File::getCurrentWorkingDirectory().getChildFile (argv[2 + d]);
        if (auto t = deckrig::loadTrack (engine.formats, f, err))
        {
            std::printf ("%s: %.2f BPM, first beat %.3f s, %.1f s long\n", f.getFileName().toRawUTF8(), t->beats.bpm, t->beats.firstBeat, t->lengthSeconds());
            engine.decks[(size_t) d]->player.setTrack (t);
            engine.decks[(size_t) d]->player.position = d == 0 ? 6.3 : 12.0;
            engine.decks[(size_t) d]->player.hotcues[0] = t->beats.firstBeat;
            engine.decks[(size_t) d]->player.hotcues[1] = d == 0 ? 3.9 : 9.0;
            engine.decks[(size_t) d]->player.effectiveBpm = t->beats.bpm;
            if (d == 0) { engine.decks[0]->player.loopStart = t->beats.firstBeat + 8 * 60.0 / t->beats.bpm; engine.decks[0]->player.loopEnd = engine.decks[0]->player.loopStart + 4 * 60.0 / t->beats.bpm; engine.decks[0]->player.looping = true; engine.decks[0]->player.loopBeats = 4.0; }
        }
        else
            std::printf ("could not load %s: %s\n", argv[2 + d], err.toRawUTF8());
    }
    engine.decks[1]->params.padMode = padLoop;
    engine.decks[0]->player.playing = true;
    engine.decks[1]->params.tempo = 0.62f;

    {
        MainComponent mc (engine, nullptr);
        mc.setSize (1440, 900);
        auto& themes = getThemes();
        for (int i = 0; i < (int) themes.size(); ++i)
        {
            mc.setTheme (i);
            mc.refreshNow();
            save (mc, dir.getChildFile ("theme-" + String (i + 1) + "-" + themes[(size_t) i].name.replaceCharacter (' ', '-').toLowerCase() + ".png"));
        }
        mc.setTheme (0);
        mc.setMonitorVisible (true);
        mc.setLearnMode (true);
        engine.beginLearn ("deck.A.eqMid");
        mc.refreshNow();
        mc.setSize (1440, 900);
        save (mc, dir.getChildFile ("midi-learn.png"));
        engine.cancelLearn();
        mc.setLearnMode (false);
    }
    return 0;
}
