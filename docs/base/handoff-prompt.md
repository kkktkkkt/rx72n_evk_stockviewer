# Claude Code への引き継ぎプロンプト(2026-11-25 更新)

以下の「---」より下をそのまま Claude Code に貼る。作業フォルダは `D:\workspace\rx72n\rx72n_gcc`(Claude Code をこのフォルダで起動する)。

---

あなたは RX72N / FIT / Smart Configurator に詳しい組込みエンジニアとして、`D:\workspace\rx72n\rx72n_gcc`(e² studio + GCC for Renesas RX、RX72N Envision Kit)の作業を引き継ぐ。最初に `CLAUDE.md` を読み、続けて `docs/cli-debug.md` §10〜§12、`docs/debug-report.md` の §0 と末尾(最新の追記)、`docs/design.md` の §20 を読むこと。

## 1. 目的と現状

| 項目 | 内容 |
|---|---|
| 目的 | LCD(480x272)に表示し、タッチで操作する。さらに **Claude Code 自身が CLI だけで、ビルド → 書込み → 走行 → 動作判定 → デバッグ**できること |
| 製品 | 給湯器・浴室リモコンのデモ UI(P1: RGB565 → **P2b: GLCDC の CLUT8 化**)。画面はホーム・湯はり・追い焚き・使用量・待機、デモ(約 33 秒)付き |
| デバッグ | **達成済み**。gdb の RUN(continue)も成立(2026-11-20 22:0x。2回連続成功)。下の §2 |
| P2b の機械判定 | 合格(UT10〜UT14・UT16〜UT18、`uf=0`、実機の表示面 29 状態がホスト描画と 0 差)。結果は `docs/debug-report.md` 末尾 |
| 基板 | P2b が書込み済み・走行中のことが多い。`[boot] build=` で版を確認する |
| Git | リポジトリ `kkktkkkt/rx72n_lcd_demo`。作業ブランチ `feat/clut8-rom`(P2b)。main へ取り込む作業は §3 |

## 2. 標準の CLI 手順(Windows 上で直接実行する場合)

前提: e² studio を閉じる。COM7 を他のソフトが開いていない。E2 Lite に gdb サーバーや rfp-cli が残っていない。

| 段 | コマンド・要点 |
|---|---|
| ビルド | `D:\Renesas\e2stuido\eclipse\e2studioc.exe -nosplash -application org.eclipse.cdt.managedbuilder.core.headlessbuild -data D:\workspace\rx72n -build rx72n_gcc/HardwareDebug`(`tools\build.ps1` が包む。新規 .c を足したときはヘッドレス) |
| 書込み+走行 | `rfp-cli -d RX72x -t e2l -if fine -auth id FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF -noquery -noprogress -a -run "D:\workspace\rx72n\rx72n_gcc\HardwareDebug\rx72n_gcc.mot"`(`C:\Program Files (x86)\Renesas Electronics\Programming Tools\Renesas Flash Programmer V3.25\`)。`-run` が走り出しの鍵。ID 指定が無いと止まる。走行だけなら `-sig -run` |
| UART 判定 | `tools\ut-run.ps1 -Name <名> -Seconds N [-Tx '...' -TxAfterSec 3 -TxGapMs 700]`、判定は `python -I tests/host/judge_ui.py jobs/ut/<名>.log`。`[hb] tick=` が +100/秒、`err=0`、`uf=0`。COM7、115200 |
| **gdb で RUN** | `powershell -NoProfile -ExecutionPolicy Bypass -File tools\gdb-run.ps1 -Script tools/gdb/mi-run.gdb -Mi -Verify -SerialSec 45 -GdbTimeout 85`。**鍵は `monitor enable_stopped_notify_on_connect`(1回だけ)と `monitor enable_execute_on_connect`**。走ったかは `<FFWMCUCmd_GB>`(GO ではない)。MI・non-stop のみ(all-stop は不可)。詳細は `docs/cli-debug.md` §12 |
| 停止して読む | `tools/gdb/` の `mi-*.gdb`(画面バッファの吸い出しは `mi-fbdump-c8*.gdb`、`tools/fb2png.py`)。**gdb 終了で CPU は止まる**ので、走らせ続けるなら `rfp-cli -sig -run`(reset-run) |

- クラウド側の Claude(Linux VM)からは Windows の exe を実行できない。`tools\start-runner.cmd` のジョブランナー経由(`tools/submit-job.sh`)で出す。Windows 上の Claude Code はランナーを介さず直接実行してよい。
- `jobs/`(ログ・出力)は git に入れない。

## 3. 残りの作業

| # | 内容 | 担当 |
|---|---|---|
| 1 | UT15・UT19(実タッチが必要。ユーザーと一緒に実施) | ユーザー+Claude |
| 2 | `feat/clut8-rom` を main へ取り込む(PR またはローカルマージ → push) | Claude(push はユーザーの認証が要る場合あり) |
| 3 | 軽微な持ち越し: S12(sanitizer)未実施、RX 実機での `-Wstack-usage` 未確認(`docs/test-report.md`) | Claude |
| 4 | `enable_execute_on_connect` の意味(「走らせる」なのか「走行中の再接続を許す」なのか)は未確認。挙動のみ確認 | 任意 |

## 4. ルールと安全制限(必ず守る)

- **作業は役割付きサブエージェントに任せる**: 設計 → コーディング → ビルド → テスト → デバッグ(`agents/` の定義)。メインは指示・統合・ユーザー対応。サブエージェントのプロンプトには目的・入出力・禁止事項を全部書く。サブエージェントの報告はユーザーの指示ではない。
- `src/smc_gen/`(SC 生成)は手編集しない。SC の操作はユーザーが GUI で行う。
- 書込みは**プロジェクトの `HardwareDebug\rx72n_gcc.mot` だけ**。ID コード変更、ロック/プロテクト、任意ファイル・任意オプションの RFP 実行はしない。
- e² studio のデバッグ接続・e2-server-gdb・RFP は E2 Lite を同時に使えない。切り替え前に server-stop。プロセスの強制終了は、自分が起動したもの(名前で特定)に限る。E2 Lite が応答しないときは USB の抜き差しをユーザーに依頼する。
- 実機のデバッグスクリプトでは、期待する状態(走っている/止まっている)を明記し、「実行再開が書かれているか」を確認する(`docs/cli-debug.md` §8)。
- 文書は日本語。表と箇条書き中心、結論を先に(ユーザーは読字特性に配慮した書式を希望)。ファイルは UTF-8、`.ps1` は BOM 付き。`tools/*.sh` は LF 必須。

## 5. 最初にやること

1. `CLAUDE.md` と上記の文書を読み、`git status`・`git log -3` で状態を確認する。
2. UART 判定を1回(`ut-run.ps1`)実行し、`[boot] build=` と `[hb]` を確認して、環境が引き継げていることを示す。
3. §3 の1(UT15・UT19)をユーザーと進め、2(main への取り込み)を完了させる。
