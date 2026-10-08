# SW AUDIO 有料配布のセキュリティ計画

2026-10-09 作成（依頼者「有料配布するときに必要なセキュリティを考えといて」）。価格・条件は下の出典で 2026-10-09 に確認したもの。変わることがあるので、契約・購入の前に出典をもう一度見る。

## 結論：販売前にやること

| # | やること | 状態 |
| --- | --- | --- |
| 1 | **GitHub リポジトリを非公開にする**（いまは公開。製品の全ソースを誰でもビルドできるので、ライセンス認証を入れても外して配れる） | **あとで**（依頼者 2026-10-09「あとでやる」。販売開始の前に必要） |
| 2 | **コード署名と公証**：macOS は Developer ID 署名＋公証（Apple の規則で、ダウンロードしたプラグインは公証がないとホストに読み込まれない）。Windows は OV のコード署名証明書 | 未着手（アカウントと証明書の取得が要る） |
| 3 | **ライセンス認証**（**買い切り**・決済は **Stripe**・ライセンスがなければ**無音を挟む**：いずれも依頼者の決定 2026-10-09）：署名付きライセンスファイル（Ed25519）をオフラインで検証。有効化の台数はサーバー側で数える。ネットがなくても動く | **実装済み**：検証（`core/src/license.cpp`）、全プラグインへの組み込みと無音（`sw/demo_gate.hpp`）、発行サーバー（`server/license/`、Stripe Webhook・有効化）、署名の道具、テスト。**残り**：本番の鍵、サーバーの設置（Cloudflare・Stripe のアカウント作業）、画面での有効化 |
| 4 | **配布と更新の経路**：HTTPS、インストーラーも署名・公証、SHA-256 の公開、更新情報は署名を確かめる | 未着手 |
| 5 | **プラグイン自体の守り**：共有されるファイル（プリセット・プロジェクトの保存データ）を「信用しない入力」として読む | **実装済み**（下の 6 章。テストと ASan で確認） |

方針：正規の購入者の手間を増やさない（毎回のネット確認やドングルは使わない。LIVE 製品はネットのない現場でも止まってはいけない）。完全なコピー防止は狙わず、「簡単には使い回せない」「改ざんされた配布物を見分けられる」「プラグインが壊れたファイルで落ちない」を目標にする。

## 1. 何から守るか

| 脅威 | 起きること | 対策（章） |
| --- | --- | --- |
| ソースの公開・クラック・キーの共有 | 売上が減る | 非公開化（結論 1）、ライセンス（3 章） |
| 改ざんされたインストーラーの再配布 | 購入者の PC にマルウェア、ブランドの信用が落ちる | 署名・公証（2 章）、配布経路（4 章） |
| 署名鍵・API キーの流出 | 偽物に正規の署名が付く、ライセンスの偽造 | 鍵の保管、CI の守り（5 章） |
| 悪意のあるプリセット・プロジェクトファイル | ホストごと落ちる、異常な大音量（耳・スピーカーの事故）、勝手な場所へのファイル書き込み | 6 章（実装済み） |
| 画面（WebView）へのスクリプト埋め込み | プリセット名などに仕込まれたスクリプトがパラメータを操作する | 6 章の残り（画面を作るとき） |
| 購入者・有効化の情報 | 個人情報の漏えい | 集める情報を最小に（3・4 章） |
| 依存ソフト・CI の乗っ取り | ビルドに不正なコードが混ざる | 5 章 |

## 2. コード署名と公証

**名義：SEVENTHWELL**（依頼者の決定 2026-10-09）。プラグインの発行元（CLAP の vendor、AU の製造元、インストーラー、Web、Stripe の公開事業者名）は SEVENTHWELL にできる。ただし**署名に出る名前は法人かどうかで変わる**：
- Apple：組織（Organization）として登録できるのは契約主体になれる法人だけで、「DBA・架空の事業名・屋号・支店は受け付けない」。個人事業主は個人として登録し、**本人の氏名**が開発者名（Developer ID 証明書と Gatekeeper の表示）になる（Apple「Enrollment」）。
- Windows：OV 証明書は登録された事業体が必要。個人事業主向けは本人の氏名が載る IV／EV（例：SSL.com の個人事業主向け EV は「認証済みの氏名が記載」、年 359 USD〜）。屋号の登記（商号登記）で OV が取れるかは認証局ごとに要確認。
- したがって、SEVENTHWELL が法人でない（個人事業の屋号）なら、署名の発行元は氏名になる。署名の名前も SEVENTHWELL にしたいなら法人化（合同会社など）が要る。製品名・発行元表示・販売ページは屋号のままでよい。

**macOS**（出典：Apple「Notarizing macOS software before distribution」）
- 必要なもの：Apple Developer Program（年 99 USD、Apple の日本語ページの表示は米ドル）、Developer ID Application 証明書（バンドルの署名）と Developer ID Installer 証明書（pkg の署名）。
- 公証の条件（Apple の文書）：すべての実行ファイルに有効な署名、Developer ID 証明書、Hardened Runtime を有効、安全なタイムスタンプ、`get-task-allow` を入れない。
- プラグインは必須：「macOS 10.15 以降、アプリは隔離属性の付いた（ダウンロードした）プラグインを、公証済みのときだけ読み込める」。
- 手順（CI に入れる）：`codesign --force --options runtime --timestamp --sign "Developer ID Application: …"` を .clap・.vst3・.component の各バンドルに → pkg を `productsign` → `xcrun notarytool submit … --wait` → `xcrun stapler staple`。notarytool は App Store Connect の API キーで動かす（キーは CI の承認付き環境だけに置く、5 章）。

**Windows**（出典：Microsoft Learn「Code signing options」、日本からの配布の解説記事）
- Microsoft の Azure Artifact Signing（旧 Trusted Signing、月約 9.99 USD）は、法人は米国・カナダ・EU・英国、個人は米国・カナダだけが対象。**日本の個人・法人は使えない**（2026-10-09 時点の Microsoft の記載）。
- したがって OV のコード署名証明書を買う。2023 年 6 月以降、秘密鍵はハードウェア（FIPS 140-2 Level 2 等）に置くのが必須で、.pfx を CI に置く方法は使えない。CI で署名するなら、証明書会社のクラウド署名（例：DigiCert KeyLocker、SSL.com eSigner）を使う。
- 価格は出典で差がある：Microsoft は OV を「年 150〜300 USD 程度」と書く。日本の解説記事の例は GMO GlobalSign 年 6 万円、DigiCert OV 年 696 USD。購入時に確認する。
- EV 証明書は不要：2024 年から EV でも SmartScreen の警告がすぐには消えなくなった（Microsoft：EV は OV と同じく評判を積み上げる）。署名を続けると発行者の評判がたまり、警告は減っていく。最初の数か月は警告が出る前提で、購入者向けの説明を用意する。
- 2026-03-01 から、公的なコード署名証明書の最長有効期間は 460 日（約 15 か月）。更新を毎年の作業に入れる。
- 署名するもの：.clap（DLL）、.vst3 の中の DLL、インストーラーの exe。`signtool sign /fd sha256 /tr <RFC 3161 のタイムスタンプ URL> /td sha256`。

**Linux**：署名なしが一般的。SHA-256 と、配布物への署名（minisign など）を公開する。

## 3. ライセンス認証

| 方式 | 利用者の手間 | 費用 | 強さ | 判断 |
| --- | --- | --- | --- | --- |
| シリアル番号だけ（形式チェック） | 少ない | ほぼ 0 | 弱い（キー生成器で破られる） | 不採用 |
| **署名付きライセンスファイル**（Ed25519、オフライン検証）＋初回のオンライン有効化 | 少ない（最初の 1 回だけ） | 小さなサーバーの費用 | 中（偽造できない。共有は台数上限で抑える） | **推奨** |
| 定期的なオンライン確認 | ネットが要る | サーバー | 中〜強 | 不採用（ライブ・オフラインのスタジオで止まる） |
| iLok（PACE） | アカウント（とドングル） | Starter：年 2,000 USD＋販売額の 2 %（買い切り）／5 %（サブスク）、最低 1 件 2.50 USD。Standard：年 5,000 USD＋5 %／10 %（PACE の価格ページ） | 強 | 小規模では割高。後で必要になれば検討 |

**売り方：買い切り**（依頼者の決定、2026-10-09「かいきり」）。ライセンスに期限はない。1 本のライセンスは、買った時点のメジャー版（1.x）のすべての更新に使える（ファイルの `major=`）。次のメジャー版（2.0）を有償のアップグレードにするかは、2.0 を出すときに決める（仕組みはどちらにも対応）。台数上限は推奨の 3 台のまま（設計値。発行サーバーの設定で変えられる）。

**推奨の仕組み**
1. 決済は **Stripe**（依頼者の決定 2026-10-09）。**推奨：Stripe の Managed Payments**（Stripe が Merchant of Record＝販売者になり、80 か国以上の消費税・VAT・GST を計算・徴収・申告・納付する。対象の事業者の所在地に日本（JP）があり、ダウンロード型ソフト（税コード `txcd_10202000`）は対象商品。手数料は通常の決済手数料（日本 3.6 %）に加えて 3.5 %。使えるのは Stripe Checkout と Payment Link だけ。購入者の明細は「LINK.COM* ＋事業者の表記」、領収書は Link から届く。2026-10-09 の Stripe の文書）。使わない場合は SEVENTHWELL が販売者で、海外の個人への販売では各国の VAT 登録が要ることがある（EU は域外事業者の電子サービスに金額の下限なし）。Stripe Tax は税額の計算と徴収はするが、登録と申告は自分。どちらにするかは要判断（7 章）。
2. 決済完了の通知（Webhook、署名を検証）を小さな発行サーバー（例：Cloudflare Workers＋D1）が受け、購入者・製品・有効化の台数を記録する。
3. 有効化：プラグイン（または小さなアプリ）がキーと端末の識別値（ハッシュしたもの）を送る → サーバーが台数上限（例 3 台）を確かめ、**Ed25519 で署名したライセンスファイル**（製品 ID・購入 ID・端末のハッシュ・発行日）を返す。ネットのない端末用に、別の端末でファイルを受け取って持ち込む手順も用意する。
4. プラグインは埋め込んだ公開鍵で署名を確かめるだけ（オフライン）。秘密鍵はサーバーの鍵管理（KMS）かオフラインに置き、**リポジトリと CI には置かない**。
5. 未認証のとき：**一定間隔で無音を挟む**（依頼者の決定 2026-10-09）。設計値：起動（有効化）から 30 秒後に始め、60 秒ごとに 3 秒の無音、前後 10 ms のフェード（クリックなし）。最初の 30 秒は普通に聴けて、1 分以内に必ず気づく間隔。保存データ・プリセット・パラメータには触らない。無音の外は 1 ビットも変えない。
6. ライセンスの検証は共通の層（`sw::Shell` と楽器のアダプター）に 1 か所で入れる。テストと validator 用の動作モードを用意する（CI が通るように）。
7. 難読化に時間をかけすぎない：クラックされても、更新・サポート・新しいプリセットは正規の購入者だけが受け取れる形のほうが効く。

**実装済みの部分（2026-10-09）**
- ライセンスファイル（`core/include/sw/license.hpp`・`core/src/license.cpp`）：テキストで、`SW-LICENSE 1`・`key=`（どの公開鍵か。鍵を入れ替えられるよう、プラグインは複数の公開鍵を持てる）・`product=`（製品コードかバンドル名、複数可）・`license=`（ライセンス ID。氏名やメールは入れない）・`machine=`（任意。端末に縛るときだけ）・`issued=`・`major=`、最後に `sig=`（それより前の全バイトへの Ed25519 署名、Base64）。改行が Windows 形式に変わっても検証できる。8 KiB まで、各項目は文字種と長さを厳密に確かめ、署名が確かめられるまで中身を信用しない。
- 署名の方式は標準の Ed25519（RFC 8032）。**OpenSSL で署名したファイルをプラグイン側が検証でき、プラグイン側の道具で署名したファイルを OpenSSL が検証できる**ことを確かめた。発行サーバーは標準のライブラリ（Cloudflare Workers の WebCrypto など）で署名できる。暗号の実装は自作せず、監査済みの Monocypher 4.0.3（BSD-2／CC0）をリポジトリに入れた（`core/third_party/monocypher/`）。
- 端末の識別：OS の機械 ID（Windows は MachineGuid、macOS は IOPlatformUUID、Linux は /etc/machine-id）に固定の文字列を混ぜた BLAKE2b-256 のハッシュだけを使う（元の ID はサーバーに送らない）。
- 道具 `tools/sw_license.cpp`（CMake の `sw-license-tool`）：鍵の作成（OS の安全な乱数、所有者だけが読めるファイル、上書き拒否）、署名（出力前に検証）、検証、この端末のハッシュの表示。**本番の鍵はまだ作っていない**：作るのは所有者の PC か発行サーバーの鍵管理で、秘密鍵はリポジトリにも CI にも置かない。
- テスト（`tests/test_license.cpp`）：OpenSSL の署名の検証、往復、署名部分のどの 1 バイトを変えても不合格、別の鍵・知らない鍵 ID、製品違い、メジャー版、端末違い、改行の違い、壊したファイル 3,000 件（落ちない。通るのは末尾の改行・空白と CR の挿入だけ）。
- プラグインへの組み込み（2026-10-09）：全製品（エフェクトの共通層と楽器の層）が、有効化のたびにライセンスフォルダー（Windows `%APPDATA%\SEVENTHWELL\Licenses`、macOS `~/Library/Application Support/SEVENTHWELL/Licenses`、Linux `~/.local/share/SEVENTHWELL/Licenses`）の `*.swlicense` を確かめる（`core/src/license_state.cpp`）。製品は自分の製品コード、ライン（`studio`／`live`。LV の 30 本が LIVE、残りが STUDIO。IN07 は STUDIO）、`all` のどれかのライセンスで使える。なければ出力の最後に無音を挟む（`sw/demo_gate.hpp`）。**開発版は無音を挟まない**：検査は CMake の `SW_LICENSE_ENFORCE=ON` と本番の公開鍵（`builtInKeys()`）がそろったときだけ（validator と CI はそのまま通る）。環境変数 `SW_LICENSE_TEST_DEMO=1` はどのビルドでも無音を挟ませる（テスト用。外から無音を消す方法はない）。
- 確認：無音の時刻・フェード・ブロック長に依らないこと（`tests/test_demo_gate.cpp`）、フォルダーの判定（`tests/test_license_state.cpp`：製品・ライン・all・他の鍵・壊れたファイル・端末・メジャー版）、実際のプラグインで 30.5〜32.5 秒が無音・その前は鳴る・開発版は鳴る（IN07 は `tools/clap_note_host.cpp`、エフェクトは `tools/clap_fx_demo_check.cpp`。CI の Linux で実行）。
- 発行サーバー（`server/license/`、Cloudflare Workers＋D1）：Stripe Webhook（署名を検証、支払い済みのセッションだけ、2 回来ても 1 本）、商品のメタデータ `sku` から対象製品、サンクスページでライセンスキー（セッションから作り、保存しない）、有効化（その PC に縛ったファイル、3 台まで、同じ PC は数えない、解除で 1 台空く）、全額返金で新しい有効化を止める。氏名・メール・キーは保存しない。テストは `node --test`（Stripe と D1 は偽物、暗号は本物）で、発行したファイルをプラグイン側の C++ でも検証する（CI）。立ち上げの手順は `server/license/README.md`。
- まだのこと：本番の鍵（所有者の PC で `sw-license-tool keygen`）とサーバーの設置、画面での有効化（「この PC のコード」の表示・キーの入力）、キーのメール送付（送信サービスを決めてから）。

## 4. 配布と更新

- ダウンロードは HTTPS だけ。購入者ページか期限付きの URL から。
- インストーラー：macOS は署名・公証済みの pkg、Windows は署名した exe（Inno Setup など）。zip で配るときも中のプラグインは署名済み。
- 各ファイルの SHA-256 をダウンロードページに載せる。
- 更新の確認をプラグインから行うなら、既定は Off（設定で On）。送るのは製品 ID と版だけ。サーバーが返す更新情報は Ed25519 で署名し、プラグインが検証してから「新しい版があります」と表示するだけ（自動でダウンロード・インストールはしない）。
- プライバシーポリシー：集めるのは購入時のメールアドレスと有効化の端末ハッシュだけ、と書けるようにする。

## 5. 開発環境と CI の守り

- 2 段階認証：GitHub、Apple ID、決済サービス、ドメイン／DNS、メール。
- GitHub Actions（出典：GitHub Docs「Secure use reference」）
  - 外部の action はコミットの SHA で固定する（**済** 2026-10-09：checkout v4.4.0・upload-artifact v4.6.2）。
  - `GITHUB_TOKEN` の権限は既定を読み取りだけに（**済**：`permissions: contents: read`）。
  - 署名・公証・アップロードは別のワークフローにし、タグの push だけで動かす。秘密情報（API キー・署名サービスの資格情報）は GitHub の Environment に置き、承認者を必須にする。
  - `pull_request_target` は使わない（他人のフォークのコードを秘密情報のある環境で動かさない）。
  - CI がダウンロードする clap-validator（zip）は SHA-256 を確かめてから使う（**済**：3 つの OS の zip のハッシュを固定）。
- 依存：CLAP 1.2.10・clap-wrapper v0.16.0・VST3 SDK v3.8.0_build_66 はタグで固定。doctest（テスト用）と WebView2 SDK（Windows の画面）は URL からの取得なので SHA-256 を固定した（**済**：CMake の `URL_HASH`）。タグは付け替えられるので、販売版のビルドでは CLAP と clap-wrapper もコミットの SHA に固定する（浅いクローンが使えなくなるので取得は遅くなる）。
- サードパーティのライセンス表記：`NOTICE.md` を製品に同梱する。VST3 SDK は 3.8 から MIT。「VST」の名前やロゴを使うなら Steinberg のガイドラインに従う（最初に出す「VST」に ®、クレジットと文書に「VST is a registered trademark of Steinberg Media Technologies GmbH.」）。

## 6. プラグイン自体の守り（2026-10-09 実装）

プリセットとプロジェクトは人から人へ渡るので、プラグインが読むファイルとデータはすべて「信用しない入力」として扱う。

| 対象 | やったこと | 確認 |
| --- | --- | --- |
| パラメータの値（全製品） | 非数（NaN）は既定値として読む。対数カーブに負の値が来ても NaN にならず最小値（`core/include/sw/param.hpp`） | `tests/test_param.cpp` |
| ホストからの値・保存データ（全製品の 2 つのアダプター） | 有限でない値は既定値、範囲外は端に丸める。保存データは全部読んでから反映（途中で切れたデータは何も変えない）。件数の上限 65,536 | 自作ホスト `tools/clap_note_host.cpp`：切れた状態・ありえない件数は拒否して何も変わらない、NaN と 1e300 だけの状態でも音は有限 |
| ユーザープリセット（.swpreset） | 256 KiB・1 行 1,024 バイト・8,192 行まで。NUL を含む・`=` のない行・他製品・新しい形式は拒否。数値は厳密に（16 進・inf・nan・余計な文字を拒否、ロケールに依らない）、範囲に丸める。知らない ID は飛ばす。名前は正しい UTF-8 だけ・制御文字なし・長さ上限。ファイル名はパス区切り・`..`・Windows の予約名を取り除く。書き込みは一時ファイル→改名（途中で落ちても半端なファイルが残らない）、既存ファイルは確認なしに上書きしない | `tests/test_preset_file.cpp`（ランダムに壊したファイル 4,000 件で落ちない・受け付けた値はすべて範囲内。ASan／UBSan） |
| 製品ごとの追加データ | RV04 の IR：非数は 0、振幅は ±16（+24 dBFS）まで、サンプルレートは 1 k〜768 kHz（極小のレートで巨大な長さを求めない）。UT01：非数を保持しない。LV30 の録音先：絶対パスで `..` と制御文字がないものだけ | `tests/test_extra_state.cpp`（ランダムなデータで落ちない・音が有限） |

**残り（画面を作るときに入れる）**
- WebView：ページ外への移動を禁止（Windows の `NavigationStarting`、macOS の `decidePolicyForNavigationAction`）、CSP（`default-src 'none'` で外部の読み込みを禁止）、開発者ツールを無効（Windows は済み）。
- プリセット名・作者名など外から来た文字列は `textContent` で表示し、`innerHTML` に入れない（現在の画面は自社のデータしか表示していない）。
- LV30：共有されたプロジェクトを開くと、そのプロジェクトの録音先で自動録音が始まる。開いたときに録音先の確認を出すかは要判断。
- LV27・SW Link・OBS 連携（未実装）：localhost だけで待ち受け、パスワードは OS の資格情報ストアに保存。

## 7. 決めてほしいこと（推奨つき）

1. **リポジトリの非公開化**：**あとで**（依頼者 2026-10-09）。推奨は販売前に非公開。ただし過去に公開していた分は回収できない。非公開にすると GitHub Actions が有料になる（無料枠は月 2,000 分。超過は macOS 0.062 USD／分、Windows 0.010 USD／分、Linux 2 コア 0.006 USD／分：GitHub Docs）。直近の CI 1 回（d76f432）は macOS 計 約 42 分、Windows 約 36 分、Linux 約 17 分で、枠を超えた分は 1 回あたり約 3 USD の試算（無料枠の macOS の数え方は要確認）。対策：macOS と Windows はタグと手動実行のときだけにする。
2. ~~売り方~~：**買い切りに決定**（2026-10-09）。台数上限は推奨の 3 台（設計値）。次のメジャー版を有償にするかは 2.0 のときに決める。
3. ~~決済サービス~~：**Stripe に決定**（2026-10-09）。残り：**Managed Payments（Stripe が販売者・税の代行、＋3.5 %）を使うか**（推奨：使う。海外の VAT 登録と申告を自分でしなくてよい）。
4. ~~未認証のときの動作~~：**無音を挟むに決定**（2026-10-09）。30 秒後から 60 秒ごとに 3 秒（設計値）。
5. ~~名義~~：**SEVENTHWELL に決定**（2026-10-09）。製品・販売ページの発行元は SEVENTHWELL。署名（Apple の Developer ID、Windows のコード署名）に SEVENTHWELL と出すには法人であることが要る（2 章）。個人事業のままなら署名は氏名で出る。法人化するかは要判断。

## 範囲外（セキュリティ以外で販売前に要るもの）

特定商取引法に基づく表記、利用規約・使用許諾（EULA）、プライバシーポリシー、税の扱い。専門家に確認する。

## 出典

- Apple: Notarizing macOS software before distribution — https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution
- Apple Developer Program（日本語ページ）— https://developer.apple.com/programs/jp
- Microsoft Learn: Code signing options for Windows app developers — https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options
- Windows のコード署名（日本から配布する場合、2026）— https://codenote.net/en/posts/windows-desktop-app-code-signing-distribution-japan-2026/
- DigiCert: コード署名証明書の有効期間の変更（2026-03-01 から 460 日）— https://www.digicert.com/blog/understanding-the-new-code-signing-certificate-validity-change
- PACE: iLok Licensing の価格 — https://paceap.com/products/ilok-software-licensing-25/pricing/
- Paddle: Classic と Billing の機能比較（ライセンスキー発行の廃止）— https://developer.paddle.com/migrate/paddle-classic/features
- Lemon Squeezy: License API — https://docs.lemonsqueezy.com/help/licensing/license-api
- Keygen: 署名付きライセンスファイルの検証（Ed25519）— https://keygen.sh/docs/api/cryptography
- GitHub Docs: Secure use reference（Actions）— https://docs.github.com/en/actions/reference/security/secure-use
- GitHub Docs: GitHub Actions の課金 — https://docs.github.com/en/billing/concepts/product-billing/github-actions
- Steinberg: VST 3.8 を MIT ライセンスに（2025-10-29）— https://ocl-steinberg-live.steinberg.net/_storage/asset/819253/storage/master/Press%20Release%20-%202025-10-29%20-%20VST%203.8%20-%20EN.pdf
- Steinberg: VST usage guidelines — https://steinbergmedia.github.io/vst3_dev_portal/pages/VST+3+Licensing/Usage+guidelines.html
- Apple: Enrollment（個人・組織の登録条件）— https://developer.apple.com/programs/enroll/
- SSL.com: 個人事業主向け EV コード署名 — https://www.ssl.com/ja/products/software-integrity/code-signing/ev-sole-proprietor/
- Stripe: Managed Payments — https://docs.stripe.com/payments/managed-payments
- Stripe: Managed Payments の対象（事業者の所在地・商品）— https://docs.stripe.com/payments/managed-payments/eligibility
- Stripe: Managed Payments の仕組み — https://docs.stripe.com/payments/managed-payments/how-it-works
- Stripe: 料金（日本）— https://stripe.com/jp/pricing
- Stripe: Payment Link の完了後（`{CHECKOUT_SESSION_ID}`）— https://docs.stripe.com/payment-links/post-payment
- Cloudflare: Workers の Web Crypto（Ed25519）— https://developers.cloudflare.com/workers/runtime-apis/web-crypto/
