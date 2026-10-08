# SW AUDIO 有料配布のセキュリティ計画

2026-10-09 作成（依頼者「有料配布するときに必要なセキュリティを考えといて」）。価格・条件は下の出典で 2026-10-09 に確認したもの。変わることがあるので、契約・購入の前に出典をもう一度見る。

## 結論：販売前にやること

| # | やること | 状態 |
| --- | --- | --- |
| 1 | **GitHub リポジトリを非公開にする**（いまは公開。製品の全ソースを誰でもビルドできるので、ライセンス認証を入れても外して配れる） | **要判断**（リポジトリ設定の変更なので依頼者の承認後に実行） |
| 2 | **コード署名と公証**：macOS は Developer ID 署名＋公証（Apple の規則で、ダウンロードしたプラグインは公証がないとホストに読み込まれない）。Windows は OV のコード署名証明書 | 未着手（アカウントと証明書の取得が要る） |
| 3 | **ライセンス認証**（**買い切り**：依頼者の決定 2026-10-09）：署名付きライセンスファイル（Ed25519）をオフラインで検証。有効化の台数はサーバー側で数える。ネットがなくても動く | **検証の部分は実装済み**（`core/src/license.cpp`・署名の道具・テスト）。発行サーバーとプラグインへの組み込みは、決済サービスと未認証時の動作が決まってから |
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
1. 決済は Merchant of Record（販売者として消費税・海外の VAT などを代行する決済サービス。どこにするかは要判断。例：Paddle、Lemon Squeezy、FastSpring）。Paddle Billing はライセンスキーの発行をやめているので（Paddle の移行文書）、どこを選んでもライセンスは自前で発行する前提にする。
2. 決済完了の通知（Webhook、署名を検証）を小さな発行サーバー（例：Cloudflare Workers＋D1）が受け、購入者・製品・有効化の台数を記録する。
3. 有効化：プラグイン（または小さなアプリ）がキーと端末の識別値（ハッシュしたもの）を送る → サーバーが台数上限（例 3 台）を確かめ、**Ed25519 で署名したライセンスファイル**（製品 ID・購入 ID・端末のハッシュ・発行日）を返す。ネットのない端末用に、別の端末でファイルを受け取って持ち込む手順も用意する。
4. プラグインは埋め込んだ公開鍵で署名を確かめるだけ（オフライン）。秘密鍵はサーバーの鍵管理（KMS）かオフラインに置き、**リポジトリと CI には置かない**。
5. 未認証のとき：機能を落とすか、一定間隔で無音を挟むか（要判断）。どちらでも、保存データとプリセットは壊さない・音声スレッドで重い処理をしない。
6. ライセンスの検証は共通の層（`sw::Shell` と楽器のアダプター）に 1 か所で入れる。テストと validator 用の動作モードを用意する（CI が通るように）。
7. 難読化に時間をかけすぎない：クラックされても、更新・サポート・新しいプリセットは正規の購入者だけが受け取れる形のほうが効く。

**実装済みの部分（2026-10-09）**
- ライセンスファイル（`core/include/sw/license.hpp`・`core/src/license.cpp`）：テキストで、`SW-LICENSE 1`・`key=`（どの公開鍵か。鍵を入れ替えられるよう、プラグインは複数の公開鍵を持てる）・`product=`（製品コードかバンドル名、複数可）・`license=`（ライセンス ID。氏名やメールは入れない）・`machine=`（任意。端末に縛るときだけ）・`issued=`・`major=`、最後に `sig=`（それより前の全バイトへの Ed25519 署名、Base64）。改行が Windows 形式に変わっても検証できる。8 KiB まで、各項目は文字種と長さを厳密に確かめ、署名が確かめられるまで中身を信用しない。
- 署名の方式は標準の Ed25519（RFC 8032）。**OpenSSL で署名したファイルをプラグイン側が検証でき、プラグイン側の道具で署名したファイルを OpenSSL が検証できる**ことを確かめた。発行サーバーは標準のライブラリ（Cloudflare Workers の WebCrypto など）で署名できる。暗号の実装は自作せず、監査済みの Monocypher 4.0.3（BSD-2／CC0）をリポジトリに入れた（`core/third_party/monocypher/`）。
- 端末の識別：OS の機械 ID（Windows は MachineGuid、macOS は IOPlatformUUID、Linux は /etc/machine-id）に固定の文字列を混ぜた BLAKE2b-256 のハッシュだけを使う（元の ID はサーバーに送らない）。
- 道具 `tools/sw_license.cpp`（CMake の `sw-license-tool`）：鍵の作成（OS の安全な乱数、所有者だけが読めるファイル、上書き拒否）、署名（出力前に検証）、検証、この端末のハッシュの表示。**本番の鍵はまだ作っていない**：作るのは所有者の PC か発行サーバーの鍵管理で、秘密鍵はリポジトリにも CI にも置かない。
- テスト（`tests/test_license.cpp`）：OpenSSL の署名の検証、往復、署名部分のどの 1 バイトを変えても不合格、別の鍵・知らない鍵 ID、製品違い、メジャー版、端末違い、改行の違い、壊したファイル 3,000 件（落ちない。通るのは末尾の改行・空白と CR の挿入だけ）。
- まだのこと：発行サーバー（決済サービスが決まってから）、プラグインへの組み込み（ライセンスファイルの置き場所、画面での入力、未認証時の動作が決まってから。validator と CI 用の動作モードも一緒に）。

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

1. **リポジトリの非公開化**：推奨は販売前に非公開。ただし過去に公開していた分は回収できない。非公開にすると GitHub Actions が有料になる（無料枠は月 2,000 分。超過は macOS 0.062 USD／分、Windows 0.010 USD／分、Linux 2 コア 0.006 USD／分：GitHub Docs）。直近の CI 1 回（d76f432）は macOS 計 約 42 分、Windows 約 36 分、Linux 約 17 分で、枠を超えた分は 1 回あたり約 3 USD の試算（無料枠の macOS の数え方は要確認）。対策：macOS と Windows はタグと手動実行のときだけにする。
2. ~~売り方~~：**買い切りに決定**（2026-10-09）。台数上限は推奨の 3 台（設計値）。次のメジャー版を有償にするかは 2.0 のときに決める。
3. **決済サービス（Merchant of Record）**：どこにするか。手数料と日本からの出金条件を比べて決める。
4. **未認証のときの動作**：機能制限か、一定間隔の無音か。
5. **名義**：個人か法人か（Apple の開発者登録名、コード署名証明書の名義、決済サービスの契約に出る）。

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
