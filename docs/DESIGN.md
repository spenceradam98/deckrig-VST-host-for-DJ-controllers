# DeckRig: design (v0.2)

Two-deck DJ software with a VST3 host built in (AU too on macOS). Each deck plays
music files and/or live input and/or an instrument plugin, through its own chain of
plugins, into a DJ mixer, all driven from a DJ controller. First target: Adam's
Hercules DJControl Mix Ultra.

## What a deck is

```
track player ──────────┐
live input (optional) ─┼─► plugin chain (up to 8) ─► gain ─► 3-band EQ ─► filter ─┬─► volume ─► crossfader ─► master (outs 1/2)
instrument plugin ─────┘   (pads play it in INSTR mode)                           └─► CUE (PFL) ─► headphones (outs 3/4)
```

- Track player: WAV, AIFF, FLAC, Ogg and MP3, plus AAC/M4A through the system decoders
  on Mac and Windows (not yet tried on those systems), decoded into memory (up to 30 minutes per track). Play/pause, CDJ-style
  CUE (paused: set and preview while held; playing: back to cue and stop), tempo fader
  with 8/16/50 % ranges, jog wheel (touch = scratch in vinyl mode, edge = nudge while
  playing, seek while paused), SYNC (matches tempo and snaps beat phase to the other
  deck, then follows it), 8 hot cues, beat loops, beat jump. Cue and hot cues are
  remembered per file.
- Tempo detection: onset envelope, comb-filtered autocorrelation, fractional-lag search,
  then a beat-grid phase fit (`Source/core/TrackAnalysis.cpp`). Tests on synthetic
  tracks hit 92/124/128/140/174 BPM within 0.05 BPM and the grid within 1 % of a beat.
  Right-click the BPM readout to double or halve it when a track is ambiguous.
  No key lock (tempo changes pitch, like vinyl).
- Waveforms: a scrolling close-up with the beat grid, cue, hot cue and loop markers
  (drag it to scratch), and a full-track overview (click to jump).
- Library: pick a music folder (searched with subfolders), type to filter, LOAD A/B,
  double-click, or drag a row or any audio file onto a deck. The controller's browse
  encoder scrolls the list.
- Input: none, or a stereo pair of the audio interface (turntables, a mic, another app).
- Chain: VST3 effects and instruments. The track runs through the effects; an
  instrument adds its sound, played from the pads in INSTR mode (notes 36-43, velocity).
- Pads have four modes, like the Mix Ultra's mode buttons:
  HOT CUE (set/jump, Shift deletes), LOOP (1/2 to 16 beats, halve, double; Shift
  beat-jumps), FX (pads switch plugins 1-8 on and off), INSTR (play the instrument).
- Rig presets: save and recall a deck's whole plugin chain (every plugin's settings)
  and its macro assignments from the RIG button.
- 8 macro knobs per deck. Right-click a macro to point it at any parameter of any
  plugin in the chain. On the controller, Shift + filter/EQ knobs drive macros 1-4 and
  Shift + jog nudges whichever macro was used last (Shift + Load moves the focus).
- Mixer: master, headphone level, cue/master blend, crossfader with smooth, sharp and
  linear curves. Headphones need an interface with 4 outputs (the Mix Ultra has no audio
  interface of its own, so on a laptop with stereo out the CUE buttons do nothing yet).

## Controller mapping

A mapping file (JSON, `deckrig-mapping` format) has two layers:

- `controls`: the hardware. Each has an id, label, MIDI address (CC / note / pitch bend,
  channel, number), kind (absolute, relative, button, pad) and whether it has an LED.
- `bindings`: control id -> DeckRig target (`deck.A.volume`, `mixer.crossfader`,
  `deck.B.macro.3`, `deck.A.slot.2.bypass`, `deck.A.pads`, ... see `Source/core/Targets.h`).

Three ways to build one:

1. **Learn controller** (Controller menu): steps through every control in the file
   ("Move Deck A volume fader", "Press Deck B Play") and records what the hardware
   actually sends. Bindings are kept, so the Hercules draft becomes exact in a few minutes.
2. **MIDI learn**: click any knob, fader or button on screen, touch the hardware control.
   Right-click to clear.
3. Edit the JSON by hand. The MONITOR panel shows every incoming message and the full
   mapping, with DRAFT/OK per control.

Bindings can be Shift-only (`"shift": true`): while either deck's Shift is held, a
control uses its Shift bindings if it has any. Faders and knobs use soft takeover (they
take over only once they reach the on-screen value), switchable in the Controller menu.
Buttons and pads light their LEDs (note-on velocity 127/0 back to the controller):
play, cue, sync, PFL, pad modes, hot cues set, active loop, plugins on. Relative
controls support the three common encodings (two's complement, offset-64, sign-bit).

### Hercules DJControl Mix Ultra

Hercules doesn't publish a MIDI chart for this model, and neither Mixxx nor VirtualDJ's
public docs list its numbers. The bundled profile
(`profiles/hercules-djcontrol-mix-ultra.json`) has the Mix Ultra's real control layout
with MIDI numbers that follow other Hercules DJControl models (mixer on channel 1, decks
on channels 2/3, pads on 6/7, jog CC 0x0A relative). Every control is marked DRAFT until
"Learn controller" confirms it on the hardware.

Default bindings: everything does what its label says (Play, Cue, Sync, Load, PFL,
pitch fader = tempo, jog + touch, browse encoder = library, crossfader, master, EQ,
filter, volume). The four mode buttons pick the pad mode (Hotcue, Loop, FX, and
Neural = INSTR). Shift layer as above, plus Shift + Play = deck FX on/off.

## Threads

- Audio callback: renders both decks and the mixer. No blocking locks, no allocation;
  EQ/filter coefficients are recomputed in place when a knob moves. Transport commands
  (play, cue, jog...) arrive through a lock-free queue per deck and are applied at the
  start of each block; a track swap uses a try-lock, so the audio thread never waits.
- Loader thread: decodes and analyses tracks, then hands them to the message thread.
- MIDI callback: mixer and deck knobs are atomics written directly (lowest latency);
  pads go into each deck's `MidiMessageCollector`; anything touching a hosted plugin
  (macros, plugin bypass) is posted to the message thread.
- Message thread: UI, plugin loading, LEDs, hot-plug check every 2 s.

## Code layout

```
Source/core/Mapping.*     mapping model, MIDI decode, JSON (juce_core only, unit-tested)
Source/core/Targets.h     every controllable target
Source/core/MixerMath.h   crossfader curves, EQ/fader tapers, biquads (scalar, no AVX)
Source/core/TrackAnalysis.*  tempo + beat grid detection, waveform overview
Source/audio/TrackPlayer.*   file loading, transport, cues, loops, sync, scratch
Source/audio/Deck.*       one deck: player + input + chain + gain/EQ/filter, macros, rigs
Source/audio/HostEngine.* audio device, MIDI in/out, mixer, mapping dispatch, session
Source/hosting/PluginChain.*  serial plugin chain (from Scope) + MIDI and instruments
Source/ui/                themes, widgets, deck and mixer panels, learn overlay, wizard
tools/Snapshots.cpp       renders screenshots of every theme
tests/Tests.cpp           headless tests
```

## Platforms

macOS 11+ universal (Intel without AVX2 and Apple Silicon), Windows 10/11 64-bit with
static runtime. Same CMake settings as Tapehead Echo. Installers come later.

## Known limits / next ideas

- The Mix Ultra MIDI numbers are a draft until "Learn controller" is run on the hardware.
- No key lock (time-stretch), no recording, no streaming services, no beat-grid editor
  beyond x2 / /2.
- Headphone cue needs an audio interface with 4 outputs.
- Built and tested on Linux; Mac/Windows settings match the other products but those
  builds and installers come with the GitHub repo.
