# SW AUDIO 仕様書 v1.0 初稿（全139製品）

Oct 5, 2026 · @shogo kajiyama

## この文書について

v1.0 初稿は全139製品（STUDIO 109・LIVE 30）を定義する。詳細形式で書いたのは共通章、EQ・ストリップ13製品（EQ01〜EQ09、CS01〜CS04）、DY・MS 19製品（DY01〜DY12、MS01〜MS07）。SA 以降の107製品は簡略形式。

- **正とするデータ：** 03\_product\_lineup.csv（製品一覧）、04\_parameters.csv（画面から抜き出したパラメータ）、05（決定事項）、01・02（デザインシステム）。
- **範囲と既定値：** 「範囲」はホストに公開する値域、「既定値」は新規インスタンスの初期値。画面に描いた値はデモ状態なので、既定値とは別に扱う。
- **見積もり：** 遅延の一部とCPU目安はすべて**設計上の見積もり**。実測値ではない。実測後に差し替える。
- **案：** 05 に無い新しい決め事は「案」と書く。承認されたら外す。
- **【要確認】：** 画面値と仕様のずれ、または未決事項。最終章「確認事項」にまとめる。

## 共通章

全139製品はこの章の規約に従う。製品章には、この章との差分だけを書く。

### 1. 形式と動作条件

| 項目 | 仕様 | 状態 |
| --- | --- | --- |
| 形式 | VST3 ／ AU ／ CLAP。OBS は専用の「SW AUDIO for OBS」で対応（決定、LIVE 共通章の1）。AAXは後回し、VST2は使わない | 決定済み（05） |
| サンプルレート | 44.1〜192 kHz | 案 |
| チャンネル | モノ／ステレオ。M/S 切替のある製品はステレオ時のみ有効 | 案 |
| ブロック長 | 可変 1〜4096。内部では最大32サンプル単位に分割してパラメータを補間 | 案 |
| 演算精度 | フィルタ係数と状態量は64-bit、その他は32-bit浮動小数 | 案 |
| 実装フレームワーク | 未決定 | 【要確認】 |

### 2. パラメータ規約

- **ID：** `製品コード.セクション.名前`（小文字。例 `eq05.hmf.gain`）。一度公開したIDは変えない。保存済みプロジェクトの互換を守るため。
- **ホスト公開値：** 正規化 0〜1。下の「カーブ」で実値に変換する。
- **オートメーション：** 連続値とセレクターはすべて可（○）。UI専用のもの（表示倍率・Undo/Redo・操作履歴・表示切替）と、解析を始めるボタン（Learn・Assist・Match など）は不可（—）。
- **In（パネルの電源トグル）：** 製品内バイパス。ホストのバイパスパラメータ（VST3 の bypass フラグ、CLAP の bypass）と連動する。Off でも遅延値は変えず、10 ms のクロスフェードで切り替える。
- **なめらかさ：** ゲイン類は 20 ms のランプで追従。EQのフィルタは状態変数フィルタ（TPT SVF）で組み、係数をサンプルごとに補間しても発振しない構造にする（案）。
- **パネル目盛りの扱い：** 「0〜10」や「Dark〜Bright」は表示上の目盛り。実値との対応は製品章で定義する。「Dry〜Wet」は Mix 0〜100 %（直線、既定 100 %）。「Output −10〜+10」は dB（直線、既定 0.0 dB）。
- **数値表示：** 02 の value スタイル（数値＋半角スペース＋単位）。dB は 0.1 刻み。周波数は 1 kHz 未満が整数 Hz、1〜10 kHz が小数1桁 kHz、10 kHz 以上が整数 kHz。Q は小数2桁、% は整数。

### 3. カーブ定義

| 記号 | 名前 | 正規化値 x（0〜1）→ 実値 v | 主な用途 |
| --- | --- | --- | --- |
| LIN | 直線 | v = min + x × (max − min) | dB ゲイン、%、Mix |
| LOG | 対数 | v = min × (max ÷ min)^x | 周波数、Q、レシオ |
| SKW | スキュー | v = min + (max − min) × x^k（k は製品章で指定） | アタック、リリースなどの時間 |
| STEP | 段階 | n 段を等間隔に割り当て | セレクター、段階ゲイン |

### 4. 信号の流れ（全製品共通）

1. 入力を Dry 経路に分岐する。Dry 経路は本体と同じ遅延で揃える。
2. 本体の DSP。非線形段だけをオーバーサンプリングする。
3. Auto gain の補正をかける。
4. Mix（製品にある場合）で Dry と混ぜる。
5. Output ゲイン。
6. Δ が有効なら「Auto gain 補正後の Wet − Dry」を出力する。

### 5. 全製品共通の機能

| 機能 | 仕様 | オートメーション | 状態 |
| --- | --- | --- | --- |
| モーフ | A／B 2プリセット間を Morph 0〜1 で補間。連続値は正規化値で直線補間するため、周波数は対数、dB は直線で動く。セレクターは 0.5 で切り替え、10 ms クロスフェード。解析ボタンは補間しない | ○ | 決定済み（05）、補間方式は案 |
| Auto gain | 入力と Wet のラウドネス（BS.1770 の K 特性、3 秒窓）を比べて補正ゲインを出す。追従 2 秒、補正幅 ±18 dB、−60 LUFS 以下の無音では更新しない | ○（On/Off） | 決定済み（05）、数値は案 |
| Δ | 上の手順6。変化した成分だけを聴く | ○（On/Off） | 決定済み（05） |
| SW Link | 同じホストプロセス内の SW AUDIO 同士でスペクトル・ラウドネス・トラック種別を共有する。プロセス内の共有領域とロックフリーのリングバッファで渡す。ホストがプラグインを別プロセスで動かすと届かないため、その場合は Link 表示を消灯する | — | 案 |
| Unit A／B／C | アナログ筐体（2Uラック・500シリーズ）のみ。部品公差を模した固定の偏差を左右別に持つ（ゲイン ±0.3 dB、周波数 ±3 %、飽和の効き始め ±0.5 dB）。A が基準、B・C は固定シード。デジタル筐体には出さない（画面も同じ） | ○ | 案 |
| 操作履歴 | Undo/Redo 100段 | — | 案 |
| Low lat | 遅延の出る処理を低遅延版に切り替える。切替時はホストに遅延変更を通知する（VST3 の再起動通知、CLAP の latency 拡張、AU の Latency プロパティ） | ○ | 決定済み（05）、動作は製品章 |
| オーバーサンプリング | 1×／2×／4×、既定 2×。標準は最小位相 IIR ハーフバンド（報告遅延 0。周波数で変わる数サンプルの群遅延あり）。高品質設定は直線位相 FIR（遅延を報告） | ○ | 案 |

**EVO スイッチ（案）：** 進化機能が音を自動で動かす製品は、On/Off を `製品コード.evo.on` として公開する（Auto ○）。操作は EVO バー左端の EVO タグ。既定値は製品章で決める。解析ボタン型（Assist・Match など）はスイッチを持たない。

### 6. 遅延の規約

- 遅延はサンプル数でホストに報告する。レートに比例する値は「@48 kHz」で書く。
- Dry 経路（Δ・Mix・In Off）は本体と同じ遅延に揃え、ホストの遅延補正とずれないようにする。
- LIVE ラインはゼロ遅延が原則。遅延が出る場合はツールバーに常時表示する（05）。

### 7. CPU目安の基準（設計上の見積もり）

ステレオ1インスタンス、48 kHz での処理量で3段階に分ける。実測値ではない。基準機と測定方法は未決定。

| 段階 | 処理量の目安 |
| --- | --- |
| 軽 | フィルタ20本程度以下。非線形段なし、または 2× OS の非線形1段 |
| 中 | 2〜4× OS の非線形が複数段、またはエンベロープ検出付きの多バンド処理 |
| 重 | 数千タップ以上の FIR 畳み込み、多数の FFT、ML推論 |

スペクトル表示など画面だけの解析は UI スレッドで行い、オーディオ処理の段階には含めない。

### 8. 進化機能（EVO）の難易度区分

| 区分 | 中身 | 時期 |
| --- | --- | --- |
| A：ルール・DSP | 固定の信号処理・規則表・ログだけで完結 | 初期 |
| B：解析 | 統計、スペクトル解析、ピッチやテンポの検出、数秒の学習ボタン | 中期（案） |
| C：ML | 学習済みモデルの推論 | 後期 |

05 は「ログ系＝初期、ML系＝後期」の2区分。B を間に足すのは案。

## EQ01 Passive 進化版

パッシブ型のトーンシェイパー。低域シェルフと高域（Air）の2バンドに、出力段の真空管風ドライブを持つ。STUDIO、2Uラック（crinkle）。ボーカル・バス・マスターの音色付け用。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Low Freq | eq01.low.freq | 20〜400 | Hz | 60 | LOG | ○ |
| Low Gain | eq01.low.gain | −12〜+12 | dB | 0.0 | LIN | ○ |
| Contour | eq01.low.contour | 0〜10 | — | 0 | LIN | ○ |
| Air Freq | eq01.air.freq | 2〜20 | kHz | 10 | LOG | ○ |
| Air Gain | eq01.air.gain | −12〜+12 | dB | 0.0 | LIN | ○ |
| Width | eq01.air.width | Narrow〜Wide（Q 2.0〜0.4） | — | Q 0.8 | LOG | ○ |
| Drive | eq01.out.drive | 0〜10（入力 0〜+18 dB） | — | 2 | LIN | ○ |
| Output | eq01.out.level | −10〜+10 | dB | 0.0 | LIN | ○ |
| Mode | eq01.mode | LR ／ MS | — | LR | STEP | ○ |

**DSP方式**

- Low：2次の低域シェルフ（TPT SVF）。Contour でシェルフの Q を 0.71 から 2.0 まで上げ、カットオフ下の盛り上がりと上の窪みを1つのノブで作る。
- Air：Gain がプラスなら Width 付きのベル、マイナスならシェルフで削る（パッシブ機の流儀）。20 kHz 付近のベルは双一次変換で形が崩れるため、高域を補正した係数設計を使う（案）。
- Drive：偶数次寄りの非対称ソフトクリップ1段、2× OS。ドライブ量に応じて出力を自動で下げ、Drive を回しても音量がほぼ変わらないようにする。Heat ランプ（5灯）はドライブ量の表示のみ。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 軽。フィルタ3本＋非線形1段（2× OS）。

**進化機能：** Contour。従来は同じ周波数でブーストとカットを同時に回して作っていた「盛り上げて少し上を削る」形を、シェルフの Q 1本で作る。区分 A（初期）。

**共通機能との差分：** Unit A／B／C あり（アナログ筐体）。

**【要確認】**

- 04 では Low Freq・Air Freq が「セレクター」だが、画面（EQ01\_v2）は両端の値だけの連続ノブ。本書は連続ノブ（LOG）とした。段階式にするなら段数を決める。
- キャンバスに EQ01 が3枚ある（EQ01・EQ01B・EQ01\_v2）。04 と一致する EQ01\_v2 を正とした。

## EQ02 Surgical 進化版

多バンドのデジタル精密EQ。共振の除去と他トラックとのぶつかり解消が主目的。STUDIO、デジタル筐体。

下表のバンドパラメータは n = 1〜24（最大24バンド、案）。画面に見えている5バンドは初期表示で、「＋」で追加する。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Band n On | eq02.bn.on | Off ／ On | — | Off（1〜5 は On） | STEP | ○ |
| Band n Type | eq02.bn.type | Bell ／ Lo shelf ／ Hi shelf ／ Lo cut ／ Hi cut ／ Notch | — | Bell | STEP | ○ |
| Band n Freq | eq02.bn.freq | 10〜30,000（上限は fs の 0.49 倍） | Hz | 100・400・1.6k・6.3k・12k（1〜5） | LOG | ○ |
| Band n Gain | eq02.bn.gain | −30〜+30 | dB | 0.0 | LIN | ○ |
| Band n Q | eq02.bn.q | 0.10〜40 | — | 1.00 | LOG | ○ |
| Band n Slope | eq02.bn.slope | 6 ／ 12 ／ 18 ／ 24 ／ 36 ／ 48 ／ 72 ／ 96（cut のみ） | dB/oct | 24 | STEP | ○ |
| Band n Place | eq02.bn.place | Stereo ／ Mid ／ Side（Mid/side 有効時） | — | Stereo | STEP | ○ |
| Band n Dyn Range | eq02.bn.dynrange | −24〜+24（0 で無効＝画面の Range off） | dB | 0.0 | LIN | ○ |
| Band n Dyn Thresh | eq02.bn.dynthresh | −60〜0 | dB | −30 | LIN | ○ |
| Phase | eq02.phase | Zero latency ／ Natural ／ Linear | — | Zero latency | STEP | ○ |
| Mid/side | eq02.ms | Off ／ On | — | Off | STEP | ○ |
| Output | eq02.out | −24〜+24 | dB | 0.0 | LIN | ○ |
| Assist ／ Unmask | — | 解析ボタン | — | Off | — | — |

**DSP方式**

- Zero latency：TPT SVF の最小位相 IIR。高域の形崩れはアナログ原型に合わせた係数設計で補正する。Cut は2次セクションの縦続で 6〜96 dB/oct。
- Natural：振幅は Zero latency と同じ。位相をアナログ原型に近づける短い補正 FIR を足す。
- Linear：目標振幅から FIR を作る直線位相。EQ08 と同じ畳み込みエンジンを共有する（FIR 2048 タップ、分割 FFT 畳み込み）。
- Dynamic：バンドごとに、そのバンド帯域を切り出した信号のエンベロープで Gain を Dyn Range まで動かす。アタック・リリースは周波数から自動で決める（低いほど遅く）。

**遅延（@48 kHz）：** Zero latency 0 ／ Natural 256 ／ Linear 1024 サンプル。Natural・Linear の値は設計上の見積もり。Low lat を押すと Zero latency に切り替わる。

**CPU目安（設計上の見積もり）：** Zero latency は軽（5バンド）〜中（24バンド・Dynamic 多用）。Linear は重。

**進化機能**

- Assist：短時間 FFT のスペクトルを 1/24 oct で平滑化し、1/3 oct の移動中央値より一定以上高く、数秒続く山を共振として印を付ける。印をタップすると Notch／Bell を置く。区分 B（中期）。
- Unmask：SW Link で他インスタンスのスペクトルを受け取り、両方のエネルギーが高い帯域を重なりとして表示する。SW Link が届かない環境では使えない。区分 B（中期）。

**共通機能との差分：** Unit A／B／C なし（デジタル筐体）。

**【要確認】**

- 画面に Δ ボタンが無い（EQ07・EQ08・CS04 も同じ）。Δ は全製品標準なので、デジタル筐体のどこに置くかを決める。
- バンド数の上限（24は案）。
- Dynamic は EQ07 と機能が重なる。EQ02 側は「バンドごとの簡易版」とする前提で書いた。

## EQ03 Mid Shaper

パッシブ型の中域シェイパー。Dip（削り）と Peak（持ち上げ）の2バンド。STUDIO、2Uラック（graphite）。ボーカルやギターの中域の抜けを作る用途。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Dip kHz | eq03.dip.freq | 0.2 ／ 0.5 ／ 1 ／ 1.5 ／ 2 ／ 3（6段） | kHz | 1 | STEP | ○ |
| Dip | eq03.dip.amount | 0〜10（0〜−10 dB） | — | 0 | LIN | ○ |
| Peak kHz | eq03.peak.freq | 0.7 ／ 1 ／ 1.5 ／ 2 ／ 3 ／ 4 ／ 5（7段） | kHz | 2 | STEP | ○ |
| Peak | eq03.peak.amount | 0〜10（0〜+10 dB） | — | 0 | LIN | ○ |
| Width | eq03.width | Narrow〜Wide（Q 2.5〜0.6） | — | Q 1.2 | LOG | ○ |
| Drive | eq03.drive | 0〜10（入力 0〜+18 dB） | — | 2 | LIN | ○ |
| Output | eq03.out | −10〜+10 | dB | 0.0 | LIN | ○ |
| Ride（EVO） | eq03.evo.on | Off ／ On | — | Off | STEP | ○ |

**DSP方式**

- Dip・Peak：TPT SVF のベル2本。Width は両バンド共通。
- Drive：EQ01 と同じ出力段モジュール（非対称ソフトクリップ1段、2× OS、音量補正つき）。
- Ride：入力の 200 Hz〜5 kHz を RMS 検出（アタック 50 ms、リリース 300 ms）。基準（−18 dBFS RMS、案）より大きいほど Peak の実効ゲインを下げ、小さいほど設定値に戻す。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 軽。フィルタ3本（検出用含む）＋非線形1段。

**進化機能：** Peak が歌の音量に合わせて自動で動く。大声の部分で刺さらず、小声の部分で埋もれない。区分 A（初期）。

**共通機能との差分：** Unit A／B／C あり。

**【要確認】**

- 画面に Ride の On/Off が無い。EVO タグで切り替える前提（共通章の EVO スイッチ案）。
- Ride の基準レベル −18 dBFS RMS は案。

## EQ04 Inductor

インダクター型のコンソールEQ。HPF＋3バンド、周波数は段階式。STUDIO、2Uラック（bronze）。ボーカル・ギター・ドラムの太さ付け。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| HPF | eq04.hpf | Off ／ 50 ／ 80 ／ 160 ／ 300（5段） | Hz | Off | STEP | ○ |
| Low Hz | eq04.low.freq | 35 ／ 60 ／ 110 ／ 220（4段） | Hz | 60 | STEP | ○ |
| Low | eq04.low.gain | −16〜+16 | dB | 0.0 | LIN | ○ |
| Mid kHz | eq04.mid.freq | 0.36 ／ 0.7 ／ 1.6 ／ 3.2 ／ 4.8 ／ 7.2（6段） | kHz | 1.6 | STEP | ○ |
| Mid | eq04.mid.gain | −18〜+18 | dB | 0.0 | LIN | ○ |
| High kHz | eq04.high.freq | 10 ／ 12 ／ 16（3段） | kHz | 12 | STEP | ○ |
| High | eq04.high.gain | −16〜+16 | dB | 0.0 | LIN | ○ |
| Drive | eq04.drive | 0〜10（入力 0〜+18 dB） | — | 2 | LIN | ○ |
| Output | eq04.out | −10〜+10 | dB | 0.0 | LIN | ○ |
| Iron（EVO） | eq04.evo.on | Off ／ On | — | On | STEP | ○ |

**DSP方式**

- HPF：18 dB/oct（3次）。
- Low：シェルフ。カットオフの少し下にインダクター特有の小さな盛り上がり（約 +1 dB、設計値）を持たせる。
- Mid：ブロードなベル（Q 0.9 固定）。
- High：シェルフ。
- Iron：各バンドの「持ち上げた分」だけを並列に取り出し、鉄心風の飽和（低域ほど効く周波数依存ソフトクリップ）を通してから足し戻す。飽和量はそのバンドのブースト量に比例し、カット時は0。2× OS。
- Drive：出力段（EQ01 と同じモジュール）。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 中。フィルタ5本＋非線形4段（Iron 3＋Drive）を 2× OS。

**進化機能：** Iron。ブーストしたバンドほど倍音が乗り、カットしたバンドは濁らない。区分 A（初期）。

**共通機能との差分：** Unit A／B／C あり。

**【要確認】**

- Iron の既定を On にした（製品の個性のため）。Off 既定にするなら変更する。

## EQ05 Console

4バンドのコンソールEQ。HF・LF はシェルフ／ベル切替、中2バンドは Q 可変。STUDIO、2Uラック（anodized）。万能のミックス用EQ。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| HF Gain | eq05.hf.gain | −15〜+15 | dB | 0.0 | LIN | ○ |
| HF Freq | eq05.hf.freq | 1.5〜16 | kHz | 8 | LOG | ○ |
| HF Shape | eq05.hf.shape | Shelf ／ Bell | — | Shelf | STEP | ○ |
| HMF Gain | eq05.hmf.gain | −15〜+15 | dB | 0.0 | LIN | ○ |
| HMF Freq | eq05.hmf.freq | 0.6〜7 | kHz | 2 | LOG | ○ |
| HMF Q | eq05.hmf.q | 0.5〜3 | — | 1.0 | LOG | ○ |
| LMF Gain | eq05.lmf.gain | −15〜+15 | dB | 0.0 | LIN | ○ |
| LMF Freq | eq05.lmf.freq | 0.2〜2.5 | kHz | 0.6 | LOG | ○ |
| LMF Q | eq05.lmf.q | 0.5〜3 | — | 1.0 | LOG | ○ |
| LF Gain | eq05.lf.gain | −15〜+15 | dB | 0.0 | LIN | ○ |
| LF Freq | eq05.lf.freq | 30〜450 | Hz | 100 | LOG | ○ |
| LF Shape | eq05.lf.shape | Shelf ／ Bell | — | Shelf | STEP | ○ |
| HPF | eq05.hpf | Off ／ 40 ／ 80 ／ 120 ／ 200（5段） | Hz | Off | STEP | ○ |
| LPF | eq05.lpf | Off ／ 8k ／ 12k ／ 16k ／ 20k（5段） | Hz | Off | STEP | ○ |
| Drive | eq05.drive | 0〜10（入力 0〜+18 dB） | — | 2 | LIN | ○ |
| Drive Pos | eq05.drive.pos | Pre ／ Post（EQ の前／後） | — | Post | STEP | ○ |
| Output | eq05.out | −10〜+10 | dB | 0.0 | LIN | ○ |
| Match | — | 解析ボタン（EVO バー） | — | — | — | — |

**DSP方式**

- 全バンド TPT SVF。HMF・LMF は定Q（ブーストとカットで同じ形）。Bell 時の HF・LF は Q 0.7 固定。
- HPF 18 dB/oct、LPF 12 dB/oct。
- Drive：コンソール風の対称ソフトクリップ（奇数次寄り）1段、2× OS、音量補正つき。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 軽。フィルタ7本前後＋非線形1段。

**進化機能：** Match。参照曲（プラグインに音声ファイルをドロップ、または SW Link 経由で UT03 Reference から）と入力の長時間平均スペクトル（1/6 oct）を比べ、差分カーブを EQ05 の各ノブ範囲に収まるよう最小二乗で当てはめて値を書き込む。入力側は 10 秒以上の再生で学習。書き込んだ結果は Undo で戻せる。区分 B（中期）。

**共通機能との差分：** Unit A／B／C あり。

**【要確認】**

- 04 の HMF Freq「.6〜7k」、LMF Freq「.2〜2.5k」は 0.6〜7 kHz、0.2〜2.5 kHz と解釈した。
- 画面の「Pre／Post」は Drive の位置と解釈した。別の意味なら直す。
- 画面に Match ボタンが無い。EVO バーから起動する前提。

## EQ06 Stepped

段階式ゲインの3バンドEQ。ゲインを大きくするほど帯域が狭くなる（比例Q）。STUDIO、2Uラック（brushed）。キック・スネア・ベースの輪郭作り。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Low Hz | eq06.low.freq | 30 ／ 50 ／ 100 ／ 200 ／ 300 ／ 400（6段） | Hz | 100 | STEP | ○ |
| Low Gain | eq06.low.gain | −12〜+12、2 dB 刻み（13段） | dB | 0 | STEP | ○ |
| Mid kHz | eq06.mid.freq | 0.4 ／ 0.8 ／ 1.5 ／ 3 ／ 5 ／ 8（6段） | kHz | 1.5 | STEP | ○ |
| Mid Gain | eq06.mid.gain | −12〜+12、2 dB 刻み（13段） | dB | 0 | STEP | ○ |
| High kHz | eq06.high.freq | 2.5 ／ 5 ／ 7.5 ／ 10 ／ 12.5 ／ 15（6段） | kHz | 10 | STEP | ○ |
| High Gain | eq06.high.gain | −12〜+12、2 dB 刻み（13段） | dB | 0 | STEP | ○ |
| Shape | eq06.shape | Peak ／ Shelf（Low と High に適用） | — | Peak | STEP | ○ |
| Drive | eq06.drive | 0〜10（入力 0〜+18 dB） | — | 2 | LIN | ○ |
| Output | eq06.out | −10〜+10 | dB | 0.0 | LIN | ○ |

**DSP方式**

- 3バンドとも TPT SVF。比例Q：±2 dB で Q 0.4、±12 dB で Q 1.5（その間は直線補間、設計値）。
- Glide：段が変わったら、ゲインと Q を 30 ms かけて新しい値へ動かす。SVF はサンプルごとの係数変化に耐えるため、ジッパーノイズが出ない。
- Drive：出力段（EQ01 と同じモジュール）。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 軽。フィルタ3本＋非線形1段。

**進化機能：** Glide。段階式のノブを回しても、オートメーションで段が飛んでも、音が「プツッ」と切り替わらない。常時動作でスイッチは持たない。区分 A（初期）。

**共通機能との差分：** Unit A／B／C あり。ゲインが段階式なので、モーフ中のゲインは段の間を Glide で移る（共通章の「セレクターは 0.5 で切替」の例外）。

**【要確認】**

- 04 ではゲインが連続ノブだが、画面の目盛りは 22.5° 刻みの13段（2 dB 刻み）。本書は段階式とした。

## EQ07 Dynamic

6バンドのダイナミックEQ。各バンドが静的ゲインと、しきい値を超えたときだけ動く幅（Range）を持つ。STUDIO、デジタル筐体。低域のこもりや一瞬の刺さりを、必要なときだけ抑える。

下表のバンドパラメータは n = 1〜6。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Band n On | eq07.bn.on | Off ／ On | — | Off | STEP | ○ |
| Band n Type | eq07.bn.type | Bell ／ Shelf ／ Cut ／ Notch | — | Bell | STEP | ○ |
| Band n Freq | eq07.bn.freq | 20〜20,000 | Hz | 80・180・500・1.5k・4k・10k（1〜6） | LOG | ○ |
| Band n Gain | eq07.bn.gain | −24〜+24（静的） | dB | 0.0 | LIN | ○ |
| Band n Q | eq07.bn.q | 0.10〜20 | — | 1.0 | LOG | ○ |
| Band n Threshold | eq07.bn.thresh | −60〜0 | dB | −30 | LIN | ○ |
| Band n Range | eq07.bn.range | −24〜+24（負＝抑える、正＝持ち上げる） | dB | −6 | LIN | ○ |
| Band n Attack | eq07.bn.attack | 0.1〜100 | ms | 10 | SKW（k=3） | ○ |
| Band n Release | eq07.bn.release | 5〜2000 | ms | 120 | SKW（k=3） | ○ |
| Sidechain | eq07.sc | Internal ／ External | — | Internal | STEP | ○ |
| Spectral | eq07.spectral | Off ／ On | — | Off | STEP | ○ |
| Auto thresh | — | 学習ボタン | — | — | — | — |

**DSP方式**

- 各バンド：TPT SVF。検出側は同じ周波数・Q のバンドパスで切り出した信号（External 時は外部サイドチェーン入力）を、ピークと RMS の中間の検出器で測る。ゲイン計算は 6 dB のソフトニー。実効ゲイン＝Gain＋（Range × 動作量）。
- Spectral：バンドの範囲内を短時間 FFT（1024点、ホップ256）で細かく分け、突出した成分だけを動かす。
- 画面の表示：実線＝静的カーブ、破線＝動的な可動範囲（画面の凡例どおり）。

**遅延（@48 kHz）：** Spectral Off で 0、On で 1024 サンプル（設計上の見積もり）。Low lat を押すと Spectral が Off になる。

**CPU目安（設計上の見積もり）：** 中（6バンド・検出器6系統）。Spectral On で重。

**進化機能：** Auto thresh。押すと5秒間聴き、バンドごとに検出レベルの分布を取る。Range が負なら上位20 %（80パーセンタイル）、正なら下位20 % にしきい値を置く。区分 B（中期）。

**共通機能との差分：** Unit A／B／C なし（デジタル筐体）。Δ ボタンが画面に無い件は EQ02 と同じ。

**【要確認】**

- 画面に Output が無い。04 も7項目でOutput無し。Auto gain で足りる前提で、Output を持たない仕様にした。
- 80／20 パーセンタイルは案。

## EQ08 Linear

直線位相のマスタリング用EQ。5バンド、位相モード3種。STUDIO、デジタル筐体。2ミックスやステムの位相を崩さずに整える。

下表のバンドパラメータは n = 1〜5。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Band n On | eq08.bn.on | Off ／ On | — | Off | STEP | ○ |
| Band n Type | eq08.bn.type | Bell ／ Lo shelf ／ Hi shelf ／ Lo cut ／ Hi cut | — | Bell | STEP | ○ |
| Band n Freq | eq08.bn.freq | 10〜30,000（上限は fs の 0.49 倍） | Hz | 60・250・1k・2.4k・10k（1〜5） | LOG | ○ |
| Band n Gain | eq08.bn.gain | −18〜+18 | dB | 0.0 | LIN | ○ |
| Band n Q | eq08.bn.q | 0.10〜10 | — | 0.70 | LOG | ○ |
| Phase | eq08.phase | Linear ／ Minimum ／ Mixed | — | Linear | STEP | ○ |
| Pre-ring guard（EVO） | eq08.evo.on | Off ／ On | — | On | STEP | ○ |
| Mid/side | eq08.ms | Off ／ On | — | Off | STEP | ○ |
| Output | eq08.out | −18〜+18 | dB | 0.0 | LIN | ○ |
| Analyzer | — | 表示切替（UI専用） | — | Off | — | — |

**DSP方式**

- Linear：全バンドの合成振幅から FIR を作り、分割 FFT 畳み込みで処理する。カーネル長は時間で固定（21.3 ms 分＝48 kHz で 2048 タップ）。
- Minimum：TPT SVF の IIR。
- Mixed：200 Hz（案）より下を最小位相、上を直線位相にして合成する。
- カーネルの再計算は別スレッド。新旧カーネルは 20 ms でクロスフェードし、オートメーション中も音が切れない。

**遅延：** Linear・Mixed は 21.3 ms（48 kHz で 1024 サンプル、レートに比例）。画面の表示値と一致。Minimum は 0。Low lat を押すと Minimum に切り替わる。

**CPU目安（設計上の見積もり）：** Linear・Mixed は重（ステレオで長い FFT 畳み込み2系統）。Minimum は軽。

**進化機能：** Pre-ring guard。直線位相 FIR の主ピークより前に出る「前鳴り」のエネルギーを帯域ごとに見積もり、しきい値（主ピーク比 −60 dB、案）を超える帯域だけ位相を最小位相側に寄せたカーネルを設計し直す。急峻な低域カットで起きるにじみを抑える。音声の解析はせず、フィルタ設計の計算だけで完結するため区分 A（初期）。ただし設計の難度は高め。

**共通機能との差分：** Unit A／B／C なし。Δ ボタンが画面に無い件は EQ02 と同じ。

**【要確認】**

- 画面にバンドの種類（Bell・Shelf・Cut）を選ぶボタンが無い。本書は種類を持たせた。置き場所を決める。
- 2048 タップでは周波数分解能が約 23 Hz で、80 Hz 以下の狭いベルは形が甘くなる。低域精度を上げる「High」設定（85.3 ms ＝ 4096 サンプル@48 kHz）を追加するか決める。

## EQ09 Tilt

傾き（明るい／暗い）を1つのノブで変えるトーンシェイパー。低域と最高域の持ち上げを別に持つ。STUDIO、2Uラック（olive）。バスやマスターの全体の色合わせ。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Tilt | eq09.tilt | Dark〜Bright（−6〜+6） | dB | 0.0 | LIN | ○ |
| Pivot Hz | eq09.pivot | 300 ／ 600 ／ 1k ／ 2k ／ 4k（5段） | Hz | 1k | STEP | ○ |
| Low lift | eq09.lowlift | 0〜10（60 Hz シェルフ 0〜+6 dB） | — | 0 | LIN | ○ |
| Air | eq09.air | 0〜10（12 kHz シェルフ 0〜+6 dB） | — | 0 | LIN | ○ |
| Output | eq09.out | −10〜+10 | dB | 0.0 | LIN | ○ |
| Auto pivot（EVO） | eq09.evo.on | Off ／ On | — | Off | STEP | ○ |

**DSP方式**

- Tilt：Pivot を中心にした1次の低域シェルフと高域シェルフを逆向きに動かす。Tilt +6 で、最低域は −6 dB、最高域は +6 dB に近づく。
- Low lift・Air：2次のシェルフ（Q 0.7）。
- Auto pivot：Pivot を連続値で内部に持ち、セレクターの値は無視する。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 軽。フィルタ4本。Auto pivot の解析は 1/3 oct の帯域エネルギーだけなので軽い。

**進化機能：** Auto pivot。曲のスペクトル重心（10 秒窓）を追い、Pivot を 300 Hz〜4 kHz の範囲でゆっくり動かす（時定数 5 秒、案）。曲の明るさが変わっても、傾きの効き方が一定になる。区分 B（中期）。

**共通機能との差分：** Unit A／B／C あり。

**【要確認】**

- Auto pivot 中の画面表示（セレクターの指針をどう見せるか）が未定。

## CS01〜CS04 チャンネルストリップ

4本ともカテゴリは EQ（03）。プリアンプ・EQ・ダイナミクスを1枚にまとめた製品群。

### CS01 Inductor Strip

インダクター型のプリ＋EQ＋コンプ。STUDIO、2Uラック（bronze）。ボーカル・アコギの録り音仕上げ。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Drive | cs01.pre.drive | 0〜10（入力 0〜+18 dB） | — | 2 | LIN | ○ |
| HPF | cs01.hpf | Off ／ 50 ／ 80 ／ 160（4段） | Hz | Off | STEP | ○ |
| High | cs01.eq.high | −16〜+16（12 kHz シェルフ、固定） | dB | 0.0 | LIN | ○ |
| Mid kHz | cs01.eq.midfreq | 0.7 ／ 1.6 ／ 3.2 ／ 4.8（4段） | kHz | 1.6 | STEP | ○ |
| Mid | cs01.eq.mid | −18〜+18 | dB | 0.0 | LIN | ○ |
| Low | cs01.eq.low | −16〜+16（60 Hz シェルフ、固定） | dB | 0.0 | LIN | ○ |
| Thresh | cs01.comp.thresh | 0〜10（0〜−40 dBFS） | — | 0 | LIN | ○ |
| Ratio | cs01.comp.ratio | 2 ／ 4 ／ 8 ／ 20（4段） | :1 | 4 | STEP | ○ |
| Release | cs01.comp.release | Fast〜Slow（50〜1500 ms） | ms | 200 | SKW（k=2） | ○ |
| Order | cs01.order | EQ first ／ Comp first | — | EQ first | STEP | ○ |
| Mix | cs01.comp.mix | Dry〜Wet（コンプ部の並列） | % | 100 | LIN | ○ |
| Output | cs01.out | −10〜+10 | dB | 0.0 | LIN | ○ |
| Link | cs01.link | Off ／ On（ステレオ連動） | — | On | STEP | ○ |

- **DSP：** プリは鉄心風の飽和（低域ほど効く）、2× OS。EQ は EQ04 と同じ回路モデル。コンプはフィードバック型、アタックは信号に応じて 2〜20 ms で自動。
- **遅延：** 0 サンプル。**CPU目安（設計上の見積もり）：** 中。
- **進化機能：** Mic profile。マイクの種類（ダイナミック・コンデンサー・リボン）と音源（ボーカル・アコギ・キック等）を選ぶと、HPF・EQ・コンプの初期値を規則表で書き込む。実在機種名は使わない。区分 A（初期）。

### CS02 Console Strip

コンソール型のダイナミクス＋フィルター＋4バンドEQ＋フェーダー。STUDIO、2Uラック（anodized）。ドラムやバスのミックス。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Ratio | cs02.comp.ratio | 1〜20、右端 Max（∞） | :1 | 1（無効） | LOG | ○ |
| Thresh | cs02.comp.thresh | +10〜−20（0 dB ＝ −18 dBFS、案） | dB | +10 | LIN | ○ |
| Release | cs02.comp.release | 0.1〜4 | s | 0.3 | SKW（k=2） | ○ |
| Gate | cs02.gate.thresh | −30〜+10（同上の基準） | dB | −30 | LIN | ○ |
| Range | cs02.gate.range | 0〜40（0 で無効） | dB | 0 | LIN | ○ |
| HPF | cs02.hpf | Off ／ 40 ／ 80 ／ 160 ／ 350（5段） | Hz | Off | STEP | ○ |
| LPF | cs02.lpf | Off ／ 12k ／ 8k ／ 4k（4段） | Hz | Off | STEP | ○ |
| HF | cs02.eq.hf | −15〜+15（10 kHz シェルフ、固定） | dB | 0.0 | LIN | ○ |
| HMF | cs02.eq.hmf | −15〜+15（3 kHz ベル Q 1.0、固定） | dB | 0.0 | LIN | ○ |
| LMF | cs02.eq.lmf | −15〜+15（600 Hz ベル Q 1.0、固定） | dB | 0.0 | LIN | ○ |
| LF | cs02.eq.lf | −15〜+15（100 Hz シェルフ、固定） | dB | 0.0 | LIN | ○ |
| Route | cs02.route | Dyn to EQ ／ EQ to Dyn | — | Dyn to EQ | STEP | ○ |
| Fader | cs02.fader | Off（−∞）〜+10 | dB | 0.0 | フェーダー則（0 dB を 75 % 位置） | ○ |
| Link | cs02.link | Off ／ On | — | On | STEP | ○ |

- **DSP：** コンプは VCA 型フィードフォワード、アタック 3 ms 固定。ゲートはアタック 0.1 ms・ホールド 20 ms・リリース 100 ms 固定。HPF 18 dB/oct、LPF 12 dB/oct。GR history は表示のみ。
- **遅延：** 0 サンプル。**CPU目安（設計上の見積もり）：** 中。
- **進化機能：** ゲートの被り学習。学習中に拾った音の立ち上がりを、ピークレベルとスペクトル重心で2群に分け、「狙いの太鼓」と「被り」の間にしきい値を置き、狙いの帯域にキーフィルターを合わせる。区分 B（中期）。

### CS03 Stepped Strip

段階式EQのプリ＋EQ＋コンプ。STUDIO、2Uラック（brushed）。ベースDI・ドラムの太い録り音。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Gain | cs03.pre.gain | 目盛り 0〜+60（内部 −30〜+30 dB、目盛り 30 で 0 dB、案） | dB | 30 | LIN | ○ |
| Impedance | cs03.pre.z | Lo Z ／ Hi Z | — | Lo Z | STEP | ○ |
| Low | cs03.eq.low | −12〜+12、2 dB 刻み（100 Hz シェルフ、固定） | dB | 0 | STEP | ○ |
| Mid | cs03.eq.mid | −12〜+12、2 dB 刻み（1.5 kHz、比例Q、固定） | dB | 0 | STEP | ○ |
| High | cs03.eq.high | −12〜+12、2 dB 刻み（10 kHz シェルフ、固定） | dB | 0 | STEP | ○ |
| Thresh | cs03.comp.thresh | 0〜10（0〜−40 dBFS） | — | 0 | LIN | ○ |
| Ratio | cs03.comp.ratio | 1.2〜10 | :1 | 2 | LOG | ○ |
| Knee | cs03.comp.knee | Hard ／ Soft | — | Soft | STEP | ○ |
| Output | cs03.out | −10〜+10 | dB | 0.0 | LIN | ○ |

- **DSP：** プリはトランス風の飽和、Hi Z は高域の負荷と飽和の出方を楽器入力向けに変える。EQ は EQ06 と同じ比例Q＋Glide。コンプはアタック・リリース自動。
- **遅延：** 0 サンプル。**CPU目安（設計上の見積もり）：** 中。
- **進化機能：** 入力レベル合わせ。5秒聴いて、プリ通過後が平均 −18 dBFS RMS・ピーク −6 dBFS 以下になるよう Gain を書き込む。音源の種類ごとに目標を変える。区分 A（初期）。

### CS04 Modular Strip

モジュールを並べ替えて組むデジタルのストリップ。STUDIO、デジタル筐体。ポッドキャストや配信前の声の整音。

モジュールは Gate・EQ・Comp・Saturate・De-ess・Limit の6種、各1個まで（案）。並び順はドラッグで変え、プリセットに保存する（オートメーション不可）。画面にあるのは EQ モジュールだけなので、他のモジュールの項目は案。

| モジュール | パラメータ（ID は `cs04.モジュール.名前`） |
| --- | --- |
| EQ（画面あり） | Low −12〜+12 dB（100 Hz シェルフ）、Mid freq 200 Hz〜8 kHz（LOG、既定 2.5 kHz）、Mid −12〜+12 dB（Q 1.0）、High −12〜+12 dB（10 kHz シェルフ）、Output −12〜+12 dB |
| Gate | Threshold −80〜0 dB、Range 0〜−80 dB、Release 5〜2000 ms |
| Comp | Threshold −60〜0 dB、Ratio 1〜20、Attack 0.1〜100 ms、Release 5〜2000 ms、Makeup 0〜+24 dB |
| Saturate | Drive 0〜+24 dB、Mix 0〜100 % |
| De-ess | Freq 2〜12 kHz、Threshold −60〜0 dB、Range 0〜−20 dB |
| Limit | Ceiling −12〜0 dBFS、Release 1〜1000 ms |

各モジュールに On（○）を持つ。

- **遅延：** Limit が On のとき 48 サンプル（1 ms の先読み、@48 kHz）。それ以外は 0。Low lat で先読みを 0 にする。
- **CPU目安（設計上の見積もり）：** 中（全モジュール On 時）。
- **進化機能：** 並び順の提案。初期版は音源の種類（声・ボーカル・ドラムバス等）を選ぶと規則表で順番を出す（区分 A）。後期版で音源を自動判別する（区分 C）。

## EQカテゴリ一覧

13本中、遅延が出るのはデジタル4本の特定モードだけ。EVO は A（初期）8本、B（中期）5本。遅延の一部とCPUは設計上の見積もり。

| コード | 名前 | 筐体 | 遅延（サンプル@48 kHz） | CPU目安 | EVO区分 | EVOの操作 |
| --- | --- | --- | --- | --- | --- | --- |
| EQ01 | Passive 進化版 | 2Uラック | 0 | 軽 | A | 常時（Contour ノブ） |
| EQ02 | Surgical 進化版 | デジタル | 0 ／ 256 ／ 1024（Zero ／ Natural ／ Linear） | 軽〜中、Linear 重 | B | Assist・Unmask ボタン |
| EQ03 | Mid Shaper | 2Uラック | 0 | 軽 | A | スイッチ（既定 Off） |
| EQ04 | Inductor | 2Uラック | 0 | 中 | A | スイッチ（既定 On） |
| EQ05 | Console | 2Uラック | 0 | 軽 | B | Match ボタン |
| EQ06 | Stepped | 2Uラック | 0 | 軽 | A | 常時 |
| EQ07 | Dynamic | デジタル | 0 ／ 1024（Spectral） | 中、Spectral 重 | B | Auto thresh ボタン |
| EQ08 | Linear | デジタル | 1024（Linear・Mixed）／ 0（Minimum） | 重、Minimum 軽 | A | スイッチ（既定 On） |
| EQ09 | Tilt | 2Uラック | 0 | 軽 | B | スイッチ（既定 Off） |
| CS01 | Inductor Strip | 2Uラック | 0 | 中 | A | Profile 選択 |
| CS02 | Console Strip | 2Uラック | 0 | 中 | B | 学習ボタン |
| CS03 | Stepped Strip | 2Uラック | 0 | 中 | A | 学習ボタン |
| CS04 | Modular Strip | デジタル | 0 ／ 48（Limit On） | 中 | A（後期 C） | 提案表示 |

## DY・MS 共通事項

DY（12本）と MS（7本）の19本は、共通章に加えて次の決まりに従う。すべて案。

- **既定値：** しきい値系は、普通の素材でほとんど圧縮がかからない位置にする。音作りの出発点はプリセットで出す。
- **SC HPF：** 検出側の2次ハイパス（12 dB/oct）。Off＋20〜300 Hz（LOG）、既定 Off。
- **ステレオ：** 既定はリンク（左右の大きい方で検出）。Link／Dual 切替がある製品だけ変えられる。
- **外部サイドチェーン：** 画面に切替がある製品（DY04・DY08）だけ持つ。VST3 の補助入力バス、AU のサイドチェーン、CLAP の補助ポートで受ける。
- **dB 目盛りの基準：** アナログ筐体の dB 目盛り（DY07 など）は CS02 と同じ「0 dB ＝ −18 dBFS」。確認事項の未決項目に従う。
- **ラウドネス計測：** ITU-R BS.1770 準拠（K特性、ゲート付き Integrated、3秒 Short-term、True peak は 4× 以上のオーバーサンプリング）。
- **GR・ラウドネスのメーター：** 表示専用。オートメーション不可。

## DY01 FET 進化版

FET型のキャラクターコンプ。入力を固定しきい値に押し込む方式。STUDIO、2Uラック（brushed）。ボーカル・ドラムに速く強くかける用途。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Drive | dy01.drive | 0〜10（入力 0〜+36 dB、固定しきい値 −6 dBFS へ） | — | 0 | LIN | ○ |
| Ratio | dy01.ratio | 2〜20、右端5 % は Max | :1 | 4 | LOG | ○ |
| Speed | dy01.speed | Slow〜Fast（アタック 800〜20 µs、リリース 1100〜50 ms を連動） | — | 0.5（約130 µs／240 ms） | LOG | ○ |
| Bite | dy01.bite | Soft〜Hard（0〜100 %） | % | 0 | LIN | ○ |
| Color | dy01.color | Clean ／ Grit ／ Crush | — | Grit | STEP | ○ |
| Output | dy01.out | −12〜+24 | dB | 0.0 | LIN | ○ |
| Mix | dy01.mix | Dry〜Wet | % | 100 | LIN | ○ |
| SC HPF | dy01.schpf | Off＋20〜300 | Hz | Off | LOG | ○ |

**DSP方式**

- フィードバック型の検出。Ratio の Max は、ニーが急に硬くなり、深くかかるほど歪みが増える特別な動作にする。
- Bite：速い包絡と遅い包絡の差から音の立ち上がりを見つけ、立ち上がり後 5〜15 ms だけゲインリダクションを緩める。Hard ほど緩める量が大きい。
- Color：増幅段の歪み方を3種で切り替える。Clean は偶数次が少し、Grit は奇数次と偶数次が混ざりレベルで変わる、Crush はゲインリダクションが深いほど強く歪む。2× OS（Crush は 4× 推奨）。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 中。検出器1系統＋非線形2段を 2× OS。

**進化機能：** Bite と Color。速くかけても頭のアタックが残り、歪みの量と質を圧縮量とは別に決められる。区分 A（初期）。

**共通機能との差分：** Unit A／B／C あり。

**【要確認】**

- 04 に Color が無い。画面（DY01\_v2）では3段のレバー。本書は追加した。
- キャンバスに DY01 が3枚（DY01・DY01B・DY01\_v2）。04 と一致する DY01\_v2 を正とした。

## DY02 Opto 進化版

光学式のレベラー。かかりが柔らかく、戻りが2段階。前段に、狙った音量へ寄せる自動フェーダー（Ride）を持つ。STUDIO、2Uラック（olive、アイボリーノブ）。ボーカル・ベース。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Level | dy02.level | 0〜10（しきい値 0〜−40 dBFS） | — | 0 | LIN | ○ |
| Output | dy02.out | −12〜+24 | dB | 0.0 | LIN | ○ |
| Speed | dy02.speed | Fast ／ Prog ／ Slow（3段） | — | Prog | STEP | ○ |
| Target | dy02.target | −30〜−6（短時間ラウドネス） | LUFS | −18 | LIN | ○ |
| Emphasis | dy02.emph | Flat〜Max（検出側の高域持ち上げ 0〜+12 dB） | dB | 0 | LIN | ○ |
| Mix | dy02.mix | Dry〜Wet | % | 100 | LIN | ○ |
| Ride（EVO） | dy02.evo.on | Off ／ On | — | Off | STEP | ○ |
| Auto makeup | dy02.automakeup | Off ／ On | — | Off | STEP | ○ |

**DSP方式**

- 光学セルのモデル：アタック約 10 ms。リリースは速い段（GR の半分まで）と遅い段の2段で、遅い段は直前の圧縮の深さと長さで伸びる。Speed の各段の時間（速い段／遅い段）：Fast 40 ms／0.5 s、Prog はモデルどおり、Slow 200 ms／3 s。
- Emphasis：検出側だけに 2 kHz 以上の高域シェルフをかけ、明るい音で早めにかかるようにする。
- Ride：コンプの前に置く自動ゲイン。400 ms 窓の短時間ラウドネスを Target に寄せる。補正幅 ±12 dB、追従 1〜2 秒。−50 dBFS 以下（息・無音）では動かない。
- Auto makeup：平均のゲインリダクションを補う。共通の Auto gain（比較用）とは別に、出力の音量そのものを変える。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 軽。検出器2系統（Ride とコンプ）。

**進化機能：** Ride。人がフェーダーを上げ下げするように、歌の大小を先に揃えてからコンプに渡す。区分 B（中期）。

**共通機能との差分：** Unit A／B／C あり。

**【要確認】**

- 画面のトグル「Auto｜Makeup」は Auto makeup の On/Off と解釈した。
- Auto makeup と共通の Auto gain の役割の違いを、画面上でどう説明するか。
- キャンバスに DY02 が2枚。04 と一致する DY02\_v2 を正とした（DY03・MS01 も同様に \_v2 を正）。

## DY03 Bus 進化版

VCA型のバスコンプ。ミックス全体やドラムバスを「まとめる」用途。STUDIO、2Uラック（anodized）。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Threshold | dy03.thresh | −30〜0 | dB | 0.0 | LIN | ○ |
| Ratio | dy03.ratio | 1.5 ／ 2 ／ 4 ／ 10（4段） | :1 | 2 | STEP | ○ |
| Attack | dy03.attack | 0.1 ／ 0.3 ／ 1 ／ 3 ／ 10 ／ 30 ／ 100（7段） | ms | 10 | STEP | ○ |
| Release | dy03.release | 50 ／ 100 ／ 200 ／ 400 ／ 800 ／ Auto（6段） | ms | Auto | STEP | ○ |
| Makeup | dy03.makeup | 0〜+20 | dB | 0.0 | LIN | ○ |
| Mix | dy03.mix | Dry〜Wet | % | 100 | LIN | ○ |
| Knee | dy03.knee | Hard〜Soft（ニー幅 0〜12 dB） | dB | 0 | LIN | ○ |
| SC HPF | dy03.schpf | Off＋20〜300 | Hz | Off | LOG | ○ |
| Punch keep（EVO） | dy03.evo.on | Off ／ On | — | Off | STEP | ○ |

**DSP方式**

- フィードフォワードの VCA 型。検出はピークと RMS の中間。Release Auto は2段（約 100 ms と 1.2 s）で、短い山には速く、長い圧縮には遅く戻る。
- Punch keep：DY01 の Bite と同じ立ち上がり検出を使い、キックやスネアの頭 5〜30 ms だけゲインリダクションを緩める。バス用に、検出は低域（150 Hz 以下）と中域（1〜5 kHz）の2帯域の大きい方で行う。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 軽。検出器1系統＋立ち上がり検出2帯域。

**進化機能：** Punch keep。深めにまとめても、打楽器の頭が潰れない。区分 A（初期）。

**共通機能との差分：** Unit A／B／C あり。

**【要確認】**

- 04 では Ratio・Attack・Release が「セレクター」で両端の値だけ（1.5／10、.1／100、50／Auto）。段数が無いので、本書の段（4・7・6段）は仮決め。
- 旧ボード DY03 にあった「Auto fade」は \_v2 で消えている。不要と判断した。

## DY04 Gate

ゲート／エキスパンダー／ダッカー。500シリーズ縦型の唯一の製品。STUDIO。ドラムの被り除去、ナレーションの間の処理。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Threshold | dy04.thresh | −80〜0 | dBFS | −80（開きっぱなし） | LIN | ○ |
| Range | dy04.range | 0〜−80 | dB | −40 | LIN | ○ |
| Attack | dy04.attack | 0.01〜25 | ms | 0.1 | LOG | ○ |
| Hold | dy04.hold | 0〜2000 | ms | 50 | SKW（k=3） | ○ |
| Release | dy04.release | 5〜4000 | ms | 100 | LOG | ○ |
| Mode | dy04.mode | Gate ／ Expand ／ Duck | — | Gate | STEP | ○ |
| Key HPF | dy04.key.hpf | Off ／ On（周波数は下の内部値） | — | Off | STEP | ○ |
| Key LPF | dy04.key.lpf | Off ／ On | — | Off | STEP | ○ |
| Key HPF Freq | dy04.key.hpffreq | 20〜2000（画面にノブなし、学習で設定） | Hz | 100 | LOG | ○ |
| Key LPF Freq | dy04.key.lpffreq | 1〜20（同上） | kHz | 8 | LOG | ○ |
| Listen | dy04.listen | キーの試聴（監視用） | — | Off | STEP | — |

**DSP方式**

- 検出はピーク。開く値と閉じる値に 4 dB の差（ヒステリシス）を持たせ、しきい値付近でバタつかないようにする。
- Expand はしきい値以下を 1:2 で下げる（Range が下限）。Duck はキー信号がしきい値を超えると Range だけ下げる（外部サイドチェーン推奨）。
- キーフィルターは2次。外部サイドチェーン入力を持つ。

**遅延：** 0 サンプル（先読みなし）。

**CPU目安（設計上の見積もり）：** 軽。

**進化機能：** 被りと本打ちの学習。CS02 と同じ処理を共有する。学習中の立ち上がりをピークレベルとスペクトル重心で2群に分け、間にしきい値を置き、狙いの帯域に Key HPF／LPF を合わせる。区分 B（中期）。

**共通機能との差分：** 画面に Δ・Unit・Low lat が無い（縦長で EVO バーに SW Link しか入っていない）。

**【要確認】**

- 500シリーズはアナログ筐体なので、共通章どおりなら Unit A／B／C を持つ。画面に置き場が無い。持たせるか決める。
- Δ の置き場（ツールバーの AG の隣が候補）。
- キーフィルターの周波数ノブが画面に無い。本書は学習で決まる内部値にした。

## DY05 De-ess

ディエッサー。サ行の刺さりだけを下げる。歌の高い母音は下げない。STUDIO、デジタル筐体。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Mode | dy05.mode | Wide ／ Split | — | Split | STEP | ○ |
| Freq | dy05.freq | 2〜16 | kHz | 6.5 | LOG | ○ |
| Threshold | dy05.thresh | −60〜0 | dB | −24 | LIN | ○ |
| Range | dy05.range | 0〜−24 | dB | −8 | LIN | ○ |
| Lookahead | dy05.lookahead | 0〜5 | ms | 2 | LIN | ○ |
| Listen | dy05.listen | 検出帯域の試聴（監視用） | — | Off | STEP | — |
| Pitch follow（EVO） | dy05.evo.on | Off ／ On | — | On | STEP | ○ |

**DSP方式**

- 検出：Freq から上の帯域のエネルギーと全帯域のエネルギーの比で判定する（音量に左右されにくい）。
- Wide：全帯域を下げる。Split：Freq から上だけを下げる（帯域分割は加算して平らになる構成）。
- 先読み：Lookahead 分だけ本線を遅らせ、サ行の頭から下げる。
- Pitch follow：歌の基本周波数を追い（自己相関系の検出）、有声音の区間（ピッチが取れている区間）では判定を鈍くし、無声音の区間でだけ強く効かせる。歌い手の声域で Freq の初期位置も寄せる。

**遅延：** Lookahead 分（既定 2 ms ＝ 96 サンプル@48 kHz、最大 240）。Low lat を押すと Lookahead が 0 になる。

**CPU目安（設計上の見積もり）：** 中。帯域分割＋ピッチ検出。

**進化機能：** Pitch follow。高い声で明るく歌った母音を、サ行と間違えて下げない。区分 B（中期）。

**共通機能との差分：** Unit A／B／C なし。

**【要確認】**

- 03 の説明欄が「Multiband dynamics」になっている。中身はディエッサーなので「De-esser」への修正を推奨。
- 画面に Δ ボタンが無い（Listen はある）。

## DY06 Vari-Mu

真空管のバリミュー型コンプ。深くかけるほど比率が上がる柔らかいかかり方。STUDIO、2Uラック（graphite）。ミックスバス・マスターの温かさ付け。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Input | dy06.input | 0〜10（入力 −10〜+20 dB、目盛り 3.3 で 0 dB） | — | 3.3 | LIN | ○ |
| Threshold | dy06.thresh | 0〜10（0〜−30 dBFS） | — | 0 | LIN | ○ |
| Time | dy06.time | 1〜6（6段、下表） | — | 3 | STEP | ○ |
| Mu | dy06.mu | Soft〜Hard（比率の立ち上がり方） | — | 0.5 | LIN | ○ |
| Mix | dy06.mix | Dry〜Wet | % | 100 | LIN | ○ |
| Stereo | dy06.stereo | Link ／ Dual | — | Link | STEP | ○ |
| Density adapt（EVO） | dy06.evo.on | Off ／ On | — | On | STEP | ○ |

| Time | 1 | 2 | 3 | 4 | 5 | 6 |
| --- | --- | --- | --- | --- | --- | --- |
| アタック（ms） | 2 | 2 | 4 | 8 | 4 | 2 |
| リリース（s） | 0.3 | 0.8 | 1.5 | 3 | Auto 0.5〜5 | Auto 0.3〜10 |

**DSP方式**

- ゲイン要素：GR が深いほど比率が上がる曲線（約 1.5:1 から 6:1）。Mu で上がり方の速さを変える。
- 真空管段：入力段に偶数次寄りの飽和、2× OS。
- Density adapt：直近2秒のクレストファクター（ピークと RMS の差）と立ち上がりの数から「詰まり具合」を出す。詰まった音ほどリリースを長く（最大 2 倍）、まばらな音ほど短く（最小 0.5 倍）する。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 中。検出器＋真空管段を 2× OS。

**進化機能：** Density adapt。曲の密度が変わっても、戻りの速さが自然に追従する。区分 B（中期）。

**共通機能との差分：** Unit A／B／C あり。

**【要確認】**

- 画面と 04 に Output が無い。音量は Input と共通の Auto gain で合わせる前提。Output を足すなら画面も直す。
- Time 各段の時間は仮決め。

## DY07 Snap

VCA型のパンチ系コンプ。アタック・リリースは信号に応じて自動で、代わりに Snap で頭の鋭さを決める。STUDIO、2Uラック（anodized）。キック・スネア・ベース。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Threshold | dy07.thresh | −40〜+20（0 dB ＝ −18 dBFS、案） | dB | +20 | LIN | ○ |
| Compress | dy07.ratio | 1〜20、右端5 % は ∞ | :1 | 4 | LOG | ○ |
| Output | dy07.out | −20〜+20 | dB | 0.0 | LIN | ○ |
| Snap | dy07.snap | Soft〜Hard（頭の強弱 −6〜+6 dB） | dB | 0 | LIN | ○ |
| Mix | dy07.mix | Dry〜Wet | % | 100 | LIN | ○ |
| Knee | dy07.knee | Soft knee ／ Hard knee | — | Soft knee | STEP | ○ |

**DSP方式**

- フィードフォワードの VCA 型。検出は真の RMS（窓は信号に応じて 5〜15 ms）。アタック・リリースは信号の変化速度に合わせて自動で決まる。
- Below／Above の2灯は、しきい値より下か上かの表示（表示専用）。
- Snap：コンプの後ろに置く立ち上がり整形。速い包絡と遅い包絡の差で頭を検出し、Soft 側で丸め、Hard 側で強調する。比率を変えても頭の鋭さが変わらない。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 軽。

**進化機能：** Snap。圧縮の深さと頭の鋭さを別々に決められる。区分 A（初期）。

**共通機能との差分：** Unit A／B／C あり。

**【要確認】**

- Threshold の dB 基準（0 dB ＝ −18 dBFS）は確認事項の未決項目に従う。

## DY08 Clean

色付けのない透明なデジタルコンプ。全パラメータを数値で触れる。STUDIO、デジタル筐体。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Threshold | dy08.thresh | −60〜0 | dB | 0.0 | LIN | ○ |
| Ratio | dy08.ratio | 1〜20、右端5 % は ∞ | :1 | 2.0 | LOG | ○ |
| Knee | dy08.knee | 0〜24 | dB | 6 | LIN | ○ |
| Attack | dy08.attack | 0.05〜200 | ms | 10 | SKW（k=3） | ○ |
| Release | dy08.release | 5〜3000 | ms | 150 | SKW（k=3） | ○ |
| Auto release（EVO） | dy08.evo.on | Off ／ On（On の間 Release は「Auto」表示） | — | Off | STEP | ○ |
| Makeup | dy08.makeup | −12〜+24 | dB | 0.0 | LIN | ○ |
| Mix | dy08.mix | 0〜100 | % | 100 | LIN | ○ |
| SC HPF | dy08.schpf | Off＋20〜300 | Hz | Off | LOG | ○ |
| Detector | dy08.detector | Peak ／ RMS ／ Program | — | Program | STEP | ○ |
| Lookahead | dy08.lookahead | Off ／ On（5 ms） | — | Off | STEP | ○ |
| Sidechain | dy08.sc | Internal ／ External | — | Internal | STEP | ○ |

**DSP方式**

- フィードフォワード、ゲイン計算は dB 領域。Program 検出はピークと RMS を信号の性質で混ぜる。
- Auto release：ホストのテンポを使い、リリースを拍の長さ（1/16〜1/4 拍）に合わせる。テンポが無いときは立ち上がりの間隔の分布からテンポを推定する。次の拍までに GR が戻る長さを選ぶ。

**遅延：** 0 サンプル。Lookahead On で 240 サンプル（5 ms@48 kHz）。Low lat を押すと Lookahead が Off になる。

**CPU目安（設計上の見積もり）：** 軽。

**進化機能：** Auto release。曲のノリに合わせて戻るので、ポンピングが拍とずれない。区分 B（中期）。

**共通機能との差分：** Unit A／B／C なし。

**【要確認】**

- 画面に Δ ボタンが無い。

## DY09 Transient

トランジェントシェイパー。音量に関係なく、頭（Attack）と余韻（Sustain）を足し引きする。STUDIO、デジタル筐体。ドラムの締まりと鳴りの調整。

Split bands では下表の Attack・Sustain を帯域ごとに持つ（n = Low／Mid／High）。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Attack | dy09.attack ／ dy09.bn.attack | −15〜+15 | dB | 0 | LIN | ○ |
| Sustain | dy09.sustain ／ dy09.bn.sustain | −15〜+15 | dB | 0 | LIN | ○ |
| Speed | dy09.speed | Fast ／ Medium ／ Slow | — | Medium | STEP | ○ |
| Clip | dy09.clip | Off ／ Soft ／ Hard（出力段 0 dBFS） | — | Off | STEP | ○ |
| Mix | dy09.mix | 0〜100 | % | 100 | LIN | ○ |
| Mode（EVO） | dy09.mode | Smooth ／ Split bands | — | Smooth | STEP | ○ |

**DSP方式**

- 検出：速い包絡と遅い包絡の比（dB）で頭を、遅い包絡と更に遅い包絡の比で余韻を判定する。比で見るため入力の大小に左右されない。Speed で3つの包絡の時定数を一括で変える。
- Split bands：150 Hz と 4 kHz（案）で3帯域に分け、帯域ごとに整形して足し戻す。分割は加算して平らになる構成。
- Clip：2× OS のソフト／ハードクリップ。

**遅延：** 0 サンプル（先読みなし）。

**CPU目安（設計上の見積もり）：** 軽（Smooth）、中（Split bands）。

**進化機能：** Split bands。キックの頭とシンバルの余韻を別々に整形できる。区分 A（初期）。

**共通機能との差分：** Unit A／B／C なし。Δ は画面にある（「Δ Delta」ボタン）。

**【要確認】**

- Split bands 時に帯域を選ぶ UI が画面に無い。帯域選択ボタン（Low／Mid／High）を足すか決める。
- 分割周波数 150 Hz・4 kHz は固定の案。可変にするか。

## DY10 Multiband 4

4帯域のマルチバンドコンプ。ミックスやバスの帯域ごとの暴れを抑える。STUDIO、デジタル筐体。

下表のバンドパラメータは n = 1〜4（Low／Low mid／High mid／High）。クロスオーバーは x = 1〜3。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Band n Threshold | dy10.bn.thresh | −60〜0 | dB | 0.0 | LIN | ○ |
| Band n Ratio | dy10.bn.ratio | 1〜20 | :1 | 2.0 | LOG | ○ |
| Band n Attack | dy10.bn.attack | 0.1〜200 | ms | 15 | SKW（k=3） | ○ |
| Band n Release | dy10.bn.release | 5〜3000 | ms | 150 | SKW（k=3） | ○ |
| Band n Range | dy10.bn.range | 0〜−24（GR の上限） | dB | −12 | LIN | ○ |
| Band n Gain | dy10.bn.gain | −12〜+12 | dB | 0.0 | LIN | ○ |
| Band n Solo | dy10.bn.solo | Off ／ On（監視用） | — | Off | STEP | — |
| Band n Bypass | dy10.bn.bypass | Off ／ On | — | Off | STEP | ○ |
| Crossover x | dy10.xx.freq | 20〜20,000 | Hz | 240 ／ 2k ／ 8k（1〜3） | LOG | ○ |
| Output | dy10.out | −24〜+24 | dB | 0.0 | LIN | ○ |
| Auto | — | クロスオーバー解析ボタン | — | — | — | — |

**DSP方式**

- 帯域分割は4次の Linkwitz-Riley（加算すると振幅が平ら）。各帯域はフィードフォワードの RMS 検出コンプ。
- クロスオーバー同士は1オクターブ以上離す（近づけると押し返す）。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 中。分割フィルタ＋検出器4系統。

**進化機能：** Auto。10秒以上の再生から長時間平均スペクトルを取り、耳の感度で重み付けしたエネルギーがほぼ4等分になり、かつスペクトルの谷に近い位置にクロスオーバーを置く。結果は Crossover に書き込み、Undo できる。区分 B（中期）。

**共通機能との差分：** Unit A／B／C なし。

**【要確認】**

- 画面に Δ ボタンが無い。
- 直線位相の分割（遅延あり）を選べるようにするか。本書は最小位相のみ。

## DY11 Multiband 6

6帯域のダイナミクス。帯域ごとにコンプ／エキスパンダー／ダイナミックEQを切り替える。STUDIO、デジタル筐体。マスターのバランス調整。

下表のバンドパラメータは n = 1〜6。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Band n Mode | dy11.bn.mode | Compress ／ Expand ／ Dynamic EQ | — | Compress | STEP | ○ |
| Band n Freq | dy11.bn.freq | 20〜20,000（中心周波数、画面のグラフで操作） | Hz | 60・200・600・2k・6k・14k（1〜6） | LOG | ○ |
| Band n Threshold | dy11.bn.thresh | −60〜0 | dB | 0.0 | LIN | ○ |
| Band n Ratio | dy11.bn.ratio | 1〜20 | :1 | 2.0 | LOG | ○ |
| Band n Attack | dy11.bn.attack | 0.1〜200 | ms | 20 | SKW（k=3） | ○ |
| Band n Release | dy11.bn.release | 5〜3000 | ms | 100 | SKW（k=3） | ○ |
| Band n Range | dy11.bn.range | 0〜−24 | dB | −12 | LIN | ○ |
| Band n Gain | dy11.bn.gain | −12〜+12 | dB | 0.0 | LIN | ○ |
| Band n Width | dy11.bn.width | 0.1〜4（Dynamic EQ 時） | oct | 1.0 | LOG | ○ |
| Output | dy11.out | −24〜+24 | dB | 0.0 | LIN | ○ |

**DSP方式**

- 帯域分割フィルタは使わず、6本の「動く」フィルタを直列に置く構成。Compress・Expand では隣の帯域との中点までを覆う広いベル（両端はシェルフ）、Dynamic EQ では Width のベルになる。
- この構成なら帯域を足し戻す必要がなく、遅延0のまま帯域ごとにモードを混在できる。
- Expand はしきい値以下を Ratio で下げる（Range が下限）。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 中。動的フィルタ＋検出器6系統。

**進化機能：** 帯域ごとのモード切替。広い帯域を潰すか、狭い山だけ削るかを帯域単位で選べる。区分 A（初期）。

**共通機能との差分：** Unit A／B／C なし。

**【要確認】**

- 画面に帯域の周波数を表す数値が無い（Width のみ）。本書は Freq を持たせ、グラフ上で操作する前提にした。
- 画面に Δ ボタンが無い。

## DY12 Parallel

パラレル圧縮とアップワード圧縮を1台にしたもの。強く潰した音を混ぜる従来の方法に加え、小さい音を持ち上げる方法を持つ。STUDIO、2Uラック（olive）。ドラム・ボーカル。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Squash | dy12.squash | 0〜10（潰す側のしきい値 0〜−40 dBFS、比率 10:1 固定） | — | 5 | LIN | ○ |
| Blend | dy12.blend | Dry〜Wet（潰した音の混ぜ量） | % | 30 | LIN | ○ |
| Upward | dy12.upward | 0〜10（小さい音の持ち上げ 0〜+12 dB） | — | 0 | LIN | ○ |
| Tone | dy12.tone | Dark〜Bright（潰した側だけの傾き −6〜+6 dB） | dB | 0 | LIN | ○ |
| Speed | dy12.speed | Fast ／ Med ／ Slow ／ Auto（4段） | — | Auto | STEP | ○ |
| Output | dy12.out | −10〜+10 | dB | 0.0 | LIN | ○ |

| Speed | Fast | Med | Slow | Auto |
| --- | --- | --- | --- | --- |
| アタック／リリース | 1 ms／50 ms | 5 ms／150 ms | 20 ms／400 ms | 信号に応じて可変 |

**DSP方式**

- 潰す側：フィードフォワードの強圧縮。潰した分の音量は自動で補い、Blend で原音に足す。Tone は潰した側だけにかける1次の傾き。
- Upward：原音側に置くアップワード圧縮。−40 dBFS 付近より下の小さい部分を最大 +12 dB 持ち上げる。−60 dBFS 以下は持ち上げない（ノイズを上げないため）。
- 共通の Mix は持たない（Blend が役割を兼ねる）。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 軽。検出器2系統。

**進化機能：** Upward。大きい音を潰さずに、埋もれた細部だけを持ち上げられる。区分 A（初期）。

**共通機能との差分：** Unit A／B／C あり。Mix なし。

**【要確認】**

- 既定値（Squash 5・Blend 30 %）は「挿してすぐ効果が分かる」位置にした。DY・MS 共通事項の「既定はかからない位置」の例外。

## MS01 Maximizer 進化版

マスタリング用のマキシマイザー。ラウドネス目標を決めると、そこに届く入力ゲインを自動で求める（Lock）。STUDIO、デジタル筐体。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Gain | ms01.gain | 0〜+24（Lock 中は自動で書き込まれる） | dB | 0.0 | LIN | ○ |
| Target | ms01.target | −30〜−5（0.1 刻み） | LUFS | −14.0 | LIN | ○ |
| Lock（EVO） | ms01.evo.on | Off ／ On | — | Off | STEP | ○ |
| Target preset | — | Stream −14 ／ Podcast −16 ／ Broadcast −24 ／ Club −8（Target に値を書くボタン） | — | — | — | — |
| Character X | ms01.char.x | Clean〜Dense（0〜100） | — | 50 | LIN | ○ |
| Character Y | ms01.char.y | Smooth〜Punch（0〜100） | — | 50 | LIN | ○ |
| Ceiling | ms01.ceiling | −12〜0 | dBTP | −1.0 | LIN | ○ |
| Release | ms01.release | 1〜1000、右端 Auto | ms | Auto | SKW（k=3） | ○ |
| Stereo | ms01.stereo | 0〜100（左右リンク量） | % | 100 | LIN | ○ |
| True peak | ms01.tp | Off ／ On | — | On | STEP | ○ |
| Dither | ms01.dither | Off ／ 16 bit ／ 24 bit | — | Off | STEP | ○ |
| Low end guard | ms01.lowguard | Off ／ On | — | Off | STEP | ○ |

**DSP方式**

- 先読み付きの2段リミッター。速い段がピークを止め、遅い段が全体の密度を作る。先読み 2 ms。
- Character：X は遅い段の効き量とニーの丸さ（Dense ほど詰まる）、Y は立ち上がりの通し方（Punch ほど頭を残す）。
- True peak：4× OS で標本間のピークまで含めて Ceiling を守る。
- Low end guard：120 Hz 以下を検出側で弱め、低域がリミッターを揺らして全体がうねるのを防ぐ。
- Lock：Integrated ラウドネスを測りながら Gain を Target へ寄せ（時定数 10 秒）、30 秒以上測って値が安定したら Gain を固定する。固定値はパラメータとして保存されるので、書き出し（オフラインレンダー）では毎回同じ結果になる。

**遅延：** 約 110 サンプル（先読み 96＋True peak の補間フィルタ、@48 kHz、設計上の見積もり）。Low lat を押すと先読み 0.5 ms・IIR 補間になり約 24 サンプル。

**CPU目安（設計上の見積もり）：** 中。4× OS の検出＋ラウドネス計測。

**進化機能：** Lock。「−14 LUFS にしたい」と決めるだけで、入力ゲインの追い込みが要らない。区分 B（中期）。

**共通機能との差分：** Unit A／B／C なし。Δ は画面にある。

**【要確認】**

- 04 は Ceiling・Release・Stereo の3項目だけ。Target・Lock・Character・Gain が無い。本書は追加した。
- 画面に手動の Gain（入力の押し込み量）が無い。Lock Off のときに操作する Gain を画面に足す必要がある。
- Broadcast −24 は日本の放送基準（ARIB TR-B32）の目標値に合わせた想定。

## MS02 True Peak

標本間のピーク（インターサンプルピーク）まで止める安全リミッター。音作りより「超えない」ことが目的。STUDIO、デジタル筐体。配信・納品前の最終段。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Gain | ms02.gain | 0〜+24 | dB | 0.0 | LIN | ○ |
| Ceiling | ms02.ceiling | −12〜0 | dBTP | −1.0 | LIN | ○ |
| Release | ms02.release | 1〜1000、右端 Auto | ms | Auto | SKW（k=3） | ○ |
| Lookahead | ms02.lookahead | 0.5〜5 | ms | 1.5 | LIN | ○ |
| True peak | ms02.tp | Off ／ On | — | On | STEP | ○ |
| ISP detect | ms02.isp | 4x ／ 8x | — | 8x | STEP | ○ |
| Link | ms02.link | 0〜100 | % | 100 | LIN | ○ |
| Dither | ms02.dither | Off ／ 16 bit ／ 24 bit | — | Off | STEP | ○ |

**DSP方式**

- 先読みの1段ブリックウォール。ゲインの変化は先読み区間で滑らかにつなぐ。
- 検出は 4× または 8× の多相 FIR で補間した波形で行う。BS.1770 の True peak（4×）より細かく見られる。
- 止めた標本間ピークは時刻つきで記録し、画面の波形に印を付ける。

**遅延：** 先読み分＋補間フィルタ分。既定で約 100 サンプル（72＋約 30、@48 kHz、設計上の見積もり）。Low lat を押すと先読み 0.5 ms になる。

**CPU目安（設計上の見積もり）：** 中（8× 検出）。

**進化機能：** ISP 8x と印付け。どこで標本間ピークが出たかを画面で確認できる。DSP と記録だけで完結するため区分 A（初期）。

**共通機能との差分：** Unit A／B／C なし。

**【要確認】**

- 画面に Δ ボタンが無い。
- MS01 と機能が近い。MS02 は「Character なし・Lock なしの安全専用」と位置付けた。

## MS03 Multiband Limit

4帯域のマルチバンドリミッター。帯域ごとに上げて、全体を1つの天井で止める。STUDIO、デジタル筐体。音圧を上げつつ濁りを抑えるマスター用。

下表のバンドパラメータは n = 1〜4（Low／Low mid／High mid／High）、クロスオーバーは x = 1〜3。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Band n Gain | ms03.bn.gain | 0〜+12 | dB | 0.0 | LIN | ○ |
| Band n Ceiling | ms03.bn.ceiling | −12〜0 | dB | 0.0 | LIN | ○ |
| Band n Release | ms03.bn.release | 1〜1000 | ms | 60 | SKW（k=3） | ○ |
| Crossover x | ms03.xx.freq | 20〜20,000 | Hz | 120 ／ 1k ／ 6k（1〜3） | LOG | ○ |
| Out ceiling | ms03.outceiling | −12〜0 | dBTP | −1.0 | LIN | ○ |
| Character | ms03.char | Clean ／ Punch ／ Dense | — | Punch | STEP | ○ |
| Link bands（EVO） | ms03.evo.on | Off ／ On | — | On | STEP | ○ |

**DSP方式**

- 帯域分割は4次の Linkwitz-Riley。各帯域に先読みリミッター（先読み 2 ms）。最後に MS02 と同じ True peak リミッターを置き、Out ceiling を必ず守る。
- Link bands：合成後の波形が天井を超える分を、その瞬間の各帯域のエネルギー比で配分して下げる。1つの帯域が大きく下がって他の帯域まで揺れる（ポンピング）のを防ぐ。
- Character：帯域リミッターのニーとリリースの形を3種で切り替える。

**遅延：** 約 200 サンプル（帯域リミッターの先読み 96＋最終段 約 100、@48 kHz、設計上の見積もり）。Low lat を押すと先読みを 0.5 ms に縮める。

**CPU目安（設計上の見積もり）：** 重。先読みリミッター5段＋4× 以上の検出。

**進化機能：** Link bands。帯域ごとに上げても、帯域同士が引っ張り合わない。DSP だけで完結するため区分 A（初期）。ただし配分の計算は難度が高め。

**共通機能との差分：** Unit A／B／C なし。Δ は画面にある。

**【要確認】**

- 画面では「Link bands」はボタン。本書はこれを進化機能のスイッチとして扱い、既定 On にした。

## MS04 Clipper

クリッパー。リミッターの前段でピークを削り、音圧を稼ぐ。削り方をハードからテープ風まで連続で変えられる。STUDIO、デジタル筐体。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Drive | ms04.drive | 0〜+24 | dB | 0.0 | LIN | ○ |
| Ceiling | ms04.ceiling | −12〜0 | dB | −0.3 | LIN | ○ |
| Knee（EVO） | ms04.knee | 0〜100（0 ＝ Hard、50 ＝ Soft、100 ＝ Tape） | % | 50 | LIN | ○ |
| Mix | ms04.mix | 0〜100 | % | 100 | LIN | ○ |
| Oversample | ms04.os | 4x ／ 8x ／ 16x | — | 8x | STEP | ○ |
| Gain match | ms04.gainmatch | Off ／ On（Drive 分を出力で戻す） | — | On | STEP | ○ |
| Listen | ms04.listen | 削った成分だけを試聴（監視用） | — | Off | STEP | — |

**DSP方式**

- 伝達関数を Knee で連続に変える：0 は折れ線（ハード）、50 付近は3次の滑らかな曲線、100 はテープ風（高域ほど早く飽和し、わずかに履歴を持つ曲線）。途中は隣の形の補間。
- オーバーサンプリングは直線位相の多相 FIR（アタックの形を崩さないため）。共通章の OS 設定（EVO バーの「2× OS」）は MS04 では使わず、パネルの Oversample が優先する。

**遅延：** 4x で約 20、8x で約 30、16x で約 40 サンプル（@48 kHz、設計上の見積もり）。Low lat を押すと最小位相の IIR に切り替わり 0。

**CPU目安（設計上の見積もり）：** 中（8x）、16x で重寄り。

**進化機能：** Knee。1つのノブで、硬いデジタルクリップから柔らかいテープ飽和までを選べる。区分 A（初期）。

**共通機能との差分：** Unit A／B／C なし。Δ は画面にある。

**【要確認】**

- Listen（削った成分）と Δ（変化した成分）は同じ音になる。どちらかに統一するか決める。
- Gain match と共通の Auto gain が重複する。MS04 では Gain match を Drive 専用の補正として残した。
- EVO バーの「2× OS」とパネルの Oversample が両方ある。MS04 では EVO バー側を隠すことを推奨。

## MS05 Leveler

自動フェーダー（ゲインライダー）。音量の上下を目標に寄せ、その動きを DAW のオートメーションとして書き出せる。STUDIO、デジタル筐体。ポッドキャスト・ナレーション・ボーカル。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Target | ms05.target | −40〜−6（短時間ラウドネス） | LUFS | −18 | LIN | ○ |
| Range | ms05.range | 0〜24（上下それぞれの最大補正） | dB | 6 | LIN | ○ |
| Speed | ms05.speed | Slow ／ Medium ／ Fast | — | Medium | STEP | ○ |
| Gate | ms05.gate | −80〜−20（これ以下では動かない） | dBFS | −50 | LIN | ○ |
| Source | ms05.source | Vocal ／ Mix ／ Bass | — | Vocal | STEP | ○ |
| Ride | ms05.ride | −24〜+24（実際にかけた補正量） | dB | 0.0 | LIN | ○ |
| Write automation（EVO） | ms05.evo.on | Off ／ On | — | Off | STEP | — |

**DSP方式**

- 検出は 400 ms 窓の短時間ラウドネス。Source で検出の重み付けと時定数を変える（Vocal は中域重視、Bass は低域の長い窓、Mix は全帯域）。
- 補正は Ride パラメータを通してかける。Ride は計算結果の出口であり、同時にホストのオートメーションの入口でもある。
- Write automation On の間は、プラグインが Ride を自分で動かし、その変化をホストに「操作」として通知する（VST3 の編集開始・値変更・編集終了、CLAP のジェスチャー、AU のパラメータ変更通知）。トラックが書き込みモードならホストが記録する。Off にすると Ride はホストのオートメーションに従う（読み取り）。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 軽。

**進化機能：** Write automation。自動で整えた結果を、あとから手で直せるオートメーションとして残す。区分 A（初期）。ただし DAW ごとの記録の挙動差が大きく、主要 DAW での動作確認が必須。

**共通機能との差分：** Unit A／B／C なし。

**【要確認】**

- 画面に Δ ボタンが無い。
- 画面の「DAW」表記は Write automation の見出しと解釈した。

## MS06 Master Chain

マスタリング用のチェーン。EQ・Comp・Saturate・Width・Limit の5段を並べ替えて使う。STUDIO、デジタル筐体。

各段は1個ずつ、並び順はドラッグで変えてプリセットに保存する（オートメーション不可）。画面にあるのは Comp 段だけなので、他の段の項目は案。ID は `ms06.段.名前`。

| 段 | パラメータ |
| --- | --- |
| EQ | Tilt −6〜+6 dB（ピボット 1 kHz）、Low shelf −6〜+6 dB（80 Hz）、High shelf −6〜+6 dB（12 kHz）、Bell −6〜+6 dB（200 Hz〜8 kHz、Q 0.7） |
| Comp（画面あり） | Threshold −40〜0 dB（既定 0）、Ratio 1〜4（既定 1.5）、Attack 1〜100 ms（既定 30）、Release 20〜1000 ms＋Auto（既定 Auto）、Mix 0〜100 %（既定 100） |
| Saturate | Drive 0〜+12 dB、Mix 0〜100 % |
| Width | Width 0〜200 %、Mono below 20〜300 Hz（Off あり） |
| Limit | Gain 0〜+24 dB、Ceiling −12〜0 dBTP、Release Auto／1〜1000 ms（MS02 と同じ処理） |

各段に On（○）を持つ。全体に Gain match（Off／On、既定 On）と Reference A/B（参照曲への切替、監視用）を持つ。

**DSP方式**

- Gain match：各段の前後のラウドネスを測り、段ごとに音量差を打ち消す補正を持つ。どの段を外しても、並べ替えても、全体の音量が変わらない。
- Reference A/B：読み込んだ参照曲とラウドネスを揃えて切り替える（UT03 と同じ処理を共有）。
- メーター（Integrated・True peak・LRA）は表示専用。

**遅延：** Limit 段が On のとき約 100 サンプル（MS02 と同じ、@48 kHz、設計上の見積もり）。それ以外は 0。

**CPU目安（設計上の見積もり）：** 中。5段＋段ごとのラウドネス計測。

**進化機能：** 段ごとの Gain match と並べ替え。「大きい方が良く聞こえる」錯覚なしに、各段の効果と順番を比べられる。区分 A（初期）。

**共通機能との差分：** Unit A／B／C なし。

**【要確認】**

- 画面に Δ ボタンが無い。
- CS04 と同じく、Comp 以外の段の画面が無い。
- Reference A/B は UT03 と機能が重なる。

## MS07 Dither

ディザーと再量子化。書き出し前の最終段で、ビット数を落とすときの歪みをノイズに変える。STUDIO、2Uラック（brushed）。

| 名前 | ID | 範囲 | 単位 | 既定値 | カーブ | Auto |
| --- | --- | --- | --- | --- | --- | --- |
| Bits | ms07.bits | 16 ／ 20 ／ 24（3段） | bit | 16 | STEP | ○ |
| Shape | ms07.shape | Off ／ Light ／ Mid ／ Strong ／ Ultra（5段） | — | Mid | STEP | ○ |
| Output | ms07.out | −10〜+10（量子化の前にかける） | dB | 0.0 | LIN | ○ |
| Blank（EVO） | ms07.evo.on | Auto blank ／ Always | — | Auto blank | STEP | ○ |
| Truncation check | — | 解析ボタン | — | — | — | — |

**DSP方式**

- TPDF（三角分布）ディザーを足してから Bits に量子化する。左右で独立した乱数。
- Shape：誤差帰還によるノイズシェーピング。Off は平坦、Light から Ultra へ順に次数を上げ、耳の感度が低い高域へノイズを寄せる。44.1／48 kHz 用と 88.2 kHz 以上用で係数を分ける。
- Auto blank：入力が完全な無音（全標本 0）が 1024 標本以上続いたらディザーを止め、音が戻ったら 2 ms でフェードインする。
- Truncation check：入力の実効ビット数を調べ、すでに切り捨てられた信号や、量子化後に切り捨てが起きていないかを表示する。

**遅延：** 0 サンプル。

**CPU目安（設計上の見積もり）：** 軽。

**進化機能：** Auto blank。曲の頭と終わりの完全な無音にディザーノイズを乗せない。区分 A（初期）。

**共通機能との差分：** Δ は画面にある（ディザーと量子化誤差だけが聴ける）。Auto gain は使わない（音量を変えないため）。

**【要確認】**

- 画面に Unit A／B／C がある。ディザーは部品公差の意味が無いので、MS07 では Unit を外すことを推奨。
- Shape の既定を Mid にした。迷ったら無難な Light にする案もある。

## DY・MSカテゴリ一覧

19本中、既定の設定で遅延が出るのは DY05・MS01〜MS04 の5本。EVO は A（初期）12本、B（中期）7本。遅延の一部とCPUは設計上の見積もり。

| コード | 名前 | 筐体 | 遅延（サンプル@48 kHz） | CPU目安 | EVO区分 | EVOの操作 |
| --- | --- | --- | --- | --- | --- | --- |
| DY01 | FET 進化版 | 2Uラック | 0 | 中 | A | 常時（Bite・Color） |
| DY02 | Opto 進化版 | 2Uラック | 0 | 軽 | B | スイッチ（既定 Off） |
| DY03 | Bus 進化版 | 2Uラック | 0 | 軽 | A | スイッチ（既定 Off） |
| DY04 | Gate | 500シリーズ | 0 | 軽 | B | 学習ボタン |
| DY05 | De-ess | デジタル | 96（Lookahead 2 ms） | 中 | B | スイッチ（既定 On） |
| DY06 | Vari-Mu | 2Uラック | 0 | 中 | B | スイッチ（既定 On） |
| DY07 | Snap | 2Uラック | 0 | 軽 | A | 常時（Snap ノブ） |
| DY08 | Clean | デジタル | 0 ／ 240（Lookahead On） | 軽 | B | スイッチ（既定 Off） |
| DY09 | Transient | デジタル | 0 | 軽、Split 中 | A | Mode 切替 |
| DY10 | Multiband 4 | デジタル | 0 | 中 | B | 解析ボタン |
| DY11 | Multiband 6 | デジタル | 0 | 中 | A | 帯域ごとのモード |
| DY12 | Parallel | 2Uラック | 0 | 軽 | A | 常時（Upward ノブ） |
| MS01 | Maximizer 進化版 | デジタル | 約 110 | 中 | B | スイッチ（既定 Off） |
| MS02 | True Peak | デジタル | 約 100 | 中 | A | ISP 4x／8x |
| MS03 | Multiband Limit | デジタル | 約 200 | 重 | A | スイッチ（既定 On） |
| MS04 | Clipper | デジタル | 約 20〜40（Oversample） | 中 | A | 常時（Knee ノブ） |
| MS05 | Leveler | デジタル | 0 | 軽 | A | スイッチ（既定 Off） |
| MS06 | Master Chain | デジタル | 0 ／ 約 100（Limit 段 On） | 中 | A | 常時 |
| MS07 | Dither | 2Uラック | 0 | 軽 | A | Auto blank ／ Always |

## 残り107製品の書き方

SA 以降は、同じ中身を短い形で書く。決まりは共通章・DY・MS 共通事項をそのまま引き継ぐ。

- **パラメータ表の列：** 名前／範囲・単位／既定値／カーブ。
- **ID：** `製品コード.名前`（小文字、空白は除く。例 `sa01.saturation`）。同じ名前が2つ以上ある製品だけ個別に書く。
- **オートメーション：** 全項目 ○。例外（監視用・解析ボタン・UI専用）だけ「（Auto 不可）」と書く。
- **既定値：** 補正系は効かない位置。エフェクト・楽器（CR・IN、空間系の一部）は画面に描いた値を既定にして、挿してすぐ効果が分かるようにする。
- **Unit A／B／C：** アナログ筐体は「あり」が既定。外す場合だけ書く。Δ が画面に無い場合は【要確認】に書く。
- **各製品の行：** DSP ／ 遅延 ／ CPU（設計上の見積もり）／ 進化機能と区分 ／ 要確認。

## SA01〜SA08 倍音

8本。アナログ5本（SA01〜04・07）、デジタル3本。

### SA01 Tape — 2Uラック（brushed）

テープレコーダーの飽和・ヘッドバンプ・揺れ・ヒス。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Speed ips | 7.5 ／ 15 ／ 30 | 15 | STEP |
| Formula | A ／ B ／ C（磁性体の種類、汎用名） | A | STEP |
| Input | −12〜+12 dB | 0.0 | LIN |
| Saturation | 0〜10 | 3 | LIN |
| Wow ／ Flutter | 各 0〜10 | 0 | LIN |
| Hiss | Off〜Max（−90〜−50 dBFS） | Off | LIN |
| Output | −10〜+10 dB | 0.0 | LIN |
| Repro | Off ／ On（再生ヘッドの特性を通す） | On | STEP |

- **DSP：** 簡略化したヒステリシス（磁化の履歴）モデル、2× OS。速度ごとのヘッドバンプと高域損失。Wow・Flutter は補間付きの可変遅延。
- **遅延：** 48 サンプル固定（揺れの中心遅延 1 ms、@48 kHz）。Wow・Flutter が 0 でも同じ値を報告し、遅延が途中で変わらないようにする。
- **CPU：** 中。
- **進化機能：** Calibrate。5秒の入力から平均レベルを測り、テープの基準レベル（0 VU ＝ −18 dBFS）に合うよう Input を書き込む。区分 A。
- **要確認：** 画面のトグル「Repro」の反対側の表記が無い。

### SA02 Console Sum — 2Uラック（anodized）

アナログ卓のサミング風の色付け。全トラックに挿して使う前提。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Color | Iron ／ Clean ／ Punch ／ Vint | Iron | STEP |
| Drive | 0〜10 | 2 | LIN |
| Crosstalk | 0〜10（左右の漏れ −80〜−40 dB） | 0 | LIN |
| Noise | Off〜Max | Off | LIN |
| Width | Narrow〜Wide（0〜150 %） | 100 % | LIN |
| Output | −10〜+10 dB | 0.0 | LIN |
| Group | 1〜8（同じ番号のインスタンス同士を1台の卓として扱う） | 1 | STEP |

- **DSP：** 入出力トランスと加算アンプの飽和、2× OS。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** インスタンスごとの個体差。挿した時にインスタンス固有の乱数の種を作って状態に保存し、全トラックが少しずつ違う偏差を持つ（Unit A／B／C はその上に重なる）。保存されるので書き出しは毎回同じ。区分 A。
- **要確認：** 「Group」は画面ではトグル。番号選択にするか決める。

### SA03 Tube — 2Uラック（olive）

真空管の倍音付け。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Drive | 0〜10 | 3 | LIN |
| Bias | Cold〜Hot | 中央 | LIN |
| Tone | Dark〜Bright（−6〜+6 dB の傾き） | 0 | LIN |
| Tube | 12AX7 ／ 12AT7 ／ EL34（汎用の型番） | 12AX7 | STEP |
| Mix | Dry〜Wet | 100 % | LIN |
| Output | −10〜+10 dB | 0.0 | LIN |

- **DSP：** 三極管・五極管の伝達特性モデル、2× OS（4× 推奨）。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** 動くバイアス。入力の包絡（50 ms で戻る）で動作点をずらし、大きい音ほど非対称な歪みが増える。区分 A。

### SA04 Transformer — 2Uラック（bronze）

トランスとプリアンプの色付け。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Iron | Nickel ／ Steel ／ Mu（鉄心の材質、汎用名） | Steel | STEP |
| Gain | 目盛り 0〜+60（内部 −30〜+30 dB、目盛り 30 で 0 dB。CS03 と同じ） | 30 | LIN |
| Load | Low〜High（送り側のインピーダンス） | 中央 | LIN |
| Low weight ／ Top air | 各 0〜10 | 0 | LIN |
| Output | −10〜+10 dB | 0.0 | LIN |
| Pad | Off ／ On（−20 dB） | Off | STEP |

- **DSP：** 周波数で変わる飽和（低域ほど効く）、高域の共振と低域の損失をトランスの等価回路で計算。2× OS。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** Load。送り側のインピーダンスで高域の共振と低域の量が変わる。区分 A。

### SA05 Exciter — デジタル

高域の倍音付け。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Tune | 1〜16 kHz（ここから上に倍音を足す） | 4.5 kHz | LOG |
| Harmonics | 0〜100 % | 35 % | LIN |
| Mix | 0〜100 % | 25 % | LIN |
| Low drive | 0〜100 %（低域にも軽く倍音） | 0 % | LIN |
| Mode | Even ／ Odd ／ Both | Even | STEP |
| Mono low | Off ／ On（低域の倍音をモノに） | Off | STEP |
| Auto fill（EVO） | Off ／ On | On | STEP |

- **DSP：** Tune 以上を取り出し、偶数次・奇数次の多項式で倍音を作って足す。2× OS。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** 1/3 oct ごとに入力の高域を目標の傾き（ピンクノイズ寄り）と比べ、足りない帯域にだけ倍音を多く足す。区分 B。

### SA06 Saturator — デジタル

5種の歪みを3帯域で使える多機能サチュレーター。下表は帯域ごと（n = 1〜3、分割 200 Hz・3 kHz）。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Band n Type | Tape ／ Tube ／ Diode ／ Fold ／ Fuzz | Tube | STEP |
| Band n Drive | 0〜+24 dB | 0.0 | LIN |
| Band n Shape | Soft ／ Medium ／ Hard | Soft | STEP |
| Band n Bias | −1〜+1 | 0 | LIN |
| Band n Dynamics（EVO） | −5〜+5 | 0 | LIN |
| Band n Mix | 0〜100 % | 100 % | LIN |
| Tone | Dark〜Bright（−6〜+6 dB） | 0 | LIN |
| Output | −24〜+24 dB | 0.0 | LIN |

- **DSP：** 帯域分割は4次 Linkwitz-Riley。各帯域で波形整形、4× OS（Fold・Fuzz は 8× 推奨）。
- **遅延：** 0。**CPU：** 中（3帯域）。
- **進化機能：** Dynamics。＋側で強く弾いた音ほど歪み、−側で弱い音ほど歪む（歪み量を入力の包絡で動かす）。区分 A。
- **要確認：** 画面には帯域選択ボタンだけで、どの値が帯域ごとかの区別が無い。Δ ボタンが無い。

### SA07 Lo-Fi — 2Uラック（tolex）

レコード・古いテープ風の劣化。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Era | 1950 ／ 1970 ／ 1990 ／ Tape | 1970 | STEP |
| Crackle ／ Dust ／ Wow | 各 0〜10 | 0 | LIN |
| Bandwidth | Narrow〜Full（上限 3〜20 kHz） | Full | LOG |
| Mono | Off〜Full（0〜100 %） | Off | LIN |
| Mix | Dry〜Wet | 100 % | LIN |

- **DSP：** パチパチ音は確率で発生するインパルス＋短い共鳴、Dust は細かいノイズ粒、Wow は SA01 と同じ可変遅延。
- **遅延：** 48 サンプル固定（SA01 と同じ理由）。**CPU：** 軽。
- **進化機能：** Era。年代を選ぶと Crackle・Wow・Bandwidth をまとめて書き込む。区分 A。

### SA08 Bitcrush — デジタル

ビット数とサンプルレートを落とす。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Bits | 1〜24 bit | 8 | LIN（整数） |
| Rate | 200 Hz〜fs | 11 kHz | LOG |
| Jitter | 0〜100 % | 2 % | LIN |
| Mix | 0〜100 % | 70 % | LIN |
| Pre filter ／ Post filter ／ Dither | 各 Off ／ On | On ／ Off ／ Off | STEP |
| Tempo lock（EVO） | Off ／ On | Off | STEP |

- **DSP：** サンプルホールドと量子化。Pre filter は折り返し防止、Post filter は鏡像の除去。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** Tempo lock。Rate をテンポの整数倍の周波数に寄せ、折り返しのうなりが拍に合う。区分 A。
- **要確認：** Δ ボタンが無い。Tempo lock のスイッチが画面に無い。

## LO01〜LO03 低域

### LO01 Low Harm — デジタル

倍音で低域を「聞こえる」ようにする（小さいスピーカー対策）。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Frequency | 40〜200 Hz（ここより下から倍音を作る） | 80 Hz | LOG |
| Harmonics | 0〜100 % | 30 % | LIN |
| Original | −24〜0 dB（元の低域の残し量） | 0.0 | LIN |
| Width | Narrow ／ Medium ／ Wide（倍音の帯域） | Narrow | STEP |
| Preview | Off ／ Phone safe ／ Club（監視用、Auto 不可） | Off | STEP |

- **DSP：** Frequency 以下を取り出し、2〜5次の倍音を作って帯域を絞って足す。2× OS。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** Phone preview。スマホのスピーカー（約 300 Hz 以下が出ない＋小さな共振）を模した試聴。倍音で低域が聞こえるかを確かめる。区分 A。
- **要確認：** Preview は書き出しにも乗るため、On のままだと警告表示を出す。Δ ボタンが無い。

### LO02 Sub Gen — 2Uラック（anodized）

サブハーモニック（1オクターブ下）を作る。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Sub | 0〜10（0〜+12 dB） | 0 | LIN |
| Range Hz | 30 ／ 45 ／ 60 ／ 90（検出帯域の上限の半分） | 45 | STEP |
| Tune | −12〜+12 st | 0 | LIN（整数） |
| Punch | 0〜10（頭の強調） | 0 | LIN |
| Dry | Off〜Full | Full | LIN |

- **DSP：** ゼロ交差の分周（遅延0）で位相のそろったサブを作り、低域のピッチ検出で正弦波を安定させる混成方式。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** ベースの音程追従。Tune 指定時も音程に合わせて作る。区分 B。

### LO03 Low Focus — デジタル

キックとベースの低域を分け、ぶつかりを減らす。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Role | Kick ／ Bass ／ Both（このインスタンスの役割） | Both | STEP |
| Focus | 30〜120 Hz | 55 Hz | LOG |
| Tight | 0〜100 % | 50 % | LIN |
| Mud cut | 150〜500 Hz | 250 Hz | LOG |
| Mono below | Off＋20〜300 Hz | 120 Hz | LOG |

- **DSP：** Focus に動的ベル、Mud cut に静的ベル、Mono below で側成分のハイパス。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** キーの相互連動。Bass 役のインスタンスは、SW Link（または外部サイドチェーン）で Kick 役の立ち上がりを受け、キックが鳴る瞬間だけ Focus 帯域を下げる。Both は1トラックの中で同じことを推定で行う。区分 B。
- **要確認：** Δ ボタンが無い。SW Link が届かない環境ではサイドチェーン接続が必要。

## GT01〜GT05 ギター

### GT01 Amp — 2Uラック（tolex）

ギターアンプのヘッド部（キャビネットなし）。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Channel | Clean ／ Crunch ／ Lead | Crunch | STEP |
| Gain | 0〜10 | 5 | LIN |
| Bass ／ Middle ／ Treble ／ Presence | 各 0〜10 | 5 | LIN |
| Master | 0〜10 | 5 | LIN |
| Bright | Off ／ On | Off | STEP |

- **DSP：** 三極管2段＋受動トーンスタック（回路の伝達関数を計算）＋電源の垂れ下がり（サグ）＋出力段。4× OS。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** ボリュームでのクリーンアップ。入力の大きさで歪み方が変わる回路モデルにし、最初の5秒で入力の平均を測って「ギター側を絞るときれいになる」位置を合わせる。区分 A。
- **要確認：** キャビネットが無いので単体だと耳障りな音になる。GT02 の初期 IR を内蔵して「Cab On/Off」を足すか決める。

### GT02 Cab IR — デジタル

キャビネットとマイクの畳み込み。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Cab | 1x12 ／ 2x12 ／ 4x12 | 4x12 | STEP |
| Mic | Dynamic ／ Ribbon ／ Condenser（汎用の種類名） | Dynamic | STEP |
| Mic distance | 0〜30 cm | 4 cm | LIN |
| Off axis | 0〜90 deg | 15 deg | LIN |
| Room | 0〜100 % | 10 % | LIN |
| Low cut | Off＋20〜300 Hz | 80 Hz | LOG |

- **DSP：** 先頭を直接畳み込み、後半を FFT 分割する方式（遅延0）。マイク位置は格子状に収録した IR の補間。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** スピーカー上でマイクをドラッグ。区分 A。
- **要確認：** IR は自社で収録する必要がある（他社 IR は使えない）。収録の計画と費用が未定。Δ ボタンが無い。

### GT03 Pedalboard — デジタル

ペダルを並べるボード。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Input ／ Output | 各 −24〜+24 dB | 0.0 | LIN |
| Noise gate | Off＋−80〜−20 dB | −60 dB | LIN |
| Bypass all | Off ／ On | Off | STEP |
| ペダル | Comp ／ Drive ／ Fuzz ／ Chorus ／ Delay ／ Reverb（最大8台、並び順はプリセット保存、Auto 不可） | 画面の6台 | — |

- **DSP：** ペダルは他製品の処理を流用（Comp＝DY08、Drive・Fuzz＝SA06、Chorus＝MD01、Delay＝DL01、Reverb＝RV01 の簡易版）。
- **遅延：** 0。**CPU：** 中（6台）。
- **進化機能：** ドラッグで並べ替え、チューナー常時動作（入力のピッチ検出を表示のみに使う）。区分 A。
- **要確認：** 各ペダルのノブが画面に無い。Δ ボタンが無い。

### GT04 Bass Amp — 2Uラック（tolex）

ベースアンプ。DI との混ぜを持つ。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Gain ／ Drive ／ Master | 各 0〜10 | 5 ／ 0 ／ 5 | LIN |
| Low ／ Lo mid ／ Hi mid ／ High | 各 0〜10（5 で平ら） | 5 | LIN |
| Mid Hz | 250 ／ 500 ／ 800 ／ 1.5k ／ 3k | 800 | STEP |
| DI | Off ／ On | On | STEP |
| DI blend | 0〜100 %（画面に無い、追加） | 50 % | LIN |

- **DSP：** プリ＋4バンドEQ＋オーバードライブ＋簡易キャビネット。2× OS。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** DI とアンプの位相合わせ。アンプ側の位相特性は自前の処理なので計算で分かる。その分だけ DI 側に全域通過フィルタと遅延を入れて揃える（全体の遅延は増えない）。区分 A。
- **要確認：** DI の混ぜ量ノブが画面に無い。

### GT05 Reamp — 2Uラック（brushed）

DI 録りの音を、別のギター・別の入力条件に変える。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Level | −20〜+10 dB | 0.0 | LIN |
| Impedance | 10k ／ 47k ／ 100k ／ 1M Ω | 1M | STEP |
| Cable | Short〜Long（100〜1000 pF） | Short | LOG |
| Pickup | Single〜Hum | 中央 | LIN |
| Output | −10〜+10 dB | 0.0 | LIN |

- **DSP：** ピックアップの共振（2〜6 kHz の山）とケーブル容量・受け側インピーダンスで決まる低域通過を計算する。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** ピックアップ置き換え。DI から元のピックアップの共振を推定して打ち消し、Pickup で選んだ共振を付け直す。区分 B。
- **要確認：** 画面のトグル「Lift」（グラウンドリフト）はプラグインでは意味が無い。外すか、別の機能に割り当てる。

## RV01〜RV08 リバーブ

リバーブは全製品で「残響側の遅延は音の一部」として扱い、ホストへの報告遅延は 0。Mix の既定は画面の値（センド用途では 100 % にする）。

### RV01 Hall — デジタル

アルゴリズムリバーブ。5種のアルゴリズム。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Algorithm | Hall ／ Room ／ Chamber ／ Plate ／ Ambience | Hall | STEP |
| Pre-delay | 0〜500 ms | 24 ms | SKW（k=2） |
| Size | 0〜100 % | 74 % | LIN |
| Decay | 0.2〜20 s | 2.8 s | LOG |
| Diffusion | 0〜100 % | 82 % | LIN |
| Damping | 1〜20 kHz | 6.5 kHz | LOG |
| Low cut ／ High cut | 20〜1000 Hz ／ 1〜20 kHz | 120 Hz ／ 9 kHz | LOG |
| ER / late | 0〜100（初期反射の比率） | 40 | LIN |
| Width | 0〜150 % | 100 % | LIN |
| Mix | 0〜100 % | 22 % | LIN |
| Freeze ／ Mono low end | 各 Off ／ On | Off | STEP |
| Duck（EVO） | 0〜−18 dB（画面に無い、追加） | −6 dB | LIN |

- **DSP：** 16本の遅延線のフィードバック遅延網（FDN）、各線にゆるい変調と周波数別の減衰。初期反射はタップ遅延。
- **CPU：** 中。
- **進化機能：** 原音が大きい間は残響を Duck 分下げ、隙間で戻す。区分 A。
- **要確認：** Duck 量のノブが画面に無い。Δ ボタンが無い。

### RV02 Plate — 2Uラック（graphite）

プレートリバーブ。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Decay | 0.5〜6 s | 2.0 s | LOG |
| Pre-delay | 0〜200 ms（Sync 時は音符長） | 20 ms | SKW（k=2） |
| Damping | Dark〜Bright | 中央 | LIN |
| Low cut | 20〜500 Hz | 80 Hz | LOG |
| Width | Mono〜Wide | Wide | LIN |
| Mix | Dry〜Wet | 30 % | LIN |
| Mono in | Off ／ On | Off | STEP |

- **DSP：** 分散を持つ全域通過の連鎖（金属板の高域が先に届く性質）＋FDN。
- **CPU：** 中。
- **進化機能：** プリディレイのテンポ同期と、ボーカル中の Duck（RV01 と同じ処理、Duck −6 dB 固定）。区分 A。

### RV03 Spring — 2Uラック（tolex）

スプリングリバーブ。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Springs | 1 ／ 2 ／ 3 | 2 | STEP |
| Dwell | 0〜10（スプリングへの入力の強さ） | 5 | LIN |
| Tone | Dark〜Bright | 中央 | LIN |
| Tension | 0〜10（さえずりの音程） | 5 | LIN |
| Drip | 0〜10 | 5 | LIN |
| Mix | Dry〜Wet | 30 % | LIN |

- **DSP：** バネごとに多段の「引き伸ばした全域通過」連鎖で分散を作り、フィードバック。
- **CPU：** 中。
- **進化機能：** Drip が立ち上がりにだけ反応する（立ち上がり検出で分散経路の入力を開く）。伸ばした音ではビヨンと鳴らない。区分 A。

### RV04 Convolution — デジタル

インパルス応答（IR）の畳み込み。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Category | Halls ／ Rooms ／ Churches ／ Gear ／ Custom | Halls | STEP |
| Pre-delay | 0〜500 ms | 12 ms | SKW（k=2） |
| Length | 10〜100 % | 100 % | LIN |
| Size | 50〜150 %（IR の伸縮） | 100 % | LIN |
| Low cut ／ High cut | 20〜1000 Hz ／ 1〜20 kHz | 80 Hz ／ 12 kHz | LOG |
| Reverse | Off ／ On | Off | STEP |
| Mix | 0〜100 % | 20 % | LIN |
| Load IR | ファイル読み込み（Auto 不可） | — | — |

- **DSP：** 先頭直接畳み込み＋不均一分割 FFT（遅延0）。Size は IR の再標本化。
- **CPU：** 重（長い IR）。
- **進化機能：** Length をテンポに合わせ、残響が1小節・2小節などの区切りで消えるようフェードを付ける。区分 A。
- **要確認：** IR は自社収録。「Gear」カテゴリは実在機材の IR を想起させるため、名前と中身の扱いを決める（実機名は使わない）。Δ ボタンが無い。

### RV05 Chamber — 2Uラック（olive）

エコーチェンバー（残響室）。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Room | Small ／ Medium ／ Large | Medium | STEP |
| Decay | 0〜10（0.4〜4 s） | 5 | LOG |
| Mic distance | Near〜Far | 中央 | LIN |
| Speaker tilt | 0〜10 | 5 | LIN |
| Tone | Dark〜Bright | 中央 | LIN |
| Mix | Dry〜Wet | 30 % | LIN |

- **DSP：** 鏡像法の初期反射（部屋の形から計算）＋FDN の残響。
- **CPU：** 中。
- **進化機能：** マイク距離で、直接音と残響の比・初期反射の並び・高域の減り方が連続して変わる。区分 A。

### RV06 Shimmer — デジタル

音程を上げた残響が重なるシマー。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Decay | 1〜60 s | 12 s | LOG |
| Shimmer | 0〜100 % | 60 % | LIN |
| Interval | Octave ／ Fifth ／ Both | Octave | STEP |
| Mix | 0〜100 % | 35 % | LIN |
| Freeze（EVO） | Off ／ On | Off | STEP |
| Duck | Off ／ On | On | STEP |

- **DSP：** FDN のフィードバック内に2粒の重ね合わせによる音程変換。
- **CPU：** 中。
- **進化機能：** Freeze。フィードバックを 1.0 にして入力を止め、和音のパッドとして保持する。区分 A。
- **要確認：** 04 の Pitch「+12 st」は Interval の選択と解釈した。Δ ボタンが無い。

### RV07 Early — デジタル

初期反射だけで「部屋の中の位置」を作る。台詞・効果音向け。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Use | Dialog ／ Instrument ／ Foley | Dialog | STEP |
| Distance | 0.5〜20 m | 3.5 m | LOG |
| Angle | −90〜+90 deg | 15 deg | LIN |
| Room size | Small ／ Medium ／ Large | Medium | STEP |
| Wall | Wood ／ Concrete ／ Glass ／ Curtain | Wood | STEP |

- **DSP：** 鏡像法の初期反射（2次まで）、距離による減衰と空気吸収、左右の配置。直接音は遅らせない（映像と同期を保つ）。
- **CPU：** 軽。
- **進化機能：** 部屋の図の上で音源をドラッグして置く。区分 A。
- **要確認：** Mix が無い（台詞用は 100 % 前提）。Δ ボタンが無い。

### RV08 Gated — 2Uラック（anodized）

ゲートリバーブ。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Size | 0〜10 | 5 | LIN |
| Gate time | 50〜800 ms | 250 ms | LOG |
| Threshold | 0〜10（−60〜0 dBFS） | 5 | LIN |
| Shape | Flat〜Reverse | Flat | LIN |
| Tone | Dark〜Bright | 中央 | LIN |
| Mix | Dry〜Wet | 40 % | LIN |

- **DSP：** 高密度の残響を、立ち上がりで開くゲートで切る。Shape で開いている間の音量の形を変える。
- **CPU：** 中。
- **進化機能：** スネアにだけ開くゲート。CS02 と同じ被り学習で、スネアの帯域（約 150〜250 Hz と 2〜5 kHz）にキーを合わせる。区分 B。

## DL01〜DL05 ディレイ

ディレイは全製品で報告遅延 0（遅れて返る音は効果そのもの）。Feedback 100 % 超の発振は、ループ内のソフトリミッターで頭打ちにする。

### DL01 Echo — 2Uラック（anodized）

3種の音色を持つエコー。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mode | Tape ／ Analog ／ Digital | Tape | STEP |
| Time | 1〜2000 ms（Sync 時は 1/64〜2 小節） | 375 ms | LOG |
| Feedback | 0〜110 % | 35 % | LIN |
| HPF ／ LPF | 20〜1k Hz ／ 1k〜20k Hz（ループ内） | 100 Hz ／ 8 kHz | LOG |
| Depth ／ Rate | 0〜100 % ／ 0.1〜10 Hz（揺れ） | 10 % ／ 0.5 Hz | LIN ／ LOG |
| Duck（EVO） | 0〜20 dB | 0 | LIN |
| Mix | Dry〜Wet | 25 % | LIN |
| Sync ／ Ping-pong | 各 Off ／ On | On ／ Off | STEP |

- **DSP：** 補間付き遅延線。Mode で、ループ内の飽和・帯域・揺れの性格を切り替える。
- **CPU：** 軽。
- **進化機能：** 歌っている間は返りを Duck 分下げ、隙間で持ち上げる。区分 A。
- **要確認：** 画面のパネル表記は「Hybrid echo processor」、03 の説明は「Tape delay」。どちらかに揃える。Δ ボタンが無い。

### DL02 Tape Echo — 2Uラック（olive）

3ヘッドのテープエコー。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Heads | 1 ／ 2 ／ 3 ／ 1+2 ／ 2+3 ／ All（ヘッド間隔は 1:2:3） | 1+2 | STEP |
| Rate | Slow〜Fast（1ヘッド目 50〜200 ms） | 中央 | LOG |
| Intensity | 0〜10（Feedback 0〜110 %） | 4 | LIN |
| Bass ／ Treble | 各 −6〜+6 dB | 0 | LIN |
| Wear（EVO） | 0〜10 | 3 | LIN |
| Mix | Dry〜Wet | 25 % | LIN |

- **DSP：** テープ速度で遅延と帯域が連動する遅延線、SA01 のテープ飽和を流用。
- **CPU：** 中。
- **進化機能：** Wear で高域の劣化・ドロップアウト・揺れの増加をまとめて動かす。Heads の切替は小節頭に揃え、10 ms でつなぐ。区分 A。

### DL03 Bbd — 2Uラック（graphite）

バケツリレー素子（BBD）のアナログディレイ。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Time | 20〜600 ms | 300 ms | LOG |
| Feedback | 0〜10 | 4 | LIN |
| Mod depth ／ Mod rate | 各 0〜10 | 2 ／ 3 | LIN |
| Grit（EVO） | 0〜10 | 2 | LIN |
| Mix | Dry〜Wet | 25 % | LIN |
| Sync | Off ／ On | Off | STEP |

- **DSP：** 時間に応じてクロック周波数を変えるモデル（長いほど帯域が狭く、折り返しが増える）、コンパンダー、クロックに追従するフィルタ。
- **CPU：** 中。
- **進化機能：** Grit。クロックノイズを返りの音にだけ足す（原音は汚さない）。区分 A。

### DL04 Multitap — デジタル

6タップのディレイ。下表はタップごと（n = 1〜6）。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Tap n On | Off ／ On | 1〜3 On | STEP |
| Tap n Time | 1〜4000 ms（Sync 時は音符長、付点・3連あり） | 1/8・1/8 D・1/4… | LOG |
| Tap n Level | −60〜0 dB | −6 dB | LIN |
| Tap n Pan | L100〜R100 | 左右交互 | LIN |
| Tap n Filter | 200 Hz〜20 kHz（低域通過） | 8 kHz | LOG |
| Feedback | 0〜100 % | 30 % | LIN |
| Mix | 0〜100 % | 20 % | LIN |
| Sync ／ Ping-pong | 各 Off ／ On | On ／ Off | STEP |

- **CPU：** 軽。
- **進化機能：** タップごとのフィルタとパン。区分 A。
- **要確認：** Δ ボタンが無い。

### DL05 Reverse — デジタル

逆再生のディレイ。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mode | Reverse ／ Forward ／ Random | Reverse | STEP |
| Time | 1/16〜2 小節（音符長） | 1/4 | STEP |
| Grain size | 10〜500 ms | 80 ms | LOG |
| Spray | 0〜100 % | 30 % | LIN |
| Pitch +12 ／ Freeze | 各 Off ／ On | Off | STEP |
| Mix | 0〜100 % | 40 % | LIN |

- **CPU：** 軽。
- **進化機能：** 粒の区切りをホストの拍位置に合わせ、拍頭で逆再生が始まる。区分 A。
- **要確認：** Δ ボタンが無い。

## MD01〜MD07 モジュレーション

### MD01 Chorus — 2Uラック（brushed）

BBD 風のコーラス。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mode | I ／ II ／ I+II | II | STEP |
| Rate | Slow〜Fast（0.1〜5 Hz） | 0.5 Hz | LOG |
| Depth | 0〜10 | 5 | LIN |
| Width | Mono〜Wide | Wide | LIN |
| Tone | Dark〜Bright | 中央 | LIN |
| Mix | Dry〜Wet | 50 % | LIN |

- **DSP：** 変調遅延線（中心 7 ms 前後）＋BBD 風の帯域制限とノイズ。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** モノ互換の広がり。左右の変調を逆向きにして、L+R（モノ）では揺れが打ち消し合う構成にする。モノで聴いても濁らない。区分 A。

### MD02 Flanger — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Rate | 0.01〜10 Hz（Sync 時は音符長） | 0.2 Hz | LOG |
| Depth | 0〜100 % | 70 % | LIN |
| Feedback | −100〜+100 % | +60 % | LIN |
| Manual | 0.1〜10 ms | 3 ms | LOG |
| Through zero（EVO） | Off ／ On | On | STEP |
| Sync | Off ／ On | Off | STEP |
| Mix | 0〜100 %（画面の「Mix 50%」ボタンは 50 % に戻す操作） | 50 % | LIN |

- **DSP：** 変調遅延線。Through zero では原音側を 10 ms 遅らせ、効果音が原音を追い越して交差する。
- **遅延：** Through zero On で 480 サンプル（10 ms@48 kHz）、Off で 0。
- **CPU：** 軽。
- **進化機能：** テンポ同期した Through zero の掃引。区分 A。
- **要確認：** Δ ボタンが無い。

### MD03 Phaser — 2Uラック（tolex）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Stages | 4 ／ 6 ／ 8 ／ 12 | 6 | STEP |
| Rate | Slow〜Fast（0.05〜8 Hz） | 0.5 Hz | LOG |
| Depth ／ Feedback | 各 0〜10 | 5 ／ 3 | LIN |
| Center | Low〜High（200 Hz〜4 kHz） | 800 Hz | LOG |
| Mix | Dry〜Wet | 50 % | LIN |
| Sync | Off ／ On | Off | STEP |
| Note follow（EVO） | Off ／ On | Off | STEP |

- **DSP：** 1次全域通過フィルタの直列。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 弾いている音の高さ（ピッチ検出）に Center を追従させ、どの音でも同じ位置で効く。区分 B。

### MD04 Tremolo Pan — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mode | Tremolo ／ Auto pan ／ Harmonic | Auto pan | STEP |
| Rate | 0.1〜20 Hz（Sync 時は音符長） | 1/8 | LOG |
| Depth | 0〜100 % | 60 % | LIN |
| Shape | Sine ／ Triangle ／ Square ／ Ramp | Sine | STEP |
| Width | 0〜100 % | 100 % | LIN |
| Sync | Off ／ On | On | STEP |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** Harmonic。約 800 Hz で上下に分け、低域と高域を逆位相で揺らす。区分 A。
- **要確認：** Δ ボタンが無い。

### MD05 Rotary — 2Uラック（walnut）

回転スピーカー。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Speed | Stop ／ Slow ／ Fast | Slow | STEP |
| Accel | 0〜10（回転の加減速の時間） | 5 | LIN |
| Horn ／ Drum | 各 0〜10（音量） | 7 | LIN |
| Mic distance | Near〜Far | 中央 | LIN |
| Drive | 0〜10 | 2 | LIN |
| Mix | Dry〜Wet | 100 % | LIN |

- **DSP：** 800 Hz で分けた上下の回転体ごとに、ドップラー（変調遅延）・音量変化・キャビネット共振。ホーンとドラムは慣性の時定数が別。
- **遅延：** 48 サンプル固定（ドップラーの中心遅延 1 ms）。**CPU：** 中。
- **進化機能：** フットスイッチや MIDI（CC64・CC1、Note）で Speed を切り替え、加減速は物理モデルどおりに追従する。区分 A。

### MD06 Freq Shift — デジタル

周波数シフター。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Shift | −2000〜+2000 Hz | +35 Hz | 対数対称 |
| Direction | Up ／ Down ／ Both | Both | STEP |
| Ring mod | Off ／ On | Off | STEP |
| Feedback | 0〜100 % | 20 % | LIN |
| LFO | Off ／ On | Off | STEP |
| Mix | 0〜100 % | 50 % | LIN |
| Pitch track（EVO） | Off ／ On | Off | STEP |

- **DSP：** IIR 全域通過のヒルベルト変換対で直交信号を作る（遅延0）。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** シフト量を音の高さに比例させ、どの音域でも同じ「セント」のずれにする。区分 B。
- **要確認：** Δ ボタンが無い。

### MD07 Ensemble — 2Uラック（graphite）

ストリングアンサンブル風の多重コーラス。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Voices | 2 ／ 3 ／ 4 ／ 6 | 4 | STEP |
| Spread ／ Rate ／ Depth | 各 0〜10 | 6 ／ 4 ／ 5 | LIN |
| Tone | Dark〜Bright | 中央 | LIN |
| Mix | Dry〜Wet | 50 % | LIN |

- **DSP：** 複数の変調遅延線を、速い揺れと遅い揺れの2系統で動かす。
- **遅延：** 0。**CPU：** 軽〜中。
- **進化機能：** MD01 と同じモノ互換の構成を声部数ぶん適用。区分 A。

## ST01〜ST06 ステレオ

### ST01 Imager — デジタル

4帯域の広がり調整。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Low ／ Lo mid ／ Hi mid ／ High width | 各 0〜200 %（0 ＝ Mono） | 100 % | LIN |
| Crossover 1〜3 | 20〜20,000 Hz | 200 Hz ／ 2 kHz ／ 8 kHz | LOG |
| Mono check | Off ／ On（監視用、Auto 不可） | Off | STEP |

- **DSP：** 4次 Linkwitz-Riley で分け、帯域ごとに M/S の S を増減。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 帯域ごとの相関メーターと、モノで聴き比べる Mono check。広げすぎた帯域に印を出す。区分 A。
- **要確認：** Δ ボタンが無い。

### ST02 Mid Side — 2Uラック（graphite）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mid level ／ Side level | 各 −12〜+12 dB | 0.0 | LIN |
| Side HPF | Off＋20〜500 Hz | Off | LOG |
| Side air | 0〜10（S の 10 kHz シェルフ 0〜+6 dB） | 0 | LIN |
| Mid low | −6〜+6 dB（M の 100 Hz シェルフ） | 0 | LIN |
| Encode | Off ／ On（入出力を M/S のまま扱う） | Off | STEP |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 参照曲と M/S のバランスを帯域ごとに比べて表示する。区分 B。

### ST03 Phase Align — デジタル

2本のマイク（例：キックの内と外）の時間と位相を合わせる。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Delay | 0〜20 ms（0.01 ms 刻み） | 0.00 ms | SKW（k=2） |
| Phase | −180〜+180 deg（全域通過で回す） | 0 deg | LIN |
| Polarity | Normal ／ Invert | Normal | STEP |
| Mix | 0〜100 % | 100 % | LIN |
| Auto align | 解析ボタン（Auto 不可） | — | — |

- **DSP：** 外部サイドチェーンに基準のマイクを入れる。Auto align は相互相関で遅延を出し、低域の相関が最大になる位相回転を探す。
- **遅延：** 0（遅らせるだけなので、早い方のトラックに挿す）。**CPU：** 軽。
- **進化機能：** Auto align。区分 B。
- **要確認：** 画面の「Δ Compare」は合わせる前後の比較で、共通の Δ とは別。名前を変えるか決める。

### ST04 Center — 2Uラック（brushed）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Center | Wide〜Focus（S を +6〜−∞ dB） | 中央 | LIN |
| Haas | 0〜40 ms | 0 ms | SKW（k=2） |
| Side | L ／ R（遅らせる側） | L | STEP |
| Low center | 0〜10（この周波数より下をモノに、20〜300 Hz） | 0 | LOG |
| Balance | L〜R | 中央 | LIN |
| Link | Off ／ On | On | STEP |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** モノ互換の自動確認。Haas でモノにしたときの櫛形の落ち込みを測り、深すぎれば遅らせた側の高域とレベルを自動で弱める。区分 A。

### ST05 Phones — デジタル

ヘッドホンで部屋のスピーカーを聴いているように再現する。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Speakers | Nearfield ／ Mains ／ Car | Nearfield | STEP |
| Room | Studio A ／ Studio B ／ Living | Studio A | STEP |
| Angle | 0〜60 deg | 30 deg | LIN |
| Head size | Small ／ Medium ／ Large | Medium | STEP |
| Tracking | Off ／ On（頭の動きの追従） | Off | STEP |
| Phones profile | ヘッドホンの補正カーブ（下の要確認） | Off | STEP |

- **DSP：** 両耳の室内インパルス応答の畳み込み（左右×2本）。
- **遅延：** 0（先頭直接畳み込み）。**CPU：** 中〜重。
- **進化機能：** ヘッドホン機種ごとの補正。区分 A（データは要収録）。
- **要確認：** 「一般的なヘッドホン機種のプロファイル」は機種名を画面に出すことになり、05 の「実機名は使わない」と衝突する。種類別の汎用カーブ（密閉・開放・イヤホン）か、ユーザーの測定データ読み込みにすることを推奨。室内インパルス応答は自社収録か、商用利用できるライセンスのデータが必要。Tracking は外部センサーとの連携が必要で、対応機器が未定。Monitor 用なので書き出し時の注意表示も要る。

### ST06 Mono Low — 2Uラック（anodized）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Frequency | 20〜300 Hz | 120 Hz | LOG |
| Slope | 6 ／ 12 ／ 24 ／ 48 dB/oct | 24 | STEP |
| Side boost | −6〜+6 dB（Frequency より上の S） | 0 | LIN |
| Output | −10〜+10 dB | 0.0 | LIN |
| Listen（EVO） | Off ／ On（監視用、Auto 不可） | Off | STEP |

- **DSP：** S だけにハイパス（M は触らない）。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** Listen。モノにされて消える成分（Frequency 以下の S）だけを試聴する。区分 A。

## VO01〜VO08 ボーカル

音程を変える VO01・VO02・VO03・VO06 は共通の音程エンジン（ピッチ検出＋時間領域の波形重ね合わせ、フォルマントはスペクトル包絡で保持）を使う。

### VO01 Tune — デジタル

ピッチ補正。自動と、グラフで音を直す編集の2方式。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| View | Graph ／ Auto | Auto | STEP |
| Scale | C major ／ Chromatic ／ Custom（キーは12種） | C major | STEP |
| Speed | 0〜400 ms | 20 ms | SKW（k=2） |
| Humanize | 0〜100 % | 40 % | LIN |
| Vibrato | Natural ／ Reduce ／ Flat | Natural | STEP |
| Formant | Keep ／ Follow | Keep | STEP |
| Transpose | −12〜+12 st | 0 | LIN（整数） |
| Detect MIDI ／ Snap to grid ／ Reference | 各 Off ／ On | Off ／ On ／ Off | STEP |

- **遅延：** Auto で 512 サンプル（ピッチ検出の窓、@48 kHz、設計上の見積もり）。**CPU：** 重。
- **進化機能：** キー検出。解析した音程の分布をキーの型と照合し、Scale を提案する。区分 B。
- **要確認：** Graph 編集には音声の取り込みが必要。ARA 2 に対応するか（ライセンス条件の確認を含む）、プラグイン内に録音して取り込むかを決める。Δ ボタンが無い。

### VO02 Tune Rt — 2Uラック（anodized）

ライブで歌う人向けの低遅延ピッチ補正。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Key | C〜B（12段） | C | STEP |
| Scale | Maj ／ Min ／ Chr | Maj | STEP |
| Speed | Slow〜Hard（100〜0 ms） | 中央 | SKW（k=2） |
| Humanize | 0〜10 | 3 | LIN |
| Formant | −〜＋（−3〜+3 半音相当） | 0 | LIN |
| Mix | Dry〜Wet | 100 % | LIN |

- **遅延：** 128 サンプル（2.7 ms@48 kHz、設計上の見積もり）。**CPU：** 中。
- **進化機能：** 低遅延とフォルマント保持。検出窓を短くし、不安定な区間は補正を弱める。区分 B。
- **要確認：** 画面の Key セレクターは C・D・E・F・G・A・B の7段で、♯・♭が無い。12段にする必要がある。

### VO03 Harmony — デジタル

ハモリ生成。下表は声部ごと（n = 1〜4）。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Source | MIDI ／ Scale ／ Fixed（音程の決め方） | Scale | STEP |
| Voice n On | Off ／ On | 1・2 On | STEP |
| Voice n Interval | −1oct〜+1oct（音階上の度数） | +3rd ／ +5th | STEP |
| Voice n Level | −60〜0 dB | −3 dB | LIN |
| Voice n Pan | L100〜R100 | L40 ／ R40 | LIN |
| Voice n Formant | −3〜+3 | 0 | LIN |
| Voice n Humanize | 0〜100 % | 25 % | LIN |
| Voice n Delay | 0〜100 ms | 15 ms | LIN |

- **遅延：** 512 サンプル（VO01 と同じ）。**CPU：** 重（4声部）。
- **進化機能：** MIDI トラックのコードに従ってハモる音を選ぶ（声部の動きが最小になる音を選ぶ）。区分 B。
- **要確認：** Δ ボタンが無い。

### VO04 Doubler — 2Uラック（graphite）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Voices | 1 ／ 2 ／ 4 ／ 8 | 2 | STEP |
| Spread ／ Timing ／ Pitch var | 各 0〜10 | 6 ／ 4 ／ 3 | LIN |
| Tone | Dark〜Bright | 中央 | LIN |
| Mix | Dry〜Wet | 50 % | LIN |

- **DSP：** 声部ごとに、ゆっくり動く不規則な遅延（0〜30 ms）とごく小さな音程のずれ。フレーズの頭では遅延を小さくして、歌い出しがずれすぎないようにする。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** 周期的な揺れ（コーラス）ではなく、人が重ね録りしたようなずれ方。区分 A。

### VO05 Rider — デジタル

曲に対してボーカルの音量を自動で整える。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Target | −40〜−6 dB（曲に対する相対値） | −18 dB | LIN |
| Range | 0〜12 dB | 6 dB | LIN |
| Sensitivity | Low ／ Mid ／ High | Mid | STEP |
| Breath skip | Off ／ On | On | STEP |
| Ride | −12〜+12 dB（実際の補正量） | 0.0 | LIN |
| Write automation | Off ／ On（Auto 不可） | Off | STEP |

- **DSP：** 曲側の音量は外部サイドチェーンか SW Link で受ける（画面の「Music: Listening」表示）。書き出しは MS05 と同じ仕組み。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 曲の大小に合わせて、ボーカルと曲の比を一定に保つ。区分 B。
- **要確認：** Δ ボタンが無い。

### VO06 Formant — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Pitch | −12〜+12 st | 0 | LIN |
| Formant | −5〜+5 | 0 | LIN |
| Character | Neutral ／ Deep ／ Bright ／ Child | Neutral | STEP |
| Mix | 0〜100 % | 100 % | LIN |
| Keep timing ／ Smooth | 各 Off ／ On | On ／ Off | STEP |

- **遅延：** 512 サンプル。**CPU：** 中。
- **進化機能：** 時間を変えずに声の性格だけを変える。区分 A。
- **要確認：** 既定は 0（画面は +2 st・+1.5）。Δ ボタンが無い。進化機能が「普通の機能」に近いので、別の案（例：話者の声域に合わせた自動補正）を検討する余地あり。

### VO07 Vocal Strip — 2Uラック（bronze）

声の処理を「掃除→音色→ダイナミクス→空間」の正しい順に並べたストリップ。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| HPF | 20〜300 Hz | 80 Hz | LOG |
| De-ess ／ Breath | 各 0〜10 | 0 | LIN |
| Body ／ Presence ／ Air | 各 −6〜+6 dB（200 Hz ／ 3 kHz ／ 12 kHz） | 0 | LIN |
| Comp | 0〜10（光学式1ノブ） | 0 | LIN |
| Level | −12〜+12 dB（コンプ後の音量） | 0 | LIN |
| Plate ／ Echo | 各 0〜10（内蔵の空間の送り量） | 0 | LIN |
| Output | −10〜+10 dB | 0.0 | LIN |

- **DSP：** DY05・EQ05・DY02・RV02・DL01 の処理を簡略版で順に並べる。段の間の音量は自動で整える。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** 正しい処理順と段間の音量合わせ。区分 A。
- **要確認：** 画面の「Level −〜＋」はコンプ後の音量と解釈した。

### VO08 Breath — デジタル

息継ぎを下げる・消す・印だけ付ける。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mode | Reduce ／ Remove ／ Mark only | Reduce | STEP |
| Reduction | 0〜−40 dB | −12 dB | LIN |
| Sensitivity | Low ／ Mid ／ High | Mid | STEP |
| Keep | Natural ／ Less ／ None | Natural | STEP |
| Fade | 1〜50 ms | 10 ms | LOG |

- **DSP：** 息の判定（スペクトルの平たさ、有声音の有無、フレーズの間かどうか）。初期版は規則、後期版で学習モデル。
- **遅延：** 1024 サンプル（息の頭からフェードするための先読み、@48 kHz）。**CPU：** 中。
- **進化機能：** 息に印を付け、フレーズごとに残す・消すを選べる。区分 B（後期に C）。
- **要確認：** フレーズごとの選択には VO01 と同じく音声の取り込みが必要。Δ ボタンが無い。

## RS01〜RS07 修復

修復系は解析の窓が必要なため、STUDIO でも遅延が出る製品が多い。各製品の「Δ ○○ only」ボタンは共通の Δ と同じもの（取り除いた成分だけを聴く）として扱う。

### RS01 Denoise — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Profile | Voice ／ Music ／ Field | Voice | STEP |
| Adaptive（EVO） | Off ／ On | On | STEP |
| Reduction | 0〜−40 dB | −12 dB | LIN |
| Threshold | −10〜+20 dB（ノイズ推定に対する余裕） | +3 dB | LIN |
| Smoothing | Low ／ Mid ／ High | Mid | STEP |
| Low band ／ High band | 各 −20〜+20 dB（帯域ごとの補正） | 0 | LIN |
| Artifact guard | Off ／ On | On | STEP |
| Learn | 学習ボタン（Auto 不可） | — | — |

- **DSP：** 短時間 FFT（2048点、ホップ512）のスペクトル抑圧（SN 比の推定を前フレームから引き継ぐ方式）。Adaptive は最小値統計でノイズを追い続ける。Artifact guard は細かく泡立つ雑音（ミュージカルノイズ）を時間方向の平滑で抑える。
- **遅延：** 2048 サンプル（42.7 ms@48 kHz）。Low lat で 512 点・512 サンプル。**CPU：** 中〜重。
- **進化機能：** Adaptive。ノイズが変わっても学習し直しが要らない。区分 B。

### RS02 Voice Isolate — デジタル

声と、音楽・部屋の響きを分ける。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Voice | 0〜100 % | 100 % | LIN |
| Background | −60〜0 dB | −24 dB | LIN |
| Reverb | −40〜0 dB | −12 dB | LIN |
| Quality | Low ／ Mid ／ High | High | STEP |
| Mode | Real time ／ Offline best | Real time | STEP |

- **DSP：** 学習済みの音源分離モデル（声・その他・残響の3出力）。
- **遅延：** Real time で 1024 サンプル（設計上の見積もり）。**CPU：** 重。
- **進化機能：** 声と音楽と部屋を分離。区分 C（後期）。
- **要確認：** 学習データの権利と収集方法。「Offline best」はプラグインの中では書き出し時（オフライン処理）にしか使えない。ホストがオフラインかどうかの判定（VST3 の処理モード、CLAP の render 拡張）で切り替える。Δ ボタンが無い。

### RS03 Dehum — 2Uラック（anodized）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Base Hz | 50 ／ 60 ／ Auto | 50 | STEP |
| Harmonics | 2 ／ 4 ／ 8 ／ 16 | 8 | STEP |
| Depth | 0〜10（0〜−40 dB） | 5 | LIN |
| Width | Narrow〜Wide | Narrow | LIN |
| Buzz | 0〜10（奇数次・高次を追加で削る） | 0 | LIN |
| Track（EVO） | Off ／ On | On | STEP |

- **DSP：** 基本波と倍音のノッチ列。Track は基本波の周波数を位相ロックで追い（±1 Hz）、ノッチ列ごと動かす。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 電源周波数のゆらぎ追従。区分 B。
- **要確認：** 画面に Unit A／B／C があるが、修復機には意味が無い。外すことを推奨（MS07 と同じ）。

### RS04 Declick — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Target | Click ／ Crackle ／ Both | Both | STEP |
| Sensitivity | Low ／ Mid ／ High | Mid | STEP |
| Click width | 0.1〜5 ms | 1 ms | LOG |
| Crackle | 0〜100 % | 40 % | LIN |
| Low guard | Off ／ On（低域の打撃音を誤検出しない） | On | STEP |

- **DSP：** 自己回帰（AR）モデルの予測誤差で外れ値を検出し、前後から補間して埋める。
- **遅延：** 512 サンプル（補間の先読み）。**CPU：** 中。
- **進化機能：** 直したクリックをすべてスペクトログラムに印で残す。区分 A。
- **要確認：** 画面の「Repair」ボタンの役割（手動で範囲を直す？）が未定。

### RS05 Declip — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Threshold | −3〜0 dB（クリップとみなす値）／ Detect 時は自動 | −0.5 dB | LIN |
| Quality | Low ／ Mid ／ High | High | STEP |
| Makeup | −12〜0 dB（復元後の頭打ちを避ける余裕） | −3 dB | LIN |
| Smooth | Low ／ Mid ／ High | Mid | STEP |
| Detect（EVO） | Off ／ On | On | STEP |

- **DSP：** 天井に張り付いた連続区間を見つけ、AR モデルと滑らかさの制約で元の波形を推定する。
- **遅延：** 1024 サンプル。**CPU：** 中〜重。
- **進化機能：** Detect。サンプル値の分布の端の山からクリップの値を自動で決める。区分 B。

### RS06 Dereverb — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Reduction | 0〜−30 dB | −10 dB | LIN |
| Tail length | 0.1〜5 s | 0.8 s | LOG |
| Early | Keep ／ Reduce | Keep | STEP |
| Smooth | Low ／ Mid ／ High | Mid | STEP |
| Learn room | 学習ボタン（Auto 不可） | — | — |

- **DSP：** 残響時間から後部残響のスペクトルを推定して抑える統計的な方式。
- **遅延：** 1024 サンプル。**CPU：** 中。
- **進化機能：** Learn room。無音の隙間で音が減衰する様子から残響時間を測り、Tail length を書き込む。区分 B。

### RS07 Mouth Noise — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Sensitivity | Low ／ Mid ／ High | Mid | STEP |
| Click size | Small ／ Medium ／ Large | Small | STEP |
| Freq skew | −50〜+50 % | 0 % | LIN |
| Fade | 0.5〜10 ms | 2 ms | LOG |

- **DSP：** RS04 の検出を、声の有無（ピッチの取れない区間・語の間）で重み付けして使う。
- **遅延：** 512 サンプル。**CPU：** 中。
- **進化機能：** 語と語の間のリップノイズだけを見つける。区分 B。

## CR01〜CR06 クリエイティブ

既定値は画面の値（挿してすぐ効果が分かる状態）。テンポ・拍位置はホストの再生情報を使う。

### CR01 Filter — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Type | LP ／ BP ／ HP ／ Notch | LP | STEP |
| Mod source | Envelope ／ LFO ／ Sidechain | Envelope | STEP |
| Cutoff | 20 Hz〜20 kHz | 1.2 kHz | LOG |
| Resonance | 0〜100 % | 60 % | LIN |
| Env amount | −100〜+100 % | +40 % | LIN |
| Drive | 0〜100 % | 20 % | LIN |

- **DSP：** 非線形を含む状態変数フィルタ（2極・4極）、2× OS。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 包絡が叩き手の強弱に追従。直近10秒の入力の強弱の幅を測って包絡の振れ幅を正規化し、音量に関係なく Env amount が全域で効く。区分 B。
- **要確認：** Δ ボタンが無い（CR01〜CR05 共通）。

### CR02 Stutter — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Grid | 1/8 ／ 1/16 ／ 1/32 ／ Triplet | 1/16 | STEP |
| Gate | 0〜100 % | 60 % | LIN |
| Repeat | 1x〜16x | 4x | STEP |
| Pitch | −12〜+12 st | 0 | LIN（整数） |
| Reverse | Off ／ On | Off | STEP |
| Filter | 200 Hz〜20 kHz | 4 kHz | LOG |
| Mix | 0〜100 % | 100 % | LIN |
| Pattern | 16ステップ（プリセット保存、Auto 不可）／ Randomize ／ Clear | — | — |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** ランダムでもノリから外れない。拍頭・裏拍に重みを付けた確率で配置し、入力の立ち上がり位置に寄せる。区分 A。

### CR03 Granular — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mode | Cloud ／ Scatter ／ Glitch | Cloud | STEP |
| Grain | 5〜500 ms | 60 ms | LOG |
| Density | 1〜100 /s | 40 /s | LOG |
| Spray | 0〜100 % | 30 % | LIN |
| Pitch | −24〜+24 st | +5 st | LIN |
| Spread | Mono ／ Narrow ／ Wide | Wide | STEP |
| Mix | 0〜100 % | 50 % | LIN |
| Freeze input | Off ／ On | Off | STEP |
| Harmony（EVO） | Off ／ On | On | STEP |

- **遅延：** 0。**CPU：** 中〜重。
- **進化機能：** 入力の和音（12音の分布）を解析し、粒の音程を和音の構成音に寄せる。区分 B。

### CR04 Freeze — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Trigger | Hold ／ Momentary ／ Auto | Hold | STEP |
| Freeze | Off ／ On | Off | STEP |
| Blur | 0〜100 % | 40 % | LIN |
| Drift | Off ／ Slow ／ Fast | Slow | STEP |
| Mix | 0〜100 % | 50 % | LIN |

- **DSP：** 短時間 FFT でスペクトルを保持し、位相を少しずつ乱して鳴らし続ける。
- **遅延：** 0（原音は遅らせない）。**CPU：** 中。
- **進化機能：** Auto で立ち上がりを検出して取り込む。MIDI ノートでも取り込める。区分 A。

### CR05 Tape Stop — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Action | Stop ／ Start ／ Spin back | Stop | STEP |
| Stop time ／ Start time | 1/16〜2 小節 | 1/2 小節 ／ 1/8 小節 | STEP |
| Curve | Lin ／ Exp ／ Log | Exp | STEP |
| Filter | Off ／ On | On | STEP |
| Trigger | Off ／ On（オートメーションで起動） | Off | STEP |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 停止がちょうど小節線で終わるよう、逆算した位置で開始する。区分 A。

### CR06 One Knob — 2Uラック（anodized）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Effect | Wide ／ Warm ／ Air ／ Punch ／ Space ／ Lo-fi | Air | STEP |
| Amount | 0〜10 | 0 | LIN |
| Mix | Dry〜Wet | 100 % | LIN |
| Output | −10〜+10 dB | 0.0 | LIN |
| Macro | Off ／ On（内部の個別値を表示） | Off | STEP |

- **DSP：** 効果ごとに既存製品の処理を組み合わせた内部チェーン（例：Air＝SA05＋EQ01 の Air 帯、Punch＝DY07＋DY09）。Amount で内部の複数の値を調整済みの曲線で同時に動かす。
- **遅延：** 0。**CPU：** 軽〜中。
- **進化機能：** 1ノブで6種、それぞれに調整した曲線。区分 A。

## IN01〜IN06 インストゥルメント

6本とも MIDI 入力で音を出す楽器プラグイン（VST3 の Instrument、AU の音源タイプ、CLAP のノートポート）。OBS は楽器プラグインを扱わないため STUDIO 専用。既定値は画面の値。オーディオ入力が無いので Δ・Auto gain・Mix は持たない（要確認：共通章との例外として承認が必要）。

### IN01 Synth — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Osc 1 ／ Osc 2 Wave | Saw ／ Square ／ Triangle ／ Sine | Saw | STEP |
| Detune | 0〜50 cent | 7 cent | LIN |
| Sub ／ Noise | 各 0〜100 % | 0 | LIN |
| Cutoff | 20 Hz〜20 kHz | 2.4 kHz | LOG |
| Resonance | 0〜100 % | 30 % | LIN |
| Attack ／ Decay ／ Release | 1 ms〜10 s | 10 ms ／ 400 ms ／ 600 ms | SKW（k=3） |
| Sustain | 0〜100 % | 70 % | LIN |
| Voice mode | Poly ／ Mono ／ Legato（最大16声） | Poly | STEP |

- **DSP：** 帯域制限した波形の発振器（折り返しを抑える方式）、IN 共通のフィルタ（CR01 と同じ）。
- **遅延：** 0。**CPU：** 中（16声）。
- **進化機能：** 2音色のパッチモーフ。区分 A。
- **要確認：** 進化機能が全製品共通の「モーフ」と同じ内容になっている。差を付けるなら、ベロシティやモジュレーションホイールで声ごとにモーフする案。

### IN02 Drums — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Kit | Studio ／ Vintage ／ Electronic | Studio | STEP |
| Tune | −12〜+12 st | 0 | LIN |
| Decay | Short ／ Medium ／ Long | Medium | STEP |
| Velocity | Soft ／ Linear ／ Hard（強さの曲線） | Soft | STEP |
| Room | 0〜100 % | 20 % | LIN |
| Comp | Off ／ On | On | STEP |
| Output | −24〜+12 dB | 0.0 | LIN |
| Humanize style（EVO） | Off ／ Tight ／ Laid back ／ Pushed | Off | STEP |

- **DSP：** 多層サンプル再生（強さの段×連打用の変化）。Pattern と Mixer は画面の切替。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** 叩き手のスタイル別に、拍の中のずれと強弱の傾向を統計の型として持ち、MIDI のタイミングと強さを揺らす。区分 A。
- **要確認：** サンプルは自社収録が必要。

### IN03 Keys — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Instrument | Grand ／ Upright ／ Electric ／ Reed | Electric | STEP |
| Tone | Warm ／ Neutral ／ Bright | Warm | STEP |
| Hammer | Soft ／ Hard | Soft | STEP |
| Pedal noise | 0〜100 % | 30 % | LIN |
| Tremolo ／ Chorus | 各 0〜100 % | 40 % ／ 0 % | LIN |
| Room | 0〜100 % | 25 % | LIN |
| Output | −24〜+12 dB | 0.0 | LIN |

- **DSP：** アコースティックは多層サンプル、エレクトリック・リードは物理モデル＋サンプルの混成。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** ペダル（CC64）の踏み込み・離しと打鍵の強さに合わせて、ペダル音・ハンマー音を鳴らす。区分 A。
- **要確認：** サンプル収録。

### IN04 Bass — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Style | Finger ／ Pick ／ Slap ／ Synth | Finger | STEP |
| Tone | Round ／ Neutral ／ Bright | Round | STEP |
| Pickup | Neck ／ Both ／ Bridge | Neck | STEP |
| Attack ／ Release | Soft・Medium・Hard ／ Short・Long | Medium ／ Short | STEP |
| Amp | DI ／ Amp ／ DI and amp（GT04 を流用） | DI and amp | STEP |
| Auto slide ／ Ghost mute | 各 Off ／ On | Off ／ On | STEP |
| Output | −24〜+12 dB | 0.0 | LIN |

- **遅延：** 0。**CPU：** 中。
- **進化機能：** 強さの小さいノートはゴーストノート、重なったノートはスライドとして鳴らす。区分 A。
- **要確認：** サンプル収録。

### IN05 Organ — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Manual | Upper ／ Lower ／ Pedal（編集対象） | Upper | STEP |
| Drawbar 1〜9（鍵盤ごと） | 各 0〜8 | 888000000（上鍵盤） | STEP |
| Drive | Clean ／ Warm ／ Hot | Warm | STEP |
| Key click | Off ／ Low ／ High | Low | STEP |
| Percussion ／ Vibrato | 各 Off ／ On | On ／ Off | STEP |
| Rotary | Off ／ Slow ／ Fast（MD05 を流用） | Fast | STEP |
| Reverb | 0〜100 % | 15 % | LIN |
| Output | −24〜+12 dB | 0.0 | LIN |

- **DSP：** トーンホイールの加算合成（汎用のモデル）、キークリックとパーカッションの包絡。
- **遅延：** 48 サンプル固定（Rotary のドップラー、MD05 と同じ）。**CPU：** 中。
- **進化機能：** エクスプレッションペダル（CC11）で2つのドローバー設定の間を連続で動かす。区分 A。
- **要確認：** ドローバーの画面は 04 に無い（ディスプレイ部）。

### IN06 Sampler — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Start | 0〜サンプル長 ms | 0 ms | LIN |
| Loop | Off ／ On | On | STEP |
| Pitch | −24〜+24 st | 0 | LIN |
| Filter | 20 Hz〜20 kHz | 8 kHz | LOG |
| Attack ／ Release | 0〜10 s | 2 ms ／ 300 ms | SKW（k=3） |
| Load sample ／ Auto map | ファイル読み込み・解析ボタン（Auto 不可） | — | — |

- **遅延：** 0。**CPU：** 軽〜中。
- **進化機能：** 立ち上がり検出で切り分け、C1 から順に鍵盤へ割り当てる。区分 B。

## MT01〜MT05 計測

計測系は音を変えない（入力をそのまま出す）。解析は UI スレッドで行い、オーディオ処理は計測値の受け渡しだけ。遅延 0・CPU 軽（オーディオ側）。Δ・Auto gain・Unit は意味が無いので持たない。

### MT01 Loudness — デジタル（画面にノブなし、キャンバスから補完）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Preset | ARIB TR-B32 −24 ／ EBU R128 −23 ／ Streaming −14 ／ Custom | ARIB TR-B32 | STEP |
| Target（Custom 時） | −40〜−5 LUFS | −24.0 | LIN |
| Tolerance | ±0.5〜±3 LU（目標帯の表示幅） | ±1 LU | LIN |
| Pause ／ Reset | 計測の一時停止・リセット（Auto 不可） | — | — |

- **表示：** Integrated（目標との差）、Momentary、Short-term、Range（LRA）、True peak、直近10分の推移グラフ。
- **進化機能：** 日本の放送と配信のプリセット。区分 A。
- **要確認：** 画面は「LUFS」、LV23 は「LKFS」。同じ量なので表記を揃える。±1 LU は画面の表示幅で、規格上の許容値かどうかは要確認。

### MT02 Spectrum — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| FFT | 4k ／ 8k ／ 16k ／ 32k 点 | 8k | STEP |
| Speed | Slow ／ Medium ／ Fast | Medium | STEP |
| Range | −60〜−120 dB | −90 dB | LIN |
| Slope | 0〜6 dB/oct（表示の傾き補正） | 4.5 dB/oct | LIN |
| Smoothing | Off ／ 1/24 ／ 1/12 ／ 1/6 ／ 1/3 oct | 1/6 oct | STEP |
| Display | Peak ／ Average ／ Hold | Average | STEP |

- **進化機能：** 参照カーブ（参照曲の長時間平均、またはジャンル別の型）と自分のミックスを重ねて比べる（Compare A）。区分 B。
- **要確認：** ジャンル別の型を持つなら、その元データの作り方。

### MT03 Spectrogram — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Scale | Linear ／ Log ／ Mel | Log | STEP |
| Scroll | 2〜60 s | 10 s | LOG |
| Floor | −60〜−120 dB | −90 dB | LIN |
| Contrast | Low ／ Mid ／ High | Mid | STEP |
| Palette | Mono ／ Heat | Mono | STEP |
| Show notes ／ Show freq | 各 Off ／ On | Off ／ On | STEP |

- **進化機能：** スペクトログラムをクリック（ドラッグ）した帯域だけを、その場でバンドパスして試聴する。区分 A。
- **要確認：** 試聴中は出力が変わるため、監視用として書き出し時の警告が必要。Heat パレットは状態色（緑・黄・赤）と混ざらない配色にする。

### MT04 Phase Scope — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Persistence | 0.1〜5 s | 1 s | LOG |
| Zoom | 1x ／ 2x ／ 4x ／ 8x | 1x | STEP |

- **進化機能：** 帯域ごとの相関を追い、どこかの帯域で相関が負の状態が1秒以上続きそうになったら警告する。区分 A。

### MT05 Vu Ppm — 2Uラック（brushed）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Ref dBFS | −14 ／ −18 ／ −20（0 VU の基準） | −18 | STEP |
| Meter | VU ／ PPM | VU | STEP |

- **表示：** VU は立ち上がり約 300 ms の動き。PPM の種類（立ち上がりと戻りの規格）は要決定。
- **進化機能：** 0 VU の基準をプロジェクト単位で持つ。SW Link で同じセッションの MT05 同士が基準を共有する。区分 A。
- **要確認：** 画面に Δ・Auto・Unit があるが計測器には意味が無い。外すことを推奨。

## UT01〜UT03 ユーティリティ

03 ではカテゴリが「Meter」だが、01・05 では UT は Creative・Instrument（CR・IN・UT）。どちらに揃えるかは確認事項に回す。

### UT01 Gain — 2Uラック（anodized）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Gain | −24〜+24 dB | 0.0 | LIN |
| Balance | L〜R | 中央 | LIN |
| Width | 0〜200 % | 100 % | LIN |
| Ø L ／ Ø R ／ Swap ／ Mono | 各 Off ／ On | Off | STEP |
| Channel | Both ／ L only ／ R only | Both | STEP |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** トラックの種類ごとの音量設定を覚える。ホストからトラック名を受け取り（VST3 のトラック情報、CLAP の track-info 拡張）、同じ種類のトラックに挿したとき前回の Gain を提案する。区分 A。
- **要確認：** 共通の Auto gain は UT01 の Gain を打ち消してしまうので、UT01 では外す。Unit も意味が無いので外すことを推奨。

### UT02 Mono Check — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Listen | Stereo ／ Mono ／ Side ／ Left ／ Right | Mono | STEP |
| Mono fold | −6〜0 dB（モノ化の補正） | −3 dB | LIN |
| Phone speaker | Off ／ On（LO01 と同じ模擬） | Off | STEP |
| Low cut | Off＋20〜300 Hz | Off | LOG |
| Level | −24〜+24 dB | 0.0 | LIN |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** スマホのスピーカーの模擬試聴。区分 A。
- **要確認：** 監視専用なので、Stereo 以外のまま書き出すと警告を出す。

### UT03 Reference — デジタル

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Source | A Mix ／ B Ref 1 ／ C Ref 2 | A Mix | STEP |
| Loudness match（EVO） | Off ／ On | On | STEP |
| Crossfade | 0〜500 ms | 50 ms | LIN |
| Loop | 参照曲の区間（Intro ／ Verse ／ Chorus ／ 任意） | Chorus | STEP |
| Sync play | Off ／ On（ホストの再生位置に追従） | On | STEP |
| Level | −24〜+24 dB | 0.0 | LIN |

- **DSP：** 参照曲は WAV・AIFF・FLAC・MP3 を読み込み、ホストのサンプルレートに変換して再生。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** ラウドネスを揃えて A/B する。大きい方が良く聞こえる錯覚を防ぐ（画面の Match −1.2 LU は補正量の表示）。区分 A。

## LIVE 共通章

LIVE 30本は、共通章に加えて次の決まりに従う。OBS では専用の「SW AUDIO for OBS」で動かす（下の1、決定）。

### 1. OBS で使う方法：SW AUDIO for OBS（決定 2026-10-05）

OBS 用の橋渡しを自社で作る。OBS 本体の VST フィルタは VST 2.x のみで VST3 は対象外（[OBS 公式ナレッジベース](https://obsproject.com/kb/vst-2-x-plugin-filter)）、本体への VST3 ホスト追加は 2026年7月末時点でもレビュー中（[obs-studio PR #12752](https://github.com/obsproject/obs-studio/pull/12752)）。第三者の橋渡し（[atkAudio の解説](https://www.elgato.com/us/en/explorer/news/streaming/how-to-use-vst3-plugins-in-obs-studio/)、[OBS Safe VST3 Host](https://github.com/masarray/obs-vst3)）に頼ると、SW Link・シーン連動・MIDI・映像連携が成り立たない。

| 部品 | 中身 | ライセンス |
| --- | --- | --- |
| OBS フィルタプラグイン | OBS のフィルタ一覧に「SW AUDIO」を出す薄い部品。音声ブロックをエンジンに渡して受け取るだけ。シーン変更と、LV19 用の映像フレームも OBS から受け取る | GPL（ソース公開） |
| SW AUDIO エンジン | 別プロセス。DSP コアを直接組み込み（VST3 を経由しない）、全インスタンスを1プロセスで動かす。プラグイン画面、LV28 のリモートサーバー、LV30 の録音、MIDI 入力もここ | 自社（非公開） |
| DSP コア | VST3／AU／CLAP 版とエンジンで同じものを使う。フレームワークに依存しない C++ ライブラリにする | 自社（非公開） |

**通信と安全**

- 音声は共有メモリで同期的に受け渡す（追加遅延 0 が目標）。制御はローカルのソケット。渡すのは音声と公開パラメータだけで、内部のデータ構造は渡さない。
- エンジンが時間内に返さない・落ちた場合は原音を通し（フェイルオープン）、エンジンを自動で再起動して状態を戻す。配信を止めないことを最優先にする。
- 往復にかかった時間は EVO バーの CPU % に含めて表示する。

**ライセンスの考え方：** FSF の FAQ は、動的リンクで関数呼び出しやデータ構造を共有するプラグインは1つのプログラムとみなす一方、パイプやソケットでの通信は通常は別プログラムとしている（[GPL FAQ](https://www.gnu.org/licenses/gpl-faq.html)）。この構成はその考え方に沿う。ただし通信の中身が密接すぎると結合とみなされうるとも書かれているため、公開前に弁護士の確認を取る。

**この方式で解決する項目：** SW Link（LV05・LV11・LV15・LV27・LV29）、シーン連動（LV27 が OBS から直接受け取り、obs-websocket が不要）、MIDI（LV25、エンジンが MIDI 機器を直接開く）、映像（LV19 の手拍子の自動検出）。

**対象と順番（案）**

1. 対象 OS は Windows・macOS（Linux は後回し）。対象製品は LIVE 30本＋配信でよく使う STUDIO（声・修復・ダイナミクス）。DSP コアが共通なので、全製品に広げるのは対応表と試験の追加だけ。
2. VST3 版も、第三者の橋渡しで基本動作を確認しておく（すでに導入済みの人向け）。
3. OBS 本体が VST3 に対応したら、VST3 版もそのまま使える。

**05 との関係：** 05 は「OBS ネイティブは GPL でソース公開が必要」として避けた。この案で公開するのは薄い OBS プラグインだけで、DSP 本体は公開しない。ソース公開を避けるという 05 の理由は守ったまま、OBS 対応を確保する。

### 2. 処理の決まり

- **遅延：** 0 が原則。遅延が出る処理は、ツールバーの「LIVE x.x ms」に常に表示する（画面どおり）。
- **CPU 表示：** EVO バーの CPU % は実測値（1ブロックの処理時間 ÷ ブロックの長さ）。設計上の見積もりではない。
- **リアルタイム安全：** 処理中のメモリ確保・ファイル操作・ロック待ちをしない。ログや録音の書き込みは別スレッドに渡す。非正規化数を出さない。
- **値の表示：** LED リング（32分割）には必ず数値を併記。クリップは赤ではなく反転点滅。
- **Lock：** 画面の操作をすべて止める（オートメーション・Remote・Scene の切替は通す）。ただし LV11 の咳ボタンなど、本番中に押す前提のボタンは Lock 中も効く。
- **Scene：** プリセットのスロット。LV27 が OBS のシーンに合わせて切り替える。切替は 10〜300 ms のクロスフェード。
- **Remote：** LV28 のハブ経由で、同じ LAN のタブレットから操作する。
- **チップ：** 高さ 44 px 以上（02 の live-chip）。

### 3. カテゴリ

03 で LV02〜LV06 のカテゴリが空欄。本書は LV02＝Broadcast、LV03＝EQ、LV04・LV06＝Dynamics・Mastering、LV05＝Broadcast と仮置きする（色と型番バッジに影響）。

### 4. LIVE の書き方

表の列は SA 以降と同じ。LED リングのパラメータは連続値で、画面の値を既定にする（本番直前の標準状態として描かれているため）。

## LV01〜LV10

### LV01 Voice — 声の1ノブストリップ（画面にノブなし、キャンバスから補完）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Use | Narration ／ Stream ／ Meeting ／ Singing | Stream | STEP |
| Voice | 0〜100 %（処理全体のかかり具合） | 62 % | LIN |
| Mute | 押している間ミュート（Lock 中も有効、Auto 不可） | — | — |

- **内部の段（表示のみ）：** Noise（多帯域エキスパンダー）→ EQ → Comp → Limit（−1 dBTP 目標、先読みなし）。各段の値は Voice と Use から決まり、画面に数値で出す。
- **遅延：** 0（ノイズ処理を FFT ではなく遅延0の多帯域エキスパンダーで行う）。**CPU：** 中。
- **進化機能：** 1ノブで4段を同時に、用途別の曲線で動かす。区分 A。

### LV02 Feedback — ハウリング抑制

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Sensitivity | Low ／ Mid ／ High | High | STEP |
| Max depth | −3〜−24 dB | −12 dB | LIN |
| Width | 1/3〜1/20 oct | 1/10 oct | LOG |
| Release | 1〜60 s（LIVE フィルタが戻る時間） | 8 s | LOG |
| Ring out ／ Lock filters ／ Clear live | 操作ボタン（Auto 不可） | — | — |

- **DSP：** フィルタ12本（F1〜F12）。FIXED（固定）と LIVE（本番中に自動）。検出は別スレッドの FFT 解析（ピークの持続と倍音関係で判定）、削りは IIR のノッチなので遅延0。
- **遅延：** 0。**CPU：** 軽〜中。
- **進化機能：** Ring out。本番前にゲインを上げて鳴りやすい周波数を探し、FIXED に登録する。区分 B。

### LV03 Channel — ライブ用チャンネルストリップ

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mic | Handheld ／ Lavalier ／ Headset ／ Podium | Handheld | STEP |
| Trim | −20〜+40 dB | +6 dB | LIN |
| HPF | Off＋20〜400 Hz | 80 Hz | LOG |
| Ø | Off ／ On | Off | STEP |
| Gate Thresh ／ Range | −80〜0 dB ／ 0〜−80 dB | −42 ／ −30 | LIN |
| EQ Low ／ Mid ／ High | 各 −12〜+12 dB | −2 ／ +2 ／ +1.5 | LIN |
| EQ Mid f | 200 Hz〜8 kHz | 1.2 kHz | LOG |
| Feedback guard | Off ／ On（LV02 の LIVE フィルタ4本分） | On | STEP |
| Comp Thresh ／ Ratio | −40〜0 dB ／ 1〜10 | −18 ／ 3 | LIN ／ LOG |
| De-ess Amount ／ Freq | 0〜12 dB ／ 3〜12 kHz | 4 dB ／ 6.5 kHz | LIN ／ LOG |
| Out | −20〜+10 dB | −2.0 dB | LIN |

- **遅延：** 0（ディエッサーは先読みなし）。**CPU：** 中。
- **進化機能：** マイクの種類を選ぶとストリップ全体を書き込む。Copy／Paste で他チャンネルへ。区分 A。

### LV04 Safety limiter — 安全リミッター

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mode | Zero（遅延0、標本値のピーク）／ True peak（1.5 ms 先読み） | Zero | STEP |
| Ceiling | −12〜0 dBFS（True peak 時は dBTP） | −1.0 | LIN |
| Release | 10〜1000 ms | 50 ms | LOG |
| RMS limit | −20〜0 dB（Ceiling に対する長時間の上限、スピーカー保護） | −6 dB | LIN |
| Subsonic | Off＋20〜60 Hz | 30 Hz | LOG |

- **遅延：** Zero 0、True peak 72 サンプル（ツールバーに表示）。**CPU：** 軽〜中。
- **進化機能：** 制限が働くたびに時刻・GR・長さを記録し（画面の Last limit）、CSV で書き出せる。区分 A。

### LV05 Auto ducker — 自動ダッカー

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Key | SW Link のインスタンス選択（例：LV01 Voice）／ 外部サイドチェーン | 未選択 | — |
| Depth | 0〜−40 dB | −12 dB | LIN |
| Attack | 1〜500 ms | 80 ms | LOG |
| Hold | 0〜5 s | 1.2 s | LIN |
| Release | 0.1〜10 s | 2.0 s | LOG |
| Voice only（EVO） | Off ／ On | On | STEP |
| Hold to duck | 押している間下げる（Auto 不可） | — | — |

- **遅延：** 0（判定の遅れは Attack に含まれる）。**CPU：** 軽。
- **進化機能：** キー信号が人の声のときだけ下げる（有声音の判定とスペクトルの形）。拍手や物音では下げない。初期は規則、後期に学習モデル。区分 B。

### LV06 Stream master — 配信の最終段

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Target | Stream −14 ／ Podcast −16 ／ Broadcast −24 ／ 任意（−30〜−5 LUFS） | −14 | STEP |
| Ride speed | Slow ／ Medium ／ Fast | Slow | STEP |
| Max boost | 0〜+12 dB | +6 dB | LIN |
| Ceiling | −3〜0 dB | −1.0 | LIN |
| Mono safe | Off ／ On | Off | STEP |

- **DSP：** ゆっくり動く自動ゲイン（−6〜+6 dB を表示）→ LV04 の Zero モード。「Dead air OK」は無音検知の表示。
- **遅延：** 0。**CPU：** 軽〜中。
- **進化機能：** 配信先のラウドネス目標に向けて自動で寄せる。区分 B。
- **要確認：** 画面は Ceiling を「dBTP」と表示しているが、遅延0では標本間ピークまでは保証できない。dBTP を保証するなら 1.5 ms の遅延を出して表示するか、表記を dBFS にする。

### LV07 Speech Agc — 話者レベラー

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Use | Speech ／ Panel ／ Lecture | Speech | STEP |
| Target | −30〜−10 LUFS | −18 | LIN |
| Max gain | 0〜+24 dB | +12 dB | LIN |
| Speed | Slow ／ Medium ／ Fast | Medium | STEP |
| Gate | −70〜−30 dB | −50 dB | LIN |
| Talker hold ／ Freeze | 各 Off ／ On | On ／ Off | STEP |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** マイクに近い人・遠い人を、直接音と残響の比と高域の落ち方で見分け、音量と明瞭度（中高域）を補う。区分 B。

### LV08 Room Noise — 室内ノイズ抑制

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Reduction | 0〜−30 dB | −18 dB | LIN |
| Sensitivity | Low ／ Mid ／ High | Mid | STEP |
| Voice guard | Low ／ Mid ／ High | High | STEP |
| Keyboard ／ HVAC | 各 Off ／ On | On | STEP |
| Learn noise | 学習ボタン（Auto 不可） | — | — |

- **DSP：** 短い窓のスペクトル抑圧（空調などの定常ノイズ）＋キー打鍵の瞬間的な音の検出と抑制。
- **遅延：** 256 サンプル（5.3 ms、画面表示どおり）。**CPU：** 中。
- **進化機能：** 空調とキーボードの音を学習。区分 B（後期に C）。

### LV09 Hum Cut — ハム除去

RS03 と同じ処理の LIVE 版。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Base | 50 Hz ／ 60 Hz ／ Auto | Auto | STEP |
| Harmonics | 1〜16 | 8 | STEP |
| Depth | 0〜−40 dB | −30 dB | LIN |
| Width | Narrow ／ Medium ／ Wide | Narrow | STEP |
| Track drift（EVO） | Off ／ On | On | STEP |
| Listen | 取り除いた成分の試聴（Auto 不可） | Off | STEP |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 電源周波数のゆらぎをその場で追う。区分 B。

### LV10 Voice Fx — ライブ用ボイスチェンジャー

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Preset | Low ／ High ／ Robot ／ Radio ／ Anon | なし | STEP |
| Pitch | −12〜+12 st | −3 st | LIN |
| Formant | −5〜+5 | +2 | LIN |
| Robot | Off ／ On | Off | STEP |
| Mix | 0〜100 % | 100 % | LIN |
| Monitor | Off ／ On（本人の返しにも通す） | On | STEP |

- **DSP：** VO02 の低遅延音程エンジン。
- **遅延：** 128 サンプル（2.7 ms@48 kHz、設計上の見積もり）。
- **CPU：** 中。
- **進化機能：** Anon。音程・フォルマント・スペクトルのぼかしを組み合わせ、声から本人を特定しにくくする。区分 A。
- **要確認：** 画面は「LIVE 0.0 ms」だが、音程変換は遅延0にできない。表示を実際の値にする。また、声の加工は元に戻される可能性があり、匿名化を保証できない。取材用途の説明文で保証しない旨を明記する必要がある。

## LV11〜LV20

### LV11 Mic Switch — マイクのオンオフと咳ボタン

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mic | Live ／ Push to talk ／ Off | Live | STEP |
| Auto mute | Off ／ On silence | On silence | STEP |
| Silence | −70〜−30 dB | −48 dB | LIN |
| Hold | 0.5〜10 s | 3.0 s | LIN |
| Fade | 5〜200 ms | 20 ms | LOG |
| Duck others | 0〜−30 dB（SW Link の他インスタンスを下げる） | −10 dB | LIN |
| Hold to cough | 押している間ミュート（Lock 中も有効、Auto 不可） | — | — |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 無音が続いたら自動ミュート、咳ボタン。区分 A。

### LV12 Geq 31 — 31バンドグラフィックEQ

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Band 20 Hz〜20 kHz（1/3 oct の31本） | 各 −12〜+12 dB | 0.0 | LIN |
| Edit | Left ／ Right ／ Both | Both | STEP |
| Link L/R | Off ／ On | On | STEP |
| HPF ／ LPF | Off＋20〜200 Hz ／ 5〜20 kHz＋Off | 40 Hz ／ 18 kHz | LOG |
| Output | −12〜+12 dB | 0.0 | LIN |
| RTA overlay ／ Feedback guard | 各 Off ／ On | On | STEP |
| Flat | 全バンドを 0 に戻す（Auto 不可） | — | — |

- **DSP：** 定Q（約 4.3）のベル31本×2ch。
- **遅延：** 0。**CPU：** 中。
- **進化機能：** Feedback guard。鳴り始めた帯域のフェーダーに印を付ける（自動では削らない）。区分 B。

### LV13 Live Peq — ライブ用パラメトリックEQ

下表は n = 1〜6。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Band n Type | Bell ／ Shelf | Bell | STEP |
| Band n Freq | 20 Hz〜20 kHz | 100 Hz〜10 kHz に分散 | LOG |
| Band n Gain | −15〜+15 dB | 0.0 | LIN |
| Band n Q | 0.3〜10 | 2.0 | LOG |
| HPF ／ LPF | Off＋20〜400 Hz ／ 5〜20 kHz＋Off | 90 Hz ／ 18 kHz | LOG |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 実時間アナライザーを重ね、突出した山に「ここを削る」候補を出す。区分 B。

### LV14 Align — スピーカーの時間合わせ

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Delay | 0〜500 ms（0.01 ms 刻み） | 0 ms | SKW（k=2） |
| Distance | Delay と連動（m） | — | — |
| Air temp | −10〜40 ℃（音速 ＝ 331.5 ＋ 0.6 × 温度 m/s） | 22 ℃ | LIN |
| Polarity | Normal ／ Invert | Normal | STEP |
| Measure | 測定ボタン（Auto 不可） | — | — |

- **DSP：** 遅延線。Measure は、メイン系をサイドチェーンで受け、測定マイクとの相互相関で遅延を出す。
- **遅延：** 0 を報告する。この遅延は効果そのもので、ホストに遅延補正されると意味が無くなるため。**CPU：** 軽。
- **進化機能：** 1クリック測定。区分 B。

### LV15 Auto Mixer — ゲインシェア式オートミキサー

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mode | Gain share ／ Gate | Gain share | STEP |
| Last mic hold | Off ／ On | On | STEP |
| Off atten | −40〜0 dB | −15 dB | LIN |
| Response | Slow ／ Medium ／ Fast | Fast | STEP |
| Priority | なし ／ Mic 1〜8 | Mic 1 | STEP |
| NOM limit | 1〜8 本（同時に開く上限） | 4 | STEP |

- **DSP：** 最大8マイク。OBS では1ソースに1インスタンスなので、各マイクのインスタンスが SW Link で1つの計算に参加し、自分の分のゲインだけをかける。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 最後に話した人のマイクを開けておく、開く本数の上限。区分 A。
- **要確認：** SW AUDIO for OBS（LIVE 共通章の1）なら全インスタンスが同じエンジンに入るので成立する。第三者の橋渡し経由では届かない場合がある。

### LV16 Live Gate — ライブ用ゲート／ダッカー

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mode | Gate ／ Duck | Gate | STEP |
| Threshold | −80〜0 dB | −42 dB | LIN |
| Range | 0〜−80 dB | −40 dB | LIN |
| Hold | 0〜2000 ms | 80 ms | SKW（k=3） |
| Release | 5〜4000 ms | 250 ms | LOG |
| Key HPF（EVO） | Off ／ On | On | STEP |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** キーフィルターで舞台の床鳴り・低い振動を無視する。On の間、本番前に測った床鳴りの帯域より上にハイパスを自動で置く。区分 A。

### LV17 Bus Comp — ライブ用バスコンプ

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mode | Speech ／ Music ／ Band | Speech | STEP |
| Threshold | −40〜0 dB | −20 dB | LIN |
| Ratio | 1〜10 | 3 | LOG |
| Attack | 0.1〜100 ms | 10 ms | SKW（k=3） |
| Release | 10〜2000 ms ／ Auto | Auto | SKW（k=3） |
| Makeup | 0〜+20 dB | +3 dB | LIN |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** OBS のシーン切替で Mode が変わる（LV27 の対応表を使う）。区分 A。

### LV18 Pop Guard — ポップ・ノイズ防止

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Sensitivity | Low ／ Mid ／ High | High | STEP |
| Mute time | 10〜200 ms | 40 ms | LOG |
| Plug pop ／ Wind ／ Handling ／ Plosive | 各 Off ／ On | On ／ On ／ On ／ Off | STEP |

- **DSP：** 直流の段差・急な低域の塊・広帯域の衝撃を判定し、短いフェードで下げる。「Caught today」は検出回数の表示。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 抜き差し・持ち替えのノイズを種類別に捕まえる。区分 B。
- **要確認：** 遅延0では検出までの最初の 1〜2 ms は通ってしまう。2 ms の先読みを選べるようにするか。

### LV19 Av Sync — 音声と映像のずれ補正

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Frame rate | 24 ／ 25 ／ 29.97 ／ 30 ／ 59.94 fps | 29.97 | STEP |
| Delay | 0〜1000 ms | 0 ms | LIN |
| Frames | Delay と連動（fr） | — | — |
| Lock to video | Off ／ On（フレーム単位に丸める） | On | STEP |
| Clap sync | 測定ボタン（Auto 不可） | — | — |

- **遅延：** 0 を報告する（LV14 と同じ理由）。**CPU：** 軽。
- **進化機能：** 手を叩いてずれを測る。区分 B。
- **要確認：** SW AUDIO for OBS では OBS プラグインが映像も受け取れるので、映像側の手拍子も自動で検出できる。VST3 版は映像を見られないため、プレビューで手が合わさった瞬間にユーザーが押す半自動にする。

### LV20 Rta — 実時間アナライザー

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Resolution | 1/3 ／ 1/6 ／ 1/12 oct | 1/3 | STEP |
| Speed | Slow ／ Medium ／ Fast | Medium | STEP |
| Peak hold | 0〜10 s | 2 s | LIN |
| Weight | Z ／ A ／ C | Z | STEP |
| Pink ref ／ Freeze | 各 Off ／ On | On ／ Off | STEP |

- **遅延：** 0（音は素通し）。**CPU：** 軽。
- **進化機能：** ピンクノイズを流したときの理想の線を重ね、部屋の偏りを読みやすくする。区分 A。

## LV21〜LV30

### LV21 Test Gen — テスト信号発生器

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Signal | Sine ／ Pink ／ White ／ Sweep ／ Polarity（極性確認用パルス） | Sine | STEP |
| Freq | 20 Hz〜20 kHz | 1 kHz | LOG |
| Level | −60〜0 dBFS | −20 dBFS | LIN |
| Sweep time | 1〜60 s | 10 s | LOG |
| Left ／ Right | 各 Off ／ On | On | STEP |
| Arm ／ Output | 2段階の出力操作（Auto 不可） | Off | — |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** Arm してから Output を押すまで音を出さない。読み込み時・セッション復元時は必ず Off、出力開始は 0.5 s で音量を上げる、60 秒で自動停止（案）。区分 A。

### LV22 Polarity — 極性チェッカー

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Mode | Mic vs mic ／ Speaker ／ Line | Mic vs mic | STEP |
| Window | 50〜1000 ms | 200 ms | LOG |
| Hold result | Off ／ On | On | STEP |

- **DSP：** 基準（サイドチェーン、または LV21 の Polarity パルス）との相関の符号で判定。音は素通し。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 瞬時の同相・逆相判定。区分 B。

### LV23 Loudness — 放送用ラウドネスメーター（画面にノブなし、キャンバスから補完）

MT01 と同じ計測エンジンの LIVE 版。

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Preset | ARIB −24 ／ EBU −23 ／ Stream −14 | ARIB −24 | STEP |
| Reset ／ Export log | 操作ボタン（Auto 不可） | — | — |

- **表示：** Integrated（目標との差）、Momentary、Short-term、Range、True peak。「Dead air OK」（無音なし）「No TP over」（True peak 超過なし）の状態表示。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** ARIB TR-B32 向けのログ書き出し。1秒ごとの計測値を時刻つきで別スレッドに記録し、CSV で書き出す。区分 A。
- **要確認：** 書き出す項目と書式を、納品先が求める様式に合わせる必要がある（様式の確認が未実施）。表記 LKFS／LUFS の統一（MT01 と同じ）。

### LV24 Live Reverb — 低負荷リバーブ

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Type | Vocal hall ／ Room ／ Plate | Vocal hall | STEP |
| Decay | 0.3〜5 s | 1.8 s | LOG |
| Pre-delay | 0〜200 ms | 30 ms | SKW（k=2） |
| Tone | Warm ／ Neutral ／ Bright | Warm | STEP |
| Mix | 0〜100 % | 18 % | LIN |
| Duck（EVO） | Off ／ On（話している間は残響を下げる） | Off | STEP |

- **DSP：** 8本の遅延線の FDN（RV01 の縮小版）。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 低負荷と、話している間のダッキング。区分 A。
- **要確認：** 進化機能の文言と画面に「1% CPU」とある。実測前の数値なので、販売コピーと同じ扱いで外す（「Low CPU」などに）。

### LV25 Live Delay — タップテンポのディレイ

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Clock | Tap ／ MIDI ／ BPM | Tap | STEP |
| Time | 1〜2000 ms | 500 ms | LOG |
| Feedback | 0〜95 % | 35 % | LIN |
| Tone | Dark ／ Neutral ／ Bright | Dark | STEP |
| Mix | 0〜100 % | 15 % | LIN |
| Tap | タップボタン（Auto 不可） | — | — |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** タップか MIDI クロックで時間を合わせる。バイパスしても残りの返りは鳴り終わるまで出す（プラグイン内のバイパスで入力だけ止める）。区分 A。
- **要確認：** OBS は MIDI をプラグインに渡さないが、SW AUDIO for OBS ではエンジンが MIDI 機器を直接開いて受ける（VST3 版を第三者の橋渡しで使う場合は MIDI クロック不可）。また OBS 側でフィルタを無効にすると処理自体が呼ばれず、残響は切れる。

### LV26 Mono — モノ互換の安全装置

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Width | 0〜200 % | 100 % | LIN |
| Low mono | Off＋20〜300 Hz | 120 Hz | LOG |
| Mono check | Off ／ On（監視用、Auto 不可） | Off | STEP |
| Auto phase fix（EVO） | Off ／ On | On | STEP |

- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 帯域ごとに相関を見て、モノで打ち消し合う帯域だけ S を減らす（スマホのモノ再生の視聴者向け）。区分 B。

### LV27 Scene Sync — OBS シーンとプリセットの連動（画面にノブなし、キャンバスから補完）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Follow scenes | Off ／ On | On | STEP |
| Fade between | 0〜1000 ms | 300 ms | LIN |
| 対応表 | OBS シーン → SW プリセット（プリセット保存、Auto 不可） | — | — |
| Learn current | 今のシーンに今の設定を登録（Auto 不可） | — | — |

- **DSP：** 番組出力のシーン変更を受け取り（SW AUDIO for OBS は OBS から直接、VST3 版は obs-websocket 経由）、SW Link で各インスタンスに Scene の切替を送る。音の処理は素通し。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** OBS のシーンに合わせてプリセットを呼び出す。区分 A。
- **要確認：** VST3 版で使う obs-websocket の対応版と、接続パスワードの保存方法。

### LV28 Remote Hub — タブレット遠隔操作（画面にノブなし、キャンバスから補完）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Allow control | Off ／ On | On | STEP |
| Require PIN | Off ／ On | On | STEP |
| 端末ごとの権限 | Control ／ View only | View only | STEP |
| Lock all | 全インスタンスを Lock（Auto 不可） | — | — |

- **DSP：** プラグイン内で LAN 向けの HTTP／WebSocket サーバーを立てる（画面の例は 8640 番ポート）。タブレットのブラウザから開く。音の処理は素通し。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** PIN で守られたタブレット操作。区分 A。
- **要確認：** 安全設計が必須。PIN の桁数と試行回数制限、接続ごとの一時トークン、LAN 外からの接続拒否、OS のファイアウォール許可の案内。通信の暗号化（LAN 内の証明書の扱い）は未決定。

### LV29 Interp Mix — 同時通訳のミックス

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Output | Floor ／ Interp and floor ／ Interp | Interp and floor | STEP |
| Floor under | −40〜0 dB | −14 dB | LIN |
| Crossfade | 50〜2000 ms | 400 ms | LOG |
| Interp level | −20〜+10 dB | 0 dB | LIN |
| Auto detect（EVO） | Off ／ On | On | STEP |

- **DSP：** 会場音（本線）と通訳音声（サイドチェーンまたは SW Link）を混ぜる。
- **遅延：** 0。**CPU：** 軽。
- **進化機能：** 通訳者が話している間だけ会場音を下げる（LV05 の声判定を流用）。区分 B。

### LV30 Recorder — 常時バックアップ録音（画面にノブなし、キャンバスから補完）

| 名前 | 範囲・単位 | 既定 | カーブ |
| --- | --- | --- | --- |
| Format | WAV ／ FLAC、44.1 ／ 48 kHz、16 ／ 24 bit ／ 32 bit float | WAV 48k 24-bit | STEP |
| Auto start | Off ／ On | On | STEP |
| Split hourly | Off ／ On | On | STEP |
| 保存先 | フォルダ指定（Auto 不可） | 書類フォルダ | — |
| Mark | 印を打つ（Auto 不可、Lock 中も有効） | — | — |

- **DSP：** オーディオ処理側はロックフリーのバッファに書くだけ。別スレッドがディスクに書き、ヘッダーを定期的に更新して、落ちても直前まで残す。4 GB を超える場合は RF64 形式。印はファイル内の目印と CSV の両方に残す。音は素通し。
- **遅延：** 0。**CPU：** 軽（ディスク書き込みは別スレッド）。
- **進化機能：** 常時録音と印付け。区分 A。
- **要確認：** 空き容量が少ないときの動作（警告・古いファイルの扱い）。

## 残り107製品の一覧

既定の設定で遅延が出るのは18本（揺れの中心遅延 48 サンプルが4本、音程処理5本、修復6本、MD02・VO08・LV08）。EVO は A 69本、B 37本、C 1本。全139本では A 89本、B 49本、C 1本。遅延の一部とCPUは設計上の見積もり。

| コード | 名前 | 遅延（サンプル@48 kHz） | CPU目安 | EVO区分 |
| --- | --- | --- | --- | --- |
| SA01 | Tape | 48 | 中 | A |
| SA02 | Console Sum | 0 | 軽 | A |
| SA03 | Tube | 0 | 中 | A |
| SA04 | Transformer | 0 | 中 | A |
| SA05 | Exciter | 0 | 中 | B |
| SA06 | Saturator | 0 | 中 | A |
| SA07 | Lo-Fi | 48 | 軽 | A |
| SA08 | Bitcrush | 0 | 軽 | A |
| LO01 | Low Harm | 0 | 軽 | A |
| LO02 | Sub Gen | 0 | 中 | B |
| LO03 | Low Focus | 0 | 軽 | B |
| GT01 | Amp | 0 | 中 | A |
| GT02 | Cab Ir | 0 | 中 | A |
| GT03 | Pedalboard | 0 | 中 | A |
| GT04 | Bass Amp | 0 | 中 | A |
| GT05 | Reamp | 0 | 軽 | B |
| RV01 | Hall | 0 | 中 | A |
| RV02 | Plate | 0 | 中 | A |
| RV03 | Spring | 0 | 中 | A |
| RV04 | Convolution | 0 | 重 | A |
| RV05 | Chamber | 0 | 中 | A |
| RV06 | Shimmer | 0 | 中 | A |
| RV07 | Early | 0 | 軽 | A |
| RV08 | Gated | 0 | 中 | B |
| DL01 | Echo | 0 | 軽 | A |
| DL02 | Tape Echo | 0 | 中 | A |
| DL03 | Bbd | 0 | 中 | A |
| DL04 | Multitap | 0 | 軽 | A |
| DL05 | Reverse | 0 | 軽 | A |
| MD01 | Chorus | 0 | 軽 | A |
| MD02 | Flanger | 480（Through zero On）／ 0 | 軽 | A |
| MD03 | Phaser | 0 | 軽 | B |
| MD04 | Tremolo Pan | 0 | 軽 | A |
| MD05 | Rotary | 48 | 中 | A |
| MD06 | Freq Shift | 0 | 軽 | B |
| MD07 | Ensemble | 0 | 軽〜中 | A |
| ST01 | Imager | 0 | 軽 | A |
| ST02 | Mid Side | 0 | 軽 | B |
| ST03 | Phase Align | 0 | 軽 | B |
| ST04 | Center | 0 | 軽 | A |
| ST05 | Phones | 0 | 中〜重 | A |
| ST06 | Mono Low | 0 | 軽 | A |
| VO01 | Tune | 512 | 重 | B |
| VO02 | Tune Rt | 128 | 中 | B |
| VO03 | Harmony | 512 | 重 | B |
| VO04 | Doubler | 0 | 中 | A |
| VO05 | Rider | 0 | 軽 | B |
| VO06 | Formant | 512 | 中 | A |
| VO07 | Vocal Strip | 0 | 中 | A |
| VO08 | Breath | 1024 | 中 | B（後期 C） |
| RS01 | Denoise | 2048（Low lat 512） | 中〜重 | B |
| RS02 | Voice Isolate | 1024 | 重 | C |
| RS03 | Dehum | 0 | 軽 | B |
| RS04 | Declick | 512 | 中 | A |
| RS05 | Declip | 1024 | 中〜重 | B |
| RS06 | Dereverb | 1024 | 中 | B |
| RS07 | Mouth Noise | 512 | 中 | B |
| CR01 | Filter | 0 | 軽 | B |
| CR02 | Stutter | 0 | 軽 | A |
| CR03 | Granular | 0 | 中〜重 | B |
| CR04 | Freeze | 0 | 中 | A |
| CR05 | Tape Stop | 0 | 軽 | A |
| CR06 | One Knob | 0 | 軽〜中 | A |
| IN01 | Synth | 0 | 中 | A |
| IN02 | Drums | 0 | 中 | A |
| IN03 | Keys | 0 | 中 | A |
| IN04 | Bass | 0 | 中 | A |
| IN05 | Organ | 48 | 中 | A |
| IN06 | Sampler | 0 | 軽〜中 | B |
| MT01 | Loudness | 0 | 軽 | A |
| MT02 | Spectrum | 0 | 軽 | B |
| MT03 | Spectrogram | 0 | 軽 | A |
| MT04 | Phase Scope | 0 | 軽 | A |
| MT05 | Vu Ppm | 0 | 軽 | A |
| UT01 | Gain | 0 | 軽 | A |
| UT02 | Mono Check | 0 | 軽 | A |
| UT03 | Reference | 0 | 軽 | A |
| LV01 | Voice | 0 | 中 | A |
| LV02 | Feedback | 0 | 軽〜中 | B |
| LV03 | Channel | 0 | 中 | A |
| LV04 | Safety limiter | 0 ／ 72（True peak） | 軽〜中 | A |
| LV05 | Auto ducker | 0 | 軽 | B |
| LV06 | Stream master | 0 | 軽〜中 | B |
| LV07 | Speech Agc | 0 | 軽 | B |
| LV08 | Room Noise | 256 | 中 | B |
| LV09 | Hum Cut | 0 | 軽 | B |
| LV10 | Voice Fx | 128 | 中 | A |
| LV11 | Mic Switch | 0 | 軽 | A |
| LV12 | Geq 31 | 0 | 中 | B |
| LV13 | Live Peq | 0 | 軽 | B |
| LV14 | Align | 0（遅延は効果として扱う） | 軽 | B |
| LV15 | Auto Mixer | 0 | 軽 | A |
| LV16 | Live Gate | 0 | 軽 | A |
| LV17 | Bus Comp | 0 | 軽 | A |
| LV18 | Pop Guard | 0 | 軽 | B |
| LV19 | Av Sync | 0（遅延は効果として扱う） | 軽 | B |
| LV20 | Rta | 0 | 軽 | A |
| LV21 | Test Gen | 0 | 軽 | A |
| LV22 | Polarity | 0 | 軽 | B |
| LV23 | Loudness | 0 | 軽 | A |
| LV24 | Live Reverb | 0 | 軽 | A |
| LV25 | Live Delay | 0 | 軽 | A |
| LV26 | Mono | 0 | 軽 | B |
| LV27 | Scene Sync | 0 | 軽 | A |
| LV28 | Remote Hub | 0 | 軽 | A |
| LV29 | Interp Mix | 0 | 軽 | B |
| LV30 | Recorder | 0 | 軽 | A |

## 確認事項

決めてほしい項目は、全製品・EQ・ストリップ分が20、DY・MS 分が下の別表に16、SA〜LV の主なものが別表に17（細かい項目は各製品の要確認）。OBS 対応方式は決定済み（別表3の1行目）。上の6つは全製品に効くので先に決めたい。各行の「本書の扱い」は仮決めで、指示があれば直す。

| 対象 | ずれ・未決 | 本書の扱い |
| --- | --- | --- |
| 全製品 | Δ ボタンが無い画面が12枚（EQ02・EQ07・EQ08・CS04・DY04・DY05・DY08・DY10・DY11・MS02・MS05・MS06） | DY09・MS01・MS03・MS04 にある「Δ Delta」ボタン（パネル内のデジタルボタン）に揃えることを推奨。DY04 はツールバーの AG の隣 |
| 全製品 | アナログ筐体のパネル「Auto」とツールバー「Auto gain」が重複 | 同じ機能を2か所に出すものとした |
| 全製品 | 進化機能の On/Off の置き場所 | EVO タグで切替（共通章の EVO スイッチ案） |
| 全製品 | Drive の既定値 | 2（薄く色付け）で統一。0（無色）にするなら一括で変える |
| 全アナログ製品 | しきい値・レベルの dB 表示の基準 | CS02 だけ「0 dB ＝ −18 dBFS」。全アナログ製品に広げるか決める |
| 全製品 | 実装フレームワーク、CPU の基準機と測定方法 | 未決定 |
| EQ01 | 04 では Freq がセレクター、画面は連続ノブ。キャンバスに EQ01 が3枚 | 連続ノブ。EQ01\_v2 を正とした |
| EQ02 | バンド数の上限 | 24（案） |
| EQ05 | 04 の「.6〜7k」「.2〜2.5k」、画面の Pre／Post、Match ボタンが無い | 0.6〜7 kHz・0.2〜2.5 kHz、Drive の位置、EVO バーから起動 |
| EQ06・CS03 | 04 ではゲインが連続、画面は13段 | 段階式（2 dB 刻み） |
| EQ07 | Output が無い | 持たない（Auto gain で代用） |
| EQ08 | バンド種類のボタンが無い。2048 タップでは低域が甘い | 種類あり。High 設定（4096 サンプル）の追加を提案 |
| EQ09 | Auto pivot 中の指針の見せ方 | 未定 |
| CS01 | Mix がコンプ部だけか全体か。「Solo」ボタンの意味が不明 | Mix はコンプ部の並列。Solo は仕様に入れていない |
| CS02 | EQ に周波数ノブが無い。ゲートのホールド・リリースが無い。学習したキーフィルターの置き場所 | 周波数・時間は固定値。キーフィルターは内部値 |
| CS03 | 進化機能が共通機能「Auto gain」と名前がぶつかる。Gain 目盛り 0〜+60 の意味 | 「Gain learn」への改名を推奨。目盛り 30 ＝ 0 dB |
| CS03 | EQ の周波数ノブが無い | 固定値（100 Hz・1.5 kHz・10 kHz） |
| CS04 | 03 の説明欄が空。EQ 以外のモジュール画面が無い | モジュールの項目は案 |
| 03（次章以降） | UT01〜03 のカテゴリが Meter（01・05 では CR・IN・UT ＝ Creative・Instrument）。LV02〜06 のカテゴリが空欄 | 次章で扱う前に決める |
| 画面全般 | デジタル弧ノブの弧の長さが表示値と厳密に対応していない | 描画上の近似として扱い、仕様の値を正とした |

### DY・MS の確認事項

| 対象 | ずれ・未決 | 本書の扱い |
| --- | --- | --- |
| DY01・DY02・DY03・MS01 | キャンバスに旧版と \_v2 が並んでいる | 04 と一致する \_v2 を正とした |
| DY01 | 04 に Color（Clean／Grit／Crush のレバー）が無い | 追加した |
| DY02 | トグル「Auto｜Makeup」の意味。Auto makeup と共通の Auto gain の違いの見せ方 | Auto makeup の On/Off と解釈 |
| DY03 | Ratio・Attack・Release の段数が画面と 04 に無い | 4・7・6段で仮決め |
| DY04 | 500シリーズに Unit A／B／C を持たせるか。キーフィルターの周波数ノブが無い | Unit なし（画面どおり）。周波数は学習で決まる内部値 |
| DY05 | 03 の説明欄が「Multiband dynamics」 | 「De-esser」への修正を推奨 |
| DY06 | Output が無い。Time 各段の時間 | Output なし。Time は表のとおり仮決め |
| DY09 | Split bands で帯域を選ぶ UI が無い。分割周波数 | 帯域選択ボタンの追加を推奨。150 Hz・4 kHz 固定 |
| DY10 | 直線位相の帯域分割を選べるようにするか | 最小位相のみ |
| DY11 | 帯域の周波数を示す数値が無い | Freq を持たせ、グラフで操作 |
| DY12 | 既定値が「かからない位置」の方針の例外 | Squash 5・Blend 30 % |
| MS01 | 04 に Target・Lock・Character・Gain が無い。手動の Gain が画面に無い | 追加した。Gain の画面追加が必要 |
| MS03 | 「Link bands」ボタンを進化機能のスイッチとして扱うか | 扱う（既定 On） |
| MS04 | Listen と Δ が同じ音。Gain match と Auto gain の重複。EVO バーの「2× OS」とパネルの Oversample | Listen か Δ に統一。Gain match は Drive 専用。EVO バー側を隠す |
| MS06 | Comp 以外の段の画面が無い。Reference A/B が UT03 と重なる | 段の項目は案。処理は UT03 と共有 |
| MS07 | 画面に Unit A／B／C があるが、ディザーには意味が無い。Shape の既定 | Unit を外すことを推奨。既定 Mid |

### SA〜LV の主な確認事項

| 対象 | ずれ・未決 | 本書の扱い |
| --- | --- | --- |
| LIVE 全体 | OBS 本体は VST3 を読めない（2026年10月時点、本体への VST3 ホスト追加はレビュー中） | 自社の「SW AUDIO for OBS」（GPL の薄い OBS プラグイン＋非公開のエンジン）で決定（2026-10-05）。公開前に弁護士確認。VST3 版は第三者の橋渡しでも動作確認（LIVE 共通章の1） |
| SW Link を使う製品 | 橋渡しプラグインがプラグインを別プロセスで動かすと SW Link が届かない（LV05・LV11・LV15・LV27・LV29、STUDIO の LO03・VO05 など） | SW AUDIO for OBS では解決。第三者の橋渡し経由は動作確認し、届かない場合はサイドチェーンで代替 |
| LV06・LV10 | 画面の「LIVE 0.0 ms」と処理が合わない（遅延0で dBTP は保証できない、音程変換は遅延0にできない） | LV06 は表記を dBFS にするか遅延を出す。LV10 は 128 サンプルを表示 |
| LV24 | 画面と EVO 文に未実測の「1% CPU」 | 外す（「Low CPU」等） |
| LV25 | OBS は MIDI をプラグインに渡さない | SW AUDIO for OBS はエンジンが MIDI を直接受ける |
| LV19 | 音声プラグインは映像を見られない | SW AUDIO for OBS は映像も見て自動。VST3 版は半自動 |
| LV28 | LAN 内サーバーの安全設計 | PIN・試行制限・一時トークン・LAN 限定。暗号化は未決 |
| LV10 | 声の加工で匿名化は保証できない | 説明文に明記 |
| ST05 | ヘッドホン機種プロファイルが「実機名を使わない」と衝突 | 種類別の汎用カーブか、ユーザー測定の読み込み |
| GT02・RV04・IN02〜04・ST05 | IR・楽器サンプル・室内応答の自社収録が必要 | 収録計画と費用が未定 |
| RS02 | 学習データの権利。Offline best は書き出し時のみ動く | 未決 |
| VO01・VO08 | グラフ編集・フレーズ選択に音声の取り込みが必要 | ARA 2 対応か内部録音かを決める |
| VO02 | Key セレクターが7段で♯・♭が無い | 12段に直す |
| IN01 | 進化機能が共通のモーフと同じ | 別案（声ごとのモーフなど） |
| 共通機能の例外 | Unit が無意味な製品（MS07・RS03・UT01・MT05）、Auto gain が邪魔な UT01、Δ・Auto gain を持たない IN・MT | 外す方向で承認がほしい |
| 監視用の機能 | LO01 Preview・UT02・MT03・ST05 は書き出しにも乗る | On のままの書き出しに警告を出す |
| 03 のデータ | UT のカテゴリ（Meter か CR・IN・UT か）、LV02〜06 のカテゴリ空欄、DL01 の説明とパネル表記の不一致 | LV は LIVE 共通章の3で仮置き |

**次の作業：** 確認事項への回答を反映し、簡略形式の107製品を詳細形式に上げる（LIVE から）。
