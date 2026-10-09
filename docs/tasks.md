# 残りの作業（バックログ）

仕様書の順（共通章 → EQ → DY/MS → SA/LO/GT → RV/DL/MD/ST → VO/RS → CR/IN/MT/UT → LV）。
製品の仕様は docs/spec/SW_AUDIO_spec_v1.0.md の各製品の節（パラメータ表・DSP・遅延・CPU・進化機能・要確認）。

## 先にやること（この順）

1. **CI を通す。** `.github/workflows/build.yml`（Windows MSVC／macOS ユニバーサル＋AU／Linux）はまだ一度も動かしていない。失敗したら直す。Mac 版（AU）はここで初めてできる。
2. ~~**アナログ出力段を仕様どおり非対称にする。**~~ 済（v0.11.1、README「Drive 段の設計」）。元の記述： 仕様書は EQ01 の Drive を「非対称ソフトクリップ1段、2× OS、音量補正つき」と定め、EQ03・EQ04 も「EQ01 と同じ出力段」。今の `core/include/sw/drive.hpp` は対称の tanh（ヘッドルーム +6 dBFS は決定事項なので維持）。偶数次倍音が出ることをテストで確かめる。

## 製品（済 132・残り 7）

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
- [x] LO03 Low Focus — Separates kick and bass and keys them to each other

### GT
- [x] GT01 Amp — Cleans up when you roll back the guitar volume
- [x] GT02 Cab Ir — Drag the mic across the speaker
- [x] GT03 Pedalboard — Drag to reorder pedals, tuner always running（Chorus＝MD01・Delay＝DL01・Reverb＝RV01 を流用するため、それらの後で作る）
- [x] GT04 Bass Amp — DI and amp blend is phase aligned automatically
- [x] GT05 Reamp — Pickup model turns a DI into a different guitar

### RV
- [x] RV01 Hall — Tail ducks while the dry signal is loud
- [x] RV02 Plate — Pre-delay syncs to tempo and ducks under vocals
- [x] RV03 Spring — Drip reacts to transients, not to sustain
- [x] RV04 Convolution — IR length trims itself to the song tempo
- [x] RV05 Chamber — Mic distance morphs the chamber realistically
- [x] RV06 Shimmer — Freeze holds a chord pad on demand
- [x] RV07 Early — Place the source in the room by dragging
- [x] RV08 Gated — Gate opens on the snare only and ignores bleed

### DL
- [x] DL01 Echo — Repeats duck while you sing and swell in the gaps
- [x] DL02 Tape Echo — Wear simulates tape age, heads switch per bar
- [x] DL03 Bbd — Grit adds clock noise to repeats only
- [x] DL04 Multitap — Each tap has its own filter and pan
- [x] DL05 Reverse — Grains follow the tempo and reverse on the beat

### MD
- [x] MD01 Chorus — Width stays mono compatible
- [x] MD02 Flanger — Through-zero sweep synced to tempo
- [x] MD03 Phaser — Center frequency follows the note being played
- [x] MD04 Tremolo Pan — Harmonic mode splits lows and highs
- [x] MD05 Rotary — Speed ramps follow a footswitch or MIDI
- [x] MD06 Freq Shift — Shift tracks pitch for even detune
- [x] MD07 Ensemble — Voices stay mono compatible

### ST
- [x] ST01 Imager — Width per band with a live mono check
- [x] ST02 Mid Side — Compares mid and side balance against a reference
- [x] ST03 Phase Align — Auto align finds the best delay and phase
- [x] ST04 Center — Haas with an automatic mono compatibility check
- [x] ST05 Phones — Calibrated profiles for common headphone models
- [x] ST06 Mono Low — Listen solos only what becomes mono

### VO
- [x] VO01 Tune — Detects the key and suggests the scale
- [x] VO02 Tune Rt — Low latency and formant safe for live singers
- [x] VO03 Harmony — Harmony follows chords from a MIDI track
- [x] VO04 Doubler — Doubles with natural timing, not chorus wobble
- [x] VO05 Rider — Listens to the music and rides the vocal against it
- [x] VO06 Formant — Shifts character while keeping timing
- [x] VO07 Vocal Strip — Clean, tone, dynamics and space in the right order
- [x] VO08 Breath — Marks breaths so you choose per phrase

### RS
- [x] RS01 Denoise — Adaptive profile updates as the noise changes
- [ ] RS02 Voice Isolate — Separates voice from music and room （保留：学習済みの音源分離モデルが必要。区分 C・後期。モデルの権利と収集方法が未確認）
- [x] RS03 Dehum — Tracks drift in the mains frequency
- [x] RS04 Declick — Every repaired click is marked on the spectrogram
- [x] RS05 Declip — Detects clipped sections automatically
- [x] RS06 Dereverb — Learns the room tail from silence
- [x] RS07 Mouth Noise — Finds mouth clicks between words

### CR
- [x] CR01 Filter — Envelope follows the drummer's dynamics
- [x] CR02 Stutter — Random patterns stay inside the groove
- [x] CR03 Granular — Grains follow the harmony of the input
- [x] CR04 Freeze — Freeze triggers on transients or MIDI
- [x] CR05 Tape Stop — Stops land exactly on the bar line
- [x] CR06 One Knob — One knob drives six effects with tuned macro curves

### IN
- [ ] IN01 Synth — Patch morph between two sounds （対象外：楽器プラグイン（MIDI で鳴らす音源）は作らない）
- [ ] IN02 Drums — Humanize timing by drummer style （対象外：楽器プラグイン（MIDI で鳴らす音源）は作らない）
- [ ] IN03 Keys — Pedal and hammer noise follow your playing （対象外：楽器プラグイン（MIDI で鳴らす音源）は作らない）
- [ ] IN04 Bass — Slides and ghost notes from MIDI velocity （対象外：楽器プラグイン（MIDI で鳴らす音源）は作らない）
- [ ] IN05 Organ — Drawbar settings morph with an expression pedal （対象外：楽器プラグイン（MIDI で鳴らす音源）は作らない）
- [ ] IN06 Sampler — Auto maps slices to keys by transient （対象外：楽器プラグイン（MIDI で鳴らす音源）は作らない）

### MT
- [x] MT01 Loudness — Loudness presets for Japanese broadcast and streaming
- [x] MT02 Spectrum — Compares your mix against a reference curve
- [x] MT03 Spectrogram — Click the spectrogram to hear that band
- [x] MT04 Phase Scope — Warns before a mono problem happens
- [x] MT05 Vu Ppm — VU reference calibrated per project

### UT
- [x] UT01 Gain — Remembers gain staging per track type
- [x] UT02 Mono Check — Phone speaker simulation
- [x] UT03 Reference — Loudness matched A/B so louder never wins

### LV
- [x] LV01 Voice — One knob sets noise, EQ, comp and limit together
- [x] LV02 Feedback — Ring out learns fixed filters before the show
- [x] LV03 Channel — Mic presets set the whole strip
- [x] LV04 Safety limiter — Logs every limit event with the time
- [x] LV05 Auto ducker — Ducks for voices only, ignores claps and noise
- [x] LV06 Stream master — Rides to the platform loudness target
- [x] LV07 Speech Agc — Holds level for close and distant talkers
- [x] LV08 Room Noise — Learns HVAC and keyboard noise
- [x] LV09 Hum Cut — Tracks mains drift live
- [x] LV10 Voice Fx — Anonymous mode for interviews
- [x] LV11 Mic Switch — Mutes on silence, cough button
- [x] LV12 Geq 31 — Feedback guard flags ringing bands
- [x] LV13 Live Peq — RTA overlay with peak suggestions
- [x] LV14 Align — Measures delay in one click
- [x] LV15 Auto Mixer — Last mic hold and open mic limit
- [x] LV16 Live Gate — Key filter ignores stage rumble
- [x] LV17 Bus Comp — Modes switch with OBS scenes
- [x] LV18 Pop Guard — Catches plug and handling pops
- [x] LV19 Av Sync — Clap sync measures the offset
- [x] LV20 Rta — Pink noise reference overlay
- [x] LV21 Test Gen — Output stays off until armed
- [x] LV22 Polarity — Instant in-phase check
- [x] LV23 Loudness — ARIB TR-B32 log export
- [x] LV24 Live Reverb — 1% CPU and ducks under speech
- [x] LV25 Live Delay — Tap or MIDI clock, tails spill over on bypass
- [x] LV26 Mono — Auto phase fix for mono viewers
- [x] LV27 Scene Sync — Follows OBS scenes to recall presets
- [x] LV28 Remote Hub — Tablet control protected by PIN
- [x] LV29 Interp Mix — Ducks the floor when the interpreter talks
- [x] LV30 Recorder — Always-on backup recording with markers

## 画面（UI）

方式：Web 画面（HTML/CSS/JS）を各 OS の Web ビューに載せる（macOS＝WKWebView、Windows＝WebView2、Linux は画面なし）。画面は各製品のパラメータ表（`products/*/` の `specs()`）から自動で組み立てる共通ランタイム `ui/sw-ui.js`（デザインシステムのデジタル系パネル）で、`ui/host.js` がプラグインとの通信（`plugin/clap/gui_bridge.hpp`）を受け持つ。
- [x] 共通ランタイム：ノブ・ボタン・セレクター・フェーダー、値の入力、A/B、Undo/Redo、Auto gain／Delta、EVO バー、レイテンシと CPU の表示（`ui/`、全 132 製品を Chromium で描画して確認）。
- [x] 曲線と値の書式が C++ と一致することのテスト（`tests/ui/curves.test.js`、8055 点）。
- [x] プラグイン側：CLAP の gui 拡張、画面→ホストのジェスチャ（begin／value／end）の待ち行列、CPU の実測、macOS／Windows のビュー（`gui_mac.mm`／`gui_win.cpp`）。**macOS と Windows での実機確認は未実施（CI のビルドとバリデータだけ）**。
- [~] 製品ごとの専用表示：EQ カーブ・スペクトラム・ステレオスコープ・ラウドネス・GR など約 70 製品が動いた（README「画面の中央の表示」の表）。残り（静的な絵のまま）：CR02〜03・CS04・DL05・DY09・LO03・LV03・LV04・LV15・LV27・LV28・LV30・MS06・RS02・RS05・ST03・UT03
- [x] 製品ごとのボタン（コアのメソッドを呼ぶもの）：デザインの画面でも動くように接続（12 製品）。残り：DL01 Tap（仕様にない）、LV08 Forget・LV21 Arm・LV30 Record・UT01 Remember gain（デザインにない）
- [ ] Blender 描画のノブ・パネル素材（デザインシステムの画像）、ラックイヤー・LIVE 筐体の意匠。
- [ ] Linux の画面（X11 への埋め込み）、ウィンドウの拡大縮小。

## 製品のあと

- 画面（キャンバス docs/design/canvas/project/<コード>.dc.html を WebView で流用）。共通機能のうちモーフ（A/B）・EVO バー・Unit A/B/C・Low lat は画面と一緒に。
- 解析・学習系の EVO（区分 B／C：EQ02 Assist/Unmask、EQ07 Auto thresh、CS02 被り学習、CS03 入力レベル合わせ、DY04 Learn など）。
- SW AUDIO for OBS（GPL の薄い OBS フィルタ＋非公開エンジン、共有メモリ、フェイルオープン。公開前に弁護士確認）。
- MIDI・フットスイッチ入力（ホストのノート入力をプラグイン層に通す）：MD05 Rotary の Speed 切替（CC64・CC1・Note）、VO03 Harmony、CR04 Freeze、IN04 Bass、LV25 Live Delay など。
- EQ08・EQ02 Linear のカーネル再計算を別スレッドへ（仕様書どおり）。
- [x] UI: CR02 の Randomize／Clear ボタン

- [x] 画面：入出力ピークメーター（全製品共通・アダプタで実測）、ボタン（Randomize／Tap など 12 製品）を本体へ配線
- [x] 画面：デザインキャンバス（docs/design/canvas）の配置をそのまま使う（tools/gen_skins.py。つまみ 527/554・バンド選択式の画面に対応。残りは EQ01 など仕様と項目が合わないもの）
- [~] 画面：中央の表示の連動（上の「製品ごとの専用表示」）／画面だけのボタン（Low lat・2× OS など）は機能が無いので薄く表示（共通機能の実装を参照）
- [x] 画面：仕様とずれていたデザインの項目を仕様に合わせた（EQ01・EQ05・DY01・DY10・MS03・ST01・MS06。README「画面の項目を仕様に合わせた箇所」）。DY01 は仕様どおり Speed に統一（決定済み）
- [x] 画面：GT03 Tuner・VO05 Music の読み取り表示をコアの値に結び付け（DY02 Meter の切り替えは画面だけの設定のまま）
- [x] 画面：中央の表示の連動の仕組み（ui/displays.js。パラメータから描く曲線＋プラグインが測るレベル）。DY08 の圧縮特性図・レベル履歴・GR 履歴が動く
- [~] 画面：中央の表示を他の製品へ広げる（上の「製品ごとの専用表示」の残り）
- [x] 画面：GT03 のペダルを Blender で描き直し、つまみ A・B・C／フットスイッチ／LED／種類をパラメータに結び付け（tools/blender/）
- [x] 画面：GT03 のペダルの追加・並べ替え（Add pedal、ドラッグで入れ替え）、スロット 7・8
- [ ] 画面：他の製品でも、デザインの平らな部品を Blender の描画に置き換えられるものを探す
- [x] 画面：VU メーター（DY01・DY02・DY06・MT05）の針をレベルで動かす（目盛りに合わせた角度、300 ms の動き）。枠は Blender で描画（DY01 は Ratio ボタンと Meter 切り替えも動く）
- [ ] 画面：DY03 の GR メーター、テープのリール（DL02・SA01）、回転スピーカー（MD05）、MS01 のフェーダーを Blender で作り込み、動かす
- [x] 画面：テープのリール（DL02・SA01）を Blender で描き、音が通っている間回す（SA01 は Speed ips に比例）
- [x] 画面：MD05 の回転スピーカー（ホーン・ドラムを Blender で描き、Speed・Accel の物理モデルどおりに回す。Hz 表示も連動）
- [x] 画面：パラメトリック EQ の周波数特性図（EQ02・EQ07・EQ08）。バンドのフィルタ応答から曲線を描き、ドットのドラッグで周波数・ゲイン、ホイールで Q、空きの場所を 2 回クリックでバンド追加、ドットを 2 回クリックで削除
- [x] 画面：LV12 の 31 バンドのフェーダーを操作できるように（Left／Right／Both に従って書き込み、ダブルクリックで 0 dB）
- [x] 画面：DY10・DY11 のマルチバンド表示（クロスオーバー／中心周波数のドラッグ。実測スペクトルとバンドごとの GR は未対応）
- [x] 画面：フォントを同梱（Barlow Condensed・Michroma・Space Mono、data: URI。README「フォントの同梱」）
- [x] 画面：パラメータだけで描ける中央表示を追加（RV06・LV24・RS06 の減衰、DL04・LV25 のディレイの棒、LV16 のゲート、ST01 の幅、MS04 のクリッパー、MS03 のマルチバンド）
- [x] 画面：スペクトラム（アダプタで FFT→64 バンドを画面へ。MT02・MD06・LV09・LV08・LV02・LO01・SA05・LV20・MT03・RS04・RS07）。テストあり（正弦波の帯域とレベル）
- [x] 画面：ステレオスコープ（MT04・ST01・UT02・LV26。L/R の直近サンプルと相関を渡す）。テストあり
- [x] 画面：コアの測った値を渡す口（readouts トレイト）、MT01・LV23 のラウドネス表示、MS01 の GR をコアの値に。テストあり
- [x] 画面：LV06（ストリーム用マスター）・LV07（スピーチレベラー）をコアの測った値で表示（LV06 は仕様に合わせて項目を Short-term／Auto gain／Limiter／Output ≈ に替えた）
- [x] 画面：DY01〜DY08・DY12 の GR をコアの値に、DY05 のディエッサー表示、MS05・VO05 のライド履歴
- [x] 画面：LIVE 製品（25 製品）のつまみ（.rc）とトグル（.tile）が動かなかった不具合を修正（結び付け 551→641）。LV14 Distance・LV19 Frames は読み取り表示に
- [x] 画面：LV13（EQ カーブ＋スペクトラム）、CR05（テープストップの曲線）
- [x] 共通の Bypass パラメータ（CLAP bypass）と、画面の「In」トグル・電源ボタン・立体トグル（.tog）の結び付け。残り：SA02／GT05／DY02／DY01 の 4 つのトグル
- [x] 画面：A／B のモーフのスライダーを動くように（補間は画面側）
- [x] 画面：コアを呼ぶボタン（12 製品）をデザインの画面でも動くように。機能が無い部品（Low lat・2× OS・Unit A/B/C・履歴・100%・LIVE の Main show/Remote/Lock）は薄く表示
- [ ] 共通機能の実装（画面の部品はある）：Low lat、オーバーサンプリング 1×/2×/4×、Unit A/B/C、プリセット、拡大率、LV23 のログの書き出し、DL01 の Tap
