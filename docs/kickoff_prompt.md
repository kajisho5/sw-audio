# Claude Code への最初の指示（iPhone の Code タブに貼る）

リポジトリ：kajisho5/sw-audio、ブランチ：main

---

SW AUDIO の開発を引き継いで、おまかせで進めてください。

最初に CLAUDE.md、README.md、docs/tasks.md を読んでください。仕様の正は docs/spec/SW_AUDIO_spec_v1.0.md です。画面デザイン（docs/design/canvas）は別に追加するので、空でも DSP の作業は進めてください。

次の順で進めてください。
1. tools/setup_linux.sh で環境を用意し、tools/validate_all.sh で現状（23 製品、単体テスト 221 件、両 validator で不合格 0）を再現する。
2. GitHub Actions（.github/workflows/build.yml）を動かして、Windows・macOS（AU を含む）・Linux で全部通るまで直す。
3. アナログ出力段（core/include/sw/drive.hpp。EQ01・EQ03・EQ04 で共通）を、仕様書どおり「非対称ソフトクリップ1段、2× OS、音量補正つき」にする。偶数次倍音が出ることをテストで確かめる。
4. docs/tasks.md の残り 116 製品を、仕様書の順に 1 製品ずつ。各製品は「仕様の節を読む → テストを先に書いて失敗を確認 → 実装 → validate_all と ASan → README と tasks.md を更新 → commit」。

守ること：決定済みの仕様は勝手に変えない。変える必要があるとき（仕様にない値、仕様どおりだと品質に問題がある、など）は、数値の根拠を README に書いてから決める。実在機材の名前は使わない。

区切りごとに、何ができたか・テストと検証の結果・仕様から離れた判断を短く報告してください。止まらずに次へ進んでかまいません。
