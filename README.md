# SW AUDIO — プラグイン本体

SEVENTHWELL の SW AUDIO のプラグイン実装。仕様は「SW AUDIO 仕様書 v1.0 初稿」に従う。

## 資料の場所

- 仕様書の全文：`docs/spec/SW_AUDIO_spec_v1.0.md`／プロジェクト資料：`docs/project/`／画面デザイン：`docs/design/canvas/`（`project/<コード>.dc.html` が製品ごとの画面、`index.html` で全体）／デザインシステム：`docs/design/design-system/`
- 作業ルール（Claude Code 用）：`CLAUDE.md`／残りの作業：`docs/tasks.md`

## 方式

- **CLAP を正として作り、clap-wrapper で VST3（macOS は AU も）を生成する。** すべて MIT／Apache 2.0 で、売上上限や利用料はない（NOTICE.md）。
- **DSP 本体はフレームワークに依存しない C++17。** `core/`（共通部品）と `products/`（製品ごとの処理）に置き、プラグイン層（`plugin/`）からも、将来の OBS 用エンジンからも同じものを呼ぶ。
- ホストに見せる値は仕様書どおり、連続値は正規化 0〜1、段階式は段番号。パラメータの番号（ParamId の並び）は保存データの互換のため**一度公開したら並べ替えない**。

## 現在の製品（v0.16.0、132本）

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
| SW EQ05 Console | 4バンドのコンソールEQ、HPF/LPF、Drive | Match（参照の音色に合わせる）あり。下の表「EQ05『Match』」 |
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
| SW LO03 Low Focus | キックとベースの低域を分けてぶつかりを減らす。Role（Kick／Bass／Both）、Focus（30〜120 Hz、動的ベル）、Tight（0〜100 ％）、Mud cut（150〜500 Hz、静的ベル、深さは 6 dB × Tight）、Mono below（Off＋20〜300 Hz）。Bass はサイドチェーンのキックの立ち上がりで下げる。遅延 0 | 動的ベルの動き・深さは設計値（下の「LO03 の設計」）。画面の Tight 既定 60 ％は仕様書の 50 ％に合わせていない。SW Link：Bass 役は、同じホストにある Role＝Kick の LO03 の出力を鍵にする（サイドチェーンに信号があればそちらが先。下の「LO03 の設計」）。Δ ボタンが無い |
| SW GT01 Amp | ギターアンプのヘッド（キャビネットなし）。Channel（Clean／Crunch／Lead）、Gain、Bass／Middle／Treble（受動トーンスタックの伝達関数）、Presence、Master、Bright、三極管 2 段＋サグつき出力段（4× OS）。進化機能：Volume match（最初の 5 秒で弾く大きさを測り、ギター側を絞るときれいになる位置を合わせる、既定 Off） | 歪みの量・サグ・チャンネルの音量は設計値、トーンスタックの式の符号を 1 つ直した（下の「GT01 の設計」）。キャビネットなし（Cab On/Off を足すかは未決）。Δ ボタンが無い |
| SW GT02 Cab Ir | キャビネットとマイクの畳み込み（遅延 0）。Cab（1x12／2x12／4x12）、Mic（Dynamic／Ribbon／Condenser）、Mic distance（近接効果）、Off axis、Room、Low cut（Off＋20〜300 Hz）。録音した IR の代わりに、パラメータから作る最小位相のモデル IR | IR は録音ではなくモデル（仕様書の「IR は自社収録が必要」は未解決のまま。収録した IR が入ったら差し替える）。音の形は設計値（下の「GT02 の設計」）。Δ ボタンが無い |
| SW GT03 Pedalboard | 最大 8 台のペダルボード（Comp・Drive・Fuzz・Chorus・Delay・Reverb を他製品のコアで流用、各 3 ツマミ）、Noise gate、Bypass all、常時動作のチューナー。遅延 0 | ノブは設計値（画面にない）。設計は README「GT03 Pedalboard の設計」。並べ替え操作は画面側 |
| SW GT04 Bass Amp | ベースアンプ（DI との混ぜつき）。Gain／Drive／Master、Low／Lo mid／Hi mid／High（ノブ 5 で平ら）、Mid Hz、DI（Off／On）、DI blend（画面に無い、追加）。Drive は高域だけ歪ませ低域は残す。進化機能：Phase align（アンプ側の位相を測って DI 側に全域通過と遅延を入れる、既定 On） | Mid Hz の読み方・EQ の周波数・Drive・簡易キャビネット・DI 側の 35 Hz ハイパスは設計値（下の「GT04 の設計」）。位相は 150 Hz で一致（他の周波数では少しずれる）。DI blend ノブは画面に無い |
| SW GT05 Reamp | DI 録りの音を、ピックアップ・ケーブル・受け側インピーダンスから決まる回路の伝達関数（2 次ローパス、2〜7 kHz の山）に通す。Level、Impedance（10k／47k／100k／1M）、Cable（100〜1000 pF）、Pickup（Single〜Hum）、Output。進化機能：Pickup swap（DI の元の共振を推定して打ち消し、選んだ共振を付け直す、既定 Off） | 回路の定数・Pickup swap の推定方法は設計値（下の「GT05 の設計」）。既定でも約 +15 dB の山が 5 kHz にある（回路どおり）。画面の Lift トグルは入れていない（未決）。Δ ボタンが無い |
| SW RV01 Hall | アルゴリズムリバーブ（Hall／Room／Chamber／Plate／Ambience）。16 本の遅延線のフィードバック網（FDN）＋初期反射のタップ遅延＋拡散の全域通過。Pre-delay、Size、Decay（0.2〜20 s）、Diffusion、Damping、Low cut／High cut、ER / late、Width、Mix、Freeze、Mono low end、Duck（原音が大きい間は残響を下げる、画面に無い追加）。残響の遅延は 0 と表示 | FDN の部品（core/include/sw/fdn.hpp）・アルゴリズムごとの長さ・初期反射・Duck の動きは設計値（下の「RV01 の設計」）。Duck 量のノブが画面に無い。Δ ボタンが無い |
| SW RV02 Plate | プレートリバーブ。分散を持つ全域通過 48 段（高域が先に届く）＋FDN。Decay（0.5〜6 s）、Pre-delay（ms、または Sync でテンポの音符長）、Damping（Dark〜Bright）、Low cut、Width（Mono〜Wide）、Mix、Mono in。進化機能：Duck（−6 dB 固定、既定 Off） | 分散の段数・FDN の長さ・Damping の周波数は設計値（下の「RV02 の設計」）。Sync と Duck の操作は仕様書の表にない追加（rv02.sync、rv02.evo.on）。RV01 の音量正規化を線の長さの平均で割る式に直した（Fdn::meanLengthSeconds） |
| SW RV03 Spring | スプリングリバーブ。バネごとに「引き伸ばした全域通過」28 段の分散（さえずり）を持つ帰還の輪。Springs（1／2／3）、Dwell（入力の強さ、飽和つき）、Tone、Tension（さえずりの音程）、Drip（立ち上がりにだけ反応）、Mix | 分散の段数・輪の時間・Drip の励起・出力の係数は設計値（下の「RV03 の設計」）。専用の進化スイッチは無い（Drip が立ち上がり検出を内蔵）。Δ ボタンが無い |
| SW RV04 Convolution | 畳み込みリバーブ。Category（Halls／Rooms／Churches／Gear／Custom）、Pre-delay、Length、Size（IR の伸縮）、Low cut／High cut、Reverse、Mix。録音した IR の代わりに合成した IR、Custom は読み込んだ IR（画面の「Load IR」から読む。プロジェクトに保存）。先頭直接＋不均一分割 FFT で遅延 0。進化機能：Bar fit（長さを拍数に切り下げ、既定 Off） | IR は録音ではなく合成（仕様書の「IR は自社収録」は未解決。Gear は実機名を使わない汎用の音）。Load IR の操作は画面側の作業。CPU は 256 サンプルのバッファでは足りないことがある（下の「RV04 の設計」）。Δ ボタンが無い |
| SW RV05 Chamber | エコーチェンバー（残響室）。Room（Small／Medium／Large、直方体）、Decay（0〜10 ＝ 0.4〜4 s）、Mic distance（Near〜Far）、Speaker tilt、Tone、Mix。鏡像法の初期反射 25 個（部屋と位置から計算）＋ FDN の後部（臨界距離で大きさを決める）。進化機能：マイク距離で直接音と残響の比・初期反射・高域の減りが連続して変わる | 部屋の寸法・位置・壁の反射率・後部の大きさの決め方は設計値（下の「RV05 の設計」）。Δ ボタンが無い |
| SW RV06 Shimmer | 音程を上げた残響が重なるシマー。16 本の FDN の線のうち 8 本の帰還の中に、2 粒の重ね合わせの音程変換（Octave＝2 倍／Fifth＝1.5 倍／Both）を入れる。Decay（1〜60 s）、Shimmer、Interval、Mix、Freeze（EVO、保持したパッドは昇らない）、Duck（既定 On） | 音程変換を線ごとの帰還の中に入れた理由（外側のループは発散した）・窓や割合は設計値（下の「RV06 の設計」）。Pitch「+12 st」は Interval の選択と解釈。Δ ボタンが無い |
| SW RV07 Early | 初期反射だけのリバーブ。Use／Distance／Angle／Room size／Wall。直接音は遅らせない | 仕様書どおり Mix なし。設計値は README「RV07 の設計」。Δ ボタンなし |
| SW RV08 Gated | ゲートリバーブ。Size／Gate time／Threshold／Shape／Tone／Mix、Snare key（検出をスネア帯域に絞る） | 設計値は README「RV08 Gated の設計」。被り学習（Learn）は画面のボタンから（README「RV08 Gated の設計」）。Δ ボタンの有無は画面で確認 |
| SW DL01 Echo | 3 種の音色（Tape／Analog／Digital）のエコー。Time（Sync で音符長）、Feedback 0〜110 %、ループ内 HPF／LPF、Depth／Rate、Ping-pong、Duck、Mix | 設計値は README「DL01 Echo の設計」。Sync は Time のつまみ位置を 18 の音符長に割り当て（既定 375 ms 位置は 1/4 付点）。画面パネル表記の確認（Hybrid echo processor／Tape delay）と Δ ボタンは未決 |
| SW DL02 Tape Echo | 3 ヘッドのテープエコー。Heads、Rate（Slow〜Fast）、Intensity 0〜110 %、Bass／Treble、Wear（高域劣化・ワウ・ドロップアウト）、Mix | 設計値は README「DL02 Tape Echo の設計」。ループ内の飽和は SA01 の履歴モデルではなく tanh に簡略化。Heads 切替は 10 ms、再生中は小節線まで待つ（プラグイン層に setTransport を追加）。Δ ボタンの有無は画面で確認 |
| SW DL03 Bbd | バケツリレー素子（BBD）のアナログディレイ。Time（クロックと帯域が連動）、Feedback、Mod depth／rate、Grit、Mix、Sync | 設計値は README「DL03 Bbd の設計」。Sync は実テンポでの最近傍の音符（既定 Off）。Δ ボタンの有無は画面で確認 |
| SW DL04 Multitap | 6 タップのディレイ。タップごとに On／Time／Level／Pan／Filter、Feedback、Mix、Sync、Ping-pong | 設計値は README「DL04 Multitap の設計」。Sync は Time を 120 bpm の ms と読み最近傍の音符に寄せる。Δ ボタンは無い（仕様書の要確認）。clap-validator の denormals 警告が時々出る（既知の偏り） |
| SW DL05 Reverse | 逆再生・順再生・ランダムの粒ディレイ。Mode、Time（音符 1/16〜2 小節）、Grain size、Spray、Pitch +12、Freeze、Mix。拍位置があれば区間境界を小節線にそろえる | 設計値は README「DL05 Reverse の設計」。Δ ボタンは無い（仕様書の要確認） |
| SW MD01 Chorus | BBD 風のコーラス。Mode（I／II／I+II）、Rate、Depth、Width（左右の LFO 位相 0〜180°）、Tone、Mix。Wide ではモノの和で揺れが打ち消し合う | 設計値は README「MD01 Chorus の設計」。Δ ボタンの有無は画面で確認 |
| SW MD02 Flanger | フランジャー。Rate（Sync で音符長）、Depth、Feedback ±100 %、Manual、Through zero（報告遅延 480／0）、Sync、Mix | 設計値は README「MD02 Flanger の設計」。Sync 時の LFO 位相は小節線にそろえる。Δ ボタンは無い（仕様書の要確認） |
| SW MD03 Phaser | 全域通過を 4／6／8／12 段重ねたフェイザー。Rate（Sync で音符長）、Depth、Feedback、Center、Mix、Note follow（音高に Center が追従） | 設計値は README「MD03 Phaser の設計」。音高検出は DY05 から core/include/sw/pitch_tracker.hpp に移して共有。Δ ボタンの有無は画面で確認 |
| SW MD04 Tremolo Pan | トレモロ／オートパン／ハーモニック（800 Hz で上下を逆位相）。Rate（Sync で音符長）、Depth、Shape（Sine／Triangle／Square／Ramp）、Width | 設計値は README「MD04 Tremolo Pan の設計」。Mix は無い（仕様書どおり）。Δ ボタンは無い（仕様書の要確認） |
| SW MD05 Rotary | 回転スピーカー。Speed（Stop／Slow／Fast、ホーンとドラムは別の慣性）、Accel、Horn／Drum、Mic distance、Drive、Mix。ドップラー・音量変化・キャビネット共振、遅延 48 サンプル固定 | 設計値は README「MD05 Rotary の設計」。MIDI／フットスイッチ（CC64・CC1・Note）での Speed 切替は実装済み（画面の表の「MIDI 入力」の行） |
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
| SW VO03 Harmony | ハーモニー 4 声（Scale／Fixed／MIDI は Scale 同等）、Interval ±7 度、Level・Pan・Formant・Humanize・Delay。遅延 1450 サンプル | 設計値は README「VO03 Harmony の設計」。遅延は仕様書の見積もり 512 サンプルとは違う。MIDI（保持中の和音への追従）は実装済み（画面の表の「MIDI 入力」の行）。Key／Scale は末尾に追加 |
| SW VO04 Doubler | 声部 1／2／4／8、Spread・Timing（遅延 0.5〜30 ms の不規則なゆっくりした動き）・Pitch var・Tone・Mix。遅延 0 | 設計値は README「VO04 Doubler の設計」。フレーズの頭は休止中に遅延を最小に戻し、以後は変化率 0.5 % 以内で戻る |
| SW VO05 Rider | 音楽を外部サイドチェーンで聴いて、ボーカルを Target（音楽に対する相対値）へライド。Range・Sensitivity・Breath skip・Ride・Write automation。遅延 0 | 設計値は README「VO05 Rider の設計」。音楽の取り方（Music from）は末尾に足したパラメータ（サイドチェーン／ほかの SW AUDIO 全部／製品指定。README「SW Link の残り」）。音楽を 1 秒聴くまで動かない |
| SW VO06 Formant | Pitch ±12・Formant ±5（母音だけ動く）・Character 4 種・Keep timing・Smooth・Mix。遅延 1450 サンプル | 設計値は README「VO06 Formant の設計」。遅延は仕様書の見積もり 512 とは違う。Keep timing Off は声色が音程に追従する設計（タイミングは常に保つ） |
| SW VO07 Vocal Strip | HPF→De-ess→Breath→Body/Presence/Air→Comp→Level→Plate/Echo 送り→Output の順の声のストリップ。遅延 0 | 設計値は README「VO07 Vocal Strip の設計」。DY05・DY02・RV02・DL01 のコアを内部で使用。段間の音量合わせはコンプの自動メイクアップのみ |
| SW VO08 Breath | Reduce／Remove／Mark only、Reduction・Sensitivity・Keep・Fade。規則による息の検出。遅延 1024 サンプル | 設計値は README「VO08 Breath の設計」。学習モデルとフレーズごとの選択は未実装 |
| SW RS01 Denoise | 短時間 FFT（2048）のスペクトル抑圧。Profile・Adaptive（最小値統計）・Reduction・Threshold・Smoothing・Low/High band・Artifact guard・Learn。遅延 2048（Low lat で 512） | 設計値は README「RS01 Denoise の設計」。共通部品 sw/stft.hpp。定常な純音は雑音として覚える。Low lat は未実装 |
| SW RS03 Dehum | 基本波と倍音のノッチ列（Base 50/60/Auto、Harmonics、Depth、Width、Buzz）。Track で ±2 Hz を追従。遅延 0 | 設計値は README「RS03 Dehum の設計」。Width は Q 60〜8、Buzz は設計値 |
| SW RS04 Declick | AR モデルの励起で検出して補間（Click／Crackle／Both、Sensitivity、Click width、Crackle %、Low guard）。遅延 512。共通部品 sw/ar_repair.hpp | 設計値は README「RS04 Declick の設計」。2 ms 級のクリックは直りにくい。Repair ボタンの役割は未定 |
| SW RS05 Declip | クリップした山の AR 補間（Threshold・Quality・Makeup・Smooth・Detect）。窓内の見積もりを挟んで高次数でも悪化しない。遅延 1024 | 設計値は README「RS05 Declip の設計」。極端に切れた信号（60 % 近く）は改善 2〜5 dB |
| SW RS06 Dereverb | 後部残響の統計的抑制（STFT 1024・Polack 型）。Reduction・Tail length・Early・Smooth・Learn room（Tail へ書き込み）。遅延 1024 | 設計値は README「RS06 Dereverb の設計」。実際の声では残響時間の測定が難しい（無音の隙間が要る） |
| SW RS07 Mouth Noise | RS04 の検出に「語と語の間か」の重み。Sensitivity・Click size・Freq skew・Fade。遅延 512 | 設計値は README「RS07 Mouth Noise の設計」。Click size Large は約 1 ms まで（先読み 512 の制約） |
| SW CR01 Filter | 2 極の ZDF 状態変数フィルタ（LP/BP/HP/Notch、2×OS）。Envelope/LFO/Sidechain で変調。直近 10 秒の範囲に正規化する進化機能。遅延 0 | 設計値は README「CR01 Filter の設計」。4 極は未実装。LFO は 1 小節 1 周期（設計値）。進化機能の On/Off は末尾に追加した cr01.evo.on |
| SW CR02 Stutter | 拍に同期した直前スライスの繰り返し（Grid・Gate・Repeat・Pitch・Reverse・Filter・Mix・16 ステップのパターン）。Randomize は拍位置と入力の立ち上がりで重み付け。遅延 0 | 設計値は README「CR02 Stutter の設計」。4/4 を仮定。Randomize・Clear は UI ボタン用のメソッド（パラメータではない） |
| SW CR03 Granular | 入力の過去から切り出した粒（Cloud/Scatter/Glitch、Grain・Density・Spray・Pitch・Spread・Mix・Freeze input）。Harmony は直近 4 秒の 12 音分布から粒の音程を構成音へ。遅延 0 | 設計値は README「CR03 Granular の設計」。Harmony の On/Off は末尾ではなく表の最後の cr03.evo.on。和音の入力には向かない |
| SW CR04 Freeze | スペクトルを固めて鳴らし続ける（Trigger Hold/Momentary/Auto、Freeze、Blur、Drift、Mix）。ピーク位相ロックで取り込んだ音程とレベルを保つ。原音は遅らせない（遅延 0） | 設計値は README「CR04 Freeze の設計」。MIDI ノートでの取り込みは実装済み（画面の表の「MIDI 入力」の行）。Mix は製品内で処理 |
| SW CR05 Tape Stop | テープが止まる／立ち上がる／逆回転する（Action・Stop/Start time・Curve・Filter・Trigger）。ホストの小節線がわかれば停止が小節線で終わるよう逆算して開始。遅延 0 | 設計値は README「CR05 Tape Stop の設計」。4/4 を仮定。Start の終わりは 30 ms のクロスフェード |
| SW CR06 One Knob | 6 つの効果（Wide/Warm/Air/Punch/Space/Lo-fi）を 1 つの Amount で動かす。Amount 0 は原音そのもの。Macro は内部値を画面へ。遅延 0 | 設計値は README「CR06 One Knob の設計」。内部チェーンは既存製品の簡略版（Space だけ RV02 のコア）で、曲線は設計値 |
| SW MT01 Loudness | ラウドネスメーター（Momentary/Short-term/Integrated/LRA/True peak、10 分の推移、ARIB/EBU/配信のプリセット）。音は変えない。遅延 0 | 設計値は README「MT01 Loudness の設計」。±1 LU が規格上の許容値かは要確認のまま。アダプターに kDelta=false を追加 |
| SW MT02 Spectrum | スペクトラムアナライザ（FFT 4k〜32k、Speed、Range、Slope、Smoothing、Display、参照との比較）。音は変えない。遅延 0 | 設計値は README「MT02 Spectrum の設計」。ジャンル別の型は未実装 |
| SW MT03 Spectrogram | スクロールするスペクトログラム（Scale Linear/Log/Mel、Scroll、Floor ほか）。帯域のその場試聴（出力が変わる）。遅延 0 | 設計値は README「MT03 Spectrogram の設計」。試聴中は previewActive() で警告 |
| SW MT04 Phase Scope | 位相スコープと相関メーター（全帯域＋8 帯域、負相関 0.7 秒で警告、Persistence・Zoom）。音は変えない。遅延 0 | 設計値は README「MT04 Phase Scope の設計」 |
| SW MT05 Vu Ppm | VU/PPM メーター（Ref −14/−18/−20、VU は 300 ms で 99 %、PPM は IEC Type II 基準の設計値）。遅延 0 | 設計値は README「MT05 Vu Ppm の設計」。0 VU の基準は SW Link で MT05 同士が共有する |
| SW UT01 Gain | ゲイン・バランス・幅・極性・Swap・Mono（Gain は 10 ms で滑らかに、既定値は素通し）。トラックの種類ごとの Gain を覚える。遅延 0 | 設計は README「UT01〜UT03 の設計」。ホストのトラック名は CLAP track-info（VST3 は clap-wrapper の IInfoListener 経由）で受け取る（画面の表の「UT01 の EVO の 1 行」の行）。トラック名の受け取りは、その仕組みを持つホストだけで働く |
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
| SW DY10 Multiband 4 | 4 帯域マルチバンドコンプ。4 次 LR で 3 か所のクロスオーバー（240 Hz／2 kHz／8 kHz、足すと平ら）、帯域ごとにしきい値・レシオ・アタック・リリース・Range・Gain・Solo・Bypass。クロスオーバー同士は 1 オクターブ以上離す（押し返す） | 帯域の ID は dy10.b1〜b4、クロスオーバーは dy10.x1〜x3。Auto（クロスオーバー解析）は画面の Auto ボタンから（下の「DY10 の設計」） |
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
| SW LV25 Live Delay | タップテンポのディレイ（Tap／MIDI／BPM、Feedback、Tone）。入力だけ止めるバイパスで返りは鳴らし切る。遅延 0 | MIDI クロックは、ホストが MIDI のリアルタイムバイト（0xF8）をプラグインに渡す場合だけ働く（多くのホストは渡さないので BPM かタップが現実的。エンジン経由が前提）。Input bypass は表の末尾に追加 |
| SW LV26 Mono | モノ互換の安全装置（Width、Low mono、Mono check、帯域ごとの相関で S を減らす Auto phase fix）。遅延 0 | 設計は README「LV11〜LV30 の設計」 |
| SW LV27 Scene Sync | OBS シーンとプリセットの対応表・Learn current・シーン変更の処理・Fade between。音は素通し。遅延 0 | 他インスタンスへの送信（SW Link）と obs-websocket は未実装 |
| SW LV28 Remote Hub | タブレット遠隔操作の安全設計のコア（LAN 限定・6 桁 PIN・試行回数制限とロックアウト・8 時間トークン・端末ごとの権限・Lock all）。音は素通し。遅延 0 | HTTP／WebSocket サーバーと通信の暗号化は未実装 |
| SW LV29 Interp Mix | 同時通訳のミックス（Floor／Interp and floor／Interp、Floor under、Crossfade、通訳の声で会場音を下げる Auto detect）。遅延 0 | 通訳は第 2 入力か、Interp from（末尾に足したパラメータ）で選んだ LIVE 製品のインスタンスの出力（SW Link） |
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

### 共通機能「オーバーサンプリング 1×／2×／4×」（仕様書 共通機能表・案、`core/include/sw/oversample.hpp` の `OsSwitch`）

仕様書の共通機能は「1×／2×／4×、既定 2×。標準は最小位相 IIR ハーフバンド（報告遅延 0。周波数で変わる数サンプルの群遅延あり）。高品質設定は直線位相 FIR（遅延を報告）」（案）。デザインの EVO バーの「2× OS」ボタンは、これまで全製品で薄く表示されて何も起きなかった。**非線形の段を持つ 21 製品に、製品のパラメータの末尾（Unit A／B／C を持つ製品では、その直前）に `<コード>.os`（Step 1x／2x／4x、オートメーション可）を足し、ボタンで 1×→2×→4× と切り替わるようにした。**

- **部品：** `sw::OsSwitch`（1 チャンネル分。`process(x, f)` が、`f`（その段の波形整形・フィルター）を 1×＝そのまま／2×／4×／8× のレートで呼ぶ）。標準のハーフバンドは従来の `Oversampler2x`（12 次の楕円型 IIR 2 本）のままなので、**既定の 2× の音は従来と同じ**（共通部品 `DriveStage`・`BiasShaper` の試験は従来の値のまま通る）。`DriveStage`・`BiasShaper2x`／`4x` は `setOversample()` を持つ。8× は共通パラメータの選択肢ではなく、SA06 の Fold／Fuzz のために内部で使う。
- **ループの中にある時間のもの（DC 除去、1 次フィルター、状態変数フィルター）は、オーバーサンプリング後のレート（係数 × fs）で計算し直す。** CR01 のフィルター、GT01 の真空管段のハイパス／ローパス、SA01 のバックラッシュの追従、DY01・Drive 段・BiasShaper の DC 除去が該当。テストは「フィルターが設定によらず同じフィルターであること」（CR01：600／1200／2400 Hz のゲインが 1×・2×・4× で 0.3 dB 以内。コードに 2×を決め打ちした変異で落ちることを確認）を見る。
- **既定が 2× でない製品：** GT01（仕様書が 4× OS）と SA03（仕様書が「2× OS（4× 推奨）」）は既定 4×。SA06 は既定 4×（仕様書の 4× OS）で、**Fold と Fuzz は設定の 1 段上（既定で 8×、仕様書の「8× 推奨」）**。DY01 の Crush は設定が 2×以上なら 4×（仕様書の「Crush は 4× 推奨」。従来どおり）、設定が 1× なら 1×。MS04 は仕様書どおり独自の Oversample（4x／8x／16x、直線位相 FIR）を持つので、この共通設定は使わない。
- **分かれていた処理をまとめた：** EQ04 の Iron は 3 帯域それぞれが別のアップサンプラーを持ち、ダウンサンプラーは 1 つだけだった。ダウンサンプラーは線形なので、3 つの残差を別々にダウンさせて足す形にした（結果は同じ）。GT04 の低域の素通しは「歪む側と同じ遅延にそろえる」ためにハーフバンドを通していたので、同じ設定に従わせ、Phase align は設定が変わるたびに測り直す（1×・2×・4× のどれでも DI と位相が合うことをテスト）。
- **実装の食い違いを直した（CS01・CS03）：** どちらも「低域を分けて低域ほど強く飽和させる」1 次ローパス（CS01 250 Hz、CS03 150 Hz）を、オーバーサンプリングの 2 つのサンプル（`up[0]`・`up[1]`）それぞれに**別々のフィルター**として持ち、しかも係数は 2×fs 用だった。各フィルターは 1 サンプルおきの列（基本のレート）を処理するので、実際のカットオフは設計値の半分（CS01 約 125 Hz、CS03 約 75 Hz。係数からの計算）になっていた。1 本のフィルターを 2×（設定のレート）で回す形にして、設計値どおりにした。音の違い（3 次高調波の基本波比。CS01 は Drive 10・0.3 の正弦波、CS03 は Gain 60・0.05 の正弦波）：CS01 は 100 Hz で −27.2→−23.1 dB、200 Hz で −33.9→−27.2 dB（500 Hz 以上は同じ）。CS03 は 60 Hz で −25.5→−21.8 dB、150 Hz で −29.1→−27.2 dB。低域の飽和の効く範囲が設計値の位置まで広がった。
- **測定（折り返し：オーバーサンプリングの後に残る、帯域内への折り返し成分。テスト）：** ハードクリップ（`OsSwitch` 単体）で 15 kHz の 3 次（45 kHz）が 3 kHz に折り返す量は、基本波比 1× −10.5 dB／2× −38.5 dB／4× −51.2 dB。Drive 段（15 kHz・Drive 10）は 1× −22.6 dB で、2×・4× は −120 dB 以下（ソフトクリップは高い次数が少ないため）。各製品のテストは「1× は折り返す（−50 dB 前後より上）／2× は 1× より明確に低い（製品ごとに 3〜15 dB の閾値）／4× は 2× より悪くない」を、その製品の飽和段に実際に信号を通して確かめる（GT04・GT01・SA06 の Fold は歪みが強く高次まで出るので別の閾値、DY09 のハードクリップは 4× でも角の高調波が折り返し、改善は小さい）。
- **仕様書からの差・未実装：** ①「高品質設定は直線位相 FIR（遅延を報告）」は作っていない（MS04 だけが FIR を持つ）。標準の IIR のみ。②**再生中に設定を切り替えると、その瞬間に 1 回プチッと鳴る**（ハーフバンドの群遅延が設定ごとに違い、波形の位相が飛ぶ。Drive 段で 1 kHz・0.3 の正弦波を再生中に 2×→1× にすると、1 サンプルの最大の差が通常の最大の差の約 5 倍、1×→2× は約 1.2 倍、2×⇄4× は約 2 倍（実測）。クロスフェードは入れていない）。保存した設定の読み込みと `prepare` では状態がそろっているので鳴らない。③報告遅延は従来どおり 0（IIR の群遅延は数サンプルで、設定により変わる。Mix で原音と混ぜたときの櫛形は従来と同じ性質で、1×・2×・4× で量が違う）。④CPU は 4× が 2× のおよそ 2 倍近く、1× は約半分（設計上の見積もり。この変更での再測定はしていない）。
- テスト：`tests/test_os.cpp`（`OsSwitch`：呼び出し回数、線形段のレベル、折り返し、設定の切替中も有限、8×）、`test_drive.cpp`・`test_shaper.cpp`（共通部品）、21 製品それぞれの `test_<code>.cpp`（パラメータの位置・1x／2x／4x・既定、飽和段が設定に従うこと）。共通の測定は `tests/os_helpers.hpp`。画面：EVO バーの「2× OS」は `data-cycle` のボタンで、押すと 1×→2×→4×→1× と進み、表示は設定どおり（「4× OS」など）。このパラメータを持たない製品では従来どおり薄く表示される。

### 共通機能「Unit A／B／C」（仕様書 共通機能表・案、`core/include/sw/unit.hpp`）

仕様書の共通機能は「アナログ筐体（2Uラック・500シリーズ）のみ。部品公差を模した固定の偏差を左右別に持つ（ゲイン ±0.3 dB、周波数 ±3 %、飽和の効き始め ±0.5 dB）。A が基準、B・C は固定シード。デジタル筐体には出さない」（案）。デザインの EVO バーの「Unit A／B／C」は、これまで全部の画面で薄く表示されて何も起きなかった。**2U ラック筐体の 46 製品のうち、仕様書が「外すことを推奨」とした MS07・MT05・RS03・UT01 を除く 42 製品に、最後のパラメータ `<コード>.unit`（Step A／B／C、既定 A、オートメーション可）を足し、画面の A／B／C をそのパラメータの 3 択につないだ。**（500 シリーズの DY04 は仕様書が「持たせるか決める」としたまま、デザインにも無いので持たせていない。）

- **値の決め方：** `sw::Unit`。A は偏差ゼロ。B・C は (unit, チャンネル, スロット, 種類) の整数ハッシュ（splitmix64）から ±1 の一様な値を作り、上限（0.3 dB／3 %／0.5 dB）を掛ける。**乱数の状態も保存も無く、どの環境でも同じ値**（テストに固定値を入れ、変えると落ちる）。「スロット」は 1 つの製品の中の部品（低域シェルフ、中域ベル…）で、部品ごとに別の公差になる。左右（チャンネル 0／1）は別の値。
- **ゲイン ±0.3 dB：42 製品すべて。** `sw::Shell` の出力段に入れた（コアを通った音の、Auto gain の後・Mix の前）。Mix 0 ％や In オフの原音はそのまま（ビット同一をテスト）、Auto gain は公差を打ち消さない（測るのは公差の前）。切替は 20 ms で動く。
- **周波数 ±3 %：** コアの中にフィルターの周波数を持つ製品のうち、EQ01・EQ03・EQ04・EQ05・EQ06・EQ09（全バンド）、CS01・CS03（EQ 部）、GT04（4 バンド）、SA01（ヘッドバンプとヘッドギャップの損失）、SA02・SA03・SA04（トーンとフィルター）。バンドごとに別の公差で、左右でも別。係数は 20 ms かけて動く（SA01・SA03・SA04・GT04 は従来どおり即時）。
- **飽和の効き始め ±0.5 dB：** 飽和段の駆動ゲインにチャンネルごとの係数を掛ける（小信号の利得は 1 のまま）。共通部品 `DriveStage`・`BiasShaper`・`Saturator` に口（`setOnsetDb`）を足し、それらを使う EQ01・EQ03・EQ04・EQ05・EQ06・DY06・SA03・SA04・GT04、飽和段の自前の式を持つ DY01（Color）・CS01（鉄心）・CS03（トランス）・GT01（真空管）・SA01（テープ）に入れた。
- **公差が入るのはここまで：**仕様書が Unit を持つとする 42 製品のうち、残り（コンプの DY02・DY03・DY07・DY12、ディレイ・モジュレーション・リバーブ・ステレオ・ボーカル系など）は**ゲインの公差だけ**。中に「周波数」や「飽和」の部品が無い（またはコアが 1 つの共通フィルターで作られていて、チャンネル別に動かす意味が薄い）ためで、仕様書の表現「部品公差を模した偏差」を 3 項目すべて入れてはいない。
- **SA02 Console Sum：** もともと仕様書の進化機能「個体差」（インスタンスごとの乱数。ゲイン ±0.3 dB・飽和 ±0.5 dB・トーンの角 ±3 %）を持つ。それは残し、Unit B・C はその上に固定の偏差を足す（A は従来どおり）。
- **画面：** EQ01・DY01・DY02・DY03 の画面は、使っているデザインのファイル（`EQ01.dc.html` など。Unit のボタンがあるのは `EQ01_v2.dc.html` などの別版）に Unit のボタンが無いため、ボタンは出ない（パラメータはあり、ホストのオートメーションで動く）。他の 38 製品は A／B／C のボタンで切り替える。MS07・MT05・RS03・UT01 の A／B／C は薄い表示のまま、押したときの説明に理由（「この製品に Unit は無い」）。
- **測定（左右に同じ信号を入れたときの左右の差。Unit A はどの製品も 0.000 dB）：** 倍率が上限の ±3 ％でも 6 dB/oct の肩では最大約 0.26 dB。EQ01（+10 dB のシェルフとベル）で B 0.38／C 0.28 dB、EQ04（+12 dB の 3 バンドと HPF）で B 0.83／C 0.76 dB、EQ06 で B 0.30／C 0.18 dB。飽和の効き始めは 3 次高調波の左右差が製品により 0.05 dB から数 dB（SA01 のテープは飽和がおだやか（駆動ゲインが小さい）ので 0.01 dB 台）。
- **仕様書の確認事項への答え（私の判断）：** ①公差は「固定シード」の A／B／C 3 通りだけで、ユーザーが乱数を引き直す操作は無い。②周波数の偏差は掛け算（コントロールの値の ±3 %）で、ホストの表示値は変えない。③ゲインの公差は出力段で、飽和の効き始めの公差とは別（ゲインはコアの前後で補正しない）。
- テスト：`tests/test_unit.cpp`（値：A は 0、B・C は上限内で全幅を使い中心が 0、B と C・左右・種類で別の値、固定値）、`test_shell.cpp`（出力段の公差）、`test_drive.cpp`・`test_shaper.cpp`（`setOnsetDb`）、`tests/test_unit_products.cpp`（EQ01・EQ03・EQ04・EQ05・EQ06・EQ09・DY01・DY06・CS01・CS03・GT01・GT04・SA01〜SA04：A は左右が完全に同じ、B・C は左右が小さく違う）、ブラウザ試験（ボタン）。

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
- **Auto（解析ボタン、進化機能・区分 B）。** 仕様書：「10 秒以上の再生から長時間平均スペクトルを取り、耳の感度で重み付けしたエネルギーがほぼ 4 等分になり、かつスペクトルの谷に近い位置にクロスオーバーを置く。結果は Crossover に書き込み、Undo できる」。**`sw::CrossoverFinder`（`core/include/sw/crossover_finder.hpp`）：** 入力（左右の平均）を約 85 ms（2 の冪で 48 kHz なら 4096 サンプル）の窓・半分ずつずらして Hann 窓の FFT にかけ、**−70 dBFS より小さい窓は再生とみなさず数えない**（**再生が合計 10 秒たまるまで聴く**。無音で止めても進まない。**最長 90 秒聴いて 10 秒たまらなければあきらめて何も書かない**）。窓のパワースペクトルの平均を取り、**耳の感度は K 特性（BS.1770。ラウドネスメーターと同じ曲線をビンごとに掛ける）**で重みづけして、20 Hz〜20 kHz（か Fs×0.45）の重みづけしたエネルギーの 25／50／75 % の周波数を出す。**「谷」：** パワー（ビンの平均）を 1/6 オクターブの帯域で dB にして 3 帯域で平らにし、**各点の上下 半オクターブ（3 帯域）の中で最も低い帯域が、今いる帯域より 3 dB 以上低ければ**その帯域の中心へ動かす（3 dB の条件は、平らなスペクトルのゆらぎで動かないため。条件なしだと白色ノイズの結果がサンプルレートで変わった〔48 kHz と 96 kHz で 3836／7671／15343 と 2153／6834／13669 Hz〕）。最後に 1 オクターブ以上離し（低い方から押し上げ、上限を超えたら下を押し下げる）、20 Hz 以上に収める。**聴いている間も音は通常どおり処理される**。**画面：** デザインにあった暗い **Auto ボタン**（「Not available yet」だった）が動く（ツールバーの「Auto gain」とは別。`ui/actions.json` の `exact`）。聴いている間は「Listening 42 %」（再生の秒数 ÷ 10）、聴いている間にもう一度押すと**取り消し**（何も書かない。仕様書にない設計：「10 秒以上」なので途中で打ち切って使うことはしない）。**結果は Crossover 1〜3 に書く**（コアが自分に入れると同時に `takeParamWrite` でホストへ渡す）。**Undo：** 書き込みはページには「ホストからの変更」として届くので、ページが**3 つをまとめて Undo の 1 段（「Auto」）にする**（`ui/actions.json` の `undo`＝そのパラメータの ID。書き込みが 0.4 秒止まった時点で 1 段にする。聴いている間にそのパラメータをつまみで動かすと待ちをやめる）。Redo もでき、履歴（時計）にも出る（ブラウザのテスト）。
  **テスト：** CrossoverFinder 3（エネルギーがほぼ等しい 4 つの塊〔90〜180 Hz・500〜1000 Hz・1.7〜3.4 kHz・5〜9.5 kHz。間は何もない〕で**3 つの点が間（180〜500・1000〜1700・3400〜5000 Hz）に入る**〔谷に動かす処理を外すと塊の中に落ちて失敗することを確かめた：518／1037／3031 Hz〕、再生が 10 秒たまるまで終わらない・無音は数えない・ブロック長 256 と 37 で同じ値、1 つの音でも 1 オクターブ間隔と範囲が守られる・平らなスペクトルは雑音が違っても 3 % 以内で同じ・取り消しと 90 秒であきらめる。3 つ目のテストの中身）、DY10 Auto 2（10 秒後に 3 つが書かれ、コアの有効値と一致、ブロック長で同じ値、再生中の取り消し）、ブラウザ（押す → 「Listening」→ 3 つが書かれる → **Undo 1 回で 3 つとも戻る**・Redo・取り消し）、`host_smoke`（ボタンの呼び出しが音声スレッドに届き、もう一度で止まる）、`--blocks`、両 validator 不合格 0、ASan・UBSan 合格、8 kHz〜384 kHz のサンプルレートでも落ちない。**実測（合成）：** 上の 4 つの塊で **381／1356／3836 Hz**。白色ノイズ −20 dBFS で 5000／10000／20000 Hz（K 特性は高域を持ち上げるので重みづけしたエネルギーが高いほうに寄る）、ピンクノイズで 641／2929／7697 Hz、1 kHz の正弦波 1 つで 1356／2712／5424 Hz（1 つの音では意味のある分割にならない）。**測っていないもの：実際のミックスでの使い心地**（広帯域の素材では、K 特性の等分点が中高域に寄るので、出た値から手で直す前提）。
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

- **段の処理。** ストリームの 32 サンプルの格子に沿った短いかたまり（最大 32 サンプル。ホストのブロックの切れ目と格子の切れ目の短いほうで切る）で、選んだ順に 1 段ずつ処理する（そのため先読みリミッターをチェーンのどこにでも置ける。CS04 は Limit を最後に回していた）。EQ：Tilt は 1 kHz を軸にした低域シェルフ（−Tilt）と高域シェルフ（＋Tilt）、Q 0.5。Low／High は 80 Hz／12 kHz のシェルフ Q 0.707、Bell は Q 0.7。Comp：プログラム検出、ニー 6 dB、リリース Auto は 100 ms と 1.2 秒の 2 段（DY03 と同じ方式）、Mix は並列。Saturate：Drive 段（`sw::DriveStage`、非対称 tanh）に Drive dB ÷ 1.8 を渡す、Mix は並列。Width：サイド × Width ％。Mono below：サイドを LR4 のハイパスに通し、ミッドは同じ分割の全域通過（低域＋高域）を通して位相関係を保つ（100 Hz の逆相が −12 dB より小さくなり、3 kHz は変わらない、テスト）。Limit：先読み 1.5 ms ＋ True peak 4×（`PeakLimiter`、MS02 の既定値）。
- **既定値。** すべて何も変えない値で、EQ・Comp・Width・Limit が On、Saturate が Off。Limit が On のとき遅延 88 サンプル（72＋16、@48 kHz。仕様書の「約 100」）、Off で 0。Limit の On／Off は遅延が変わるので、次の prepare で反映（再起動を要求）。Limit を Off にしても遅延は保ち、リミッターが働かないだけ。
- **Gain match。** チェーン入力と各段の出力（補正前）の K 特性の平均二乗（3 秒の指数平均）を比べ、差を打ち消す補正を段ごとに足す（±12 dB、追従 2 秒、Off の段は 0 dB。32 サンプルごとに決めて、次の 32 サンプルで直線に入れる）。どの段を外す・並べ替える・強く動かしても全体の音量は変わらず、25 秒の定常ノイズで EQ・Saturate・Limit を動かしても Integrated は入力と 0.7 LU 以内、Off では 2 LU 以上ずれる（テスト）。
- **並べ替え。** 並び順の変更は、256 サンプル（約 5 ms）かけて音を下げ、入れ替え、次の 256 サンプルかけて戻す（以前は「1 かたまり」で、ホストのバッファが小さいと短かった）。
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

### SW Link の残り（LV29・VO05・共通のラウドネスとトラック種別。何を・なぜ・どう決めたか）

仕様書の SW Link は「同じホストプロセス内の SW AUDIO 同士でスペクトル・ラウドネス・トラック種別を共有する」。スペクトル（EQ02 Unmask）、参照スペクトル（UT03 → EQ05）、LV05 の鍵、MT05 の 0 VU 基準、LO03 の Kick は済んでいた。**残っていたのは、仕様書が「SW Link で受ける」とだけ書いて相手の選び方を決めていない LV29（通訳）と VO05（音楽）、共通のラウドネスとトラック種別。** デザインにも選ぶ部品が無い（LV05 だけ「Key」のドロップダウンがある）。そこで次のように決めた（**どれも設計値で、実機の DAW での使い心地は未確認**）。

- **何を共有するか（登録簿を版 5 に）。** 各インスタンスが毎ブロック**自分の出力の短時間ラウドネス**（K 特性、3 秒の窓。LUFS）を書く（アダプターが出力に `LoudnessMeter` を当てる。計算量は 1 サンプル 2 チャンネルで双二次 2 段ずつ：48 kHz で 1 コアのごく一部と見積もったが、**測ってはいない**）。ホストが教える**トラックの名前と種別**（UT01 の 7 種：Vocal・Drums・Bass・Guitar・Keys・Bus・Other。バスとマスターは名前に関わらず Bus。名前の分類は `core/include/sw/track_kind.hpp` に移して UT01 と共有。ホストが何も教えないときは「不明」）も書く（名前はシーケンスロックで、途中で変わった読みは捨てる）。**スロットが空いて次のインスタンスが入っても、前の値は引き継がない**（テスト）。
- **VO05「Music from」（末尾に足した Step パラメータ。保存と自動化の対象外）。** 0＝ホストのサイドチェーン（既定。これまでと同じ）、1＝**ほかの SW AUDIO 全部**、2〜＝製品を 1 つ指定（VO 製品は音楽ではないので一覧に入れない：全 124 製品）。1 と 2〜は音声を読まず、**ほかのインスタンスが公開したラウドネスだけ**を使う（順序や 1 ブロック遅れに左右されない）。1 は**動いているインスタンスのラウドネス（パワー）を足したもの**（相関の無い信号のミックスの大きさ。VO 製品は除く＝歌のトラックは音楽でない）。指定は、その製品の動いている最初のインスタンス（使っていたものがまだ動いていればそれ）。**「動いている」＝そのインスタンスの出力リングの位置が 2 回の呼び出しの間に進んだ**（止まったインスタンス、ホストがバイパスしたトラックは数えない）。聞こえる音が無い（−120 LUFS 以下）ときは「Music: not listening」でライドを保持する。選んでいる間、サイドチェーンは聴かない。**測った値（`host_smoke`、実物の .clap）：** MT02 が 1 kHz を −12 dBFS で鳴らすとライドは +4.55 dB、−18 dBFS だと −1.28 dB（差 5.83 dB。Target −6 dB・Range 12 dB・Write automation On）、MT02 を止めると「not listening」でライドは動かず、MT02 だけを指定しても同じ（±1.5 dB 以内）。
- **LV29「Interp from」（末尾に足した Step パラメータ）。** 0＝サイドチェーン（既定）、1〜＝LIVE の製品を 1 つ（LV05 の Key と同じ並びで、LV29 自身を除く 29 製品）。通訳は**音**を会場音に足すので、ラウドネスでなく LV05 の Key と同じ**出力リングの最新ブロック**を第 2 入力として渡す（ホストの処理順によっては 1 ブロックだけ遅れる。会場音と通訳の間の 1 ブロック＝数 ms のずれは、足す音どうしなので許容）。**測った値：** LV01 を指定すると、Auto detect Off で会場音が **−14.00 dB**（Floor under）まで下がり、LV01 が止まると戻る。
- **LV29 の不具合も見つけて直した（検査の途中で）。** 通訳の線が無くなった（サイドチェーンを外した、SW Link の相手が止まった）とき、会場音の下げ（−14 dB）が**Crossfade をかけずに一気に戻っていた**（コアが「通訳が無い」ときすぐ戻って、ゲインの状態を更新しなかった）。今は線が無いとき会場音の目標を 1.0 にして Crossfade で戻す（テスト：50 ms 後はまだ 6 dB 以上下、0.5〜1 秒後は元に戻る）。**副作用：** 始めから線があるとき、会場音は最初の Crossfade で下がる（以前は下がった状態で始まった）。
- **選ぶ部品：SW Link のチップ。** 両方のデザインの下のバーの「SW Link」のチップを押すと、**この ホストで今鳴っているインスタンスの一覧**（製品・トラック名・種別・ラウドネス。窓が `linklist` を 1 秒ごとに送って登録簿から受け取る）と、パラメータの全選択肢が出る。一覧の行を押すとその製品が選ばれる。**製品の指定なので、同じ製品が 2 つあると動いている最初のもの**（インスタンスの個別の指定は無い）。
- **仕様書・画面（04）との差。** 両パラメータは仕様書の表にも `04_parameters.csv` にも無い、私が足したもの。保存済みの設定は壊さない（末尾の追加）。**要確認（依頼者）：** 仕様書が望むのが「自動で相手を決める」ことなら、この選び方を変える（例えば VO05 は常に「ほかの全部」、LV29 は LV01・LV03 のうち動いている最初のもの）。
- **LV15・LV11。** `sw/link.hpp`（同じ製品のインスタンスどうしが 1 つのバイナリの中で共有する）のままにした。製品ごとに別のバイナリで、同じ製品のインスタンスは同じバイナリを使うので、ホストが 1 つのプロセスで動かす限り動く。登録簿への移行はしていない（動作は同じで、得るものが無い）。
- **LV27（シーン連動）と LV17（シーンで Mode）は、まだ。** 仕様書は「シーンの切り替えを SW Link で各インスタンスに送る」「LV27 の対応表を使う」としか書かず、受け取る側が**何を呼び出すか**（プリセット番号の意味）が決まっていない。ユーザーのプリセットは名前で保存していて、番号が無い。さらに、シーンの切り替えの出どころは OBS（obs-websocket は接続パスワードの保存方法が仕様書の「要確認」）。**提案（承認されたら実装）：** 各インスタンスが「シーンごとの設定」を自分の状態に持ち（LV27 の「Learn current」＝全インスタンスに「今の設定をこのシーンに記録せよ」を送る）、シーンが替わったら全インスタンスが自分の設定を呼び出す（LV17 の Mode もその一部）。OBS 側は別の作業。
- **LO03 の Kick**（済）は登録簿の `tag`（Role）で相手を見分ける。VO05・LV29 は製品の指定、LO03 は役割の指定。

### LO03 の設計（仕様書に数値がない部分・仕様書と画面の差）

- **画面との差。** 画面（04）の Tight の既定は 60 ％、仕様書は 50 ％。**仕様書の 50 ％を採用**（決定済みの仕様を優先。画面側の見直しは依頼者に委ねる）。Mono below は Off ＋ 20〜300 Hz で、MS06 と同じく範囲の左端（20 Hz）が Off。
- **Focus（動的ベル、Q 1.2、32 サンプルごとに目標を更新）。** 帯域の検出は Focus に合わせたバンドパス（Q 1.4）。Role ごとの動き（最大の深さは Tight 100 ％のとき。深さは設計値）：**Kick ＝ 尾を締める。** 帯域のレベルが直近のピーク（戻り 0.5 秒）からどれだけ下がったかに応じて最大 −9 dB（下がり 25 ％までは不感帯、持続音は動かさない）。実測（55 Hz・0.5 のキック風の減衰音、Tight 100 ％）：叩いた直後のピークは −6.2 → −6.5 dB（ほぼ同じ）、叩いてから 250〜800 ms の尾は −27.3 → −32.0 dB。**Bass ＝ キックの立ち上がりで下げる。** サイドチェーンの低域（120 Hz 以下）で、速いフォロワが遅いフォロワの 2.2 倍を超えたとき（かつ −60 dBFS 超）に最大 −6 dB、約 50 ms の時定数で戻る（アタック 1 ms）。実測（55 Hz のベース、60 Hz のキック風）：キック直後 −13.3 → −19.6 dB、0.4 秒後は −13.5 → −13.8 dB。**サイドチェーンが無いときは下げない**（仕様書の要確認どおり、接続が必要。SW Link は未実装）。**Both ＝ 両方。** 尾を締める（最大 −6 dB）＋自分の低域の立ち上がりで下げる（最大 −4 dB、アタック 4 ms でキック自体は通す。ベースの音の頭も「立ち上がり」と見なされるので、その音の間だけ少し下がる）。実測：キック直後 −11.8 → −15.5 dB。持続音だけなら 0.5 dB 以内で変わらない（テスト）。合計の下げは最大 12 dB。
- **Mud cut（静的ベル、Q 1.0）。** 仕様書に深さがないため、深さを **Tight に連動 ＝ 6 dB × Tight**（既定の 50 ％で −3 dB、0 ％で効果なし）と決めた。250 Hz の正弦波で −6.0／−3.0／0 dB（テスト、±0.5 dB）。Focus の動的な深さも Tight に比例する。**Tight 0 ％ ＋ Mono below Off は入力とビット単位で一致**（テスト）。
- **Mono below。** 側成分を LR4 でハイパスし、中央成分は同じ分割点の LR4 ローパス＋ハイパス（オールパス）を通して位相関係を保つ（MS06 と同じ方式）。60 Hz の側成分は −20 dB 以上落ち、中央と 1 kHz の側成分は ±0.2 dB 以内（テスト）。
- Focus の帯域に合わない音は動かさない（100 Hz の尾に対し Focus 100 Hz は 3 dB 以上、Focus 30 Hz は帯の裾で 1.6 dB 程度、テスト）。左右は同じゲインで動かす（リンク）。遅延 0。
- **SW Link（Bass 役が別の LO03 の Kick を鍵にする。仕様書：「Bass 役のインスタンスは、SW Link（または外部サイドチェーン）で Kick 役の立ち上がりを受け」）。** 各 LO03 は自分の Role を SW Link の登録簿の `tag`（1 Kick・2 Bass・3 Both）に毎ブロック書き、Role が Bass の LO03 は、ホストのサイドチェーンに信号が無いとき（つながっていない、またはずっと 0）、**同じホストプロセスにある Role＝Kick の LO03 の出力（左右の平均）を鍵にする**（`readKey("LO03", …, tag = 1)`。LV05 の Key と同じ仕組みで、リングの位置が 2 回動かないインスタンスは鍵にしない）。サイドチェーンに信号があればそちらが先。Both は 1 つのトラックの中で推定するので使わない。Kick が複数あれば動いているもののうち前からの最初（鍵だったものが動いている間はそれを使い続ける）。画面は、鍵が別の LO03 のとき説明文を「Bass: ducked by the Kick of another LO03 (SW Link)」にする（読み出し [1]）。**登録簿の版を 3 → 4 に上げた**（スロットに `tag` を足した。古い版の SW AUDIO と同じホストの中では互いが見えない：登録簿の版が違うと参加しない）。**測ったこと（`host_smoke`、実物の .clap 2 つ）：** Kick 役が 55 Hz のバーストを鳴らす（20 ブロック鳴って 40 ブロック静か）あいだ、Bass 役（Tight 100 %）の Focus のベルは **−6.0 dB**まで下がる（Tight 100 % の最大の深さ）。相手が Both のあいだは下がらず（鍵が見つからない）、Kick が止まる（処理されなくなる）と鍵は消えてベルは戻る。単体テスト：tag で絞った読み出し（Kick だけ・Both だけ・Role を変えると鍵が変わる・スロットが空いたら tag は引き継がない）。**測っていないもの：** 実際のキックとベースでの聴感、ホストの処理順（Kick のトラックが後に処理されるとき 1 ブロック遅れる）、ホストの未接続のサイドチェーンが 0 でなくノイズ入りの場合（その場合はサイドチェーンが鍵になる）。

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
- **IR の作り直しを音声スレッドで止めない。** 設定を変えると、次の処理ブロックから 3 段階（約 3 ms ＋ 2.5 ms ＋ 1.5 ms、手元の -O2 の実測の最大）に分けて IR を作り、**256 サンプルごとに 1 段階ずつ進める（ストリームの 64 サンプルの格子で動く：`sw::GridClock`。下の「IR の作り直しをブロック長に依存させない」）**。メモリは prepare で確保する。最後に畳み込みへ渡す（分割 FFT の IR の変換。この部分の時間は未計測）。フェード中は次の変更を待つ。
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
- **IR の作り直しを音声スレッドで止めない。** Category／Length／Size／Reverse／Bar fit を変えると、合成（16384 サンプルずつ）→ 変換（Length の切り落としと最後の 10 ％ のフェード、Size の再標本化、Reverse、エネルギーの 1 への正規化）→ 周波数領域への変換（1 回に 1 パーティション）→ 切り替え（チャンネルごとに別の呼び出し）を、**256 サンプルごとに 1 段階ずつ**進める（ストリームの格子で動く：`sw::GridClock`。手元の -O2 の実測：1 段階 1〜4 ms）。prepare と状態の読み込み（`snapToTargets`）では全部をまとめて行う（prepare 約 0.22 秒）。
- **IR の作り直しをブロック長に依存させない（RV04・ST05・GT02。`sw::GridClock`＝`core/include/sw/grid_clock.hpp`）。** 以前は「`process()` の呼び出し 1 回に 1 ステップ」だったので、**ホストのブロック長が 64 なら 1024 の 16 倍の時間（サンプル数）がかかり、新しい IR に切り替わるサンプルも変わった**（試験で測ると、RV04 の Category の変更が 32 サンプルのブロックで 2944 サンプル、1024 で 94208、4096 で 376832 サンプル＝約 7.8 秒。ホストの「ブロック長に依存しないこと」の検査で RV04・GT02 が 1 本ずつ差を出していた）。今は、ジョブ（IR の設計・変換・畳み込みへの読み込み・切り替え）が**ストリームの絶対のサンプル位置の 64 サンプルの格子**でだけ動く：`process()` はブロックを格子の点で切り、ジョブの開始・1 ステップ（格子 4 つごと＝256 サンプル）・切り替えは格子の点で行う（セグメントの途中では起きない）。パラメータの変更はアダプターがイベントのサンプルでブロックを切るので、**ジョブは変更の次の格子の点（最大 63 サンプル後）に始まり、何ステップ後に切り替わるかも決まっている**。結果、**出力はブロック長に依らずサンプル単位で同じ**（新しい IR への切り替えをまたいでも：32・64・100・256・1024 のブロックで 256 との最大差 1e-6 未満＝テスト。格子を外すと 0.11〜0.46 の差が出て落ちることを確かめた）。`host_smoke --blocks` は RV04・GT02・ST05 とも 0 本が差。メモリ・CPU：1 ブロックの仕事は、以前の「呼び出し 1 回に 1 ステップ」と、ブロック 256 では同じ、大きなブロックでは格子 4 つごとに 1 ステップなので増える（4096 のブロックで 16 ステップ、1 段階 1〜4 ms なら最悪 約 60 ms：**大きなブロックでの音声スレッドの負荷の山は以前より大きい**。測っていない）。
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
- **進化機能（Snare key、`rv08.evo.on`、既定 Off）。** 検出だけを「約 150〜250 Hz（200 Hz の 4 次バンドパス、Q 2）と 2〜5 kHz（4 次バンドパス）」の和に通す。テスト：−20 dBFS の 3 kHz は開く、60 Hz と 1 kHz は開かない（Off ではどれも開く）。キーを通した分だけ検出レベルは下がるので、On のときは Threshold を少し下げる。
- **Learn（被り学習、進化機能・区分 B。DY04・CS02 と同じ `sw::BleedLearner`）。** 仕様書：「スネアにだけ開くゲート。CS02 と同じ被り学習で、スネアの帯域（約 150〜250 Hz と 2〜5 kHz）にキーを合わせる」。**スネアの帯域は上の固定のバンドパス（仕様書の数値）を使い、学習するのは Threshold**（周波数は学習しない。学習器が出す HPF／LPF は使わない）。**画面の Learn（EVO バーの文の隣に足したボタン）を押すと、最長 30 秒（案）、もう一度押すと止めて適用。聴くのは「Snare key を On にしたときに検出器が追う量」**＝スネアの帯域に絞った信号の、検出器と同じ追従（アタック 5 ms／リリース 15 ms）の値で、**学習器にはその値を渡す**（検出器は波形のピークでなく追従値で比べる。合成のスネア〔ピーク −3 dBFS〕の追従値は実測で −12.4 dBFS、被り〔ピーク −14 dBFS〕は −24.9 dBFS で、ピークの真ん中〔−8.5 dBFS〕をしきい値にするとスネアでも開かない。DY04・CS02 のゲートはピークで比べるので、学習器にそのまま波形を渡す）。学習器の仕組み（立ち上がり・2 群への分け方・間にしきい値）は DY04 と同じ。**書き込むのは Threshold（目盛り 0〜10 ＝ −60〜0 dBFS、1 目盛 6 dB）と Snare key＝On の 2 つ**で、ホストへ `takeParamWrite` で渡す。聴いている間は Snare key が Off でも帯域に絞った信号で測り、ゲートそのものは今の設定のまま動く。結果が使えないとき（立ち上がり 6 個未満など）は何も書かない。
  **テスト：** 合成のドラム（同じ 2.5 kHz 付近のスネア −3 dBFS と、同じ帯域の被り〔タム〕−14／−17 dBFS）で Threshold **5.78（−25.3 dBFS）**、書き込み 2 つ。学習後は**スネアのあとだけリバーブが出て**（出力 −20.2 dB）**被りのあとは完全に閉じている**（−200 dB＝無音）。学習前の既定（Threshold −30 dBFS、Snare key Off）では被りのあとも開く（−28.8 dB）。帯域の外のハイハット（−8／−11 dBFS）でも Threshold −31.0 dBFS（帯域に絞った量で）、学習後は閉じ、既定では開く。ブロック長（256 と 37）が違っても同じ値。何も聴かない・同じ種類・同じレベルは結果なし、時間切れで自動適用。ブラウザ、`host_smoke`（ボタンの呼び出しが音声スレッドに届く）、`--blocks`・`--tails`・`--reset`、両 validator 不合格 0、ASan・UBSan 合格。**測っていないもの：実際のスネアとハイハット・タムでの使い心地**（合成の信号だけ）。

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
- **MIDI／フットスイッチ（EVO）：** CC64（サスティンペダル、64 以上 Fast・未満 Slow）、CC1（モジュレーションホイール、0〜31 Stop・32〜95 Slow・96〜127 Fast）、ノート C2・C#2・D2（36・37・38）が Stop・Slow・Fast。**キー番号は設計値（仕様書は「CC64・CC1、Note」とだけ）。** Speed はすぐに切り替わり、ホーンとドラムは慣性でゆっくり追従する。切り替えはホストへ「プラグインが書いたパラメータ」として渡る（オートメーションに記録できる）。

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
- **IR の作り直し。** Speakers／Room／Angle／Head size（または頭の向き）が変わると、音声スレッドで **256 サンプルに 1 つの小さな仕事**（IR 1 本の設計 約 0.45 ms、畳み込みの変換と切り替えは 1 本ずつ。ストリームの格子で動く：`sw::GridClock`）として 4 本を作り、20 ms で切り替える。Phones profile は即時。
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
- **Source。** Scale（既定）：Key／Scale（Major／Minor）に対する度数で、歌い手の音から Interval 度ぶん上下の音階音へ動かす。Fixed：Interval を長音階の度数の半音数（2度＝2、3度＝4、5度＝7 …）として固定の半音で動かす。**MIDI：保持中のノートが和音（音名だけ。オクターブは見ない。同じ鍵が 2 回押されたら両方離すまで保持）で、各声の「Scale の音」を和音の中でいちばん近い音へ動かす**（同じ距離なら低いほう。＝音の動きが最小になる選び方）。ノートが 1 つも無いとき・Source が Scale か Fixed のときは今までどおり。CC123・CC120（オールノートオフ）で全部離す。Key／Scale は仕様書の表に無く、**末尾に追加したパラメータ**（`vo03.key`、`vo03.scale`）。
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

- **音楽は外部サイドチェーンで受ける**（SW Link でも受けられる：下の「SW Link の残り」。サイドチェーンが無い、または音楽が −50 dBFS 未満のときは「Music: not listening」で、ライドは動かさず保持する）。
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
- Low lat（512 点・512 サンプル）は表の最後のパラメータ `rs01.lowlat` として実装した（画面の表の「EVO バーの Low lat」の行）。Profile の窓長・over-subtraction は設計値。

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
- **固めていない間は合成しない（v0.14.0 で修正）。** 長時間の試験（`host_smoke --soak`）で、Freeze を一度でも使うと CPU が 0.05 % → 1.3 % のまま戻らないのが見つかった：取り込みのあと、Off になって音が消えた（a ＝ 0）後も合成フレームを作り続けていた。いまは「Freeze が On、または消えている途中（a > 0）」のときだけ合成し、止まっていた間のフレームはまとめて追いつかず、再開は取り込み直後と同じ（現在を覆う 4 つを一度に作る。ピークの位相は止まった所から続ける）。テスト：Off になって消えたあとの 5 秒でフレームが 0 個（`framesMade()`、直す前は 472 個）、再び On にしたときの追いつきは 8 フレーム以内で音は −40 dB より大きい、長い休みのあとの 2 回目の Freeze も安定して鳴る。
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
- Δ・Auto・Unit は持たない（仕様書の推奨）。
- **0 VU の基準の共有（SW Link。進化機能・区分 A：「0 VU の基準をプロジェクト単位で持つ。SW Link で同じセッションの MT05 同士が基準を共有する」）。** Ref dBFS は MT05 のインスタンスのあいだで共有する。**仕組み（`plugin/clap/swlink.hpp` の `publishShared`／`adoptShared`＝登録簿の版 3）：**人が Ref を変えたインスタンス（ホストのオートメーション・画面）が、**登録簿の通し番号**（プロセス全体で増える）を付けて値をスロットに出し、ほかの MT05 は次のブロックで、**まだ見ていない一番新しい値**を取り込んで自分のパラメータに入れ、ホストへも書く（`takeParamWrite`＝開始・値・終了）。**取り込んだインスタンスは出さない**ので跳ね返らない。**最後の変更が勝つ。後から作られたインスタンスは、すでにある値を取る**（「その基準にそろえる」。既定値のまま作られただけのインスタンスは、ほかの値を上書きしない：アダプターは「既定値と同じ値」を変更と見なさない）。プロジェクトを開いて保存された値を読み込むと、それが変更として出る（複数の MT05 の保存値が違えば、最後に読まれたものが勝つ）。**テスト：**`test_swlink`（一度だけ取る、自分では取らない、ほかの製品は取らない、最後の変更が勝つ、2 つの変更は新しいほうを取る、後から入ったものも取る、離れた持ち主は取らない：ThreadSanitizer でも静か）、`test_mt05`（`adoptShared` が値を 1 回だけホストに渡す）、`host_smoke`（**実際の MT05 の .clap を同じプロセスで 3 回読み込み、1 つで −14 に変えると別のものの Ref も −14、後から作ったものが −14 を取り、別のもので −20 にするとすべてが −20**）。**測っていないもの：**実際の DAW（ホストがプラグインからのパラメータの書き込みをどう扱うか。トラックごとに別プロセスで動かすホストでは共有されない）。
- テスト：Ref の RMS の正弦波が 0 VU（±0.1）、−12 dBFS で +6 dB、300 ms で 99 %（±1.5 %）、PPM の戻りが 20 dB ±1.5 dB（1.7 秒）、左右が別々に読める。

### UT01〜UT03 の設計（仕様書に数値がない部分）

- **共通。** 遅延 0。Auto gain は持たない（仕様書「UT01 は外す」。UT02・UT03 も打ち消し合うので同様）。Δ は持たない（`kDelta = false`）。
- **UT01 Gain。** 処理順は 極性（Ø L／Ø R）→ Swap → Width（Mid／Side で Side×Width/100）→ Mono（(L+R)/2）→ Balance（直線。小さくする側だけ下げ、中央は 0 dB）→ Gain。Gain・Balance・Width は 10 ms で滑らかに動かし、既定値ではビット単位で素通し。Channel は Gain を掛けるチャンネルの指定（もう片方はそのまま）。**進化機能（トラックの種類ごとの Gain）：** トラック名を Vocal／Drums／Bass／Guitar／Keys／Bus／Other に分類し（英語・日本語のキーワード）、種類ごとの Gain を保存データに持つ（`rememberGain()`／`suggestedGainDb()`）。**ホストからトラック名を受け取る部分（CLAP track-info）はプラグイン層に実装した（画面の表の「UT01 の EVO の 1 行」の行）。**
- **UT02 Mono Check。** Mono は (L+R)×Mono fold（−3 dB で等パワー和、無相関の素材は 0 dB、モノ素材は +3 dB）、Side は (L−R)/2 を両チャンネルへ、Left／Right はそのチャンネルを両方へ。Phone speaker は LO01 と同じ小型スピーカー模擬（300 Hz の 4 次ハイパス＋1.2 kHz に +3 dB・Q 2.5）。Low cut は Off＋20〜300 Hz（最下段が Off）の 2 次ハイパス。Level は最後に掛ける。監視専用なので Stereo 以外・Phone・Low cut が有効なときは `exportWarning()` が true（書き出し前の警告用。画面は未実装）。
- **UT03 Reference。** 参照曲 B／C は `loadReference(スロット, バイト列)` で読む。**WAV（PCM 8/16/24/32 bit、float 32/64、extensible）と AIFF（PCM 8〜32 bit）に対応。仕様書にある FLAC・MP3 は未対応**（デコーダーを自作する範囲を超えるため。読めないファイルは false を返す）。ホストのサンプルレートへは窓付き sinc（Blackman、片側 32 タップ、ダウンサンプルはカットオフを下げる）で変換。ラウドネス合わせは「入力の統合ラウドネス（30 秒の記憶）− 参照曲全体の統合ラウドネス」を参照曲に掛ける（±24 dB で頭打ち、入力が絶対ゲート未満のあいだは 0 dB）。画面に出す補正量は `matchDb()`。Loop は Intro＝先頭 20 秒、Verse＝20〜40 秒、Chorus＝ファイル中でいちばん大きい 20 秒（100 ms ブロックの平均二乗、1 秒刻み）、Custom＝`setLoopRegion()`。20 秒以下のファイルは全体をループ。Sync play On でホストの再生位置（アダプターに任意フック `setPlayhead(秒, 再生中か)` を追加）に区間内で追従、ホストが止まっていれば参照曲は無音。Off（またはホストが時刻を出さない）なら区間の頭から自走。Crossfade は等パワー、0 ms は即切り替え。Level は参照曲だけに掛ける。ループの端と位置の飛びには 5 ms のフェードを入れる。Source A のときは入力と**ビット単位で同じ**。読み込んだ参照曲は保存データに含めない（ファイルなので画面が読み直す）。

  **画面からの読み込み（以前は画面から読む手段が無く、画面だけでは UT03 が使えなかった）。** ウィンドウ（ウェブビュー）はプラグインにファイルのパスを渡せないので、**画面がファイルを読み、デコードし、16 bit ステレオの WAV にして base64 の断片で送る**（ボタン呼び出し `refbegin <スロット>`・`refdata <base64>`…・`refend`・`refclear <スロット>`。1 断片は 3 × 65536 バイト、受け側の上限は 1 メッセージ 1 MB）。**デコードはウェブビューのもの（WAV・AIFF・MP3・FLAC・AAC など。48 kHz に変換される）なので、仕様書の FLAC・MP3 もここから通る**（コア自身のデコーダーは WAV・AIFF のまま）。20 分までの長さ、1 回のアップロードは 512 MB まで。**16 bit・48 kHz に落とす**ので参照曲は量子化（−96 dBFS）と再サンプリングを 1 回通る（ラウドネスと聴き比べのための参照なので許容。元ファイルそのものではない）。コア側は `stageBegin / stageAppendBase64 / stageCommit`（`stageAbort`）で断片をためて `loadReference` と同じ読み込みをする。**読み込みは画面のスレッドで走り、音声スレッドは止まらない**：参照曲は横で作ってポインタ 1 つの差し替えで入れ替え、置き換えられた古いものは音声スレッドが次のブロックに進むまで残す（`retired_`）。**以前の `loadReference` は音声スレッドが読んでいる配列を直接書き換えていた**（ThreadSanitizer で競合を確認。別スレッドから読み込むテストを足して、競合なしで通る）。成否は読み出し値（`loadsDone`・`loadsFailed`）で画面に返す。読み込み前に `prepare` が来た順序でも、後から来ても同じ（ラウドネスと Chorus は `prepare` で測り直す）。**測っていないもの：実際の DAW・WebView2・WKWebView での読み込み（ブラウザのプレビューでファイルを選び、JS が作った WAV をコアが読めることまで）**。macOS の WKWebView でファイル選択を出すには、アプリ側が「開くパネル」に答える必要があり、`gui_mac.mm` に `WKUIDelegate`（`runOpenPanelWithParameters`）を足した（ブランチ `in07-engine` の同じ変更と同一）。Windows の WebView2 は標準で出る。
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

- **LV05 Auto ducker。** キーは外部サイドチェーン（第 2 入力が自動で付く。**既定の「Sidechain」**）か、**SW Link で選ぶ同じホストプロセスの別の SW AUDIO インスタンス**（下の「LV05 の Key」）。キーが無ければ何も下げない。Depth／Attack（下げる速さ）／Hold（キーが止んでから）／Release（戻る時間）は dB 領域の 1 次ポールで動かす。Voice only On は共通部品 `sw/voice_detect.hpp`（VoiceDetector）の判定のときだけキーとみなす：約 16 kHz に落とした 20 ms フレーム（10 ms ごと）で、ノイズ床＋10 dB かつ −55 dBFS 以上、70〜400 Hz の周期性（正規化自己相関 0.5 以上）、ゼロ交差率 0.2 未満（拍手・ノック・一定のノイズは周期性が無いか高域寄りで外れる）を満たすフレームが直近 5 つのうち 2 つ以上。Voice only Off は「−50 dBFS かノイズ床＋10 dB のどちらか高い方」を超えたときで、仕様書にしきい値の項目が無いため設計値。Hold to duck は押している間強制的に下げる（自動化不可）。学習モデルは後期（区分 B の後半）で、いまは規則のみ。
- **LV06 Stream master。** 入力の 3 秒ラウドネス（BS.1770、K 特性）と Target の差へ、Ride speed（Slow 0.5／Medium 1.5／Fast 4 dB/秒）でゲインを動かす。上は Max boost、**下は −12 dB（仕様書に記述がないため設計値）**。起動後 1 秒と、400 ms ラウドネスが −50 LUFS 未満（無音）の間はゲインを動かさず、3 秒ラウドネスが −60 LUFS 未満で 2 秒続くと Dead air を表示する。後段は LV04 の Zero モードと同じ瞬時のピークリミッター（リリース 50 ms、左右連動）で、**保証するのはサンプルピークのみ**（仕様書の要確認どおり、先読みなしでは dBTP は保証できないので画面の「dBTP」は「dBFS」にするのが妥当）。Mono safe は 150 Hz 未満（LR4）をモノにし、左右の相関が負のときサイドを最大 12 dB 下げる（設計値）。Target は「Stream −14／Podcast −16／Broadcast −24／Custom」の選択で、Custom のときだけ別の Custom パラメータ（−30〜−5 LUFS）を使う。
- **LV07 Speech Agc。** （ゲート・ゲイン・near/far の判断は、ホストのブロックでなくストリームの 32 サンプルの格子で行う：「ブロック長に依存しないこと」）400 ms ラウドネスと Target の差へゲインを動かす（Use の係数 Speech 1／Panel 1.5／Lecture 0.6 × Speed の上げ／下げ速度 Slow 2／6、Medium 6／12、Fast 15／30 dB/秒）。下は −12 dB（設計値）。Gate 未満では測らず、Talker hold On でゲインを保ち、Off では 3 dB/秒で 0 dB へ戻す。Freeze はゲイン固定。出力に −1 dBFS の瞬時ピークリミッター。**進化機能（近い人・遠い人）：** 直近 3 秒の 20 ms フレームのレベルの広がり（90 % 点−10 % 点：近い人は間が空き、遠い人は残響で埋まる）と、大きいフレームでの高域（3 kHz 超）／中域（1 kHz 付近）の比から distance（0 近い〜1 遠い）を求め、遠いほど 3 kHz のシェルフを最大 +4 dB、Max gain を最大 3 dB 足す。**高域比の基準（−26 dB で 0、−38 dB で 1）は合成音声だけで合わせた値で、実際の声での校正が要る。**
- **LV08 Room Noise。** 256 点（平方根ハン窓、ホップ 64）の STFT で遅延 256 サンプル（仕様書どおり）。HVAC は帯域ごとのノイズ推定（約 14 フレームで平滑した電力の最小値追従、上へは 3 dB/秒、最小値の偏りを 1.5 倍で補う）とスペクトルゲイン G = 1 − a·N/P（a は Sensitivity で 1.0／1.6／2.4）を Reduction で頭打ちにし、ゲインは下がるのは 1 フレーム、上がるのは約 8 フレームで動かす。Voice guard は声があるとき 200 Hz〜4 kHz を Low −30／Mid −10／High −5 dB より深く削らない。Keyboard は 2 kHz 以上のエネルギーが 200 ms 平均より 12 dB 跳ね、声が無いときを打鍵とみなし、53 ms のあいだ 1.5 kHz 以上を Reduction だけ下げる。Learn noise は操作ボタンなのでメソッド（`learnNoise()`）で、次の 2 秒の平均パワーを雑音プロファイルとして使う（保存データには入れない）。
- **LV09 Hum Cut。** RS03 のコアそのもの（Base／Track）で、Harmonics は 1〜16 の個数どおり（RS03 に `setHarmonicCount()` を足した。RS03 のパラメータ自体は 2／4／8／16 のまま）、Depth は 0〜−40 dB を連続で（RS03 の 4 dB 刻みの Depth に換算）、Width は Narrow／Medium／Wide を Width 0／50／100 % に対応させ、Buzz は 0。Listen は「取り除いた成分（入力−出力）」で自動化不可。
- **LV10 Voice Fx。** VO02 の音程エンジンで、変換比は固定の 2^(Pitch/12)、Formant で母音だけ動かす。**遅延は仕様書の 128 サンプルではなく 1085 サンプル（22.6 ms、48 kHz）。** 仕様書の「要確認」にあるとおり、声に追従する音程変換は 1 周期より短くできないため実際の値を報告する。Preset は Pitch・Formant・Robot をまとめて `takeParamWrite` で書き込む（Low −5 st／−2、High +5／+2、Robot 0／0／On、Radio 0／0＋帯域制限、Anon −3／+2）。Robot On は声の高さに関わらず 120 Hz×2^(Pitch/12) に固定する（比は 0.5〜2 に制限）。Radio は 300 Hz ハイパス・3.4 kHz ローパス・ソフトクリップ。Anon は **フォルマントの ±0.7 st のゆらぎ（0.7 Hz の滑らかなランダム）と 6 段のオールパス（位相の分散）を足す。声を分かりにくくするだけで、匿名化は保証しない（仕様書の要確認どおり、取材用途の説明文でも保証しないと明記する）。** Monitor Off は声を加工せず素通しにする（本人の返しの切り替えで、ルーティングはホスト側／エンジン側）。Mix は共通の枠（遅延補正済みの原音）。
- **LV05 の Key（SW Link。仕様書：「Key＝SW Link のインスタンス選択（例：LV01 Voice）／外部サイドチェーン、既定は未選択」）。** パラメータ `lv05.key`（表の**末尾に足した**＝保存済みの設定は壊れない）。選択肢は **0＝Sidechain（既定。ホストの第 2 入力）、1〜29＝LV05 以外の LIVE 製品（LV01 Voice … LV30 Recorder）**。**仕様書の既定は「未選択」だが、既定を Sidechain にした**（接続がなければ何も下げない点は同じ。今までの動き〔サイドチェインを接続すれば効く〕を変えない）。**仕組み（`plugin/clap/swlink.hpp` の `readKey`）：**アダプターが、Key に別のインスタンスが選ばれていれば、**そのインスタンスの出力リング（左右の平均）の最新の `frames` サンプルを、オーディオスレッドで**（原子変数の読み出しだけ。ロックも確保もしない。持ち主が消えても読み手の数で守る）コアにサイドチェイン 0 として渡す。ホストのサイドチェインはその間使わない。ホストが 2 つのインスタンスを同じサイクルで処理する順序は決まっていない：相手が先に処理されていればそのブロック、後ならひとつ前のブロックがキーになる（**最大 1 ブロックの遅れ**。ダッカーの Attack・Hold に比べて十分短い）。**相手のリングが 2 回の呼び出しのあいだ動かなければ（止まっている・ホストにバイパスされている）キー無し**として何も下げない。同じ製品のインスタンスが複数あれば、見つかった最初のもの。コアは違いを知らず、キーの信号が来るか来ないかだけを見る（`keyProduct()` は選択を返すだけ）。**画面：**デザインの Key のドロップダウン（「LV01 Voice, MC mic」）を生かし、押すと「Sidechain」と LV01〜LV30 の一覧が出て、選ぶと `lv05.key` に書く。ボタンの点は、相手がいて鳴っていれば緑、いなければ黄、Sidechain のときは灰（アダプターが読み取り値 3 番目に 1／0 を書く）。ツールチップに理由を書く（SW Link は同じホストプロセスのプラグインしか見えない）。**テスト：**`test_swlink`（最新のサンプルがそのまま返る、別の製品・自分自身・動かないリング・離れた持ち主は読まない、同じ製品の 2 つめに移る、読み取りのあいだに持ち主が出入りしても壊れない〔ThreadSanitizer でも静か〕）、`test_lv05`（表、選択肢、`keyProduct` の対応）、ブラウザ（ドロップダウン・30 項目・選択・点とツールチップ）、`host_smoke`（**実際の LV01 と LV05 の .clap を同じプロセスに読み込み、Key＝LV01 で LV05 が LV01 の雑音の下で約 12 dB 下がり、LV01 を止めるとキーが消えて戻り、Key＝Sidechain（接続なし）では下がらない**）。**測っていないもの：**実際の DAW での処理順序・別プロセスで動かすホスト（SW Link が届かず、点は黄のまま）、相手が複数のときの選び方（最初に見つかったもの）。
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
- **LV29 Interp Mix。** 本線入力が会場音、サイドチェーンが通訳（SW Link 経由は Interp from。下の「SW Link の残り」）。Output の 3 択、Floor under（通訳が話している間の会場音の下げ量）、Interp level、Crossfade（1 次ポールで時間の 1/3 を時定数に、時間で 95 %）。Auto detect On は LV05 と同じ VoiceDetector で通訳の声のときだけ下げ、Off は常に Floor under のまま。
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
- **Assist（共振の検出）** は実装した（画面の表の「EQ02「Assist」」の行）。**Unmask（他トラックとの被り）** は SW Link の最初の部分（`plugin/clap/swlink.hpp`）で実装した（画面の表の「EQ02「Unmask」」の行）。

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
- **カーネルの再計算（別スレッド。EQ08・EQ02 の Linear）。** 仕様書どおり、カーネルの設計は**別スレッド**で行う（`sw::BackgroundWork`＝`core/include/sw/worker.hpp`）。**仕組み：**プラグイン層（アダプターの `HasUseWorker`）が `prepare()` の前にコアの `useWorker(true)` を呼ぶ。EQ08 は Linear／Mixed、EQ02 は Linear のとき、`prepare()` がスレッドを 1 本起こす（カーネルを設計しない Minimum・Natural・Zero latency ではスレッドを持たない）。オーディオスレッドは、20 ms に 1 回まで、**パラメータの配列（EQ08 は 125 個の double）を `wd_.t` にコピーして `st_ = 1`、`kick()`**（ロックも確保もしない：フラグを立てて条件変数を起こすだけ。起こすのが取りこぼされても 50 ms の待ち時間切れで拾う）。ワーカーが設計し（`Design::run`＝これまでの `rebuildKernel`／`buildLinear` と同じ式。自分の `FirDesigner` と前鳴りガードのキャッシュを持つ）、`st_ = 2`。オーディオスレッドは次のブロックで、前のカーネルのクロスフェードが終わっていれば `conv_.setKernel` で受け取る（EQ08 の L = 8192 で平均 0.25 ms・最悪 0.72 ms、L = 2048 で平均 0.05 ms〔実測〕）。**新しいカーネルが入るまでの遅れ＝設計の時間（数 ms）＋最大 1 ブロック**（以前は次のブロックの頭）。**スレッドを OS が作ってくれなければ（`start()` が false）、従来どおり音声スレッドで設計する**（スレッドが無いだけで、結果は同じ）。**状態の読み込み（`snapToTargets`）は、途中の設計を待って捨て、その場で設計して即時に入れる**（古いカーネルが後から入らない）。`prepare()` はスレッドを止めて作り直す。**`Processor` をコピーするとスレッドは付いてこない**（コピーは、自分の `prepare()` までは `process()` の中で従来どおり設計する。試験は返り値でコピーする）。`useWorker` を呼ばないコア単体（試験・オフライン）は従来どおり音声スレッドで設計するので、結果は変わらない。**効果（`process()` 1 回の最悪時間・平均。24 バンド、毎ブロック 2 つのノブを動かす、VM で 1 回ずつ測った値で、数 ms の最悪値には VM のスケジューリングの揺れが含まれる）：**ブロック 64 で、L = 2048：10.8 ms → 1.6 ms（平均 0.37 → 0.03 ms）、L = 8192：17.1 ms → 1.5 ms（0.79 → 0.07 ms）。ブロック 256 で、L = 2048：14.0 ms → 3.2 ms（1.16 → 0.11 ms）、L = 8192：27.0 ms → 2.0 ms（2.45 → 0.24 ms）。**EQ02 には `reset()` を足した**（ホストが止まった・飛んだとき、フィルター・検出器・畳み込みの履歴を忘れて、カーネル・設定・スレッドは残す。以前は reset のたびにアダプターが `prepare()` をもう一度呼んでいて、音声スレッドでテーブルを作り直していた）。**テスト：**`test_eq08_worker`（3：Linear・Mixed で、別スレッドが設計したカーネルが同期で設計したものと一致〔出力の差 1e-6 未満〕、設計は別のスレッドで走る、Minimum にはスレッドが無い、ノブを動かし続けても `process()` は確保ゼロ、コピーは自分で設計する、状態の読み込みが途中の設計に勝つ、別のサンプルレートで `prepare()` し直しても・設計の最中に破棄しても止まらない）、`test_eq02_worker`（4：同じ検査を 2 つの経路〔Mid／Side の配置〕で、と `reset()` が 3 つの位相モードで音を忘れ・確保せず・設定は残る）、ThreadSanitizer のストレス（EQ02 を Linear にして窓のスレッドとオーディオスレッドを 25 秒：競合なし）、`host_smoke`・`--blocks`・`--reset`・`--tails`・両 validator 不合格 0。**測っていないもの：**実際の DAW の小さなバッファ（64 サンプル以下）での聞こえ、Windows・macOS でのスレッドの優先度（既定のまま）。

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
- **まだ無いもの。** 製品ごとの専用表示（EQ カーブ・メーター・スペクトル・スコープなど。コアから値を渡す口も要る）、コアのメソッドを呼ぶボタン（Tap、Learn、Ring out など）、Blender 描画の素材の同梱（フォントは同梱済み：下記）、Linux の窓、拡大縮小。`docs/tasks.md` の「画面（UI）」。

## 全製品共通の部品

| 部品 | ファイル | 中身 |
| --- | --- | --- |
| 共通の処理枠 | `core/include/sw/shell.hpp` | 仕様書の順番で、製品の処理を In・Auto gain・Output・Δ で包む。原音側は製品の遅延に合わせてずらす |
| ラウドネス計測 | `core/include/sw/loudness.hpp` | ITU-R BS.1770 の K 特性、400 ms（Momentary）と 3 秒（Short-term）。Auto gain と、今後の計測系（MT01・LV23・MS01）で使う |
| モーフ | `core/include/sw/morph.hpp` | A／B の補間規則（周波数は対数、dB は直線、段階式は 0.5 で切替）。画面の A／B 操作と一緒にプラグインへ組み込む |
| プラグイン層 | `plugin/clap/clap_adapter.hpp` | 製品のパラメータ表と DSP を渡すだけで CLAP になる共通の型。ホストのパラメータ番号は製品分のあとに共通分（Auto gain、Delta、Bypass）を足す |
| ダイナミクス | `core/include/sw/dynamics.hpp` | 圧縮カーブ（ニー付き）、アタック/リリース、ピーク/RMS/Program 検出、トゥルーピーク検出、先読みブリックウォール・リミッター |
| 帯域フィルタ | `core/include/sw/bandfilter.hpp` | 1バンド分の SVF（Bell／シェルフ／Notch／6〜96 dB/oct のカット）、高域の形崩れ補正 |
| FFT・FIR 設計・畳み込み | `fft.hpp`・`fir_design.hpp`・`convolver.hpp` | 基数2の FFT、アナログ原型の振幅から直線位相／最小位相（ケプストラム法）の FIR、一様分割 overlap-save 畳み込み（新旧カーネルのクロスフェード付き）。EQ02 の Linear でも使う |
| 出力段 | `core/include/sw/drive.hpp` | Drive 0〜10（入力 0〜+18 dB）、非対称ソフトクリップ1段、2× OS、音量補正つき、小信号で利得1。EQ01・EQ03・EQ04 で共通（下の「Drive 段の設計」） |
| ゲート | `core/include/sw/gate.hpp` | ゲート／エキスパンダー／ダッカー、ピーク包絡、4 dB ヒステリシス、ホールド、dB 上のアタック/リリース |
| FIR オーバーサンプラー | `core/include/sw/oversample_fir.hpp` | 直線位相 4x/8x/16x、遅延は整数（48 サンプル）、20 kHz まで平坦、像 −80 dB 以下 |
| プリセット（自分の設定の保存と呼び出し） | `plugin/clap/gui_presets.hpp`・`gui_bridge.hpp`・`ui/sw-ui.js` | **これまでデザインの「プリセット名」（「Large hall A」など）は例の文字で、押しても何も起きなかった**。**画面上部のプリセットの名前を押すと、メニューが開く**：Init（既定値に戻す）、保存済みのプリセット（押すと呼び出す。右の × で削除、2 回押し）、名前を書いて Save。**保存先は `Documents/SW AUDIO/Presets/<製品コード>/<名前>.swpreset`**（LV30 の録音の既定の保存先・LV23 のログの書き出し先と同じ流儀。**Windows では OneDrive などに移された「ドキュメント」も正しく指す**：環境変数 `USERPROFILE` ではなく `SHGetFolderPathW(CSIDL_PERSONAL)` で本当の場所を聞く（`plugin/clap/gui_paths.hpp`。Windows でのビルド・動作は未確認）。日本語の名前も可、名前に使えない文字は「-」に、60 バイトまで）。ファイルは 1 行目 `SWPRESET 1 <コード>`、2 行目が `パラメータid=値;…`（パラメータ id は仕様書どおりの `cs02.comp.thresh` など。**番号でなく id で書くので、あとでパラメータが増えても読める**。ファイルに無いパラメータは今のまま、知らない id は無視）。**画面のラベルは、最後に呼び出した／保存したプリセットの名前（動かすと「*」）、全部が既定値なら「Init」、それ以外は「Custom」**（以前のデザインの例の名前は出さない）。保存・呼び出しは音声スレッドと無関係（画面のスレッドでのファイル入出力）で、値はふつうの画面操作と同じ経路でホストへ書く（オートメーションの記録に乗る）。含めるのは製品自身のパラメータだけ（Auto gain・Δ・Bypass は含めない）。**まだ無いもの：工場出荷のプリセット（中身を作る仕事。メニューは空の状態から）、製品間のプリセットの共有、LIVE 製品（デザインに「Main show」の枠があるだけでプリセットのボタンが無い。シーンは SW Link 待ち）、DY04・UT02（デザインにプリセットのボタンが無い）** |
| 窓のメッセージの試験用の口 | `plugin/clap/sw_message.h`・`tools/host_smoke.cpp` | **全プラグインに、窓のページが投げる文字メッセージ（`p`・`s`・`c …`）を窓なしで受ける拡張 `com.seventh-well.sw-audio.message/1` を付けた**（Linux には窓が無く、ページからコアまでの経路がこれまで一度も機械で試されていなかった。この拡張を探すホストは無く、害もない）。`host_smoke` がこれで、更新スクリプトの形、プリセットの保存・一覧・呼び出し・削除（一時的な HOME の中で）、UT03 の参照曲・RV04 の IR を**ページと同じ断片で送って**コアに届くこと（長さと成功・失敗の数）を確かめる。**この試験で、アダプタの `guiCall` が「音声スレッドへ渡す短い呼び出し」の長さの上限（引数 16 文字）を、画面のスレッドで呼ぶ呼び出しにまで掛けていて、ファイルの断片が捨てられる誤りが見つかり、直した** |
| 外部サイドチェーン | `shell.hpp`・`clap_adapter.hpp` | 製品が processWithSidechain を持つと、プラグインに2つ目の入力端子「Sidechain」が付き、共通枠が信号を渡す |
| ホストとのやりとり（アダプター共通） | `plugin/clap/clap_adapter.hpp`・`core/include/sw/tail.hpp` | **ブロック長に依存しない**（`max_frames` を超えるブロックは割る。製品の判定は 32〜64 サンプルのストリームの格子で）、**入力の NaN・∞・絶対値 10⁶ 超は 0 にしてからコアへ**、**テール**（コアの `tailSeconds()` に遅延を足して CLAP の tail 拡張に。VST3 の `getTailSamples` へは clap-wrapper が写す）、**`reset()`**（`Shell::reset()` とコアの `reset()`、無ければテールのある製品は `prepare()` を呼び直す）。それぞれの検査は「検証結果」の `host_smoke --blocks`・`--tails`・`--reset`・「壊れた値」 |

### 製品の足し方

1. `products/<code>/<code>.hpp/.cpp` に DSP とパラメータ表（仕様書の順）を書き、`tests/test_<code>.cpp` にテストを書く。
2. `plugin/clap/<code>_clap.cpp` に数行の定義（名前・分類・Output/In/Mix の番号）を書き、最後に `SW_CLAP_ENTRY(<code>, 型名)`。
3. `CMakeLists.txt` の `SW_PRODUCTS` に加え、`sw_add_plugin(<code> "<表示名>" <AU の4文字>)` を1行足す。

- Auto gain：原音と処理後の 3 秒ラウドネスを比べて補正。追従 2 秒、±18 dB まで、−60 LUFS 以下の無音では補正を保持。既定は Off。
- Δ：「Auto gain 補正後の処理音 − 原音」に Output をかけて出す。製品の遅延に合わせて揃えるので、遅延だけの処理ならちょうど 0 になる。

## 検証結果（v0.16.0、2026-10-09、132本）

| 項目 | Linux x86_64（このクラウド環境） | Windows x64・macOS（GitHub Actions） |
| --- | --- | --- |
| 単体テスト（1665件） | 全合格（手元の Linux で 1665 件全部。**SW Link v5・VO05 の Music from・LV29 の Interp from を足したあとは、関係する 43 件〔test_swlink・test_vo05・test_lv29・test_gui_bridge〕を ASan・UBSan 付きで実行して合格。SW Link に触る 10 製品〔VO05・LV29・UT01・LV05・LO03・EQ05・UT03・MT05・LV01・LV27〕の ThreadSanitizer ストレス試験（各 5 秒）も 0 件**。それより前に足した 4 件〔EQ08・EQ02 のオフライン、SW Link の 2 つの LV01 と tag〕と関連する計 19 件も ASan・UBSan で合格。**1665 件すべてを ASan で回し直してはいない**）。以前の記録：**AddressSanitizer・UBSan 付きでも全合格**：commit 579f2de の 1628 件〔Learn 系・音声スレッドの確保の検査・14 製品の修正を含む〕。その後の変更〔カーネル設計の対数格子と `test_fir_designer.cpp` の 2 件、全製品を 0〜3 フレームで呼ぶ検査〕は、関係するテスト 159 件〔FirDesigner・FIR・EQ02・EQ08・全製品の確保の検査〕を単独で ASan・UBSan で実行して合格。**EQ05 の Match を足したあとは、EQ05・BandSpectrum・確保の検査・Unit・fuzz・shell の 323 件、EQ08・EQ02 の別スレッド設計のあとは、EQ02・EQ08・確保の検査・fuzz の 283 件〔別スレッドの試験を含む〕も合格**）。`test_swlink` は ThreadSanitizer でも合格。全 132 製品の ThreadSanitizer ストレス試験は下の「データ競合の検査」） | ビルド後に実行（Windows MSVC・macOS ユニバーサル）。**全ジョブが成功した実行：run 169（commit 2b60862。v0.15.0＝畳み込みの積和ループ・`--leaks`・DY04／CS02／RV08 の Learn・CS03 の Set input・DY10 の Auto を含む。全 132 本）**。**run 172（commit d00b4c2）：v0.16.0＝EQ05 の Match・EQ08／EQ02 の別スレッド設計・音声スレッドの確保の検査・MS07・CS04 まで、Windows・macOS・Linux の全ジョブが成功。** その前の 2 回は試験側の不具合で落ちていた（製品のバグではない）：run 170 は、Windows が `tests/test_alloc_hook.cpp` の glibc の `<execinfo.h>`・`posix_memalign` でコンパイルできず（`_aligned_malloc`／`_aligned_free` を使い、トレースは glibc のときだけにした）、macOS が「確保の検査が確保を数える」自己試験の `new`／`delete` を Apple clang に消されて 0 回になっていた（ポインタを volatile に出した。**macOS でも確保しないことの検査そのものは全部通っていた**）。run 171 は、EQ05 の Match を試す `host_smoke` の参照（一次の低域通過を足した雑音）が、サンプルレートで傾きの角が変わる作りで、96 kHz では LF シェルフでなく LMF のベルが低域を持ち上げ「LF Gain が +1 dB 以上」が通らなかった（手元で 96 kHz に切り替えて同じ −0.92 dB を再現してから、角を Hz で固定〔400 Hz〕・検査は LF か LMF の持ち上げに直した）。**run 173（commit e209561。UT03 の参照を SW Link で EQ05 へ・RV04／ST05／GT02 の IR の作り直しをブロック長に依存させる）は、Windows・Linux・macOS の 3 つのシャードが成功し、macOS の 1 つのシャードで単体テスト 1 件だけ失敗した：`test_swlink` の「別スレッドが書き換え続ける参照を読んでも混ざらない」で、混ざった値が読めた。原因は製品のバグ（SW Link の参照スペクトルの読み書き）：読み手は「番号→値→番号」と読むが、値の読み出し（relaxed）が 2 回目の番号の読み出しより後ろに回ることがある弱い順序の CPU（Apple の ARM）で、番号が同じのまま値が混ざった。x86 の Linux・Windows では起きない。シーケンスロックの定石どおり、書き手は番号を 0 にしたあとに release フェンス、読み手は値のあとに acquire フェンスを置いた（共有の設定 `publishShared`／`adoptShared` も同じ形にした）。**run 174（commit 9e0aae8。シーケンスロックのフェンス・LV05 の Key・MT05 の 0 VU 共有・ワーカーの開始の保険）は、macOS の 4 つのシャードと Windows が全て成功（macOS の ARM の不具合は直った）。Linux は単体テスト 1 件だけ失敗：同じ `test_swlink` の読み取りの試験が、書き手スレッドがまだ動き出さないうちに固定の 20000 回の試行を終えて「読めた回数が 0」になった（試験の作りの問題。CPU を埋めた状態で再現してから、「2000 件読めるまで（最長 5 秒）」に直し、同じ負荷で 6 回通した）。run 175（commit 3be4ee9。test_swlink の読み手の修正）は、Windows と macOS が全て成功し、Linux は単体テストまで通り、ホスト経由の試験（`host_smoke`）の「SW Link key」だけが落ちた：LV05 の Key を LV01 にして LV01 が止まっても ducking が戻らない（5 秒後のゲイン −6.09 dB、サイドチェインに戻しても −3.2 dB。48 kHz での正常値は −1.70 dB・−0.47 dB）。**原因は試験の作り（製品のバグではない）：** 待つ長さをブロック数（480・960 ブロック）で固定していたので、**96 kHz の実行では 2.56 秒・5.12 秒が 1.28 秒・2.56 秒になり**、保持 1.2 s ＋ 戻り（時定数 2 s）の ducking が戻りきらない（−12 dB × e^(−(2.56−1.2)/2) ＝ −6.09 dB が CI の値と一致）。手元で `--rate=96000` にして CI と同じ −6.089545／−3.206692 dB を再現してから、待つ長さを秒で決める（ブロック数をサンプルレートに比例させる）ように直し、44.1／48／96／192 kHz の 4 つで合格を確かめた。この試験が CI で 96 kHz を通ったのは初めてだった（run 173・174 は単体テストの失敗で、ホスト経由の試験まで進まなかった）。**調査の途中で別の本物の不具合を見つけて直した（これは run 175 の原因ではない）：`readKey`（LV05 の Key）が、同じ製品のスロットが 2 つあって一方が止まっているとき、**状態が 1 スロット分しかなく、別のスロットを「初めて見たもの」として動いているとみなし、止まった方の最後の 1 ブロックを鍵として読み続けた（状態が 2 つのスロットの間を行き来し、3 回に 2 回は鍵が「ある」）。手元で 2 つ目の LV01（鳴らさない）を足すと、−12 dB のまま戻らず再現した（ミュートしたトラックに同じ製品が挿してある形で起きる）。直したこと：`KeyState` を**スロットごとの動きの記録**（ring の位置と、動かなかった回数）にし、動いているスロットだけを鍵にする（最初の呼び出しだけは全部が候補、あとから現れたスロットは動いてから。鍵だったスロットがまだ動いていればそれを使い続ける）。試験は直す前に落ちることを確かめた（`test_swlink` に 2 つの LV01 の試験を追加：止まったあとの 40 回に鍵が残った）。`host_smoke` の LV05 の試験にも「鳴らさない 2 つ目の LV01」を足した。**この修正後の実行：run 176〜179 は次の push で打ち切られた。run 180 と 182 は `race` ジョブが EQ08 の TSan 報告 2 件で失敗した（GCC 11 の ThreadSanitizer が `pthread_cond_clockwait` を見ず、同じ mutex の下の読み書きを競合と報告する誤検出。手元の GCC では出ない。`BackgroundWork` が TSan のときだけ `system_clock` で待つ形にした：製品の挙動は変えていない）。**run 183（commit e703e7b。v0.16.0 のあとの SW Link の修正・オフライン書き出し・追加ブロックの門・テンポの検査・`race` ジョブを含む）は Windows・macOS（auval を含む）・Linux の全ジョブが成功し、`race` ジョブ〔17 製品の ThreadSanitizer〕も 0 件**。**run 186（commit 86f57f3。SW Link v5＝出力ラウドネス・トラック名と種別の共有、VO05 の Music from、LV29 の Interp from を含む）も、Windows・macOS 4 シャード（auval を含む）・Linux（単体テスト、UI の検査とブラウザ検査、host_smoke の 4 つのレート、両 validator）の全ジョブと、19 製品の `race` ジョブが成功。** |
| CLAP：clap-validator 0.4.1 | 各 0不合格（33合格・11対象外。軽い製品で「極小値で遅い」の警告が出ると32合格：上の「clap-validator の「極小値で遅い」警告について」） | 同じ検証を各 OS で実行（run 169：成功） |
| VST3：Steinberg validator（SDK 3.8.0） | 各 47合格・0不合格 | 同じ検証を各 OS で実行（run 169：成功） |
| AU：auval | — | macOS で実行（run 169：成功。aufx の効果 128 本と、MIDI を受ける aumf の 4 本〔CR04・LV25・MD05・VO03〕を、auval が登録している種類で） |

v0.11.0（23本）の時点では Windows を MinGW でクロスビルドして Wine 上で検証していた（CS04 以外の 22 本で不合格 0）。現在の Windows の根拠は上の GitHub Actions（MSVC）で、Wine での再検証はしていない。

`tools/validate_all.sh` で、ビルド・単体テスト・全プラグインの両検証を一括で実行できる（Linux）。

**実機（DAW）でしか確かめられないこと**は `docs/real_host_checklist.md` に項目ごとにまとめた（画面・ファイル選択・プリセットの保存先・SW Link・トラック名・耳で確かめるもの・負荷）。

**ホスト経由の音声経路テスト（`tools/host_smoke.cpp`、Linux、CI の Linux ジョブでも実行）。** 単体テストはコア（DSP）だけ、validator は API の作法だけを見るので、「プラグイン層がパラメータをどこへ回すか」の間違いは通り抜ける（GT03 の Input が枠の In スイッチに繋がって全体がバイパスされた、DY10・DY11・SA02 の Output が枠とコアの両方でかかって 2 倍になった、はどちらもこのすき間にあった）。そこで、ビルドした `.clap` を DAW と同じように `dlopen` して、−20 dBFS 相当のノイズを通して次を確かめる：Output（dB のゲイン）を +12 dB（上限が低いものは +10 dB）にして実際にその分だけ上がるか（3 dB 以上多いと不合格＝二重にかかっている、少ないと警告）、ホストの Bypass で入力のレベルに戻るか（±0.5 dB）、`<コード>.mix` を 0 % にすると遅延分だけずらした入力そのものになるか（差 1e-3 以内）、パネルの In を Off にすると入力のレベルに戻るか、既定で NaN／Inf／異常なピークが出ないか。全 132 本で不合格 0（Output 30 本・Bypass 131 本・Mix 41 本・In 1 本を確認。Bypass が無い 1 本は EQ05 で、パネルの In がパラメータなので In Off で確認した）。既定で入力とビット単位で同じ出力になる製品が 49 本ある（メーターや計測、既定でオフのエフェクト、しきい値に届かない −20 dBFS のノイズでは動かない動的処理など。**音が変わらないことの確認ではなく、動かないことを示すだけ**）。同じ実行で処理時間（1 秒の音に対する CPU 割合）も出るが、CI のランナーは揺れるので判定には使わない。**測っていないもの：**パラメータを動かしたときの音の中身（それは単体テスト）、ホスト固有の挙動（トランスポートなし・イベントは先頭だけ）。

**サンプルレート（仕様書は 44.1〜192 kHz）：** `sw-host-smoke --rate=44100`・`96000`・`192000` で、同じ検査を同じ秒数のまま別のレートで実行できる（CI の Linux ジョブでは 4 つのレートすべて）。**このクラウド環境で、132 本すべて 44.1・96・192 kHz で 0 FAIL・警告 0**（NaN・Output・Bypass・Mix・In・画面メッセージの各検査）。**CPU（このクラウド環境、4 コアのうち他の作業が 1.5 コア分の負荷、ノイズ入力、`host_smoke` の平均。1 コアに対する割合）：** FFT と畳み込みを速くする前（192 kHz）は RV04 138 %・ST05 121 %・GT02 108 %・EQ08 Linear 51 %、48 kHz では ST05 26 %・RV04 19 %・RS04 12 %・GT02 10 % だった。**速くした後（RV04・ST05・GT02 だけ測り直し）：48 kHz で RV04 10.6 %・ST05 13 %・GT02 6.8 %、192 kHz で RV04 106 %・ST05 63 %・GT02 100 %。GT02 は畳み込みを「256 サンプル均等分割の 1 本」から RV04・ST05 と同じ `TieredConvolver`（左右 1 本ずつ）に替えて、48 kHz で 3.9 %・192 kHz で 30.6 % になった**（IR の 1 タップ 1 タップがこれまでと同じ出力になることは、新しいテスト「プロセッサのインパルス応答が設計した IR と 48・192 kHz で全タップ一致」が替える前の実装で合格してから置き換えた）。**192 kHz の RV04 は、この時点ではまだ 1 コアに収まらなかった**（その後、積和ループを直して収まった：下の「畳み込みの積和ループ」）。**この環境は浮動小数点のループが遅い**（`c[k] += a[k]*b[k]` が 1 要素 1.3 ns。AVX512 を持つ CPU なのに、基本の SSE2 でビルドした倍精度のループがこの速さ）ので、**実機の割合はこれよりずっと低いはずだが、測っていない**。96 kHz までは全製品が 1 コアに収まった（最大 ST05 58 %、FFT 高速化の前の測定）。
**仕様の範囲の外のレート：**22.05 kHz と 384 kHz でも同じ検査を通した（132 本 0 FAIL。384 kHz では RV04 が 1 コアの 373 %、EQ08 Linear 182 %、ST05 169 %：実時間で処理できない。仕様は 192 kHz まで）。

**プロジェクトの保存と読み込み（`host_smoke`、全 132 製品の実物の .clap で）：** すべてのパラメータに乱数の値を入れ → 状態を保存 → 新しいインスタンスに読み込み → 全パラメータが同じ値で戻り、**もう一度保存すると同じバイト列**になる（1 回で読み込む場合と、7 バイトずつ読む場合の両方）。保存した状態を**途中で切ったもの（最大 13 か所）と、でたらめなバイト列（4 通り：先頭だけ正しいものを含む）を読ませても落ちず、パラメータが範囲の外に出ない**。**パラメータ ID（ホストがオートメーションとセッションに書き込む番号）は、製品のパラメータ＝表の並び順（0 から）、共通のスイッチ＝固定の 0x1000（Auto gain）・0x1001（Delta）・0x1002（Bypass）：**これまでスイッチの ID は「製品のパラメータ数 ＋ 0〜2」だったので、製品のリストの末尾にパラメータを足す（Low lat・Oversample）たびにスイッチの ID が動き、足した後の版でホストのオートメーションが別のパラメータを動かす恐れがあった。固定にして、`host_smoke` が全製品で確かめる。**古い版で保存した状態（製品のパラメータが 1 つ少ない状態）も読める：**状態は「製品のパラメータ → 共通のスイッチ（Auto gain・Delta・Bypass）」の順で並んでいて、製品のリストの末尾にパラメータを足す（Low lat・Oversample など）と、スイッチの位置が 1 つずれる。以前は位置で読んでいたので、足した後に古い状態を読むとスイッチが別のパラメータに入った（EQ01 で再現：Delta が 0 で戻る）。**読み込みは「最後のスイッチ分は末尾から、残りは先頭から」にした**（`plugin/clap/clap_adapter.hpp`）。`host_smoke` が全製品で、最後の製品パラメータを抜いた状態を作って読ませ、残りのパラメータとスイッチがそのまま戻ることを確かめる（元のコードに戻すと落ちることを確認）。結果：**全 132 製品で合格**（このクラウド環境、Linux）。

**ランダムな設定（`host_smoke`、全 132 製品）：** すべてのパラメータを乱数の値にして（3 通りの種）、ノイズを 0.4 秒通す：出力に NaN・Inf が無く、`process()` がエラーを返さず、ピークが 10⁴ 未満（実プラグインの経路。コアだけの同様の試験は `test_fuzz_bounds`）。全 132 製品で合格。

**インスタンスあたりのメモリ（`host_smoke --memory`、このクラウド環境）：** DAW のセッションは同じプラグインを何十個も立てる。各製品を 1＋6 個作って有効化し、常駐メモリの増え方を 1 個あたりで出す。**RV04 Convolution が桁違い：48 kHz で 88 MB、192 kHz で 342 MB**（IR は最大 10 秒。16384 サンプル以降の段が、周波数領域の区画を kernel の 3 組（今・次・読み込み中）と履歴の計 4 組、2 チャンネル分、倍精度の複素数で持つ：約 60 MB、そこに kernel と IR のコピーが約 30 MB。内訳は構造からの見積もりで、部分ごとには測っていない）。ほかの製品は最大でも GT03（5 MB）・DL05（4 MB）・CR05（4 MB）、192 kHz で GT03 18 MB・DL05 16 MB。**132 製品を 1 つずつ立てた合計は 48 kHz で 113 MB、192 kHz で 416 MB。**RV04 を減らす（区画を単精度にする、IR の最大長を使う長さに合わせるなど）のは、精度と IR の長さの仕様に触れるので、今はしていない。**作って壊すのを繰り返したときのメモリ（`host_smoke --leaks`）：**各製品で「作成・有効化・8 ブロック処理・無効化・破棄」を 4 回で慣らしたあと 40 回繰り返し、常駐メモリの増えが 4 MB を超えたら失敗：**全 132 製品で増えなかった**（約 13 秒）。小さな漏れ（1 インスタンスあたり 100 KB 未満）は見えない。

**ホストが止まった・飛んだとき：`reset()`（`host_smoke --reset`、全 132 製品の実物の .clap）：** DAW は再生を止めたとき・ループの頭・位置を飛ばしたときに CLAP の `reset()` を呼ぶ。これまでの `reset()` はパラメータをなめらかにする途中の値を合わせるだけで、**音を覚えている部分は何も消していなかった**ので、リバーブの残りやディレイの繰り返しが飛んだ先で鳴った。測り方：0.4 秒のノイズを通し、`reset()` を呼び、0.5 秒の無音を通す（遅延ぶんは待つ、最初の 20 ms は除く）。**直す前は 39 製品が −19〜−80 dBFS で鳴り続けた**（ST05 −19、CR03 −18、RV02 −22、DL05 −22、RV04 −25、DL02 −25、DL01 −27、MD02 −27 など。リバーブとディレイはどれも −20〜−35 dBFS）。直したこと：`Shell::reset()`（ドライを揃える遅延線・ラウドネスメーター・Auto gain の推定を消す）、コアに `reset()` があればそれ（RV04・ST05・GT02：畳み込みの履歴・プリディレイ・フィルターだけ消して IR は残す。`Convolver`・`DeferredConvolver`・`ZeroLatencyConvolver`・`TieredConvolver` に `reset()` を追加、`TieredConvolver` は重いブロックの位置を保つ）、無くてテールを報告する製品は `prepare()` をもう一度呼ぶ（バッファを同じ大きさで確保し直すだけで、割り当ては起きない。DL01〜DL05・LV25・RV01〜RV03・RV05・RV06・LV24・CR03・CR06・VO07・GT03・MD02・EQ02・LV14・LV19）。MD02 Flanger はテールの報告を追加した（フィードバックの繰り返し）。**直したあと、−40 dBFS を超えて鳴り続ける製品は無い。** 残る 23 製品は −43〜−80 dBFS のフィルターの余韻（ノッチ・シェルフなど：RS03 −43、RV07 −43、LV09 −45、LV25 −50、SA03 −51 ほか）。**挙動が変わる点：**ホストが `reset()` を呼ぶと、リバーブ・ディレイの残りが消える（以前は残った）。学習した値（RS01 のノイズ、EQ07 の閾値など）を持つ製品は、`prepare()` を呼び直す対象にしていないので消えない。**測っていないもの：**実際の DAW が `reset()` をどの場面で呼ぶか、`prepare()` の呼び直し（最大 10 ms）が音声スレッドで起きたときの影響。

**ホストが壊れた値を渡したとき（`host_smoke`、全 132 製品の実物の .clap）：** ①**パラメータ**：上の「ランダムな設定」の 4 つ目の種で、各パラメータに NaN・±∞・±10³⁰・範囲を 10⁶ 倍超えた値を混ぜて渡す（壊れたオートメーションのレーン）：出力は有限、`process()` はエラーを返さない、ピーク 10⁴ 未満（全製品で合格）。②**音声入力**：入力の 1 サンプル（左）が NaN／+∞／−∞（上流のプラグインの不具合、壊れたファイル）→ そのあとの 0.8 秒のノイズの最後の 0.3 秒が有限で、極端に大きくない（サイドチェインのある製品は、サイドチェインにも同じ値）。**最初の測定で、RS05 Declip が音声スレッドで無限ループした**（NaN の入ったサンプルが「クリップした区間」の探索で進まない：`std::abs(NaN) < clip` が偽になり、同じ位置を回り続ける）。直したこと：アダプターが入力（とサイドチェイン）の NaN・∞・絶対値 10⁶ 超（フルスケールの +120 dB）を 0 にしてからコアに渡す（再帰フィルター・エンベロープ・ディレイ線に NaN が残り続けるのを全製品でまとめて防ぐ。入力メーターにも NaN を出さない）＋RS05 の探索を NaN でも進むように（`!(abs >= clip)`。単体テストは直す前のコードでは終わらない）。以後、全 132 製品が合格。

**ホストが変なトランスポートを渡したとき（`host_smoke --transport`、全 132 製品の実物の .clap。通常の `host_smoke` でも走る）：** テンポや小節の位置を読む製品（DL01〜05・MD02〜04・CR01・CR02・CR05・DY08・LV25・RV02・RV04・SA08・VO07・UT03 の再生位置）に、ホストが壊れた値を渡しても落ちないことの検査。3 つの台本を 1 ブロックずつ順に送る：①**テンポ** 120・0・−120・NaN・+∞・−∞・10⁻³⁰⁰・10⁻⁹・10⁹・10³⁰・20・999・60・240 bpm（5 ブロックずつ）、②**小節の位置と拍子**（拍の位置が 0・負・INT64 の両端・10⁶ 拍、小節の頭が別の値、拍子 4/4・0/4・4/0・65535/1・1/65535・7/8・0/0、再生中と停止中、テンポの旗あり・なし）、③**再生位置（秒）と旗**（INT64 の両端、旗の乱数、旗なし、トランスポートそのものが無いブロック）。各台本を 2 通りの乱数の設定（全パラメータが乱数。Sync の入り切りも半々）で 0.4 秒鳴らし、出力が有限・`process()` がエラーを返さない・ピーク 10⁴ 未満・実時間の 5 倍を超えない（来ない拍を待つループの検出）を確かめる。**最初の実行で 5 製品が NaN／∞ を出した**：**テンポが +∞ だと DL05・MD02・MD03・MD04**（`bpm_ > 0` が真のまま音符の長さ〔240 / bpm 秒〕が 0 になり、`1 / 長さ` や長さでの割り算が ∞／NaN になる）、**テンポが NaN だと DY08**（`tempo_ <= 0` が偽のままオートリリースの `60000 / tempo_` が NaN になり、`std::clamp` も NaN を返す。`bpm == tempo_` も NaN では常に偽なので毎ブロック計算し直す）。直したこと：アダプターが、テンポを **1〜1000 bpm のときだけコアに渡し、NaN・±∞・0・負・範囲外は「テンポなし」（0）にする**（`clap_adapter.hpp`。どのコアも 0 を「なし」と読む）。直す前のアダプターでは上の 5 製品が落ち、直したあとは 132 製品とも合格。**測っていないもの：** 実際の DAW が壊れたテンポを渡す頻度（普通はない。テンポマップの切れ目や、スクリプトからの不正な設定に備えた防御）、拍子が 4/4 でないときの小節線の位置の正しさ（この検査は「落ちない」だけを見る）。

**ホストが 1 チャンネルだけ渡したとき（`host_smoke --mono`、全 132 製品の実物の .clap。通常の `host_smoke` でも走る）：** プラグインはステレオの端子を宣言するので普通のホストは 2 チャンネルを渡すが、宣言に従わないホストやバスを狭めたホストが 1 チャンネルだけ渡しても、第 2 チャンネルに手を伸ばすコアが落ちないことの検査。入出力（とサイドチェイン）を 1 チャンネルにし、**2 つ目のポインタを `nullptr` にして**（本物のモノのバッファには 2 つ目が無い。読めば落ちる）、全パラメータを乱数にして 0.4 秒のノイズを 3 通りの設定で鳴らす：出力が有限・`process()` がエラーを返さない・ピーク 10⁴ 未満。**全 132 製品が合格**（落ちた製品は無く、直したコードは無い。最初のポインタ版でも同じ結果。うち 130 製品が最初の設定で実際に音を出した〔残りは出力が無音のまま動く製品〕ので、1 チャンネルが通ったことの確認にもなっている）。**測っていないもの：** モノラルの入力に対する音の正しさ（左右の相関を使う製品が 1 チャンネルのとき何を表示するか）、VST3／AU でホストが本当にモノを渡す経路（clap-wrapper がステレオ以外のバス配置を断るかどうか。実機待ち）。

**オフラインの書き出しで同じ結果になること（`host_smoke --offline`、全 132 製品の実物の .clap。通常の `host_smoke` でも走る）：** 書き出し（バウンス）は実時間より速く回るので、同じプロジェクトから同じファイルが出なければならない。2 つの新しいインスタンスに同じブロックと同じパラメータの変更（全パラメータが乱数、最初と 10 ブロックごと）を与え、2 つ目は 1 つ目の保存した状態を読み込んだもの（SA02 の個体ごとのシードは状態に入っている）にして、出力が **1 ビットも違わない**ことを確かめる（3 通りの乱数の設定）。**最初の実行で、EQ08 Linear が 20 回中 14 回、2 回の出力が違った**（別スレッドの設計が「できたとき」に入るので、新しいカーネルに切り替わるサンプルが実行ごとに違う。速く回るほど差が大きい）。EQ02 Linear も同じ作りで同じ性質（待たない版のコアの試験は、EQ08・EQ02 とも 12 回の実行のうち 1 回目と比べた 11 回がすべて違った）。直したこと：①コア（EQ08・EQ02）に `setOffline(bool)`：オフラインのとき、音声スレッドは設計を頼んだあと**終わるまで待つ**（`BackgroundWork::waitIdle`。次のブロックの頭で必ず受け取る＝切り替わるサンプルが毎回同じ。実時間では待たない）。②アダプターが **CLAP の render 拡張**を実装（`setOffline` を持つコアだけ。`has_hard_realtime_requirement` は偽、`set(OFFLINE／REALTIME)` でコアに伝える）。VST3 のホストがオフライン処理で `setupProcessing` に `kOffline` を渡すと、clap-wrapper がこの `render->set` を呼ぶ（wrapasvst3.cpp で確認）。直した後は 132 製品とも同じ（EQ08・EQ02 は render 拡張あり、他は 2 回の出力が最初から同じ）。**測っていないもの：** render 拡張を呼ばないホストでの書き出し（その場合はこれまでどおり実時間と同じ振る舞いで、切り替えのサンプルが実行ごとに揺れる）、実際の DAW のバウンス。

**ホストがサンプルレートやバッファの大きさを変えたとき（`host_smoke --reactivate`、全 132 製品の実物の .clap。通常の `host_smoke` でも走る）：** 同じインスタンスを deactivate → activate し直す（48 kHz/256 → 96 kHz/4096 → 44.1 kHz/64 → 192 kHz/512 → 48 kHz/256 → 22.05 kHz/1 フレーム）。最初の activate の大きさで確保したバッファや、古いレートで計算した係数が残っていないことを見る。パラメータは乱数（2 通り）で、各段階で 20 ブロック：出力が有限・`process()` がエラーを返さない・ピーク 10⁴ 未満。**全 132 製品が合格**（見つかった不具合・直したコードは無い）。**測っていないもの：** 再 activate の前後で音が同じになること（フィルターの状態などを引き継ぐかは製品による。この検査は「落ちない・暴れない」だけ）、実際の DAW がレートを変える操作。

**ホストが 0 フレームや 1 フレームのブロックを渡したとき（`host_smoke --tiny`、全 132 製品の実物の .clap。通常の `host_smoke` でも走る）：** イベントを渡すためだけに `process()` を 0 フレームで呼ぶホストや、1 フレームずつ呼ぶホストがある。普通のブロック 20 個のあとに 0 フレームのブロック 40 個（そのうち 4 回は全パラメータが乱数のイベント付き）、1 フレームのブロック 300 個（3 回はイベント付き）、また普通のブロック 20 個：出力が有限・`process()` がエラーを返さない・ピーク 10⁴ 未満（2 通りの設定）。**全 132 製品が合格**（見つかった不具合・直したコードは無い）。測っていないもの：0 フレームのブロックが音の状態を進めないこと（時間がブロック数で進む製品が無いか。この検査は「落ちない・暴れない」だけ）。

**長時間の連続運転（`host_smoke --soak=秒`、全 132 製品の実物の .clap）：** 指定の秒数ぶんの音（ノイズ・正弦波・ごく小さい信号）を実時間より速く通し、1 秒ごとにランダムなパラメータを 1 つ動かす。配置は [ウォームアップ 2 秒][A：既定値で 8 秒][ランダム操作][全パラメータを既定値へ戻す][ウォームアップ][B：既定値で 8 秒]。**A と B は設定が同じなので、B の 1 ブロックあたりの時間が A の 1.6 倍を超えて（かつ 1 コアの 0.5 % 超で）増えたら、プラグインが何かを溜め込んでいるとして落とす**（各区間の最速の 1 秒どうしを比べる：このクラウド環境の CPU の速さは数秒のうちに 2 倍ほど動くので、遅い側の揺れを除く。時間だけで落ちた製品は同じ条件でさらに 2 回測り、3 回とも落ちたときだけ不合格。バグのある CR04 は 3 回とも 4〜5 倍で落ちる）。ほかに、メモリが 16 MB 以上増える、NaN・Inf、出力のピークが 10⁴ 超、`process()` のエラーでも落とす。**これで見つけた不具合：CR04 Freeze は一度でも使うと、音が消えたあとも合成を続けて CPU が 0.05 % → 1.3 % のまま戻らなかった（直した：上の「CR04 Freeze の設計」）。** 最初の判定は区間の中央値で、DY01・EQ02・LV23・RS06 が 1.6〜2 倍で落ちたが、単独で測り直すと同じ製品が 1.3〜2.0 % の範囲で上下した（EQ02 は A のほうが遅い回もあった）ので、機械の揺れと判断して最速の 1 秒＋再測定に変えた。**結果（このクラウド環境、120 秒ぶん × 132 製品）：**132 本とも合格（FAIL 0）。メモリの増加は全製品で 0 MB（MB 単位の表示で）、NaN・Inf・暴走した出力は 0。同じ設定での CPU は A→B でほぼ同じ（最も重いのは ST05 12 %・RV04 11 %・MS04 8.5 %・VO03 8 %）。** 初回の測定で時間が増えて再測定に回ったのは LV04 だけ（0.37 → 0.74 %、再測定は 0.66 → 0.45 %、単独の 2 回は 0.37 → 0.44・0.37 → 0.45 %）。** 測っていないもの：1 時間以上の実時間での連続運転、DAW の中での長時間運転。

**ブロック長に依存しないこと（`host_smoke --blocks`、全 132 製品の実物の .clap）：** DAW のバッファの大きさ、オフラインの書き出し、オートメーションの点でブロックを割るホスト、どれでも同じ音が出るはずなので、同じ入力を 256 サンプルずつ処理した結果を基準に、1・7・64・509・1024・混合（1, 2, 3, 5, 8 … 987）・2048（`max_frames` 1024 より長い：CLAP の決まりに反するホストの想定）のブロックで処理した結果との差の最大値を、基準のピークに対する dB で出す。入力は次のとおり：①−20 dBFS RMS のノイズ（定常）、②大きい入力（ピークが 0 dBFS を超える：リミッターやコンプが働く）、③220 Hz のトーンのバースト（0.12 秒鳴って 0.1 秒休む）＋小さいノイズ（ゲート・検出器・ピッチ追従が動く）、④サイドチェイン入力のある製品だけ：メインにノイズ、サイドチェインにトーンのバースト、⑤①＋パラメータ 3 個をランダムに動かす（動かす位置は全部同じ絶対サンプル、アダプターがそこでブロックを割る）。⑥MIDI 入力のある製品だけ：①のバースト入力に、ノート・CC・ノートオフを絶対サンプルの位置で（Freeze のある製品は Freeze On）。「混合」のブロック長には 0 サンプルのブロックも入れてある（何もしないで返ること）。2 個目以降のインスタンスには 1 個目の状態を読み込ませる（SA02 のように部品公差のシードを状態に持つ製品のため）。**判定：定常（①②）は −90 dB より大きい差で DIFF、パラメータを動かす場合（③）は −50 dB より大きい差で DIFF**（係数のランプが各製品の制御の格子から始まるため、ブロックの切れ目の位置で −57〜−107 dB ほどの差が残る。下の「残る差」）。コアの単体テストは `tests/block_helpers.hpp`（`bsi::worstDb`）と `tests/test_block_sizes.cpp`。

**最初の測定（132 製品中 15 が DIFF）で見つかって直したもの：**
- **アダプター：`max_frames` より長いブロックを渡されると `Shell` の内部バッファ（`max_frames` 個）の外へ書いていた**（valgrind が「確保した 4096 バイトの直後に書き込み」を報告、ヒープが壊れて落ちた）。`process()` は `max_frames` を超えるブロックを割って渡すようにした。
- **GT01 Amp：** 32 サンプルごとの制御値（ゲイン・マスター）を `process()` の中の変数に持っていたので、ブロックが 32 サンプルの区切りで始まらないと、次の区切りまで利得 1 で処理していた（256・512 など 32 の倍数のバッファでは起きず、−5 dB〔ピーク比〕の差が 1・7・509 サンプルで出た）。メンバーに持つようにした。
- **MS06 Master Chain：** Gain match の 3 秒・2 秒の時定数、ランプ、並べ替えのフェード（1 かたまり）、リミッターの Auto release の判定が、**ホストのブロック（256 サンプル未満のとき）ごと**に行われていた。たとえばバッファ 64 では Gain match の時定数が設計の 1/4 になっていた（−46 dB の差）。ストリームの 32 サンプルごとの格子（`controlBlock()`）で決めるようにし、並べ替えのフェードは 256 サンプル固定にした。Gain match の補正は決めた次の 32 サンプル（0.7 ms）かけて直線で入れる。**挙動が変わる点：Auto release の 300 ms は、以前は毎かたまり `set()` が 30 ms に戻していたため実際には働いていなかった。いまは仕様どおり（短い圧縮は 30 ms、100 ms 以上続く圧縮は 300 ms）働く。**
- **MS01・MS02・MS06 のリミッター Auto release：** 「ブロックの最後のゲインが −1 dB より下か」をブロックごとに見ていたので、ブロック長で結果が変わった（MS02 の大きい入力で −34 dB）。`PeakLimiter::setAutoRelease(速い, 遅い)` に移し、1 サンプルごとに判定する（−1 dB より下が 100 ms 続いたら遅いほう）。単体テスト：短い圧縮は 120 ms 未満、500 ms の圧縮のあとは 300 ms 超で戻り、ブロック 1・37・256 で同じ。
- **LV07 Speech AGC：** ゲート・near/far・ゲインをホストのブロックごとに決め、ゲインはブロック内で一定だった（−27〜−45 dB の差）。ストリームの 32 サンプルの格子で決めて、次の 32 サンプルかけて直線で入れるようにした（ゲインの反応は 32 サンプル＝0.7 ms 遅れるが、時定数は秒単位）。
- **VO05 Rider：** 64 サンプルの制御ブロックがホストのブロックの頭から始まっていた（ホストが Ride を動かすと、1・7 サンプルのブロックでランプの長さが変わり −7〜−10 dB の差）。ストリームの格子にした。
- **LV08 Room Noise：** 声の検出器を、STFT のフレームの時刻まででなくホストのブロック全体まで先に進めてからフレームを処理していた（バーストの入力で −23〜−31 dB）。ブロックを STFT のフレームの切れ目で割り、検出器をそこまで進めるようにした（`Stft::toNextFrame()`）。
- **SA05 Exciter：** Auto fill の 100 ms ごとの解析がブロックの終わりで行われ、入力を全部入れてから解析していた。解析の時刻（絶対サンプル）でブロックを割るようにした。
- **LV05 Auto ducker：** キーの判定（64 サンプルごと）の区切りがホストのブロックの頭から始まっていた（サイドチェインのキーで −34 dB）。ストリームの 64 サンプルの格子で、判定は区切りの終わりで次の区切りに効かせる。
- **LV29 Interp Mix：** 通訳の声の検出器をホストのブロック全体まで進めてから、ブロック全体に同じ判定を使っていた（サイドチェインで −28〜−41 dB）。32 サンプルの格子で読むようにした。
- **SA02 Console Sum（直したのは試験のほう）：** 差の原因はブロック長でなく、インスタンスごとの乱数シード（部品公差とノイズ）だった。試験は状態を引き継ぐ（コアの試験は `setSeed`）。`setSeed()` はノイズの乱数も種から始めるようにした（状態を読み込んでも毎回違うノイズ列にならない）。

**残る差（直していない）：** ①パラメータを動かしたときの CS01〜CS03・EQ03〜EQ06 の −57〜−107 dB（同じ構造の CS04・DY11・EQ01・EQ02・EQ07・MS05 も、制御ブロック〔32〜64 サンプル〕がホストのブロックの頭から始まる。係数の補間は時間で進むので音は同じで、補間の更新点がずれるだけ：聞こえる大きさではないと判断）。②GT02・RV04（・ST05）の IR の作り直し：1 回の `process()` 呼び出しで 1 ステップずつ進める作りなので、新しい IR に切り替わる時刻がブロック長で変わる（RV04 の Length を変えてから IR が変わるまで、48 kHz で 17 回の呼び出し＝バッファ 256 で 0.09 秒、1024 で 0.36 秒、4096 で 1.45 秒。実測）。呼び出し 1 回の CPU を一定に保つための作りで、ブロックに比例して仕事を増やすと大きなバッファで 1 回の処理時間が超過するため、このままにした。**測っていないもの：**実際の DAW のバッファ（32〜4096）と、ホストがブロックを割る場合の実機での挙動。

**テール（残響・繰り返し）の長さ（`host_smoke --tails`、全 132 製品の実物の .clap）：** DAW が書き出しやフリーズ（バウンス）で素材の終わりのあとにどれだけ待つかは、プラグインが伝える「テール」（CLAP の tail 拡張、VST3 では clap-wrapper が `getTailSamples` に写す）で決まる。**これまでどの製品も伝えていなかった**ので、テールを使う DAW では、リバーブとディレイの残りが書き出しの終わりで切れた。測り方：0.1 秒のノイズ（約 −20 dBFS RMS）のあと 30 秒の無音を通し、出力が −80 dBFS を下回って最後に超えた時刻を、ノイズの終わりから数える（プラグインが報告する遅延を含む）。既定値と乱数の設定ごとに測り、**プラグインが報告する値が実測以上であること**を確かめる（実測が報告より 0.5 秒を超えて長ければ失敗。ヒス・ハム・クラックルを出し続ける SA01・SA02・SA07・SA08 はテールでないので除く）。**22 製品がテールを報告するようにした**（`sw/tail.hpp`、コアの `tailSeconds()`、アダプターが遅延を足して tail 拡張にする。無限〔自己発振・Freeze〕は INT32_MAX）：DL01〜DL05・LV25（フィードバックの繰り返し：1 周の遅れ × 80 dB 下がるまでの回数。DL02 は 3 つのヘッドが一緒に戻る式、DL04 はタップごとの遅れとピンポン、DL03 はコンパンダーで繰り返しが速く小さくなる式、DL05 は逆再生の最大 3 倍の遅れ）、RV01〜RV03・RV05・RV06・LV24（Decay を RT60 として 80 dB 下がるまで＋プリディレイ。RV01・RV06 の Freeze は無限）、RV04・GT02・ST05（IR の長さ）、GT03（オンのディレイ・リバーブのペダルのテールの和、Bypass all は 0）、CR03（グレインが読み戻せる範囲。Freeze は無限）、CR06（Space のときだけ）、VO07（Plate・Echo の送り）、EQ02（高い Q のバンドの共振）、LV14・LV19（意図して入れた遅れ）。**報告した値は多めで、たとえば既定値のホール（RV01）は実測 1.75 秒に対して 4.2 秒と報告する**（Decay 2.8 秒の 80 dB 分。実測が短いのは、リバーブの出力が入力より小さいため）。報告しない製品（フィルター、EQ、コンプ、歪みなど）の実測は多くが 0.1 秒未満で、0.1〜0.4 秒のものもある（極端な設定の共振：LV09・LV12・DY11・LO02 など）。**測っていないもの：**実際の DAW が tail をどう使うか（Logic・Cubase・Reaper・Bitwig の書き出しでテールが入るか）。

**報告する遅延と実際（`host_smoke`、インパルスを通して最大の出力の位置を見る）：** DAW は報告された遅延で他のトラックを遅らせる（PDC）ので、違うと並列のコピーと櫛形になる。**インパルスに一つの山があり、報告値との差が 8 サンプル（または 2 %）以内の製品が 127 本**。差が出た 5 本は、遅延の報告の間違いではなく**信号経路そのものの遅れ**と思われる：GT01 Amp 19・GT04 Bass Amp 9・SA06 Saturator 12 サンプル（フィルター類の群遅延と思われるが、原因は未調査。報告は 0）、ST05 Phones 24 サンプル（両耳の到達時間差。設計どおり）、MD05 Rotary 32 サンプル（報告は 48：ドップラーの中心の遅れ 1 ms に対し、山の位置がその揺れで前にずれる）。山が一つに決まらない製品（リバーブなど）と、インパルスを通さない製品（RS04・RS07・CR02）は比べていない。差は警告として出し、CI は落とさない。**他のレートでは警告が増える**（v0.14.0 の実測：44.1 kHz で 5 本・96 kHz で 6 本・192 kHz で 8 本。96 kHz で CR01、192 kHz で CR01・GT02・GT05 が加わる）：フィルターの遅れは時間で決まるので、サンプル数はレートに比例して目安の 8 サンプルを超える。オーバーサンプリング（IIR ハーフバンド）の群遅延も報告 0 のまま乗る：CR01 のインパルスの山は、OS 1×／2×／4× で 48 kHz は 4／8／11 サンプル、96 kHz は 10／14／16、192 kHz は 24／29／30 サンプル（仕様書の「周波数で変わる数サンプルの群遅延」の範囲）。

**音声スレッドでメモリを確保しないことの検査（`tests/test_no_alloc.cpp`＝`tools/gen_fuzz.py` が作る、`tests/test_no_alloc_learn.cpp`、`tests/alloc_guard.hpp`・`tests/test_alloc_hook.cpp`）。** これまで「音声スレッドでメモリを確保しない」はルールとして書いてあるだけで、確かめる手段が無かった。単体テストの実行ファイルの `operator new` を置き換え、**ガードが有効なスレッドでの確保を数える**ようにして、**全 132 製品**で「パラメータを 4 つずつ範囲のどこかへ変える（アダプターは音声スレッドで `setParam` を呼ぶ）→ 256 サンプルのブロックを 4 回処理（サイドチェーンのある製品はそれも）→ 書き込みの待ち行列を空にする・`tailSeconds()`・`reset()`」を 60 回、48 kHz と 96 kHz で回して確保の回数を 0 と比べる（`SW_ALLOC_TRACE=1` で最初の確保の呼び出し元の関数が出る）。学習ボタン（DY04・CS02・RV08・CS03・DY10・MS07）の一巡も別に検査する。**最初の実行（6 回）で 12 製品、回数を増やして RV04・ST05 の 2 製品、合わせて 14 製品が違反していた**（同じ部品を使う GT02・EQ02 は検査には出なかったが同じ作りなので一緒に直した。原因別）：**CS04・MS06**＝並び順の番号と順列の変換が `std::vector` を使っていた／**LV05・LV08・LV24・LV29**＝声の検出器（`sw::VoiceDetector`）の作業用の配列を最初の窓で作っていた／**LV03・LV10**＝Mic・Preset を設定するたびにプリセットの `vector` を作っていた／**SA07**＝Era で書き込みの待ち行列に `push_back`／**RV05・RV07**＝部屋の設定を変えるたびに初期反射の `vector` を作っていた／**RV04・ST05**（と同じ部品の GT02）＝`sw::DeferredConvolver` の作業用の配列を最初の計算で作っていた／**EQ08**（と同じ作りの EQ02 の Linear／Natural）＝カーネルの設計のたびに FFT の表と作業用の配列を作っていた。新しく作った DY04・CS02・RV08 の学習（`BleedLearner::finish`）と DY10 の Auto（`CrossoverFinder::analyse`）も同じ違反をしていたのを、テストを書く前に見直しで直した。**直し方：** 固定長の配列に変える／`prepare()`（か構造体の構築）で作業用の配列を作っておく／書き込みの待ち行列は構築時に `reserve`／カーネルの設計は `sw::FirDesigner`（FFT の表と作業用の配列を持ち回る。テンプレートで `std::function` も使わない）。**副産物（カーネルの設計が速くなった）：** ①設計のたびに FFT の表（`cos`・`sin`）と作業用の配列を作り直さなくなった。②**振幅の関数を 1 ビンずつでなく、20 Hz からの 1/96 オクターブの対数の格子で評価して 3 次補間する**（応答は周波数の対数に対してなめらかで、24 バンドを 16k ビンで評価するのが設計のほとんどだった）。**補間が外れるところ**（ノッチ・とても狭いベル）は、区間の真ん中を評価して比べ（対数振幅で 0.01＝約 0.09 dB を超えたら）、その区間だけ 1 ビンずつ評価する。③遅延の位相（4L 点の格子で L/2 サンプルの遅れ＝ビン kk で −π kk/4 の位相）は 8 通りの表、窓は `prepare` で表にした。**精度：** 1 ビンずつ評価した設計と、20 Hz〜0.45 fs で両方が −30 dB より上のところの応答の差が **0.10 dB 以内**（ランダムな 2〜11 バンド・ノッチと Q 25 までのベル・L ＝ 2048／8192・48／96 kHz。単体テスト `test_fir_designer.cpp` は 0.15 dB 以内）。**速さ**（別の処理と同時に動かした参考値）：24 バンド・線形のみで L ＝ 2048：5.5 → 2.7 ms、8192：22.9 → 3.8 ms、最小位相の部分があると 10.7 → 5.6 ms、44.8 → 8.1 ms。結果：**全 132 製品で 0 回**（学習の一巡も 0 回）。**まだ残っていること：** EQ08・EQ02 の Linear でカーネルを再設計する **計算そのもの**は音声スレッドのまま（20 ms に 1 回まで。上の速さで、24 バンド・L ＝ 2048 なら約 3 ms〔最小位相の部分があれば約 6 ms〕、L ＝ 8192 なら 4〜8 ms。バッファが小さい〔128〜256 サンプル＝約 3〜5 ms〕と 1 回の呼び出しで間に合わない可能性が残る）。仕様書どおり別スレッドにするか、数サンプルごとの小さな仕事に分けて進めるのが次の作業。

**データ競合の検査（`tools/host_stress.cpp`＋`tools/stress_tsan.sh`、Linux、ThreadSanitizer 付きのビルド）。** 音声スレッドと窓のスレッドを同じプラグインに同時に当てる：音声側はノイズを 256 サンプルずつ処理しながら、ときどきパラメータのイベント（ホストのオートメーション）を受ける。窓側は `p`（ポーリング）・`b`／`s`／`e`（つまみの操作）・`c …`（ボタン 37 種の呼び出し）を繰り返し送る。**全 132 製品を各 3 秒：130 本は競合なし、2 本で本物のデータ競合が見つかり、直した。** LV23：Export log が読むログ（件数と各行）を音声スレッドが書いているのが非アトミックだった → 件数と各行の項目をアトミックにした（行の途中で秒が変わる 1 行のずれは従来どおり許容）。LV30：ホストのオートメーションが音声スレッドで書くパラメータの表を、録音の開始（メインスレッド）と書き込みスレッドが読んでいた → アトミックにした。直したあと、この 2 本は 10 秒ずつで 0 件。**オーバーサンプリングと Unit を足したあとの全 132 製品を各 8 秒でもう一度：0 件。****テール・`reset()`・壊れた入力の対応を足したあとの全 132 製品を各 3 秒でもう一度（音声側が 1 ブロックごとに 0.4 ％の確率で `reset()` も呼ぶ）：0 件。****今回の変更（EQ05 の Match・EQ08／EQ02 のワーカースレッド・SW Link の参照／キー／共有の設定・IR の作り直しの格子）で変えた製品を、それぞれの口に当たるよう拡張した検査で：**EQ05 は窓のスレッドが参照を送って `match`・`fit` を呼ぶあいだ（40 秒、適用 8 回）、EQ02 は Linear に切り替えて、ほかに LV05・MT05・UT03・EQ08・RV04・ST05・GT02 を各 20 秒：**0 件**（ワーカースレッドが設計するあいだ、音声スレッドが同じ製品のパラメータを書く形も含む）。**検査が効くことの確認：**アダプターのメーターの値をわざと非アトミックにすると TSan が報告して終了コード 66 になる。**限界：**3 秒の間に出会った組み合わせだけ（長時間と、ボタンの引数の全部は未検査）。TSan は実際に起きた並びしか見ない。CI には入れていない（ビルドが長い）。

**データ競合の検査、ホストのメインスレッドを足して（`tools/host_stress.cpp` の 3 本目のスレッド）。** ホストのメインスレッドが音声スレッドの動いている最中にすること：プロジェクト状態の保存と読み込み（保存したものを読み戻す）、パラメータの値と文字の相互変換、遅延・テールの問い合わせ、render モードのオフライン／リアルタイム切り替え（DAW は再生中にもプリセットの切り替えや Undo で状態を読み込む）。全 132 製品を各 3 秒：**129 本は競合なし、3 本で本物のデータ競合が見つかった：SA02・UT01・LV02**（72・4・15 件）。原因は同じ：アダプターの `stateLoad` がプロジェクト状態の「追加ブロック」（`loadExtra`：SA02 の個体差のシード、UT01 の Remember gain、LV02 の固定フィルター）をメインスレッドからコアに直接書き、音声スレッドが同じデータを読み書きしていた（LV02 は `std::vector` の作り直しなので、再生中に状態を読み込むとクラッシュしうる形）。`loadExtra` を持つ残りの 3 製品（LV27・LV30・RV04）は今回の実行では出なかったが、同じ作りだった。直したこと：**追加ブロックを持つコアだけ、アダプターに 3 状態の門（0 空き・1 音声スレッドが `process()` の中・2 メインスレッドが `loadExtra`／`saveExtra` の中）を置いた**。メインスレッドは音声スレッドが外に出るまで待って（`process()` は数 ms）から読み書きし、その間にホストのブロックが来たら**そのブロックは入力のまま通す**（状態を読み込む瞬間の 1 ブロックだけ。状態の読み込みでパラメータが飛ぶ音に重なる）。直したあと、6 製品（SA02・UT01・LV02・LV27・LV30・RV04）を各 6 秒：0 件（ストレス中の音声ブロック 4000〜8400 個、ホストの呼び出し 1.4〜1.9 万回。RV04 だけは TSan の遅さで 20 ブロックしか進まず、この確認は弱い）。**CI に `race` ジョブを足した**（`.github/workflows/build.yml`。ThreadSanitizer 付きで、スレッドをまたいでデータを共有する 17 製品〔EQ02・EQ08 のワーカー、EQ05・LV01・LV05・LO03・MT05・UT03 の SW Link、LV02・LV27・LV30・RV04・SA02・UT01 の追加ブロック、DY04・CS02・RV08 の学習ボタン〕を各 4 秒。ほかのジョブと並行で、全 132 製品は約 1 時間かかるので手で `tools/stress_tsan.sh 3`。まっさらな clone から 2 製品で通ることは手元で確認した。**CI 上での結果：run 180・182 は EQ08 の誤検出〔GCC 11 の TSan と `pthread_cond_clockwait`〕で失敗、直した run 183 で 0 件、VO05・LV29〔SW Link の鍵・ラウドネス〕を足して 19 製品にした run 186 でも 0 件**）。**測っていないもの：** 状態を読み込む瞬間のブロックが通されるときの聴感、`loadExtra` を持たない製品でもコアのデータを直接触る別の主スレッドの入口が無いか（今回の実行では出なかった）。

- **畳み込みの積和ループ（`DeferredConvolver::mac`・`Convolver::convolveInto`）。** 上の「192 kHz の RV04 が 1 コアに収まらない」の原因を callgrind（命令数、`process()` の中だけ）で調べると、**時間の 55 ％が `DeferredConvolver` の複素積和で、1 ビンあたり約 25 命令**だった（`std::complex` の演算子と `std::vector` の添字：コンパイラが SSE2 の 2 並びにできない）。同じ計算を `double*` の `restrict` ポインタで書き直した（足し算の順序は同じ。浮動小数点の結果は変えない）。**RV04（IR 5 秒）：192 kHz で 107.7 % → 31.3 %（3.4 倍）、48 kHz で 10.9 % → 3.7 %（`rv04_prof` という計測用の小さな駆動プログラム、8 秒ぶん）。プラグイン全体の `host_smoke`（同じ環境、ノイズ入力、1 コアに対する割合）：192 kHz で RV04 108.6 % → 34.7 %、ST05 65 % → 28 %、GT02 25 % → 10 %、EQ08 Linear 44 % → 5.5 %。48 kHz で RV04 11 % → 5.7 %、ST05 13 % → 5.1 %、GT02 3.9 % → 3.0 %。**単体テスト 1463 件は変わらず合格（`test_fft` は直接和と 2〜3×10⁻⁶ 以内で比べる）。**192 kHz の最大は RS04 Declick 38 %、MS04 Clipper 36 %、RV04 35 %、RV06 34 %。全製品が 192 kHz で 1 コアの 40 ％以内。**
- **FFT と畳み込みの高速化（`core/include/sw/fft.hpp`・`convolver.hpp`・`deferred_convolver.hpp`・`zl_convolver.hpp`）。** ①`Fft` は最初の 2 段（回転因子が 1 と ∓i）を掛け算なしで行い、以降の段は段ごとの連続した回転因子の表を使う（これまでは `k*step` の飛び飛びの参照と、逆変換の条件分岐が内側のループにあった）。②**`RealFft`（実数の n 点 ↔ n/2+1 本のビン）を追加**：n/2 点の複素 FFT 1 回で済む（これまでは実数の入力を複素 FFT にそのまま入れていた）。`Convolver`・`DeferredConvolver` の入力ブロックの変換、カーネルの分割の変換、出力の逆変換はこれを使う。③`ZeroLatencyConvolver` の直接畳み込み（先頭の 128〜256 タップ）は、足し算が前の足し算を待つ 1 本の鎖だった（コンパイラは浮動小数点の足し算の順序を変えられない）ので、4 本の部分和にした。**効果（`TieredConvolver` 1 本、192 kHz、この環境）：カーネル 1152 タップで 88 → 44 ms/秒、16384 タップで 189 → 109、384000 タップで 427 → 278 ms/秒。** 結果は丸め誤差の範囲で変わる（足す順序が変わる）：全 1369 件の単体テストが合格し、新しい `test_fft`（`Fft` が素朴な DFT と一致、`RealFft` が複素 FFT の半分のスペクトルと一致して逆変換で戻る、`Convolver`・`DeferredConvolver`・`ZeroLatencyConvolver`・`TieredConvolver` が直接の畳み込みと 2〜3×10⁻⁶ 以内で一致〔遅延を含めて〕）が、変更の前の実装でも合格することを確認してから置き換えた。**測っていないもの：実機での CPU、192 kHz の RV04・GT02 が 1 コアに収まるか。**

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

詳しい一覧とチェックは `docs/tasks.md`。主なもの：

- **実機の DAW と実機の Web ビュー（WKWebView・WebView2）での確認**：音と画面とも、クラウド環境の検査（単体テスト・両 validator・`host_smoke`・ブラウザ・TSan・ASan）と CI の範囲まで。項目ごとの手順は `docs/real_host_checklist.md`。
- **RS02 Voice Isolate**（学習済みの音源分離モデルが要る。保留）。IN01〜IN06 の楽器プラグインは作らない（依頼者の決定）。
- **画面**：Linux の画面（X11 への埋め込み）、ウィンドウの拡大縮小（デザインの「100%」）、Blender 描画の素材の追加（DY03 の GR メーター、MS01 のフェーダーなど）、一部の製品の中央の表示（`docs/tasks.md`）。
- **工場出荷のプリセットの中身**（自分の設定の保存・呼び出しは済み）。
- 64 bit 浮動小数での入出力（VST3 validator の情報表示より。32 bit で動作）。
- 直していない小さな差：再生中のオーバーサンプリング倍率の切り替えで 1 回プチッと鳴る。直線位相 FIR のオーバーサンプリングは無い。

## 手早いテスト（開発中）

`./build_tests.sh` は CMake を使わずに単体テストだけを作る。`third_party/doctest.h`（doctest 2.4.11 の単一ヘッダー）を置いてから使う。正式な手順は上の CMake。

### フォントの同梱（`ui/fonts/`、`ui/fonts.css`）

Barlow Condensed（400・500・600・700）、Michroma（400）、Space Mono（400・700）の latin サブセット（woff2、約 156 KB。出典は npm の @fontsource、SIL Open Font License 1.1、ライセンス文は `ui/fonts/LICENSE-*.txt`）。`tools/embed_ui.py` が `data:` URI にして画面の CSS の先頭に入れる（ネットワーク不要、端末にフォントが無くても同じ見た目）。`@font-face` は影の DOM の中では効かないため、ページ本体の `<style>` に置く。これまでプラグインの画面はフォントを読み込んでおらず、端末のフォントに頼っていた（プレビューだけ Google Fonts のリンク）。ネットワークを切ってブラウザで読み込めることを確認（DAW 内の WebView では未確認）。latin 以外の文字（日本語など）は含まない。

### 本物のページの確認（`tools/gui_page_dump.cpp`、`tools/gui_page_check.js`）

プレビュー（`ui/preview.html`）ではなく、プラグインが実際に WebView へ渡す HTML（`gui::page()`：ランタイム＋フォント＋製品のデザイン＋パラメータ表）を書き出し、Chromium に読み込ませる。ネットワークを切り、ホストとの橋渡しを仮の関数に置き換え、本物と同じ引数の `SWHOST.update(値, 遅延, CPU, メーター, スペクトラム, 読み取り値, ステレオ)` を流して、スクリプトのエラー、読み込めたフォント、ホストへ送られたメッセージを確かめる。LV07 で確認（エラーなし、フォント 7 種、つまみ・タイル・中央の表示が動く）。**WKWebView・WebView2 そのものでの確認ではない**。

### DY10・DY11・SA02 の Output が二重に掛かっていた不具合の修正

この 3 製品は、コア自身が Output（dB）を音に掛けているのに、プラグイン層の `kOutputParam` にも Output を指定していて、**共通の枠でもう一度掛かっていた**（Output を +6 dB にすると +12 dB になる。単体テストはコア単体だったので検出されなかった）。`kOutputParam = -1`（コアが掛ける）にして直した。副作用：Auto gain の補正は「コアの出力」を基準にするので、Auto gain が On のとき、この 3 製品の Output の変更は補正で一部打ち消される（共通の枠に Output を置くほかの製品では Output は補正の後に掛かる）。Output の扱いは全製品を機械的に照合した：コアが掛けているのにプラグイン層にも指定している製品は、この 3 つだけだった。

### GT03 の Input つまみの不具合の修正

GT03 は `kInParam` に **Input（±24 dB のゲイン）を指定していた**。共通の枠の「In」はパネルの電源スイッチ（`sw::Shell::setIn`、Off で 10 ms のクロスフェードのあと入力そのまま）で、ゲインではない。そのため **GT03 のプラグインは、Input が 0 dB（既定）のとき In が Off 扱いになり、ボード全体が素通しになっていた**（Input を +0.5 dB より上げると効きだす）。単体テストはコア単体で動かしていたので検出されず、validator は音を見ないので通っていた。修正：`kInParam = -1` にして、Input はコア自身がボードの前で掛けるゲイン（ブロック内でなめらかに変化、0 dB は素通しのビット一致、Bypass all では掛けない。テストあり）に。Output は従来どおり共通の枠。GT03 も共通の Bypass パラメータを持つ。**他に同じ指定をしている製品は無い**（`kInParam` を使っているのは EQ05 の In パラメータだけ）。

### 共通の Bypass パラメータ（パネルの「In」トグルと電源ボタン）

仕様書の共通機能「In（パネルの電源トグル）＝製品内バイパス、ホストのバイパスと連動、遅延は変えず 10 ms のクロスフェード」の部分は、`sw::Shell` に `setIn()`（10 ms のクロスフェード、遅延はそのまま）まであったが、**アダプターが公開しておらず、画面の「In」トグル（43 製品）とヘッダーの電源ボタンは動かなかった**。共通パラメータ `common.bypass`（名前 Bypass、Off／On、既定 Off）を Delta の後ろに足し、`CLAP_PARAM_IS_BYPASS` を付けた（ホストのバイパスと連動。VST3 ラッパーも bypass として扱う）。`P::kInParam` を持つ製品（EQ05 など 2 製品）は自前の In パラメータを使うので足さない（画面は In を反転して電源ボタンに結び付ける）。古い状態の読み込みは従来どおり（足りない分は既定のまま）。画面の立体トグル（`.tog`）は、「In」「Power」＝上が In、2 つの名前の間のトグル（Link／Solo、Sync／Free、Hard／Soft …）は同じ名前の 2 択パラメータ（名前が 1 つだけ一致するときは上＝On）に結び付けた。**対応するパラメータが仕様書にない 4 つ（SA02 Group／Bus、GT05 Lift／Ground、DY02 Limit／Comp、DY01 SC HPF のスイッチ）は、動かない部品を残さないよう非表示（場所は空けたまま）にした**。必要なら仕様のほうにパラメータを足す（保存済みの設定を壊さないため末尾に追加）。

### 画面のボタン：コアを呼ぶもの、まだ無い機能

- **コアのメソッドを呼ぶボタン**（Randomize・Clear・Ring out・Lock filters・Clear live・Learn noise・Flat・Clap sync・Output・Reset・Tap・Lock all・Mark の 12 製品。`ui/actions.json`）は、**共通の画面（デザインなし）にだけ繋がっていて、デザインの画面では動いていなかった**。`tools/gen_skins.py` が名前で `data-call` を付け、ランタイムが `bridge.call` で呼ぶようにした（トグルは画面側で On／Off を持つ）。デザインにないボタン（LV08 Forget、LV21 Arm、LV30 Record、UT01 Remember gain）と DL01 Tap は繋がっていない（DL01 の Tap は仕様書のパラメータ表にもない）。**LV23 の Export log** は、アダプタに GUI スレッドで動く呼び出し（`guiOnGui`／`guiCallGui`）を足し、`Documents/SW AUDIO/lv23-log-<日時>.csv` へ測定ログを書く（時計の列は「ログの先頭＝いまから記録秒数前」で算出。書き出し先の変更はまだできない）。
- **押している間だけ効くボタン**（LV01 Hold to mute＝Mute、LV11 Hold to cough）、パネルの **In ボタン**（DY03 など。Bypass を反転して点灯）、**EQ05 の Bell／Shelf**（上の組が HF Shape、下の組が LF Shape）を結び付けた。仕様書で消えている DY03 の **Auto fade は非表示**、EQ02 の Dynamic・Assist・Unmask、EQ07 の Auto thresh、EQ08 の Analyzer、CS04 の Add module・Save chain、LV03 の Copy・Paste、LV14 の Measure、LV27 の Learn current は機能が無いので薄く表示。
- **値を印刷したチップ**（MS01・MS02 の「Dither off」、MS02 の「ISP 8x」、MT02 の「FFT 4k」など）はクリックで次の選択肢へ（文字も追従）。**MS01 の Character の 4 つのボタン**は Character X／Y の四隅に結び付けた（**設計値**：Transparent＝Clean・Smooth＝(0, 0)、Punch＝Clean・Punch＝(0, 100)、Loud＝Dense・Punch＝(100, 100)、Warm＝Dense・Smooth＝(100, 0)。仕様書は X＝Clean〜Dense、Y＝Smooth〜Punch の 2 軸だけで、4 つの名前との対応は書かれていない）。**Δ で始まるボタン**（RS の「Δ Noise only」など）は共通の Delta に、Δ や Auto gain を持たない製品のそれらは薄く表示。
- パネルの **Δ の隣の「Auto」ボタン**（40 製品）は共通の Auto gain に結び付けた（ヘッダーの Auto gain と同じパラメータ）。
- **機能がまだ製品に無い部品**（拡大率の「100%」、LIVE の Main show・Remote。Low lat は仕様書が定める 11 製品で、2× OS は非線形段のある 21 製品で、Unit A／B／C は 42 製品で、履歴ボタンと Lock は全製品で動くようにした）は、動いているふりをしないよう**薄く表示して「Not available yet」の説明を付けた**（Low lat・2× OS・100%・Unit A／B／C は、押したときに出る説明に理由を書いた：Low lat は「この製品に低遅延モードは無い」、2× OS は「この製品には、オーバーサンプリングを当てる非線形の段が無い」、100% は「ウィンドウの大きさは固定」、Unit は「部品公差はまだ」）（Unit は仕様書が「持たない」とした 4 製品だけが薄いまま）。（プリセットのメニューは以前この扱いだったが、自分の設定の保存・呼び出しが動くようになった。上の「プリセット」の行。）

### 設計の絵に印刷されていた見本の数字

デザインには、測っていない数字が例として印刷されている部品があった（「−11.2 LUFS」「Latency 21.3 ms」など）。実機で測れないものを測ったように見せないため、**画面が値を持つものは本物の値に、持たないものは「—」に替えた**：MS06 の LUFS・TP・LRA は「—」（コアが公開していない）、MS06 の圧縮の曲線は Threshold・Ratio とコアの GR、LV01 の LUFS は「—」、EQ08 の Latency はホストから渡る値、LV04 の「Last limit」はコアのイベント数、UT03 は Mix・Ref の LUFS と Match をコアの値、LV19 の「ms late」はコアの検出値（遅れていなければ in sync）。残っている見本の数字：EQ02 の「400 Hz −3.0 dB」（ホバーの表示）、MT02 の Tilt 4.5 dB・Slope、LV01 のタイルの GR・Limit の数字など（見つけ次第、同じ形で直す）。

### A／B のモーフのスライダー

デザインの A と B の間のスライダー（`.morph`、105 製品）は絵だけだった。ドラッグすると A の設定と B の設定の間を補間する：連続値はそれぞれの目盛り（`ParamSpec` の曲線）の上で線形、段階値は中央で切り替わる。補間は自動化できる製品パラメータだけ（Auto gain・Delta・Bypass は動かさない）。補間の間もスロット自体は変わらない（A／B のボタンはつまみを放した位置に近い方が点灯）。A／B の切り替えはスライダーの位置も動かす。画面側の機能で、DSP の「モーフ」とは別（ホストの 1 本のオートメーションにはならず、個々のパラメータの動きとして書かれる）。

### LIVE 製品の画面の不具合の修正（つまみ・タイル）

LIVE 製品（LV02〜LV30、25 製品）のデザインは、つまみが `.rc`／`.rk`／`.kn`／`.rl`／`.rv`、トグルが `.tile`（`<b>名前</b>` と `.tv` の値）という別の部品で作られており、`tools/gen_skins.py` がそれを拾っていなかった。**画面の絵は出ていたが、つまみとトグルは動かなかった**（STUDIO 側の `.ctl`／`.dk`／`.dc` の部品だけがパラメータに結び付いていた）。`.rc` を `.ctl` と同じに扱い、`.tile` は `<b>` の名前でパラメータに結び付け（値の文字とランプも追従）、ランタイム（`ui/sw-ui.js`）もこの部品に対応した。つまみの結び付け数は 551/559 → 641/651。**DAW 内の実機確認は未実施**（ブラウザの擬似画面で、LV07 のつまみを動かして値が変わること、タイルのオン／オフが切り替わることを確認）。Distance（LV14）・Frames（LV19）はパラメータがない派生値のため、つまみをやめてコアの値の読み取り表示にした。

### コアが測った値を画面へ渡す口（`readouts`）

製品の traits に `static constexpr int kReadouts = N;`（160 まで。`gui::kMaxReadouts`。以前は 16、のち 32。RS05 は波形 2 × 64 点を渡すので広げた。画面へ渡す 1 回あたり約 1 KB。ブロックごとの写しはアトミック 160 個ぶんの置き場で、渡す個数だけ書く）と `static void readouts(const Core&, double* out)` を書くと、アダプタがブロックごとにその値をアトミックへ写し、画面の更新（`SWHOST.update` の 6 番目の引数、`info.readouts`）で渡す。7 番目の引数はステレオスコープ用（相関と L・R の点。全製品に付く）、5 番目はスペクトラム。音声スレッドとの競合を避けるため GUI スレッドはコアを直接読まない。MT01・LV23・MS01・LV06・LV07・DY01〜DY08・DY12・GT03・VO05・MS05・LV22・LV05・LV29・MD02・MD04・LV14・LV19・LV17・LV18 で使用。他の製品（LV06・LV07・UT03・VO05 など）も同じ形で足せる。

### ブランチ `in07-engine` から取り込んだ共有部分の修正

別のセッションが IN07（楽器）のために進めているブランチ `in07-engine` には、**効果プラグインの窓・状態にも効く共有部分の修正**がある。統合のとき衝突しないよう、**①〜③は同じ文面のまま**この版にも入れた（④は RV04 の読み込みを郵便受け方式に書き直したので文面が違う。統合時の衝突はこの版の `loadIr` を採る — 確認の中身は同じ）：①窓へ渡す数値と窓から来る値を、ホストの C ロケールが小数点にカンマを使っていても小数点で扱う（`num()`・`parseNum()`。以前は「0,5」でページが壊れ、値が 0 と読まれる恐れがあった）、②ホストのパラメータ値・保存済みの状態の値が数でない・範囲外のとき、既定値または範囲内に収める（`sanitizeHost`）、③保存済みの状態の個数の上限（65536）と、読み切ってから反映すること（途中で切れた状態は何も変えない）、④RV04 の IR の値（数でなければ無音、±16 まで）とレート（1〜768 kHz の外は 48 kHz）の確認。（ライセンスとデモ無音の部分はこの版には入れていない。）

### 画面の中央の表示（`ui/displays.js`）

デザインの絵はそのまま使い、パラメータと、プラグインが測るレベル（入出力のピーク、約 60 ms ごと）から描き直す。**実際の信号処理の中身ではなく、画面側で再計算した表示**である（誤差あり）。

| 製品 | 表示 | 動き・注意 |
| --- | --- | --- |
| DY08 | 圧縮の特性図、レベル履歴、GR 履歴 | Threshold・Ratio・Knee・Makeup から曲線。GR は入出力のピークの差から出す（コア内部の値ではない） |
| EQ02・EQ07・EQ08 | 周波数特性図 | バンドごとの双二次フィルタ（RBJ）の応答を足して描く（48 kHz 想定、カットは 12 dB/oct の段を重ねる）。ドットのドラッグ＝Freq・Gain、ホイール＝Q、空きを 2 回クリック＝バンド追加、ドットを 2 回クリック＝削除。実際の DSP（EQ08・EQ02 Linear の FIR など）の応答とは、細部が違う |
| LV12 | 31 バンドのフェーダー | 縦のドラッグでゲイン（Edit の Left／Right／Both に従って L・R に書き込み）、ダブルクリックで 0 dB。実際の RTA の重ね表示は、デザインの見本のまま |
| DY10 | 4 バンドの帯とクロスオーバー | 縦線・丸のドラッグで Crossover 1〜3（20 Hz–20 kHz の対数軸）、ダブルクリックで既定値。帯の高さは各バンドの Range（最大の減衰量）。デザインの見本スペクトルは実測ではないため外した |
| DY11 | 6 バンドの帯 | 各バンドの Freq に丸（ドラッグで Freq）、帯の幅は Width（oct）、高さは Range。帯の入れ替わり（クロスオーバー）はない設計 |
| MS01 | フェーダー・メーター・GR 履歴 | デザインの Threshold フェーダーは仕様の Gain（0〜24 dB）に結び付け（名前も Gain に変更）。Ceiling フェーダーと合わせてドラッグ可（ダブルクリックで既定値）。In／Out のバーは入出力ピーク。**GR はコア自身の値**（slow 段＋リミッター、`readouts`）、履歴は約 10 秒。Integrated はコアの値（10 秒の記憶）。Short-term・True peak はコアが公開していないため「—」（Max GR は記録、クリックでリセット） |
| RV06・LV24・RS06 | リバーブの減衰 | Decay（RS06 は Tail length）で −60 dB に落ちる直線（dB 軸で直線）、LV24 は Pre-delay だけ右へ。時間軸は 4 s、長いときは自動で広げる。左の初期反射の棒はデザインの見本のまま。実際の残響の形（高域の減衰差など）ではない |
| RV01 | リバーブの減衰（RT60 とプリディレイ） | Pre-delay の位置から Decay（RT60）まで −60 dB に落ちる直線、RT60 の縦線、時間軸は 4 s から Decay に合わせて広がる。デザインの「High band decay」の線は（Damping の模型が要るため）外した |
| LV10・VO06 | ピッチ／フォルマントのパッド | 点が Pitch（左右）と Formant（上下）。パッド上のドラッグで両方、ダブルクリックで既定値（画面だけで、コアの値は不要） |
| LV17 | GR 履歴 | オレンジ＝コアの `gainReductionDb()`（今回コアに足した読み取り用の値。音は変えない）、灰色＝出力ピークの履歴（約 10 秒） |
| MS02 | トゥルーピークリミッターの Ceiling とレベル | Ceiling の線と文字は Ceiling パラメータ、波形は出力ピークの履歴（鏡像）。デザインの「Inter-sample peaks caught」の印と数はコアが公開していないため外した |
| LV21 | テスト信号の波形 | Signal（Sine は 4 周期、Pink／White は固定の乱数、Sweep は上がる掃引、Polarity は正のクリック）を Level の振幅で描く（周波数・掃引時間の長さは縮尺どおりではない）。Arm・Running の状態は未表示 |
| LV18 | ハンドリングノイズの検出 | 3 つの印にコアの検出数（Plug pop・Wind・Handling。`caught()`）、オフの種類は薄く、検出の瞬間に光る。灰色の面は出力レベルの履歴。「Last 30 s」ではなく起動からの累計（Plosive は画面に印がない） |
| SA08 | ビットクラッシャーの波形 | 2 ms の窓に正弦波 2 周期を、Bits（段数 2^Bits、表示は 64 段まで）と Rate（保持の間隔。48 kHz で 96 点）どおりに量子化・サンプルホールドして描く |
| RV07 | 部屋を上から見た図 | 聞き手（円）と音源（四角）を Distance・Angle（＋が右）どおりに置く。部屋の縦横比・大きさは `products/rv07/rv07.cpp` の `geometry()` と同じ（Room size の Small／Medium／Large、音源が遠いときは同じ縦横比で拡大）。聞き手は後ろの壁から長さの 0.3、幅の真ん中で、正面が上。**部屋の中を押す・ドラッグすると音源がそこへ動き、Distance と Angle が決まる**（EVO バーの「Place the source in the room by dragging」の中身。ドラッグ中の縮尺は押した瞬間で固定）。**デザインは四角を 2 つ描いていたが、製品の音源は 1 つなので 2 つ目は隠した** |
| VO08 | 入力レベルの履歴（直近 約 12 秒）と息の帯 | 灰色の面＝入力のピークレベル、白い線＝出力。ピンクの帯は検出器が「息」と判断していた区間（コアの `breathActive()`。Mark only のときも出る）で、幅が十分あれば「Breath」と書く。デザインの 3 つの例の帯は消して、実際のものだけ描く |
| CR01 | フィルターの周波数特性（20 Hz〜20 kHz）と変調の推移 | 2 極の状態変数フィルターの伝達関数を、Type・Resonance・**いま使っているカットオフ**（変調で動いた値。コアの `cutoffInUse()`）で描く（Notch の谷は −60 dB で止める。Drive の歪みは描かない）。灰色の線は変調量の直近 約 12 秒（コアの `modulator()`）。左上の字は Mod source。C++ のコアで測ったゲイン（Type 4 × Resonance 3 × Cutoff 2 × 周波数 6）と JS の式が一致することを `tests/test_cr01.cpp`・`tests/ui/shapes.test.js` で確認 |
| CR02 | ステップのパッド（16 個 × 3 行） | 1 行目（Repeat）が 16 ステップのパターン（`cr02.step01〜16`）で、パッドをクリックするとオン・オフが変わる。2・3 行目（Reverse・Pitch）は製品ではステップごとの設定ではない（全体の Reverse・Pitch）ので、**オンのステップに対してその設定が有効なら点く読み取り表示**にした（クリックできない）。再生中のステップの印は、コアがその位置を持たないので無い |
| LO03 | 2 つのベルの特性（20 Hz〜2 kHz）と Mono below の線 | Focus のベル（Q 1.2、コアが**いま**かけているゲイン `focusCutDb()` ＝ 尾の引き締めやダッキングで動く）と Mud cut のベル（Q 1.0、−6 dB × Tight）を足した曲線。数字の付いた点を横にドラッグすると Focus（1）・Mud cut（2）の周波数が動く。点線は Mono below の周波数。左上の字は Role の説明に置き換えた。**デザインの例にあった「持ち上げ」はこの製品に無い（カットだけ）ので描かない** |
| LV03・LV04 | レベルの梯子（`.bar`）・GR の棒・ゲートのランプ・数字の箱 | 梯子は測ったピーク（LV03 は出力 L／R、LV04 は入力 L／R と出力 L／R）。LV03：GR の棒はコンプの `compGainDb()`（20 dB で満タン）、ゲートのランプはゲートのゲイン（`gateGainDb()`、−3 dB を超えると Open、それ以下は Closed）。LV04：コアに GR の値が無いので、**GR の棒と Max GR は「入力ピークから出力ピークへの落ち（ピークの差）」**、Max peak は出力ピークの最大（画面を開いてからの値、クリックでリセット）、Overs は制限イベントの数（コアの `totalEvents()`）。LV03 の EQ の小さな曲線は Gain・Freq・Q から描く（下の別の行）。**LV04 の「Last limit」は、コアの最後の制限イベントから**：「いつ」＝コアの『そのイベントの開始から今まで何秒か』（`nowSample()`・`startSample`・`sampleRate()`）を画面の現在時刻から引いた時刻（コアが持つのは経過サンプルだけで、時刻は画面側が時計から出す。新しいイベントが来たときに 1 回だけ時計から決めて固定する）、「GR −3.2 dB」＝そのイベントの最大の減衰、「0.4 s」＝その長さ。イベントが無ければ「No limit events」。**測っていないもの：窓を開く前に起きたイベントの時刻は、開いた時点の『経過サンプル』から出すので、ホストが止まっていた時間ぶんだけ実際より後の時刻になる** |
| LV30 | 録音バッジ・直近 1 分の入力レベル・マーカー・Disk・Format | 左上のバッジは **Rec hh:mm:ss（録音中）／Stopped**（コアの `recording()`・`secondsRecorded()`）で、**クリックで録音の開始・停止**（デザインに Record ボタンが無いため、バッジをボタンにした。フォルダが未選択なら Documents/SW AUDIO）。波形は入力ピークを 0.5 秒ごとに 120 点（60 秒分）流したもの、Mark を押すたびに（コアの `marksMade()`）黄色い三角が付いて流れていく。**Disk free は「OK／LOW／—」に変えた**（コアは空き容量が 200 MB を切ったことしか知らない。デザインの「412 GB」は例だった）。Format はパラメータとホストの実際のレートから組み立て（FLAC を選ぶと「*」と説明。FLAC はまだ書かない） |
| DL01 | LCD（遅延の長さ・テンポ・音符・Ping-pong） | 大きな数字は、コアが**いま使っている遅延**（`timeSeconds()`。Sync On でホストのテンポがあれば音符の長さ、無ければ Time）。「BPM」はホストのテンポ（無ければ「— BPM」）、下の行は Time のつまみが選ぶ音符名（Sync On のとき。Off は「Free」）。**デザインの「L 375  R 250」（左右で別の時間）は製品の動きと違う（Ping-pong でも 2 本の線の時間は同じ）ので、「Ping-pong」の文字に置き換えた** |
| DL01「Tap」 | タップテンポ | **これまで押しても何も起きなかった**（仕様にない部品で、暗くしていた）。**押した間隔の平均（直近 5 回まで、2.5 秒空くとやり直し）を Time（ms、1〜2000）に書き、Sync を Off にする**（Sync On のままだと Time は音符長に寄せられて、叩いた間隔にならない）。コアは関与しない画面側の機能（パラメータを書くだけ。`gen_skins.py` の別名 `{"tap": "Time", "off": "Sync"}`）。ブラウザでの確認（ボタンを 4 回押して Time が叩いた間隔になり Sync が切れる）まで。**測っていないもの：実際のホストでの叩きやすさ（画面の再描画の遅れ）** |
| LV01 | 4 つのステージタイル・IN／GR／OUT の棒・Voice の大きなつまみ | **Voice のつまみが画面の部品として結び付いていなかった（動かなかった）ので結び付けた**（`gen_skins.py` が、`.rk`・`.rl`・`.rv` を並べただけの組を 1 つのつまみとして扱うように。LV04 の大きな Ceiling のつまみも同じ理由で動いていなかった）。タイル：Use × Voice が決める値（コアの `stage()`：ノイズの深さ、EQ の有無、コンプのいまの GR、リミッターの天井）。棒：入力・出力のピーク（測定）と、コンプ＋リミッターのゲイン。**「Stream loudness −14.6 LUFS target −14」は例だった（この製品はラウドネスを測らない）ので「—」にした**。リミッターの天井の表記はデザインの dBTP でなく dBFS（コアは先読みなしのサンプルピークで止める） |
| LV03 の結び付けの修正 | Gate／Comp の Thresh と EQ の Mid、Out フェーダー | **LV03 の「Thresh」（ゲート・コンプ）と「Mid」のつまみが、ラベルの一致で位相反転（Ø）のスイッチに結び付いていた**（`match_knob` が、名前が記号だけで正規化すると空になる Ø を「どのラベルにも前方一致」として拾っていた）。Ø を除外し、LV03 は `skin_aliases.json` で Gate Thresh（4）・Comp Thresh（11）・EQ Mid（7）を指定した。Out のフェーダー（`lv03.out`）は結び付いていなかったので、新しい `data-fader` の部品（縦ドラッグ・Shift で細かく・ダブルクリックで既定値）で結び付けた |
| 値の出る chip／tile | MD02「Mix 50%」、MS02「Link 100%」、LV15「NOM limit 4 mics」、LV27「Fade between 300 ms」、LV04 の Zero／True peak | パラメータとその値を書いた chip／tile が、どのパラメータにも結び付いておらず**画面から触れなかった**（LV04 の Mode、MD02 の Mix、MS02 の Link、LV15 の NOM limit、LV27 の Fade between）。`data-valchip`（連続値は縦ドラッグ・ホイール・ダブルクリック、段階値はクリックで次へ）と、`_valchip` の指定（`skin_aliases.json`）で結び付けた。LV04 の 2 つの chip は Mode の 0／1 に。LV14「Auto align」・LV24「Low CPU」の tile は製品に機能が無いので薄く表示した |
| EVO ラベル（下の帯の「EVO」） | 進化機能のスイッチ | **進化機能のパラメータ（`〜.evo.on` など）が画面のどの部品にも結び付いていない 19 製品**（CR01・CR03・DY05・DY06・EQ03・EQ04・EQ09・GT01・GT04・GT05・MD03・MD06・RV02・RV04・RV08・SA03・SA05・SA08・ST04）では、EVO のラベルをクリックするとそのスイッチが切り替わる（点灯＝On。ツールチップにパラメータ名）。以前はホストのオートメーション欄からしか触れなかった。EVO のラベルが無いデザイン（DY02・DY03・MS01）と、別の部品で結び付いている製品（DY08・EQ08 など）は対象外 |
| VO01・VO03 のスケールの chip | Key の選択と chip の結び付けの修正 | **VO03 の「Scale」chip は Source の選択肢（MIDI／Scale／Fixed）のはずが、同じ名前のパラメータ Scale（Major／Minor）に結び付いていた**（Source が Scale にならず、押すと Major／Minor が切り替わった）ので Source に直した。VO01 の最初の chip「C major」はどこにも結び付いていなかった（常に点灯）ので Scale＝Major に結び付けた。**Key（C〜B）は、どちらのデザインにも操作する部品が無く、ホストのパラメータ一覧からしか変えられなかった**ので、**「Key C」の chip を足した**（クリックで次のキー）。VO03 には「Major／Minor」の chip も足した。VO01 の最初の chip は選んだキーの名前になる（「D major」）。デザインに無い部品の追加は、機能が画面から届かないのを避けるための例外 |
| DY02 の結び付けの修正と、画面の整合チェック（`tools/check_skins.py`、CI の Linux ジョブでも実行） | Peak reduction／Gain のつまみ | **DY02 の「Peak reduction」が Output に、「Gain」が共通の Auto gain（スイッチ）に結び付いていた**（`skin_aliases.json` の指定ミス）ので、Peak reduction＝Level（しきい値）、Gain＝Output に直した。同じ種類の間違いを見つけるため、生成した画面と `ui/specs.json` を突き合わせる `tools/check_skins.py` を作った：①つまみのラベルと結び付いたパラメータの名前に共通の語が無い、②つまみが共通パラメータ（Auto gain・Delta・Bypass）に結び付いている、③同じパラメータに複数のつまみ、④スイッチの文字が別のパラメータの選択肢でもある（VO03 の Scale、LV10 の Robot）、⑤選択肢の chip の文字が別の選択肢を指している、⑥結び付いていない・薄くもない・読み取り表示でもないつまみ。意図した違い（DY01 の Input＝Drive など）は許可表に書いてある。全 132 画面で 0 件 |
| LV09・RS03・RS04・RV08・LV05・MT02・MT03 の見出しの数字 | 例の数字を値に置き換え | LV09・RS03：「Hum at 50 Hz and 7 harmonics」はコアが追っている周波数（`humHz()`。Base が Auto のとき動く）と Harmonics から組み立てる。RS04：「Repaired 128 events」は修復した数（`clicksRepaired()`）。RV08：「Threshold −42 dB」は Threshold（0〜10 ＝ −60〜0 dBFS）から。LV05：Depth の破線と「−12 dB」は Depth に合わせて動く。MT02：「Tilt 4.5 dB」の chip は Slope のパラメータに結び付けた（連続値のドラッグ）、「Compare A」は薄く表示。MT03：「Pause」は画面だけの一時停止（音には関係しない）、「Snapshot」は薄く表示 |
| LV27・LV28 | 例のデータを消して「まだ無い」と表示 | **OBS シーンの表（Opening、Main show …、「4 linked」）、「OBS connected」、「2 devices」、iPad／iPhone の行、URL、QR コードは、デザインの例（偽のデータ）だった**。LV27 に OBS との接続は無く、LV28 にタブレット用サーバーは無い（README の「未実装」のとおり）ので、「OBS: not connected」「No server」と書き、QR コードは薄く、表と端末の行は消して説明文に置き換えた。パラメータのスイッチ・値（Follow scenes、Fade between、Allow control、Require PIN）はそのまま使える |
| LV14 | 遅延のグラフ | 「Main PA」と「Delay speaker」の 2 つの山の間隔と括弧の「12.4 ms」は例だったので、Delay の値に合わせた（対数の目盛り 0〜500 ms。0 のときは重なる）。Distance は、コアの `distanceM()`（Delay × 音速）が来たときに出る |
| CS04 | モジュールのカード 6 枚・下のつまみの列・EQ の曲線 | **これまで画面から触れたのは EQ モジュールだけだった**（デザインが EQ だけを描いていて、他の 5 モジュールのつまみと On／順序の操作は画面に無く、27 個のパラメータのうち 22 個がホストの一覧からしか動かせなかった）。**カードのクリックで下の 5 つのつまみがそのモジュールのもの（Gate：Thresh・Range・Release、EQ：Low・Mid freq・Mid・High・Output、Comp：Thresh・Ratio・Attack・Release・Makeup、Saturate：Drive・Mix、De-ess：Freq・Thresh・Range、Limit：Ceiling・Release）に切り替わり、カードの点が各モジュールの On、カードをドラッグすると順序（720 通りの `Order`）が変わる。**つまみは同じ列を使い、足りないところは隠す（デザインに無い部品の追加だが、機能を画面から届かせるための例外）。EQ の曲線は Low／Mid／High／Output どおり（コアの Low 棚 100 Hz、Mid ベル Q 1、High 棚 10 kHz）、EQ 以外を選ぶと曲線の場所にモジュール名を出す。「Add module」「Save chain」は薄いまま |
| 「All parameters」ボタン（下の EVO の帯の右端。EVO の帯が無い DY01・DY02・DY03・EQ01・MS01 は上の帯） | 全パラメータの一覧と操作 | **デザインに操作する部品が無いパラメータは、これまでホストのパラメータ一覧からしか動かせなかった**（EQ02 の各バンドの Slope・Place・Dynamic、EQ08 の Type・On・FFT の Length、EQ07 の各バンドの On、DY01 の Color・SC HPF、DY09 の帯域ごとの Attack／Sustain、MS06 の EQ・Saturate・Width・Limit の各設定など。コアは持っていて、画面から届かなかった）。**押すと、すべてのパラメータを汎用のつまみ・スイッチ・選択で出す一覧が開く**（名前で絞り込める。バンド／タップ／ボイスのパラメータは id（`eq08.b3.freq` → 「Band 3」）でまとめる。デザインの部品とは同じ値を見せ合う）。デザインに無い部品だが、機能が画面から届かないままにしないための例外。汎用の画面の見た目（`ui/sw-ui.css`）をそのまま使う |
| LV15 | 8 本のマイクの棒（ゲイン・開いているか） | 同じプロセスにある LV15 のインスタンスは互いのレベルを共有している（README の「SW Link の代わり」）ので、各インスタンスが**グループ全員のいまのゲイン**を計算して画面に渡す（コアの `micGainDb(i)`・`micOpen(i)`、棒の高さ＝ゲイン、白＝開いているマイク、灰＝閉じている）。グループにいないマイクは「—」、このインスタンスのマイクには「•」。**デザインの例の数字（−14 dB など）は偽のデータだった**。ホストが別プロセスで動かすとき（SW Link が要る）は、そのプロセス内の分だけが見える |
| 下の帯の「SW Link」のランプ | 緑 → 灰色 | 全デザインの EVO の帯にある「SW Link」の緑のランプは、つながっているように見せていたが、**SW Link はこの版に無い**（README の「未実装」）ので、灰色にしてツールチップに「SW Link is not part of this version」と出した（127 画面） |
| LV03 の EQ の小さな曲線、LIVE デザインの「LIVE 0.0 ms」と「CPU 2%」 | 値に合わせる | LV03 の小さな EQ 曲線は、このストリップのフィルター（HPF のハイパス、100 Hz の低域シェルフ、Mid f のベル（Q 1）、8 kHz の高域シェルフ）どおりに描く。**LIVE デザインの上の帯の「LIVE 0.0 ms」は例の値のままだった**ので、アダプターが報告する遅延（ms）に、下の帯の「CPU 2%」も実測の CPU に置き換えた（LV03 は 1 つの chip に両方） |
| ST03 の位相の波形 | 3 つの操作が何をするかの図 | 100 Hz の正弦波を 30 ms 見せて、白＝入ってきたままのトラック、紫＝Delay（遅れる）・Phase（回転）・Polarity を通したあと。**デザインの破線「Kick out before」（もう一方のマイクの例）は、Auto align をするまで製品が相手の位置を知らないので隠した** |
| LV14「Measure」・ST03「Auto align」 | ボタンが動く | **これまで押しても何も起きなかった**（コアにある測定を呼ぶ口が無かった）。**押すと、プラグイン側が 3 秒（ST03 は 4 秒）分を集め、「準備できた」状態になったのを画面が見て分析を頼み（FFT は音声スレッドでなく画面側のスレッドで走る）、結果（LV14 は Delay、ST03 は Delay・Phase・Polarity）がホストのパラメータとして書かれる。**ボタンの字は Collecting… → Analysing… → Done（LV14 は見つけた ms も）／No clear match → 元に戻る。基準になる信号は 2 つ目（サイドチェイン）の入力に入れる（ツールチップに書いた）。**音声スレッドと画面のスレッドで共有する状態・結果は `sw::CopyAtomic` にした（以前は普通の int・double で、分析が別スレッドから走ると競合した）。ThreadSanitizer 付きで、分析を別スレッドから走らせるテスト（`tests/test_lv14.cpp`）が競合なしで通る。**測っていないもの：ホスト（DAW）の中で画面→プラグイン→パラメータ書き込みまでの全経路を通したこと（コアの単体テストと、プラグイン層の組み込み・ビルドまで）** |
| VO01 | ピッチのグラフ（直近 約 12 秒） | 灰色＝歌い手の音高、ピンク＝補正後の音高（コアが測った値：`measuredSemitones()`／`lastNoteSemitones()`、無声音のところは線が切れる）。縦の行は選んだスケール（Key・Scale。Custom はそのビット）の音で、8 行を一度に出し、音高が窓の外へ出たら音の単位でずらす（行の名前も変わる）。最初に聞こえた音高で窓を合わせる。**Graph（グラフを編集）は未実装なので、Auto と同じ表示（編集はできない）** |
| VO03 | ハーモニーの音高の線（直近 約 12 秒） | 白＝歌い手、ピンク＝選んだボイス（Voice 1〜4 のチップ）、薄いピンク＝選んでいないボイスのうち最初に On のもの（コアの `leadSemitones()`／`voiceSemitones()`）。縦は 24 半音の窓で、歌い手が窓の中心から 7 半音以上離れたら 6 半音単位でずらす。行の名前は無い（デザインのとおり） |
| RV04 | IR の包絡（180 本の棒） | カテゴリの帯域別残響時間（`products/rv04/rv04.cpp` の `kCat`、7 帯域の振幅の 2 乗平均）を、Length（切り落としと最後の 10 ％ の cos² の消え方）・Size（時間の伸縮）・Reverse（時間を反転）・Pre-delay（最初の棒までの空き）どおりに描く。0 dB ＝ いちばん高い棒、幅 60 dB。タイトルは「カテゴリ名、IR の長さ」、右端は Pre-delay ＋ IR の長さ。**初期反射の単発や雑音のばらつきは描かない（なめらかな包絡）。Bar fit（テンポで長さが切り下がる）は画面が拍を知らないので反映しない。Custom（API で読み込んだ IR）は中身が分からないので棒を出さず「Custom IR」と表示** |
| RV04「Load IR」 | Custom の IR を読み込むボタン | **これまで押しても何も起きなかった**（デザインにあるが、画面からコアへ IR を渡す口が無かった。コアの `loadIr()` は API のみ）。**押すとファイルを選び、画面（ウェブビュー）がデコードして（WAV・AIFF・MP3・FLAC・AAC など、30 秒まで）、32 bit float のサンプルを base64 の断片で送り（`irbegin <チャンネル数> <レート>`・`irdata`…・`irend`）、成功したら Category を Custom にする**。ボタンの字は Decoding → Sending 42 % → Reading → IR loaded（失敗は Could not read it）。IR はプロジェクトの状態に保存される（`saveExtra`、これまでどおり）。**Custom のときの棒は、画面が送った IR の包絡**（121 区間の RMS、−50〜0 dB を Length・Size・Reverse・Pre-delay に合わせて並べる）。画面を開き直したときは、IR はコアに残っているが画面は包絡を知らないので「Custom IR」とだけ出す。**コア側：`loadIr()` が音声スレッドの読む配列を直接書き換えていた**（ThreadSanitizer で競合 2 件を確認）ので、**郵便受け方式**（空き／書き込み中／準備完了／受け取り中の 4 状態。音声スレッドはブロックの頭で、IR の作成中でなければ `swap` で取り込む。割り当てなし）にした。プロジェクト保存用の複製は読み込む側が別に持つ（`saveExtra` が音声スレッドの配列を読まない）。別スレッドから読み込みながら処理するテストが競合なしで通る。**測っていないもの：実際のホストでの読み込み、30 秒を超える IR（画面が断る）、コアの 10 秒を超える IR（これまでどおり 10 秒で切る）** |
| SA06 | トランスファー曲線と正弦波の通過後の波形（選択中のバンド） | 左は Band の Type・Shape・Drive・Bias・Mix どおりの入出力特性（`products/sa06/sa06.cpp` の `shapeFn` を JS に移したもの。C++ と JS が同じ 120 点の表で一致することを `tests/test_sa06.cpp` と `tests/ui/shapes.test.js` で確認）、右は振幅 0.9 の正弦波 4 周期を通した波形（灰色が入力、橙が出力。シェイプが足す直流分は音声処理と同じく除く）。Dynamics（演奏の強さで歪みが変わる）と Tone・Output は絵に入れていない |
| ST05 | スピーカーの三角形 | 2 つのスピーカーを Angle（0〜60°、スピーカー間の角度とみなす。距離は図の固定値）で左右対称に置く。Room・Speakers・Head size は反映しない。RV07 の同じ絵は Distance・Angle の意味が仕様から読み取れないため、そのまま |
| GT02 | キャビネットとマイクの位置 | 点は Off axis（0〜90°。中心〜縁）の位置（方向は図の固定）、文字は入る輪の名前（Centre of dust cap／Edge of dust cap／Cone／Surround）と Mic distance。Cab・Mic の種類は絵に出さない |
| DL04・LV25 | ディレイの繰り返しの棒 | DL04 は Tap 1〜6 の On・Time・Level、LV25 は Time の整数倍に Feedback の累乗の高さ。Clock が Tap／MIDI／BPM のときの実際の時間は反映しない（Time の値で描く） |
| LV16 | ゲート／ダッカー | しきい値の線（上下ドラッグで Threshold）、入力ピークの履歴（同じ目盛り）、Open／Closed（Duck では Ducking／Idle）は入力ピークがしきい値を超えたかで判定。Key HPF 通過後の値ではない |
| ST01 | バンドごとのステレオ幅 | 棒の幅が各バンドの Width（左右にドラッグ、ダブルクリックで既定値） |
| MS04 | クリッパーの特性とクリップ波形 | Drive・Ceiling・Knee・Gain match から、`products/ms04` の clipCurve と同じ式で描く（オーバーサンプラーは含まない） |
| MS03 | 4 バンドとクロスオーバー | DY10 と同じ（縦線・丸のドラッグで Crossover 1〜3）。帯の高さと見本スペクトルのうち、スペクトルだけ外した |
| MT02・MD06・LV09・LV08・LV02・LO01・SA05・CR04・RS01 | 出力のスペクトラム（デザインの灰色の面） | **実測**：プラグインが出力の L+R の平均を 4096 サンプルで Hann 窓→FFT し、20 Hz〜20 kHz を対数 64 バンドにまとめて約 50 ms ごとに画面へ渡す（`plugin/clap/gui_spectrum.hpp`。0 dB＝フルスケールの正弦波）。画面で立ち上がり速く・戻りゆっくり平滑化。デザインの他の絵（ハムの線、学習したノイズの面、ノッチの線、ハーモニクスの帯）は見本のまま |
| LV20 | 1/3 オクターブ 31 本のバー＋ピークホールド | 上と同じ実測（64 バンドから 1/3 オクターブごとの最大）。ピークは 1 更新あたり 0.012 ずつ下がる |
| MT03・RS04・RS07 | セルのスペクトラム | 上と同じ実測を、列＝周波数・行＝レベル（−80〜0 dB）のセルの点灯で表す。デザインのセルの色は保つ。RS04・RS07 の白い枠（修復範囲）は見本のまま |
| MT01・LV23 | ラウドネスメーター | **コアの測った値そのもの**（`readouts` トレイト：Momentary・Short-term・Integrated・Range・True peak・Target・差・帯の内外、LV23 は Dead air・TP over も）。M・S・I の 3 本のバー（−36〜−12 LUFS、白線は Target）、巨大な数字、差の色（帯内＝緑、3 LU 超＝赤）、履歴（Short-term を 1 秒ごとに溜める。画面を閉じると消える。最大 10 分）。音がまだ無い間は「—」 |
| LV06 | ストリーム用マスターの表示 | コアが測るのは**入力**の 3 秒ラウドネスだけなので、デザインの Integrated／True peak／Range は出さず、大きな数字を「Short-term (input)」に、小さな欄を Auto gain・Limiter（GR）・Output ≈（入力＋Auto gain の見積もり）に替えた。Auto level バーは Auto gain（±6 dB）、履歴は Short-term（最大 10 分、画面を閉じると消える）、差は入力と Target の差 |
| LV07 | スピーチレベラーの入出力ライン | コアが測る入力の 400 ms ラウドネスを灰色、白は「入力＋適用ゲイン」（見積もり）、破線は Target。約 20 秒 |
| MT04・ST01・UT02・LV26 | ステレオスコープ（ひし形の点） | **実測**：出力の L・R の直近 160 サンプル（8 サンプルおき）を点にする（上＝L と R が同じ、横＝差。古い点ほど薄い）。MT04 の相関バーは直近 2048 サンプルの相関（−1〜+1）、Zoom は点の拡大（1〜8x）。Persistence（残像の長さ）は未対応 |
| DY01・DY02・DY03・DY06・DY08（と DY05・DY07・DY12 の表示） | ゲインリダクション（針・バー・履歴） | **コア自身の値**（`readouts`：`gainReductionDb()`）。それまでの「入力ピーク−出力ピーク」の見積もりは、コアの値が来ない場合（プレビューなど）だけ使う |
| DY05 | ディエッサー | 出力のスペクトラム（1〜20 kHz）、検出帯（ドラッグで Freq。帯は Freq の ÷1.3〜×1.3 の見積もり）、しきい値の線（ドラッグ）、GR バーと Reduction の数字（コアの値）、帯内の最大点の丸 |
| MS05・VO05 | ゲインライダー | オレンジ＝ライドの履歴（コアの `rideDb()`、12 秒。縦軸は ±Range、最小 ±6 dB）、灰色＝出力ピークの履歴。VO05 の Music 欄はコアの `listening()`（Listening／Not listening） |
| GT03 | チューナーの読み取り表示 | コアの `tunerHz／tunerNote／tunerCents`（例 A2 +6¢ 110.4 Hz。音が無いと「—」） |
| LV22 | 極性ゲージ | 針はコアの相関（−1〜+1、左端〜右端）、左上に r と遅れ（ms） |
| UT03 | 2 段の波形（A＝あなたのミックス、B／C＝参照曲）・ループ区間・読み込みボタン | **デザインの 2 つの波形と「Loop: chorus」は例だった**ので実測に置き換えた。A の段＝入力のピークレベルの履歴（直近 約 12 秒）。**B／C の段＝読み込んだ参照曲の概観**（100 ms 単位ではなく 121 区間の RMS、−50〜0 dB。画面が読み込んだときに作る）。コアが持つ長さ（`referenceSeconds()`）で、画面を閉じて開き直したときも「読み込み済み」と分かる（このときファイル名と概観は画面が知らないので、薄い帯と「Loaded (the file name is not known to this window)」）。**ループ区間はコアの値**（`regionOf()`。Intro・Verse・Chorus・Custom に合わせた範囲）を長さに対して明るい枠で重ね、「Loop: chorus」の字も選んだ Loop に合わせる。**「Load B …」（Source が C のときは C）と「Clear」のボタンを足した**（デザインに無い部品。参照曲を読み込む手段が他に無いため）。**Loop が Custom のとき、下の段をドラッグして区間を選べる**（これまで Custom の区間を決める画面の手段が無かった。`looprange <始まり> <終わり>`（秒）→ コアの `setLoopRegion()`。区間はプロジェクトに保存されない＝参照曲と同じ）。読み込み中は「Decoding … → Sending … 42 % → Reading …」、失敗は「Could not load: …」「The plug-in could not read this file」。Mix・Ref の LUFS 表示は従来どおりコアの値 |
| DL05 | グレインの散らばり | **デザインの 45 個の粒（紫の丸い帯）と灰色の波形は例だった**ので、実測に置き換えた。横軸＝録音（右が今、左が 2 × Time だけ前。Reverse の区間は最大 2 × Time 前から読むため。Freeze 中は右が止めた瞬間）、灰色の面＝その軸に並べた入力のピークレベル。**帯＝コアが今読んでいる粒の位置**（`grains()`：4 つの枠それぞれの「どれだけ前を読んでいるか」「速さ（向き × Pitch の倍率）」「窓のどこか」「読む長さ」）で、濃さは窓（ハン窓）の山、矢印は向き。直近 約 3 秒ぶんは薄くなりながら残る（Random なら散らばり、Reverse なら一方向への流れが見える）。テストあり（Forward は 1 つの Time ぶん前を速さ +1 で、Reverse は −1 で、Pitch +12 は ±2 で、Freeze 中は止めた 1 つの Time の中を読む）。**測っていないもの：DAW でテンポが動くときの見え方** |
| CR03 | グレインの散らばり | **デザインの 45 個の粒と灰色の波形は例だった**ので、実測に置き換えた。**横軸＝入力のどれだけ前を読んでいるか**（右が今、左が 2 秒前）、**縦軸＝その粒の音程**（速さの半音換算 −24〜+24。Pitch でまとまって上下し、Harmony On なら構成音の高さに並ぶ）、帯の幅＝読む長さ、濃さ＝窓、矢印＝逆再生（Glitch）。灰色の面＝同じ軸に並べた入力のピークレベル。右上は鳴っている粒の数。コアの `grains()` が最大 24 粒ぶんを渡す（1 粒 5 値。パンも渡すが画面ではまだ使っていない）。直近 約 1.5 秒ぶんは薄くなりながら残る。テストあり（Pitch +7 で速さ 2^(7/12)・読む長さ 0.1 秒 × 速さ・Mono でパン 0、Wide でパンが −1〜+1 に広がる、Scatter が 1 秒以上前まで届く、Glitch に逆再生が混ざる） |
| RS05 | クリップ波形の例 | **デザインの波形（灰色＝クリップした波形、緑＝復元した波形）は静的な例だった**ので、実測に置き換えた。コアの `scope()` が、**出ていった直近 1024 サンプル（約 21 ms @48 kHz）のチャンネル 0 を 64 点に**まとめ（各点は 16 サンプルのうち最も大きいサンプル）、灰色＝入ってきたそのまま、緑＝修復後（Makeup の前）。点線はコアが今使っている天井（Threshold、または Detect が信号から読んだ値）。右上に **復元した山の数（`runsRestored()`）・天井（dBFS）・窓の長さ（ms）**。クリップしていない入力では 2 本が重なる。テストあり（クリップした 120 Hz の正弦波で、入力側は天井で平ら、出力側は天井より上に出る）。**測っていないもの：実機の素材での見え方** |
| MT02「Compare A」 | 参照カーブとの比較 | **これまで押しても何も起きなかった**（暗くしていた）。**押すと、そのときの測定スペクトルの長時間平均（約 3 秒）を破線の参照カーブとして残し、色の面は今の信号を出し続ける。もう一度押すと消える。**コアには `captureReference()`・`compareDb()`（自前の FFT の各ビンで）があるが、画面のスペクトルは全製品共通の 64 バンドなので、参照は画面側で持つ（プロジェクトには保存されない。ジャンルの標準カーブのデータは仕様書の未決のまま）。ブラウザのプレビューで、押す→破線が出る→もう一度で消えるまで確認。**測っていないもの：実際の曲での使い心地** |
| MS06 | 上の行の「−14.2 LUFS・TP −1.0・LRA 6.1」 | **これまでは「— LUFS」「TP —」「LRA —」の固定表示だった**（コアが測っていなかった）。**コアがチェーンの出力を測る**ようにした：ショートターム・ラウドネス（K 特性、3 秒、`outShortTermLufs()`）・トゥルーピーク（4 倍補間、`outTruePeakDb()`、開始またはリセットからの最大）・ラウドネスレンジ（EBU Tech 3342、`outRangeLu()`。3 秒たってから 100 ms ごとの値を集め、相対ゲート −20 LU の上の 95 − 10 パーセンタイル）。画面の 3 つの字を押すと測り直す（`resetmeters`）。LRA は 1 つのレベルの音なら 0（「LRA —」）。ラウドネスレンジの部品は `sw::LoudnessRange` として `core/include/sw/loudness.hpp` に出し、MT01 もそれを使う（MT01 の計算と同じ式を移しただけ。試験は変えずに通る）。テストあり（出力のラウドネスが入力に基準メーターを当てた値と ±0.3 dB、−3.01 dBFS のピークの正弦波でトゥルーピーク ±0.15 dB、無音は何も出さない、一定の音は LRA < 0.5、10 LU 離れた 2 つのレベルで約 10 LU）。**測っていないもの：実際の曲での値の妥当性（MT01 との比較）、CPU の増え分（K 特性と 4 倍補間が 2 チャンネルぶん増えた）** |
| EQ02「Dynamic」と下の小さなつまみ | 選んだバンドのダイナミック EQ | **これまで画面から届かなかった**（「Dynamic」は暗く、小さなつまみは「Range off」の例の字で、どのパラメータにも結び付いていなかった。ダイナミック EQ の `Dyn Range` はホストの一覧か「All parameters」からだけ動かせた）。**選んだバンドの `Dyn Range`（−24〜+24 dB、0 ＝ ふつうの EQ）に結び付けた**：「Dynamic」を押すと 0 ↔ −6 dB（点灯＝ダイナミック）、小さなつまみは値を決める（0 のとき「Range off」、そのほかは「Range −6 dB」）。バンドを切り替えるとそのバンドの値を指す。`Dyn Thresh` は引き続き「All parameters」から。**「Assist」「Unmask」は暗いまま**（マスキングの補助で、他トラックの解析が要る） |
| EQ08「Analyzer」 | EQ の曲線の後ろのスペクトラム | **これまで暗く、押しても何も起きなかった。押すと、測定した出力のスペクトラム（全製品共通の 64 バンド、20 Hz〜20 kHz）を、EQ の曲線の後ろに薄い面で出す／隠す**（押すまでは出さない。画面だけの設定で保存されない）。縦は −90〜0 dB を窓の高さに対応させた（EQ のゲインの目盛りとは別のスケール）。**測っていないもの：実際のホストでの見え方** |
| LV03「Copy」「Paste」・CS04「Save chain」 | 設定の持ち運び | **これまで暗く、押しても何も起きなかった**。**LV03：Copy でこの製品の設定（パラメータ id=値の列）をプラグインが覚え、同じ製品の別の窓（別チャンネル）の Paste で呼び出す**（ホストの 1 プロセスの中で、製品ごとに 1 つ。プロジェクトを閉じると消える。製品が違えば別。コピーしていなければ「Nothing copied yet」）。**CS04：Save chain はプリセットメニューを開き、名前の欄に移す**（チェーンの設定＝この製品の設定なので、プリセットの保存と同じ。ファイルは Documents/SW AUDIO/Presets/CS04）。「Add module」は暗いまま（モジュールは仕様書どおり 6 つで、足すものが無い。ツールチップにそう書いた） |
| LV18 | 「Caught today 3」の箱 | **「3」はデザインの例の固定値だった**（3 つの印の数が動いても変わらなかった）。**3 種類（Plug pop・Wind・Handling）の合計に置き換え、字は「Caught」に変えた**（コアが数えているのはプラグインが始まってからで、「today」ではない）。**箱を押すと数え直す**（コアの `resetCounts()`、`resetcounts`。ツールチップに書いた）。 |
| EQ02「+」・MS01「Link 100%」「Gain match」 | バンドを足す・値の箱 | **EQ02「+」：最初の Off のバンドを On にして、そのバンドを選ぶ**（これまで押すと「100 番目のバンド」＝最後のバンドを選んでいた。**生成の誤り：ズームの「100%」が数字の字としてバンド番号に結び付いていた**ので直し、「100%」は暗い表示に戻した）。デザインのバンド選択ボタンは 5 つだが、コアは 24 バンドで、**選択ボタンの無いバンドも「+」とグラフの点のクリックで選べるようにした**。**MS01「Link 100%」はパラメータ Stereo の値の箱**（ドラッグで変える）、**「Gain match」は共通の Auto gain の切り替え**。 |
| ST01 の「Low／Low mid／High mid／High」「Correlation」・LV05 の Key の選択 | 表示の切り替え・文字 | **ST01：4 つのバンドの札は、そのバンドの棒を目立たせ（ほかは暗く。もう一度押すと全部）、「Correlation」は右下のステレオスコープを出す／隠す**（これまで点灯しているだけで何もしなかった。画面だけの設定で保存されない）。**LV05：Key の「LV01 Voice, MC mic」は SW Link で選ぶインスタンスの選択ボタン**で、SW Link のキーを作ったので**生きたドロップダウンにした**（下の「LV05 の Key（SW Link）」。以前は「Sidechain input」に変えて暗くしていた） |
| LIVE 製品（30 製品）の「Lock」 | 画面のロック | **これまで暗く、押しても何も起きなかった。押すと、ヘッダーより下を透明なシートで覆い、つまみ・ボタンへの操作を受け付けなくする**（本番中の誤タッチ対策。もう一度押すと外れる。**ホストのオートメーションは今までどおりパラメータを動かす**。画面を閉じて開き直すと外れる＝保存しない）。「Main show」「Remote」は SW Link／タブレット接続待ちで暗いまま。 |
| EQ07「Auto thresh」 | しきい値の自動学習（進化機能・区分 B） | **これまで暗く、押しても何も起きなかった（コアに学習が無かった）。仕様書どおり実装した**：押すと **5 秒間聴き、ダイナミックなバンド（On・ゲインのある型・Range が 0 でない）ごとに検出レベルの分布（16 サンプルごと、1 dB の箱）を取り、Range が負なら 80 パーセンタイル、正なら 20 パーセンタイルを Threshold にする**（−60〜0 dB に収める。Off のバンド、カット・ノッチ、Range 0 は触らない。無音なら −60 dB）。値はコアが自分の設定に入れると同時に、**ホストへパラメータの書き込み（ジェスチャーの開始・値・終了）として渡す**（オートメーションに記録できる。LV14 の Measure などと同じ口 `takeParamWrite`）。聴いている間は、ボタンの字が「Listening 42 %」になる（読み出し値：聴取中・進み）。音は聴いている間も通常どおり処理される。**テスト：** 70 ％が −30 dB・30 ％が −10 dB の 200 Hz（Range −6）と、その逆の配分の 3 kHz（Range +6）で、前者が大きいほうのレベル付近、後者が小さいほうのレベル付近になる／聴いている間は何も書かない／書き込みはバンドの順に 1 回ずつ／無音は −60 dB／触らないバンドは書かれない。ホスト経路の試験（`host_smoke`）で「画面のボタンの呼び出し → 音声スレッド → 聴き始める」まで。**測っていないもの：実際の素材での使い心地（検出レベルは RMS 寄りで、ピークのレベルより数 dB 低く出る）** |
| DY04「Learn」 | 被りと本打ちの学習（進化機能・区分 B。CS02 と共有する処理） | **仕様書は「学習中の立ち上がりをピークレベルとスペクトル重心で 2 群に分け、間にしきい値を置き、狙いの帯域に Key HPF／LPF を合わせる」「学習ボタン」と書くだけで、聴く長さ・検出の条件・周波数の決め方・ボタンの置き場は無かった。デザインの EVO バーにも文があるだけでボタンは無かった。次のとおり決めた（設計値）。** **共通の学習器 `sw::BleedLearner`（`core/include/sw/bleed_learner.hpp`。CS02 の被り学習も使う）：** キー信号（モノ）を 1 サンプルずつ見て、**立ち上がり**＝包絡（アタック 0.3 ms・リリース 20 ms）が **−55 dBFS を超え、かつ時定数約 15 ms の平均の 2.5 倍（8 dB）を超えた点**を数える（測り終えてから 80 ms は次を数えない）。立ち上がりごとに**次の約 21 ms（48 kHz で 1024 サンプル）のピーク**と、Hann 窓の FFT の**スペクトル重心**（100 Hz〜0.45 fs、振幅で重み付け）を測る。止めたとき、**（ピーク 6 dB 刻み、重心のオクターブ）の 2-means** で 2 群に分け、大きいほうが狙いの打撃、小さいほうが被り。**しきい値**＝打撃の下位 10 % と被りの上位 90 % の真ん中（重なるときは両群の平均の真ん中。−80〜−1 dB に収める）。**Key HPF Freq**＝打撃の重心の下位 10 % の 1/4（被りが低いときは、打撃と被りの間＝幾何平均、ただし打撃の 0.8 倍まで）、**Key LPF Freq**＝上位 90 % の 2.5 倍（被りが高いときは間、ただし 1.5 倍まで）。範囲は仕様の 20〜2000 Hz・1〜20 kHz。 **使い方：** 画面の **Learn** を押すと聴き始め（**最長 30 秒、これは案**。時間が来ると自動で止まって適用する）、もう一度押すとそこで止めて適用する。聴いている間もゲートは通常どおり動く。**キーは、外部サイドチェーンがつながっていればそれ、なければ入力（チャンネルの平均）で、キーフィルターの前の信号**。**書き込むのは Threshold・Key HPF Freq・Key LPF Freq の 3 つだけで、Key HPF／LPF のスイッチは入れない**（周波数は画面にノブが無い内部値で、フィルターを使うかどうかは利用者の選択。仕様書の「合わせる」は周波数のこと）。値はコアが自分の設定に入れると同時に、**ホストへパラメータの書き込み（開始・値・終了）として渡す**（EQ07 Auto thresh と同じ口 `takeParamWrite`）。**聴いた結果が使えないとき（立ち上がりが 6 個未満、片方の群が 3 個未満、2 群のピークの平均差が 4 dB 未満＝しきい値で分けられない）は何も書かない**（読み出し値で画面に知らせる。無音・同じ種類・同じレベルは結果なし）。 **画面：** デザインに無いので、`tools/gen_skins.py` が EVO バーの文の隣に **Learn ボタンを足す**（`EVO_BUTTONS`。ツールチップに使い方）。聴いている間は字が「Listening: 12 hits (press to finish)」になって点灯する（読み出し値：聴取中・進み・数えた立ち上がり・直近の結果）。 **テスト：** 合成のドラム（`tests/drums.hpp`：2.5 kHz 付近のスネア 0.6 秒ごと −8 dBFS、8 kHz 以上のハイハットのかぶり −34／−37 dBFS）12 秒で、立ち上がり 58 個を数え（打撃 19・被り 39）、**しきい値 −21 dB（打撃と被りの間）、HPF 1515 Hz、LPF 9748 Hz**。被りが −24 dB なら −16 dB。低い被り（120 Hz のキック）なら HPF が打撃と被りの間（上限 2000 Hz に当たる）。同じ種類・同じレベル・無音は結果なし。ブロックの切り方（256、1〜7 サンプルの不規則、37）が違っても同じ結果。DY04 に通すと、**打撃は −9.5 dBFS 以上で通り（入力は −8 dBFS）、被りは −54 dBFS 未満に下がる**（入力は −34 dBFS＝20 dB 以上の低下。Range は −40 dB。ゲートを開けたままだと被りは −36 dBFS を超える）。ブラウザ（Playwright）で「ボタンがある → 押すと点灯して数が出る → もう一度で戻る」、`host_smoke` で「画面のボタンの呼び出し → 音声スレッドで聴き始める → もう一度で止まる」。 **測っていないもの：実際のドラム素材での使い心地**（合成の信号で通っただけ。実素材では打撃と被りの重なりが大きく、2 群に分けられない／分け方が違う場合がある。その場合は何も書かないか、しきい値が真ん中に来る）。 |
| CS04「Suggest order」 | 並び順の提案（進化機能・区分 A の初期版） | **仕様書：「初期版は音源の種類（声・ボーカル・ドラムバス等）を選ぶと規則表で順番を出す（区分 A）。後期版で音源を自動判別する（区分 C）」。規則表は仕様書になく、デザインの EVO バーには文（「Suggests the best module order for the source」）だけでボタンも無かった（文だけ動かない状態だった）。次のとおり作った（規則表は設計値＝案）。** `tools/gen_skins.py` が EVO バーの文の隣に **「Suggest order」ボタン**を足し、押すと **音源の種類のメニュー**（`ui/displays.js` の `moduleOrderSuggest`）が開く。選ぶと **Order（720 通りの番号）をその種類の並びに 1 回のジェスチャーで書く**（Undo 1 段）。**変えるのは並びだけ**で、各モジュールの On／Off は触らない（使わないモジュールは並びの末尾にまとめてあるので、Off のまま端に寄る）。ツールチップに並びと理由が出る。 **規則表（案）：** 〔Voice（話し声）〕Gate → EQ → De-ess → Comp → Saturate → Limit（強く圧縮するとサ行が持ち上がるので、ディエッサーを先に。ゲートは部屋の音を先に切る）／〔Vocal（歌）〕Gate → EQ → Comp → De-ess → Saturate → Limit（音色を整えてからレベル、コンプが持ち上げたサ行をあとのディエッサーで抑える。色づけとリミッターは最後）／〔Drums〕Gate → EQ → Comp → Saturate → Limit → De-ess（被りを先に切り、音色・たたき・色づけ。ディエッサーは使わないので最後）／〔Bass〕Gate → Comp → EQ → Saturate → Limit → De-ess（先にレベルをならし、音色、倍音。ディエッサーは最後）／〔Mix bus〕EQ → Comp → Saturate → Limit → Gate → De-ess（バスの定番：音色・まとめ・色づけ・リミット。ゲートとディエッサーは普通要らないので最後）。 **テスト：** ブラウザ（押す → メニューに 5 つの種類 → Mix bus を選ぶとモジュールのカードが 「EQ > Comp > Saturate > Limit > Gate > De-ess」に並ぶ → メニューが閉じる → Undo 1 回でもとの並びに戻る。5 つの種類すべてで表どおりの並び）。実際のページ（`gui::page`）でも script エラーなし、Order への書き込み（`s 26 1`＝Drums）を確認。**まだやっていないこと：後期版の音源の自動判別**（ホストのトラック名で選ぶ案は UT01 と同じ仕組みで足せる）と、**実際の素材で並びが良いかの耳での確認**（規則は一般的な慣習に基づく案で、測ったものではない）。 |
| MS07「Truncation check」 | 入力の実効ビット数の確認（仕様書のパラメータ表の「解析ボタン」） | **仕様書は「入力の実効ビット数を調べ、すでに切り捨てられた信号や、量子化後に切り捨てが起きていないかを表示する」とだけで、方法・聴く長さ・表示は無かった。デザインの Test の下の暗いボタンだった（「Not available yet」）。次のとおり作った（設計値）。** **`sw::BitDepthProbe`（`core/include/sw/bit_depth_probe.hpp`）：** 入力の 0 でないサンプル（float）は 24 ビットの仮数を持ち、nビットのファイルをそのままの音量で通したものは 2^(1−n) の倍数になるので、**一番下の立っているビットの位置**から、そのサンプルが何ビットの格子に乗っているかが分かる（n(x)＝25 − 指数 − 仮数の末尾の 0 の数。テスト：16 ビットの格子の −3 dBFS／−30 dBFS の雑音は 16、20・24 ビットも同様）。**信号が 5 秒分（0 でないサンプルのある時刻。完全な無音は数えない）たまるまで聴き、最長 60 秒であきらめる**。**サンプルの 99.9 % が収まる最も粗い格子（8／12／16／20／24 ビット）を答えにし、どれにも収まらなければ「float」**（まれに格子から外れたサンプルがあっても答えは変わらない：0.025 % のずれで 16 ビットのまま、テスト）。**限界（画面のツールチップにも書いた）：** 16 ビットの信号でも、そのあとに音量を変えたり処理したりすると格子は失われ、「float」としか言えない。つまり「ファイルをそのままの音量で通したか」を見分けるもので、途中で処理した信号の過去の切り捨ては見えない。 **使い方：** パネルの **Truncation check** を押す → 「Checking 42 %」→ 終わると字が **「Input: 16 bit grid」／「Input: float, no coarser grid」** になる（聴いている間にもう一度押すと取り消し）。ツールチップが Bits の設定と並べて説明する（例：「Bits は 24。24 ビットのディザーでは前の切り捨ては直らない」「Bits は 16。ここで切り捨てられるので、ディザーはここに要る」）。**音には触らない**（同じ入力で出力が変わらないことをテスト）。「量子化後に切り捨てが起きていないか」は、この段の出力が Bits の格子に乗っているのは作りから明らかなので、画面には出していない（そのあとの段〔書き出しの形式〕での切り捨ては、プラグインからは分からない）。 **テスト：** BitDepthProbe 3、MS07 2、ブラウザ（押す → 「Checking」→ 「Input: 16 bit grid」とツールチップ）、`host_smoke`（呼び出しが音声スレッドに届き、もう一度で取り消し）、`--blocks`、両 validator 不合格 0。**測っていないもの：実際のファイル（16 ビットの WAV を DAW に読んで通した場合など）。DAW が内部でディザーや音量処理を入れていないか**。 |
| EQ05「Match」 | 参照の音色に合わせる（進化機能・区分 B。画面は「EVO バーから起動」） | **仕様書：「参照曲（ファイルのドロップ、または SW Link 経由で UT03 Reference から）と入力の長時間平均スペクトル（1/6 oct）を比べ、差分カーブを各ノブ範囲に収まるよう最小二乗で当てはめて値を書き込む。入力側は 10 秒以上の再生で学習。Undo で戻せる」。デザインの EVO バーには文だけでボタンは無かった。次のとおり作った（設計値）。** **長時間平均スペクトル `sw::BandSpectrum`（`core/include/sw/band_spectrum.hpp`）：** 約 85 ms の Hann 窓を半分ずつずらして FFT（1 サンプルずつ進むので、ホストのブロック長に結果が依存しない：テスト）、**1/6 オクターブ 60 帯域（20 Hz × 2^(b/6)。最後は 20.5 kHz まで）の「FFT ビンあたりの平均パワー」の dB**。−70 dBFS より小さい窓は鳴っていないとして数えない（無音は聴いたことにならない）。再生 10 秒（＝窓の数で数える）で止まる。参照は**そのサンプルレートのまま**解析する（帯域は Hz なので 44.1 kHz と 48 kHz が混ざってよい）。**参照の渡し方：**画面（ウェブビュー）がファイルを開いて（選ぶ、またはウィンドウにドロップ）デコードし、モノにして（左右の平均）float で、base64 の断片（`refbegin <rate>`／`refdata`／`refend`。RV04 の IR と同じ道）で核へ送る。**40 秒より長い曲は、全体に等間隔の 5 秒×8 か所を 20 ms のフェードでつないで送る**（曲全体の平均に近く、転送は 8 MB 弱で済む）。核は 3 秒分の「鳴っている」窓がなければ参照として受け取らない。**読み込みに失敗しても、すでにある参照は残る**（読み込めた回数・失敗した回数を読み取り値で返し、画面が「Loaded」「Could not read it」を出す）。参照は**プロジェクトに保存しない**（再読み込みで選び直し）。**当てはめ：**モデル＝4 バンドが各帯域の中心で作る dB の和（HF・LF は Shelf か Bell をノブの Shape どおりに。アナログ原型を TPT の周波数ゆがみで評価＝`sw/svf.hpp` の説明どおり）。**ノブの範囲（仕様書の範囲）に収まるよう、有界のレーベンバーグ・マルカート法**（パラメータは Gain の dB と、Freq・Q の対数。開始点は周波数を範囲の 20 %／50 %／80 %／両端に振った 4 通り×HF・LF の Shelf／Bell 4 通り＝16 回、最良を採用。同じ当たりなら Shelf を優先）。**全体のレベルは合わせない**（差のカーブから重み付き平均を引く。Output は触らない）。**重み：30 Hz〜16 kHz が 1、その外側 1/6 オクターブで 0（録音の両端は音色でなくコーデックや部屋なので）。**Gain に小さな正則化（0.01×dB）をかけて、重なったベルの縮退を避ける。**今かかっている HPF・LPF はモデルに固定の項として入れる**（かかっている分は差から引くので、HPF を 80 Hz にした入力に Match しても LF シェルフが HPF の代わりをしようとしない：テスト）。**書き込むのは 12 個：HF／HMF／LMF／LF の Gain と Freq、HMF・LMF の Q、HF・LF の Shape。HPF・LPF・Drive・Output は触らない。**Unit A／B／C の公差（±3 %）は見ない。**測定を知っているモデル（設計の理由を測って決めた）：**帯域より狭い低域（約 95 Hz 以下は 1 帯域が 1 ビン〔11.7 Hz〕より狭い）では、`BandSpectrum` は隣り合う 2 ビンの電力から補間して読み、さらに Hann 窓が隣のビンの電力を 1/6 ずつ混ぜる。中心の値だけでモデルを評価すると、急な傾きのあるところで読みと数 dB ずれる（HPF 80 Hz をかけた参照で **LF が +7.6 dB 持ち上がってしまった**）。そこで `BandSpectrum::samplePoints` が「その帯域は平坦なスペクトルのとき何を読むか」を返し（広い帯域は代表のビン 4 つの平均、狭い帯域は補間する 2 ビン×窓の混ざり）、モデルと HPF・LPF の項はそのとおりに評価する。**急な傾き（500 Hz の 18 dB/oct HPF）のかかった雑音の読みとの差：中心で評価 平均 0.22 dB、samplePoints で 0.045 dB（各帯域 0.4 dB 以内：テスト）。** **スレッド：**聴く（`match()`、`process()` の頭で入力のモノ和を `BandSpectrum` に入れる）のは音声スレッドで**確保なし**（テスト）。当てはめ `fit()` は**画面のスレッド**（読み取り値の「聴き終わった」を見て、ページが `fit` を呼ぶ。16 回の最適化で `-O2` の実測 約 105 ms〔入力 14 秒・参照 14 秒。参照の解析が約 60 ms、聴くのは 14 秒の音に 約 0.24 s〕）。結果は音声スレッドが次のブロックの頭でコアに入れ、`takeParamWrite` で 12 個をホストへ渡す（開始・値・終了）。**押し直すと取り消し**（聴いている間）。参照が無いときに押すとファイルを選ぶ窓が開く。 **画面：** EVO バーに **Reference**（ファイルを選ぶ、またはウィンドウにドロップ。読み込めるとファイル名が出る。Shift+クリックで外す）と **Match** を足した（デザインにボタンは無い）。押すと「Listening 42 %」→「Fitting …」→**「Matched」**（ツールチップに「差は 3.1 dB → 0.3 dB」：30 Hz〜16 kHz、レベルを除いた RMS）。**書かれた 12 個は 1 回の Undo で戻る**（`ui/actions.json` の undo）。 **テスト：** `BandSpectrum` 4（白色雑音は平らで音量に従う、傾きが見える・無音は数えない・ブロック長に依らない、上限で同じ窓で止まる、samplePoints の予測）、`EQ05 Match` 4（既知の設定で通した雑音を参照にして、**別の雑音の入力に合わせた EQ が 参照との帯域差 0.16 dB（1 dB 未満）、適用後の差 0.18 dB（適用前 2.7 dB）**〔試験とは別に測った例：低域を持ち上げた傾きの参照で 6.2 dB → 0.14 dB〕、範囲外の目標でも値はノブの範囲に収まる、参照なし・無音・取り消し、ブロック長 256 と 37 で同じ値、`refclear`、HPF をかけた参照、壊れた・短い・無音の参照を断る・前の参照が残る）、音声スレッドの確保（聴く 12 秒・適用・書き込み＝0 回）、ブラウザ（Reference を選ぶ→名前→Match→「Listening」→「Matched」→12 個の値→Undo で全部戻る→Redo→取り消し→Shift+クリック）、`host_smoke`（**実際の .clap に、参照を断片で送る→壊れた参照は断られ前のが残る→Match→11 秒聴く→`fit`→12 個がパラメータに入る、「LF が +1 dB 以上持ち上がる」〔低域に傾いた参照〕**）、`--blocks`・`--reset`・`--tails`、両 validator 不合格 0、ThreadSanitizer（窓のスレッドが参照を送って `match`・`fit` を呼びながら音声スレッドが動く 40 秒：**適用 8 回・競合なし**）、本物のページ（`gui_page_check.js`：エラーなし）。**SW Link 経由で UT03 の参照を使う（仕様どおり。「From UT03」ボタン）：**UT03 は参照を読み込むとき（読み込みの途中の窓ではなく、読み込みを受けたスレッドで）**参照ファイル全体のモノの長期平均スペクトル（`BandSpectrum`、1/6 oct・60 帯域）を計算**して（実測：60 秒分で約 0.13 秒〔別の作業と同時に走らせた VM、雑音の生成込み〕、20 分の曲で約 3 秒。内部の `Ref` に持つ。読み込みを受けたスレッドで行う）、SW Link の登録簿の自分のスロットに出す（`Slot` に `refSerial`＋`refDb[60]` を足した＝登録簿の版を 2 にした。**値を先に書き、番号〔0 でない〕を最後に書く。読む側は番号→値→番号と読み、途中で変わっていたら捨てる**。別スレッドが書き続けるあいだ 2 万回読んで、混ざった値が 0 回：テスト、ThreadSanitizer でも静か）。どの参照を出すかは Source で決まる（B なら B、C なら C、A ならあるほう〔B が先〕）。出すのはアダプターが**ブロックのあとにオーディオスレッドで**行う（原子変数への書き込みだけ。窓を開いていなくても出る）。EQ05 は「From UT03」を押すと（`linkref`）、同じホストプロセスにいる UT03 の参照スペクトルを**ファイルの代わりに**参照にする（`refFromBands`：同じ当てはめ。**同じ雑音から作った参照で、ファイルで渡した場合と同じ値が書かれる**：テスト）。ボタンは、UT03 が参照を持っているあいだだけ点灯し（`info.link[1]`）、いないときは薄く表示してツールチップに理由を出す。UT03 が外れる（破棄される）と参照は消える（`leave()` が番号を 0 に戻す）。**限界：**プラグインを 1 つずつ別プロセスで動かすホストでは見えない（SW Link の仕様どおり）。**版の違う SW AUDIO が混ざると登録簿の版が合わず、互いに見えない**（全製品を同じ版で出す前提）。UT03 の Source が変わると出す参照が変わる（次のブロックで）。**テスト：**`test_swlink`（公開・探す・取り下げ・書き換え・ほかの製品の参照は取らない・自分のは取らない・離れると消える・読み取りの途中で書き換えられても混ざらない）、`test_ut03`（傾いた参照のスペクトル、Source の選び方、新しい参照で番号が変わる、無音は出さない、確保しない）、`test_eq05_match`（スペクトルで渡した参照が同じ値を書く、壊れた値は断る・前の参照が残る）、ブラウザ（薄い・点灯・押すと参照になる・Match が聴く）、`host_smoke`（**実際の UT03 と EQ05 の .clap を同じプロセスに読み込み、UT03 に参照ファイルを断片で送る→EQ05 が見える→取る→Match が聴いて当てはめて低域を持ち上げる→UT03 を外すと消える**。全 132 本を読むと、このあと変更していない古い版の .clap が混ざって登録簿の版が合わなくなるため、手元では 2 本だけのフォルダで確認した。CI は全製品を同じ版でビルドする）。**やっていないこと・測っていないもの：**実際の曲・実際の DAW での聞こえ（試験は雑音と、雑音に EQ をかけたもの。**数値で一致を確かめただけで、聴感の確認はしていない**）。ウィンドウへのドロップが WKWebView／WebView2 で届くか（依頼者の実機待ち。選ぶほうは RV04 と同じ道）。入力が止まっている（再生しない）あいだは「聴く」は進まない。聴き終わってから結果が入るまでは、ホストが `process()` を呼ぶ必要がある（停止中は次の再生まで待つ）。 |
| CS02「Learn」 | ゲートの被り学習（進化機能・区分 B。DY04 と同じ学習器 `sw::BleedLearner`） | **仕様書の文は DY04 と同じで、「学習したキーフィルターの置き場所」が要確認だった（仕様書の結論：キーフィルターは内部値）。** **内部値の置き場：パラメータ表の末尾（Unit の後）に `cs02.gate.keyhpf`（20〜2000 Hz、既定 20 Hz＝使わない）と `cs02.gate.keylpf`（1〜20 kHz、既定 20 kHz＝使わない）を足した**（仕様書の表にない設計値。画面にノブは無い。保存した設定に学習の結果が残り、ホストの記録にも出る。既存の番号は動かしていない）。既定値ではゲートは今までと全く同じ音（テストで、キーフィルターを何も触らない場合と一致）。使うときは、ゲートの検出の前に **2 次の HPF・LPF** をキーにだけ掛ける（音は通さない。DY04 のキーフィルターと同じ作り）。 **Learn の動き：** 画面の **Learn** を押すと聴き始め（最長 30 秒＝案、時間が来ると適用）、もう一度押すと止めて適用する。**聴くのはストリップの入力（入力 HPF／LPF の後、チャンネルの平均）**。結果は **Gate・キーの HPF・キーの LPF の 3 つ**で、DY04 と同じに `takeParamWrite` でホストへ渡す。**学習器はしきい値を dBFS で出すので、CS02 の目盛り（0 dB ＝ −18 dBFS）に直して Gate に入れる**（−30〜+10 dB に収める。−48 dBFS より小さいしきい値は −30 dB で止まる）。**Range は触らない**（0 のままならゲートは効かない。Learn の後に Range を上げる）。聴いた結果が使えないときは何も書かない。 **画面：** DY04 と同じで、`tools/gen_skins.py` が EVO バーの文の隣に Learn ボタンを足す。 **テスト：** 合成のドラム（DY04 と同じ。打撃 −8 dBFS・被り −34／−37 dBFS）12 秒で、**Gate −3.0 dB（＝ −21 dBFS）、キー HPF 1515 Hz、LPF 9748 Hz**（DY04 と同じ学習器なので同じ値）。続けて通すと**打撃は −9.5 dBFS 以上で通り、被りは 15 dB 以上下がる**（Range 30 dB。学習しないと被りは −36 dBFS を超える）。何も聴かない・同じ種類は結果なし。時間切れで自動適用。ブロックの切り方が違っても同じ値。書き戻した新しいストリップが同じ音。ブラウザ、`host_smoke`（ボタンの呼び出しが音声スレッドに届く）、clap-validator・VST3 validator とも不合格 0。**測っていないもの：実際のドラム素材での使い心地**（DY04 と同じ）。 |
| CS03「Set input」 | 入力レベル合わせ（進化機能・区分 A） | **仕様書：「5 秒聴いて、プリ通過後が平均 −18 dBFS RMS・ピーク −6 dBFS 以下になるよう Gain を書き込む。音源の種類ごとに目標を変える」。デザインの EVO バーには文（「Auto gain staging sets the input for each source」）だけでボタンは無かった。** **共通の測り方 `sw::LevelLearner`（`core/include/sw/level_learner.hpp`）：** 入力（Gain の前、左右の電力の平均）を 10 ms ごとに測り、**−60 dBFS 以下の区間は休止として平均から除き**（休みの多い音源でゲインを上げすぎないため。設計値）、鳴っている区間の電力の平均を RMS とする。ピークは左右のサンプルの最大値。**Gain ＝ min（−18 − RMS，−6 − ピーク）**（dB。−30〜+30 dB に収め、目盛り 0〜60 の 30＝0 dB に直す）。0.5 秒分に満たない（ほぼ無音）ときは何も書かない。 **「音源の種類ごとに目標を変える」は、種類の表が仕様書になく、選ぶ画面も無いので、表は作らず、RMS の目標とピークの上限の 2 つのうち厳しいほうが効く形にした**（持続する音源〔ベース〕は −18 dBFS RMS に届き、ピークの鋭い音源〔ドラム〕はピーク −6 dBFS で止まって RMS が低く残る）。種類ごとの数字が決まれば、UT01 のトラック種別などと結んで足せる。 **使い方：** 画面の **Set input**（`tools/gen_skins.py` が EVO バーの文の隣に足す）を押すと **5 秒聴き**（もう一度押すとそこで止めて適用）、終わると Gain を書く。値はコアが自分に入れると同時に `takeParamWrite` でホストへ渡す（オートメーションに記録できる。EQ07 Auto thresh・DY04 Learn と同じ口）。聴いている間もプラグインは通常どおり音を処理し、字は「Listening 42 %」になる。 **「プリ通過後」は、プリの飽和を計算に入れていない（小信号のゲイン 1 の直線として扱う）。** この目標のあたり（RMS −18、ピーク −6 dBFS）でのプリの飽和は小さい（tanh で、高域のヘッドルーム +6 dBFS、低域 +1.6 dBFS）ので、**実測で確かめた：** 合成のベース（55 Hz と倍音）−38 dBFS RMS → Gain +20.0 dB で出力 **−18.05 dBFS RMS**（ピーク −11.7）、−10 dBFS RMS（熱い入力）→ −8.0 dB で同じく −18.05、ドラム状（ノイズの床 −50 dBFS ＋ 減衰する 180 Hz のたたき、ピーク −25 dBFS）→ +19.0 dB で**ピーク −5.89 dBFS・RMS −24.1 dBFS（ピークで止まる）**。 **ピークは「聴いた 5 秒」の最大値が基準**で、そのあとの音がもっと大きなピークを出せば −6 dBFS を超える（ガウスノイズ −30 dBFS RMS で 0〜5 秒を聴いて Gain を決めると、出力の 2〜6 秒のピークは −5.13 dBFS だった）。 **テスト：** LevelLearner 3（定常音・ピークの鋭い音・休止・無音・短すぎる・途中停止）、CS03 3（上の数字、ブロック長 256 と 37 で同じ Gain、無音は何も書かない）、ブラウザ（押す → 「Listening」→ 5 秒で自動に戻る）、`host_smoke`（ボタンの呼び出しが音声スレッドに届く）、両 validator 不合格 0。**測っていないもの：実際の楽器・声での使い心地。** |
| EQ02「Assist」 | 共振の検出（進化機能・区分 B） | **これまで押しても何も起きなかった（コアに解析が無く、設計の「Assist found 2 resonances」は例の文章）。仕様書どおり実装した**（`core/include/sw/resonance.hpp`）：押すと **EQ に入る前の信号（L・R の平均）を聴き**、0.17 秒の Hann 窓の FFT（ホップは 1/4）のパワーを **80 Hz〜20 kHz の 1/24 オクターブごとのセル**に集め、各セルの「出っ張り」＝そのセルのレベル − 周り 9 セル（1/3 オクターブ）の中央値を、**時定数 2 秒で平均**する（続いているものだけが残る）。平均した出っ張りが **6 dB 以上**で、両側 ±2 セルの中で最大、かつ **より強い印の 1/6 オクターブ以内でない**セルが印になる（最大 6 個、強い順。**2 秒分の信号を聴くまでは何も出さない**）。画面：EQ 図の上端に **橙の三角と周波数**が出て（ボタンの点灯は読み出し値＝コアの状態に従い、設計の「点灯したまま」は消した）、**三角を押すとその周波数に Bell（Q 6、ゲイン −0.7×出っ張り、3〜12 dB に収める）を、まだ Off の最初のバンドに置いて選ぶ**（置く値はホストへ通常のジェスチャーとして渡る）。設計の静的な文章（「found 2 resonances」）は出さない。解析は Assist が On の間だけ走る（Off では CPU を使わない）。**テスト（`test_resonance`・`test_eq02`）：** ノイズだけでは印なし／ノイズに 1.5 kHz の持続音を足すと 1 つだけ印が付く（裾に余計な印は付かない）／2 つの持続音（600 Hz と 4 kHz）は両方見つかり、出っ張りの大きい順に並ぶ／0.5 秒では何も出さず、音が止まれば印が消える／44.1・96 kHz、無音、モノ、極端な入力でも有限の値／EQ02 は Assist が Off なら何も返さず、On なら入力（EQ 前）の 2.5 kHz を返し、**聴いても出力は 1 サンプルも変わらない**／On にし直すとやり直し。ホスト経路の試験（`host_smoke`）で「画面のボタンの呼び出し → 音声スレッド → 読み出しの点灯」まで。**測っていないもの：実際の素材（声・楽器・部屋の鳴り）での使い心地（しきい値 6 dB・時定数 2 秒は設計値）。Unmask は次の行** |
| EQ02「Unmask」と EVO バーの「SW Link」ランプ（全製品） | 他のインスタンスとの被り（進化機能・区分 B）／SW Link（共通機能。**最初の部分：誰がいるか・何を鳴らしているか**） | **これまで暗く、Unmask は押しても何も起きなかった（SW Link が無かった）。SW Link の最初の部分を作った**（`plugin/clap/swlink.hpp`）。**仕組み：** 同じホストプロセスにいる SW AUDIO の全インスタンスが 1 つの登録簿（128 スロット）を共有する。製品ごとに別のバイナリなので static では共有できない。そこで**最初に読み込まれた製品が OS から直接メモリを取り（VirtualAlloc／mmap。作ったバイナリがアンロードされても消えない）、そのアドレスを環境変数 `SW_AUDIO_LINK` に「プロセス ID:アドレス」で書く**。あとから読み込まれた製品はそれを読む（プロセス ID が違う値は信用しない＝子プロセスが継承した値で落ちない。登録簿の版・大きさが違うものは参加しない）。**プラグインを 1 つずつ別プロセスで動かすホストでは登録簿が別々になり、互いに見えない**（仕様書どおり。ランプは消灯のまま）。各インスタンスは `init()` で、**画面のスペクトル用にもともと持っている出力のリング（L・R の平均）の「見え方」（サンプル・書き込み位置・大きさ）**をスロットに出す。**オーディオスレッドの仕事は増えない。** 読む側は GUI スレッド（画面のポーリング、50 ms ごと）：スロットを走査して生きているインスタンスを数え、Unmask が頼んだときだけ、他のインスタンスの最後の 4096 サンプルから 64 バンドのスペクトルを作って電力で足す（90 ms ごと）。**持ち主が消えるときの安全：** 読む側はスロットの `readers` に自分を数えてからポインタを見る。持ち主は `leave()` でポインタを取り下げ、読み手が 0 になるのを待ってからリングを壊す（スロットの持ち主が入れ替わったことは乱数 ID で気付いて読み捨てる）。**1.5 秒、書き込み位置が進まないインスタンスは「いない」扱い**（停止中・ホストがバイパス中）。**画面：** 全製品のランプ（EVO バーの「SW Link」）が、他のインスタンスがいるとき点灯し、ツールチップに数が出る（これまでは常に消灯・「この版には無い」）。EQ02 の Unmask は、押している間だけ、他のインスタンスのスペクトルの合計と自分のスペクトルを比べ、**どちらも自分の最大から 12 dB 以内（かつ −70 dBFS 以上）の帯域を EQ 図の裏に赤で塗る**（濃さは、その 12 dB の幅のなかでどれだけ最大に近いか。設計値。仕様書は「両方のエネルギーが高い帯域を重なりとして表示する」）。他に誰もいなければ「no other SW AUDIO plug-in found (a host that runs plug-ins in separate processes cannot connect them)」、登録簿に入れなかったインスタンスは「not connected」と画面に出す。**テスト：** `test_swlink`（登録簿は 1 つで環境変数から見つかる／他プロセスの値・版違いは使わない／2〜4 個のインスタンスが互いを数え、自分は数えず、スペクトルのピークが相手の音の帯域に出る／離れたものは数えない／書き込みが止まったものはタイムアウトで外れる／128 スロット満杯と空き／**別スレッドで読みながら入退場を 300 回繰り返してリングを壊しても ASan・TSan が静か。読み手を待つ行を外した変異体は、ヒープ解放後使用で 3 回中 3 回落ちる**）、`test_gui_bridge`（更新の最後の引数）、ホスト経路の試験（`host_smoke`：**実際の .clap を 3 つ別々に読み込み**、DY08 が 1 kHz を鳴らし EQ02 の画面メッセージに 1 kHz 付近のスペクトルが届く、DY08 を破棄してライブラリをアンロードしても登録簿が生きていて、次に読み込んだ EQ07 と EQ02 が互いを見つける）。**Linux の GCC は、inline 関数の static を読み込んだ全プラグインで 1 つに統合してしまう（macOS・Windows はしない）ので、そのままだと環境変数を使わなくても Linux の試験が通ってしまう。`registry()` を inline でなく `static`（内部リンケージ）にして、バイナリごとに自分で環境変数を見るようにした（登録簿を製品ごとに持つ変異体で試験が落ちること、本物では通ることを確認した）。**〔`-fno-gnu-unique` で全体をビルドする案は取りやめた：モジュールが本当にアンロードされるようになり、Steinberg validator が VO07 の終了時にクラッシュした（CI run 148 で見つかり、ここで再現。原因の詳細は未調査）。〕** **測っていないもの：実際の DAW（トラックごとに別の製品を挿した状態）、macOS・Windows での動き（host_smoke は Linux のみ。コンパイルは CI）、CPU（走査は 128 スロット／50 ms、Unmask は 1 インスタンスにつき 4096 点 FFT 1 回／90 ms の見積もり）。環境変数を書く `setenv` は他のスレッドの `getenv` と同時だとスレッドセーフでない（最初の 1 回だけ）。同時に初めて読み込まれた 2 つが別々の登録簿に入る可能性は残る（その場合は互いが見えないだけで、落ちない）。** SW Link を前提にする他の機能のうち、**EQ05 の Match の参照元（UT03 の参照スペクトル。上の「EQ05『Match』」の行）はつないだ**（登録簿の版は 2）。LV05 の Key 選択、LV15・LV11 の他製品との連携などはまだつないでいない（`sw/link.hpp` ＝同じ製品のインスタンス同士の代用はそのまま） |
| ヘッダーの Undo／Redo と EVO バーの履歴ボタン（時計、全製品） | 操作履歴（共通機能：Undo／Redo 100 段、案） | **これまで、つまみをドラッグした変更は Undo に記録されていなかった**（クリック・ホイール・ボタンだけ。しかもクリック 1 回が 1 段）。**ジェスチャー（begin 〜 end）ごとに 1 段で記録するようにした**：つまみ・フェーダー・EQ の点のドラッグは 1 回につき 1 段（EQ の点は周波数とゲインで 1 段）、ダブルクリックの初期値戻しも 1 段、**プリセットの呼び出し・A/B の切り替え・Init は全パラメータでまとめて 1 段**、モーフのスライダーは 1 回のドラッグで 1 段。ホイールや同じボタンの連打（1 回の動きだけのジェスチャー）は 0.6 秒以内なら 1 段にまとめる（ドラッグは必ず独立した 1 段）。ホストのオートメーションによる変化は記録しない。段数は 100（これまでは 200）。画面ごとの橋渡し（`bridge`）をページ側で包んでジェスチャーを見ているので、各部品の側は変えていない。**履歴ボタン（これまで暗く「Not available yet」だった）：** 押すと、直近の変更を新しい順に並べたリスト（「Threshold: -13.3 dB → -26.7 dB」「Preset」「A / B B」など）が出て、**1 つを押すとその変更の前の状態まで戻る**（その分だけ Undo する。Redo で戻せる）。外を押すと閉じる。**キーボード：Ctrl／Cmd＋Z が Undo、Shift＋Ctrl／Cmd＋Z と Ctrl＋Y が Redo**（画面にキーボードが渡っているとき。DAW がキーを先に取る場合は効かない：実機で確認）。**テスト：** `tests/ui/browser.test.js`（Playwright。CI の Linux ジョブでも実行）：ドラッグ 2 回 → Undo 2 回で 1 回ずつ戻る・Redo・履歴の行から戻る・ダブルクリックが 1 段・ホイール 4 ノッチが 1 段。同じファイルで、132 画面すべてがスクリプトエラーなく開くこと、EQ02 の Assist・Unmask・SW Link ランプ、EQ08 の Low lat、UT01 の行も確認している。**実機の WebView（WKWebView・WebView2）での動きは未確認** |
| UT01 の EVO の 1 行（「Remembers gain staging per track type」）と、ホストのトラック情報（アダプタ共通） | トラックの種類ごとの Gain を覚える（進化機能・区分 A） | **これまでコアには種類分け・記憶があったが、ホストからトラック名を受け取る部分が無く、画面の 1 行は飾りだった。実装した：** アダプタに **CLAP track-info の受け取り**（`trackInfo` トレイト。起動時と、ホストが変更を知らせたとき〔メインスレッド〕に名前とフラグを取得。clap-wrapper が VST3 のチャンネル情報〔IInfoListener〕をこの拡張に写す）。UT01 は名前を Vocal／Drums／Bass／Guitar／Keys／Bus／Other に分類し、**バスやマスターのフラグがあれば名前に関わらず Bus**にする。コアの種類は音声スレッドが読むので `CopyAtomic`。**画面：** デザインにあるのは EVO バーの 1 行の文字だけなので、**その行を操作部にした**：「Vocal track: Gain +12.8 dB remembered (shift-click to use it)」のように種類と覚えた Gain を表示し、**クリックで今の Gain をその種類のものとして覚え、Shift＋クリックで覚えた Gain を Gain のつまみに入れる**（ホストがトラック名を教えないときは「Track kind unknown」と出し、Gain は Other として覚える）。**テスト：** `test_ut01`（種類は教わるまで不明／名前で決まる／バスのフラグ／範囲外は Other／種類ごとに覚えた Gain が分かれる）、ホスト経路の試験（`host_smoke`：試験用ホストが CLAP track-info で「Lead Vocal」を返し、UT01 が Vocal と受け取り、画面のボタン呼び出しで覚える。**起動時の取得を外した変異体で落ちる**）。Playwright で行のクリック・Shift＋クリックの動作を確認。**測っていないもの：実際のホストが返すトラック名（CLAP track-info を実装しているホストは限られる。VST3 の IInfoListener は Cubase 系などが対象）、バス・マスターのフラグ** |
| MIDI 入力（MD05・CR04・VO03・LV25、アダプター共通） | MIDI／フットスイッチ／和音／ノートでの取り込み（進化機能・区分 A〜B） | **これまでプラグインに MIDI 入力が無く、EVO バーの文（「Speed ramps follow a footswitch or MIDI」「Freeze triggers on transients or MIDI」「Harmony follows chords from a MIDI track」）は動かないのに書いてあった。作った：** アダプターに**ノート入力ポートを 1 つ**（CLAP のノートと MIDI の両方。`midi` トレイトを持つ製品だけ）と、イベントの**サンプル位置での**受け取り（ノート on／off、MIDI のノート・コントロールチェンジ・リアルタイムバイト。ベロシティ 0 のノート on はノート off）。VST3 は clap-wrapper がイベント入力バスに写す。AU は MIDI 制御エフェクト（`aumf`）にした。**MD05：** CC64・CC1・ノート 36／37／38 で Speed（上の MD05 の節）。**CR04：** Freeze が On のとき、ノート on で新しい取り込み（どのトリガーモードでも。Auto の立ち上がり検出と同じ 15 ms のクロスフェード）。Freeze が Off のときは何もしない。読み出し：取り込みの回数。**VO03：** Source が MIDI のとき、保持中の和音に追従（上の VO03 の節）。EVO バーの文は、和音があると「MIDI chord: C F A」のようにそのときの音名を表示する（読み出し：保持中の音名のビット）。**LV25：** MIDI クロック（0xF8）を `midiClockTick()` へ（ホストがリアルタイムバイトを渡す場合だけ。多くのホストは渡さない）。**テスト：** コアの単体テスト（MD05：CC・ノートごとの Speed と書き込みが 1 回だけ、ローターが追従する／CR04：Freeze On のときだけ 1 回取り込む／VO03：和音の保持と、F メジャーで D→C・G メジャーで F→G・和音なし・Source Scale では変わらない）、**ホスト経路の試験（`host_smoke`：実際の .clap にノートポートがあり、CLAP のノートと MIDI の両方で、MD05 の Speed がホストの値として変わる、VO03 の和音、CR04 の取り込み。MIDI の取り込みを止めた変異体で落ちる）**、clap-validator は 36 合格（ノートポートの検査が増えた）・Steinberg validator は 47 合格。**測っていないもの：実際の DAW が MIDI を効果プラグインへ送れるか（Logic の MIDI 制御エフェクト、Reaper、Bitwig など。送れないホストもある）、macOS の auval での `aumf`（CI が決める）、実際のペダルやキーボードでの使い心地** |
| EVO バーの「Low lat」（EQ02・EQ07・EQ08・DY05・DY08・MS01・MS02・MS03・MS04・CS04・RS01） | 遅延の出る処理を低遅延版へ切り替える（共通機能） | **これまで暗く、押しても何も起きなかった。仕様書の各製品の章が「Low lat を押すと〜になる」と書いている製品の 11 製品すべてで動くようにした**。**6 製品（EQ02・EQ07・EQ08・DY05・DY08・MS02）は、新しいパラメータは足さず、その製品がもともと持つ設定を書き換えるボタン**（RS01・CS04・MS04・MS03・MS01 は該当する設定が無かったので `rs01.lowlat`・`cs04.lowlat`・`ms04.lowlat`・`ms03.lowlat`・`ms01.lowlat` を足した。下）：EQ02 は Phase＝Zero latency、EQ07 は Spectral＝Off、EQ08 は Phase＝Minimum、DY05 は Lookahead＝0、DY08 は Lookahead＝Off、MS02 は Lookahead＝0.5 ms（最小）。押すとホストへ通常のジェスチャーとして値が渡り（オートメーションに記録でき）、遅延の変化は既存の経路（`latencySamples()` → ホストへ再起動要求）で通知される。**ボタンはそれらの値になっている間だけ点灯する**（EQ02 の既定は最初から Zero latency なので、既定で点灯している）。**もう一度押しても元には戻らない**（元の値は画面の Phase などの選択で戻す。保存しないものを覚えているふりはしない）。**仕様との差：** MS02 の仕様書は「先読み 0.5 ms・IIR 補間で約 24 サンプル」だが、IIR 補間は作っていないので、押した後の遅延は先読み 24＋True peak の補間 16＝約 40 サンプル（@48 kHz。`latencySamples()` の式）。**RS01 は製品のパラメータとして実装した（`rs01.lowlat`、表の最後。Off が既定）：Low lat On で 512 点の窓・ホップ 128・報告遅延 512（Off は 2048 点・ホップ 512・2048）。** 遅延が変わる設定なので、ほかの遅延の設定（EQ02 の Phase など）と同じ仕組み：値を変えるとプラグインがホストに再起動を求め（`host_smoke` で確認：変えただけでは報告値は変わらず、再起動のあとに 512／2048 になる）、`prepare()` で窓が替わる。**1 フレームごとの定数（スペクトルの平滑 0.8、アルゴリズムの a-priori SNR の α、ガードの戻り 0.3、最小値統計の窓）はホップ 512 向けの値なので、ホップ 128 では同じ秒数で効くようにスケールした**（0.8^(ホップ/512) など）。合成した白色ノイズ（−40 dB）での実測：ノイズの低下は Reduction −12 で −11.89（Off）／−12.00（On）、−24 で −21.26（Off）／−24.00（On）。**実際の素材（声・部屋のノイズ）での音質は測っていない（窓が短いので周波数の分解能が粗く、低域のノイズの扱いは Off より荒いはず）。** **CS04 も同じく製品のパラメータ `cs04.lowlat`（表の最後）：Limit が On のとき、先読みが 1 ms（48 サンプル）から 1 サンプル（リミッターの最小。仕様書は「先読みを 0 にする」だが 0 にはできないので報告も 1）になる。** 先読みが無いのでゲインは音の当たった瞬間に下がる（天井は守る：サイン波とクリックの試験で −6 dBFS の天井を超えない。波形は先読みのときより荒れる）。Limit が Off なら遅延は 0 のまま。**MS04 も `ms04.lowlat`（表の最後）：オーバーサンプラーが直線位相 FIR（報告遅延 48）から、最小位相の IIR ハーフバンドを 2・3・4 段重ねたもの（`sw::IirOversampler`。4×・8×・16×）に替わる。** **仕様書は「報告遅延 0」だが 8 サンプルを報告する（仕様との差）：** この IIR は低域を 4×／8×／16× でそれぞれ 7.2／8.4／9.0 サンプル遅らせる（測定。高域ほど増え、16 kHz で 8× は約 19 サンプル）ので、0 と報告すると Mix を 100 % 未満にしたとき原音と並列に混ざって櫛形になる（計算上、8 サンプル遅れの 50 % 混ぜで 3 kHz 付近に谷ができる。聴いては確かめていない）。8 を報告すれば共通の枠が原音を 8 サンプル遅らせて揃える。Listen（取り除いた分だけを聴く）は、IIR の遅れが周波数で違うので、同じハーフバンドを通した「クリップしない原音」との差で計算する（さもないと原音との位相のずれがそのまま聞こえる：最初の試作でそうなった）。**テスト：** 4×・8×・16× の遅延 48 → 8、小さい音は 0.1 dB 以内で素通し、Gain match、天井から +1 dB 以内、16× は 4× よりエイリアスが 1/2 未満、Listen は無音に近い（−35 dB 未満）、`IirOversampler` 単体（直流・1 kHz が 0.05 dB 以内で戻る、遅れが 5〜10 サンプル）、ホスト経路（再起動を求め、再起動のあとに 48 ↔ 8）。**測っていないもの：実際の素材での音（FIR の直線位相のぶん、IIR は波形が非対称にずれる）。** **MS03 も `ms03.lowlat`：帯域ごとの先読み 2 ms とリンク段の先読み 1 ms がどちらも 0.5 ms になる（最終段の 0.5 ms とあわせて遅延 184 → 88 サンプル @48 kHz）。MS01 も `ms01.lowlat`：先読み 2 ms → 0.5 ms（遅延 112 → 40）。仕様との差：仕様書は MS01 の Low lat を「先読み 0.5 ms・IIR 補間で約 24 サンプル」とするが、True peak の補間は FIR のまま（16 サンプル）なので 40。IIR の補間（`PeakLimiter` の検出側の作り替え）は作っていない。** どちらも天井は保たれる（試験：MS03 は Out ceiling が −1 dBTP の +0.1 以内、MS01 は −1 dBFS の +0.05 以内）。**これで仕様書が Low lat を定めている 11 製品（EQ02・EQ07・EQ08・DY05・DY08・MS01・MS02・MS03・MS04・CS04・RS01）すべてで動く。****テスト：** 画面の結び付け（`gen_skins.py` ＋ `check_skins.py`）と、プレビューでボタン → 値 → 点灯の確認。値そのものは各製品の単体テスト（遅延の表）が見ている |
| DY09 | トランジェントの波形 | **デザインの静かな波形と 5 組の帯（オレンジの縦帯と白い箱）は例だった**ので、直近 約 7 秒の実測に置き換えた。灰色の面＝入力のピークレベル（左右対称）、**オレンジ＝Attack が実際に足した・引いた量、白＝Sustain の量**（コアが持つ値。`attackPartDb()`・`sustainPartDb()`・`gainDb()`、約 80 ms ピークを保持して画面の更新間隔で取りこぼさない。Split bands のときは 3 帯域のうち最も大きいもの）。高さは ±15 dB が上限。左上に Attack／Sustain の設定値（Split では Low／Mid／High）、右上に今かかっているゲイン。符号は設定値のほうで見る（量は大きさだけ描く）。**測っていないもの：実際のドラム素材での見え方（試験は合成の減衰音）** |
| LV05・LV29 | ダッキングのゲイン履歴とブロック | コアのゲイン（LV05 は BGM、LV29 は Floor）と、キーの有無（声／通訳の発話）を 30 秒分。デザインの見本の線・ブロックは置き換え |
| MD02・MD04 | LFO の波形 | 直近 1.5 秒（右端が「いま」）。周期は実際に使われている Rate（Sync ではノート長から。コアの `rateHz()`）、振幅は Depth、MD04 は Shape（Sine／Triangle／Square／Ramp）、MD02 はコアの LFO 位相に同期して流れる。Through zero・Feedback の影響は描かない |
| CR05 | テープストップの速度曲線 | Action（Stop＝1−w、Start＝w、Spin back＝1−3w）と Curve（Lin／Exp／Log）から、`products/cr05` と同じ式で描く（時間軸は動作の長さ全体。実際の秒数・いまの位置は出さない） |
| GT03 のペダル列 | 8 スロット、Add pedal、並べ替え | ペダルの列は横にスクロール（スロット 1〜8）。**Add pedal**＝最初の空きスロットを Comp にして見える位置へスクロール。**ペダルの本体をドラッグして別のペダルへ落とす**とスロットの中身（種類・On・A・B・C）を入れ替える（ホストへは個々のパラメータの変更として書かれる） |
| DY03 | GR メーターの針 | デザイン独自の目盛り（右が 0、左が 20 dB）に合わせる。GR は「入力ピーク＋Makeup−出力ピーク」から出す（コア内部の値ではない） |
| DY01・DY02・DY06・MT05 | VU 針 | 目盛りの角度に合わせる。基準は 0 VU ＝ −15 dBFS（ピーク）の設計値、GR は入出力の差 |
| DL02・SA01・MD05 | リール・ホーンとドラムの回転 | 音が通っている間（DL02・SA01）、Speed と Accel のモデル（MD05） |

### 画面の項目を仕様に合わせた箇所（デザインキャンバスとのずれ）

画面は `docs/design/canvas` のデザインをそのまま使うが（`tools/gen_skins.py`）、デザインの項目名・個数が仕様書／04 と合わない箇所は、**仕様と DSP を変えず、画面側を仕様に合わせた**（`ui/skin_aliases.json` の `_edit`）。

| 製品 | デザインの項目 | 画面での扱い |
| --- | --- | --- |
| EQ01 | Low の Freq・Boost・Atten／High の Freq・Boost・Width／High atten の Freq・Atten／Level | 仕様どおり Low（Freq・Gain・Contour）・Air（Freq・Gain・Width）・Drive・Output の 8 つに組み替え。High atten の Freq は削除、Mode（LR/MS）はデザインにないため画面なし |
| EQ05 | Gain・Freq・Q が同名で 4 バンド分 | ページ上の目盛り（1.5k–16k／.6–7k／.2–2.5k／30–450）から HF・HMF・LMF・LF の順と判断して結び付け。HF Shape・LF Shape はデザインにないため画面なし |
| DY01 | Attack・Release | 仕様は Speed 1 つ（アタックとリリースを連動）と Bite のため、Speed・Bite に名前を替えて結び付け。**決定（依頼者の「おまかせ」）**：仕様どおり Speed 1 つ（アタック 800〜20 µs とリリース 1100〜50 ms を連動）とする。理由：DSP・テスト・保存済みの設定に触れず、仕様書の記述（Speed で連動）と一致するため。Attack／Release を別々に動かしたくなった場合は、パラメータを末尾に追加する形で後から足せる |
| DY10・MS03・ST01 | Crossover 1 つ | 仕様の Crossover 1〜3 に合わせて 3 つに複製 |
| MS06 | Mix（Comp の画面） | Comp mix に結び付け |
| GT03 ペダル | デザインは色付きの平らなカード（CSS）で、つまみは 2 つ・飾りだけ | Blender で描いたストンプボックス（`tools/blender/`）に置き換え。6 種類（Comp・Drive・Fuzz・Chorus・Delay・Reverb）で仕上げ・つまみの配置と大きさ・刻印を変え、一目で見分けられるようにした（オリジナルの意匠。実在機材の名前・外観は使っていない）。仕様どおり つまみ A・B・C、フットスイッチ（On）、LED、種類（Pedal k Type。名前のクリックで None→Comp→…→Reverb）を各スロットのパラメータに結び付け |
| DY02 Meter／GT03 Tuner／VO05 Music | パラメータにない表示・切り替え | **決定（依頼者の「おまかせ」）**：パラメータは足さず、つまみをやめて表示用の部品にした（DY02 Meter は GR／+4／+10 の切り替えスイッチ、GT03 Tuner と VO05 Music は読み取り表示）。仕様にない設定を増やすと保存済みの設定と DSP に触れるため。中央の表示を音に連動させる段階で、DY02 はメーターの表示切り替え（GR／+4／+10）、GT03 はチューナー表示、VO05 は音楽の聞き取り状態の表示として結び付ける |
