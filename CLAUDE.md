# CLAUDE.md — SW AUDIO（SEVENTHWELL のオーディオプラグイン・バンドル）

STUDIO 109 本＋LIVE 30 本＝139 製品。CLAP を正として作り、clap-wrapper で VST3／AU を生成する。DSP はフレームワーク非依存の C++17。
現状は v0.13.0：132 製品が完成（単体テスト 1328 件。clap-validator・Steinberg VST3 validator とも Linux で全製品不合格 0。GitHub Actions は run 136＝共通の Bypass パラメータと画面の大半まで Windows・macOS・Linux で全ジョブ成功）。画面（UI）は全製品にデザインを載せ、中央の表示も大半が動く（残りと未実装の共通機能は `docs/tasks.md`）。残りは RS02（学習済みモデルが要る・保留）と、共通機能（Low lat・オーバーサンプリング 1×/2×/4×・Unit A/B/C・プリセット）の実装。IN01〜IN06 の楽器プラグインは作らない（依頼者の決定）。

## 話し方・進め方

- 日本語で、結論から簡潔に。依頼者は「おまかせ」で進めてほしい人。確認のために止まらず、検証済みの成果まで進める。
- 取り返しのつかない操作（force push、main の履歴の書き換え、リポジトリ設定の変更）だけは先に確認する。
- 事実と推測を分ける。測っていない数値を「確認した」と書かない。

## 正（source of truth）

| 何 | どこ |
| --- | --- |
| 製品の仕様（パラメータ表・DSP・遅延・CPU・進化機能・要確認） | `docs/spec/SW_AUDIO_spec_v1.0.md`（製品コードで検索。例 `grep -n "### SW DY01" -A80`） |
| 画面に描いた値・範囲 | `docs/project/04_parameters.csv`、製品一覧 `docs/project/03_product_lineup.csv` |
| 決定事項・ブランド・デザイン規定 | `docs/project/05_decisions_and_status.md`、`01_design_system.md`、`02_design_tokens.json` |
| 画面デザイン（UI を作るとき） | `docs/design/canvas/project/<コード>.dc.html`（製品ごとの画面。容量のため別 zip で追加する。入っていなければ依頼者に頼む）、`docs/design/design-system/project/`（トークン・部品・ロゴ） |
| これまでの決定・設計値・仕様との差 | `README.md`（製品表の「注意」、「決定事項」「〜の設計」「〜の注意」の各節） |
| 残りの作業 | `docs/tasks.md` |

- 決定済みの仕様は勝手に変えない。仕様書と画面（04）の値がずれる、仕様書に数値がない、仕様のままだと品質に問題がある、のどれかのときは README に「何を・なぜ・どう決めたか」を書いてから決める。品質問題は数値で示す（例：EQ08 のカーネル長と低域の誤差）。
- 仕様書に「案」とある値は採用してよい。書かれていない値は設計値として README に明記する。
- 実在機材の名前・ロゴ・外観は使わない。裏付けのない数値（遅延 0 ms など）を販売向けの文章に入れない。CPU 目安は「設計上の見積もり」と明記する。
- 色：状態色（緑・黄・赤）とカテゴリ色を混ぜない。光は左上から一方向（UI 作業時）。

## 1 製品の作り方（この順で）

1. 仕様書の節を読む（表・DSP・遅延・EVO・要確認）。範囲は 04 と照合する。
2. **テストを先に書く**：`tests/test_<code>.cpp`。仕様表（ID・範囲・既定値・カーブ・Auto 可否）と、DSP の要点を数値で確かめる。空実装でビルドして、**失敗することを確認**してから実装する。
3. 実装：`products/<code>/<code>.hpp/.cpp`（`specs()`・`ParamId`・`Processor`）。
4. プラグイン：`plugin/clap/<code>_clap.cpp`（traits：Output/In/Mix の番号、`kAutoGain=false` が必要なら）＋ `SW_CLAP_ENTRY`。`CMakeLists.txt` の `SW_PRODUCTS` と `sw_add_plugin(...)` に1行ずつ。
5. 検証：`tools/validate_all.sh`（ビルド・単体テスト・全プラグインの両 validator）。ASan／UBSan でも全テストを回す（下のコマンド）。
6. README（製品表、設計値、仕様との差）と `docs/tasks.md` のチェックを更新して commit。

### 実装の決まりごと（過去の不具合から）

- **パラメータ ID は仕様書のとおり**（`cs02.comp.thresh` など）。ホスト内部の番号は表の並び順。**一度出した番号は並べ替えない**。追加は末尾（保存済みの設定を壊さないため）。
- 範囲・既定値・カーブは `ParamSpec` に：`Curve::Lin/Log/Skew/Step/Fader`、`reversed`（ノブが逆向き）、`minLabel`/`maxLabel`/`maxLabelNorm`（Off・Auto・∞・右端5 %）、`automatable=false`（Listen・並び順などの監視・保存用）。
- 共通の処理枠 `sw::Shell`（In → Auto gain → Mix → Output → Δ）。製品の Output／In／Mix は traits で枠へ回す。コンプ部だけの Mix などは製品の中で処理し、traits は -1。量子化する製品は `kAutoGain=false`。
- 外部サイドチェーン：Core に `processWithSidechain(...)` を書くと、自動で2つ目の入力端子が付く。
- 遅延が変わる設定（先読み、位相モード、FIR 長など）：`latencySamples()` は「次の prepare で使う値」を返す。prepare 時に確定させる。違えばプラグイン層がホストに再起動を求める。
- 音声スレッドでメモリを確保しない（バッファは prepare で確保）。出力は `|y|<1e-30` を 0 に。
- アナログ系の段のヘッドルームは +6 dBFS（決定事項）。
- **テストの処理ループは端数ブロックを必ず `std::min(256, n - off)` で切る**（配列の外を読む不具合を2回出した）。
- 有効化前に状態を読み込まれても落ちないこと（prepare 前は `snapToTargets()` で何もしない）。

## コマンド

```bash
tools/setup_linux.sh                       # 初回：ビルド道具・clap-validator・Steinberg validator を用意
cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
tools/validate_all.sh                      # ビルド＋単体テスト＋全プラグインの両 validator（1行ずつ結果）
build-cmake/sw-host-smoke build-cmake/plugins   # ホスト経由の音声経路テスト（Linux。Output ゲイン・Bypass・Mix 0 %・In Off・NaN。窓のページのメッセージを窓なしで送る試験（`plugin/clap/sw_message.h`）：更新スクリプトの形、プリセット、UT03 の参照曲・RV04 の IR を断片で送る。validate_all.sh も実行）
python3 tools/gen_skins.py && python3 tools/check_skins.py   # 画面とパラメータ表の突き合わせ（つまみのラベルと結び付き先、共通パラメータへの誤結合、重複、結び付かない部品。CI の Linux ジョブでも実行）
node tests/ui/refload.test.js   # UT03・RV04 の読み込み（ページが作る WAV・base64 の断片・概観）。curves・traits・shapes と同じく CI の Linux ジョブで走る
NODE_PATH=$(npm root -g) node tools/audit_static_text.js [コード]   # 画面で数字が動かない文字（デザインの例の数字の残り）を探す（preview を http.server で配っておく）
./build_tests.sh && ./build/tests          # CMake なしの手早い単体テスト（third_party/doctest.h が要る）
# ASan / UBSan（先に python3 tools/embed_ui.py build/gui_assets.hpp）
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -Icore/include -Iproducts -Iplugin/clap -Ibuild -Itests \
    $(ls tests/test_*.cpp | grep -v test_main) $(ls products/*/*.cpp) tests/test_main.cpp -o /tmp/tests_asan && /tmp/tests_asan
```

- 初回の CMake は CLAP SDK・clap-wrapper・VST3 SDK・doctest を GitHub から取る（クラウド環境なら github.com への通信を許可）。
- clap-validator の `process-audio-denormals` は、処理の軽い製品で「極小値で約2倍遅い」と警告することがある。検証ツール側のバッファ作成時間まで測っているための偏りで、不合格ではない（README に実測あり）。新しい製品で出たら、同じ判断でよいか自作ホストの計測で確かめる。
- CI：`.github/workflows/build.yml`（Windows MSVC・macOS ユニバーサル＋auval・Linux。ビルド→単体テスト→両 validator→成果物の保存）。

## 決定事項（依頼者の「おまかせ」で確定済み）

1. アナログ系の段（EQ01/03/04/05/06 の Drive、CS01・CS03 のプリ）のヘッドルームは +6 dBFS。
2. EQ07 の Shelf／Cut：1 kHz 未満は低域側、以上は高域側。
3. EQ08・EQ02 Linear のカーネル：既定 2048（仕様書）、Length で 4096／8192 も選べる。
4. MS モード（EQ01・EQ08）：EQ は Mid だけ、Side は素通し（Linear では同じ遅延で揃える）。

## 次にやること

`docs/tasks.md` の UI の項目（画面の実装、ホストのトラック名・MIDI・SW Link・OBS 連携など、コアに口だけある部分）と、RS02。
1 つごとに commit。まとまったら版を上げ（`CMakeLists.txt` の VERSION と各 `*_clap.cpp` の版文字列）、README の検証結果を更新する。

## 画面（UI）の作業（v0.13.0 以降）

- 画面は `docs/design/canvas/project/<コード>.dc.html` のデザインをそのまま使う（`tools/gen_skins.py`。結び付けは `ui/skin_aliases.json`）。部品の種類は **`.ctl`（`.dk`／`.knob`）、LIVE の `.rc`（`.rk`／`.kn`／`.rl`／`.rv`）、`.tile`、`.tog`（立体トグル）、`.morph`、ボタン**。新しい種類の部品を見つけたら、`python3 tools/gen_skins.py --report` の「結び付かなかった」一覧と、`ui/sw-ui.js` の `bindSkin` を見る（動かない絵を残さない。仕様にないものは非表示か読み取り表示にして README に書く）。
- 中央の表示は `ui/displays.js`（レジストリ `registry`）。値の出どころは 3 つ：パラメータ、アダプタが測るもの（入出力ピーク、スペクトラム 64 バンド、ステレオの L/R 点と相関）、**コアが測った値（traits の `kReadouts`／`readouts`、アダプタが 1 ブロックごとにアトミックへ写す）**。コアが値を持っているなら見積もりでなくそれを使う。
- 画面データの検査：`node tests/ui/curves.test.js`（曲線）、`node tests/ui/traits.test.js`（In／Output／Mix の枠への指定。**コアが Output を掛けているのに traits にも指定すると二重に掛かる／In に dB のゲインを指定するとバイパスになる**。過去の不具合）。CI の Linux で実行。
- 確認：`ui/preview.html?p=コード&sim=1`（`python3 -m http.server` で `ui/` を配る。Playwright は `NODE_PATH=$(npm root -g)`）。**本物のページ**（`gui::page()` の出力）は `tools/gui_page_dump.cpp` で書き出して `tools/gui_page_check.js` で確認（ネットワークなし、橋渡しは仮、本物と同じ引数の `SWHOST.update`）。全製品のエラー掃引はプレビューで行える。WKWebView／WebView2 そのものでの確認は依頼者の実機待ち。
- 連続 push は CI（同じブランチの古い run を打ち切る）を何度もやり直させる。push は区切りごと、CI が終わるまでは手元で commit して作業を続ける。
