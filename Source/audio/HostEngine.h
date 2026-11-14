#pragma once
#include "Deck.h"
#include "core/Mapping.h"
#include <deque>

namespace deckrig
{
struct MixerParams
{
    std::atomic<float> crossfader { 0.5f }, master { 0.8f }, cueMix { 0.0f }, cueLevel { 0.8f };
    std::atomic<int> curve { (int) XfadeCurve::Smooth };
    std::atomic<float> meterL { 0 }, meterR { 0 };
};

// Owns the audio device, both decks, the mixer and the controller mapping.
//
// Threads: the audio callback renders; the MIDI callback applies mixer/deck
// knob values straight to atomics (lowest latency) and queues pads into each
// deck's MidiMessageCollector; anything that touches a hosted plugin (macros,
// per-plugin bypass) is posted to the message thread.
class HostEngine : public juce::AudioIODeviceCallback,
                   public juce::MidiInputCallback,
                   public juce::ChangeBroadcaster,
                   private juce::Timer
{
public:
    HostEngine();
    ~HostEngine() override;

    void start (const juce::XmlElement* savedDeviceState);
    void shutdown();

    juce::AudioDeviceManager deviceManager;
    PluginLibrary library;
    std::array<std::unique_ptr<Deck>, numDecks> decks;
    MixerParams mixer;

    // ---- Targets (message thread) ----
    float getValue (const juce::String& target) const;
    void setValue (const juce::String& target, float value01);
    bool getToggle (const juce::String& target) const;
    void setToggle (const juce::String& target, bool on);
    void trigger (const juce::String& target);          // any thread
    bool getLed (const juce::String& target) const;     // state shown on the controller
    void padPress (int deck, int pad, bool down, int velocity, bool shift);  // any thread
    bool isShiftHeld() const noexcept { return shiftMask.load() != 0; }

    // ---- Tracks ----
    juce::AudioFormatManager formats;
    void loadTrack (int deck, const juce::File&);       // async; onTrackLoaded fires when done
    std::function<void (int deck, const juce::String& error)> onTrackLoaded;
    bool isLoading (int deck) const { return loading[(size_t) deck].load(); }

    // ---- Library (the UI owns the list; the controller scrolls it) ----
    std::atomic<int> librarySelection { 0 }, librarySize { 0 };
    std::function<void (int deck)> onLoadSelected;      // message thread

    std::atomic<bool> softTakeover { true };

    // ---- Mapping ----
    MappingProfile getProfile() const;
    void setProfile (const MappingProfile&);
    juce::String getBindingSummary (const juce::String& target) const;  // "" if unmapped

    // MIDI learn: the next suitable message binds to this target.
    void beginLearn (const juce::String& target);
    void cancelLearn();
    juce::String getLearnTarget() const;
    std::function<void (const juce::String& target, const juce::String& controlLabel)> onLearned;

    // Raw capture for the "learn controller" wizard. While set, mapping is suspended.
    std::function<void (const RawMidi&)> onCapture;

    // MIDI monitor: recent messages as text, newest last.
    juce::StringArray getMonitorLines() const;
    void clearMonitor();
    juce::String getLastMidiDevice() const;

    // ---- Session ----
    std::unique_ptr<juce::XmlElement> createSessionXml() const;
    void restoreSessionXml (const juce::XmlElement&);

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    // MidiInputCallback
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

private:
    void timerCallback() override;
    void enableNewMidiInputs();
    void refreshMidiOutput();
    void refreshLeds (bool force);
    void applyEvent (const RawMidi&);   // MIDI thread, profileLock held
    void applyToTarget (const ControlDef&, const Binding&, const RawMidi&);
    std::atomic<float>* findFloat (const juce::String& target) const;
    std::atomic<bool>* findBool (const juce::String& target) const;
    static bool parseDeckTarget (const juce::String& target, int& deck, juce::String& rest);
    void addMonitorLine (const juce::String&);
    void momentary (const juce::String& target, bool down);
    void rememberTrack (int deck);
    void loadTrackCache();
    void saveTrackCache();

    juce::ThreadPool loader { 1 };
    std::array<std::atomic<bool>, numDecks> loading {};
    std::array<int, numDecks> loadGeneration {}, savedChange {};
    juce::var trackCache;     // path -> { cue, hotcues[], bpm }
    bool trackCacheDirty = false;
    std::atomic<int> shiftMask { 0 };
    std::map<juce::String, float> lastHardware;  // soft takeover, MIDI thread only

    mutable juce::CriticalSection profileLock;
    MappingProfile profile;
    juce::String learnTarget;

    mutable juce::CriticalSection monitorLock;
    std::deque<juce::String> monitor;
    juce::String lastMidiDevice;

    std::unique_ptr<juce::MidiOutput> ledOut;
    juce::String ledOutId;
    std::map<juce::String, bool> ledSent;  // control id -> last state sent
    juce::Array<juce::MidiDeviceInfo> knownInputs;

    juce::AudioBuffer<float> mixBuffer, cueBuffer;
    int maxBlock = 512;
    double sampleRate = 44100.0;
    std::array<juce::SmoothedValue<float>, numDecks> deckGain, deckCue;
    juce::SmoothedValue<float> masterGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostEngine)
};
} // namespace deckrig
