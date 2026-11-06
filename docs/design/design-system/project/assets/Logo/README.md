SW AUDIO の確定ロゴ（C Rack Ear）。すべて黒1色（#050505）のマスター。暗い地では sw-white（#F0F0F0）に置き換えて使う。

- sw-audio-horizontal：横組み（基本形）。sw-audio-stacked：縦組み。sw-audio-studio / sw-audio-live：ライン別。
- sw-audio-symbol：記号（円弧のみ、アンカー19点）。sw-audio-symbol-small：16〜32px用の太らせ版。
- sw-audio-app-icon：アプリアイコン（#101113の角丸タイルに白の小サイズ版記号）。
- 規定（余白・最小サイズ・色・禁止事項）は LOGO.md。

**このフォルダのファイルは `tools/gen_logo.py` が LOGO.md の寸法から作る**（`python3 tools/gen_logo.py`。SVG と確認用の PNG、README の見出し画像 `docs/images/sw-audio-banner.png`）。
- 記号（通常版・小サイズ版）は LOGO.md の寸法どおり（角丸長方形と、横長の穴 2 つ。直線と円弧だけ）。穴の中心は板の端から 46（小サイズ版 50）：3D レンダーの位置に合わせた設計値。
- **ワードマーク「SW AUDIO」は、規定（モノライン 線幅13／字高100、ワイドな全大文字、A の斜線 60°、丸い端）から引き直したもの**。元の専用字形のデータはこのリポジトリに無かった。元データが見つかったら、そちらに差し替える（LOGO.md「やらないこと：他の書体で打ち直す」に当たらないよう、配布データとしてはこの版を正とするか、元データに戻すかを決めること）。
- 字間 22・語間 56 は設計値。
