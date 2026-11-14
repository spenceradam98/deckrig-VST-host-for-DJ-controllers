# DeckRig: design plan (v0.1)

A standalone app that hosts VST3 plugins (and AU on macOS) on two decks and lets a
DJ controller play them: faders, EQ and filter knobs, crossfader, jog wheels, pads,
buttons and their LEDs. First target: Adam's Hercules DJControl Mix Ultra.

## What a deck is

```
live input (optional) ─┐
                       ├─► plugin chain (up to 8) ─► gain ─► 3-band EQ ─► filter ─┬─► volume ─► crossfader ─► master (outs 1/2)
instrument plugin ─────┘   (pads play the instrument)                             └─► CUE (PFL) ─► headphones (outs 3/4)
```

- Input: none, or a stereo pair of the audio interface (turntables, a DJ app, a mic).
- Chain: VST3 effects and instruments. An instrument becomes the deck's sound source;
  the 8 pads send it notes 36-43 with velocity.
- 8 macro knobs per deck. Right-click a macro to point it at any parameter of any
  plugin in the chain. The pitch fader drives macro 1 by default; the jog wheel nudges
  whichever macro was used last ("select next macro" moves the focus).
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

Buttons bound to on/off targets light their LED (note-on velocity 127/0 back to the
controller). Relative controls support the three common encodings (two's complement,
offset-64, sign-bit).

### Hercules DJControl Mix Ultra

Hercules doesn't publish a MIDI chart for this model, and neither Mixxx nor VirtualDJ's
public docs list its numbers. The bundled profile
(`profiles/hercules-djcontrol-mix-ultra.json`) has the Mix Ultra's real control layout
with MIDI numbers that follow other Hercules DJControl models (mixer on channel 1, decks
on channels 2/3, pads on 6/7, jog CC 0x0A relative). Every control is marked DRAFT until
"Learn controller" confirms it on the hardware.

Default bindings: volume/EQ/filter/crossfader/master as labelled; pitch fader -> macro 1;
jog -> nudge selected macro; Play/Cue/Sync -> plugins 1/2/3 on/off; Hotcue/Loop/Neural
mode buttons -> plugins 4/5/6 on/off; FX mode -> whole chain bypass; Load -> next macro;
PFL -> headphone cue; pads -> deck instrument.

## Threads

- Audio callback: renders both decks and the mixer. No locks, no allocation; EQ/filter
  coefficients are recomputed in place when a knob moves.
- MIDI callback: mixer and deck knobs are atomics written directly (lowest latency);
  pads go into each deck's `MidiMessageCollector`; anything touching a hosted plugin
  (macros, plugin bypass) is posted to the message thread.
- Message thread: UI, plugin loading, LEDs, hot-plug check every 2 s.

## Code layout

```
Source/core/Mapping.*     mapping model, MIDI decode, JSON (juce_core only, unit-tested)
Source/core/Targets.h     every controllable target
Source/core/MixerMath.h   crossfader curves, EQ/fader tapers, biquads (scalar, no AVX)
Source/audio/Deck.*       one deck: input + chain + gain/EQ/filter, macros
Source/audio/HostEngine.* audio device, MIDI in/out, mixer, mapping dispatch, session
Source/hosting/PluginChain.*  serial plugin chain (from Scope) + MIDI and instruments
Source/ui/                themes, widgets, deck and mixer panels, learn overlay, wizard
tools/Snapshots.cpp       renders screenshots of every theme
tests/Tests.cpp           headless tests
```

## Platforms

macOS 11+ universal (Intel without AVX2 and Apple Silicon), Windows 10/11 64-bit with
static runtime. Same CMake settings as Tapehead Echo. Installers come later.

## Next milestones

- v0.2: verify the Mix Ultra map on hardware, soft takeover for faders, Shift layer,
  per-deck preset slots (save/recall a whole chain + macros), pad LED colours.
- v0.3: optional track player per deck (play/cue/sync/jog scratching on audio files),
  if wanted. See open decisions.
- Installers and code signing alongside the other products.
