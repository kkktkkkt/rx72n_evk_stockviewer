# CLIデバッグの手順(LLM向け)

最終更新: 2026-11-20 / **標準手順は §10(RFPで書込み+走行 → serialで判定)**。 **ステータス: ping・server-start・gdb(接続/書込み/メモリ読出し)・serial は実機で動作確認済み。実行再開(continue)は未確認**。実行記録は §7

## 0. 結論

| 項目 | 内容 |
|---|---|
| 目的 | e² studio のGUIを使わず、LLMが実機のビルド・書込み・実行・変数確認・シリアル確認をする |
| なぜ間接か | LLMの作業環境はLinux。ユーザーPCのWindowsのexe(e2-server-gdb、rx-elf-gdb、make)を直接動かせない |
| 仕組み | ユーザーPCで `tools\job-runner.ps1` を1回起動しておく。LLMは `jobs\inbox\` にジョブ(JSON)を置く。ランナーが実行し、`jobs\outbox\` にログと結果を書く |
| ユーザーが最初にやること | ① e² studio の**デバッグ接続を切る**(E2 Liteは同時に1つしか使えない)② PowerShellで `cd D:\workspace\rx72n\rx72n_gcc ; .\tools\job-runner.ps1` |

## 1. ユーザーのチェックリスト

| # | 内容 |
|---|---|
| 1 | e² studio のデバッグを**終了**(赤い■)。接続を切る。e² studio は開いたままでよい(`Make` ビルドなら)。`Headless` ビルドは e² studio を閉じる必要がある |
| 2 | **`tools\start-runner.cmd` をダブルクリック**(実行ポリシーも回避する)。または PowerShell で `.\tools\job-runner.ps1`。「job-runner 開始」と出れば成功。ウィンドウは開いたままにする |
| 3 | 実行ポリシーで止まる場合: `powershell -ExecutionPolicy Bypass -File .\tools\job-runner.ps1` |
| 4 | 止めたいときは Ctrl+C か、`jobs\STOP` というファイルを作る |
| 5 | CN8のUSBケーブルを挿す。基板の SW3 2ch は OFF のまま |

## 2. ジョブの出し方(LLM側)

```bash
cd $HOME/mnt/rx72n/rx72n_gcc
tools/submit-job.sh ping                                    # ランナー生存確認
tools/submit-job.sh find-tools '{}' 180                     # exeの場所を探す(初回)
tools/submit-job.sh build '{"mode":"Make"}' 300             # ビルド
tools/submit-job.sh server-start '{}' 30                    # e2-server-gdb 起動(E2 Lite接続)
tools/submit-job.sh gdb '{"script":"tools/gdb/connect-load-run.gdb","timeoutSec":60}' 90
tools/submit-job.sh gdb '{"script":"tools/gdb/read-vars.gdb","timeoutSec":30}' 60
tools/submit-job.sh serial '{"port":"COM7","baud":115200,"seconds":15}' 40
tools/submit-job.sh server-stop '{}' 20
```

| action | args | 動作 |
|---|---|---|
| `ping` | なし | 生存確認。`pong` を返す |
| `find-tools` | なし | `tools\find-tools.ps1` を実行。結果は `jobs\tools.json` に自動キャッシュされる(e2-server-gdb/rx-elf-gdb) |
| `build` | `mode`(`Make`/`Headless`)、`clean`、`makeExe`、`toolchainBin` | `tools\build.ps1` を実行 |
| `server-start` | なし | `.launch` のサーバ引数で e2-server-gdb を起動。出力を `jobs\server.*.log` に保存 |
| `server-stop` | なし | サーバを止める |
| `gdb` | `script`(`tools/gdb/` 配下)、`timeoutSec`、**`port`(必須。サーバログの `GDB:` の値)**、**`nonstop`(true にする。all-stop では接続できない)**、`noconnect`(`$gdbport` を使い、スクリプト側で `target remote`) | `rx-elf-gdb -batch` で接続し、スクリプトを実行。タイムアウトで強制終了。ログに gdb の stdout/stderr/終了コードを出す |
| `serial` | `port`(`COMn`)、`baud`、`seconds`(≦600) | 指定秒数ぶん受信して記録 |

結果: `jobs/outbox/<id>.result.json`(終了コード、所要秒)と `<id>.log`。完了したジョブは `jobs/done/` へ移る。

## 3. 未確認の点(2026-11-19 の実行結果)

| # | 疑問 | 結果 |
|---|---|---|
| 1 | e2-server-gdb の待ち受けポート | **61234ではない。起動のたびに変わる**。`jobs/server.out.log` の `GDB: <port>` 行を毎回読み、gdbジョブの `port` に渡す(`tools/fresh-gdb.sh` が自動でやる)。サーバ引数の `-p` で固定できるか(フォーラム情報)は**未試験** |
| 2 | `optionInitCommands` を gdb にも書く必要があるか | 書いて load は成功。書かない場合は**未試験** |
| 3 | `-batch` の挙動、タイムアウトで gdb を殺したときのターゲット | 接続時にターゲットは**止まっている**(サーバ `Target status=stopped.`)。**自動で走り出さない**(`monitor reset`、`monitor resume` の後も停止)。走らせるのは `continue -a` では走らず、§12 の標準手順(`monitor enable_execute_on_connect` ほか)で走る。タイムアウトで殺す件は**未確認** |
| 3b | all-stop で接続できるか | **できない**。`Could not read registers; remote failure reply '01'`。**ジョブに `"nonstop":true` を付ける**(`-iex "set non-stop on"`) |
| 3c | 1つのサーバに何回gdbを繋げるか | **1回だけ**(切断後の2回目は `could not connect (error 138)`)。gdbジョブごとに server-stop → server-start |
| 3d | `compare-sections`(照合) | 使えない(`Invalid hex digit 112`)。照合は `x/16xw 0xffc00000` 等の読出しとELFの比較で代替(案、未実施) |
| 4 | Headless ビルド | 成功(22:47、64秒)。`docs/build-and-debug.md` 参照 |
| 5 | `Make` ビルドのPATH | 未確認(Headless のみ使用) |
| 6 | `.ps1` の構文・文字コード | 動作する。**PowerShellの癖**: `Start-Process -ArgumentList` は配列を空白で連結するだけ(空白を含む引数は自前で `"..."` に)。`$p.ExitCode` は `$null = $p.Handle` を先に触らないと null |
| 7 | COM7に出るか | **未確認**。ログ出力は既定で無効なので、`serial` ジョブ(10秒)は 0バイトで正常。COM7を開くことはできた |

## 4. LLMに見えないもの

- **画面の表示とタッチ操作**。ユーザーに「何を見て、何を触って、どうなれば合格か」を具体的に頼む。
- CLIで確かめられるのは、ビルド可否、書込み可否、実行中の変数(タッチ座標、画像番号、I2Cのエラー値)、シリアルログまで。
- 画面に何も出ないとき: `g_app_dbg` のGLCDC開始状態、I2Cのアドレス、タッチ値で切り分ける(設計書 §9, §14 の B項目)。

## 5. 安全

- ランナーは許可リストのactionだけ実行し、任意のコマンドは実行しない。gdbスクリプトは `tools\gdb\` 配下に限る。
- 書込みは実機のフラッシュを書き換える。ELFは `HardwareDebug\rx72n_gcc.elf` のみ。
- ジョブフォルダ(`jobs\`)は、ユーザーが `job-runner.ps1` を起動している間だけ有効。止めれば何も実行されない。

## 6. なぜ「LLMがPCのコンソールを直接操作」ではないのか(2026-11-19 確認)

| 手段 | 結果 |
|---|---|
| `device_bash` | ユーザーPC上の **Linux VM(Ubuntu)**。Windowsのexeは動かない(`cmd.exe`/WSL連携なし。`uname` で確認) |
| 画面操作(computer use)でコンソールに入力 | 端末アプリは「クリックのみ許可・文字入力は不可」の階層。さらに本セッションでは e² studio への入力も届かなかった |
| 代替 | `job-runner`(ユーザーが1回起動)。ログオン時の自動起動は任意(`tools\install-runner-autostart.cmd`。常駐するので、不要なら入れない) |

## 7. 実行記録と直した点(2026-11-19 夜)

| 症状 | 原因 | 直した所 |
|---|---|---|
| gdbジョブ: 終了コード null・出力空 | `$p.ExitCode` が null になる癖。引数が空白で割れる | `job-runner.ps1`: `$null = $p.Handle`、引数を `"..."` で囲む、stdout/stderr/終了コードをログに出す |
| gdb 終了コード -1073741515(0xC0000135) | `rx-elf-gdb.exe` が python3.dll を要求。`.eclipse\...\DebugComp\RX` に無い | `job-runner.ps1`: `D:\Renesas\e2stuido\eclipse\plugins\com.renesas.python3.win32*\bin` を PATH の先頭に追加 |
| サーバ stderr: `Failed to open file : .\RX\rxv3-dpfpu-regset` | サーバの作業フォルダが違った | `server-start` に `-WorkingDirectory (exeのフォルダ)`(これで消えた。単独の効果は未分離) |
| `Could not read registers; remote failure reply '01'` | all-stop だと接続時にレジスタを読む | gdb ジョブに `nonstop` 引数を追加(`-iex "set non-stop on"`)。さらに `-iex "set $gdbport = <port>"` を常に渡す |
| 2回目の gdb が接続できない | サーバは1セッション限り | `tools/fresh-gdb.sh`(stop → start → ポート取得 → gdbジョブ。ポートが60秒出なければ中止) |
| `server-start` が「ターゲット接続開始」で止まる / `Rx_Init_E1_E20() Failed` | load の後、E2 Lite が不安定に。ユーザーがUSBを抜き差しして復旧 | 原因は未確定。強制終了(`Stop-Process -Force`)の後遺症の疑い。`server-stop` は名前で孤児の e2-server-gdb/rx-elf-gdb も止め、残っているプロセスをログに出す |
| 注意 | ランナーを `restart` すると子のサーバも止まる | 何度も再起動しない。サーバ起動後に再起動したら server-start をやり直す |
| ログの文字化け | `jobs/outbox/*.log` に出る gdb/サーバの日本語が UTF-8 で二重解釈される | 実害は小さい(`jobs/server.out.log` は正しく読める) |

使える gdb スクリプト(`tools/gdb/`): `probe*.gdb`(接続の実験)、`connect-load-run.gdb`(**書込み。フラッシュを書き換える**)、`reset-run.gdb`(リセット。**continue が未入り**)、`read-vars.gdb`(MSTPCRC・`g_app_dbg`・RAM2 の読出し)、`resume1.gdb` / `allstop1.gdb`(実験)。

## 8. 再発防止: 「走らせたつもりが走っていなかった」(2026-11-19 23:20 追記)

事故: `reset-run.gdb` が「リセットして走らせる」と書きながら、`monitor reset` の後に実行再開(`continue -a`)が無かった。リセット後の RX72N は先頭で停止する。tick が進まず、LCDが無表示になった原因の切り分けが1往復ぶん遅れた。

| # | ルール |
|---|---|
| 1 | **「走らせる」スクリプトには、実行再開の命令を明示的に書く**。non-stop 接続では `continue -a`(`continue` だけでは足りない場合がある)。`monitor reset` や `load` は走らせない |
| 2 | **読む前に「走っている証拠」を取る**。`g_app_dbg.tick` を2回読み(間に待ち時間)、増えていなければ、値の解釈をやめて「停止中」と報告する。乱数の値を製品コードの不具合と判断しない |
| 3 | スクリプトの先頭に **前提と終了状態**を1行ずつ書く(例: 前提=書込み済み、終了状態=走行中のまま detach) |
| 4 | **ひな形を使う**: `tools/gdb/template-run-and-read.gdb`(下記の順序: 事前の読み → reset → continue -a → 待つ → tick 2回 → 変数 → detach) |
| 5 | 担当(サブエージェント)に依頼するとき、**期待する状態(走っている/止まっている)を明記**する。「リセット後はどの状態か」は、実機の観察で確認し、この手順書に書いてから使う |
| 6 | 実行前に、主セッションがスクリプトを読んで、上の1〜3を確認する(レビュー1回) |

観察された事実(23:17): `monitor reset` の後、ターゲットは停止状態(`Target status=stopped`)で、tick は不変。→ reset は停止で終わる。走らせるには continue が要る。

追記(2026-11-19 23:4x 実機で確認):

| # | ルール・観察 |
|---|---|
| 7 | **毎回 load する(ユーザー指示)**。`tools/gdb/load-only.gdb`。load 直後の PC は 0xffc00000(自動でリセット済み)で、`monitor reset` は不要 |
| 8 | **接続するとCPUが止まる**(non-stop 接続時に gdb が `vCont;t` を送る)。走行中の値は読めない。「tick を2回読んで増えるか」は、**この環境では常に不変**(CPUが止まっているため)。tick が不変でも製品コードの不具合とは言えない |
| 9 | **(旧記述。解決済み: §12)CLIからの実行再開は未成功だった**。`docs/debug-report.md` §4.1 に試した方式の一覧。raw `vCont;s`(1命令)だけ動く。書込み後はユーザーがリセットボタンを押す前提にする(未検証) |
| 10 | gdb の `info threads` の `(running)` は実態と一致しない。`monitor is_target_stopped,0`(stderr に `Target status=stopped.`)と `monitor getPC` が実態 |
| 11 | `maint packet <RSP>` で生のパケットを送れる(`sending:` / `received:` が出る)。`set debug remote 1` でパケットを全部見られる |
| 12 | `job-runner` の gdb ジョブに `"stdin":true`(スクリプトを標準入力から読む。イベントループが回る。最後に `quit`)を追加。効果は上の通り限定的 |
| 13 | **gdb の thread 状態を「停止」に直す方法**: stdin モード(`SI=true`)で `maint packet vCont;s:p1.1`(raw 1命令)を送り、数秒待つ。本物の停止通知が処理され、`Program received signal SIGTRAP` と frame 表示が出る。`interrupt` や `vCont;t` ではこうならない(CPUが既に止まっているため停止通知が出ない)。(2026-11-20) |
| 14 | **(旧記述。原因は §12 で判明: `monitor enable_execute_on_connect` が無かった。走行時のログは GO ではなく `<FFWMCUCmd_GB>`)** 上の状態でも `continue` は `vCont;c` → `OK` で、サーバーは GO を送らない(`<FFWMCUCmd_GO>` は一度も出ない。出るのは STEP、DUMP、SetBM、STOP、REST)。サーバーログの確認: `grep -o "FFWMCUCmd_[A-Za-z0-9_]*" jobs/server.out.log \| uniq -c`。`monitor console_log_enable` を接続直後に出す |
| 15 | all-stop / extended-remote は、e² studio と同じサーバー引数にしても接続時の `g` が E01 で失敗する |
| 16 | `tools/fresh-gdb.sh` に環境変数 `NC=true`(スクリプトで接続)、`SI=true`(stdin モード)を追加 |

## 9. `serial` ジョブの送信機能(2026-11-20、設計 `docs/design-uart.md` §6.1)

| 引数 | 既定 | 内容 |
|---|---|---|
| `port` `baud` `seconds` | — / 115200 / 10 | 従来どおり。`port` は `COMn`、`seconds` ≦ 600。受信は `seconds` の終わりまで続く |
| `tx` | なし(送らない) | 送る文字。**`[npsd+?x-]`(n p s d + - ? x)の1〜200文字だけ**(大文字小文字を区別。それ以外はジョブがエラーで終わる) |
| `txAtSec` | 2 | ポートを開いてから送り始めるまでの秒。`0 ≦ txAtSec < seconds`(範囲外はエラー) |
| `txGapMs` | 50 | 1文字ごとの間隔(0〜1000)。**0 なら全文字を1回の書込みで送る**(連打試験)。間隔を待つ間も受信は取りこぼさない |
| `dtr` | false | `true` なら `DtrEnable` を立ててから開く(`docs/design-uart.md` §7 U4。0バイトのときの再試験用) |

- ログには**受信した文字だけ**を書く(送信の記録は入れない)。形式は従来どおり(`outbox\<id>.log`、UTF-8 BOM)。
- 例(`tools/submit-job.sh serial '<JSON>' <待ち秒>`): `{"port":"COM7","baud":115200,"seconds":12,"tx":"nnnp","txAtSec":3,"txGapMs":400}`
- 確認済み: 引数の検査(`tx":"N"` と `txAtSec` ≧ `seconds` がエラーになる)。**実機の送受信は未確認**(基板側にUART出力がまだ無い)。
- 2026-11-20 20:29: `serial` を seconds=3 で1回実行 → `ポート 'COM7' へのアクセスが拒否されました`(**他のソフトが COM7 を開いている**。ターミナルや e² studio のシリアルビューなど)。ランナーのコードの不具合ではない。使う前に COM7 を開いているソフトを閉じる。

## 10. 標準手順(RFP): ビルド → 書込み+走行 → UART判定 →(必要なら)gdb で停止して読む(2026-11-20 実機で通し確認)

**方針**: 書込み+走行は RFP(`rfp` ジョブ)、判定は UART(`serial` ジョブ)。gdb は「停止して変数を読む」(`attach-read.gdb`)に加え、**§12 の手順なら gdb 自身で RUN(continue)もできる**(2026-11-20 22:0x 実機で2回連続成功)。以前の「gdb の continue で走らせない」は、`monitor enable_execute_on_connect` を知らなかった時期の方針で、**今は無効**(経緯は `docs/debug-report.md` §4.1b〜§4.1g と 11-20 22:0x の追記)。

### 10.1 前提(ユーザー側)

| # | 内容 |
|---|---|
| 1 | e² studio を閉じる(E2 Lite と COM7 を掴むため。ヘッドレスビルドにも必要) |
| 2 | job-runner が動いている(`tools/submit-job.sh ping` が `pong`) |
| 3 | COM7 を開いているソフト(ターミナル等)が無い。ECN1 と CN8 の両方のUSBが接続済み |

### 10.2 コマンド(コピペ用)

| 段 | コマンド | 所要 | 成功の見分け |
|---|---|---|---|
| ビルド | `tools/submit-job.sh build '{"mode":"Headless"}' 300` | 約65秒 | ログ末尾 `OK: ...rx72n_gcc.elf`、`.mot` の更新時刻が変わる。ビルド中は書込みを出さない |
| 書込み+走行 | `tools/submit-job.sh rfp '{"mode":"write"}' 175` | 約65秒 | `Operation successful`、exit 0。ログに `mot:` の更新時刻とサイズが出る |
| UART判定 | `tools/submit-job.sh serial '{"port":"COM7","baud":115200,"seconds":15}' 40` | 15秒 | 下の正規表現。`[hb] tick` が +100 ずつ(1秒ごと)、`err=0`、`i2c_fail=0`、`drop=0` |
| 遠隔操作(任意) | `tools/submit-job.sh serial '{"port":"COM7","seconds":12,"tx":"nnnp","txAtSec":3,"txGapMs":400}' 40` | 12秒 | `[ev] img=N src=uart`(§9、設計 `docs/design-uart.md` §3) |
| 変数読み出し(必要なときだけ) | `NS=true tools/fresh-gdb.sh tools/gdb/attach-read.gdb 40 70` の後に `tools/submit-job.sh server-stop '{}' 30` | 約1分 | `g_app_dbg` が出る。**接続でCPUが止まる**(`Target status=stopped.`)。tick は接続時の値で凍結 |
| 走行に戻す(書込みなし) | `tools/submit-job.sh rfp '{"mode":"reset-run"}' 120` | 約3秒 | `Operation successful`。直後の `serial` で `tick` が 100 から再開(リセットされている) |

- 書込み後に起動行(`[boot]`、`[init]`)も見たいとき: `rfp reset-run` の直後に `serial` を出す(1〜2秒遅れ、`[init]` の途中から見える)。
- UART の期待行(Python `re`、`fullmatch`、仕様は `docs/design-uart.md` §2・§6.4): `\[hb\] tick=(\d+) img=(\d+) err=(\d+) detail=(\d+) pts=(\d+) x=(\d+) y=(\d+) i2c_fail=(\d+) streak=(\d+) found=(\d+) drop=(\d+)`、`\[init\] stage=(lcd|touch|timer|loop) ok=([01]) err=(\d+) detail=(\d+)`、`\[ev\] img=(\d+) src=(touch|uart)`。先頭と末尾の行は途中で切れうるので判定から外す。BOM を取る。

### 10.3 `rfp` ジョブの中身(確定した引数と理由)

固定テンプレート。引数は `mode` だけ(`help`、`doc`、`list`[+`device`]、`write`、`reset-run`)。任意のパス・追加オプション・ID変更・セキュリティ/ロック系は受け付けない。

| オプション | 理由(出典: `docs/rfp-cli` = インストール先 `docs\rfp-cli.md` を `rfp doc` で `jobs\rfp-cli.md` にコピー) |
|---|---|
| `-d RX72x` | `-list-devices` で確認(RX72x が有る) |
| `-t e2l` | E2 emulator Lite(`-list-tools`: `e2l  E2 emulator Lite`、シリアル OBE110008) |
| `-if fine` | `-list-interfaces`: `uart` と `fine`。**E2 Lite+FINE で接続できた**。省略時は 2 wire UART になり、`E3000105: The device is not responding` で失敗した |
| `-auth id FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF` | ID コード(既定)。これが無いと `Enter ID Code (16 Bytes)?` で入力待ちになる |
| `-noquery` | コンソールの問い合わせをキャンセル扱いにして止まらなくする |
| `-noprogress` | 進捗バーでログが崩れるのを防ぐ |
| `-a` | 消去+書込み+ベリファイ(`-e -p -v`) |
| `-run` | 成功したら、切断後にリセットを解除して**走らせる**(既定は `-reset` = リセットのまま。ここが走り出しの鍵) |
| `-sig`(reset-run のみ) | 署名を読むだけ(書込みなし)。`-run` と合わせて「走らせるだけ」にする |
| ファイル | `HardwareDebug\rx72n_gcc.mot` 固定(.mot は S レコード。.elf は渡さない) |

- 実行前に e2-server-gdb / rx-elf-gdb / 残った rfp-cli を止める。実行後に残った rfp-cli も止める。タイムアウトは write 170秒、reset-run 90秒。
- 書込み前に、`.mot` の更新時刻とサイズをログに出す。inbox に `*-build.json` が残っていれば書込みを拒否する。
- 書込まれる領域(2026-11-20 のログ): Code Area 1 `FFC00000-FFCCE27F` と `FFFFFF80-FFFFFFFF`、Config Area 1(OFS)`FE7F5D00-FE7F5D2F` `FE7F5D40-FE7F5D7F`。消去は Data Area 1 `00100000-00107FFF` と Code Area 1 全体。

### 10.4 失敗の見分け

| 症状 | 原因 | 対処 |
|---|---|---|
| `rfp` が応答しない/接続エラー | E2 Lite を gdb サーバー(e2-server-gdb)か e² studio が掴んでいる | `rfp` は実行前に gdb サーバーを止める。e² studio はユーザーが閉じる |
| `Enter ID Code (16 Bytes)?` で止まる | `-auth id` が無い | `rfp` ジョブは `-auth id FF…` を付けている。止まったら強制終了されたログ(`[タイムアウト…]`)になる |
| `E3000105: The device is not responding` | `-if fine` を付けずに(2 wire UART で)接続した(`rfp list` の `-list-speeds` で再現)。FINE 以外では応答しない | `-if fine` を付ける(`write`/`reset-run` は付けている) |
| `Tool Firmware Update: V3.02 -> V3.06` が出る | RFP が初回接続で E2 Lite のファームを更新した(1回だけ。その後の gdb サーバー接続は正常だった) | 対処不要。e² studio が再度ファーム更新を促す可能性はある(未確認) |
| `rfp write` が 2秒で `E4000004: A framing error occurred while receiving data. (BFW: 0354)` で失敗 | gdb サーバーを止めた直後の1回目に1度だけ発生(2026-11-20)。原因未確認(E2 Lite の通信が安定していない?) | **同じジョブをもう1回出す**(2回目は成功)。続けて失敗するなら E2 Lite の USB を抜き差し |
| `serial` が `アクセスが拒否されました` | COM7 を他のソフトが開いている | ユーザーが閉じる(e² studio のシリアルビューも) |
| `serial` が 0 バイト | CPU が止まっている(gdb 接続の後)、または書込み前 | `rfp reset-run` |
| `gdb` ジョブが `Could not read registers` | all-stop で接続した | `NS=true`(non-stop)を付ける |
| gdb 後に `[hb]` が止まる | gdb 接続で CPU が止まる(仕様) | `server-stop` → `rfp reset-run` |

### 10.5 使い分け

| 道具 | 役割 | 注意 |
|---|---|---|
| RFP(`rfp`) | 書込み+走らせる | E2 Lite を専有する間(約65秒)は gdb が使えない |
| UART(`serial`) | 動作判定・遠隔操作 | gdb を繋ぐと `[hb]` が止まる |
| gdb(`fresh-gdb.sh`、走らせて読むなら `gdb-run.ps1`=§12) | 停止して `g_app_dbg` などを読む | 接続で CPU が止まる。走らせ直しは `rfp reset-run`(書込み込みなら `rfp write`) |
| LCD の目視・タッチ | ユーザー | 画面の色・欠け・ちらつき、タッチ切替(B1〜B9) |



## 11. Windows 直接実行(2026-11-20 確認。job-runner 不要)

Windows 上の Claude Code から、e² studio を閉じた状態で直接実行できる(§10 の job-runner は使わなくてよい)。

| 段 | コマンド(PowerShell) | 確認 |
|---|---|---|
| 書込み+走行(約61秒) | `& "C:\Program Files (x86)\Renesas Electronics\Programming Tools\Renesas Flash Programmer V3.25\rfp-cli.exe" -d RX72x -t e2l -if fine -auth id FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF -noquery -noprogress -a -run "D:\workspace\rx72n\rx72n_gcc\HardwareDebug\rx72n_gcc.mot"` | `Operation successful`、exit 0。タイムアウトは 175秒 |
| UART受信(受信専用) | `powershell -NoProfile -ExecutionPolicy Bypass -File D:\workspace\rx72n\rx72n_gcc\tools\serial-read.ps1 -Port COM7 -Baud 115200 -Seconds 15` | `[hb] tick=` が +100 ずつ |

- 引数(`-if fine` `-auth id` `-noquery` `-run` など)の意味は §10.3 と同じ。フレーミングエラー時は §10.4 のとおり1回だけ再実行。
- `serial-read.ps1` は BOM付きUTF-8(PS 5.1 で日本語コメントを読むため)。受信文字だけを標準出力へ。ファイルへ保存するなら `| Out-File -Encoding ascii`。
- 書込み完了から受信開始まで約4秒かかる。`[boot]` `[init]` を見たいときは書込み前に `serial-read.ps1` を別プロセスで起動しておく(未確認。COM7 は書込み中も開けるはずだが試していない)。
- 送信(`n` `p` `s` `?`)機能は無い。必要なら別途作る。
- 事前確認: `Get-Process e2-server-gdb,rx-elf-gdb,rfp-cli,e2studio`(居れば止める。自分で起動したもの以外は殺さない)。
## 12. gdb で RUN(continue)する標準手順(2026-11-20 22:0x 実機で2回連続成功。`tools\gdb-run.ps1`、Windows 直接実行)

| 項目 | 内容 |
|---|---|
| コマンド | `powershell -NoProfile -ExecutionPolicy Bypass -File tools\gdb-run.ps1 -Script tools/gdb/mi-run.gdb -Mi -Verify -SerialSec 45 -GdbTimeout 85`(`-Repeat 2` で2回続けて実行)。約50秒/回 |
| 前提 | e² studio・rfp-cli・他の e2-server-gdb/rx-elf-gdb が居ない(居れば中止。自分以外のプロセスは止めない)。COM7 を他が開いていない。E2 Lite が認識されている |
| 処理 | e2-server-gdb 起動(e² studio と同一引数)→ `GDB: <port>` 待ち+6秒 → `rx-elf-gdb --interpreter=mi2` に `tools/gdb/mi-run.gdb` を1行ずつ流す → 終了時に自分が起動したサーバー/gdb を停止 |
| 合否 | 最終行 `VERIFY: PASS breakAtMain=True GB=2 ticks=[0,763,763] uartHb=7 uartUp=True` |
| ログ | `jobs\gdbrun-<Tag>-<時刻>.{gdb,server,serial}.log`(gdb ログは、送信した行(`>`)と gdb の出力(`O`/`E`)に経過秒を付けたもの。サーバー側は `monitor console_log_enable` 後の FFW ログ) |
| 終了状態 | CPU は**停止**(サーバー切断時に STOP)。走行に戻すには `rfp reset-run`(§10)。LCD は停止中は消えうる |

`mi-run.gdb` の肝(順序を変えない):

1. `-gdb-set non-stop on`、`-gdb-set mi-async on`、`-target-select remote localhost:$PORT`(all-stop は `g` が E01 で接続不可。e² studio も non-stop)
2. `monitor console_log_enable` / `set_internal_mem_overwrite 0-645` / `force_rtos_off`
3. **`monitor enable_stopped_notify_on_connect`**(1回だけ。gdb の thread が stopped になる。`interrupt` と二重にすると gdb が assertion で落ちる)
4. **`monitor enable_execute_on_connect`**(これが無いと `vCont;c` は OK を返すだけで GO を出さない。サーバーログに `<FFWMCUCmd_GB>` が出れば走っている)
5. `-target-download` → `-break-insert -t -h main` → `-exec-continue --thread 1`(main で `*stopped,reason="breakpoint-hit"`)→ もう一度 `-exec-continue --thread 1`(走行)
6. 判定: UART の `[hb] tick=` の増加、または `-exec-interrupt --all` で止めて `g_app_dbg.tick` を main 時点と比較

- GO の確認は `grep -ao "<FFWMCUCmd_[A-Za-z0-9_]*>" jobs/gdbrun-*.server.log | sort | uniq -c`。**`GO` ではなく `GB`(Go to Break)が出る**。過去の試験は `<FFWMCUCmd_GO>` だけを探していた(`GB` も `GPB` も未確認だった)。
- 同じ E2 Lite を RFP と同時に使えない。`gdb-run.ps1` の後に走らせ続けたいなら `rfp reset-run`。
- 隠し monitor コマンド一覧(exe の文字列から。引数なしで受理されるものだけ確認): `enable_stopped_notify_on_connect`、`enable_execute_on_connect`、`ignore_continue_during_connect`、`skip_continue`、`prg_download_start`/`prg_download_end`、`configuration_complete`、`launch_complete`、`all_launches_complete`、`go_all`、`can_go_all`。
- `job-runner.ps1` の `serial` の癖(2026-11-20 確認): `$sp.Open()` 後に `$sp.DiscardInBuffer()` を呼ばないため、COM7 を閉じていた間に OS/ドライバの受信バッファへ溜まった直近 2〜3 秒分(2〜3行)がログ先頭に混入する。そのため `seconds:15` の `UT1` で body が 17〜18 行になり、`judge_ut1.py` の `(1) 13〜16 本` に引っかかることがある(間隔 `tick` 差 100 で実機側は正常)。また、しばらく COM7 を閉じた後の初回オープン時、先頭付近のバッファ行が 1 回だけ化けた事例(`222923-ut3` 2行目)があった。
- サンドボックス環境下の `run_command` から `tools\serial-read.ps1` を直接起動すると `COM7 アクセス拒否` になる場合がある(`job-runner.ps1` 経由の `serial` は正常に開ける)。

### 12.1 追記(2026-11-21): 読み取り用スクリプトと注意

- `tools/gdb/mi-peek-uart.gdb`: 接続 → load なしで continue → 10秒走行 → interrupt → SCI2/ポート/ICU/MSTP/アプリ変数を読む(読み取りのみ)。**接続のたびに MCU はリセットされる(PC=0xffc00000)ので、周辺レジスタは一度走らせて interrupt した後でないと意味のある値にならない**。`mi-peek.gdb` は接続直後の RAM(tick など)だけを見る。
- `tools/gdb/mi-tx-poke.gdb`: 停止中に SCI2 の TDR(0x8a043)へ書いて送信を確認(UART 経路の切り分け用)。
- `gdb-run.ps1` は `jobs\gdbrun-*.timeline.log` に各段の絶対時刻を出す。`serial-read.ps1 -Stamp` は受信行に時刻を付ける。`-Verify` の UART 判定は「最後の単調増加区間が3行以上」。

### 11.1 追記(2026-11-21): 送信と撮影
- `tools\serial-read.ps1 -Port COM7 -Seconds N -Stamp -Tx "d" -TxAtSec 3 -TxGapMs 700`: 送信付き。許可文字は `[npsd+-?x]`(1〜200文字)。範囲外はエラー。リダイレクトで動かすと出力は終了時にまとめて書かれる。
- `tools\cam-grab.ps1 -Out D:/.../jobs/cam/x.jpg -Crop 200:210:700:510 -Rot 2 -Eq brightness=-0.2:contrast=1.6`: LCD 撮影(Logicool HD Webcam C510)。パスは `/` 区切り(bash から渡す時 `\` が消える)。LCD は白飛びするので細部は見えない。

### 12.2 追記(2026-11-21): 画面の検証(フレームバッファ)
- `tools\gdb-run.ps1` のスクリプトに追加した行: `#uart <文字列>`(COM7 へ送信。許可文字 `[npsd+-?x]`。受信は時刻付きで `jobs\gdbrun-*.serial.log`)、`#sync <名前>`(それ以前の gdb コマンドの完了待ち。dump の後に使う)、`#waitnew <regex> <秒>`(直前に送ったコマンドより後の出力で待つ)。
- 表示面の dump: `set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2` → `dump binary memory <path> $fb ($fb+261120)`(約4秒)。`python tools\fb2png.py check <gdb.log> jobs/fb/<接頭辞>` で PNG 化と比較(ホスト描画 `jobs\fb\fb_hostrender.exe` は `tools\fb_hostrender.c` の先頭コメントでビルド)。
- 状態変数(`s_ui` など)は dump より先に進む。動きのある画面は1コマ前の値で比べる。

### 12.3 追記(2026-11-21 CLUT8 実験): gdb-run の注意
- `gdb-run.ps1 -Mi` には **`-NonStop` を付ける**(最初の送信行に BOM が付き、先頭の `-gdb-set non-stop on` が失敗するため。無いと all-stop になり `Could not read registers; remote failure reply '01'`)。
- **`-SerialSec` と `#uart` は併用不可**(COM7 の二重オープン)。送信が要る回は `-SerialSec` なし(受信は `jobs\gdbrun-*.serial.log`)。
- 停止中(interrupt 後)も LCD は映り続け、段階の経過時間(tick)は進まない。gdb 切断で LCD は消える。CLUT メモリ(0xE0800 / 0xE0C00)は CPU から読める。
- スクリプトは `tools\gdb\mi-c8snap.gdb`(CLUT8 実験)、`mi-p1regs.gdb`(製品の GLCDC レジスタ)。

### 12.4 追記(2026-11-25 天気つき製品の表示面の検証): HardwareDebug 以外の ELF のシンボルで gdb する
- `tools\gdb-run.ps1 -Script tools/gdb/mi-wx-fb.gdb -Mi -NonStop -NoElfArg`。`-NoElfArg` で `HardwareDebug\rx72n_gcc.elf` を渡さず、スクリプトで `file <jobs\wxbuild\prod_wx\rx72n_gcc.elf>` を読む(書込んだ版と違う ELF のシンボルで読むと値がずれるので、書込んだ版の ELF を指定する)。
- **`file` は最初の行に置かない**(最初の行に BOM が付いて `Undefined command` で失敗する)。先頭に `-gdb-set confirm off` などの捨て行を1行置く。
- `s_sched.net` / `s_wx_have` / `s_wx_ok_ms` / `s_clk.synced` / `s_wx_temp` / `s_wx_kind` は `set var` で書ける(停止中に書き、走らせて 0.6 秒後に interrupt。10ms ごとに `ui_set_wx` が `s_ui.wx` を作り直すので、**`s_ui.wx` ではなくこの元の変数を書く**)。最初の取得が終わる前に書くと予定表が上書きするので、接続後 14 秒待ってから書く。
- ホスト描画は `tools\fb_hostrender.c` を作り直す(`jobs\fb\fb_hostrender.exe` は 11-22 の古い版。天気つきは `jobs\fb\fb_hostrender_wx.exe`。ビルド行は `tools\fb_hostrender.c` の先頭)。判定: `python -I tools/fb2png.py check <gdb.log> jobs/fb/W --c8 --host jobs/fb/fb_hostrender_wx.exe`。

### 12.5 追記(2026-11-27 stockview の Phase 0): 起動から録る・送信の許可文字・複数ジョブの組み合わせ
- `serial` ジョブの `tx` の許可文字は `[npsd+?xtygz-]`(`job-runner.ps1` 147 行目。`t y g z` は TLS 検証用)。`tools\serial-read.ps1` と `tools\gdb-run.ps1` の許可文字は**まだ従来のまま**(`t y g z` は送れない)。
- **`[boot]` から全部を録る手順**: `serial` ジョブ(`tx` 付き)をバックグラウンドで出す(`(tools/submit-job.sh serial '<JSON>' 160 > out 2>&1 &)`)→ 4 秒待つ → **ランナーを経由せず** `rfp-cli.exe -d RX72x -t e2l -if fine -auth id FF… -noquery -noprogress -sig -run` を直接実行(ランナーは 1 つずつ順に処理するので、`serial` の実行中に `rfp reset-run` のジョブは出せない)。`serial` は先頭に直前の走行の残りが混ざる(`DiscardInBuffer` なし)ので、`[boot]` の行から判定する。
- `serial` の `txGapMs` の上限は 1000ms。15 秒間隔で送るには、`?`(`[help]` を 1 行出すだけ)を 14 個はさむ。
- `rfp-cli -a -run` の書込みは約 82 秒(2026-11-27。旧記録の約 65 秒より長い)。
- 試験版の UART: `[wx] res=fail ... err=<n> http=<n> ms=<n> len=<n>`。`err=4` は HTTP が 200 でない、`err=5` はキー無し(天気用のキーなので想定どおり)、`err=7` はバッファ超過。結果は `docs/debug-report.md`。
