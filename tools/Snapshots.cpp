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

    {
        MainComponent mc (engine, nullptr);
        mc.setSize (1280, 760);
        auto& themes = getThemes();
        for (int i = 0; i < (int) themes.size(); ++i)
        {
            mc.setTheme (i);
            save (mc, dir.getChildFile ("theme-" + String (i + 1) + "-" + themes[(size_t) i].name.replaceCharacter (' ', '-').toLowerCase() + ".png"));
        }
        mc.setTheme (0);
        mc.setMonitorVisible (true);
        mc.setLearnMode (true);
        engine.beginLearn ("deck.A.eqMid");
        mc.setSize (1280, 900);
        save (mc, dir.getChildFile ("midi-learn.png"));
        engine.cancelLearn();
        mc.setLearnMode (false);
    }
    return 0;
}
