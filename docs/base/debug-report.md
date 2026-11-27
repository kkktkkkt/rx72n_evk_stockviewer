# デバッグ報告(実機・CLI)

作成: 2026-11-19 23:15 / 担当: デバッグ / 対象ELF: `HardwareDebug\rx72n_gcc.elf`(1,839,068バイト、22:47ビルド、ソースより新しい)

## 0. 結論(先に読む)

| 項目 | 内容 |
|---|---|
| **走り出しは RFP で解決** | `rfp write`(Renesas Flash Programmer、`-a -run`)で、消去・書込み・ベリファイ OK、書込み後に基板が走った。`serial` で COM7 に `[hb] tick=...` を受信(tick が +100/秒、`err=0 i2c_fail=0 drop=0`)。2026-11-20 20:53 と 21:0x の2回確認 |
| **gdb の RUN は解決(2026-11-20 22:0x)** | 隠し monitor コマンド `enable_stopped_notify_on_connect` と `enable_execute_on_connect` で gdb の `continue` が走る(サーバーログは `<FFWMCUCmd_GB>`。`GO` ではない)。標準手順は `docs/cli-debug.md` §12。本節の以下の記述(「gdb では走らせない」)と §4.1b〜§4.1g の試験は**解決前の経緯**で、読む必要はない。gdb 終了で CPU は止まるので、走らせ続けるなら `rfp reset-run` |
| 標準手順 | ビルド → `rfp write` → `serial`。gdb で走らせて読むなら §12(`gdb-run.ps1` + `mi-run.gdb`)。停止して読むだけなら `attach-read.gdb` → `rfp reset-run`。`docs/cli-debug.md` §10・§12 |
| gdb 後の走行復帰 | `rfp reset-run`(書込みなし、約3秒)で再開。直後の `serial` で tick が 100 から再開することを確認 |
| 製品コード | 不具合は見つかっていない。UART で `[init]`(touch、timer、loop)が `ok=1 err=0 detail=0`、`[hb]` の `i2c_fail=0 err=0`。LCD の映像は目視(ユーザー申告 B1〜B9 OK) |
| 目視が要る項目 | LCD の色・欠け・ちらつき、タッチ切替。UART では映像の正しさは判定できない |

## 1. 結果の表

| 項目 | 実行したジョブ | 結果 | 証拠 |
|---|---|---|---|
| ランナー生存 | `ping` | 合格 | `pong`(22:56) |
| E2 Lite検出・サーバ起動 | `server-start` | 合格 | `jobs/server.out.log`: `E2LITE へ接続, RX Target` / `E2LITE Rev.0` / `User Vcc 3.3 V` / FW `1.12.00.000` / `GDB: <port>` |
| 待ち受けポート | 同上 | **61234ではない。起動のたびに変わる**(49824, 55756, 60430, 62764, 57272, 50664 …) | ログ末尾の `GDB: <port>`。gdbジョブの `port` に毎回指定。`tools/fresh-gdb.sh` が自動で取り出す |
| rx-elf-gdb の起動 | `gdb` | 修正後に合格 | 初回は終了コード -1073741515(0xC0000135)。e² studio同梱の `plugins\com.renesas.python3.win32*\bin` を PATH に足して解消 |
| gdb接続(all-stop) | `gdb`(probe.gdb 22:5x、allstop1.gdb 23:2x) | **不合格(2回とも)** | `Could not read registers; remote failure reply '01'`。書込み前も書込み後も同じ |
| gdb接続(non-stop) | `gdb` + `nonstop:true` | 合格 | `Thread 1.1 (single core) (running)`。サーバ側は `Target status=stopped.`(gdbの表示は「running」でも、実際は停止中)。メモリ読出しは可能 |
| 書込み(load) | `gdb` + `nonstop:true`(connect-load-run.gdb)23:06 | **完了(照合未実施)** | `Loading section .text/.rodata/...`、`Start address 0xffc00000, load size 819828`、`Transfer rate: 64 KB/sec`。ベリファイ無効(`-uverifyOnWritingMemory= 0`)。`compare-sections` は `Invalid hex digit 112` で失敗 |
| リセット | `gdb`(reset-run.gdb)23:1x | リセットは受理(エラー無し)。**実行再開はしていない** | tick 0x8f2ddb97 のまま変化なし。`Target status=stopped.` |
| `monitor resume` で走るか | `gdb`(resume1.gdb) | **走らない** | tick 0x8f2ddb97 のまま、`Target status=stopped.` ×3。`monitor resume` は単一コアでは効かない |
| `continue -a` で走るか | — | **未実施**(中断) | — |
| E2 Lite再接続 | `server-start`(23:07/23:09) | 不合格 → USB抜き差しで復旧(23:13) | 23:07 `エミュレーターを初期化できませんでした` / `Rx_Init_E1_E20() Failed`。23:09 は接続開始のまま3分以上停止。23:13 後は `ターゲット接続終了` と `GDB: 57272` |
| 孤児プロセス | `server-stop`(改修版) | 無し | `e2studio系プロセス:` 空 |
| **MSTPCRC(0x00080018)** | `gdb`(read-vars.gdb)23:14 | **0xffff0000(bit2=0、RAM2停止ではない)**。MSTPCRB(0x80014)=0xffffffff、MSTPCRA(0x80010)=0x46ffffff。コードが走る前の値 | 出力 `0x80018: 0xffff0000` |
| **RAM2(0x00800000)書込み→読戻し** | 同上 | **合格**。0x5a5aa5a5 を書いて同値を読戻し、元の 0x65ef90c9 に復元 | `readback = 0x5a5aa5a5`、`restored = 0x65ef90c9`。元の値が乱数なので、RAM2へのコピー(`lcd_show`)は**まだ実行されていない** |
| `g_app_dbg` の各項目 | 同上 | **意味のある値は読めていない**(プログラム未実行。乱数) | `err_code=13784243`、`found_count=70` など。実行後に読み直しが必要。`&g_app_dbg=0x1248` |
| シリアル COM7 115200 10秒 | `serial` 23:12 | 受信0バイト。ログ出力は既定で無効なので正常。COM7を開けたことは確認 | exit 0、ログ空。**B11は未確認** |
| `server-stop` | `server-stop` | 実施済み(最後は23:2x) | exit 0 |

## 2. ユーザー確認待ち(私には見えない項目)

E2 Liteを繋ぎ直し、プログラムが走った後に実施。基板のLCDを見る。

| ID | 何を見る/触る | 合格の条件 | 状態 |
|---|---|---|---|
| B1 | 電源投入またはリセットボタンの後、LCDを見る | 2秒以内に画像0(テストパターン)が出る。真っ暗・真っ白・乱れは不合格(その状態を教えてほしい) | ユーザー確認待ち |
| B2 | 画像0の縦バーの色順 | 左から 赤・緑・青・白・黒 | ユーザー確認待ち |
| B3 | 画面の外周 | 白い枠が4辺とも見える(端が欠けない) | ユーザー確認待ち |
| B4 | 画面の右半分を指で1回触る/左半分を触る | 右で次の画像、左で前の画像。3枚で循環。0.3秒以内に切り替わる | ユーザー確認待ち |
| B5 | 右半分を5秒間押し続ける | 切り替わりは1回だけ | ユーザー確認待ち |
| B6 | 2本指で同時に触る | 切り替わらない | ユーザー確認待ち |
| B7 | 10分放置 | 画像が崩れない・ちらつかない。その後もタッチが効く | ユーザー確認待ち |
| B8 | 四隅(左上、右上、左下、右下)を順に1回ずつ触る | 私が `g_app_dbg` の x/y 最小・最大を読んで、`TOUCH_X_MAX` を決める(触るだけでよい) | ユーザー確認待ち + CLIで読む |
| B9 | B1の見え方 | 何も映らない場合、バックライトだけ点いているか(明るく光るか)、全く光らないかを教えてほしい。極性の切り分けに使う | ユーザー確認待ち |
| B10 | — | CLIで `found_addr[]` を読む。ユーザー作業なし | 未実施(接続待ち) |
| B11 | — | ログ出力は既定で無効。COM7に出ないのは正常 | 未確認(有効化していない) |

## 3. 観察して分かったこと(`docs/cli-debug.md` §3 に反映)

| 疑問 | 分かったこと | 確度 |
|---|---|---|
| 接続時にターゲットは止まるか | gdb接続の前から**停止している**(`Target status=stopped.`)。gdbはnon-stopで「running」と表示するが実際は停止。書込み前の接続でも同じ表示 | 観察した |
| 自動で走り出すか | **走り出さない**(reset の後も、`monitor resume` でも)。`continue -a` が必要(未試験) | 観察した / 未試験 |
| タイムアウトで gdb を落としたとき、ターゲットは走り続けるか | **未確認**(そもそも走らせていない) | 未確認 |
| `monitor set_internal_mem_overwrite 0-645` 等は必要か | load は書いて成功。書かない場合は試していない | 一部 |
| gdb接続の1サーバ1セッション | gdb が切れるとサーバは「ターゲットデバッガから切断されました」。次の接続は `could not connect (error 138)`。**gdbジョブごとにサーバを作り直す**(`tools/fresh-gdb.sh`) | 観察した |

## 4. CLIデバッグの確定手順(2026-11-19 23:4x 時点)と、できないこと

**結論**: CLI だけで「接続 → 書込み → 停止状態の確認 → 変数読出し → 切断」は実証できた。**CLI からの「実行再開(走らせる)」は成功していない**。走らせるには、書込み後にユーザーが基板のリセットボタンを押す(または USB 電源を入れ直す)必要がある(この点は未検証)。

| 段階 | 結果 | 根拠 |
|---|---|---|
| 接続 | 成功(non-stop のみ) | all-stop は `g` パケットが E01(接続時にスレッド選択が `Hgp0.0` で失敗)。`set remote multiprocess-feature-packet off` でも同じ |
| **接続するとCPUが止まる** | **止まる**(ユーザーが GUI で走らせたプログラムに接続 → tick が 16285 で凍結) | 接続時に gdb が `vCont;t` を送る。tick 16285 ≒ 163秒 = GUI で走らせていた時間 |
| 書込み | 成功(毎回 load。約13秒、819,828バイト)。load 直後の PC = 0xffc00000(自動リセット済み。`monitor reset` は不要) | `monitor getPC` = 0xffc00000、`x/4xw 0xffc00000` = 0x800273fd 0xfd000027 0x17800a73 0x76040000(ELFの .text 先頭と一致するかは未照合) |
| 1命令実行(raw `vCont;s:p1.1`) | **動く**。PC 0xffc00000 → 0xffc00007 → 0xffc0000e | `maint packet vCont;s:p1.1`(OKが返り、`monitor getPC` で確認) |
| 連続実行(continue) | **動かない**。下の全方式で PC 不変・`Target status=stopped.`・tick 不変 | 表 4.1 |
| 走行中の変数読出し | 検証不能(走らせられていない)。tick が増えるかは未確認 | — |
| 切断 | 成功(`detach`)。サーバは1セッション限り | `ターゲットデバッガから切断されました。` |

### 4.1 試した「走らせる」方式(すべて不成功)

| 方式 | 結果 |
|---|---|
| non-stop `continue -a`(前景) | 返らない(gdb は thread を running と思い込み、何も再開しない)。タイムアウトで強制終了 |
| non-stop `continue -a &` | 走らない。PC 0xffc00000 のまま |
| stdin モード(イベントループが回る) + `interrupt -a` + `continue -a &` | gdb の表示は (running) のまま。走らない |
| `monitor resume,0` / `monitor start_resume` | 前者は受理だが無効、後者は `Protocol error with Rcmd: 05` |
| raw `vCont;c` / `vCont;c:p1.1` / `vCont;c:p1.-1` / `vCont;c:-1` / `vCont;C00:p1.1`(`maint packet`) | 全て `OK` が返るが走らない |
| `hbreak main` + raw `vCont;c` | 走らない(PC 不変) |
| `vStopped` を先に送ってから `vCont;c` | 走らない |
| `QNonStop:0` + raw `c` | `error while fetching packet`、走らない |
| load 直後(reset なし)の raw `vCont;c` | 走らない |
| all-stop で接続 | 接続自体が失敗(上記) |
| ブレーク: `info breakpoints`(gdb)、`monitor printBkPts`(サーバ) | どちらも空。main での停止は観察していない |

### 4.1b 2026-11-20 の追加試験(GO = `<FFWMCUCmd_GO>` がサーバーログに出るか)

サーバーは e² studio と同じ追加引数(`-l -uCore= ... -uSyncMode= async -uFirstGDB= main --gdbVersion= 16.2`)で起動。ログは `monitor console_log_enable`。

| 試したもの | GO | 結果 |
|---|---|---|
| all-stop で `target remote` → load → reset → continue(ユーザー提供の標準手順) | 出ない | **接続で失敗**: `Could not read registers; remote failure reply '01'`(`Hgp0.0` と `g` が E01)。サーバー引数を e² studio に合わせても同じ |
| `target extended-remote`(all-stop) | 出ない | 同じ E01 で接続失敗(`!` は OK、`qAttached` = 0、`?` = S02、`g` = E01) |
| `vRun;` / `vRun;;` / `vAttach;1` | 出ない | `OK` が返るだけ |
| `R00` | 出ない | gdb が `error while fetching packet` |
| `qRcmd run` / `go` / `RUN` | 出ない | `E05` |
| `jump *0xffc00000`(thread が running 表示のまま) | 出ない | `Cannot execute this command while the selected thread is running.` |
| **stdin モード**で raw `vCont;s:p1.1` を送り、本物の停止通知を gdb に処理させる | STEP のみ | **成功**: `Program received signal SIGTRAP ... PowerON_Reset_PC () ... reset_program.S:49` が出て、`info threads` が**停止(frame表示)**になる。gdb の thread 状態を正しくする方法として有効 |
| 上の後に `continue -a &`(gdb が `vCont;c` を送る) | **出ない** | 応答 `OK`、サーバーの命令は DUMP のみ。`Target status=stopped.`、PC 不変(0xffc00007) |
| 上の後に `hbreak main` + `continue -a &` | 出ない | `SetBM`(ブレーク設定)は出るが GO は出ない |
| 上の後に `monitor notify_resumed` / `monitor resume` → continue | 出ない | 無効。`monitor get_current_cores` = `single core`、`get_freerunning_cores` = `no free running cores`、`get_disabled_cores` = `no disabled cores`、`get_core_go_synced_in_target_DLL` = `Protocol error with Rcmd: 05` |
| 上の後に `set $pc` → continue | 出ない | `set $pc` は反映されず(PCが 0xffc00007 のまま)。`vCont;c` に OK だけ |

→ **gdb 側の状態は直せた(stdin + raw step)。それでもサーバーは `vCont;c` に OK を返すだけで GO を E2 Lite に送らない。** 残る手がかりは e² studio の実通信(`docs/e2studio-ffw-log-request.md`)。

### 4.1c e² studio の gdb コンソール(`jobs/e2studio-gdb-console.txt`)の順序を再現(2026-11-20 20:3x)

e² studio は、最初に `monitor set_io_access_width,RW,<4|2|1>,<アドレス範囲>`(71行)を送り、load(「ダウンロード開始/終了」)、ハードウェアBP(`main` = 0xffc08468、`Temporary breakpoint 1`)、再開、の順。71行は `tools/gdb/e2-io-access-width.gdb` に原文のまま保存。

| 試したもの | GO | 結果 |
|---|---|---|
| io幅71行 → `console_log_enable` → init 2行 → load → `hbreak main` → raw `vCont;c:p1.1`(`e2-order-run.gdb`) | **出ない** | `OK` のみ。PC 0xffc00000、`Target status=stopped.`、tick 0 のまま。サーバー命令: WRITE、DUMP、SetBM/GetBM、STOP |
| 同じ順序 + `is_target_connected` ×2 + stdin モードで raw step → `hbreak main` → `continue &`(`e2-order-stdin.gdb`) | 出ない | gdb は stopped に認識(`SIGTRAP`)、`continue` で `Continuing.`、サーバーは GO を出さず(STEP、DUMP、SetBM のみ)。tick 0 |

- io幅の設定は、GO が出る条件ではなかった(少なくともこの形では)。
- `monitor console_log_enable` を e² studio の `optionInitCommands` に入れても、サーバーコンソールに FFW のダンプが出なかった。この欄のコマンドが実行されていない可能性がある。第2回の依頼(`docs/e2studio-rsp-log-request.md`)は、標準の「Startup → Initialization Commands」欄に `set debug remote 1` と `monitor console_log_enable` を入れ、gdb→サーバーの**パケット**そのものを取る。
- ELF は e² studio が再ビルドした UART 付き(load size 842,556 バイト)。書込み済み。

### 4.1d 2026-11-20 RFP での書込み・走行(解決)

| 項目 | 実行したジョブ | 結果 | 証拠 |
|---|---|---|---|
| rfp-cli の場所・ヘルプ | `rfp {"mode":"help"}` | `C:\Program Files (x86)\Renesas Electronics\Programming Tools\Renesas Flash Programmer V3.25\rfp-cli.exe`(CLI V1.18.0)。ドキュメントは `docs\rfp-cli.md`(`rfp doc` で `jobs\rfp-cli.md` にコピー) | ヘルプ出力、`-run`、`-a`、`-auth id`、`-noquery`、`-if fine`、`-t e2l` を確認 |
| デバイス/ツール/IF | `rfp {"mode":"list"}`、`{"mode":"list","device":"RX72x"}` | `RX72x` あり、`e2l  E2 emulator Lite`(OBE110008)、interface は `uart` と `fine` | ログ |
| 失敗1 | `-if fine` 無し(`-list-speeds`) | `[Error] E3000105: The device is not responding`(2 wire UART で接続して失敗)。初回接続で `Tool Firmware Update: V3.02.00.000 -> V3.06.00.000`(E2 Lite のファーム更新) | ログ |
| 失敗2 | `-list-flashoptions`(`-auth` 無し) | `Enter ID Code (16 Bytes)?` で入力待ち → タイムアウト強制終了 | ログ。`-auth id FF…` と `-noquery` で解決 |
| 書込み+走行 | `rfp {"mode":"write"}`(`-a -run`、`.mot` 20:46:58、2,533,766 bytes) | **合格**。`Operation successful`、約61〜68秒。Erase → Write → Verify(Code Area 1 `FFC00000-FFCCE27F`、`FFFFFF80-FFFFFFFF`、Config `FE7F5D00-FE7F5D7F`) | ジョブ 115200-rfp 他 |
| UART で走行確認 | `serial {"seconds":20}` / `{"seconds":15}` | **合格**。`[hb] tick=500 … tick=2600`、毎秒 +100、`err=0 detail=0 pts=0 i2c_fail=0 streak=0 found=0 drop=0` | ジョブ 115314-serial 他 |
| gdb で接続して読む | `gdb`(attach-read.gdb) | 接続で CPU が停止(`Target status=stopped.`)。tick 4023 / 2913 で凍結(書込み/リセットからの経過時間)。`err_code=0`、`i2c_fail_total=0`、`image_index=0` | ログ |
| gdb 後の走行復帰 | `rfp {"mode":"reset-run"}`(`-sig -run`) | **合格**。直後の `serial` で `[init] stage=touch/timer/loop ok=1 err=0 detail=0` → `[hb] tick=100, 200, …` | ログ |
| LCD の表示 | — | **ユーザー確認待ち**(書込み+走行後に表示が出るか) | — |

### 4.1e 2026-11-20 21:2x E2 Lite ファーム更新後の gdb 走り出し再試験(GO = `<FFWMCUCmd_GO>` を `jobs/server.out.log` で厳密一致)

前提: RFP が E2 Lite のツールファームを V3.02 → V3.06 に更新した後。サーバー起動は e² studio と同じ追加引数。毎回 `server-stop` → `server-start` の新しいサーバー。`monitor console_log_enable` 有効。判定は GO の個数と tick。

| # | パターン | スクリプト/環境 | 結果 | GO | tick |
|---|---|---|---|---|---|
| 1 | e² studio 順(io幅71行 → load → hbreak main → raw `vCont;c`)、non-stop | `e2-order-run.gdb`(NS=true) | load OK、`vCont;c` に `OK`、サーバーは STEP/DUMP のみ | 0 | 7164 → 7164 |
| 2 | e² studio 順 + stdin(raw step で本物の停止通知 → `continue &`) | `e2-order-stdin.gdb`(SI=true NS=true) | gdb は「stopped」まで行くが continue 後も GO なし | 0 | 52804 凍結 |
| 3 | all-stop: load → `monitor reset` → continue | `allstop-load-reset-continue.gdb` | 接続で `Could not read registers; remote failure reply '01'`(従来どおり。ファーム更新では直らない) | 0 | — |
| 4 | 走行中のプログラムへ接続 → 停止 → 変数読み → continue(load なし) | `stdin-attach-continue.gdb`(SI=true NS=true) | 接続で CPU 停止(tick 810 で凍結)。gdb は `(running)` のまま。continue しても GO なし | 0 | 810 凍結 |
| 5 | 同上を raw step で stopped にしてから continue(load なし) | `variant-test.gdb`(SI=true NS=true) | raw step で `SIGTRAP`、`info threads` は stopped、`continue &` 後は `(running)` | 0 | 7164 凍結 |
| サーバー | 報告バージョン | `e2-server-gdb` 起動ログ | 1.12.00.000(ツールファーム更新後も変化なし) | — | — |

サーバー引数の別案(`variant-test.gdb`、環境変数 `SX`=`serverExtra`。引数は e² studio の起動ログ(`jobs/e2studio-server-console.txt`)と、`.launch` の同名属性が根拠):

| # | `serverExtra`(既定との差) | サーバーが反映したか | GO | 結論 |
|---|---|---|---|---|
| A | `-uSyncMode= sync`(既定 async) | サーバーログ `Synchronsiation mode:Synchronised` に変化 | 0 | 効果なし |
| B | `-uFirstGDB= main` を外す | `Asynchronous` のまま | 0 | 効果なし |
| C | `-l` と `-uSyncMode=` と `-uFirstGDB=` を外す | `Synchronised`(SyncMode 未指定の既定) | 0 | 効果なし |
| D | `-uExecuteProgram= 1` を末尾に追加(`.launch` の `e2lite.execute.program=false` に対応する引数。元の `= 0` も残るので二重指定) | ログに `= 0` と `= 1` が両方出る。どちらが有効かは不明 | 0 | 効果なし(ただし二重指定のため未確定) |
| 未試験 | `-uDebugMode`、`-uresetOnReload= 0` | — | — | e2-server-gdb にヘルプ(`-h`/`--help`)が無く(`server-help` で確認、引数なしで通常起動するだけ)、Web にも説明が見つからなかった(llvm-gcc-renesas.com のフォーラムは見出しのみ)。意味不明なまま変える試験はしない。`resetOnReload` は load 後のリセット動作で、GO の有無とは別の項目と推定(推定) |

判定: ファーム更新・引数変更では gdb 経由の GO は出ない。サーバーが `vCont;c` を受けても E2 Lite へ GO を発行しない原因は、CLI 側の既知のパケット列では特定できていない。次は e² studio の実パケット(§4.4)との比較が必要。

### 4.1f 2026-11-20 21:4x サーバー引数の置換試験と、サーバーメッセージの比較(GO = `<FFWMCUCmd_GO>` を `jobs/server.out.log` で厳密一致)

方法: `server-start` の新引数 `serverSub`(`uresetOnReload=0|1`、`uDebugMode=0|1|2` のみ許可、値を**置換**して二重指定にしない)。毎回 `server-stop` → `server-start`、`monitor console_log_enable` 有効。`tools/fresh-gdb.sh` は環境変数 `SUB=` で渡す。ID コード、ロック、バンク切替、ダウンロード関連の引数は触っていない。

| # | 引数(置換後、サーバー起動ログで確認) | スクリプト | 結果 | GO | 備考 |
|---|---|---|---|---|---|
| 1 | `-uresetOnReload= 0`(元 `= 1`) | `e2-order-run.gdb`(NS=true) | load OK、`vCont;c` に OK、tick 12943 のまま | 0 | 効果なし |
| 2a | `-uDebugMode= "1"`(元 `"0"`) | `e2-order-run.gdb` | 同上 | 0 | 効果なし |
| 2b | `-uDebugMode= "2"` | `e2-order-run.gdb` | 同上 | 0 | 効果なし。基板に異常なし(後で `rfp reset-run` で走行確認) |
| 3 | `uresetOnReload=0` + `uDebugMode=1`(単独で効果が無かったので参考の組合せ) | `stdin-attach-continue.gdb`(SI=true NS=true) | 接続で停止、gdb は `(running)` のまま、tick 12943 凍結 | 0 | 効果のあった組合せは無いので、attach の再確認は組合せ1回のみ |

サーバーのメッセージ比較(e² studio の成功ログ `jobs/e2studio-server-console.txt` と CLI の `jobs/server.out.log`):

| 行 | e² studio | CLI | 差 |
|---|---|---|---|
| `ターゲット接続状況 - OK` ×2 | あり | `monitor is_target_connected` を2回送ると出る(`e2-order-stdin.gdb`、`load-packet-trace.gdb` で確認。`e2-order-run.gdb` は送らないので出ない) | 差なし(e² は2回送っている) |
| `ダウンロード開始` / `ダウンロード終了` | あり(接続状況と hbreak の間) | **一度も出ない**(load は成功し FFW の `WRITE` は出ているのに) | **差あり** |
| `ハードウェア・ブレークポイントをアドレス…に設定します。` | あり(`0xffc08468`、e² のELF) | あり(`0xffc08ba8`、CLI のELF。アドレス違いはビルド差) | 差なし |
| `ターゲットデバッガから切断されました。` | あり | あり | 差なし |

差の手掛かり(確認したこと):
- CLI の `load` は `$Xaddr,len:...`(バイナリ書込み)だけを約290回送る(`load-packet-trace.gdb` + `set debug remote 1`。`jobs/outbox/123715-gdb.log`)。`vFlashErase`/`vFlashWrite`/`vFlashDone` は送っていない。
- サーバーの memory-map(`qXfer:memory-map:read`)は、フラッシュ `0xffc00000`(長さ 0x400000)を `type="ram"` として返す(`tools/gdb/memmap.gdb`)。gdb は flash 型でないので `vFlash*` を使わず `X` で書く。
- 以上から、e² studio の「ダウンロード開始/終了」は CLI の `load` とは別の経路(サーバー独自のダウンロード開始/終了を促す何かの命令)で出ていると推定(推定)。その経路が、サーバー内部の「プログラム書込み済み → 実行可能」状態を作り、GO の発行条件になっている可能性がある。§4.4 の e² studio パケットログで、`ダウンロード開始` の直前に gdb が送るパケット(`qRcmd`、`vFlash*`、その他)を確認する。
- `monitor help`/`monitor ?` は `Protocol error with Rcmd: 05`(使えない)。

判定: `resetOnReload`、`DebugMode` 1/2 のどれでも GO は出ない。次は上記の「ダウンロード開始/終了」を出させる命令の特定(e² studio のパケットログ待ち)。

### 4.1g 2026-11-20 21:5x 「ダウンロード開始/終了」を出す命令の検証(vFlash* 仮説。GO は `<FFWMCUCmd_GO>` を `jobs/server.out.log` で厳密一致)

仮説: e² studio では `vFlashErase`/`vFlashWrite`/`vFlashDone` 経由のダウンロードでサーバーが「ダウンロード開始/終了」を出し、それが GO の前提になる。毎回 `server-stop` → `server-start`、`monitor console_log_enable` 有効、NS=true。

| 試験 | 送ったパケット(`maint packet`) | サーバー応答 | 「ダウンロード」表示 | FFW コマンド | GO | 備考 |
|---|---|---|---|---|---|---|
| A | 通常の `load`(`X` 書込み、`FFWMCUCmd_WRITE` ×290)→ `vFlashDone` → `vCont;c:p1.1`(`flashdone-after-load.gdb`) | `vFlashDone` → `""`(空) / `vCont;c` → `OK` | なし | DUMP×2、WRITE×290、STOP×1 | 0 | tick 6850 のまま |
| B | load なしで `vFlashDone` → `vCont;c:p1.1`(`flashdone-only.gdb`、書込みなし) | `vFlashDone` → `""` / `vCont;c` → `OK` | なし | DUMP×2、STOP×1 | 0 | tick 6850 のまま |
| D(C の代わりの確認) | `vFlashErase:00800000,0`(RAM アドレス、長さ0)、`vFlashWrite:00800000:`(空データ)(`flash-probe.gdb`、書込みなし。実フラッシュには触れていない) | どちらも `""`(空) | なし | — | 0 | vFlash* は未対応 |
| 参考 | `qSupported:multiprocess+;swbreak+;hwbreak+;vContSupported+` / `vCont?` | `PacketSize=c00;qXfer:memory-map:read+;qXfer:features:read+;qXfer:threads:read+;QStartNoAckMode+;multiprocess+;QNonStop+;swbreak+;hwbreak+;vContSupported+` / `vCont;c;C;s;S;t` | — | — | — | qSupported に vFlash 系の記載なし。`vCont` は c、C、s、S、t |

判定:
- サーバーは `vFlashErase`/`vFlashWrite`/`vFlashDone` を実装していない(空応答)。e² studio の「ダウンロード開始/終了」は vFlash* 由来ではない。仮説は否定。
- C(実フラッシュの消去・書込み)は、A・B・D で vFlash* が未対応と分かったため実施していない。ID コード領域、オプション設定メモリ、ブートスワップ領域、アプリ領域のいずれにも vFlash* は送っていない(P1、P2 の宛先は RAM)。
- 残る手掛かり: 「ダウンロード開始/終了」は別の命令(`qRcmd` の monitor、独自の `q`/`Q` パケット、`X` の書込み方など)で出る。§4.4 の e² studio パケットログで、この2行が出る直前のパケットを確認する。
- 最後の `vCont?` の `t`(stop)は gdb の non-stop 用。再開の `c` は OK を返すだけで GO を出さない点は従来どおり。

### 4.2 接続時スナップショットで読めた値(ユーザーが GUI で走らせていたプログラムの停止時点)

| 項目 | 値 |
|---|---|
| tick | 16285(約163秒) |
| err_code / err_detail | 0 / 0(GLCDC開始・タッチ初期化にエラーなし) |
| i2c_fail_total / i2c_fail_streak | 0 / 0 |
| found_count / found_addr[] | 0 / なし(アドレススキャンは実行されていない、または応答なし。タッチは動いている(ユーザー申告)ので 0x38 は有効) |
| raw_points / image_index | 0 / 0(停止時点でタッチなし、画像0) |
| raw_x / raw_y | 389 / 224(最後に触った位置) |
| x_min / x_max | 5 / 426(ユーザーが四隅を触る前の範囲。B8は未確定) |
| y_min / y_max | 141 / 242 |

(注意: この値は GUI 実行中に接続して止めた時点のスナップショット。その後の `monitor reset` でRAMは消えない。)

### 4.3 次の一手(候補)

1. **ユーザーがリセットボタンを押す**: load(`tools/gdb/load-only.gdb`)→ server-stop → ユーザーがリセットボタン → プログラムが走る。LCD が再表示されるかをユーザーに聞く。
2. その後 `tools/gdb/attach-read.gdb`(接続でCPUが止まるスナップショット)で `g_app_dbg` を読む。tick が小さい値(リセット後の経過時間)なら「新しく書込んだELFが走った」証拠になる。
3. 連続実行の CLI 化は、(a) e² studio が使う `.launch` の初期化コマンド・サーバ設定(`-uExecuteProgram` など)の差、(b) raw RSP のトレース(`set debug remote 1`)を e² studio のログと比較する、で調べる必要がある。**未解決**。

### 4.4 e² studio の RSP パケットログ解析手順(ユーザーの記録待ち)

入力(ユーザーが `docs/e2studio-rsp-log-request.md` の手順で保存する):
- `jobs\e2studio-gdb-console2.txt`(gdb コンソール全文。`set debug remote 1` で `[remote] Sending packet: $...#xx` と `Packet received: ...` が出る)
- `jobs\e2studio-server-console2.txt`(サーバー側。`monitor console_log_enable` で `<FFWMCUCmd_GO>` が出るはず。GO が出る直前の gdb パケットが目的)

手順:
1. サーバーログで `<FFWMCUCmd_GO>` の行番号を探す(`grep -n "<FFWMCUCmd_GO>" jobs/e2studio-server-console2.txt`)。GO が無ければ、e² studio の「再開」が別経路(例: `monitor` コマンド)という意味なので、gdb 側の全 `monitor`/`qRcmd` 行を洗う。
2. gdb ログで Resume(F8)の押下直後の `Sending packet:` を抜き出す: `grep -n "Sending packet" jobs/e2studio-gdb-console2.txt | tail -60`。`$vCont;...`、`$c`、`$Hc...`、`$QNonStop`、`$vStopped`、`$Z1/Z0`(ブレーク)、`$qRcmd,<hex>`(monitor。16進をASCIIに戻して読む: `echo <hex> | xxd -r -p`)に注目する。
3. 「main で止まるまで」(`load` → `hbreak main` → 再開)の区間を、行番号で3つに分ける: (a) 接続〜初期化、(b) load(`vFlashErase`/`vFlashWrite`/`vFlashDone`、`X`/`M` 書込み)、(c) 再開〜停止通知(`%Stop`、`T05...`、`S05`)。
4. 同じ区間を CLI で取る: スクリプト先頭に `set debug remote 1` を入れた gdb ジョブ(`stdin-attach-continue.gdb` が例)。gdb のログは `jobs/outbox/<id>.log` に出る。`[remote] Sending packet:` は `--- gdb stderr` 側に出る(確認済み: `jobs/outbox/143101-gdb.log`)。サーバーは `jobs/server.out.log`。
5. 下の比較表を埋める。差が出た行が「CLI に足りない/余計なもの」。差を1つずつ CLI の `maint packet <hex>` で再現し、GO の有無で判定する(判定は常にサーバーログの `<FFWMCUCmd_GO>` の個数。tick だけで判断しない)。

比較表(記入用):

| 区間 | e² studio(`console2.txt` の行) | CLI(`outbox/<id>.log`) | 差 | 再現試験の結果(GO の個数) |
|---|---|---|---|---|
| 接続直後(`qSupported`、`QNonStop`、`?`、`vStopped`) | | | | |
| スレッド選択(`Hg`、`Hc`、`qfThreadInfo`) | | | | |
| `monitor` 系(`qRcmd` を ASCII に戻す) | | | | |
| load(`vFlash*`、`X`/`M`) | | | | |
| ブレーク設定(`Z0`/`Z1`、`z`) | | | | |
| 再開(`vCont;c` の書式、スレッド指定) | | | | |
| 再開の直前直後に挟まる読み(`m`、`g`、`p`) | | | | |
| 停止通知(`%Stop`、`T05`、`vStopped`) | | | | |

見る順番の目安: 再開の書式(`vCont;c:p1.-1` のようなスレッド指定の違い)→ 再開の直前の `monitor`(`resume`、`notify_resumed` 等)→ ブレーク設定の種類(`Z1` ハードウェア)→ 接続直後のハンドシェイク(`QNonStop:1` のタイミング)。

## 5. 不具合・疑い(コーディング担当へ)

| # | 内容 | 状態 |
|---|---|---|
| 1 | **製品コードの不具合は今のところ見つかっていない**(プログラムをまだ1命令も走らせていない) | — |
| 2 | RAM2 モジュールストップ疑い(R1): 実行前の値は MSTPCRC bit2=0、RAM2 読み書きOK。**否定**。lcd_init 実行後の値は未確認 | 実行後に再確認 |
| 3 | load 後にE2 Liteの初期化が失敗した件。原因不明。疑い: (a) gdb/サーバの強制終了、(b) 書込んだプログラムの影響。**プログラムは未実行なので (b) は考えにくい**(a が有力) | 抜き差しで復旧 |

---

## 追記: 2026-11-19 23:21 ユーザー目視(e² studio GUIで書込み・実行)

| 項目 | 結果 | 出典 |
|---|---|---|
| B1 画像0が出る | **合格**。カラーバーが見える | ユーザー申告 |
| B4 タッチで切り替わる | **合格(概ね)**。触ると画面が切り替わる。画像は3枚: ①カラーバー ②緑と青の四角線の格子 ③青→赤のグラデーションと十字線 | ユーザー申告 |
| 左右の向き(右=次、左=前)、B2(色順)、B3(白枠)、B5(長押し)、B6(2本指)、B8(座標範囲)、B9 | **未確認**(ユーザーの追加確認待ち) | |
| 解決した疑い | RAM2のモジュールストップ(MSTPCRC): 否定。LCDが出る=RAM2とGLCDC、DISP/バックライトの極性、タッチI2C(SCI6、FT5206)の基本動作はすべて成立 | |
| CLIデバッグ | e² studio のGUIで実行したため、CLI経由の「実行→変数読み出し」は未実施。次回 `tools/gdb/template-run-and-read.gdb` で確認する | |


## 追記: 2026-11-20 Windows直接実行での引き継ぎ確認

**結論: 合格。** Windows から rfp-cli と PowerShell を直接実行して、書込み+走行+UART判定まで通った(job-runner 不使用)。

| 項目 | 実行コマンド | 結果 | 証拠 |
|---|---|---|---|
| 事前確認 | `Get-Process e2-server-gdb,rx-elf-gdb,rfp-cli,e2studio` / `Get-Item ...\rx72n_gcc.mot` | 残留プロセスなし。.mot は 2026-11-20 20:46:58、2,533,766 B(期待どおり) | 出力が空 / 日時・サイズ一致 |
| 書込み+走行 | `rfp-cli.exe -d RX72x -t e2l -if fine -auth id FF.. -noquery -noprogress -a -run ...\rx72n_gcc.mot`(§10.2 と同じ) | 合格。1回目で成功、framing error なし。約61秒 | `Operation successful`、exit=0、Verify 2領域 OK |
| UART判定 | `powershell -NoProfile -ExecutionPolicy Bypass -File tools\serial-read.ps1 -Port COM7 -Baud 115200 -Seconds 15` | 合格 | 下記 |

- 受信 18行(先頭=途中切れ行、末尾=最終行を除く 16行を判定)。全行が R-HB に fullmatch、異物 0。
- `tick` は 400 → 2000、差はすべて 100(= 毎秒 +100)。
- `err=0 detail=0 i2c_fail=0 streak=0 found=0 drop=0`、`img=0`、`pts=0`(全行同じ)。
- 抜粋: 先頭 `[hb] tick=400 img=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0` / 末尾 `[hb] tick=2000 img=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0`
- 受信開始は書込み完了の約4秒後(PowerShell 起動分)なので `[boot]` `[init]` は見ていない(UT2 は未実施)。
- 未実施: LCD目視、タッチ、`tx` 送信(`tools\serial-read.ps1` は受信専用)。
## 追記: 2026-11-20 22:0x gdb からの RUN(continue)に成功(Windows 直接実行、`tools\gdb-run.ps1`)

**結論: 達成。** 鍵は2つの隠し monitor コマンド(`e2-server-gdb.exe` の文字列から発見): `monitor enable_stopped_notify_on_connect`(gdb の thread を停止状態にする)と `monitor enable_execute_on_connect`(これを送るまで `vCont;c` は GO を出さない。送ると次の continue で `<FFWMCUCmd_GB>` が出る)。all-stop は従来どおり接続不可(e² studio も non-stop)。

### 試験の表(サーバー=e² studio と同一引数、接続後 6秒待機。全て MI/non-stop、ログは `jobs\gdbrun-<tag>-*.{gdb,server,serial}.log`)

| # | 試したこと | 結果 | GB/GO | 証拠 |
|---|---|---|---|---|
| 1 | all-stop `target remote` → load → hbreak → continue(`std-allstop.gdb`、接続待機あり) | 失敗: `Could not read registers; remote failure reply '01'`。ファーム V3.06 更新後・待機後も同じ | 0 | `gdbrun-allstop1-*.gdb.log` |
| 2 | MI(`mi-e2.gdb`)で e² studio 相当の `-gdb-set`/`-target-select`/`-target-download`/`-break-insert -t -h`/`-exec-continue` | 接続・load OK。gdb の thread が `running` のままで `Cannot execute this command while the selected thread is running` | 0 | `gdbrun-mi1-*` |
| 3 | 上 + `monitor prg_download_start` / `prg_download_end` / `configuration_complete` / `launch_complete` / `all_launches_complete`(`mi-launch1/2.gdb`) | 「ダウンロード開始/終了」と `FFWMCUCmd_DWNP_OPEN` が出る。`prg_download_end` の応答に約23秒。ただし thread は running のまま、GO なし | 0 | `gdbrun-milaunch1/2-*`。「ダウンロード開始/終了」は `prg_download_start/end` で出る(vFlash ではない) |
| 4 | launch 系 + raw step + continue(`mi-launch3.gdb`) | 停止通知は来るが GO なし | 0 | `gdbrun-milaunch3-*` |
| 5 | `monitor go_all` / `can_go_all`(`mi-go1.gdb`) | go_all は無反応(単一コア)。`can_go_all`=true | 0 | `gdbrun-migo1-*` |
| 6 | `monitor enable_stopped_notify_on_connect`(`mi-notify1/2.gdb`) | **効く**。直後に `*stopped`(signal 0 / SIGINT)が来て thread が stopped になる(raw step 不要)。`interrupt` と二重に送ると gdb が `skip_inline_frames` assertion で落ちる(送るのは1回だけ) | 0 | `gdbrun-minotify1/2-*` |
| 7 | 隠し monitor の応答探索(`mi-probe-monitor.gdb`) | 引数なしの `ignore_continue_during_connect` / `enable_execute_on_connect` / `skip_continue` は受理、`,0` `,1` や `start_resume` 等は `Protocol error with Rcmd: 05` | — | `gdbrun-miprobe-*` |
| 8 | 6 の後に `monitor enable_execute_on_connect` → `-exec-continue`(`mi-flags.gdb`) | **GB が出て走った**。UART に `[boot]` `[init]` `[hb] tick=100…`(リセットから) | **GB 1** | `gdbrun-miflags-*`(server.log に `<FFWMCUCmd_GB>`) |
| 9 | `enable_execute_on_connect` だけ(continue なし)(`mi-exec1.gdb`) | 走らない(`Target status=stopped`)。その後の `-exec-continue` で GB、`Target status=running` | GB 1(continue 後) | `gdbrun-miexec1-*` |
| 10 | **標準手順 `mi-run.gdb`**: 停止通知 → execute フラグ → load → `-break-insert -t -h main` → continue(main で停止) → continue → 8秒 → interrupt → tick | `*stopped,reason="breakpoint-hit"... func="main" addr=0xffc08ba8`、続けて走行、interrupt で `app_log_poll` に停止。tick: main 時点 0 → 停止後 763(同値で再読み 763)。UART: `[boot]` `[init]` `[hb] tick=100…700` | GB 2 | `gdbrun-mirun1-*` |
| 11 | **(D) 再現性**: `gdb-run.ps1 -Script tools/gdb/mi-run.gdb -Mi -Verify -Repeat 2 -SerialSec 45` | 2回とも `VERIFY: PASS breakAtMain=True GB=2 ticks=[0,763,763] uartHb=7 uartUp=True` | 各2 | `gdbrun-final-1-20261120-220556.*`、`gdbrun-final-2-20261120-220656.*` |

### 合格条件

| 条件 | 判定 | 証拠 |
|---|---|---|
| (A) 接続 | 合格(non-stop。e² studio の起動構成も `NON_STOP=true`)。all-stop は不可のまま | 表 2、1 |
| (B) main でブレーク停止、サーバーに GB | 合格 | `*stopped,reason="breakpoint-hit",... func="main",... addr="0xffc08ba8"`、server.log `<FFWMCUCmd_GB>` |
| (C) RUN | 合格 | UART `[hb] tick=` が +100/秒で増加、gdb 停止後 tick 763 > main 時点 0 |
| (D) 2回連続 | 合格 | 表 11 |

### LCD の点灯・消灯との対応(ユーザー観察への回答、時刻は PC 時計)

- 各 gdb-run の流れ: サーバー起動(E2 Lite 接続)→ load(リセット)→ continue の GB(main へ)→ 走行(`[hb]` が出る間=LCD 点灯の可能性)→ **interrupt と終了時のサーバー停止で `FFWMCUCmd_STOP`(CPU 停止、`[hb]` 停止)**。終了後は CPU が停止のまま。
- 時刻: final-1 は 22:05:56 開始〜22:06:51、final-2 は 22:06:56〜22:07:51(gdb ログ末尾が 22:06:36 / 22:07:36、server.log が 22:06:51 / 22:07:51)。各回で GB は main 到達用に1回と走行再開用に1回。STOP は interrupt と切断で出る。
- 「点灯 → 消灯」は、GB で走って GLCDC が動く→ STOP/接続・load のリセットで止まる、と時刻が合う。ただし LCD を私は見ていないので、消灯の原因(リセット中 / CPU 停止 / GLCDC 停止)は決めていない。UART の `[hb]` の有無が判断材料(走行中のみ出る)。
- 試験の終了後に `rfp reset-run`(22:08)で走行に戻した。直後の `serial-read` で `[hb] tick=500…1000`(+100/秒)を確認。
- 22:03〜22:04 の試験(`mi-exec1`、`mi-flags`、`mirun1`)も同様。USB 差し直し(約22:01)の直後の最初の gdb 試験は 22:02:03(`miprobe`)で、走らせていない(monitor 応答の探索のみ)。
- 補足: 20:xx〜21:4x の他の担当(別ジョブ)も同じ E2 Lite を使っていた時間帯があり、`jobs\done` に 21:30〜21:41 の gdb/rfp/serial ジョブがある。同時利用の衝突に注意。

### 未確認・注意

- `enable_execute_on_connect` が「走らせる」効果なのか、「走らせた状態での再接続を許す」フラグなのかの意味は未確認(挙動のみ確認)。
- gdb 終了時のサーバー停止で CPU は止まる(STOP)。走らせ続けるには、その後 `rfp reset-run`(書込みなし、約3秒)。
- `tick 763` が3回同一なのは、スクリプトの sleep が決定的なため(UART の `[hb]` は別途 +100/秒)。
- 試験 3 の `prg_download_end` が約23秒かかる理由は未確認(標準手順では不要なので使わない)。

---

## 追記: 2026-11-20 22:25〜22:33 実機 CLI UART 試験(UT1, UT3, UT5, UT6, UT4 UART側, UT2 確認)

### 0. 結論(先に読む)

| 項目 | 結果 | 要点 |
|---|---|---|
| 前提確認 | **OK** | `e2studio` / `rfp-cli` / `e2-server-gdb` / `rx-elf-gdb` なし。`runner.alive` 更新中、`222523-ping` → `pong`。`.mot` は `2026-11-20 20:46:58` / `2,533,766 B`(`build=Nov_20_2026_20:46:57` と整合。なお `src/` 全ファイルはコピー等により `21:26:02` の同一タイムスタンプ) |
| UT3(`s?`) / UT5(`xxxx`) / UT6(`n`×60連打) | **合格** | コマンド応答、未知文字無視、60連打(取りこぼし 0・`drop=0`・`tick` 差すべて `100`)を確認 |
| UT4(`nnnp`) | **UART側のみ確認済み** | `[ev]` 4本(`0→1→2→1`)と `[hb]` 一致を確認。**LCD目視はユーザー確認待ち** |
| UT1(15秒受信) | **実機は正常 / `judge_ut1.py` は条件(1)のみ本数超過** | `tick` 差(`100`)・異物(`0`)・`drop=0`・`err=0`・`i2c_fail=0` は全合格。ただし `job-runner` がポート開放前に溜まった 2〜3 行(`DiscardInBuffer` なし)も読むため、15秒ログの本数が 17〜18 本となり `13〜16 本` の上限を超過 |
| UT2(起動順序) | **既存ログで `[boot]`+`[init]` 4行を確認 / 物理リセットは未実施** | `gdbrun-final-2-20261120-220656.serial.log` 等で確認済み。物理リセットボタン押下による UT2 は**ユーザー操作待ち** |
| 終了時の状態 | **走行中(`[hb]` 出力中)** | `223205-ut2-rfp-reset` 後に `223223-final-hb`・`223304-hb-check` で `[hb]` +100/秒を確認。なお `tick=1600〜6200` の間 `pts=1 x=90 y=80` が継続(ユーザー押下中か要確認、`img=2` 不変) |

### 1. 試験開始時の状態と復旧

- `222537-ut1-serial`: `[ev] img=2 src=touch`、`[hb] tick=14700`、`[hb] tick=14800`、`?` を受信した後に停止。
- `222617-ut1-check`: 受信 0 バイト。`222718-gdb-attach` で確認すると `g_app_dbg.tick=0`、`Target status=stopped.`(リセット直後の停止状態)。
- `222746-rfp-reset-run`(`rfp {"mode":"reset-run"}`, exit 0, 3秒)で走行に復帰させた。

### 2. 実機 UART 試験の結果

| 項目 | 実行したジョブ・コマンド | 結果 | 証拠(ジョブID・ログ抜粋) |
|---|---|---|---|
| UT1(受信のみ 15秒) | `serial {"port":"COM7","baud":115200,"seconds":15}` + `python tests/host/judge_ut1.py` | **条件(2)〜(5) PASS / 条件(1)本数のみ FAIL(`18`, `17`)** ※ランナーがオープン前のバッファ 2〜3 行を先頭に拾うため | `222803-ut1`: `tick=1100..2900`(body 18本, diff `100`, 異物 0, `drop=0`, `err=0`, `i2c_fail=0`, `streak=0`)。`222844-ut1-b`: `tick=5300..7000`(body 17本, diff `100`, すべて正常) |
| UT3(`s` と `?`) | `serial {"port":"COM7","baud":115200,"seconds":12,"tx":"s?","txAtSec":3,"txGapMs":300}` | **合格**(2回目 `223136-ut3-b`。1回目 `222923-ut3` はコマンド応答は正常だが、オープン直前の 2 行目 `tick=9500` に文字化け `i2c_fail\x02??????` あり) | `223136-ut3-b`: 18行全一致(`bad=[]`)。`[st] tick=23087 img=1 err=0 detail=0 pts=0 x=90 y=80 i2c_fail=0 streak=0 found=0 drop=0 xmin=57 xmax=90 ymin=77 ymax=137`、`[help] n=next p=prev s=state ?=help` |
| UT5(未知文字無視) | `serial {"port":"COM7","baud":115200,"seconds":10,"tx":"xxxx","txAtSec":3,"txGapMs":100}` | **合格** | `222951-ut5`: `R-EV`/`R-ST`/`R-HELP`/`R-ERR` 0本。`[hb] tick=12000..13300`(diff `100`), `img=2` 不変, `drop=0` 不変 |
| UT6(60連打・UX3/UX5) | `serial {"port":"COM7","baud":115200,"seconds":14,"tx":"n"×60,"txAtSec":3,"txGapMs":0}` | **合格** | `223018-ut6`: `R-EV` ちょうど 60本(`src=uart`), `0→1→2` 循環で最後 `img=2`(開始 `b=2`)。`R-ERR code=7` 0本、`drop=0` 全一致、`tick=14600..16300` 差すべて `100`(≦130)。`15200..15300` に 51本、`15300..15400` に 9本(≧0.6秒) |
| UT4(UART側 `nnnp`) | `serial {"port":"COM7","baud":115200,"seconds":12,"tx":"nnnp","txAtSec":3,"txGapMs":400}` | **UART側のみ確認済み / LCD目視はユーザー確認待ち** | `223053-ut4-uart`: `b=2`(`tick=18600`) → `[ev] img=0/1/2/1 src=uart`(4本) → `[hb] tick=18800..19500 img=1`, `drop=0` |
| UT2(起動順序) | 既存 `gdbrun-final-2-20261120-220656.serial.log` 参照 / `serial-read.ps1`+`rfp` 併用試行(COM7 アクセス拒否で不可) | **既存ログで順序確認済み / 物理リセット押下は未実施(ユーザー確認待ち)** | `gdbrun-final-2`: `[boot] fw=rx72n_gcc build=Nov_20_2026_20:46:57 ch=2 baud=115200` → `[init] stage=lcd/touch/timer/loop ok=1 err=0 detail=0` → `[hb] tick=100` |
| 終了時走行確認 | `rfp reset-run` → `serial` 10秒 / 6秒 | **走行中** | `223205-ut2-rfp-reset`(exit 0) → `223223-final-hb`(`tick=1700..2600`) → `223304-hb-check`(`tick=5700..6200`, `img=2`, `err=0`, `i2c_fail=0`, `drop=0`, `pts=1 x=90 y=80`) |

### 3. 残っているユーザー操作・目視待ち項目チェックリスト

以下は画面の目視や実機への物理操作が必要なため、CLI デバッグ担当では「未実施(ユーザー確認待ち)」とする。

- [ ] **UT2 (物理リセットボタン)**: 20秒シリアル取得中に基板のリセットボタンを押し、`[boot]` と `[init]` 4行が出ることを確認
- [ ] **UT4 (LCD目視)**: `nnnp` 送信(または同等の操作)に合わせて、実際に LCD の絵が切り替わることを目視確認
- [ ] **UT7 (タッチ操作 + UARTログ一致)**: 右半分タップ → 2秒待ち → 左半分タップ → 2秒待ち → 右半分3秒長押し → 離す → 2本指タッチ(`src=touch`・押し続けで追加切替なし・2本指で反応なし)
- [ ] **UT8 / B8 (座標範囲)**: 開始後30秒以内に四隅(左上→右上→左下→右下)を順に触り、末尾の `[st]` の `xmin/xmax/ymin/ymax` を確認して `TOUCH_X_MAX` を確定
- [ ] **UT9 / B7 (10分放置)**: 600秒シリアル計測と表示崩れ・ちらつき・放置後タッチの目視確認
- [ ] **B2 (画像0の色順)**: 縦バーが左から 赤・緑・青・白・黒 に見えるか
- [ ] **B3 (外周白枠と四隅)**: 外周の白い枠 4辺と四隅の四角(左上マゼンタ・右上黄・左下シアン・右下オレンジ)が欠けずに見えるか
- [ ] **B4 の向き**: 右半分で「次」、左半分で「前」に進むか
- [ ] **B5 (5秒長押し)**: 切り替わりが1回だけか
- [ ] **B6 (2本指同時タッチ)**: 切り替わらないか
- [ ] *(参考・要確認)* 終了時チェック(`223304-hb-check`)で `pts=1 x=90 y=80` が継続していたが、その後の `224014-post-gdbrun-hb` では `pts=0 x=60 y=186` に戻っていた(タッチ操作によるものと見られる)

---

## 追記: 2026-11-20 22:38 `tools\gdb-run.ps1` による GDB 接続・ダウンロード・`main` ブレーク・実行試験

### 0. 結論(先に読む)

| 項目 | 結果 | 要点 |
|---|---|---|
| 事前確認 | **OK** | `jobs/inbox` 空、`e2studio`/`rfp-cli`/`e2-server-gdb`/`rx-elf-gdb` なし、`HardwareDebug\rx72n_gcc.elf`(`1,896,388 B`, `2026/11/20 20:46:58`)確認 |
| GDB 接続・ダウンロード・実行(`gdb-run.ps1`) | **合格(`VERIFY: PASS`)** | `breakAtMain=True`, `GB=2`, `ticks=[0,763,763]`。`-target-download` (`844,536 B`) → `main` (`0xffc08ba8`) 一時ブレーク停止 → `tick=0` → 8秒走行 → 停止 `tick=763`(再読 `763`)を確認 |
| UART 出力(`.serial.log`) | **正常起動・走行を確認**(`uartUp=False` 判定の注記あり) | `[boot]` → `[init]` 4行(`ok=1`) → `[hb] tick=100..700` を受信。`uartUp=False` は、ダウンロード前に先行走行中だった `tick=35100..35500` が先頭に入り、終了値 `700` より大きかったため |
| 終了後の走行復帰 | **走行中** | `223923` は E4000004(GDB切断直後1回目)で失敗後、2回目 `223936-post-gdbrun-reset2` で `rfp reset-run` 成功。`224014-post-gdbrun-hb` で `[hb] tick=3300..4000` (`pts=0 x=60 y=186`, `drop=0`, `err=0`)を確認 |

### 1. 実行内容と証拠(`jobs\gdbrun-gemini-gdbrun-20261120-223802.*`)

- 実行コマンド: `powershell -NoProfile -ExecutionPolicy Bypass -File D:\workspace\rx72n\rx72n_gcc\tools\gdb-run.ps1 -Script tools/gdb/mi-run.gdb -Tag gemini-gdbrun -Mi -Verify -SerialSec 45 -GdbTimeout 85`
  - サンドボックス内では `e2-server-gdb` の作業フォルダ参照で `DirectoryNotFoundException` となったため、`BypassSandbox: true` で実行。
- 結果行: `VERIFY: PASS  breakAtMain=True GB=2 ticks=[0,763,763] uartHb=11 uartUp=False`

| 確認項目 | ログファイル | 結果・抜粋 |
|---|---|---|
| 接続・ダウンロード | `jobs\gdbrun-gemini-gdbrun-20261120-223802.gdb.log` | `^done,address="0xffc00000",load-size="844536",transfer-rate="555888",write-rate="2912"` |
| `main` ブレーク停止 | 同上 | `Temporary breakpoint 1, main () at ../src/rx72n_gcc.c:19` (`addr="0xffc08ba8"`, `monitor getPC`=`0xffc08ba8`, `g_app_dbg.tick`=`0`) |
| 走行 → `interrupt` 停止 | 同上 | 8秒走行後 `-exec-interrupt --all` で `app_log_poll ()` (`app_debug.c:126`) に停止。`g_app_dbg.tick`=`763` → 2秒後再読 `763` |
| サーバー FFW コマンド | `jobs\gdbrun-gemini-gdbrun-20261120-223802.server.log` | `port=64882`, `<FFWMCUCmd_WRITE>`: 290, `<FFWMCUCmd_SetBM>`: 2, **`<FFWMCUCmd_GB>`: 2**, `<FFWMCUCmd_STOP>`: 2 |
| UART シリアル | `jobs\gdbrun-gemini-gdbrun-20261120-223802.serial.log` | 先行走行分 `tick=35100..35500` の後、`[boot] fw=rx72n_gcc build=Nov_20_2026_20:46:57 ch=2 baud=115200` → `[init]` 4行(`ok=1`) → `[ev] img=2 src=touch` → `[hb] tick=100..700`(`pts=1 x=90 y=80`, `err=0`, `i2c_fail=0`, `drop=0`) |
| 後処理(走行復帰) | `223936-post-gdbrun-reset2`, `224014-post-gdbrun-hb` | 1回目 `223923` は `E4000004 (BFW: 0354)` で失敗、再実行 `223936` で `Operation successful` (exit 0, 3秒)。`224014` で `tick=3300..4000` (`+100/秒`, `pts=0 x=60 y=186`, `err=0`, `i2c_fail=0`, `drop=0`)を確認 |

---

## 追記: 2026-11-20 23:07〜23:11 4枚スライド版(`img_03.c`, `APP_IMAGE_COUNT=4`) 書込み・UART 検証

### 0. 結論(先に読む)

| 項目 | 結果 | 要点 |
|---|---|---|
| 事前確認・`.mot` | **OK** | 残留プロセスなし、`runner.alive` 正常、`jobs/inbox` 空。`HardwareDebug\rx72n_gcc.mot` (`2026-11-20 22:53:18`, `3,317,134 B`) |
| 書込み(`rfp write`) | **合格** | `230721-rfp-write-4img` 1回目で成功(`Operation successful`, exit 0, 76秒, `CRC=83F247B1`) |
| 4枚循環 UART(`UT1`/`UT4`/`UT6`) | **UART側は全合格** | `[hb]` +100/s・`drop=0`・`err=0`・`i2c_fail=0`。`nnnp` で `3→0→1→2→1`、60連打で 60本(`0→1→2→3` 循環、最後 `img=1`=`b`、`code=7` 0本)。実機タッチ操作(`src=touch`)による `img=3` や `1→0→3→2` のログも観察 |
| 新ビルド時刻(`build=`) | **確認済み** | `jobs\4img-boot-check.serial.log` で `[boot] fw=rx72n_gcc build=Nov_20_2026_22:53:17 ch=2 baud=115200` と `[init]` 4行(`ok=1`)を確認 |
| 終了時状態 | **走行中(`img=0`)** | `231116-rfp-reset-4img` → `231140-final-4img-hb` で `[hb] tick=1700..2600`, `img=0`, `pts=0 x=0 y=0`, `err=0`, `i2c_fail=0`, `drop=0` を確認(ユーザーがすぐ `0→1→2→3` を確認できる状態) |
| LCD 目視項目 | **未実施(ユーザー確認待ち)** | 4枚目の絵(`img_03`)の表示品質・色・切替目視はユーザー確認待ち |

### 1. 実機検証結果(4枚スライド版)

| 項目 | ジョブID / 実行方法 | 結果 | 証拠(ログ抜粋) |
|---|---|---|---|
| 書込み+走行 | `230721-rfp-write-4img` (`rfp {"mode":"write"}`) | **合格** | `Load: "...rx72n_gcc.mot" (Size=3317134, CRC=83F247B1)`, Code Area 1 (`FFC00000-FFD0DE7F`, `FFFFFF80-FFFFFFFF`)・Config Area 1 照合 OK, `Operation successful` |
| UT1 (12s受信) | `230850-ut1-4img` (`seconds:12`) | **合格** | `tick=1200..2400`(差 `100`), `err=0`, `i2c_fail=0`, `streak=0`, `drop=0`。※直前のタッチ操作(`x=309 y=215`)により `img=3` となり 4枚目への遷移を確認 |
| UT4 UART側 (`nnnp`) | `230917-ut4-4img` (`tx:"nnnp"`, `txAtSec:3`, `txGapMs:400`) | **UART側のみ確認済み / LCD目視待ち** | 開始 `b=3`(`tick=4300`) → `[ev] img=0, 1, 2, 1 src=uart`(4本) → `[hb] tick=4600..5200 img=1`, `drop=0` |
| UT6 (60連打 `n`×60) | `230945-ut6-4img` (`tx:"n"×60`, `txAtSec:3`, `txGapMs:0`) | **合格** | 開始 `b=1`(`tick=7100`) → `[ev] src=uart` ちょうど 60本(`0→1→2→3` 循環)、最後 `img=1`(=`b`)、`R-ERR code=7` 0本、`drop=0`、全 `tick` 差 `100`(≦130)。※`7500..7600` 間に実機左半分タッチ(`x=51 y=180`)で `[ev] img=0, 3, 2 src=touch` も正常記録 |
| `[boot]` 時刻・走行復帰 | `231116-rfp-reset-4img` + `serial-read.ps1` (`BypassSandbox:true`) → `231140-final-4img-hb` | **合格(`img=0` 走行中)** | `jobs\4img-boot-check.serial.log`: `[boot] fw=rx72n_gcc build=Nov_20_2026_22:53:17 ch=2 baud=115200` → `[init]` 4行(`ok=1`) → `[hb] tick=100..400 img=0`。<br>`231140-final-4img-hb`: `[hb] tick=1700..2600 img=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0` |

### 2. 4枚スライド版 ユーザー目視・タッチ確認チェックリスト(未実施)

- [ ] **B4 (4枚循環タッチ)**: `img=0` の状態から画面右半分をタップし、`0 → 1 → 2 → 3(添付画像) → 0` の順に切り替わり、左半分タップで `0 → 3 → 2 → 1 → 0` と戻るか
- [ ] **B4b (4枚目画像の表示確認)**: 4枚目(`img=3`)の添付画像が、色化け・上下左右の欠け・崩れ・ちらつきなく正しく表示されるか
- [ ] **UT4 (LCD目視)**: UART の `n` / `p` 操作(`nnnp`)に合わせて、LCD の表示が実際に切り替わるか
- [ ] **UT7 (タッチ操作 + UARTログ一致)**: 右タップ → 左タップ → 3秒長押し(追加切替なし) → 2本指タッチ(反応なし)で `[ev] src=touch` と表示が一致するか




## 追記: 2026-11-21 10:05 ユーザー依頼の再試行(`gdb-run.ps1` + `mi-run.gdb` を1回)

ログ: `jobs\gdbrun-retry-20261121-100550.{gdb,server,timeline,serial}.log`。この回から `gdb-run.ps1` は `*.timeline.log`(各段の絶対時刻)を出し、`serial-read.ps1 -Stamp` は受信行に時刻を付ける。

| 時刻 | 事象(gdb ログの経過秒) |
|---|---|
| 10:05:51.0 | e2-server-gdb 起動 |
| 10:05:54.7 | `GDB: 57533` を確認(6秒待機) |
| 10:06:02.7 | gdb 起動(経過 0) |
| 10:06:06.5 | 接続(`*running`、経過 3.8) → 10:06:07.0 停止通知(`monitor enable_stopped_notify_on_connect`、PC 0xffc00000) |
| 10:06:07.1 | `monitor enable_execute_on_connect`、`-target-download` 開始(1,105,660 バイト) |
| 10:06:23.6 | load 終了(`^done,address="0xffc00000"`、経過 20.84。約16.5秒) |
| 10:06:23.6〜23.9 | continue → サーバーログ `<FFWMCUCmd_GB>`(1回目)→ main で停止(`reason="breakpoint-hit" func="main" addr="0xffc08ba8"`、経過 21.13)。`g_app_dbg.tick`=0 |
| 10:06:23.9 | 2回目の continue(`*running`、経過 21.22)→ `<FFWMCUCmd_GB>`(2回目) |
| 10:06:32.0 | interrupt(経過 29.21 送信、29.28 停止、`applog_q_peek`) |
| 10:06:35〜37 | tick を読む: 763、763(経過 32.2、34.2) |
| 10:06:36.9〜37.0 | `-gdb-exit`(vKill → サーバーログ `<FFWMCUCmd_STOP>`)、サーバー停止 |

判定(この回のログのみ): (A) 接続 = 合格(non-stop、load 成功)。(B) main 停止 + GB = 合格。(C) RUN = **gdb 側は合格**(tick 0 → 763)。**UART は不合格ではなく計測不能**: `serial` ログは 0 バイト(`[boot]` も `[hb]` も無し)。

- UART の無音の切り分け(10:07〜10:10): `rfp -sig -run` の後も COM7 で 115200/9600/57600/230400/921600 のどれでも 0 バイト、DTR/RTS を立てても 0。COM7 はデバイスとしては存在(`USB シリアル デバイス (COM7)` 状態 OK)。
- しかしプログラムは走っている: reset-run(10:08:02)の約53秒後に gdb で接続して読むと、`g_app_dbg.tick=5294`(約53秒ぶん)、`err_code=0`、`s_log_ready=true`、ログキュー `s_q` に `[hb] tick=4500…5200` が積まれている。つまりファーム側は `[hb]` を作っている。**ホストに届いていない**(CN8 の USB ケーブル/ブリッジ側の疑い。原因は未確定)。
- `HardwareDebug\rx72n_gcc.elf` は 2026-11-20 22:53 に再ビルドされている(`g_img_03` を追加し画像4枚。`src/app/images.c/h` の差分)。今回の load はその ELF。UART 以外の挙動(tick、main 停止)は前回(22:0x、旧ELF)と同じ。
- 前回(11/20 22:0x)の UART 成功と今回の無音の差は、ELF の差ではなくホストへの経路の差と見られる(ファーム側のキューは生成済み)。未確定。

## 追記: 2026-11-21 10:1x UART 無音の切り分け(読み取りのみ。製品コード・ビルド・書込みは無し)

**結論: 10:13 以降の再試験では UART は正常に出た。無音だった 10:05〜10:13 の間も、MCU 側の SCI2 は正常に送信を完了していた。原因は MCU の外側(USB ブリッジ/ケーブル側の一時的な不通)と推定するが、未確定。** 無音の間にユーザーが CN8 に触ったかは私には分からない。

| 項目 | 結果 | 証拠 |
|---|---|---|
| 無音の間の MCU 側(`mi-peek-uart.gdb`、10:13 実行。接続→load なしで continue→10秒走行→interrupt→読み出し) | SCI2 は設定済みで送信完了状態: SMR=0x00、BRR=0x56、SCR=0xf0(TIE/RIE/TE/RE)、SSR=0x84(TDRE=1、TEND=1)、SCMR=0xf2、SEMR=0x58。BRR/SEMR は 60MHz PCLKB で約115200bps(ABCSE=1 の計算で約114.9k、手計算) | `gdbrun-peekuart-20261121-101318.gdb.log` |
| 端子・クロック・割り込み | MSTPCRB=0xddffffff(bit29=0、SCI2 動作)、PORT1 PMR=0x0c、P12PFS=P13PFS=0x0a(RXD2/TXD2)、IPR62/63=2、IER07=0x40(bit6 のみ)、IR[63]=1、DTCER63=0、GENBL0=0x20 | 同上 |
| アプリ側 | `s_log_ready=true`、`s_q.count=0 head=6 tail=6 drops=0`(全行を送信側に渡し済み)、`*s_log_hdl` の `tx_idle=true`、`baud_rate=115200` | 同上。前回見えた「キューに [hb] が溜まっている」は、リングバッファに残った古い内容(count=0)だった。キューは詰まっていない(前回の観察を訂正) |
| 接続時にリセットされる | gdb 接続の直後の PC は常に 0xffc00000 で、周辺レジスタはリセット値になる。周辺の状態は「continue で走らせて interrupt した後」にしか読めない(RAM は保持される) | 各 peek ログ |
| PC→MCU(受信) | 'n' を2回送った後に `image_index=2`。**ただしユーザーが LCD を触っていた時間帯で、`[ev] src=touch` が出ている。この結果は受信の証拠にしない** | `gdbrun-peekrx2-*` |
| MCU→PC(送信)の切り分け | CPU 停止中に gdb から SCI2 の TDR(0x8a043)へ 0x55 を6回書くと、PC の COM7 に 0x55 が1バイトずつ届いた(10:19:43〜45)。同じ回、走行中の `[boot]` `[init]` `[hb]` `[ev]` も COM7 で受信できた | `gdbrun-txpoke-*`。10:19:33 の受信ログ |
| 再現(UART 回復後) | 10:20:10 の reset-run で `[boot] fw=rx72n_gcc build=Nov_20_2026_22:53:17`(新ELF)、`[hb]` +100/秒。標準手順の再実行も UART 付きで合格(下) | 受信ログ |
| 5つのボーレート、DTR/RTS | 無音の間は 115200/9600/57600/230400/921600 の全てで 0 バイト、DTR/RTS を立てても 0 | 10:10 の試験(無音期間) |

標準手順の再実行(UART 回復後、`-Verify`):

| 回 | 結果 |
|---|---|
| retry2(10:20:38〜10:21:21) | gdb 起動 10:20:49.0、load 終了 10:21:08.1(経過19.0)、main 停止 10:21:08.3(`addr=0xffc08ba8`、GB)、continue 10:21:08.5(GB 2回目)、UART `[boot]` 10:21:08.59、`[init]`×4 08.64〜08.93、**最初の `[hb] tick=100` 10:21:09.93**、`[hb] tick=700` 10:21:15.94、interrupt 10:21:16.5(経過27.47)、tick 762。判定 `breakAtMain=True GB=2`。`uartUp` は古い tick(2600〜2900=リセット前)が混ざって False だった。**判定ロジックの不具合**(`gdb-run.ps1` を修正済み: 最後の単調増加区間で判定) |
| retry3(10:21:3x〜10:22:3x) | `VERIFY: PASS breakAtMain=True GB=2 ticks=[0,763,763] uartHb=7 uartUp=True(last run 7 lines)` |

- 旧 ELF と新 ELF の比較(map の比較、案D)は、無音の原因が MCU 側に無いと分かったので実施していない。
- 終了状態: 最後に `rfp-cli -sig -run`(10:22:49)で走行に戻し、COM7 で `[hb] tick=500/600…`(img=3 は、ユーザーのタッチ操作の結果)を確認。残留プロセスは 0。
- 注意: 接続の都度 MCU がリセットされるので、gdb で読む周辺レジスタは「走らせて interrupt した後」の値(`mi-peek-uart.gdb` の方法)。

## 追記: 2026-11-21 11:19 デバッガ接続確認(読み取りのみ)— **接続できず(USB 未認識)**

| 項目 | 結果 | 証拠 |
|---|---|---|
| 事前: 残留プロセス | なし(e2-server-gdb / rx-elf-gdb / rfp-cli / e2studio) | 11:19:29 |
| Windows のデバイス | **ボード側の USB デバイスが1つも見えない**: VID_045B(Renesas。E2 Lite も CN8 の COM7)が現在接続なし。`USB シリアル デバイス (COM7)`(VID_045B&PID_8111)は「未接続(Present=False)」。COM ポートは Bluetooth の COM3/COM4 のみ。他の USB(NexiGo カメラ、Logitech、ハブ等)は OK | `Get-PnpDevice` |
| `rfp-cli -d RX72x -list-tools` | e2l / e2 / e1 / e20 を表示(ツール種別の固定一覧で、実機の検出ではない) | 11:19 |
| gdb 接続(`tools/gdb-run.ps1 -Script tools/gdb/mi-connect-only.gdb -Tag conn1`。load なし) | **失敗**。e2-server-gdb が `GDB の操作 'ターゲットに接続' は 'エミュレータと接続できません。デバイスドライバがインストールされていないか、エミュレータが接続されていません' のエラーで失敗` と出して終了。gdb 本体は起動していない | `jobs\gdbrun-conn1-20261121-111954.server.log` |
| `rfp-cli -sig -run`(走行復帰の試み、1回) | `[Error] E3000201: Cannot find the specified tool.` | 11:20:51 |
| ユーザー側のダイアログ | 「デバッグ接続に失敗しました(E1/E20 デバイスへの接続がオープンできません)」(e² studio 側と見られるが、どのプロセスが出したかは `MainWindowTitle` で確認できず未特定) | ユーザー申告 |

判断: PC から見て E2 Lite と CN8 のどちらも存在しない。ユーザー申告(USB カメラ接続後に PC 全体の USB が不調、USB オーディオも不可)と合う。原因(ケーブル抜け、USB コントローラ/ハブの不調、ドライバ)は未確定。再試行は止めた。基板の走行復帰(`rfp reset-run`)は、E2 Lite が見えるまで不可。
- 復旧後の確認順: (1) PC 再起動後に基板の USB(E2 Lite 用 ECN1 と CN8)を直接 PC へ接続、(2) `Get-PnpDevice -PresentOnly | ? InstanceId -match VID_045B` で E2 Lite と COM7 が出ること、(3) `mi-connect-only.gdb`(接続のみ)、(4) `rfp-cli -sig -run` で走行へ。

## 追記: 2026-11-21 13:2x 浴室リモコンデモ P1 の新ビルド確認(途中。UART が無音のため UT10〜UT19 は未実施)

| 項目 | 結果 | 証拠 |
|---|---|---|
| USB | E2 Lite(VID_045B&PID_82A0)と COM7(VID_045B&PID_8111)を認識 | `Get-PnpDevice` 13:2x |
| 書込み+走行(CLI §11 の rfp-cli 1行、`-a -run`) | **成功**(`Operation successful`)。`.mot` = 2026-11-21 11:46:55、6,319,566 B。13:21:40〜13:23:55(約2分15秒)。Code Area 1 `FFC00000-FFE023FF` と `FFFFFF80-FFFFFFFF`、Config を消去・書込み・ベリファイ | `jobs\p1-rfp.log` |
| 書込み中に COM7 を開けるか | 開けた(エラーなし)。ただし書込み後も受信 0 バイト(下) | `jobs\p1-write-serial.log`(0 バイト) |
| LCD(カメラ Logicool HD Webcam C510、`tools\cam-grab.ps1`) | 書込み+走行後と reset-run 後にホームらしい画面。**見えたもの**: 左に大きい「21:45」、下段に薄い青・橙・緑の点(湯はり・追い焚き・使用量のアイコンらしい位置)。**見えなかったもの**: LCD が白飛びし、ヘッダー・設定カード・ボタンの枠や文字は判別できない(明るさ補正でも不変)。色・文字の細部、位置の精度、動きは判定不可 | `jobs\cam\after-write.jpg`、`lcd-home3.jpg`、`final-home.jpg`(人物が写るので jobs\ 配下のみ) |
| 画面送り(`n` を COM7 へ送信、1.2秒後に撮影) | 「21:45」が消えて白一色になった(別の画面に変わったと見られる。ただし白飛びで画面名は判別不可、`[ev]` も受信できず未確定) | `jobs\cam\after-n.jpg` |
| gdb で状態を読む(`mi-p1-state.gdb`、load なし。接続でリセット → 5秒走行後 / さらに10秒走行後) | `tick` 458 → 1456、`err_code=0`、`i2c_fail_total=0`、`scr=0`、`demo=0`、**`frames=1`、`uf=1`(どちらも2回の読みで同じ)**。停止位置は `memcmp()`(0xffc00045/47) | `jobs\gdbrun-p1state-*.gdb.log` |
| UART(COM7、115200) | **無音(0 バイト)**。書込み直後〜200秒、reset-run の前に開いた状態、`?` 送信後、DTR/RTS の3通り、いずれも 0 | 13:21〜13:30 |
| MCU 側の UART(`mi-peek-uart.gdb`、13:27) | SCI2 は旧ビルドの正常時と同一: SCR=0xf0、SSR=0x84(TDRE/TEND=1)、BRR=0x56、SEMR=0x58、P12/P13 PFS=0x0a、PMR=0x0c、MSTPCRB bit29=0、IPR=2、`s_log_ready=true`。つまりファームは送信を完了している | `jobs\gdbrun-p1peekuart-*.gdb.log` |

- 判断: **MCU からホストへの UART だけが届かない**。PC→MCU は動いている可能性が高い(`n` で画面が変わった。ただし `[ev]` が見えず、画面名は未確認)。前回(11/21 10:05〜10:13)も同じ症状で、10:18〜10:22 の間だけ正常だった(その時間帯はユーザーが LCD を触っていた)。**原因は未確定**。MCU 側ではなく CN8(RL78/G1C ブリッジ、ケーブル、コネクタ)の接触不良の疑いがあるが、確認はしていない。
- 気になる値: `uf=1`(GLCDC のアンダーフローを起動後に1回検出し、その後は増えない)。設計 R2〜R4 の判断材料として、UART の `[hb]` の `uf` と `[perf]` を取れてからでないと評価できない。`frm=1` のままなのは、ホームが静止している(動きがない)ため自然。
- 未実施: UT10〜UT19(デモ、n/p/+/-、[perf])。UART が戻るまで進められない。
- 今回追加: `tools\serial-read.ps1` に `-Tx` `-TxAtSec` `-TxGapMs`(許可文字 `[npsd+-?x]`、検証のみ確認。実機の送信は `?` `n` で書込み自体は成功したが、応答の受信は未確認)。`tools\cam-grab.ps1`(`-Out` 必須、`-Crop w:h:x:y`、`-Rot 1|2`、`-Eq`、`-Frames`。LCD 用に `-Crop 200:210:700:510 -Rot 2` で LCD 付近を切り出して正立にできる)。`tools\gdb\mi-p1-state.gdb`。
- 注意: `serial-read.ps1` を `Start-Process -RedirectStandardOutput` で動かすと、出力はプロセス終了まで書かれない(バッファ)。

## 追記: 2026-11-21 13:3x〜13:48 浴室リモコンデモ P1 の実機確認(UART 復旧後。ファーム build=Nov_21_2026_11:46:53、`.mot` 11:46:55)

**結論: UART で判定できる項目(UT10〜14、16〜18)は合格。フレームバッファを gdb で読み、ファームと同じ `ui_render` を PC で同じ状態から描いた画像と全画素一致(15枚)。不具合は1件(`uf` が起動時に1回出る)。未実施: タッチ(UT15、UT19、V0 の座標)と、目視でしか分からない項目。**

### 1. UART(`tools\ut-run.ps1`。COM7 を先に開き、`rfp-cli -sig -run` でリセット後、時刻付きで記録。ログ `jobs\ut\UT*.log`)
| UT | 結果 | 証拠 |
|---|---|---|
| 起動 UT18 | `[boot] fw=rx72n_gcc build=Nov_21_2026_11:46:53` → `[init]`×4(lcd/touch/timer/loop すべて ok=1 err=0)→ **`[perf] n=50 copy_us=800 home_us=2000 slide_us=1800`**(目標 ≤8000 / ≤12000 / ≤12000 に対し余裕あり。分解能 200µs)。gdb で `applog_fmt_perf` の引数も読み(r4=800、スタック 2000 / 1800)一致 | `UT10.log`、`gdbrun-uf-*` |
| UT10 順送り | `n` ×5 で `[ev] scr=` が fill / reheat / usage / standby / home の順、from も正しく、すべて `src=uart`。最後の `[hb]` が `scr=0`、`drop=0`。**合格** | `UT10.log` |
| UT11 逆送り | `p` ×5 で standby / usage / reheat / fill / home。**合格** | `UT11.log` |
| UT12 温度 | `+`×8: `btn=h_up st=down` → `st=act`(押下から約130〜160ms)→ `set=43..48`(6本)。7回目・8回目は `set=` が出ない。`-`×2 で `set=47`、`46`。`s` で `[ui] ... set=46`。**合格** | `UT12.log` |
| UT13 デモ全走 | `demo=start src=uart` から `demo=end` まで `[ev]` **36行**(設計どおり)。順序・`src=demo`・`scr=` の列(fill/home/reheat/home/usage/home/standby/home)すべて台本どおり。時刻のずれは最大 約20ms(例: h_up down 22.186 = 開始 21.392+0.8、done=fill +11.34、done=reheat +19.34、s_wake +31.73、demo=end +33.0)。`demo=1` の `[hb]` は 33本連続の後 `demo=0`、最後は `scr=0 demo=0`。`err` なし、`drop=0` | `UT13.log` |
| UT14 中断(UART) | 開始 +5.7秒(湯はり中)で `demo=abort src=uart` と `scr=home from=fill src=uart`。以後 `scr=0 demo=0`、`[ev]` なし。**合格** | `UT14.log` |
| UT16 通常速度 | `scr=reheat` から `done=reheat` まで 16.0秒(`[hb]` 17本。設計 16±2)。`done=fill` なし。**合格** | `UT16.log` |
| UT17 連打 | 60文字を1回の書込みで送信 → 押下 4回ぶん(`set=43..46`)、`[err]` なし、`drop=0`、`[hb]` が1秒ごとに継続。**合格** | `UT17.log` |
| UT15、UT19 | **未実施**(実タッチが必要。ユーザー離席) | — |

### 2. フレームバッファ(gdb で表示面を dump → `tools\fb2png.py`。表示面は GR2FLM2 から求め、s_back と一致を確認)
- 手段: `tools\gdb\mi-fbdump.gdb`(静的な各画面)、`mi-demo-fb.gdb`(デモ中)、`mi-press-fb.gdb`(押下)。UART の `n` `+` `d` は `tools\gdb-run.ps1` の `#uart` で送信、`interrupt` で止めて `s_ui` `g_app_dbg` を print、表示面を dump。比較は `fb2png.py check`。基準は (a) 同じ状態を `tools\fb_hostrender.c`(ファームと同じ ui_render)で描いた画像、(b) `tools\assets\bath\preview\fw_*.png`。
- 連鎖の確認: ホスト描画(preview と同じ状態)と preview 8枚(home / fill_run / fill_done / reheat_run / reheat_done / usage / usage_growing / standby)は **全画素一致**(`fb_hostrender` は preview の基準と同じ描画)。

| 画面(実機の状態) | 実機 vs 同状態のホスト描画 | 実機 vs preview |
|---|---|---|
| ホーム(set=42、bath=40.0、clock=21:45) | **100%(0px)** | **100%**(fw_home) |
| 湯はり走行(水位 5.0%) | 100% | 93.6%。差は水位バー・数字(preview は水位 55.5%)と下の数字スロットのみ(36,94)-(262,131) など |
| 追い焚き走行(38.4℃) | 100% | 98.2%。差は温度数字・バー(preview は 39.1℃) |
| 使用量(流量 9.8、棒 640ms) | 100% | 98.3%。差は流量数字のみ(26,81)-(129,122)(preview は 10.3) |
| 待機 | 100% | **100%**(fw_standby) |
| ホーム(浴槽 38.4 で戻った) | 100% | 99.9%。差は浴槽温度の数字(372,12)-(387,23)のみ |
| ホーム(set=43) | 100% | 99.5%。差は設定数字と浴槽温度のみ |
| 湯はり 33.2%(デモ中、タグ付き) | 100% | 差は水位とデモ中タグ(228,5)-(311,28) |
| 湯はり 78%(デモ中) | 100% | 同上 |
| 追い焚き 39.4℃ / 完了 43.0℃(デモ中、set=43) | 100% / 100% | 差は温度と「デモ中」タグ |
| 使用量(棒 640)/ 待機(デモ中) | 100% / 100% | 使用量は温度・流量差、待機は別状態 |
| ホーム+デモ中タグ | 100% | **100%**(fw_home_demo) |
| **スライド途中(湯はり→ホーム)** | 状態の `tr_dx` は dump より1コマ新しい。**`tr_dx=369` で再描画すると全画素一致(0px)**(402 では不一致。表示面は1フレーム前) | — |
| 押下中(h_up、h_dn)、白光中(h_up) | 100% / 100% / 100% | 白光は fw_home_flash_up と、設定数字(367,82)-(395,124)を除いて一致(set=43 と 42 の違い) |
- 15枚すべて、実機の表示面は設計どおり(画素単位)。RAM2・GLCDC の面入れ替え・RGB565 リトルエンディアンの経路に問題なし。LCD パネル上の見た目(色、欠け、ちらつき)は目視。
- `tr_dx` の例: 実機の状態変数は draw より先に進む。**動きのあるコマの比較は、状態をそのまま使わず1コマ前の値を探す必要がある**(静止画面は常に100%)。

### 3. デモ台本の時刻(gdb で約0.87秒ごとに停止して状態を読んだ。停止中はデモ時間が進まない)
`jobs\gdbrun-fbB-*.gdb.log` を `経過s = tick×10 − demo_t0` で整理。`demo_i` は台本の進み(0〜10)。
| 経過s | 状態 |
|---|---|
| 1.7 / 2.5 | scr=0、set=44 → 43、押下フェーズ(ph=2 / btn=2 は h_up の白光) |
| 3.4 | scr=1 へスライド(tr=1)、水位 0.5%、btn=3(h_fill) |
| 4.3〜12.2 | 水位 11.3 → 22.3 → 33.2 → 44.6 → 55.7 → 66.7 → 78.0 → 89.0 → 99.7 → 100.0%(約 11 %/秒 = 8秒で100%。設計 11.35 で完了) |
| 13.1 | scr=0 へスライド(tr=1) |
| 14.9〜19.3 | scr=2、浴槽 38.5 → 43.0℃(4倍速) |
| 20.1 | 追い焚き完了画面(bath 43.0) |
| 21.0 | scr=0 へスライド |
| 22.8〜23.6 | scr=3、棒 210 → 640ms(伸びる)、流量 9.2〜10.3 |
| 27.2 | btn=14(u_back)の押下中 |
| 28.9 | scr=4(待機) |
(台本の最後の復帰・終了は UT13 の `[ev]` で確認済み。停止による読み出しでは 28.9 秒で打ち切った)

### 4. `uf`(アンダーフロー)
- 8回のリセットすべてで、`[hb] tick=100 uf=0` → `tick=200 uf=1`、その後デモ・連打・画面送りでも **uf=1 のまま増えない**。
- 原因の切り分け(`mi-uf-trace.gdb`): `touch_init`(`lcd_init` の直後)で止めた時点で **GLCDC.STMON = 0x4(GR2UF が既に立っている)**。`applog_fmt_perf` の時点でも 0x4(perf 計測が原因ではない)。走行1.3秒後に 0x0(最初の `lcd_underflow_poll` が読んでクリアし、`uf` が 1 になる)。つまり **GLCDC の表示開始時に1回だけ検出される**(起動時の初期のデータ取り込みが間に合わない)。
- **コーディング担当へ戻す**: `uf` の定義(`design-uart.md` §10.2「0 であるべき」)に対し、起動時の1回が常に付く。対処案 (a) `lcd_init` の直後(または最初の表示面の確認後)に `lcd_underflow_poll()` を1回呼んで捨てる、(b) 設計側で「起動後の増分が0」と定義し直す。映像への影響は未確認(目視)。

### 5. その他の数値(R2〜R4 の判断材料)
| 項目 | 値 |
|---|---|
| 1コマ描画 | `copy_us=800`(全画面コピー)、`home_us=2000`、`slide_us=1800`(UART と gdb の両方で確認) |
| デモ中の `frm`(表示の入れ替え回数) | 開始〜終了で 約620(設計の見積 200〜600 を少し上回る。湯はり中は 約55コマ/秒でほぼパネルのリフレッシュ上限) |
| 押下 → 実行 | 約127〜160ms(設計: 最短 90ms、仮想タップ 150ms) |
| 画面遷移 | `frm` の増加から約15コマ/スライド |
| 停止位置 | interrupt の停止は常に `memcmp()`(メインループの表示キー比較。待ち時間が長く、描画に余裕がある) |

### 6. (旧版。最終版は末尾の「ユーザー目視の手順 最終版」を使う)UART で判定できないこと・ユーザーの目視
| ID | 触る場所と順序 | 合格(見えれば) |
|---|---|---|
| V0 | ホームの四隅(左上・右上・左下・右下)を1回ずつ触る。続けて `s` を UART で送って `[st]` の `xmin xmax ymin ymax` を見る(私が実施) | 右上は「＋」、左下は「湯はり」付近で反応、反対側のボタンは反応しない。`xmax` が約 480、`ymax` が約 272 |
| V6 | 「＋」を1回押す | 押した瞬間に暗くなり、離すと白く光り、設定温度が 42 → 43 |
| V1〜V5 | 各画面を見る | モックと同じ色・文言・位置。日本語が欠けていない(画素は設計どおりと確認済み。パネル上の見え方が対象) |
| V7 | 「湯はり」→「ホーム」 | なめらかにスライド。ちらつき・割れがない。待機への出入りは即切替 |
| V10・V11 | 「デモ」ボタン → 約33秒 / 途中で画面に触れる | 台本どおり動く、終了後ホームで止まる / 触ると即ホームに戻りタグが消える |
| V13 | 画面の右端・下端 | 画素の欠け・1行/1列のずれがない |
| UT15、UT19 | デモ開始の 8〜14秒後に画面を1回触る(UT15)/ ホームで「＋」「湯はり」「ホーム」を順に押す(UT19) | `demo=abort src=touch` と `scr=home from=fill src=touch`、タッチで `btn=` なし / `btn=h_up st=down` → `st=act` → `set=43`、`btn=h_fill ...` → `scr=fill from=home src=touch` |

### 7. 今回追加したツール
`tools\ut-run.ps1`(UT 用。リセット→送信→時刻付き受信)、`tools\serial-read.ps1 -Tx`、`tools\cam-grab.ps1`、`tools\fb2png.py`、`tools\fb_hostrender.c`(ビルドコマンドは先頭コメント。`jobs\fb\` に exe)、`tools\gdb-run.ps1` に `#uart` `#sync` `#waitnew`、`tools\gdb\mi-fbdump.gdb` `mi-demo-fb.gdb` `mi-press-fb.gdb` `mi-uf-trace.gdb` `mi-fbtest0.gdb`。
- 注意: COM7(CN8)は挿し直し後に回復した。前回の無音の原因は CN8 の接触と推定(未確定)。
- 終了状態: 基板は走行中(13:47:50 に `rfp-cli -sig -run`、`[hb] tick=600/700 scr=0` を受信)。残留プロセス 0。

## 追記: 2026-11-21 13:53〜14:08 修正版ビルドの再確認(`.mot` 13:52:33、6,319,710 B。build=Nov_21_2026_13:52:30)

**結論: 修正(uf の初回破棄、D1〜D3)は実機で確認できた。回帰 UT も合格。不具合は見つからなかった。UART が1回無音になった(下記)。**

| 項目 | 結果 | 証拠 |
|---|---|---|
| 書込み+走行(§11 の rfp-cli 1行) | **成功**。13:53:12〜13:55:30(約2分18秒)、`Operation successful`。書込み中も COM7 は開けた(旧プログラムの `[hb]` の後に新しい `[boot]` を受信) | `jobs\p1b-rfp.log`、`p1b-write-serial.log` |
| 起動ログ | `[boot] ... build=Nov_21_2026_13:52:30` → `[init]`×4(ok=1 err=0)→ `[perf] n=50 copy_us=800 home_us=2000 slide_us=1800`(前回と同じ) | `p1b-write-serial.log` |
| **`uf`(8回リセット)** | **8回すべて** `[hb] tick=100〜500` が `uf=0`(以前は tick=200 から `uf=1`)。デモ・連打・画面送りの後も `uf=0` | `jobs\ut\R1〜R8.log`、V13 |
| UT10 順送り | fill / reheat / usage / standby / home、`scr=0`。合格 | `V10.log` |
| UT11 逆送り | standby / usage / reheat / fill / home。合格 | `V11.log` |
| UT12 温度 | `set=43..48`、`47`、`46`、`st=act` 10回(= 8 + 2)、`[ui] set=46`。合格 | `V12.log` |
| UT13 デモ | `[ev]` **36行**、`demo=1` の `[hb]` 33本。開始 14:... から done=fill **+11.34秒**、done=reheat **+19.34秒**、demo=end **+33.0秒**(台本どおり)。`uf` `err` `drop` に異常なし | `V13.log` |
| UT16 | reheat 入りから `done=reheat` まで 16.0秒、`done=fill` なし。合格(1回目 `V16.log` は UART が無音のため判定不可、2回目 `V16b.log` で合格) | `V16b.log` |
| UT17 | 60文字を1回で送信 → 押下 4回(`set=43..46`)、`[hb]` 継続、`uf=0` | `V17.log` |

### D1〜D3(`tools\ut-run.ps1 -Tx -Tx2 -Tx2AfterSec` で2回目の `d` を指定時刻に送信)
| ID | 条件 | 結果 | 証拠 |
|---|---|---|---|
| **D1** デモの仮想押下の最中に `d` で中断 | 開始(`d`)から 0.80 / 0.84 / 0.88 / 0.92 秒後(h_up の窓 0.8〜0.95 秒)と 2.20 / 2.26 秒後(h_dn の窓)に中断 | 6回中、`btn=h_up (or h_dn) st=down src=demo` の直後(押下中)に `demo=abort src=uart`。**中断後 5秒間、`st=act` も `set=` も出ない**。`[hb]` は `scr=0 demo=0` を維持。**合格**(中断前の押下が3秒後に動く不具合は出ない) | `D1_0.80〜D1_2.26.log` |
| **D1 の画面** | 上と同じ条件で gdb で表示面を dump(中断の約 0.06秒後、押下は `down` 済み、`act` 前) | `fb_phase=0 fb_btn=0`(押下表示が残っていない)。ホーム、デモ中タグ付きホーム、中断直後、中断の3秒後の4枚が**ホスト描画と全画素一致(0px)**。中断でタグが消え、`set=42` のまま | `gdbrun-fbD3-*.gdb.log`、`jobs\fb\D_*.png` |
| **D2** `+` の直後(30〜140ms)に `d` | 30 / 70 / 110 / 140 ms | `+` の `btn=h_up st=down src=uart` が出た後に `demo=start`。**`+` の `st=act src=uart` は出ず、デモ最初の押下は `set=43`(`+` の効果なし)**。以後は台本どおり。**合格**。補足: UART `+` の `down` だけが出て `act` が出ない(押下は取り消し)。`[ev]` の対が揃わないのは仕様の範囲と見なすが、設計の確認を推奨 | `D2_0.03〜0.14.log` |
| **D3** 戻るスライド中の中断 | 戻るスライド(`scr=home from=fill`、250ms)の開始 +29 / +96 / +157 / +205 ms で `d` | 4回とも `demo=abort src=uart`。以後 `scr=0 demo=0` が続き、余計な `[ev]` なし。**合格** | `D3_13.00〜13.18.log` |

### 気づいた点(不具合ではない)
- **UART が1回無音になった**: UT16 の1回目(13:58:59〜)は、送信 `n` `n` の記録だけで `[boot]` も `[hb]` も受信 0 行。同じ回の前後の UT は正常。MCU 側ではなく CN8 側の一時的な不通と見られる(原因未確定)。UART の判定が必要な時は、`[boot]` の有無で受信できているかを先に確認する。
- 本来「起動ログの先頭が欠ける」ことはなく、`[boot]` から全部受信できた。

### ユーザー目視の手順 最終版(`design.md` §12.4、`design-uart.md` §10.8 に対応。UART と画素では確認済み。パネル上の見え方と実タッチが対象)
**前提**: 基板は走行中、CN8 の USB が PC に接続済み(COM7)。私は UART で `s` `[hb]` と `[ev]` を同時に見る。ユーザーは指で触るだけでよい。ホームから始める。

| # | ユーザーの操作 | 合格(目で見て) | 私が UART で見るもの |
|---|---|---|---|
| V0 | ホームの **四隅**(左上→右上→左下→右下)を指で1回ずつ触る。1回ごとに1秒あける | 触った場所のボタンだけが反応する(右上=「＋」付近、左下=「湯はり」付近)。反応しない場所に何も起きない | `[hb] x= y=`、`[st] xmin xmax ymin ymax`(`s`)。向き(`TOUCH_*`)の判断 |
| V6 | 「＋」を1回、ゆっくり押して離す | 押した瞬間に暗くなり、離すと白く光り、設定温度が 42 → 43 | `btn=h_up st=down` → `st=act` → `set=43` |
| V1〜V5 | ホーム、湯はり、追い焚き、使用量、待機を見る(ボタンで移動) | モックと同じ配色・文言・位置。日本語が欠けていない。時計 21:45 | `[ev] scr=` の順序。画素は設計どおりと確認済み |
| V7 | 「湯はり」を押す → 「ホーム」で戻る → 「電源」を押す → 画面中央を押す | 右から左へなめらかにスライド(約0.25秒)。ちらつき・割れ・半分だけ新しい絵がない。待機は即切替、中央を押すと即ホーム | `scr=fill from=home`、`scr=standby`、`btn=s_wake` → `scr=home` |
| V8 | 「湯はり」を押して32秒待つ。または「追い焚き」で16秒待つ | 水位 / 温度が進み、完了で「湯はり完了」と緑の「完了」ボタンに変わる | `done=fill`(+32秒)/ `done=reheat`(+16秒) |
| V9 | 「使用量」を押す | 流量が揺れ、棒が下から伸びる | `scr=usage` |
| UT15 | ホームの「デモ」ボタン(上部中央)を押し、**8〜14秒後**(湯はりの画面)に画面のどこかを1回触る | 即ホームに戻り、タグが消える。触った場所のボタンが動かない | `demo=start src=touch`、`demo=abort src=touch`、`scr=home from=fill src=touch`、その後 `btn=` が出ない |
| V10 | 「デモ」ボタンを押して約33秒見る(触らない) | 「デモ中」タグが出て、温度 → 湯はり → 追い焚き → 使用量 → 待機 → ホームの順に自動で進む。終了後はホームで止まる | `[ev]` 36行、`demo=end` |
| UT19 | ホームで「＋」「湯はり」「ホーム」を順に押す。最後にホームの四隅近くを押す | 押したボタンだけが動く | `btn=h_up` → `set=43`、`btn=h_fill` → `scr=fill`、`btn=f_home` → `scr=home`、ボタン外で `btn=` が出ない |
| V11 | デモ中に画面を1回触る(UT15 と同じ) | 即ホーム、タグが消える | 同上 |
| V12 | 10分放置 | 画面が崩れない・ちらつかない。その後もタッチが効く | `[hb] tick` が増え続け、`err=0 drop=0 uf=0` |
| V13 | 画面の右端と下端、四隅の外周を見る | 画素の欠けや1行/1列のずれがない | — |

- 実施の目安: V0 → V6 → V7 → UT15・V10・V11(デモ)→ V8(32秒待ち)→ UT19 → V12(10分)。全体で 約20分。
- 注意: ユーザーの目視で不合格が出たら、画面の状態(どの画面で何をしたか)を教えてほしい。私は UART と gdb で表示面を読んで原因を切り分ける。

### 今回追加
`tools\ut-run.ps1 -Tx2 -Tx2AfterSec`(2回目の送信を指定時刻に)、`tools\gdb\mi-abort-fb.gdb`(中断直後の表示面の検証)。
- 終了状態: 基板は走行中(14:07:57〜 `[hb] tick=300〜600 scr=0 uf=0` を受信)。残留プロセス 0。

## 追記: 2026-11-21 23:16〜23:35 CLUT8 表示の実機実験 E1〜E4(`design.md` §19、`design-uart.md` §11)

**結論(先に): 機械判定(UART・gdb のレジスタ・面とパレットの dump)は E1〜E4 すべて合格。不具合なし。カメラでは「`SEL` が垂直同期より前に効く」現象が 3秒の窓で一度も出なかった(= E2 の核の前提が実機で成立)。ユーザー目視(V1a〜V4)は未実施。P2b の入口条件は、機械判定の範囲では満たした。**
対象: 実験ファーム(`APP_CLUT8_TEST=1`、`jobs\c8build\clut8exp\`、build=Nov_21_2026_22:31:25)。書込み 23:16〜23:18(約140秒、`Operation successful`、E4000004 などの失敗なし)。実験後は製品版を書き戻し済み(§7)。

### 1. E1〜E4 の結果

| 項目 | 方法 | 期待 | 結果 | 証拠 |
|---|---|---|---|---|
| C8-U1 起動 | UART(reset-run) | `[init] stage=lcd ok=1 err=0 detail=0`、段階0 `ev=start fb=a pal=a rc=0 ven=0 uf=0` | **合格**。`sel=0`(記録) | `jobs\ut\c8_boot.log` |
| **E1** C8-E1-R レジスタ | gdb、段階0で停止 | §19.4.1 の表 | **合格**(§3 の表) | `jobs\gdbrun-c8snap-20261121-232759.gdb.log` |
| E1 C8-E1-D 面・パレット | `c8png.py cmp / cmppal / png / sample` | 面 0 バイト差、パレット 0 エントリー差、PNG 0 画素差 | **合格**。面 0/139264、CLUT0・CLUT1 とも 0/256、PNG の差 bbox = None、`sample` の箱8個 = 赤・緑・青・黄・シアン・マゼンタ・白・灰、余り 480〜511 は全部 idx 9 | `jobs\c8\gdb\face_s0.bin` `pal0_s0.bin` `pal1_s0.bin`、`s0.png` = `ref.png` |
| E1 C8-E1-V 目視 | ユーザー | V1a〜V1f | **未実施(ユーザー確認待ち)**。カメラの部分的な所見は §2 | — |
| **E2** C8-E2-U 段階1 | UART(2回) | `flip` 6行、fb b,a,b,a,b,a、`sel` 毎回反転、`ven=1`、`rc=0`、間隔 1000±30ms | **合格**(2回とも)。間隔 1012/987/1016/998/1000 ms、`sel` は交互(start は sel=1)、`done fb=a pal=a` | `jobs\c8\log\v2.serial.log` 23:22:57〜23:23:03 |
| E2 C8-E2-U 段階2 | UART | `nr` の `sel` 反転 → 3000±30ms 後に `commit` | **合格**。`nr`→`commit` = 3000 / 3002 ms。`nr` の `ven=0`、`commit` の `ven=1`、`rc=0` | 同 23:23:05.568 / 08.568 / 11.570 / 14.572 |
| E2 C8-E2-U 段階3 | UART | `stress_end` の `n` = 500〜600、`m` = 0 | **合格**。`n`=595、`m`=0(2回とも同じ)。`[hb]` の `frm` が 624 → 1220(+596)で連続 | v1/v2 の `ev=stress_end`、`[hb]` |
| E2 C8-E2-R 「`nr` と `commit` の間」 | gdb、段階2の t=2410 ms で停止 | `GR2FLM2` = 面A、`SEL` 反転済み、`VEN` = 0、使用中の側は パレットA のまま | **合格**。`FLM2`=0x800000、`CLUTINT`=0x10119(`SEL`=1)、`VEN`=0、CLUT0(使用中だった側)= パレットA、CLUT1 = パレットB(マーカー緑)。CLUT0 は基準 pal_a と 0 差、CLUT1 は基準 pal_b_green と 0 差 | SNAP s2_between1、`pal*_s2_between1.bin` |
| E2 追加: 2つ目の窓 | gdb、段階2の t=7900 ms で停止 | (設計に数値なし)`nr`(パレットA)済み・`commit` 前 | `FLM2`=0x822000(面B)、`CLUTINT`=0x119(`SEL`=0)、`VEN`=0、CLUT0=A、CLUT1=B(緑)。**CPU 側の `SEL` は 0 だが、カメラでは同じ時間帯に反転が出なかった(§2)= 画面は `VEN` の反映まで旧い `SEL` を使っている** | SNAP s2_between2、v2 の 23:23:11.6〜14.5 |
| E2 C8-E2-V 目視 | ユーザー | V2a / V2b / V2c | **未実施(ユーザー確認待ち)**。カメラの所見は §2 | — |
| **E3** C8-E3-U 段階4 | UART + gdb | `wr_unused` の `sel` = `wr_inuse` の `sel`、書込み時間を記録 | **合格**。`wr_unused n=1 sel=0`、`wr_inuse n=0 sel=0`(`n` = 書いたセット番号 = `1−sel` / `sel`)。**`bench` n=2000 m=50 ms(1回 約25µs)**(2回とも 50)。gdb: 未使用セット(CLUT1)idx2 = 0xFFFFFF00(黄)、使用中(CLUT0)idx2 は赤のまま → 書込み後は使用中 idx2 = 0xFF0000FF(青) | `[clut] step=4`、SNAP s4_after_unused / s4_after_inuse |
| E3 C8-E3-U 段階5 | UART | `nr1` の `sel` 反転、`nr2` の `sel` が元に戻る、`settle` で `fb=a pal=a` | **合格**。start sel=0 → `nr1 sel=1` → `nr2 sel=0` → `settle sel=1`、`ven=0`、`rc=0`(落とし穴を `SEL` の値で確認) | v2 23:23:38.843〜44.351 |
| E3 C8-E3-V(カメラ) | 動画(10fps)で左端の赤い箱 | V3a: 1秒後は変化なし、4秒後に青、7秒後に赤。V3b: 変化なし → 青(黄でない)→ 赤 | **カメラでは期待どおり**。`wr_unused` で変化なし、`wr_inuse` の瞬間(±0.1s)に青、`wr_back` で赤。段階5: `nr1` で変化なし、`nr2` の瞬間に**青(黄にならない)**、`settle` で赤 = **落とし穴は再現**(記録) | §2。ユーザー目視は未実施 |
| **E4** C8-E4 `uf` | UART 全行 | `uf=0` | **合格**。`[clut]` と `[hb]` の `uf` は 4回の走行(`c8_boot` / v1 / v2 / gdb 走行)で**すべて 0**。段階3の負荷の間も 0。gdb で止めた全 5回の `STMON` = 0(`GR2UF` 立たず) | 各ログ |
| C8-X1 | UART | `err=0`、`drop=0` | **合格**。全 `[hb]` が `err=0 detail=0 drop=0`、`i2c_fail=0` | 同上 |
| C8-E4-V | ユーザー | V4、V1f | **未実施(ユーザー確認待ち)** | — |

### 2. カメラで分かったこと・分からなかったこと
**撮影位置の制約(重要)**: 現在のカメラ位置では、LCD は**映像の下端で切れている**(LCD の左端から約 45% までしか写らない。LCD の x 軸が映像の下向き、LCD の上が映像の右向き)。**写るもの** = 灰の階段・箱の1〜6番目・格子の左部分・白い枠(左・上・右の3辺)。**写らないもの** = 箱7(白)・箱8(灰)・右下のマーカー(赤↔緑)・右端の橙の余り・枠の右辺。人物が写るので、画像は `jobs\cam\` のみ(git 管理外)。

| 条件 | 見えたもの |
|---|---|
| **明るい部屋**(23:23:15 より前。`cam-grab.ps1 -Crop 240:160:720:560 -Rot 2`、明るさ −0.2/−0.4/−0.6 × コントラスト 1.0/1.6 の6枚 = `jobs\cam\c8_s0_b*_c*.jpg`) | 白飛び。箱1 = 淡い橙(赤の白飛び)、箱2 = 白に近い淡緑、箱3 = 淡いシアン(**青が青に見えない**)、箱4 以降は白。**6条件のどれでも箱は見分けられない**(明るさを下げると背景が黒くなるだけ) |
| **暗めの部屋**(23:23:15 以後。基準の領域も同時に暗くなった = 室内の明るさかカメラの自動露出の変化。原因は不明で、私は照明を操作していない。動画 `jobs\cam\v2.avi` 後半、`g1.avi`) | 無補正で **箱1 = 赤(254,180,199)、箱2 = 淡い緑(232,254,245)、箱3 = 青(172,221,252)** が見分けられる。箱4〜6 は白に近い(箱4 = かすかな黄緑、箱6 = かすかなピンク)。**黄・シアン・マゼンタは見分けられたと言えない**。後処理(明るさ −0.2〜−0.6、コントラスト 1.0/1.6)では箱1〜3 の彩度が上がるだけで、箱4〜6 は淡いまま(`jobs\cam\eq\montage_t80.jpg`) |
| 格子・階段・枠(暗めの部屋) | 格子の縦横線は**まっすぐ**(斜めに流れない)。階段は暗→明が見える。左・上・右の枠の白線が見える。この範囲の V1c・V1d の裏付けになる(合否はユーザー目視) |
| 段階1(マーカーの赤↔緑) | **見えない**(マーカーが視野外)。切替が分かるかは**未確認** |
| 段階2「3秒間変わらない」(動画 v2。同期は `[clut]` の時刻と箱1 の青の出入りで ±0.1s) | `nr`→`commit` の 3秒(23:23:05.6〜08.6)と次の 3秒(23:23:11.6〜14.6)を含め、格子の平均値の変動は R 192〜198(基準領域の変動と同程度)。**背景が白く反転する・階段が逆になる現象は一度も出なかった**。ただし 10fps なので**1コマ(16.8ms)の反転は撮れない** |
| 段階3(10秒間の毎垂直同期 `flip`) | 10秒間、**持続する反転・点滅は無い**(格子 R 171〜174 で一定)。1コマの組み違いは 10fps では見えない = V2c は**未確認(ユーザー目視)** |
| 段階4・5(左端の赤い箱) | §1 の E3 の行のとおり(`wr_inuse` で青、`nr2` で青、黄は出ない) |
| gdb で停止中(C6) | **LCD は映り続けた**。段階0で 10秒間停止しても模様(階段・箱・格子)は変わらず映っていた。**gdb 切断(サーバー停止)の直後に模様が消え、一様な灰色になった**。段階の経過時間は停止中は進まない |

### 3. gdb で読んだ値と期待値(段階0、停止後。`GR2*` は `((struct st_glcdc *)0xe0000)->`)
| レジスタ | 期待 | 読んだ値 | 判定 |
|---|---|---|---|
| `GR2FLM2` | 0x00800000 | 0x00800000 | 合 |
| `GR2FLM3` | 0x02000000 | 0x02000000 | 合 |
| `GR2FLM5` | 0x010F0007 | 0x010F0007 | 合 |
| `GR2FLM6` | 0x50000000 | 0x50000000 | 合 |
| `GR2FLMRD` | 0x1 | 0x1 | 合 |
| `GR2AB1` | P1 と同じ(DISPSEL=2 の見込み) | 0x2。**P1(製品 RGB565)も 0x2**(`mi-p1regs.gdb`) | 合 |
| `GR2AB2` / `GR2AB3` | 0x00080110 / 0x005701E0 | 同じ(P1 とも同じ) | 合 |
| `GR2CLUTINT` | 0x119 または 0x10119 | 0x119(`SEL`=0) | 合 |
| `GR2VEN` / `STMON` | 0 / `GR2UF` 立たず | 0 / 0 | 合 |
| `OFSM.MDE`(`x/wx 0xFE7F5D00`) | 0xFFFFFFFF | 0xFFFFFFFF | 合 |
| `g_c8_mde` | 7 | 7 | 合 |
| `g_app_dbg`(段階0) | `tick` が進む、`err_code`=0 | tick=389、err=0、frames=1、uf=0 | 合 |
| CLUT0 / CLUT1(`0xE0800` / `0xE0C00`) | 使用中 = パレットA | **両方とも パレットA**(0 エントリー差) | 合 |
| `BGEN` | (記録) | 0x10001(**P1 も 0x10001**) | 記録 |

**C2 は確認できた: CLUT メモリは gdb(CPU 経由)で読める**(0xE0800・0xE0C00 から 1024 バイトずつ dump でき、内容は書込んだパレットと一致)。**C6 も確認できた: gdb で停止中も LCD は映り続ける**(§2)。C3: 段階0の `start` は `sel=0`、CLUT0・CLUT1 とも パレットA(Open と settle の2回の書込みの結果。リセット直後の `SEL` の値は読んでいない)。

### 4. 設計とのずれ・不具合
- **製品コード・設計の不具合は見つからなかった。**
- 手順(`design.md` §19.4.2)の補足: ① `gdb-run.ps1 -Mi` に **`-NonStop` が要る**(StreamWriter が最初の送信行に BOM を付けるため、スクリプト先頭の `-gdb-set non-stop on` が `Undefined command` で失敗し、all-stop のまま `-target-select` が `Could not read registers; remote failure reply '01'` になる)。② **`-SerialSec` と `#uart` は併用できない**(COM7 を二重に開いて `Access to the port 'COM7' is denied`)。gdb の回は `-SerialSec` なしで、受信は `jobs\gdbrun-*.serial.log`(最初の `#uart` の後から)。
- `design.md` §19.4.4 のカメラ切り出し(`-Crop 200:210:700:510`)は、今のカメラ位置では LCD を外す。今回は `-Crop 240:160:720:560`(動画は `crop=320:240:680:480`)を使った。

### 5. P2b へ進む条件(`design.md` §19.9)
| 条件 | 状態 | 根拠 |
|---|---|---|
| E1 合格 | **機械判定は合格**(レジスタ・面・パレット・UART)。目視 V1 は未実施 | §1・§3 |
| E2 合格 | **機械判定は合格**(UART の `nr`〜`commit` = 3000ms、gdb の `SEL`/`VEN`/`FLM2`)。カメラでは 3秒の窓に反転が出ず、`SEL` は `VEN` まで画面に効かない形で動いている。V2b・V2c のユーザー目視は未実施 | §1・§2 |
| E3(記録) | 書込み 約25µs/回(2000回で 50ms)、落とし穴はカメラと `SEL` の値で再現、未使用セットへの書込みは見えない | §1 |
| E4 `uf`=0 | 合格(段階3の負荷の間も 0) | §1 |

→ **機械判定の範囲では P2b に進める条件を満たした。最終確認はユーザーの目視(V1b・V1e・V2a〜V2c・V4)。**

### 6. ユーザーに頼みたい目視(約 5 分。`design.md` §19.5 のとおり。実験ファームが要るので、頼む前に実験版を再書込みする)
| 何を | 見えれば合格 | カメラで代用できなかった理由 |
|---|---|---|
| V1b 箱8個の色 | 左から 赤・緑・青・黄・シアン・マゼンタ・白・灰 | 箱4〜6は淡く、7〜8は視野外 |
| V1e 右端と枠 | 橙の縦帯が見えない、白い枠が4辺とも同じ太さ | 右端・右の枠が視野外 |
| V2a 段階1 | **右下の四角だけ**が 1秒ごとに 赤↔緑、他は変わらない | マーカーが視野外 |
| V2b 段階2 | 開始の約1秒後から 3秒間 何も変わらず、その後 四角だけ緑 | カメラでは「反転なし」まで(切り替わる瞬間の乱れは見えない) |
| V2c 段階3 | 10秒間、点滅・横じまが一瞬も出ない | 10fps では 1コマ(16.8ms)の組み違いが撮れない |
| V4 ティアリング | 切替の瞬間に横に裂けない | 同上 |

カメラを LCD 全体が入る位置(下げる・離す)に置き直せば、V1b・V1e・V2a は私が確認できる(要ユーザー作業)。

### 7. 今回追加したもの・終了状態
- `tools\gdb\mi-c8snap.gdb`(段階0〜4 のスナップ。レジスタ・面・パレットの dump、`#uart nn` で段階を進める。生成元 `jobs\c8\gen_gdb.py`)、`tools\gdb\mi-p1regs.gdb`(製品の GLCDC レジスタ)。コマンド: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\gdb-run.ps1 -Script tools/gdb/mi-c8snap.gdb -Mi -NonStop -Tag c8snap -GdbTimeout 160`(約45秒。`-SerialSec` は付けない)。基準データ: `gcc -DAPP_CLUT8_TEST=1 -I src/app tools/clut8_ref.c src/app/clut8_pat.c -o jobs/c8/clut8_ref.exe && jobs/c8/clut8_ref.exe jobs/c8/ref`。動画: ffmpeg `-f dshow -video_size 1280x720 -framerate 30 -i "video=Logicool HD Webcam C510" -vf crop=320:240:680:480 -r 10 -c:v mjpeg -q:v 2`(同期は録画開始の約 +0.85秒を映像 0 秒とした)。
- **終了状態**: 製品版(`jobs\c8build\product\`、build=Nov_21_2026_22:33:04)を `HardwareDebug\` へ戻し、23:31〜23:34 に rfp-cli で書込み(`Operation successful`)。製品 GLCDC のレジスタを `mi-p1regs.gdb` で読んだ後、23:34:47 に reset-run し、`[boot]` `[init]`(lcd/touch/timer/loop ok=1)`[perf] copy_us=800 home_us=2000 slide_us=1800`、`[hb] tick=100〜500 scr=0 uf=0 err=0 drop=0` を受信(基板は製品デモが走行中)。残留プロセス 0。`src\` は未変更。

## 追記: 2026-11-22 14:2x〜14:4x P2b(製品 UI の CLUT8 化)の実機確認 — **UART が無音のため途中。gdb で読める範囲だけ確認**(`design.md` §20.9)

**結論(先に)**: P2b の書込みと走行は成功し、gdb で読める項目(G-D1、ホームの G-D2、`[perf]` の値、アイドル中の `uf`)は**すべて期待どおり**。しかし **COM7 が無音**(P2b でも、製品版 P1 に戻しても 0 バイト)なので、UART で判定する U-D1(UT10〜UT19、デモ)・U-D2 の `[perf]` 行、UART で画面を送る G-D2 の残り 22 状態は**未実施**。カメラは LCD が視野に無く(人物が写る向き)使えなかった。**CN8(COM7)側の復旧(ユーザーが USB を挿し直す)が要る**。終了状態は**製品版 P1 を書込み済み・走行中**(UART が無音で `[hb]` は確認できず)。

### 1. 結果の表
| ID | 内容 | 結果 | 証拠 |
|---|---|---|---|
| 書込み | `HardwareDebug\rx72n_gcc.{elf,mot}` = `jobs\c8build\p2b\` と同一(`fc /b` で差なし、mot 3,513,430 B)。rfp-cli で書込み+走行 | **合格**(1回目 83.6秒 `Operation successful`。**再書込みの1回目だけ E4000004 → 1回再試行で成功**) | `jobs\p2b-rfp.log` |
| 走行(gdb) | P2b は走っている | **合格** | `gdbrun-p2bpeek-*`: 10秒走行後 `g_app_dbg.tick=959`、停止位置 `memcmp`(メインループ)。`gdbrun-p2bnouart-*`: 3秒後 tick=287、さらに 10秒後 tick=1283、`err_code=0` |
| **P-D1/P-D2** | ビルドの 0 warnings、ROM | ビルド担当の記録(`jobs\c8build\p2b\build.log`: 0 errors 0 warnings、text 1,168,212 + data 2,876 = 1,171,088、bss 12,992)。私は再ビルドしていない | — |
| **G-D1** | レジスタ・表示面・パレット | **合格** | 下の §2 |
| **G-D2**(ホームだけ) | 実機の表示面 = ホスト描画 | **合格(1状態)**: 添字の違い 0 / 130,560、パネル色の違い 0。残り 22 状態は**未実施**(画面を送る UART が使えない) | `jobs\fb\dev\A_home.*`、`fb2png.py check --c8` |
| **U-D2** `[perf]` | `copy_us` `home_us` `slide_us` | **gdb で値を読んで目標内**(UART の行は未確認) | §3 |
| `uf` | 8bit のバイト書込みで 0 | **部分**: 起動〜13秒(アイドル、ホーム静止)で `uf=0`、`STMON=0`。デモ・画面送り中の `uf`(R-D2 の本命)は**未確認** | `gdbrun-p2bnouart-*` |
| U-D1 | UT10〜UT19、デモ 33秒、`uf` の増分 | **未実施**(UART 無音) | §4 |
| V-D0〜D2 | 目視 | **ユーザー確認待ち**(§6) | — |
| カメラ | LCD の色 | **未実施**(視野に LCD が無い) | §5 |

### 2. G-D1: gdb で読んだ値(`mi-p2b-nouart.gdb`、停止後。`GR2*` は `((struct st_glcdc *)0xe0000)->`)
| 項目 | 期待 | 読んだ値 | 判定 |
|---|---|---|---|
| `GR2FLM3` | 0x02000000 | 0x02000000 | 合 |
| `GR2FLM5` | 0x010F0007 | 0x010F0007 | 合 |
| `GR2FLM6` | 0x50000000(CLUT8) | 0x50000000 | 合 |
| `GR2FLM2`(表示面) | `s_back` の反対 | 0x00822000(面1)、`s_back`=0 | 合 |
| `GR2CLUTINT` | 0x119 または 0x10119 | **0x10119**(`SEL`=1) | 合(E1 は 0x119 だった。どちらも設計の許容) |
| 使用中のセット = `ui_pal` | 0 エントリー差 | **pal1(0xE0C00、`SEL`=1 の側)= ELF の `ui_pal`(ROM 0xFFD1CC58)と 0 / 256 エントリー差**(`c8png.py cmppal`) | 合 |
| 未使用のセット(pal0 = 0xE0800) | (規定なし) | `ui_pal` と 256 / 256 違う(初期化されていない値) | 所見(FIT は使用中でない側に書かない。`ClutUpdate` を呼ばないので問題なし) |
| `GR2AB1` / `AB2` / `AB3` | P1 と同じ | 0x2 / 0x00080110 / 0x005701E0 | 合(P1 と同じ) |
| `GR2VEN` / `GR2BASE` / `BGEN` / `STMON` | 0 / 0 / 0x10001(P1 と同じ)/ 0 | 0 / 0 / 0x10001 / 0 | 合 |
| `OFSM`(`x/2wx 0xFE7F5D00`) | 0xFFFFFFFF(MDE=7) | 0xFFFFFFFF 0xFFFFFFFF | 合 |
| 表示面の右 32 列(x=480..511) | (表示されない) | 0 ではなく、古い内容(190、239、255 など)が残っている | 所見(RAM2 は起動で消えない。`hsize`=480 なので表示には出ない。実装ノートの「0 のまま」は正しくないが無害) |

### 3. `[perf]`(applog_fmt_perf で止めて引数を読んだ。r4 = `copy_us`、スタックの 2 語 = `home_us`、`slide_us`。P1 の時と同じ読み方)
| 項目 | P1 実測 | P2b 見積 | **P2b 実測** | 目標(P1 以下) | 絶対上限 | 判定 |
|---|---|---|---|---|---|---|
| `copy_us` | 800 | 400〜500 | **400**(r4=0x190) | 800 | 8,000 | 合(P1 の半分) |
| `home_us` | 2,000 | 1,200〜1,800 | **1,200**(0x4b0) | 2,000 | 12,000 | 合 |
| `slide_us` | 1,800 | 1,100〜1,500 | **1,200**(0x4b0) | 1,800 | 12,000 | 合 |
- 整合: `home_us ≈ 2,000 − 800 + 400 = 1,600`(±400)に対し 1,200(下限を 200 下回る。分解能 200µs なので許容と見なす。数字の描画が表引きで軽い分と推定)。`n=50`、分解能 200µs。その時点の `STMON`=0、`uf`=0。
- UART の `[perf]` 行そのもの(書式)は未確認。

### 4. UART が無音だった経緯(不具合ではなく、COM7 側の問題と見られる)
| 時刻 | 操作 | 結果 |
|---|---|---|
| 14:23 | P2b 書込み後、`ut-run.ps1 -Name P2B_boot -Seconds 12`(COM7 を先に開いて rfp でリセット) | **受信 0 行**(`jobs\ut\P2B_boot.log` は `# reset released` と `# end` だけ) |
| 14:24 | `serial-read.ps1 -Seconds 8` | 0 バイト |
| 14:24 | gdb(`mi-peek-uart.gdb`) | **基板は走っている**(tick=959)。SCI2: SCR=0xF0(TE・RE・TIE・RIE)、MSTPCRB の SCI2 解除済み、P12/P13 の PFS=0x0A、`s_q.drops`=0、`s_log_ready`=true。**`SSR`=0xF2(RDRF・ORER・FER が立っている = RXD2 に何か来ている)** |
| 14:2x | DTR・RTS を有効にして開き、rfp でリセット | 0 バイト |
| 14:3x | **製品版 P1 を書込み**(UART は P1 でも出ていたもの)、8秒受信 | **0 バイト**(P2b 固有ではない) |
| 14:4x | 最後に P1 のまま 6秒+8秒 | 0 バイト |
- 結論: **P2b のファームの不具合ではない**(P1 でも同じ)。Windows 上では COM7(VID_045B&PID_8111)は「OK・接続済み」と見えるが、データが来ない。過去(2026-11-21 10:1x、13:58)にも同じ症状があり、CN8 の挿し直しで回復した(`docs/debug-report.md` 前の追記)。**ユーザーに CN8 の USB を挿し直してもらい**、`[boot]` の受信を確認してから U-D1 を再開する必要がある。
- 14:2x のカメラの画像は、LCD ではなく人物が写っていた(→ §5)。基板の電源・USB の動きは、E2 Lite 経由の書込み・gdb が成功しているので問題なし。

### 5. カメラ(Logicool HD Webcam C510)
- `cam-grab.ps1`(クロップなしの全体、2枚)で撮影したが、**視野に LCD も基板も無く、椅子に座った人物の胴体と手(ゲームのコントローラ)が写っていた**。LCD の色の確認は**できなかった**(見分けられなかったのではなく、見えていない)。
- 人物が写っているため、撮った 2 枚は `jobs\cam\` から**削除した**(`docs\` には入れていない)。カメラを LCD に向け直してもらえば、`-Crop` を決め直して再開できる。

### 6. ユーザーに頼みたいこと
| # | 内容 | 目的 |
|---|---|---|
| 1 | **CN8(マイクロ USB。COM7)の USB を挿し直す**(基板側と PC 側) | UART を復旧し、U-D1(UT10〜UT19、デモ)・`[perf]` の UART 行・G-D2 の残り 22 状態を実施する |
| 2 | (カメラを使うなら)Webcam を LCD 全体が映る向きに戻す | 色の確認(ホームの時計「21:45」〜、4 つのボタンの色、アイコン) |
| 3 | **V-D0**: `tools\assets\bath\preview_p2b\` の PNG(`bg_*.png`、`scr_*.png`、`buttons_press.png`、`buttons_flash.png`、`palette.png`)を見て承認 | 見てほしい所: 文字の縁、ボタンの角(押下)、バーの端、色の段差。差分の PNG は 1 段 = 黄、2 段 = 赤、3 段以上 = 黒の点が文字の縁にあるだけで、面の色は P1 と見分けられなければ合格 |
| 4 | **V-D1**(P2b を書込んだ状態で。実機の LCD) | ホーム・湯はり・追い焚き・使用量・待機が、P1 と見分けられない(色の濃淡・文字の縁・背景のざらつきがない)。右端・下端に縦帯・欠けがない |
| 5 | **V-D2** | 「＋」を押すと**暗くなり**、離すと**白く光る**(角の丸みの縁がわずかに硬い・柔らかいのは設計どおり)。湯はり↔ホームのスライドが滑らか(ちらつき・横じま・半分だけ新しい絵がない)。デモ 約33秒が台本どおり |
- 今の基板は**製品版 P1** が走っている。V-D1・V-D2 の前に、P2b の再書込みが要る(`HardwareDebug\` へ `jobs\c8build\p2b\` の elf と mot をコピーし、`docs/cli-debug.md` §11 の rfp-cli 1行)。

### 7. 今回追加したもの・終了状態
- `tools\gdb\mi-p2b-nouart.gdb`(UART を使わない P2b の確認: `applog_fmt_perf` で `[perf]` の引数、レジスタ、`ui_pal` の ROM dump、ホームの面とパレット dump、10秒走行後の `uf`)。コマンド: `New-Item -ItemType Directory -Force jobs\fb\dev; powershell -NoProfile -ExecutionPolicy Bypass -File tools\gdb-run.ps1 -Script tools/gdb/mi-p2b-nouart.gdb -Mi -NonStop -Tag p2bnouart -GdbTimeout 150`(約20秒)→ `python -I tools/fb2png.py check <gdb.log> jobs/fb/dev/A --c8 --host jobs/fb/p2b/fb_hostrender_c8.exe`。ホスト側 `fb_hostrender_c8.exe` は `tools\fb_hostrender.c` の先頭のコマンドを `-o jobs/fb/p2b/fb_hostrender_c8.exe` でビルド(警告なし)。**他の 22 状態の gdb スクリプト(B_・C_・D_)の 8bpp 版は、UART が使えないので作っていない**(規則: `261120` → `139264`、dump 行の後ろに `.pal0.bin` / `.pal1.bin` の 2 行)。
- 製品コード(`src\`)は未変更。ビルドもしていない。
- **終了状態**: `HardwareDebug\rx72n_gcc.{elf,mot}` = 製品版 P1(`jobs\c8build\product\`、mot 6,319,710 B)。rfp-cli(`-a -run`)で書込み済み・走行中(`Operation successful`)。**UART が無音のため `[hb]` は未確認**。残留プロセス(e2-server-gdb、rx-elf-gdb、rfp-cli、e2studio、ffmpeg)は 0。


## 追記: 2026-11-22 14:41〜15:00 P2b(製品 UI の CLUT8 化)の実機確認の続き — UART 復旧後(`design.md` §20.9)

**結論(先に)**: **機械判定の範囲では P2b は合格。** UART(UT10〜UT14、UT16〜UT18)はすべて合格で、全 `[hb]` が `uf=0`(デモ・画面送り・連打の最中も 0)。実機の表示面 29 状態(ホーム・湯はり・追い焚き・使用量・待機、デモ中のタグ付き、押下中、白光中、スライド途中 6 コマ)が、同じ状態をホストで描いた結果と**添字まで 0 差**、パレットも `ui_pal` と 0 差。不具合は見つからなかった(UT14 の最初の 1 回だけ起動ログが無かった件は §5)。カメラは LCD の上の細い帯しか映らず、色の確認は一部だけ。**ユーザーの目視(V-D0、V-D1、V-D2)が残っている。** 終了状態は **P2b を書込んだまま走行中**(`[hb]` 受信済み)。

### 1. 結果の表
| ID | 内容 | 結果 | 証拠 |
|---|---|---|---|
| 書込み | `jobs\c8build\p2b\` の elf・mot を `HardwareDebug\` へコピー(mot 3,513,430 B、elf 2,334,708 B。elf は元と同サイズ)。rfp-cli(§11 の1行)で書込み+走行 | **合格**(81.1 秒、`Operation successful`、exit 0。E4000004 なし) | `jobs\ut\P2B2_flash.log` |
| 起動 U-D2 / UT18 | `[boot] build=Nov_22_2026_10:06:26`(P2b)→ `[init]` ×4 すべて ok=1 err=0 → `[perf] n=50 copy_us=400 home_us=1200 slide_us=1200` → `[hb]` tick=100,200,… | **合格**(UART の行でも確認。前回 gdb で読んだ値 400 / 1,200 / 1,200 と同じ) | `P2B2_flash.log` 14:43:31、`P2B_final.log` 15:00:18 |
| **U-D1 UT10** 順送り | `nnnnn`(700ms 間隔)→ `scr=` が fill / reheat / usage / standby / home、すべて `src=uart` | **合格**(judge_ui.py) | `jobs\ut\P2B_UT10.log` |
| UT11 逆送り | `ppppp` | **合格** | `P2B_UT11.log` |
| UT12 温度 | `++++++++--s`: `set=43..48`(6本)、続いて `47`、`46`。`[ui] scr=home set=46` | **合格** | `P2B_UT12.log` |
| **UT13 デモ全走** | `d`。`[ev]` **36 行**、`demo=start` 14:45:23.785 → `demo=end` 14:45:56.787 = **33.002 秒**。`demo=1` の `[hb]` 33 本連続。デモ中の `frm` 2 → 622(増加 約 620。基準 200〜900 内)。台本の時刻のずれは judge の許容内(合格) | **合格** | `P2B_UT13.log` |
| UT14 中断(UART) | `dxxxxxxxd`: 開始 +5.7 秒(湯はり中)に `demo=abort src=uart`、`scr=home from=fill src=uart`。以後 `scr=0 demo=0` | **合格**(再実行 `P2B_UT14b.log`。最初の実行は §5) | `P2B_UT14b.log` |
| UT16 通常速度 | `nn` → `done=reheat`(`scr=reheat` の 16.0 秒後。14:46:32.744 → 14:46:48.745)、`done=fill` なし | **合格** | `P2B_UT16.log` |
| UT17 連打 | `+` を 36 文字、1 回の書込み → `set=43,44,45`、`[err]` なし、`[hb]` 14 本継続 | **合格** | `P2B_UT17.log` |
| UT15、UT19 | 実タッチが必要 | **未実施**(ユーザーが基板を触る必要。P1 と同じ扱い) | — |
| **`uf`**(R-D2) | 受信した全 `[hb]` が `uf=0`: 書込み後 30 本、UT10 18、UT11 14、UT12 16、**UT13(デモ)42**、UT14b 18、UT16 30、**UT17(連打)14**、`P2B_final` 10。`err=0`、`drop=0`、`[err]` 行 0 | **合格**(8bit のバイト書込みでの `uf` は 0 のまま。P1 の起動時 1 回も出ない) | 各 `P2B_*.log` |
| **G-D2**(実機の表示面 = ホスト描画) | 下の §2 | **合格(29 状態、すべて 0 差)** | `jobs\fb\dev\*` |
| V-D0〜V-D2 | 目視 | **ユーザー確認待ち**(§6) | — |
| カメラ | §3 | 一部のみ(見分けられた範囲を §3 に) | `jobs\cam\p2b_*.jpg` |

### 2. G-D2: 実機の表示面とホスト描画(`tools\fb2png.py check <gdb.log> jobs/fb/dev/<A|B|C|D|S> --c8 --host jobs/fb/p2b/fb_hostrender_c8.exe`)
全 29 状態で「添字の違い(x<480)= 0」「パネル色の違い = 0」。使用中のパレット(`GR2CLUTINT.SEL`=1 の側、pal1)は全状態で **`ui_pal` と 0 エントリー差**(ホスト側の `ui_pal` と比較)。最大差は 0 段(添字が全画素一致なので、色の差は出ない)。
| 系列 | 状態(`s_ui` から読んだ値) | 添字の違い / 色の違い |
|---|---|---|
| A(静止 7) | ホーム(set=42)、湯はり(scr=1)、追い焚き(scr=2)、使用量(scr=3、棒 640)、待機(scr=4)、ホームに戻る、ホーム(set=43) | 0 / 0(各) |
| B(デモ中 8。タグ付き、`demo=1`) | デモ開始直後のホーム、湯はり 31.8%(`tr_dx=480`)、湯はり 75.8%、**スライド開始(tr_on=1、湯はり→ホーム。`fb_btn=9` の白光中)**、追い焚き(bath 39.2)、追い焚き(bath 43.0)、使用量、待機 | 0 / 0(各)。※段の名前(`reheat_mid` など)と実際の画面は、止める時刻のずれで一致しない段がある(上の状態が実際) |
| C(押下 4) | **押下中 h_up**(btn=2、phase=1)、**押下中 h_dn**(btn=1、phase=1、set=43)、**白光中 h_up**(btn=2、phase=2)、押下後のアイドル | 0 / 0(各) |
| D(中断 4) | ホーム、デモ開始直後(`demo=1`)、中断直後、中断 3 秒後(`demo=0`、タグなし) | 0 / 0(各) |
| **S(スライド途中 6)** | `n` / `p` の 0.04〜0.20 秒後に停止。`tr_dx` = 201(湯はり進入)、306(戻り)、369、451、476、386。表示面は **状態変数より 1 コマ前**(既知)。1 コマ前の `tr_dx` で再描画: **172、(306 は状態のまま)、348、442、467、369** | **1 コマ前の状態で 6 コマすべて 0 / 0**(状態のままだと 5 コマが不一致 = 11,184〜36,977 画素。これは表示面が 1 コマ古いだけで、描画の誤りではない) |
- 前回 P1 の 23 状態に準じた範囲(ホーム、湯はり、追い焚き、使用量、待機、デモ中のタグ、押下、白光、スライド途中)をカバー。スライドは 6 コマ(進入 3、戻り 3、`tr_dx` 172〜467)。
- 実機の表示面の右 32 列(x=480..511)は比較対象外(表示されない)。
- 1 コマ前の `tr_dx` の探索: `jobs\fb\dev\S_*.state.txt` の `tr_dx` を 0 まで下げて `fb_hostrender_c8` で描き、`c8png.face_diff` で比較(探索の使い捨てスクリプトは scratchpad)。fill の値の 1〜2 の遅れも試したが、不要だった(`fill_lag=0`)。

### 3. カメラ(Logicool HD Webcam C510)
- **カメラの向きは「LCD 全体が映る」位置になっていなかった**。全体の撮影(クロップなし)では、人物とゲームのコントローラが写り、LCD は**画像の下端中央に上の 1/3 程度だけ**(LCD が 90 度回って映り、LCD の上端付近の帯しか見えない)。人物が写る全体画像は**削除した**。以後は LCD 付近だけを切り出した(`-Crop 220:140:720:580`、`-Eq brightness=-0.3:contrast=1.6`。`jobs\cam\p2b_{home,fill,reheat,usage,standby}.jpg`、並べた `p2b_montage.png`。人物は写っていない。`docs\`・git には入れていない)。
| 画面 | 見えたもの(LCD の上の帯だけ) |
|---|---|
| ホーム | 時計「21:45」(濃い青灰)が読める。ほかに小さなアイコンの点(青・ピンク・青緑のような色)。**4 つのボタンは視野外(見えない)** |
| 湯はり | 青緑の四角と文字(「湯はり中」らしい文字、`%` の数字)。水位バーの色が薄い青緑 |
| 追い焚き | 「38.9」(数字が読める)、赤〜ピンクの縦の棒(バー) |
| 使用量 | 「10.3」の数字、薄い青の四角が並ぶ |
| 待機 | 暗い背景(濃い青灰)に「21:46」。ホームとはっきり区別できる |
- 色は白飛びしていて淡い。**ホームのボタンの色、アイコンの形、文字の縁、使用量の棒の色は見分けられなかった**(視野外か白飛び)。画面の違い(5 画面の区別)と、待機の暗転、数字の読み取りまでは確認できた。**押下の暗転・白光・スライド・デモは、カメラの視野に入らないので確認していない**(動画は撮っていない)。
- カメラを LCD 全体が入る位置(もっと離す・下を写す)に置き直せば、ホームのボタンの色などを確認できる(要ユーザー作業)。合否は §1(UT・G-D2)で判定済みで、カメラは補助。

### 4. 追加・変更したもの
- `tools\gdb\mi-fbdump-c8-dev.gdb`(A 7 段。`mi-fbdump-c8.gdb` の dump 先を `jobs\fb\dev\` にしたもの)、`mi-demo-fb-c8.gdb`(B 8 段)、`mi-press-fb-c8.gdb`(C 4 段)、`mi-abort-fb-c8.gdb`(D 4 段)、`mi-slide-fb-c8.gdb`(**新規**。S 6 段: `n`/`p` の 0.04〜0.20 秒後に interrupt して dump)。B/C/D は P1 のスクリプトから機械的に作った(`261120` → `139264`、dump 行の後に `.pal0.bin`/`.pal1.bin` と `GR2CLUTINT` の print)。
- コマンド(各 約 30〜60 秒。`-SerialSec` は付けない): `powershell -NoProfile -ExecutionPolicy Bypass -File tools\gdb-run.ps1 -Script tools/gdb/<script> -Mi -NonStop -Tag <名> -GdbTimeout 200`、続けて `python -I tools/fb2png.py check <gdb.log> jobs/fb/dev/<A|B|C|D|S> --c8 --host jobs/fb/p2b/fb_hostrender_c8.exe`。`mi-slide-fb-c8.gdb` は最後に `continue` + `sleep` で終わるため、`gdb-run.ps1` が `-GdbTimeout` まで待つ(約 3 分。ログと dump は先に出ている)。
- UT 用: `tools\ut-run.ps1 -Name P2B_UTnn -Seconds N -Tx '...' -TxAfterSec 3 -TxGapMs 700`(`design-uart.md` §10.8 の UT10〜UT17 と同じ引数)、判定は `python -I tests/host/judge_ui.py jobs/ut/P2B_UTnn.log`。

### 5. 所見
- **UT14 の最初の実行(`P2B_UT14.log`)で、リセット後に基板の出力が 0 行だった**(`# reset released` の後、`[boot]` も `[hb]` も出ず、送った `d` にも反応なし)。直後の `serial-read` では基板は走っており(`tick=2000 ... uf=0`)、同じ引数の再実行(`P2B_UT14b.log`)は正常で合格。UT13 の直後の rfp reset が効かなかった一過性の事象と見られ、**原因は未確認**。再現するか(UT13 → UT14 の連続で起きるか)は 1 回しか見ていない。P2b 固有かどうかも不明(P1 の UT では起きなかった)。ファームの不具合の根拠は無い。
- **設計とのずれ・製品コードの不具合は見つからなかった。** 前回の所見(使われていない側のパレット、表示面の右 32 列に古い内容が残る)は変わらず、無害。
- UT15・UT19 は未実施(実タッチ)。S12 のサニタイザ・実際のタッチ座標は対象外。

### 6. ユーザーの目視(P2b は今、基板に書込み済みで走行中。デモが続いている場合は画面を触らない)
| ID | 何を見て、何が見えれば合格か |
|---|---|
| **V-D0** | `tools\assets\bath\preview_p2b\` の PNG(`bg_*.png`、`scr_*.png`、`buttons_press.png`、`buttons_flash.png`、`palette.png`)。文字の縁、ボタンの角(押下)、バーの端、色の段差。差分 PNG は 1 段 = 黄、2 段 = 赤、3 段以上 = 黒の点が文字の縁にあるだけで、面の色が P1 と見分けられなければ合格 |
| **V-D1** | 実機の LCD で、ホーム・湯はり・追い焚き・使用量・待機が P1 と見分けられない(色の濃淡・文字の縁・背景のざらつきなし)。右端・下端に縦帯・欠けがない。`n` で順に出せる(UART)か、LCD の「湯はり」「ホーム」ボタンで切り替える |
| **V-D2** | 「＋」を押すと**暗くなり**、離すと**白く光る**(角の縁がわずかに硬い・柔らかいのは設計どおり)。湯はり↔ホームのスライドが滑らか(ちらつき・横じま・半分だけ新しい絵がない)。「デモ」ボタンで約 33 秒が台本どおり動き、終了後ホームで止まる |

### 7. 終了状態
- `HardwareDebug\rx72n_gcc.{elf,mot}` = **P2b**(`jobs\c8build\p2b\`)。gdb の回の後、`ut-run.ps1`(rfp-cli `-sig -run`)で走行に戻し、15:00:18 に `[boot] build=Nov_22_2026_10:06:26` → `[init]` ×4 → `[perf] copy_us=400 home_us=1200 slide_us=1200` → `[hb] tick=100〜1000 scr=0 uf=0 err=0 drop=0` を受信。残留プロセス(e2-server-gdb、rx-elf-gdb、rfp-cli、e2studio、ffmpeg)は 0。`src\` は未変更、ビルドもしていない。

## 追記: 2026-11-25 09:33〜09:42 P2b の実タッチ確認 UT15・UT19(ユーザー協力。書込みなし、`src\` 未変更)

**結論(先に)**: **UT15 は合格、UT19 は「要求した操作の範囲で合格(手動判定)」。** `judge_ui.py` の UT19 は FAIL だが、原因は試験の前提のずれ(開始時の設定温度が 43 で、判定の期待 `set=43` と合わない)と、ユーザーが指示以外の「＋」「－」も多く押したこと。製品の不具合ではない。**タッチ座標は、向き(入れ替え・反転なし)も範囲(0〜479 / 2〜271)も、仮値 `TOUCH_*`(SWAP=0、FLIP=0、X_MAX=479、Y_MAX=271)のままで一致**した。基板は P2b(build=Nov_22_2026_10:06:26)のまま走行中。

### 1. 結果の表
| ID | 方法 | 期待 | 結果 | 証拠(`jobs\ut\`) |
|---|---|---|---|---|
| 事前 | `tools\serial-read.ps1` で 8 秒受信 → **無音**(CPU が止まっていたと見られる)。`tools\ut-run.ps1`(rfp-cli `-sig -run` = リセット解除のみ。書込みなし)で走行へ | `[boot] build=Nov_22_2026_10:06:26`、`[hb]` | **合格**(09:34:07 `[boot]` P2b、`[init]`×4 ok=1、`[perf] copy_us=400 home_us=1200 slide_us=1200`、`[hb] tick=100〜1400 uf=0`)。無音の原因は未確認(前日 15:00 の走行後、基板の電源や USB の状態は不明) | `T_pre.log` |
| **UT15** デモの中断(実タッチ) | 私が `d` で `demo=start src=uart`(再送 4 回: デモ終了 `demo=end` の 3 秒後に再開)。ユーザーが 5 回目のデモの湯はり画面で 1 回触る | `demo=abort src=touch`、`scr=home from=fill src=touch`、そのタッチの `btn=` なし、以後 `demo=0` | **合格**(`judge_ui.py --ut UT15`。ユーザーが 8 秒後に別のボタンを触った分を除いた `T_UT15_touch_trim.log` で PASS。元のログは、その後のユーザーの操作(下)が「`btn=` が出ない」に引っかかり FAIL) | 09:38:23.268 `demo=start src=uart` → 09:38:26.617 `scr=fill` → **09:38:29.331 `demo=abort src=touch`** → 09:38:29.332 `scr=home from=fill src=touch`。続く `[hb]`: 29.988 `scr=0 demo=0 x=139 y=209`(以後 7 本 `demo=0`、09:38:36 まで `[ev]` なし) |
| **UT19** タッチでの操作 | ホームで「＋」「湯はり」→ 湯はり画面右下「ホーム」→ ホームの四隅の端 | `btn=h_up` → `set=`、`btn=h_fill` → `scr=fill`、`btn=f_home` → `scr=home`、四隅で `btn=` なし | **要求した操作は合格(手動判定)**。`judge_ui.py` は **FAIL**(2/9 まで。理由は下) | 09:39:40.037〜.118 `btn=h_up st=down/act` `set=44`。09:39:57.055〜.152 `btn=h_fill ...` `scr=fill from=home src=touch`。09:40:02.784〜.915 `btn=f_home ...` `scr=home from=fill src=touch`。**09:40:02.915 〜 09:41:51(四隅の後)まで `[ev]` が 0 本** |
| 共通 | 2 本のログ | `err=0`、`uf=0`、`drop=0`、書式 | **合格**: `[err]` 0、全 `[hb]`(169 本、151 本)が `uf=0 err=0 drop=0`、C1〜C4 PASS。UT19 は `down` 11 = `act` 11(UT15 のログの `down` 52 / `act` 47 の差 5 は、デモの `s_wake` の仮想押下で `act` がない設計どおり) | 両ログ |

**UT19 の `judge_ui.py` が FAIL になった理由(不具合ではない)**
1. 判定の期待は `set=43`(設定 42 から開始)だが、開始時の設定が **43**(直前のデモで set=43 のまま)で、最初の「＋」は **`set=44`**。
2. ユーザーが指示のほかに、「＋」を連続で押し(`set=44,45,46,47,48`、さらに 2 回押しても `set=` は出ない = **上限 48 が効いている**)、「－」を 2 回(`set=47,46`)押した。判定は `btn=` が `h_up` `h_fill` `f_home` だけであることを求めるので `h_dn` で FAIL。
- 参考: 設計どおり、上限(48)で押しても `set=` は出ない。押下(down)と離し(act)は対で出た。

### 2. タッチ座標(`x`、`y`、`pts`)の実測と、向き・範囲
ホームの画面で、ボタン配置(`src\app\ui_layout.c` の `ui_btn_defs`)と突き合わせた。

| 触った所 | `[hb]` / `[st]` の `x` `y` | 配置との照合 |
|---|---|---|
| デモ中の湯はり画面(中断の 1 回) | x=139 y=209(`T_UT15_touch.log` 09:38:29.988) | `f_stop` の矩形(16,208,224,48)の中。実際は中断なので `btn=` なし |
| ホームの「追い焚き」(ユーザーの追加の操作) | **x=179 y=225**(`[st]` 09:38:37.372、`pts=1`)→ `btn=h_reheat` | `h_reheat` の矩形(130,192,106,64)の**中心(183,224)にほぼ一致** |
| 追い焚き画面の「停止」 | x=92 y=219 | `r_stop`(16,216,224,42)の中 |
| 「＋」付近 | x=393〜415 y=151〜176 | `h_up`(382,136,72,38)付近。`+` を押している y は 151〜176 で、矩形(y 136〜174)とヒット枠(133〜177)の下端寄り |
| 「－」付近 | x=331〜339 y=159〜169 | `h_dn`(298,136,72,38)の右端寄り |
| 「湯はり」 | x=51 y=240(`pts=0`、直後に `[st]` で確認) | `h_fill`(16,192,106,64)の中 |
| 湯はり画面の「ホーム」 | x=329 y=237 | `f_home`(248,208,216,48)の中 |
| **四隅(ユーザーの 4 回)** | 左下 **(0, 271)**、左上 **(0, 14)**、右上 **(465, 18)**、右下 **(464, 271)**(`[hb]` 09:40:13.991〜17.986) | 順に画面の左下・左上・右上・右下。**入れ替え・反転はない**(X は右へ増え、Y は下へ増える) |
| `[st]` の累計(起動後) | 09:40:21 の `xmin=0 xmax=479 ymin=2 ymax=271` | 範囲は 0〜479、2〜271 = 画面(480×272)と一致。`TOUCH_X_MAX=479`、`TOUCH_Y_MAX=271` が合う |

- **分かったこと**: `TOUCH_SWAP_XY=0`、`TOUCH_FLIP_X=0`、`TOUCH_FLIP_Y=0`、`TOUCH_X_MAX=479`、`TOUCH_Y_MAX=271`(いずれも「仮値」)は、実機の座標と合っている。V0(四隅の確認)と同等の確認になった(ユーザーが四隅を触った)。**マクロの変更は不要**。
- 四隅の端で `btn=` は出ない(`h_fill` のヒット枠は x 13〜125、y 189〜259、`h_power` は x 355〜467 までで、端の点は枠の外)。
- `pts` は触れている間だけ 1(短いタップは 1 秒周期の `[hb]` に写らず、`x` `y` は最後の値が残る)。

### 3. カメラ(Logicool C510)
- ユーザーが位置を変えた後の全体撮影(`jobs\cam\ut15_full.jpg`)で、**LCD の全体が映る**(上下が逆さまに映る=カメラが 180 度回転。LCD は明るく白飛びして淡い。ホームの時計「21:45」が逆さまに読める)。机の上のケーブルなどが写る。**人物は写っていない**。`docs\`・git には入れていない。UT の判定には使っていない(UART で判定)。ボタンの色などを見るなら `-Rot` が不要で、180 度回転(`transpose` を 2 回)が必要。

### 4. 所見(コーディング担当・設計担当へ)
- **不具合・設計とのずれは見つからなかった。** 実タッチによるデモの中断は、設計(`design.md` §6.1)どおり(即ホーム、そのタッチは動作にならない、`demo=0`)。
- `judge_ui.py` の UT19 は、「開始時の設定温度が 42」と「ボタンが 3 個だけ」を前提にしている。実タッチ試験では人が余分に押しうるため、`set=` を「開始時の値 + 1」と読む・余分なボタンを許す `--loose` のようなオプションがあると使いやすい(要望。テスト担当へ)。
- 試験の最初に UART が無音だった(`serial-read` 8 秒)。`rfp-cli -sig -run` でリセットすると復旧した。原因は未確認(前回の終了状態は走行中だった。基板への給電の入れ直しがあった可能性)。再現するかは未確認。

### 5. 追加・変更したもの / 終了状態
- 製品コード・`tools\` は変更なし。受信用の使い捨てスクリプト `arm.ps1`(デモの自動再開・実タッチ中断の検出。scratchpad)を使用。ログ: `jobs\ut\T_pre.log`、`T_UT15_touch.log`(+ `_trim.log`)、`T_UT19_touch.log`。
- **終了状態: P2b を書込んだまま走行中**(`[hb] tick=46300 scr=0 demo=0 uf=0 err=0 drop=0` を 09:41:50 に受信。画面はホーム、set=46)。書込み・ビルド・gdb は行っていない。残留プロセスなし(`arm.ps1` は終了)。

---

## Wi-Fi(ESP32 AT)試作 実機確認(2026-11-25 10:55〜11:02、デバッグ担当)

**結論: ゴール(ping)は未達。AT ファームは入っていて 115200bps で通信できる。止まった所は `join`(`+CWJAP:3` = AP が見つからない)。ESP32 の AT ファームは古い版(`AT version:1.1.3.0`)で、設計の想定(2.x)と違う。**

条件: 試作版(`APP_WIFI_TEST=1`、`jobs\wifibuild\test\`)を書込み。SW3 の 2ch = OFF、ECN1 と CN8 は USB ハブ(セルフパワー)。**自動開始は無効だった**(`[init]` の後に `[wifi]` が出ず、`d` を送って開始)。ログ: `jobs\ut\wifi-01-boot.log`〜`wifi-05-s.log`。SSID・パスワード・生の受信行は、どのログにも出ていない(`[wifi]` `[wifiv]` 行だけ)。

| ID | 方法 | 期待 | 結果 | 証拠(ログ行) |
|---|---|---|---|---|
| UW0 | 書込み+リセットで受信 | `[boot]` → `[init] stage=wifi ok=1 err=0 detail=0` → `[hb]` +100 | **合格** | `[boot] fw=rx72n_gcc build=Nov_25_2026_10:53:10 ch=2 baud=115200`、`[init] stage=wifi ok=1 err=0 detail=0`、`[hb] tick=100`〜`1200`(+100、`err=0 drop=0`)。`stage=lcd` は出ない |
| UW1 | `n`、`d` | `reset` ok、`boot` ok、`at` ok | **合格** | `step=reset res=ok ms=20`、`step=boot res=ok ms=700`(`ready` 受信)、`step=at res=ok try=1`。`d` を3回(`n` 1回 + `d` 2回)送って毎回同じ。`bytes` は 19 → 475(累計) |
| UW2 | `d` | `echo` ok、`gmr` ok(版 2 以上)、`store`、`mode` ok | **一部不合格** | `echo` ok、`gmr` ok、`mode` ok、**`store res=fail`**(古い版は `AT+SYSSTORE` 未対応)、**版は 1.1.3.0 でメジャー 1(基準は 2 以上 → 不合格)**。`[wifiv] key=ver val=1.1.3.0(5a40576_-_Nov_28_2018_12:50:55)`、`key=sdk val=v3.0.7` |
| UW3 | `d` | `join` ok、`ip` ok | **不合格** | `step=join res=fail try=1 ms=16020 code=3`(2回とも同じ。再現する)、`[wifi] result=fail stage=join ping_ip=0 ping_dns=0`。`ip` 以降は未実施 |
| UW4 | `d` | `dns`、`ping_ip`、`ping_dns`、`result=pass` | **未確認**(`join` で打ち切り) | — |
| UW5 | `d` を2回続けて `pass` | `result=pass` が2回 | **不合格**(2回とも `result=fail stage=join`) | `wifi-03-d1.log`、`wifi-04-d2.log` |

### 所見(コーディング・設計へ戻す事項と、ユーザーの判断)

| # | 所見 | 内容 |
|---|---|---|
| 1 | **ESP32 の AT ファームは入っている** | 115200bps、UART1(SCI1)で応答。EN リセット(P43)は効き、`ready` が 約 0.7 秒で来る。SW3・配線・SC 設定は問題なし。RW1〜RW4・RW5 は否定された |
| 2 | **AT の版が古い(`AT version:1.1.3.0`、SDK v3.0.7、2018-11-28)** | 設計 §21.9 RW10 に該当(版が 2 未満)。`AT+SYSSTORE` は未対応(`store` が `fail`。**ESP32 に設定が保存される可能性あり。パスワードが ESP32 のフラッシュに残りうる**)。`AT+CIPDOMAIN`・`AT+PING` の書式と応答が 2.x と違う可能性(未確認)。`AT+CWJAP` の保存の挙動も違う(1.x は `CWJAP_DEF` / `CWJAP_CUR` の区別あり) |
| 3 | **`join` が `+CWJAP:3`(AP が見つからない)で2回とも失敗** | 原因は未特定。疑う順: (a) SSID の打ち間違い・全角や余分な空白(**デバッグ担当は `wifi_secret.h` を開けないので確認していない。ユーザーが目視で確認**)、(b) ルーターの SSID が 5GHz だけ、または 2.4GHz が別の SSID、バンドステアリング、ステルス(非公開)SSID、(c) 古い AT(1.1.3.0)とルーターの設定(チャネル 12・13、WPA2/WPA3 混在、PMF 必須、11ax のみ、など)の相性、(d) ESP32 のアンテナの感度(基板上の位置)。**`AT+CWLAP`(周囲の AP の一覧)を、秘密を出さない形(自分の SSID があるかだけ)で試験に足すと、(a)(b)(c) が切り分けられる**(コーダーへ) |
| 4 | **ユーザーの判断が要る事項** | ① **ESP32 の AT ファームを新しい版(2.x)に書き換えるか**(設計 §21.2「書き込み」。今のファームの退避、esptool と ESP-AT のダウンロード、SW3 の 2ch の操作が要る。**デバッグ担当は実施していない**)。② 先に、古い版(1.1.3.0)向けにコマンド表を直して試すか(コーダーの作業。`AT+SYSSTORE` を使わない、`AT+CWJAP_CUR` / `AT+CIPDOMAIN` の有無の確認など) |
| 5 | 受信・送信の経路は健全 | `[hb]` は `join` の 16 秒の間も途切れず、`err=0`、`drop=0`、`code=10`(受信異常)なし |

### 実機の状態(終了時)
製品版(P2b。`jobs\wifibuild\product\`)を `HardwareDebug\` に戻して書込み、`[hb]` が +100 ずつ進むことを確認。試作版の elf・mot は `HardwareDebug\` に残していない(`cmp` で製品版と同一)。

---

## Wi-Fi(ESP32 AT)試作 再試験(2026-11-25 12:57〜13:03、デバッグ担当)

**結論: ゴール達成。`[wifi] result=pass` が `d` の2回連続で出た。`ping_ip` と `ping_dns` が ok。** 前回の `join` 失敗(`code=3`)は、パスワードの打ち間違い(ユーザーが修正)と AT 1.1.3.0 向けの手順(`qap` `lapopt` `scan` を追加、`store` を廃止)に直した版で解消した(原因の切り分けは未実施。この版では通った、という事実だけ)。

条件: 試作版(`APP_WIFI_TEST=1`、自動開始なし、ELF 2,402,184 B)。SW3 の 2ch = OFF。ログ: `jobs\ut\wifi2-01-boot.log`、`wifi2-02-d1.log`、`wifi2-03-d2.log`。SSID・パスワード・生の `+CWLAP:` 行・他の AP の名前と MAC は、ログに出ていない(`key=oth` の形だけ。英字は `a`、数字は `9`)。

| ID | 結果 | 証拠 |
|---|---|---|
| UW0 | **一部不合格**(`[hb]` の `drop=0` が守れない) | `[boot]`、`[init] stage=wifi ok=1 err=0 detail=0`、`[hb]` +100、`err=0`。ただし **`drop` が `d` の実行中に増える**(1回目 3、2回目まで累計 5)。下の所見 1 |
| UW1 | 合格 | `step=reset ok ms=20`、`boot ok ms=680`、`at ok try=1` |
| UW2 | 合格(版は古いまま) | `echo` ok、`gmr` ok(`key=ver val=1.1.3.0(5a40576_-_Nov_28_2018_12:50:55)`、`key=sdk val=v3.0.7`)、`mode` ok、`qap` ok。**メジャー 1 のまま動く**(基準「2 以上」は古い版向けの手順で不要になった) |
| `scan`(新) | 合格 | `[wifiv] key=scan val=total=17,found=2,near=0,bad=0`(2回目 total=20)、`key=ap val=ch=4,rssi=-39,ecn=4`(2回目 rssi=-41)、`step=scan res=ok try=1 ms=1680 code=0`(code=0 = ユーザーの AP が見える。`bad=0` なので `+CWLAP:` の形は想定どおり) |
| UW3 | 合格 | `step=join res=ok try=1 ms=5490 code=0`(2回目 5140)、`step=ip res=ok`、`key=ip` はプライベートアドレス(0.0.0.0 でない)。`gw`・`mask` は出ない(設計どおり) |
| UW4 | **合格(ゴール)** | `step=dns res=ok ms=20`(`key=dns` は公開アドレス)、`step=ping_ip res=ok ms=30`、`key=ping val=15`、`step=ping_dns res=ok ms=30`、`key=ping val=25`、`[wifi] result=pass stage=none ping_ip=15 ping_dns=25` |
| UW5 | 合格(`d` をもう1回。約 70 秒後ではなく、1回目の完了から約 2 分後) | 2回目も `result=pass stage=none ping_ip=9 ping_dns=7`(`join` 5140ms、`scan` total=20) |
| 秘密 | 合格 | 受信ログの `[wifi]` `[wifiv]` `[hb]` 以外の行は、受信開始時の途切れた1行だけ。生の AP 一覧・MAC なし(`key=oth` は形だけ) |

### 所見

| # | 内容 |
|---|---|
| 1 | **ログ行の取りこぼし(`drop`)**: `[hb] drop=3`(1回目)→ 5(2回目の後)。`gmr` と `scan` の周辺で、短い間に行が多く出る所(`qap` ok の直後)で、ログのキュー(8行)が溢れる。**欠けた行: `step=lapopt res=start` と `res=ok`(1回目)、`step=scan res=start`**(2回目は `lapopt res=ok` は出て、`scan res=start` が欠けた)。動作と判定には影響しない(`result=pass`、`scan` の結果は出る)。設計 UW0 の `drop=0` を守るなら、コーダーへ: `[wifiv] key=oth` と `key=ap` の行を減らすか、`wifi_core` が1周回に出す行数を制限する(行キューは `APPLOG_QUEUE` 8行)。 |
| 2 | `key=oth` は `gmr` と `ip` で出る(`gmr` の途中の2行と、`+CIPSTA` 系の MAC 行の形)。生の値は出ていない。仕様どおり |
| 3 | ESP32 の AT ファームは書き換えていない。`AT+RESTORE` も実行していない。ファームの版は 1.1.3.0 のまま |
| 4 | **ping の応答**: 8.8.8.8 = 15ms と 9ms、`www.google.com` = 25ms と 7ms。`join` は 5.1〜5.5 秒 |

### 実機の状態(終了時)
製品版(P2b。`jobs\wifibuild\product\`)を `HardwareDebug\` に戻して書込み、`[hb]` が +100 ずつ進むのを確認(`frm=1`、`err=0`、`drop=0`)。試作版の elf・mot は `HardwareDebug\` に残していない(`cmp` で製品版と同一)。残留プロセスなし。

---

## 天気・気温の取得 第1段階: 関門 X0(専用モード)の実機確認(2026-11-25 14:11〜14:18、デバッグ担当)

**結論: X0 合格。`x` で `wx_mux`〜`wx_recv` がすべて `ok`、`[wx] res=ok http=200`。6回連続で成功。ESP32 1.1.3.0 の TCP の書式は想定どおりで、コード直しは不要。製品への組み込みに進んでよい(下の所見 2 だけ、PC の時計の注意)。**

条件: 試作版(`jobs\wxbuild\test\`、`APP_WIFI_TEST=1`、`APP_WX=0`、ELF 2,463,288 B)。SW3 の 2ch = OFF。ログ: `jobs\ut\wx-01-boot.log`〜`wx-08-x6.log`。SSID・パスワード・IP・応答の本文・`Date:`・要求の中身は、どのログにも出ていない(`latitude` `generationtime` `HTTP/1` `Date:` `open-meteo` `GET /` `+IPD` `+CWLAP` の一致 0 件)。

| ID | 結果 | 証拠 |
|---|---|---|
| X0 起動・`d` | 合格 | `[init] stage=wifi ok=1 err=0 detail=0`、`d` で `[wifi] result=pass stage=none ping_ip=9 ping_dns=14`(回帰なし。`join` 5130ms) |
| **X0 `x`(6回)** | **合格** | 毎回 `wx_mux ok`、`wx_close ok`、`wx_tcp ok ms=280〜300`、`wx_send ok ms=20`、`wx_recv ok ms=320〜340`(すべて `try=1`)。`[wx] res=ok src=uart temp10=131 wmo=0 kind=sun http=200 ms=620〜660 sync=1 jst=hhmmss obs=1400/1415 closed=1`。`err=` なし、`[err]` なし |
| RW-X1(書式) | **想定どおり** | `[wifiv] key=oth` の形は1種類だけ: `aaaa_999_aaaaa`(= `Recv 190 bytes`。`wx_send` の中)。`>`、`SEND OK`、`+IPD`、`CONNECT`、`CLOSED` は分類できていて `oth` に出ない。`key=oth` は他に `gmr`(`aaaaaaa_aaaa:aaa_99_9999_99:99:99`、`aaa_aaaaaaa:9.9.9(aaaaa-99)`)と `ip`(`+aaaaa:aaaaaa,"a9:9a:99:a9:99:99"`)の既知の形だけ |
| X3 時刻同期 | **合格(±3 秒以内)** | `[wx] jst` と行の PC 受信時刻の差(PC − 機器): 1.95、2.47、1.99、2.35、2.75、2.09 秒(平均 2.27、最大 2.75)。`s` の `clk` と PC の差: 1.36、1.92、1.39、1.77、2.15、1.53 秒(平均 1.69、最大 2.15)。`date=20261125 wd=3`(水曜)は PC と一致。毎回 `sync=1`、`age=4` |
| X3 の注意(所見 2) | PC の時計が約 1.7 秒進んでいる | 機器は PC より約 1.7 秒遅れて見えるが、**PC 側が速い**。`w32tm /stripchart` が −1.67 秒、Open-Meteo の `Date:`(秒切り捨て)と PC の差が平均 2.2 秒(正しければ約 0.5 秒)。補正すると機器の `clk` は真の時刻とほぼ一致(誤差 ±0.5 秒以内)。**`WX_SYNC_BIAS_MS` は調整不要** |
| X4 `curl` との照合 | **合格(完全一致)** | 14:16:56 に `curl` で同じ URL: `current.time=2026-11-25T14:15`、`temperature_2m=13.1`、`weather_code=0`。機器(14:15:43 〜 14:16:37)は `obs=1415 temp10=131 wmo=0`。初回(14:14)は `obs=1400 temp10=131 wmo=0` |
| X5 の一部 | 未実施 | 専用モードでは `[ev]` と `req=ignored` が無い(製品で確認) |
| 秘密 | 合格 | 上のとおり。受信ログの `[wifi]` `[wifiv]` `[wx]` `[hb]` 以外の行は、受信開始時の途切れた行だけ |

### 所見

| # | 内容 |
|---|---|
| 1 | **`drop`**: `[hb] drop=1`(`d` の最中。`step=scan res=start` の行が欠ける。前回 3〜5 から減った)。`x` の6回では増えない。動作に影響しない(`result=pass`) |
| 2 | **PC の時計**(NTP 未同期の疑い): 約 1.7 秒進んでいる。X3 を厳しく見るなら、PC を同期してから再測定。**機器は ±3 秒に収まっている** |
| 3 | 取得の所要: `wx_tcp` 約 0.3 秒、`wx_recv` 約 0.33 秒、全体 0.62〜0.66 秒(設計の上限 5 秒に対し十分) |
| 4 | ESP32 のファーム(1.1.3.0)は書き換えていない。`AT+RESTORE` も実行していない |

### 実機の状態(終了時)
製品版(`jobs\wxbuild\product\`)を `HardwareDebug\` に戻して書込み(`cmp` で一致)、`[hb]` が +100 ずつ進むのを確認(`frm=1`、`err=0`、`drop=0`)。試作版は `HardwareDebug\` に残していない。残留プロセスなし。

---

## 天気・時刻同期つき製品版(`APP_WX=1`)の実機検証 X1〜X13(2026-11-25 18:30〜18:45、デバッグ担当)

**結論: 途中まで実施(機械判定できる X1〜X4・X10・X12・X13 の大半と、UT10〜UT14・UT16・UT17 の回帰は合格)。18:42 に基板が応答しなくなり(RFP が `E4000004` → `E4000003`、UART 無音)、X6・X7・X8・X9・X11 は未実施で止めた。** 外部要因(USB・給電の不調の疑い)なので、再試行を重ねていない。

条件: `jobs\wxbuild\prod_wx\`(`APP_WX=1`、ELF 2,492,756 B)を RFP で書込み。書込みは `E4000004` で 1 回失敗、再試行で成功。SW3 の 2ch = OFF。ログ: `jobs\ut\wxp-*.log`。PC の時計は、この時点で約 ±0.3 秒に合っていた(`w32tm /stripchart` +0.11 秒、`curl` の `Date:` との比較)。前提の「PC が約 1.7 秒進んでいる」は、検証の開始前に直っていた(補正なしで比べた)。

| ID | 判定 | 証拠 |
|---|---|---|
| X1 起動〜最初の取得 | **合格** | `[init]` lcd / touch / timer / loop が ok、`[perf]`、約 3 秒後に `[wx] net=conn`、`join` 4.1 秒、`[wifi] result=pass`、`[wx] res=ok src=boot ... http=200 ms=640 sync=1`。**接続開始から最初の取得まで 5.5 秒**(基準 20 秒以内)。`judge_ui.py`: C1〜C5 合格、`res=start` と `key=oth` は 0 行。`[hb]` は +100、`err=0`、`uf=0`、`drop=0` |
| X2 気温・天気 | **合格** | `temp10=108 wmo=1 kind=sun`(範囲内)、`ms=620〜660`(基準 ≦ 5000) |
| X3 時刻同期 | **合格** | 同期ごとの機器の時計と PC の差(機器 − PC): 4 回の同期で −0.2〜+0.55 秒(区間の中心。−0.31〜−0.08、+0.54〜+0.56、+0.24〜+0.25)。**100 秒間のずれの増え方は 0.01 秒未満**(`s` を 101 回)。`date=20261125 wd=3`(水)は PC と一致。**`WX_SYNC_BIAS_MS=700` は妥当(調整不要)**。LCD の目視は未実施 |
| X4 `curl` との照合 | **合格(完全一致)** | 18:37:43 の `curl`: `time=2026-11-25T18:30`、`10.8`、`weather_code=1`。機器: `obs=1830 temp10=108 wmo=1` |
| X5 更新ボタン(UART `x`) | **合格(`x` の部分)** | `[ev] btn=h_wx st=down src=uart` → `st=act`(約 140ms 後)→ `[wx] res=ok src=uart`。**1 秒後の 2 回目は `[wx] req=ignored src=uart`**。5 秒以上あけた `x` は 3 回とも取得。取得中は `[hb]` 間の `frm` が約 6.7 コマ/秒で増え(更新アイコンの回転)、終わると止まる。**取得中の `x`(`req=busy`)と実タッチは未実施** |
| X6 定期取得(10 分) | **未実施**(基板が止まった) | — |
| X7 表示面の画素比較(gdb 注入) | **未実施**(同上) | — |
| X8 OFFLINE・AP 停止 | **未実施**(ユーザー作業) | 手順は報告に記載 |
| X9 デモの回帰 | **一部合格** | UT10・UT11・UT12・UT13(`[ev]` 36 行)・UT14・UT16・UT17 は `judge_ui.py` で合格(UT16・UT17 は、前の試験で設定温度が上限になっていたため、リセット後に取り直した。最初の UT16 の 20 本は状態の汚れで、不具合ではない)。UT15・UT19 は実タッチ(ユーザー)。**デモ中に定期取得が重なる試験は未実施** |
| X10 `[perf]` | **合格** | `copy_us=400 home_us=800 slide_us=600`(基準 home ≦ 1,400) |
| X11 受信の余裕 | **一部** | `[err] code=10` は 1 回(下の所見 2)。`s` 連打と描画 + 取得の重なる試験は未実施 |
| X12 ビルド | **合格(ビルド担当の記録)** | `build.log`: 0 errors、0 warnings(`-Wstack-usage=100` の警告なし)。text 1,197,408 + data 2,876 = 1,200,284(≦ 1,250,000)。bss 20,928(従来版 18,368 に対し +2,560 ≦ 3.2KB) |
| X13 秘密 | **合格** | 受信ログ全部(`wxp-*.log`)で `latitude` `generationtime` `HTTP/1` `Date:` `open-meteo` `GET /` `+IPD` `+CWLAP` の一致 0 件。SSID・パスワードは見ていない |
| V1 目視 | **未実施**(ユーザー) | — |

### 所見

| # | 内容 | 差し戻し先 |
|---|---|---|
| 1 | **`wx_tcp` が 1 回だけ 6,040ms かかった**(`x` の 2 回目の取得。ほかは 270〜300ms)。取得全体 6,390ms で、X2 の `ms ≦ 5000` を超えた。時間切れ(10 秒)内で成功。ESP32 の DNS・接続の遅れ(RW-X13)。頻度は 1 / 8 回 | 設計(`ms` の基準を `wx_tcp` の外れ値を許す形に。コード直しではない) |
| 2 | **`[err] code=10 detail=3`(SCI1 の受信のフレーミングエラー)が 1 回**。最初の起動の列(書込み直後の走行から RFP のリセットまでの間)で出た。リセット後の 4 回の起動・約 12 分の走行では出ていない。ESP32 の起動中・RFP のリセットの信号の乱れによる可能性が高い(未確定) | 参考。再現しなければ対処不要 |
| 3 | **試験の途中で基板が応答しなくなった**。18:42:28 の `rfp -sig -run`(リセット)が `E4000004`、再試行が `E4000003`。その後、UART が無音(`[boot]` も `[hb]` も出ない)。E2 Lite と COM7 は PC から見えている。直前までは、書込み 1 回とリセット 5 回(約 12 分の走行)が正常だった。**給電の不足(ESP32 の送信ピーク + USB ハブ。設計 RW-X6)**か、**MCU の停止**が疑わしいが、未特定(電源の入れ直しは未実施) | ユーザー(電源の入れ直し) |

### 実機の状態(終了時)
- **基板には、書込み済みの `prod_wx`(SSID・パスワード入り)が残ったまま。応答なし。** 従来版(`prod_base`)の書込みは、RFP が接続できず未実施。
- `HardwareDebug\` には、従来版(`prod_base`)の成果物だけがある(`prod_wx` を置いていない。`cmp` で確認)。
- 残留プロセス: rfp-cli・e2-server-gdb・rx-elf-gdb・e2studio なし。

---

## 天気つき製品版(`APP_WX=1`)の実機検証 続き(2026-11-25 18:45〜19:30、デバッグ担当)

**結論: 機械判定できる項目は、X1〜X7・X9(重なり)・X10〜X13 まで合格。** 前回止まった事象は、再開後の約 45 分(37 分の連続走行を含む)で再現しなかった。残りは、ユーザー作業(実タッチ X5・UT15・UT19、AP 停止 X8、目視 V1)だけ。基板には `prod_wx`(SSID・パスワード入り)が書込み済みのまま走行中(`prod_base` は、指示により未書込み)。

### 停止の状況と手掛かり

| 項目 | 内容 |
|---|---|
| 再開時 | ユーザーが ECN1 と CN8 の USB を挿し直した後、COM7 を 15 秒受信したが**無音**(`[boot]` も `[hb]` も出ない)。E2 Lite と COM7 は PC から見えていた |
| 回復 | `rfp-cli -sig -run`(リセット解除)を 1 回実行して `Operation successful`。直後に `[boot]` から起動した。**USB の入れ直しだけでは MCU が走り出さなかった**(電源投入だけでは起動しない状態。E2 Lite の接続を待つ状態、または給電が不安定な状態のどちらか。未特定) |
| 前回の失敗 | `E4000004`(1 回目)→ `E4000003`(2 回目)。今回は発生なし |
| 再現観察 | リセット後 `[boot]` 1 回、**36.7 分の連続走行**(`[hb]` 2,202 本、tick 差 100 のみ、`err=0`、`drop=0`、`uf=0`、`[err]` 0 行、`res=fail` 0 行)で止まらなかった。続く gdb・UART 試験(約 10 分)でも止まらなかった |
| 参考 | 初回の起動の列(書込み直後〜RFP リセット)に `[err] code=10 detail=3`(SCI1 のフレーミングエラー)が 1 回(前回と同じ)。リセット後の起動では出ない |

### 判定表(X1〜X5 は前回の続きの追加分のみ)

| ID | 判定 | 実測値・証拠 |
|---|---|---|
| X6 定期取得(10 分) | **合格** | `jobs\ut\wxp-20-soak.log`: `src=boot` 18:46:29、`src=timer` 18:56:29、19:06:29、19:16:29。**間隔は毎回 600.0 秒(±0.1 秒)**。`temp10` と `obs` は 108/1845 → 108/1845 → 107/1900 → 107/1915 で、`curl` の更新(15 分刻み)に追従。`ms=640〜710` |
| X7 表示面の画素比較 | **合格(17 状態すべて 0 差)** | `tools\gdb\mi-wx-fb.gdb`。状態: 通常(取得済み・同期済み)、OFFLINE、接続中、エラー、雨 −3.4℃、雷 10.5℃、雪 −10.5℃、不明の天気、stale(31 分前)、stale 復帰、未取得(`--.-`)、未同期の時計(`--:--`)、再同期、更新ボタン押下中(`fb_btn=16 fb_phase=press`)、取得中の回転 2 通り、通常。**表示面の添字の違い 0、パネル色の違い 0、パレットは `ui_pal` と一致**(`fb2png.py check --c8` と、今回作り直したホスト描画 `fb_hostrender_wx.exe`)。書き換えた値(`s_sched.net` `s_wx_have` `s_wx_ok_ms` `s_clk.synced` `s_wx_temp` `s_wx_kind`)は最後に元へ戻した。押下の白光(flash)と回転 4 段階の全部は、ホストの試験(S25)が担当で、実機では押下・回転の 2 通りまで |
| X9 デモ + 定期取得の重なり | **合格** | 18:56:19 に `d`、18:56:29.1〜29.8 に定期取得(デモの開始から 10 秒、湯はり中)。デモ 33.0 秒(`demo=end` 18:56:52)、`[ev]` 36 行、`judge_ui.py` の UT13 と C1〜C5 合格、`uf=0`、`drop=0`、取得は `res=ok` |
| X11 受信の余裕 | **合格(注記あり)** | `x` の連打(1 秒ごと 45 回): 取得 9 回(すべて ok、`ms=600〜660`)と `req=ignored` 36 回、`[err]` 0、`uf=0`、新しい `drop` 0。更新ボタン 2 連打(0.3 秒間隔): 2 回目は **`[wx] req=busy src=uart`**。`s` を `x` の直後に 30 連続(0.3 秒に約 120 行): **`drop` が 57 に増えた**(ログのキュー 8 行を超える人工的な負荷。取得は ok、`[err]` 0、`uf=0`)。通常の使い方では `drop=0` |
| X5 UART `x`(追加) | **合格** | 取得中の `x` → `req=busy`。1 秒後・約 5 秒未満 → `req=ignored`。5 秒以上あけると取得 |
| 長時間の走行 | **合格** | 上の「再現観察」のとおり |
| 目視(粗い確認) | 一部 | カメラ(`jobs\cam\wx_home.jpg`。角度が悪く細部は見えない)で、LCD の時計が **19:28**、PC の時刻と一致。ほかは未実施(V1) |

### 所見

| # | 内容 | 差し戻し先 |
|---|---|---|
| 1 | `s` を数十個連続で送ると `drop` が増える(1 `s` = 3〜4 行)。製品の通常の使い方では出ない | 参考(対処不要の想定。直すならキューを 16 行に) |
| 2 | `fb_hostrender.exe`(11-22 版)は古く、天気つきの状態は描けない。`fb_hostrender_wx.exe` を作り直した | — |
| 3 | 取得中は `x` が 2 回とも `[ev] btn=h_wx st=act` を出す(2 回目は `req=busy`)。仕様どおり | — |
