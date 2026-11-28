# テスト報告(テスト担当)

作成: 2026-11-19 / 対象: `src/app/`(コーディング担当の成果物)、`docs/design.md` v1 の受入基準 / 状態: **ホスト検証は完了。実機は未実施**

**2026-11-21 追記: 製品デモ v2 P1 の試験結果は末尾の §10。上の §0〜§9 は v1(画像ビューア)と UART ログ v1 の記録。**

## 0. 結論(まず読む)

| 区分 | 結果 |
|---|---|
| ホスト(H1〜H10) | **合格 9 / 一部合格 1(H8: 座標の範囲外チェックが未実装)/ 不合格 0** |
| PC(P1, P2) | P2 **合格**(マップ実測)。P1 は**保留**(私はビルドしていない。成果物は 13:47:52 に生成済み) |
| 静的レビュー(a〜f) | a〜e は問題なし。**f(RAM2)は「未確認」**。実機で最初に確かめる |
| 不具合 | **重大 0 / 誤動作 0 / 軽微 3**。設計役への質問 1、実機で確認してから判断するもの 1(§4) |
| 実機でしか確認できない項目 | 11 + 追加 4。CLIでできるもの / ユーザーの目視・操作が要るもの に分けた(§5) |
| **UARTログ(2026-11-20)** | **ホスト S1〜S12 全部合格(12/12)。重大 0 / 中 0 / 軽微 5。実機未実施。詳細は §8** |
| **4枚目スライド対応(2026-11-20)** | **ホスト(MinGW+Python): 合格 9 / 不合格 1(S5: `img_00..02.c` の CRLF/LF 改行差分のみ) / 未実施 2(S8, S12) + `run.sh` 本体未実施(bash不可)。H10/P2 合格。実機 B4/B4b/UT4/UT6 未実施(実機待ち)。詳細は §9** |

**この報告の「合格」は、ホストPCの GCC 11.4(x86)で実際に実行して確認したものだけ。** RX用GCCでのビルド成功、実機の表示、タッチの動作の証明ではない。

## 1. 再実行の方法

```bash
cd $HOME/mnt/rx72n/rx72n_gcc        # Linux VM 側
bash tests/host/run.sh              # 全部(約20秒)
bash tests/host/run.sh S2 S6        # 指定ステップだけ
```

- 出力の保存: `tests/host/last-run.log`(今回の全文)。ビルド生成物は `/tmp/rx72n-host-build`(プロジェクト外)。
- フラグ: `-std=gnu99 -Wall -Wextra -Wpedantic -Wconversion`。製品コード(標準C部分)は**警告ゼロ**を合格条件にしている。
- 構成: `tests/host/run.sh`(S1〜S7)、`test_nav.c`、`test_touch_decode.c`、`test_images.c`、`test_lcd_panel.c`、`test_img2c.py`、`tests/stubs/platform.h`(仮 platform.h)。

| ステップ | 内容 | 結果 |
|---|---|---|
| S1 | `nav_logic.c`/`touch_decode.c` が標準Cだけ(gnu99 / c99 -pedantic-errors / c11、警告ゼロ、platform.h 非依存、データシンボル無し) | 合格 |
| S2 | `nav_logic` 単体(通常モード 245 検査、次へのみモード 196 検査) | 合格 |
| S3 | `touch_decode` 単体(135 検査) | 合格 |
| S4 | 画像配列(4027 検査)+ `nm` で `const`=R(読み取り専用)+ リンカスクリプト確認 | 合格 |
| S5 | `tools/img2c.py`(Pythonテスト 10 件) | 合格 |
| S6 | `lcd_panel.c` ホスト・ハーネス(135 検査。呼び出し順、cfg の値、実FITのパラメータ検査) | 合格 |
| S7 | ハード依存部 5ファイルの `-fsyntax-only`(実 platform.h 経路 + 仮 platform.h 経路 + `APP_LOG_ENABLE=1`、2026-11-20 から `=0` も) | 合格 |
| S8〜S12 | UARTログ: `app_logfmt` / `app_logq` / `nav_step` / `app_debug`(偽 SCI)/ `app_main`(結合)/ サニタイザ。**§8 を参照** | 合格 |

テストが「効く」ことの確認(変異テスト): `nav_logic.c` に 10 通りの故障を入れた → 9 通りを検出(残り1通りは §4 差し戻し #1 と同じ原因)。`touch_decode.c` 5 通り → 全部検出。`lcd_panel.c` 5 通り(順序入替、HBP=0、offset=961、DE極性反転、PinSet 抜け)→ 全部検出。構文検査も、引数を1つ足した/関数名を1字変えた複製で、ちゃんとエラーになることを確認。

## 2. 受入基準(設計 §14)の結果

### 2.1 H: ホスト

| ID | 方法 | 結果 | 証拠 |
|---|---|---|---|
| H1 | `test_nav.c`: 右半分(x=400)を2標本で +1、1標本目は動かない。境界 x=238→左、239→右(x_max=479)。x_max=480/1/0/4095 でも確認 | 合格 | `test_nav: 245 checks, 0 failed` |
| H2 | 左で -1、0 の左→2、2 の右→0。3回ずつで元に戻る。画像 1/0/2/255 枚の端 | 合格 | 同上 |
| H3 | 1標本だけ → 動かない。触る/離すの交互 100 回 → 動かない | 合格 | 同上 |
| H4 | 500標本(5秒)押しっぱなし → 1回だけ。離して 300ms 後に再度 → また1回 | 合格 | 同上 |
| H5 | 動作から 50/100/200/290/299 ms → 無視、300/301/400/1000 ms → 動作(境界は 299/300) | 合格 | 同上 |
| H6 | 最初から2点、1点→2点→1点残り、動作後に2本目、点数 5/6/255、離し2標本だけでは再動作しない、3標本以上で再動作 | 合格 | 同上 |
| H7 | 動作時刻 `0xFFFFFFF0`, `0xFFFFFFFF`, `0xFFFFFF00`, `0xFFFFFE00`, `0`, `20`, `0x7FFFFFF0` のそれぞれで H5 と同じ結果。乱数入力(30万標本×6回、うち3回は一周をまたぐ)で不変条件(動作は単点2標本連続のみ / 300ms以上 / 間に離し3標本 / 多点を含む押下では動かない / 向きの正しさ)違反 0 | 合格 | 同上 |
| H8 | `test_touch_decode.c`: 既知バイト列 14 通り、タッチ数 0〜15 の全値(0〜5 は true、6〜15 は false)、NULL、上位ニブルのマスク。**座標の「範囲外」で false にする処理は実装に無い**(実装メモ §6-6 で申告済み)ので、その部分は未検証 | **一部合格** | `test_touch_decode: 135 checks, 0 failed`。範囲外は §4 #2 |
| H9 | `test_img2c.py`: 初期化子の数 = 130,560(3枚とも。数が足りないと 0 埋めされて気づけないので数えている)。既知ピクセルの RGB565 値。481x272 / 480x271 / 272x480 / 1x1 / 960x544 / 479x272 / 480x273 は終了コード 2 で、出力ファイルも作らない。RGBA/パレット/グレー入力。`src/app/img_00..02.c` が `--testpattern` の出力とバイト一致 | 合格 | `Ran 10 tests ... OK` |
| H10 | 既存のビルド成果物のマップ `HardwareDebug/rx72n_gcc.map`(2026-11-19 13:47:52 生成、`src/app/*.o` を含む)を解析: 出力セクションに **0x00800000〜0x0087FFFF は無い**(.data/.bss は 0x4〜0x1280、スタックは 0x1380〜0x2780、ROM は 0xFFC00000〜)。リンカスクリプトにも `>RAM2` が無い | 合格 | マップの出力セクション一覧(下表)。**私がビルドしたのではなく、ユーザー側ビルドの成果物を読んだ** |

マップの要点(2026-11-19 13:47:52 のビルド):

| 項目 | 値 |
|---|---|
| `.text` | 0xFFC00000、33,800 バイト |
| `.rodata` | 0xFFC08808、**784,676 バイト**(画像 3 x 261,120 = 783,360 + その他 1,316) |
| `g_img_00` / `g_img_01` / `g_img_02` | 0xFFC08C2C / 0xFFC4882C / 0xFFC8842C(間隔 0x3FC00 = 261,120。すべて ROM) |
| `g_images`(表) | 0xFFC08C20、12 バイト、ROM |
| `.data` + `.bss` | 148 + 4,584 バイト(RAM 0x4〜0x1280)。`g_app_dbg` = 0x00001248、36 バイト |

### 2.2 P: ユーザーPC

| ID | 方法 | 結果 | 証拠 |
|---|---|---|---|
| P1 | ビルドが通ること。**私はRX用GCCを実行できない**。代わりに: (1) 実ヘッダでの `-fsyntax-only`(S7)は合格、(2) `HardwareDebug` の `rx72n_gcc.elf/.map/.mot` が 13:47:52 に更新され(ソースの最終更新 13:31 より後)、マップに `app_main` `lcd_init` `touch_read` `g_img_0x` が入っている → リンクまで通ったと推定できる | **保留**(状況証拠のみ。ビルドログとコンパイラ警告は未確認) | `HardwareDebug/rx72n_gcc.map`。ユーザーが e² studio のビルドログ(Problems/Console)を見て、エラー0を確認してほしい |
| P2 | ROM 使用量 | 合格 | 上の表。画像分 783,360 バイト増、ROM 合計 約 820 KB / 4 MB |

## 3. 静的レビュー(重点項目)

| # | 項目 | 方法 | 結果 | 根拠 |
|---|---|---|---|---|
| a-1 | `s_iic`(`sci_iic_info_t`)は転送完了まで有効か | 読む + ドライバのソース確認 | 問題なし | `touch_ft5x.c`: `static sci_iic_info_t s_iic`。ドライバは `psci_iic_info_ch = p_sci_iic_info` でポインタを保持(`r_sci_iic_rx.c` `sci_iic_master_send_receive`)。`s_slave`/`s_reg`/`s_buf` も static |
| a-2 | スレーブアドレスのシフト | ドライバのソース確認 | 問題なし | ドライバが `(*p_slv_adr) << 1` して R/W ビットを足す(`sci_iic_after_gen_start_cond`)。アプリは 7bit のまま `0x38` を渡している |
| a-3 | 2段読み(レジスタ送信→再スタート→受信) | ソース確認 | 問題なし | `cnt1st > 0` で `SCI_IIC_MODE_SEND_RECEIVE`(`R_SCI_IIC_MasterReceive`)。`cnt1st = 0` なら単純受信(スキャン用) |
| a-4 | 完了待ちの無限ループ | 読む | 問題なし(アプリ側) | `iic_wait` は `TOUCH_WAIT_STEPS`(2000)x10µs で必ず終わる。`R_BSP_SoftwareDelay(10, MICROSECS)` は ICLK 240MHz で有効(オーバーヘッド 2 サイクル、`r_bsp_common.c`)。タイムアウト時は `R_SCI_IIC_Close`→`Open` で復旧 |
| a-5 | 完了状態の読み出し順 | ソース確認 | 問題なし | FINISH/NACK は `set_ch_status` で `dev_sts` に入れた**後**にコールバックを呼ぶ(`sci_iic_advance`)。NACK も `callbackfunc` が呼ばれるので、NACKで20ms待たされることはない |
| a-6 | コールバック必須 | ソース確認 | 問題なし | `callbackfunc == NULL` は `SCI_IIC_ERR_INVALID_ARG`(`r_sci_iic_check_arguments`)。アプリは毎回設定 |
| a-7 | ピン | `r_sci_iic_rx_pin_config.h` | 問題なし | SSCL6 = P33、SSDA6 = P32。回路図(`schematic-findings.md` §2)と一致。ポート設定はドライバが行う(`SCI_IIC_CFG_PORT_SETTING_PROCESSING = 1`) |
| a-8 | 失敗後の状態 | 読む | 問題なし | NACK は次回の `MasterReceive` を妨げない(`check_chstatus_start` が NACK を許可)。`SCI_IIC_ERROR` のときだけ Close/Open |
| a-9 | ドライバ内の待ち | 読む | 注意(情報) | `r_sci_iic_rx.c` は `while (IICSTIF != クリア)` などのハード待ちを持つ(アプリからは打ち切れない)。SDA/SCL が Low に張り付くと `MasterReceive` の中で止まる可能性。**プルアップが無い場合、ドライバは `SCI_IIC_ERR_BUS_BUSY`(=5)を返す**(ドライバ注記)。実機で `err_detail = 5` ならプルアップを疑う |
| b-1 | `glcdc_cfg_t` と `r_glcdc_rx_config.h` の既定値 | `test_lcd_panel.c`: `LCD_CH0_*` マクロと 40 項目以上を比較 | **一致**(下記の意図的差分を除く) | S6 合格 |
| b-2 | `glcdc_cfg_t` と設計 §3/§6 | 同上 | 一致 | HFP17/HBP62/HSYNC25、VFP8/VBP7/VSYNC1、÷24、RGB565 入出力、HSYNC/VSYNC Low-active、DE High-active、TCON: VSYNC=0 / HSYNC=2 / DE=3(`PinSet` の PB4/PB2/PB1 と一致)、GR2 のみ・GR1 は `p_base=NULL`、`MODE=0`。合計 584 x 288、10MHz → **59.46 Hz** |
| b-3 | 実FITのパラメータ検査 | `r_glcdc_private.c` の `r_glcdc_open_param_check()` をホストでそのまま実行 | 通る | 対照: offset=961、base=0x00800020(32バイト境界)、HFP=2、VBP=0、明るさ=1024 は、期待どおりの別々のエラーコードになる(検査が効いている)。base=0x00800040 は通る |
| b-4 | 意図的な差分 | 読む | 軽微・申告済み | 明るさ/コントラスト/ガンマ補正を**無効**にしている(FIT既定は有効)。実装メモ §6-3。色味が既定と少し違う可能性(B2 で目視) |
| b-5 | メモリ制約 | 静的アサート | 問題なし | FB = 261,120 バイト = 画像1枚。base と offset(960)は 64 の倍数。FB は 0x00800000〜0x0083FEFF で RAM2 の範囲内 |
| c | `lcd_init` の順序 | 偽の GPIO/GLCDC/memcpy で呼び出し順を記録(13 イベント) | **設計 §8.2 どおり** | BL消灯→DISP無効→タッチRST=アクティブ(書いてから出力方向)→画像0を 0x00800000 へ 261,120 バイトコピー→`R_GLCDC_PinSet`→`Open`→`START_DISPLAY`→DISP有効→20ms待ち→BL点灯。ピンは P67/P66/PB3(コード 0x0607/0x0606/0x0B03)で回路図と一致。`Open`/`START` が失敗したら BL を点けず `false`、`err_code` に記録 |
| d-1 | 割り込み/タイマ | 読む | 問題なし | CMT 10ms(優先度5)のコールバックは `s_tick++` だけ。`volatile uint32_t`(32bit は RX でアトミック)。I2C 割り込み優先度 2。`BSP_CFG_RUN_IN_USER_MODE = 0`、割り込みは `resetprg.c` が許可。GLCDC 割り込みは使わない(`interrupt.* = false`)。`now * 10` の一周(49.7日)も、符号なし演算で差が合う |
| d-2 | ブロッキング | 読む | 既知(軽微) | `touch_init` 約310ms、`touch_read` 最悪約20ms+復旧(10ms周期を超える。実装メモ §8 で申告済み)。アドレススキャンは10回連続失敗後に1回だけ、応答なしが続くと最悪 約2秒止まる(NACK なら速い) |
| e | 画像配列 | `nm`、`objdump -h`、実マップ | 問題なし | 3枚とも `const uint16_t[130560]` = 261,120 バイト、`nm` で `R`。`g_images` も `-fno-pic` で `R`。**RX の実マップで 0xFFC08C2C から ROM に配置**を確認。初期化子数も 130,560(H9) |
| f | **RAM2 の有効化は必要か** | `r_bsp`・FIT・`docs/ref` を grep、`iodefine.h` を確認 | **未確認** | 下記 |
| g | `src/smc_gen/` の手編集 | ファイルの更新時刻 | 問題なし(推定) | `src/smc_gen/` の全ファイルの更新時刻は 12:46 / 13:11 / 13:24 の3回(SCの生成時刻)のみ。アプリ作成(13:27〜)より後に変わったものは無い。`.git` が無いので差分比較はできない |
| h | `src/rx72n_gcc.c` | 読む | 注意 | `app_main()` を呼ぶ3行を追加済み。SC の再生成で消える可能性(実装メモ §1) |

### f. RAM2 の件(根拠と結論)

| 事実 | 出典 |
|---|---|
| RAM2 には**モジュールストップ・ビットがある**: `MSTP_RAM2 = SYSTEM.MSTPCRC.BIT.MSTPC2`(RAM0 は MSTPC0)。アドレスは `SYSTEM.MSTPCRC` = **0x00080018**(iodefine の構造体から計算)、RAM2 は bit2 | `src/smc_gen/r_bsp/mcu/rx72n/register_access/gnuc/iodefine.h` 634 行 |
| `MSTP_RAM2` / `MSTPC2` を**書き換えるコードは、アプリ・r_bsp・FIT のどこにも無い** | `grep -rn "MSTP_RAM2\|MSTPC2" src` |
| `BSP_CFG_EXPANSION_RAM_ENABLE`(現在 0)は、拡張RAM領域の**セクション初期化**(`dbsct.c`/`reset_program.S` の `*EXRAM*`)を切り替えるもので、ハードの有効化ではない | `r_bsp_config.h`、`dbsct.c`、`reset_program.S` |
| GLCDC のアプリケーションノート(r01an3609)は、フレームバッファの既定を 0x00800000 とするが、RAM2 の有効化操作には触れていない(`RAM2`・`expansion` で検索) | `src/smc_gen/r_glcdc_rx/doc/en/r01an3609ej0162-rx-lcd.pdf` |
| **MSTPCRC.MSTPC2 のリセット後の値は、手元の資料で確認できない**(ユーザーズマニュアルが無い。iodefine にリセット値は無い) | — |

**結論: 未確認。** 多くの RX では RAM のモジュールストップは初期状態で解除されているが、これは記憶に基づく推測で、確認していない。実機で 1 回読めば分かる(§5.1 の R1)。もし停止(1)なら、`lcd_show` の書き込みは無視され、画面は黒/ゴミになる。**参考(未読)**: Renesas「RX72N Group Initial Settings Example」(`https://www.renesas.cn/zh/document/apn/rx72n-group-initial-settings-example-rev101`)に記載があるかもしれない。

## 4. 不具合・差し戻し

重大度: **重大**=実機が動かない / **誤動作**=動くが間違う / **軽微**=実害が小さい / **確認要**=実機の結果次第。

| # | 重大度 | 内容 | 再現・根拠 | 期待 | 該当箇所 | 宛先 |
|---|---|---|---|---|---|---|
| 1 | 軽微 | `NAV_PRESS_CONFIRM` を 1 にしても 1標本では動作しない(2標本必要のまま) | `nav_logic.h` の値を 1 にして、1標本目 false・2標本目 true を実測。IDLE→`PRESS_PENDING` で 1 標本を消費し、確定判定は次の標本からのため | 「暫定値、実機で調整」とあるので、1 にすれば反応が速くなるはず。直すなら IDLE で `count>=CONFIRM` を判定する | `nav_logic.c` `nav_update` の `NAV_ST_IDLE` 分岐 | コーダー。直さないなら `nav_logic.h` のコメントに「2 以上」と書く。テストは `NAV_PRESS_CONFIRM < 2` をコンパイルエラーにして固定済み |
| 2 | 軽微(設計との差) | `touch_decode` は座標の範囲外を false にしない(点数 > 5 だけ)。設計 §14 H8 は「点数>5・範囲外は false」 | 実装メモ §6-6 で申告済み。座標範囲が未確認(B8)のため意図的 | B8 で `TOUCH_X_MAX` と Y の範囲が決まったら範囲検査を入れるか、設計役が H8 の文言を直す | `touch_decode.c` | 設計役 → コーダー(B8 後) |
| 3 | 軽微 | コメントの誤り: `app_debug.c` の「`r_sci_rx` の送信バッファ(80)より小さく」。SC設定は CH2 が 800 | `r_sci_rx_config.h` `SCI_CFG_CH2_TX_BUFSIZ (800)` | コメント修正のみ(64バイト制限自体は安全側で問題なし) | `app_debug.c` `APP_LOG_BUF_BYTES` | コーダー |
| 4 | **確認要**(最悪: 重大) | RAM2 のモジュールストップが解除済みか未確認(§3 f) | §5.1 R1 | 解除済み(MSTPC2 = 0)。停止なら `lcd_init` の先頭で解除が必要(`R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_LPC_CGC_SWR)` → `MSTP_RAM2 = 0` → `Enable`)。**実機で値を読むまで追加しないことを勧める** | `lcd_panel.c` `lcd_init` | デバッガ(読む)→ 停止ならコーダー |
| 5 | 設計への質問 | 300ms の起点が文書で食い違う。設計 §11.1「動作後 300ms 以内の押下確定は無視」/ §14 H5「**離し後** 300ms 未満の再押下は無視」。実装は §11.1(動作から)。長押し(1秒)→ 離す → すぐ再押下 は、実装では動く(H5 の文言なら無視) | `test_nav.c` の記録テスト `test_h5_wording_gap` | どちらかに統一(体感はほぼ変わらない) | `design.md` §11.1 / §14 | 設計役 |

気づいた点(差し戻しではない):

| 内容 | 備考 |
|---|---|
| X が押下確定待ちの間に動いたら、確定時(最新標本)の X で左右を判定する | 設計に明記なし。テストで現状を記録(`test_x_uses_latest_sample`) |
| `TOUCH_X_MAX = 479` は仮値。実座標が 0〜4095 のような大きい範囲なら、ほぼ常に「右」になる | B8 で確認。`nav_init` の `x_max` は `TOUCH_X_MAX` マクロ1か所 |
| `APP_LOG_ENABLE = 1` で `vsnprintf` を使う。BSP のヒープは 0x400(`BSP_CFG_HEAP_BYTES`)で、`vsnprintf` がヒープを使うかは未確認 | ログを有効にして文字が出ないときに疑う。既定は無効 |
| `APP_LOG_SCI_CH` が 1/2 以外はコンパイルエラー(意図どおり。確認済み) | — |
| 回路図メモと設計でバックライトのピン番号が違う(80 と 98)件は、コードがポート名(P67)で書いているため影響なし | 実装メモ §7 のとおり |

## 5. 実機でしか確認できない項目

前提: `docs/cli-debug.md` の `job-runner` が起動済み(ユーザーが `tools\start-runner.cmd` を実行し、e² studio のデバッグ接続は切る)。**CLI は全部「未検証」の仕組み**なので、最初の1回は結果を疑って確認する。

### 5.1 デバッグ担当へ: CLI で確認する項目

共通: `build` → `server-start` → `gdb`(書込み+実行)→ 数秒待つ → `gdb`(変数を読む)→ `server-stop`。変数は **シンボル名で読む**(ビルドで番地が変わる)。`g_app_dbg` の中身(`app_debug.h`):

| フィールド | 意味 |
|---|---|
| `tick` | 10ms ごとに +1(動いていれば増える) |
| `err_code` / `err_detail` | 1=LCD_OPEN(detail は `glcdc_err_t`)、2=LCD_START、3=TOUCH_OPEN(detail は `sci_iic_return_t`)、4=TIMER、5=IIC_TIMEOUT(detail は `dev_sts`)、6=IIC_START(detail: `sci_iic_return_t`、または 0x100+`dev_sts`: **0x103=NACK**、0x105=ERROR) |
| `i2c_fail_total` / `i2c_fail_streak` | I2C 失敗の累計 / 連続 |
| `raw_x` `raw_y` `raw_points` | 最新の生座標と点数 |
| `x_min` `x_max` `y_min` `y_max` | 触れた範囲(初期値 min=0xFFFF, max=0) |
| `image_index` | 現在の画像番号 |
| `found_count` / `found_addr[4]` | アドレススキャンで応答したアドレス |

`glcdc_err_t`: 0=成功、1=PTR、2=LOCK、3=ARG、4=MODE、5=NOT_OPEN、6=TIMING、7=LAYER、8=ALIGNMENT、9=GAMMA、10=UPDATE_TIMING、11=CLUT、12=BLEND。`sci_iic_return_t`: 0=成功、1=LOCK、2=CHAN、3=ARG、4=NO_INIT、5=**BUS_BUSY**(プルアップ無しを疑う)、6=OTHER。

生レジスタ(gdb の `x/wx`。番地は `iodefine.h` から計算したが**実機で読んだことはない**):

| 名前 | 番地 | 見方 |
|---|---|---|
| `SYSTEM.MSTPCRC` | `0x00080018` | bit2 = RAM2 のモジュールストップ(0 = 解除)、bit29 = GLCDC(0 = 解除。`R_GLCDC_Open` が解除する) |
| `GLCDC.BGMON` | `0x000E1018` | bit0 = 表示動作中 |
| `GLCDC.STMON` | `0x000E144C` | bit1 = GR1 アンダーフロー、bit2 = GR2 アンダーフロー(1 なら RAM2 からの読みが間に合っていない) |
| `PORT6.PODR` | `0x0008C026` | bit7 = バックライト(P67)、bit6 = タッチRST(P66) |
| `PORTB.PODR` | `0x0008C02B` | bit3 = DISP(PB3) |
| `PORT6.PDR` / `PORTB.PDR` | `0x0008C006` / `0x0008C00B` | bit が 1 = 出力 |
| RAM2 の先頭 | `0x00800000` | `x/8hx` で `g_img_00` の先頭と比べる |

| ID | 確認内容 | 手順(CLI) | 合格の目安 | 合否 |
|---|---|---|---|---|
| R1(新) | RAM2 が使える(§3 f) | **アプリを動かす前**に `x/wx 0x00080018` を読み、bit2 を見る。続けて `set *(unsigned*)0x00800000 = 0x12345678` → `x/wx 0x00800000` で読み戻す | bit2 = 0、読み戻しが一致 | [ ] |
| R2(新) | 起動して LCD/タッチ初期化が成功 | 書込み+実行、3秒後に `print g_app_dbg` | `err_code = 0`、`tick` が増え続ける(2回読んで差を見る。10秒で約1000) | [ ] |
| R3(新) | フレームバッファに画像0が入った | `x/8hx 0x00800000` と `x/8hx &g_img_00`(または `print/x g_img_00[0]@8`)を比べる。押し続けたときは `image_index` に合う画像と比べる | 一致 | [ ] |
| R4(新) | GLCDC が動作中、アンダーフローなし | `x/wx 0x000E1018`(bit0=1)、`x/wx 0x000E144C`(bit1,2=0) | bit0=1、アンダーフロー 0 | [ ] |
| B9(一部) | バックライト/DISP/RST の出力レベル | `x/bx 0x0008C026`、`x/bx 0x0008C02B`、`x/bx 0x0008C006`、`x/bx 0x0008C00B` | PODR: bit7=1、bit3=1(コードの意図)、PDR の該当 bit=1。**画面が見えるかはユーザー目視(§5.2)と合わせて極性を判定** | [ ] |
| B10 | タッチICのアドレス = 0x38 か | 10秒待って `print g_app_dbg`。動いていれば `i2c_fail_total = 0`、触れば `raw_points` が変わる。失敗が続けば `err_detail` と `found_addr[]` / `found_count` を記録 | 0x38 で応答。違えば `found_addr` の値を `board_pins.h` の `TOUCH_I2C_ADDR` へ(コーダー) | [ ] |
| B8(一部) | 座標の範囲 | ユーザーが四隅を触った**後**に `print g_app_dbg`。`x_min/x_max/y_min/y_max` を記録(初期値のままなら触れていない) | X の範囲が 0〜479 前後か。違えば `TOUCH_X_MAX` の見直し(コーダー) | [ ] |
| B4/B5/B6(一部) | 画像番号の変化 | ユーザーが操作する前後で `image_index` を読む | B4: 右で +1・左で -1。B5: 5秒押し続けて +1 だけ。B6: 2本指で変化なし(`raw_points = 2`) | [ ] |
| B7(一部) | 10分放置 | 開始時と10分後に `print g_app_dbg`、`x/wx 0x000E144C` | `tick` が約 60,000 増える、`i2c_fail_total` が増えない、`err_code` が変わらない、アンダーフロー 0 | [ ] |
| B11(任意) | UARTログ | `app_debug.h` の `APP_LOG_ENABLE=1` で再ビルド(コーダー依頼)→ `serial` ジョブ(COM7, 115200, 15秒)。SW3 の 2ch は OFF のまま | `start` と `img=N` が読める | [ ] |

### 5.2 ユーザーへ: 目視・操作が要る項目

読字の負担を減らすため、1項目=「何を見る/触る」と「どうなれば合格」だけにした。(ユーザーが実施。実施前に、CLI 担当が書込み済みであること)

| ID | 何をする | どうなれば合格 | 合否 |
|---|---|---|---|
| B1 | ボードをリセットして画面を見る | **2秒以内に絵が出る**。絵が出ない/真っ白/真っ暗なら、「バックライトは光っているか」「何かの縞が見えるか」を伝える(極性・順序・タイミングの切り分けに使う) | [ ] |
| B2 | 最初の画像(縦バー)を見る | 左から **赤・緑・青・白・黒** の順。色が違えば「何色に見えるか」を伝える | [ ] |
| B3 | 画面の端を見る | 外周の**白い枠が4辺とも**見える。四隅に小さな四角(左上=マゼンタ/右上=黄/左下=シアン/右下=オレンジ) | [ ] |
| B4 | 画面の**右半分**を1回タッチ → 次に**左半分**を1回タッチ | 右で次の画像、左で前の画像(0.3秒以内)。3枚で一周する | [ ] |
| B5 | 右半分を**5秒間**押し続ける | 切り替わるのは**1回だけ** | [ ] |
| B6 | **2本指**で同時に触る | 切り替わらない | [ ] |
| B7 | 10分放置して、その後に触る | 画像が崩れない、ちらつかない、タッチが効く | [ ] |
| B8 | **四隅を順に触る**(左上→右上→左下→右下)。触り終わったら「触った」と伝える | (CLI担当が座標範囲を読む) | [ ] |
| B9 | 画面が点いているかを伝える(黒/白/絵) | 絵が見える。見えなければ、バックライト・DISP・RST の極性マクロ(`board_pins.h`)をコーダーが反転して再試行 | [ ] |
| (新) B12 | 画像の切り替え時に画面を見る | 一瞬ちらついても許容(1面のため)。気になるなら2面化を検討(設計 §15 #6) | [ ] |
| (新) B13 | 左半分と右半分の境目(画面中央付近)を触る | 中央より左で前、右で次(境目の1画素は誤差) | [ ] |

### 5.3 ホストでは確認できない前提(実機まで残る不確かさ)

| 項目 | 理由 |
|---|---|
| RX用GCC 14.2 でのコンパイル・最適化(`-Og`、`-Wstack-usage=100`) | ホストは GCC 11.4 の x86。実 platform.h は `__attribute__` を空にして通した(属性の検査は含まれない) |
| `r_sci_iic_rx` の実際の波形・タイミング、FT5206 のレジスタ配置(FT5x06系の仮定) | ホストでドライバは動かせない |
| パネルのタイミング(FIT既定 = LVGL ポートと同じ値。データシート未照合) | 値は一致を確認したが、パネルが受け付けるかは実機 |
| 色順・エンディアン(B2) | 設計 §12 のとおり推定 |

## 6. 次の一手(提案)

1. ユーザー: e² studio のビルドログでエラー 0 を確認(P1)。
2. デバッガ担当: R1(RAM2)→ R2 → R3 → R4 の順に CLI で確認。R1 が NG なら、先にコーダーへ戻す(差し戻し #4)。
3. ユーザー: B1〜B3 を目視。見えなければ B9 の情報(バックライトは光る/光らない)を伝える。
4. 設計役: 差し戻し #5(300ms の起点)と #2(範囲外チェック)を決める。

## 7. 実機の判定方法の更新(2026-11-20、デバッグ担当)

- 書込み+走行は `rfp` ジョブ、動作判定は `serial` ジョブ(UART)でできる。手順は `docs/cli-debug.md` §10。結果は `docs/debug-report.md` §0 と §4.1d。
- UART で確認できたこと: 起動(`[init] stage=touch|timer|loop ok=1 err=0 detail=0`)、メインループの周期(`[hb] tick` が毎秒 +100)、`err=0`、`i2c_fail=0`、`drop=0`。
- gdb での変数読み出しは接続で CPU が止まる。走らせ直しは `rfp reset-run`。gdb の continue が GO を出さない問題は未解決(回避済み)。
- 目視が必要なのは、映像の正しさ(B1〜B3)、タッチ切替(B4〜B6)、長時間(B7)。UART では判定できない。

---

## 8. 2026-11-20 UARTログ(テスト担当)

対象: `src/app/app_logfmt.h/.c`、`app_logq.h/.c`、`app_debug.h/.c`、`app_main.c`、`nav_logic.c`(`nav_step`)。基準: **`docs/design-uart.md`**(最優先。実装は 11:30〜11:33 の改訂版)。
確認した版: `app_logfmt.c` 11:33、`app_main.c` 11:30、`app_debug.c` 11:30、`app_logq.c` 11:29(いずれも 2026-11-20、この節を書いた時点で最新)。

### 8.0 結論(まず読む)

| 区分 | 結果 |
|---|---|
| ホスト全体(`bash tests/host/run.sh`) | **S1〜S12 全部合格(12 / 12)**。新規の検査は 11,949 件(S8〜S11)+ サニタイザ再実行(S12) |
| 設計書 §5.4 の U-H1〜U-H11 | **すべて実施・合格**(§8.2) |
| 見つけた問題 | **重大 0 / 中 0 / 軽微 5**(§8.4)。実装が設計書に反している点は **0** |
| 実機でしか確認できない項目 | UT1〜UT9 + 追加 5 項目(§8.5)。**実機・ランナーは今回使っていない**(デバッグ担当が使用中) |

**この節の「合格」は、ホストPC(GCC 11.4 x86)での実行結果だけ。** RX用GCCでのビルド(特に `-Wstack-usage=100`)、割り込み、実際の送信時間、LCD表示は証明していない。

### 8.1 試験の構成(新規)

| ステップ | ファイル | 内容 | 検査数 |
|---|---|---|---|
| S1(拡張) | — | `app_logfmt.c` `app_logq.c` も gnu99 / c99 -pedantic-errors / c11 で警告ゼロ、`platform.h`・FIT 非依存、データシンボル無し | — |
| S7(拡張) | — | `APP_LOG_ENABLE=0`(実 platform.h 経路と仮 platform.h 経路)を追加(U-H11) | — |
| S8 | `test_logfmt.c` | U-H1〜U-H6。全 `applog_fmt_*` の正確な文字列、境界値、`size`=0〜必要長+4 の総当たり(番兵・`buf[0]`)、build、コマンド、周期、設計 §6.4 の正規表現 | 10,879 |
| S9 | `test_logq.c` / `test_nav_step.c` | U-H7(キュー)/ U-H8(`nav_step`)。乱数 2 万回のモデル照合、20,000 標本の差分試験 | 154 / 573 |
| S10 | `test_app_debug.c` + `tests/stubs/fake_sci.h` | `app_debug.c` のログ部を r_sci_rx の偽物で試験(BUSY・満杯・再送・受信・コールバック) | 121 |
| S11 | `test_app_main.c` | `app_main.c` のメインループを偽の SCI/CMT/LCD/タッチで動かす結合試験(M1〜M18) | 222 |
| S12 | — | S2/S3/S8〜S11 を `-fsanitize=address,undefined -fno-sanitize-recover=all` で再実行 | 同上 |

- `fake_sci.h` は `r_sci_rx.c` を読んで合わせた: 送信中の `R_SCI_Send` は `SCI_ERR_XCVR_BUSY`、成功後は `tx_idle=false`、`SCI_ERR_INSUFFICIENT_SPACE`(800B 超)、`R_SCI_Receive` は空なら `SCI_ERR_INSUFFICIENT_DATA`。**割り込み・実時間・TEI は模していない。**
- `test_app_main.c` の「10ms の tick」「送信完了」「受信文字」は、ループ先頭の `app_log_poll()` をマクロで差し替えたフックが与える(`lcd_init`/`touch_init` の間は、実機の TX 割り込みが送り終える想定で `tx_done` を与える)。
- `tests/host/judge_ut1.py`: UT1 の合否を判定する(§8.5)。合成ログ 6 種(正常 1、異常 5)で動作確認済み。実機のログではまだ使っていない。

### 8.2 設計書 §5.4 の項目別結果

| ID | 内容 | 結果 | 証拠(要点) |
|---|---|---|---|
| U-H1 | 各 `applog_fmt_*` が既知の入力で期待どおり(1文字単位、`\r\n` 終わり) | 合格 | `test_logfmt: 10879 checks, 0 failed`。boot / init / hb / st / scan / ev / err / help を各 `size` で総当たり。各行を設計 §6.4 の正規表現(POSIX 版)にも通した。Python の `re.fullmatch` 版も実出力 9 行で 1 行 1 正規表現に一致 |
| U-H2 | 最長: `[hb]` = 144、`[st]` = 188、192 未満 | 合格 | 実測 144 / 188(設計の計算値と一致)。`APPLOG_LINE_MAX`=192 のバッファに最長 `[st]` が入る(余裕 3 バイト) |
| U-H3 | `size` が 0、1、必要長-1、必要長 のときの戻り値・`buf[0]`・境界外 | 合格 | 全行種で `size`=0〜必要長+4。不足は 0 を返し `buf[0]='\0'`、`size`=0 は無書込み、番兵(ヒープ確保+ASan)に変化なし |
| U-H4 | build の空白・制御文字・長い文字列・NULL | 合格 | 1〜255 の全バイトで、0x21〜0x7E 以外は `_`(0x7F、0x80〜0xFF 含む)。31文字ちょうど/32文字/200文字 → 31文字で切る。NULL → `unknown`。実際の `__DATE__ " " __TIME__` で R-BOOT に一致 |
| U-H5 | `n p s ?` 以外の 0〜255 と -1 がすべて NONE | 合格 | -300〜600 の総当たり + `INT_MIN/INT_MAX`。`'n'±256` を誤認しない。大文字・`\r`・`\n`・0x00・0xFF は NONE |
| U-H6 | `applog_due`: 100 未満 0、以上 1、折り返し | 合格 | `last=0xFFFFFFCE` から 99/100 tick。開始値 5 通り × 1000 tick で 10 回・間隔 100。tick が飛んだ場合は 1 本だけ |
| U-H7 | `applog_q`: 8 行、9 行目 false/`drops==1`、pop 後 push、192 以上 false、飽和 | 合格 | `test_logq: 154 checks`。191 は可、192・0・巨大・NULL は false(各 `drops`+1)。`drops` を `0xFFFFFFFD` から進めて `0xFFFFFFFF` で止まる。乱数 2 万操作を単純な配列と照合 |
| U-H8 | `nav_step`: 循環、1枚/NULL/dir=0 で false、`nav_update` の挙動を変えない | 合格 | `test_nav_step: 573 checks`。0→1→2→0 / 0→2→1→0、255枚。`nav_step` の前後で `index` 以外のフィールドが不変(IDLE/PRESS_PENDING/HELD/RELEASE_PENDING)。+1 と -1 を毎回挟んだ 20,000 標本の差分で `nav_update` の戻り値・全フィールドが同一(時刻の折り返しも通過)。300ms 規則は `nav_step` に効かず、タッチ側の 300ms 判定も `nav_step` の影響を受けない |
| U-H9 | 既存 H1〜H7 が合格 | 合格 | `test_nav: 245`、`test_nav_next: 196`、`test_touch_decode: 135`(変更前と同数) |
| U-H10 | S1 の条件(3規格、警告ゼロ、`platform.h` 非依存) | 合格 | S1 に `app_logfmt.c` `app_logq.c` を追加。`-Wall -Wextra -Wpedantic -Wconversion` で警告ゼロ、データシンボル無し |
| U-H11 | `APP_LOG_ENABLE=0/1`、`APP_LOG_SCI_CH=1/2` の `-fsyntax-only` | 合格 | S7 に `APP_LOG_ENABLE=0` を追加(実 platform.h / 仮 platform.h)。警告なし。`SCI_CH=3` は `#error` |

### 8.3 `app_debug.c` と `app_main.c` の試験結果(依頼 2 と、設計 §2.5・§3・§4.2・§2.4)

| 観点 | 結果 | 確認した内容 |
|---|---|---|
| 初期化 | 合格 | `R_SCI_PinSet_SCI2` が `R_SCI_Open` より前。ch=2、ASYNC、115200、8N1、**優先度 2**、コールバックあり。2 回目の `app_log_init` は何もしない。`init` 前の `app_log_line` は自動で init |
| `R_SCI_Open` 失敗 | 合格 | 無音で続行、`drop` は数えない、`Open` を繰り返さない。`app_main` 全体でも、LCD/タッチ/タイマは動き続け、タッチで画像が切り替わる(M16) |
| 送信中(BUSY)の保持 | 合格 | 先頭の行を送信後、`tx_idle=false` の間に 8 行まで保持。BUSY の間に `poll` を 1000 回呼んでも行は減らず、`drop` も増えない。中身は呼び出し側のバッファを壊しても不変 |
| 満杯での破棄 | 合格 | 9 行目は捨てる(古い行は残る)。`drop`+1。送信完了のたびに先頭から順に再送され、内容・順序が一致 |
| BUSY 以外の送信エラー | 合格 | その行を捨てて `drop`+1、次の行へ進む。BUSY は数えない |
| 長さ | 合格 | 191 バイトはそのまま通る。192 以上・0・NULL は `drop`+1 で送らない |
| 受信 | 合格 | 0x00〜0xFF が非負で返る(0xFF が -1 にならない)。空は -1 |
| コールバック | 合格 | `SCI_EVT_RXBUF_OVFL`(2)`FRAMING_ERR`(3)`PARITY_ERR`(4)`OVFL_ERR`(8)だけ記録。他のイベント・NULL は無視。読むと既読。2 回続けて起きたら最新値 1 回。seq が 32bit 一周しても検出。**列挙子の数値が設計 §2.4 の 2/3/4/8 と一致することを `r_sci_rx_if.h` で確認** |
| 起動順序(M1) | 合格 | `[boot]` → `lcd_init`(この時点で `[boot]` だけが送信済み)→ `[init] lcd` → `touch_init`(boot+lcd のみ送信済み)→ `[init] touch` → `[init] timer` → `[init] loop` → 最初の `[hb] tick=100`。build に空白なし |
| `[hb]`(M2, M3) | 合格 | 1000 tick で 10 本、`tick`=100,200,…、`drop=0`。**その tick の `touch_read` より前に積まれる**(`touch_read` 時点の積み済み行数が tick 99→100 で増える)。`touch_init` 失敗・`touch_read` 常時失敗でも 10 本(M4, M5) |
| コマンド(M6〜M8) | 合格 | `nnn` で 1→2→0、`pppp` で 2→1→0→2。LCD に渡る画像も同じ番号。`N P S \r \n` 空白 0x00 0xFF `x` `0` 0x80 は出力なし・LCD 変化なし。`s` → `[st]`(+`found>0` なら直後に `[scan] n=2 a0=0x38 a1=0x1a`)。`?` → `[help]` |
| 連打(M9, UT6 相当) | 合格 | `n` を 60 個同時に受信 → `[ev]` ちょうど 60 本、`drop=0`、`lcd_show` は 1 tick に最大 1 回、`[hb]` 10 本、最後の `img=0`(60 mod 3) |
| 遅い回線(M10) | 合格 | 1 行 5 tick かかる回線で 60 連打 → 満杯で新しい行を捨て、`drop` が単調増加して `[hb]` に載る。途中で切れた行は無い。保存則(積んだ = 送った + 破棄 + 残り)が成立 |
| タッチ(M11) | 合格 | UART で 1 にした後、右半分のタッチで 2(`src=touch`)。**`nav` の番号が UART と同期**。5 秒の長押しは追加 1 回のみ(次の押下)、2 本指は無し。`[st]` に触れた範囲が載る |
| `[err]` (A)(M12) | 合格 | 変化で 1 行、同じ値は出さない、10 tick に 1 行まで(最新値) |
| `[err]` (B)(M13) | 合格 | `code=7 detail=<event>`、1 秒に 1 行(間の事象は保留して最新値)。`g_app_dbg.err_code` に書かない |
| タイマ失敗(M14) | 合格 | `[init] stage=timer ok=0 err=4 detail=0` と `[err] code=4 detail=0` を出し切って停止。`[hb]` も `[init] loop` も出ない。送信キューが空になる |
| LCD 初期化失敗(M15) | 合格 | `[init] stage=lcd ok=0 err=1 detail=3` を出して続行。`[hb]` は動く。`[err] code=1 detail=3` も 1 本(設計 §1.3 の「重複は仕様」) |
| tick の折り返し・飛び(M17, M18) | 合格 | `s_tick` が `0xFFFFFF9C` から始まり 32bit を実際に折り返しても、`[hb]` の間隔はすべて 100。250 tick 飛んでも 1 本だけ |

### 8.4 見つけた問題(重大度つき)

**重大 0 / 中 0 / 軽微 5。** 設計書に反する実装は見つからなかった(§8.6)。

| # | 重大度 | 内容 | 再現・根拠 | 該当 | 提案 |
|---|---|---|---|---|---|
| T1 | 軽微 | `app_log_rx_event` の `s_rx_ev_seq` は `seq = seq + 1` を ERI(優先度3)と RXI(優先度2)の両方のコールバックが書く。RXI のコールバック中に ERI が割り込むと更新が 1 回失われる可能性がある。また主側は `seq` を読んでから `val` を読むため、その間に割り込みが入ると同じ値を 2 回返しうる | コード読み。`SCI_EVT_RXBUF_OVFL` は `sci_receive()`(RXI)、`FRAMING/PARITY/OVFL_ERR` は `sci_error()`(ERI グループ)から呼ばれる(`r_sci_rx.c` 2008・2766〜2777 行付近)。ホストでは再現できない | `app_debug.c` `log_cb` / `app_log_rx_event` | 影響は「`[err] code=7` が 1 回出ない/重複する」だけで、`drop` や画像は壊れない。直すなら ERI 優先度を RXI と同じにするか、割り込み禁止区間で読む。今のままでも可 |
| T2 | 軽微 | `APP_LOG_ENABLE=0` でも `app_main.c` は毎秒 `[hb]` などを整形し、`app_log_line` が空マクロで捨てる(整形のコスト・ログ用 static バッファ約 0.2KB が残る)。動作は正しい | `S7 A log0` は警告なし。コード読み | `app_main.c` `emit` | 無効時に整形も省くなら `#if APP_LOG_ENABLE` で囲む。必須ではない |
| T3 | 軽微 | `build=""`(空文字)だと `build= ch=2 ...` になり、R-BOOT(`build=\S+`)に一致しない | `test_logfmt` U-H4(参考出力)。実際の呼び出しは `__DATE__ " " __TIME__`(常に 20 文字)なので起きない | `applog_fmt_boot` | 空のとき `unknown` にすると設計の正規表現と常に整合する(設計役の判断) |
| T4 | 軽微 | **PC 側の運用上の制約(コードの不具合ではない)**: `[boot]` と `[init] lcd` は、PC が COM7 を開く前に出ていると受信できない。実機ログでも `[init] stage=touch/timer/loop` から見えていた(`docs/debug-report.md` §4.1d の `reset-run` 直後の `serial`) | デバッグ担当の実測(私は実機未使用)。設計 §7 の U10 と同じ | UT2 | UT2 は「ジョブ開始後 5 秒以内にリセット」を守る。ランナー側で先にポートを開いてからリセットする方法は設計 §9(ソフトリセット)の範囲 |
| T5 | 軽微(未確認) | RX 用 GCC での `-Wstack-usage=100`。コーダーは整形を 3 引数ずつに分割し、バッファを `static` にして対策したが、**RX での実測は未実施**。ホスト(x86 `-Os`)の最大フレームは `log_state` / `apply_image` の 32 バイト。`snprintf`(newlib)自体のスタック使用量も RX では未測定(ユーザースタック 0x1000、割り込みスタック 0x400) | `gcc -fstack-usage` の結果(ホスト)。RX は未ビルド | ビルド担当 | 再ビルドでの警告確認と、実機で `[hb]` が出ること(UT1)で確認 |

調べて**問題なし**だった点(依頼 4 の項目に対応):

| 観点 | 結果 |
|---|---|
| バッファ超過 | なし。`size`=0〜必要長+4 の総当たりと ASan で境界外書込みなし。キューは `len < 192` を検査して `memcpy`。`R_SCI_Send` に渡す長さは `(uint16_t)n`(n ≤ 191) |
| 符号付き未定義動作 | なし。UBSan(`-fno-sanitize-recover=all`)で S8〜S11 とも検出なし。`snprintf` の戻り値は `n<0` を先に弾いてから `size_t` へ |
| `nav.index` が範囲外 | ならない。`nav_step` は `nav_move`(`image_count` で剰余)だけが書く。`app_main.c` に `nav->index` の直接書込みは無い(`grep` と M11 で確認)。UART 経由の番号が `lcd_show(g_images[idx])` の添字に使われるが、`idx < APP_IMAGE_COUNT` |
| `n/p` が `lcd_show` 失敗時に不整合 | `lcd_show` は `void` で失敗を返さない(NULL 画像のときだけ何もしない)。`lcd_init` 失敗でも `[ev]` は出る(画面が出ていなくても番号は進む)。**UART の `[ev]` は「コピーが終わった」までで「画面に出た」ではない**(設計 §0 #10 のとおり。B1〜B3 は目視) |
| 割り込みとメインの競合 | キューと整形用バッファはメインループ専用(ISR から触らない)。ISR が書くのは `s_rx_ev_val/seq`(T1)と `s_tick`(書くのは ISR だけの 32bit)。`g_app_dbg` の複数フィールドは主側が書く |
| BUSY の再試行 | 合格(§8.3)。BUSY の間は pop しない、数えない、待たない |
| `drop` の累計 | 合格。整形失敗・満杯・長さ不正・BUSY 以外のエラーで +1、飽和、リセットで 0 |
| 初期化中(約 310ms)の起動バナー | **失われない**。`[boot]` は `app_log_line` の中ですぐ `R_SCI_Send` され(約 4ms 分)、`lcd_init`/`touch_init` の間は TX 割り込みが送る。`[init]` 各行も各初期化の直後に送る。**ただしこれは偽物 SCI での論理確認**。実機で PC が間に合うかは T4 |
| `[hb]` が touch 処理を遅らせる | 順序は設計どおり(touch 前)。整形 7 回の `snprintf` + `R_SCI_Send`(最大 144 バイトのコピー)が毎秒 1 回入るだけ。所要時間は**未測定**だが、`touch_read` 最大約 20ms より十分小さいはず。実機の `tick` 差(UT1)で確認 |
| 1 tick に 1 文字 | 合格(M9)。ただし `lcd_show`(261,120 バイトのコピー)の所要時間は未測定(設計 U7)。60 連打で `tick` 差が 130 を超えないか UT6 で見る |
| 割り込み優先度 | `int_priority = 2`(設計 §4.4)を `Open` の引数で確認。SC の生成物は未変更(`src/smc_gen` は変更されていない) |
| 旧 API の残り | `app_log_printf` `APP_LOG(` `applog_step_index` `applog_hb_due` `applog_fmt_img` は `src/` `tools/` に残っていない(`grep`) |

### 8.5 実機で確認する項目(設計 §6.5 の UT1〜UT9 + 追加)

前提(設計 §6.2): ECN1 と CN8 の両方の USB を接続、SW3 の 2ch は OFF、ランナー起動、COM7 を他のソフトが開いていない、書込み後にリセット、**gdb ジョブと同時に使わない**。判定は 1 行ずつ(`\r?\n` で分割、BOM を除く)。**最初の行と最後の行は途中切れの可能性があるので判定から外す**。`[` で始まらない非空行は「異物」(文字化け)。

**正規表現(Python `re.fullmatch`。設計 §6.4。実装の出力 9 行で 1 行 1 正規表現に一致することを確認済み)**

```
R-BOOT : \[boot\] fw=rx72n_gcc build=\S+ ch=2 baud=115200
R-INIT : \[init\] stage=(lcd|touch|timer|loop) ok=([01]) err=(\d+) detail=(\d+)
R-HB   : \[hb\] tick=(\d+) img=(\d+) err=(\d+) detail=(\d+) pts=(\d+) x=(\d+) y=(\d+) i2c_fail=(\d+) streak=(\d+) found=(\d+) drop=(\d+)
R-ST   : \[st\] tick=(\d+) img=(\d+) err=(\d+) detail=(\d+) pts=(\d+) x=(\d+) y=(\d+) i2c_fail=(\d+) streak=(\d+) found=(\d+) drop=(\d+) xmin=(\d+) xmax=(\d+) ymin=(\d+) ymax=(\d+)
R-EV   : \[ev\] img=(\d+) src=(touch|uart)
R-ERR  : \[err\] code=(\d+) detail=(\d+)
R-HELP : \[help\] n=next p=prev s=state \?=help
R-SCAN : \[scan\] n=(\d+)( a\d=0x[0-9a-f]{2})*
```

**UT1(受信のみ)。コマンド: `tools/submit-job.sh serial '{"port":"COM7","baud":115200,"seconds":15}' 40`**

| 判定 | 合格 | 警告 | 不合格 |
|---|---|---|---|
| (1) R-HB の本数 | 13〜16 本 | — | それ以外(0 本なら配線・ボーレート・COM7・優先度 U1〜U4 を疑う) |
| (2) 連続する R-HB の `tick` 差 | すべて 100〜110 | 111〜130、または 130 超でも直前で `i2c_fail` が 10 以上増えた(`iic_scan` の停止) | 130 超(上の例外を除く) |
| (3) 異物の行 | 0 | — | 1 行以上(文字化け: ボーレート誤差 U3) |
| (4) `drop` | すべて 0 | — | 0 以外 |
| (5) `err` `streak` `i2c_fail` | `err=0`、`streak=0`、`i2c_fail` が全 R-HB で同じ | — | `err≠0` または `i2c_fail` 増加 |

判定の自動化: `python3 -I tests/host/judge_ut1.py <ジョブのログ>`(上の表のとおり。終了コード 0 = 合格)。合成ログで動作確認済み(正常 1 + `drop`≠0 / `tick` 飛び / 異物 / 本数不足 / `err`≠0 の 5 つを不合格と判定)。

| ID | 目的 | ジョブ(`serial` の JSON) | 人の操作 | 合格条件(要点。詳細は設計 §6.5) | 合否 |
|---|---|---|---|---|---|
| UT1 | 配線・ボーレート・ループ・I2C 健全性 | `{"port":"COM7","baud":115200,"seconds":15}` | なし | 上の表 | [ ] |
| UT2 | 起動順序 | `seconds` 20 | **ジョブ開始から 5 秒以内にリセット** | R-BOOT → R-INIT lcd(`ok=1 err=0 detail=0`)→ touch → timer → loop(各 `ok=1`)→ 最初の R-HB(`tick` 100〜112)。各 1 回 | [ ] |
| UT3 | `s` と `?` | `"tx":"s?","txAtSec":3,"txGapMs":300`、`seconds` 12 | なし | R-ST 1 本、R-HELP 1 本(1 秒以内)。`found>0` なら R-SCAN が R-ST の直後に 1 本 | [ ] |
| UT4 | `n/p` の遠隔切替 | `"tx":"nnnp","txAtSec":3,"txGapMs":400`、`seconds` 12 | **LCD を見て、絵が切り替わったか伝える** | R-EV が 4 本、すべて `src=uart`、`img` が `(b+1)%3` `(b+2)%3` `b` `(b+2)%3`。直後の R-HB の `img` が一致 | [ ] |
| UT5 | 未知の文字を無視 | `"tx":"xxxx","txAtSec":3,"txGapMs":100`、`seconds` 10 | なし | R-EV / R-ST / R-HELP / R-ERR が 0 本。`img` と `drop` は不変 | [ ] |
| UT6 | 60 連打 | `"tx":"n"×60,"txAtSec":3,"txGapMs":0`、`seconds` 14 | なし | R-EV がちょうど 60 本、最後の `img`=`b`、`code=7` が 0 本、`drop` すべて 0、R-HB の `tick` 差 ≦ 130、最初〜最後の R-EV が約 0.6 秒以上 | [ ] |
| UT7 | タッチ | `seconds` 25 | **右半分を 1 回タップ → 2 秒待つ → 左半分を 1 回タップ → 2 秒待つ → 右半分を 3 秒押し続ける → 離す → 2 本指で触る** | R-EV が `src=touch` で `(b+1)%3` → `b` → `(b+1)%3`、押し続けで追加なし、2 本指で無し。押している間の R-HB に `pts=1` と `x` `y` の変化 | [ ] |
| UT8 | 座標範囲(B8) | `"tx":"s","txAtSec":32`、`seconds` 40 | 開始後 30 秒以内に**四隅を順に 1 回ずつ**触る | R-ST の `xmin xmax ymin ymax` を記録 | [ ] |
| UT9 | 長時間(B7 の一部) | `seconds` 600(待ち 625) | なし | R-HB が 598〜602 本、`tick` 単調増加、`drop`・`i2c_fail` 不変、R-ERR なし | [ ] |

ホストで確認できなかったため、実機で追加確認するもの:

| ID | 確認内容 | 方法 | 合格の目安 |
|---|---|---|---|
| UX1 | **RX 用 GCC のビルドで `-Wstack-usage=100` の警告が出ない**(T5) | ビルド担当の再ビルドのログ | 警告なし。出たら関数名を報告 |
| UX2 | 優先度 2 の UART と I2C(優先度 2)が干渉せず、タッチの反応が遅くならない(設計 U-B7) | UART 付きで B4〜B6(目視)。UT7 の `[ev]` 数と一致 | 切替がもたつかない、取りこぼしなし |
| UX3 | `lcd_show` の所要時間(設計 U7)。連打で `[hb]` とタッチが遅れないか | UT6 の `tick` 差。最初〜最後の `[ev]` の所要 | `tick` 差 ≦ 130、所要 ≧ 約 0.6 秒 |
| UX4 | `[hb]` の整形が touch を遅らせないか(最大 144 バイトの整形・コピー) | UT1 の `tick` 差が 100〜110 に収まる | 収まる |
| UX5 | UART RX の取りこぼし(オーバーラン): 87µs 間隔で受信しても `SCI_EVT_OVFL_ERR` が出ないか(設計 §4.4) | UT6(`txGapMs`=0 で一括送信)。R-ERR `code=7` が 0 本 | 0 本 |

**LCD の絵が実際に切り替わったか、色・欠け・ちらつき(B1〜B3、B12)は UART では判定できない。ユーザーの目視が要る**(UT4 の「LCD を見て」)。

### 8.6 設計書(`docs/design-uart.md`)との照合

| 設計の項目 | 実装 | 結果 |
|---|---|---|
| §2.2 行の種類 8 種と書式 | 一致(実出力を §6.4 の正規表現で検査) | 一致 |
| §2.1 共通ルール(ASCII・`\r\n`・192・部分行を出さない) | 全行で ASCII + 末尾 `\r\n` のみを検査。小バッファは 0 を返し `buf[0]='\0'` | 一致 |
| §2.2 `[boot]` の build(空白→`_`、31 文字、NULL→unknown) | 一致 | 一致 |
| §2.4 `[err]` (A)(B)(C) と頻度上限 | 一致(M12, M13, M14)。(A) の最初の 1 行は時間制限なし(実装メモに記載の判断。設計の「100ms に 1 行まで」と矛盾しない) | 一致 |
| §2.5 あふれ・失敗の扱いと `drop` | 一致(D5〜D7、M10) | 一致 |
| §3.1 コマンド | 一致。1 枚だけのとき `n/p` が何も変えず `[ev]` も出さない点は `nav_step` が false を返すことで確認(アプリ全体では 3 枚のみ) | 一致 |
| §3.2 受信の処理(コールバックは値を書くだけ、1 tick 1 文字) | 一致(D10、M9) | 一致 |
| §3.3 `nav_step`/`apply_image`、間隔規則を適用しない、直接書込み禁止 | 一致(U-H8、M6、M11) | 一致 |
| §4.2 メインループの順序(poll → tick → hb → err → 受信 → touch) | 一致(M3 は hb が touch より前、M9 は `lcd_show` が 1 tick に 1 回まで) | 一致 |
| §4.2 タイマ失敗の停止ループで `app_log_poll()` | 一致(M14。変異試験でこの行を消すとテストが無限ループを検出) | 一致 |
| §4.4 優先度 2 | 一致(D1) | 一致 |
| §5.1〜5.3 関数名・署名 | 一致(`applog_due`、`applog_fmt_ev` など。旧名は残っていない) | 一致 |
| §8 #3 `app_log_printf`/`APP_LOG` 廃止、`APP_ERR_UART_RX`、`app_log_drops`/`app_log_rx_event`、`APP_LOG_ENABLE=0` の空関数 | 一致 | 一致 |
| §8 #6 S1/S7 の対象拡張 | **テスト担当が実施済み**(S1 に `app_logfmt.c` `app_logq.c`、S7 に `APP_LOG_ENABLE=0`) | 完了 |

設計書の曖昧な点(設計役への確認):

| # | 内容 | 現状 |
|---|---|---|
| Q1 | `[boot]` の `build` が空文字のとき `unknown` にするか(T3)。設計は NULL のみ `unknown` | 空文字は `build=`(値なし) |
| Q2 | 設計 §6.5 UT6 の「最初の `[ev]` から最後の `[ev]` までが約 0.6 秒以上」は、1 tick 1 文字(10ms × 60)から来る下限。実機では `lcd_show` の時間も乗るので、上限も決めるか | 下限のみ |

### 8.7 試験がちゃんと効くことの確認(変異テスト)

製品コードのコピー(`/tmp`、プロジェクト外)に 25 通りの故障を 1 つずつ入れて、該当する S8〜S11 を実行した。**24 通りを検出、1 通りは等価変異**(`nav_move` の `dir > 0` を `dir >= 0` にしても、`dir==0` は `nav_step` が先に弾くため挙動が同じ)。

| 区分 | 入れた故障 → 検出したステップ |
|---|---|
| `app_logfmt.c` | `fin` の `>=`→`>`(S8)/ build 31→32(S8)/ `applog_due` の `>=`→`>`(S8, S11)/ `s`→PREV(S8, S11)/ `?` 削除(S8, S11)/ scan の丸め削除(S8)/ `[hb]` 末尾に空白(S8, S11) |
| `app_logq.c` | 満杯判定 `>=`→`>`(S9, S10)/ 長さ上限 `>=`→`>`(S9, S10)/ `drops` の飽和なし(S9, S10)/ 空 pop で減算(S9) |
| `app_debug.c` | BUSY でも pop(S10, S11)/ 送信エラーを数えない(S10)/ 優先度 3(S10)/ `getc` の符号拡張(S10)/ RX_CHAR でも記録(S10) |
| `app_main.c` | `[hb]` 出力を削除(S11)/ 受信を毎周回(S11)/ タイマ失敗ループの `poll` 削除(S11:無限ループを 60 秒で打ち切り)/ `image_index` 更新を削除(S11)/ (A) の間引きなし(S11)/ (B) を `err_code` に書く(S11)/ `ok=1` でも `err` を出す(S11) |
| `nav_logic.c` | `nav_step` が状態機械を触る(S9) |

### 8.8 再実行

```bash
cd $HOME/mnt/rx72n/rx72n_gcc
bash tests/host/run.sh               # 全部(S1〜S12。この環境では約 2 分)
bash tests/host/run.sh S8 S9 S10 S11 # UARTログだけ
```

- 出力の保存: `tests/host/last-run.log`(今回の全文: `集計: PASS=12 FAIL=0`)。
- `S10`/`S11` は実 platform.h + FIT 実ヘッダ + `tests/stubs/fake_sci.h`。`S12` はサニタイザ付き(`-D_FORTIFY_SOURCE=0 -D__NO_INLINE__` が必要: `__attribute__` を空にする定義が glibc の fortify と衝突するため)。

---

## 9. 2026-11-20 4枚目スライド(`img_03`, `APP_IMAGE_COUNT=4`)対応(テスト担当)

対象: `src/app/img_03.c`、`src/app/images.h/.c`、`tools/prepare_slide.py`、`tools/assets/slide_03_480x272.png`、最新ビルド成果物(`HardwareDebug/rx72n_gcc.map`, `docs/build-and-debug.md` 22:53 JST 記録)。

### 9.0 結論(まず読む)

| 区分 | 結果 | 要点 |
|---|---|---|
| ホストテスト(MinGW GCC 13.2.0 + Python 3.14.7) | **合格 9 / 不合格 1 / 未実施 2** | S1, S2, S3, S4, S6, S7, S9, S10, S11 合格。S5 不合格(`img_00..02.c` が CRLF で LF と改行のみ不一致、`test_prepare_slide.py` は 6/6 合格)。S8/S12 および `bash tests/host/run.sh` 本体は未実施 |
| 静的レビュー / マップ確認 (H10, P2) | **合格** | `g_img_03` (`0xffccd6e4`, 261,120B)・`g_images` (`0xffc0e2d4`, 16B) は `.rodata`(ROM)。RAM2 配置 **0**。ROM 使用量 1.054 MB / 4 MB。`src/smc_gen/` 未変更 |
| 実機確認 (`B4`, `B4b`, `UT4`, `UT6`, `UT7`) | **未実施(実機待ち)** | §9.4 のチェックリストを作成済み |
| 製品コード (`src/`) への変更 | **なし** | 不具合・差分は報告のみ(S5 の CRLF 差分は §9.3 参照) |

### 9.1 ホストテスト更新内容(`tests/host/`)

| ファイル | 更新内容 |
|---|---|
| `tests/host/run.sh` | S4 / S6 / S11 / S12 のリンク対象に `$APP/img_03.c` を追加。S5 に `test_prepare_slide.py` を追加。改行コードを **LF** で維持 |
| `tests/host/test_images.c` | `APP_IMAGE_COUNT == 4`、`g_img_03` サイズ(261,120B / 130,560 画素)・4バイトアライン、`g_images[3] == g_img_03`、`g_img_03` の左右黒帯(`x=0..127`, `x=352..479` が `0x0000`)・中央非黒画素、4枚相互非一致を検査(4,037 checks) |
| `tests/host/test_app_main.c` | M6 を 4枚循環(`0→1→2→3→0`, `0→3→2→1→0`)へ更新、M9/M10 を `60 mod 4 = 0` に更新、M11 にタッチ 4枚循環期待値(`img=3` および `0→3→2→1→0→1→2→3→0`)を追加(239 checks) |
| `tests/host/test_nav_step.c` | `NS1b` 4枚循環(`0→1→2→3→0`, `0→3→2→1→0`)を追加(589 checks) |
| `tests/host/test_prepare_slide.py`(新規) | `251x305` RGBA → `480x272` RGB(中央配置・左右 128px 黒帯・完全透明/半透明合成)、`--bg "#FFFFFF"`、横長画像上下黒帯、不正 `--bg` 拒否、実アセット一致(`slide_03_src.png` → `slide_03_480x272.png` → `img_03.c`)、`img2c.py` が `251x305` を終了コード 2 で拒否維持を確認(6 tests) |

### 9.2 ホストテスト実行結果(`tests/host/last-run.log`)

環境制約: Windows サンドボックス制約により WSL `bash.exe` (`Bash/E_ACCESSDENIED`) および MSYS2/Git `bash.exe` (`NtCreateDirectoryObject 0xC0000022`) が起動不可のため、`bash tests/host/run.sh` 本体は**未実施**。代わりに Windows 上の MinGW GCC 13.2.0 (`x86_64-w64-mingw32`) と Python 3.14.7 で各ステップを個別実行した。

| ステップ | 結果 | 検査数 / 内容 | 備考 |
|---|---|---|---|
| `bash tests/host/run.sh` | **未実施** | — | WSL/MSYS2 bash 起動不可(Linux/WSL 環境での実行待ち) |
| S1 (標準C静的条件) | **合格** | `nav_logic`, `touch_decode`, `app_logfmt`, `app_logq` | gnu99 / c99 -pedantic-errors / c11 警告ゼロ、`nm` データシンボルなし |
| S2 (`test_nav`) | **合格** | 245 + 196 checks | 通常モード・次へのみモード |
| S3 (`test_touch_decode`) | **合格** | 135 checks | H8 既存範囲 |
| S4 (`test_images` + `nm`/`objdump`/ld) | **合格** | **4,037 checks** | `g_img_00..03` / `g_images` が `R`(.rdata)、各 261,120B、左右黒帯 `0x0000`、4枚相違、`linker_script.ld` に `RAM2` 出力なし |
| S5 (`test_img2c.py` + `test_prepare_slide.py`) | **不合格** | `test_img2c`: **9/10**、`test_prepare_slide`: **6/6** | `test_prepare_slide.py` は全合格(`251x305` RGBA変換、`img_03.c` 一致、`img2c.py` 終了コード2維持)。`test_img2c.py` の `test_committed_files_match_generator` のみ失敗(§9.3 #1) |
| S6 (`test_lcd_panel` + 実 `r_glcdc_private.c`) | **合格** | 135 checks | 注: MinGW PE/COFF で `--gc-sections` が未参照関数を落とさないため、未使用 4関数のダミースタブと `-D__NO_INLINE__ -Wl,--allow-multiple-definition` を補って実行 |
| S7 (`-fsyntax-only` A/B 経路) | **合格** | 5ファイル × 7構成 + `#error` | `images.c` を含む 5ファイルで実 `platform.h` / 仮 `platform.h` とも警告ゼロ |
| S8 (`test_logfmt`) | **未実施** | — | MinGW に POSIX `<regex.h>` が無くコンパイル不可(Linux/WSL 待ち) |
| S9 (`test_logq` + `test_nav_step`) | **合格** | 154 + **589 checks** | `NS1b` 4枚循環を含む |
| S10 (`test_app_debug`) | **合格** | 121 checks | 注: MinGW ヘッダ `gnu_inline` 多重定義回避のため `-D__NO_INLINE__ -Wl,--allow-multiple-definition` を付与 |
| S11 (`test_app_main`) | **合格** | **239 checks** | 4枚循環(M6/M9/M10/M11)を含む。注記は S10 と同じ |
| S12 (`-fsanitize=address,undefined`) | **未実施** | — | MinGW に `libasan` / `libubsan` が無く実行不可(Linux/WSL 待ち) |

### 9.3 静的レビュー & マップ確認 (H10, P2)

1. **マップ確認 (`HardwareDebug/rx72n_gcc.map`, 2026-11-20 22:53:18 JST ビルド)**:
   - `g_images`: `.rodata.g_images` `0xffc0e2d4` (`0x10` = 16 バイト、ROM、4バイト境界)
   - `g_img_00`: `.rodata.g_img_00` `0xffc0e2e4` (`0x3fc00` = 261,120 バイト、ROM、4バイト境界)
   - `g_img_01`: `.rodata.g_img_01` `0xffc4dee4` (`0x3fc00` = 261,120 バイト、ROM、4バイト境界)
   - `g_img_02`: `.rodata.g_img_02` `0xffc8dae4` (`0x3fc00` = 261,120 バイト、ROM、4バイト境界)
   - `g_img_03`: `.rodata.g_img_03` `0xffccd6e4` (`0x3fc00` = 261,120 バイト、ROM、4バイト境界)
   - **H10 判定: 合格** — `RAM2` (`0x00800000`〜`0x0087FFFF`, 長さ `0x00080000`) に配置された出力セクションは **0 件**。
   - **P2 判定: 合格** — ROM 配置合計 = `1,105,608` バイト (`1,079.70 KB`, **`1.0544 MB` / 4.00 MB**, 使用率 `26.36%`, 残容量約 `2.95 MB`)。
     内訳: `.text` 55,264B + `.rvectors` 1,024B + `.rodata` 1,047,340B(画像4枚 1,044,480B 含む) + `.data`(LMA) 1,852B + `.exvectors`/`.fvectors` 128B。
2. **`src/smc_gen/` 未変更確認**:
   - 配下 130 ファイルの更新時刻はすべて `2026-11-19 21:46` 〜 `2026-11-20 21:26:02` であり、4枚目追加作業(`22:43〜22:53`)での編集なし。
3. **設計との照合 (`docs/design.md` §10, §12)**:
   - `images.h`: `APP_IMAGE_COUNT` = `(4u)`、`g_img_03` 宣言あり。`images.c`: `g_images[4]` に `g_img_00..03`。
   - `app_main.c`: `nav_init(&nav, (uint8_t)APP_IMAGE_COUNT, ...)` および `g_images[idx]` を使用しており、4枚への自動追従を確認。
   - `tools/prepare_slide.py`: `LANCZOS` リサイズ、`251x305` → `224x272`、`480x272` 黒背景(`#000000`)の `(128, 0)` へ `alpha_composite`、不透明 RGB PNG 出力で設計 §12.2 と一致。`tools/img2c.py` の非 `480x272` 終了コード 2 は維持。
4. **見つかった軽微な問題(S5 不合格の原因)**:

| # | 重大度 | 内容 | 再現・根拠 | 期待 | 該当箇所 | 宛先 |
|---|---|---|---|---|---|---|
| 1 | 軽微(環境由来) | `test_img2c.py` の `test_committed_files_match_generator` が失敗(`img_00..02.c` が CRLF、`img2c.py` の出力は LF) | `python -I tests/host/test_img2c.py` で `\r\n` vs `\n` 差分を検出。`\r\n` を `\n` に正規化すると 3ファイルとも **100% 一致** (`norm_lf_eq=True`)、`img_03.c` は LF で一致 | `img_00..02.c` を LF で保存する(または `img2c.py --testpattern` を再実行して LF に揃える) | `src/app/img_00.c`, `img_01.c`, `img_02.c` | コーダー(またはリポジトリ管理者) |

### 9.4 実機確認チェックリスト(4枚対応版・未実施)

書込み: `tools/submit-job.sh rfp '{"mode":"write"}' 175` (4枚ビルド `rx72n_gcc.mot` 使用)後、以下を実施する。

| ID | 対象 | 操作・コマンド | 合格条件 | 合否 |
|---|---|---|---|---|
| B4 | 目視・タッチ | 画面右半分を4回タップ → 左半分を4回タップ | 右で `0→1→2→3→0`、左で `0→3→2→1→0` と4枚で循環(各 0.3秒以内) | [ ] |
| B4b | 目視(4枚目) | 画像 3(4枚目)を表示し画面を見る | 黒背景(`#000000`)の中央に添付画像が正しい向き・色・縦横比(`224x272`)で欠けや角丸の白ゴミなく表示される | [ ] |
| UT4 | UART `n/p` + 目視 | `serial`: `{"port":"COM7","baud":115200,"seconds":12,"tx":"nnnp","txAtSec":3,"txGapMs":400}` | `R-EV` が 4本(`src=uart`)、`img` が `(b+1)%4`, `(b+2)%4`, `(b+3)%4`, `(b+2)%4`。直後の `R-HB` の `img` と一致し、LCD も追従 | [ ] |
| UT6 | UART 60連打 | `serial`: `{"port":"COM7","baud":115200,"seconds":14,"tx":"n"×60,"txAtSec":3,"txGapMs":0}` | `R-EV` がちょうど 60本、最後の `img` = `(b+60)%4` = `b`、`drop=0`、`code=7` が 0本、`tick` 差 ≦ 130 | [ ] |
| UT7 | タッチ + UART | `serial`: `{"port":"COM7","baud":115200,"seconds":25}` (右タップ→左タップ→長押し→2本指) | `R-EV`(`src=touch`)が `(b+1)%4` → `b` → `(b+1)%4`、長押し追加なし、2本指なし | [ ] |


---

## 10. 2026-11-21 製品デモ v2 P1(`docs/design.md` v2 §12、`docs/design-uart.md` §10)の試験(テスト担当)

### 10.0 結論(まず読む)

| 区分 | 結果 |
|---|---|
| `bash tests/host/run.sh`(自分で実行) | **PASS=16 FAIL=0 SKIP=1**(SKIP = S12 sanitizer。libasan が無い。**SKIP は合格ではない**)。全文は `tests/host/last-run.log` |
| 設計 §12 のホスト項目(H-G1〜H-S、14 項目) | **14 / 14 がホストの試験でカバー済み**(既存の試験で 14、うち 8 項目は今回の追加で強化。対応表は §10.2) |
| 設計 UT10〜UT19(10 項目) | ホストで論理を **10 / 10 検証済み**。実機の合否は **未実施(実機待ち)**。**判定スクリプトを新規に用意**(§10.5) |
| 製品コードの不具合 | **誤動作 1(中)、軽微 2、設計の見積の食い違い 1(中)**、観察 4(§10.4)。**製品コードは直していない** |
| 追加した試験 | S16(`test_ui_extra.c`、11,990 checks)、S17(`judge_ui.py` とそのテスト 43 件、ログ生成 `trace_ui.c`) |

**最重要: D1(デモを中断した時に、仮想押下が離されずに残る)。** 3 秒後に、中断したはずのボタンが動作する。再現済み(§10.4)。

### 10.1 実行結果(2026-11-21、この PC、MinGW gcc 13.2 / Python 3.14)

| ステップ | 内容 | 結果 | 件数 |
|---|---|---|---|
| S1 | 標準Cの7ファイルの静的条件 | 合格 | — |
| S2 / S3 / S9 | nav_logic / touch_decode / logq / nav_step | 合格 | 245+196 / 135 / 154+589 |
| S4 | H-A1 / H-A2(アセット生成・サイズ・ROM) | 合格 | — |
| S5 | img2c / prepare_slide(2 件 skip は設計どおり) | 合格 | 10 + 6 テスト |
| S6 | lcd_panel(実 FIT の param check を通す) | 合格 | 153 |
| S7 | ハード依存部の -fsyntax-only(log 0/1/2、perf 0) | 合格 | — |
| S8 | app_logfmt(全行・境界・長さ) | 合格 | 14,384 |
| S10 | app_debug のログ部 | 合格 | 121 |
| S11 | app_main 結合(起動順・hb・コマンド・[ev]・描画と入れ替え・失敗系) | 合格 | 362 |
| **S12** | sanitizer(address, undefined) | **未実施(SKIP)** | — |
| S13 | gfx(H-G1 / H-G2) | 合格 | 37 |
| S14 | ui_logic(H-U1〜H-U7) | 合格 | 343 |
| S15 | ui_render(H-G3) | 合格 | 38 |
| **S16(新規)** | test_ui_extra | 合格(KNOWN-DEFECT 9 件は表示のみ。§10.4) | 11,990 |
| **S17(新規)** | trace_ui(製品の app_main が出す UT10〜UT19 のログ)+ judge_ui とそのテスト | 合格 | 10 + 43 テスト |

- S12 の代わりに、S16 の X12 / X13 で「面の前後に番兵を置いて、極端な引数でも書かない」を試験した。**sanitizer の代わりにはならない**(読み出しの範囲外・未初期化は見えない)。Linux / WSL があれば S12 を再実行する。S16 は S12 の対象に入れていない。
- ここでの合格は「ホスト GCC での論理の検証」。RX 用 GCC のビルド・実機・実時間・LCD の見た目の証明ではない。

### 10.2 受入基準の対応表

凡例: ◎ = 既存の試験で検証済み / ＋ = 今回追加した試験で強化 / 実機 = ホストでは判定できない

#### 設計 §12.1 ホスト(H)

| ID | 検証した試験 | 判定 |
|---|---|---|
| H-G1 | S13 H-G1a〜d(基準値との差 ≤ 2、実測最大 2。scale 0 / 256、番兵つきで外へ書かない)。＋ S16 X12(座標・大きさが INT32 の端 11^3 通り) | ◎ ＋ |
| H-G2 | S13 H-G2a / b。＋ S16 X12(fillrect を遅い基準実装と全画素比較) | ◎ ＋ |
| H-G3 | S15 H-G3a / b / c(5 画面 × running/done、遷移 dx = 0,1,120,240,479,480 × 前進/後退、極端な値)。＋ S16 X13(範囲外の値 400 通り + 前後の番兵) | ◎ ＋ |
| H-U1 | S14 H-U1a〜i(全ボタン、35 / 48 で止まる)。＋ S16 X1(当たり判定: 画面内・見た目 ±3・重ならない・境界の 1 画素・ホーム全面の走査) | ◎ ＋ |
| H-U2 | S14 H-U2a〜c(e = 0, 1000, 8000, 32000 ms、done の時刻、24 時間で巻き戻り)。＋ S16 X4(通常速度の追い焚き 16.0 秒 / 設定 48 で 40 秒)、X10b | ◎ ＋ |
| H-U3 | S14 H-U3a〜c(PRESS・90ms・FLASH 60ms・3 秒強制・遷移中は無視) | ◎ |
| H-U4 | S14 H-U4a〜g(2 標本 / 3 標本 / 2 点 / NULL / flags)。＋ S16 X10(x_max = 0、0xFFFF、軸入れ替え時の正規化、単調) | ◎ ＋ |
| H-U5 | S14 H-U5a〜c(e = 125 で 360、単調、前進/後退、待機は即切替、古い画面の写し)。S15(平行移動と全画素一致) | ◎ |
| H-U6 | S14 H-U6a〜f(36 本・時刻 ± 10ms)。＋ S16 X6(更新が 20〜40ms 飛んでも同じ 36 本)、X7(32bit 折り返し)、X8(中断後の再開・終了後の再開) | ◎ ＋ |
| H-U7 | S14 H-U7a〜c。＋ S16 X2(n p 5 回)、X3(+ 8 回 - 2 回)、X5(+ 60 個、600 個) | ◎ ＋ |
| H-A1 | S4(--check、再実行で同一、コミット済みと一致、背景 7 枚 480x272、総バイト ±30%) | ◎ |
| H-A2 | S4(警告なし、`.rdata` 合計 2,037,568、背景 261,120 × 7、RAM に入るデータなし) | ◎ |
| H-L | S8(最長 `[hb]` 178 / `[st]` 222 / `[ui]` 96、境界、小バッファ) | ◎ |
| H-S | S1(gnu99 / c99 -pedantic-errors / c11、警告ゼロ、`platform.h` 非依存、データシンボルなし) | ◎ |

#### 設計 §12.2 ビルド(P)

| ID | 判定 |
|---|---|
| P1(Headless ビルド 0 errors) / P2(ROM ≤ 2.4 MB、RAM2 に出力セクションなし、.bss 増 ≤ 1 KB) | **ビルド担当の項目。私は未実施**(コーディング担当の報告: 個別 -c で警告 0、ROM 見積 約 2.1 MB。リンク後の実測は未) |

#### 設計 `design-uart.md` §10.8 UT10〜UT19

| ID | ホストで検証した所(製品の論理) | 判定スクリプト | 実機 |
|---|---|---|---|
| UT10 順送り | S11 M6 / S16 X2(to・from・src・btn なし。300ms 間隔でも取りこぼさない。250ms 未満は無視) | judge_ui UT10 | 実機待ち |
| UT11 逆送り | 同上 | UT11 | 実機待ち |
| UT12 温度と上下限 | S11 M9 / S16 X3(8 組・43〜48・47・46) | UT12 | 実機待ち |
| UT13 デモ全走 | S11 M10 / S14 H-U6a / S16 X6 X7(36 本・順序・時刻)。**`frm` の 200〜600 は超える見込み**(D4) | UT13 | 実機待ち |
| UT14 中断(UART) | S14 H-U6d(5.6 秒で abort + scr=home from=fill)。**D1 あり** | UT14 | 実機待ち |
| UT15 中断(実タッチ) | S11 M10b / S14 H-U6c。**D1 / D3 あり** | UT15 | 実機待ち |
| UT16 通常速度の完了 | S16 X4(done=reheat まで 16.0 秒、done=fill なし) | UT16 | 実機待ち |
| UT17 連打 | S11 M13 / S14 H-U7c / S16 X5(set の [ev] は 4 本、48 を超えない) | UT17 | 実機待ち |
| UT18 [perf] と起動 | S11 M1 / M18(順序・形式)。**値(copy_us 等)は実機でのみ** | UT18 | 実機待ち |
| UT19 タッチ | S11 M11 / S14 H-U4 / H-U1h(論理)。**V0(向き)が先** | UT19 | 実機待ち |

#### 設計 §12.3〜§12.4 実機の項目(ホストでは判定不能)

| 項目 | 内容 | 担当 |
|---|---|---|
| UT10〜UT19 の合否 | 上の表。`uf=0`、`drop=0`、`[perf]` の実測値、`frm` の実測値 | デバッグ担当(`judge_ui.py` で判定) |
| V0 | タッチの向き(`TOUCH_SWAP_XY / FLIP_X / FLIP_Y`、`TOUCH_X_MAX / Y_MAX` は仮値のまま) | ユーザー + デバッグ担当 |
| V1〜V13 | 画面の見た目(モックとの一致 ± 2 画素、日本語の欠け)、押下の暗転・白光の濃さ、スライドの滑らかさ・ティアリング、デモの見た目、10 分放置、右端・下端の欠け | ユーザー(目視) |
| (UART で見えない) | 垂直同期の反映時刻、GLCDC のアンダーフローが起きる条件、割り込みとメインの実際の競合 | 実機 |

### 10.3 追加・変更したファイル(製品コードは変更していない)

| ファイル | 内容 |
|---|---|
| `tests/host/test_ui_extra.c`(新規) | S16。X1〜X13(§10.2 の「＋」)。`XCHECK` = 既知の不具合の再現(表示のみ。`STRICT_XFAIL=1` で失敗扱い。直ると FIXED と出る) |
| `tests/host/trace_ui.c`(新規) | S17。`test_app_main.c` を取り込み、**製品の app_main** を偽の周辺で動かして UT10〜UT19 と同じ操作をし、UART 出力を `utNN.log` と `utNN.stamp.log`(`-Stamp` 形式、`# tx` 行つき)に書く |
| `tests/host/judge_ui.py`(新規) | UT10〜UT19 の判定スクリプト(§10.5) |
| `tests/host/test_judge_ui.py`(新規) | 判定スクリプトの試験 43 件(固定ログ・新しいログ・途中切れ/BOM/LF・日またぎ・壊したログが FAIL になること) |
| `tests/host/data/ut10〜ut19.stamp.log`、`ut10 / ut13 / ut15 / ut19.log`(新規) | 固定の試験ログ(上の trace_ui が作ったもの。時刻つき 10 本、なし 4 本) |
| `tests/host/test_app_main.c`(2 行) | `g_send_hook_user`(ログ生成が送信を横取りするための差し込み口。通常は NULL) |
| `tests/host/run.sh` | S16・S17 を追加 |
| `tests/host/last-run.log` | 今回の全文 |
| `tests/host/judge_ut1.py` | **変更なし**(v1 用)。v2 は `judge_ui.py` |

### 10.4 不具合・レビューの指摘(重大度つき)

**製品コードは修正していない。** 修正はコーディング担当へ。

| # | 重大度 | 内容 | 再現 | 期待 / 実際 | 場所 |
|---|---|---|---|---|---|
| **D1** | **中(誤動作)** | **デモを中断した時、仮想押下(150ms)の最中だと、押下表示(PRESS)が残る。** 離しを送る側(`vt_on`)を中断で消すので、誰も離さない。3 秒の強制実行で、**中断済みのデモのボタンが動作する**(例: `btn=h_up st=act src=demo` と `set=43`。ホームのボタンなら画面遷移もありうる)。その間(最大 3 秒)、**実タッチのボタンも効かない**(PRESS 中は別の押下を無視する)。UART `d` で中断した場合も同じ | `S16 X9 / X9b / X9c`(デモ開始から 0.84 秒で実タッチ or `d` → 3.8 秒後に act)。ファズ(X11)でも 13,661 tick 出る | 期待: 中断で仮想押下を取り消す(`fb_phase = NONE`)。実際: PRESS のまま 3 秒。**デモの全ステップのうち、ホームでの 7 回 × 150ms が危険な窓(約 3%)** | `src/app/ui_logic.c` `demo_abort`(254〜263 行。`vt_on = 0` の後に仮想押下を取り消していない)、`home_instant`(228〜235 行。ホームにいる時は何もしない) |
| D2 | 軽微 | `+` を送って 150ms 以内に `d` で開始すると、同じ理由で `+` の押下が 3 秒後にデモ中に実行される(`btn=h_up st=act src=uart` と set+1) | `X9d` | 期待: デモ開始で取り消す。実際: 3 秒後に act | `ui_logic.c` `demo_start`(238〜250 行。`vt_on = 0` のみ) |
| D3 | 軽微 | ホームへ戻るスライドの最中(ホームにいるが `tr_on = 1`)に中断すると、スライドが続く(設計 §6.1 は「アニメーションなしで即ホーム」) | `X9e`(デモ 13.05 秒) | 期待: 即停止。実際: 残り最大 250ms スライド | `ui_logic.c` `home_instant`(`scr != HOME` の時だけ動く) |
| **D4** | **中(設計の見積)** | **UT13 の「`frm` の増加 200〜600」は、ホストの模擬で 613〜614(表示キーの変化 699 回)で超える。** 実機の垂直同期 16.8 ms でも同程度の見込み。`design-uart.md` UD2 は「実測値で更新」と書いてある。**実機で UT13 が不合格になる可能性が高いが、不具合ではなく見積の誤り** | S11 M10(614)、S14 H-U6a(699)、判定スクリプトが WARN を出す | 設計: 200〜600。実際(ホスト): 約 614。**判定スクリプトは 900 まで PASS + WARN**。実機の実測で設計を更新 | `docs/design-uart.md` §10.8 UT13 / UD2 |
| O1 | 観察(低) | `[perf]` の測定(約 150 回の描画)の間、メインループが止まる。10ms タイマは先に動くので、**最初の `[hb]` の tick が 100〜112 に収まらない**(v1 の UT2 の条件は v2 では当てはまらない)。また、測定の直前に積んだ `[init] timer / loop` の送信が、測定の後まで遅れる(落ちない。キュー 8) | 構造から。ホストの模擬は tick が止まるので見えない | 判定スクリプトは WARN にした | `app_main.c` `log_perf` |
| O2 | 観察(低) | `s_flip_pending` に時間切れが無い。`GR2VEN.VEN` が 0 に戻らない異常(表示が止まる)が起きると、描画が黙って止まり、`[err]` も出ない(`frm` が増えないことでだけ分かる) | 構造から | 提案: 約 100ms 以上待ったら `[err]` を 1 回出す。必須ではない | `app_main.c` 主ループ(反映待ちの確認) |
| O3 | 観察(情報) | 「湯はり停止」を押した直後(押下から離しの間)に水位が 100% に達すると、動作時点で `full` が評価され、`bath = 40.0` になる(押下時は f_stop、実行は完了扱い)。見た目に害はない | 設計の想定外の隙間 | 対応不要 | `ui_logic.c` `act`(`F_DONE / F_HOME`) |
| O4 | 観察(情報) | `ui_clock_min()` は `now_ms` が 2^32(約 49.7 日)で折り返すと、時計が 24 時間の倍数でない量だけ跳ぶ。`now * 10`(app_main)も同じ。展示のデモでは影響しない | 構造から | 対応不要(連続稼働が 49 日を超える製品になる時に直す) | `ui_logic.c` `ui_clock_min`、`app_main.c` tick_step |

**問題なしと確認した所(レビュー)**

| 観点 | 結果 |
|---|---|
| 描画の範囲外書込み | `gfx` の 3 関数は INT32 の端の引数でも面の外へ書かない(番兵 4096 画素、11^3 通り、fillrect は全画素を基準実装と比較)。`ui_render` も範囲外の値(65535 など)で番兵を壊さず、全画素を書く(X12 / X13) |
| 整数オーバーフロー | `ui_fill_x10` / `ui_bath_x10` は `e_ms` を 3,600,000 で切る。`ui_bar_h` は `e >= 400` を先に判定。`ui_slide_dx` は 250ms 以上で 480。`ui_touch_map` は x_max / y_max = 0 でもゼロ割りせず、生座標 0xFFFF でも 0..479 / 0..271 |
| 32bit の折り返し | `ui_update` の時刻の引き算は unsigned。X7(折り返しをまたぐデモ)と M20(tick の折り返し)で確認 |
| UART バッファ | 最長 `[hb]` 178 / `[st]` 222 / `[ui]` 96 は `APPLOG_LINE_MAX` 256 未満。`s_line` は static でメインのみ。`ui_event_pop` を毎 tick で空にするので、`ev` 待ち行列 8 はあふれない(ファズで `ev_lost` = 0) |
| 割り込みとメインの競合 | 割り込みは `s_tick++` と SCI のコールバック(値を書くだけ)のみ。`g_app_dbg` と `ui_t` はメインだけが書く。`s_tick` は 32bit 整列で読み出しは原子的 |
| 未初期化 | `ui_init` が `memset`。`ui_key_t` の比較は `memset` 済みの値の `memcmp`(パディングも 0) |
| タッチ座標のマクロ | `TOUCH_X_MAX / Y_MAX` は仮値(479 / 271)。`board_pins.h` の `TOUCH_SWAP_XY / FLIP_X / FLIP_Y` は `UI_TF_*` に変換される。向きは V0 で決める(実装メモ差分 #4 の定義: 入れ替え時は軸ごとの最大値のまま) |
| デモ中断の漏れ | 実タッチ(押下確定)・UART `d`・台本の失敗で中断される。n p + - は無視。**ただし D1 / D2 / D3** |
| `src/smc_gen/` | 私は触っていない |

### 10.5 判定スクリプトの使い方(実機の試験でデバッグ担当が使う)

```
python -I tests/host/judge_ui.py <受信ログ>              # ログから該当する UT を自動で選ぶ
python -I tests/host/judge_ui.py <受信ログ> --ut UT13    # UT を指定(壊れたログは自動選択に頼らず、指定して詳しく見る)
```

| 項目 | 内容 |
|---|---|
| 入力 | `serial` ジョブの受信ログ / `tools\serial-read.ps1` の出力。BOM は取る。`-Stamp` 付き(行頭 `HH:MM:SS.mmm `)も、なしも可。`# tx 'c'` の行は無視 |
| 先頭と末尾の行 | 途中で切れている可能性のある行だけ捨てる(先頭: 書式に fullmatch しなければ捨てる / 末尾: ログが改行で終わっていなければ捨てる)。**無条件に捨てたい時は `--strict-edges`** |
| 共通(C1〜C4) | C1 書式(§10.9 の正規表現を fullmatch、異物 0)/ C2 行の長さ(255 バイト未満)/ C3 `[hb]`(scr 0〜4、frm・uf・drop が減らない、tick 差 100〜110、130 超は不合格)/ C4 `[hb]` あり |
| UT10〜UT19 | それぞれ PASS / FAIL / 対象外。ログの内容から該当を選ぶ(`--ut all` で全部)。[ev] があるのにどの UT にも当てはまらないログは **FAIL**(黙って合格にしない) |
| 時刻 | `-Stamp` あり: `demo=start` からの相対時刻を §6.2 の表と比べる(許容 200ms、`--tol-ms` で変更)。なし: 前後の `[hb]` の tick から作った区間と比べる(許容 100ms + 区間幅) |
| WARN(合否に影響しない) | `frm` の増加が 600 を超えた(D4)/ `[perf]` の値が目標超え(`copy_us` 8000、`home_us` / `slide_us` 12000: 後退案の判断材料)/ 最初の `[hb]` の tick が 100〜112 でない(O1)/ tick 差 110〜130 |
| 終了コード | FAIL が 1 つでもあれば 1(UT 名が不正なら 2) |
| UT18 | `[boot]` が受信中にあるログだけ対象(途中から受信したログは対象外) |
| 判定できないこと | 画面の見た目、実時間そのもの(`-Stamp` が無いと [hb] の区間でしか時刻を見ない)、LCD の動き |

### 10.6 実機に回す項目(デバッグ担当へ・ユーザーへ)

| 順 | 項目 | 何を見るか |
|---|---|---|
| 1 | V0(ユーザー + デバッグ担当) | タッチの向き。`xmin xmax ymin ymax` と四隅。決まるまで UT19 の合否は意味を持たない |
| 2 | UT18 | `[perf]` の `copy_us / home_us / slide_us`。**最初に測る**(O1: 最初の `[hb]` が遅れるのは正常) |
| 3 | UT10 / UT11 / UT12 / UT16 / UT17 | `judge_ui.py` で判定。UT12 は目視(押下の暗転・白光)も |
| 4 | UT13 | 判定スクリプトで PASS + `frm` の WARN が出る見込み(D4)。**`uf=0`、`drop=0` を必ず確認**。`frm` の実測値を `design-uart.md` UD2 に書く |
| 5 | UT14 / UT15 | 判定スクリプトで PASS するはず。**D1 が直るまでは、中断のタイミング次第で、3 秒後に `btn=... st=act src=demo` が出る場合がある**(UT14 / UT15 は湯はりの最中に中断するので窓には入らない) |
| 6 | UT19 + V1〜V13 | ユーザーの操作・目視(`docs/design.md` §12.4) |

### 10.7 再実行

| 目的 | コマンド |
|---|---|
| 全部 | `bash tests/host/run.sh`(約 50 秒。S12 は SKIP) |
| 追加試験だけ | `bash tests/host/run.sh S16 S17` |
| 不具合の再現を失敗扱いにする | `STRICT_XFAIL=1 /tmp/rx72n-host-build/test_ui_extra`(S16 のビルド後。D1〜D3 の 9 件が失敗になる。直ると `FIXED` と出る) |
| 判定スクリプトを手元のログで | `python -I tests/host/judge_ui.py tests/host/data/ut13.stamp.log` |

**変異テスト(試験が効くことの確認)**: `test_judge_ui.py` で、順序の入れ替え・イベントの欠落・src の違い・時刻のずれ・frm の異常・`[err]` の混入・書式違反・長すぎる行・値の範囲外などでログを壊し、狙った項目が FAIL になることを確認した(43 件のうち 31 件が変異テスト)。製品側は、D1〜D3 が S16 で KNOWN-DEFECT として検出されることで、試験が不具合を検出できることを確認した。

### 10.8 追記(2026-11-21): 実機ログでの判定と、判定スクリプトの修正

**結論: `jobs\ut\UT10 / 11 / 12 / 13 / 14 / 16 / 17.log`(旧ビルド、デバッグ担当が `tools\ut-run.ps1` で記録した実機ログ)7本すべてが、修正後の `judge_ui.py` で PASS(WARN は `uf` の1回だけ)。UT15 / UT19 の実機ログはまだ無い。** `bash tests/host/run.sh` は **PASS=16 FAIL=0 SKIP=1**(S12 のみ SKIP)。テストは 43 → 49 件。

| 実機ログ | 修正前 | 修正後 |
|---|---|---|
| UT10 / 11 / 14 / 16 / 17 / 12 / 13 | 全部 FAIL(C1 異物 2 行、C3 tick 逆戻り(リセット前の出力)、`uf` が 0 でない) | **全部 PASS**。WARN = `uf` が起動後に 1 回だけ増えた(旧ビルド)。UT13 の `frm` 増加は 621(範囲内。WARN なし) |

| # | 実ログでずれた点 | 修正 |
|---|---|---|
| 1 | ログが**リセット前の古い出力**(別の走行の `[hb]`、途中切れの行)から始まる。tick が大きい値から 0 付近へ戻るので C3 が落ち、`frm` も減る | **最後の `[boot]` より前を、リセット前の出力として判定から外す**(「注: 最後の [boot] より前の N 行は…」と表示)。[boot] が無いログは従来どおり |
| 2 | `# reset released (rfp-cli 終了)`、`# end` の行が異物(C1 不合格)になる | `# ` で始まる行(時刻つき・なし)はコメントとして無視。`# tx` もこれに含む |
| 3 | 先頭に BOM + 空行、先頭が途中切れの行(`[k=0 found=...`) | 既に対応済み(BOM 除去、書式に合わない先頭行は捨てる)。上の #1 で、そもそも判定の外になる |
| 4 | 行頭時刻 `HH:MM:SS.mmm ` | 既に対応済み。`demo=start` からの相対時刻の判定は実ログ(UT13)で PASS(許容 200ms) |
| 5 | `uf` | 下の表 |
| 6 | `frm` | 下の表 |

| 判定項目 | 変更 | 理由 |
|---|---|---|
| `uf`(C を各 UT に入れていた「すべて 0」) | **「起動後の増分」で判定**。基準は 0([boot] があるとき)。増分が **1 回だけなら PASS + WARN**、**2 以上なら FAIL**。[boot] が無いログは最初の `[hb]` の値を基準にする。旧ビルド(`tick=200` から `uf=1`)も新ビルド(全部 `uf=0`)も通る。`[boot]` の build= では見分けない | 依頼の方針どおり。実機 7 本で確認。2 回目の増分は FAIL になることを `test_uf_first_increment_warns_later_fails` と実ログの改変で確認 |
| UT13 の `frm` 増加(200〜900) | **範囲内は PASS で WARN を出さない**(旧: 600 超で WARN)。**800 を超えたら「上限に近い」WARN**、900 超は FAIL、200 未満は FAIL | 設計(`design-uart.md` UD2・UT13)が 200〜900 と決めた(実測約 620)ので、範囲内の値で毎回 WARN を出すと実機のたびにノイズになる。800 超の WARN は「表示キーの無駄な変化が増えていないか」を気づく余裕の確認用(設計の `FRM_SOFT_MAX = 600` は廃止。UD2 の記述は設計担当が更新) |

**固定ログに追加**(`tests\host\data\`、UART ログのみ。個人情報なし): `real_ut10.stamp.log`、`real_ut12.stamp.log`、`real_ut13.stamp.log`、`real_ut17.stamp.log`(実機の旧ビルドのログ。`jobs\ut` から複製)。テスト `RealLogTests` が、PASS + `uf` の WARN、リセット前の出力の除外、コメント行が異物にならないこと、`uf=0` に直した新ビルド相当で WARN なし、2 回目の `uf` の増分で FAIL を確認する。

**未確認**: 新ビルド(`lcd_init` 直後に `uf` を捨てる修正)の実ログは未入手。新ビルドで全 `[hb]` が `uf=0` になることは、実ログの `uf=1` を `uf=0` に置き換えた改変ログで判定が通ることまでしか確認していない。UT15(実タッチ中断)・UT19(タッチ)・UT18 の `[perf]` 値の実ログも未入手(UT12 / UT16 の `[perf]` 行は書式 OK で `copy_us=800 home_us=2000 slide_us=1800`、目標内)。


## 11. 2026-11-22 P2b(製品 UI の CLUT8 化。`docs/design.md` §20)の試験(テスト担当)

### 11.0 結論(まず読む)

**ホスト側では合格。不合格 0、重大・中の指摘 0、低の指摘 5。実機・ビルドが要る項目は未実施(§11.7)。S12(sanitizer)は SKIP のまま(合格ではない)。**

| 項目 | 結果 |
|---|---|
| `bash tests/host/run.sh`(自分で実行。2026-11-22、MinGW gcc / Python 3.14) | **PASS=20 FAIL=0 SKIP=1**(S12 = libasan / libubsan が無く SKIP)。コーディング担当の報告 PASS=18 + 今回追加の S20・S21 |
| 追加した試験 | S20 `test_p2b_extra.c`(41 checks)、S21 `test_p2b_indep.py`(P1 のコミットから独立に再計測) |
| 誤差の独立再計測 | 設計・コーディング担当の値と**一致**(背景7枚 1,605 画素 = 0.1756%、最大 2 段。静的 最大 3 段)。§11.4 |
| 見つけた問題 | 不具合なし。**低 5 件**(§11.5)。プレビュー PNG に P2b 固有の異常なし(§11.6) |
| 試験が効くことの確認 | 製品コードの複製を壊して確認(§11.8)。製品コードは変更していない |

### 11.1 実行結果

| 項目 | 結果 | 証拠 |
|---|---|---|
| `run.sh`(全部。1回目 = 追加前) | PASS=18 FAIL=0 SKIP=1 | S19 = 23 状態で不合格 0 件、静的 不一致 0.454% 最大 3 段、押下 不一致 0.660% 最大 6 段 |
| `run.sh`(全部。2回目 = S20・S21 追加後) | **PASS=20 FAIL=0 SKIP=1** | S12 SKIP |
| S20(`tests/host/test_p2b_extra.c`) | PASS | 41 checks、0 failed。押下・白光の組合せ 静的 56 + スライド 2,992 |
| S21(`tests/host/test_p2b_indep.py`) | PASS | 背景 7 枚・固定色・remap・check_c8 の検出力 5 通り・描画 852 状態(約 20 秒) |

### 11.2 受入基準(§20.9)の対応表

| ID | ホスト試験 | 状態 | 補足 |
|---|---|---|---|
| H-D1 | S4(`--check`、再実行で同一、作業ツリーと一致(CR 以外)、`ui_bg_*.o` の `.rdata` が各 130,560、生成データ合計 1,101,171)。**追加**: S21 I1(P1 の背景を `git archive 0368fe3` から取り出して再計測)、I2(固定色)、I3(`ui_remap` が最近傍)、I4(`check_c8` が壊したパレットを検出)。S20 P1・P1b・P2(パレットの形・棒の2色・表の形) | **検証済み** | 固定色は設計 39 に対し実装 24(コーディング担当の差分 #2)。P1 の背景で 200 画素以上ある 20 色は**すべてパレットに厳密にある**(I2)。`ramp` の「再計算との一致」は下地・文字色がジェネレータ内部の値のため単独では再計算していない。代わりに数字の網羅走査(I5)で端から端を P1 と比べた |
| H-D2 | S13(`test_gfx.c` H-D2a〜e、83 checks)。**追加**: S20 P4(8bpp の 4 関数を、座標・大きさが INT32 の端で。面の前後と右の 32 列に番兵。`fill8` は遅い基準実装と全画素一致) | **検証済み** | 既存の「INT32 の端」は RGB565 版(X12)だけで、8bpp 版は無かった。`clip()` は共通だが、8bpp の行幅(512)の書込みは別の式なので追加 |
| H-D3 | S15(H-G3a/b/c: 0x00 / 0xFF で埋めて同一、右の 32 列は不変、遷移 600 通り)、S16 X13(範囲外の値) | **検証済み** | |
| H-D4 | S19(実機で読んだ 23 状態。基準 = `p1_ref.npz`)。**追加**: S21 I5(P1 のコード = `0368fe3` を直接ビルドし、乱数 + 走査の 852 状態を比較) | **検証済み** | 判定の当て方の注意は §11.5 の R3 |
| H-D5 | S1・S11・S14・S16・S17 ほか | **検証済み**(回帰なし) | |
| H-D6 | S6(実 FIT の `r_glcdc_open_param_check` を通る。`offset` 513 / 480 は弾かれる対照あり) | **検証済み** | |
| P-D1 / P-D2 | ビルド担当が並行(今回は触らない) | 未実施(ビルド待ち) | |
| U-D1 / U-D2 | — | **未実施(実機待ち)** | `uf` の増分 0、デモ 33.0 秒、`[perf]` |
| G-D1 / G-D2 | — | **未実施(実機待ち)** | `fb2png.py check --c8` も実機ログでは未実行(R5) |
| V-D0〜D2 | — | **未実施(ユーザーの目視)** | プレビューは §11.6 |

**追加したテストで埋めた「ホストで試験できるのに無かった項目」**

| 項目 | 追加した試験 |
|---|---|
| 押下 index 置換の境界(形の A8 ≥ 128 の画素だけ。ボタンの角・画面の端) | S20 P3a(全 14 ボタン x 暗転/白光 x 状態 2 通り。**画素単位で全一致**)、P3b(別の画面のボタンは何も変えない) |
| スライド中の押下の列(古い画面・新しい画面・どちらでもない画面のボタン、前進/後退、dx = 1〜480) | S20 P3c(2,992 組合せ。見えている列だけが置き換わる) |
| パレットの 256 色の範囲・形 | S20 P1(256 エントリー、上位 8bit = 0xFF、RGB565 の 8bit 展開、すべて異なる)、P2(表の形) |
| 固定色の保持・使用量の棒の2色 | S20 P1b(0x6DD5 / 0xCEFC)、S21 I2 |
| `--check` の失敗検出 | S21 I4(255 エントリー、固定色を 1 段ずらす、背景 3,000 画素を乱す、1 画素だけ遠い色、のそれぞれで違反を返す) |
| 数字の ramp の端 | S21 I5(水位 0〜1000、浴槽 300〜499 x 設定 3 通り、流量、時計、待機を P1 と比較。数字の部分は最大 2 段) |
| 8bpp の INT32 の端 | S20 P4 |

### 11.3 追加・変更したファイル(製品コードは変更していない)

| ファイル | 内容 |
|---|---|
| `tests/host/test_p2b_extra.c`(新規) | S20。P1・P1b・P2・P3a・P3b・P3c・P4(41 checks) |
| `tests/host/test_p2b_indep.py`(新規) | S21。I1〜I5。P1 は `git archive 0368fe3` から取り出す(tarfile)。gcc と git が無ければ終了コード 77 = SKIP |
| `tests/host/run.sh`(変更) | S20・S21 を追加(S21 は終了コード 77 を SKIP に)。他は変更なし |
| `docs/test-report.md`(この節) | |

### 11.4 誤差の独立再計測(コーディング担当の出力は使わない)

方法: P1 = `git archive 0368fe3` の背景配列・`fb_hostrender`(P1 のコードをビルド)。P2b = 作業ツリーの `ui_bg_*.c`・`ui_pal`・`fb_hostrender`。誤差 = 3チャンネルの段の差の最大(RGB565 の 1 段)。状態は乱数(seed 固定)+ 数字の走査。

| 項目 | 設計・コーディング担当の値 | 独立再計測 | 判定 |
|---|---|---|---|
| 背景7枚の不一致 | 1,595〜1,605 画素(0.175〜0.176%) | **1,605 / 913,920 = 0.1756%**(home 0.550%、fill_run 0.111%、fill_done 0.083%、reheat_run 0.189%、reheat_done 0.114%、usage 0.101%、stby 0.081%) | 一致。目標 ≤ 0.3%、1 枚 ≤ 1.0% |
| 背景の最大 / 1段超 | 2 段 / 13 | **2 段 / 13**(2 段超 0) | 一致 |
| パレット | 256 エントリー | 256 色すべて異なる、上位 8bit = 0xFF、RGB565 の 8bit 展開と 0 個違い | 合格 |
| 固定色 | 24(設計 39) | P1 の背景で 200 画素以上ある 20 色すべてがパレットにある | 合格(基準の文言は R2) |
| 静的(200 枚)/ デモタグ(40 枚) | 設計 0.307%、最大 3 段 | 不一致 0.290% / 0.425%、2 段超 0.0005% / 0.0006%、**最大 3 段** | 合格(≤ 3 段) |
| スライド(押下なし 88 枚) | 設計 最大 2 段 | 不一致 0.314%、2 段超 0.0004%、**最大 3 段** | 合格(≤ 3 段)。設計の「2 段」は 70 フレームの試作の値。乱数で 3 段が出るのは満タンのバーの角(R7) |
| 押下・白光(92 枚、スライド中の押下を含む) | 設計 最大 6 段 | 不一致 0.497%、2 段超 0.0202%、**最大 6 段** | 合格(≤ 8 段) |
| 数字の走査(全走査は使い捨てスクリプトで 3,369 状態: 水位 1001 + 浴槽 600 + 流量 1000 + 時計 480 + 待機 288。S21 には間引いた約 150 状態を入れた) | — | 数字の部分は最大 2 段。**3 段以上は 301 状態すべて x ≥ 449(満タンのバーの右端の角 4 画素)だけ** | 合格 |
| `ui_remap` | 暗転・白光の「内側は 0 段」 | 256 エントリーすべて「mix565(p, 黒/白, l) に最も近い(R1 G1 B0.6)色」。目標色がパレットにあれば厳密(暗転 59 個、白光 131 個) | 合格。ただし暗転は目標色がパレットに無い 42 色で目標と 3〜5 段ずれる(R1) |

### 11.5 レビューの指摘(コードは読むだけ。修正していない)

| # | 重大度 | 場所 | 内容 | 対応案 |
|---|---|---|---|---|
| R1 | 低 | `docs/design.md` §20.4.2、`implementation-notes.md` 付記 §4 | 押下で P1 と 2 段超ずれるのは「角の丸みの縁」だけ、と書かれているが、**文字・アイコンの縁(ぼかしの色)にもある**。1回の押下で 2 段超: H_DN 32(角だけ)、F_STOP 175、R_STOP 153、H_REHEAT 138、H_USAGE 123 画素(`error_table.md`)。暗転の remap は、目標色がパレットに無い 42 色で最大 5 段ずれる。H-D4 の範囲(2 段超 0.1% 以下、最大 8 段)には入っている | 文書の記述を直す。見た目は V-D2(押下の目視)で確認。許容できなければ、押下の暗転だけ remap をぼかしの色用に増やす(設計の判断) |
| R2 | 低 | `docs/design.md` §20.9 H-D1 | 「固定色 39 色が厳密」だが実装は 24 色(数え方の違い。差分 #2 に開示済み)。実質の基準(平らな色が厳密)は I2 で満たしている | 設計の文言を「200 画素以上ある色がすべて厳密」に直す |
| R3 | 低 | `tests/host/judge_p2b.py`、設計 H-D4 | 設計の「不一致 ≤ 0.5%」は、種類ごとの合計に当てている(静的・動き 0.454%。余裕は 9%)。**1 状態ごとに当てると A_home 0.626% などが超える**(23 状態がホーム寄りのため)。乱数 760 枚の独立再計測では 0.27% | 設計の文言を「合計に当てる」と明記する。上限の余裕が小さいので、ホーム以外の状態が増えると変わる |
| R4 | 低 | `implementation-notes.md` 付記 §1、`tools/fb_hostrender.c` の先頭コメント | 「右の 32 列は 0 のまま」は製品では不正確。製品の面は RAM2 の未初期化領域で、起動直後の値は不定で、描かないのでそのまま。GLCDC は `hsize` = 480 バイトだけ読むので表示には影響しない。ホスト(`fb_hostrender`)は静的配列なので 0 | 「書かない(値は不定)」に直す。G-D2 の比較は x < 480 だけなので影響なし |
| R5 | 低 | `tools/fb2png.py` の `check_c8` | パレットの dump が無い時、使用中のパレットを**ホストの `ui_pal` で代用**するので、「パネル色の違い」の列が実機のパレットを見なくなる。「どちらも ui_pal と違う」でも終了コードは 0。実機ログでは未実行 | G-D1 では `c8png.py cmppal`(0 エントリー差)を必ず別に実施する。または、dump が無い段は警告を出して終了コード 1 にする |
| R6 | 情報 | `ui_layout.c` | `ui_slot_ramp` の 2 つ目の表は FILL_PCT 以外は 0 埋め(`alt < 0` で参照されない)。`ramp[0]` は未使用の値(`l = 0` は何もしない)。S20 P2 で形を検査した | なし |
| R7 | 情報 | `ui_render.c` の `draw_bar` | 満タンのバー(432 画素)の右端の角 4 画素が P1 と 3 段(キャップの ramp が「溝の上」を仮定。`fill_done` / `reheat_done` / 水位 1000)。設計の「最大 3 段」の範囲内 | なし |
| R8 | 情報 | `tests/host/run.sh` S15 | 実行すると `tools/assets/bath/preview/fw_*.png` が上書きされ、`git status` に差分が出る(従来どおり) | なし |
| R9 | 情報 | `board_pins.h` | `LCD_FB0_ADDR`(0x00800000)と `LCD_C8_FB_A_ADDR`(既定 `LCD_FB0_ADDR`)の二重定義。ホスト試験が後者だけ差し替えると前者は変わらない(実機は同値で問題なし) | なし(気になるなら `LCD_FB0_ADDR` を `LCD_C8_FB_A_ADDR` に寄せる) |

**読んで問題が無かった点**(確認の根拠つき)

| 観点 | 結果 |
|---|---|
| 8bpp の範囲外書込み | `clip()` が右端を `GFX_W`(480)で切る。面の外・右の 32 列へ書かない(H-D2、S20 P4。INT32 の端、H-D3、X13) |
| 整数オーバーフロー | `(dy + r) * GFX_STRIDE + dx` の最大は 271 x 512 + 479 で `int32_t` に収まる。`clip` の加算は INT32 の端でも安全(S20 P4) |
| パレット添字の範囲 | 面は `uint8_t`、`ui_remap[2][256]`、`ramp[33]` は `l = (a + 4) >> 3` の最大 32 で範囲内。`gfx_a8_ramp` は `ramp0` NULL・`alt >= 0` で `ramp1` NULL を弾く |
| 行幅 512 と 480 の取り違え | 面の行幅は `GFX_STRIDE`(512)。背景(`UI_W` = 480 の詰めた配列)・タグ(84)・バー(`b->w`)・形・数字は元の幅を `src_stride` / `stride` に渡している。`ui_render.c` と `app_main.c` の全呼出しを確認(取り違えなし) |
| スライドの列 | S15 H-U5c(平行移動の一致)+ S20 P3c(押下の見える列) |
| `lcd_panel.c` | `offset` = 512、`format` = CLUT8、`hsize` 480、`clut` = `{true, ui_pal, 0, 256}`、GR1 は `p_base` NULL・`clut` 無効。`R_GLCDC_ClutUpdate` は製品に無い(`clut8_test.c` だけ)。FIT の `R_GLCDC_Open` は `r_glcdc_clut_update` で選択されていない側のプレーンへ 256 エントリーを書いて選択する(`r_glcdc_private.c` 2391〜2426)ので、Open の時の 1 回で足りる(`graphics_read_enable` が真 = `p_base` あり) |
| `APP_CLUT8_TEST` との共存 | S18 PASS。`= 1` では `clut8_test_patch_cfg` が同じ項目を上書きする(`ui_pal` の参照は残るのでリンクは通る) |
| UART `[perf]` | `bench_copy` を `gfx_blit8`(ホーム背景を裏面へ)に変更しただけ。書式・N は不変。値(U-D2)は実機 |
| 型の幅・未初期化 | 面 1 は起動時に未初期化だが、描画で左 480 列を全部書いてから `flip` する(H-D3)。右の 32 列は表示されない |

### 11.6 プレビュー PNG(`tools/assets/bath/preview_p2b/`)の所見

見た画像: `bg_home.png`、`scr_home_demo.png`、`scr_fill_done.png`、`scr_reheat_run.png`、`scr_usage_growing.png`、`scr_slide_home_fill_dx240.png`、`digits.png`、`buttons_press.png`、`palette.png`、`error_table.md`。

| 所見 | 内容 |
|---|---|
| 異常なし | 色の崩れ、文字の欠け、縞、ぼかしの段差の目立つ所は**無い**。P1 と P2b を並べて見分けがつかない。差分の黄(1 段)は文字の縁にまばらに出る程度。数字(時計・設定・温度・水位・流量)の縁も自然 |
| 押下の差分(`buttons_press.png`) | 差分に赤・黒(2 段以上)が出るのは、ボタンの角(円弧状)と、**中の文字・アイコンの縁**(停止・完了・追い焚き・使用量)。縁が細く、暗転は押している間(約 90 ms 以上)だけ。R1 と同じ。目視で許容できるかは V-D2 |
| P1 から引き継いだもの | `scr_fill_done.png` の水位バーの左端に、P1 にも P2b にもある薄い灰色の縦の筋が見える(P2b の差分には出ない = P1 の焼き込み画像の性質)。P2b の問題ではない |
| パレット | `palette.png` は空・水色・オレンジ・緑・灰・暗い灰が揃い、固定色(左上のマゼンタの点)が 24 個ある |

### 11.7 実機に回す項目(デバッグ担当へ・ユーザーへ)

| 担当 | 項目 |
|---|---|
| ビルド担当 | P-D1・P-D2(ヘッドレス 0 errors・0 warnings、text + data ≤ 1,200,000、RAM2 に出力セクションなし、`.bss` の増加 ≤ 64)。ホストの見積: 生成データ 1,101,171 バイト、`gen_ui_assets.py --check` は 1,098,987 バイト |
| デバッグ担当 | G-D1: `GR2FLM3` = `0x02000000`、`GR2FLM5` = `0x010F0007`、`GR2FLM6` = `0x50000000`、`GR2FLM2` が `s_back` の反対、パレット 0 エントリー差(**`c8png.py cmppal` を必ず別に実施**。R5)。G-D2: 15 段の面がホスト描画と 0 バイト差(`mi-fbdump-c8.gdb` と `fb2png.py check --c8` は**実機では未実行**)。U-D1: `uf` の増分 0(UT10・UT13・UT17)、デモ 33.0 秒、`drop=0`、`[err]` なし。U-D2: UT18 の `[perf]` |
| ユーザー | V-D0(プレビューの承認。承認を待たずに実装まで進んでいる)、V-D1(LCD の色。ホーム・湯はり・追い焚き・使用量・待機が P1 と見分けられない)、V-D2(押下で暗くなり離すと白く光る。角と文字の縁(R1)が目立たない。スライドが滑らか。デモ 33 秒) |
| 前提 | 色順・右端・画素の並び(`MDE`)が外れていると全画面が壊れる(R-D1)。8bit のバイト書込みでの `uf`(R-D2)も実機でのみ確認できる |

### 11.8 再実行と、試験が効くことの確認

| 項目 | コマンド |
|---|---|
| 全部 | `bash tests/host/run.sh`(約 1 分) |
| 追加分だけ | `bash tests/host/run.sh S20 S21`(S21 は約 20 秒。`CC` で gcc を指定。`git` と `0368fe3` が必要) |
| 独立再計測だけ | `python -I tests/host/test_p2b_indep.py <作業フォルダ>` |

**変異テスト**(製品コードの複製を壊して実行。製品コードは変更していない)

| 壊し方 | 結果 |
|---|---|
| `ui_render.c` の押下の暗転から画面のずれ `xo` を外す | S20 P3c が失敗(978 組合せ) |
| `gfx.c` の `clip()` の右端を 480 から 512 に | S20 P4 が失敗、既存の `test_gfx`(H-D2)も 7 件失敗(右の 32 列に書く) |
| 白光のしきい値を 128 から 127 に | **検出されない**(今の形のデータに A8 = 127 の画素が無いため)。境界そのものは S13 H-D2e が 127 / 128 で検査している |
| `check_c8` に壊したパレット 4 通り | S21 I4 がすべて検出 |


## 12. 2026-11-25 天気・気温の取得と時刻同期(Wi-Fi)の製品への組み込み(`docs/design.md` §22)の独立試験(テスト担当)

**結論: `bash tests/host/run.sh` を自分で実行して PASS=26 FAIL=0 SKIP=1(S12 = ASan が使えない環境。SKIP は合格ではない)。** コーディング担当の報告(PASS=25)に、今回追加した S27 の 1 つを足した数。実機・E2 Lite・COM7・カメラ・gdb は使っていない。`src/` は編集していない。`wifi_secret.h` は開いていない。

### 12.1 最初の実行で S17 が不合格だった件(試験側の弱点。修正済み)

| 項目 | 内容 |
|---|---|
| 現象 | 最初の全体実行で S17(`judge_ui.py` の判定)が不合格。`[wx] net=stub now=8000` の行が書式に合わない(C1) |
| 原因 | 並行中のビルド担当が `wifi_sw.h` の `APP_WX` を一時的に 1 にしていた(その後 0 に戻った)。`APP_WX=1` の時、`test_app_main.c` の偽の `wx_log_state` が出す行が、判定の書式(design-uart.md §13.1)と違っていた。**`APP_WX` の既定を 1 にしてコミットする(§22.16 の順 8)と、同じ理由で S17 が落ちる** |
| 対処 | `tests/host/test_app_main.c` の偽の行を正しい書式(`[wx] net=on busy=0 have=0 temp10=0 kind=none age=<now> ...`)にし、M23d の照合を合わせた。`run.sh` S17 に、**`-DAPP_WX=1` の `trace_ui` を同じ判定に通す**手順を足した(`wifi_sw.h` の既定値に依らず製品の形を試験できる) |

### 12.2 設計 §22.12(H-X1〜H-X11)・§22.13 の対応表

| ID | 内容 | ホストの試験(場所) | 判定 |
|---|---|---|---|
| H-X1 ①〜⑩ | `wx_parse_http`(実測の応答・chunked・気温の形・異常・`current_units`・状態 301/429/500/404・途中切れ・`Date:` 無し/小文字・ヘッダーの `current`・巨大) | `test_wx_core.c` H-X1-1〜10(S24)。**追加: `test_wx_indep.c` A1・A2(ガードページ。8 種の応答をあらゆる長さで、末尾/先頭を禁止ページに接して解析 + 変異ファズ 60,000 回)** | 検証済み + 追加 |
| H-X2 | `Date:` → 通算秒・うるう年・2100・年末・曜日・不正の拒否 | `test_wx_core.c` H-X2(gmtime との突き合わせ)。**追加: B1(1970〜2099 の全 47,482 日 x 3 時刻を、1 日ずつ数えた暦と比較。`Date:` の往復、JST の繰り上がり)、B2(全月の「日数 + 1」と 0 日を拒否)、A3(`Date:` のガードページ)** | 検証済み + 追加 |
| H-X3 | ソフト時計(経過・32bit の巻き戻り・再同期・未同期・日またぎ) | `test_wx_core.c` H-X3。**追加: C1(乱数 200,000 回を 64bit の計算と比較。基準 ms が 32bit の端)、C2(23:59:50 に同期 → 日・月・年・曜日が進む。うるう日)** | 検証済み + 追加 |
| H-X4 | WMO 0〜99 と範囲外 | `test_wx_core.c` H-X4 | 検証済み |
| H-X5 ①〜⑬ | `wifi_core` の WX step(正常・`+IPD` の分割・1 バイトずつ・`SEND OK` より先・本文の中の `OK`/`CLOSED`/`+IPD,`・CIPDINFO の形・失敗各種・`CLOSED` 無し + 解析成功・429・超過 `err=7`・`BUSY` の渡し直し・範囲・既存 14 step の回帰) | `test_wifi_core.c` H-X5-1〜14(S22)。**追加: E1(乱数 3,000 走行 x 40 秒。トークン列 + 長い +IPD。範囲・固まらない・送信は既知の 5 種だけ・終了後に本文バッファが 0)** | 検証済み + 追加 |
| H-X6 | 秘密・本文・`Date:` がログに出ない | `test_wifi_core.c` H-W6・H-X5、`test_wifi_at.c`。**追加: E1(`SECRETBODY`・`Date`・`current`・SSID・パスワードがログに 0 件。quiet 有無の両方)** | 検証済み + 追加 |
| H-X7 | 予定表(3 秒後・10 分・更新要求・5 秒・busy/off・失敗の 20 秒・再接続・60/120/300・429 の 300 秒・巻き戻り・二重開始なし) | `test_wx_core.c` H-X7(S24)、`test_wifi_at.c` P2〜P7(S26)。**追加: D1(3 時間 + 24 時間の乱数走行を、時刻の起点 0 と 0xFFFF0000 で行い、行動の列が完全一致。要求の返値・二重開始・範囲外の完了通知の無視・時間の規則を検査)、D2(OFF)** | 検証済み + 追加 |
| H-X8 | UI の描画(要素の位置・全画素・スライド・矩形の外が変わらない) | `test_ui_wx.c` X3a〜k(S25)。**追加: `test_ui_geom.c` G2(日付 12x31x7・気温 -99.9〜99.9・更新時刻 00:00〜23:59 の外接矩形を字形表から計算し、隣の要素と重ならないこと)、G3a(スプライトの継ぎ目)、G3b(区別)、G3c(更新アイコンが 90 度ずつの回転)** | 検証済み + 追加 |
| H-X9 | UI の論理(`clk_ext`・`UI_CMD_WX`・`h_wx` の `BTN_ACT`・当たり判定・デモの台本) | `test_ui_wx.c` X1〜X4、`test_ui_logic.c`、`test_app_main.c` M10・M22・M23(S11・S14・S26)。**追加: G1(全ボタンの当たり判定が画面内・見た目 ⊂ 当たり判定・同じ画面で重ならない)** | 検証済み + 追加 |
| H-X10 | 生成物(`--check`・誤差・ROM) | S4。**追加: `test_wx_palette_indep.py`(独立の再計測。12.4)** | 検証済み + 追加 |
| H-X11 | 静的検査(標準 C・`APP_WX=0` で空) | S22 | 検証済み |
| X0 | 専用モードで TCP の実機確認 | ホストでは不可 | 実機へ |
| X1・X2 | 製品の起動 → 接続 → 最初の取得、値の範囲、`drop=0` | ホストは台本の応答のみ(S26 P2) | 実機へ |
| X3 | 時刻同期の精度(PC との差 ±3 秒)、`WX_SYNC_BIAS_MS` | ホストは計算の正しさのみ | 実機へ |
| X4 | PC の `curl` との照合 | — | 実機へ |
| X5 | 更新ボタン(`x`・実タッチ・当たり判定の境界) | UART と仮想タップはホストで検証。境界は S25 X2e | 実機へ(実タッチ) |
| X6 | 定期取得 10 分 | ホストで検証(D1)。実時間は実機 | 実機へ |
| X7 | 実機の表示面とホスト描画の画素比較(gdb) | — | 実機へ(デバッグ担当) |
| X8 | OFFLINE・エラー(AP を止める) | ホストで検証(S26 P7・P10)。実際の AP 断は実機 | 実機へ(ユーザー協力) |
| X9・X10 | デモ 33 秒の回帰・`[perf]` の目標 | ホストの結果は 12.4(b)。`[perf]` の時間は実機のみ | 実機へ |
| X11 | 受信の余裕(`[err] code=10` が出ない) | ホストは 64 バイト/周回の上限のみ(S23 C1) | 実機へ |
| X12 | Headless ビルド・ROM・`.bss`・`-Wstack-usage=100` | ホストでは不可(ホストの GCC の警告 0 のみ) | ビルド担当 |
| X13 | 受信ログに SSID・パスワード・本文が出ない | ホストで検証(E1、H-W6)。実機のログの照合は別 | 実機へ(一致数を報告) |

### 12.3 追加・変更したファイル(`tests/` と本書のみ)

| ファイル | 内容 |
|---|---|
| `tests/host/test_wx_indep.c`(新規) | A ガードページ(Windows は `VirtualAlloc`、他は `mmap`)、B 全日の暦、C ソフト時計、D 予定表の乱数走行(起点 0 / 0xFFFF0000)、E `wifi_core` の乱数入力。120,008 checks、約 1 秒 |
| `tests/host/test_ui_geom.c`(新規) | G1 当たり判定、G2 配置、G3 スプライト。121 checks |
| `tests/host/test_wx_palette_indep.py`(新規) | パレット再生成後の誤差の独立再計測(12.4) |
| `tests/host/run.sh` | S27 を追加。S17 に `-DAPP_WX=1` の `trace_ui` を同じ判定に通す手順を追加 |
| `tests/host/test_app_main.c` | 偽の `wx_log_state` の行の書式を正しい書式に(12.1) |
| `docs/test-report.md` | 本章 |

### 12.4 独立した確認

**(a) パレット再生成後の誤差**(`test_wx_palette_indep.py`。誤差の計算は自前。`gen_ui_assets.py` は理想の画像と色の表を作るためだけに使用。比べる相手は作業ツリーの `ui_bg_*.c`・`ui_layout.c`・`ui_gfx_data.c`)

| 項目 | 基準 | 実測 | 判定 |
|---|---|---|---|
| 背景 7 枚の不一致の合計 | ≤ 0.3% | 2,078 / 913,920 = **0.227%** | 合格 |
| 背景 1 枚の最大(home) | ≤ 1.0% | **0.666%**(他は 0.106〜0.236%) | 合格 |
| 背景の最大誤差 | ≤ 2 段 | **2 段**(home の 19 画素だけ 1 段超。2 段超は 0) | 合格 |
| スプライト 16 枚の最大 | ≤ 3 段 | **3 段**(partly・snow・thunder)。他は 2 段以下 | 合格 |
| 数字 ramp(混色の理想との差) | ≤ 3 段 | **2 段** | 合格 |
| バー ramp | ≤ 3 段 | 3 段 | 合格 |
| 押下の remap(暗転 / 白光。256 x 2 を重み付き最近傍で再計算) | 最近傍 | 最近傍でないもの 0 個 | 合格 |
| 新しい色 5 つ・名前つきの色 30 個 | パレットに厳密に有る | 30 / 30 | 合格 |
| パレットの使用 | ≤ 256 | 245 / 256(背景・スプライト・ramp のみ) | 合格 |

P1 との描画比較(`test_p2b_indep.py` I5): 静的 + スライドの合計 0.376〜0.425%、2 段超 0.0001%、最大 3 段、押下の最大 6 段(S21 PASS)。コーディング担当の `gen_ui_assets.py --check`(背景 0.227%、スプライト最大 3 段)と一致した。

**(b) 既存の動作の回帰**: `test_app_main.c` M1〜M21(起動順・`[hb]`・デモ 33 秒・`[ev]` 36 行・押下・スライド・32bit 折り返し)が **`APP_WX=0` と `APP_WX=1` の両方で合格**(S11・S26)。`trace_ui`(UT10〜UT19 相当)を `judge_ui.py` に通した結果が `APP_WX=0` と `APP_WX=1` の両方で合格(S17)。`[perf]` の時間(`home_us` ≤ 1,400 など)は実機のみ(ホストでは行の書式だけ)。

### 12.5 コードレビュー(読むだけ。不具合は報告のみ)

| # | 重大度 | 場所 | 指摘 |
|---|---|---|---|
| R1 | **中**(設計との食い違い・承認が要る) | `tests/host/judge_p2b.py:31`、`tests/host/test_p2b_indep.py`(I5)、`tools/gen_ui_assets.py:301` | **合格基準が緩められている**: 静的の不一致の上限 0.5% → **0.7%**(23 状態の実測 0.587%)。`SPRITE_W` を設計 §22.9 の 50 から **20** に変更(掃引の記録あり)。P1 との比較は日付・天気の行・Wi-Fi の矩形をマスク。設計担当の承認と §22.9 の更新が無い。**独立再計測(12.4)の背景・スプライトの基準(H-D1)は守られている**ので、判断は設計担当・ユーザー。状態ごとの上限 1.0% に対し、ホームのデモ表示は 0.926% |
| R2 | 低 | `src/app/wx_core.c:681-684` | 更新要求は RETRY 中(429 の 300 秒待ちを含む)なら**直ちに再接続**する(設計 §22.7 のとおり)。429 の最中に押すと API をまた叩く。意図どおりなら対処不要 |
| R3 | 低 | `src/app/wifi_core.c:1293-1305`(`PH_SEND` / `PH_SEND_DATA`) | `ops->send` が `BUSY` を返し続けると、その step に**タイムアウトが無い**。`r_sci_rx` の BUSY は送信中だけなので実機では詰まらない見込み。詰まっても、メインループは止まらない |
| R4 | 低 | `src/app/wifi_at.c`(`act_end`) | 接続の失敗は `[wx] res=fail err=8` が出るが、`wx_mux` の失敗(続行扱い)は何も出ない。取得は `wx_tcp` 以降の結果行で分かるので影響なし |
| R5 | 低(使い勝手) | `test_ui_geom.c` G2 の参考値 | 更新時刻の文字の右端 x=232 が、更新ボタンの当たり判定の左端 x=228 に 4 画素入る。文字の右端を触っても更新になる。見た目・他のボタンとの重なりは無し |
| R6 | 情報 | `wx_core.c`・`wifi_core.c` | 範囲外書込み・読み出し: `wx_buf` 1536 は `ipd_byte` が超過分を捨てて `F_OVER`。E1 の 3,000 走行で `wx_len ≤ 1536`・`ipd_left ≤ 2048`・`llen ≤ 160` を毎バイト検査して違反 0。`wx_parse_*` はガードページで 1 バイトも範囲外を読まない(A1〜A3)。ms の折り返しは差で扱い(D1 で起点 0xFFFF0000 と完全一致)、気温 ×10 は ±600 に制限、描画は 999 に丸め。**指摘なし** |
| R7 | 情報 | `wifi_at.c` `sci_cb` | 割り込み(SCI1)は `s_rx_ev_val` / `s_rx_ev_seq`(volatile)だけを書き、受信バイトは `r_sci_rx` のキューをメインが読む。ログのキューはメインループ専用。競合は見当たらない。メインループをブロックする処理は無い(受信は 1 周回 64 バイトまで) |
| R8 | 情報 | `wifi_at.c`(`AT_HAVE_SECRET`)・`wx_init` | `wifi_secret.h` が無い・SCI1 を開けない → `wx_sched_init(..., false)` で OFF(EN にも触れない)。S26 P9・P10 で検証 |
| R9 | 情報 | `src/app/ui_logic.c` `ui_cmd` | `APP_WX=0` のビルドでも更新ボタンは描かれ、押すと `[ev] btn=h_wx` が出るが何も起きない(設計どおり) |
| R10 | 情報 | `wx_core.c` `wx_fmt_state` 等 | `-Wstack-usage=100` はホストで検証できない。ビルド担当の結果を確認 |

設計との食い違い: R1 のほか見つからなかった(`UI_BTN_H_WX` = 16、当たり判定 (228,136,60,52)、見た目 (248,158,28,28)、Wi-Fi (112,7)、天気 (20,158)、更新アイコン (252,162)、日付 x=16、気温の右端 106、更新時刻 x=196 は G1・G2 で一致)。

### 12.6 プレビュー PNG の所見(Read で画像として確認)

| 画像 | 所見 |
|---|---|
| `mock_home_c8.png` | 異常なし。日付「11月25日(水)」・Wi-Fi・「12.6℃」・「東京 更新 13:24」・更新ボタンが読める。色の崩れ・文字の欠けなし |
| `wx_gallery.png`(ホストの C 描画 21 状態) | 天気 8 種が区別できる。Wi-Fi 4 状態(灰 + 斜線 / 細い青 / 青 / 赤 + 「!」)、更新アイコンの 4 回転、未取得(`--.-` `--:--`)、古い値(灰色の気温)、負の気温(`-1.2` `-10.5`)、旧時計(11月21日(土))が描けている。**各タイルの右上の灰色の半円は、ギャラリーの切り出しに「デモ」ボタンの左端が写ったもの**(製品の画面の欠陥ではない) |
| `icons_x4.png` | 16 枚の形は自然。雲の濃淡・雨の斜線・雪の点・稲妻・「?」が判別できる。Wi-Fi の `conn`(弧 2 本)と `on`(弧 3 本)の差はやや小さいが、色と弧の数で区別できる(G3b の最小差 51 画素) |

`states.png`・`palette.png`・`compare_report.md`・`error_table.md` は、数値を 12.4 で再計測して一致を確認した(画像の目視は上の 3 枚)。

### 12.7 実機に回す項目(デバッグ担当・ユーザーへ)

X0〜X13(12.2 の「実機へ」)。特に: **X0(専用モードで ESP32 の TCP の書式 = RW-X1)を最初に**。X3(`WX_SYNC_BIAS_MS` の調整)、X7(表示面の 0 バイト差。`s_ui.wx` を gdb で注入)、X9・X10(`[perf]` と `uf`。デモ中に定期取得が重なる)、X5 の実タッチ(R5)。ユーザー確認: R1(合格基準の緩和の承認)、Open-Meteo の利用条件(設計 §22.17)。

### 12.8 再実行

| 項目 | コマンド |
|---|---|
| 全部 | `bash tests/host/run.sh`(約 1 分) |
| 今回の追加分だけ | `bash tests/host/run.sh S27 S17`(S17 は `APP_WX=1` の trace を含む) |
