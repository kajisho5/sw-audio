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
| CLAP・VST3・AU（macOS）、macOS 11 以降（ユニバーサル）、Windows 64 ビット | CMakeLists.txt、CI |
| 買い切り、3 台まで、1.x のアップデート無料、最初の有効化のあとはオフライン | `docs/security.md`、`server/license/README.md` |
| 体験版：機能制限なし、起動から 30 秒後とその後 60 秒ごとに 3 秒の無音 | `core/include/sw/demo_gate.hpp`、`tests/test_demo_gate.cpp` |
| 画面：ダーク／ライト、動き 60・30・OFF | `docs/design/in07/README.md`（画面は開発中。画面のことを書くときは「開発中のデザイン」と添える） |

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
  - **MACRO × 8｜8 つのマクロで、すぐ自分の音に。** 明るさ・アタック・リリース・広がり・リバーブなどを PLAY 画面に。
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
  - **4 oscillators — Four sources, one sound.** Analog, wavetable, FM and sample on every layer. Waveforms are generated at startup, so there is no sample library to install.
  - **FX × 6 — Reorder to reshape.** Drive, chorus, delay, reverb, EQ and limiter in any order. Effects that are off are not processed.
  - **8 macros — Make it yours, fast.** Brightness, attack, release, width, reverb and more, right on the Play page.
- **Presets**: 128 sounds, one level. Every factory preset is measured with BS.1770 loudness and set to −16 LUFS.
- **Buy**: Buy once. No subscription. Up to 3 computers. Free 1.x updates. Works offline after the first activation. Secure checkout by Stripe.
- **Trial**: Try everything free. The trial is the full plug-in, with every feature and preset. Without a licence, it inserts 3 seconds of silence 30 seconds after start and every 60 seconds after that.
- **Closing**: See you in orbit.

## まだ決まっていない欄

価格、Stripe の購入リンク、体験版のリンク、対応する Windows の版、特定商取引法に基づく表記・プライバシーポリシー・利用規約・お問い合わせ（LP の [ ] の箇所）。

## 参考（構成と書き方だけを見たページ、2026-10-09）

- https://vital.audio/
- https://www.pluginboutique.com/products/5391-Phase-Plant
- https://www.native-instruments.com/de/products/nks-partners/u-he/diva/
