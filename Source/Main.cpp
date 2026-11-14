#include "ui/MainComponent.h"

namespace deckrig
{
using namespace juce;

class DeckRigApplication : public JUCEApplication
{
public:
    const String getApplicationName() override { return DECKRIG_PRODUCT_NAME; }
    const String getApplicationVersion() override { return DECKRIG_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const String&) override
    {
        PropertiesFile::Options o;
        o.applicationName = DECKRIG_PRODUCT_NAME;
        o.folderName = String (DECKRIG_BRAND_NAME) + "/" + DECKRIG_PRODUCT_NAME;
        o.filenameSuffix = "settings";
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters (o);
        auto* settings = props.getUserSettings();

        engine = std::make_unique<HostEngine>();

        // Mapping: last used, else the Hercules draft.
        auto mapFile = MainComponent::getMappingFile();
        std::optional<MappingProfile> profile;
        if (mapFile.existsAsFile())
            profile = MappingProfile::fromJson (mapFile.loadFileAsString());
        engine->setProfile (profile.value_or (MainComponent::loadFactoryProfile()));

        auto audioState = settings->getXmlValue ("audioDevice");
        engine->start (audioState.get());
        if (auto session = settings->getXmlValue ("session"))
            engine->restoreSessionXml (*session);

        window = std::make_unique<MainWindow> (*engine, settings);
    }

    void shutdown() override
    {
        if (window != nullptr)
            if (auto* mc = dynamic_cast<MainComponent*> (window->getContentComponent()))
                mc->saveState();
        window.reset();
        engine.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public DocumentWindow
    {
    public:
        MainWindow (HostEngine& e, PropertiesFile* s)
            : DocumentWindow (DECKRIG_PRODUCT_NAME, Colours::black, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (e, s), true);
            setResizable (true, true);
            setResizeLimits (1024, 640, 4096, 2400);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    ApplicationProperties props;
    std::unique_ptr<HostEngine> engine;
    std::unique_ptr<MainWindow> window;
};
} // namespace deckrig

START_JUCE_APPLICATION (deckrig::DeckRigApplication)
