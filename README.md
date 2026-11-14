# DeckRig

Two-deck DJ software with a VST3 host built in. Play tracks, live input or instrument
plugins through a chain of your plugins on each deck, mix them on a DJ mixer, and drive
it all from a DJ controller. First controller: Hercules DJControl Mix Ultra.
Built with JUCE 8 and CMake.

![Themes](docs/screenshots/themes.png)

- Track decks: play/CDJ cue, tempo (8/16/50 %), jog scratch and nudge, sync with beat
  phase, 8 hot cues, beat loops, beat jump, waveforms, automatic BPM and beat grid
- Music library: folder browser with search, controller scrolling, drag and drop
- Each deck also takes live input and instrument plugins, up to 8 plugins, 8 macros,
  and saves/loads its whole rig
- Pad modes: hot cue, loop, plugin on/off, play instrument; Shift layer; soft takeover
- 3-band kill EQ, bipolar filter, gain, volume, CUE per deck; smooth/sharp/linear crossfader
- MIDI learn (click a control, touch the hardware), guided "Learn controller" wizard,
  MIDI monitor, JSON mapping files, LED feedback
- 6 themes: Club Night, Silver Booth, Neon Grid, Walnut Rotary, Terminal, Daylight
- Session (chains, plugin states, macros, mixer) and settings are restored on launch

See [docs/DESIGN.md](docs/DESIGN.md) for the design, signal flow and mapping format.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release        # fetches JUCE 8.0.4
cmake --build build --config Release
ctest --test-dir build -C Release                 # mapping + mixer tests
```

App: `build/DeckRig_artefacts/Release/`. `-DJUCE_DIR=/path/to/JUCE` uses a local JUCE.
`-DDECKRIG_BUILD_TOOLS=ON` adds `DeckRigSnapshots`, which renders the screenshots.

Linux needs: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev
libfreetype-dev libfontconfig1-dev libgl1-mesa-dev`.
