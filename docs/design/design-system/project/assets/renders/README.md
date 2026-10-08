# Blender 描画の画像（assets/renders）

SW AUDIO の画面・販売素材で使っている Blender 描画（Blender 4.0.2、Cycles）の画像 37 種。
画面デザイン（キャンバス）の HTML に埋め込まれていたものを、**元のバイト列のまま**（再圧縮なし）1 枚ずつのファイルにした（2026-10-08）。
各ファイルの大きさ・SHA-256・元の CSS 変数名・使っている画面は `manifest.json`、見た目は `contact-sheet.png`。
取り出しは `tools/extract_renders.py <canvas/project> <出力先>` でやり直せる。

## 中身

| フォルダ | 枚数 | 内容 |
| --- | --- | --- |
| `parts/` | 11 | ノブ4種（knob-black 主ノブ／knob-alu セレクター／knob-ivory オプト系／knob-rubber LIVE の LED リング）192×192、screw 56×56、toggle-up・toggle-down 60×120、rack-ear 72×648、corner-bracket 64×64（LIVE）、fader-cap 80×44、vu-bezel 280×200 |
| `panels/` | 8 | パネル素材 900×420：anodized・brushed・graphite・olive・tolex・bronze・crinkle・walnut。製品ごとの割り当ては `docs/project/03_product_lineup.csv` の material 列（空なら crinkle） |
| `catalog/` | 13 | カタログ画像 600×450（EQ01〜EQ09、CS01〜CS04）。元は 1200×900 だが、キャンバスには縮小版しか入っていない |
| `marketing/` | 5 | hero-studio（3台積み）・hero-live（LV03）・hero-studio-x（X 告知用）2400×1350、logo3d-black・logo3d-9colors 1600×900 |

## 使うときの決まりごと（デザインシステム RENDERING.md と同じ）

- 照明はすべて左上キー＋右下フィル＋リム。**画像は回転させない**（光の向きが変わる）。
- **ノブ：** 本体は回さず、カテゴリ色の指針だけを回す（フィルムストリップ不要）。画像は本体の外側に影の余白を含むので、要素の 135 %（inset −17.5 %）で置くと本体の直径＝要素サイズになる。表示サイズは主ノブ 64px、小 40〜48px、大 72〜84px。
- **ラックイヤー：** グレーのヘアライン板。カテゴリ色の上に `mix-blend-mode: overlay` で重ねて色を付ける。穴にネジ（screw）を置く。
- **コーナー金具：** 1 枚を回転して 4 隅に使う（隅ごとに影の向きがずれるのは既知の妥協。ここだけ回転を許す）。
- **VU ベゼル：** 9 スライス（`border-image`、slice 40）で任意の大きさに伸ばす。
- **フェーダーキャップ：** MS01・LV03・LV12。**トグル：** 電源・切替（上下2状態を画像で切り替え）。
- 色の規定（状態色とカテゴリ色を混ぜない等）は `docs/project/01_design_system.md` と `02_design_tokens.json`。

## ないもの

- 描画の元（.blend、`gen/` `blender/` の生成スクリプト一式 = `sw-audio-handover.zip`）は入っていない。このため部品の描き直し、カタログの残り約 125 本、1200×900 のカタログ原寸は作れない。必要になったら依頼者に handover の zip を頼む。

## 追加分（2026-10-08、Claude Code で描画）

| フォルダ | 内容 |
| --- | --- |
| `pedals/` | GT03 のペダル本体 6 種（comp・drive・fuzz・chorus・delay・reverb、各 168×254 の透明 WebP）。種類ごとに仕上げ（ブラシ・ハンマー・ラメ・光沢・シワ・クリーム＋木の側板）、つまみの数・大きさ・配置、刻印（メーター窓・ネジ・稲妻・波形・エコー・減衰）が違う。配置は `tools/blender/pedals.json`（画面と描画で共通）、フットスイッチのクローム・キャップ `stomp.webp`（58×58）、小ノブ `knob-small.webp`（180×180、影なし） |

描画スクリプトは `tools/blender/`（`pedal.py`・`stomp.py`・`knob.py`、後処理 `post.py`）。Blender 4.0.2（apt）、Cycles、CPU。この Blender には OIDN がないため、高サンプルで描いて OpenCV でノイズ除去している（元の素材と同じ事情）。
照明は元の素材と同じ構成（左上キー＋右下フィル＋リム）。元の素材の描画スクリプト（handover の zip）は無いので、形状・材質は新しく作ったもの。

| `parts/vu-bezel-frame.webp` | VU メーターの枠（900×546、中は透明で枠の影だけ）。`tools/blender/vu_bezel.py`。border-image（slice 30）で全サイズに使う |
| `parts/reel.webp` | テープのリール（288×288、透明）。リム・3 本のスポーク・ハブ。`tools/blender/reel.py`。画面で回す（光が回らないよう、柔らかい上からの光だけ）。DL02・SA01 |
| `parts/rotor-horn.webp`・`rotor-drum.webp` | 回転スピーカー MD05 のホーン（2 つのベル）とドラム（スリット付きの胴）を上から見た図（240×240、透明）。`tools/blender/rotor.py`。画面で回す |
| `parts/vu-glass.webp` | VU メーターのガラスの反射（840×486、反射だけの白い透明画像）。`tools/blender/vu_glass.py`（少し膨らんだガラスに柔らかい光源を映す）＋ `post_glass.py`。デザイン側の平らな白い反射の代わりに、針の上に重ねる |
| `in07/core.webp`・`moon.webp`・`hero.webp`・`hero-swingby.webp` | SWINGBY（SW IN07）A 案「Orbital」。中心の球（ガラスの殻＋光る星雲、640×640、**黒背景**：発光の霧は透明度を持たないので画面では `mix-blend-mode: screen` で重ねる）、衛星（真珠色の球、192×192、透明）、全景（1920×1080、ブルーム付き。コンセプト用で販売素材ではない）、全景にロゴタイプ（`product-logos/swingby-dark.svg`、幅 500px）を重ねたキービジュアル。`in07/bodies/` はレイヤーごとの天体（自転のスプライトシート、1 回転 96 コマを 12 列×8 行に左上から行ごと・透明）：ring＝L1 ウェーブテーブル（縞の惑星＋環、1 コマ 160×160）、pearl＝L2 VA（真珠）、crater＝L3 サンプル（クレーター）、crystal＝L4 FM（多面体の宝石）、各 96×96。lfo（LFO の月、96×96）・bead（ユニゾンの小衛星、64×64）は 1 コマ。天体を光の下で回して描いたもの（光は左上のまま表面だけが動く）なので、画面はコマを送ってよい（画像そのものは回さない）。`tools/blender/in07_orbital.py body <種類> <出力>` → `tools/blender/pack_sheet.py``tools/blender/in07_orbital.py`。リムは左上の後ろ、フィルはガラスに映さない（映り込みを左上に揃えるため）。衛星は画面で軌道に沿って動かすが回さない |
