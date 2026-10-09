# SW AUDIO ライセンスサーバー（Cloudflare Workers ＋ D1 ＋ Stripe）

売り方は買い切り（2026-10-09 決定）、決済は Stripe（同日決定）。このサーバーはライセンスの発行と有効化だけを行う。お金の処理と顧客情報は Stripe が持ち、ここには氏名・メールアドレス・ライセンスキーを保存しない（キーは SHA-256 だけ）。

## 流れ

1. 購入者が Stripe Checkout（または Payment Link）で支払う。
2. Stripe が `checkout.session.completed`（コンビニ払いなどは後から `checkout.session.async_payment_succeeded`）を `POST /stripe/webhook` に送る。署名（`Stripe-Signature`、HMAC-SHA256、5 分以内）を確かめ、支払い済みなら、購入した Stripe 商品のメタデータ `sku`（`in07` などの製品コード、`studio`・`live`・`all`、カンマ区切り可）からライセンスを作る。同じ通知が 2 回来ても 1 本。
3. Checkout の戻り先 `GET /thanks?session_id={CHECKOUT_SESSION_ID}` にライセンスキー（`SWL-XXXXX-XXXXX-XXXXX-XXXXX`）を表示する。キーはセッション ID とサーバーの秘密から作るので、再読み込みで同じキーが出る（データベースには残らない）。
4. 有効化：プラグインの画面に出る「この PC のコード」（機械 ID の塩付きハッシュ、64 桁）とキーを `POST /api/activate`（JSON）か `POST /activate`（フォーム、ファイルのダウンロード）に送ると、その PC に縛った `.swlicense`（Ed25519 署名）が返る。1 本 3 台まで（`MAX_ACTIVATIONS`）。同じ PC はもう一度でも数えない。ネットのない PC 用に、キーとコードを入れるフォーム `GET /activate` もある。
5. 解除：購入者が `GET /manage` でキーを入れると、有効化した PC の一覧（コードの最初の 8 文字と最後の 4 文字、日付）と「解除」のボタンが出る（`POST /manage`・`POST /deactivate`。プラグインの画面からは `POST /api/deactivate`）。1 本あたり 365 日で `MAX_DEACTIVATIONS`（既定 3、設計値）まで。配ったライセンスファイルはオフラインで動き続けるので、回数を絞らないと「有効化 → ファイルを残す → 解除 → 別の PC」で 1 本のキーが何台にも使える。それより多い解除はサポートで（D1 の `activations` から手で消す）。
6. 全額返金（`charge.refunded`）のライセンスは、新しい有効化を断る（配布済みのファイルはオフラインで動き続ける。取り消しはしない）。

プラグインは受け取った `.swlicense` をライセンスフォルダー（Windows `%APPDATA%\SEVENTHWELL\Licenses`、macOS `~/Library/Application Support/SEVENTHWELL/Licenses`）から読み、ネットなしで確かめる（`core/src/license_state.cpp`）。ライセンスがなければ、30 秒後から 60 秒ごとに 3 秒の無音が入る。

## テスト

```bash
cd server/license
node --test test/*.test.js                                                       # Node 20 以上
SW_LICENSE_TOOL=../../build-cmake/sw-license-tool node --test test/*.test.js     # プラグイン側の C++ でも検証する
```

Stripe と D1 は偽物に置き換え、暗号は本物（WebCrypto）。CI（Linux）は C++ の検証つきで回す。

## 立ち上げ（所有者の作業）

1. **鍵**（自分の PC で。秘密鍵はリポジトリ・CI・チャットに出さない）
   ```bash
   build-cmake/sw-license-tool keygen ~/sw-licence-key-1.hex       # 公開鍵が表示される
   build-cmake/sw-license-tool pkcs8 ~/sw-licence-key-1.hex        # サーバー用（下の LICENSE_PRIVATE_KEY）
   ```
   表示された公開鍵（64 桁）を `core/src/license_state.cpp` の `builtInKeys()` に id 1 で入れ、販売版は CMake の `-DSW_LICENSE_ENFORCE=ON` でビルドする。秘密鍵のファイルはオフラインで保管し、バックアップを取る（失うと同じ鍵で発行できない。鍵を替えるときは id 2 を足し、id 1 も残す）。
2. **Cloudflare**
   ```bash
   cd server/license
   npx wrangler d1 create sw-licences                  # 出た database_id を wrangler.toml に
   npx wrangler d1 execute sw-licences --remote --file schema.sql
   npx wrangler secret put LICENSE_PRIVATE_KEY         # 上の pkcs8 の出力
   openssl rand -hex 32 | npx wrangler secret put LICENSE_KEY_SECRET
   npx wrangler secret put STRIPE_WEBHOOK_SECRET       # 手順 3 の whsec_...
   npx wrangler secret put STRIPE_API_KEY              # 手順 3 の制限付きキー
   npx wrangler deploy
   ```
   `/api/activate`・`/activate`・`/manage`・`/deactivate`・`/api/deactivate` には Cloudflare のレート制限ルールを付ける（キーの総当たり対策。キーは 100 ビットなので実際には当たらないが、負荷を抑える）。
3. **Stripe**
   - 商品ごとにメタデータ `sku`（例：`in07`、バンドルは `studio`／`live`／`all`）と税コード `txcd_10202000`（Downloadable Software - personal use）を付ける。
   - Checkout または Payment Link の完了後の戻り先：`https://<このサーバー>/thanks?session_id={CHECKOUT_SESSION_ID}`。
   - Webhook：`https://<このサーバー>/stripe/webhook`、イベント `checkout.session.completed`・`checkout.session.async_payment_succeeded`・`charge.refunded`。表示される署名シークレット（`whsec_...`）を上へ。
   - 制限付き API キー：Checkout Sessions と Products の読み取りだけ。
   - **Managed Payments**（Stripe が販売者になり、80 か国以上の消費税・VAT・GST の計算・申告・納付まで代行。日本の事業者・ダウンロード型ソフトは対象。手数料は通常の決済手数料に加えて 3.5 %）を使うかどうかは `docs/security.md` の 7 章。使う場合も Checkout と Payment Link は同じ形で動く（使えるのは Checkout と Payment Link だけ）。テストモードで Webhook が届くことを最初に確かめる。

## キーをなくした購入者への対応

キーはデータベースにないが、Checkout Session ID から同じキーが作れる。Stripe のダッシュボードで購入者のメールアドレスから支払いを探し、その Checkout Session の ID で `https://<このサーバー>/thanks?session_id=cs_...` を案内する（本人確認のうえで）。

## まだのこと

- ライセンスキーのメール送付（送信サービスを決めてから）。それまでは購入完了のページとその再読み込み（マニュアル・特定商取引法に基づく表記もそう書いた）。
- ページ（購入完了・`/activate`・`/manage`）は日本語だけ。
- 同じ購入で複数本（数量 2 以上）には未対応（1 セッション＝1 本）。
