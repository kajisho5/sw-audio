# SWINGBY（SW IN07）画面デザイン — A 案「Orbital」

デザインキャンバス「SWINGBY（SW IN07）Synth UI」の最下段 2 行（「SWINGBY — A version screens」）のソース。2026-10-08 時点。
キャンバスの書式（`.dc.html`：`{{穴}}`・`<sc-for>`・`<sc-if>`・`<dc-import>`・`class Component extends DCLogic`）のまま置いてある。プラグインの画面（WebView）に移すときの元。

| ファイル | 画面 | 中身 |
| --- | --- | --- |
| `Orbit.dc.html` | 共通部品 | 動く星系ビュー（820×740）。props：`values`（cutoff・res・unison・detune・lfoRate・lfoDepth・drive・release・bpm・spb・gateSpb）、`layers`、`focus`、`view`（system／close／arp／mod）、`mods`、`arpSteps`、`gateSteps`、`arpOn`、`gateOn`、`noteKey`、`motion`（60／30／off）、`theme`（dark／light）、`scale`。自前の描画ループを持つ |
| `SW_Main.dc.html` | PLAY（ダーク） | プリセット（カテゴリ・一覧・EVO Similar）、星系ビュー、選んだレイヤー（Cutoff・Res・Drive・Unison・Detune・アンプのエンベロープ）、マクロ 8、リボン（押すと発音）、ARP |
| `SW_Main_Light.dc.html` | PLAY（ライト） | `SW_Main` を `theme="light"` で読み込む |
| `SW_Layer.dc.html` | LAYER | レイヤー 4 枚の切り替え、選んだレイヤーの天体の大写し（`view="close"`）、OSC（種類で天体が替わる）・FILTER・AMP エンベロープ・LFO |
| `SW_Arp.dc.html` | ARP | アルペジエーター（モード・レート・オクターブ・長さ・スイング、16 ステップのベロシティと音程）とトランスゲート（16 ステップ・レート・深さ）。外周の輪と内側の輪が拍で光る |
| `SW_Mod.dc.html` | MOD | 変調 8 スロット（ソース → 行き先・量・On）。星系ビューに「重力線」として描く（太さ＝量、流れる向き＝正負） |
| `SW_FX.dc.html` | FX | 6 スロット（Drive・Chorus・Delay・Reverb・EQ・Limit）の順番入れ替え・On/Off・パラメータ。音の通り道をスイングバイの軌道として描き、光の点が流れる。駅はエフェクトごとに別の天体（Blender、自転のコマ送り）。Off の駅は色と明るさを落とす |

- 全画面の右上に動きの設定 MOTION 60／30／OFF（依頼者の希望）。OS の「視差効果を減らす」なら OFF で始まる。
- 数値・プリセット名・FX の既定値は画面の見本用の設計値（エンジンにまだ無い機能を含む）。エンジンの既定値（README「IN07 の設計」）と揃えてあるのは L1 の値（Cutoff 2.4 kHz・Res 30・Drive 18・Detune 22・A 5 ms／D 320 ms／S 70 %／R 420 ms）。
- 文字は SVG の `<text>` ではなく HTML の要素で置く（キャンバスでは `{{穴}}` 入りの SVG 文字が表示されなかったため。FX の駅名がこれで消えていた）。
- 色はデザインシステムのカテゴリ色（Creative・Instrument のティール）だけ。状態色は SW LINK の点（緑）だけに使う。光は左上から（天体・中心の球は Blender で左上から照らした画像）。

## 画像（`/_blob/…` の中身）

キャンバスでは画像をアップロード先の `/_blob/<id>` で参照している。リポジトリ内のファイルとの対応：

| `/_blob/` id | ファイル |
| --- | --- |
| eea73410a66867fa6abf11bd1a56eee6 | `docs/design/design-system/project/assets/product-logos/swingby-dark.svg` |
| 702f4b752a177d6e1601ccac384e7886 | `…/product-logos/swingby-light.svg` |
| 90121e1a7bee5f359e51e024081e48ca | `docs/design/design-system/project/assets/renders/in07/core-alpha.webp`（暗い画面の中心の球） |
| dbb1f155628a29abbb82a4a9674392c5 | `…/renders/in07/coreday.webp`（ライト画面の中心の球） |
| e6ee8b6569e63ef27cedfc1666d39d47 | `…/renders/in07/bodies/ring.webp` |
| 7709cc70a363f53b1b2d78aa4da28c56 | `…/renders/in07/bodies/pearl.webp` |
| 8c95e5bce1ae22c9f1954ef07f849f45 | `…/renders/in07/bodies/crater.webp` |
| af099d0a5b96dfa72328a1af6c2cb648 | `…/renders/in07/bodies/crystal.webp` |
| d939afb85ed95fb8e9491d08127f2208 | `…/renders/in07/bodies/lfo.webp` |
| e38db7eb052c9ac25fd71783f0559f60 | `…/renders/in07/bodies/bead.webp` |
| fe87118687e9fb3359a02b9dbc9fdcbd | `…/renders/in07/fx/fx_drive.webp` |
| ce619443ef6e3e08f54434c374143598 | `…/renders/in07/fx/fx_chorus.webp` |
| 043458125b4f450392ec146178f4c7a4 | `…/renders/in07/fx/fx_delay.webp` |
| 7ebc58bb2417d398eb1ee5ceacc9c52a | `…/renders/in07/fx/fx_reverb.webp` |
| 56941051af29746414654413d55d0cf3 | `…/renders/in07/fx/fx_eq.webp` |
| 4a8c4eb3f89a1d3aaaa204b0dbd11874 | `…/renders/in07/fx/fx_limit.webp` |
