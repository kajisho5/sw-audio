# SWINGBY 取扱説明書

SW IN07 SWINGBY ― 軌道で動かすプリセットシンセ（SEVENTHWELL）

〔版〕 この説明書はプラグインの版 〔VERSION〕 に対応しています。

---

## 1. SWINGBY について

SWINGBY は、128 のファクトリープリセットから選んですぐに弾ける、軽量のプリセットシンセです。プリセットはすべて −16 LUFS（BS.1770 のラウドネス。カテゴリごとの試聴フレーズで測定）にそろえてあるので、音量ではなく音色で選べます。

音は画面の中で惑星系として描かれます。中心の星が音そのもの、まわりの 4 つの軌道がレイヤーです。重力・フライバイ・楕円軌道の LFO で音を動かすと、星系も一緒に動きます。

- レイヤー 4 枚（それぞれ Analog・Wavetable・FM・Sample の発振器、フィルター、エンベロープ 2 つ）
- 同時発音 最大 32、Poly／Mono／Legato、ユニゾン 最大 8
- LFO 2（楕円軌道の Orbit を含む）、変調マトリクス 8、マクロ 8、フライバイ、重力
- アルペジエーター（16 ステップ）とトランスゲート
- エフェクト 6（Drive・Chorus・Delay・Reverb・EQ・Limit）、順番の入れ替え可
- プリセットの保存、ホストのプリセットブラウザ（CLAP）からの読み込み
- 音のファイルを持ちません。波形は起動時に計算して作ります

## 2. 動作環境

| | |
| --- | --- |
| Windows | Windows 10／11（64 ビット）。画面の表示に Microsoft Edge WebView2 ランタイムを使います（Windows 11 には入っています。Windows 10 で入っていない場合は Microsoft のページから入れてください） |
| macOS | macOS 11 以降（Apple シリコン・Intel のユニバーサル） |
| 形式 | CLAP・VST3（Windows・macOS）、Audio Units（macOS） |
| ホスト | 上の形式のインストゥルメントを読み込める DAW |

〔動作を確かめた DAW と版は、確認後にここに書きます〕

## 3. インストール

ダウンロードした zip を展開し、使う形式のファイルを次のフォルダにコピーします。DAW を起動し直すと、プラグインの一覧に「SW IN07 SWINGBY」（メーカー SEVENTHWELL）が出ます。

**Windows**

| 形式 | ファイル | コピー先 |
| --- | --- | --- |
| VST3 | `SW IN07 SWINGBY.vst3`（フォルダごと） | `C:\Program Files\Common Files\VST3\` |
| CLAP | `SW IN07 SWINGBY.clap` | `C:\Program Files\Common Files\CLAP\` |

**macOS**

| 形式 | ファイル | コピー先 |
| --- | --- | --- |
| Audio Units | `SW IN07 SWINGBY.component` | `/Library/Audio/Plug-Ins/Components/`（または自分のホームの `~/Library/Audio/Plug-Ins/Components/`） |
| VST3 | `SW IN07 SWINGBY.vst3` | `/Library/Audio/Plug-Ins/VST3/` |
| CLAP | `SW IN07 SWINGBY.clap` | `/Library/Audio/Plug-Ins/CLAP/` |

**アンインストール**：コピーしたファイルを削除します。ユーザープリセット（6 章）、画面の設定、ライセンスファイル（4 章）は別の場所にあり、残ります。要らなければそれぞれのフォルダを削除してください。

## 4. 体験版とライセンス

体験版は製品版と同じプラグインです。機能もプリセットも制限はありません。ライセンスがない間は、起動から 30 秒後、そのあと 60 秒ごとに 3 秒の無音が入ります。

### 有効化

画面右上のライセンスの表示（体験版のときは「TRIAL」）を押すと、ライセンスの画面が開きます。

1. **このパソコンのコード**：64 桁のコードが出ます（パソコンの ID そのものではなく、それから作った値です）。
2. **キーで有効化**：購入後に届くライセンスキー（`SWL-` で始まる）を入れて ACTIVATE を押します。インターネットにつながっている必要があります。
3. **ライセンスファイルで有効化**：インターネットにつながらないパソコンでは、別のパソコンで〔販売ページの有効化のページ〕を開き、キーとこのパソコンのコードを入れてライセンスファイル（`.swlicense`）を受け取り、LICENCE FILE で読み込みます。

有効化すると、その場で無音が入らなくなります。有効化のあとはオフラインで使えます。

- 1 本のライセンスで 3 台まで有効化できます。同じパソコンで有効化し直しても台数は増えません。
- パソコンを入れ替えるときは〔お問い合わせ先〕へご連絡ください。
- ライセンスは買い切りで、使用期限はありません。1.x の版のアップデートは無料です。

ライセンスファイルの置き場所（有効化すると自動で入ります）：

- Windows：`%APPDATA%\SEVENTHWELL\Licenses\`
- macOS：`~/Library/Application Support/SEVENTHWELL/Licenses/`

## 5. 画面

画面は 5 つ（PLAY・LAYER・ARP・MOD・FX）です。上の帯で切り替えます。

**上の帯（右側）**

- **MOTION 60／30／OFF**：画面の動き。60 は表示のたび、30 は 1 秒に 30 回、OFF は止めた絵（値を変えると描き直します）。パソコンが重いときは 30 か OFF に。OS で「動きを減らす」設定が入っていると、最初は OFF になります。
- **☀／☾**：ダーク（夜空）とライト（昼の空）の切り替え。
- **100%**：画面の大きさ（75・90・100・115・130 %）。
- **ライセンス**：緑は有効化済み、琥珀は体験版。

動き・ダーク／ライト・大きさは、パソコンの利用者ごとに覚えておきます（すべての曲で同じ）。

### 5.1 PLAY

- **プリセット名と ‹ ›**：今のプリセット。‹ › で前後に。
- **CONSTELLATIONS（左）**：プリセットの一覧。ALL・LEAD・PAD・BASS・PLUCK・KEYS・SEQ・FX・USER（自分で保存したもの）で絞り込み。名前を押すと読み込みます。音を押さえたままでも、8 ms でつなぐので切り替えの音は途切れません。
- **SAVE PRESET**：今の音をユーザープリセットとして保存（6 章）。
- **星系（中央）**：中心の星が音、軌道がレイヤー。カットオフで光の輪が広がり、レゾナンスで細い輪が光り、ドライブで火花が出ます。ユニゾンは小さな月、デチューンはその散らばりです。
- **ORBITS（右）**：4 つのレイヤーの名前と音量。押すとそのレイヤーを選びます。下に選んだレイヤーの CUTOFF・RES・DRIVE・UNISON・DETUNE・アンプのエンベロープ。EDIT LAYER で LAYER 画面へ。
- **マクロ 8 つ**：BRIGHT・RESO・ATTACK・RELEASE・DRIVE・WIDTH・DELAY・REVERB。50 が「そのまま」です。ドラッグ（Shift で細かく）、ダブルクリックで 50 に、ホイールでも動きます。右クリックで MIDI ラーン（7 章）。
- **RIBBON**：押してなぞると C1〜C6 の音が鳴ります（鍵盤がなくても試せます）。
- **ARP**：アルペジエーターの入／切。

### 5.2 LAYER

- **レイヤーのカード（左）**：L1〜L4。押して選び、LAYER ON で入／切。
- **星系の拡大（中央）**と **PLAY NOTE**：選んだレイヤーの音を確かめる。
- **ENVELOPE**：AMP（音量）と FILTER（フィルター）のアタック・ディケイ・サステイン・リリース。
- **LFO 1／2**：形（Orbit・三角・鋸・矩形・ランダム）、RATE、SYNC（テンポに合わせる。Off のときは RATE）、ORBIT（楕円のつぶれ具合：0 で円、大きいほど「遠くでゆっくり、近くで一気に」）、START（Free：いつも回っている／Note：弾くたびに始めから）。
- **OSCILLATOR**：TYPE で音源の種類を選びます。
  - Analog：Sine・Triangle・Saw・Square、PULSE W（矩形の幅）
  - Wavetable：8 つの表（CLASSIC〜GLASS）、POSITION（表の中の位置）
  - FM：2 オペレーター。RATIO・INDEX・DECAY・FEEDBACK
  - Sample：8 種（AIR〜CLICK）
  - 共通：OCTAVE（±2）・SEMI（±12）・FINE（±100 セント）・UNISON（1〜8）・DETUNE・SPREAD（左右の広がり）・GRAVITY（ユニゾンの声が引き合い位相がそろう）・LEVEL・PAN・VELOCITY（強さで音量が変わる量）
- **FILTER**：LP 12・LP 24・BP 12・HP 12、CUTOFF・RESO・DRIVE・ENV（フィルターのエンベロープの量、±）・KEY（キートラック）

### 5.3 ARP（アルペジエーターとトランスゲート）

- **ARPEGGIATOR**：MODE（Up・Down・Up-Down・Order＝押した順・Random）、RATE（1/8・1/16・1/16 T・1/32）、OCTAVES（1〜4）、LENGTH（音の長さ）、SWING（裏拍を遅らせる）、STEPS（1〜16）。
  - 縦の棒：各ステップの強さ。ドラッグで変更、ダブルクリックで 0／100。0 は休み。
  - 下の段：各ステップの音程（0・+12・+7・−12）。押すたびに変わり、右クリックで 0 に。
- **TRANCE GATE**：16 ステップの入／切で音を刻みます。RATE（1/8・1/16・1/32）、DEPTH（刻みの深さ）。
- ホストが再生中はホストの拍に合わせます。止まっているときは最初の鍵盤から数えます。

### 5.4 MOD（変調）

- **変調マトリクス 8 段**：ON、SOURCE（LFO 1・LFO 2・Env 2・Velocity・Mod wheel・Aftertouch・Key・マクロ M1〜M8）→ TARGET（Cutoff・Resonance・Pitch・Drive・Pan・Level・L1〜L4 の音量・LFO 1／2 の速さ・Pulse width・Detune・Gravity・WT position・FM index）、AMOUNT（±100）。
- 星系に「重力の線」として描かれます。量が多いほど太く、元から先へ流れます（マイナスは逆向きの点）。
- **FLYBY**：弾くたびに音が近づく・通り過ぎる・去る動き。MODE（Off・Arrive・Pass・Leave）、DEPTH、TIME、NEAR（どれだけ近くを通るか）、SIDE（左→右・右→左・交互）。音程（ドップラー）・音量・左右・空気のこもりが一緒に動きます。
- **VOICE**：PLAY（Poly・Mono・Legato）、VOICES（同時発音 1〜32）、GLIDE（0〜2000 ms）、BEND（ピッチベンドの幅 0〜24 半音）、LEVEL（全体の音量）。

### 5.5 FX（エフェクト）

上に信号の道筋（声 → エフェクト → OUT）、下に 6 枚のカード。並びがそのまま処理の順番です。

- **ON／OFF**：Off のエフェクトは計算しません（軽くなります）。入／切は 5 ms でつなぎます。
- **‹ ›**：エフェクトを 1 つ前／後ろへ。並べ替えるときは 4 ms 音を下げてつなぎます。
- Drive（AMOUNT・TONE・MIX）、Chorus（RATE・DEPTH・MIX）、Delay（TIME：テンポに合わせた 1/16〜1/2・FEEDBACK・MIX）、Reverb（SIZE＝残響の長さ・DAMP・MIX）、EQ（LOW・MID・HIGH ±12 dB）、Limit（GAIN・CEILING・RELEASE。出力が CEILING を超えません）。

## 6. プリセット

- **ファクトリー**：128 種（LEAD 18・PAD 19・BASS 19・PLUCK 18・KEYS 18・SEQ 18・FX 18）。すべて −16 LUFS。
- **保存**：SAVE PRESET で名前・カテゴリ・作者・コメントを入れて SAVE。同じ名前があると確かめてから上書きします。名前に使えない文字は置き換えます。
- **保存先**（CLAP のホストのプリセットブラウザにも出ます）
  - Windows：`ドキュメント\SEVENTHWELL\SWINGBY\Presets\`
  - macOS：`~/Library/Audio/Presets/SEVENTHWELL/SWINGBY/`
- ファイル（`.swpreset`）は文字のファイルなので、ほかのパソコンへコピーして使えます。
- 曲を保存すると、使っているプリセットの名前も一緒に保存されます。
- ホストの汎用画面では、パラメータが「Layer 1/Filter」「Effects/Delay」などのまとまりで並びます。オートメーションはここから、または画面のつまみを動かして書けます。

## 7. MIDI

| 受けるもの | 働き |
| --- | --- |
| ノートオン／オフ・ベロシティ | 発音 |
| ピッチベンド | BEND の幅で音程 |
| CC 1（モジュレーションホイール） | 変調の SOURCE「Mod wheel」 |
| チャンネルプレッシャー（CLAP のプレッシャー） | 変調の SOURCE「Aftertouch」 |
| CC 64（サステインペダル） | サステイン |
| プログラムチェンジ 0〜127 | ファクトリープリセット 1〜128 を読み込む |
| CC 120・123 | すべての音を止める |
| MIDI ラーンした CC | そのマクロを動かす |

**MIDI ラーン**：PLAY 画面のマクロを右クリック → MIDI LEARN → コントローラーのつまみを動かすと、そのマクロに割り当てられます（つまみの上に CC 番号が出ます）。外すときは右クリック → FORGET。割り当ては曲と一緒に保存されます。CC 0・1・32・64・120〜127 は割り当てられません。

※ ホストによっては、プログラムチェンジや CC をプラグインに渡さない設定のものがあります。

## 8. 困ったとき

| こんなとき | 確かめること |
| --- | --- |
| 60 秒ごとに音が 3 秒消える | 体験版です。4 章の手順で有効化してください |
| Windows で画面が真っ暗、または「WebView2 Runtime が必要」と出る | Microsoft Edge WebView2 ランタイムを入れて、画面を開き直してください（音とホストの汎用画面はそのままでも使えます） |
| 画面が重い | MOTION を 30 か OFF に |
| CPU の負荷が高い | VOICES を減らす、UNISON を減らす、使わないレイヤーとエフェクトを Off に |
| プリセットブラウザに自分のプリセットが出ない | 保存先のフォルダ（6 章）にあるか。ホストのブラウザの再読み込み |
| ライセンスファイルが読み込めない | このパソコンのコードで作ったファイルか（別のパソコンのファイルは使えません）。ファイルが壊れていないか |

〔お問い合わせ先〕

## 9. 仕様

| | |
| --- | --- |
| 同時発音 | 1〜32（既定 16）、Poly／Mono／Legato |
| レイヤー | 4 |
| 発振器 | Analog（4 波形）・Wavetable（8 表）・FM（2 オペレーター）・Sample（8 種） |
| ユニゾン | 1〜8 声（デチューン・広がり・重力） |
| フィルター | LP 12・LP 24・BP 12・HP 12（ドライブ付き） |
| エンベロープ | レイヤーごとに AMP・FILTER |
| LFO | 2（Orbit・三角・鋸・矩形・ランダム、テンポ同期） |
| 変調 | マトリクス 8 段、マクロ 8、フライバイ、重力 |
| アルペジエーター | 16 ステップ（強さ・音程）、5 モード、スイング |
| トランスゲート | 16 ステップ |
| エフェクト | Drive・Chorus・Delay・Reverb・EQ・Limit（順番入れ替え可） |
| プリセット | ファクトリー 128（−16 LUFS）、ユーザー保存 |
| 出力 | ステレオ |

## 10. ライセンス表記

SWINGBY は次のものを使っています。

- フォント：Barlow Condensed、Michroma、Space Mono（SIL Open Font License 1.1。同梱の `licenses` フォルダ）
- CLAP SDK・clap-wrapper（MIT）、VST3 SDK（MIT。VST は Steinberg Media Technologies GmbH の商標です）、AudioUnitSDK（Apache 2.0、macOS）
- Monocypher（BSD-2-Clause／CC0）：ライセンスファイルの署名の確認
- WebView2 SDK（Microsoft、Windows）

詳しくは同梱の `licenses` フォルダをご覧ください。

© SEVENTHWELL
