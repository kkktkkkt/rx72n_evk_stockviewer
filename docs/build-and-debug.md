# ビルド・デバッグ手順(LLM向け)

最終更新: 2026-11-19

## 0. 状態(重要)

| 項目 | 状態 |
|---|---|
| `tools/find-tools.ps1` | **未実行・未検証** |
| `tools/build.ps1` | **未実行・未検証** |
| `tools/debug.ps1` | **未実行・未検証** |

これらは `.launch` / `.cproject` の実際の設定と e² studio の一般的な使い方から組み立てた。**ユーザーPCで一度も走らせていない。** 初回に実行した結果(成功/失敗、実際のパス、直したところ)を、下の「実行記録」に必ず書き足す。

## 1. 使い方(PowerShell、ユーザーPCで実行)

```powershell
cd D:\workspace\rx72n\rx72n_gcc
.\tools\find-tools.ps1                 # 必要なexeの場所を探して表示
.\tools\build.ps1 -Mode Headless       # e² studio をヘッドレスで起動してビルド(e² studio を閉じてから)
.\tools\build.ps1 -Mode Make           # 生成済みMakefileを使う。事前に e² studio で1回ビルドしておく
.\tools\debug.ps1                      # E2 Lite で書込み+実行(GDBバッチ)
```

## 2. ビルドの仕組み

| 項目 | 値 |
|---|---|
| ビルド構成 | `HardwareDebug`(出力先 `D:\workspace\rx72n\rx72n_gcc\HardwareDebug`) |
| 成果物 | `HardwareDebug\rx72n_gcc.elf` |
| ヘッドレスの本体 | `e2studioc.exe -nosplash -application org.eclipse.cdt.managedbuilder.core.headlessbuild -data <ワークスペース> -build rx72n_gcc/HardwareDebug` |
| 制約 | 同じワークスペースを開いた e² studio が起動中だとロックで失敗する可能性が高い |
| GUIでの代替 | e² studio のプロジェクトを選び `Ctrl+B`、または金槌アイコン |

## 3. デバッグの仕組み(`rx72n_gcc HardwareDebug.launch` の実設定)

| 項目 | 値 |
|---|---|
| サーバ | `${renesas.support.targetLoc:rx-debug}\e2-server-gdb`(e² studio が持つ支援フォルダ内) |
| サーバ引数 | `.launch` の `com.renesas.cdt.core.serverParam`(`-g E2LITE -t R5F572NN -uConnectionTimeout= 30 -uClockSrcHoco= 1 -uAllowClockSourceInternal= 1 -uUseFine= 1 -uJTagClockFreq= "6.00" ...`) |
| GDB | `rx-elf-gdb -rx-force-isa=v3 -rx-force-double-fpu` |
| 接続先 | `localhost`(ポートは `.launch` に明示なし。既定 `61234` と想定 → **未確認**) |
| 初期コマンド | `monitor set_internal_mem_overwrite 0-645` / `monitor force_rtos_off` |
| 停止位置 | `main` |
| 接続方式 | E2 Lite、FINE=0(JTAG)、JTAGクロック 6.00MHz、供給電圧 3.3V |

`tools/debug.ps1` は `.launch` を毎回読み込んでサーバ引数を取り出すので、e² studio 側で設定を変えても追従する。

## 4. 実行記録(ここに追記する)

| 日付 | コマンド | 結果 | 備考(実際のパス、直した点) |
|---|---|---|---|
| 2026-11-19 22:08 | e² studio GUI: ビルド → デバッグ構成 `rx72n_gcc HardwareDebug` を実行(ユーザー実施) | **E2 Liteでの接続に成功**(ユーザー申告) | DIPスイッチは現状のままで接続できた(位置は不明のまま)。既存の `.launch` のままで接続できたので、JTAG/FINEの食い違いは現状では問題なし。`main` で停止したかなど詳細は未報告 |
| (未実行) | `tools/find-tools.ps1` / `tools/build.ps1` / `tools/debug.ps1` | | 引き続き未検証 |

## 5. うまくいかないとき

| 症状 | 確認すること |
|---|---|
| `find-tools.ps1` で exe が見つからない | e² studio のインストール先(`D:\Renesas\e2stuido`)と、`C:\Users\<user>\.eclipse\com.renesas.platform_download` を探索対象に追加 |
| ヘッドレスビルドが即終了 | e² studio を閉じる。それでも駄目なら `-data` に別ワークスペースを指定し `-import` でプロジェクトを取り込む |
| `make` が見つからない | e² studio 付属の `make.exe` を `find-tools.ps1` の結果から指定 |
| GDBが接続できない | ポート番号を `e2-server-gdb` の出力で確認し `-Port` で指定。E2 Liteの接続(USB)と他のデバッガ起動中でないかを確認 |

## 追記: 2026-11-19 22:48 CLI経由(job-runner)での実行記録

| 日時 | 操作 | 結果 | 備考 |
|---|---|---|---|
| 22:33 | `ping` / `find-tools` | 成功 | e2-server-gdb と rx-elf-gdb の本物は `C:\Users\kkktk\.eclipse\com.renesas.platform_1953403423\DebugComp\RX\`。GCCは `D:\Renesas\GNURX\GCC_fo_ Renesas_RX_14.2.0.202607-GNURX-ELF\`(フォルダ名は `fo_` が実在)。make は e² studio 付属 `...\mk\make.exe` |
| 22:34 | `build` Headless | 失敗 | e² studio 起動中のロック。起動中は不可 |
| 22:35 | `build` Headless | 失敗(`r_qe_display_rx` が QE_DISPLAY_CONFIGURATION 未定義でコンパイル不可) | QE for Display を使わないなら、SCでコンポーネントを削除する |
| 22:35〜22:44 | 同上 | ランナーが停止 | `e2studioc` がビルド後に終了しない → `build.ps1` に「結果が出て15秒後に強制終了」を追加 |
| 22:47 | `build` Headless(r_qe_display_rx 削除後) | **成功: 0 errors, 0 warnings**(64秒)。`rx72n_gcc.elf` 1,839,068バイト | `.text` 0x8408、`.rodata` 0xbf924(画像3枚はROM)、`.bss` 0x11e8。RAM2 は未使用(フレームバッファは実行時に割当) |
| | 直した点 | | `build.ps1`: `$ErrorActionPreference='Continue'`(exeのstderr警告で止まっていた)、ヘッドレス終了待ちの時間制限。`job-runner.ps1`: 引数は配列ではなくハッシュテーブルで渡す |

## 追記: 2026-11-19 23:2x CLI経由のデバッグ実行記録(job-runner)

| 日時 | 操作 | 結果 | 備考 |
|---|---|---|---|
| 22:56 | `server-start` | 成功 | ポートは**毎回変わる**(61234ではない)。サーバログの `GDB: <port>` を読む |
| 22:57〜23:03 | `gdb`(接続) | 失敗を3段階で修正 | ①0xC0000135 → python3.dll の場所をPATHへ ②引数の空白 ③all-stop で `Could not read registers` → `nonstop:true`。詳細は `docs/cli-debug.md` §7 |
| 23:06 | `gdb` + `nonstop:true`: `load` | **書込み完了**(819,828バイト、64KB/s)。照合は不可(`compare-sections` 失敗) | `monitor set_internal_mem_overwrite 0-645`、`monitor force_rtos_off` を先に実行 |
| 23:07〜23:13 | `server-start` | E2 Lite 初期化失敗 → ユーザーがUSB抜き差し → 復旧 | 原因未確定 |
| 23:14 | `gdb`: MSTPCRC・RAM2 読出し | MSTPCRC=0xffff0000(bit2=0)、RAM2 読み書きOK | プログラム未実行時の値 |
| 23:1x | `gdb`: `monitor reset` | 受理。**走らない**(tick 変化なし) | `continue -a` が必要(未試験) |
| 23:12 | `serial` COM7 | 0バイト(ログ無効で正常) | |

## 追記: 2026-11-20 20:15〜20:18 (JST) UARTログ機能(app_logfmt 新規ほか)のビルド(ビルド担当)

| 時刻(JST) | コマンド(`tools/submit-job.sh`) | 結果 | 備考 |
|---|---|---|---|
| 20:15:33 | `build '{"mode":"Make"}'` | 失敗(exit 1, 0秒) | `make` が PATH に無い。Make モードは `makeExe` と `toolchainBin` が必須(下記) |
| 20:15:54 | `build '{"mode":"Make","makeExe":"D:\\Renesas\\e2stuido\\eclipse\\plugins\\com.renesas.ide.exttools.gnumake.win32.x86_64_4.3.1.v20240909-0854\\mk\\make.exe","toolchainBin":"D:\\Renesas\\GNURX\\GCC_fo_ Renesas_RX_14.2.0.202607-GNURX-ELF\\rx-elf\\rx-elf\\bin"}'` | **リンク失敗**(6秒) | app_main.c / app_debug.c は**コンパイル成功**。リンクで `applog_fmt_state` `applog_fmt_img` `applog_cmd_parse` `applog_step_index` `applog_fmt_boot` `applog_hb_due` が未定義。原因: 既存の `HardwareDebug/src/app/subdir.mk` と `sources.mk` に `app_logfmt.c` が無い(Make モードは生成済み Makefile をそのまま使い、再生成しない)。失敗したリンクで古い `rx72n_gcc.elf` が消えた(.mot は古いまま残った) |
| 20:17:13 | `build '{"mode":"Headless"}'` | **成功**(70秒、exit 0、`Build Finished. 0 errors, 0 warnings`) | ヘッドレスは Makefile を再生成し、`subdir.mk` に `app_logfmt.c/.lst/.d/.o` が載った。この回は前回の Make で作った app_main/app_debug の .o が再利用されたため、警告は表示されなかった |
| 20:18:03 | `build '{"mode":"Headless","clean":true}'`(警告の全数確認) | **成功**(24秒、exit 0、`0 errors, 6 warnings`) | 全ファイル再コンパイル。以下が最終成果物 |

### 成果物(クリーンビルド 20:18:00 JST = 11:18:00 UTC)
| 項目 | 値 |
|---|---|
| `HardwareDebug/rx72n_gcc.elf` | 1,879,452 バイト、更新 2026-11-20 20:18:00 JST(前回 1,839,068 バイト → +40,384) |
| `HardwareDebug/rx72n_gcc.mot` | 2,527,822 バイト、更新 2026-11-20 20:18:00 JST(前回 2,459,662 バイト → +68,160) |
| `rx-elf-size`(berkeley) | text 839,680 / data 2,876 / bss 10,816 / dec 853,372 |
| `app_logfmt` | `./src/app/app_logfmt.o` が `rx72n_gcc.map` に載り、`.text` に配置(例 0xffc07ab8〜)。newlib の `snprintf` がリンクされた(`vsnprintf` は app_debug が以前から使用) |

### 警告(6件、すべて `-Wstack-usage=100`。`.cproject` の「Warn if stack size exceeds the limit」= 100)
| 区分 | ファイル:行 | メッセージ |
|---|---|---|
| 新規(今回のコード) | `src/app/app_main.c:29` `log_state` | stack usage is 148 bytes |
| 新規 | `src/app/app_main.c:50` `log_img` | stack usage is 124 bytes |
| 新規 | `src/app/app_main.c:86` `app_main` | stack usage is 176 bytes |
| 新規 | `src/app/app_debug.c:145` `app_log_printf` | stack usage is 132 bytes |
| smc_gen(今回のコードではない) | `src/smc_gen/r_bsp/mcu/all/r_bsp_common.c:105` `delay_wait` | stack usage computation not supported for this target |
| smc_gen | `src/smc_gen/r_bsp/mcu/all/r_rx_intrinsic_functions.c:347` `R_BSP_ChangeToUserMode` | 同上 |

- smc_gen の2件が 2026-11-19 22:47 の「0 warnings」の時にも出ていたかは不明(その時のログは残っていない。`-Wstack-usage=100` が `.cproject` に入ったのは同日 13:46 UTC)。
- `app_logfmt.c` 自体の警告・エラーは無し。`app_logfmt` の関数は 100 バイト以内。
- e² studio は閉じている状態だった(ヘッドレスがロックされずに成功したことで確認)。
- 教訓: **新しい .c を `src/` に足したら、Make モードではなく必ず Headless を使う**(Make は Makefile を再生成しない)。Make モードの実行には `makeExe` / `toolchainBin` の指定が必要。ヘッドレス実行のログ末尾の `Managed Build system manifest file error: ... llvm ...` が出るが、ビルドは成功している(影響なしと見ている。原因は未調査)。

## 追記: 2026-11-20 20:45〜20:47 (JST) UART実装の全面改訂(app_logq 新規、nav_step 追加)のビルド(ビルド担当)

| 時刻(JST) | コマンド | 結果 | 備考 |
|---|---|---|---|
| 20:45:37 | `ls jobs/inbox`(投入前) | 空 | デバッグ側のジョブなし |
| 20:45:58 | `build '{"mode":"Headless"}'` | **成功**(22秒、exit 0、0 errors, 0 warnings) | 再生成された Makefile に `app_logq.c` が載り、app_logq/app_logfmt/app_debug/app_main/nav_logic ほかを再コンパイル。再コンパイルした app 系ファイルは警告なし |
| 20:46:46 | `build '{"mode":"Headless","clean":true}'` | **成功**(exit 0、0 errors, 2 warnings) | 全再コンパイルでの警告全数確認。警告2件は smc_gen の "stack usage computation not supported for this target"(`r_bsp_common.c:105` `delay_wait`、`r_rx_intrinsic_functions.c:347` `R_BSP_ChangeToUserMode`)のみ |
| 20:47 | `ls jobs/inbox`(終了後) | 空 | |

### 成果物(クリーンビルド 20:46:58 JST = 11:46:58 UTC)
| 項目 | 値 | 前回(20:18) |
|---|---|---|
| `rx72n_gcc.elf` | 1,896,388 バイト | 1,879,452(+16,936) |
| `rx72n_gcc.mot` | 2,533,766 バイト | 2,527,822(+5,944) |
| text / data / bss | 841,660 / 2,876 / 12,224 | 839,680 / 2,876 / 10,816(text +1,980、bss +1,408) |
| `app_logq.o` | map に載っている(`.text` 0xffc07fa8 付近) | — |

- 前回の `-Wstack-usage=100` 新規4件(app_main.c の log_state / log_img / app_main、app_debug.c の app_log_printf)は**解消**(RX での実コンパイルで警告なし)。app_logfmt / app_logq にも警告なし。

## 追記: 2026-11-20 RFP(Renesas Flash Programmer)での書込み・走行

| 操作 | 結果 | 備考 |
|---|---|---|
| `rfp write`(`-d RX72x -t e2l -if fine -auth id FF…(32桁) -noquery -noprogress -a -run <mot>`) | 合格(約65秒、消去+書込み+ベリファイ、書込み後に走行) | gdb の continue では走らなかったが、RFP の `-run` で走る。詳細は `docs/cli-debug.md` §10 |
| `rfp reset-run`(`-sig -run`) | 合格(約3秒) | gdb 接続で止めた CPU を走らせ直す |
| `serial` COM7 | `[hb] tick=…` 受信(毎秒 +100) | UART の仕様は `docs/design-uart.md` |

## 追記: 2026-11-20 22:52〜22:53 (JST) 画像4枚目(img_03.c 新規追加)のビルド(ビルド担当)

| 時刻(JST) | コマンド(`jobs/inbox` 経由) | 結果 | 備考 |
|---|---|---|---|
| 22:52:29 | 前提確認(`Get-Process` / `runner.alive` / `jobs/inbox`) | OK | e2studio / rfp-cli / e2-server-gdb / rx-elf-gdb 未起動、`runner.alive` 1.2秒前、`inbox` 空 |
| 22:52:54 | `build '{"mode":"Headless"}'` (ジョブ `225254-build`) | **成功**(40秒、exit 0、`0 errors, 2 warnings`) | Makefile 再生成により `smc_gen/` と `src/app/` 全ファイル(`img_03.c` / `images.c` / `app_main.c` 等)をコンパイル・リンク |
| 22:53:49 | `ls jobs/inbox`(終了後) | 空 | |

### 成果物(Headlessビルド 22:53:18 JST)
| 項目 | 値 | 前回(20:46) |
|---|---|---|
| `HardwareDebug/rx72n_gcc.elf` | 2,159,268 バイト (2026-11-20 22:53:18 JST) | 1,896,388 (+262,880) |
| `HardwareDebug/rx72n_gcc.mot` | 3,317,134 バイト (2026-11-20 22:53:18 JST) | 2,533,766 (+783,368) |
| `rx-elf-size` (text / data / bss / dec) | 1,102,784 / 2,876 / 12,224 / 1,117,884 | 841,660 / 2,876 / 12,224 (text +261,124) |
| `g_images` / `g_img_03` (`rx72n_gcc.map`) | `.rodata.g_images` `0xffc0e2d4` (0x10B)、`.rodata.g_img_03` `0xffccd6e4` (0x3fc00B = 261,120B) いずれも ROM 領域 | — |
| RAM2 (`0x00800000`) | 出力セクション配置なし | — |

- 警告は2件のみで、新規の警告・エラーは無し(`src/app/` は 0 warnings)。2件はいずれも既知の `smc_gen` の `-Wstack-usage=100` 非対応警告(`r_bsp_common.c:105` `delay_wait`、`r_rx_intrinsic_functions.c:347` `R_BSP_ChangeToUserMode`)。


## 追記: 2026-11-21 11:45〜11:47 (JST) 製品デモ v2 P1(RGB565、新規 .c 16個、images.c/img_00..03.c 削除)のビルド(ビルド担当)

| 時刻(JST) | コマンド | 結果 | 備考 |
|---|---|---|---|
| 11:45:08 | 前提確認(`Get-Process e2studio,e2studioc,make`) | OK | 該当プロセスなし。直前の ELF/MOT は 2026-11-20 22:53:18(2,159,268 / 3,317,134 バイト) |
| 11:45:08 | Windows 直接: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Mode Headless -TimeoutSec 400`(ログ保存) | **成功**(スクリプト全体 111秒、`Build Finished` 11:46:55、ビルド本体 4.6秒、exit 0、`0 errors, 0 warnings`) | **clean は不要だった**。Makefile が再生成され、`gfx.c` `ui_logic.c` `ui_render.c` `ui_layout.c` `ui_gfx_data.c` `ui_bg_*.c`(7) と `app_*.c` `lcd_panel.c` `touch_ft5x.c` の計18ファイルをコンパイル。削除済みの images.h 等を参照する `No rule to make target` は出なかった(古い .d は Makefile 再生成で消えた)。ログ末尾の llvm の manifest エラーは従来どおり(影響なし) |

### 成果物(Headlessビルド 11:46:55 JST)
| 項目 | 値 | 前回(11-20 22:53) |
|---|---|---|
| `HardwareDebug/rx72n_gcc.elf` | 3,261,128 バイト | 2,159,268(+1,101,860。デバッグ情報を含む) |
| `HardwareDebug/rx72n_gcc.mot` | 6,319,566 バイト | 3,317,134 |
| `rx-elf-size`(berkeley) text / data / bss / dec | 2,103,592 / 2,876 / 12,992 / 2,119,460 | 1,102,784 / 2,876 / 12,224 / 1,117,884 |
| ROM 使用(text + data 初期値) | 約 2,106,468 バイト = 4MB の約 50.2%(基準 ≤ 2.4MB を満たす) | 約 1.1MB |
| `.bss` の増分 | +768 バイト(基準 ≤ 1KB を満たす)。`app_debug.s_q`(行キュー)0x818、`app_main.s_ui` 0xa0 ほか。`.bss` 全体の内訳は ELF のセクション `.bss` 0x1dc0(スタック領域 `.r_bsp_*` 別) | — |
| `.data` | 変化なし(2,876)。新規 .c のデータは全て `.rodata`(ROM) | — |

- 背景7枚: `.rodata.ui_bg_*` が各 0x3fc00(261,120 バイト)、`0xffc102d4`〜`0xffdce6d4`。`ui_gfx_data` の数字・形・バー・タグも `.rodata`(ROM、`0xffdce6d4`〜`0xffe015db`)。`.rodata` 合計 0x1f2224(2,040,356)、`ffc0fa50`〜`ffe01c74`。いずれも ROM 領域。
- **RAM2(`0x00800000`、512KB、フレームバッファ用)に出力セクションなし**: `rx-elf-objdump -h`(セクション表に 0x008xxxxx の VMA/LMA が無い。最大の RAM 側は `.bss` 0x740 + 0x1dc0 と `.r_bsp_ustack` 0x2a00+0x1000)と `rx72n_gcc.map`(0x008xxxxx の配置行は `RAM2 0x00800000 0x00080000` のメモリ定義のみ)で確認。`lcd_panel.c` の面0/面1(`LCD_FB0/FB1_ADDR`)は固定番地のまま、リンカは触らない。

### 警告
- **0件**(`Build Finished. 0 errors, 0 warnings`)。新規 .c(`gfx.c`、`ui_logic.c`、`ui_render.c`、`ui_layout.c`、`ui_gfx_data.c`、`ui_bg_*.c`)と変更した `app_main.c` `app_debug.c` `app_logfmt.c` `lcd_panel.c` は `-Wstack-usage=100` を含め警告なし。
- 今回は増分ビルド(smc_gen は再コンパイルされない)なので、既知の smc_gen の2件(`r_bsp_common.c:105` `delay_wait`、`r_rx_intrinsic_functions.c:347` `R_BSP_ChangeToUserMode` の stack usage 非対応)は出ていない。clean ビルドでの全数確認は行っていない。
- 書込み・実機操作は未実施(E2 Lite が PC から見えていない)。

## 追記: 2026-11-21 13:51〜13:52 (JST) 製品デモ v2 P1 の修正後の再ビルド(`ui_logic.c` D1〜D3、`app_main.c` 起動時 uf 1回を捨てる)(ビルド担当)

| 時刻(JST) | コマンド | 結果 | 備考 |
|---|---|---|---|
| 13:51:04 | 前提確認(`Get-Process e2studio,e2studioc,make`)後、`powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Mode Headless -TimeoutSec 400` | **成功**(スクリプト全体 93秒、`Build Finished` 13:52:33、ビルド本体 4.1秒、exit 0、`0 errors, 0 warnings`) | 該当プロセスなしで開始、終了後も残留なし。コンパイルされたのは `ui_logic.c` と `app_main.c` の2ファイルのみ。clean は不要 |

| 項目 | 今回(13:52:32) | 前回(11:46:55) |
|---|---|---|
| `rx72n_gcc.elf` | 3,261,320 バイト | 3,261,128(+192) |
| `rx72n_gcc.mot` | 6,319,710 バイト(13:52:33) | 6,319,566(+144) |
| text / data / bss | 2,103,640 / 2,876 / 12,992 | 2,103,592 / 2,876 / 12,992(text +48、data・bss 変化なし) |

- 警告 0件(新規なし)。RAM2(`0x00800000` 台)に出力セクションなし(`rx-elf-objdump -h` で VMA が 0x008xxxxx のセクションは 0 個。`.text` 0xf680、`.rodata` 0x1f2224 は ROM)。
- 書込み・実機操作は未実施。

## 追記: 2026-11-21 22:29〜22:33 (JST) CLUT8 実験(`APP_CLUT8_TEST`、新規 .c `clut8_pat.c` `clut8_test.c`)のビルド(ビルド担当、ブランチ feat/clut8-rom)

| 時刻(JST) | コマンド | 結果 | 備考 |
|---|---|---|---|
| 22:29:55 | 前提確認(`Get-Process e2studio,e2studioc,make,rfp-cli,e2-server-gdb,rx-elf-gdb`)。`src\app\clut8_sw.h` の `APP_CLUT8_TEST` を **1** に変更 | OK(該当プロセスなし) | 直前の ELF/MOT は 13:52(3,261,320 / 6,319,710 バイト) |
| 22:29:55〜22:31:29 | **実験版(1)**: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Mode Headless -TimeoutSec 400` | **成功**(スクリプト全体 95秒、`Build Finished` 22:31:29、ビルド本体 7.1秒、exit 0、`0 errors, 0 warnings`) | Makefile 再生成。コンパイルされたのは `src/app/` の20ファイル(新規 `clut8_pat.c` `clut8_test.c` を含む)。clean 不要 |
| 22:31:47〜22:33:05 | `clut8_sw.h` を **0** に戻して**製品版(0)**: 同コマンド | **成功**(77秒、`Build Finished` 22:33:05、ビルド本体 4.2秒、exit 0、`0 errors, 0 warnings`) | コンパイルは `clut8_pat.c` `clut8_test.c` `lcd_panel.c` `touch_ft5x.c`(`board_pins.h` を include)`app_main.c` の5ファイル |

### 成果物
| 項目 | 実験版(1)(22:31:28) | 製品版(0)(22:33:05) | 前回 P1(13:52) |
|---|---|---|---|
| `rx72n_gcc.elf` | 3,312,844 バイト | 3,261,964 バイト | 3,261,320 |
| `rx72n_gcc.mot` | 6,331,654 バイト | 6,319,710 バイト | 6,319,710 |
| `rx-elf-size` text / data / bss | 2,107,616 / 2,880 / 16,444 | **2,103,640 / 2,876 / 12,992** | 2,103,640 / 2,876 / 12,992 |

- **製品版(0)の text/data/bss は P1 最終ビルドと一致**。`.mot` のサイズも P1 と同じ(6,319,710)。`.text` 0xf680、`.rodata` 0x1f2224、`.bss` 0x1dc0 も P1 と同じ。`clut8_pat.o` と `clut8_test.o` は 0 の時 text/data/bss とも 0 バイト。
- 実験版(1)との差: text +3,976、data +4、bss +3,452(`clut8_test` のパレット等。`g_c8_mde` は `.bss` に配置)。ELF のサイズ差はデバッグ情報。
- **RAM2(`0x00800000`、512KB)に出力セクションなし**(両ビルドとも): `rx-elf-objdump -h` で VMA が 0x008xxxxx のセクションは 0 個、`rx72n_gcc.map` に 0x008xxxxx の配置行なし。面A/B(`LCD_C8_FB_A_ADDR` = 0x00800000、`LCD_C8_FB_B_ADDR` = 0x00822000)は実験コードが固定番地で使う(リンカは触らない)。実験版(1)の `.text` 0x104e8、`.rodata` 0x1f2344、`.bss` 0x2b3c は ROM / 内蔵RAM側。
- 警告・エラー: 両ビルドとも **0件**(新規 .c `clut8_pat.c` `clut8_test.c` 由来も 0)。増分ビルドなので smc_gen の既知2件(stack usage)は出ていない。ログ末尾の llvm manifest エラーは従来どおり(影響なし)。
- 注意: ヘッドレスビルドは `.settings/com.renesas.smc.generationsetting.properties` と `.settings/com.renesas.smc.tools.swcomponent.fit.properties` のタイムスタンプ行だけを書き換える(`git status` で M になる。内容の変化なし)。
- **退避した成果物**(`jobs/` は git 管理外): 実験版 = `jobs\c8build\clut8exp\`(`rx72n_gcc.elf` `.mot` `.map`)、製品版 = `jobs\c8build\product\`(同3点)。ビルドログ = `jobs\c8build\build1.log`(実験)、`build0.log`(製品)。`HardwareDebug\` の現在の中身は製品版(0)。
- 最後の `clut8_sw.h` の `APP_CLUT8_TEST` = **0**(`git status` では未追跡の新規ファイル)。書込み・実機操作は未実施。

## 追記: 2026-11-22 10:04〜10:07 (JST) P2b(製品 UI の CLUT8 化。背景・ui_gfx_data・ui_layout の入れ替え、gfx/ui_render/lcd_panel/app_main の変更)のビルド(ビルド担当、ブランチ feat/clut8-rom)

| 時刻(JST) | コマンド | 結果 | 備考 |
|---|---|---|---|
| 10:04:54 | 前提確認(e2studio / e2studioc のプロセス無し)、`src\app\clut8_sw.h` の `APP_CLUT8_TEST` = **0** を確認(変更なし) | OK | 直前の ELF/MOT は 2026-11-21 23:31(3,261,964 / 6,319,710 バイト、P1 + 実験コード 0) |
| 10:04:54〜10:06:29 | Windows 直接: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Mode Headless -TimeoutSec 400`(ログ = `jobs\c8build\p2b\build.log`) | **成功**(スクリプト全体 97秒、`Build Finished` 10:06:29、ビルド本体 4.9秒、exit 0、`0 errors, 0 warnings`) | Makefile 再生成。コンパイルされたのは `src/app/` の16ファイル(`gfx.c` `clut8_test.c` `lcd_panel.c` `touch_ft5x.c` `ui_bg_*.c`(7) `app_main.c` `ui_layout.c` `ui_render.c` `ui_logic.c` `ui_gfx_data.c`)。新規 .c なし。clean 不要。ログ末尾の llvm manifest エラーは従来どおり(影響なし) |

### 成果物(Headlessビルド 10:06:29 JST)
| 項目 | P2b(今回) | P1 最終(13:52、11-21) |
|---|---|---|
| `rx72n_gcc.elf` | 2,334,708 バイト | 3,261,964(23:31 の製品版 0 も同じ) |
| `rx72n_gcc.mot` | 3,513,430 バイト | 6,319,710 |
| `rx-elf-size`(berkeley) text / data / bss / dec | **1,168,212 / 2,876 / 12,992 / 1,184,080** | 2,103,640 / 2,876 / 12,992 |
| ROM(text + data) | **1,171,088**(見込み 約 1,171,000 とほぼ一致) | 2,106,516 |

### 確認基準との比較
| 基準 | 結果 |
|---|---|
| text + data ≤ 1,200,000 | **満たす**(1,171,088、余裕 28,912) |
| P1(2,106,516)から 900,000 以上減 | **満たす**(−935,428) |
| `.bss` の増加 ≤ 64 | **満たす**(増加 0。12,992 のまま) |

- セクション(`rx-elf-objdump -h`): `.text` 0xf8c8(P1 は 0xf680、+584)、`.rodata` 0x10d9d8(1,104,344。P1 は 0x1f2224)、`.data` 0x73c(変化なし)、`.bss` 0x1dc0(変化なし)。`ui_bgi_home` は `.rodata` の `0xffc5014c`(ROM)。
- **RAM2(`0x00800000`、512KB)に出力セクションなし**: `rx-elf-objdump -h` で VMA/LMA が 0x008xxxxx のセクションは 0 個。`rx72n_gcc.map` の 0x008xxxxx は `RAM2 0x00800000 0x00080000` のメモリ定義の行のみ。8bpp の面(面0 `0x00800000`、面1 `0x00822000`、各 139,264 バイト)は固定番地で、リンカは触らない。
- 警告・エラー: **0件**(P2b で変更・再生成した全ファイル、`-Wstack-usage=100` を含む)。増分ビルドのため smc_gen の既知2件(stack usage 非対応)は出ていない(clean ビルドでの全数確認は未実施)。
- `.settings/com.renesas.smc.generationsetting.properties` と `com.renesas.smc.tools.swcomponent.fit.properties` のタイムスタンプ行(`#Sun Nov 22 10:06:20 JST 2026`)をビルドが書き換えた(内容の変化なし。既に git 上 M)。
- **退避した成果物**(`jobs/` は git 管理外): `jobs\c8build\p2b\`(`rx72n_gcc.elf` `.mot` `.map` `build.log`)。`HardwareDebug\` の現在の中身も P2b。書込み・実機操作は未実施。

## 追記: 2026-11-25 10:52〜10:53 (JST) Wi-Fi 試作(`APP_WIFI_TEST`、新規 .c `wifi_at.c` `wifi_core.c`、SC 再生成後の初ビルド)のビルド(ビルド担当、ブランチ feat/clut8-rom)

| 時刻(JST) | コマンド | 結果 | 備考 |
|---|---|---|---|
| 10:52:02 | 前提確認(e2studio / e2studioc / make / rfp-cli / e2-server-gdb / rx-elf-gdb のプロセス無し)。`src\app\wifi_sw.h` の `APP_WIFI_TEST` を 0 → **1** に変更(`wifi_secret.h` は存在。開いていない) | OK | 直前の ELF/MOT は 2026-11-22 10:06(P2b、2,334,708 / 3,513,430 バイト) |
| 10:52:02〜10:53:15 | **試作版(1)**: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Mode Headless -TimeoutSec 400`(ログ = `jobs\wifibuild\test\build.log`) | **成功**(スクリプト全体 73秒、`Build Finished` 10:53:12、ビルド本体 3.6秒、exit 0、`0 errors, 0 warnings`) | Makefile 再生成。コンパイルは14ファイル: smc_gen の7(`r_sci_rx.c` `r_sci_rx_dmaca.c` `r_sci_rx_dtc.c` `r_sci_rx72n.c` `r_sci_rx72n_data.c` `r_sci_rx_pinset.c` `Pin.c`)+ app の7(新規 `wifi_at.c` `wifi_core.c`、`app_main.c` `app_debug.c` `clut8_test.c` `lcd_panel.c` `touch_ft5x.c`)。clean ではない |
| 10:53:30〜10:53:55 | `wifi_sw.h` を **0** に戻して**製品版(0)**: 同コマンド(ログ = `jobs\wifibuild\product\build.log`) | **成功**(25秒、`Build Finished` 10:53:50、ビルド本体 1.2秒、exit 0、`0 errors, 0 warnings`) | コンパイルは `wifi_at.c` `wifi_core.c` `clut8_test.c` `lcd_panel.c` `touch_ft5x.c` `app_main.c` の6ファイル(`wifi_sw.h` を include するもの) |

### 成果物
| 項目 | 試作版(1)(10:53:12) | 製品版(0)(10:53:50) | P2b 最終(2026-11-22 10:06) |
|---|---|---|---|
| `rx72n_gcc.elf` | 2,386,572 バイト | 2,335,076 | 2,334,708 |
| `rx72n_gcc.mot` | 3,529,526 バイト | 3,513,382 | 3,513,430 |
| `rx-elf-size` text / data / bss | 1,173,580 / 2,876 / 19,264 | **1,168,196 / 2,876 / 18,368** | 1,168,212 / 2,876 / 12,992 |

- **製品版(0)と P2b の差の内訳**: text −16(SC 再生成の影響と推定。`.text` 0xf8b8 vs 0xf8c8。`.rodata` は 0x10d9d8 で完全に一致、`.data` 0x73c も同じ)、data 0、**bss +5,376**(`.bss` 0x1dc0→0x32c0。SCI1 のバッファ `SCI_CFG_CH1_TX_BUFSIZ` 2048 / `RX_BUFSIZ` 3500 が入った SC 再生成の効果。見込みの約 5.4KB と一致)。**`wifi_at.o` `wifi_core.o` は製品版(0)で text/data/bss とも 0 バイト**。つまり Wi-Fi 試作のコードによる ROM・RAM の差は 0。
- 試作版(1)と製品版(0)の差: text +5,384(`wifi_at.o` 1,156 + `wifi_core.o` 4,242 + `app_main.c` の呼び出しなど)、bss +896(`wifi_at.o` の bss 926 が主)。試作版(1)の `.text` 0x10a08、`.rodata` 0x10dd90、`.bss` 0x3640。
- **RAM2(`0x00800000`、512KB)に出力セクションなし**(両ビルドとも): `rx-elf-objdump -h` で VMA/LMA が 0x008xxxxx のセクションは 0 個。`rx72n_gcc.map` の 0x008xxxxx は `RAM2 0x00800000 0x00080000` のメモリ定義の行のみ(製品版で確認、試作版は objdump のみ)。
- 警告・エラー: 両ビルドとも **0件**。今回の変更由来(`wifi_at.c` `wifi_core.c` `app_main.c` 等)も、**SC 再生成由来の smc_gen 7ファイル(試作版ビルドで再コンパイル)も 0件**(`-Wstack-usage=100` を含む)。clean ビルドではないので、再コンパイルされていない smc_gen のファイルの警告は未確認。ログ末尾の llvm manifest エラーは従来どおり(影響なし)。
- **秘密の確認**: 両ビルドログに `wifi_secret` の文字列も include の内容も出ていない(`Building file:` の行だけ。ヘッドレスのログに `wifi_secret` は 0 件)。値の grep は未実施(していない)。**試作版(1)の ELF・MOT・map には `wifi_secret.h` の SSID・パスワードが平文で入っている**(設計 §21.5)。他人に渡さない・`jobs\` の外に出さない。製品版(0)には入らない(`wifi_at.o` が空)。
- `.settings/com.renesas.smc.generationsetting.properties` と `com.renesas.smc.tools.swcomponent.fit.properties` のタイムスタンプ行(`#Wed Nov 25 10:53:46 JST 2026`)をビルドが書き換えた(内容の変化なし。既に git 上 M)。
- **退避した成果物**(`jobs/` は git 管理外): 試作版 = `jobs\wifibuild\test\`(`rx72n_gcc.elf` `.mot` `.map` `build.log`)、製品版 = `jobs\wifibuild\product\`(同4点)。`HardwareDebug\` の現在の中身は製品版(0)。
- 最後の `wifi_sw.h` の `APP_WIFI_TEST` = **0**。書込み・実機操作は未実施。

## 追記 2026-11-25 12:55〜12:56 (JST) Wi-Fi 試作 AT 1.1.3.0 向け修正版のビルド(試作版(1)→製品版(0)、ブランチ feat/clut8-rom)

| 時刻(JST) | コマンド | 結果 | 備考 |
|---|---|---|---|
| 12:55:27 | 前提確認: e2studio / e2studioc / make / rfp-cli / e2-server-gdb / rx-elf-gdb のプロセス無し。`wifi_sw.h` の `APP_WIFI_TEST` を 0 → 1 に変更 | OK | `wifi_secret.h` は開かず、内容は確認していない |
| 12:55:27〜12:55:29 | Windows 直接: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Mode Headless -TimeoutSec 400`(ログ = `jobs\wifibuild\test\build.log`) | **成功**(exit 0、`Build Finished` 12:55:29、0 errors, 0 warnings、所要 24.6 秒) | 再コンパイルは `wifi_at.c` `wifi_core.c` `app_main.c` `clut8_test.c` `lcd_panel.c` `touch_ft5x.c` の 6 ファイル。smc_gen は再コンパイルされていない |
| 12:56:00 | `wifi_sw.h` の `APP_WIFI_TEST` を 1 → 0 に戻す | OK | |
| 12:56:00〜12:56:15 | 同上の build.ps1 Headless(ログ = `jobs\wifibuild\product\build.log`) | **成功**(exit 0、`Build Finished` 12:56:15、0 errors, 0 warnings、所要 24.6 秒) | 再コンパイルは `wifi_at.c` `wifi_core.c` `clut8_test.c` `lcd_panel.c` `touch_ft5x.c` `app_main.c` の 6 ファイル |

### 成果物
| 項目 | 試作版(1)(12:55:29) | 製品版(0)(12:56:15) | 前回の製品版(2026-11-25 10:53:50) |
|---|---|---|---|
| `rx72n_gcc.elf` | 2,402,184 バイト | 2,335,076 バイト | 2,335,076 |
| `rx72n_gcc.mot` | 3,534,446 バイト | 3,513,382 バイト | 3,513,382 |
| `rx-elf-size` text / data / bss | 1,175,216 / 2,876 / 19,264 | **1,168,196 / 2,876 / 18,368** | 1,168,196 / 2,876 / 18,368 |

- 製品版は前回と**完全一致**(text / data / bss とも)。`wifi_at.o` と `wifi_core.o` の text / data / bss は製品版で 0(`rx-elf-size` で確認)。
- 試作版の `wifi_at.o` は text 1,147 / bss 938、`wifi_core.o` は text 5,887 / bss 0。
- RAM2(`0x00800000` 台)に出力セクションは無い(`rx-elf-objdump -h` で VMA が `0x008xxxxx` のセクションは試作版・製品版とも 0 個。RAM2 はリンカスクリプトのメモリ定義のみ)。

### 警告・エラー
- 両ビルドとも **0 件**(`0 errors, 0 warnings`)。`-Wstack-usage=100` の警告も出ていない。
- smc_gen は今回再コンパイルされていないため、既知の smc_gen の stack usage 警告 2 件(`r_bsp_common.c:105` `delay_wait`、`r_rx_intrinsic_functions.c:347` `R_BSP_ChangeToUserMode`)は今回のログでは確認していない(clean ビルドは未実施)。
- ログ末尾の llvm manifest エラー(`Managed Build system manifest file error: ... llvm ...`)は従来どおり。ビルド結果には影響なし。

### 秘密情報の確認
- ビルドログ(`build.log` `headless_stdout.log`)に `wifi_secret` の文字列は無い(`Building file:` 行のみ)。ログの中身は SSID・パスワードの値を含まない。
- **試作版(1)の `rx72n_gcc.elf` `.mot` `.map` には `wifi_secret.h` の SSID・パスワードが平文で入っている可能性がある**(設計 §21.5 のとおり)。`jobs\wifibuild\test\` は git 管理外だが、外部へ渡さないこと。製品版(0)の成果物には入っていない(`wifi_at.o` `wifi_core.o` が空)。値の grep は未実施。

### 退避した成果物
- 試作版 = `D:\workspace\rx72n\rx72n_gcc\jobs\wifibuild\test\`(`rx72n_gcc.elf` `rx72n_gcc.mot` `rx72n_gcc.map` `build.log` `headless_stdout.log`)
- 製品版 = `D:\workspace\rx72n\rx72n_gcc\jobs\wifibuild\product\`(同上)
- `HardwareDebug\` の現在の中身は製品版(0)。

### 注意
- `.settings/com.renesas.smc.generationsetting.properties` と `com.renesas.smc.tools.swcomponent.fit.properties` のタイムスタンプ行をビルドが書き換えた(内容は変化なし。既に git 上 M)。
- 書込み・実機操作・COM ポート・rfp-cli・gdb は未実施。
- 最後の `wifi_sw.h` の `APP_WIFI_TEST` = **0**。`APP_WIFI_AUTOSTART` は 0 のまま。

## 追記: 2026-11-25 14:10〜14:12 (JST) 天気・気温取得 第1段階(新規 `wx_core.c` `wx_core.h`、変更 `wifi_core.c` `wifi_at.c` `wifi_sw.h`)のビルド(ビルド担当、ブランチ feat/wifi-at-prototype の作業ツリー)

| 時刻(JST) | コマンド | 結果 | 備考 |
|---|---|---|---|
| 14:10 | 前提確認(e2studio / e2studioc のプロセス無し)。`src\app\wifi_sw.h` の `APP_WIFI_TEST` 0 → 1 | OK | `wifi_secret.h` は開いていない・内容は確認していない |
| 14:10〜14:11:03 | Windows 直接: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Mode Headless -TimeoutSec 400`(ログ = `jobs\wxbuild\test\build.log`) | **成功**(スクリプト 27秒、`Build Finished` 14:11:03、exit 0、`0 errors, 0 warnings`) | Makefile 再生成。再コンパイルは `wx_core.c` `wifi_core.c` `wifi_at.c` `clut8_test.c` `lcd_panel.c` `touch_ft5x.c` `app_main.c` の7ファイル。clean ではない |
| 14:11:17〜14:11:44 | `wifi_sw.h` の `APP_WIFI_TEST` 1 → 0 に戻して同コマンド(ログ = `jobs\wxbuild\product\build.log`) | **成功**(`Build Finished` 14:11:44、exit 0、`0 errors, 0 warnings`) | 再コンパイルは上と同じ7ファイル |

### 成果物
| 項目 | 試作版(1)(14:11:03) | 製品版(0)(14:11:44) | 前回の製品版(2026-11-25 12:56) |
|---|---|---|---|
| `rx72n_gcc.elf` | 2,463,288 バイト | 2,335,388 バイト | 2,335,076 |
| `rx72n_gcc.mot` | 3,556,734 バイト | **3,513,382 バイト** | 3,513,382 |
| `rx-elf-size` text / data / bss | 1,182,648 / 2,876 / 20,928 | **1,168,196 / 2,876 / 18,368** | 1,168,196 / 2,876 / 18,368 |

- 製品版(0)の text / data / bss と `.mot` のサイズは前回と**完全一致**。`.elf` だけ +312 バイト(ロードされない部分の差。原因は未確認。text / data / bss と `.mot` が変わっていないので、ROM・RAM への影響は無い)。
- `wx_core.o` `wifi_core.o` `wifi_at.o` の製品版(0)の text / data / bss は**すべて 0**(`rx-elf-size` で確認)。
- 試作版(1)の各オブジェクト: `wx_core.o` text 5,221 / data 0 / bss 0、`wifi_core.o` text 7,759 / data 0 / bss 0、`wifi_at.o` text 1,531 / data 0 / bss 2,555。
- RAM2(`0x00800000` 台)に出力セクションは無い(両ビルドとも `rx-elf-objdump -h` で VMA が 0x008xxxxx のセクションは 0 個)。

### 警告・エラー
- 両ビルドとも **0 件**(`0 errors, 0 warnings`)。`-Wstack-usage=100` の警告も出ていない。
- smc_gen は再コンパイルされていないため、既知の smc_gen の stack usage 警告 2 件は今回のログでは未確認(clean ビルドは未実施)。
- ログ末尾の llvm manifest エラーは従来どおり。ビルド結果には影響なし。

### 秘密情報の確認
- 試作版(1)の `rx72n_gcc.elf` `.mot` `.map` には `wifi_secret.h` の SSID・パスワードが平文で入っている可能性がある(設計 §21.5)。値の grep は未実施(していない)。`jobs\wxbuild\test\` は git 管理外だが、外部へ渡さないこと。
- 製品版(0)の成果物には入っていない(`wifi_at.o` `wifi_core.o` `wx_core.o` が空)。

### 退避した成果物
- 試作版 = `D:\workspace\rx72n\rx72n_gcc\jobs\wxbuild\test\`(`rx72n_gcc.elf` `rx72n_gcc.mot` `rx72n_gcc.map` `build.log`)
- 製品版 = `D:\workspace\rx72n\rx72n_gcc\jobs\wxbuild\product\`(同上)
- `HardwareDebug\` の現在の中身は製品版(0)。

### 注意
- `.settings/com.renesas.smc.generationsetting.properties` と `com.renesas.smc.tools.swcomponent.fit.properties` のタイムスタンプ行をビルドが書き換えた可能性がある(内容は変化なし。既に git 上 M)。
- 書込み・実機操作・COM ポート・rfp-cli・gdb は未実施。
- 最後の `wifi_sw.h` の `APP_WIFI_TEST` = **0**。`APP_WX` と `APP_WIFI_AUTOSTART` は 0 のまま(変更していない)。
## 追記: 2026-11-25 18:10〜18:12 (JST) 天気・時刻同期を含む製品版のビルド(ビルド担当、ブランチ feat/wifi-at-prototype)

前提: e2studio / e2studioc のプロセス無し。`src\app\wifi_sw.h` の `APP_WX` を 0 → 1 → 0 に切り替えた(`APP_WIFI_TEST` と `APP_WIFI_AUTOSTART` は 0 のまま。最後は `APP_WX` = 0)。`wifi_secret.h` は開いていない・内容を出力していない。成果物(ELF/MOT/MAP)に SSID・パスワードが入りうるため、`jobs\wxbuild\` の外へは出していない。

| 時刻(JST) | コマンド | 結果 | 備考 |
|---|---|---|---|
| 18:10:51〜18:11:46 | `tools\build.ps1 -Mode Headless -TimeoutSec 400`(`APP_WX` = 1) | **成功**(Build Finished. 0 errors, 0 warnings。exit 0) | `rx-elf-size`: text 1,197,408 / data 2,876 / bss 20,928。ROM(text + data)= **1,200,284**(基準 ≤ 1,250,000 を満たす。見積 約 1,201,000 と一致) |
| 18:12:03〜18:12:32 | 同上(`APP_WX` = 0 に戻して再ビルド) | **成功**(0 errors, 0 warnings。exit 0) | text 1,182,612 / data 2,876 / bss 18,368。ROM = 1,185,488 |

### サイズと基準との比較

| 項目 | 結果 |
|---|---|
| 製品版(`APP_WX` = 1)の text + data ≤ 1,250,000 | 満たす(1,200,284、余裕 49,716) |
| `.bss` の増加(対 `APP_WX` = 0 の 18,368) | +2,560(見積 約 +2.6KB と一致) |
| 従来版(`APP_WX` = 0)と前回 P2b+Wi-Fi 記録(text 1,168,196)との差 | text +14,416(見積の +約 18,000 より小さい。UI の差分は計測値の通り。要因は未分解) |
| `wifi_at.o` / `wifi_core.o` / `wx_core.o`(`APP_WX` = 1) | text / data / bss = 1,680 / 0 / 2,534、7,791 / 0 / 0、5,221 / 0 / 0 |
| `app_main.o`(`APP_WX` = 1) | text / data / bss = 2,590 / 0 / 567 |
| `wifi_at.o` / `wifi_core.o` / `wx_core.o`(`APP_WX` = 0) | 全て 0 / 0 / 0 |

### 警告・エラー

- 両ビルドとも **0 errors, 0 warnings**(`-Wstack-usage=100` の警告も出ていない)。
- 毎回出る `Managed Build system manifest file error: ... llvm ... includeFileDir.cpp` は前回までと同じ既知のマニフェストメッセージで、ビルド結果には影響しない。

### RAM2(0x00800000 台)

- `rx-elf-objdump -h`: VMA が 0x008xxxxx の出力セクションは **0 個**(両ビルドとも)。
- `rx72n_gcc.map` の 0x008xxxxx は `RAM2 0x00800000 0x00080000` のメモリ定義の行のみ。

### 成果物(`jobs` は git 管理外。SSID・パスワードを含みうるので外へ出さない)

- 製品版(`APP_WX` = 1): `D:\workspace\rx72n\rx72n_gcc\jobs\wxbuild\prod_wx\`(`rx72n_gcc.elf` 2,492,756 バイト、`rx72n_gcc.mot` 3,601,006 バイト、`rx72n_gcc.map`、`build.log`(要約版))
- 従来版(`APP_WX` = 0): `D:\workspace\rx72n\rx72n_gcc\jobs\wxbuild\prod_base\`(`rx72n_gcc.elf` 2,356,604 バイト、`rx72n_gcc.mot` 3,556,630 バイト、`rx72n_gcc.map`、`build.log`(要約版))
- `HardwareDebug\` の中身は従来版(`APP_WX` = 0)。書込み・実機操作・COM・gdb・rfp-cli は**未実施**。

## 追記: 2026-11-27 09:58〜10:01 (JST) 新プロジェクト rx72n_stockview の初ビルド(ビルド担当)

### 前提と取り込み(新プロジェクト特有の手順)
| 時刻(JST) | 操作 | 結果 | 備考 |
|---|---|---|---|
| 09:58 | 前提確認(e2studio / e2studioc / make / rfp-cli / e2-server-gdb / rx-elf-gdb のプロセス無し) | OK | `jobs\` は無かった |
| 09:58 | `tools\start-runner.cmd` を最小化で起動(`cmd /c`) | 成功 | `jobs\runner.alive` 更新を確認。ランナーは起動したまま |
| 09:58〜09:59(時刻未記録) | 取り込み: `e2studioc.exe -nosplash -application org.eclipse.cdt.managedbuilder.core.headlessbuild -data D:\workspace\rx72n -import D:\workspace\rx72n\rx72n_stockview`(ログ = `rx72n_stockview\jobs\import-headless.log`) | 成功 | ログに「'rx72n_stockview' をオープンしています」。ビルドは走らず、150秒以内にプロセスは終了した(終了コードは PowerShell 側で取れず空。登録フォルダの作成で成功を確認) |

- **取り込みが必要だった理由**: `D:\workspace\rx72n\.metadata\.plugins\org.eclipse.core.resources\.projects\` に登録されていたのは `rx72n_gcc` だけだった。未登録のプロジェクトは `-build` が通らない。取り込みは追加のみで、`rx72n_gcc` の登録は残っている(取り込み後も `.projects\rx72n_gcc` が存在することを確認)。
- 取り込み後、`.projects\rx72n_stockview` が作られた。
- **名前**: `tools/build.ps1`(`$Project` 既定値 `rx72n_stockview`)、`tools/job-runner.ps1`(`$Project` 既定値 `D:\workspace\rx72n\rx72n_stockview`)、`tools/submit-job.sh` はいずれも新プロジェクト名で動く。`rx72n_gcc` を指す箇所はスクリプトに無い(`.cproject` の `src/rx72n_gcc.o` はソースファイル名で、仕様どおり)。
- **取り込み直後の初回ビルドの注意**: 取り込み直後のヘッドレスは先に「クリーンのみのビルド」を走らせ、`make -r clean` が `No rule to make target 'clean'` で失敗して `1 errors` を出す。その後の `all` ビルドは成功し、最終の結果は `0 errors`。2回目以降は出ない(ただし、ここで出た 1 errors は最終結果ではない)。

### ビルド(ジョブランナー経由、`tools/submit-job.sh build '{"mode":"Headless"}' 400`)
| 時刻(JST) | 構成 | 結果 | 備考 |
|---|---|---|---|
| 09:59:39〜10:00:14 | 既定(`APP_WIFI_TEST` = 0、`APP_WIFI_AUTOSTART` = 0、`APP_WX` = 0) | **成功**(exit 0、34秒) | `Build Finished. 0 errors, 2 warnings`。警告は既知の smc_gen の stack usage 非対応2件(`r_bsp_common.c:105` `delay_wait`、`r_rx_intrinsic_functions.c:347` `R_BSP_ChangeToUserMode`)。clean 相当の全再コンパイル(新規の取り込み) |
| 10:00:24〜10:00:48 | 試験(`APP_WIFI_TEST` = 1、`APP_WIFI_AUTOSTART` = 1) | **成功**(exit 0、22秒) | `Build Finished. 0 errors, 0 warnings`。再コンパイルは7ファイル(`clut8_test.c` `lcd_panel.c` `touch_ft5x.c` `app_main.c` `wifi_at.c` `wx_core.c` `wifi_core.c`)。smc_gen は増分で再コンパイルなし |
| 10:00:48 | `src\app\wifi_sw.h` の 2 値を (0) に戻す | 確認済み | `git diff -- src/app/wifi_sw.h` が 0 行(コミットされる値は 0)。`APP_WX` は 0 のまま |

### 成果物
| 項目 | 既定(10:00:11) | 試験(10:00:45、現在の `HardwareDebug` の中身) |
|---|---|---|
| `rx72n_stockview.elf` | 2,357,128 バイト | 2,496,516 バイト |
| `rx72n_stockview.mot` | 3,556,642 バイト | 3,603,354 バイト |
| `rx-elf-size` text / data / bss | 1,182,612 / 2,876 / 18,368 | 1,198,184 / 2,876 / 22,592 |
| ROM(text + data) | 1,185,488 | 1,201,060 |
| 構成の証拠 | `_wifi_test_main` 0 件 | `_wifi_test_main` 1 件(`ffc0b518 T`) |

- 既定の `text / data / bss` は、2026-11-25 の `APP_WX` = 0 の製品版の記録(1,182,612 / 2,876 / 18,368)と一致。
- 証拠の確認は `rx-elf-nm.exe`(`D:\Renesas\GNURX\GCC_for_Renesas_RX 8.3.0.202411-GNURX-ELF\rx-elf\rx-elf\bin\` にあるもの。プロジェクトのツールチェーン 14.2 は `jobs/tools.json` に未登録。シンボルを読むだけのため使用)。既定の ELF のコピーは確認後に削除した。
- RAM2(`0x00800000` 台)の出力セクションは未確認(この手順では `objdump` を実行していない)。

### 秘密情報
- 試験の ELF・MOT には `wifi_secret.h` の SSID・パスワードが平文で入っている想定(設計 §21.5)。値の確認はしていない(`wifi_secret.h` は開いていない)。ビルドログ本文に SSID・パスワードらしい値は見えなかった(目視のみ。grep は未実施)。
- `HardwareDebug\` の中身は試験版(`APP_WIFI_TEST` = 1)のまま。外へ渡さないこと。

### 注意
- ヘッドレスビルドが `.settings/com.renesas.smc.generationsetting.properties`、`.settings/com.renesas.smc.tools.swcomponent.fit.properties` のタイムスタンプ行を書き換えた(内容の変化なし)。`.settings/language.settings.xml` も `git status` で M になった(ビルドが書いたもの。内容の確認は未実施)。
- 書込み・実機操作・COM・gdb・rfp-cli は**未実施**。ランナーは起動したまま。
