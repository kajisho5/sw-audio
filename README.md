# SW AUDIO — プラグイン本体

SEVENTHWELL の SW AUDIO のプラグイン実装。仕様は「SW AUDIO 仕様書 v1.0 初稿」に従う。

## 資料の場所

- 仕様書の全文：`docs/spec/SW_AUDIO_spec_v1.0.md`／プロジェクト資料：`docs/project/`／画面デザイン：`docs/design/canvas/`（`project/<コード>.dc.html` が製品ごとの画面、`index.html` で全体）／デザインシステム：`docs/design/design-system/`
- 作業ルール（Claude Code 用）：`CLAUDE.md`／残りの作業：`docs/tasks.md`

## 方式

- **CLAP を正として作り、clap-wrapper で VST3（macOS は AU も）を生成する。** すべて MIT／Apache 2.0 で、売上上限や利用料はない（NOTICE.md）。
- **DSP 本体はフレームワークに依存しない C++17。** `core/`（共通部品）と `products/`（製品ごとの処理）に置き、プラグイン層（`plugin/`）からも、将来の OBS 用エンジンからも同じものを呼ぶ。
- ホストに見せる値は仕様書どおり、連続値は正規化 0〜1、段階式は段番号。パラメータの番号（ParamId の並び）は保存データの互換のため**一度公開したら並べ替えない**。

## 現在の製品（v0.13.0、132本）

画面はまだ無い（DAW の汎用パラメータ画面で操作する）。共通機能は Auto gain・Δ（MS07 は Δ のみ）。

| 製品 | 中身 | 注意 |
| --- | --- | --- |
| SW CS01 Inductor Strip | プリ（鉄心風の飽和、低域ほど強い、2× OS）→ EQ（EQ04 と同じ回路：HPF 18 dB/oct、60 Hz・12 kHz の固定シェルフ、段階式の中域ベル）→ フィードバック型コンプ（アタックは 2〜20 ms で自動、Mix はコンプ部だけの並列）。Order で EQ／コンプの順番を入れ替え（10 ms でクロスフェード）、Link でステレオ連動 | Mic profile（初期値を書き込む EVO）は画面と一緒に作る |
| SW CS02 Console Strip | 入力 HPF 18 dB/oct・LPF 12 dB/oct → ダイナミクス（ゲート 0.1／20／100 ms 固定 → VCA フィードフォワードのコンプ、アタック 3 ms）と EQ（100 Hz・10 kHz シェルフ、600 Hz・3 kHz ベル Q1）を Route の順に → フェーダー（0 dB を 75 % の位置） | しきい値の 0 dB＝−18 dBFS（仕様書の案）。被り学習（区分 B）は画面と一緒に作る |
| SW CS03 Stepped Strip | トランス風プリ（目盛り 30 で 0 dB、Hi Z で高域の負荷と偶数次倍音）→ EQ06 方式の段階式 EQ（比例 Q・Glide）→ アタック・リリース自動のコンプ | Hi Z の負荷（8 kHz で −2.5 dB）と非対称の量は設計値。入力レベル合わせ（EVO）は画面と一緒に作る |
| SW CS04 Modular Strip | Gate・EQ・Comp・Saturate・De-ess・Limit の6モジュール（各1個、各 On）を好きな順に。並び順は 720 通りの番号1つ（オートメーション不可、設定と一緒に保存、切り替えは 5 ms で音を下げてから入れ替え）。Limit が On のとき 1 ms 先読み（48 サンプル @48 kHz） | 既定は EQ だけ On（挿しただけでは音が変わらない）。EQ 以外の項目は仕様書の案。De-ess は動くハイシェルフ（抑えていないときは原音と一致）。並び順の提案（EVO）は画面と一緒に作る |
| SW EQ01 Passive | 低域シェルフ＋Air ベル（Width）、Contour（シェルフの Q 0.707〜3.0 で「盛り上げて少し上を削る」形を1本で）、出力段 Drive、LR／MS | MS モードは「Mid だけに EQ、Side は素通し」と解釈した（仕様書に MS の動作の記述がない。確認事項） |
| SW EQ02 Surgical | 24 バンド（Bell／シェルフ／カット 6〜96 dB/oct／Notch）、バンドごとのダイナミック（Range・Thresh）、Stereo／Mid／Side の配置、Zero latency／Natural／Linear | 下の「EQ02 の設計」 |
| SW EQ03 Mid Shaper | 中域の Dip（0〜−10 dB）と Peak（0〜+10 dB）、Width は両バンド共通、Ride（200 Hz〜5 kHz の RMS が −18 dBFS より大きいほど Peak を下げる） | Ride の下げ幅（基準から +12 dB で Peak が 0）は設計値 |
| SW EQ04 Inductor | HPF 18 dB/oct、低域シェルフ（Q 1.0 で約 +1 dB の盛り上がり。実測 +0.95 dB、上側に −0.94 dB の小さなへこみも出る）、中域ベル Q 0.9、高域シェルフ、Iron（ブーストした分だけ 2× OS で飽和、低域ほど強い） | Iron の帯域ごとの強さは設計値 |
| SW EQ05 Console | 4バンドのコンソールEQ、HPF/LPF、Drive | — |
| SW EQ06 Stepped | 2 dB 刻みの3バンドEQ、比例Q、段の間を 30 ms でつなぐ Glide | — |
| SW EQ07 Dynamic | 6 バンドのダイナミック EQ。バンドと同じ周波数・Q のバンドパスで検出（External で外部サイドチェーン）、6 dB ソフトニー、実効ゲイン＝Gain＋Range×動作量。Spectral（短時間 FFT 1024 点・ホップ 256 で、帯域内の突出した成分だけを動かす。遅延 1024） | 下の「EQ07 の解釈と設計値」 |
| SW EQ08 Linear | 24 バンド。Linear（全バンドの合成振幅から FIR、分割 FFT 畳み込み）／Minimum（IIR、遅延 0）／Mixed（200 Hz より下は最小位相、上は直線位相）。新旧カーネルは 20 ms でクロスフェード。Pre-ring guard（バンドごとに前鳴りを見積もり、超えるものだけ最小位相へ） | 下の「EQ08 の注意」 |
| SW EQ09 Tilt | 傾き・低域・最高域、Auto pivot（曲のスペクトル中心にピボットが追従） | — |
| SW SA01 Tape | テープレコーダー。簡略化したヒステリシス飽和（2× OS）、Formula（A／B／C＝ヘッドルーム 0／+3／−3 dB）、Speed（7.5／15／30 ips）ごとのヘッドバンプと高域損失（Repro）、Wow・Flutter（補間付き可変遅延）、Hiss、Input／Output。遅延は 48 サンプル固定。Calibrate（EVO）は 5 秒の入力から Input を決める | Calibrate の開始ボタンは画面と一緒に作る（コアは startCalibrate() と書き戻しを持つ）。値はすべて設計値（下の「SA01 の設計」） |
| SW SA02 Console Sum | アナログ卓のサミング風の色付け。Color（Iron／Clean／Punch／Vint）、Drive、Crosstalk（左右の漏れ −80〜−40 dB）、Noise、Width（0〜150 %）、Output、Group（同じ番号のインスタンスが 1 台の卓として互いに負荷をかける）。インスタンスごとの個体差（EVO：種を状態に保存） | Group の効かせ方・個体差の幅・色の設計は設計値（下の「SA02 の設計」）。Unit A／B／C は共通機能と一緒に後で |
| SW SA03 Tube | 真空管の倍音付け。Drive（0〜+24 dB を小信号利得 1 の非対称 tanh で、4× OS）、Bias（Cold＝対称〜Hot＝非対称）、Tube（12AX7／12AT7／EL34）、Tone（1 kHz を軸に ±6 dB の傾き）、Mix、Output。動くバイアス（EVO、既定 On）：入力の包絡（50 ms で戻る）で動作点をずらし、大きい音ほど非対称に歪む | 管ごとのバイアス・ヘッドルーム・ドライブ量、EVO のスイッチ（sa03.evo.on、既定 On）は設計値（下の「SA03 の設計」） |
| SW SA04 Transformer | トランスとプリアンプの色付け。Iron（Nickel／Steel／Mu：飽和の天井・バイアス・低域の角）、Gain（目盛り 0〜60 ＝ −30〜+30 dB、30 で 0 dB）、Load（送り側のインピーダンス：高域の共振と低域の量を動かす、EVO）、Low weight／Top air、Pad（−20 dB）。低域ほど早く飽和する（2× OS） | 飽和の天井・共振の周波数と量などは設計値（下の「SA04 の設計」） |
| SW SA05 Exciter | 高域の倍音付け。Tune 以上の帯域を取り出し、偶数次（2 次）・奇数次（3 次）の倍音を、帯域自身のレベルで正規化して（入力の大小によらず）作って足す。Harmonics、Mix、Low drive（低域にも軽く倍音）、Mode（Even／Odd／Both）、Mono low、Auto fill（EVO：1/3 オクターブごとに高域を目標の傾きと比べ、足りない帯域に倍音を最大 +12 dB 多く足す） | 倍音の作り方・目標の傾き（−1.5 dB/oct）・3 領域の分け方は設計値（下の「SA05 の設計」）。共通部品に `ThirdOctaveAnalyzer` を追加 |
| SW SA06 Saturator | 5 種の歪み（Tape／Tube／Diode／Fold／Fuzz）を 3 帯域（分割 200 Hz・3 kHz、LR4）で使える多機能サチュレーター。帯域ごとに Type・Drive（0〜+24 dB）・Shape（Soft／Medium／Hard）・Bias・Dynamics（EVO：包絡で歪み量を動かす）・Mix、全体に Tone・Output。4× OS（Fold・Fuzz は 8×） | 画面に帯域ごとの区別が無い点は仕様書の要確認のまま。各形の式・Shape の意味は設計値（下の「SA06 の設計」） |
| SW SA07 Lo-Fi | レコード・古いテープ風の劣化。Era（1950／1970／1990／Tape：選ぶと Crackle・Wow・Bandwidth をまとめて書き込む、EVO）、Crackle（確率で出るパチパチ音、4 kHz で鳴る）、Dust（細かい粒）、Wow（可変遅延、SA01 と同じ）、Bandwidth（上限 3〜20 kHz、右端 Full）、Mono、Mix。遅延は 48 サンプル固定 | 音の作り方・Era の値は設計値（下の「SA07 の設計」）。Era の書き込みは複数パラメータ対応の共通プラグイン層を使う |
| SW SA08 Bitcrush | ビット数とサンプルレートを落とす。Bits（1〜24、整数）、Rate（200 Hz〜fs、LOG）、Jitter（ホールド周期の揺れ）、Pre filter／Post filter（折り返し・鏡像の除去）、Dither、Tempo lock（EVO、Rate を拍の周波数の整数倍に寄せる）、Mix。遅延 0 | 音の作り方は設計値（下の「SA08 の設計」）。Tempo lock のスイッチは画面に無い。Δ ボタンも無い |
| SW LO01 Low Harm | 低域を倍音で聞こえるようにする。Frequency（40〜200 Hz）以下から 2〜5 次の倍音を作って足す。Harmonics、Original（元の低域の残し量 −24〜0 dB）、Width（Narrow／Medium／Wide ＝ 最高次数 3／4／5）、Preview（Off／Phone safe／Club、監視用で Auto 不可）。遅延 0 | 倍音の作り方・Preview のフィルタは設計値（下の「LO01 の設計」）。2× OS は使っていない（理由は同節）。Preview On の警告表示は UI 側。Δ ボタンが無い |
| SW LO02 Sub Gen | 1 オクターブ下の正弦波を低音に位相を合わせて作る（ゼロ交差の分周＋音程検出の混成、遅延 0）。Sub（0〜10 ＝ 低域 +0〜+12 dB）、Range Hz（30／45／60／90）、Tune（−12〜+12 st、整数）、Punch（頭の強調）、Dry（Off〜Full） | Sub の値の読み方・検出系の位相補正・Punch は設計値（下の「LO02 の設計」）。専用の進化スイッチは無い（音程追従は常時）。clap-validator の denormals 警告が実行ごとに出たり出なかったりする（処理が軽い製品の既知の偏り）。Δ ボタンが無い |
| SW LO03 Low Focus | キックとベースの低域を分けてぶつかりを減らす。Role（Kick／Bass／Both）、Focus（30〜120 Hz、動的ベル）、Tight（0〜100 ％）、Mud cut（150〜500 Hz、静的ベル、深さは 6 dB × Tight）、Mono below（Off＋20〜300 Hz）。Bass はサイドチェーンのキックの立ち上がりで下げる。遅延 0 | 動的ベルの動き・深さは設計値（下の「LO03 の設計」）。画面の Tight 既定 60 ％は仕様書の 50 ％に合わせていない。SW Link は未実装（Bass 役はサイドチェーン接続が必要）。Δ ボタンが無い |
| SW GT01 Amp | ギターアンプのヘッド（キャビネットなし）。Channel（Clean／Crunch／Lead）、Gain、Bass／Middle／Treble（受動トーンスタックの伝達関数）、Presence、Master、Bright、三極管 2 段＋サグつき出力段（4× OS）。進化機能：Volume match（最初の 5 秒で弾く大きさを測り、ギター側を絞るときれいになる位置を合わせる、既定 Off） | 歪みの量・サグ・チャンネルの音量は設計値、トーンスタックの式の符号を 1 つ直した（下の「GT01 の設計」）。キャビネットなし（Cab On/Off を足すかは未決）。Δ ボタンが無い |
| SW GT02 Cab Ir | キャビネットとマイクの畳み込み（遅延 0）。Cab（1x12／2x12／4x12）、Mic（Dynamic／Ribbon／Condenser）、Mic distance（近接効果）、Off axis、Room、Low cut（Off＋20〜300 Hz）。録音した IR の代わりに、パラメータから作る最小位相のモデル IR | IR は録音ではなくモデル（仕様書の「IR は自社収録が必要」は未解決のまま。収録した IR が入ったら差し替える）。音の形は設計値（下の「GT02 の設計」）。Δ ボタンが無い |
| SW GT03 Pedalboard | 最大 8 台のペダルボード（Comp・Drive・Fuzz・Chorus・Delay・Reverb を他製品のコアで流用、各 3 ツマミ）、Noise gate、Bypass all、常時動作のチューナー。遅延 0 | ノブは設計値（画面にない）。設計は README「GT03 Pedalboard の設計」。並べ替え操作は画面側 |
| SW GT04 Bass Amp | ベースアンプ（DI との混ぜつき）。Gain／Drive／Master、Low／Lo mid／Hi mid／High（ノブ 5 で平ら）、Mid Hz、DI（Off／On）、DI blend（画面に無い、追加）。Drive は高域だけ歪ませ低域は残す。進化機能：Phase align（アンプ側の位相を測って DI 側に全域通過と遅延を入れる、既定 On） | Mid Hz の読み方・EQ の周波数・Drive・簡易キャビネット・DI 側の 35 Hz ハイパスは設計値（下の「GT04 の設計」）。位相は 150 Hz で一致（他の周波数では少しずれる）。DI blend ノブは画面に無い |
| SW GT05 Reamp | DI 録りの音を、ピックアップ・ケーブル・受け側インピーダンスから決まる回路の伝達関数（2 次ローパス、2〜7 kHz の山）に通す。Level、Impedance（10k／47k／100k／1M）、Cable（100〜1000 pF）、Pickup（Single〜Hum）、Output。進化機能：Pickup swap（DI の元の共振を推定して打ち消し、選んだ共振を付け直す、既定 Off） | 回路の定数・Pickup swap の推定方法は設計値（下の「GT05 の設計」）。既定でも約 +15 dB の山が 5 kHz にある（回路どおり）。画面の Lift トグルは入れていない（未決）。Δ ボタンが無い |
| SW RV01 Hall | アルゴリズムリバーブ（Hall／Room／Chamber／Plate／Ambience）。16 本の遅延線のフィードバック網（FDN）＋初期反射のタップ遅延＋拡散の全域通過。Pre-delay、Size、Decay（0.2〜20 s）、Diffusion、Damping、Low cut／High cut、ER / late、Width、Mix、Freeze、Mono low end、Duck（原音が大きい間は残響を下げる、画面に無い追加）。残響の遅延は 0 と表示 | FDN の部品（core/include/sw/fdn.hpp）・アルゴリズムごとの長さ・初期反射・Duck の動きは設計値（下の「RV01 の設計」）。Duck 量のノブが画面に無い。Δ ボタンが無い |
| SW RV02 Plate | プレートリバーブ。分散を持つ全域通過 48 段（高域が先に届く）＋FDN。Decay（0.5〜6 s）、Pre-delay（ms、または Sync でテンポの音符長）、Damping（Dark〜Bright）、Low cut、Width（Mono〜Wide）、Mix、Mono in。進化機能：Duck（−6 dB 固定、既定 Off） | 分散の段数・FDN の長さ・Damping の周波数は設計値（下の「RV02 の設計」）。Sync と Duck の操作は仕様書の表にない追加（rv02.sync、rv02.evo.on）。RV01 の音量正規化を線の長さの平均で割る式に直した（Fdn::meanLengthSeconds） |
| SW RV03 Spring | スプリングリバーブ。バネごとに「引き伸ばした全域通過」28 段の分散（さえずり）を持つ帰還の輪。Springs（1／2／3）、Dwell（入力の強さ、飽和つき）、Tone、Tension（さえずりの音程）、Drip（立ち上がりにだけ反応）、Mix | 分散の段数・輪の時間・Drip の励起・出力の係数は設計値（下の「RV03 の設計」）。専用の進化スイッチは無い（Drip が立ち上がり検出を内蔵）。Δ ボタンが無い |
| SW RV04 Convolution | 畳み込みリバーブ。Category（Halls／Rooms／Churches／Gear／Custom）、Pre-delay、Length、Size（IR の伸縮）、Low cut／High cut、Reverse、Mix。録音した IR の代わりに合成した IR、Custom は読み込んだ IR（API のみ）。先頭直接＋不均一分割 FFT で遅延 0。進化機能：Bar fit（長さを拍数に切り下げ、既定 Off） | IR は録音ではなく合成（仕様書の「IR は自社収録」は未解決。Gear は実機名を使わない汎用の音）。Load IR の操作は画面側の作業。CPU は 256 サンプルのバッファでは足りないことがある（下の「RV04 の設計」）。Δ ボタンが無い |
| SW RV05 Chamber | エコーチェンバー（残響室）。Room（Small／Medium／Large、直方体）、Decay（0〜10 ＝ 0.4〜4 s）、Mic distance（Near〜Far）、Speaker tilt、Tone、Mix。鏡像法の初期反射 25 個（部屋と位置から計算）＋ FDN の後部（臨界距離で大きさを決める）。進化機能：マイク距離で直接音と残響の比・初期反射・高域の減りが連続して変わる | 部屋の寸法・位置・壁の反射率・後部の大きさの決め方は設計値（下の「RV05 の設計」）。Δ ボタンが無い |
| SW RV06 Shimmer | 音程を上げた残響が重なるシマー。16 本の FDN の線のうち 8 本の帰還の中に、2 粒の重ね合わせの音程変換（Octave＝2 倍／Fifth＝1.5 倍／Both）を入れる。Decay（1〜60 s）、Shimmer、Interval、Mix、Freeze（EVO、保持したパッドは昇らない）、Duck（既定 On） | 音程変換を線ごとの帰還の中に入れた理由（外側のループは発散した）・窓や割合は設計値（下の「RV06 の設計」）。Pitch「+12 st」は Interval の選択と解釈。Δ ボタンが無い |
| SW RV07 Early | 初期反射だけのリバーブ。Use／Distance／Angle／Room size／Wall。直接音は遅らせない | 仕様書どおり Mix なし。設計値は README「RV07 の設計」。Δ ボタンなし |
| SW RV08 Gated | ゲートリバーブ。Size／Gate time／Threshold／Shape／Tone／Mix、Snare key（検出をスネア帯域に絞る） | 設計値は README「RV08 Gated の設計」。被り学習は UI と一緒に作る（CS02 と同じ扱い）。Δ ボタンの有無は画面で確認 |
| SW DL01 Echo | 3 種の音色（Tape／Analog／Digital）のエコー。Time（Sync で音符長）、Feedback 0〜110 %、ループ内 HPF／LPF、Depth／Rate、Ping-pong、Duck、Mix | 設計値は README「DL01 Echo の設計」。Sync は Time のつまみ位置を 18 の音符長に割り当て（既定 375 ms 位置は 1/4 付点）。画面パネル表記の確認（Hybrid echo processor／Tape delay）と Δ ボタンは未決 |
| SW DL02 Tape Echo | 3 ヘッドのテープエコー。Heads、Rate（Slow〜Fast）、Intensity 0〜110 %、Bass／Treble、Wear（高域劣化・ワウ・ドロップアウト）、Mix | 設計値は README「DL02 Tape Echo の設計」。ループ内の飽和は SA01 の履歴モデルではなく tanh に簡略化。Heads 切替は 10 ms、再生中は小節線まで待つ（プラグイン層に setTransport を追加）。Δ ボタンの有無は画面で確認 |
| SW DL03 Bbd | バケツリレー素子（BBD）のアナログディレイ。Time（クロックと帯域が連動）、Feedback、Mod depth／rate、Grit、Mix、Sync | 設計値は README「DL03 Bbd の設計」。Sync は実テンポでの最近傍の音符（既定 Off）。Δ ボタンの有無は画面で確認 |
| SW DL04 Multitap | 6 タップのディレイ。タップごとに On／Time／Level／Pan／Filter、Feedback、Mix、Sync、Ping-pong | 設計値は README「DL04 Multitap の設計」。Sync は Time を 120 bpm の ms と読み最近傍の音符に寄せる。Δ ボタンは無い（仕様書の要確認）。clap-validator の denormals 警告が時々出る（既知の偏り） |
| SW DL05 Reverse | 逆再生・順再生・ランダムの粒ディレイ。Mode、Time（音符 1/16〜2 小節）、Grain size、Spray、Pitch +12、Freeze、Mix。拍位置があれば区間境界を小節線にそろえる | 設計値は README「DL05 Reverse の設計」。Δ ボタンは無い（仕様書の要確認） |
| SW MD01 Chorus | BBD 風のコーラス。Mode（I／II／I+II）、Rate、Depth、Width（左右の LFO 位相 0〜180°）、Tone、Mix。Wide ではモノの和で揺れが打ち消し合う | 設計値は README「MD01 Chorus の設計」。Δ ボタンの有無は画面で確認 |
| SW MD02 Flanger | フランジャー。Rate（Sync で音符長）、Depth、Feedback ±100 %、Manual、Through zero（報告遅延 480／0）、Sync、Mix | 設計値は README「MD02 Flanger の設計」。Sync 時の LFO 位相は小節線にそろえる。Δ ボタンは無い（仕様書の要確認） |
| SW MD03 Phaser | 全域通過を 4／6／8／12 段重ねたフェイザー。Rate（Sync で音符長）、Depth、Feedback、Center、Mix、Note follow（音高に Center が追従） | 設計値は README「MD03 Phaser の設計」。音高検出は DY05 から core/include/sw/pitch_tracker.hpp に移して共有。Δ ボタンの有無は画面で確認 |
| SW MD04 Tremolo Pan | トレモロ／オートパン／ハーモニック（800 Hz で上下を逆位相）。Rate（Sync で音符長）、Depth、Shape（Sine／Triangle／Square／Ramp）、Width | 設計値は README「MD04 Tremolo Pan の設計」。Mix は無い（仕様書どおり）。Δ ボタンは無い（仕様書の要確認） |
| SW MD05 Rotary | 回転スピーカー。Speed（Stop／Slow／Fast、ホーンとドラムは別の慣性）、Accel、Horn／Drum、Mic distance、Drive、Mix。ドップラー・音量変化・キャビネット共振、遅延 48 サンプル固定 | 設計値は README「MD05 Rotary の設計」。MIDI／フットスイッチ（CC64・CC1・Note）での Speed 切替は未実装（ホストのノート入力をプラグイン層に通す作業） |
| SW MD06 Freq Shift | 周波数シフター（IIR ヒルベルト対）。Shift ±2000 Hz（対数対称）、Direction、Ring mod、Feedback、LFO、Mix、Pitch track（シフト量を音高に比例） | 設計値は README「MD06 Freq Shift の設計」。対数対称カーブ Curve::SymLog を param.hpp に追加。Δ ボタンは無い（仕様書の要確認） |
| SW MD07 Ensemble | ストリングアンサンブル風の多重コーラス。Voices（2／3／4／6）、Spread、Rate、Depth、Tone、Mix。声の位相を等間隔に置き、モノの和で 1 次の揺れが打ち消し合う | 設計値は README「MD07 Ensemble の設計」。Δ ボタンの有無は画面で確認 |
| SW ST01 Imager | 4 帯域のステレオ幅調整（LR4 分割、帯域ごとに S を 0〜200 %）。Crossover 1〜3、Mono check（監視用）、帯域ごとの相関メーターと広げすぎの印 | 設計値は README「ST01 Imager の設計」。画面の描画は UI の作業。Δ ボタンは無い（仕様書の要確認） |
| SW ST02 Mid Side | M/S のレベルと音色。Mid level／Side level ±12 dB、Side HPF、Side air、Mid low、Encode（M/S のまま入出力） | 設計値は README「ST02 Mid Side の設計」。参照曲との M/S バランス比較（区分 B）は画面側 |
| SW ST03 Phase Align | 2 本のマイクの時間と位相を合わせる。Delay 0〜20 ms（0.01 ms 刻み）、Phase ±180°（ヒルベルト対で全帯域を回す）、Polarity、Mix。外部サイドチェーンに基準のマイク。Auto align（相互相関で遅延、低域の相関が最大になる位相／反転を選ぶ） | 設計値は README「ST03 Phase Align の設計」。Auto align の analyse() は画面がメインスレッドで呼ぶ（画面は未作成）。『Δ Compare』の名前は要確認 |
| SW ST04 Center | センターと広がりと Haas。Center（S を +6 dB〜−∞）、Haas 0〜40 ms（遅らせる側を選ぶ）、Low center、Balance、Link、Mono safe（モノの和の櫛形の落ち込みを自動で浅く） | 設計値は README「ST04 Center の設計」。Link の意味は仕様書にないため設計値（遅れ 1 ms あたり +0.35 dB）。確認が要る |
| SW ST05 Phones | ヘッドホンでスピーカーの部屋を聴く。Speakers（Nearfield／Mains／Car）、Room、Angle、Head size、Phones profile（Closed／Open／Earbud の汎用カーブ）、Tracking（頭の向きの土台）。モデルから作った 4 本の両耳 IR を畳み込む（遅延 0） | 設計値は README「ST05 Phones の設計」。IR は録音ではなくモデル（仕様書の収録は未対応）。ヘッドホンは機種名でなく種類別、測定データ読み込みと Tracking の機器は未対応。CPU は 256 サンプルで平均約 27 %、512 以上を推奨 |
| SW ST06 Mono Low | 低域をモノにする。Frequency 20〜300 Hz、Slope 6／12／24／48 dB/oct（S だけをハイパス）、Side boost、Output、Listen（消える成分の試聴） | 設計値は README「ST06 Mono Low の設計」 |
| SW VO01 Tune | ピッチ補正（Auto）。PSOLA の音程エンジン＋Scale／Key／Speed／Humanize／Vibrato／Formant／Transpose、キー検出の提案。遅延は 1450 サンプル（仕様書の見積もり 512 は低い男声の 1 周期が入らず満たせない） | 設計値は README「音程エンジンと VO01 Tune の設計」。Graph 編集・Detect MIDI・Snap・Reference は画面／MIDI／ARA が要るので保存だけ。Δ ボタンは無い（仕様書の要確認） |
| SW VO02 Tune Rt | ライブ向けのピッチ補正。Key（12）、Scale（Maj／Min／Chr）、Speed（Slow〜Hard）、Humanize、Formant（母音だけ動かす）、Mix。不安定な区間は補正を弱める。遅延 1085 サンプル | 設計値は README「VO02 Tune Rt の設計」。遅延は仕様書の見積もり 128 サンプルとは違う（110 Hz 以上限定の短縮版でも 1 周期ぶん要る）。Key の ♯・♭は 12 段にした |
| SW VO03 Harmony | ハーモニー 4 声（Scale／Fixed／MIDI は Scale 同等）、Interval ±7 度、Level・Pan・Formant・Humanize・Delay。遅延 1450 サンプル | 設計値は README「VO03 Harmony の設計」。遅延は仕様書の見積もり 512 サンプルとは違う。MIDI 入力は未実装（Scale と同じ動作）。Key／Scale は末尾に追加 |
| SW VO04 Doubler | 声部 1／2／4／8、Spread・Timing（遅延 0.5〜30 ms の不規則なゆっくりした動き）・Pitch var・Tone・Mix。遅延 0 | 設計値は README「VO04 Doubler の設計」。フレーズの頭は休止中に遅延を最小に戻し、以後は変化率 0.5 % 以内で戻る |
| SW VO05 Rider | 音楽を外部サイドチェーンで聴いて、ボーカルを Target（音楽に対する相対値）へライド。Range・Sensitivity・Breath skip・Ride・Write automation。遅延 0 | 設計値は README「VO05 Rider の設計」。SW Link は未実装（サイドチェーンのみ）。音楽を 1 秒聴くまで動かない |
| SW VO06 Formant | Pitch ±12・Formant ±5（母音だけ動く）・Character 4 種・Keep timing・Smooth・Mix。遅延 1450 サンプル | 設計値は README「VO06 Formant の設計」。遅延は仕様書の見積もり 512 とは違う。Keep timing Off は声色が音程に追従する設計（タイミングは常に保つ） |
| SW VO07 Vocal Strip | HPF→De-ess→Breath→Body/Presence/Air→Comp→Level→Plate/Echo 送り→Output の順の声のストリップ。遅延 0 | 設計値は README「VO07 Vocal Strip の設計」。DY05・DY02・RV02・DL01 のコアを内部で使用。段間の音量合わせはコンプの自動メイクアップのみ |
| SW VO08 Breath | Reduce／Remove／Mark only、Reduction・Sensitivity・Keep・Fade。規則による息の検出。遅延 1024 サンプル | 設計値は README「VO08 Breath の設計」。学習モデルとフレーズごとの選択は未実装 |
| SW RS01 Denoise | 短時間 FFT（2048）のスペクトル抑圧。Profile・Adaptive（最小値統計）・Reduction・Threshold・Smoothing・Low/High band・Artifact guard・Learn。遅延 2048 | 設計値は README「RS01 Denoise の設計」。共通部品 sw/stft.hpp。定常な純音は雑音として覚える。Low lat は未実装 |
| SW RS03 Dehum | 基本波と倍音のノッチ列（Base 50/60/Auto、Harmonics、Depth、Width、Buzz）。Track で ±2 Hz を追従。遅延 0 | 設計値は README「RS03 Dehum の設計」。Width は Q 60〜8、Buzz は設計値 |
| SW RS04 Declick | AR モデルの励起で検出して補間（Click／Crackle／Both、Sensitivity、Click width、Crackle %、Low guard）。遅延 512。共通部品 sw/ar_repair.hpp | 設計値は README「RS04 Declick の設計」。2 ms 級のクリックは直りにくい。Repair ボタンの役割は未定 |
| SW RS05 Declip | クリップした山の AR 補間（Threshold・Quality・Makeup・Smooth・Detect）。窓内の見積もりを挟んで高次数でも悪化しない。遅延 1024 | 設計値は README「RS05 Declip の設計」。極端に切れた信号（60 % 近く）は改善 2〜5 dB |
| SW RS06 Dereverb | 後部残響の統計的抑制（STFT 1024・Polack 型）。Reduction・Tail length・Early・Smooth・Learn room（Tail へ書き込み）。遅延 1024 | 設計値は README「RS06 Dereverb の設計」。実際の声では残響時間の測定が難しい（無音の隙間が要る） |
| SW RS07 Mouth Noise | RS04 の検出に「語と語の間か」の重み。Sensitivity・Click size・Freq skew・Fade。遅延 512 | 設計値は README「RS07 Mouth Noise の設計」。Click size Large は約 1 ms まで（先読み 512 の制約） |
| SW CR01 Filter | 2 極の ZDF 状態変数フィルタ（LP/BP/HP/Notch、2×OS）。Envelope/LFO/Sidechain で変調。直近 10 秒の範囲に正規化する進化機能。遅延 0 | 設計値は README「CR01 Filter の設計」。4 極は未実装。LFO は 1 小節 1 周期（設計値）。進化機能の On/Off は末尾に追加した cr01.evo.on |
| SW CR02 Stutter | 拍に同期した直前スライスの繰り返し（Grid・Gate・Repeat・Pitch・Reverse・Filter・Mix・16 ステップのパターン）。Randomize は拍位置と入力の立ち上がりで重み付け。遅延 0 | 設計値は README「CR02 Stutter の設計」。4/4 を仮定。Randomize・Clear は UI ボタン用のメソッド（パラメータではない） |
| SW CR03 Granular | 入力の過去から切り出した粒（Cloud/Scatter/Glitch、Grain・Density・Spray・Pitch・Spread・Mix・Freeze input）。Harmony は直近 4 秒の 12 音分布から粒の音程を構成音へ。遅延 0 | 設計値は README「CR03 Granular の設計」。Harmony の On/Off は末尾ではなく表の最後の cr03.evo.on。和音の入力には向かない |
| SW CR04 Freeze | スペクトルを固めて鳴らし続ける（Trigger Hold/Momentary/Auto、Freeze、Blur、Drift、Mix）。ピーク位相ロックで取り込んだ音程とレベルを保つ。原音は遅らせない（遅延 0） | 設計値は README「CR04 Freeze の設計」。MIDI での取り込みは未実装。Mix は製品内で処理 |
| SW CR05 Tape Stop | テープが止まる／立ち上がる／逆回転する（Action・Stop/Start time・Curve・Filter・Trigger）。ホストの小節線がわかれば停止が小節線で終わるよう逆算して開始。遅延 0 | 設計値は README「CR05 Tape Stop の設計」。4/4 を仮定。Start の終わりは 30 ms のクロスフェード |
| SW CR06 One Knob | 6 つの効果（Wide/Warm/Air/Punch/Space/Lo-fi）を 1 つの Amount で動かす。Amount 0 は原音そのもの。Macro は内部値を画面へ。遅延 0 | 設計値は README「CR06 One Knob の設計」。内部チェーンは既存製品の簡略版（Space だけ RV02 のコア）で、曲線は設計値 |
| SW MT01 Loudness | ラウドネスメーター（Momentary/Short-term/Integrated/LRA/True peak、10 分の推移、ARIB/EBU/配信のプリセット）。音は変えない。遅延 0 | 設計値は README「MT01 Loudness の設計」。±1 LU が規格上の許容値かは要確認のまま。アダプターに kDelta=false を追加 |
| SW MT02 Spectrum | スペクトラムアナライザ（FFT 4k〜32k、Speed、Range、Slope、Smoothing、Display、参照との比較）。音は変えない。遅延 0 | 設計値は README「MT02 Spectrum の設計」。ジャンル別の型は未実装 |
| SW MT03 Spectrogram | スクロールするスペクトログラム（Scale Linear/Log/Mel、Scroll、Floor ほか）。帯域のその場試聴（出力が変わる）。遅延 0 | 設計値は README「MT03 Spectrogram の設計」。試聴中は previewActive() で警告 |
| SW MT04 Phase Scope | 位相スコープと相関メーター（全帯域＋8 帯域、負相関 0.7 秒で警告、Persistence・Zoom）。音は変えない。遅延 0 | 設計値は README「MT04 Phase Scope の設計」 |
| SW MT05 Vu Ppm | VU/PPM メーター（Ref −14/−18/−20、VU は 300 ms で 99 %、PPM は IEC Type II 基準の設計値）。遅延 0 | 設計値は README「MT05 Vu Ppm の設計」。SW Link での基準共有は未実装 |
| SW UT01 Gain | ゲイン・バランス・幅・極性・Swap・Mono（Gain は 10 ms で滑らかに、既定値は素通し）。トラックの種類ごとの Gain を覚える。遅延 0 | 設計は README「UT01〜UT03 の設計」。ホストのトラック名の受け取りは未実装 |
| SW UT02 Mono Check | Mono／Side／Left／Right 試聴、Mono fold、Phone speaker 模擬、Low cut。監視専用で、書き出し警告の判定あり。遅延 0 | 設計は README「UT01〜UT03 の設計」。警告の表示は画面と一緒に作る |
| SW UT03 Reference | 参照曲 B／C と入力の A/B（ラウドネス自動合わせ、Crossfade、Loop 区間、ホスト位置に同期）。WAV・AIFF 対応。遅延 0 | FLAC・MP3 は未対応。設計は README「UT01〜UT03 の設計」 |
| SW DY01 FET | FET 型のキャラクターコンプ。Drive（入力 0〜+36 dB）を固定しきい値 −6 dBFS に押し込む。Speed でアタック 800〜20 µs とリリース 1100〜50 ms を連動。Ratio 右端 5 % は Max（硬いニー・無限大）。Bite（立ち上がり後 5〜15 ms だけゲインリダクションを緩める）、Color（Clean／Grit／Crush、2× OS、Crush は 4×）、SC HPF | 検出はフィードバックの静的解をフィードフォワードで計算（下の「DY01 の設計」）。Color の歪みの量は設計値 |
| SW DY02 Opto | 光学式レベラー。Level（しきい値 0〜−40 dBFS）、2 段リリース（速い段が GR の半分、遅い段が残り）、Speed（Fast／Prog／Slow）、Target、Emphasis（検出側の 2 kHz 以上のハイシェルフ）、Ride（EVO：400 ms ラウドネスを Target に寄せる前段フェーダー）、Auto makeup | 検出の比率 3:1・ニー 12 dB と Prog のモデルは設計値（下の「DY02 の設計」） |
| SW DY03 Bus | VCA バスコンプ。段階式の Ratio/Attack/Release、Auto release（100 ms／1.2 秒の2段）、Punch keep（打楽器の頭を 15 ms 通す） | — |
| SW DY04 Gate | ゲート／エキスパンダー（1:2）／ダッカー、4 dB のヒステリシスとホールド、キー HPF/LPF、キー試聴、外部サイドチェーン | 既定の Threshold は −80 dBFS（開きっぱなし、仕様書どおり） |
| SW DY05 De-ess | ディエッサー。高域（Freq 以上）のエネルギー比でサ行かどうかを判定し（音量に左右されにくい）、Threshold を超えた分だけ Range まで下げる。Wide（全帯域）／Split（Freq から上だけ、LR4 で分割して足す）、Lookahead（本線を遅らせる、既定 2 ms＝96 サンプル）、Listen（検出帯域の試聴）、Pitch follow（EVO：有声音の区間では判定を鈍くする） | 検出の重み・遷移幅・有声音の重みは設計値（下の「DY05 の設計」）。Freq の初期位置を声域に寄せる機能は画面と一緒に作る |
| SW DY06 Vari-Mu | 真空管のバリミュー型コンプ。Input（−10〜+20 dB、3.3 で 0 dB）→ 偶数次寄りのチューブ段（2× OS）→ 深いほど比率が上がるゲイン要素（約 1.5:1 から 6:1、Mu で立ち上がりの速さ）。Time 1〜6（アタック 2/2/4/8/4/2 ms、リリース 0.3/0.8/1.5/3 s・Auto 2 種）、Link／Dual、Density adapt（EVO：リリース ×0.5〜×2） | Output は無い（仕様どおり。音量は共通の Auto gain で）。曲線・Auto の 2 段・密度の式は設計値（下の「DY06 の設計」） |
| SW DY07 Snap | VCA コンプ。RMS 検出、深く超えるほど速くなるアタック、120 dB/秒の一定速度リリース、Snap（打楽器の頭を ±6 dB） | Threshold 0 dB ＝ −18 dBFS（仕様書の案） |
| SW DY08 Clean | 透明なデジタルコンプ。Peak/RMS/Program 検出、SC HPF、5 ms 先読み、テンポ同期の Auto release、外部サイドチェーン | — |
| SW DY09 Transient | トランジェントシェイパー。速い／遅い／さらに遅い 3 つの包絡の比（dB）で頭（Attack）と余韻（Sustain）を ±15 dB 足し引き（音量に左右されない）。Speed（Fast／Medium／Slow）、Clip（Off／Soft／Hard、2× OS、0 dBFS）、Mode：Smooth／Split bands（150 Hz・4 kHz の LR4 で 3 帯域に分けて帯域ごとに整形、足すと平ら） | 帯域ごとの ID は dy09.b1〜b3（Low／Mid／High）と解釈。時定数と量の式は設計値（下の「DY09 の設計」） |
| SW DY10 Multiband 4 | 4 帯域マルチバンドコンプ。4 次 LR で 3 か所のクロスオーバー（240 Hz／2 kHz／8 kHz、足すと平ら）、帯域ごとにしきい値・レシオ・アタック・リリース・Range・Gain・Solo・Bypass。クロスオーバー同士は 1 オクターブ以上離す（押し返す） | 帯域の ID は dy10.b1〜b4、クロスオーバーは dy10.x1〜x3。Auto（クロスオーバー解析）は画面と一緒に作る（下の「DY10 の設計」） |
| SW DY11 Multiband 6 | 6 帯域のダイナミクス。帯域分割なし、6 本の「動く」フィルタを直列に置く（遅延 0、帯域ごとに Compress／Expand／Dynamic EQ を混在できる）。Compress・Expand は隣の帯域との中点までを覆う広いベル（両端はシェルフ）、Dynamic EQ は Width のベル。しきい値・レシオ・アタック・リリース・Range・Gain | 帯域の ID は dy11.b1〜b6。ニー 6 dB・検出の RMS 10 ms は設計値（下の「DY11 の設計」） |
| SW DY12 Parallel | パラレル圧縮とアップワード圧縮。Squash（しきい値 0〜−40 dBFS、10:1 固定、−12 dBFS の基準レベルで音量が変わらない自動メイクアップ）、Blend（潰した音の混ぜ量、既定 30 %）、Upward（原音側で小さい音を最大 +12 dB、−60 dBFS 以下は持ち上げない）、Tone（潰した側だけの 1 次の傾き ±6 dB）、Speed（Fast／Med／Slow／Auto） | 共通の Mix は無い（Blend が兼ねる、仕様どおり）。メイクアップは静的、Upward の曲線は設計値（下の「DY12 の設計」） |
| SW MS01 Maximizer | マスタリング用マキシマイザー。Gain（0〜+24 dB）→ 遅い段（Character X：比率 1〜4・ニー 0〜12 dB、Y：アタック 1〜30 ms）→ 速い段（先読み 2 ms の PeakLimiter、True peak 4×）→ TPDF ディザー。Lock（EVO）：出力の Integrated ラウドネスを測って Gain を Target へ寄せ（時定数 10 秒）、30 秒以上・0.3 LU 以内・安定で固定。Ceiling、Release（右端 Auto）、Stereo、Low end guard | 遅延 112 サンプル@48 kHz（先読み 96＋補間 16）。固定した Gain のホストへの書き戻しは画面と一緒に作る（下の「MS01 の設計」） |
| SW MS02 True Peak | 先読みブリックウォール＋標本間ピーク検出（4x/8x）、TPDF ディザー | 下の「MS02 の保証範囲」 |
| SW MS03 Multiband Limit | 4 帯域マルチバンドリミッター。LR4 で分割（120 Hz／1 kHz／6 kHz、足すと平ら）、帯域ごとに Gain（0〜+12 dB）・Ceiling・Release、先読み 2 ms の帯域リミッター、最後に True peak リミッターで Out ceiling を必ず守る。Character（Clean／Punch／Dense）、Link bands（EVO：合成後に天井を超える分を、帯域ごとのピークの大きさに応じて配分） | 遅延 184 サンプル@48 kHz（仕様書の見積もり約 200 より少し短い、下の「MS03 の設計」） |
| SW MS04 Clipper | 直線位相 FIR で 4x/8x/16x、硬いクリップからテープ風まで連続で変わる Knee、Gain match、Listen（削った成分だけを試聴） | 遅延は全倍率で 48 サンプル（仕様書の見積もり 20〜40 より長い） |
| SW MS05 Leveler | 自動フェーダー。K 特性の短時間ラウドネス（Source：Vocal 150 Hz〜5 kHz／Mix 全帯域／Bass 250 Hz 以下の長い窓）を Target へ寄せる Ride（±Range、Speed 3 秒／1 秒／0.3 秒）。Gate 以下では動かない。Write automation On の間は Ride をプラグインが自分で動かし、ホストに操作（ジェスチャー開始・値・終了）として通知する。Off では Ride パラメータ（ホストのオートメーション）がそのままゲイン | Ride を書き出す仕組みを共通のプラグイン層（CLAP）に追加。VST3／AU への伝わり方と DAW ごとの記録の挙動は、主要 DAW での確認が必要（下の「MS05 の設計」） |
| SW MS06 Master Chain | マスタリング用のチェーン。EQ（Tilt ±6 dB・80 Hz／12 kHz のシェルフ・Bell 200 Hz〜8 kHz）・Comp・Saturate・Width（Mono below）・Limit を、並び順 120 通りの 1 つ（オートメーション不可、設定と一緒に保存）で並べて使う。各段に On。Gain match（既定 On）は、各段の出力をチェーン入力のラウドネスに合わせる | Comp 以外の項目は仕様書の案。Reference A/B は監視用のスイッチだけで、参照曲の読み込みと整列は UT03・画面と一緒（下の「MS06 の設計」） |
| SW MS07 Dither | TPDF ディザー＋再量子化、ノイズシェーピング 4種、Auto blank（完全な無音は完全な無音で出す） | シェーピングは (1 − z⁻¹)ⁿ（n＝1〜4、常に安定）。量子化後に音量を変えないよう Auto gain は持たない |
| SW LV01 Voice | LIVE 用の声の 1 ノブ。Use（Narration／Stream／Meeting／Singing）と Voice で Noise（多帯域エキスパンダー）・EQ・Comp・Limit（−1 dBFS）を同時に動かす。遅延 0 | 設計値は README「LV01 Voice の設計」。Limit は先読みなしのためサンプルピークのみ保証 |
| SW LV02 Feedback | LIVE 用のハウリング抑制。FFT 検出（持続と倍音関係）、12 枠のベルフィルタ（FIXED／LIVE）、Ring out、Release で戻る。遅延 0 | 設計は README「LV02 Feedback・LV03 Channel の設計」。操作ボタンの画面は未実装 |
| SW LV03 Channel | LIVE 用のチャンネルストリップ。Trim・HPF・Gate・EQ 3 バンド・Feedback guard・Comp・De-ess・Out、Mic の種類で全体を書き込む。遅延 0 | Copy／Paste は画面と一緒に作る。設計は README「LV02 Feedback・LV03 Channel の設計」 |
| SW LV04 Safety limiter | LIVE 用。遅延0の Zero モード／トゥルーピークモード、長時間 RMS 制限、制限イベントの記録 | True peak モードのイベント記録はブロック単位 |
| SW LV05 Auto ducker | LIVE 用の自動ダッカー。外部キーの声（周期性・ゼロ交差率の規則判定）だけで下げる（Voice only）。Depth・Attack・Hold・Release、Hold to duck。遅延 0 | SW Link のキー選択は未実装。設計は README「LV05〜LV10 の設計」 |
| SW LV06 Stream master | 配信の最終段。3 秒ラウドネスで Target（Stream −14／Podcast −16／Broadcast −24／Custom）へゆっくり寄せ、−1 dBFS 級のピークリミッター、Mono safe、Dead air 検知。遅延 0 | 保証はサンプルピークのみ（画面の dBTP 表記は dBFS が妥当）。下方向 −12 dB は設計値 |
| SW LV07 Speech Agc | 話者レベラー。400 ms ラウドネスで Target へ。Use・Speed・Gate・Talker hold・Freeze、近い人／遠い人の推定で presence と Max gain を補う。遅延 0 | 高域比の基準は合成音声のみで校正。設計は README「LV05〜LV10 の設計」 |
| SW LV08 Room Noise | 室内ノイズ抑制。STFT でスペクトル減算（HVAC）、Voice guard、キー打鍵の抑制、Learn noise。遅延 256 サンプル | Learn の画面ボタンは未実装。設計は README「LV05〜LV10 の設計」 |
| SW LV09 Hum Cut | RS03 のコアを使う LIVE 版ハム除去（Base・Harmonics 1〜16・Depth・Width・Track drift）。Listen で除去成分の試聴。遅延 0 | 設計は README「LV05〜LV10 の設計」 |
| SW LV10 Voice Fx | ライブ用ボイスチェンジャー。VO02 の音程エンジンで Pitch・Formant、Robot（120 Hz 固定）、Preset（Low／High／Robot／Radio／Anon）。遅延 1085 サンプル | 遅延は仕様書の 128 でなく実測どおり 1085。Anon は匿名化を保証しない。設計は README「LV05〜LV10 の設計」 |
| SW LV11 Mic Switch | マイクのオンオフ・無音で自動ミュート・咳ボタン（Live／Push to talk／Off、Silence・Hold・Fade）。Duck others は同一プロセスの他インスタンスを下げる。遅延 0 | SW Link の代わりに同一プロセスの登録簿（sw/link.hpp）。設計は README「LV11〜LV30 の設計」 |
| SW LV12 Geq 31 | 31 バンドのグラフィック EQ（ISO 20 Hz〜20 kHz、Q 4.3、±12 dB）、HPF／LPF、Link L/R、RTA 値、Feedback guard の帯域マーク、Flat。遅延 0 | 右チャンネル用の 31 本を表の末尾に追加（仕様書は 31 本のみ） |
| SW LV13 Live Peq | 6 バンドのライブ用パラメトリック EQ（Bell／Shelf、Freq・Gain・Q）、HPF／LPF、RTA に基づく「ここを削る」候補。遅延 0 | Shelf は 1 kHz 未満ローシェルフ・以上ハイシェルフ（EQ07 と同じ） |
| SW LV14 Align | スピーカーの時間合わせ（0〜500 ms の遅延線、距離表示、気温補正、極性）。Measure は基準との相互相関で遅延を求めて書き込む。報告遅延 0 | 設計は README「LV11〜LV30 の設計」 |
| SW LV15 Auto Mixer | 最大 8 マイクのゲインシェア式オートミキサー（Gain share／Gate、Last mic hold、Off atten、Response、Priority、NOM limit）。遅延 0 | SW Link の代わりに同一プロセスの登録簿。マイク番号は prepare 順 |
| SW LV16 Live Gate | LIVE 用ゲート／ダッカー。Key HPF（120 Hz・24 dB/oct）で床鳴りではゲートが開かない、外部サイドチェーン | 床鳴りの帯域を測って自動で置く学習は未実装（固定 120 Hz） |
| SW LV17 Bus Comp | LIVE 用バスコンプ。Speech／Music／Band で検出方式とニーが変わる（OBS シーン連動は LV27 から Mode を切り替える）、Auto release 2段 | — |
| SW LV18 Pop Guard | ポップ・ノイズ防止（Plug pop／Wind／Handling／Plosive の規則判定、Mute time、検出回数）。遅延 0 | 検出までの最初の 1〜2 ms は通る（先読みなし） |
| SW LV19 Av Sync | 音声と映像のずれ補正（0〜1000 ms、フレーム単位に丸め、Clap sync）。報告遅延 0 | 映像側の手拍子の自動検出は OBS 連携が要る。VST3 版はボタン押下の半自動 |
| SW LV20 Rta | 実時間アナライザー（1/3・1/6・1/12 oct、Slow／Medium／Fast、Peak hold、Z／A／C、ピンクノイズ基準線）。音は素通し。遅延 0 | 画面は未実装 |
| SW LV21 Test Gen | テスト信号発生（Sine／Pink／White／Sweep／極性パルス）。Arm→Output の 2 段階、読み込み後は必ず Off、0.5 秒で立ち上げ、60 秒で自動停止。遅延 0 | 設計は README「LV11〜LV30 の設計」 |
| SW LV22 Polarity | 極性チェッカー（Mic vs mic／Speaker／Line、相互相関の符号、Hold result）。音は素通し。遅延 0 | 基準はサイドチェーン、無ければ L が R を基準にする |
| SW LV23 Loudness | 放送用ラウドネスメーター（MT01 のエンジン、ARIB／EBU／Stream）。Dead air・TP over の状態、1 秒ごとのログを CSV に書き出す。遅延 0 | CSV の項目・書式は仮（納品先の様式は未確認） |
| SW LV24 Live Reverb | 低負荷リバーブ（8 本の FDN、Vocal hall／Room／Plate、Decay、Pre-delay、Tone、Duck）。遅延 0 | 「1% CPU」は実測前のため載せない |
| SW LV25 Live Delay | タップテンポのディレイ（Tap／MIDI／BPM、Feedback、Tone）。入力だけ止めるバイパスで返りは鳴らし切る。遅延 0 | MIDI クロックはアダプターに MIDI 入力が無く、エンジン経由が前提。Input bypass は表の末尾に追加 |
| SW LV26 Mono | モノ互換の安全装置（Width、Low mono、Mono check、帯域ごとの相関で S を減らす Auto phase fix）。遅延 0 | 設計は README「LV11〜LV30 の設計」 |
| SW LV27 Scene Sync | OBS シーンとプリセットの対応表・Learn current・シーン変更の処理・Fade between。音は素通し。遅延 0 | 他インスタンスへの送信（SW Link）と obs-websocket は未実装 |
| SW LV28 Remote Hub | タブレット遠隔操作の安全設計のコア（LAN 限定・6 桁 PIN・試行回数制限とロックアウト・8 時間トークン・端末ごとの権限・Lock all）。音は素通し。遅延 0 | HTTP／WebSocket サーバーと通信の暗号化は未実装 |
| SW LV29 Interp Mix | 同時通訳のミックス（Floor／Interp and floor／Interp、Floor under、Crossfade、通訳の声で会場音を下げる Auto detect）。遅延 0 | 通訳は第 2 入力（SW Link 経由は未実装） |
| SW LV30 Recorder | 常時バックアップ録音（ロックフリーのリング＋書き込みスレッド、WAV 16/24/32f、1 秒ごとのヘッダー更新、RF64、1 時間で分割、印は CSV と cue）。音は素通し。遅延 0 | FLAC とサンプルレート変換は未対応。録音フォルダが選ばれるまで何も書かない |

### 仕様書との照合（v0.5.1）

仕様書（Claude Doc）のパラメータ表と照合した。範囲・既定値・カーブ・オートメーション可否・ID を合わせた。
- EQ05・EQ06・EQ09・MS07：一致していた。
- EQ01・EQ03・EQ04（v0.6.0 で追加）：仕様書の表と処理方式の記述を先に読んでから実装した。
- DY03・DY07・DY08・MS02：既定値と範囲は一致。ID の命名だけ直した（例：`dy08.threshold` → `dy08.thresh`）。DY07・DY08 の Ratio は「右端 5 % は ∞」を表示と処理の両方で実装した。
- DY04：Threshold の既定値を −80 dBFS に直した（仮の値 −40 は誤り）。ID を直し、Listen をオートメーション不可にした。
- MS04：省いていた Listen を仕様どおり追加した（オートメーション不可）。
- LV04・LV16・LV17：仕様書は簡略形式でパラメータ表がないため、画面の値を既定値とした。

ホスト内部の番号（状態の保存に使う）は変えていないので、保存済みの設定はそのまま読める。

### 決定事項（2026-10-06、お任せで確定）

1. **ヘッドルーム。** すべてのアナログ系の段（EQ01・EQ03・EQ04・EQ05・EQ06 の Drive、CS01・CS03 のプリ）は、飽和の上限を +6 dBFS にそろえた。Drive 0 なら通常のレベルでほぼ色が付かない（EQ05 で、ピーク −7 dBFS の音の変化が 0.2 dB 未満であることを確認）。
2. **EQ07 の Shelf／Cut の向き。** 1 kHz 未満なら低域側、以上なら高域側で確定。
3. **EQ08・EQ02 のカーネル長。** 既定は仕様書どおり 2048。Length（2048／4096／8192）を末尾に足し、精度が要るときは遅延と引き換えに長くできる（8192 で 100 Hz・Q4 のベルの誤差 0.1 dB 未満を確認）。
4. **MS モード（EQ01・EQ08）。** 「EQ は Mid だけ、Side は素通し（Linear では同じ遅延で揃える）」で確定。バンドごとの配置がない製品では、これが実用上意味のある唯一の動き。

### Drive 段の設計（EQ01・EQ03・EQ04、v0.11.1）

仕様書は「偶数次寄りの非対称ソフトクリップ1段、2× OS、音量補正つき」。以前は対称の tanh（偶数次が出ない）だったので、次のように直した。ヘッドルーム +6 dBFS（決定事項）は維持。

- 式：`v = g·x/h + b`、`y = h·s·(tanh(v) − tanh(b))/g`、`s = 1/sech²(b)`。g は Drive の入力ゲイン（0〜+18 dB）、h = 2.0。傾きを 1 に正規化してあるので、**小信号の利得は Drive によらず 1**（測定 0.1 dB 未満）。
- **バイアス b は Drive に比例（Drive 10 で 0.3、設計値）。** 動作点を tanh の中心からずらして正負の山を非対称に歪ませる。Drive 0 は b = 0 の対称な tanh で、偶数次は出ない（測定 −195 dB）。最初は b を固定 0.3 にしたが、Drive 0 でも 2 次が −38 dB（0.18 の正弦波）出たため比例に変えた。
- 偶数次が主：1 kHz・0.18 の正弦波の 2 次／3 次（基本波比）は Drive 2 で −48／−56 dB、Drive 5 で −35／−46 dB、Drive 10 で −21／−30 dB（実測）。b の候補は 0.3 と 0.5 を数値で比べ、0.5 は Drive 10 で 2 次が −15 dB と強すぎるため 0.3 とした。ピークをそろえた対称／区分的非対称（正負で飽和点を変える形）は、3 次と 2 次が同程度になり「偶数次寄り」にならなかった。
- **音量補正：** 出力を 1/g するため、入力を上げた分だけ飽和したピークも下がる。−18 dBFS RMS の 1 kHz で Drive 0 に対する RMS の変化は最大 1.4 dB（Drive 10）。小信号の利得 1 を優先したため、補正を足して 1 dB 以内にはしていない（足すと小信号が持ち上がる）。
- **DC：** 非対称で平均値がずれる。2× のループ内で「出力 − 入力」（足された歪み成分）だけを 5 Hz の 1 次ハイパスで DC 除去する。線形の経路は触らないので、低域の位相・振幅は変わらない。0.3 の 1 kHz・Drive 10 で DC は 2 mV 未満。
- 2× OS：15 kHz の 3 次が 3 kHz に折り返す量は −60 dB 未満（テスト）。
- テスト：`tests/test_drive.cpp`（偶数次の有無と 2 次 > 3 次、Drive 0 で 2 次 −55 dB 未満、Drive で 2 次が増える、小信号利得 1、音量差、DC、折り返し、出力が有限で 1 未満）。

### DY01 の設計（仕様書に数値がない部分）

- **検出。** 仕様は「フィードバック型の検出」。実際にループを組むと、1 サンプル遅れの帰還は Ratio 20 でアタック 20 µs（48 kHz で 1 サンプル未満）のとき発振する（時定数の係数 c が (1−c)(R−1) < 1+c を満たす必要があり、c > 0.9 が要る）。そこで、フィードバック則 `GR = −(R−1)·(出力レベル − T)` の静的な解 `GR = −(1−1/R)(入力レベル − T)` を直接評価し、アタック／リリースの動特性をかける。固定しきい値は −6 dBFS（Drive でその手前に押し込む）、ニーは Max 以外 6 dB、Max は無限大のレシオと 0 dB ニー。検出は左右の大きい方（リンク）のピーク。
- **Speed。** アタック 0.8 ms × (0.02/0.8)^Speed、リリース 1100 ms × (50/1100)^Speed（対数補間）。0.5 で 126 µs／235 ms（仕様の約 130 µs／240 ms と一致）。
- **Bite。** 入力の速い包絡（5 ms で戻る）が遅い包絡（30 ms）の 2 倍（+6 dB）を超えた立ち上がりで、ゲインリダクションを `1 − 0.8×Bite` 倍に緩める。保持は 5 ms + 10 ms × Bite（2 ms でなめらかに戻す）。Bite 0 では何も変わらない。
- **Color（設計値）。** 傾き 1 に正規化した非対称 tanh `(tanh(g·u+b) − tanh b)/(g·sech²b)`、歪みで生じる直流は 2× のループ内で 5 Hz ハイパスして除く（Drive 段と同じ方式）。Clean は g 0.5・b 0.08、Grit は g 0.8・b 0.05+0.3×レベル（レベルで偶数次が増える）、Crush は g 0.8+GR(dB)/4・b 0.15（4× OS）。Max のときは全 Color で g に GR(dB)/8 を足す（深くかかるほど歪む）。1 kHz のレベル −3 dBFS・4:1 での 2 次／3 次（基本波比）：Clean −40／−44 dB、Grit −25／−39 dB、Crush −27／−28 dB。+12 dBFS では Crush 3 次 −12 dB まで増える（実測）。
- 遅延 0。Mix は共通枠の Dry と混ぜる（Dry は元の信号で Drive はかからない）。Unit A／B／C と EVO の画面操作は画面と一緒に作る。

### DY02 の設計（仕様書に数値がない部分）

- **静的な特性。** 仕様は Level 0〜10 でしきい値 0〜−40 dBFS（Level × −4 dBFS）。比率とニーは書かれていないので 3:1・ニー 12 dB（設計値。光学式のやわらかいかかりを狙う）。検出は左右の大きい方の RMS（10 ms）。
- **セルのモデル。** 速い段と遅い段の 2 つの積分器が、どちらも同じ 10 ms のアタックで目標のゲインリダクションを追い、リリースだけ別の時定数で戻る。出力のゲインリダクションは 2 つの平均なので、「速い段が GR の半分」を戻したあと遅い段が残りを戻す。Fast は 40 ms／0.5 s、Slow は 200 ms／3 s（仕様どおり）。Fast で GR が半分戻るまでは実測で約 90 ms（2 つの指数の和から求めた計算値は約 78 ms）。
- **Prog（モデル）。** 速い段 60 ms、遅い段は 1 秒 ×（1 + 2 ×「圧縮の記憶」）。記憶は GR の深さに追従（上り 2 秒・下り 6 秒、12 dB で頭打ち）し、10 秒の深い圧縮のあとの 90 % 回復は 0.3 秒の圧縮のあとの 1.5 倍以上かかる（テスト）。数値はすべて設計値。
- **Emphasis。** 検出側だけに 2 kHz のハイシェルフ（0〜+12 dB）。6 kHz と 200 Hz の GR 差は Emphasis 0 で 0.5 dB 以内、12 で 3 dB 以上（テスト）。
- **Ride。** 入力の K 特性ラウドネス（400 ms）を Target に寄せる前段のゲイン。±12 dB、追従 1.5 秒、−50 LUFS 以下では更新しない（保持）。Off にすると 1.5 秒で 0 dB に戻る。**ステレオで左右が同じ信号だと BS.1770 は +3 dB 大きく数える**ため、Target −18 LUFS は片 ch の RMS −20.3 dBFS に相当する（テストで確認）。
- **Auto makeup。** 2 秒平均のゲインリダクションを足して補う（最大 +24 dB）。共通の Auto gain とは別に、出力の音量そのものを変える。
- 遅延 0。EVO の既定は Off。

### DY05 の設計（仕様書に数値がない部分）

- **判定と量。** 何をサ行とみなすか（Freq から上の 4 次ハイパスのエネルギー ÷ 全帯域のエネルギー、4 ms 平均）と、どれだけ下げるか（ハイパス出力のレベルが Threshold を超えた分）を分けた。割合が −14 dB 以下なら 0、−6 dB 以上で 1 の滑らかな重み（設計値）。ハイパス出力のレベルが Threshold より 6 dB 以上高いと Range の全量。割合だけで判定すると静かな息継ぎも下げてしまうため、Threshold を併用している。アタック 0.5 ms、リリース 40 ms（設計値）。
- **Split。** LR4（4 次バターワース 2 段の積）で低域と高域に分け、`低域 + g × 高域` で足す。足した結果の振幅は平ら（Threshold 以下の 100 Hz〜12 kHz の正弦波で ±0.1 dB 以内を確認）が、位相は分割フィルタの全域通過分だけ回る（抑えていないときも）。Wide は `g × 本線` でビット単位で遅延だけ。**LR4 の遷移はなだらか**で、Freq 6.5 kHz・Range −12 dB のとき正弦波の下がり量は 5 kHz −0.3、6.5 kHz −4.1、7.5 kHz −5.9、8 kHz −6.8、10 kHz −9.6、14 kHz −11.6 dB（実測）。Range の全量が効くのは Freq の約 2 倍から上。
- **Pitch follow。** 1 kHz のローパス→約 6 kHz に間引き→ 64 ms の窓で 10 ms ごとに正規化自己相関（70〜1000 Hz）。ピークが 0.6 を超えたら有声音とし、その間は重みを 0.25 倍にする（設計値）。基本周波数は `voiceF0()` で取れる。「歌い手の声域で Freq の初期位置を寄せる」は、パラメータを勝手に動かさない方針で、画面の提案ボタンと一緒に作る。
- **遅延。** Lookahead（0〜5 ms）× レート。変更は次の prepare で反映（再起動を要求）。2 ms の先読みで、サ行の頭の 1 ms のピークが 3 dB 以上下がる（テスト）。

### DY06 の設計（仕様書に数値がない部分）

- **曲線。** しきい値は Threshold 0〜10 ＝ 0〜−30 dBFS。傾き `1 − 1/比率`、比率 = 1.5 + 4.5×(1 − exp(−超過量/W))、W = 30×(4/30)^Mu（Soft 30 dB 〜 Hard 4 dB、設計値）。しきい値の 6 dB 手前から滑らかに入る（12 dB の窓で傾きを 0→全量）。実測（Threshold 10、Time 6、1 kHz 正弦波、Mu 0／0.5／1 で、しきい値の 6／12 dB 上の GR）：−2.7／−3.3／−3.8 と −6.3／−7.7／−8.6 dB（しきい値 0 dB 上は −0.4 dB）。区間ごとの比率は Mu 0.5 で しきい値〜+6 dB が約 1.9:1、+6〜+12 dB が約 3.7:1。6:1 に近づくのはもっと深い（+30 dB 超）ところで、チューブ段が +6 dBFS 付近で飽和するため、実際に使う範囲では 4:1 前後が上限。
- **チューブ段。** `sw::DriveStage`（Drive 段と同じ非対称 tanh）を Drive 3 に固定して、Input のあと・検出の前に置く。−12 dBFS RMS の 1 kHz で 2 次 > 3 次（テスト）。0 dBFS 近くでは奇数次が優勢になる。
- **Time。** アタック 2/2/4/8/4/2 ms。リリース 1〜4 が 0.3/0.8/1.5/3 s の指数の時定数、5・6 は Auto＝2 段（速い段 0.5 s／0.3 s、遅い段 5 s／10 s、遅い段のアタックは 1 s）の平均（Auto の「0.5〜5」「0.3〜10」を速い段・遅い段の時定数と解釈。確認事項）。Time 5 で GR が半分戻るまで 1 秒未満、90 % 戻るまで 2〜12 秒。
- **Density adapt。** 2 秒のクレストファクター（ピーク ÷ RMS）と立ち上がりの数から密度 d = 0.85×clamp((16 − クレスト dB)/8) + 0.15×clamp(立ち上がり/秒 ÷ 6)、リリース倍率 = 0.5×4^d（0.5〜2 倍、1 秒でなめらかに追従）。定常な正弦波は 1.5 倍以上、まばらなクリックは 0.8 倍以下（テスト）。Off では 1.0。
- 遅延 0。Mix は共通枠（Dry は Input もかからない原音）。

### DY09 の設計（仕様書に数値がない部分）

- **検出と量。** 整流した入力を、速い・遅い・さらに遅い 3 つの 1 次の追従（Speed で 0.5/5/50 ms、1/15/150 ms、2/40/400 ms ＝ Fast／Medium／Slow、設計値）にかける。頭＝速い包絡 ÷ 遅い包絡（dB）、余韻＝遅い包絡 ÷ さらに遅い包絡（dB）が負に振れた量。ゲイン（dB）＝ Attack × clamp(頭の比 ÷ 12 dB) ＋ Sustain × clamp(−余韻の比 ÷ 12 dB)、0.5 ms でなめらかに。比で見るので入力が 40 dB 小さくても頭の変化量は 1.5 dB 以内で同じ（テスト）。定常な音は変わらない（±0.2 dB）。
- **効き方（実測）。** 減衰する 200 Hz（時定数 100 ms）で Attack +12 は頭のピークを 6 dB 以上上げ、Sustain −12 は 150〜250 ms 後を約 2.8 dB 下げる（+12 は 3 dB 以上上げる）。比が 12 dB に達する急な立ち上がりだけが全量で、ゆるい立ち上がりや減衰では指定値の一部しか出ない。
- **Split bands。** 150 Hz と 4 kHz の LR4。低域は 4 kHz の LR4 の低域＋高域を通して全域通過分をそろえ、3 帯域を足すと振幅が平ら（60 Hz〜12 kHz で ±0.15 dB）。帯域の値は Split のときだけ使う。Split は LR4 の位相が回るため、整形なしでも波形のピークが少し変わる（振幅は同じ）。分割周波数は固定（仕様の案どおり）。
- **Clip。** 2× OS の tanh（Soft）／±1 でのクリップ（Hard）。−30 dBFS では ±0.1 dB で透明。
- 遅延 0。

### DY10 の設計（仕様書に数値がない部分）

- **分割。** LR4 を 3 か所。低域は 2・3 番目のクロスオーバーの全域通過分（LP＋HP）を、2 番目の帯域は 3 番目の分を通して、4 帯域の和の振幅が平ら（30 Hz〜16 kHz の正弦波で ±0.15 dB 以内）。各帯域の端の応答は LR4 のまま（例：2〜8 kHz の帯域に 3.5 kHz の正弦波を単独で通すと −1.1 dB）。
- **検出。** 帯域ごとに RMS（10 ms）、左右の大きい方（リンク）、6 dB のソフトニー（設計値）。アタックは検出の 10 ms の後に掛かる。Range は GR の下限、Bypass は帯域の圧縮と Gain をどちらも外す、Solo はどれかがオンのとき、オンの帯域だけを出す（Solo はオートメーション不可）。
- **クロスオーバーの距離。** 低い方から `x2 = max(x2, 2×x1)`、`x3 = max(x3, 2×x2)` とし、上限（20 kHz か Fs×0.45）を超えたら、逆に下の方を押し下げる。パラメータの値そのものは書き換えず、有効値だけを変える（保存した値は保たれる）。
- **Auto。** 10 秒の再生から長時間平均スペクトルを取り、耳の感度（K 特性）で重みづけした等分点付近の谷にクロスオーバーを置く、解析ボタン型（区分 B）。他の解析ボタンと同じく画面と一緒に作る。
- 遅延 0。

### DY11 の設計（仕様書に数値がない部分）

- **帯域の形。** Compress・Expand：中間の帯域は、両隣との周波数の幾何平均（中点）までを覆うベル（幅 ＝ log2(上の中点 ÷ 下の中点) オクターブ、アナログのベルの式で Q に換算）、帯域 1 は「帯域 1 と 2 の中点」の低域シェルフ、帯域 6 は「5 と 6 の中点」の高域シェルフ（Q 0.707）。Dynamic EQ：Freq 中心・Width オクターブのベル（両端の帯域も同じ）。
- **検出。** 帯域の形に合わせたバンドパス（シェルフ側はローパス／ハイパス）→ RMS 10 ms、左右の大きい方。ゲイン変化は 16 サンプルごとに係数を更新（EQ07 と同じ方式、補間つき）。
- **動作。** Compress は 6 dB のソフトニーで、しきい値を超えた分を（1 − 1/レシオ）で下げる。Expand はしきい値より下の分を（レシオ − 1）倍で下げる（20 dB 下・レシオ 2 → −20 dB を Range で頭打ち）。Dynamic EQ はベルのゲインが Compress と同じ式で動く。Range は動的ゲインの下限、Gain は常時足す静的なゲイン。
- 何もしない設定（既定：しきい値 0 dB）で 40 Hz〜16 kHz の正弦波が ±0.3 dB 以内（6 本のベル／シェルフが直列で平ら）。遅延 0。

### DY12 の設計（仕様書に数値がない部分）

- **潰す側。** しきい値 −4×Squash dBFS、10:1、6 dB のソフトニー、検出は Program（RMS とピークの混合）、左右の大きい方。メイクアップは「−12 dBFS RMS の基準レベルで失う分」を静的に足す（Squash 5＝しきい値 −20 で +7.2 dB、Squash 10 で +25 dB、Squash 0 は 0）。**静的なので、アタックの間は頭がメイクアップ込みで通る**（Blend 40・Squash 8 のドラムでピークは Dry より最大 +6 dB 以内、テストの範囲）。メイクアップを GR に追従させると、定常な信号で圧縮そのものが打ち消されて 10:1 でなくなるため、静的にした。ヘッドを守る先読みは遅延 0 の仕様で持たない。Speed Auto は 2 段（3 ms／80 ms と 100 ms／800 ms の小さい方）。
- **Upward（原音側のみ）。** 10 ms の RMS が −60 dBFS 以下は 0、−50〜−40 は全量（Upward 10 で +12 dB）、−20 で 0 に戻る滑らかな曲線（設計値）。アタック 20 ms、リリース 100 ms。−50 dBFS で +12 dB、−30 dBFS で約 +6 dB、−6 dBFS で 0、−70 dBFS で 0（テスト）。
- **Tone。** 1 kHz の 1 次ローパスで低域・高域に分け、低域を −Tone dB、高域を +Tone dB（Bright で高域が上がる）。潰した側だけにかかり、Dry は変わらない。
- **出力。** (1 − Blend) × Dry（Upward 後）＋ Blend × 潰した音。Blend 0 で Dry のみ、100 で潰した音のみ。遅延 0。

### MS01 の設計（仕様書に数値がない部分）

- **2 段。** 遅い段は、しきい値（Ceiling − 6 dB）を超えた分を比率 1＋3×X/100（Clean 1:1 〜 Dense 4:1）、ニー 12×X/100 dB で下げる（検出は 10 ms でリリースするピーク、リリースは Release の 3 倍＝20〜1000 ms、Auto は 300 ms）。アタックは 30^(Y/100) ms（Smooth 1 ms 〜 Punch 30 ms。2 ms 時点の遅い段の GR は Smooth の方が 2 dB 以上深い、テスト）。メイクアップは足さない（足すと既定の X 50 で Gain 0 のまま音量が変わるため）。速い段は `sw::PeakLimiter`（MS02 と同じ、先読み 2 ms ＝ 96 サンプル、True peak On なら 4× の補間検出＋余白 16 で遅延 112）で、Ceiling −0.02 dB の余白付き。Auto のリリースは、短い圧縮 40 ms・100 ms 以上続く圧縮は 400 ms。X を上げると、速い段が受け持つ平均の GR が 0.5 dB 以上減る（テスト）。
- **Low end guard。** 遅い段の検出側だけに 120 Hz の −12 dB ローシェルフ。速い段は Ceiling を守るため全帯域のまま（低域を検出から外すと天井を超えうる）。
- **Lock。** 出力（ディザー前）の BS.1770 Integrated（ゲート付き）を、10 秒で古い分が薄れる重み（設計値）で測り、0.1 秒ごとに `Gain += (Target − Integrated) × 0.1 / 10`。2 秒は測るだけ。30 秒以上経ち、Target との差が 0.3 LU 未満、かつ直近 5 秒の Integrated の変動が 0.15 LU 未満になったら固定（以後 Gain は動かない）。実測：−26 dBFS RMS の定常ノイズで Target −14 LUFS に対し 47 秒で Gain 6.05 dB に固定、積算値 −13.8 LUFS。同じ入力なら毎回同じ固定値（テスト）。Lock の On/Off、Target の変更で測り直す。**固定値は `gainDb()` で読める**。パラメータ（Gain）へ書き戻してホストに保存させる処理は、プラグイン層が画面の「Lock」操作と一緒に作る（音声スレッドからホストのパラメータを書けないため）。
- 共通部品に `IntegratedLoudness`（`core/include/sw/loudness.hpp`、BS.1770-4 のゲート付き Integrated：400 ms ブロック・100 ms 刻み、絶対ゲート −70 LUFS、相対ゲート −10 LU、0.1 LU のヒストグラムで固定メモリ）を追加。MT01・LV23 でも使う。
- Low lat（先読み 0.5 ms・IIR 補間で約 24 サンプル）は EVO バー（画面）と一緒に作る。

### MS03 の設計（仕様書に数値がない部分）

- **段の構成と遅延。** 帯域リミッター（先読み 2 ms ＝ 96）→ 合成 → 「配分段」（合成を見る先読み 1 ms ＝ 48 の `PeakLimiter`）→ 最終段（True peak 4×、先読み 0.5 ms ＝ 24 ＋ 補間 16 ＝ 40）。合計 **184 サンプル@48 kHz**（仕様書の見積もり約 200：帯域 96＋最終段約 100 より少し短い。最終段が残りだけを受け持つため先読みを短くした）。Link を切り替えても遅延は変わらない。最終段が Out ceiling を必ず守る（Link On／Off とも、+12 dB のノイズでサンプルピークが Out ceiling −0.1 dB 以内）。
- **Link bands。** 配分段は合成の波形にかかる「あるべきゲイン g」を先読み付きで求める。Link Off では g を合成にそのままかけ（全帯域が一緒に下がる＝従来のポンピング）、On では帯域 i にかけるゲインを `g^αᵢ`、αᵢ ＝ eᵢ·Σe ÷ Σe²（eᵢ は帯域のピーク、立ち上がり即時・50 ms で戻る包絡）とする。**帯域のピークが同相で重なった最悪の場合に和が g 倍になる**条件から出した式で、帯域が同じ大きさなら α＝1（通常のゲイン）、特に大きい帯域だけが多く下がる。実測（80 Hz のバーストが 0.85、2 kHz の定常音 0.5、Out ceiling −1 dBTP）：バースト中の 2 kHz の音量は、Link Off で −9.5 dB、On で −9.0 dB（バーストなしでは −6.3 dB）。最初の案（シェア×4、5 ms の包絡）は、バーストの立ち上がりで無関係な帯域に責任が回り、かえって悪化したので上の式に変えた。配分後の残りは最終段が受ける。
- **Character（設計値）。** 帯域リミッターのリリースの倍率：Clean ×1、Punch ×0.5、Dense ×2。Dense は帯域のリミッターの前に、帯域の天井の 1.25 倍で効く tanh の丸めを置く（ニーの丸さ）。`PeakLimiter` のアタック（先読みの長さ）は変えない。
- **帯域の端の応答。** LR4 のまま（単独の帯域に 500 Hz を通すと隣の帯域に −24.6 dB の漏れがあり、帯域 2 の天井 −12 dB で止めても全体のピークは約 −10 dBFS になる、テストで確認）。
- Low lat（先読み 0.5 ms）は EVO バー（画面）と一緒に作る。

### MS05 の設計（仕様書に数値がない部分）

- **検出。** 入力の K 特性（BS.1770 の 2 段）をかけた信号に、Source ごとの重みを足す：Vocal は 150 Hz ハイパス＋5 kHz ローパス（各 12 dB/oct×2 段）、Mix は K 特性のみ、Bass は 250 Hz ローパス（窓も 400 ms の指数平均で長め。他は 200 ms の指数平均＝約 400 ms の窓、設計値）。ラウドネス ＝ −0.691 ＋ 10 log10(全チャンネルの平均二乗の和)。左右同じ信号は片チャンネルより 3 dB 大きく数える（BS.1770 どおり）。Gate は重みなしの RMS（dBFS）に対して判定し、以下では Ride を保持する。
- **動き。** 64 サンプルごとに、`Ride += (clamp(Target − ラウドネス, −Range, +Range) − Ride) × (1 − exp(−dt/τ))`、τ は Slow 3 秒・Medium 1 秒・Fast 0.3 秒（設計値）。補正は 64 サンプルの中で直線補間。実測：RMS −26 dBFS の定常音で Target −18 LUFS に対し Ride は約 +5.7 dB に収まり、Range 6 で −10 dBFS の音は −6 dB で止まる（テスト）。
- **Write automation。** 共通のプラグイン層（`clap_adapter.hpp`）に「コアが自分でパラメータを動かす」仕組みを追加した。コアが `takeParamWrite(id, plain)` を実装すると、各サブブロックの終わりに CLAP の出力イベントとして、ジェスチャー開始 → 値（0.02 dB 以上動いたとき）→ ジェスチャー終了を出す。On の間、Ride パラメータへのホストからの値はコアが無視する（自分が書いた値の戻りと衝突しないため）。Off にすると Ride はホストのオートメーションに従い、1 サブブロックの補間でつながる。VST3 は clap-wrapper が出力イベントを編集開始／値変更／編集終了に、AU もパラメータ変更通知に変換する（clap-wrapper の機能。実際の DAW で記録されるかは未確認で、主要 DAW での動作確認が必要）。**この項目の動作の保証範囲は、コアの単体テストと clap-validator／Steinberg validator の合格まで。**
- 遅延 0。

### MS06 の設計（仕様書に数値がない部分）

- **段の処理。** 256 サンプルごとのかたまりで、選んだ順に 1 段ずつ処理する（そのため先読みリミッターをチェーンのどこにでも置ける。CS04 は Limit を最後に回していた）。EQ：Tilt は 1 kHz を軸にした低域シェルフ（−Tilt）と高域シェルフ（＋Tilt）、Q 0.5。Low／High は 80 Hz／12 kHz のシェルフ Q 0.707、Bell は Q 0.7。Comp：プログラム検出、ニー 6 dB、リリース Auto は 100 ms と 1.2 秒の 2 段（DY03 と同じ方式）、Mix は並列。Saturate：Drive 段（`sw::DriveStage`、非対称 tanh）に Drive dB ÷ 1.8 を渡す、Mix は並列。Width：サイド × Width ％。Mono below：サイドを LR4 のハイパスに通し、ミッドは同じ分割の全域通過（低域＋高域）を通して位相関係を保つ（100 Hz の逆相が −12 dB より小さくなり、3 kHz は変わらない、テスト）。Limit：先読み 1.5 ms ＋ True peak 4×（`PeakLimiter`、MS02 の既定値）。
- **既定値。** すべて何も変えない値で、EQ・Comp・Width・Limit が On、Saturate が Off。Limit が On のとき遅延 88 サンプル（72＋16、@48 kHz。仕様書の「約 100」）、Off で 0。Limit の On／Off は遅延が変わるので、次の prepare で反映（再起動を要求）。Limit を Off にしても遅延は保ち、リミッターが働かないだけ。
- **Gain match。** チェーン入力と各段の出力（補正前）の K 特性の平均二乗（3 秒の指数平均）を比べ、差を打ち消す補正を段ごとに足す（±12 dB、追従 2 秒、Off の段は 0 dB）。どの段を外す・並べ替える・強く動かしても全体の音量は変わらず、25 秒の定常ノイズで EQ・Saturate・Limit を動かしても Integrated は入力と 0.7 LU 以内、Off では 2 LU 以上ずれる（テスト）。
- **並べ替え。** 並び順の変更は、1 かたまり（256 サンプル ＝ 約 5 ms）で音を下げ、入れ替え、次のかたまりで戻す。
- **Reference A/B。** 切り替え用のパラメータ（オートメーション不可）だけ。参照曲のラウドネス合わせは UT03 と共通の処理を作るときに実装する。

### SA01 の設計（仕様書に数値がない部分）

- **ID。** 簡略形式の製品は `製品コード.名前`（小文字、空白除く）。`Speed ips` は `sa01.speed`（値は 7.5／15／30）。
- **飽和。** 入力を 0 VU ＝ −18 dBFS（Formula の倍率で動かす）で割った磁界 p に、バックラッシュ（play 演算子）をかけたあと `tanh(g·p)/g`（小信号の利得 1、2× OS）。g ＝ 0.05 ＋ 0.0527×Saturation^1.34。バックラッシュの幅は ＝ 0.08×min(Saturation/3, 2)×e²/(1+e²)（e は直近のレベル、5 ms）で、信号レベルの二乗で増えるので小信号は線形（バイアスが線形にする、の見立て）。最初は幅をレベルに比例させたため、−60 dBFS でも一定の歪み（3 次 −30 dB）が出て、二乗に直した。実測（1 kHz、3 次、基本波比）：Saturation 0 は −18 dBFS RMS でも −68 dB、3 は −32 dB、10 は −17 dB（−18 dBFS RMS、ゲインは −4.1 dB）。−50 dBFS では 3 で −59 dB、利得 0.0 dB。奇数次が優勢（2 次 +6 dB 以上低い）。
- **Formula。** ヘッドルームの倍率 A 1.0／B 1.41（+3 dB、きれい）／C 0.71（−3 dB、歪みやすい）（設計値、汎用名のまま）。
- **Repro。** Speed ごとにヘッドバンプのベル（7.5 ips：50 Hz +2.5 dB、15：70 Hz +2、30：100 Hz +1.5、Q 1）と高域のローパス（12／16／22 kHz、2 次）を再生側として通す。Off は平ら。
- **Wow・Flutter。** 中心 1 ms（48 サンプル@48 kHz）の補間つき（4 点 Hermite）可変遅延。Wow は 0.62 Hz と約 1.4 Hz の揺れ、最大振れ 0.4 ms（Wow 10）、Flutter は 6.1／約 9.7／14.3 Hz、最大振れ 16 µs。ゆっくりした不規則なずれ（0.35 秒）が揺れの周波数を少し動かす。遅延の報告値は常に 48（2× OS の数サンプルの群遅延は報告しない、共通章の方針どおり）。
- **Hiss。** Off（−90）で完全な無音、−50〜−90 dBFS で RMS がその値（±1.5 dB、テスト）。高域寄りの形（白色 0.3 ＋ 1.5 kHz ハイパス）、Speed が遅いほど +3 dB 付近まで増える（7.5：×1.4、15：×1、30：×0.7）。左右は別のノイズ。
- **Calibrate。** `startCalibrate()` から 5 秒の入力の平均二乗を測り、Input ＝ −18 dBFS − 平均レベル（±12 dB）。結果は `takeParamWrite()`（MS05 と同じ仕組み）でホストに書き戻される。−24 dBFS RMS のノイズで +6 dB（±0.7）、−50 dBFS では +12 dB で頭打ち（テスト）。ボタンは画面と一緒に作る。

### SA02 の設計（仕様書に数値がない部分）

- **共通部品。** `core/include/sw/shaper.hpp` の `BiasShaper2x`（`y = h·s·(tanh(g·x/h + b) − tanh b)/g`、2× OS、歪みで出る直流は 2× のループ内で 5 Hz ハイパス、小信号の利得 1。テスト `tests/test_shaper.cpp`）。SA03・SA04・SA06 などでも使う。
- **色。** 低域（160 Hz 以下の 1 次分割）と残りを別の整形にかける。Iron：低域にドライブ +6 dB（Drive 4 で全量）・バイアス 0.05、Clean：ドライブ ×0.5、Punch：ヘッドルーム 1.5（硬め）＋ 6 kHz の +1.2 dB シェルフ、Vint：低域 +3 dB・バイアス 0.45（偶数次が主）＋ 13 kHz ローパス＋ 60 Hz の +1.5 dB。Drive 0〜10 ＝ 0〜+15 dB（設計値）。実測の傾向（1 kHz、−12 dBFS RMS、Drive 8）：Clean は Iron より 3 次が 4 dB 以上低く、Iron の 60 Hz は 1 kHz より 3 次が 6 dB 以上高く、Vint は 2 次が 3 次より大きい（テスト）。
- **Crosstalk。** 漏れ ＝ 10^((−80 ＋ 4×値)/20)（0 で −80 dB、10 で −40 dB、実測 −40±2.5）。Noise は −100〜−70 dBFS RMS（Off は 0、白色、左右別、個体差 ±1 dB）。Width は M/S のサイドを 0〜1.5 倍。
- **個体差（EVO）。** 種（1〜65535）はパラメータではなく**隠れた状態**で、インスタンスを作ったとき（時刻・カウンタ・アドレスから）決まり、共通のプラグイン層が状態の末尾（`SWX1`＋長さ＋バイト列）に保存・復元する（`saveExtra`／`loadExtra`。以前の状態は末尾がなくても読める）。最初はパラメータ（`sa02.seed`）にして、最初の処理でホストへ値を書き戻したが、clap-validator の `param-set-wrong-namespace`（パラメータ値が勝手に変わらないこと）が不合格になったため、この形に変えた。種から、左右別のゲイン ±0.3 dB、飽和の始まり ±0.5 dB（ドライブの差）、ノイズ ±1 dB、トーンの角 ±3 % を作る（同じ種なら出力はビット単位で同じ、種が違えば違う。テスト）。そのため Width 0 の左右逆相でも、ゲイン差の分（約 −30 dB）が残る。Unit A／B／C はこの上に重ねる共通機能で、後から。
- **Group。** 同じプロセス内で Group 番号が同じインスタンスが、ロックフリーの共有領域（8 グループ × 64 スロット）に自分の平均二乗（約 100 ms の平滑）を出し、互いの合計を読んで **ドライブを最大 +3 dB 増やす**（3·tanh(√(他の合計) ÷ 0.3)、設計値）。別のグループには影響しない（テスト：隣が −6 dBFS で 3 次が 0.5 dB 以上増える）。ホストがプラグインを別プロセスで動かすと届かない（SW Link と同じ制約）。
- 遅延 0。

### SA03 の設計（仕様書に数値がない部分）

- **整形。** `BiasShaper4x`（`BiasShaper2x` を 2 段重ねた 4× OS、`core/include/sw/shaper.hpp`）。ドライブ ＝ Drive × 2.4 dB × 管ごとの倍率（12AX7 1.0、12AT7 0.8、EL34 0.7）、バイアス＝ 管ごとの中心値（0.30／0.18／0.08）× 2 × Bias（Cold 0、中央＝その管の値、Hot＝2 倍）、ヘッドルーム 2.0／2.0／1.4（設計値）。小信号の利得 1（−50 dBFS で ±0.15 dB）。
- **実測**（1 kHz、−12 dBFS RMS、Drive 6、EVO Off、基本波比の 2 次／3 次）：12AX7 −20.0／−26.2 dB（偶数次が先）、12AT7 −26.0／−30.2 dB、EL34 −31.9／−26.7 dB（奇数次が先）。Bias Cold は 2 次が −80 dB 未満、Hot は −35 dB より大きい（テスト）。
- **動くバイアス。** 入力のピーク包絡（アタック即時、50 ms で戻る）に応じてバイアスを `0.35×tanh(4×包絡)` だけ足す。Drive 5・1 kHz の 2 次は、−26 dBFS RMS → −6 dBFS RMS で、Off では −33.8 → −18.9 dB（+14.9 dB）、On では −31.6 → −12.6 dB（+19.0 dB）。大きいバーストのあと 50 ms の時定数で戻る。仕様書の表に項目がないため、スイッチ `sa03.evo.on`（Off／On、既定 On：製品の売りのため）を末尾に足した（共通章の「EVO スイッチ（案）」）。
- **Tone。** 1 kHz の低域シェルフ（−Tone）と高域シェルフ（＋Tone）、Q 0.5（MS06 の Tilt と同じ）。
- 遅延 0。Mix・Output は共通枠。

### SA04 の設計（仕様書に数値がない部分）

- **信号の流れ。** Pad（−20 dB）→ Gain（目盛り − 30 dB）→ Low weight（120 Hz のローシェルフ 0〜+6 dB）・Top air（8 kHz のハイシェルフ 0〜+6 dB）→ トランスの等価回路（低域の損失 ＝ 角 12 Hz 付近のハイパス、高域の共振 ＝ ベル）→ 分割飽和。Gain は信号をそのまま持ち上げるので、大きく上げるほど飽和に深く入る（プリアンプの挙動）。小信号の利得は目盛り 30 で 0 dB、40 で +10 dB、0 で −30 dB（±0.5 dB、テスト）。
- **Iron（設計値）。** 飽和の天井（Nickel 3.0／Steel 2.0／Mu 1.0、直線値）、バイアス（0.03／0.05／0.10）、低域の角（8／12／18 Hz）。**低域（150 Hz 以下の 1 次分割）の天井は高域の 1/3**で、低域ほど早く飽和する（50 Hz の 3 次は 1 kHz より 6 dB 以上多い、テスト）。−30 dBFS RMS・Gain +12 dB・1 kHz で Mu ＞ Steel ＞ Nickel の順に 3 次が 2 dB 以上ずつ増える。
- **Load（EVO）。** 高域の共振：中心 26 kHz（上限は Fs×0.45）→ 14 kHz、ゲイン 0 → +4 dB、Q 1.2。低域：ハイパスの角を（1＋1.5×Load）倍、50 Hz のローシェルフを −1.5×Load dB。Load 1 は 0 に比べ 16 kHz が +2 dB 以上、30 Hz が −1 dB 以下、1 kHz は ±0.7 dB 以内で同じ（テスト）。既定（0.5）で 100 Hz〜10 kHz は ±0.8 dB 以内で平ら。
- 遅延 0。

### SA05 の設計（仕様書に数値がない部分）

- **倍音。** Tune 以上（LR4 のハイパス）の帯域 x を、帯域の平均二乗 ms（10 ms）で正規化して 2× OS で作る。偶数次 ＝ (x² − ms)/√ms（正弦波なら 2 次だけで基本波比 −3 dB）、奇数次 ＝ 1.4×(x³/ms − 1.5x)（3 次だけ、基本波は引く、基本波比 −3 dB）、Both は両方の和。生成後に Tune で再度ハイパスして、Tune 未満の混変調と直流を落とす。Harmonics ％ がそのまま倍音の量（35 → 100 ％ で +9.1 dB、テスト）。入力が 25 dB 小さくても倍音の相対量は ±1.5 dB 以内で同じ。Tune の下の音（2 kHz）は 7 kHz より 20 dB 以上少ない。Mix は共通枠の Dry／Wet で、既定 25 ％ のとき、足される倍音は Harmonics × 25 ％。
- **Low drive／Mono low。** 200 Hz 以下（ローパス 2 次×2）を同じ生成器にかけ、120 Hz のハイパスで直流と基本波を落として、Low drive ％ × 0.5 で足す（100 ％ で 100 Hz の 2 次が基本波比 −9 dB）。Mono low が On のときは左右の和（ミッド）から作り、同じものを両チャンネルに足す。
- **Auto fill（EVO、区分 B）。** `core/include/sw/bandlevels.hpp` の `ThirdOctaveAnalyzer`（31 バンド、25 Hz〜20 kHz、4096 点・Hann・50 % 重なり、1 秒の平滑、一般の用途に使えるよう共通部品にした。テスト：1 kHz の正弦波が自分のバンドに出る／ピンクノイズは平ら／白色ノイズは 1 バンドごとに約 1 dB 上がる）で入力の左右の和を測り、100 ms ごとに更新。Tune/8〜Tune/1.2（200 Hz 以上）のバンドの平均を基準に、目標は Tune/1.2 から 1.5 dB/oct で下がる線とし、足りない分（目標 − 実測、0〜+12 dB）を 3 領域（Tune〜2 倍、2〜4 倍、4 倍以上）の平均で出して、倍音側の 3 つのフィルタ（ベル 1.4×Tune、ベル 2.8×Tune、ハイシェルフ 5×Tune）に 1 秒で追従させる。8 kHz でカットしたノイズで 10〜15 kHz の倍音が Auto fill で 2 dB 以上増え、ピンクノイズに近い入力では変わらない（±2 dB、テスト）。
- 遅延 0。

### SA06 の設計（仕様書に数値がない部分）

- **ID。** `sa06.b1.type` 〜 `sa06.b3.mix`（帯域ごとに type・drive・shape・bias・dynamics・mix）と `sa06.tone`・`sa06.output`。共通部品 `Lr4Split3`（`core/include/sw/lr4split.hpp`）で、低域に高い方の分割の全域通過分をそろえ、3 帯域の和の振幅が平ら（50 Hz〜14 kHz で ±0.2 dB、テスト）。Drive 0・Bias 0 のときは帯域をそのまま足す。
- **形（どれも原点の傾き 1、Shape ごとに）。** Tape：`c·u/(1+|u/c|^k)^(1/k)`、Soft c 1.0・k 2、Medium 0.7・4、Hard 0.5・8（硬いほど天井が低く、膝が鋭い）。Tube：`h·s(tanh(u/h + 0.3) − tanh 0.3)`、h 2.0／1.4／1.0（偶数次が主）。Diode：正側 `ln(1+a·u)/a`、負側 `−ln(1+0.5a·|u|)/(0.5a)`、a 1／3／8（非対称、2 次・3 次の両方）。Fold：`sin(k·u)/k`、k 1／2／4。Fuzz：正側 `tape(u, k)`、負側は天井 0.6、k 2／4／12。実測（1 kHz、−12 dBFS RMS、Drive 12、基本波比）：Tape Soft は 3 次 −18 dB・5 次 −33 dB、Hard は 3 次 −12 dB・5 次 −20 dB（偶数次は −110 dB 未満）、Tube は 2 次 −21 dB・3 次 −31 dB（Soft）、Diode は 2 次 −27 dB・3 次 −25 dB（Soft）。
- **Bias。** 動作点を 0.5×Bias ずらす（入力に足す）。式は `(f(u + 0.5·Bias) − f(0.5·Bias))` をその点の傾きで割り、小信号の利得を 1 に保つ。形が足した平均値（Bias・非対称が生む直流）は 5 Hz の 1 次で取り除く（足した分だけを引くので線形部分は変わらない）。Tape の Bias 1 で 2 次が −40 dB より大きくなり、Bias 0 では −80 dB 未満（テスト）。
- **Dynamics（EVO）。** 帯域のピーク包絡（10 ms で戻る）が −18 dBFS を基準に ±18 dB で ±1 になる量 m を取り、ドライブに `Dynamics × 2.4 dB × m` を足す（±5 で ±12 dB）。＋なら大きい音ほど、−なら小さい音ほど歪む。Drive 12 の Tape で、20 dB 大きくしたときの 3 次の増え方が、Dynamics ＋5 で +3 dB 以上、−5 で −3 dB 以上、0 と比べて変わる（テスト）。
- **OS。** 4×（2 段）、Fold と Fuzz は 8×（3 段、仕様書の推奨どおり）。Drive は 20 ms でなめらかに動く。Drive の分だけ音量が上がる（補正は Output で）。
- 遅延 0。

### SA07 の設計（仕様書に数値がない部分）

- **信号の流れ。** 入力 → Wow（共通部品 `WobbleDelay`＝`core/include/sw/wobble.hpp`、中心 1 ms ＝ 48 サンプル@48 kHz 固定、4 点 Hermite、SA01 と同じ揺れ。最大の振れ 0.4 ms＋Flutter 成分 16 µs（Tape 年代は 2 倍））→ 年代ごとのローカット（150／60／30／40 Hz）→ Bandwidth（LR4 のローパス、右端 Full の 20 kHz はバイパス）→ Mono（サイドを 1 − Mono ％ に）→ Crackle・Dust を足す。Mix は共通枠。
- **Crackle。** 1 秒あたり Crackle/10 × 30 個のランダムなパチッ（確率はサンプルごと）。大きさは 0.3〜1.0（二乗の分布）、10 で最大 −24 dBFS（3 で −38 dBFS）、4 kHz・Q 2 のバンドパスで鳴らす。実測：Crackle 10 の無音入力で RMS −61 dBFS、4 秒に 20〜800 個。**Dust。** 1 秒あたり Dust/10 × 600 個の 1 サンプルの粒（符号ランダム）、最大 −50 dBFS（0 で −66）、RMS は Dust 10 で −75 dBFS より大きい。乱数は prepare で種を固定するので、書き出しは毎回同じ（テスト）。
- **Era。** 1950：Crackle 6・Wow 4・Bandwidth 4.5 kHz・ローカット 150 Hz、1970：3・2・10 kHz・60 Hz、1990：0・0.5・16 kHz・30 Hz、Tape：0・3・12 kHz・40 Hz（設計値）。Era を変えると、3 つのパラメータ値と、`takeParamWrite()` の 3 回の書き込み（それぞれジェスチャー開始・値・終了）が出る。**Era のあとで同じイベントの中、または手で指定された値は、そのパラメータの保留中の書き込みを取り消す**（最初は取り消さず、clap-validator の `param-set-events` が「flush と process で値が同じ」を満たさず不合格になった）。状態を読み込んだ直後は `snapToTargets()` で保留中の書き込みを捨てるので、保存された Crackle などが Era のプリセットで上書きされることはない。共通のプラグイン層は、1 回の処理で最大 8 個のパラメータ書き込みを出せるようにした。
- 遅延 48 サンプル固定（Wow が 0 でも同じ。2× OS は使わず、実際の遅延も 48〜52 サンプル以内、テスト）。

### SA08 の設計（仕様書に数値がない部分）

- **信号の流れ。** 入力 → Pre filter（LR4 ローパス、遮断 0.45 × Rate）→ サンプルホールド（周期 fs / Rate サンプル、小数の端数は繰り越す）→ 量子化（Bits）→ Post filter（同じ LR4）。Mix は共通枠。Rate が fs（48000）以上のときはホールドを行わず量子化だけ（Rate の最大は 48000 Hz 固定。96 kHz 以上のホストでは最大でも fs の半分）。
- **量子化。** 振幅 ±1 を 2^(Bits−1) 段に割り、四捨五入（中央が 0 の方式）。Bits 1 は −1／0／＋1 の 3 値、Bits 24 は 2^−23 刻みで float の入力とほぼ同じ（差 1e-6 未満、テスト）。Bits は整数に丸める。8 bit・振幅 0.7 の正弦波で信号対量子化雑音比 約 46〜51 dB（理論値 6.02 × 8 + 1.76 − 3.1 dB。テストの範囲）。
- **Jitter。** ホールドの周期を毎回 ±(Jitter/100 × 50) % ランダムに振る（Jitter 100 ％で ±50 %、周期は最小 1 サンプル）。平均周期は変わらない。乱数は prepare で種を固定（書き出しは毎回同じ、テスト）。左右は同じ周期で動かす（像が左右にずれないように）。
- **Dither。** 量子化の直前に ±1 LSB の三角分布の雑音を足す。振幅 0.25 LSB の 1 kHz 正弦波が、Off では消え（−100 dB 未満）、On では ±3 dB で残る（テスト）。
- **Tempo lock。** ホストのテンポ（拍の周波数 bpm / 60 Hz）が分かるとき、Rate を拍の周波数の最も近い整数倍に寄せる。テンポが無いときは何もしない。Rate のつまみの値は動かさず、実際に使う値だけを変える（CLAP の `param-set-events` を守るため。パラメータ ID は `sa08.evo.on`）。仕様書の「要確認」どおり、画面にスイッチは無い。
- ホールドの周期が整数サンプルにならない Rate では、ホールドの切り替わりが最も近いサンプルになる（補間はしない。それがこの効果の音）。遅延は 0。

### LO01 の設計（仕様書に数値がない部分）

- **信号の流れ。** LR4 で Frequency を境に低域と残りに分ける（足し戻すと振幅は平坦、テスト：40 Hz〜8 kHz で ±0.1 dB）。低域から倍音を作り、Frequency の半分より下を 2 次ハイパス 2 段で捨てて（DC・サブを残さない）足す。出力 ＝ 残り ＋ Original × 低域 ＋ 倍音。
- **倍音の作り方。** 低域をピークフォロワ（立ち上がり即時・戻り 0.3 秒）で割って ±1 に正規化し、チェビシェフ多項式 T2〜T5 を重み 1／0.7／0.5／0.35 で足し、フォロワの値を掛け戻す。入力が正弦波なら T_k はちょうど k 次倍音だけを出すので、**倍音の量は入力の大きさに依存しない**（入力 −20 dB 下げても倍音比 ±1 dB、テスト）。重みは使う次数の二乗和が 1 になるよう正規化し、Harmonics 100 ％で倍音全体の RMS が低域の RMS と等しい（30 ％で −10.5 dB）。Width は最高次数：Narrow 2〜3 次、Medium 2〜4 次、Wide 2〜5 次（仕様書は「倍音の帯域」とだけ書いてあるため設計値）。Width を替えても倍音の総量は変わらない（±1 dB、テスト）。
- **実測（50 Hz・0.3、Frequency 80 Hz、Harmonics 100 ％、Wide）。** 2〜5 次が低域に対し −2.7／−5.8／−8.7／−11.7 dB（設計値の ±1.5 dB 以内）。6 次（300 Hz）は 2 次より約 28 dB 低い。フォロワの戻りが 0.15 秒だと 23 dB だったので 0.3 秒にした（戻りの揺れが 2 倍周期で次数の境目を濁すため）。Narrow／Medium の次数の外側は 2 次より 26〜35 dB 低い。
- **2× OS は使っていない（仕様書からの差）。** 倍音は 5 次の多項式で、入力は 200 Hz 以下の LR4 ローパス後（2 kHz で −80 dB 以下）なので、折り返しが出る周波数に成分がない。10 kHz の正弦波を入れても 20 kHz・100 Hz に −70 dB 以上の成分が出ないことをテストで確かめた。
- **Preview（監視用・Auto 不可）。** Phone safe：300 Hz の LR4 ハイパス＋1.2 kHz に +3 dB・Q 2.5 の小さな山（設計値）。60 Hz の正弦波が 30 dB 以上下がり、Harmonics 100 ％だと Harmonics 0 より 15 dB 以上大きく聞こえる（テスト）。Club：28 Hz の 2 次ハイパスだけ（50 Hz −0.4 dB、15 Hz −11 dB）。どちらも常に動かしておき、10 ms で切り替える（クリック防止）。Preview は書き出しにも乗るため、On のままだと警告を出す仕様は画面側の作業（UI）で、DSP 側にはない。
- 無音入力は完全な 0。−90 dBFS より小さい入力は倍音を作らない（T2・T4 は入力 0 でオフセットを持つため）。左右は独立に処理する。遅延 0。

### LO02 の設計（仕様書に数値がない部分）

- **信号の流れ。** 中央（L＋R）/2 → 20 Hz ハイパス → LR4 ローパス（検出帯域の上限 ＝ Range Hz × 2）→ シュミット式のゼロ交差検出（ヒステリシスは帯域レベルの 10 ％、レベルが −66 dBFS 未満なら検出しない）。上向きのゼロ交差ごとに周期 T を更新し（前の周期から 30 ％以内なら 0.7／0.3 で平滑、それ以外は置き換え。直前の 0.6 T より短い交差は 2 回まで無視して倍音による二重検出を避ける）、フリップフロップを反転する（＝1 オクターブ下の分周）。
- **混成方式。** 分周の矩形波をそのまま使わず、周波数 2^(Tune/12) / (2T) の正弦波発振器をフリップフロップの端に引き寄せる（端ごとに位相誤差の半分を補正、Tune が 0 のときだけ）。出力は純音で、ゼロ交差が低音のゼロ交差に乗る（遅延 0）。**検出系（ローパス＋ハイパス）が低音より遅れる分は、解析式（アナログ原型の位相）で補正して位相を進めている**（60 Hz・Range 90 の遅れは約 28°＝約 1.3 ms。補正しないと 62 サンプルずれる）。実測：60 Hz の正弦波に対し、サブの上向きゼロ交差と低音の交差は 2 サンプル以内（テスト）、サブの周期 1600 サンプル ±4（テスト）。
- **Sub の値。** 仕様書は「0〜10（0〜+12 dB）」。既定が 0 で「効果なし」にするため、0 ＝ サブなし、10 ＝「低域の量が +12 dB 増える」と読んだ（設計値）。低音とサブが無相関として、サブの振幅 ＝ √(r² − 1) × 帯域のレベル（r ＝ 10^(12 × Sub/10 / 20)、Sub 10 で 3.85 倍）。60 Hz・0.1 の正弦波で 30 Hz の成分が設計値の ±2 dB 以内（テスト）。帯域のレベルはピークフォロワ（立ち上がり即時・戻り 0.12 秒）。
- **Tune。** 整数に丸める（−12〜+12 st）。0 が 1 オクターブ下、+12 で入力と同じ周波数、−12 で 2 オクターブ下。Tune が 0 以外のときは低音との位相固定をせず、検出した音程だけに追従する（60 Hz 入力で −12／−5／+7／+12 st が 15／22.45／44.9／60 Hz の ±2 ％、テスト）。**Punch。** 立ち上がりを検出（速いフォロワ ÷ 遅いフォロワ が 1.8 を超えた分）して、約 40 ms かけて戻る最大 2 倍（+6 dB）の持ち上げ。持続部は変わらない（±0.5 dB、テスト）。**Dry。** 入力の音量（0〜100 ％の振幅。0 ＝ Off、100 ＝ Full、50 ％で −6.02 dB）。サブは左右に同じ量を足す。
- 音程の変化に追従（55 Hz → 41 Hz で 27.5 Hz → 20.5 Hz、テスト）。無音は完全な 0、DC には反応しない。0.2 秒以上レベルが検出の下限を下回ると追跡をやり直す。進化機能（ベースの音程追従）は常に働く仕組みなので専用のスイッチは持たない。

### LO03 の設計（仕様書に数値がない部分・仕様書と画面の差）

- **画面との差。** 画面（04）の Tight の既定は 60 ％、仕様書は 50 ％。**仕様書の 50 ％を採用**（決定済みの仕様を優先。画面側の見直しは依頼者に委ねる）。Mono below は Off ＋ 20〜300 Hz で、MS06 と同じく範囲の左端（20 Hz）が Off。
- **Focus（動的ベル、Q 1.2、32 サンプルごとに目標を更新）。** 帯域の検出は Focus に合わせたバンドパス（Q 1.4）。Role ごとの動き（最大の深さは Tight 100 ％のとき。深さは設計値）：**Kick ＝ 尾を締める。** 帯域のレベルが直近のピーク（戻り 0.5 秒）からどれだけ下がったかに応じて最大 −9 dB（下がり 25 ％までは不感帯、持続音は動かさない）。実測（55 Hz・0.5 のキック風の減衰音、Tight 100 ％）：叩いた直後のピークは −6.2 → −6.5 dB（ほぼ同じ）、叩いてから 250〜800 ms の尾は −27.3 → −32.0 dB。**Bass ＝ キックの立ち上がりで下げる。** サイドチェーンの低域（120 Hz 以下）で、速いフォロワが遅いフォロワの 2.2 倍を超えたとき（かつ −60 dBFS 超）に最大 −6 dB、約 50 ms の時定数で戻る（アタック 1 ms）。実測（55 Hz のベース、60 Hz のキック風）：キック直後 −13.3 → −19.6 dB、0.4 秒後は −13.5 → −13.8 dB。**サイドチェーンが無いときは下げない**（仕様書の要確認どおり、接続が必要。SW Link は未実装）。**Both ＝ 両方。** 尾を締める（最大 −6 dB）＋自分の低域の立ち上がりで下げる（最大 −4 dB、アタック 4 ms でキック自体は通す。ベースの音の頭も「立ち上がり」と見なされるので、その音の間だけ少し下がる）。実測：キック直後 −11.8 → −15.5 dB。持続音だけなら 0.5 dB 以内で変わらない（テスト）。合計の下げは最大 12 dB。
- **Mud cut（静的ベル、Q 1.0）。** 仕様書に深さがないため、深さを **Tight に連動 ＝ 6 dB × Tight**（既定の 50 ％で −3 dB、0 ％で効果なし）と決めた。250 Hz の正弦波で −6.0／−3.0／0 dB（テスト、±0.5 dB）。Focus の動的な深さも Tight に比例する。**Tight 0 ％ ＋ Mono below Off は入力とビット単位で一致**（テスト）。
- **Mono below。** 側成分を LR4 でハイパスし、中央成分は同じ分割点の LR4 ローパス＋ハイパス（オールパス）を通して位相関係を保つ（MS06 と同じ方式）。60 Hz の側成分は −20 dB 以上落ち、中央と 1 kHz の側成分は ±0.2 dB 以内（テスト）。
- Focus の帯域に合わない音は動かさない（100 Hz の尾に対し Focus 100 Hz は 3 dB 以上、Focus 30 Hz は帯の裾で 1.6 dB 程度、テスト）。左右は同じゲインで動かす（リンク）。遅延 0。

### GT01 の設計（仕様書に数値がない部分）

- **信号の流れ。** 入力 →（Volume match）→ Bright（2.5 kHz のハイシェルフ）→ 4× OS：三極管 1（偏りつきの tanh、偶数次倍音）→ 結合コンデンサ（1 次ハイパス）→ 12 kHz の 1 次ローパス → 三極管 2（Clean は 0.7 倍のごく軽い段、Crunch 3 倍、Lead 6 倍）→ 結合ハイパス → ダウンサンプル → **受動トーンスタック**＋回復ゲイン → 4× OS：出力段（tanh、サグ）→ Presence（3.2 kHz のハイシェルフ、5 で平ら、±6 dB）→ 出力トランスの帯域（40 Hz の 2 次ハイパス、9 kHz の 2 次ローパス）。左右は独立に処理する（ギター 1 本のための機材なので連動なし）。キャビネットは含まない（仕様書の要確認：GT02 の初期 IR と「Cab On/Off」を足すかは未決）。
- **Gain と 3 チャンネル。** 三極管 1 の利得（ノブ 0〜10 を dB で直線）：Clean 0〜+24 dB、Crunch −8〜+22 dB、Lead −6〜+26 dB。結合ハイパス：Clean 30／30 Hz、Crunch 70／80 Hz、Lead 140／160 Hz（Lead は低域を締める）。バイアス 0.20〜0.35。チャンネル間の音量を合わせるため出力に Clean +7 dB、Lead −5 dB を足した（設計値）。実測（1 kHz・−24 dBFS RMS の正弦波、高調波 2〜8 次の合計を基音に対する dB）：Clean は Gain 0／5／10 で −32／−20／−10 dB、Crunch は −30／−16／−6 dB、Lead は −23／−10／−5 dB。出力は Gain 5 で 3 チャンネルとも −13 dBFS 前後。
- **トーンスタック（回路の伝達関数）。** 3 つのつまみ（高域 250 kΩ・低域 1 MΩ・中域 25 kΩ、スロープ抵抗 56 kΩ、コンデンサ 250 pF／20 nF／20 nF）の受動回路の伝達関数（3 次）を、つまみの位置から計算し、双一次変換で 3 次の IIR にしている（つまみの変化は 20 ms の補間、32 サンプルごとに係数を更新）。Bass は対数カーブ（5 で 0.09）、Treble・Middle は直線。ノブ 5 のままだと中域が約 8〜10 dB へこむ（受動スタックの特徴）。回復ゲインは「つまみ全部 5・1 kHz」の損失を戻す固定値。**式の符号を 1 つ直した：** 参考にした 3 次の係数式のうち a2 の m 項の `C2·C3·R3·R4` を「−」と覚えていたが、そのままだと Bass 0 で Middle 0.2 以上のとき極が複素数になり、利得も 0 dB を超えた（受動の RC 回路ではあり得ない）。この符号を「＋」にすると、つまみ 3 つの全組み合わせ（0.1 刻み）と 20 Hz〜20 kHz で、極は実数・負、利得は 0 dB 以下になった。テストで固定している。
- **サグ。** 出力段の出力パワー（二乗の平均、立ち上がり 40 ms・戻り 200 ms）に応じて電源が下がり、出力段の許容レベルが下がる（Clean 10 ％、Crunch 30 ％、Lead 22 ％まで）。実測（Crunch・Master 9・200 Hz の持続音）：叩いた直後の RMS −1.4 dBFS → 約 100 ms で −3.2 dBFS に落ち着く（差 約 2 dB、テストは 1 dB 以上）。Master 0〜10 は −60〜+18 dB（平方根のカーブ、5 で −5.5 dB、0 でほぼ無音）。
- **Volume match（EVO、`gt01.evo.on`、既定 Off）。** 弾いている最初の 5 秒（100 ms ごとの RMS が −50 dBFS を超えたブロックだけ数える）の平均の大きさを測り、−20 dBFS RMS に合わせる固定の入力ゲイン（±18 dB まで）を 1 秒かけて入れる。以後は固定なので、ギター側のボリュームを絞る（入力が下がる）と、段の歪みが減ってきれいになる（−12 dB で歪みが 10 dB 以上減る、テスト）。測り終わるまでは 0 dB。On にし直すと測り直す。
- 4× OS：5 kHz の正弦波を Lead・Gain 10 に入れても、6 次高調波の折り返し（18 kHz）が基音より 50 dB 以上低い（テスト）。2 段の半帯域 IIR で、遅延は 0 と表示する（他の OS 製品と同じ扱い）。無音は完全な 0、DC は結合コンデンサで止まる。

### GT02 の設計（仕様書に数値がない部分・仕様書からの差）

- **録音した IR は使っていない（仕様書からの差）。** 仕様書は「IR は自社で収録する必要がある（他社 IR は使えない）。収録の計画と費用が未定」としている。いまは**収録なしで済むよう、パラメータから IR を作るモデル**にした（収録した IR が入ったら差し替える前提。畳み込みの仕組みはそのまま使える）。音はスピーカーの実測ではなく「それらしい形」の設計値。
- **モデル。** 振幅特性 ＝ キャビネット × マイク × 近接効果（Mic distance）× 軸外し（Off axis）。キャビネット（1x12／2x12／4x12）：低域の 3 次ハイパス（120／90／65 Hz）、低域の共振（150 Hz +1 dB／105 Hz +3 dB／80 Hz +5 dB）、コーンの高域の 3 次ローパス（5.2／4.9／4.5 kHz）、2.6〜3 kHz のプレゼンス（+3／+4／+5 dB）。マイク：Dynamic ＝ 5 kHz に +6 dB の山と 12 kHz の 3 次ローパス、Ribbon ＝ 200 Hz 以下 +3 dB・4 kHz に −2 dB・7 kHz の 2 次ローパス・近接効果 1.6 倍、Condenser ＝ 7.5 kHz 以上に +8 dB のシェルフ・19 kHz の 2 次ローパス。近接効果：180 Hz 以下に 12 dB × exp(−距離 cm / 7)（0 cm で +12 dB、30 cm でほぼ 0）。Off axis：2.5 kHz 以上に −0.16 dB/度（90 度で −14.4 dB）。200 Hz〜4 kHz の平均を 0 dB にそろえるので、どの組み合わせでも中域の音量は ±1.5 dB 以内（テスト）。
- **最小位相の IR。** 振幅特性から実ケプストラムで最小位相の IR を作る（キャビネット部分は 2048 サンプル＝約 43 ms、最後の 1/4 を窓で消す）ので、先頭から音が立ち上がり遅延がない（先頭 64 サンプルに 1024 サンプルまでのエネルギーの半分以上、テスト）。**Room** は 4 ms 後から始まる暗い雑音の減衰（時定数 60 ms、全長 8192 サンプル）を足し、エネルギーは直接音の 0.35 × Room（100 ％で直接音に対し約 −4.5 dB）。左右とも同じ IR（左右で別の IR は持たない）。
- **畳み込み。** `core/include/sw/zl_convolver.hpp`（新規）：IR の先頭 256 サンプルを時間領域で直接畳み込み、残りを既存の分割 FFT 畳み込み（遅延 256）に「256 サンプルずらした IR」で任せる。合計の遅延は 0。ランダムな 3000 サンプルの IR で直接計算との差 1e-5 未満（テスト）。IR を替えるときは古い IR と 20 ms でクロスフェードする（直接部分も同時にフェード）。
- **IR の作り直しを音声スレッドで止めない。** 設定を変えると、次の処理ブロックから 3 段階（約 3 ms ＋ 2.5 ms ＋ 1.5 ms、手元の -O2 の実測の最大）に分けて IR を作り、1 ブロックに 1 段階ずつ進める。メモリは prepare で確保する。最後に畳み込みへ渡す（分割 FFT の IR の変換。この部分の時間は未計測）。フェード中は次の変更を待つ。
- **Low cut。** 畳み込みのあとの 2 次ハイパス（Off＋20〜300 Hz。MS06 と同じく範囲の左端 20 Hz が Off）。Mic の画面上のドラッグ（EVO）は画面側の作業。CPU は、直接部分が 1 サンプルあたり左右で 512 回の積和（設計上の見積もり）。

### GT04 の設計（仕様書に数値がない部分）

- **信号の流れ（アンプ側）。** 入力 → 20 Hz ハイパス → **プリ**（2× OS の偏りつき tanh。Gain 0〜10 で利得 1〜6 倍の歪みと ±6 dB のレベル）→ **4 バンド EQ**（ノブ 5 で平ら、ノブ 1 目盛り ＝ 2.4 dB で ±12 dB。Low ＝ 80 Hz のロー・シェルフ、Lo mid ＝ Mid Hz の半分（最小 100 Hz）のベル Q 1、Hi mid ＝ Mid Hz のベル Q 1、High ＝ 3.5 kHz のハイ・シェルフ）→ 150 Hz で LR4 分割し、**低域は歪ませず**、高域だけ **Drive**（2× OS の tanh、利得 1〜25 倍、余裕 8→1.2、音量補正つき）→ **簡易キャビネット**（35 Hz の 2 次ハイパス、90 Hz に +2 dB、6 kHz の LR4 ローパス）→ Master。Mid Hz が 1 つで Lo mid／Hi mid の両方に関わる読み方は設計値（仕様書は「Mid Hz 250／500／800／1.5k／3k」とだけ書いている）。Master は ノブ 5 で 0 dB、0 で −60 dB、10 で +10 dB。**Master は DI には掛からない**（アンプ側のボリューム）。実測（400 Hz・−24 dBFS RMS）：Gain 2／5／10 で高調波 −49／−44／−39 dB、Drive 0／3／6／10 で −44／−40／−32／−16 dB。出力は Drive を上げても −24〜−18 dBFS（ほぼ一定）。平らな設定で 50 Hz〜3 kHz は ±2 dB 以内、25 Hz は −8 dB、12 kHz は −31 dB（キャビネット）。
- **DI と Phase align（EVO、`gt04.evo.on`、既定 On）。** DI Off ならアンプ側だけ。On なら (1−blend)×アンプ ＋ blend×DI（直線の混ぜ。画面にない DI blend は仕様書のとおり追加）。DI 側には、アンプ側と同じ **35 Hz のハイパスと 150 Hz の分割の全域通過**を入れ（DI も 35 Hz 以下は同じように落ちる。位相が追従できるようにするための設計判断。「DI 側に全域通過フィルタと遅延を入れる」のとおり）、さらに**遅延（整数サンプル＋1 次の全域通過で端数）**を足す。遅延量は、EQ の設定が変わるたびに、小さな試験音（150 Hz、1/1000）をアンプ側のコピーに通して位相を測り、DI 側の同じ試験音の位相との差から決める（実測 約 9 サンプル）。**150 Hz では位相が一致**（実測：差 0.00 rad。テストは差 0.25 rad 未満）、100 Hz で 0.19 rad・300 Hz で 0.13 rad まで（半帯域 IIR の遅延が周波数で少し変わるため）。Off だと DI は遅延なしで、アンプ側との位相差は 100 Hz で 1.5 rad になる（テストは「On の差の 2 倍以上」）。遅延は 0 と表示する（OS の IIR を含む他製品と同じ扱い）。EQ を動かすと遅延量が変わり DI 側に小さな段差が出うる（ゆっくり動かす前提）。
- 無音は完全な 0、DC は止まる。左右は独立。

### GT05 の設計（仕様書に数値がない部分）

- **回路の伝達関数。** ピックアップ（インダクタンス L・直列抵抗 R・自己容量 Cp）が、ケーブル容量 Cc と受け側の抵抗 Rl に繋がったときの 2 次ローパス：`H(s) = 1 / (L·C·s² + (L/Rl + R·C)·s + 1 + R/Rl)`、C ＝ Cp ＋ Cc。Pickup 0〜100 ％で、シングルコイル（L 2.5 H・R 6 kΩ・Cp 100 pF）からハムバッカー（L 5.5 H・R 9 kΩ・Cp 180 pF）へ直線で動かす（設計値）。共振の周波数は 1/(2π√(LC))、高さは Rl で決まる。実測（Rl 1 MΩ）：Single・Short（100 pF）＝ 7.1 kHz に +15.7 dB、中央・Short ＝ 5.1 kHz に +14.6 dB、Hum・Short ＝ 4.0 kHz に +13.9 dB、Single・Long（1000 pF）＝ 3.0 kHz に +15.3 dB、Hum・Long ＝ 2.0 kHz に +14.0 dB。Rl を 10 kΩ にすると山はなくなり（+1 dB 以内）、低域も −4.1〜−5.7 dB 下がる（1 + R/Rl の分）。47 kΩ は山が約 3 dB 以上低い。**回路どおりなので既定（1 MΩ・Short・中央）でも約 +15 dB の山が 5 kHz にある**（仕様書の「2〜6 kHz の山」）。双一次変換のため、山のすそ（6 kHz 以下）で解析式から最大 0.9 dB ずれる（テストは 1 dB 以内）。
- **Level／Output** は単なる利得（−20〜+10／−10〜+10 dB、20 ms の補間）。
- **Pickup swap（EVO、`gt05.evo.on`、既定 Off、区分 B）。** DI の長時間スペクトル（1/3 オクターブ、3 秒の平均）から、元のピックアップの共振を推定する：2〜6.3 kHz の各帯域が、その 2 つ両側の帯域の平均より何 dB 高いかを見て、最も高い帯域を共振とし（3 dB 未満なら「なし」）、3 点の放物線補間で周波数を出す。見つかったら、その周波数に Q 2.5 のベルで **−0.85 ×（山の高さ、最大 15 dB）** を入れて打ち消し、そのあとに Pickup／Cable／Impedance の共振を付け直す。0.5 秒ごとに推定し、200 ms かけて追従する。実測（3.5 kHz・Q 3 の共振を持つ雑音、8 秒）：推定 3352 Hz・山 8.1 dB、300 Hz・1 kHz は変化なし（−0.1 dB 以内）、3.5 kHz は −6.0 dB。共振を完全に打ち消すものではない（推定の精度と、帯域が 1/3 オクターブである限界）。
- 画面のトグル「Lift」（グラウンドリフト）は、仕様書の要確認どおり**入れていない**（プラグインでは意味がない。別の機能に割り当てるかは未決）。遅延 0。無音は完全な 0。左右は独立。

### RV01 の設計（仕様書に数値がない部分）

- **共通部品 `core/include/sw/fdn.hpp`（新規。RV02・RV05・RV06・RV08 でも使う）。** 遅延線 16 本（4 点 Hermite 補間、ゆるい変調）、ハウスホルダー行列（I − 2/N·11ᵀ、損失なし）で帰還、各線に「減衰時間から決まる利得」と「高域の減衰用の 1 次ローパス」。入力は ±1 の並びで各線へ、出力は直交する 2 つの和（左右）。線の長さは 1 サンプルあたり 0.05 サンプルずつ動く（Size を動かしてもクリックしない。音程が滑る）。Freeze は帰還利得を 0.99995・ダンピングなし・入力を閉じる（20 ms で切り替え）。単体の実測（Damping 20 kHz）：Decay 1／3／6 秒に対し 0.89／2.68／5.23 秒（T20 を 60 dB に外挿）。
- **RV01 の流れ。** 左右の中央 →（Pre-delay、0.5 秒の遅延線）→ ① 初期反射（アルゴリズムごとの 8〜10 本のタップ、Size で時間を伸縮、左右に定パワーで振り分け、1 側あたりエネルギー 1 に正規化）と ② 4 段の全域通過（Diffusion 0〜100 ％ → 係数 0.2〜0.7、長さ 3.0／2.2／7.9／5.8 ms）→ FDN → ER / late の混ぜ（√比で、エネルギーを保つ。0 ％ ＝ 後部のみ）→ Width（M/S、0〜150 ％）→ Mono low end（サイドを 120 Hz の LR4 ハイパス）→ Low cut／High cut（2 次）→ Duck。**後部のエネルギーは「FDN のインパルス応答のエネルギー ≒ 0.002 秒 × Decay ÷（線の長さの平均、秒）」（実測：Hall の既定で 0.033 × Decay）を使って 1 に正規化**する（`Fdn::kEnergyConstant`、`meanLengthSeconds()`）。エネルギーは帰還の中を回り続け、線が短いほど頻繁に出ていくため、線の長さの平均で割る。これで Decay・Size・アルゴリズムを変えても全体の大きさがそろう（実測、ER を 0 にした後部だけの IR エネルギー：5 つのアルゴリズム × Decay 0.6／2.8／10 秒で 0.43〜1.04）。広帯域の雑音を入れたときの出力は入力と同程度（Mix は共通枠の 22 ％）。
- **アルゴリズム（設計値）。** 線の長さ（Size 100 ％の 0.4〜1.6 倍のうち、74 ％で 1.29 倍）：Hall 24〜80 ms・変調 6 サンプル／0.3 Hz、Room 8〜29 ms・2／0.4、Chamber 15〜55 ms・3／0.35、Plate 10〜40 ms・5／0.5（Damping を 1.5 倍）、Ambience 4〜16 ms・1.5／0.4（Damping を 1.2 倍）。初期反射のタップ時間：Hall 17〜112 ms、Room 4〜44 ms、Chamber 9〜83 ms、Plate 3〜30 ms、Ambience 2〜27 ms（Size の倍率がかかる）。**Decay は全アルゴリズム共通で 0.2〜20 秒**（実測：Decay 0.6／1.5／3／6 秒に対し、ER 0・Damping 20 kHz の T20 で 0.57／―／2.75／5.35 秒、約 0.9 倍）。Ambience は最初の 50 ms にエネルギーの 42 ％（Hall は 23 ％、Pre-delay 0 のとき、テスト）。
- **Duck（`rv01.duck`、−18〜0 dB、既定 −6。画面に無いので追加）。** 原音（中央）のピークフォロワ（立ち上がり 5 ms・戻り 150 ms）が −40 dBFS 以下なら効果なし、−10 dBFS 以上で全量（その間は直線）。残響のレベルを 10 ms で下げ、250 ms で戻す。−12 dB 設定で、大きい音（−8 dBFS 雑音）の間は −6〜−16 dB 下がり、止まった 1 秒あとの尾は変わらない（±1.5 dB、テスト）。
- **Pre-delay**：長さは 1 サンプル/サンプルで滑る（動かしても音程の変化は小さい）。Freeze 中は入力と初期反射を止める。遅延は 0 と表示（仕様書「残響側の遅延は音の一部」）。出力は残響のみ（Mix は共通枠）。

### RV02 の設計（仕様書に数値がない部分）

- **流れ。** 入力（ステレオ。Mono in On なら左右の和）→ Pre-delay → **分散の全域通過 48 段**（1 次、a = −0.7）→ FDN（`sw::Fdn`、線 16 本、9〜36 ms、変調 5 サンプル／0.5 Hz）→ Width（M/S）→ Low cut（2 次）→ Duck。出力は残響のみ（Mix は共通枠）。ステレオ入力は左右を別の ±1 の並びで FDN に入れる（`Fdn::processStereo` を追加）。**分散：** 1 次の全域通過の群遅延は低域で (1−a)/(1+a) ＝ 5.7 サンプル、高域で (1+a)/(1−a) ＝ 0.18 サンプル。48 段で 200 Hz が 271 サンプル（5.6 ms）、1 kHz が 240、4 kHz が 88、8 kHz が 31、16 kHz が 11 サンプル（解析式、テストは 200 Hz が 8 kHz の 4 倍超）。金属板の「高域が先に届く」性質。
- **Decay 0.5〜6 秒**：実測（ER なし、Damping Bright）0.6／2.0／5.0 秒に対し 0.54／1.78／4.42 秒（約 0.89 倍、FDN と同じ）。後部のエネルギーは RV01 と同じ式で正規化（ステレオ同相の入力でインパルス応答のエネルギーは 1 より大きい）。**Damping Dark〜Bright ＝ 2〜14 kHz**（対数、中央で 5.3 kHz。設計値）。**Width**：Mono 〜 Wide は 0〜100 ％。
- **Pre-delay の Sync と Duck は仕様書の表にない操作なので追加した。** `rv02.sync`（Off／1/32／1/16／1/8／1/4、ホストのテンポがあるとき音符長 ＝ 60000/bpm × 拍の分数、200 ms で頭打ち。テンポが無いときは Pre-delay の値）、`rv02.evo.on`（Duck、既定 Off）：Duck は RV01 と同じ処理で **−6 dB 固定**（原音が −40 dBFS 以下で効果なし、−10 dBFS 以上で全量、10 ms で下げ 250 ms で戻す。−6 dB 設定で大きい音の間は −3〜−9 dB、尾は変わらない：テスト）。
- 遅延は 0 と表示。無音は完全な 0。

### RV03 の設計（仕様書に数値がない部分）

- **バネ 1 本 ＝ 帰還の輪。** 遅延線 → 1 次ローパス（Tone）→ **「引き伸ばした」全域通過 28 段**（a + z^−M）/（1 + a·z^−M）、a ＝ −0.6 → 帰還。低域ほど遅れる（高域が先に出る）ので、叩くと「ビヨン」と下がる音（さえずり）になる。M は分数（隣り合う履歴の線形補間）。**Tension 0〜10 ＝ M を 4〜2 に**（Tension を上げると M が小さくなり、さえずりの音程が上がり、同じ周波数の遅れが短くなる）：150 Hz の分散の遅れは Tension 0／5／10 で 438／332／223 サンプル、5 kHz では 30 サンプル前後（解析式）。**バネの数 1／2／3 ＝ 輪の時間 33／41／52 ms のものを並列**（遅延線は輪の時間の 0.55 倍、残りは分散の遅れ）。帰還利得は 2.5 秒で −60 dB になる値（実測の減衰：インパルス応答のエネルギーが −10 dB まで 0.19 秒、−30 dB まで 0.75 秒。Tone と分散の損失のぶん、設計より短い）。バネ 1 本は左右同じ、2 本以上は左右に少しずつ振る。
- **Dwell 0〜10 ＝ バネへの入力の強さ。** −18〜+18 dB（5 で 0 dB）で tanh のソフトクリップに通す（強くするほど飽和）。インパルス応答のエネルギーは Dwell 0／5／10 で 0.02／0.76／1.32（出力は既定で約 1 に合わせた固定の係数 5.0）。**Drip 0〜10 ＝ 立ち上がりでの励起。** 持続音の間の励起は 0.5、立ち上がり検出器（速いピークフォロワが遅いフォロワの 2 倍を超えたとき開き、約 60 ms で閉じる）が開いている間は 0.5 ＋ 1.0 × Drip/10。インパルスを入れたときの最初の 0.2 秒のエネルギーは Drip 10 が Drip 0 の 3 倍以上、220 Hz の持続音では Drip 10 と 0 は 1 秒あとに ±1 dB 以内で同じ（テスト）。**Tone 0〜100 ％ ＝ 輪の中のローパス 1.5〜8 kHz**（対数）。
- バネが多いほど周波数特性の凸凹（くし形）が小さい（300 Hz〜4 kHz の標準偏差：1／2／3 本で 21.0／19.3／17.8 dB、テストは 1 本と 3 本の差 2 dB 以上）。遅延は 0 と表示。無音は完全な 0。

### RV04 の設計（仕様書に数値がない部分・仕様書からの差）

- **録音した IR は使っていない（仕様書からの差、GT02 と同じ事情）。** 仕様書は「IR は自社収録」「Gear は実在機材を想起させるので名前と中身の扱いを決める（実機名は使わない）」としている。いまは **カテゴリの IR を合成する**：7 つのオクターブ帯域（125 Hz〜8 kHz）のバンドパス雑音に帯域ごとの減衰時間をかけて足し、初期反射の単発を足す（左右は別の乱数）。Halls／Rooms／Churches／Gear の帯域の残響時間（125 Hz → 8 kHz）：Halls 3.4→0.8 s（長さ 5 秒）、Rooms 0.9→0.25 s（1.6 秒）、Churches 7.0→1.4 s（8 秒）、Gear 2.8→1.4 s（3.5 秒、全域通過 48 段の分散を通して「金属板・バネ」風の汎用の音にする。実機名・実機を想起させる名前は使わない）。広帯域の残響時間の実測（T20 を 60 dB に外挿）：Halls 3.0 s、Rooms 0.8 s、Churches 6.5 s、Gear 2.5 s。IR のエネルギーは常に 1 に正規化するので、カテゴリや Length・Size を変えても大きさはほぼ同じ（0.96〜0.99）。**Custom** は、`loadIr()` で読み込んだ IR（1〜2 チャンネル、どのサンプルレートでも。再標本化する）を再生する。IR はプロジェクトの状態に保存される（`saveExtra`／`loadExtra`、「R4IR」＋ヘッダー＋ float）。IR を読み込んでいない Custom は、入力をそのまま通す（単位インパルス）。**ファイルを選んで読み込む操作（Load IR）は画面側の作業で、いまは API だけ。**
- **畳み込みの仕組み（新規 `core/include/sw/tiered_convolver.hpp`・`deferred_convolver.hpp`、`zl_convolver.hpp` の高速化）。** 1 チャンネルごとに、先頭 128 サンプルを時間領域で直接、128〜1152 を FFT（ブロック 128）、1152〜16384 を FFT（ブロック 1024、入力を 128 サンプル遅らせる）、16384 以降を **分散処理の FFT（ブロック 8192）** で畳み込む。最後の層は、ブロックの終わりに前向き FFT だけを行い、パーティションの積は次のブロックの前半に数個ずつ、逆 FFT はそのあとに行い、結果を 1 ブロックあとに使う（遅延 2 ブロック ＝ 16384。この層の IR を 16384 から始めて、全体の遅延が 0 になるようにしてある）ので、長い IR でも 1 回の処理が長くならない。層ごとに「入力を（開始位置 − その層の遅延）だけ遅らせる」ことで合計の遅延を 0 にしている。ランダムな 30000 サンプルの IR で直接計算との差 1e-4 未満（テスト）。**FFT と積和の複素数の掛け算は自前で書いた**（`std::complex<double>` の `*` は NaN 処理の関数呼び出しになり遅い）。IR の差し替えは、`Convolver` に追加した段階的な読み込み（`beginKernel`／`stepKernel`／`commitKernel`、使う分だけのパーティション）で行い、20 ms でクロスフェードする。
- **IR の作り直しを音声スレッドで止めない。** Category／Length／Size／Reverse／Bar fit を変えると、合成（16384 サンプルずつ）→ 変換（Length の切り落としと最後の 10 ％ のフェード、Size の再標本化、Reverse、エネルギーの 1 への正規化）→ 周波数領域への変換（1 回に 1 パーティション）→ 切り替え（チャンネルごとに別の呼び出し）を、処理 1 回につき 1 段階ずつ進める（手元の -O2 の実測：1 段階 1〜4 ms）。prepare と状態の読み込み（`snapToTargets`）では全部をまとめて行う（prepare 約 0.22 秒）。
- **Length**：IR の長さの 10〜100 ％で切り、最後の 10 ％（最低 20 ms）を cos² で消す。**Size 50〜150 ％**：IR を線形補間で伸縮する（音程も動く。長くなる側は最大 10 秒まで）。実測：Size 150／50 ％で残響時間が 1.5／0.5 倍（±0.2／±0.15、テスト）。**Reverse**：IR を逆順にする。**Pre-delay**：入力側の遅延線（1 サンプル/サンプルで滑る）。Low cut／High cut は 2 次。
- **Bar fit（EVO、`rv04.evo.on`、既定 Off）。** ホストのテンポが分かるとき、Length で決まる長さを、1／2／4／8／16／32 拍のうち、それを超えない最大のものに切り下げる（拍数 ＝ 60/bpm 秒 × 拍数）。120 bpm・Halls（5 秒）・Length 60 ％（3 秒）→ 4 拍 ＝ 2.0 秒（テスト）。テンポが無いときは Length のまま。
- **CPU（手元の実測、-O2、仮想マシン）。** 2 チャンネルで 256 サンプルのブロック 1 つあたり平均 約 1.0 ms（実時間の約 19 ％）、最悪 約 5〜8 ms（256 サンプルは 5.3 ms なので、小さいバッファでは足りないことがある。512 サンプル以上を推奨）。実数 FFT への置き換えや 1152〜16384 の層の分散処理で下げられる（未実施）。メモリは 1 インスタンスで約 80 MB（10 秒の IR 2 本分、設計上の見積り・未計測）。遅延は 0 と表示。

### RV05 の設計（仕様書に数値がない部分）

- **部屋とマイク。** スピーカーを部屋の一方の端、マイクをそこから Mic distance だけ離して置く直方体の部屋（寸法は設計値）：Small 5.0×4.0×3.2 m、Medium 8.0×6.0×4.0 m、Large 14.0×10.0×5.5 m。スピーカーは (0.12 長さ, 0.35 幅, 0.4 高さ)、マイクは同じ幅・高さで長さ方向に離す。**Mic distance Near〜Far ＝ 0.5 m 〜 部屋の長さの 0.7 倍**（Medium は 0.5〜5.6 m）。
- **初期反射 ＝ 鏡像法（2 次まで、25 個）。** 直接音（0 次）＋ 6 つの壁の 1 次の鏡像 ＋ 18 個の 2 次の鏡像を、部屋の寸法と 2 つの位置から計算する。遅延 ＝ 経路長 ÷ 343 m/s、振幅 ＝ 0.85^反射回数 ÷ 経路長（0.3 m で頭打ち）、左右の振り分けは鏡像がマイクのどちら側にあるか。直接音がいちばん早く強い（テスト）。Near のとき最初の反射が Large は Small の 1.5 倍以上遅れる（テスト）。遅延は 1 サンプル当たり 0.5 サンプルずつ動く（Mic distance を動かすと遅れが滑らかに変わる。ドップラー的な音程の動きが出る）。**マイクが離れるほど** 直接音は遅れて小さくなり、高域も減る（空気の吸収として、早期成分に 1 次ローパス 18 kHz ÷ (1 + 距離/4 m)）。
- **後部 ＝ 拡散の全域通過 4 段 → FDN（`sw::Fdn`）。** 線の長さ：Small 10〜35 ms、Medium 20〜65 ms、Large 35〜110 ms、後部の開始の遅れ 7／12／21 ms、変調 4 サンプル／0.35 Hz。**後部の大きさは部屋から決める：** 臨界距離 r_c ＝ 0.057 √(V / RT60)（V ＝ 部屋の体積）で、直接音のエネルギー（1/距離²）と拡散音のエネルギー（1/r_c²）が等しくなる、という関係から、後部の振幅 ＝ 1/r_c（`Fdn::kEnergyConstant` による単位エネルギーへの正規化のあとに掛ける）。これで **Mic distance だけで「直接音と残響の比」が連続して変わる**（実測、Medium：直接音＋初期反射の最初の 12 ms が Near で +1.9 dB・中央で −16 dB、後部は −5.6 dB のまま。Far では直接音が 12 ms より後に着くため 12 ms の窓には入らない）。インパルス応答の全エネルギー：Small 3.6／2.1／1.95（Near／中央／Far）、Medium 2.4／0.85／0.80、Large 2.2／0.68／0.66。
- **Decay 0〜10 ＝ 0.4〜4 秒（対数）**：RT ＝ 0.4 × 10^(Decay/10)（5 で 1.26 秒）。実測（Tone Bright）0／5／10 で 0.36／1.13／3.56 秒（約 0.9 倍、FDN と同じ）。**Tone Dark〜Bright ＝ 後部の高域の減衰 2〜14 kHz**（RV02 と同じ式）。**Speaker tilt 0〜10 ＝ スピーカーに送る信号の傾き** ±6 dB（5 で平ら、600 Hz を境に低域と高域を逆向きに動かす）。10 は 0 より高域／低域の比が 6 dB 以上大きい（テスト）。遅延は 0 と表示。Mix は共通枠。

### RV06 の設計（仕様書に数値がない部分）

- **音程変換は FDN のフィードバックの中。** 仕様書「FDN のフィードバック内に 2 粒の重ね合わせによる音程変換」のとおり、16 本の線のうち偶数の 8 本の出力を音程変換器に通し、元の線と混ぜて戻す：s′ ＝ s ＋ a（shift(s) − s）、a ＝ **0.7 × Shimmer（％）**。利得 1 の変換器と元の線の混ぜ合わせは振幅を増やさないので（三角不等式）、**ループは暴走しない**。**最初は音程変換の出力を FDN の入力へ戻す外側のループにしたが、Decay を長くすると FDN の共振のピークでループ利得が 1 を超え、Shimmer 60 ％（既定）でも 9 秒で +168 dBFS まで発散した**（モードの密度が低く減衰が長いと、白色雑音での平均利得 1 に対してピークが数倍になるため）。線の中に入れる方式に変えて、Decay 60 秒・Shimmer 100 ％でも有界（テスト、20 秒、出力 4.0 未満）。
- **音程変換器（`Processor::Shifter`、線ごとに 1 つ）。** 50 ms の窓の遅延線を、半窓（25 ms）離した 2 つのタップで読み、窓の端で sin²／cos² で交差させる（2 粒の重ね合わせ）。読み出しの遅れを 1 サンプルあたり（比 − 1）ずつ縮める：**Octave ＝ 2 倍、Fifth ＝ 1.5 倍**、Both ＝ 偶数番目の線を Octave、奇数番目を Fifth（線ごとに窓の位相をずらす）。実測（440 Hz・−18 dBFS の 0.3 秒の正弦波、Decay 12 秒、0.6〜2 秒の尾、Shimmer 100 ％）：Octave は 440 Hz −50.5 dB・**880 Hz −51.6 dB**・1760 Hz −55.3 dB（さらに 1 オクターブ上へ昇る）、Fifth は 440 Hz −50.5 dB・**660 Hz −54.3 dB**、Both は 660／880 Hz がともに −54〜−60 dB。Shimmer 0 では 880 Hz が −99 dB（元の音程のまま、テストは 30 dB 以上の差）。Shimmer 30 ％で 880 Hz は −67 dB。
- **Decay 1〜60 秒（対数）**：FDN の線は 25〜75 ms、Damping は 9 kHz 固定（昇った音が聞こえるよう）、実測（Shimmer 0）Decay 1／3／6 秒に対し 0.88／2.65／5.39 秒（約 0.9 倍）。**Freeze（EVO、`rv06.evo.on`）：** フィードバックを 0.99995・ダンピングなしにし、入力を閉じ、**Shimmer も 0 にして**（保持したパッドが昇り続けないように）、保持する。2 秒の Decay・0.5 秒の雑音のあと Freeze を入れて、1 秒後と 8.5 秒後の差が 3 dB 以内（Freeze なしは 30 dB 以上下がる、テスト）。**Duck（`rv06.duck`、既定 On）：** RV02 と同じ処理、−6 dB 固定。遅延は 0 と表示。

### RV07 の設計（仕様書に数値がない部分）

- **Mix は無く、直接音を遅らせない（仕様書のとおり）。** 出力 ＝ 直接音（その場で、距離による減衰と空気吸収）＋ 初期反射。反射の到着は「直接音のあと（経路長 − 直接音の経路長）÷ 343 m/s」なので、直接音のインパルスは出力の先頭にある（テスト：ピークが 0〜2 サンプル目）。
- **部屋と位置。** 直方体（Room size：Small 4.0×3.0×2.6 m、Medium 8.0×6.0×3.5 m、Large 20×14×6 m、設計値）。聞き手は (0.3 長さ, 0.5 幅)、高さ 1.6 m（天井が低ければ天井の 0.6 倍）、正面は長さ方向。音源は聞き手から Distance だけ離れ、Angle が正なら右、負なら左（高さ 1.5 m）。**音源が部屋の外に出てしまう距離（例 Small で 20 m）では、部屋の寸法を同じ縦横比のまま拡大する**（音源が長さの 0.95 倍、幅の 0.95 倍を超えない大きさまで。Small で 20 m ＝ 約 7.7 倍）。鏡像法で直接音＋ 2 次までの鏡像 24 個（25 個）。左右の位置は鏡像の方位角の sin（右が正）。直接音は Angle の sin で振り分ける（定パワー）。
- **反射の大きさと音色。** 振幅 ＝ 壁の反射率^反射回数 ×（Use の係数）÷ 経路長（0.3 m で頭打ち）。**Wall（反射率、反射のローパス）：** Wood 0.80／6 kHz、Concrete 0.95／14 kHz、Glass 0.90／10 kHz ＋ 250 Hz のハイパス（薄い板の低域の逃げ）、Curtain 0.35／2.5 kHz。**Use（反射の係数、設計値）：** Dialog 0.7（反射に 150 Hz のハイパス：こもりを避ける）、Instrument 1.0、Foley 1.4。**空気吸収：** 1 次ローパス 18 kHz ÷ (1 + 距離/6 m)（反射は距離 + 3 m で計算）。実測（Medium、3.5 m、正面、Instrument）：直接音に対する反射のエネルギー（2 ms 以降）は Wood 0.1／Concrete 4.0／Glass 2.5／Curtain −12.1 dB、Use は Dialog −2.9／Instrument 0.4／Foley 3.3 dB。最初の反射は Small 1.6 ms・Medium／Large 3.4 ms（床の反射。天井や壁の遠さは後ろの反射に出る）、24 個目は Small 31 ms／Medium 47 ms／Large 117 ms。距離が 2 倍で直接音は 1/2（±0.25、テスト）。遅れは 1 サンプル当たり 0.5 サンプルずつ動く。
- 遅延は 0 と表示（反射は直接音のあとに来る）。Δ ボタンが無い。

### RV08 Gated の設計（仕様書に数値がない部分）

- **構成。** 入力（左右の平均）→ 拡散オールパス 4 段 → 16 ライン FDN（`sw::Fdn`）→ ゲートのゲイン → Tone。Mix は共通の枠（既定 40 %）。遅延 0。
- **Size（0〜10）。** FDN の線長を 20〜65 ms × 2^((Size−5)/5)（0.5〜2 倍）、FDN の減衰を 0.6＋0.2×Size 秒（0.6〜2.6 s）にした（設計値）。**減衰を長めにしてあるのは意図的：** 尾の長さと形はゲートが決めるため。出力は他のリバーブと同じ式（エネルギー ≒ 0.002 s × 減衰 ÷ 平均線長）で単位エネルギーに揃えた。
- **ゲート。** 検出：入力のピーク追従（アタック 5 ms／リリース 15 ms）。Threshold（0〜10）＝ −60〜0 dBFS（1 目盛 6 dB、既定 5 ＝ −30 dBFS）を超えると開く。**一度しきい値−6 dB を下回ってからでないと再び開かない**（ヒステリシス）。開くたびに時間を 0 から数え直すので、**開いている間に次のヒットが来ると延長される**（テスト：0.2 s の 2 発目で 0.3 s でも開いている）。リリースを 15 ms にしたのは、60 ms では 26 dB 下がるのに約 180 ms かかり、150 ms 間隔のスネアが再トリガされなかったため（測定）。開いている間のゲイン g(x) ＝ (1−Shape)＋Shape·x、x ＝ 経過時間 ÷ Gate time。Flat（Shape 0）は全区間 1、Reverse（100 %）は 0 から 1 への直線の立ち上がり。Gate time（50〜800 ms）が過ぎたら 3 ms で閉じる（Reverse は尾が最大のところで切れるのが特徴）。先読みはしない（遅延 0）ので、Reverse は「音の前から立ち上がる」ものではなく、ヒットの後に膨らむ形。
- **Tone。** 1 kHz を境にした 1 次のチルト（Dark：高域 −6 dB／低域 +6 dB、Bright：逆）。中央で平坦。
- **進化機能（Snare key、`rv08.evo.on`、既定 Off）。** 検出だけを「約 150〜250 Hz（200 Hz の 4 次バンドパス、Q 2）と 2〜5 kHz（4 次バンドパス）」の和に通す。テスト：−20 dBFS の 3 kHz は開く、60 Hz と 1 kHz は開かない（Off ではどれも開く）。**被りの学習（CS02 と同じ）は UI のキャリブレーション操作と一緒に作る**（CS02 と同じ扱い）。キーを通した分だけ検出レベルは下がるので、On のときは Threshold を少し下げる。

### DL01 Echo の設計（仕様書に数値がない部分）

- **構成。** チャンネルごとに 録音 ＝ フィルタ（リミッタ（入力＋Feedback×返り））→ 遅延線 → 返り（4 点 Hermite 補間、時間は滑らかに追従）。出力は返りだけ（Mix は共通の枠、既定 25 %）。遅延 0。最大 4 s（Sync の 2 小節は 120 bpm 以上で収まり、それより遅いテンポは 4 s で頭打ち）。
- **Feedback 110 % の頭打ち。** ループ内のソフトリミッタ L·tanh(x/L)（小信号のゲインは 1）。L ＝ Digital 8、Analog 1.6、Tape 1.0。**Feedback 110 % でも 20 秒で暴走せず（全モード・ピーク 8 未満、テスト）鳴り続ける。**
- **Mode の性格（設計値）。** Tape：リミッタが L ＝ 1 で飽和（0 dBFS の 1 kHz で 3 次高調波が −30 dB より大きい）、9 kHz のローパス、時間の追従 0.12 s（変えるとピッチが滑る）、揺れは Depth 100 % で ±3 ms の正弦（Rate）＋フラッター（6.3 Hz、±0.05 ms × Depth）。Analog（BBD）：5 kHz の 2 次ローパス、追従 0.06 s、揺れ ±2.5 ms。Digital：フラット（0 dBFS の 1 kHz で 3 次高調波 −40 dB 未満）、追従 0.02 s、揺れ ±1 ms。実測：8 kHz の返りは Digital 比で Analog が 6 dB 以上下がり、Tape はその間。
- **HPF／LPF** はループ内（返りと入力の両方を通る）、2 次。HPF の最小 20 Hz、LPF の最大 20 kHz は **Off（バイパス）**（画面の表記は Off がどちらにも付く想定）。
- **Ping-pong。** 左の線だけが入力を受け、左の返りは右の線へ、右の返りは左の線へ入る（1 回目の返りは左、2 回目は右。テスト）。Off では左右が別々にエコー。
- **Sync。** 仕様書は「Time は Sync 時 1/64〜2 小節」としか書いていないため、**Time のつまみの位置を 18 個の音符長（1/64〜2 小節、3連・付点つき。`sw/notes.hpp`）に等分で割り当てる**（今後の DL04／DL05 等と共通）。ホストのテンポが無いときは Time（ms）のまま。既定の 375 ms の位置は 1/4 付点（120 bpm で 750 ms）になる（画面の 375 ms と一致しない）。
- **Duck。** 入力（左右の平均）が −40〜−10 dBFS の間で 0→全量、返りを Duck dB まで下げる（10 ms で下げ、250 ms で戻す）。実測：12 dB 設定で定常ノイズ中 −12 ±1.5 dB、入力が止まれば戻る。Duck の ID は `dl01.duck`（量を持つため `evo.on` ではない）。

### DL02 Tape Echo の設計（仕様書に数値がない部分）

- **構成。** テープ 1 周：録音 ＝ 低域・高域シェルフ ← ローパス ← 飽和 ← 入力＋Feedback、遅延線を 3 つのヘッドが t・2t・3t（t ＝ Rate）で読む。Heads で聞こえるヘッドを選び（1／2／3／1+2／2+3／All、既定 1+2）、出力 ＝ 合計 ÷ √（ヘッド数）、帰還 ＝ 聞こえているヘッドの平均 × Intensity（0〜10 ＝ 0〜110 %。テスト：Intensity 3／6／9 で 2 回目の返りが 1 回目の × 0.33／0.66／0.99 ±1.5 dB）。Mix は共通の枠（既定 25 %）。遅延 0。
- **Rate。** 1 つ目のヘッドの遅延 50〜200 ms、LOG。「Slow〜Fast」なので **Slow（左端）＝ 200 ms、Fast（右端）＝ 50 ms**（`reversed`、中央 100 ms）。変えると 0.15 s で追従する（ピッチが滑る）。
- **テープの速度と帯域（設計値）。** ヘッドギャップのローパス（2 次）＝ 9 kHz × √(100 ms ÷ t) × (1 − 0.06 × Wear)。速い（t が短い）ほど明るい（テスト：50 > 100 > 200 ms で各 2 dB 以上）。飽和は tanh（小信号の利得 1、SA01 のヒステリシスは流用せず簡略化）。**仕様書は「SA01 のテープ飽和を流用」だが、1 周ごとに通る反復ループで SA01 の履歴モデル（2× OS）を持つと CPU が増えるので、ループ内は tanh に簡略化した**（SA01 そのものを通す版は必要なら別途）。
- **Bass／Treble。** 200 Hz のロー・シェルフと 3 kHz のハイ・シェルフ（±6 dB、ループ内で毎周かかる）。
- **Wear（EVO、0〜10、既定 3、ID は `dl02.wear`）。** ひとつの量で、高域の劣化（上の式）、ワウ（0.12 ms × Wear、0.55／1.37 Hz）、フラッター（0.006 ms × Wear、9.1 Hz。ヘッドの遅延は距離に比例して揺れる）、ドロップアウト（Wear 2 超で毎秒 (Wear−2)×0.375 回、20〜60 ms、4〜10 dB の落ち込み）を同時に動かす。テスト：Wear 10 で 8 kHz が Wear 0 より 6 dB 以上低い、1 kHz の搬送波が 1 dB 以上減る（サイドバンドへ）、20 秒の 300 Hz で 10 ms 窓の最大最小差が Wear 0 で 0.3 dB 未満、Wear 10 で 3 dB 超。
- **Heads の切替。** 10 ms の直線で入れ替える（テスト：10 %〜90 % が 6.5〜9.5 ms）。**ホストが再生中で小節位置（拍子・小節の頭）を教えるときは、次の小節線まで待ってから切り替える**（そのためにプラグイン層へ `setTransport(playing, 次の小節線までの拍数)` を追加：全製品に影響しない任意の呼び出し）。再生していない・小節位置が無いときは即座に 10 ms で切り替える。

### DL03 Bbd の設計（仕様書に数値がない部分）

- **構成。** 入力＋Feedback×返り → tanh → コンプレッサー → プレフィルタ → **バケツ**（クロックごとに 1 回サンプルして 4096 段のリングへ入れ、4096 ティック前を読む＝ Time）→ ホールド → ポストフィルタ → エキスパンダー → 返り。出力は返りだけ（Mix は共通の枠、既定 25 %）。遅延 0。
- **クロック（設計値）。** クロック f ＝ 4096 ÷ Time（Time 300 ms で 13.7 kHz、600 ms で 6.8 kHz）。ホストのサンプルレート以上になる短い Time（約 85 ms 未満）では、バケツのティックはホストのサンプルごと（実質ふつうの遅延）。フィルタは「限定しない 4096 ÷ Time」に追従する。**長いほどクロックが遅く、帯域が狭く、折り返しが増える**：プレフィルタ ＝ 2 次ローパス 0.4 f（1 つだけ、f/2 を超える音が折り返ってくる）、ポスト ＝ 2 次 2 つ 0.2 f（Time 300 ms で約 2.7 kHz）。実測：5 kHz のトーンの返りは Time 40 → 300 → 600 ms で −24.5 → −59.9 → −73.5 dB（テスト：10 dB／6 dB 以上の差）。7 kHz のトーンを 600 ms（クロック 6.8 kHz）に通すと差の音（約 170 Hz）が −44.8 dB で出る（Time 40 ms では出ない、テスト：−60 dB より大きく、40 ms より 12 dB 以上大きい）。
- **コンパンダー。** 圧縮 x_c ＝ x ÷ √(E(x)＋0.003)、伸張 y ＝ y_c × E(y_c)（E は整流 5 ms の包絡）。大きい音ではゲイン 1（−24 dBFS のトーンの往復で −0.5 dB）、小さい音ほど伸張で下がる（バケツで入るノイズを隠し、音が止むと「息をする」）。
- **Feedback（0〜10 ＝ 0〜110 %）。** 返りをループ内の tanh（L ＝ 1）で頭打ち。テスト：Feedback 3／6／9 で 2 回目の返りが 1 回目の × 0.33／0.66／0.99 ±2 dB、110 % でも 20 秒で暴走しない。
- **Mod depth／Mod rate（0〜10）。** Time を ±（Depth ÷ 10 × 3 ms）で正弦に揺らす（左右の位相差 90°、クロックも一緒に動く）。Rate は 0.05〜8 Hz（0.05 × 160^(値/10)）。Depth 10 で 1 kHz の搬送波が 3 dB 以上減る（テスト）。
- **Grit（EVO、0〜10、既定 2、ID は `dl03.grit`）。** バケツにクロックのノイズ（最大 −40 dB）を入れる。コンプレッサーのあとなので伸張が形を整える。**入力がある間（包絡に比例）だけ入り、無音では 0（テスト：無音は無音）。** 実測：300 Hz のトーンの返りで 2 kHz のフロアは Grit 0 で −216 dB、2 で −89 dB、10 で −75 dB（トーンに対して）。原音には触れない（原音は共通の枠の Dry で、バケツを通らない）。
- **Sync。** 仕様書の画面の値は Off 既定。On のとき Time（ms）を、ホストのテンポでの音符長のうち対数で最も近いものに寄せ（`sw/notes.hpp` の `noteNearest`）、20〜600 ms に収める（テンポが無いときは Time のまま）。DL01 は「つまみを音符の列に割り当てる」方式、DL03 は「ms を最も近い音符に寄せる」方式（デバイスの範囲が狭いため）。

### DL04 Multitap の設計（仕様書に数値がない部分）

- **ID。** タップごと `dl04.tap<n>.on／time／level／pan／filter`（n ＝ 1〜6、仕様書の並びの順）、そのあと `dl04.feedback`、`dl04.mix`、`dl04.sync`、`dl04.pingpong`（全 34 個）。
- **構成。** 入力（左右の平均）＋Feedback×ループ → 4 秒までの遅延線。タップ n：On、Time（1〜4000 ms、LOG）、Level（−60〜0 dB）、Pan（L100〜R100）、Filter（2 次ローパス 200 Hz〜20 kHz、**20 kHz は素通し**）。出力 ＝ 各タップ（フィルタ後）× Level × Pan の和。Mix は共通の枠（既定 20 %）。遅延 0。タップの On／Off は 10 ms の直線（クリックなし、テスト）、Time の変更は 30 ms で追従。
- **Pan の法則。** 定パワー（左 cos θ、右 sin θ、θ ＝ (Pan＋1)×π/4）。**端（L100／R100）が 0 dB、中央が −3 dB**（テスト：5 点で L²＋R² ＝ 1）。
- **既定値（仕様書「1/8・1/8 D・1/4…」の続きは設計値）。** Time 250／375／500／750／1000／1500 ms ＝ 120 bpm の 1/8、1/8 D、1/4、1/4 D、1/2、1/2 D。On は 1〜3、Level −6 dB、Pan は左右交互（−50／+50 …）、Filter 8 kHz。
- **Feedback（0〜100 %）。** 帰還は「聞こえているタップ（フィルタ後・**Level の前**）の平均」× Feedback（ループ利得は Feedback 以下で暴走しない：全タップ 100 % の大音量ノイズで 10 秒、ピーク 40 未満、テスト）。Level は聞こえる量だけを決め、帰還には影響しない。1 タップのとき 2 回目の返りは 1 回目の × Feedback（テスト：0.40 ±0.01）。
- **Ping-pong。** 2 本目の線 B を持ち、B ではすべての Pan が左右反転。A のループは B へ、B のループは A へ入る（1 回目は設定どおり、2 回目は反対側、3 回目は元に戻る。テスト）。Off では B は使わない。
- **Sync。** **Time のつまみは「120 bpm での ms」と読み、最も近い音符長（`sw/notes.hpp`、1/64〜2 小節、3連・付点つき）に寄せて、ホストのテンポで鳴らす。** 既定の 250／375／500 ms はどのテンポでも 1/8、1/8 D、1/4 になり、範囲 1〜4000 ms がちょうど 1/64〜2 小節（120 bpm）に当たる（DL01 は「つまみ位置→音符の列」、DL03 は「実テンポでの最近傍」で、この製品だけテンポに依存しない読み方）。テンポが無い・Sync Off のときは ms のまま。4 秒で頭打ち（遅いテンポの 2 小節）。

### DL05 Reverse の設計（仕様書に数値がない部分）

- **構成。** 入力を 10 秒のリングに録り、出力はハン窓の粒の和（粒の長さ ＝ Grain size、半分ずつ重ねるので窓の和がちょうど 1）。Mix は共通の枠（既定 40 %）。遅延 0（遅れそのものが効果）。
- **Time（音符長 1/16〜2 小節、13 段、既定 1/4）。** ホストのテンポで鳴る（テンポが無いときは 120 bpm と見なす）。4 秒で頭打ち（遅いテンポの 2 小節）。`sw/notes.hpp` の 1/16 から 2 小節まで。
- **Reverse。** 直前の区間（長さ P、境界は P ごと）を逆に再生する：音は 境界 B で折り返した位置 2B − t に聞こえる。**テスト：P ＝ 0.5 s、0.3 s のクリックは 0.7 s、0.1 s のクリックは 0.9 s に出る（±2 サンプル）。順序も逆になる。** **Forward** は P だけ遅らせるふつうのディレイ（Grain size によらず 1 サンプルのずれもない、テスト）。**Random** は粒ごとに P 以内の位置と向き（前／後）をランダムに選ぶ（レベルは入力から ±3 dB、テスト）。
- **Spray。** 粒の読み出し位置を ±（Spray × 粒の長さ）ずらす。
- **拍位置（EVO）。** 区間の境界は始めは自由に回るが、ホストが再生中で小節の位置を教えるとき（`setTransport`）は「次の小節線 − k × P」にそろえる（拍頭で逆再生が始まる）。テスト：120 bpm、小節線まで 0.3 拍 → 境界は 7200 ＋ 24000 k サンプル（再生していなければ従来どおり 24000 k）。
- **Pitch +12。** 粒を 2 倍速で読む。**読み出し位置の動きは「粒から粒へ rate × hop」にそろえてある（そうしないと重なる粒の位相が合わず、音が消える）：** Reverse は s0 ＝ 現在 − (1＋rate) × u（u ＝ 境界からの時間）、Forward は遅れが縮むので P/(2(rate−1)) ごとに P に戻す（遅れは P と P/2 の間を行き来する）。テスト：440 Hz の正弦が 880 Hz に出る（Forward は 20 dB 以上、Reverse は 15 dB 以上、440 Hz より大きい）。Spray が 0 でないと粒ごとの位相がばらつくので、既定（30 %）では「粒状の質感」が付く。
- **Freeze。** 録音を止め、その瞬間までの P 秒に読み出しを巻き付ける（Reverse なら逆向きにぐるぐる回る）。解除すると、その間のリングの内容は使わない（無音扱い）。テスト：入力が止まって 6 秒後でもレベルが −30 dB より大きい（Freeze なしは −100 dB 未満）。

### MD01 Chorus の設計（仕様書に数値がない部分）

- **構成。** 左右それぞれ、中心 7 ms の変調遅延（4 点 Hermite 補間）。変調幅 dev ＝ Depth ÷ 10 × 3 ms × （Mode I：0.6、II：1.0）、波形は三角波。出力は遅延音だけ（Mix は共通の枠、既定 50 %）。遅延 0。
- **Mode（設計値）。** I：1 つの声、LFO ＝ Rate。II：1 つの声、LFO ＝ 1.6 × Rate。I+II：両方の声（II の位相を 90° ずらす）を各 1/√2 で足す。
- **Width（Mono〜Wide、既定 100 %）。** **右の LFO の位相を左に対して 0°〜180° ずらす**（Mono ＝ 左右同じ、Wide ＝ 逆向き）。テスト：左右の揺れの相関 ＋1.0／0／−1.0（Width 0／50／100 %）。
- **モノ互換（EVO 相当の常時構成）。** Wide では左右の揺れが逆向きなので、L＋R では 1 次の揺れ（ピッチの変動）が打ち消し合う。テスト：Rate 5 Hz、Depth 10、1 kHz の正弦で、片側のサイドバンド（±5 Hz）が搬送波に対して −30 dB より大きいのに、L＋R では 20 dB 以上小さくなる（Width 0 では打ち消されず、モノの和でも 15 dB 以上大きい）。
- **Tone と BBD 風。** Tone（Dark〜Bright）＝ 遅延音のローパス 2.5 kHz〜14 kHz（LOG、2 次）、120 Hz のハイパス、BBD のヒス（−78 dBFS、**入力がある間だけ**入る：無音は無音）。Tone 100 と 0 で 9 kHz は 12 dB 以上差が出る（テスト）。

### MD02 Flanger の設計（仕様書に数値がない部分）

- **構成。** 遅延線を d(t) で読み、入力に Feedback ×（リミッタ L·tanh(x/L)、L ＝ 4）を足して書き戻す（Feedback −100〜+100 %、小信号の利得 1、100 % の大音量ノイズでも暴走しない：テスト）。右の LFO は左より 90° 遅れ（設計値）、波形は正弦。出力は遅延音だけ（Mix は共通の枠、既定 50 %）。
- **Through zero なし。** d ＝ Manual × 2^(1.5 × Depth × sin)：中心が Manual、Depth 70 % で最大／最小 ＝ 2^2.1 ≒ 4.3（テスト：2^(3×0.7) ±0.1）、Depth 100 % で 1/2.83〜2.83 倍。遅延 0。
- **Through zero（EVO、`md02.evo.on`、既定 On）。** 共通の枠が原音を 10 ms（480 サンプル@48 kHz）遅らせ（報告遅延 480、Off なら 0）、d ＝ 10 ms ＋ Manual × Depth × sin。**遅延音が原音の経路より後から前へ、また後へと交差する**（Depth 100 %、Manual 3 ms で 336〜624 サンプル：原音の 480 をまたぐ、テスト）。Depth 0 では遅延音が原音とぴったり同じ 480 サンプル。報告遅延は「次の prepare で使う値」（CLAP では変更にホストの再起動が必要）で、動作中のコアは prepare 時の値を使い続ける。
- **Sync。** LFO の周期を音符長にする：Rate（Hz）を周期（120 bpm での秒）と読んで最も近い音符長に寄せ、ホストのテンポで鳴らす（既定 0.2 Hz ＝ 5 s → 2 小節 ＝ 4 s、Rate 2 Hz → 1/4：90 bpm なら 1.5 Hz、テスト）。テンポが無いときは Hz。**再生中で小節線が分かるときは LFO の位相を「次の小節線 − k 周期」にそろえる**（`setTransport`、仕様書の「テンポ同期した Through zero の掃引」）。再生していなければ 0 から自由に回る。

### MD03 Phaser の設計（仕様書に数値がない部分）

- **構成。** 1 次の全域通過を N 段（4／6／8／12、既定 6）直列にし、全段を同じ fc(t) ＝ Center × 2^(2×Depth/10×sin) で動かす（Depth 10 ＝ ±2 オクターブ、テスト：Center 800 Hz で 200〜3200 Hz）。係数 a ＝ (t−1)/(t+1)、t ＝ tan(π fc / fs)：各段は fc で −90°。Feedback（0〜10 ＝ 0〜0.9）は最後の段の出力を 1 サンプル遅らせて入力へ戻す（全域通過なのでループ利得は Feedback 以下で安定）。右の LFO は左より 90° 遅れ（設計値）、波形は正弦。出力は全域通過の出力そのもの（Mix は共通の枠、既定 50 %：50 % でノッチが深く出る）。遅延 0。
- **実測（テスト）。** ① 全域通過の振幅は 0 ±0.05 dB で平ら（4／6／8／12 段）。② 原音＋遅延音のノッチは N/2 本、最初のノッチは Center × tan(π/2N)（±3 %）。③ Feedback 9（0.81）で応答の山は 12〜16 dB（理論 1/(1−0.81) ＝ 14.4 dB）。
- **Sync。** LFO の周期を音符長にする（MD02 と同じ：Rate を 120 bpm での周期と読んで最も近い音符長に寄せ、ホストのテンポで鳴らす。再生中で小節線が分かれば位相を小節線にそろえる）。
- **Note follow（EVO、`md03.evo.on`、既定 Off、区分 B）。** DY05 と同じ音高検出（70〜1000 Hz、自己相関）を `core/include/sw/pitch_tracker.hpp` に移して共有し、Center ← Center × f0 ÷ 220 Hz（0.15 s で追従、無声のときは最後の値を保つ、倍率は 0.25〜8）。どの音でもノッチが同じ倍音の位置に来る（テスト：110／220／330 Hz の正弦で Center が 400／800／1200 Hz ±5 %）。基準 220 Hz は設計値（Center のつまみは「A3 を弾いたときの位置」）。

### MD04 Tremolo Pan の設計（仕様書に数値がない部分）

- **構成。** Mix は無い（全体を処理する、仕様書どおり）。遅延 0。LFO u（−1〜1）は Sine／Triangle／Square／Ramp（Square と Ramp の縁は時定数 2 ms でなめらかにして、クリックを避ける：縁は数 ms かかる。テスト：Square の中間値の時間は 5 % 未満、Ramp は 9 割がゆっくり上昇で下降は 1 割未満）。uni ＝ (u＋1)/2。
- **Tremolo。** ゲイン ＝ 1 − Depth × (1 − uni)（Depth 100 % で無音まで下がる。テスト：Depth 30／60／100 % で最小 0.7／0.4／0.0 ±0.03）。**Width は右の LFO の位相を左に対して 0°〜180° ずらす**（100 % で左右が逆位相のステレオ・トレモロ、テスト：和が一定）。
- **Auto pan。** θ ＝ π/4 × (1 ＋ Depth × Width × u)、左 ×√2 cos θ、右 ×√2 sin θ（中央で 1、定パワー：テスト L²＋R² が一定、端で一方が 0・他方が ×√2）。ステレオ入力はバランスとして効かせる。**Depth と Width は掛け合わせた量が振れ幅**（仕様書は Depth と Width の使い分けを書いていない：Depth＝量、Width＝ステレオの広がり）。Width 0 では動かない（テスト）。
- **Harmonic（EVO 相当）。** 800 Hz（LR4）で上下に分け、低域のゲイン ＝ 1 − Depth × (1 − uni)、高域のゲイン ＝ 1 − Depth × uni（逆位相、和は常に 2 − Depth：テスト、Depth 80 % で 1.2 ±0.12）。Width は Tremolo と同じ。
- **Rate／Sync。** Rate 0.1〜20 Hz。Sync On（既定）では Rate を「120 bpm での 1 周期」と読んで最も近い音符長に寄せ、ホストのテンポで鳴らす（既定 4 Hz ＝ 1/8、90 bpm なら 1/8 の長さ）。再生中で小節線が分かるときは位相を「次の小節線 − k 周期」にそろえる。テンポが無いときは Hz。

### MD05 Rotary の設計（仕様書に数値がない部分）

- **構成。** 入力（左右の平均）→ Drive（小信号の利得 1 のソフトクリップ、0〜+18 dB）→ 800 Hz（LR4）で分け、**高域はホーン、低域はドラム**の回転体へ。回転体ごとに 2 本のマイク（±60°）に届く：ドップラー（遅延 ＝ 1 ms − R·cos(θ−φ)/343 m/s：ホーン R ＝ 0.18 m で ±0.52 ms、ドラム R ＝ 0.12 m で ±0.35 ms、テスト：中心 48 サンプル、振れ ±25.2／±16.8 サンプル）と音量変化（g ＝ ((1−k)＋k(1＋cos(θ−φ))/2) ÷ (1−k/2)、k ＝ ホーン 0.7／ドラム 0.45 ×（1 − 0.5 × Mic distance）：1 回転の平均が 1。Far（右端）で振れが半分）。左右のマイクの出力がそのまま L／R。キャビネット共振（設計値）：ホーン 2.5 kHz +1.5 dB（Q 1）、ドラム 110 Hz +2 dB（Q 0.9）。Mix は共通の枠（既定 100 %）。
- **Speed と Accel（物理モデルの追従）。** Stop 0 Hz、Slow ホーン 0.8／ドラム 0.67 Hz、Fast 6.7／5.7 Hz（設計値）。目標へ指数的に近づく：時定数 ＝ ホーン 0.3＋0.2×Accel 秒（Accel 0 で 0.3 s、5 で 1.3 s、10 で 2.3 s）、**ドラムはその 3 倍**（慣性が別）。テスト：Slow→Fast の 1 時定数後にホーンが 63 % ±2 %、ドラムが 28 % ±2 %。Stop へ向かうと両方が止まる。
- **Horn／Drum（0〜10）。** 音量 ＝ v÷7（7 で 1、10 で +3.1 dB、0 で無音）。
- **遅延 48 サンプル固定（仕様書どおり）。** ドップラーの中心遅延 1 ms（サンプルレートに比例：96 kHz で 96）を報告する。
- **MIDI／フットスイッチ（EVO、CC64・CC1・Note）はまだ**：ホストのノート入力をプラグイン層に通す作業が要る（Speed のパラメータそのものはオートメーションできる）。docs/tasks.md に残す。

### MD06 Freq Shift の設計（仕様書に数値がない部分）

- **構成。** IIR 全域通過のヒルベルト対（`core/include/sw/hilbert.hpp`、4 段 × 2 系統の 12 極設計。**係数は 2 乗して z² の全域通過に使い、片方の系統だけ 1 サンプル遅らせる**：測定で 80 Hz〜18 kHz（48 kHz）に 90° ±1°、振幅差 0.1 dB 以内）で i（遅れた方）と q（q が 90° 進む）を作り、`i cos(ωt) + q sin(ωt)` で全成分を s Hz だけずらす（ピッチシフトではない：倍音は倍音でなくなる。テスト：200／400／600 Hz を +50 Hz → 250／450／650 Hz、元の位置は −45 dB 以下）。出力は処理した音だけ（Mix は共通の枠、既定 50 %）。遅延 0。
- **Shift（−2000〜+2000 Hz、既定 +35 Hz）。** 仕様書の「対数対称」を新しいカーブ `Curve::SymLog` として `sw/param.hpp` に追加した：中央が 0、v ＝ ±max × ((1＋K)^|u| − 1)/K（u ＝ 2x − 1、K ＝ 2000）。+35 Hz は x ≒ 0.74（テスト：往復で誤差 1e-6）。
- **Direction。** Up は |Shift|（上へ）、Down は −|Shift|（下へ）、Both は Shift の符号に従う。テスト：+100 Hz で 1 kHz が 1100 Hz に（反対側の側波帯は 35 dB 以上低い）、−100 Hz で 900 Hz、Up は −100 でも 1100、Down は +100 でも 900。
- **Ring mod。** i cos(ωt)（両側波帯、各々 −6 dB：テスト）。
- **Feedback（0〜100 %）。** 出力をリミッタ（L ＝ 4）を通して入力へ戻す：コピーが 1 回ごとに s Hz ずつ上がる螺旋（テスト：Feedback 50 % で 1100／1200／1300 Hz が 6 dB ずつ下がる）。100 % でも暴走しない。
- **LFO。** On のとき、シフト量 × sin(2π × 0.25 Hz × t)（+s から 0 を通って −s まで揺れる。設計値：仕様書は LFO の速さを書いていない）。
- **Pitch track（EVO、`md06.evo.on`、既定 Off、区分 B）。** 音高検出（DY05 と共有）でシフト量 × f0 ÷ 220 Hz（Shift のつまみ値は A3 を弾いたときのシフト量）。0.15 s で追従、無声のときは最後の値を保つ（倍率は 0.25〜8）。テスト：110／220／440 Hz の正弦で 20／40／80 Hz ±6 %。

### MD07 Ensemble の設計（仕様書に数値がない部分）

- **構成。** 遅延線（中心 10 ms）の N 本のタップ（Voices ＝ 2／3／4／6、既定 4）。各声は 2 つの LFO で動く：**遅い揺れ**（Rate 0〜10 ＝ 0.15 × 20^(Rate/10) Hz ＝ 0.15〜3 Hz、振れ ±Depth/10 × 4 ms）と**速い揺れ**（遅い揺れの 12 倍の速さ、振れはその 1/4。テスト：振幅の比 −12 dB ±1.5）（設計値）。出力は遅延音だけ（Mix は共通の枠、既定 50 %）。遅延 0。
- **モノ互換（MD01 と同じ構成を声部数ぶん）。** 各声の位相を円周に等間隔に置く（遅い揺れは v/N、速い揺れは −v/N 周期）：**全声の遅延の変化量の和が常にほぼ 0**（テスト：2／3／4／6 声で和の最大値が揺れ幅の 1 % 未満）なので、L＋R では 1 次の揺れが打ち消し合う（テスト：Spread 10、1 kHz の正弦で、片側の側波帯（3 Hz 離れ）が −30 dB より大きいのに、L＋R では 20 dB 以上小さい）。
- **Spread（0〜10）。** 声を中央（0）から全幅（10）まで等間隔に並べる：声 v の位置 p ＝ 0.5 ＋ Spread/10 × (v/(N−1) − 0.5)。**パンは線形**（左 1 − p、右 p：左右のゲインの和が常に 1 なので、モノの和の打ち消しが保たれる。定パワーだとこの和が位置によって変わり、打ち消しが崩れる）。出力は単位パワーに正規化（左右の平均、テスト：Voices 2〜6 × Spread 0／5／10 で、白色ノイズの出力が入力 −2.7 dB ±1.2：差は Tone 100 の 14 kHz ローパスが白色ノイズの上端を削る分）。
- **Tone。** MD01 と同じ（遅延音のローパス 2.5〜14 kHz、120 Hz のハイパス）。ヒスは付けない（仕様書にない）。

### ST01 Imager の設計（仕様書に数値がない部分）

- **構成。** 左右それぞれを 4 次 Linkwitz-Riley の 4 帯域に分け（`sw::Lr4Split4`、既定 200 Hz／2 kHz／8 kHz、オクターブ以上離す規則つき）、帯域ごとに M ＝ (L＋R)/2、S ＝ (L−R)/2、S × 幅（0〜200 %）、L ＝ M＋S、R ＝ M−S。帯域を足し戻す（大きさは平ら：幅 100 % の全帯域で L／R の位相も保たれる。テスト）。**Mix は無い**（全体を処理）。遅延 0。モノのトラックは触らずに通す（幅という概念がないため）。
- **実測（テスト）。** 幅 50／200 % で、その帯域の S だけが −6／+6 dB（±1 dB）、他の帯域は ±1.2 dB 以内、M は幅によらず ±0.2 dB。**幅 0 でも S は −∞ にならない：隣り合う帯域がクロスオーバーで位相をずらし合うため（40 Hz〜14 kHz の 4 点で 15 dB 以上、中間帯域で約 −20 dB）**。
- **Mono check（監視用、Auto 不可：`automatable=false`）。** 左右とも (L＋R)/2 を出す。
- **相関メーターと印（EVO、区分 A、画面用の値）。** 帯域ごとに「出力の L と R の相関」（300 ms の平均）を出し（`correlation(band)`：同相 ＋1、逆相 −1、無相関 0、無音は 1）、**相関が 0 未満で信号があるとき「広げすぎ」の印**（`overWide(band)`）。テスト：同相の正弦で ＋1、逆相で −1（印が付く）、独立なノイズで ±0.15 以内。画面の描画は UI の作業。

### ST02 Mid Side の設計（仕様書に数値がない部分）

- **構成。** M ＝ (L＋R)/2、S ＝ (L−R)/2、L ＝ M＋S、R ＝ M−S（左右がそのまま入った信号は何も変えずに通る。テスト：既定で 0.01 dB 以内）。Mid level／Side level ±12 dB（10 ms の滑らかな追従）、**Side HPF**（Off ＋ 20〜500 Hz、S だけ、2 次 Butterworth：最小の 20 Hz が Off、角で −3 dB）、**Side air**（S の 10 kHz ハイシェルフ、0〜10 ＝ 0〜+6 dB）、**Mid low**（M の 100 Hz ローシェルフ ±6 dB）。Mix は無い。遅延 0。
- **Encode（Off／On）。** On のとき入力はすでに M（左）と S（右）で、出力も M／S のまま（行列を通さない）。同じ処理がそのまま効く（テスト：M +6 dB、S −6 dB）。
- **進化機能（参照曲との M/S バランスの帯域別比較、区分 B）** は画面側の解析なので、コアには入れていない。

### ST06 Mono Low の設計（仕様書に数値がない部分）

- **構成。** M ＝ (L＋R)/2 には触れず、S ＝ (L−R)/2 だけに Frequency（20〜300 Hz、既定 120 Hz）の**ハイパス**（Slope 6／12／24／48 dB/oct ＝ 1 次／2 次 1 段／2 次 2 段（4 次）／2 次 4 段（8 次）、Butterworth）をかけ、Side boost（Frequency の 1 オクターブ上から効くハイシェルフ ±6 dB）、L ＝ M＋ S′、R ＝ M − S′、Output ±10 dB。Mix は無い。遅延 0。
- **実測（テスト）。** 角（Frequency）で S が −3.01 dB ±0.3（どの Slope でも）、1 オクターブ下で Butterworth の理論値 ±0.7 dB、M は 30／120 Hz でも ±0.01 dB。Frequency 未満では出力がモノ（L と R の差 −40 dB 以下、200 Hz・48 dB/oct）。Frequency を 30／120／300 Hz に動かすと角も動く。
- **Listen（EVO、監視用、Auto 不可）。** 「モノにして消える成分」＝ Frequency 未満の S だけを両チャンネルに出す（同じ次数のローパス：40 Hz の側音がほぼそのまま、4 kHz は −40 dB 以下、M は無音）。

### ST04 Center の設計（仕様書に数値がない部分）

- **構成。** M ＝ (L＋R)/2、S ＝ (L−R)/2。**Center（Wide〜Focus、0〜100 %、既定 50 ＝ 中央）は S のゲイン：** 左半分は +6 dB から 0 dB（6×(1−x/50) dB）、右半分は 0 dB から −∞（20 log10(1−(x−50)/50)、100 % ＝ モノ）。M は動かない（テスト：0／25／50／75 % で S が +6／+3／0／−6.02 dB）。Mix は無い。遅延 0。
- **Low center（0〜10、0 ＝ Off、設計値：20×15^(v/10) Hz ＝ 26〜300 Hz）。** S に 4 次のハイパス（その周波数より下はモノ）。角で −3 dB、M は触らない（テスト：v ＝ 2／5／10）。
- **Haas（0〜40 ms、Skew k ＝ 2）。** 選んだ側（Side：L／R）を遅らせる（4 点 Hermite、30 ms で追従）。テスト：5／10／25 ms でぴったりそのサンプル数、反対側は 0。
- **Link（既定 On）。** 仕様書は「Link」の意味を書いていない。**設計値：Haas で遅らせた側のレベルを遅れ 1 ms あたり 0.35 dB（最大 +6 dB）持ち上げて、先に届く側に像が引かれるのを和らげる**（Off なら補正なし）。確認が要る（README の注意）。
- **Balance（L〜R、±100 %）。** 線形：大きい側はそのまま、反対側を 0 まで下げる（テスト：+50 % で左が 0.5 倍）。
- **進化機能 Mono safe（`st04.evo.on`、既定 Off、区分 A）。** 遅らせた側をモノに足すと、最初のノッチの深さは 20 log10((1−r)/(1+r)) dB（r ＝ 遅らせた側／反対側のレベル比）。**−10 dB より深いとき（r ＞ 0.52）、r ＝ 0.52（−5.7 dB）まで遅らせた側を下げ、その側に 20 kHz × (0.52/r)² のローパス（3 kHz 以上）をかける。** テスト：5 ms・等レベルでモノの和の 100 Hz の落ち込みが −25 dB 未満（Off）から −10 dB ±1.5（On）に、12 kHz は 10 dB 以上下がる。`monoCombDepthDb()` と `delayedSideGainDb()` を画面用に出す。

### ST03 Phase Align の設計（仕様書に数値がない部分）

- **構成。** トラックを Delay（0〜20 ms、**0.01 ms に丸める**、Skew k ＝ 2、4 点 Hermite、20 ms で追従）だけ遅らせ、Phase（−180〜+180°）で位相を回し、Polarity で反転する。外部サイドチェーン（参照のマイク）は出力に足さない（聞こえない）。出力は処理したトラックだけ（Mix は共通の枠、既定 100 %）。遅延 0（遅らせるだけなので、早い方のトラックに挿す）。
- **Phase（設計値）。** 「全域通過で回す」を、全帯域に同じ角度を与える IIR ヒルベルト対（MD06 と同じ `sw/hilbert.hpp`）で実現：out ＝ cos φ·i ＋ sin φ·q。**|Phase| が 0.05° 未満は完全な素通し（20 ms でクロスフェード）**、それ以上は対を通る（対そのものの全域通過の位相と約 2 サンプルの群遅延を含む。振幅は変わらない：テスト 0.1 dB 以内）。テスト：120／400／1500／6000 Hz で、角度の差が ±2.5° で一定、±180 は反転。
- **進化機能 Auto align（区分 B）。** `startAutoAlign()` で音声スレッドが 4 秒ぶん（トラックと参照のモノの和）を録り（状態 Collecting → Ready）、**`analyse()` を画面（メインスレッド）が呼ぶ**（FFT を使うので実時間では回さない。画面はまだ無い）：① 0〜20 ms で |相互相関| が最大のラグを FFT で求める（0.01 ms に丸める）。② そのラグで遅らせたトラックを 250 Hz 未満に絞り、三つの候補を比べる：素通し（C0 ＝ Σxy）、反転（−C0）、回転器（A ＝ Σiy、B ＝ Σqy、角 ＝ atan2(B,A)、値 ＝ √(A²＋B²)：閉形式で探索なし）。**最大のものを採る**（単なる遅れだけのコピーには Phase 0・Normal が返る：回転器は素通しより必ず相関が小さいため）。③ 正規化相関が 0.1 未満（無音・無関係・参照がトラックより早い）は失敗（Failed）でパラメータは動かさない。成功すると Delay・Phase・Polarity の 3 つを `takeParamWrite` でホストへジェスチャとして返す。テスト：1.25／3.37／9／17.5 ms のコピーでラグが ±0.011 ms、反転コピーで Polarity が反転、回転器を通した参照（70°）で 70° ±3°（遅れは対の群遅延ぶん 0.06 ms 以内）。
- **要確認（仕様書）：** 画面の「Δ Compare」は合わせる前後の比較で、共通の Δ とは別。名前を変えるか決める。

### ST05 Phones の設計（仕様書に数値がない部分）

- **構成。** 左右のスピーカー → 左右の耳の 4 本のインパルス応答（最大 0.5 s）を、入力のステレオに畳み込む：左耳 ＝ L∗h_LL ＋ R∗h_RL、右耳 ＝ L∗h_LR ＋ R∗h_RR（`sw::TieredConvolver`、先頭 128 タップを直接畳み込むので**遅延 0**）。モニター用なので Mix は無い。モノのトラックは触らずに通す。
- **IR は録音ではなくモデルから作る（仕様書の「室内 IR は自社収録か商用利用できるデータ」は未対応。GT02／RV04 と同じ扱い）。** `designIr()`：
  - **配置。** スピーカーは正面から ±Angle（0〜60°、既定 30°）、距離 D（Nearfield 1.2 m、Mains 3 m、Car 0.8 m）。直方体の部屋（**Studio A 5×6×3 m・RT60 0.25 s、Studio B 6×8×3.5 m・0.40 s、Living 4×5×2.5 m・0.55 s**、設計値）の長さの 0.45 に聞き手、耳の高さ 1.2 m（Car は車室 1.6×2.4×1.2 m・RT60 0.08 s、耳 0.9 m。Car は Room を使わない）。部屋に収まらないときは D を縮める。
  - **頭。** 球（Head size：半径 7.5／8.75／10 cm）。直接音と 2 次までの鏡像（25 個）を、距離÷343 m/s で届け、**両耳の時間差は Woodworth の (a/c)(θ＋sin θ)** を両耳に半分ずつ割り振る（テスト：30° で 12.5 サンプル ±3、角度と頭の大きさで単調に増える）。振幅は 1/距離 × 壁の反射率^次数（√(1−α)、α ＝ 0.161 V/(S RT60) の Sabine）。**頭の影は Brown–Duda のモデル：H ＝ (1＋α(φ) jω/2ω0)/(1＋jω/2ω0)、ω0 ＝ c/a、α(φ) ＝ 1.05＋0.95 cos(1.2 φ)**（φ ＝ 到来方向と耳の軸の角）を各到来に掛ける（テスト：4 kHz で遠い耳が近い耳より 6 dB 以上暗く、150 Hz は 3 dB 以内で同じ）。
  - **後部。** 30 ms 以降は独立した雑音（スピーカーと耳ごとに別の種）× exp(−6.9 t/RT60)、ローパスは 10 kHz から下がる。エネルギーは臨界距離 0.057√(V/RT60) で決める（E_後部／E_直接 ＝ (D/Dc)²）。テスト：Schroeder 積分の RT60 が設計値（0.25／0.40／0.55／0.08 s）に ±0.06〜0.2 s で合う、Mains は Nearfield より直接音の比が 3 dB 以上小さい、両耳の後部の相関は 0.2 未満。
  - **スピーカー自身の特性。** 2 次ハイパス（Nearfield 60 Hz、Mains 35 Hz、Car 80 Hz）、Car は 100 Hz の +5 dB ローシェルフ（車室の低域の持ち上がり）。
  - 直接音の大きさは、ふつうの経路で 0.5（左右のスピーカーが同じ音のとき両耳で 1 付近）。
- **Phones profile（設計値）。** 仕様書の「機種名を出さない」方針どおり、**種類別の汎用カーブ**：Off（平ら）／Closed（90 Hz −2.5 dB の低域シェルフ、3.2 kHz +1.5 dB）／Open（90 Hz +3 dB、9 kHz −2 dB）／Earbud（100 Hz +2.5 dB、3 kHz −3 dB、10 kHz +2 dB）。ユーザーの測定データの読み込みは未対応。
- **Tracking（外部センサー連携は未定のため土台だけ）。** On のとき `setHeadYaw(度)` で頭を回して IR を作り直す（1.5° 以上動いたとき）。トラッカーはまだ接続していない（0° のまま）。Off では無視（テスト）。
- **IR の作り直し。** Speakers／Room／Angle／Head size（または頭の向き）が変わると、音声スレッドで **1 回の呼び出しに 1 つの小さな仕事**（IR 1 本の設計 約 0.45 ms、畳み込みの変換と切り替えは 1 本ずつ）として 4 本を作り、20 ms で切り替える。Phones profile は即時。
- **CPU の実測（このクラウド環境、-O2）。** 定常：256 サンプルのブロックあたり平均 約 1.4 ms（余裕の 27 %）、最悪 約 3.6 ms。作り直し中の最悪 約 4.6 ms。畳み込み 4 本のうち重いブロックが別々の呼び出しに入るように位相をずらしてある（`TieredConvolver` の位相 0〜3）。**バッファ 256 では余裕が少ないので、512 以上を推奨**（RV04 と同じ）。
- **要確認（仕様書）：** ヘッドホンの補正を機種名でなく種類別にすること／測定データの読み込み、IR の収録、Tracking の対応機器、**書き出し時の注意表示**（Monitor 用であること）は画面側。

### 音程エンジン（VO01・VO02・VO03・VO06 が共有、`core/include/sw/pitch_engine.hpp`）と VO01 Tune の設計

- **音程エンジン。** 仕様書どおり「ピッチ検出＋時間領域の波形重ね合わせ、フォルマントは保持」：
  - **`PitchAnalyzer`：** 入力をリングに入れ、128 サンプルごとに YIN 型の周期推定（12 kHz に間引いた複製で粗く、フルレートで 0.05 サンプルまで精密化：正規化相関＋放物線補間）。周期の履歴（周期・有声か）を持つ。**1 周期ずつ離した「印」**を置く（前の印から 1 周期先を、1 周期ぶんの波形が前の周期と最もよく合う位置 ±P/20 に動かす：どの周期でも同じ位相に置ける）。無声のときは 5 ms ごと。倍音が強い母音で倍の周波数に誤る（オクターブ誤り）対策：しきい値を 0.05 に絞り、見つからないときは最小値の 1/2・1/3・1/4 のラグが同じくらい深ければそちらを採る。
  - **`PsolaSynth`：** 合成の印を P/ratio 間隔で置き、最も近い解析の印のまわりから、`windowPeriods`（2）周期のハン窓の粒を切り出し（フォルマント係数で読み出しの速さを変えるので、スペクトル包絡がその倍率で動く）、重ねて足す。**各サンプルで窓の和で割る**（粒の重なりの量にレベルが左右されない）。粒の位相が隣と違うことによる損失（ピッチの移動が大きいと最大 −3.5 dB）は、入力のレベルに合わせる遅い利得（80 ms、±6 dB まで）で戻す。無声は ratio 1（入力が遅れて戻る）。**印ごとに 1 回だけ `RatioSource` に聞く。**
  - **遅延（仕様書の見積もり 512 サンプルとは違う）：** 粒の右半分と音程の窓（2×最長周期）が入っている必要があるので、**遅延 ＝ 窓周期 × 最長周期 ÷ 2 ＋ 最長周期 ＋ 2 ホップ＋64 ＝ 1450 サンプル（30 ms@48 kHz、85 Hz まで）**。仕様書の 512 は設計上の見積もりで、低い男声の 1 周期（85 Hz で 565 サンプル）が入らないため満たせない。85 Hz 未満は動かさない。
  - 単体テスト（`tests/test_pitch_engine.cpp`）：周期を 0.15 サンプル以内で当てる（100〜660 Hz）、ノイズ・無音は無声、印の間隔は 1 周期、ratio 1 で入力が戻る（相関 0.95 超）、ratio 0.5〜2 で基本周波数が ±0.4 %、フォルマントが Keep で動かず Follow で倍率どおり動く、滑る声とノイズが有限でレベルを保つ。CPU は 256 サンプルで平均 約 0.4 ms（7 %）。
- **`PitchCorrector`（`sw/pitch_correct.hpp`、VO01・VO02 共通）。** 測った音高 m（半音）、歌い手のゆっくりした音高 c（時定数 120 ms）、音階の最寄りの音 n（40 ms の平均から選び、0.2 半音のヒステリシス）、補正した中心 cc（Speed の時定数で n へ）、ビブラート v ＝ m − c。**出力 ＝ cc ＋ 0.6×Humanize×(c − n) ＋ kV×v ＋ Transpose**（kV：Natural 1／Reduce 0.4／Flat 0）、ratio ＝ 2^((出力 − m)/12)。Formant：Follow なら ratio、Keep なら 1。
- **VO01 の ID。** 仕様書の順に `vo01.view／scale／speed／humanize／vibrato／formant／transpose／detectmidi／snap／reference`、末尾に追加：`vo01.key`（12 種：仕様書の「キーは 12 種」）、`vo01.customscale`（Custom 用の音のビット、自動化不可）。Scale は Major／Chromatic／Custom（「C major」の表示は Key＋Scale）。Speed は 0〜400 ms（Skew k ＝ 2）。
- **実測（テスト）。** 235 Hz（58.1 半音）は C メジャーで B（59）へ、226 Hz は A（57）へ（±0.08 半音）。Chromatic で 228 Hz → A#。Key（D メジャー）や Custom（C と G だけ → 220 Hz が G）も動く。Transpose +7 で 66.0 ±0.1。Speed 0 なら 0.1 s 後に新しい音へ、300 ms なら 0.1 s 後はまだ動かず 1.2 s 後に着く。Vibrato：±0.6 半音のビブラートの標準偏差が Natural で 0.25 超、Reduce で Natural の 0.6 倍未満、Flat で 0.35 倍未満。Humanize 0／50／100 % で 0.4 半音の誤差が 0／0.12／0.24 半音残る。Formant の Keep／Follow、無声ノイズの素通し（±1 dB）、Graph（補正なし）も確認。
- **View／Detect MIDI／Snap to grid／Reference は画面・MIDI・ARA が要るので、保存だけで効かない**（Graph は補正なしの Auto）。トラックはモノ（ステレオ入力は和にして、左右同じ音を出す）。Mix は無い。
- **進化機能（キー検出、区分 B）。** 聞いた音の高さのクラス（30 秒で薄まる時間重みのヒストグラム）を Krumhansl–Schmuckler のメジャーの型と 12 のキーで相関させ、`suggestedKey()` と確からしさを出す（提案だけ、Scale は書き換えない）。テスト：E メジャーの旋律で E（4）、確からしさ 0.6 超、無音では 0。

### VO02 Tune Rt の設計（仕様書に数値がない部分）

- **構成。** VO01 と同じ音程エンジン（`PitchAnalyzer`＋`PsolaSynth`＋`PitchCorrector`）の**ライブ向け設定**：最低音 110 Hz（音程の窓が短い）、粒は 1.5 周期、**不安定な区間は補正を弱める**（連続する 2 つの印で音高が 2.5 半音以上跳んだら、次の 4 つの印は 30 % の強さで補正：誤検出をそのまま歌わせない。テスト：定常 0.2 半音上ずりの完全補正 −0.2 に対し、跳んだ直後は 0.3 倍、弱めなしなら +0.8）。出力は処理した音だけ（Mix は共通の枠、既定 100 %）。トラックはモノ。
- **遅延（仕様書の見積もり 128 サンプルとは違う）：1085 サンプル（22.6 ms@48 kHz）。** 同じ理由（粒の右半分と音程の窓が入る前に出せない）で、周期の短い高い声（110 Hz 以上）に限って短くしてある。**110 Hz 未満は動かさない。** 128 サンプルにするには別の方式（因果的な粒、検出の短縮）が要る。
- **パラメータ。** Key（12）、Scale（Maj／Min（自然短音階）／Chr）。**Speed は Slow〜Hard ＝ 100〜0 ms（Skew k ＝ 2、つまみが逆向き。中央が 25 ms ＝ 既定）。** Humanize 0〜10（VO01 の 0〜100 %）、既定 3。**Formant −3〜+3（半音相当）：声の母音だけを動かし、音高は動かさない**（テスト：基本周波数が同じで、エネルギーが 700 Hz から 700×2^(3/12) Hz へ移る）。Mix は 0〜100 %。ビブラートは残す（Natural）。
- **音階の切り替わり。** 新しい音に移ったとき、ビブラートを取り出すためのゆっくりした中心を歌い手の位置から始め直す（音の段差をビブラートと誤って残さない。VO01 の実測を直した：Natural でもステップ後に 0.5 半音程度の戻りに収まる）。

### VO03 Harmony の設計（仕様書に数値がない部分）

- **構成。** 音程の解析（`PitchAnalyzer`）は 1 つを共有し、ハーモニー 4 声それぞれが `PsolaSynth`＋`PitchCorrector`（Speed 15 ms、ビブラートは残す）＋遅延リングを持つ。**原音（リード）は遅延を揃えて中央にそのまま通す**。Mix は無い（仕様書の表のとおり。リードとハーモニーの比は各声の Level で決める）。
- **Source。** Scale（既定）：Key／Scale（Major／Minor）に対する度数で、歌い手の音から Interval 度ぶん上下の音階音へ動かす。Fixed：Interval を長音階の度数の半音数（2度＝2、3度＝4、5度＝7 …）として固定の半音で動かす。**MIDI は MIDI 入力がアダプターにまだ無いため Scale と同じ動作**（保存用の値だけ持つ）。Key／Scale は仕様書の表に無く、**末尾に追加したパラメータ**（`vo03.key`、`vo03.scale`）。
- **Interval −7〜+7 度。** 既定は +2（3度上）、+4（5度上）、−2（3度下）、+7（オクターブ上）。1・2 声目だけ On。Level −60〜0 dB（既定 −3）、Pan −100〜+100（既定 −40／+40／−70／+70、定電力）、Formant ±3 半音、Delay 0〜100 ms（既定 15）。
- **Humanize。** 声ごとの遅いランダムな音高の揺れ（2 つの遅い正弦波＋なめらかなノイズ）、**±30 cents × Humanize**。テスト：Humanize 100 では音高の標準偏差が 6 cents 超（実測は 0.25 倍のとき 5.9 cents、0.3 倍に上げて 6 超）、0 では 4 cents 未満。
- **遅延：1450 サンプル（30.2 ms@48 kHz）**。VO01 と同じ理由・同じエンジン（仕様書の見積もり 512 サンプルとは違う）。
- 入力がステレオのときは L/R を足してモノで解析する。出力はステレオ。

### VO04 Doubler の設計（仕様書に数値がない部分）

- **構成。** 声部（1／2／4／8）ごとに遅延線の 1 タップ。遅延は**周期のない不規則なゆっくりした動き**（声ごとに周波数の違う 2 つの遅い正弦波、0.02〜0.07 Hz。コーラスのような周期的な揺れにしない）。遅延 ＝ 0.5 ms ＋ Timing × 3 ms × u（u は 0.15〜1 でゆっくり動く。Timing 10 で最大 30 ms、仕様書の 0〜30 ms）。遅延 0 を報告（声部は 0.5 ms 以上）。Mix は共通の枠。
- **Pitch var。** 音程は遅延の変化率だけで動く（比 ＝ 1 − dD/dt）。声ごとに 0.7〜2.3 Hz の 3 つの正弦波の遅延変化を足し、**変化率の rms ＝ 0.8 × Pitch var セント**（テスト：2・5・10 で ±30 % 以内）。Pitch var 0 でも Timing の動きで音程がわずかに動く（Timing 4 で最大 約 4 セント未満をテスト、Timing 10 では最大約 8 セント）。
- **フレーズの頭。** 入力が −60 dB を下回る状態が 100 ms 続いたら、全声部の遅延を最小（0.5 ms）に**一気に**戻す（鳴っていないので音程は動かない）。次に音が来たら、変化率 0.5 %（約 8 セント）以内で本来の遅延へ戻る（Timing 10 で 30 ms に戻るまで数秒かかる。歌い出しの音がずれすぎない）。テスト：休止後の頭は 1.5 ms 未満、変化率は 0.5 % 以下。
- **Spread。** MD07 と同じ線形パン（左 1−p、右 p）で中央〜全幅、出力は平均して単位パワー（1／2／4／8 声とも入力の −18 dB 雑音で ±1.5 dB 以内）。
- **Tone。** 重ねた音だけを傾ける：3 kHz の高域シェルフ ±8 dB と 400 Hz の低域シェルフ ∓4 dB（50 % で平ら。100 % ‑ 0 % の差は 8 kHz で 16 dB、100 Hz で −8 dB）。
- 入力がステレオのとき L/R はそれぞれの遅延線に入る。進化機能（区分 A）は仕様書どおり「周期的でないずれ方」で実現し、`.evo.on` のスイッチは持たない（画面に無い）。

### VO05 Rider の設計（仕様書に数値がない部分）

- **音楽は外部サイドチェーンで受ける**（SW Link は未実装。サイドチェーンが無い、または音楽が −50 dBFS 未満のときは「Music: not listening」で、ライドは動かさず保持する）。
- **検出。** ボーカル：K 特性、150 Hz〜5 kHz（4 次）、200 ms の指数平均（歌っている間だけ更新。休止・息継ぎでは値を保つ。最初の 200 ms は累積平均）。音楽：K 特性、全帯域、3 秒の指数平均（最初の 3 秒は累積平均で、無音からの立ち上がりをライドしない。**音楽を 1 秒聴くまではライドを動かさない**）。
- **ライド。** 目標 ＝ clamp((音楽の LUFS ＋ Target) − ボーカルの LUFS, −Range, +Range)。Target は「音楽に対するボーカルの相対値」（仕様書の −40〜−6 dB、既定 −18）。Sensitivity（Low／Mid／High）＝ 時定数 2／0.8／0.3 秒、不感帯 1.5／0.75／0.25 dB（誤差が不感帯を超えたら動き始め、0.03 dB まで目標に寄せて止まる）。テスト：ボーカル −30、音楽 −20 dBFS（Target −18）でライド −8 dB（±0.8）、音楽が 6 dB 大きいとライドも 6 dB 上（±0.6）。
- **Breath skip（On）。** ボーカルの 40 ms の瞬時レベルが、直近のピーク（1 秒に 6 dB の速さで下がる保持）より 15 dB 以上低いとき、またはボーカルが −50 dBFS 未満のときはライドを保持する（息継ぎを持ち上げない。テスト：0.7 秒の息継ぎ（フレーズより 20 dB 低い）でライドは −6 dB 以下のまま、Off では 3 dB 以上持ち上がる）。**ピークの保持が下がりきる約 3 秒以上の長い息継ぎは、通常の小さな声として扱う。**
- **書き出し。** MS05 と同じ仕組み（`vo05.evo.on` ＝ Write automation On で Ride をホストへ書く。Off では Ride パラメータ（ホストのオートメーション）がそのままゲイン）。遅延 0。Output・Mix・Δ は無い（仕様書の「要確認：Δ ボタンが無い」のとおり）。

### VO06 Formant の設計（仕様書に数値がない部分）

- **構成。** VO01 と同じ音程エンジン（`PitchAnalyzer`＋`PsolaSynth`）に、固定の比を返す `RatioSource` をつないだもの。音の長さ・タイミングは変えない（PSOLA は各印の時刻を保つ）。Mix は共通の枠（既定 100 %）。入力がステレオのときは足してモノ。
- **遅延：1450 サンプル（30.2 ms@48 kHz）。** VO01 と同じ設定のため（仕様書の見積もり 512 サンプルとは違う）。
- **Pitch ±12 半音、Formant ±5。** Formant は **1 ＝ 母音の 1 半音ぶん**（スペクトル包絡だけが動き、基本周波数は動かない。テスト：基本周波数は 1.5 Hz 以内で同じ、700 Hz のエネルギーが ±4 で 700×2^(±4/12) へ移る）。
- **Character（設計値）。** 2 つのつまみに足す半音数（Pitch／Formant）：Neutral 0／0、Deep −2／−3、Bright 0／+2、Child +4／+4。
- **Keep timing。** On（既定）：母音は Formant（と Character）でしか動かない。**Off：声色が音程に追従する**（Pitch +n で母音も n 半音動く、テープのように声が大きくも小さくも聞こえる）。**どちらでもタイミングは同じ**（リアルタイムでは遅延を変えずに時間を伸び縮みさせられないため、「タイミングを保たない」動作は作っていない。仕様書の語感とは違う設計）。
- **Smooth。** On：Pitch／Formant の変更を 40 ms の時定数でなめらかに追従（次の印ごと）。Off（既定）：次の印で新しい値にそのまま切り替える。
- 仕様書の要確認：既定は 0（画面は +2 st・+1.5）。進化機能の「声域に合わせた自動補正」は未実装。

### VO07 Vocal Strip の設計（仕様書に数値がない部分）

- **順序。** HPF → De-ess → Breath → Body／Presence／Air → Comp → Level → 送り（Plate・Echo）→ Output（共通の枠）。**遅延 0。** 仕様書の「DY05・EQ05・DY02・RV02・DL01 の簡略版」は、DY05・DY02・RV02・DL01 の**コアをそのまま内部で使い**、パラメータを固定して必要な 1 つのつまみだけを出す形にした（EQ だけは Drive が不要なので、固定周波数の 3 つのフィルタを自前で持つ）。
- **HPF** 20〜300 Hz（2 次、既定 80 Hz）。**De-ess**＝DY05（Split、6.5 kHz、しきい値 −30 dB、Pitch follow On）の Range ＝ −1.5 dB × De-ess（最大 −15 dB）。純音ではなく高域ノイズで確かめた（テスト：ヒス −30 dBFS で De-ess 10 は −3 dB 超、500 Hz の音は変わらない）。0 のときも DY05 の分割（位相は all-pass、振幅は平ら）は通る。
- **Breath（自前）。** 40 ms のレベルが直近のフレーズのピーク（1 秒に 6 dB 下がる保持）より 15 dB 以上低く、かつ雑音的（20 ms の零交差率 0.12/サンプル超）なとき、ゲインを 1.5 dB × Breath（最大 −15 dB）下げる（10 ms／60 ms）。低い零交差率の小さな音（弱く歌った音）は息として扱わない（テスト）。
- **Body／Presence／Air。** 200 Hz（Q 0.8）／3 kHz（Q 0.9）のベルと、ハイシェルフ（角 6.5 kHz。12 kHz で ±6 dB に近づけるため、仕様書の「12 kHz」は角ではなく効きの位置と解釈：測定で ±6 dB から 1.2 dB 以内）。0 のフィルタは通さない。
- **Comp。** DY02 のコア（Level ＝ Comp、Speed Prog、Auto makeup On：音量が保たれる）。**段間の音量合わせは、コンプの自動メイクアップだけにした。** EQ の Body／Presence／Air は「聞こえる変化」が目的なので、音量の補正はかけない（広帯域の音楽では補正が EQ の効果を打ち消すため。仕様書の「段の間の音量は自動で整える」に対する設計判断）。
- **Level** ±12 dB はコンプの後。**Plate／Echo は送り**：Level の後の音を RV02（Decay 1.8 s、プリディレイ 20 ms、Low cut 120 Hz）と DL01（Analog、8 分音符（テンポ無しは 250 ms）、Feedback 30 %、HPF 200 Hz、LPF 6 kHz）へ送り、戻りは 0.5 × つまみ／10（つまみ 10 で −6 dB）。つまみ 0 で完全に無音の戻り（テスト）。Echo はホストのテンポに従う（60 bpm で 500 ms）。
- 進化機能（区分 A）は「正しい処理順」そのもの（`.evo.on` のスイッチは無い）。

### VO08 Breath の設計（仕様書に数値がない部分）

- **検出（規則による初期版。仕様書の「後期版で学習モデル」は未実装）。** 4 ms ごとに 12 ms の窓で、次の 3 つがすべて成り立つフレームを「息」とする：①雑音的（零交差率が Sensitivity Low／Mid／High で 0.16／0.12／0.09 を超え、約 12 kHz に間引いたコピーの正規化自己相関のピークが 0.6 未満＝有声音でない）、②フレーズのピーク（4 秒保持、1 秒に 3 dB 下がる）より 20／15／10 dB 以上小さい、③無音でない（−80 dBFS 超）。**2 フレーム続いたら息の開始**（息の頭から約 16 ms）、条件を外れた最初のフレームで終了。
- **遅延 1024 サンプル（21.3 ms@48 kHz）。** 判定は入力で、ゲインは 1024 サンプル遅らせた信号にかかる。判定が出る時点で出力側は息の最初の約 5 ms 前にいるため、**Fade が約 5 ms より長いと、一部は息の中でフェードする**。
- **Mode。** Reduce：ゲインを Reduction × Keep の係数だけ下げる。Remove：60 dB 下げる（Reduction は使わない）。Mark only：音は変えず、`breathActive()`／`breathCount()` を表示用に出す。
- **Keep（設計値）。** Natural は Reduction の 0.6 倍（息が聞こえる程度に残す）、Less は 1.0 倍、None は 1.5 倍（−40 dB で頭打ち）。
- **Fade** 1〜50 ms（Log）：下げ切るまでの時間（dB で直線）。**戻りは最大 10 ms**（息の終わりの判定は、次のフレーズの頭より約 10 ms 前に出るため。長いフェードで次のフレーズを覆わない）。テスト：Fade 50 ms で 24 dB の下げのうち 3〜21 dB の区間が約 37 ms、1 ms でほぼ即時。
- フレーズごとに残す・消すを選ぶ機能（区分 B・C）は、音声の取り込みが必要なため未実装（保存用の値も無し）。Δ ボタンは無い（仕様書どおり）。

### RS01 Denoise の設計（仕様書に数値がない部分）

- **共通部品 `sw/stft.hpp`。** √Hann の分析・合成窓と重ね合わせ加算（N＝2048、ホップ 512）。ハンドラは各ホップで半スペクトルを書き換える。遅延は N ＝ 2048 サンプル（42.7 ms@48 kHz、仕様書どおり）。ゲイン 1 のとき入力を 2048 サンプル遅らせた信号と 1.2e-7 以内で一致（テスト）。RS06 でも使う。
- **ノイズ推定。** Adaptive On：各ビンの平滑パワー（時定数 約 50 ms）の最小値を、8 つの小区間（Voice 0.2 s／Music 0.4 s／Field 0.25 s ＝ 窓 1.6／3.2／2 秒）で追い、×1.5（最小値の偏りの補正）。Adaptive Off：推定を固定（**一度も学習していなければ何も抑えない**）。Learn On：その間の平均パワーで推定を置き換える（Adaptive が On でも Learn 中は最小値統計を止める）。
- **ゲイン。** 前フレームから引き継ぐ事前 SNR（α ＝ 0.93／0.96／0.98 が Smoothing Low／Mid／High）、λ ＝ ノイズ推定 × 10^(Threshold/10)（Field は更に 1.15 倍）、Wiener ゲイン ξ/(1+ξ)、下限は Reduction。**Low band／High band** は下限に dB を足す（＋で深く、−で浅く。Low は 400 Hz 以下で全量、1.2 kHz で 0。High は 4 kHz 以上で全量、1.5 kHz で 0）。**Artifact guard** On：ゲインは時間方向に速い立ち上がり・遅い戻り（0.3）、周波数方向に 3 ビンの平滑。
- **注意：定常な純音はノイズとして覚えてしまう**（最小値統計は定常な成分を雑音と区別できない。テストは 0.25 秒ごとに入り切りするバーストで確認：ノイズは Reduction ±3.5 dB、信号は 1.5 dB 以内で残る）。声・音楽のように変動する信号では問題にならない。Learn で雑音だけを覚えさせれば定常音も守れる。
- Low lat（512 点・512 サンプル）は仕様書の表のつまみには無いので未実装。Profile の窓長・over-subtraction は設計値。

### RS03 Dehum の設計（仕様書に数値がない部分）

- **ノッチ列。** Harmonics（2／4／8／16）＝ノッチの本数（基本波とその整数倍、その数まで）。Depth 0〜10 ＝ 0〜−40 dB（1 で −4 dB）。各ノッチは Svf のベル（負のゲイン）。**Width は Q 60〜8（Log）**（半ゲイン点の幅が 50 Hz で 0.8〜6 Hz）。最初は Q 30〜4 にしたが、−40 dB のベルの裾が広く、隣のノッチの中間（75 Hz）が −2.3 dB 下がったため、Q を 60〜8 に変えた（Q 60 では 1 dB 以内）。
- **Buzz（設計値）。** 奇数次を Buzz × 1 dB だけ深く、Harmonics+1〜2×Harmonics 次に Buzz × 4 dB（最大 −40 dB）のノッチを足す。
- **Track（On）。** 基本波の周波数を ±2 Hz の範囲で追う：信号を 300 Hz のローパスで 1.5 kHz に間引き、1 秒の窓で最初の 4 倍音までを 0.25 Hz 刻みの 17 周波数に射影し、ピーク（放物線補間。中央値の 2 倍以上のときだけ）へ推定を半分ずつ寄せる。全ノッチが推定に追従する。テスト：51.5 Hz のハムを 9 秒後に −25 dB 以上（Track Off は −15 dB 未満）、50→51 Hz に流れるハムも −15 dB 以上。**追従は秒単位**（速く揺れるハムには間に合わない）。
- **Base Auto。** 50／60 Hz の両方を探し、強いほうに 2 回続けて決まったら切り替える（テスト：60 Hz のハムで 5〜8 秒に −25 dB 以上）。
- 遅延 0。仕様書の要確認（画面の Unit A／B／C は修復機に意味が無い）は、パラメータに含めていない。

### RS04 Declick の設計（仕様書に数値がない部分）

- **共通部品 `sw/ar_repair.hpp`。** Levinson-Durbin の AR 当てはめと、欠けた区間の最小二乗補間（Janssen／Godsill-Rayner：励起の二乗和を最小にする。帯幅 p の対称正定値 Toeplitz 系を帯 Cholesky で解く）。RS05 でも使う。
- **処理。** 128 サンプルごと・チャンネルごとに、直近 1024 サンプルへ 32 次の AR を当てはめ、励起 e の頑健な尺度 σ ＝ 中央値|e| ／ 0.6745 を出す。これから出力へ出る 128 サンプルのうち、|e| が T×σ を超えたところをクリックの頭とし、1・2・4 … サンプル（Click width まで）のうち、補間したあとの励起が T×σ 以下になる最小の長さで置き換える。**Click width を超える長さのものは触らない**（受け入れられなければ飛ばす）。遅延 512 サンプル（補間に必要な先読み。最大 5 ms ＝ 240 ＋ 32 ＜ 512 − 128）。
- **Target。** Click：T ＝ 7／5／3.5（Sensitivity Low／Mid／High）、全量置換。Crackle：T ＝ 4.5／3.5／2.8、長さは最大 0.2 ms、置換は Crackle %（x ＋ c（補間 − x））。Both：Click のあとに Crackle。**Low guard** On：窓のエネルギーの 90 % 超が 150 Hz 以下（バスドラムの打撃）のとき T を 1.6 倍にする。
- **確かめたこと（テスト）。** ランダムなクリック 1・3・12 サンプルで励起との誤差が 20 dB 以上下がる、Click width 0.3 ms と 1 ms で 48 サンプル（1 ms）のクリックの補修が分かれる（8 dB 以上）、Sensitivity High が Low より 5 件以上多く拾う、Crackle 0／50／100 % でスパイクの残りが 1.0／0.1〜0.6／0.35 未満、キック風のバーストは −40 dB 以上変わらない。
- **限界。** 2 ms（96 サンプル）級のクリックは補間が合わず、あまり直らない（実測で 3.6 dB 改善）。規則正しい繰り返し（例：+−+− の波形）は AR が予測してしまうので、クリックとして見つからない（端だけが見える）。テストのクリックはランダムな雑音のバースト。
- 画面の「Repair」ボタンの役割は未定のまま（仕様書の要確認）。直したクリックの印は `clicksRepaired()`（回数）まで。

### RS05 Declip の設計（仕様書に数値がない部分）

- **クリップの見つけ方。** 同じ側で天井以上のサンプルが 2 つ以上（最大 256）続いた区間。天井は Threshold（−3〜0 dB）。**Detect On：** 入力のままの直近 2048 サンプル（補修後の履歴ではなく、別に持った生の履歴）の最大値に「2 サンプル以上、1e-4 以内で最大値と等しい」塊が 4 回以上あれば、最大値 × 0.995 を天井にする（平滑。1 秒見つからなければ Threshold に戻る）。**なめらかな山の頂上も 0.5 % 以内に 7 サンプルほど入るため、最初の「0.5 % 以内に 6 つ」では、クリップしていない信号の 2604 か所を誤って直した（実測）。** 条件を「1e-4 以内の連続 2 つ以上の塊が 4 回」に改めた。
- **補間。** 256 サンプルごとに、出力へ出ていく区間に頭がある連続を AR（Quality Low／Mid／High ＝ 16／32／64 次）で補間する。**窓の中のクリップを先に 16 次で見積もり、その窓へ希望の次数のモデルを当てはめ直して、元のサンプルから補間し直す**（平らな頭をそのまま学習すると高次ほど悪化した：64 次の誤差が 16 次より大きかった。見積もりを挟んで解消）。補間値は天井の下へは行かないよう天井まで押し戻し（クリップした山が天井より低く出ることは無い）、天井 ＋ 9／6／3 dB（Smooth Low／Mid／High）で頭打ち。Makeup（−12〜0、既定 −3）は出力へのゲイン（戻した山の余裕）。遅延 1024 サンプル。
- **確かめたこと。** 1.5 倍のピークを 0.95 で切った音声風の信号で、原音との誤差が 8 dB 以上下がる、Detect Off では何も触らない（1e-6 以内で入力と一致）、Detect On で天井 0.6 を 1 % 以内で見つけて直す。**限界：** 信号の 60 % 近くが切れている極端な例（1.5 倍を 0.6 で切る）では、改善は約 2〜5 dB。AR 補間は短いクリップ（数 ms まで）向け。

### RS06 Dereverb の設計（仕様書に数値がない部分）

- **構成。** STFT 1024 点・ホップ 256（`sw/stft.hpp`）。遅延 1024 サンプル（仕様書どおり）。ビンごとに、平滑パワー Ps（フレームあたり 0.6）と、後部残響のパワー λ(l) ＝ max(ρ·λ(l−1), ρ^D·Ps(l−D))。ρ ＝ 10^(−6h/Tail)（Tail 秒で 60 dB 減る。h ＝ 5.33 ms）、D ＝ 初期反射の長さ（Early Keep 50 ms ＝ 9 フレーム、Reduce 20 ms ＝ 4 フレーム）。「D より後に、より大きな瞬間の後ろで来るものは残響」という Polack 型の統計モデル。
- **ゲイン。** ξ ＝ max(P/λ − 1, 0)、G ＝ ξ/(1+ξ)、下限は Reduction（0〜−30 dB）。Smooth（Low／Mid／High）＝ 戻り 0.5／0.7／0.85 フレーム（開くのは即時）、Mid と High は 3 ビンの周波数平滑も。
- **Learn room。** On の間、広帯域レベルの下降を監視（32 フレーム＝170 ms で 12 dB 以上下がり、1 ステップで 1 dB を超える上昇が無い）し、回帰した傾きから −60／傾き を Tail の候補にする。Off にしたとき候補の中央値を **Tail へ書き込む**（takeParamWrite、0.1〜5 s）。候補が無ければ書かない。テスト：T60 0.6 s の人工的な部屋（ノイズのバーストを指数減衰ノイズの IR で畳み込み）で 0.36〜0.9 s の範囲に出る。**実際の声は減衰が見えにくく、無音の隙間がある録音でないと測れない**（仕様書の「無音の隙間で…」のとおり）。
- **確かめたこと。** 同じ部屋で、Reduction −30・Tail 0.6 で尾の部分（バースト終了後 0.1〜0.35 s）が 6 dB 以上下がり、バースト頭の直接音は 3 dB 以内で残る。Reduction −30 は −10 より 3 dB 以上深く、Tail 0.6 は 0.1 より 1.5 dB 以上効き、Early Reduce は Keep より深い。

### RS07 Mouth Noise の設計（仕様書に数値がない部分）

- **RS04 の検出・補修（AR 32 次、ホップ 128、遅延 512）に「語と語の間か」の重みを付けたもの。** 候補（励起が T×σ を超えた点）は、まわり（前後 20 ms、窓 1024 サンプルの範囲内。クリック自身の最大長は除く）のレベルが、窓レベルのピーク保持（4 秒保持、1 秒に 3 dB 下がる）より **12 dB 以上低いときだけ**直す。語の中では何も触らない（テスト：同じクリックを、間では 15 dB 以上直し、語の中では 1 dB 以内で触らない）。
- **Sensitivity** Low／Mid／High ＝ T が 6／4.5／3.5。**Click size** Small／Medium／Large ＝ 最長 0.3／0.6／1.0 ms（最初は 2 ms を考えたが、補間に必要な先読み 512 サンプルから、補修区間（クリック＋両側のフェード）が 350 サンプル以内に収まる範囲で最大 約 1 ms にした。48 kHz で約 50 サンプル）。
- **Freq skew** ±50 %：検出に使う励起を傾ける。＋は高域寄り（e ＋ s(e[n] − e[n−1])：1 サンプルのスパイクを拾いやすい）、−は低域寄り（e ＋ |s|(e[n] ＋ e[n−1])：ゆるいこぶを拾いやすい）。
- **Fade** 0.5〜10 ms：クリックの長さを RS04 と同じ方法（補間後の励起が T×σ 以下になる最小の長さ）で決めたあと、補修区間を**前後に Fade/2（最大 60 サンプル）広げて補間し直す**（補修の両端がきれいな信号の上に載る）。
- **作る過程で直したもの：** 窓より長い 40 ms の範囲を読んで窓の外を参照していた（ASan で検出、窓の範囲に制限）。
- 画面の補助機能（語と語の間だけを見つける区間表示）は、検出の内部で完結しており、表示用の値は `clicksRepaired()` まで。

### CR01 Filter の設計（仕様書に数値がない部分）

- **フィルタ。** 2 極の ZDF 状態変数フィルタ（LP／BP／HP／Notch、2× オーバーサンプリング）。Resonance 0〜100 ％ → 減衰 k ＝ 2(1 − 0.95 r)（最小 0.1：カットオフでのピークは 1/k で最大 +20 dB）。Drive 0〜100 ％ → 入力ゲイン G ＝ 1 ＋ 9d の tanh（小信号のゲインは 1）と、バンドパス状態の tanh を d でブレンド（**Drive 0 は完全に線形**：最初は常に tanh を通していて、Drive 0 でも 400 Hz の 3 次高調波が −28.6 dB 出ていたため）。仕様書の「2極・4極」の 4 極は未実装（表に切り替えが無い）。Mix・Output は無い（仕様書の表のとおり）。
- **Mod source。** Envelope（入力のピーク包絡、アタック 3 ms・リリース 120 ms）、LFO（**ホストのテンポで 1 小節に 1 周期**、テンポ不明は 0.5 Hz。仕様書に LFO の速さのつまみが無いための設計値）、Sidechain（外部サイドチェーン。無ければ入力）。変調量 m（0〜1）で、カットオフを 2^(5 × Env amount × m) 倍（±100 ％ ＝ ±5 オクターブ）。
- **進化機能 `cr01.evo.on`（末尾に追加）。** On（既定）：m ＝ 直近 10 秒の最大・最小（0.5 秒ブロック 20 個の最大・最小、−70 dB 未満の無音は最小に数えない、幅は最低 6 dB）に対する位置。音量に関係なく Env amount が全域で効く（テスト：−6 dB 始まりと −36 dB 始まりの 12 dB の揺れで、通過量の差が 4 dB 以内で同じ）。Off：m ＝ (レベル ＋ 40 dB)/40 dB。**最初は「最大は保持して 10 秒で 6 dB 下げる」方式にしたが、初期値が広すぎて収まらず、ブロック最大・最小方式に変えた。**
- 画面のΔボタンは無い（仕様書の要確認のとおり）。

### CR02 Stutter の設計（仕様書に数値がない部分）

- **位置。** ホストのテンポと「次の小節線までの拍数」から小節内の拍位置を出す（**4/4 を仮定**。拍子が違うホストでは小節線の位置だけが合う）。トランスポートが無いときは、再生開始からテンポ（無ければ 120 bpm）で走らせる。Grid の 1 ステップ ＝ 1/8（0.5 拍）／1/16（0.25）／1/32（0.125）／Triplet（**1/8 の 3 連 ＝ 1/3 拍**。仕様書に長さの記載が無いための設計値）。16 ステップのパターンは、拍位置の絶対カウントを 16 で割った余りで回す（1/16 なら 1 小節、1/8 なら 2 小節で 1 周）。
- **繰り返し。** パターンが On のステップの頭で、**ちょうど今までに通り過ぎた入力の最後の（ステップ長 ÷ Repeat）サンプル**を取り込み、Repeat 回繰り返す（遅延 0）。Gate ＝ 各繰り返しの開いている割合（両端に 1 ms のフェード）、Pitch ＝ 再生速度（整数半音、スライスより先へ行けば頭に戻る）、Reverse ＝ 逆再生、Filter ＝ 繰り返しに 2 次のローパス（**20 kHz は外す**：LP が 20 kHz でも位相が回って、そのままの繰り返しにならなかったため）、Mix ＝ そのステップの間だけ原音と混ぜる（traits の Mix は使わず、製品の中で処理）。Off のステップは原音のまま（ビット単位で一致）。
- **Pattern。** `cr02.step01〜step16`（Auto 不可、既定は x.x. x..x x.x. x...）。**Randomize・Clear は `randomize()`・`clearPattern()` のメソッド（画面のボタンが呼び、16 個のステップのパラメータをホストへ書く）。** 最初は `cr02.random`・`cr02.clear` というボタン用パラメータを作り、コアが処理して takeParamWrite で自分を Off に戻していたが、clap-validator の param-set-events（フラッシュと process で同じ値になること）に不合格になったため、パラメータをやめた（コアが自分でパラメータを書き換える製品は、この検査と両立しない）。
- **Randomize（進化機能・区分 A）。** 拍の中の位置で確率を決める（拍の頭 0.85、8 分の裏 0.6、16 分の裏 0.35、その他 0.4）。直近の 1 小節で入力に立ち上がり（5 ms と 100 ms の包絡の比 2.8 倍以上）があったステップは確率を ＋0.3（最大 0.95）。テスト：400 回の平均が 0.85／0.6／0.35 の ±0.06 以内、立ち上がりがあるステップの密度は 0.35 から 0.55 超へ。
- 画面のΔボタンは無い（仕様書の要確認のとおり）。

### CR03 Granular の設計（仕様書に数値がない部分）

- **粒。** 入力の過去から切り出した Hann 窓の粒。Density 回/秒で生まれる（間隔は 1/Density の 0.7〜1.3 倍）、同時に最大 64、再生速度 2^(Pitch/12)、**重なりの数（Density × 粒の長さ）で正規化**（Hann の平均二乗 3/8 も補正して、密度を変えてもレベルは変わらない。テスト：Density 10 と 80 で 3 dB 以内、30 で入力の ±4 dB 以内。最初は 3/8 を忘れて入力より約 6 dB 小さかった）。粒は過去だけを読むので遅延 0。Mix は共通の枠（ウェットだけ返す）。
- **Mode。** Cloud：粒の長さ × 速度だけ過去から読み、Spray ％ で最大 0.5 秒まで余分にさかのぼる。Scatter：直近 2 秒のどこからでも。Glitch：開始位置を粒の長さの 1〜4 個分前に揃え（格子）、1/4 は逆再生、1/3 は直前の開始位置・長さを繰り返す。**Spread：** Mono（中央）／Narrow（±0.3）／Wide（±1）のランダムなパン（定電力）。**Freeze input：** 書き込みを止め、粒は固まった直近の音を読み続ける（テスト：入力が止まったあとも −35 dB 超が 2 秒以上続き、Off では −80 dB 未満）。
- **Harmony（進化機能 `cr03.evo.on`、On）。** 0.125 秒ごとに入力の 12 音の分布を測り（直近 8192 サンプルの FFT、60 Hz〜2 kHz のスペクトルのピークを 12 音へ）、直近 4 秒（32 フレーム）の分布の強い音（最大の半分以上、最大 4 つ）を「和音」とする。粒の音程は、その粒の元の音（粒の元の区間の真ん中に最も近いフレームの最強の音 s）に対して、s ＋ k が和音の構成音になる k のうち Pitch に最も近いもの（±6 半音、上位 2 つから 65 %／35 %）にする。和音が見つからないとき（−60 dBFS 未満）は Pitch のまま。テスト：C・E・G を 0.5 秒ずつ回す入力（和音 ＝ C E G）で Pitch +5 のとき、Off では出力の音のうち構成音（C・E・G）が占める割合が 0.6 未満、On では 0.8 超。**限界：** 和音（同時に鳴る複数の音）を入力すると、1 つの k では構成音のまま保てない（旋律・単音の入力向け）。
- 画面のΔボタンは無い（仕様書の要確認のとおり）。

### CR04 Freeze の設計（仕様書に数値がない部分）

- **原音は遅らせない（遅延 0）。** Mix は製品の中で処理する：出力 ＝ 原音 ×（1 − Mix×a）＋ 固めた音 × Mix × a（a は固まりの出入り：立ち上がり 20 ms、Hold の戻り 400 ms、Momentary の戻り 40 ms）。固めていないときは原音がそのまま通る（Mix に関係なくビット単位で一致）。共通の枠の Mix を使うと、固めていなくても原音が小さくなってしまうため、枠の Mix は使わない。
- **取り込みと再生。** 取り込み：直近 4096 サンプル（Hann）の FFT の振幅。ピーク（隣より大きく、最大の 1 % 超）を正弦波とみなし、各ビンを最も近いピークに割り当て、Hann 窓のスペクトルの符号（ピークからの距離の偶奇で π）で位相を付ける。ピークの周波数は放物線補間、1 フレーム（ホップ 1024）ごとに位相を ω×ホップだけ進める。4096 点・Hann 窓・オーバーラップ 4 で合成（Hann² の和 1.5 を 2/3 で補正）。**ランダムな位相を付けるだけだと、隣のビンどうしのうなりで音が揺れるため、ピークごとに位相をそろえる方式にした**（取り込んだ正弦波は音程・レベル・位相を保つ：テストで 440 Hz → 1.5 Hz 以内、−18 dB ±3 dB、0.25 秒ごとのレベルの差 2.5 dB 以内）。
- **Blur** 0〜100 ％：振幅を ±0〜24 ビンで平均（パワーは保つ。保たないと Blur 40 で約 10 dB 小さくなった）。**Drift** Off／Slow／Fast：ピークごとの位相に 1 フレームあたり 0／0.1／0.6 rad のランダムな歩み（＋Blur/100 × 0.2）。
- **Trigger。** Hold・Momentary：Freeze が On になった瞬間に取り込み（Hold は On の間続き、Off で 400 ms かけて消える。Momentary は 40 ms）。**Auto：** Freeze On で待機し、入力の立ち上がり（5 ms の包絡が 100 ms の包絡の 2.8 倍以上、−50 dB 超、150 ms 以上の間隔）ごとに取り込み直す（15 ms のクロスフェード）。MIDI ノートでの取り込み（仕様書）は MIDI 入力がまだ無いため未実装。
- 画面のΔボタンは無い（仕様書の要確認のとおり）。

### CR05 Tape Stop の設計（仕様書に数値がない部分）

- **テープ。** 入力を 8 秒のリングに書き、出力は速度 s(t) でそこを読む（音程は速度に従う）。全速のときは今の入力そのもの（遅延 0）。進み具合 u（0〜1、指定の時間）の曲がり w(u)：Lin ＝ u、Exp ＝ u²（最初は速度を保ち、あとで急に落ちる）、Log ＝ √u（最初に急に落ち、あとは尾を引く）。**Stop**：s ＝ 1 − w、止まったら Trigger が Off になるまで無音。**Start**：停止から s ＝ w、終わったら 30 ms のクロスフェードで今の入力へ。**Spin back**：s ＝ 1 − 3w（最大 −2 倍で逆回転）、終わると無音。音量は速度に従い min(1, 4|s|)。Filter On：ローパス max(300 Hz, 18 kHz × |s|^1.5)。時間は 1/16〜2 小節（既定 Stop 1/2、Start 1/8。4/4・テンポ無しは 120 bpm と仮定）。
- **Trigger。** Off→On で動作を始める（オートメーション用）。止まったまま（Held）の状態は Off に戻すと 20 ms ほどで今の入力へ戻る。
- **進化機能（区分 A）。** ホストのトランスポートが分かるとき、Stop と Spin back は**（次の小節線 − 時間）に始める**（過ぎていれば次の小節）ので、止まるのがちょうど小節線になる（テスト：次の小節まで 1.5 秒のとき 1/4 小節（0.5 秒）の Stop が 1 秒待って始まり、小節線（1.5 秒後）の ±0.12 秒で無音になる）。Start はすぐ始める。トランスポートが無いときはすぐ。
- **限界。** Start の終わりの 30 ms のクロスフェードは、テープが遅れた分（最大時間の半分）を一気に今の入力へ飛び越える（クロスフェードの間は 2 つの音が混ざる）。画面のΔボタンは無い。

### CR06 One Knob の設計（仕様書に数値がない部分）

- **6 種類の内部チェーンは、同梱製品の処理の簡略版**（仕様書は「既存製品の処理を組み合わせた内部チェーン」。SA05・EQ01・DY07・DY09 のコアをそのまま使うと、1 つのつまみで動かす値の範囲の調整が難しいため、必要な部分だけを自前で持った。Space だけは RV02 のコアを使う）。Amount t ＝ 0〜1 に対する曲線：**Wide** ＝ ミッド／サイドでサイド × (1 + 2t) と 4 kHz 以上に +3t dB。**Warm** ＝ tanh のドライブ (1 + 3t) を t でブレンド、150 Hz に +3t dB の低域シェルフ、6 kHz に −3t dB の高域シェルフ。**Air** ＝ 10 kHz に +6t dB の高域シェルフと、6 kHz 以上を tanh(3x) に通して 0.1t 足すエキサイター。**Punch** ＝ 2 ms と 40 ms の包絡の立ち上がりで、アタックを最大 +8 dB（×1.5t）、持続を最大 −2t dB。**Space** ＝ RV02（1.2 秒、プリディレイ 15 ms、ダンピング 60 ％、ローカット 150 Hz）を 0.5t 足す。**Lo-fi** ＝ サンプルレートを 1 + 11t 分の 1 に下げる（ホールド）、量子化 16 → 6 ビット、ローパス 12 → 3 kHz。
- **Amount 0 はどの効果でも原音そのもの**（ビット単位。テスト）。Mix・Output は共通の枠。遅延 0。
- **Macro**（`cr06.macro`、画面専用、Auto 不可）：選んだ効果の内部の値を画面に見せる。値は `macroValue(i)` で取れる（Air なら 0 番がシェルフの dB）。
- 上の曲線はいずれも設計値で、聴いて決めたものではない（テストは、各効果が狙った向きに動くことだけを確かめている：Wide はサイドが 8 dB 以上増えミッドは 0.2 dB 以内、Warm は 3 次高調波が 20 dB 以上増える、Air は 14 kHz が 5 dB 以上上がる、Punch はアタックと持続の比が 3 dB 以上広がる、Space は尾が出る、Lo-fi は高調波が 20 dB 以上増え 6 kHz が下がる）。

### MT01 Loudness の設計（仕様書に数値がない部分）

- **計測器の共通事項（MT01〜MT05）。** 音は変えない（出力は入力とビット単位で一致、遅延 0）。**Δ と Auto gain は持たない**（仕様書の共通事項）。アダプターに「Δ なし」の指定 `kDelta = false` を追加した（Auto gain は従来の `kAutoGain = false`）。計測値は処理のたびに更新し、画面が読む。解析自体もオーディオ側のコアで行う（仕様書は「UI スレッド」と書くが、プラグインの UI がまだ無いため、コアが計算して値を持つ形にした）。
- **MT01。** Momentary（400 ms）・Short-term（3 秒）は BS.1770 の K 特性の 100 ms ブロック（`sw::LoudnessMeter`）。Integrated は絶対ゲート −70 LUFS・相対ゲート −10 LU（`sw::IntegratedLoudness`）。**Range（LRA）は EBU Tech 3342**：Short-term 値を 100 ms ごとに（3 秒たってから）0.1 LU の箱へ入れ、絶対ゲート −70 LUFS、相対ゲートはそのパワー平均の −20 LU、LRA ＝ 95 パーセンタイル − 10 パーセンタイル。True peak は 4 倍の Kaiser 補間（`sw::TruePeakDetector`、dBTP）。直近 10 分の推移は Short-term 値を 1 秒ごとに 600 個。Preset：ARIB TR-B32 −24／EBU R128 −23／Streaming −14／Custom（Target −40〜−5 LUFS）。`difference()` ＝ Integrated − 目標、`inBand()` ＝ 差の絶対値が Tolerance（±0.5〜±3 LU、表示する帯の幅。**±1 LU が規格上の許容値かは仕様書の要確認のまま**）以内。Pause（パラメータ、Auto 不可）で計測を止め、`reset()`（画面のボタン）でクリア。
- **確かめたこと。** −26 dBFS RMS のステレオ 1 kHz は −23.0 LUFS（Momentary・Short-term・Integrated が 0.2 LU 以内で一致、LRA 0.3 LU 未満）、−40 と −30 LUFS の 2 水準（30 秒ずつ）の LRA は 10 ±1.5 LU で無音は数えない、fs/4 の 45° の正弦波（サンプルのピークが 0.707 倍に見える）の True peak は真のピークの 0.4 dB 以内。
- 表記は「LUFS」に揃える（LV23 の「LKFS」と同じ量。仕様書の要確認）。

### MT02 Spectrum の設計（仕様書に数値がない部分）

- **スケール。** FFT 4k／8k／16k／32k 点（Hann、ホップ N/4）、(L＋R)/2 のモノ。**ピーク振幅 A の正弦波がそのビンで 20 log10(A) dB を示す**（雑音はビンが細かいほど低く出る：ピーク基準の目盛り）。Speed：パワーの平均時間 Slow 2 秒／Medium 0.5 秒／Fast 0.12 秒。Display：Average（指数平均）、Peak（最大値、1 秒に 20 dB 下がる）、Hold（`reset()` まで最大値）。
- **表示値 `spectrumDb()`。** Smoothing（Off／1/24／1/12／1/6／1/3 オクターブ：その幅のパワー平均）、Slope（1 kHz を中心に、1 オクターブあたり Slope dB 持ち上げる。ピンクノイズが平らに見えるのは 3 dB/oct）、Range（−Range 未満は出さない）。
- **進化機能（Compare A、区分 B）。** `captureReference()` で今の平均を参照カーブとして保存し、`compareDb()` が（今の平均 − 参照）を返す。参照曲の長時間平均を作る使い方を想定。**ジャンル別の型は元データが要るため未実装**（仕様書の要確認のまま）。
- **確かめたこと。** ビン上の正弦波が 4k／8k／32k のどれでもピーク値を ±0.6 dB で示す、Slope 3 dB/oct で 2 オクターブ上が +6 dB、Smoothing 1/3 オクターブで雑音の凸凹が半分未満、20 dB の段差の 0.5 秒後に Fast は Slow より 3 dB 以上高い、Peak は 3 秒後に Hold より 10 dB 以上低い、参照との差が 6 dB の雑音で ±1 dB。

### MT03 Spectrogram の設計（仕様書に数値がない部分）

- **列。** (L＋R)/2 の STFT（4096 点・Hann・ホップ 1024 ＝ 21.3 ms）を、選んだ Scale の 256 本の表示帯域に束ねて 1 列にする（Linear は 20 Hz〜20 kHz を等間隔、Log はオクターブ等間隔、Mel はメル等間隔）。値は MT02 と同じピーク基準の dB（帯域の中で最も強いビンを採るので、純音がレベルを保つ）、下限は Floor（−120〜−60 dB）。Scroll（2〜60 秒）の間だけ保持（最大 3000 列）。Contrast・Palette・Show notes・Show freq は画面の設定として保存するだけ（Heat パレットは状態色を使わない配色にする、という仕様書の注意は画面側）。
- **進化機能（帯域のその場試聴、区分 A）。** `setPreview(true, 下端, 上端)` で、その帯域を 4 次のバンドパス（20 ms のクロスフェード）で聴く。**試聴中は出力が変わる**ので `previewActive()` を画面の警告（書き出し時に注意）に使う。**通過帯域の中心で 6 dB 小さくなっていたため（狭い帯域の 4 次）、中心の損失を補う（最大 +12 dB）。** 試聴していなければ出力はビット単位で一致。
- テスト：各 Scale で 1 kHz の純音が 1 本の帯域（±1）に、ピーク値 ±1.6 dB で出る、Scroll 2 秒で保持する列が 2 秒分、Floor 未満が出ない、試聴で 5 kHz が 40 dB 以上下がる。

### MT04 Phase Scope の設計（仕様書に数値がない部分）

- **スコープの点。** x ＝ (L − R)/√2、y ＝ (L ＋ R)/√2（モノは縦線）、16 サンプルに 1 点、Persistence 秒ぶん（最大 12000 点）。Zoom 1／2／4／8 倍は `scopePoint()` が掛ける。**相関。** 300 ms の指数窓の相関係数 ΣLR/√(ΣLL ΣRR)。全帯域と、62.5 Hz〜8 kHz のオクターブ 8 帯域（両チャンネルへ Q 1.4 のバンドパス）。
- **進化機能（区分 A）。** ある帯域の相関が **0.7 秒続けて負**になったら `warnings()` のその帯域のビットを立て（仕様書の「1 秒以上続きそうになったら」）、相関が +0.1 を超えたら下げる。信号の無い帯域は何も言わない。`summedLossDb()` は L＋R のパワーを L と R のパワーの和に対して見たもの（モノにしたときの目減り：相関 +1 で +3 dB、逆相で −40 dB 以下、無相関で 0 dB）。
- テスト：モノは +1、逆相は −1、独立した雑音は 0.15 以内、125 Hz だけ逆相の信号で 0.5 秒では警告なし・3 秒後は 125 Hz の帯域だけ警告（4 kHz は出ない）で、相関が戻ると消える。

### MT05 Vu Ppm の設計（仕様書に数値がない部分）

- **VU。** 整流平均 ×1.1107（正弦波が RMS を示す）を 65 ms の 1 次ローパスに通す（階段で 300 ms に 99 %）。Ref dBFS（−14／−18／−20）の RMS の正弦波が 0 VU。**PPM（仕様書は「種類は要決定」）：** IEC 60268-10 の Type II を基にした設計値：整流した信号が時定数 4.5 ms で上がり（10 ms のバーストは定常より約 1〜3 dB 低い）、戻りは dB で直線に 1.7 秒で 20 dB。0 の基準は Ref のピーク（正弦波のピーク値）。
- Δ・Auto・Unit は持たない（仕様書の推奨）。**SW Link でプロジェクト単位の 0 VU 基準を共有する進化機能は SW Link が無いため未実装**（Ref はインスタンスごとのパラメータ）。
- テスト：Ref の RMS の正弦波が 0 VU（±0.1）、−12 dBFS で +6 dB、300 ms で 99 %（±1.5 %）、PPM の戻りが 20 dB ±1.5 dB（1.7 秒）、左右が別々に読める。

### UT01〜UT03 の設計（仕様書に数値がない部分）

- **共通。** 遅延 0。Auto gain は持たない（仕様書「UT01 は外す」。UT02・UT03 も打ち消し合うので同様）。Δ は持たない（`kDelta = false`）。
- **UT01 Gain。** 処理順は 極性（Ø L／Ø R）→ Swap → Width（Mid／Side で Side×Width/100）→ Mono（(L+R)/2）→ Balance（直線。小さくする側だけ下げ、中央は 0 dB）→ Gain。Gain・Balance・Width は 10 ms で滑らかに動かし、既定値ではビット単位で素通し。Channel は Gain を掛けるチャンネルの指定（もう片方はそのまま）。**進化機能（トラックの種類ごとの Gain）：** トラック名を Vocal／Drums／Bass／Guitar／Keys／Bus／Other に分類し（英語・日本語のキーワード）、種類ごとの Gain を保存データに持つ（`rememberGain()`／`suggestedGainDb()`）。**ホストからトラック名を受け取る部分（CLAP track-info、VST3 のトラック情報）はプラグイン層の仕事で未実装。**
- **UT02 Mono Check。** Mono は (L+R)×Mono fold（−3 dB で等パワー和、無相関の素材は 0 dB、モノ素材は +3 dB）、Side は (L−R)/2 を両チャンネルへ、Left／Right はそのチャンネルを両方へ。Phone speaker は LO01 と同じ小型スピーカー模擬（300 Hz の 4 次ハイパス＋1.2 kHz に +3 dB・Q 2.5）。Low cut は Off＋20〜300 Hz（最下段が Off）の 2 次ハイパス。Level は最後に掛ける。監視専用なので Stereo 以外・Phone・Low cut が有効なときは `exportWarning()` が true（書き出し前の警告用。画面は未実装）。
- **UT03 Reference。** 参照曲 B／C は `loadReference(スロット, バイト列)` で読む。**WAV（PCM 8/16/24/32 bit、float 32/64、extensible）と AIFF（PCM 8〜32 bit）に対応。仕様書にある FLAC・MP3 は未対応**（デコーダーを自作する範囲を超えるため。読めないファイルは false を返す）。ホストのサンプルレートへは窓付き sinc（Blackman、片側 32 タップ、ダウンサンプルはカットオフを下げる）で変換。ラウドネス合わせは「入力の統合ラウドネス（30 秒の記憶）− 参照曲全体の統合ラウドネス」を参照曲に掛ける（±24 dB で頭打ち、入力が絶対ゲート未満のあいだは 0 dB）。画面に出す補正量は `matchDb()`。Loop は Intro＝先頭 20 秒、Verse＝20〜40 秒、Chorus＝ファイル中でいちばん大きい 20 秒（100 ms ブロックの平均二乗、1 秒刻み）、Custom＝`setLoopRegion()`。20 秒以下のファイルは全体をループ。Sync play On でホストの再生位置（アダプターに任意フック `setPlayhead(秒, 再生中か)` を追加）に区間内で追従、ホストが止まっていれば参照曲は無音。Off（またはホストが時刻を出さない）なら区間の頭から自走。Crossfade は等パワー、0 ms は即切り替え。Level は参照曲だけに掛ける。ループの端と位置の飛びには 5 ms のフェードを入れる。Source A のときは入力と**ビット単位で同じ**。読み込んだ参照曲は保存データに含めない（ファイルなので画面が読み直す）。
- テスト：UT01 は各パラメータの式・ビット同一・トラック分類・保存と読込（8 件）、UT02 は各 Listen の式・Phone の周波数特性・Low cut・警告（5 件）、UT03 は WAV 16/24/float・AIFF の読み込みと破損データの拒否、リサンプラーの誤差 −70 dB 以下と折り返しなし、ラウドネス合わせ（±0.03 の比率で一致）、Loop 区間、Sync の位置一致、Crossfade の等パワー、2 本の参照曲・モノ入力・別レート・端数ブロック（12 件）。

### LV01 Voice の設計（仕様書に数値がない部分）

- **段と終点。** 仕様書は「4 段を Voice と Use で動かす」とだけ書き数値がないので、Use ごとの終点（Voice 100 % のとき）を設計値とした。Voice はすべての段の量を同じ割合で動かす（Voice 0 は完全バイパスで入力とビット単位で一致）。

| Use | Noise 最大 | 低域カット | 〜250 Hz | 3.5 kHz | 10 kHz 以上 | Comp しきい値 | Comp 比 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Narration | −18 dB | 70 Hz | −2.5 dB | +2.5 dB | +2.0 dB | −24 dBFS | 3 : 1 |
| Stream | −14 dB | 80 Hz | −3.0 dB | +3.0 dB | +1.5 dB | −22 dBFS | 4 : 1 |
| Meeting | −20 dB | 100 Hz | −2.0 dB | +4.0 dB | 0 dB | −24 dBFS | 3.5 : 1 |
| Singing | −6 dB | 40 Hz | −1.0 dB | +2.0 dB | +3.0 dB | −20 dBFS | 2.5 : 1 |

- **Noise。** 250 Hz・3 kHz で 3 分割（LR4、遅延 0）し、帯域ごとのノイズ床を追う。床は 30 ms の電力平均の最小値（下には即座に、上へは 3 dB/秒、帯域あたり −42 dBFS が上限）。床＋6 dB を下回った分を 3 倍（1 dB 下がるごとに 3 dB）に下げ、Use の深さ×Voice で頭打ち。開く 2 ms、閉じる 80 ms。FFT を使わないので遅延 0。起動直後と Voice 0 からの復帰の 0.15 秒は床を素早く学習する。
- **EQ。** 低域カット（2 次、Voice に応じて 20 Hz から終点まで指数的に）、250 Hz のベル（Q 0.9）、3.5 kHz のベル（Q 0.9）、10 kHz のハイシェルフ。
- **Comp。** 左右連動のピーク検出、アタック 8 ms・リリース 150 ms、ソフトニー 6 dB、比は 1 から終点まで Voice で。メイクアップは −18 dBFS の信号が受ける低減量の半分。
- **Limit。** −1 dBFS（サンプルピーク）、先読みなし・即座に効く、リリース 80 ms。**仕様書は「−1 dBTP 目標、先読みなし」で、先読みなしではサンプル間ピークまでは保証できないため、保証するのはサンプルピークのみ**（トゥルーピークの厳密な制限が必要なら LV04 の True peak モード）。
- **Mute。** 押している間 5 ms で無音へ、離すと 5 ms で戻る（自動化不可）。段の値（`stage()`）は表示用に公開している。
- テスト（9 件）：表、Voice 0 のビット一致、段の値、部屋のノイズが 8 dB 以上下がり声は ±4 dB に収まる、床が部屋に追従、低域カットと presence、Comp と −1 dBFS の天井、Mute、モノ・端数ブロック・prepare 前。

### LV02 Feedback・LV03 Channel の設計（仕様書に数値がない部分）

- **共通部品 `sw/feedback.hpp`（FeedbackGuard）。** 仕様書は「別スレッドの FFT 解析」だが、スレッドを持たず、オーディオスレッドで 4096 点（ハン窓）の FFT を 2048 サンプルごとに 1 回行う（固定バッファ、1 フレーム約 0.3 ms）。候補は「−70 dBFS 以上の局所最大で、周囲（8〜20 ビン離れ）より感度のしきい値（Low 18／Mid 14／High 10 dB）以上突き出し、4〜5 ビン離れより 12 dB 以上高い（純音）」もの。フレームをまたいで追い（1.5 ビン以内、1 フレームで 3 dB より下がらない）、持続フレーム数（Low 8／Mid 5／High 3、約 0.34／0.21／0.13 秒）に達したらハウリングと判定する。**倍音関係：** 別の持続音の整数倍（2〜6 倍、±2 %）で、その音が同じかより大きいときは楽音とみなして削らない。
- **フィルタ。** ベル型のカット（幅は Width、深さは検出の突き出し量 −4 dB を下限 6 dB・上限 Max depth に）。同じ場所（1/6 oct 以内）で再検出されるたびに +3 dB 深くなり（Max depth まで）、Release 秒のあいだ検出されなければ 1 秒かけて戻り、枠が空く。FIXED は Ring out と Lock filters で作り、消えない。枠が全部埋まったら LIVE のうち最後に検出された時刻が最も古いものを入れ替える。
- **LV02。** 枠は 12（F1〜F12）。Ring out／Lock filters／Clear live は**操作ボタンなのでパラメータではなくメソッド**（CR02 と同じ扱い。画面のボタンは未実装）。Ring out 中は 2 フレームで確定し、しきい値を 4 dB 下げ、FIXED で Max depth に登録する。FIXED は保存データに入る（LIVE は入らない）。Width は oct 単位の値（1/20〜1/3）。
- **LV03。** 処理順は Trim（と Ø）→ HPF → Gate → EQ（Low 100 Hz シェルフ、Mid ベル Q 1、High 8 kHz シェルフ）→ Feedback guard（LV02 と同じ部品の LIVE 4 枠、感度 Mid・−12 dB・1/10 oct・8 秒）→ Comp → De-ess → Out。Gate はヒステリシス付き、アタック 1 ms・ホールド 100 ms・リリース 200 ms、キーは HPF 後の左右連動。Comp は連動・プログラム検出・ニー 6 dB・10／120 ms、メイクアップなし（Out がその役）。De-ess は「Freq より上の帯域の大きさ（−34 dBFS を超えた分の 0.8 倍）」で動く高域シェルフ（肩 0.7×Freq）を Amount まで下げる方式で、先読みなし・遅延 0。
- **Mic（進化機能）。** 種類を選ぶと Out 以外の 14 項目を `takeParamWrite` で書き込む（SA07 の Era と同じ方式。同じ回に値が来ればその項目の書き込みは取り消す）。Handheld は仕様書の既定値、Lavalier（Trim 14／HPF 100／High +3 …）、Headset、Podium（Trim 18／HPF 120 …）は設計値（表は `micPreset()`）。**Copy／Paste は画面側の仕事で未実装。** アダプターの書き込み上限は 1 ブロック 8 件から 32 件に上げた。
- テスト：LV02 は 9 件（表、持続するハウリングの検出と削り、Max depth、感度の違い、Release と FIXED、Ring out と Clear live、楽音の倍音を削らない、揺れる声で作動しない、保存と読込）、LV03 は 10 件（表、Trim／Ø、HPF、Gate、EQ、Comp 9 dB、De-ess の上限、Feedback guard の On／Off、Mic の書き込みと取り消し、不正入力）。

### LV05〜LV10 の設計（仕様書に数値がない部分）

- **LV05 Auto ducker。** キーは外部サイドチェーン（第 2 入力が自動で付く）。**仕様書の「SW Link のインスタンス選択」は SW Link が無いため未実装**で、キーが無ければ何も下げない。Depth／Attack（下げる速さ）／Hold（キーが止んでから）／Release（戻る時間）は dB 領域の 1 次ポールで動かす。Voice only On は共通部品 `sw/voice_detect.hpp`（VoiceDetector）の判定のときだけキーとみなす：約 16 kHz に落とした 20 ms フレーム（10 ms ごと）で、ノイズ床＋10 dB かつ −55 dBFS 以上、70〜400 Hz の周期性（正規化自己相関 0.5 以上）、ゼロ交差率 0.2 未満（拍手・ノック・一定のノイズは周期性が無いか高域寄りで外れる）を満たすフレームが直近 5 つのうち 2 つ以上。Voice only Off は「−50 dBFS かノイズ床＋10 dB のどちらか高い方」を超えたときで、仕様書にしきい値の項目が無いため設計値。Hold to duck は押している間強制的に下げる（自動化不可）。学習モデルは後期（区分 B の後半）で、いまは規則のみ。
- **LV06 Stream master。** 入力の 3 秒ラウドネス（BS.1770、K 特性）と Target の差へ、Ride speed（Slow 0.5／Medium 1.5／Fast 4 dB/秒）でゲインを動かす。上は Max boost、**下は −12 dB（仕様書に記述がないため設計値）**。起動後 1 秒と、400 ms ラウドネスが −50 LUFS 未満（無音）の間はゲインを動かさず、3 秒ラウドネスが −60 LUFS 未満で 2 秒続くと Dead air を表示する。後段は LV04 の Zero モードと同じ瞬時のピークリミッター（リリース 50 ms、左右連動）で、**保証するのはサンプルピークのみ**（仕様書の要確認どおり、先読みなしでは dBTP は保証できないので画面の「dBTP」は「dBFS」にするのが妥当）。Mono safe は 150 Hz 未満（LR4）をモノにし、左右の相関が負のときサイドを最大 12 dB 下げる（設計値）。Target は「Stream −14／Podcast −16／Broadcast −24／Custom」の選択で、Custom のときだけ別の Custom パラメータ（−30〜−5 LUFS）を使う。
- **LV07 Speech Agc。** 400 ms ラウドネスと Target の差へゲインを動かす（Use の係数 Speech 1／Panel 1.5／Lecture 0.6 × Speed の上げ／下げ速度 Slow 2／6、Medium 6／12、Fast 15／30 dB/秒）。下は −12 dB（設計値）。Gate 未満では測らず、Talker hold On でゲインを保ち、Off では 3 dB/秒で 0 dB へ戻す。Freeze はゲイン固定。出力に −1 dBFS の瞬時ピークリミッター。**進化機能（近い人・遠い人）：** 直近 3 秒の 20 ms フレームのレベルの広がり（90 % 点−10 % 点：近い人は間が空き、遠い人は残響で埋まる）と、大きいフレームでの高域（3 kHz 超）／中域（1 kHz 付近）の比から distance（0 近い〜1 遠い）を求め、遠いほど 3 kHz のシェルフを最大 +4 dB、Max gain を最大 3 dB 足す。**高域比の基準（−26 dB で 0、−38 dB で 1）は合成音声だけで合わせた値で、実際の声での校正が要る。**
- **LV08 Room Noise。** 256 点（平方根ハン窓、ホップ 64）の STFT で遅延 256 サンプル（仕様書どおり）。HVAC は帯域ごとのノイズ推定（約 14 フレームで平滑した電力の最小値追従、上へは 3 dB/秒、最小値の偏りを 1.5 倍で補う）とスペクトルゲイン G = 1 − a·N/P（a は Sensitivity で 1.0／1.6／2.4）を Reduction で頭打ちにし、ゲインは下がるのは 1 フレーム、上がるのは約 8 フレームで動かす。Voice guard は声があるとき 200 Hz〜4 kHz を Low −30／Mid −10／High −5 dB より深く削らない。Keyboard は 2 kHz 以上のエネルギーが 200 ms 平均より 12 dB 跳ね、声が無いときを打鍵とみなし、53 ms のあいだ 1.5 kHz 以上を Reduction だけ下げる。Learn noise は操作ボタンなのでメソッド（`learnNoise()`）で、次の 2 秒の平均パワーを雑音プロファイルとして使う（保存データには入れない）。
- **LV09 Hum Cut。** RS03 のコアそのもの（Base／Track）で、Harmonics は 1〜16 の個数どおり（RS03 に `setHarmonicCount()` を足した。RS03 のパラメータ自体は 2／4／8／16 のまま）、Depth は 0〜−40 dB を連続で（RS03 の 4 dB 刻みの Depth に換算）、Width は Narrow／Medium／Wide を Width 0／50／100 % に対応させ、Buzz は 0。Listen は「取り除いた成分（入力−出力）」で自動化不可。
- **LV10 Voice Fx。** VO02 の音程エンジンで、変換比は固定の 2^(Pitch/12)、Formant で母音だけ動かす。**遅延は仕様書の 128 サンプルではなく 1085 サンプル（22.6 ms、48 kHz）。** 仕様書の「要確認」にあるとおり、声に追従する音程変換は 1 周期より短くできないため実際の値を報告する。Preset は Pitch・Formant・Robot をまとめて `takeParamWrite` で書き込む（Low −5 st／−2、High +5／+2、Robot 0／0／On、Radio 0／0＋帯域制限、Anon −3／+2）。Robot On は声の高さに関わらず 120 Hz×2^(Pitch/12) に固定する（比は 0.5〜2 に制限）。Radio は 300 Hz ハイパス・3.4 kHz ローパス・ソフトクリップ。Anon は **フォルマントの ±0.7 st のゆらぎ（0.7 Hz の滑らかなランダム）と 6 段のオールパス（位相の分散）を足す。声を分かりにくくするだけで、匿名化は保証しない（仕様書の要確認どおり、取材用途の説明文でも保証しないと明記する）。** Monitor Off は声を加工せず素通しにする（本人の返しの切り替えで、ルーティングはホスト側／エンジン側）。Mix は共通の枠（遅延補正済みの原音）。
- テスト：LV05 7 件、LV06 6 件、LV07 6 件、LV08 8 件、LV09 7 件、LV10 8 件（表、各機能の数値、不正入力・端数ブロック・prepare 前）。

### LV11〜LV30 の設計（仕様書に数値がない部分・仕様との差）

- **共通部品 `sw/link.hpp`（SW Link の代用）。** LV11 の Duck others と LV15 の複数マイクは SW Link が前提だが、SW Link はまだ無い。そこで「同じ製品のインスタンスが同じプロセスにいる」場合だけ動く小さな登録簿（最大 8 つ、ロックフリー）を作った：各インスタンスが `prepare()` で番号を取り、数値（レベルなど）を公開し、1 ブロックごとに鼓動（beat）を進める。読む側は 0.5 秒鼓動が止まったインスタンスを「いない」とみなす（バイパス・停止中の残り値で誤動作しない）。**別プロセス・別製品の間は届かない**（SW AUDIO エンジンは全インスタンスを 1 プロセスで動かすのでそこでは成立する）。
- **LV11 Mic Switch。** Live は常時開、Push to talk は「Hold to cough」のボタンを押している間だけ開く（仕様書のボタンは 1 つなので兼用、設計）、Off は閉。Auto mute は 100 ms RMS が Silence を Hold 秒下回ると閉じ、Silence+3 dB で開く。開閉は Fade の直線。Duck others は、このマイクが開いていて Silence を超えている間 Duck amount を公開し、他のインスタンスが最も深い値で下がる（10 ms／300 ms）。
- **LV12 Geq 31。** ISO の 31 中心周波数（20 Hz〜20 kHz）、Q 4.3 の定 Q ベル。**左右別のゲインは仕様書の表（31 本）にないため、右チャンネル用の 31 本（「R 20 Hz」〜）を末尾に追加した**（Link L/R On のときは左の値が両方に効く。Edit は画面が書き込む先を選ぶ値でコアは読まない）。HPF（Off＋20〜200 Hz）／LPF（5〜20 kHz、最上段が Off）は 2 次。RTA overlay は 1/3 oct の測定値（`rtaDb()`）、Feedback guard は `sw/feedback.hpp` の検出だけを使い、鳴いている帯域の 31 ビットのマスク（`flaggedBands()`）を返すだけで削らない。Flat は 62 個のゼロを `takeParamWrite` で書き込む。Output は共通の枠。
- **LV13 Live Peq。** 6 バンド × （Type、Freq、Gain、Q）＋ HPF／LPF。Shelf は EQ07 と同じ決め（1 kHz 未満はローシェルフ、以上はハイシェルフ）。既定の周波数は 100 Hz〜10 kHz を等比で 6 つ（100／250／630／1.6 k／4 k／10 k）。進化機能は 1/3 oct の分析（1 秒平滑）で、両側 2 バンドの平均より 6 dB 以上突き出した帯域を最大 3 つ「半分の量（最大 −6 dB）、Q 4」の候補として返す（`suggestions()`。バンドへは書き込まない）。
- **LV14 Align。** 0〜500 ms の遅延線（Skew k=2、小数遅延は補間、変更は 50 ms で滑らかに動かす）、極性反転。**報告する遅延は 0**（仕様書どおり）。Distance は Delay×(331.5＋0.6×気温) m/s の表示用。Measure は第 2 入力（サイドチェーン）の基準信号と本線入力のマイクを 3 秒集め、FFT の相互相関で 0〜500 ms の遅れを求め（正規化 0.1 以上、2 番目のピークの 2 倍以上でないと失敗）、成功すると Delay の値を `takeParamWrite` で書き込む。
- **LV15 Auto Mixer。** 上の Link 代用で最大 8 マイク（マイク番号は prepare 順）。ゲインシェアは開いているマイクの重み（レベル、Priority のマイクは 3 倍）の比で、合計が 1 に保たれる。NOM limit は大きい順に N 本まで（Priority は常に含む）、残りは Off atten。Gate は開いたマイクを 0 dB、他を Off atten。Last mic hold は全員が −50 dBFS 未満のとき最後に話したマイクを開けておく（Off ならゲインシェアは 1/N、Gate は全員 Off atten）。Response は上げ／下げ時定数 Slow 100／400 ms、Medium 40／150 ms、Fast 15／60 ms。
- **LV18 Pop Guard。** 4 種を別々に判定する規則：Plug pop（30 Hz 未満の段差が 0.05 以上かつ 200 ms 平均の 3 倍）、Handling（2 ms のエネルギーが背景より 20 dB 以上、300 Hz 未満が 7 割、ピーク −20 dBFS 超）、Plosive（150 Hz 未満のバーストが背景より 18 dB 以上で高域が 1/10 未満）、Wind（50 Hz 未満が −38 dBFS 超で 150 ms 続く）。反応は Plug・Handling が Mute time だけミュート（3 ms で下げ 20 ms で戻す）、Plosive・Wind が 200 Hz のハイパスをかける。Sensitivity は閾値を ×1.6／×1／×0.6。**遅延 0 なので検出までの最初の 1〜2 ms は通る**（先読みは選べるようにしていない：仕様書の要確認）。検出回数は `caught()`。
- **LV19 Av Sync。** LV14 と同じ遅延線（0〜1000 ms、**報告遅延 0**）。Lock to video On でフレーム数に丸める（24／25／29.97／30／59.94 fps）。Clap sync は `markVideoClap(反応遅れ ms)` が映像で手が合った瞬間を伝え（OBS 版はフレーム時刻、VST3 版はボタン押下で反応遅れ約 150 ms を引く）、直近 2 秒の入力から鋭い過渡（中央値の 12 倍以上）を探して差を取る。音が先なら Delay を書き込み、音が遅いときは `audioLate()`（遅延線では直せないので映像側を遅らせる量 `lateMs()` を返す）。
- **LV20 Rta。** 16384 点 FFT（4096 ごと）、1000 Hz×2^(k/N) の中心（N＝3／6／12）で 20 Hz〜20 kHz、帯域のパワーは範囲内のビンの合計（1/12 oct の最低域は中心の線スペクトルを補間）。Weight は A／C を各帯域に加える。Speed は平滑 1 s／300 ms／100 ms、Peak hold は保持後に 20 dB/秒で下がる。Pink ref は 100 Hz〜10 kHz の平均に置いた水平線（ピンクノイズは各帯域のパワーが等しい）で、`deviationDb()` が部屋の偏り。音は素通し。
- **LV21 Test Gen。** Sine／Pink（Paul Kellet のフィルタ、RMS を Level に合わせる）／White／Sweep（対数 20 Hz→20 kHz を繰り返す）／Polarity（0.1 ms の正のパルスを 1 秒に 2 回、ピークが Level）。Arm と Output は 2 段階のメソッドで、読み込み後は必ず Off、0.5 秒で立ち上げ、60 秒で自動停止（仕様書の案どおり）、止めるときは 20 ms。出力中は信号を入力に置き換える。
- **LV22 Polarity。** 約 12 kHz に落として FFT 相互相関（Window の長さ、100 ms か Window/2 ごと）。サイドチェーンがあれば入力（マイク）と基準、無ければ L（テスト）と R（基準）。探索範囲は Mic vs mic ±5 ms、Speaker ±60 ms、Line ±1 ms。正規化した |r| が 0.3 以上で In／Out of phase、それ以外は Unknown。Hold result On は Unknown で前の結果を消さない。
- **LV23 Loudness。** MT01 のエンジンそのもの（Preset は ARIB／EBU／Stream）。**Tolerance（0.5〜3 LU、既定 1）を表に足した**（パラメータが 1 つだけだと clap-validator の param-set-events が「値が変わらない」で不合格になるため。目標との差が許容内かの `inBand()` に使う）。Dead air は 3 秒ラウドネスが −60 LUFS 未満で 2 秒続いたとき、No TP over は −1 dBTP 超が一度でも出たときの表示用フラグ。ログは音声スレッドが 1 秒ごとに 24 時間分のリング（8.6 万件）へ書き、`exportCsv()` が CSV にする（別スレッドを使わない形）。**CSV の項目と書式（elapsed, clock, momentary, short-term, integrated, range, true peak, dead air）は仮で、納品先の様式は未確認**（仕様書の要確認）。
- **LV24 Live Reverb。** 8 本の FDN（RV01 と同じ `sw/fdn.hpp`）、線の長さの基本セット（29.7〜97.1 ms）に Type 係数（Vocal hall 1.0／Room 0.45／Plate 0.6）、Tone は減衰（3.5／6.5／10 kHz）、RV01 と同じ単位エネルギー補正。Duck は声判定中に wet を 9 dB 下げる（80 ms／500 ms）。**「1% CPU」は実測前の数値のため載せない**（仕様書の要確認どおり）。
- **LV25 Live Delay。** Tap は 2 回以上のタップの間隔（直近 4 つの平均、3 秒を超える間隔は最初から）で Time を決め `takeParamWrite` で書き込む。BPM はホストテンポの 4 分音符。MIDI は `midiClockTick()`（24 PPQN の 24 拍分から求める）で、**アダプターに MIDI 入力が無いため普通のホストでは働かない**（エンジンが渡す想定）。バイパスは仕様書の「入力だけ止めて返りは鳴らし切る」を、表の末尾に足した「Input bypass」で実現した。
- **LV26 Mono。** M＝(L+R)/2、S＝(L−R)/2。Low mono は S の 2 次ハイパス。Auto phase fix は 250 Hz／1.5 kHz／6 kHz の 4 バンドで L・R の相関を 200 ms で追い、−0.2 未満の帯域の S を最大 12 dB 下げる（S へのシェルフ／ベルの縦続で、全部 0 dB なら S はビット単位で素通し）。Mono check は両チャンネルに M を出す（監視用・自動化不可）。
- **LV27 Scene Sync。** 音は素通し。対応表（OBS シーン名 → プリセット 1〜32）、Learn current、シーン変更ハンドラ、Fade between の進み具合までを実装した。**他のインスタンスへ切替を送る部分（SW Link）と obs-websocket クライアント（接続パスワードの保存方法は仕様書の要確認）は未実装。**
- **LV28 Remote Hub。** 音は素通し。**ネットワーク（HTTP／WebSocket サーバー、ポート 8640、LAN 内の暗号化）は作っていない。** 仕様書が要求する安全設計のコアだけを実装してテストした：LAN の私設アドレス以外は PIN を見る前に拒否、PIN は 6 桁（起動ごとに更新）、同一アドレスから 5 回間違えると 30 秒ロックして以降倍々（最大 15 分）、定時間比較、トークンは 128 ビット乱数で有効 8 時間・端末に紐づく、端末ごとに Control／View only（既定は View only）、Allow control Off と Lock all の間は操作を拒否。
- **LV29 Interp Mix。** 本線入力が会場音、サイドチェーンが通訳（**SW Link 経由は未実装**）。Output の 3 択、Floor under（通訳が話している間の会場音の下げ量）、Interp level、Crossfade（1 次ポールで時間の 1/3 を時定数に、時間で 95 %）。Auto detect On は LV05 と同じ VoiceDetector で通訳の声のときだけ下げ、Off は常に Floor under のまま。
- **LV30 Recorder。** 音は素通し。音声スレッドはロックフリーのリング（2^18 フレーム）に書くだけで、書き込みスレッドが WAV に書く。ヘッダー（サイズ）は 1 秒ごとに更新し、落ちても直前まで再生できる。4 GB を超えると RF64（ds64）。Split hourly は 3600 秒ごとに新ファイル。印は CSV に即時、ファイルを閉じるときに cue ポイントとラベル（LIST/adtl/labl）としても書く。**FLAC は書けず（選ぶと WAV で録り `flacFallback()`）、サンプルレート変換もしない（ホストのレートで書く。`rateFallback()`）。** Auto start On でも、録音フォルダが選ばれていなければ何も書かない（読み込みだけでファイルが増えないように）。空きが 200 MB を切ると止まる（古いファイルは消さない：仕様書の要確認）。
- テスト：LV11 7 件、LV12 9 件、LV13 7 件、LV14 8 件、LV15 10 件、LV18 8 件、LV19 7 件、LV20 10 件、LV21 10 件、LV22 7 件、LV23 6 件、LV24 8 件、LV25 8 件、LV26 7 件、LV27 7 件、LV28 9 件、LV29 7 件、LV30 11 件。

### GT03 Pedalboard の設計（仕様書に数値がない部分）

- **構成。** 8 スロット（信号の順）。スロットごとに Type（None／Comp／Drive／Fuzz／Chorus／Delay／Reverb）、On、ツマミ A・B・C（0〜10）。仕様書の要確認「各ペダルのノブが画面に無い」に対し、ノブは設計値（下表）。ペダルは他製品のコアを流用：Comp＝DY08、Drive・Fuzz＝SA06（3 バンド全部を同じ種類・Medium）、Chorus＝MD01（Mode I+II、Width 100 %）、Delay＝DL01（Analog、同期なし）、Reverb＝RV01（Plate、Size 60 %）。種類ごとに 2 台ぶんのコアを prepare で確保し（計 12 コア）、同じ種類の 3 台目は None 扱い。
- **ツマミ。** Comp：A Sustain（しきい値 −6−3.4A dB、比 2＋0.6A、アタック 10 ms、リリース 150 ms、ニー 6 dB、メイクアップは −18 dBFS での低減量の 0.35 倍）、B Level（5 を中心に ±12 dB）。Drive／Fuzz：A Drive（0〜24 dB）、B Tone（±6 dB）、C Level（±12 dB。**SA06 の出力ゲインは共通の枠側にありコアは持たないため、GT03 が自分で掛ける**）。Chorus：A Rate（0.1〜5 Hz 対数）、B Depth、C Mix（クロスフェード）。Delay：A Time（50〜1000 ms 対数）、B Feedback（0〜80 %）、C Mix（原音＋wet×C/10）。Reverb：A Decay（0.3〜8 s 対数）、B Tone（ダンピング 2〜16 kHz）、C Mix（原音＋wet×C/10）。
- **既定。** スロット 1 が Comp（On）、2〜6 が Drive・Fuzz・Chorus・Delay・Reverb（Off、ツマミ 5）、7・8 は空。読み込み直後は穏やかなコンプだけがかかる。Bypass all は完全バイパス（ペダルを動かさないので尾も止まり、戻すと続きから）。Input／Output は共通の枠。Noise gate は最下段が Off、それ以外は −80〜−20 dB のしきい値で 50 dB のゲート（アタック 1 ms・ホールド 60 ms・リリース 150 ms、Input の直後）。
- **チューナー（進化機能、表示のみ）。** `sw/pitch_tracker.hpp` を使い、Bypass all 中も動く。**PitchTracker に `firstPeak` モード（相関の最初の強いピーク＝最短の周期を選び、放物線で補間する）と作業レートの指定を足した**（既定は従来どおり。声の追従は変わらない）。GT03 は 12 kHz・firstPeak で使い、`tunerHz()`・`tunerNote()`（最寄りの MIDI ノート）・`tunerCents()`（±50）を返す。
- テスト（10 件）：表、素通しとバイパスのビット一致、Comp の Sustain、Drive／Fuzz の高調波とノブ、Chorus の変化と Mix 0、Delay の位置と Reverb の尾、順序の違いと 3 台目の無効、Noise gate、チューナー（A4＝69、110 Hz＋30 セント、バイパス中も）、不正入力。

### CS04 の注意

- **De-ess は分割せず「動くハイシェルフ」。** 最初は「原音 − 2次ハイパス」を低域側として残したが、2次ハイパスは位相が回るため、境目の少し上の成分がほとんど下がらなかった（境目 6 kHz・8 kHz の音で −12 dB 設定に対し −0.6 dB）。いまは同じフィルタの低域・帯域・高域の出力から、利得だけを動かすハイシェルフを作っている。抑えていないときは原音とビット単位で一致し、境目 4 kHz・10 kHz の音では −12 dB 設定で −11.2 dB、400 Hz は変化なし（実測）。ただし2次のシェルフなので、境目のすぐ上（0.4 オクターブ程度）では Range まで下がりきらない。
- 並び順の切り替えは、5 ms で音を下げ、入れ替え、5 ms で戻す（モジュールの内部状態を2組持たないため。ドラッグ操作でしか変わらない項目なので許容）。
- Windows（Wine 上）の検証は CS04 だけ未実施（Linux では両 validator とも不合格 0）。CI で確かめる。

### EQ02 の設計（仕様書に数値がない部分）

- **Zero latency の高域補正。** ベルの Q は、中心周波数の 1/16 から中心までの範囲でアナログ原型との差が最小になる値を数値で求める（設定が変わったときだけ計算して使い回す）。15 kHz・Q1・+12 dB のベルで、中心より下の誤差は 0.28 dB 以内（補正なしは 2.88 dB、RBJ の帯域幅換算式は 1.99 dB）。中心より上は、IIR ではナイキスト周波数で必ず 0 dB に戻るため原型に合わない（Natural・Linear で直す）。Notch は RBJ の帯域幅換算式。
- **Natural。** Zero latency のあとに、位相だけをアナログ原型に近づける 65 タップの FIR（遅延 32 サンプル）を足す。振幅は Zero latency と同じ（仕様書どおり）。12 kHz のベルで、原型との位相差は Zero latency の半分以下になる。
- **Linear。** EQ08 と同じエンジン（2048 タップ、遅延 1024＋128）。ダイナミックのバンドは、静的な部分を FIR に入れ、動く分だけを最小位相の IIR で後に足す。
- **ダイナミック。** 検出はバンドの帯域（Bell／Notch はバンドパス、シェルフはその側の LP／HP）のピーク包絡、アタック 5 ms・リリース 80 ms。しきい値から 12 dB 上で Range 全量に達する（いずれも設計値）。カットにはダイナミックを付けない。
- **Assist／Unmask（解析ボタン）** は画面と一緒に作る。

### EQ07 の解釈と設計値

- **Shelf／Cut の向き。** 仕様書に記述がないため、周波数が 1 kHz 未満なら低域側（ローシェルフ／ローカット）、以上なら高域側とした（確認事項）。Cut は Q に従う 12 dB/oct。
- **動作量。** しきい値を超えた量（6 dB ソフトニー）を Range の大きさで割り、1 で頭打ち。Range −12 なら、帯域がしきい値より 12 dB 上で −12 dB 全量（その間は比率 ∞ の圧縮と同じ）。Range が正なら同じ量だけ持ち上げる。Cut と Notch はダイナミックの対象外。
- **Spectral。** 平方根ハン窓・75 % 重なりで、何もしなければ遅延 1024 のまま元の音に戻る（誤差 1e-4 未満を確認）。ビンごとに、帯域内の平均レベルより 6 dB 上から効き始め、12 dB 上で全量（設計値）。ノイズに埋もれた 1.5 kHz の音で確かめると、通常モードは周りのノイズも 6 dB 以上下げるが、Spectral は音だけを下げる。
- **Auto thresh（5 秒の学習ボタン）** は画面と一緒に作る。

### EQ08 の注意（仕様書との照合で見つけた点）

- **カーネル長と低域の精度。** 仕様書の 2048 タップ（48 kHz）で作ると、低域の誤差が大きい。実測（目標の振幅との差）は次のとおり。

  | カーネル長（48 kHz） | 100 Hz・Q4 のベル | 30 Hz ローカット 24 dB/oct（20 Hz で） | 遅延 |
  | --- | --- | --- | --- |
  | 2048（仕様書） | −1.41 dB | +4.11 dB | 21.3 ms |
  | 4096 | −0.40 dB | +0.75 dB | 42.7 ms |
  | 8192 | −0.04 dB | −0.02 dB | 85.3 ms |

  今は仕様書どおり 2048。`kernelLengthFor()` の1か所で変えられる。長さを選べるようにする（例：Low／High／Max）かどうかは要決定。
- **仕様書内の食い違い。** 「カーネル長は 21.3 ms 分」と「48 kHz で 2048 タップ」は一致しない（2048 タップは 42.7 ms。21.3 ms はその半分＝遅延）。「2048 タップ、遅延 21.3 ms」と解釈した。
- **Pre-ring guard のしきい値。** 「主ピーク比 −60 dB（案）」は仕様書どおり。ただし主ピークの直前まで数えると、ふつうのベルでもほぼ全部が引っかかって直線位相の意味がなくなる。そこで、主ピークより 3 ms 以上前の前鳴りだけを数えることにした（設計値）。急峻なローカットや低域の鋭いベルが対象になる。
- **カット。** EQ08 には Slope の設定がないため、Lo cut／Hi cut は Q に従う 12 dB/oct とした。
- **Mid/side。** EQ01 と同じく「Mid だけに EQ、Side は同じ遅延だけ遅らせて素通し」と解釈した（バンドごとの配置がないため。確認事項）。
- **カーネルの再計算。** 仕様書は別スレッドだが、今は音声スレッドで 20 ms に1回まで（1回あたり数ミリ秒以下）。別スレッド化は今後の作業。

### clap-validator の「極小値で遅い」警告について

clap-validator の process-audio-denormals が、処理の軽い製品（DY04・DY07・DY08・EQ07・EQ09・LV16・LV17・MS07 など、回によって出たり出なかったりする）で「極小値の処理が約2倍遅い」と警告することがある（DY07 は3回中2回、1.8〜2.4倍で揺れる）。この項目は、検証ツール側で入力バッファを乱数で埋める時間まで計っている。そのため処理が軽い製品や入力端子が2つある製品ほど、ツール側の時間の比重が大きくなる。

ビルド済みプラグインを自作ホストで計った、極小値入力と通常入力の処理時間の比は次のとおり。DY04 1.01、DY07 0.92、DY08 0.49、EQ07 0.79、EQ09 1.10、LV16 1.19、LV17 0.72（EQ05 0.62）。

途中で、ゲートが閉じているときに毎サンプルの倍率計算が遅い経路に入る点を見つけて直した（DY04 は 1.19 → 1.01）。Wine 上の Windows 版でも、v0.5.0 の検証で DY04 に同じ警告が1回出た。

### MS02 の保証範囲

20 kHz 以下の成分では、厳密な帯域制限補間で測った真のピークが天井＋0.15 dB 以内に収まることを確認した（ノイズ 24 シードで最悪 −0.892 dBTP ＝ 天井 −1.00 に対し +0.108 dB。以前は「+0.1 dB 以内」と書いていたが、Windows の CI で別のノイズが −0.883 になり、シード違いで 0.1 を超えると分かったため改めた）。20 kHz を超えナイキスト周波数（48 kHz なら 24 kHz）近くまで強い成分がある素材を深くリミッティングすると、厳密補間では最大で約 +0.4 dB 超えうる。この帯域は放送規格の計測器（ITU-R BS.1770）でも低めに読まれる。

## 画面（UI）

- **方式。** 画面は Web（HTML／CSS／JS）で、各 OS の Web ビューにプラグインの窓として載せる：macOS＝WKWebView、Windows＝WebView2（Edge。Windows 10／11 に入っている WebView2 ランタイムが要る）。Linux は窓なし（ホストの汎用パラメータ画面）。窓は CLAP の gui 拡張で、VST3・AU には clap-wrapper が引き継ぐ。デザイン（`docs/design/canvas/*.dc.html`）が HTML なので素材と方式を揃えられる。
- **共通ランタイム `ui/sw-ui.js`。** 製品のパラメータ表から画面を自動で組み立てる：連続値は弧ノブ（ドラッグ・ホイール・ダブルクリックで既定値・値のクリックで数値入力）、段階値は 2 択ならボタン、6 択まではボタン列、それ以上はメニュー、Hold to… はおしている間だけ効くボタン、Band の並びはフェーダー。名前の前半が同じものは区画にまとめる（EQ05 の HF／HMF …、GT03 の Pedal 1〜8）。ツールバー（型番バッジ・名前・LIVE の遅延・A／B・Undo／Redo・Auto gain・Delta）、EVO バー、CPU の実測値（1 ブロックの処理時間÷長さ）を出す。色はカテゴリ色。曲線と値の書式は `sw::ParamSpec` と同じで、`tests/ui/curves.test.js` が 8055 点で C++ の値と一致を確かめる（`node tests/ui/curves.test.js`）。
- **プラグインとの通信。** 画面は `ui/host.js` の `bridge`（値・set・begin／end・call・onChange・info）だけを通す。ネイティブ側へはテキスト 1 行（`s <番号> <値>`、`b`／`e`＝ジェスチャの開始と終了、`c`＝ボタン、`p`＝50 ms ごとの問い合わせ）を投げ、答えは `SWHOST.update([...値], 遅延ms, CPU)`。処理は `plugin/clap/gui_bridge.hpp`（プラットフォームに依存しない：ページ組み立て・メッセージ解釈・応答。`tests/test_gui_bridge.cpp`）。画面の操作は待ち行列を通って音声スレッドでホストのパラメータイベント（begin／value／end）になるので、オートメーションに記録できる。素材は `tools/embed_ui.py` が 1 つのヘッダーにして埋め込む。
- **確認したこと／していないこと。** 全 132 製品の画面を Chromium（ヘッドレス）で描画して、実行時エラーが無く、ドラッグ・ダブルクリック・数値入力・ボタン・Undo・A/B が効くことを確かめた。**macOS と Windows の実機（ホストの中）では確かめていない**：Actions のビルドと validator が通ることだけ。
- **まだ無いもの。** 製品ごとの専用表示（EQ カーブ・メーター・スペクトル・スコープなど。コアから値を渡す口も要る）、コアのメソッドを呼ぶボタン（Tap、Learn、Ring out など）、Blender 描画の素材とフォントの同梱（フォントは端末のもの）、Linux の窓、拡大縮小。`docs/tasks.md` の「画面（UI）」。

## 全製品共通の部品

| 部品 | ファイル | 中身 |
| --- | --- | --- |
| 共通の処理枠 | `core/include/sw/shell.hpp` | 仕様書の順番で、製品の処理を In・Auto gain・Output・Δ で包む。原音側は製品の遅延に合わせてずらす |
| ラウドネス計測 | `core/include/sw/loudness.hpp` | ITU-R BS.1770 の K 特性、400 ms（Momentary）と 3 秒（Short-term）。Auto gain と、今後の計測系（MT01・LV23・MS01）で使う |
| モーフ | `core/include/sw/morph.hpp` | A／B の補間規則（周波数は対数、dB は直線、段階式は 0.5 で切替）。画面の A／B 操作と一緒にプラグインへ組み込む |
| プラグイン層 | `plugin/clap/clap_adapter.hpp` | 製品のパラメータ表と DSP を渡すだけで CLAP になる共通の型。ホストのパラメータ番号は製品分のあとに共通分（Auto gain、Delta）を足す |
| ダイナミクス | `core/include/sw/dynamics.hpp` | 圧縮カーブ（ニー付き）、アタック/リリース、ピーク/RMS/Program 検出、トゥルーピーク検出、先読みブリックウォール・リミッター |
| 帯域フィルタ | `core/include/sw/bandfilter.hpp` | 1バンド分の SVF（Bell／シェルフ／Notch／6〜96 dB/oct のカット）、高域の形崩れ補正 |
| FFT・FIR 設計・畳み込み | `fft.hpp`・`fir_design.hpp`・`convolver.hpp` | 基数2の FFT、アナログ原型の振幅から直線位相／最小位相（ケプストラム法）の FIR、一様分割 overlap-save 畳み込み（新旧カーネルのクロスフェード付き）。EQ02 の Linear でも使う |
| 出力段 | `core/include/sw/drive.hpp` | Drive 0〜10（入力 0〜+18 dB）、非対称ソフトクリップ1段、2× OS、音量補正つき、小信号で利得1。EQ01・EQ03・EQ04 で共通（下の「Drive 段の設計」） |
| ゲート | `core/include/sw/gate.hpp` | ゲート／エキスパンダー／ダッカー、ピーク包絡、4 dB ヒステリシス、ホールド、dB 上のアタック/リリース |
| FIR オーバーサンプラー | `core/include/sw/oversample_fir.hpp` | 直線位相 4x/8x/16x、遅延は整数（48 サンプル）、20 kHz まで平坦、像 −80 dB 以下 |
| 外部サイドチェーン | `shell.hpp`・`clap_adapter.hpp` | 製品が processWithSidechain を持つと、プラグインに2つ目の入力端子「Sidechain」が付き、共通枠が信号を渡す |

### 製品の足し方

1. `products/<code>/<code>.hpp/.cpp` に DSP とパラメータ表（仕様書の順）を書き、`tests/test_<code>.cpp` にテストを書く。
2. `plugin/clap/<code>_clap.cpp` に数行の定義（名前・分類・Output/In/Mix の番号）を書き、最後に `SW_CLAP_ENTRY(<code>, 型名)`。
3. `CMakeLists.txt` の `SW_PRODUCTS` に加え、`sw_add_plugin(<code> "<表示名>" <AU の4文字>)` を1行足す。

- Auto gain：原音と処理後の 3 秒ラウドネスを比べて補正。追従 2 秒、±18 dB まで、−60 LUFS 以下の無音では補正を保持。既定は Off。
- Δ：「Auto gain 補正後の処理音 − 原音」に Output をかけて出す。製品の遅延に合わせて揃えるので、遅延だけの処理ならちょうど 0 になる。

## 検証結果（v0.13.0、2026-10-07、132本）

| 項目 | Linux x86_64（このクラウド環境） | Windows x64・macOS（GitHub Actions） |
| --- | --- | --- |
| 単体テスト（1319件） | 全合格（AddressSanitizer・UBSan 付きでも全合格） | ビルド後に実行（Windows MSVC・macOS ユニバーサル）。**全ジョブが成功した実行：run 69（commit 08a7228、LV30 と GT03 まで全 132 本）** |
| CLAP：clap-validator 0.4.1 | 各 0不合格（33合格・11対象外。軽い製品で「極小値で遅い」の警告が出ると32合格：上の「clap-validator の「極小値で遅い」警告について」） | 同じ検証を各 OS で実行（run 69：成功） |
| VST3：Steinberg validator（SDK 3.8.0） | 各 47合格・0不合格 | 同じ検証を各 OS で実行（run 69：成功） |
| AU：auval | — | macOS で実行（run 69：成功） |

v0.11.0（23本）の時点では Windows を MinGW でクロスビルドして Wine 上で検証していた（CS04 以外の 22 本で不合格 0）。現在の Windows の根拠は上の GitHub Actions（MSVC）で、Wine での再検証はしていない。

`tools/validate_all.sh` で、ビルド・単体テスト・全プラグインの両検証を一括で実行できる（Linux）。

「対象外」は、まだ持っていない機能（画面、ノート入出力など）を調べる検査。Wine は本物の Windows ではないため、実際の DAW での確認が別途必要。

周波数特性の実測（Drive 0、−60 dBFS、48 kHz）は `docs/eq05_response.png`。

## ビルド

必要なもの：CMake 3.21 以上、C++17 コンパイラ、git、ネット接続（CLAP SDK・clap-wrapper・VST3 SDK を初回に自動取得）。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

できたプラグインは `build/plugins/`（Windows は `build/plugins/CLAP` と `build/plugins/VST3`）。

- macOS のユニバーサル版：`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` を構成時に付ける。
- Linux から Windows 版を作る：`-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DSW_BUILD_TESTS=OFF`（g++-mingw-w64-x86-64-posix が必要）。

## 自動ビルド（GitHub Actions）

`.github/workflows/build.yml` が、push のたびに Windows・macOS・Linux で ビルド → 単体テスト → CLAP 検証 → VST3 検証 →（macOS は AU 検証）まで行い、プラグインを成果物として保存する。**このワークフローはまだ一度も実行していない。** 初回は失敗箇所が出る前提で、直しながら通す。

## インストール先

| OS | CLAP | VST3 | AU |
| --- | --- | --- | --- |
| Windows | `C:\Program Files\Common Files\CLAP\` | `C:\Program Files\Common Files\VST3\` | — |
| macOS | `~/Library/Audio/Plug-Ins/CLAP/` | `~/Library/Audio/Plug-Ins/VST3/` | `~/Library/Audio/Plug-Ins/Components/` |
| Linux | `~/.clap/` | `~/.vst3/` | — |

VST3 は `SW EQ05 Console.vst3` フォルダごと置く。

## 署名について

ここで作るものは**署名なし**。macOS ではダウンロードしたプラグインが Gatekeeper に止められる。自分で試すだけなら `xattr -dr com.apple.quarantine <プラグインのパス>` で解除できる。配布するには Apple の Developer ID による署名と公証（notarization）が必要。

## フォルダ構成

```
core/include/sw/   共通部品：パラメータのカーブ、スムーザー、TPT SVF、2倍オーバーサンプラー、サチュレーター、表示書式、FTZ、
                   ラウドネス計測、共通の処理枠（Shell）、モーフ
products/eq05/     SW EQ05 Console の DSP とパラメータ表
plugin/clap/       共通のプラグイン層（clap_adapter.hpp）、EQ05 の定義（eq05_clap.cpp）と書き出し口（eq05_entry.cpp）
tests/             単体テスト（doctest）
tools/             周波数特性の測定ツール
cmake/             MinGW 用ツールチェーン
.github/workflows/ 自動ビルド
```

## まだやっていないこと

- 画面（キャンバスの HTML を WebView で流用する予定）
- 残り116製品
- DY04 の Learn（かぶりと打撃を学習してしきい値を決める EVO）
- DY08 の「Auto release が On の間は Release を Auto と表示する」（画面側で対応）
- モーフのプラグインへの組み込み（画面の A／B 操作と一緒に）、EVO バー（Low lat、オーバーサンプリング倍率の選択）、Unit A／B／C
- 64 bit 浮動小数での入出力（VST3 validator の情報表示より。32 bit で動作）

## 手早いテスト（開発中）

`./build_tests.sh` は CMake を使わずに単体テストだけを作る。`third_party/doctest.h`（doctest 2.4.11 の単一ヘッダー）を置いてから使う。正式な手順は上の CMake。

### 画面の項目を仕様に合わせた箇所（デザインキャンバスとのずれ）

画面は `docs/design/canvas` のデザインをそのまま使うが（`tools/gen_skins.py`）、デザインの項目名・個数が仕様書／04 と合わない箇所は、**仕様と DSP を変えず、画面側を仕様に合わせた**（`ui/skin_aliases.json` の `_edit`）。

| 製品 | デザインの項目 | 画面での扱い |
| --- | --- | --- |
| EQ01 | Low の Freq・Boost・Atten／High の Freq・Boost・Width／High atten の Freq・Atten／Level | 仕様どおり Low（Freq・Gain・Contour）・Air（Freq・Gain・Width）・Drive・Output の 8 つに組み替え。High atten の Freq は削除、Mode（LR/MS）はデザインにないため画面なし |
| EQ05 | Gain・Freq・Q が同名で 4 バンド分 | ページ上の目盛り（1.5k–16k／.6–7k／.2–2.5k／30–450）から HF・HMF・LMF・LF の順と判断して結び付け。HF Shape・LF Shape はデザインにないため画面なし |
| DY01 | Attack・Release | 仕様は Speed 1 つ（アタックとリリースを連動）と Bite のため、Speed・Bite に名前を替えて結び付け。**決定（依頼者の「おまかせ」）**：仕様どおり Speed 1 つ（アタック 800〜20 µs とリリース 1100〜50 ms を連動）とする。理由：DSP・テスト・保存済みの設定に触れず、仕様書の記述（Speed で連動）と一致するため。Attack／Release を別々に動かしたくなった場合は、パラメータを末尾に追加する形で後から足せる |
| DY10・MS03・ST01 | Crossover 1 つ | 仕様の Crossover 1〜3 に合わせて 3 つに複製 |
| MS06 | Mix（Comp の画面） | Comp mix に結び付け |
| DY02 Meter／GT03 Tuner／VO05 Music | パラメータにない表示・切り替え | **決定（依頼者の「おまかせ」）**：パラメータは足さず、表示用の部品（`data-static`）として残す。仕様にない設定を増やすと保存済みの設定と DSP に触れるため。中央の表示を音に連動させる段階で、DY02 はメーターの表示切り替え（GR／+4／+10）、GT03 はチューナー表示、VO05 は音楽の聞き取り状態の表示として結び付ける |
