# SWINGBY（SW IN07）販売文の原稿

LP（Design キャンバス「SWINGBY LP」）と販売サイト・ストアの説明文の元。2026-10-09、依頼者の「海外のソフトシンセのページを見て参考にした文に」で書き直した版。日本語が正、英語は海外向けの版（同じ事実だけを書く）。

## 書き方の型（海外の製品ページから取ったもの）

2026-10-09 に、海外のソフトシンセの製品ページ 3 件（下の「参考」）を、構成と書き方だけ見た。文は写していない。製品名・会社名は販売文に出さない。

| 型 | SWINGBY での使い方 |
| --- | --- |
| ヒーローは「製品名＋何の製品か」を 1 行で | ロゴタイプ＋「THE ORBITAL PRESET SYNTH」＋短い 1 文 |
| 見出しは動詞で、得られることから（3〜6 語） | 「広げた音を、引き締める。」「弾くたびに、音が飛んでくる。」。仕組みの説明は本文へ |
| 本文は 1〜2 文。「操作 → 得られること」の順 | 「Off のエフェクトは計算しないので、使わない分は軽いまま」 |
| 数を先に出す | 128 プリセット、4 レイヤー、−16 LUFS、マクロ 8 |
| 試聴を早く | 試聴をキービジュアルと数字の直後へ移した |
| 地域の言語のページでも見出しは英語 | 英語の見出し（Michroma）＋日本語の 1 行（デザイン規定の販売物の組み方と同じ） |
| 仕様は「ひと目で」の表、最後の方に淡々と | AT A GLANCE（体験版の行を追加） |
| 購入カードは「太字の項目＋1 行の説明」 | 月額なし・3 台まで・1.x は無料・オフライン・お支払い |
| 押しつけない行動ボタン | 「購入する」「無料で試す」の 2 つだけ |
| 最後に短い一言 | SEE YOU IN ORBIT. |

取らなかった型：レビューや雑誌の引用（実在のものがまだない。作らない）、「最高」「究極」などの最上級、「永久アップデート無料」（決まっているのは 1.x の無料アップデートだけ）、CPU の数値（測ったのはクラウド環境だけ。出すなら「設計上の見積もり」か測った環境を書く）。

## 使ってよい事実と出どころ

| 事実 | 出どころ |
| --- | --- |
| ファクトリープリセット 128（LEAD 18・PAD 19・BASS 19・PLUCK 18・KEYS 18・SEQ 18・FX 18） | README「IN07 のファクトリープリセット」、`tests/test_in07_presets.cpp` |
| すべて −16 LUFS（BS.1770、カテゴリごとの試聴フレーズで測定） | 同上 |
| 4 レイヤー、同時発音 最大 32、Poly／Mono／Legato、ユニゾン 最大 8 | README「IN07 の設計」 |
| 発振器：Analog・Wavetable（8 表）・FM（2 オペレーター）・Sample（8 種）。音のファイルを持たず、最初の prepare で計算して作る | 同上（osc.hpp／osc.cpp） |
| LFO 2（楕円軌道を含む）、変調マトリクス 8、マクロ 8（Bright・Reso・Attack・Release・Drive・Width・Delay・Reverb）、重力、フライバイ（Arrive・Pass・Leave） | 同上 |
| FX 6（Drive・Chorus・Delay・Reverb・EQ・Limit）、順番入れ替え可、Off の FX は計算しない | 同上（fx.hpp） |
| ユーザープリセットの保存、CLAP のプリセットブラウザ対応 | README「IN07 のユーザープリセット」 |
| アルペジエーター（5 モード・1/8〜1/32・1〜4 オクターブ・16 ステップの強さと音程・スイング）、トランスゲート（16 ステップ・1/8〜1/32・深さ）。ホストの拍に合わせる | README「IN07 のアルペジエーターとトランスゲート」、`tests/test_in07_arp.cpp` |
| MIDI：プログラムチェンジでファクトリープリセット 1〜128、マクロ 8 つに MIDI ラーン（曲と一緒に保存） | README、`tests/test_in07_window.cpp` |
| CLAP・VST3・AU（macOS）、macOS 11 以降（ユニバーサル）、Windows 10／11（64 ビット、画面に Microsoft Edge WebView2 ランタイム） | CMakeLists.txt、CI、`plugin/clap/gui_win.cpp`。実機の DAW での確認はまだ |
| 買い切り、3 台まで、1.x のアップデート無料、最初の有効化のあとはオフライン。使わなくなったパソコンの解除は購入者用のページで 1 年に 3 回まで（それ以上は問い合わせ） | `server/license/README.md`（`MAX_ACTIVATIONS`・`MAX_DEACTIVATIONS`） |
| ライセンスキーは購入完了のページに表示（メールでは送らない） | `server/license/src/worker.js` |
| 体験版：機能制限なし、起動から 30 秒後とその後 60 秒ごとに 3 秒の無音 | `core/include/sw/demo_gate.hpp`、`tests/test_demo_gate.cpp` |
| モーフ（スイングバイ）：4 つの惑星（今の音＋ファクトリー 3 種）の間を探査機で動かし、距離の 2 乗に反比例する重みで混ぜる。X・Y はオートメーション可 | README「IN07 のモーフ」、`tests/test_in07_morph.cpp` |
| 惑星の機能：惑星直列アルペジオ（2·3·4・3·4·5・3·5·7）、蝕ゲート（Eclipse）、衛星ユニゾン（0.05〜10 Hz）、ロッシュ限界（強い音の引き裂き、±24 半音まで） | README「IN07 の惑星の機能」、`tests/test_in07_planets.cpp` |
| ブラウザ体験版：プラグインと同じエンジン（WebAssembly）と画面。保存・ユーザープリセット・ライセンスはプラグインのもの。重さはこのコンテナで 1 コアの 1〜15 %（実測、7 種） | README「IN07 のブラウザ体験版」 |
| デモ曲：Slingshot（ブロステップ）・Afterglow（フューチャーベース）・Neon Coastline（シンセウェイヴ）・Escape Velocity（ドラムンベース）・Low Orbit（ローファイ）。音はすべて SWINGBY、マスターは SW MS04／MS01 | README「IN07 のデモ曲」「ジャンル別デモ 4 曲」 |
| 画面：PLAY・LAYER・ARP・MOD・FX の 5 つ、ダーク／ライト、動き 60・30・OFF、大きさ 75〜130 % | `ui/in07/`。LP の画面写真と試聴動画は `tools/in07_marketing_media.py` で今の画面から作る（手で描かない） |

## 日本語

- **タグライン**：選んで、弾いて、軌道に乗せる。
- **分類**：THE ORBITAL PRESET SYNTH（軌道で動かすプリセットシンセ）
- **ひとこと（ストアの 1 行）**：128 の音色をすぐ弾ける軽量プリセットシンセ。重力・フライバイ・楕円軌道で、音に動きを。
- **紹介文**：128 のプリセットから選んで、すぐに弾ける軽量シンセ。音は惑星系として画面に描かれ、重力・フライバイ・楕円軌道で動かすと、星系も一緒に動きます。プリセットはすべて −16 LUFS にそろえてあるので、選ぶのは音量ではなく音色です。
- **特長**
  - **GRAVITY｜広げた音を、引き締める。** ユニゾンで重ねた声が互いに引き合い、位相がそろっていく。厚いスーパーソウから芯のある 1 本まで、つまみ 1 つで。
  - **FLYBY｜弾くたびに、音が飛んでくる。** ノートに合わせて音が近づき、通り過ぎ、去っていく。音程（ドップラー）・音量・左右・空気のこもりが一緒に動く。
  - **ORBIT LFO｜揺れに、緩急をつける。** 遠くではゆっくり、惑星のそばで一気に。楕円を細くするほど、長い溜めと短い通過のリズムに。
  - **4 OSCILLATORS｜4 つの音源を、1 つの音に。** アナログ・ウェーブテーブル・FM・サンプルをレイヤーごとに。サンプルライブラリの置き場所はいらない。
  - **FX × 6｜並べ替えるだけで、音が変わる。** ドライブ・コーラス・ディレイ・リバーブ・EQ・リミッター。Off のエフェクトは計算しない。
  - **MACRO × 8｜8 つのマクロで、すぐ自分の音に。** 明るさ・アタック・リリース・広がり・リバーブなどを PLAY 画面に。MIDI ラーンで手元のつまみにも。
  - **ARP ・ GATE｜押さえるだけで、フレーズになる。** 16 ステップのアルペジエーター（強さと音程の行）と、音を刻むトランスゲート。ホストのテンポに合わせて動く。
  - **SYZYGY｜鍵盤ごとに、別の周期で。** 惑星直列アルペジオ。押さえた音がそれぞれ別の周期で鳴り、いちばん高い音がいちばん速い。周期が重なるステップだけ和音に、公倍数のところで全部がそろう（2-3-4・3-4-5・3-5-7）。
  - **ECLIPSE｜刻みを、日食のカーブで。** 蝕ゲート。閉じたステップの並びを、月が横切る 1 回の通過に。音は欠けて、細り、また満ちる。HARD でいつもの刻みに。
  - **SATELLITES｜ユニゾンが、周りを回る。** 衛星ユニゾン。重ねた声のデチューンの高さと左右の位置が、回りながら入れ替わる。Depth 0 ならいつものユニゾン。
  - **ROCHE LIMIT｜強く弾くと、音が砕ける。** ロッシュ限界。決めた強さを超えた音だけ、声が音程と左右に引き裂かれ、時間とともに 1 つに戻る（最大 24 半音、20 ms〜2 秒）。
  - **SWING-BY MORPH｜4 つの音のあいだを、飛んでいく。**（LP では写真つきの大きな枠）A は今の音、B・C・D にファクトリーのプリセット。探査機をドラッグすると、近い惑星ほど強く引く（距離の 2 乗に反比例）。位置はオートメーションでき、プリセットを替えても残る。
- **プリセット**：128 SOUNDS, ONE LEVEL ／ 選ぶのは、音量ではなく音色。
- **購入**：買い切り。使用期限なし。／月額なし・3 台まで・1.x は無料・オフライン・お支払いは Stripe（カード番号は SEVENTHWELL に届かない）
- **体験版**：無料で、全部試せる。体験版は製品版と同じプラグイン。機能もプリセットも制限なし。ライセンスがない間は、起動から 30 秒後、そのあと 60 秒ごとに 3 秒の無音。
- **締め**：SEE YOU IN ORBIT. ／ 体験版は無料です。128 の音を、あなたの曲で。

## English

- **Tagline**: Pick a sound. Play it. Put it in orbit.
- **Category**: The orbital preset synth.
- **One-liner**: A lightweight preset synth with 128 loudness-matched sounds and motion from gravity, flyby and an orbit LFO.
- **Description**: SWINGBY is a lightweight preset synth you can play the moment you load it. Pick from 128 factory presets, all matched to −16 LUFS, so you compare tone, not volume. Want more movement? Reach for gravity, flyby and the orbit LFO, and watch the planetary display move with your sound.
- **Features**
  - **Gravity — Tighten a wide sound.** Unison voices pull toward each other until their phases line up. Go from a thick supersaw to a focused single tone with one knob.
  - **Flyby — Hear every note fly in.** Notes approach, pass and leave, with pitch (Doppler), level, pan and air absorption moving together.
  - **Orbit LFO — Give your modulation a pulse.** Slow when far, fast near the planet. The flatter the orbit, the longer the build and the shorter the pass.
  - **4 oscillators — Four sources, one sound.** Choose analog, wavetable, FM or sample for each of the four layers. Waveforms are generated at startup, so there is no sample library to install.
  - **FX × 6 — Reorder to reshape.** Drive, chorus, delay, reverb, EQ and limiter in any order. Effects that are off are not processed.
  - **8 macros — Make it yours, fast.** Brightness, attack, release, width, reverb and more, right on the Play page. MIDI learn puts them on your controller.
  - **Arp and gate — Hold a chord, get a phrase.** A 16-step arpeggiator with velocity and pitch rows, and a trance gate that chops the sound, both locked to your host's tempo.
  - **Syzygy — Every key on its own orbit.** Each held note repeats on its own period, the highest note fastest. Notes sound together only where their periods meet, and all of them line up at the common multiple (2-3-4, 3-4-5 or 3-5-7).
  - **Eclipse — Gate on an eclipse curve.** A run of closed gate steps becomes one pass of a moon: the sound wanes, thins and comes back. Switch to Hard for the usual chop.
  - **Satellites — Unison that orbits.** The unison voices trade detune and stereo position as they circle. At depth 0 it is the plain unison.
  - **Roche limit — Hit hard, break apart.** Notes played harder than the limit are torn apart in pitch and pan, then come back together over time (up to 24 semitones, 20 ms to 2 s).
  - **Swing-by morph — Fly between four sounds.** A is the sound you have; B, C and D are factory presets. Drag the probe and the nearer planet pulls harder (inverse square of the distance). Automate the position; it stays when you change presets.
- **Presets**: 128 sounds, one level. Every factory preset is measured with BS.1770 loudness and set to −16 LUFS.
- **Buy**: Buy once. No subscription. Up to 3 computers. Free 1.x updates. Works offline after the first activation. Secure checkout by Stripe.
- **Trial**: Try everything free. The trial is the full plug-in, with every feature and preset. Without a licence, it inserts 3 seconds of silence 30 seconds after start and every 60 seconds after that.
- **Closing**: See you in orbit.

## まだ決まっていない欄

価格、Stripe の購入リンク、体験版のリンク、ライセンスのサイトの URL、特定商取引法に基づく表記・プライバシーポリシー・利用規約・お問い合わせ（LP の [ ] の箇所）、動作を確かめた DAW。

## LP を今の製品に合わせた記録（2026-10-09）

依頼者の指摘（「紹介ページの画像とか今と全然違うやろ」）で LP を見直した。直したもの：画面写真 6 枚と試聴動画（古いデザイン案の画面で、今の製品にない EVO FIND SIMILAR・SW LINK が写っていた）を今の画面から作り直した（`tools/in07_marketing_media.py`）／SCREENS に ARP を足し「画面は開発中のデザイン」を削った／特長と仕様に ARP・トランスゲート・MIDI を足した／Windows の版（10／11、WebView2）を入れた／「解除すれば別のパソコンへ」を、実装した購入者用ページ（1 年に 3 回まで）に合わせた／試聴の説明に「つなぎ目の短いフェード」を足した。

## LP に v0.15.0 を入れた記録（2026-10-10）

- 画面写真 5 枚と試聴動画を `tools/in07_marketing_media.py` で作り直し（MORPH ボタン、ARP の ALIGN と HARD／ECLIPSE、MOD の SATELLITES・ROCHE LIMIT が入った画面）。モーフの写真（`screen_morph_dark.webp`）を足した。
- FEATURES：SYZYGY・ECLIPSE・SATELLITES・ROCHE LIMIT の 4 枚（計 12 枚）と、モーフの写真つきの枠。SPECS の「変調」「アルペジエーター」に追記。
- LISTEN：「SWINGBY だけで作った 5 曲」（Slingshot・Afterglow・Neon Coastline・Escape Velocity・Low Orbit、音声は AAC の .mp4）。曲の説明は各曲のソース（`tools/songs/*.hpp`）の冒頭の説明から。
- TRY と FAQ：ブラウザ体験版への案内。リンク先はいまはアーティファクト（公開するときは自分のサイトの体験版の URL に替える）。

## 参考（構成と書き方だけを見たページ、2026-10-09）

- https://vital.audio/
- https://www.pluginboutique.com/products/5391-Phase-Plant
- https://www.native-instruments.com/de/products/nks-partners/u-he/diva/
