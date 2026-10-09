# SWINGBY User Guide

SW IN07 SWINGBY — the orbital preset synth (SEVENTHWELL)

〔VERSION〕 This guide covers plug-in version 〔VERSION〕.

---

## 1. About SWINGBY

SWINGBY is a lightweight preset synth you can play the moment you load it. Its 128 factory presets are all matched to −16 LUFS (BS.1770 loudness, measured on a phrase for each category), so you compare tone, not volume.

The sound is drawn as a planetary system: the star in the middle is the sound itself, the four orbits are its layers. Move the sound with gravity, flyby and the orbit LFO, and the system moves with it.

- 4 layers (each with an Analog, Wavetable, FM or Sample oscillator, a filter and two envelopes)
- Up to 32 voices, Poly / Mono / Legato, up to 8 unison voices
- 2 LFOs (including the elliptical Orbit shape), an 8-slot mod matrix, 8 macros, flyby, gravity
- Arpeggiator (16 steps) and trance gate
- 6 effects (Drive, Chorus, Delay, Reverb, EQ, Limit) in any order
- Save your own presets; load presets from a CLAP host's preset browser
- No sample library: the waveforms are generated when the plug-in starts

## 2. Requirements

| | |
| --- | --- |
| Windows | Windows 10 / 11, 64-bit. The window uses the Microsoft Edge WebView2 Runtime (included in Windows 11; on Windows 10 install it from Microsoft if it is missing) |
| macOS | macOS 11 or later (universal: Apple silicon and Intel) |
| Formats | CLAP, VST3 (Windows, macOS), Audio Units (macOS) |
| Host | Any DAW that loads instruments in one of these formats |

〔The DAWs and versions it has been tested with will be listed here.〕

## 3. Installation

Unzip the download and copy the format you use to the folder below. Restart your DAW; "SW IN07 SWINGBY" by SEVENTHWELL appears in its plug-in list.

**Windows**

| Format | File | Copy to |
| --- | --- | --- |
| VST3 | `SW IN07 SWINGBY.vst3` (the whole folder) | `C:\Program Files\Common Files\VST3\` |
| CLAP | `SW IN07 SWINGBY.clap` | `C:\Program Files\Common Files\CLAP\` |

**macOS**

| Format | File | Copy to |
| --- | --- | --- |
| Audio Units | `SW IN07 SWINGBY.component` | `/Library/Audio/Plug-Ins/Components/` (or `~/Library/Audio/Plug-Ins/Components/`) |
| VST3 | `SW IN07 SWINGBY.vst3` | `/Library/Audio/Plug-Ins/VST3/` |
| CLAP | `SW IN07 SWINGBY.clap` | `/Library/Audio/Plug-Ins/CLAP/` |

**Uninstalling**: delete the files you copied. Your presets (section 6), the window settings and licence files (section 4) are kept elsewhere; delete those folders too if you no longer need them.

## 4. Trial and licence

The trial is the full plug-in, with every feature and preset. Without a licence it inserts 3 seconds of silence 30 seconds after start and every 60 seconds after that.

### Activation

Click the licence chip at the top right ("TRIAL" while unlicensed) to open the licence window.

1. **This computer**: a 64-character code (derived from the computer's ID, not the ID itself).
2. **Activate with your key**: enter the licence key you received (it starts with `SWL-`) and press ACTIVATE. This needs an internet connection.
3. **Activate with a licence file**: on a computer without internet, open 〔the activation page〕 on another computer, enter your key and this computer's code to get a licence file (`.swlicense`), and load it with LICENCE FILE.

The silence stops as soon as the licence is accepted. After activation SWINGBY works offline.

- One licence activates up to 3 computers. Activating the same computer again does not count twice.
- To move to another computer, contact 〔support〕.
- The licence is perpetual: no subscription, no expiry. Updates within version 1.x are free.

Licence files (placed there for you on activation):

- Windows: `%APPDATA%\SEVENTHWELL\Licenses\`
- macOS: `~/Library/Application Support/SEVENTHWELL/Licenses/`

## 5. The window

Five pages — PLAY, LAYER, ARP, MOD, FX — selected in the top bar.

**Top bar (right)**

- **MOTION 60 / 30 / OFF**: how the display moves. 60 = every display frame, 30 = 30 frames a second, OFF = a still picture (redrawn when a value changes). Use 30 or OFF on a busy computer. If the OS asks for reduced motion, it starts at OFF.
- **☀ / ☾**: dark (night sky) or light (day sky).
- **100%**: window size (75, 90, 100, 115, 130 %).
- **Licence**: green = activated, amber = trial.

Motion, theme and size are remembered per user (the same in every project).

### 5.1 PLAY

- **Preset name and ‹ ›**: the current preset; step back and forth.
- **CONSTELLATIONS (left)**: the preset list, filtered by ALL, LEAD, PAD, BASS, PLUCK, KEYS, SEQ, FX or USER (your own). Click a name to load it. Even with keys held, the change is joined over 8 ms, without a step.
- **SAVE PRESET**: save the current sound (section 6).
- **The system (middle)**: the star is the sound, the orbits are the layers. Cutoff opens the halo, resonance lights its ring, drive throws sparks; unison voices are small moons, detune spreads them.
- **ORBITS (right)**: the four layers and their levels; click to select one. Below: the selected layer's cutoff, resonance, drive, unison, detune and amp envelope. EDIT LAYER opens the LAYER page.
- **8 macros**: BRIGHT, RESO, ATTACK, RELEASE, DRIVE, WIDTH, DELAY, REVERB. 50 leaves the preset as it is. Drag (Shift for fine), double-click for 50, or use the wheel. Right-click for MIDI learn (section 7).
- **RIBBON**: press and slide to play C1 to C6 without a keyboard.
- **ARP**: arpeggiator on / off.

### 5.2 LAYER

- **Layer cards (left)**: L1 to L4. Click to select; LAYER ON switches it.
- **Close-up (middle)** and **PLAY NOTE**: hear the selected layer.
- **ENVELOPE**: AMP and FILTER attack, decay, sustain, release.
- **LFO 1 / 2**: shape (Orbit, triangle, saw, square, random), RATE, SYNC (tempo-synced; Off uses RATE), ORBIT (how flat the ellipse is: 0 is a circle; the flatter, the slower far away and the faster near the planet), START (Free runs all the time, Note restarts with each note).
- **OSCILLATOR**: TYPE selects the source.
  - Analog: sine, triangle, saw, square, PULSE W (square width)
  - Wavetable: 8 tables (CLASSIC to GLASS), POSITION
  - FM: two operators — RATIO, INDEX, DECAY, FEEDBACK
  - Sample: 8 sounds (AIR to CLICK)
  - For all: OCTAVE (±2), SEMI (±12), FINE (±100 cents), UNISON (1–8), DETUNE, SPREAD (stereo width), GRAVITY (unison voices pull together until their phases line up), LEVEL, PAN, VELOCITY (how much velocity changes the level)
- **FILTER**: LP 12, LP 24, BP 12, HP 12 — CUTOFF, RESO, DRIVE, ENV (filter envelope amount, ±), KEY (key tracking)

### 5.3 ARP (arpeggiator and trance gate)

- **ARPEGGIATOR**: MODE (Up, Down, Up-Down, Order = the order you pressed, Random), RATE (1/8, 1/16, 1/16 T, 1/32), OCTAVES (1–4), LENGTH (note length), SWING (delays the off-beats), STEPS (1–16).
  - Bars: each step's velocity. Drag to set, double-click for 0 / 100. 0 is a rest.
  - Row below: each step's pitch (0, +12, +7, −12). Click to step through, right-click for 0.
- **TRANCE GATE**: 16 on/off steps chop the sound. RATE (1/8, 1/16, 1/32), DEPTH.
- While the host plays, both follow its beat; when it is stopped, they count from the first key you press.

### 5.4 MOD

- **8 mod slots**: ON, SOURCE (LFO 1, LFO 2, Env 2, Velocity, Mod wheel, Aftertouch, Key, macros M1–M8) → TARGET (Cutoff, Resonance, Pitch, Drive, Pan, Level, L1–L4 level, LFO 1/2 rate, Pulse width, Detune, Gravity, WT position, FM index), AMOUNT (±100).
- Each slot is drawn as a gravity line in the system: thicker for more, flowing from source to target (dots flowing back for a negative amount).
- **FLYBY**: every note approaches, passes or leaves. MODE (Off, Arrive, Pass, Leave), DEPTH, TIME, NEAR (how close it passes), SIDE (left to right, right to left, alternate). Pitch (Doppler), level, pan and air absorption move together.
- **VOICE**: PLAY (Poly, Mono, Legato), VOICES (1–32), GLIDE (0–2000 ms), BEND (pitch bend range, 0–24 semitones), LEVEL (master level).

### 5.5 FX

The signal path on top (voice → effects → OUT), six cards below. Their order is the processing order.

- **ON / OFF**: an effect that is off is not processed (it costs nothing). Switching is joined over 5 ms.
- **‹ ›**: move an effect one place earlier or later. The output dips for 4 ms while the order changes.
- Drive (AMOUNT, TONE, MIX), Chorus (RATE, DEPTH, MIX), Delay (TIME: tempo-synced 1/16 to 1/2, FEEDBACK, MIX), Reverb (SIZE = decay time, DAMP, MIX), EQ (LOW, MID, HIGH ±12 dB), Limit (GAIN, CEILING, RELEASE; the output never goes over CEILING).

## 6. Presets

- **Factory**: 128 (LEAD 18, PAD 19, BASS 19, PLUCK 18, KEYS 18, SEQ 18, FX 18), all at −16 LUFS.
- **Saving**: SAVE PRESET, enter a name, category, author and comment, then SAVE. An existing name asks before it is replaced. Characters a file name cannot hold are replaced.
- **Where they go** (a CLAP host's preset browser lists them too):
  - Windows: `Documents\SEVENTHWELL\SWINGBY\Presets\`
  - macOS: `~/Library/Audio/Presets/SEVENTHWELL/SWINGBY/`
- Preset files (`.swpreset`) are text: copy them to another computer to use them there.
- A saved project remembers the name of the preset in use.
- In a host's generic parameter list, parameters are grouped ("Layer 1/Filter", "Effects/Delay" ...). Write automation from there or by moving the controls in the window.

## 7. MIDI

| Message | Does |
| --- | --- |
| Note on / off, velocity | plays |
| Pitch bend | pitch, by the BEND range |
| CC 1 (mod wheel) | the mod source "Mod wheel" |
| Channel pressure (CLAP pressure) | the mod source "Aftertouch" |
| CC 64 (sustain pedal) | sustain |
| Program change 0–127 | loads factory preset 1–128 |
| CC 120, 123 | stops all notes |
| A learned CC | moves its macro |

**MIDI learn**: right-click a macro on the PLAY page → MIDI LEARN → move a control on your controller. Its CC number appears above the knob. Right-click → FORGET removes it. Assignments are saved with the project. CC 0, 1, 32, 64 and 120–127 cannot be learned.

Note: some hosts do not pass program changes or CCs to plug-ins, or only when set to.

## 8. Troubleshooting

| What happens | What to check |
| --- | --- |
| 3 seconds of silence every 60 seconds | This is the trial: activate (section 4) |
| On Windows the window stays dark or says it needs the WebView2 Runtime | Install the Microsoft Edge WebView2 Runtime and open the window again (the sound and the host's generic controls work without it) |
| The window feels heavy | Set MOTION to 30 or OFF |
| High CPU load | Fewer VOICES, less UNISON, switch unused layers and effects off |
| Your presets are not in the host's browser | Are they in the preset folder (section 6)? Rescan the host's browser |
| A licence file is refused | Was it made with this computer's code? (A file for another computer does not work.) Is the file intact? |

〔support contact〕

## 9. Specifications

| | |
| --- | --- |
| Voices | 1–32 (default 16), Poly / Mono / Legato |
| Layers | 4 |
| Oscillators | Analog (4 waves), Wavetable (8 tables), FM (2 operators), Sample (8) |
| Unison | 1–8 voices (detune, spread, gravity) |
| Filter | LP 12, LP 24, BP 12, HP 12, with drive |
| Envelopes | AMP and FILTER per layer |
| LFOs | 2 (Orbit, triangle, saw, square, random; tempo sync) |
| Modulation | 8-slot matrix, 8 macros, flyby, gravity |
| Arpeggiator | 16 steps (velocity, pitch), 5 modes, swing |
| Trance gate | 16 steps |
| Effects | Drive, Chorus, Delay, Reverb, EQ, Limit (any order) |
| Presets | 128 factory (−16 LUFS), save your own |
| Output | Stereo |

## 10. Third-party notices

SWINGBY uses:

- Fonts: Barlow Condensed, Michroma, Space Mono (SIL Open Font License 1.1; see the `licenses` folder)
- CLAP SDK and clap-wrapper (MIT), VST3 SDK (MIT; VST is a trademark of Steinberg Media Technologies GmbH), AudioUnitSDK (Apache 2.0, macOS)
- Monocypher (BSD-2-Clause / CC0): licence file signatures
- WebView2 SDK (Microsoft, Windows)

See the `licenses` folder for the full texts.

© SEVENTHWELL
