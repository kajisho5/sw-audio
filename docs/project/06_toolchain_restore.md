# SW AUDIO 作業の引き継ぎ（2026-10-05）

新しいチャットではこのzipをアップロードして「SW AUDIOの続き。仕様書から」と伝える。
このzipには、作業環境（/home/claude）にしか無かった生成スクリプト一式が入っている。

## 成果物の場所（消えない）
- 全製品画面・販売素材・ロゴ・X告知・カタログ：キャンバス https://claude.ai/artifact/YZPBvqpqrqoVXhZShrEBhE
- デザインシステム：https://claude.ai/artifact/QsE897Evf26HpwPvxwNqLJ
- キャンバスの元ファイル一式は、キャンバスを Artifact の read で取得すれば作業フォルダに展開される

## フォルダ構成（zipの中）
- gen/ … 画面生成。lib.py（共通部品・CSS・筐体）、batch1.py / batch2.py（製品定義＝生成本体）、evo.py（EVO機能文言）、
  premium_css.py / render_css.py / render2_css.py / render3_css.py / render4_css.py（仕上げ層。Blender描画画像をdata URIで内蔵）、
  mk.py（販売用ヒーロー）、lg.py / lk.py（ロゴ案・ロゴ一式ボード）、xp.py（X告知）、catalog_build.py（カタログ一覧）、ds_*.py（デザインシステム）
- blender/ … assets.py / assets2.py（ノブ・トグル・ネジ・素材・金具・フェーダー・VUベゼル）、hero.py（ヒーロー／カタログ描画）、
  logo3d/（立体ロゴ）、post.py / post2.py（ノイズ除去・WebP化）、uris.json / uris2.json（描画済み部品のdata URI）、runchain.sh（カタログ一括描画）
- logo/ … build.py / kit.py（ロゴ構築）、final/final.py（確定版C Rack Ear）、final.json・kit.json（パスデータ）
- tools/ … shot.py（全ボードのはみ出しチェック）、heroshot.py（パネルを2倍で取り込み）、xshot.py / mkexport.py（PNG書き出し）

## 再開手順
1. 環境：apt-get install -y blender（4.0.2）／ pip install shapely cairosvg --break-system-packages（playwright と opencv は既存）
2. zipを /home/claude に展開（gen/ blender/ logo/ tools/ をそのまま置く。tools/*.py は /home/claude 直下へ）
3. キャンバスを Artifact read で取得 → 出力先 /mnt/user-data/outputs/artifacts/ff90c5f0-…/project/ を lib.py の OUT と一致させる（フォルダIDが変わったら OUT と各スクリプトの P を置換）
4. 画面の再生成：cd gen && python3 batch1.py && python3 batch2.py（手作りボード19枚は生成対象外。仕上げ層は各ファイルに追記済み）
5. カタログ描画：python3 tools/heroshot.py <コード…> でパネル取り込み → blender/runchain.sh のコード一覧を差し替えて setsid nohup で実行（1本約2分）

## 次の作業：仕様書（これだけ）
カタログ画像は EQ・ストリップ13本で一旦止める（残り約125本は保留）。

### 土台データ（spec/）
- params.json / params.csv：全139製品の画面から抜き出したパラメータ一覧（計692個）。
  製品ごとに コード・名前・ライン・EVO機能文・パラメータ（名前・種類・最小/最大 or 段階・表示値）・スイッチ類。
- 画面にノブが無くタイルや表で操作する6製品（LV01・LV23・LV27・LV28・LV30・MT01）は、キャンバスのボードを見て手で補う。
- 値は「画面に描いた値」。単位・既定値・カーブ（対数/直線）・内部範囲は仕様書で決める。

### 仕様書に入れる項目（製品ごと）
1. 概要（何をするか・用途・STUDIO/LIVE）
2. パラメータ表：名前／範囲／単位／既定値／カーブ／オートメーション可否
3. DSP方式（フィルタ型、検出方式、オーバーサンプリング倍率など）
4. 遅延値（サンプル数、LIVEは0を原則とし例外は理由を書く）
5. CPU目安（軽・中・重の3段階＋根拠。実測ではなく設計上の見積もりと明記）
6. EVO機能：実装方式と難易度（ログ系＝初期、ML系＝後期）
7. 全製品共通機能（モーフ・Auto gain・Δ・EVOバー）は共通章にまとめ、製品ごとには差分だけ書く

### 進め方の案
共通章 → カテゴリごと（EQ → DY/MS → SA/LO/GT → RV/DL/MD/ST → VO/RS → CR/IN/MT/UT → LV）に分けて書く。

## その他の保留
- カタログ画像の残り約125本
- STUDIO P2〜P4の開発フェーズ設計
- ロゴの商標調査（未実施）

## 決まりごと（要点）
- ロゴ：C Rack Ear（暫定確定）。ツールバーは記号＝カテゴリ色＋SW AUDIO。
- 光は左上から一方向。ノブは本体静止・指針だけ回す。
- 状態色（緑黄赤）とカテゴリ色を混ぜない。裏付けのない数値はコピーに入れない。
