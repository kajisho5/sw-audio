# 残りの作業（バックログ）

仕様書の順（共通章 → EQ → DY/MS → SA/LO/GT → RV/DL/MD/ST → VO/RS → CR/IN/MT/UT → LV）。
製品の仕様は docs/spec/SW_AUDIO_spec_v1.0.md の各製品の節（パラメータ表・DSP・遅延・CPU・進化機能・要確認）。

## 先にやること（この順）

1. **CI を通す。** `.github/workflows/build.yml`（Windows MSVC／macOS ユニバーサル＋AU／Linux）はまだ一度も動かしていない。失敗したら直す。Mac 版（AU）はここで初めてできる。
2. ~~**アナログ出力段を仕様どおり非対称にする。**~~ 済（v0.11.1、README「Drive 段の設計」）。元の記述： 仕様書は EQ01 の Drive を「非対称ソフトクリップ1段、2× OS、音量補正つき」と定め、EQ03・EQ04 も「EQ01 と同じ出力段」。今の `core/include/sw/drive.hpp` は対称の tanh（ヘッドルーム +6 dBFS は決定事項なので維持）。偶数次倍音が出ることをテストで確かめる。

## 製品（済 45・残り 94）

### EQ
- [x] EQ01 Passive 進化版 — Contour turns the boost-and-dip trick into one knob
- [x] EQ02 Surgical 進化版 — Assist finds resonances, Unmask shows clashes with other tracks
- [x] EQ03 Mid Shaper — Peak band rides with the vocal level automatically
- [x] EQ04 Inductor — Iron saturation scales with each band's boost
- [x] EQ05 Console — Match copies the tone curve of a reference track
- [x] EQ06 Stepped — Stepped gains glide between steps with no zipper noise
- [x] EQ07 Dynamic — Learns each band's threshold from 5 seconds of audio
- [x] EQ08 Linear — Pre-ring guard limits linear phase smearing
- [x] EQ09 Tilt — Pivot follows the spectral center of the song

### CS
- [x] CS01 Inductor Strip — Mic profile sets EQ and comp starting points
- [x] CS02 Console Strip — Gate learns drum bleed and ignores it
- [x] CS03 Stepped Strip — Auto gain staging sets the input for each source
- [x] CS04 Modular Strip — Suggests the best module order for the source

### DY
- [x] DY01 FET 進化版 — Bite keeps the transient while Color sets the distortion
- [x] DY02 Opto 進化版 — Target rides the vocal like a hand on the fader
- [x] DY03 Bus 進化版 — Punch keep protects transients under the glue
- [x] DY04 Gate — Learns bleed versus hits and sets the threshold
- [x] DY05 De-ess — Sibilance detector follows the singer's pitch
- [x] DY06 Vari-Mu — Time constant adapts to program density
- [x] DY07 Snap — Snap shapes the attack independently of ratio
- [x] DY08 Clean — Auto release reads the groove tempo
- [x] DY09 Transient — Split bands shape kick and cymbals separately
- [x] DY10 Multiband 4 — Auto places crossovers from the mix spectrum
- [x] DY11 Multiband 6 — Each band switches between compressor and dynamic EQ
- [x] DY12 Parallel — Upward mode lifts quiet detail instead of only squashing

### MS
- [x] MS01 Maximizer 進化版 — Lock reaches the loudness target automatically
- [x] MS02 True Peak — Inter-sample peaks caught at 8x and marked on the display
- [x] MS03 Multiband Limit — Bands share one ceiling, no pumping across bands
- [x] MS04 Clipper — Knee morphs from hard clip to tape-like softness
- [x] MS05 Leveler — Writes the rides back as DAW automation
- [x] MS06 Master Chain — Every stage is gain matched, chain order is drag and drop
- [x] MS07 Dither — Auto blank mutes dither in true silence

### SA
- [x] SA01 Tape — Calibrate sets the tape operating level from your input
- [x] SA02 Console Sum — Every channel gets its own analog variance
- [x] SA03 Tube — Bias follows input dynamics like a real tube
- [x] SA04 Transformer — Load reacts to the source impedance
- [x] SA05 Exciter — Adds harmonics only where the mix lacks them
- [x] SA06 Saturator — Dynamics makes saturation react to playing strength
- [x] SA07 Lo-Fi — Era sets crackle, wow and bandwidth in one move
- [x] SA08 Bitcrush — Rate locks to song tempo for rhythmic aliasing

### LO
- [x] LO01 Low Harm — Phone preview checks small speaker playback
- [x] LO02 Sub Gen — Sub tracks the pitch of the bass note
- [ ] LO03 Low Focus — Separates kick and bass and keys them to each other

### GT
- [ ] GT01 Amp — Cleans up when you roll back the guitar volume
- [ ] GT02 Cab Ir — Drag the mic across the speaker
- [ ] GT03 Pedalboard — Drag to reorder pedals, tuner always running
- [ ] GT04 Bass Amp — DI and amp blend is phase aligned automatically
- [ ] GT05 Reamp — Pickup model turns a DI into a different guitar

### RV
- [ ] RV01 Hall — Tail ducks while the dry signal is loud
- [ ] RV02 Plate — Pre-delay syncs to tempo and ducks under vocals
- [ ] RV03 Spring — Drip reacts to transients, not to sustain
- [ ] RV04 Convolution — IR length trims itself to the song tempo
- [ ] RV05 Chamber — Mic distance morphs the chamber realistically
- [ ] RV06 Shimmer — Freeze holds a chord pad on demand
- [ ] RV07 Early — Place the source in the room by dragging
- [ ] RV08 Gated — Gate opens on the snare only and ignores bleed

### DL
- [ ] DL01 Echo — Repeats duck while you sing and swell in the gaps
- [ ] DL02 Tape Echo — Wear simulates tape age, heads switch per bar
- [ ] DL03 Bbd — Grit adds clock noise to repeats only
- [ ] DL04 Multitap — Each tap has its own filter and pan
- [ ] DL05 Reverse — Grains follow the tempo and reverse on the beat

### MD
- [ ] MD01 Chorus — Width stays mono compatible
- [ ] MD02 Flanger — Through-zero sweep synced to tempo
- [ ] MD03 Phaser — Center frequency follows the note being played
- [ ] MD04 Tremolo Pan — Harmonic mode splits lows and highs
- [ ] MD05 Rotary — Speed ramps follow a footswitch or MIDI
- [ ] MD06 Freq Shift — Shift tracks pitch for even detune
- [ ] MD07 Ensemble — Voices stay mono compatible

### ST
- [ ] ST01 Imager — Width per band with a live mono check
- [ ] ST02 Mid Side — Compares mid and side balance against a reference
- [ ] ST03 Phase Align — Auto align finds the best delay and phase
- [ ] ST04 Center — Haas with an automatic mono compatibility check
- [ ] ST05 Phones — Calibrated profiles for common headphone models
- [ ] ST06 Mono Low — Listen solos only what becomes mono

### VO
- [ ] VO01 Tune — Detects the key and suggests the scale
- [ ] VO02 Tune Rt — Low latency and formant safe for live singers
- [ ] VO03 Harmony — Harmony follows chords from a MIDI track
- [ ] VO04 Doubler — Doubles with natural timing, not chorus wobble
- [ ] VO05 Rider — Listens to the music and rides the vocal against it
- [ ] VO06 Formant — Shifts character while keeping timing
- [ ] VO07 Vocal Strip — Clean, tone, dynamics and space in the right order
- [ ] VO08 Breath — Marks breaths so you choose per phrase

### RS
- [ ] RS01 Denoise — Adaptive profile updates as the noise changes
- [ ] RS02 Voice Isolate — Separates voice from music and room
- [ ] RS03 Dehum — Tracks drift in the mains frequency
- [ ] RS04 Declick — Every repaired click is marked on the spectrogram
- [ ] RS05 Declip — Detects clipped sections automatically
- [ ] RS06 Dereverb — Learns the room tail from silence
- [ ] RS07 Mouth Noise — Finds mouth clicks between words

### CR
- [ ] CR01 Filter — Envelope follows the drummer's dynamics
- [ ] CR02 Stutter — Random patterns stay inside the groove
- [ ] CR03 Granular — Grains follow the harmony of the input
- [ ] CR04 Freeze — Freeze triggers on transients or MIDI
- [ ] CR05 Tape Stop — Stops land exactly on the bar line
- [ ] CR06 One Knob — One knob drives six effects with tuned macro curves

### IN
- [ ] IN01 Synth — Patch morph between two sounds
- [ ] IN02 Drums — Humanize timing by drummer style
- [ ] IN03 Keys — Pedal and hammer noise follow your playing
- [ ] IN04 Bass — Slides and ghost notes from MIDI velocity
- [ ] IN05 Organ — Drawbar settings morph with an expression pedal
- [ ] IN06 Sampler — Auto maps slices to keys by transient

### MT
- [ ] MT01 Loudness — Loudness presets for Japanese broadcast and streaming
- [ ] MT02 Spectrum — Compares your mix against a reference curve
- [ ] MT03 Spectrogram — Click the spectrogram to hear that band
- [ ] MT04 Phase Scope — Warns before a mono problem happens
- [ ] MT05 Vu Ppm — VU reference calibrated per project

### UT
- [ ] UT01 Gain — Remembers gain staging per track type
- [ ] UT02 Mono Check — Phone speaker simulation
- [ ] UT03 Reference — Loudness matched A/B so louder never wins

### LV
- [ ] LV01 Voice — One knob sets noise, EQ, comp and limit together
- [ ] LV02 Feedback — Ring out learns fixed filters before the show
- [ ] LV03 Channel — Mic presets set the whole strip
- [x] LV04 Safety limiter — Logs every limit event with the time
- [ ] LV05 Auto ducker — Ducks for voices only, ignores claps and noise
- [ ] LV06 Stream master — Rides to the platform loudness target
- [ ] LV07 Speech Agc — Holds level for close and distant talkers
- [ ] LV08 Room Noise — Learns HVAC and keyboard noise
- [ ] LV09 Hum Cut — Tracks mains drift live
- [ ] LV10 Voice Fx — Anonymous mode for interviews
- [ ] LV11 Mic Switch — Mutes on silence, cough button
- [ ] LV12 Geq 31 — Feedback guard flags ringing bands
- [ ] LV13 Live Peq — RTA overlay with peak suggestions
- [ ] LV14 Align — Measures delay in one click
- [ ] LV15 Auto Mixer — Last mic hold and open mic limit
- [x] LV16 Live Gate — Key filter ignores stage rumble
- [x] LV17 Bus Comp — Modes switch with OBS scenes
- [ ] LV18 Pop Guard — Catches plug and handling pops
- [ ] LV19 Av Sync — Clap sync measures the offset
- [ ] LV20 Rta — Pink noise reference overlay
- [ ] LV21 Test Gen — Output stays off until armed
- [ ] LV22 Polarity — Instant in-phase check
- [ ] LV23 Loudness — ARIB TR-B32 log export
- [ ] LV24 Live Reverb — 1% CPU and ducks under speech
- [ ] LV25 Live Delay — Tap or MIDI clock, tails spill over on bypass
- [ ] LV26 Mono — Auto phase fix for mono viewers
- [ ] LV27 Scene Sync — Follows OBS scenes to recall presets
- [ ] LV28 Remote Hub — Tablet control protected by PIN
- [ ] LV29 Interp Mix — Ducks the floor when the interpreter talks
- [ ] LV30 Recorder — Always-on backup recording with markers

## 製品のあと

- 画面（キャンバス docs/design/canvas/project/<コード>.dc.html を WebView で流用）。共通機能のうちモーフ（A/B）・EVO バー・Unit A/B/C・Low lat は画面と一緒に。
- 解析・学習系の EVO（区分 B／C：EQ02 Assist/Unmask、EQ07 Auto thresh、CS02 被り学習、CS03 入力レベル合わせ、DY04 Learn など）。
- SW AUDIO for OBS（GPL の薄い OBS フィルタ＋非公開エンジン、共有メモリ、フェイルオープン。公開前に弁護士確認）。
- EQ08・EQ02 Linear のカーネル再計算を別スレッドへ（仕様書どおり）。
