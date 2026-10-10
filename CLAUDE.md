# CLAUDE.md — SW AUDIO（SEVENTHWELL のオーディオプラグイン・バンドル）

STUDIO 109 本＋LIVE 30 本＝139 製品。CLAP を正として作り、clap-wrapper で VST3／AU を生成する。DSP はフレームワーク非依存の C++17。
現状は v0.16.0：132 製品が完成（単体テスト 1660 件（1656 件までは ASan・UBSan でも全合格、追加 4 件は関連テストだけ ASan 確認）。clap-validator・Steinberg VST3 validator とも Linux で全製品不合格 0。ThreadSanitizer のストレス試験（音声スレッドと窓のスレッドを同時に）で全製品 0 件。GitHub Actions は run 172＝v0.16.0（EQ05 の Match・EQ08／EQ02 の別スレッド設計・音声スレッドの確保の検査・MS07・CS04）まで Windows・macOS（auval の aumf を含む）・Linux で全ジョブ成功。そのあと：run 173 は macOS の ARM で SW Link の参照スペクトルの読み出しが混ざる不具合を見つけた〔弱い順序の CPU：シーケンスロックのフェンスを足して直した。run 174 で macOS・Windows は成功〕、Linux は試験の作りの不具合で 1 件〔書き手が動き出す前に試行が終わる：直した〕。run 175 は Linux の host_smoke の LV05 の Key で落ちた〔原因は試験：待つ長さをブロック数で固定していて 96 kHz では半分の時間になった。秒で決める形に直した。調査中に readKey の別の不具合〔同じ製品の止まったスロットが 2 つあると鍵を読み続ける〕も見つけて直した〕。修正後の実行は結果待ち）。画面（UI）は全製品にデザインを載せ、中央の表示も大半が動く（残りと未実装の共通機能は `docs/tasks.md`。実機の DAW でしか確かめられないことは `docs/real_host_checklist.md`）。MIDI 入力（MD05・CR04・VO03・LV25）、共通機能の Low lat（仕様書が定める 11 製品すべて）・オーバーサンプリング（21 製品）・Unit A/B/C（42 製品）、SW Link の最初の部分は実装済み。残りは RS02（学習済みモデルが要る・保留）と、拡大率の「100%」・Linux の画面など（`docs/tasks.md`）。IN01〜IN06 の楽器プラグインは作らない（依頼者の決定）。

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
4b. **パラメータを足す・変えたら**：`python3 tools/dump_specs.py`（`ui/specs.json` と `tests/ui/curve_samples.json`。全製品を 1 回コンパイル、約 1 分）→ `python3 tools/gen_skins.py` → `python3 tools/check_skins.py` を回して commit する（プレビュー・画面の部品の結び付き・JS の曲線テストがこの表から作られる。古いまま commit すると画面側の表と食い違う。CS02 のキーフィルターで一度やった）。
5. 検証：`tools/validate_all.sh`（ビルド・単体テスト・全プラグインの両 validator）。ASan／UBSan でも全テストを回す（下のコマンド）。
6. README（製品表、設計値、仕様との差）と `docs/tasks.md` のチェックを更新して commit。

### 実装の決まりごと（過去の不具合から）

- **パラメータ ID は仕様書のとおり**（`cs02.comp.thresh` など）。ホストのパラメータ ID は、製品のパラメータは表の並び順（0 から）、共通のスイッチ（Auto gain・Delta・Bypass）は 0x1000・0x1001・0x1002 に固定（製品のパラメータ数が変わっても動かない。`host_smoke` が確かめる）。**一度出した番号は並べ替えない**。追加は末尾（保存済みの設定を壊さないため。状態の読み込みは共通のスイッチ Auto gain・Delta・Bypass を末尾から読むので、製品のリストの末尾に足しても古い状態のスイッチはずれない）。
- 範囲・既定値・カーブは `ParamSpec` に：`Curve::Lin/Log/Skew/Step/Fader`、`reversed`（ノブが逆向き）、`minLabel`/`maxLabel`/`maxLabelNorm`（Off・Auto・∞・右端5 %）、`automatable=false`（Listen・並び順などの監視・保存用）。
- 共通の処理枠 `sw::Shell`（In → Auto gain → Mix → Output → Δ）。製品の Output／In／Mix は traits で枠へ回す。コンプ部だけの Mix などは製品の中で処理し、traits は -1。量子化する製品は `kAutoGain=false`。
- 外部サイドチェーン：Core に `processWithSidechain(...)` を書くと、自動で2つ目の入力端子が付く。
- 遅延が変わる設定（先読み、位相モード、FIR 長など）：`latencySamples()` は「次の prepare で使う値」を返す。prepare 時に確定させる。違えばプラグイン層がホストに再起動を求める。
- **Unit A／B／C（2U ラック筐体の 42 製品）：最後のパラメータ `unitSpec("<コード>.unit")`、プラグイン層の trait `kUnitParam`（Shell が出力段のゲイン公差を掛ける）。コアの中の周波数は `sw::Unit::freqMul(unit, ch, スロット)`、飽和の効き始めは共通部品の `setOnsetDb(ch, db)` か、段の駆動ゲインに `Unit::satDb` の係数を掛ける。A は偏差ゼロ（左右が完全に同じ）。**
- **音声スレッドでメモリを確保しない**（バッファは prepare で確保。`std::vector` の `push_back`・`resize`・値渡しの返り値・`std::function` の生成・`Fft` の構築も確保になる。`reserve` や固定長の `std::array`、作業用の配列を `prepare` で作っておく）。**`tests/test_no_alloc.cpp`（`tools/gen_fuzz.py` が作る。製品を足したら `python3 tools/gen_fuzz.py`）が全製品で数える**。学習ボタンのような新しい口は `tests/test_no_alloc_learn.cpp` に足す。違反が出たら `SW_ALLOC_TRACE=1 ./build-cmake/sw-tests -tc="the audio thread does not allocate: <コード>"` で呼び出し元のアドレスが出る（`addr2line -f -C -e build-cmake/sw-tests <オフセット>`）。出力は `|y|<1e-30` を 0 に。
- **非線形の段は `sw::OsSwitch`（`DriveStage`・`BiasShaper` もその上）を通し、`oversampleSpec("<コード>.os")` を製品パラメータの末尾（Unit があればその直前）にする**（1x／2x／4x、既定 2x。仕様書が 4× の製品は第 2 引数）。段のループの中にある時間のもの（フィルター係数、DC 除去、包絡の追従）は `os.rate(fs)` で計算する（`fs` 決め打ちや 2×fs 決め打ちは設定を変えると音が変わる）。画面の「2× OS」ボタンは `gen_skins.py` が `.os` に結び付ける。
- アナログ系の段のヘッドルームは +6 dBFS（決定事項）。
- **ブロック長に依存させない**：制御値・判定・ランプ・時定数は、`process()` の局所変数や「ホストのブロックの頭から」でなく、ストリームの絶対位置の格子（`ph_` を持ち、32〜64 サンプル）で決める。制御ブロックの終わりで決めたものは次の制御ブロックにかけて直線で入れる。リミッターの Auto release は `PeakLimiter::setAutoRelease`（1 サンプルごとの判定）。`tests/block_helpers.hpp` の `bsi::worstDb` が −90 dB 未満（定常）で、`host_smoke --blocks` が通ること。`activate` の `max_frames` より長いブロックはアダプターが割る。
- **残響・繰り返しのある製品はテールを報告する**：コアに `double tailSeconds() const`（今の設定で、入力が止まってから 80 dB 下がるまでの秒数。ずっと続くなら `sw::tail::kInfinite`。`sw/tail.hpp` の `loop`・`multi`・`fromRt60`）。アダプターが CLAP の tail 拡張（遅延を足した値）にする。`host_smoke --tails` が「報告値 ≥ 実測」を乱数設定で確かめる。
- **入力の NaN・∞ は、アダプターが 0 にしてからコアに渡す**（`cleanInput`）。それでもコアのループ（`while`・探索）は NaN で進まなくならないよう、比較は `!(x >= y)` の形で書く。`host_smoke` の「poisoned input」と「random settings」の 4 つ目の種（壊れたパラメータ値）が通ること。
- **ホストのテンポは、アダプターが 1〜1000 bpm のときだけコアへ渡す**（NaN・±∞・0・負・範囲外は 0＝「テンポなし」。コアは `bpm_ > 0` で「なし」を判定する形で書く）。テンポや小節の位置を読む製品を足したら `host_smoke --transport`（テンポ・小節位置・拍子・再生位置の異常値、実時間の 5 倍を超えない）と `host_smoke --mono`（1 チャンネルだけ渡されても落ちない。コアは `numCh == 1` で `ch[1]` に触れない）が通ること。
- **`reset()`（ホストが止まった・飛んだ）で音を忘れる**：コアに `void reset()`（バッファとフィルターの状態だけ消す。割り当てない。IR・学習した値・パラメータは残す）。無くてもテールを報告する製品は、アダプターが `prepare()` をもう一度呼ぶ（割り当てない作りであること）。重い `prepare()`（IR の合成）を持つ製品は `reset()` を必ず書く。`host_smoke --reset` が −40 dBFS 超で鳴る製品を落とす。
- **学習（Learn）系の EVO は、コアが決めた値を `takeParamWrite(int& id, double& plain)` でホストへ返す**（戻り値 7＝開始・値・終了。値は `setParam` で自分に入れると同時に待ち行列へ。EQ07 Auto thresh・DY04 Learn・CS02 被り学習・RV08 Learn・CS03 入力レベル合わせ・DY10 Auto・MS07 Truncation check・CS04 並び順の提案・EQ05 Match）。ボタンはプラグイン層の trait `guiCall(core, "learn", arg)`（音声スレッドで実行）、状態は `kReadouts`／`readouts`。被り学習は `sw::BleedLearner`（DY04・CS02・RV08 共通。しきい値は「その製品の検出器が見る量」で決める：ピークで比べる検出器にはそのまま、追従値で比べる RV08 には追従値を渡す）、レベル合わせは `sw::LevelLearner`、クロスオーバー解析は `sw::CrossoverFinder`、実効ビット数は `sw::BitDepthProbe`、長時間平均スペクトル（1/6 oct）は `sw::BandSpectrum`（EQ05 Match：参照のファイルは `refbegin`／`refdata`／`refend` でウィンドウのスレッドから、最小二乗の `fit` もウィンドウのスレッド＝`guiOnGui`／`guiCallGui`、聴くのと値の書き込みは音声スレッド）。コアが複数のパラメータを書く操作は、`ui/actions.json` の `"undo": [パラメータ ID]` を付けるとページが書き込みを Undo の 1 段にまとめる（`"exact": true` は「Auto」が「Auto gain」に当たらないようにする、ラベルの完全一致）。聴く時間・検出条件・周波数の決め方は仕様書にないので README に設計値として書いてある。
- **重い計算（カーネルの設計など）は別スレッド**（`sw::BackgroundWork`＝`core/include/sw/worker.hpp`。EQ08・EQ02 Linear）：コアに `useWorker(bool)` を書き、`prepare()` の最後で `job_.start(...)`、`process()` ではパラメータのコピーを渡して `kick()`（ロックしない）、できあがりは次のブロックで受け取る（`st_` の 0／1／2）。`BackgroundWork` はメンバーの最後に置く（先に join される）。コピーされたコアにスレッドは付かない（コピーは `process()` の中で同期設計）。アダプターが `prepare()` の前に `useWorker(true)` を呼ぶ（`HasUseWorker`）。`snapToTargets()` は途中の設計を待って捨てる。
- **別スレッドで設計する製品は、オフラインの書き出しで結果が同じになること**：コアに `setOffline(bool)`（オフラインのときは頼んだ設計の完了を待つ）を書くと、アダプターが CLAP の render 拡張を付ける（VST3 の kOffline も clap-wrapper がこれに写す）。`host_smoke --offline`（同じブロックを 2 回通して出力がビット単位で同じ）が通ること。製品が自分の乱数のシードを持つなら状態（`saveExtra`）に入れる。
- **プロジェクト状態の追加ブロック（`saveExtra`／`loadExtra`）はコアのデータを触る**：ホストのメインスレッドが再生中に呼ぶので、アダプターが 3 状態の門（`extraGate_`）で音声スレッドの `process()` と排他する（門が閉じている間に来たブロックは入力のまま通す）。`loadExtra` を持つコアを足したら `tools/stress_tsan.sh`（ホストのメインスレッドが状態を保存・読み込みしながら）で 0 件であること。
- **ステップで進むジョブ（IR の設計・読み込み）は `sw::GridClock`（`core/include/sw/grid_clock.hpp`）の格子で動かす**：`process()` をブロックの端でなく格子の点（絶対のサンプル位置の 64 ごと）で切り、ジョブの開始・1 ステップ（4 つごと）・切り替えは格子の点で行う（RV04・ST05・GT02）。「呼び出し 1 回に 1 ステップ」はブロック長で時間が変わる。`tests/test_ir_pacing.cpp` が「ブロック長が違っても出力がサンプル単位で同じ」を確かめる。
- **SW Link の共有メモリを複数のスレッドで読み書きする形（参照スペクトル、共有の設定）は「番号→値→番号」のシーケンスロック**：書き手は番号を 0 にして release フェンス→値→番号（release）、読み手は番号（acquire）→値（relaxed）→acquire フェンス→番号（relaxed）。**x86 の Linux・Windows では、フェンスが無くても通ってしまう**（macOS の ARM の CI だけが落ちた）。
- **テストの処理ループは端数ブロックを必ず `std::min(256, n - off)` で切る**（配列の外を読む不具合を2回出した）。
- 有効化前に状態を読み込まれても落ちないこと（prepare 前は `snapToTargets()` で何もしない）。

## コマンド

```bash
tools/setup_linux.sh                       # 初回：ビルド道具・clap-validator・Steinberg validator を用意
cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
tools/validate_all.sh                      # ビルド＋単体テスト＋全プラグインの両 validator（1行ずつ結果）
build-cmake/sw-host-smoke build-cmake/plugins   # ホスト経由の音声経路テスト（Linux。`--rate=44100|96000|192000` で別のサンプルレート（CI は 4 つのレートで実行）。Output ゲイン・Bypass・Mix 0 %・In Off・NaN。窓のページのメッセージを窓なしで送る試験（`plugin/clap/sw_message.h`）：更新スクリプトの形、プリセット、UT03 の参照曲・RV04 の IR を断片で送る、EQ02 の Assist、**MIDI（MD05・VO03・CR04）、プロジェクトの保存と読み込み（全製品：往復・途中で切れた状態・でたらめな状態）、報告遅延とインパルスの比較、SW Link：実際の .clap を 3 つ別々に読み込んで互いが見える・片方をアンロードしても登録簿が残る**（`plugin/clap/swlink.hpp`：製品ごとに別バイナリなので、登録簿は OS から直接取ったメモリのアドレスを環境変数 `SW_AUDIO_LINK` に書いて共有する）。validate_all.sh も実行）
python3 tools/gen_skins.py && python3 tools/check_skins.py   # 画面とパラメータ表の突き合わせ（つまみのラベルと結び付き先、共通パラメータへの誤結合、重複、結び付かない部品。CI の Linux ジョブでも実行）
node tests/ui/refload.test.js   # UT03・RV04 の読み込み（ページが作る WAV・base64 の断片・概観）。curves・traits・shapes と同じく CI の Linux ジョブで走る
NODE_PATH=$(npm root -g) node tools/audit_static_text.js [コード]   # 画面で数字が動かない文字（デザインの例の数字の残り）を探す（preview を http.server で配っておく）
./build_tests.sh && ./build/tests          # CMake なしの手早い単体テスト（third_party/doctest.h が要る）
tools/stress_tsan.sh [秒] [製品コード…]    # 音声スレッドと窓のスレッドを同時に当てて ThreadSanitizer でデータ競合を探す（build-tsan。全製品で約 40 分）
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

`docs/tasks.md` の項目。進化機能（学習・解析のボタン）は仕様書にあるものをすべて実装済み（EQ02・EQ05・EQ07・DY04・DY10・CS02・CS03・CS04・RV08・MS07 など）。残りは、**実機の DAW と Web ビューでの確認（依頼者の実機待ち。`docs/real_host_checklist.md`）**、ホストのトラック名・SW Link の残り（済：EQ05 Match の参照元に UT03、LV05 の Key に別のインスタンス。LO03 の Bass 役が Kick 役の LO03 を鍵にする〔Role を登録簿の tag に〕。LV15・LV11 は `sw/link.hpp` の代用。残りは LV29・LV27・VO05：デザインに相手を選ぶ部品が無く、仕様書も選び方を決めていない）。MT05 の 0 VU 基準の共有は済・OBS 連携（LV27）、拡大率の「100%」・Linux の画面、RS02（学習済みモデルが要る・保留）。
1 つごとに commit。まとまったら版を上げ（`CMakeLists.txt` の VERSION と各 `*_clap.cpp` の版文字列）、README の検証結果を更新する。

## 画面（UI）の作業（v0.13.0 以降）

- 画面は `docs/design/canvas/project/<コード>.dc.html` のデザインをそのまま使う（`tools/gen_skins.py`。結び付けは `ui/skin_aliases.json`）。部品の種類は **`.ctl`（`.dk`／`.knob`）、LIVE の `.rc`（`.rk`／`.kn`／`.rl`／`.rv`）、`.tile`、`.tog`（立体トグル）、`.morph`、ボタン**。新しい種類の部品を見つけたら、`python3 tools/gen_skins.py --report` の「結び付かなかった」一覧と、`ui/sw-ui.js` の `bindSkin` を見る（動かない絵を残さない。仕様にないものは非表示か読み取り表示にして README に書く）。
- 中央の表示は `ui/displays.js`（レジストリ `registry`）。値の出どころは 3 つ：パラメータ、アダプタが測るもの（入出力ピーク、スペクトラム 64 バンド、ステレオの L/R 点と相関）、**コアが測った値（traits の `kReadouts`／`readouts`、アダプタが 1 ブロックごとにアトミックへ写す）**。コアが値を持っているなら見積もりでなくそれを使う。
- 画面データの検査：`node tests/ui/curves.test.js`（曲線）、`node tests/ui/traits.test.js`（In／Output／Mix の枠への指定。**コアが Output を掛けているのに traits にも指定すると二重に掛かる／In に dB のゲインを指定するとバイパスになる**。過去の不具合）。CI の Linux で実行。
- 画面の自動テスト：`tests/ui/*.test.js`（node）と、**`tests/ui/browser.test.js`（Playwright。全 132 画面がエラーなく開く、Undo・履歴・Assist・Unmask・Low lat・UT01 の行。`NODE_PATH=$(npm root -g) node tests/ui/browser.test.js`、CI は Chrome を使う）**。プレビューの擬似読み出し（`ui/preview.html`）を増やしたらここにも確認を足す。
- 確認：`ui/preview.html?p=コード&sim=1`（`python3 -m http.server` で `ui/` を配る。Playwright は `NODE_PATH=$(npm root -g)`）。**本物のページ**（`gui::page()` の出力）は `tools/gui_page_dump.cpp` で書き出して `tools/gui_page_check.js` で確認（ネットワークなし、橋渡しは仮、本物と同じ引数の `SWHOST.update`）。全製品のエラー掃引はプレビューで行える。WKWebView／WebView2 そのものでの確認は依頼者の実機待ち。
- 連続 push は CI（同じブランチの古い run を打ち切る）を何度もやり直させる。push は区切りごと、CI が終わるまでは手元で commit して作業を続ける。
