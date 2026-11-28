# 実装メモ(コーディング担当)

作成: 2026-11-19 / 対象: `docs/design.md` v1 / 状態: **未ビルド・未実機(動作確認済みのものは無い)**

**2026-11-21: 製品デモ v2(`docs/design.md` v2)の実装は、末尾の「付記(2026-11-21 製品デモ v2 P1 の実装)」が現行。上の §2・§3・§6 は v1(画像ビューア)の記録。**

## 0. 結論(まず読む)

| 項目 | 内容 |
|---|---|
| ビルド | **まだしていない**(ユーザーPCの e² studio で行う)。SC作業(`r_sci_iic_rx` 追加、SCI2設定)は 22:27 に済み。「SC作業が済むまでビルド不可」の制約は**無い** |
| **未接続の1点** | `src/rx72n_gcc.c` の `main()` に **User code コメントが無い**ため、設計 §10 の規則どおり**編集していない**(差し戻し)。下の §1 のパッチを当てるまで `app_main()` は呼ばれない |
| ホストでの確認 | 標準Cの部分は `gcc 11.4 -std=gnu99 -Wall -Wextra -Wpedantic -Wconversion` で**警告なしにコンパイル**。ハード依存部は、FITの実ヘッダ + 仮の `platform.h`(/tmp、プロジェクト外)で `-fsyntax-only` が通った。**RX用GCCでのコンパイルではない** |
| 動作確認 | 無し。受入基準の B 系はすべて未確認 |

## 1. 差し戻し: `src/rx72n_gcc.c` の `main()`

現在の `main()` は空で、`/* Start user code ... */` の対が無い(SC生成ファイル)。設計 §10 に従い、勝手に編集していない。
次のどちらかを設計役/ユーザーが決める。

```c
/* 当てるパッチ案(3行) */
#include "r_smc_entry.h"
#include "app/app_main.h"      /* 追加 */

void main(void)
{
    app_main();                /* 追加(戻らない) */
}
```
- `app/app_main.h` は `src/rx72n_gcc.c` からの相対パスで解決される(インクルードパス追加は不要)。
- SC は既存の main ファイルを上書きしない、と思われるが**未確認**。再生成で消えたら同じ3行を戻す。

## 2. ファイル一覧

| ファイル | 層 | 役割 |
|---|---|---|
| `src/app/board_pins.h` | ハード依存(定義のみ) | ピン名・極性・待ち時間・I2Cアドレス・`TOUCH_X_MAX` |
| `src/app/nav_logic.h/.c` | **非依存**(標準Cのみ) | タッチ標本 → 画像番号(状態機械) |
| `src/app/touch_decode.h/.c` | **非依存**(標準Cのみ) | I2C生5バイト → `nav_sample_t` |
| `src/app/touch_ft5x.h/.c` | ハード依存 | RST制御、`r_sci_iic_rx`(CH6)での読み出し、アドレススキャン |
| `src/app/lcd_panel.h/.c` | ハード依存 | GPIO、`glcdc_cfg_t`、GLCDC起動、画像コピー |
| `src/app/images.h/.c` | データ | 画像テーブル `g_images[]` (`APP_IMAGE_COUNT = 4`) |
| `src/app/img_00.c` 〜 `img_03.c` | データ(生成物) | 合成テストパターン3枚 + 添付画像スライド1枚(各 130,560 画素) |
| `src/app/app_debug.h/.c` | ハード依存(薄い) | `g_app_dbg`、任意のUARTログ(既定は無効) |
| `src/app/app_main.h/.c` | 結合 | 初期化とメインループ |
| `tools/prepare_slide.py` | ツール | 任意画像 → `480x272` 不透明 RGB PNG(アスペクト比維持・透明合成・中央配置) |
| `tools/assets/slide_03_480x272.png` | アセット(生成物) | 4枚目スライドの中間画像(`480x272` RGB PNG) |
| `tools/img2c.py` | ツール | 画像 → RGB565 Cソース。`--testpattern` で合成パターン生成 |
| `docs/implementation-notes.md` | 文書 | このファイル |

## 3. 公開関数

| 関数 | 内容 |
|---|---|
| `void nav_init(nav_t*, uint8_t image_count, uint16_t x_max)` | 初期化(画像0、IDLE) |
| `bool nav_update(nav_t*, const nav_sample_t*, uint32_t now_ms)` | 画像番号が変わったら true |
| `uint8_t nav_index(const nav_t*)` | 現在の画像番号 |
| `bool touch_decode(const uint8_t raw[5], nav_sample_t*)` | 点数 > 5 と NULL は false(`*out` は「触れていない」) |
| `bool touch_init(void)` | RST → 解除 → 約300ms待ち → `R_SCI_IIC_Open`(CH6) |
| `bool touch_read(nav_sample_t*)` | 0x02から5バイト読む。最大約20msで打ち切り。失敗は false |
| `bool lcd_init(void)` | 設計 §8.2 の順序どおり(下記) |
| `void lcd_show(const uint16_t*)` | 261,120バイトを `0x00800000` へ `memcpy` |
| `void app_main(void)` | 戻らない |
| `g_app_dbg`(`volatile app_dbg_t`) | デバッガで見る変数。`err_code`、生座標、`found_addr[]` など |
| `APP_LOG(...)` | 既定は何もしない |

`lcd_init` の順: GPIO(BL消灯/DISP無効/タッチRST=アクティブ)→ `lcd_show(g_images[0])` → `R_GLCDC_PinSet()` → `R_GLCDC_Open` → `START_DISPLAY` → DISP有効 → 20ms → BL点灯。

## 4. FITのAPI名の確認元(推測で書いていない)

| モジュール | 確認元 |
|---|---|
| r_glcdc_rx 1.62 | `src/smc_gen/r_glcdc_rx/r_glcdc_rx_if.h`(型・列挙・`R_GLCDC_Open/Control`)と `src/r_glcdc_private.c`(`R_GLCDC_Open` のパラメータ検査: 64バイト境界、offset 960 = 64の倍数 など)。MODE=0 なのでガンマ表 `g_glcdc_gamma_table_*` は公開されない |
| r_gpio_rx 5.31 | `r_gpio_rx_if.h`(`R_GPIO_PinWrite`、`R_GPIO_PinDirectionSet`、`GPIO_DIRECTION_OUTPUT`、`GPIO_LEVEL_*`)、`targets/rx72n/r_gpio_rx72n.h` の144ピン側(`GPIO_PORT_6_PIN_6/7`、`GPIO_PORT_B_PIN_3`) |
| r_cmt_rx 5.81 | `r_cmt_rx_if.h`: `bool R_CMT_CreatePeriodic(uint32_t hz, void (*cb)(void *), uint32_t *ch)` |
| r_sci_iic_rx 2.91 | `src/smc_gen/r_sci_iic_rx/r_sci_iic_rx_if.h`(`R_SCI_IIC_Open/MasterReceive/Close`、`sci_iic_info_t` のフィールド、`SCI_IIC_FINISH/NACK/ERROR`、コールバック型 `void (*)(void)`)。設定は `r_sci_iic_rx_config.h`(CH6 のみ有効、384kbps) |
| r_sci_rx 5.61(ログ用) | `r_sci_rx_if.h`(`R_SCI_Open/Send`、`sci_cfg_t.async.*`)、`r_sci_rx_pinset.h`(`R_SCI_PinSet_SCI2`) |
| r_bsp | `R_BSP_SoftwareDelay`、`BSP_DELAY_MILLISECS/MICROSECS`(`r_bsp_common.h`) |

**WebFetch で補った点**(`raw.githubusercontent.com/renesas/rx-driver-package/master/source/r_sci_iic_rx/.../r_sci_iic_rx.c` の要約。ファイル全文は読んでいない): 7ビットアドレスはシフトしない / `cnt1st == 0` は単純受信、`cnt1st > 0` は「1st送信 → 再スタート → 2nd受信」/ 終了時にドライバが停止条件を出す / `dev_sts` は完了・NACKとも**コールバックより前**に設定される / 情報構造体のポインタはドライバが保持する(→ `s_iic` を `static` にしている)/ コールバックが NULL だと `INVALID_ARG`。ビルド後の実機で要確認。

## 5. LCD端子: `R_Pins_Create()` と `R_GLCDC_PinSet()`(指示された確認)

| 事実(`grep` で確認) | 結果 |
|---|---|
| `Pin.c` の `R_Pins_Create()` | LCD端子20本(CLK, TCON0/2/3, DATA0〜15)に加え SCI1/SCI2/SCI6 の端子も設定する |
| `R_Pins_Create()` の**呼び出し元** | **無い**。`src/` 全体で定義(`Pin.c`)と宣言(`Pin.h`、`r_smc_entry.h` が include)だけ。`R_Systeminit()`(`r_cg_hardware_setup.c`)は `R_CGC_Create` と `R_Interrupt_Create` しか呼ばない |
| `R_GLCDC_PinSet()` の呼び出し元 | `r_qe_display_rx` の `r_qe_display_lcdc.c` だけ(QE for Display を使うときのみ) |
| 結論 | **アプリが呼ばないと LCD 端子は未設定のまま**。`lcd_init()` が `R_GLCDC_PinSet()` を呼ぶ。`R_Pins_Create()` は**呼ばない**(SCI1/2 の TXD は PMR を後で立てる前提のコメント付きで、アプリの流れに合わないため。SCI6 はドライバが `SCI_IIC_CFG_PORT_SETTING_PROCESSING = 1` で自分で設定する) |
| ログ用 SCI2 の端子 | `r_sci_rx` は端子を設定しない。`APP_LOG_ENABLE = 1` のとき `app_debug.c` が `R_SCI_PinSet_SCI2()` を呼ぶ |

## 6. 設計から外れた点・設計への補足

| # | 内容 | 理由 |
|---|---|---|
| 1 | `src/rx72n_gcc.c` を**未編集**(§1) | User code コメントが無い。設計 §10 の差し戻し規則 |
| 2 | `lcd_init()` が `g_images[0]` を内部でコピーする(`lcd_panel.c` が `images.h` に依存) | 設計 §8.2 の順序(画像コピー → 表示開始)を、`lcd_init(void)` のシグネチャのまま守るため |
| 3 | ガンマ・明るさ・コントラスト補正を**すべて無効**にした | FIT既定は有効だが、MODE=0 ではガンマ表が公開されず使えない。無効 = 直線特性。色味が既定と少し違う可能性(B2で確認) |
| 4 | `g_app_dbg` に `err_detail`、`i2c_fail_total`、`x_min/x_max/y_min/y_max` を追加 | B8(四隅を触って範囲を読む)を楽にするため。設計の項目は全て含む |
| 5 | メインループは「フラグ」ではなく**10msカウンタの変化**で周期を取る | フラグのクリア競合を避けるため。動作は同じ |
| 6 | `touch_decode` の「範囲外」は**点数 > 5 のみ**を異常とした。X/Y は12bitマスクのみで範囲検査なし | 座標の範囲が未確認(設計 §9.2)。H8 の「範囲外」の解釈 |
| 7 | 左右判定は設計どおり `x < x_max/2`(整数除算)。`x_max = 479` なら X ≤ 238 が左、239 以上が右 | 設計の文言どおり。中央1画素のずれは許容 |
| 8 | 300ms の間隔は「前回の**動作**から今回の**押下確定**まで」で測る | 設計 §11.1 の「動作後 300ms 以内の押下確定は無視」の字義どおり |
| 9 | 1画像が1枚のとき、動作しても番号が変わらないので `nav_update` は false | 「画像番号が変わったら true」 |
| 10 | タッチICは**FT5206**(回路図)。コードの仮定(FT5x06系、アドレス 0x38)は設計のまま | 回路図を優先。データシート未読 |

## 7. 気づいた不整合(要確認)

- `docs/schematic-findings.md` §3 は `P67_BACKLIGHT_ENA` を**ピン80**と書く。設計 §3 はピン98。ピン80は LCD-CLK(PB5)のはずなので、ピン番号は誤記の可能性が高い(ポート名 P67 は両者一致。コードはポート名で書いたので影響なし)。
- RAM2(`0x00800000`)を使うためのモジュールストップ解除等が必要かは、`r_bsp` を grep して**見つからなかった**(ユーザーズマニュアル未読)。B1 で画が出ない場合の疑い先の一つ。

## 8. 未実装・未確認

| 区分 | 内容 |
|---|---|
| 未実装 | 2面化、UARTの受信、画像の縦横比変換(`img2c.py` は 480x272 以外を拒否) |
| 未確認 | RX用GCCでのビルド / `R_GLCDC_Open` が成功するか / RAM2からの表示 / タッチのアドレス・レジスタ配置 / RST極性と待ち時間 / バックライト・DISPの極性と順序 / `touch_read` の所要時間 / I2Cアドレススキャンの安全性(各アドレスに1バイト受信を試す) |
| 既知の制約 | `touch_init` は約310msブロックする。`touch_read` は最大約20msブロックする(10ms周期を超えることがあり、その周期は次のループでまとめて1回になる) |

## 9. 使い方メモ

| やりたいこと | 方法 |
|---|---|
| 自分の画像に差し替え | `python3 tools/img2c.py my.png --index 0`(Pillow が必要。480x272 のみ) |
| 合成パターンを作り直す | `python3 tools/img2c.py --testpattern`(標準ライブラリのみ) |
| UARTログ | **既定で有効**(2026-11-20〜)。`APP_LOG_SCI_CH = 2` = CN8、115200bps 8N1。止めるなら `app_debug.h` の `APP_LOG_ENABLE` を 0 に。形式は末尾の「付記(2026-11-20 UARTログ)」 |
| 極性・待ち時間・I2Cアドレス・`TOUCH_X_MAX` の変更 | `src/app/board_pins.h` |
| 操作を「どこでも次へ」にする | `nav_logic.h` の `NAV_MODE` を `NAV_MODE_NEXT_ONLY` にする |

## 付記(2026-11-19 22:40 主セッション)

- §1 のパッチ(`#include "app/app_main.h"` と `app_main();`)を `src/rx72n_gcc.c` に**適用済み**(ユーザーの依頼「プログラムを完成させる」の範囲)。SCが再生成で main ファイルを上書きしたら、同じ2行を戻す。

## 付記(2026-11-20 UARTログ: `docs/design-uart.md` v1 に合わせた実装)

**状態: 実装済み。ホストの構文検査と純粋関数の手元確認のみ。RX用GCCでのビルドと実機動作は未確認。** 仕様の正は `docs/design-uart.md`(本節はその実装メモ)。

### 変更ファイル(`src/app/`)
| ファイル | 内容 |
|---|---|
| `app_logfmt.h/.c`(新規) | 行の整形(`applog_fmt_boot/init/state/scan/ev/err/help`)、`applog_cmd_parse`、`applog_due`、`applog_err_changed`。標準Cのみ。`APPLOG_LINE_MAX = 192`。失敗は 0 を返し `buf[0]='\0'` |
| `app_logq.h/.c`(新規) | 8行の行キュー(`applog_q_*`)と破棄カウンタ(飽和)。標準Cのみ。メインループ専用 |
| `nav_logic.h/.c` | `bool nav_step(nav_t*, int8_t dir)` を追加。±1の循環計算を `nav_move()` に一本化(`nav_act` も使う)。`nav_update` の挙動・300ms規則・状態機械は不変 |
| `app_debug.h/.c` | `APP_LOG_ENABLE` 既定 1。`app_log_printf` と `APP_LOG(...)` は**廃止**。`app_log_init/line/poll/getc/drops/rx_event` を提供(`APP_LOG_ENABLE=0` では同名の空マクロ)。`APP_ERR_UART_RX (7u)` を追加。SCI割り込み優先度を **2**(`R_SCI_Open` の引数。SC生成物は未編集) |
| `app_main.c` | 設計 §4.2 の順序。`apply_image()`(タッチ・UART共通)。`[init]` 4段 + `[boot]`。`[err]` (A)(B)(C)。1 tick に1文字受信 |

### 出力形式(すべて `\r\n` 終わり、設計 §2.2 のとおり)
- `[boot] fw=rx72n_gcc build=<__DATE__ __TIME__ の空白を_に> ch=2 baud=115200`
- `[init] stage=lcd|touch|timer|loop ok=0|1 err=<n> detail=<n>`
- `[hb] tick=<n> img=<n> err=<n> detail=<n> pts=<n> x=<n> y=<n> i2c_fail=<n> streak=<n> found=<n> drop=<n>`(100 tick = 1秒ごと)
- `[st] ...`(`[hb]` と同じ11項目 + ` xmin= xmax= ymin= ymax=`)、`found>0` なら続けて `[scan] n=<n> a0=0x38 ...`
- `[ev] img=<N> src=touch|uart`、`[err] code=<n> detail=<n>`、`[help] n=next p=prev s=state ?=help`
- ホストで確認した最長: `[hb]` 144、`[st]` 188 バイト(設計の計算と一致)。

### 受信コマンド
`n`(次)、`p`(前)、`s`(状態)、`?`(ヘルプ)。他は無視(大文字・改行も)。メインループで **1 tick に1文字** だけ `R_SCI_Receive`。`n/p` は `nav_step()` → `apply_image()`(`lcd_show` → `g_app_dbg.image_index` → `[ev]`)。`nav->index` の直接書込みはしない。

### 実装上の判断(設計に書かれていなかった点)
| 内容 | 理由 |
|---|---|
| コールバックは `s_rx_ev_val` を書いてから `s_rx_ev_seq` を +1 するだけ。`app_log_rx_event()` は seq の差で未読を判定(割り込み禁止なし) | 1書き手・1読み手でロック不要。`stub platform.h`(S7 B)に割り込み禁止関数が無い |
| (B) `[err] code=7` は1秒に1行まで。制限中の事象は読まずに保留し、次に出せるとき最新値を出す | 設計 §2.4 の「1秒に1行まで」 |
| (A) の最初の1行は時間制限なしで出す | 起動時失敗(`[init] ok=0` 直後)を遅らせない |
| 整形関数は `snprintf` を値3個ずつに分割。`applog_fmt_boot` は局所バッファ無し | `-Wstack-usage=100` 対策(RXでは r1〜r4 以外の引数がスタック渡し) |
| ログ用バッファ・`nav_t`・`nav_sample_t` は `static`(`app_main.c`)、行キューは `app_debug.c` の `static` | 同上。メインループ専用で、割り込みから使わない |
| `R_SCI_Open` 失敗時は `app_log_line` が何もしない(`drop` も数えない) | 設計 §2.5 |

### 未確認・既知の点
- **未ビルド**(RX用GCC)。スタック警告の解消は、ビルド担当の再ビルドで確認してほしい(ホストx86の `-Wstack-usage=100` では `app_logfmt.c`/`app_logq.c`/`nav_logic.c` は警告なし。RXの値とは別)。
- 実機: `[boot]`/`[hb]` が出るか、受信、優先度2での動作は未確認。
- `tools/job-runner.ps1` の `serial` はまだ受信専用(設計 §6.1、デバッグ担当の作業)。
- `tests/host/` に純粋関数のテスト(設計 §5.4)は未追加(テスト担当)。`run.sh` は無変更で PASS=7 FAIL=0。

## 付記(2026-11-20 4枚目スライド `img_03` 追加: `docs/design.md` §12 に合わせた更新)

- **状態**: コード・アセット生成済み。**RX用GCCでのビルドおよび実機表示は未実施**(`img_03.c` 追加のため、ビルドは `Headless` モードが必須)。
- **新規**:
  - `tools/prepare_slide.py`: Pillow (`LANCZOS` + `alpha_composite`) で任意サイズ画像を `480x272` 不透明 RGB PNG へ変換(既定背景 `#000000`、`--bg` で変更可)。`tools/img2c.py` の非 `480x272` 終了コード 2 は無変更。
  - `tools/assets/slide_03_480x272.png`: `tools/assets/slide_03_src.png` (`251x305` RGBA) → `224x272` を黒背景中央 `(128, 0)` に合成した中間 RGB PNG。
  - `src/app/img_03.c`: `python tools/img2c.py tools/assets/slide_03_480x272.png --index 3` で生成(`const uint16_t g_img_03[APP_IMG_PIXELS] __attribute__((aligned(4)))`, 130,560画素)。
- **更新**:
  - `src/app/images.h`: `APP_IMAGE_COUNT` を `(4u)` に変更、`g_img_03` の `extern` 宣言を追加。
  - `src/app/images.c`: `g_images[]` の index 3 に `g_img_03` を追加。
  - `nav_logic.c` / `app_main.c` / `lcd_panel.c` は `APP_IMAGE_COUNT` 参照のため無変更(`tests/host/` の 4枚対応はテスト担当の範囲)。

## 付記(2026-11-21 製品デモ v2 P1 の実装: `docs/design.md` v2 §1〜§12、`docs/design-uart.md` §10)

**状態: 実装済み。ホストの試験(下)と、RX 用 GCC での個別コンパイル(`-c`、警告なし)まで。ヘッドレスビルド・リンク・実機は未実施。**
コーディング担当(サブエージェント)の実装。設計の変更はしていない(差分は下の表)。

### 1. 作ったもの(`src/app/`)

| ファイル | 層 | 役割 |
|---|---|---|
| `gfx.h/.c` | 標準C | `gfx_blit565` / `gfx_a8`(5bit 濃度、565 のまま混色)/ `gfx_fillrect`。全部「画面の外へ書かない」(int32 の加算あふれもなし) |
| `ui_logic.h/.c` | 標準C | 状態機械。タッチ(2標本で押下確定・3標本で離し確定・2点は無視・NULL は不変)、押下表示(PRESS 最短 90ms → 実行 → FLASH 60ms、3秒で強制実行)、画面遷移 250ms、時間で進む値(式は関数で公開)、デモ台本(`DEMO_SCRIPT`)、UART コマンド、イベント待ち行列 8、表示キー |
| `ui_render.h/.c` | 標準C | `ui_render(ui, fb)`: 背景 → 数字 → バー → 押下/白光 → デモ中タグ。遷移中は古い画面と新しい画面を平行移動して描く |
| `ui_assets.h`、`ui_layout.c`、`ui_gfx_data.c`、`ui_bg_*.c`(7) | **生成物** | `tools/gen_ui_assets.py` が出力。手で編集しない |
| `lcd_panel.h/.c` | ハード依存 | 2面化: `lcd_fb(i)`、`lcd_flip(i)`、`lcd_flip_busy()`(`GLCDC.GR2VEN.BIT.VEN`)、`lcd_underflow_poll()`。`lcd_show` と画像コピーは削除。`gr2uf_detect = true` |
| `app_main.c` | 結合 | 設計 §9.3 どおり。起動時に `ui_init` → 面0に描画 → `lcd_init`。ループ: 反映待ち確認 → 10ms ごとに `[hb]`/`[err]`/UART1文字/`touch_read`/`ui_update`/`[ev]` → 描画(裏面 → 入れ替え)。1秒ごとに `lcd_underflow_poll`。`[perf]`(`APP_PERF_BENCH`) |
| `app_logfmt.h/.c`、`app_debug.h/.c`、`board_pins.h` | — | design-uart.md §10.6 / §10.2。`APPLOG_LINE_MAX = 256`、`[hb]` の `scr demo frm uf`、`[ev]` 各種、`[ui]`、`[perf]`、新しい `[help]`、コマンド `d + -`、`APP_ERR_LCD_FLIP (8)`、`LCD_FB0/FB1_ADDR`、`TOUCH_Y_MAX`、`TOUCH_SWAP_XY/FLIP_X/FLIP_Y`、`APP_PERF_BENCH/N` |
| `tools/gen_ui_assets.py` | ツール | Pillow。背景7枚(日本語は焼き込み)、数字 A8(9フォント x 13文字)、ボタン形 A8 8種、バー画像、タグを、座標表1か所から生成。`--check`(ファイルは書かない)、`--out-dir`、`--preview-dir`、`--font`。再実行で同一出力 |
| `tools/job-runner.ps1`、`docs/cli-debug.md` | — | `serial` の `tx` 許可リストを `[npsd+?x-]` に(設計 §10.7)。**ランナーの再起動が要る(未検証)** |
| 削除 | — | `images.c/.h`、`img_00..03.c`、`tests/host/test_images.c`。`nav_logic`・`touch_decode`・`img2c.py`・`prepare_slide.py` は残した(P2 用) |

使い方: `python tools/gen_ui_assets.py`(生成。約 2 秒)/ `python tools/gen_ui_assets.py --check`。見た目の確認用 PNG は `tools/assets/bath/preview/`(`bg_*.png` = 焼き込み背景、`all_backgrounds.png` = 一覧、`digits_sample.png`、`tag_demo.png`、`fw_*.png` = **ファームと同じ C コードで描いた画面**。`bash tests/host/run.sh S15` で作り直す)。

### 2. 設計との差分(理由つき)

| # | 内容 | 理由 |
|---|---|---|
| 1 | `lcd_flip` の戻り値を `bool` でなく `lcd_flip_t`(OK / BUSY / FAIL) | 設計 §7.3「INVALID_UPDATE_TIMING は再試行、他は記録して描画を止める」を、呼び出し側が区別するため |
| 2 | UART の `n` `p` は、画面遷移中・デモ中に加え、**押下表示中(PRESS)も無視** | 指を置いたまま `n` で画面が変わると、離した時にそのボタンの動作が別の画面で走るため(軽微) |
| 3 | UART `d` で別の画面から開始した時の `[ev]` の順は `demo=start` → `scr=home from=<元> src=uart` | 設計は中断時の順(`demo=abort` → `scr=`)だけ書いてある。開始も同じ順にそろえた |
| 4 | タッチ座標の SWAP: 生 X 軸が画面の縦(0..271)、生 Y 軸が画面の横(0..479)になる。`x_max`/`y_max` は各生軸の最大値のまま | 設計 §4.2 は式だけで、SWAP 時の最大値の対応が未記載。V0 で向きを決めるとき、この定義に合わせて `TOUCH_X_MAX/Y_MAX` を入れる |
| 5 | `[perf]` の測定後に画面状態を作り直す(`ui_init`)ので、起動直後にもう1回描く(`frm=1` から始まる) | 測定で `ui_t` を書き換えるため。静止中は増えない |
| 6 | `applog_fmt_ui` は引数が多いので構造体 `applog_ui_t` を渡す | RX は r1〜r4 以外をスタック渡しで、`-Wstack-usage=100` の余裕のため |
| 7 | `▶`(10x12)と`■`(9x9)は **字形でなく図形**として描く。フォントの欠け検査の対象は、焼き込む全文字 + `－＋℃・` | 設計 §1.2 / §1.7 の寸法指定どおり。フォントに依存しない |
| 8 | ヘッダー右の「浴槽 ■■℃ ・ 保温中」: 接尾辞を右端 464 に置き、数字スロットは `464 − 接尾辞の幅 − 2 x 数字の送り幅` | 設計 §2.2 の「右端から逆算」。接頭辞は空白幅を引いた位置に右寄せ |
| 9 | `tests/host/test_img2c.py` / `test_prepare_slide.py`: `src/app/img_NN.c` との一致検査は、ファイルがある時だけ実行(無ければ skip) | 設計 §11 で `img_NN.c` を削除するため。ツール自体の試験は変更なし |
| 10 | `tests/host/run.sh`: S4 を画面アセット用に置換、S13〜S15 を追加、S12(sanitizer)は libasan が無い環境で SKIP、S1 の `nm` 検査は MinGW のセクション名シンボルを除外 | この PC(MinGW gcc 13.2、`<regex.h>` と libasan が無い)で動かすため。`test_logfmt.c` の正規表現は自前のテンプレート照合に置換 |
| 11 | `tests/stubs/platform.h` に `GLCDC.GR2VEN` の最小の偽物を追加 | `lcd_panel.c` が `lcd_flip_busy` でレジスタを読むため(S7 の B 系の構文検査用) |

### 3. ホスト試験(`bash tests/host/run.sh`、2026-11-21 実行)

結果: **PASS=14 FAIL=0 SKIP=1**(SKIP = S12 sanitizer。この PC の MinGW gcc に libasan / libubsan が無くリンクできない。`tests/host/last-run.log` に全文)。

| ステップ | 内容 | 件数(checks) |
|---|---|---|
| S1 | 標準Cの7ファイルの静的条件(gnu99 / c99 -pedantic-errors / c11、警告ゼロ、`platform.h` 非依存、データシンボルなし) | — |
| S2 / S3 / S9 | `nav_logic`(H1〜H7)、`touch_decode`、`logq`、`nav_step` | 245+196 / 135 / 154+589 |
| S4 | **H-A1/H-A2**: `--check` OK、再実行で同一、コミット済み生成物と一致、10ファイルが警告なしでコンパイル、`.rdata` 合計 2,037,568 バイト(見積 ±30% 内)、背景は 261,120 バイト x 7、RAM に入るデータなし | — |
| S5 | `img2c.py` / `prepare_slide.py`(2 件 skip) | — |
| S6 | `lcd_panel`: 順序、2面の番地、`lcd_flip` の OK/BUSY/FAIL、`lcd_flip_busy`、`lcd_underflow_poll`、実 FIT のパラメータ検査(面0・面1 とも通る) | 153 |
| S7 | 実 `platform.h` / 仮 `platform.h` での `-fsyntax-only`(log 0/1/2、perf 0) | — |
| S8 | `app_logfmt`(全行の文字列・境界・小バッファ。`[hb]` 最長 178、`[st]` 最長 222、`[ui]` 最長 96) | 14,384 |
| S10 | `app_debug` のログ部 | 121 |
| S11 | `app_main` 結合(起動順、`[hb]`、コマンド、`[ev]`、デモ全走 36 本、実タッチ中断、描画と入れ替え、BUSY/FAIL、`uf`、`[err]`、遅い回線、各種失敗、tick 折り返し) | 362 |
| S13 | **H-G1/H-G2**: `gfx_a8` の精度(基準値との差 ≤ 2、実測最大 2)・切り取り | 37 |
| S14 | **H-U1〜H-U7**: 全ボタン、式、押下表示の時間割、遷移、デモ台本(36 本・時刻 ±10ms)、中断、UART コマンド | 343 |
| S15 | **H-G3**: 全画素を書く(600 + 250 組合せ)、スライドが平行移動と一致、押下の暗転・白光、バー幅、棒の高さ。PPM → `fw_*.png` | 38 |

### 4. 実測値

| 項目 | 値 |
|---|---|
| 生成データの ROM(rx-elf-gcc `-c`、リンク前) | 背景 261,120 x 7 = 1,827,840 / `ui_gfx_data` 208,645(数字 A8 103,867、形 53,824、バー 45,406、タグ 4,032 ほか)/ `ui_layout` 700 = **2,037,185 バイト**(設計の見積 約 2,034,000 とほぼ一致) |
| 新しいコード(rx-elf-gcc `-Og -c` の `.text`) | `gfx` 585、`ui_logic` 3,056、`ui_render` 1,868、`app_main` 2,367(旧 1,129)、`app_logfmt` 2,857 ほか。**新規 + 増分で約 7 KB**(設計の見積 30,000 は大きすぎ) |
| ROM 合計の見積 | 約 2.04 MB データ + 約 0.07 MB コード + 既存ライブラリ ≒ **2.1 MB(4 MB の 約 51%)**。リンク後の実測は未(ビルド担当) |
| `.bss` の増加 | `app_main` +223、`app_debug` +512(行キュー 8 x 256)= **約 +735 バイト**(基準 ≤ 1 KB) |
| 33 秒のデモの描画要求 | 表示キーの変化 699 回 / 入れ替え 614 回(模擬の垂直同期 17ms のとき)。設計の見積 200〜600(UD2)より多い。実機で `frm` を測って更新 |
| RX 用 GCC | `rx-elf-gcc 14.2 -Og -Wall -Wextra -Wstack-usage=100 -misa=v3` で、変更・追加した全 .c(`ui_bg_*` 含む)が **警告 0**(個別の `-c`。ヘッドレスビルドではない) |

### 5. 未実施・リスク

- ヘッドレスビルド・リンク・書込み・実機は未実施。V0(タッチの向き)→ `board_pins.h` の `TOUCH_*` 決定が最初。
- 1コマ描画の CPU 時間(R2)、垂直同期の反映時刻(R4)、アンダーフロー(R3)は、`[perf]` と `[hb]` の `uf` で実機で測る(仕組みは入れた)。暗転 52/256 と白光 154/256 は `ui_render.h` の定数 2 個で調整。
- 生成フォントは Noto Sans JP(OFL)。字形・太さはブラウザのモックと完全には一致しない(R7)。見比べはユーザーが `tools/assets/bath/preview/` で行う。
- `tests/host/judge_ut1.py` は旧 `[hb]` の形式のまま(テスト担当が UT10〜UT19 用に更新する)。
- S12(sanitizer)は未実施。Linux / WSL があれば再実行を推奨。
- 追い焚きの残り分(2桁右詰め)は、1桁の時に「あと約」との間が1桁分空く(設計どおり)。

## 付記(2026-11-21 差し戻し D1〜D3 の修正: `docs/test-report.md` §10)

**状態: 修正済み(`src/app/ui_logic.c` のみ)。ホスト試験と RX 用 GCC の個別 `-c`(警告 0)まで。ビルド・書込み・実機は未実施(ビルド担当が再ビルドする)。**

| 指摘 | 原因 | 修正(関数) |
|---|---|---|
| D1(中断で PRESS が残る) | `demo_abort` が `vt_on` を消すため、仮想押下を離す人がいなくなり、3秒後の強制実行で中断済みのボタンが動作した | `demo_abort`: `fb_phase = NONE`、`fb_btn = NONE` を常に実行(押下の暗転も消える) |
| D2(`+` の直後に `d`) | 同じ。`demo_start` が `vt_on` を消す | `demo_start`: PRESS 中なら取り消す。**FLASH は残す**(ホームの「デモ」ボタン自身の白光を切らないため) |
| D3(戻るスライド中の中断) | `home_instant` が「ホーム以外の時だけ」処理していた(戻るスライド中は既にホーム) | `home_instant`: 常に `tr_on = 0`(進行中のスライドを取り消す) |

- 設計との食い違い: なし。設計 §6.1(中断は即ホーム・アニメーションなし)に合わせた。
- `tests/host/test_ui_extra.c`: KNOWN-DEFECT の枠(`XCHECK`、`STRICT_XFAIL`)を削除し、D1a〜D1g / D2a / D3a を通常の `CHECK` にした。`STRICT_XFAIL=1` でも通る。`run.sh` S16 の説明文も更新。
- D4(`frm` の見積)と O1〜O4 は指示どおり今回は触っていない。

ホスト試験(`bash tests/host/run.sh`、修正後): **PASS=16 FAIL=0 SKIP=1**(SKIP = S12 sanitizer、libasan が無い環境)。S14 `test_ui_logic` 343 checks、S16 `test_ui_extra` 11,999 checks、S11 `app_main` 362 checks はいずれも 0 failed。`STRICT_XFAIL=1 bash tests/host/run.sh S16` も PASS。全文は `tests/host/last-run.log`。

## 付記(2026-11-21 実機検証の差し戻し: 起動時の `uf=1` を数えない)

**状態: 修正済み(`src/app/app_main.c` のみ)。ホスト試験と RX 用 GCC の個別 `-c`(警告 0)まで。ビルド・書込み・実機は未実施。**

| 項目 | 内容 |
|---|---|
| 現象 | 実機 8 回のリセットすべてで、`[hb] tick=100 uf=0` → `tick=200 uf=1`、以後増えない。`lcd_init` 直後に `GLCDC.STMON = 0x4`(GR2UF が既に立っている)。`[perf]` は原因ではない(デバッグ担当の切り分け) |
| 原因(推定) | GLCDC の表示開始時に1回検出される。最初の `lcd_underflow_poll`(tick 100)がそれを数えていた。映像への影響は未確認 |
| 修正 | `app_main`(332行): `lcd_init` 成功の直後に `lcd_underflow_poll()` を1回呼び、**結果を捨てる**(`uf` に足さない)。`lcd_init` 失敗時は呼ばない。他の挙動は変えない |
| 効果 | 起動後の `uf` 増分が 0 になる(合格条件にできる)。起動後に本当に検出されれば、従来どおり `uf` に数える |
| テスト | `test_app_main.c` に M12e を追加(起動時の poll だけ 1 を返す偽物で、`uf` が全 `[hb]` で 0、`g_app_dbg.uf` = 0、poll 回数 = 起動 1 + 毎秒 10。`lcd_init` 失敗時は起動時の poll なし)。M12d の poll 回数を 10 → 11 に更新 |
| 結果 | `bash tests/host/run.sh`: **PASS=16 FAIL=0 SKIP=1**(S12 sanitizer は libasan が無く SKIP)。S11 `app_main` 380 checks、0 failed。`tests/host/last-run.log` に全文 |

## 付記(2026-11-21 CLUT8 表示の実機実験 E1〜E4: `docs/design.md` §19 / `docs/design-uart.md` §11)

**状態: 実装済み(ホスト試験と RX 用 GCC の個別 `-c` まで)。ヘッドレスビルド・書込み・実機は未実施(ビルド担当・デバッグ担当)。製品の画面は変えていない(`APP_CLUT8_TEST = 0` が既定)。**

### 1. 実装した内容

| ファイル | 内容 |
|---|---|
| `src/app/clut8_sw.h`(新規。標準C) | **ビルドスイッチ `APP_CLUT8_TEST`(既定 0)**。`board_pins.h` が include する。切り替えはここの値、または `-DAPP_CLUT8_TEST=1` |
| `src/app/board_pins.h`(変更。マクロだけ) | `#include "clut8_sw.h"`、`LCD_C8_STRIDE`(512)、`LCD_C8_FB_BYTES`(139,264)、`LCD_C8_FB_A_ADDR`(= `LCD_FB0_ADDR`。`#ifndef` 付き = ホスト試験の差し替え用)、`LCD_C8_FB_B_ADDR`(= A + 139,264 = `0x00822000`) |
| `src/app/clut8_pat.h/.c`(新規。標準C。FIT を include しない) | `c8pat_face(fb, b)`(512 x 272 の面。b = true で 255 − v)、`c8pat_pal(pal, b, marker_green)`、`c8pat_fmt_clut(...)`(`[clut]` 行。スタックを使わないよう snprintf を3回に分割)。箱・格子などの位置は `C8_*` マクロ。`APP_CLUT8_TEST = 0` の時は**中身が消える**(下の差分 1) |
| `src/app/clut8_test.h/.c`(新規。ハード依存) | `clut8_test_patch_cfg(glcdc_cfg_t *)`(GR2 の p_base / format = CLUT8 / offset = 512 / clut = {true, パレットA, 0, 256} の4か所)、`clut8_test_main()`(戻らない)。段階 0〜5 の時刻表は設計 §19.3.4 のとおり(`if (phase == k && elapsed >= T)` の形)。`flip` = VEN を先読み → `R_GLCDC_ClutUpdate_NoReflect`(start 0、size 256)→ 直後に `R_GLCDC_BufferChange`(失敗したら BufferChange だけ最大 1000 回再試行)。`settle` = VEN = 0 を待つ(最大 約 100ms)→ flip(A, パレットA)→ VEN = 0 を待つ。コマンドは `n p d s ?` だけ(`+ -` は無視)。`lcd_init` 失敗時は段階を動かさず `[hb]` だけ出す |
| `src/app/lcd_panel.c`(変更) | `#if APP_CLUT8_TEST` で `#include "clut8_test.h"` と、`lcd_build_cfg` の直後の `clut8_test_patch_cfg(&s_cfg);` の2か所 |
| `src/app/app_main.c`(変更) | `#if APP_CLUT8_TEST` で `#include "clut8_test.h"` と、`app_main()` の先頭の `clut8_test_main();` の2か所 |
| `tools/clut8_ref.c`、`tools/c8png.py` | 基準データ5個の生成(`gcc -DAPP_CLUT8_TEST=1 -I src/app tools/clut8_ref.c src/app/clut8_pat.c -o jobs/c8/clut8_ref.exe`)。`c8png.py png / cmp / cmppal / sample`(設計 §19.4.3 のとおり。終了コード 0 = 一致、1 = 違い、2 = 誤り)。`png` は面Aとパレットから作った PNG が、面Bとパレットから作った PNG と**画素で一致**することを確認済み(`jobs/c8/ref/` に生成。git 管理外) |
| `tests/host/test_clut8_pat.c`、`tests/host/test_clut8_cfg.c`、`run.sh` の **S18** | H-C8-1〜4・7(`test_clut8_pat.c`)、H-C8-5(`run.sh` の grep と nm)、H-C8-6 と段階 0〜5 の模擬走行(`test_clut8_cfg.c`)。**S17 は既に `judge_ui` で使われているので S18** |

### 2. 設計との差分(理由つき)

| # | 差分 | 理由 |
|---|---|---|
| 1 | **スイッチの定義場所**: 設計は `board_pins.h` に `#ifndef APP_CLUT8_TEST` だった。実装は **`clut8_sw.h`(新規)に置き、`board_pins.h` から include**。値を変えるのは `clut8_sw.h` | `clut8_pat.c`(標準C。FIT・`board_pins.h` を include できない)にも `#if` を掛けたかった。ヘッドレスビルドのリンク行(`HardwareDebug/makefile`)に **`--gc-sections` が無い**ので、未使用の `clut8_pat.c`(`.text` 1,259 バイト)が 0 でも ROM に残り、「0 の時は ROM の差 0」が成り立たない。`clut8_pat.c` / `clut8_test.c` の中身を 0 で消した。ホスト試験は `-DAPP_CLUT8_TEST=1` で作る |
| 2 | **面Aの階段の idx 16 と 255 は画面に出ない** | 設計 §19.2 の順(6 枠が 2 階段を上書き)のとおりに実装した結果、階段の左端 x = 0,1(idx 16)と右端 x = 478,479(idx 255)が白い枠に隠れる。出る idx は 17〜254(238 個)。**255 は面Bの背景(255 − 0)で読まれる**。16 は面A・面Bとも階段としては読まれない(面B では idx 239 の位置)。判定に影響は小さい。直すなら設計役(階段の x 範囲か、枠の順序) |
| 3 | H-C8-6 を別ファイル `test_clut8_cfg.c` に分けた | `lcd_panel.c` + `clut8_test.c` を一緒にコンパイルする試験なので、標準Cだけの `test_clut8_pat.c` とリンク条件が違う |
| 4 | `[help]` の文字列は `clut8_test.c` の定数(`c8pat_fmt_*` にしていない) | 設計 §11.3 の文字列 1 個だけなので関数にする意味が無い。R-HELPC に一致 |
| 5 | `[hb]` の `frm` = `BufferChange` が成功した回数(段階2の `commit` も数える) | 設計 §11.4 の「BufferChange を呼んだ回数」 |
| 6 | 段階3: ループの 1 周で、負荷(1面 34,816 語の書き戻し)1 回 + `flip` の機会 1 回 | 設計は「ループの空き時間は負荷」。1面の書き戻しは約 1〜2 ms(△)なので、垂直同期(16.8 ms)ごとの flip を逃さない |
| 7 | **`g_c8_mde`**(新しい `volatile uint32_t`)に `OFSM.MDE[2:0]` を起動時に読んで入れる | 追加の指示(下の §3) |
| 8 | `s_rc`(`[clut]` の `rc`)は「その出来事で最初に出た 0 でない `glcdc_err_t`」。段階3は段階の始めから `stress_end` までの最初の値 | 段階3の途中で `[clut]` を出さないため |

### 3. `OFSM.MDE`(画素の並びの前提。コーディング担当への追加指示 2026-11-21)

**結論: 現行の設定で `OFSM.MDE[2:0]` = 111b(リトルエンディアン)になる(ソースで確認。実機は未読)。設計との差分なし。**

| 確認 | 内容 |
|---|---|
| FIT(`r_glcdc_rx` と `src\smc_gen\r_glcdc_rx\src\*`)が `OFSM` / `MDE` を書くか | **書かない**(`OFSM`・`MDE` で grep 一致なし)。GLCDC の画素の並びは MCU の**エンディアン選択(オプション設定メモリ `MDE`、フラッシュの `0xFE7F5D00`)**で決まる |
| `MDE` の中身を決める所 | `src/smc_gen/r_bsp/mcu/rx72n/vecttbl.c`: `BSP_PRV_MDE_VALUE` = `__BIG` が定義されていれば `0xfffffff8`、そうでなければ `0xffffffff`(リトル)。`__BIG` は `r_rx_compiler.h` が `__RX_BIG_ENDIAN__` の時だけ定義。ビルドは `-mlittle-endian-data`(`HardwareDebug/src/subdir.mk`)なので **リトル → `0xFFFFFFFF`**。`BSP_CFG_CODE_FLASH_BANK_MODE` = 1(リニア)→ `BSP_PRV_BANK_MODE_VALUE` = `0xffffffff`。`__ofsm_sec_ofs1` の先頭 = `(0xffffffff & 0xffffffff)`。`src/linker_script.ld` が `.ofs1` を `0xFE7F5D00` に置く |
| 期待値 | `MDE` = `0xFFFFFFFF`。`MDE[2:0]` = 111b(ハード側の意味: 画素 n がアドレス +0、n+1 が +1)。`BANKMD`(bit 6:4)= 111b |
| 実験コードでの確認 | `g_c8_mde`(`volatile uint32_t`、`clut8_test.c`)に `OFSM.MDE.BIT.MDE` を起動時に入れる。期待 7。ホスト模擬走行で `g_c8_mde == 7`(偽の OFSM に `0xFFFFFFFF` を置いた場合)を確認 |
| デバッグ担当が読む値(§19.4.1 の表に加える) | **`OFSM.MDE`**: `x/wx 0xFE7F5D00` が `0xFFFFFFFF`(bit 2:0 = 7)。または `print g_c8_mde` = 7。7 以外なら、面の作り方(バイト順)を見直す(設計 §19.8 C13) |

### 4. 実行したこと

| 項目 | 結果 |
|---|---|
| `bash tests/host/run.sh`(全部) | **PASS=16 FAIL=1 SKIP=1**。S18(新規)PASS(`test_clut8_pat` 11,528 checks / `test_clut8_cfg` 223 checks、いずれも 0 failed)。S12 = SKIP(libasan が無い。合格ではない)。**S4 = FAIL(今回の変更とは無関係)**: `gen_ui_assets.py` を再実行すると、コミット済みの `ui_bg_*.c` `ui_layout.c` `ui_assets.h` と出力が違う(10 ファイル)。今回は `tools/gen_ui_assets.py` も `ui_*` も触っていない(`git status` で変更なし)。原因は未調査(この PC の Pillow / フォント環境の変化を疑う。要確認 = テスト担当) |
| 試験の効き | 模擬走行で、段階1の `flip` で `pal` に常に パレットA を渡すように壊すと「面とパレットの組み違い 180 回」で S18 が失敗することを確認(壊したコードは戻した) |
| `rx-elf-gcc -Og -Wall -Wextra -Wstack-usage=100 -misa=v3 -mlittle-endian-data -std=gnu99 -c`(個別) | `clut8_pat.c`・`clut8_test.c`・`lcd_panel.c`・`app_main.c` を `APP_CLUT8_TEST` = 0 と 1 の両方: **警告 0** |
| `APP_CLUT8_TEST = 0` の同一性 | HEAD の `lcd_panel.c` `app_main.c` と今回の 0 の `.o` の逆アセンブルが**完全に一致**(`-d` の `cmp`)。`clut8_pat.o` と `clut8_test.o` は `.text` `.data` `.bss` ともに 0 バイト |
| `APP_CLUT8_TEST = 1` のサイズ(`-c` の `.o`) | `clut8_pat` text 1,259 / `clut8_test` text 2,771・data 2・bss 3,400(パレット 3 x 1,024 + 行バッファ 256 ほか)/ `lcd_panel` 552(+10)/ `app_main` 2,328(+4)。RAM2 は面A・B で 278,528 バイト(`0x00800000`〜`0x00843FFF`) |

### 5. 未実施・リスク

- **ヘッドレスビルド・リンク・書込み・実機は未実施**。新規 .c(`clut8_pat.c` `clut8_test.c`)を足したので、ビルドは **ヘッドレス(makefile の再生成)が必要**。
- ホストの模擬走行は「`SEL` と `GRnFLM2` は VEN = 1 の次の垂直同期で同時に反映される」「使用中のセットへの書込みは即時」という**設計の前提(HW §51.2.9)を仮定したモデル**で試験している。前提そのものは実機でしか確かめられない(E2)。
- `flip` の先読み(`GR2VEN.VEN`)と `R_GLCDC_ClutUpdate_NoReflect` の間に、FIT 側の `r_glcdc_is_register_reflecting`(BGEN.VEN)が真になる経路は想定していない。起きれば `rc = 10` が `[clut]` に出る。
- 段階3の負荷(1面の全語の書き戻し)の実時間・`n`(500〜600)・`uf` は実機で測る。`ev=bench` の `m` は 10ms 刻み。
- GLCDC 起動後に `lcd_underflow_poll` を 10ms ごとに呼ぶ(設計 §19.3.3 のとおり。`lcd_panel.h` の「1秒に1回程度」より多い)。
- 段階の途中の `n` `p` `d`(打ち切り)では、`NoReflect` だけ呼んで `BufferChange` を呼ばないまま段階が切り替わることはない(`settle` が VEN = 0 を待ってから `flip` する)。ただし**段階2・5 の途中で切り替えると** `SEL` が反転したまま(VEN なし)残るので、次の段階の `settle` の `NoReflect` が使用中のセットへ書く(内容は パレットA なので絵は変わらない)。UART の `ev=start` の `sel` で確認できる。


## 付記(2026-11-22 P2b: 製品 UI の CLUT8 化 — `docs/design.md` §20)

**状態: 実装済み(ホスト試験と RX 用 GCC の個別 `-c` まで)。ヘッドレスビルド・書込み・実機・目視は未実施。P2b は P1 を置き換えた(切替スイッチなし。戻す時はコミット `0368fe3`)。`APP_CLUT8_TEST = 0` が既定のまま(実験コードは残してある)。**
**承認の段取り(§20.10 の V-D0)について: ユーザーが離席中で承認を待てないため、承認を待たずにコードまで進めた。プレビュー PNG は出力済み(下の §5)。ユーザーが戻ったら見てもらう。不承認ならパレットか表現を直す(§20.10 の 4)。**

### 1. 実装した内容

| ファイル | 内容 |
|---|---|
| `tools/gen_ui_assets.py`(変更) | §20.3 のパレット生成(numpy。universe → 固定色 → 重み付き k-means → 65,536 通りの最近傍の表)、8bit 化、ramp / remap、`--check`(パレット・固定色・P1 との誤差の自己検査)、C 出力(`ui_bgi_*`、`ui_pal`、`ui_slot_ramp` `ui_slot_alt` `ui_bar_ramp` `ui_remap`、`UI_I_BAR_U` `UI_I_BAR_U_TODAY`)、プレビュー PNG と誤差表(Python の試作描画 `Painter`。ホストの C 描画との比較は S19)。LF で出力。P1 の RGB565 出力は生成しない |
| `src/app/ui_bg_*.c`、`ui_gfx_data.c`、`ui_layout.c`、`ui_assets.h`(**再生成**) | 背景 = `const uint8_t ui_bgi_<名前>[130560]`(480 バイト幅)。バー満タン・タグも `uint8_t`。`ui_slot_t` と `ui_bar_t` の `color` は削除(ramp が持つ)。`UI_C_*`(RGB565 の色マクロ)は削除 |
| `src/app/gfx.h/.c`(追加) | `GFX_STRIDE`(512)、`gfx_blit8` / `gfx_fill8` / `gfx_a8_ramp` / `gfx_remap8`(設計 §20.5 の引数どおり。`gfx_a8_ramp` は `ramp0` が NULL、または `alt >= 0` で `ramp1` が NULL なら何もしない)。RGB565 版(`gfx_blit565` `gfx_a8` `gfx_fillrect`)は残した(基準・ホスト試験用。製品からは呼ばない) |
| `src/app/ui_render.c/.h`(変更) | `ui_render(const ui_t *, uint8_t *fb)`。構造・順序は P1 のまま。`UI_DIM_SCALE` / `UI_FLASH_SCALE` は `UI_REMAP_TH`(128)に置き換え |
| `src/app/lcd_panel.c/.h`、`board_pins.h`(変更) | GR2 = `GLCDC_IN_FORMAT_CLUT8`、`offset` = 512、`hsize` 480、`clut` = `{true, ui_pal, 0, 256}`(Open の時に1回。`ClutUpdate` は呼ばない)。`lcd_fb()` は `uint8_t *`。`LCD_LINE_BYTES` = 512、`LCD_FB_BYTES` = 139,264、`LCD_FB1_ADDR` = `LCD_C8_FB_B_ADDR`(`0x00822000`)。GR1(未使用)の `offset` は 960 のまま(FIT の検査を通る値) |
| `src/app/app_main.c`(変更) | `bench_copy` を `gfx_blit8`(ホーム背景を面へ。`[perf]` の `copy_us` の意味は変えない)に。`#include "gfx.h"` |
| `tools/fb_hostrender.c`(変更) | `ui_render` の 8bpp の面(512 x 272 = 139,264 バイト)を `.c8` に、`--pal <out>` で `ui_pal`(1,024 バイト・リトルエンディアン)を出力 |
| `tools/c8png.py`(追加) | `cmpface <dev_face> <dev_pal> <host_face> <host_pal>`(x < 480 の添字の違い・パネル色の違い・パレットの違いを数え、最初の位置を出す。終了コード 1 = 違いあり)と、関数 `face_diff` |
| `tools/fb2png.py`(追加) | `check <gdb.log> <接頭辞> --c8`: dump した面(139,264 バイト)+ パレット(`<接頭辞>_<段>.pal0.bin` / `.pal1.bin`。ui_pal と一致する側を使う)を、`fb_hostrender` のホスト描画と比較(添字・パネル色・どちらのセットが ui_pal か)。P1 の RGB565 方式は残した。**gdb の実ログでの確認は未実施**(ホスト描画を「実機の dump」に見立てた疑似データで、7 段すべて 0 差になることだけ確認) |
| `tools/gdb/mi-fbdump-c8.gdb`(新規。**未実行**) | `mi-fbdump.gdb`(A_* の 7 段)から機械的に作った 8bpp 用: 面 = `GR2FLM2` から 139,264 バイト、パレット 2 セット(`0xE0800` / `0xE0C00`)、`GR2CLUTINT` を print。B_* / C_* / D_* の各スクリプトも同じ規則(`261120` → `139264`、dump 行の後ろに `.pal0.bin` / `.pal1.bin` の 2 行と `GR2CLUTINT` の print)で作れる |
| `tests/host/judge_p2b.py`(新規)、`tests/host/data/p1_ref.npz`(新規。267,624 バイト) | H-D4。基準 = 実機で読んだ 23 状態を P1 のファームと同じ `ui_render` で描いた RGB565(`jobs/fb/p1/` の `*.state.txt` と `*.host.bin`。`jobs/` は git 管理外なので、圧縮して `tests/host/data/p1_ref.npz` に入れた。作り方: `python tests/host/judge_p2b.py --make-ref jobs/fb/p1 --ref tests/host/data/p1_ref.npz`) |
| `tests/host/run.sh`(変更)、`test_gfx.c`(H-D2 追加)、`test_ui_render.c`(8bpp 化。H-D3)、`test_ui_extra.c`(X13 を 8bpp 化)、`test_lcd_panel.c`(H-D6)、`test_app_main.c`(面の型) | 下の §3 |

### 2. 設計との差分(理由つき)

| # | 差分 | 理由 |
|---|---|---|
| 1 | **承認(V-D0)を待たずにコーディングまで進めた** | ユーザーの指示(離席中に CLUT8 で動くところまで進める)。プレビュー PNG は残した(§5) |
| 2 | **固定色は 24 色**(設計の実測は 39 色)。universe は 778 色(設計 775) | 「1つの画像(背景7枚・バー満タン2枚・タグ)の中で 200 画素以上ある色」を数えた結果。設計の試作とは数え方が少し違うらしく 39 は再現できない(同じ画像の集合で、しきい値 80 なら 37 色、60 なら 56 色)。**結果の統計は設計とほぼ同じ**(背景7枚の不一致 1,605 画素 = 0.176%、1段超 13、最大 2 段。設計は 1,595・0.175%・13・2) ので、設計の「200 画素」の規則のまま採用した。使われないパレットの添字が 12 個ある(中心が重なった分) |
| 3 | 距離の重み | 設計の「R、G、B 0.6 倍」を、R = 1、G = 1(6bit の単位)、B = 0.6 と読んだ。結果が設計の実測に近いので、この読みでよいと判断した |
| 4 | `--check` の合格基準 | 設計 §20.3.1 段7(最大 > 3 段 または 背景 1 枚の不一致 > 1.0% で失敗)ではなく、より厳しい **H-D1 の値**(背景の不一致の合計 <= 0.3%、背景 1 枚 <= 1.0%、最大 <= 2 段)を使った。固定色・`UI_I_BAR_U` `UI_I_BAR_U_TODAY` が厳密かも検査する |
| 5 | **H-D4 の判定の当て方** | 設計の「不一致 <= 0.5%、最大 <= 3 段、2 段超 <= 0.01%(静的・スライド・デモ)/ 2 段超 <= 0.1%、最大 <= 8 段(押下・白光)」は **種類ごとの合計(全状態の画素数に対する割合)** に当てた。ホーム 1 枚だけで背景の不一致が 0.57% あるので、1状態ごとには当てられない。1状態ごとは「不一致 <= 1.0%(H-D1 の背景1枚の上限)かつ最大の段」だけ |
| 6 | `ui_slot_ramp` は `[UI_SL_COUNT][2][33]`(設計のとおり)で 792 バイト | 設計の ROM 見積(約 495)より約 300 バイト多い。`FILL_PCT` 以外の 2つ目の表は未使用(0 埋め) |
| 7 | S4: 作業ツリーとの比較で **CR を無視**(`tr -d '\r'`) | 作業ツリーが `autocrlf` で CRLF になっている一方、ジェネレータは LF で出力する(コードの不具合ではない)。コミットされる中身は LF なので、改行だけの違いで失敗しないようにした。S4 のほかの検査は変えていない |
| 8 | S6 と S18 の試験のリンクに、生成データのオブジェクト(`$UIOBJ/*.o`)を追加 | `lcd_panel.c` が `ui_pal` を参照するようになったため。S18(`APP_CLUT8_TEST = 1`)では `clut8_test_patch_cfg` が同じ項目を上書きするが、参照は残る |
| 9 | S19(新規): H-D4 | `tools/fb_hostrender.c` をビルドして `judge_p2b.py` を実行 |
| 10 | `test_ui_render.c` の「使用量の棒の高さ」: 数え方を「土台から上へ棒の色が続く行数」に変更 | 8bit では、背景の文字の縁の色が `BAR_U` と同じ添字に写ることがあり、列全体を数えると 1 多くなる(P1 の RGB565 では起きなかった)。棒そのものの高さの検査としては同じ意味 |
| 11 | `gfx_a8_ramp` に不正な引数(`ramp0` NULL、`alt >= 0` で `ramp1` NULL)の保護を足した | 防御。設計の「どれも面の外へ書かない」の延長 |
| 12 | `--preview-dir`(`tools/assets/bath/preview`)の `bg_*.png` `all_backgrounds.png` `tag_demo.png` は、**P2b のパネルに出る色**で書く。P1 | P2b | 差分は `--c8-preview-dir`(既定は `--preview-dir` の隣の `preview_p2b`)へ | 設計 §20.8 の `tools/assets/bath/preview/c8_*.png` ではなく、別フォルダにした(ユーザー指示の `preview_p2b\`)。`fw_*.png` は S15(`ppm2png.py`)が P2b の描画で上書きする(従来どおりの動作) |

### 3. ホスト試験

| 項目 | 結果 |
|---|---|
| `bash tests/host/run.sh`(全部。S1〜S19) | **PASS=18 FAIL=0 SKIP=1**。S12 = SKIP(MinGW に libasan / libubsan が無い。**合格ではない**)。S4 は PASS(CR を無視する比較。§2 の 7) |
| H-D1(S4) | `gen_ui_assets.py --check` 成功、再実行で出力が同一、作業ツリーの 10 ファイルと一致(CR 以外)、`ui_bg_*.o` の `.rdata` が各 130,560、生成データの `.rodata` 合計 1,101,171(見積 約 1,098,000 の ±30% 内)。**ramp / remap の「再計算との一致」は、再実行で出力が同一であること(同じコードが同じ表を作る)に留まる**。独立した再計算はしていない |
| H-D2(S13。`test_gfx.c`) | 83 checks、0 failed。`gfx_blit8` / `gfx_fill8` / `gfx_a8_ramp` / `gfx_remap8`: クリップ(負・はみ出し・幅 0・INT32 の端)、面の前後と右の 32 列(480..511)に 1 バイトも書かない、濃度 0..255 の全値で `l = (a + 4) >> 3`、`l = 0` で不変、`alt` の分岐(`alt = 0` も有効)、`th` の境界(127 / 128、0 / 255)、NULL |
| H-D3(S15。`test_ui_render.c`) | 38 checks、0 failed。面を 0x00 と 0xFF で埋めて描き、左 480 列が同一、右の 32 列は触らない。5 画面 x 状態 x 遷移(dx = 0, 1, 120, 240, 479, 480 / 前進・後退)600 通り、値の極端、スライドの平行移動 250 通り |
| H-D4(S19) | **23 状態で不合格 0 件**。静的・スライド・デモ(20 状態): 不一致 0.454%、2段超 4 画素(0.0002%)、最大 3 段。押下・白光(3 状態): 不一致 0.660%、2段超 79 画素(0.0202%)、最大 6 段 |
| H-D5(S1、S11、S14、S16、S17 ほか) | 回帰なし。`gfx.c` / `ui_render.c` は標準C(S1: `platform.h` なし、グローバルデータなし、`-std=c99 -pedantic-errors` / `c11` でも警告 0) |
| H-D6(S6。`test_lcd_panel.c`) | GR2 = CLUT8、offset 512、hsize 480、vsize 272、面0 = `0x00800000`、面1 = `0x00822000`(64 バイト境界)、`clut` = `{true, ui_pal, 0, 256}`。実 FIT の `r_glcdc_open_param_check` を通る。`offset = 513` と `480` は `ALIGNMENT` で弾かれる(対照) |
| S18(`APP_CLUT8_TEST = 1`) | PASS(`test_clut8_cfg` の H-C8-6 を含む。`lcd_panel.c` の CLUT8 設定を実験コードのパッチが上書きしても同じ) |

### 4. 生成物の ROM と色の誤差(実測)

| 項目 | 実測 | 設計の値 |
|---|---|---|
| 生成データ(rx-elf-gcc 14.2 `-c` の `.text`/`.rodata`) | 背景 130,560 x 7 = **913,920** / `ui_gfx_data` **184,157**(数字 A8 103,867、形 53,824、バー満タン 22,464 + キャップ 478、タグ 2,016 ほか)/ `ui_layout` **3,094**(パレット 1,024、ramp / remap、ボタン・スロット表ほか)= **1,101,171 バイト**(P1 の生成データ 2,037,185 から **−936,014**) | 見積 約 1,098,000 |
| コードの増分(`-Og -c` の `.text`) | `gfx` 1,097(P1 の記録 585 から +512。RGB565 版を残したので増えた。リンクで使われなければ `--gc-sections` で消える。ヘッドレスのリンク行に `--gc-sections` が無いと残る)、`ui_render` 1,895(+27)、`app_main` 2,409(+42) | 約 +100 |
| ROM 合計(text + data)の見込み | P1 の 2,106,516 から −936,014 + コード +約 600 = **約 1,171,000**(実測はビルド担当) | 約 1,170,000 |
| 背景7枚の P1 との差(P1 の RGB565 との比較。1段 = RGB565 の1段) | 不一致 **1,605 / 913,920 = 0.176%**、1段超 13、2段超 0、最大 2 段。home 0.550%(最大 2)、fill_run 0.111%、fill_done 0.083%、reheat_run 0.189%(最大 2)、reheat_done 0.114%(最大 2)、usage 0.101%、stby 0.081% | 0.175%、13、最大 2 段 |
| 数字・バー・タグ・押下・白光・スライド(Python の試作描画 + 実機の 23 状態のホスト C 描画) | `preview_p2b/error_table.md`(Python の試作描画: 静的 0.303%、押下 0.608%、白光 0.542%、スライド 0.288%)。C 描画 vs P1(S19)は上の H-D4。押下の角の縁は最大 6 段(設計どおり)。**満タンのバー(432 px)の右端の角 4 画素が 3 段**(`fill_done` と `reheat_done`)。キャップの ramp が「溝の上」を仮定するため(満タンの時だけ、角の下地は溝と BG の混ざった色)。最大 3 段の範囲内 | 静的 0.307%、最大 3 段、押下 最大 6 段 |
| パレット | 256 エントリー(0xFF + RGB565 の 8bit 展開)、固定色 24、使われている添字 244 | — |

### 5. プレビュー PNG(ユーザーに見てもらう。`tools/assets/bath/preview_p2b/`)

`bg_<名前>.png`(背景7枚。**P1 | P2b | 差分**。差分 = 一致 白 / 1段 黄 / 2段 赤 / 3段以上 黒)、`scr_*.png`(ホーム・ホーム+デモタグ・湯はり 3 幅・湯はり完了・追い焚き 2 幅・完了・使用量 2 状態・待機・スライド途中。**数字・バー・使用量の棒・タグ**を含む)、`digits.png`(12 スロットの拡大)、`buttons_press.png` / `buttons_flash.png`(**14 ボタンの押下・白光**の拡大)、`palette.png`(256 色の見本。左上のマゼンタの点 = 固定色)、`error_table.md`(誤差表)、`host/state_*.png`(実機で読んだ 23 状態を **ホストの C 描画**で P2b にしたものと P1 の比較。`judge_p2b.py --out` が出力)。見てほしい所: 文字の縁、ボタンの角(押下)、バーの端、色の段差。再生成: `python tools/gen_ui_assets.py`(`src/app` の生成物も同じ内容で書き直される)。

### 6. 実行したこと(RX 用 GCC)

`rx-elf-gcc 14.2.0.202607 -Og -Wall -Wextra -Wstack-usage=100 -misa=v3 -mlittle-endian-data -std=gnu99 -ffunction-sections -fdata-sections -c`(個別)で、`gfx.c` `ui_render.c` `lcd_panel.c` `app_main.c` `ui_layout.c` `ui_gfx_data.c` `ui_bg_*.c`(7)`clut8_pat.c` `clut8_test.c` を **`APP_CLUT8_TEST` = 0 と 1 の両方: 警告 0**。(PATH の `rx-elf-gcc` は 8.3 なので、14.2 の `D:\Renesas\GNURX\GCC_fo_ Renesas_RX_14.2.0.202607-GNURX-ELF\...\rx-elf-gcc.exe` を使った。)

### 7. 未実施・リスク

- **ヘッドレスビルド・リンク・書込み・実機・LCD の目視・カメラは未実施**(ビルド担当・デバッグ担当・ユーザー)。**新規の .c は無い**(既存の生成物の入れ替え)ので、ビルドは通常のヘッドレスでよい(`tools/gen_ui_assets.py` が出力するファイル名は P1 と同じ)。
- **V-D0(プレビューの承認)が未了**。誤差は設計の実測とほぼ同じだが、見た目の判断はユーザーが行う。
- **R-D1(LCD での色の見え方)**: §19.5 の目視(V1b・V1e)が未実施のまま。色順・右端・画素の並びが外れていると全画面が壊れる。製品の面の右 32 列(480..511)は 0 のまま(= パレットの添字 0 の色。表示されない)。
- **R-D2**: 8bit のバイト書込み(`memcpy` / `memset` と 1 バイトずつの書込み)での `uf` は実機で確認する(U-D1 の UT10・UT13・UT17)。
- 数字の ramp は「下地が単色」の仮定。静的な文字(「約」「分」など)の縁と数字の枠が重なる所は、下地が単色でないため P1 と違う(`FILL_REMAIN` の桁数が 1 を超えると「分」に重なる。実際の値は最大 8 で 1 桁)。
- 数字が隣の字の枠と重なる所の「合成の重ね」は、P1 は混ぜ重ね、P2b は下地の ramp(後の字が前の字の縁を上書き)で、一致しない可能性がある。H-D4 の実測(最大 3 段)では問題が出ていないが、桁の組合せを網羅したものではない。
- 実機の dump(`mi-fbdump-c8.gdb`)と `fb2png.py check --c8` の組合せは**実機では未実行**。
- `tests/host/data/p1_ref.npz`(267,624 バイトのバイナリ)をリポジトリに足した。不要なら S19 とともに外せる。
- S12(sanitizer)は SKIP のまま。


## 付記(2026-11-25 Wi-Fi(ESP32 AT)試作 第1段階: `docs/design.md` §21 / `docs/design-uart.md` §12)

**状態: 第1段階(SC の生成コードに依存しない部分: `wifi_sw.h`・`wifi_core`・ひな形・ホスト試験)をホスト試験と RX 用 GCC の個別 `-c` まで実施。`wifi_at.c`(SCI1・EN・ループ)と `app_main.c` / `board_pins.h` への組み込みは第2段階(SC の再生成後)で未実施。ビルド・書込み・実機は未実施。**

### 1. 実装した内容

| ファイル | 内容 |
|---|---|
| `src/app/wifi_sw.h`(新規) | `APP_WIFI_TEST`(既定 0)、`APP_WIFI_AUTOSTART`(既定 0)、`WIFI_BAUD`(115200u)。`APP_WIFI_TEST` と `APP_CLUT8_TEST` が両方 1 なら `#error`。`clut8_sw.h` と同じ作り |
| `src/app/wifi_core.h/.c`(新規。標準Cのみ) | ① ログ行の整形 `wifi_fmt_step/result/state/val/help`、`wifi_step_name`(秘密を引数に取らない。失敗は 0 で `buf[0] = 0`)② `wifi_build_cwjap`(`" , \` の前に `\`。溢れ・SSID が空・制御文字は 0)③ `wifi_parse_line`(行全体の一致で分類。値は `num` / `num2` / `text`。壊れた行は `OTHER` で `text` も空)④ 状態機械(`wifi_core_init/start/rx/poll`)と観測用 `wifi_core_state/step/bytes`。`APP_WIFI_TEST = 0` では中身が消える(RX 14.2 `-c`: .text 0 バイト)。1 では .text+.rodata 4,242 バイト、データ・bss 0 |
| `src/app/wifi_secret.h.example`(新規) | ひな形。ダミーの値(`EXAMPLE_SSID` / `EXAMPLE_PASSWORD`)と使い方。`wifi_secret.h` は作っていない・読んでいない |
| `src/app/app_debug.h`(追加) | `APP_ERR_WIFI_OPEN`(9u)、`APP_ERR_WIFI_RX`(10u)の定数だけ(設計 §21.10 の 2) |
| `tests/host/test_wifi_core.c`(新規)、`tests/host/run.sh`(S22 を追加) | 下の 3 |

### 2. `wifi_core` のインターフェース(第2段階の `wifi_at.c` 向け)

| 項目 | 内容 |
|---|---|
| 状態 | `static wifi_core_t g;`(約 600 バイト。`wifi_core_t` のメンバーは直接触らない) |
| 初期化 | `wifi_core_init(&g, &ops, WIFI_SSID, WIFI_PASS)`。**`wifi_secret.h` が無い時は `NULL, NULL`**(`join` を `res=skip` にして、`mode` の後で `result=pass`)。文字列はコピーしない(`join` の組み立ての時だけ読む。呼び出し側が保持) |
| `ops`(3 つとも必須) | `en(int level)` = EN ピン(0 = Low、1 = High)/ `send(const char *data, size_t n)` = `\r\n` 込みのコマンド。戻り値 `WIFI_SEND_OK`(キューへ入れた)・`WIFI_SEND_BUSY`(次の poll で同じものを再度渡す。待ち時間に数えない)・`WIFI_SEND_FAIL`(step を `fail code=101`)。`data` は呼び出しの間だけ有効 / `log(const char *line, size_t n)` = 整形済みの 1 行(`\r\n` 込み)→ `app_log_line()` へ。`line` は戻ったら使えない |
| 開始 | `wifi_core_start(&g, WIFI_RANGE_ALL / _BOOT / _AT, now_ms)`(UART の `d` / `n` / `p`)。実行中は何もせず `false`(= コマンドを無視)。`d` `n` `p` の判定は `applog_cmd_parse()`(`d` = DEMO、`n` = NEXT、`p` = PREV、`s` = STATE、`?` = HELP。`+` `-` は無視)で足りる |
| 毎周回 | 受信バイトごとに `wifi_core_rx(&g, b, now_ms)`(1 周回最大 64 バイトは呼び出し側の約束)→ `wifi_core_poll(&g, now_ms)`。`now_ms` = 単調増加の ms(10ms tick x 10 でよい。32bit 一周に対応)。ブロックしない |
| 観測 | `wifi_core_state()`(idle / run / done)、`wifi_core_step()`(0..12 = `[hb]` の `scr`)、`wifi_core_bytes()`(`[hb]` の `frm` と `[wifi] state` の `bytes`)。`s` の応答 = `wifi_fmt_state(buf, size, state, step, bytes)`、`?` = `wifi_fmt_help()` |
| `[init] stage=wifi`、`[hb]`、`[err] code=9/10` | 第2段階。既存の `applog_fmt_init` / `applog_fmt_state` / `applog_fmt_err` を使う |

### 3. 設計との差分(理由つき)

| # | 差分 | 理由 |
|---|---|---|
| 1 | `APP_WIFI_AUTOSTART` の既定 = **0**(設計 §21.4.1 は 1) | ユーザー指示(2026-11-25)。コミットする値で自動開始しない方が安全。試験ビルドは `-DAPP_WIFI_AUTOSTART=1`。設計 §21.4.1 / §21.8 の書き換えは設計役 |
| 2 | `wifi_core.c` も `APP_WIFI_TEST = 0` で中身が消える | ユーザー指示(製品の ROM に差を出さない。リンクに `--gc-sections` が無い)。ホスト試験は `-DAPP_WIFI_TEST=1` でコンパイルする(S18 の `clut8_pat.c` と同じ) |
| 3 | `join` の成功条件は **`OK` だけ**(`WIFI GOT IP` を必須にしない)。設計は「`WIFI GOT IP` の後に `OK`」 | すでに接続済み(ESP32 のフラッシュに前回の AP が残っている、`AT+CWAUTOCONN` の既定 1)で同じ AP に `CWJAP` すると `WIFI GOT IP` が出ない版があるかもしれない(未確認)ので、誤って失敗にしない。接続の裏付けは次の `ip` と `ping` が取る。実機で `WIFI GOT IP` が必ず出ると分かれば、条件を足せる |
| 4 | `ping_ip` / `ping_dns` は **`+PING:<n>` を受けていない `OK` を成功にしない**(`res=fail code=0`) | 設計「`+PING:<n>` の後に `OK`」の文字どおり。W8 の `1 ≦ n` を満たさない成功を作らない |
| 5 | step 終了後の「次へ進む前に行を捨てる」時間: タイムアウトの後 **500ms**、`ERROR` / `FAIL` の後 **300ms**(`at` の再試行の間隔と同じ)。成功の後は 0 | 設計は「500ms 待ち、その間の行は捨てる」と「`at` の失敗の間隔 300ms」。2 つを 1 つの規則にした |
| 6 | 応答の待ち時間は **送信できた時(`WIFI_SEND_OK`)から数える**。`ms` もその時から(`res=start` は 0) | `SCI_ERR_XCVR_BUSY` で待った時間を、応答のタイムアウトに数えないため |
| 7 | 実行中・開始前に受けた行(`reset` 中、`SEND` 待ち中、行を捨てる間)は処理せず捨てる | 設計 §21.4.4「待っていない step では無視」の実装 |
| 8 | `+CIPDOMAIN:` / `+CIPSTA:` の値は **IPv4 の `"a.b.c.d"` だけ**を値として扱う(IPv6・形の違うものは `OTHER` で無視。step の成否には影響しない) | `[wifiv]` の `val` に出すのが IPv4 のみ。`dns` は `OK` で成否が決まる |
| 9 | 秘密の消去: 送信できた直後に `cmd[]` を、行を 1 つ処理するたびに `line[]` を 0 で埋める | エコーバックの `AT+CWJAP="..."` や組み立て済みコマンドが RAM に残らないように(試験: 構造体のバイト列を検索)。**ESP32 の送信キュー・`r_sci_rx` の送信バッファ(`wifi_at.c` 側)・ELF の `.rodata` には残る**(§21.5) |

設計の不足・矛盾で差し戻すものは無い。

### 4. ホスト試験の結果(`bash tests/host/run.sh`)

| 項目 | 結果 |
|---|---|
| 全部(S1〜S22) | **PASS=21 FAIL=0 SKIP=1**。S12 = SKIP(MinGW に libasan / libubsan が無い。**合格ではない**)。既存の S1〜S21 は回帰なし |
| S22(新規。`test_wifi_core.c` 7,331 checks、0 failed + 静的検査) | **H-W1**(行の組み立て: `\r\n` `\n` `\r`・空行・分割・160 バイトちょうど / 161 バイトを捨てて次が壊れない・NUL を含む行を捨てる)、**H-W2**(分類 約 50 行。`+PING:OK` `BOOKS` `OK ` `ready!` などを誤認しない)、**H-W3**(値。壊れた行 24 通り・桁あふれ・引用符なし・IPv4 の範囲外で `OTHER` かつ `text` が空)、**H-W4**(エスケープ、空の SSID・NULL・制御文字、バッファ不足 size = 1..L で 0 と `buf[0] = 0`・ガード無傷、最長 204 バイト)、**H-W5**(① 正常 ② `at` 3 回無応答後に OK ③ 5 回で打ち切り(無応答・ERROR)④ `+CWJAP:2` は再試行なし ⑤ `:4` / `:1` は 1 回再試行、`:3` と無応答は再試行なし ⑥ ping 失敗 ⑦ dns 失敗でも `ping_dns` へ ⑧ `boot` のタイムアウトでも `at` へ ⑨ 勝手に出る行 ⑩ 遅れて来た OK を次の step に数えない ⑪ 範囲指定 ⑫ 実行中の再開始は無視、ほかに秘密なし(`join` skip)・組み立て失敗 `code=100`・送信失敗 `code=101`・送信 BUSY・`s` の状態行)、**H-W6**(目印の SSID・パスワードを全シナリオ(特殊文字入り・エコーあり・`+CWJAP:"ssid",...` 形式の応答・手で流したエコー行を含む)で流し、**全ログに目印・`CWJAP=`・BSSID・生の受信行が出ない**。構造体のバイト列にもパスワードが残らない。検出器が働くことも確認)、**H-W7**(全 step x 全 res x 境界値で書式に適合 = `sscanf` で読んで同じ書式で書き直して一致、最長 79 バイト、無効引数、`val` 41 文字を拒否、サイズ境界)、**H-W8**(`wifi_core.*` と `wifi_sw.h` に FIT・`platform.h`・`board_pins.h` の include なし、データシンボルなし、`-std=c99 -pedantic-errors` / `c11` で警告 0、`APP_WIFI_TEST = 0` で `wifi_core.o` が空、`wifi_secret.h` を include するのは `wifi_at.c` だけ(今は無し)、両スイッチ 1 で `#error`)、**H-W9**(`git check-ignore src/app/wifi_secret.h` 成功、`git ls-files` に無い、`.example` は無視されない) |
| 試験の検出力 | 突然変異で確認: ① `line[]` の消去を外す → 失敗 1 ② `join` の再試行条件を無条件にする → 失敗 4 ③ タイムアウト後の捨てる時間を 0 にする → 失敗 2(試験は検出する。変異は戻してある) |

**範囲外・限界**: 書式は `design-uart.md` §12.3 の正規表現そのものではなく、`sscanf` + 再整形の一致で検査した(MinGW に `<regex.h>` が無い。`test_logfmt.c` と同じ事情)。Python の `re` での検査は未実施。実機の ESP32 の応答の書式は未確認(§21.9 RW11)。

### 5. RX 用 GCC での個別コンパイル

`rx-elf-gcc 14.2.0.202607 -Og -Wall -Wextra -Wstack-usage=100 -misa=v3 -mlittle-endian-data -std=gnu99 -ffunction-sections -fdata-sections -c`(`D:\Renesas\GNURX\GCC_fo_ Renesas_RX_14.2.0.202607-GNURX-ELF\rx-elf\rx-elf\bin\rx-elf-gcc.exe`)で、`wifi_core.c` `app_debug.c` `app_main.c` を **`APP_WIFI_TEST` = 0 と 1 の両方: 警告 0**(スタック使用量の警告も 0)。`wifi_core.o` のサイズ: 0 = 0 バイト、1 = 4,242 バイト。

### 6. 未実施・リスク

- **第2段階が未実施**: `wifi_at.h/.c`(SCI1 の `R_SCI_Open` / 受信 64 バイト上限 / 送信 BUSY の再試行 / EN の `r_gpio_rx` / `wifi_test_main()` / UART コマンド / `[hb]` / `[init] stage=wifi` / `[err] code=9,10` / `wifi_secret.h` の `__has_include`)、`app_main.c` の `#if APP_WIFI_TEST`、`board_pins.h`(EN の端子定義があれば)。SC の再生成の結果(`r_sci_rx_config.h` の SCI1 バッファ、`CTS1#` の消えた pinset)を確認してから。
- **ビルド(ヘッドレス)・リンク・書込み・実機は未実施**。`wifi_core.c` は新規の .c なので、**ヘッドレスビルドの対象に入るか(`.cproject` のソースフォルダ設定)をビルド担当が確認する**(`src/app/` は既存と同じフォルダなので入るはず)。`APP_WIFI_TEST = 0` のビルドでは ROM が変わらないはず(`wifi_core.o` が空)だが、サイズの比較は未実施。
- **ESP32 の AT 応答の実際の書式**(`ready` の有無、`+CIPSTA:` の形、`+PING:` の形、`AT+SYSSTORE` の対応、エコーの有無)は実機で確認する。`wifi_core` の分類を直すだけで済む作り(§21.9 RW10・RW11)。
- `WIFI_BAUD` を `wifi_core` は使わない(第2段階の `R_SCI_Open` が使う)。
- ESP-AT の `+PING:` は、複数行返る版があるかもしれない(未確認)。複数来た場合は最後の値を `ping_ms` に採用し、`[wifiv] key=ping` は来た分だけ出る。
- 状態機械は 1 コマンドずつで、遅れて来る応答の対策は 300 / 500ms の無視時間だけ。それより遅い遅延応答は、次の step の応答と区別できない(設計どおりの限界)。
- コアの `wifi_core_t` は呼び出し側が確保する(約 600 バイト。RAM)。

## 付記(2026-11-25 Wi-Fi(ESP32 AT)試作 第2段階: SCI1・EN・専用モードのメインループ。`docs/design.md` §21.4 / §21.10 の 4)

**状態: ホスト試験(S1〜S23)と RX 用 GCC の個別 `-c` まで実施。ヘッドレスビルド・書込み・実機は未実施。** `APP_WIFI_TEST` の既定は 0 のまま(製品は変わらない)。`wifi_secret.h`(ユーザーが作成済み・git 管理外)は**開いていない・読んでいない・内容を出していない**。コンパイルに使っただけ(`gcc -H` で include されることだけ確認)。

### 1. 実装した内容

| ファイル | 内容 |
|---|---|
| `src/app/wifi_at.h/.c`(新規) | `wifi_test_main()`(戻らない)。**EN(P43)**: 先に `R_GPIO_PinWrite(High)`、次に `R_GPIO_PinDirectionSet(出力)`(ESP32 を止めない。`lcd_panel.c` の RST と同じ順)。**SCI1**: `R_SCI_PinSet_SCI1()` → `R_SCI_Open(1, 非同期, WIFI_BAUD, 8N1, 優先度 2, コールバック)`。**ops**: `en` = EN を Low / High、`send` = `R_SCI_Send`(`SCI_ERR_XCVR_BUSY` → `WIFI_SEND_BUSY`、成功 → OK、その他 → FAIL)、`log` = `app_log_line`。**ループ**(順序は設計 §21.4.6): `app_log_poll` → `R_SCI_Receive` を 1 バイトずつ最大 64 回 → `wifi_core_rx` → `wifi_core_poll`(毎周回。時刻 = 10ms tick x 10)→ tick が進んだ時だけ `g_app_dbg` 更新・`[hb]`・`[err]`・UART コマンド 1 文字・自動開始。**UART コマンド**: `d` = 全部、`n` = reset〜at、`p` = at のみ、`s` = `[wifi] state=` + `[st]`、`?` = `[help]`、他は無視(実行中の d n p も無視)。**`[init] stage=wifi ok=<0|1> err=<0|9> detail=<sci_err_t>`**、Open 失敗なら続けて `[err] code=9 detail=<sci_err_t>`。**`[err] code=10 detail=<sci_cb_evt_t>`**(コールバックが値を書き、メインが 1 秒に 1 行まで)。**`[hb]`**: `scr` = step 番号(`g_app_dbg.scr`)、`frm` = ESP32 から受けたバイト数(`g_app_dbg.frames`)、`demo` `uf` `pts` `x` `y` `i2c_fail` `streak` `found` = 0。**自動開始**: `APP_WIFI_AUTOSTART = 1` で tick 200(約 2 秒)に `d` と同じを 1 回だけ。**秘密**: `__has_include("wifi_secret.h")` で取り込み、`WIFI_SSID` / `WIFI_PASS` を `wifi_core_init` へポインタで渡すだけ。無ければ `NULL, NULL`(join は skip)。ログ・`[err]` に値を出す経路は無い。`APP_WIFI_TEST = 0` では中身が空(RX 14.2 `-c`: 0 バイト) |
| `src/app/app_main.c`(変更) | `#if APP_WIFI_TEST` で `wifi_at.h` を include し、`[boot]` を出した後に `wifi_test_main()` を呼ぶ(2 か所だけ。0 のとき、TEST=0 の `app_main.o` / `app_debug.o` の逆アセンブルは HEAD(コミット済み)と同一) |
| `src/app/board_pins.h`(変更) | `#include "wifi_sw.h"` と `PIN_WIFI_EN`(= `GPIO_PORT_4_PIN_3`。P43、ピン 137)の定義だけ(マクロ。ROM に影響なし) |
| `tests/host/test_wifi_at.c`(新規)、`tests/host/run.sh`(S23 を追加) | 下の 3 |

**P43 を GPIO 出力にできるか(ソースで確認)**: `r_gpio_rx` に `GPIO_PORT_4_PIN_3 = 0x0403` がある(144 ピン)。`Pin.c`・`r_sci_rx_pinset.c`・`mcu_init.c`(`PORT4.PDR` は存在しないピンのマスク 0x00 だけ)のどこも P43 を触っていない = **リセット後の入力のまま**(PMR・MPC の既定は汎用入出力)。`R_GPIO_PinDirectionSet` で出力にして競合する設定は無い。**A/D 兼用端子の挙動(ASEL)の実機確認は未実施**(ハードウェアマニュアル未読)。P31(CTS1#)・P27 は触らない(P31 は SC 再生成で `Pin.c` と `R_SCI_PinSet_SCI1()` から消えているのを確認済み)。

### 2. 設計との差分(理由つき)

| # | 差分 | 理由 |
|---|---|---|
| 1 | 時刻は **10ms tick x 10 の ms**。`wifi_core_poll` は毎周回呼ぶが、時刻は tick 刻み。EN Low の実時間は **約 10〜20ms**(設計 20ms)、各タイムアウトの誤差は最大 10ms | ms タイマを増やさない(最小実装)。ESP32 の EN のリセットに必要なのは数十 µs(Espressif のデータシート、未確認の記憶)。`ready` が出なければ EN を疑い、必要なら `EN_LOW_MS` を伸ばす(`wifi_core.c`) |
| 2 | `[init] stage=wifi` の `err` / `detail` は、`g_app_dbg.err_code` を使わず**ローカルの値**(`APP_ERR_WIFI_OPEN` / `sci_err_t`)。`g_app_dbg.err_code` に書かない(`app_debug.h` のコメントどおり)。`[hb]` の `err=` は Open 失敗でも 0 | 既存コメント「`[err]` 行にだけ出す」に合わせた。`s_open_err`(gdb で読める static)に残す |
| 3 | タイマ(CMT)が作れない時だけ `[init] stage=timer ok=0 err=4 detail=0` と `[err] code=4`、`g_app_dbg.err_code = 4`、止まる。成功時の `[init] stage=timer` / `loop` は出さない | 設計 §12・UW0 は `stage=wifi` だけ。製品の失敗時の作りと同じ |
| 4 | `wifi_secret.h` があるのに `WIFI_SSID` / `WIFI_PASS` が定義されていなければ **`#error`**(黙って join を飛ばさない)。ホスト試験は `WIFI_AT_HOST_TEST` を定義し、実物の `wifi_secret.h` を読まず、試験が目印の値を定義する | ファイルの書き間違いを早く見つける。試験で実物の秘密を使わない |
| 5 | `APP_LOG_ENABLE = 0` または `APP_LOG_SCI_CH != 2` かつ `APP_WIFI_TEST = 1` なら `#error` | SCI1 は ESP32 専用。ログは SCI2(設計 §21.4.1) |
| 6 | `wifi_at.c` は `app_main.c` の `log_state` と同じ組み立てを自前で持つ(約 20 行) | `clut8_test.c` と同じ。共通化はしない(最小変更) |

設計の不足・矛盾で差し戻すものは無い。

### 3. ホスト試験の結果(`bash tests/host/run.sh`)

| 項目 | 結果 |
|---|---|
| 全部(S1〜S23) | **PASS=22 FAIL=0 SKIP=1**。S12 = SKIP(MinGW に libasan / libubsan が無い。**合格ではない**)。既存の S1〜S22 は回帰なし |
| S23(新規) | ① `APP_WIFI_TEST = 0` で `wifi_at.o` が空(実 platform.h + FIT ヘッダ)② `APP_WIFI_TEST = 1` の `app_main.c` と `wifi_at.c` の `-fsyntax-only` 警告 0 ③ **`test_wifi_at.c` を 3 種で実行**: 既定(169 checks、0 failed)/ `-DT_NOSECRET`(72、0)/ `-DAPP_WIFI_AUTOSTART=1`(52、0)④ `WIFI_SSID` / `WIFI_PASS` を参照するのは `wifi_at.c` だけ(`wifi_core.h` は使い方のコメントだけ) |
| `test_wifi_at.c` が見ているもの | **A1** `R_SCI_PinSet_SCI1` が `R_SCI_Open` より前、ch=1・115200・8N1・内部クロック・優先度 2・コールバックあり、EN は「High → 出力」の順、`[init] stage=wifi ok=1 err=0 detail=0`、開始時は何も送らない。**B1** `d` で全部: EN は Low → High(20ms 以上)、偽 ESP32(echo あり、`ready`、join に 3 秒、ping 23 / 31)で `result=pass ping_ip=23 ping_dns=31`、`join` のコマンド文字列が正しく送られる、`[wifiv]` の値、**全ログ行に目印の SSID・パスワード・`CWJAP`・生の受信行が無い**、`[hb]` が join の最中も毎秒出る、`drop=0`・`err=0`、`g_app_dbg.scr` / `frames`。**B2** 送信 BUSY の間は同じコマンドを再度渡し、1 回だけ送る(送信 10 回)。**B3** 無応答 → `at` を 5 回送って `result=fail stage=at`、join は送らない。**B4** `p` は EN を動かさず at だけ、`n` は reset〜at。**B5** 実行中の `d` `n` `p` は無視、`s` `?` の応答、`x` `+` `-` は無視、完了後に `d` でもう一度。**C1** 受信は 1 周回 64 バイト(200 バイトが 64 / 128 / 192 / 200)。**C2** SCI1 の受信異常は `[err] code=10 detail=8` が 1 秒に 1 行まで、`RX_CHAR` は無視、`g_app_dbg.err_code` は 0 のまま。**C3** `R_SCI_Open` 失敗 → `[init] ok=0 err=9 detail=<値>` と `[err] code=9`、d n p は無視、受信しない、EN は動かさない、`[hb]` は続く、`s` は idle。**C4** タイマ失敗。**D1**(自動開始)tick 200 に 1 回だけ、2 秒前は何もしない、失敗で終わっても繰り返さない。**E1**(秘密なし)join は skip、`result=pass`、CWJAP は送らない |
| 試験の検出力 | 突然変異で確認: ① 受信上限を 128 → 失敗 3 ② BUSY を FAIL 扱い → 失敗 2 ③ EN の初期 High を外す → 失敗 5 ④ 割り込み優先度を 3 → 失敗 1 ⑤ `[err]` の detail を 0 → 失敗 1(試験は検出する。変異は戻してある) |

**範囲外・限界**: 偽物は `fake_sci.h`(`r_sci_rx.c` を読んで合わせた)と、この試験の偽 ESP32・偽 GPIO・偽 CMT。**実機の割り込み・実時間・ESP32 の本当の応答・基板の配線・P43 の実際の電気的な挙動は試験できない**。偽の ESP32 の応答の書式は ESP-AT 2.x の記憶と公式ページ(設計 §21.4.3)に基づく(実機で未確認)。

### 4. RX 用 GCC での個別コンパイル

`rx-elf-gcc 14.2.0.202607 -Og -Wall -Wextra -Wstack-usage=100 -misa=v3 -mlittle-endian-data -std=gnu99 -ffunction-sections -fdata-sections -c`(`HardwareDebug/compile_commands.json` の `-I` と同じ)で、`wifi_at.c` `wifi_core.c` `app_main.c` `app_debug.c` を **`APP_WIFI_TEST` = 0 と 1 の両方: 警告 0**(スタック使用量の警告も 0)。`wifi_at.c` は `-DAPP_WIFI_AUTOSTART=1` でも警告 0。`APP_WIFI_TEST = 1` の `wifi_at.c` は実物の `wifi_secret.h` を include して通った。

| オブジェクト | `APP_WIFI_TEST` = 0 | = 1 |
|---|---|---|
| `wifi_at.o` | 0 バイト | text 1,156 / bss 926(`AUTOSTART` = 1 で text 1,196) |
| `wifi_core.o` | 0 バイト | text 4,242 |
| `app_main.o` / `app_debug.o` | **HEAD(コミット済み)と逆アセンブルが完全に同一**。text 2,409 / bss 499 | `app_main.o` は同じ大きさ(`wifi_test_main` への呼び出しが入る) |

### 5. 未実施・リスク

- **ヘッドレスビルド・リンク・書込み・実機は未実施。** 新規の .c(`wifi_at.c`、`wifi_core.c`)があるため、ビルドは**ヘッドレス(makefile を作り直す)**が必要。SC の再生成で `SCI_CFG_CH1_TX_BUFSIZ` 2048 / `RX_BUFSIZ` 3500 が入っているので、**`APP_WIFI_TEST = 0` の製品でも `.bss` が約 5.4KB 増える**(今回の変更ではなく SC 再生成の効果)。
- **ROM が変わらないことの確認**: `APP_WIFI_TEST = 0` のビルドの `.text` / `.rodata` は、**SC 再生成の差(`r_sci_rx` の定数・`Pin.c` の P31)を除けば**変わらないはず。`wifi_at.o` / `wifi_core.o` が 0 バイトであることは上で確認済み。リンク後の map で `wifi_at` `wifi_core` のセクションが 0 バイトであることを確認する。
- 実機で未確認: ESP32 に AT ファームが入っているか(設計 §21.2)、ボーレート、EN(P43)で本当にリセットできるか、`ready` の有無、P43(A/D 兼用)の GPIO 出力、USB 給電での電圧、ESP-AT のフロー制御の既定(P27 = ESP32 の CTS 入力は未接続の入力のまま。フロー制御が有効なファームだと送信が止まりうる)。
- `wifi_secret.h` の値は **ELF・MOT・map の `.rodata` に平文で入る**(設計 §21.5)。`wifi_at.o` には `.rodata.str` が入る。他人に渡さない・dump しない。
- **gdb で `s_core` 全体を表示しない**(join の最中、BUSY で待つ間は `cmd[]` にパスワード入りのコマンドが残る。`ssid` / `pass` は文字列へのポインタで、`x/s` で値が見える)。読むのは `g_app_dbg`・`s_core.bad_lines`・`s_core.rx_bytes` のようにフィールド単位だけ。
- 受信は ESP32 の勝手な長い出力(`+IPD` など)が来る設計ではない(状態機械は 1 コマンドずつ)。受信バッファ 3500 バイトは 64 バイト/周回で十分速く空く(専用モードのループは数 µs 周期。実測は未実施)。

## 付記(2026-11-25 Wi-Fi 試作 第3段階: AT 1.1.3.0 向けの改修。`docs/design.md` §21 冒頭の改訂・§21.4.3〜§21.4.8)

**状態: ホスト試験(S1〜S23)と RX 用 GCC の個別 `-c` まで実施。ヘッドレスビルド・書込み・実機は未実施。** `APP_WIFI_TEST` の既定は 0 のまま。`wifi_secret.h` は開いていない・読んでいない(コンパイルに使っただけ)。新規の .c は無い。

### 1. 変更内容

| ファイル | 内容 |
|---|---|
| `src/app/wifi_core.h` | step を 14 個に(`store` 削除、`mode` の後に `qap` `lapopt` `scan`。`WIFI_STEP_END` = 15、`[hb]` の `scr` は 0〜14)。`wifi_key_t` に `SCAN` `AP` `OTH`(END = 10)。`wifi_line_t` に `WIFI_L_CWLAP`。新規 `wifi_ap_t`、`wifi_parse_cwlap`、`wifi_shape`。`wifi_core_t` に scan 集計(`scan_total/found/near/bad`、最強の AP の `ap_ch/ap_rssi/ap_ecn`、`oth_n`)を追加し、`cwjap_code` を `fail_code` に改名(join の `+CWJAP:<n>` と ping の 200 / 201 を持つ) |
| `src/app/wifi_core.c` | `k_def[]`: `store` 削除、`qap`(`AT+CWQAP` 2000ms)・`lapopt`(`AT+CWLAPOPT=1,31` 1000ms)・`scan`(`AT+CWLAP` 15000ms、2 回、続行)を追加、`ip` を `AT+CIFSR` に。`wifi_parse_line`: `+CIFSR:STAIP,` を `STA_IP`(引用符は任意)、`+CIPDOMAIN:` と `+CIPSTA:*` の引用符を任意に、`+CWLAP:` を `WIFI_L_CWLAP`(中身を見ない・`text` 空)。`wifi_parse_cwlap`: SSID を配列に写さず、読み取り位置(`sit_t`)だけで exact(バイト一致)と near(大文字小文字・空白 0x20 と U+3000・全角 U+FF01〜FF5E をそろえた一致。正規化後 1 文字以上)を判定。`wifi_shape`(英字 a、数字 9、他の 0x21〜0x7E はそのまま、他 `_`、40 文字)。`on_line`: scan 中の `+CWLAP:` を集計して捨てる(各 999 で頭打ち)、`OK` で `key=scan` → `key=ap`(found ≧ 1 の時、rssi 最大の 1 件)→ `step=scan res=ok code=0..3`、分類できなかった行(OTHER。エコー・空行を除く)と読めなかった `+CWLAP:` は `key=oth`(形。1 step に最大 3 行)、`+PING:TIMEOUT` → `code=201`、`+PING:` が来ずに `ERROR` → `code=200`。`end_ok` に `code` 引数。`wifi_at.c` は変更なし(`STORE` の参照が無かった) |
| `tests/host/test_wifi_core.c` | H-W2・H-W3・H-W5・H-W6・H-W7 を 14 step に更新。H-W10(`wifi_parse_cwlap`)、H-W11(scan の集計と秘密)、H-W12(`wifi_shape` と oth)、H-W13(新キーの書式)を追加。目印は `NEIGHBOR_SENTINEL`(他の AP)・`de:ad:be:ef:..`(MAC)・`BADNAME_SENTINEL`・`SSID_SENT_9a1`・`PW_SENTINEL_9f3` のダミーだけ |
| `tests/host/test_wifi_at.c`、`tests/host/run.sh` | 偽 ESP32 に `AT+CWLAP`(自分の AP + 他の AP、MAC 入り)と `AT+CIFSR`、1.1.3.0 の `AT+GMR` を追加。step 28 本・送信 12 回・scan と ap の行・`SYSSTORE` / `CIPSTA` を送らないこと・ログに他の AP 名と MAC が無いことを確認。S22 / S23 の説明文だけ更新(検査の中身は変えていない) |

### 2. 設計との差分(理由つき)

| # | 差分 | 理由 |
|---|---|---|
| 1 | `+CWLAP:` の「引用符の有無」: **括弧は任意、MAC の引用符は任意、SSID の引用符は必須**。SSID が引用符なしの行(ガイドの記載どおりの形)は読めず `bad` + `oth`(設計 §21.4.7 (a) のとおり)。依頼文の「引用符の有無を両方受ける」は、SSID については実装していない | 設計 H-W10(g) が「引用符なしは false」。右から読む後退案は RW17 のとおり、実機の `oth` で形を見てから |
| 2 | `scan` の `ERROR` / `busy` だけでなく**タイムアウトも再試行**(15 秒 x 2 = 最悪 約 31 秒) | 既存の `can_retry` の共通規則を変えない(最小実装)。`serial` 120 秒に収まる(通常 30〜50 秒、最悪の見積は設計 §21.4.4 の約 110 秒に 15 秒ほど加算) |
| 3 | `oth` の val / `scan` の val の作業領域に `c->ev.text`(41 バイト)を使う | スタックを使わない(`-Wstack-usage=100`)。中身は数字と形だけで、使い終わりに先頭を 0 にする |
| 4 | `wifi_at.c` の受信上限 64 バイト/周回・RX バッファ 3500 は**変更なし** | 専用ループの 1 周回は数十 µs(LCD・タッチなし)。ESP32 の入力は 115200bps = 約 11.5 バイト/ms なので、64 バイト/周回で十分に追いつく(AP 50 件の約 4KB でも溢れない計算。**実測は未実施**)。足りなければ `WIFI_RX_PER_LOOP` を上げる(設計 RW6) |

設計の不足・矛盾で差し戻すものは無い(上の 1 は設計どおり)。

### 3. ホスト試験(`bash tests/host/run.sh`)

| 項目 | 結果 |
|---|---|
| 全部(S1〜S23) | **PASS=22 FAIL=0 SKIP=1**(S12 = SKIP: MinGW に libasan / libubsan が無い。合格ではない) |
| S22(`test_wifi_core.c` 12,083 checks、0 failed) | H-W10: exact / near(大小・空白・全角英数・全角空白・逆向き)/ 違う(前方・後方・部分一致を near にしない)/ 日本語はバイト一致 / 32 バイトと 33 バイト / NULL・空 / 壊れた行 36 通りで false・ap が 0 / `\"` `\` `\,`。H-W11: 30 件・50 件(約 3KB 超)で `total` `found` `near` `bad` と `ap`、code 0・1・2・3、同じ SSID が複数で rssi 最大の 1 件、特殊文字の SSID、秘密なし(NULL)、読めない行 5 件で `oth` は 3 行まで、各 1100 行で 999 に頭打ち、ERROR 後の再試行で集計が 2 回目だけ、いつも ERROR・無応答でも `join` へ進む、遅れた OK を次に数えない。H-W12: 形(全バイト 1〜255 で英数字は a と 9 だけ)、40 文字、サイズ境界、gmr に 5 行で `oth` は 3 行、次の step で数え直し、エコー・空行は出ない。H-W13: `scan` `ap` `oth` が書式と最大値で適合 |
| S23(`test_wifi_at.c` 180 / 72 / 52 checks、0 failed) | 14 step、送信 12 回、scan / ap の行、`SYSSTORE` `CIPSTA` を送らない、ログに NEIGHBOR・MAC・CWLAP が無い |
| 変異による検出力 | 変異版(コピーで作成。本体は変更なし)で全て検出: ① oth に生の行を出す → 失敗 34 ② `+CWLAP:` の分類で AP 名を `ev.text` に残す → 4 ③ `key=scan` に自分の SSID を出す → 26 ④ `wifi_shape` が英字をそのまま出す → 38 ⑤ 前方一致を near にする → 4 ⑥ `oth` の上限 3 → 99 → 5 ⑦ ping の code 200 が出ない → 2 ⑧ 行バッファを消さない → 7 ⑨ `key=ap` に MAC を出す → 16 |

範囲外・限界: 書式は `sscanf` + 再整形で検査(`<regex.h>` が MinGW に無い)。`+CWLAP:` の実機の形、`AT+CWLAPOPT` / `AT+PING` の対応は実機で未確認。

### 4. RX 用 GCC での個別コンパイル

`rx-elf-gcc 14.2.0.202607 -Og -Wall -Wextra -Wstack-usage=100 -misa=v3 -mlittle-endian-data -std=gnu99 -ffunction-sections -fdata-sections -c`(`-I` は `src/smc_gen` の各フォルダ)で、`wifi_core.c` `wifi_at.c` `app_main.c` `app_debug.c` を **`APP_WIFI_TEST` = 0 と 1 の両方: 警告 0**。`wifi_at.c` は `-DAPP_WIFI_AUTOSTART=1` でも 0。

| オブジェクト | `APP_WIFI_TEST` = 0 | = 1 |
|---|---|---|
| `wifi_core.o` | 0 バイト | text 5,887(以前 4,242) |
| `wifi_at.o` | 0 バイト | text 1,147 / bss 938(`AUTOSTART` = 1: text 1,187) |
| `app_main.o` / `app_debug.o` | text 2,409 / bss 499、text 612 / bss 2,090(前回と同じ) | 同じ |

### 5. 未実施・リスク

- ヘッドレスビルド・書込み・実機は未実施。`wifi_secret.h`(ユーザーが修正済み。値は不明)は ELF・MOT・map に平文で入る(§21.5)。
- `+CWLAP:` の実機の形が引用符なしなら、`total` = `bad` になり `oth` の形で分かる(RW17。右から読む読み方に直す)。`AT+CWLAPOPT=1,31` が `fail` でも続行する。
- 行キュー(8 行)は、`scan` の終わり(`scan` `ap` `step` と次の `start`)と `oth` が重なると埋まりうる(`drop` が増えたら `oth` を 2 行にする。設計 §12.1)。
- ESP32 に SSID・パスワードが残ること(§21.5)は変わらない。`AT+RESTORE` は未実装。

## 付記(2026-11-25 天気・気温の取得 第1段階: コアと専用モードの拡張。`docs/design.md` §22.3〜§22.7・§22.12・§22.16 の順 1〜2 の手前)

**状態: ホスト試験(S1〜S24)と RX 用 GCC の個別 `-c` まで実施。ヘッドレスビルド・書込み・実機は未実施。** UI(アイコン・パレット・ホーム・`ui_*`・`gen_ui_assets.py`)と製品への組み込み(`app_main.c`)は**触っていない**(次の段階)。`APP_WX` の既定は 0、`APP_WIFI_TEST` の既定も 0 のまま。`wifi_secret.h` は開いていない・読んでいない(コンパイルに使っただけ)。**新規の .c は `wx_core.c`**(ビルドは**ヘッドレス**が必要)。

### 1. 変更内容

| ファイル | 内容 |
|---|---|
| `src/app/wx_core.h/.c`(新規。標準 C) | 要求(`wx_build_request`。固定 190 バイト)、応答の解析(`wx_parse_http`: 状態行 → `Date:`(大文字小文字を区別しない。年 2020〜2099・月名・時分秒・日の範囲検査)→ 本文の `"current":` の後ろから `temperature_2m`(0.1℃。整数部 2 桁・小数 1 桁、`-0.0` は 0、範囲 ±60.0)と `weather_code`(0〜99)と `time`(`obs`)。**文字列検索は `len` まで**。数字の後ろに区切り(`,` `}` 空白)が無ければ途切れとして失敗)、WMO → 8 種(`wx_kind_of`。表にあるコードだけ)、日付(`wx_parse_date` / `wx_date_fields`。Hinnant の days↔civil。うるう年・月末・年末・曜日)、ソフト時計(`wx_clock_*`。`base_ms = first_ms − WX_SYNC_BIAS_MS`。32bit の巻き戻りは差で処理)、ログ行(`wx_fmt_ok` / `wx_fmt_fail` / `wx_fmt_req` / `wx_fmt_state`。本文・SSID・パスワードを引数に取らない)、取得の予定表(`wx_sched_*`: 起動 3 秒後に接続 → 直ちに最初の取得 → 取得開始から 10 分ごと、更新要求は IDLE で即時・5 秒未満は ignored・接続中と取得中は busy・OFF は off・エラー中は直ちに再接続、取得の失敗は 20 秒後に 2 回やり直して 3 回目で再接続、接続の失敗は 60 / 120 / 300 / 300 秒、429 は 300 秒でやり直しの回数に数えない)。`APP_WIFI_ON` = 0 で中身が消える。 |
| `src/app/wifi_sw.h` | `APP_WX`(既定 0)と `APP_WIFI_ON`(= `APP_WIFI_TEST` か `APP_WX`)。 |
| `src/app/wifi_core.h/.c` | **step を 20 に**(`wx_mux` `wx_close` `wx_tcp` `wx_send` `wx_recv` = 15〜19。`[hb]` の `scr` は 0〜19)。**範囲を `c->last` から `c->mask`(ビットマスク)に**。`WIFI_RANGE_ALL` / `BOOT` / `AT` は同じ範囲(動きは変わらない)、新規 `WIFI_RANGE_CONNECT`(reset boot at echo mode qap join ip)と `WIFI_RANGE_WX`。行の分類に `CONNECT`(`<n>,CONNECT`)・`CLOSED`(`<n>,CLOSED`)・`SEND OK`・`SEND FAIL`・`>`・`DNS Fail`。**データモード**: `wx_send` / `wx_recv` の応答待ちの間、行が `+IPD,<n>[,<ip>,<port>]:` になった**瞬間**(`:` を受けた時点)に、続く `n`(1〜2048)バイトを行として扱わず `wx_buf`(1536)へ入れる(本文の `OK` `CLOSED` `+IPD,` を解釈しない)。数字が無い・0・2048 超の `+IPD` 行は捨てる(`bad_lines` +1)。`wx_send` の間は行頭の `>` を、改行を待たず 1 行として扱う。`wx_send`: `AT+CIPSEND=<n>`(n は `wx_build_request` の長さ)→ `OK` と `>` の両方 → 要求本体 190 バイトを `\r\n` を足さずに送る(BUSY は同じものを渡し直す。`PH_SEND_DATA`)→ `SEND OK`。`wx_recv`: `CLOSED` で終わり(`closed=1`)、10 秒で時間切れ(解析できていれば成功、`closed=0`)。**結果の取り出し**: `wifi_core_wx_seq` / `wifi_core_wx`(`wx_result_t`。数値だけ)、`wifi_core_done_seq` / `wifi_core_pass` / `wifi_core_fail_stage`。取得の失敗は `wx.err`(1 = TCP、2 = 送信、3 = 応答なし、4 = HTTP が 200 でない、5 = キー、6 = 範囲外、7 = バッファ超過)と、`[wifi] step=wx_* res=fail/timeout` の `code=`(同じ値)。解析のあと `wx_buf` を 0 で消す。 |
| `src/app/wifi_at.c` | UART `x` = `WIFI_RANGE_WX`(実行中なら無視)。取得の結果が出るたびに `[wx] res=ok src=uart ...` / `[wx] res=fail src=uart err=...`(`wx_fmt_*`)を 1 行。`Date:` が読めたらソフト時計を合わせる。`s` の応答に、**一度でも取得の結果が出ていれば**`[wx] net=... clk=... date=... wd=...` の状態行を足す。 |
| `tests/host/test_wx_core.c`(新規)、`test_wifi_core.c`、`test_wifi_at.c`、`run.sh`(S22・S23 の更新と S24 の追加) | 下の 3。 |

### 2. 設計との差分(理由つき。差し戻しが要るものは無い)

| # | 差分 | 理由 |
|---|---|---|
| 1 | `[wx]` の結果行は **`wifi_core` の `ops->log` ではなく、結果の消費側(`wifi_at.c`)が `wx_fmt_*` で出す**(§22.4 の 5 は `wifi_core` が出すと読める) | `src=boot/timer/touch/uart` は消費側(予定表)しか知らない。`wifi_core` を取得のきっかけに依存させない。出る行は同じ |
| 2 | 要求本体は応答バッファ(`wx_buf`)ではなく **`c->cmd`(224 バイト)**に作って送る | `SEND OK` より先に `+IPD` が来ても(§22.3)、要求と応答が同じバッファで衝突しない。RAM の増えは無い |
| 3 | `x` は **`applog_cmd_parse` に足さず、`wifi_at.c` が生の文字 `'x'` で受ける**(`APPLOG_CMD_WX` は第 4 段階) | `'x'` を「無視される文字」とする既存の試験(`test_logfmt` `test_app_main` `test_clut8_cfg`)と製品の `app_main.c` を変えない。`test_wifi_at.c` の B5 は、`x` が実行中だから無視される、で期待は変わらない |
| 4 | **`wx_send` の `CLOSED`**: 要求を送る前は即失敗(err=2)、送った後は印だけ立てて `wx_recv` の頭で終わりにする | §22.3 の表(即失敗)と「受信の読み方」(印だけ)が食い違う。送信の前後で分けて両方を満たした |
| 5 | WMO の対応表は**設計の表のコードだけ**(52・54・62・64・72・74 などの未定義は `unknown`) | §22.5 の表のとおり。範囲(51〜57 など)で書くと未定義まで入る(ホスト試験が検出した) |
| 6 | **H-X2 の「2100-03-01(うるう年でない)」**は `Date:` の経由では試験できない(`Date:` の年は 2020〜2099 だけ受ける §22.5)ので、**秒から日付を作る `wx_date_fields`** で試験(2100-02-28 の次は 03-01) | 設計の範囲検査と H-X2 の例が食い違う。実害なし |
| 7 | 解析の失敗の番号: 状態行まで届かない(12 バイト未満・空)= **3**、`HTTP/1.x ddd ` の形でない = **4**(`http=0`)、気温が `null`・文字列・途切れ = **5**、`weather_code` が `null`・負・100 以上 = **6**(設計 §22.5 のとおり)、数字の後ろに区切りが無い(途切れ)= **5** | §22.5 の 7(途切れ)を、値が途中で切れても前半を採らないために、区切りの確認を入れた(`-3.4` が `-3` や `-3.4` の区切りなしで成功にならない) |
| 8 | `APP_WX = 1` かつ `APP_WIFI_TEST = 0` では `wifi_core.o` `wx_core.o` に中身が入るが、`wifi_at.o` は空(**製品への組み込みは第 5 段階**)。**§22.4 の 6(製品のログを減らす: `res=start` と `key=oth` を出さない)は未実装** | この段階は専用モードだけ |
| 9 | `x` は `d` の後でなくても動く(ESP32 が AT に応答していれば)。`d` の前に押すと `wx_tcp` が失敗して `[wx] res=fail err=1` になる | ゲートを足さない(最小実装) |
| 10 | 専用モードの `s` に `[wx]` 状態行を足した(設計 design-uart §13.2 は「専用モードの `s` は変えない」)。**一度も取得の結果が出ていなければ出さない**ので、UW の既存の判定には影響しない。`net` は直近の取得が成功なら `on`、失敗なら `err` | X3(ソフト時計と PC の時刻の比較)を専用モードで暫定確認するため(§22.16 の順 2) |
| 11 | 行の分類を足したことで、`CONNECT` / `CLOSED` / `SEND OK` / `SEND FAIL` / `>` / `DNS Fail` は、**既存の step では `key=oth` に出なくなる**(以前は OTHER として形を出していた) | 既存の step が受けるはずの行ではない。試験の `SEND OK` を OTHER とする 1 件だけ、`SEND OK ` に直した |

### 3. ホスト試験(`bash tests/host/run.sh`)

| 項目 | 結果 |
|---|---|
| 全部(S1〜S24) | **PASS=23 FAIL=0 SKIP=1**(S12 = SKIP: MinGW に libasan / libubsan が無い。**合格ではない**)。既存の S1〜S23 は回帰なし(`test_wifi_core.c` の既存の期待は 3 か所だけ直した: `WIFI_STEP_END` 15 → 20 と新しい step 名、`SEND OK` の分類) |
| S24(新規 `test_wx_core.c` 7,284 checks、0 failed) | H-X0 要求(190 バイト・末尾 `\r\n\r\n`・`Accept-Encoding` なし・サイズ境界)/ H-X1(実測の応答・chunked・気温 17 通り・異常 15 通り・weather_code 8 通り・`current_units` を読まない・ヘッダーの `current`・状態 301 / 429 / 500 / 404 …・`Date:` なし・壊れ・小文字・本文中の `Date:`・**あらゆる長さで切った応答**(切った後ろを本物の続きにした版と毒 `Z` にした版で結果が同じ = バッファの外を読まない)・8000 バイトのゴミとランダム列)/ H-X2(`Date:` の拒否 23 通り、うるう日・年末・UTC 15:00 の繰り上がり、**2020〜2099 を約 17,000 秒刻みでホストの `gmtime` と全フィールド一致、`Date:` の往復一致**)/ H-X4(0〜99 の全コードと 100 / 255 / 0xFFFFFFFF)/ H-X3(同期・700ms のバイアス・再同期・**32bit の巻き戻り**・日またぎと曜日)/ 書式(`wx_fmt_*` を R-WX-* の独立した検査で、全 src・全 kind・境界値・小バッファ・引数不正)/ H-X7(予定表 6 場面: OFF、起動〜10 分、更新要求の即時・5 秒・busy・連打、失敗のやり直し 20 秒 x2 → 再接続、429、接続の失敗 60 / 120 / 300 / 300 秒、エラー中の要求、**now の 32bit 巻き戻り**) |
| S22(`test_wifi_core.c` 15,311 checks、0 failed) | 追加の H-X5・H-X6: 分類 / 正常(5 step、送ったコマンドの文字列、要求 190 バイトに `\r\n` を足さない、EN に触れない、結果の `ok` `temp` `wmo` `kind` `obs` `jst` `first_ms` `ms` `closed`、本文が `wx_buf` と構造体に残らない、ログに本文がない)/ 2 回続けて / `+IPD` の分割(1・9・100・256・257・300 バイトで 2 つに、3 つに、**1 tick に 1 バイト**)/ `SEND OK` より先の `+IPD`・`CLOSED` / **データの中の `OK` `CLOSED` `0,CLOSED` `+IPD,5:` `SEND OK` `ERROR` `>` `FAIL`(境目を `CLOSED` の途中にした版も)** / `+IPD,<n>,<ip>,<port>:` / 壊れた `+IPD` 行 4 通り / `wx_tcp` の失敗(ERROR・`DNS Fail`・`CLOSED`・`FAIL`・`0,CLOSED`・無応答・CONNECT だけ)と `OK` → `0,CONNECT` / `wx_mux` ERROR と `wx_close` の `link is not valid`+OK / `wx_send` の失敗 6 通り / **BUSY**(要求本体 5 回・`CIPSEND` 3 回)と送信 FAIL / `CLOSED` なしのタイムアウト + 解析成功(`closed=0`)/ HTTP 429・500・301 / err=5・6・3 と途中までの応答・受信中の ERROR / **1536 バイトちょうど(成功)・1537(err=7)・2048 バイトの `+IPD` 2 回** / 範囲 CONNECT(送ったコマンド 6 つ、gmr・lapopt・scan・dns・ping を含まない、秘密なしは join skip、join 失敗)・ALL の後の WX。最後の全ログ検査に `BODY_SENTINEL` `latitude` `generationtime` `Date:` `api.open-meteo` `GET /` が無いことを追加 |
| S23(`test_wifi_at.c` 259 / 151 / 52 checks、0 failed) | 追加の F1〜F5(偽 ESP32 が TCP と HTTP 応答を返す): `d` の後の `x` で `[wifi] step=wx_*` が各 1 回 ok・`[wx] res=ok src=uart temp10=127 wmo=0 kind=sun http=200 ... sync=1 jst=132413 obs=1330 closed=1`・要求本体が送られ末尾に `\r\n` が付かない・`s` の `[wx] net=on ... clk=... date=20261125 wd=3 sync=1`(`clk` は 13:24:13 + 経過 ± 1 秒)・`drop=0`・本文・要求・`Date:` がログに出ない / `x` だけ(EN に触れない・2 回連続)/ `d` の最中の `x` は無視 / TCP が張れない(`err=1`、`s` は `net=err have=0`)/ 応答なし(`err=3`) |
| 変異による検出力(本体は戻してある) | 19 件中 18 件を検出: ① `current_units` を読む(`"current` で探す)→ 失敗 292 ② 気温を本文の先頭から探す → 289 ③ データモードを無効(本文を行として読む)→ 364 ④ UTC→JST の +9h を外す → 22 ⑤ `>` を改行なしで検出しない → 413 ⑥ バッファ超過を見ない → 4 ⑦ 更新要求の最小間隔 5 秒 → 0 → 6 ⑧ WMO 71 を雨に → 2 ⑨ TCP 失敗で結果を出さない → 12 ⑩ ソフト時計を合わせない → 4 ⑪ 要求本体の BUSY を失敗扱い → 23 ⑫ 解析後に本文を消さない → 67 ⑬ ログに本文の先頭を出す → 67 ⑭ `+IPD` の数字の上限を外す → 16 ⑮ 送信前の `CLOSED` を許す(最初は生き残ったので、`res=fail` で即失敗することを確認する 1 行を足して検出)⑯ 範囲 CONNECT に gmr を入れる → 7 ⑰ `Date:` の日の範囲検査を外す → 4 ⑱ 同期のバイアスを 0 に → 9。**生き残り 1 件: うるう年の 100 年・400 年の規則を外す(`is_leap`)は、受け付ける年(2020〜2099)では結果が変わらない等価な変異**(2100 は `Date:` の範囲外。`wx_date_fields` の日付計算は別の式でホストの `gmtime` と全域一致) |

範囲外・限界: 書式の判定は `strstr` と手書きの検査(`<regex.h>` が MinGW に無い)。**ESP32 の実際の TCP の返事の形**(`>` の形、`SEND OK` の前後の `Recv n bytes`、`+IPD` の形、`CONNECT` の前後)は、台本が設計の資料(v1.2)に基づく想定で、実機で未確認(RW-X1。`key=oth` の形で見る)。

### 4. RX 用 GCC での個別コンパイル

`rx-elf-gcc 14.2.0.202607 -Og -Wall -Wextra -Wstack-usage=100 -misa=v3 -mlittle-endian-data -std=gnu99 -ffunction-sections -fdata-sections -c`(`-I` は `src/smc_gen` の各フォルダ)で、`wifi_core.c` `wx_core.c` `wifi_at.c` `app_main.c` `app_debug.c` を **`APP_WIFI_TEST` = 0 と 1 の両方: 警告 0**(スタック使用量の警告も 0)。`wifi_at.c` は `-DAPP_WIFI_AUTOSTART=1` でも 0。`wifi_core.c` `wx_core.c` は `-DAPP_WX=1` でも 0。(途中で `wx_fmt_state` の `snprintf` が 108 バイトのスタックを使うと出たので 2 回に分けて直した。`%02u` の切り詰め警告は値を `% 100u` で抑えた。)

| オブジェクト | `APP_WIFI_TEST` = 0 | = 1 |
|---|---|---|
| `wifi_core.o` | 0 バイト(`APP_WX = 1` だけだと text 7,759) | text 7,759(以前 5,887) |
| `wx_core.o` | 0 バイト(`APP_WX = 1` だけだと text 5,221) | text 5,221 |
| `wifi_at.o` | 0 バイト | text 1,531 / bss 2,555(`s_core` に `wx_buf` 1,536 が入る。`AUTOSTART` = 1: text 1,571 / bss 2,556) |
| `app_main.o` / `app_debug.o` | text 2,409 / bss 499、text 612 / bss 2,090(前回と同じ) | 同じ |

### 5. 未実施・リスク

- **ヘッドレスビルド・リンク・書込み・実機は未実施。** 新規の .c(`wx_core.c`)があるので、ビルドは**ヘッドレス(makefile を作り直す)**。`APP_WIFI_TEST` の切り替えは `-DAPP_WIFI_TEST=1`。`APP_WIFI_TEST = 0` では `wifi_core.o` `wx_core.o` `wifi_at.o` が 0 バイトのはず(リンク後の map で確認)。
- 実機で未確認: ESP32 1.1.3.0 の TCP の書式(`AT+CIPSTART` の返事の順序、`>` の形(`> ` か `>`)、`Recv n bytes` と `SEND OK` の順、`+IPD` の形、`CLOSED` の形)、HTTP の応答の分割、`Date:` の精度(`WX_SYNC_BIAS_MS` = 700 の調整)、115200bps の受信(約 55ms)と受信バッファ 3500 バイト。
- 専用モードの `x` を続けて押す時は、取得が終わるまで(約 1〜2 秒)あける(実行中の `x` は無視)。
- `AT+CIPSEND` の `>` の後ろに空白が付く版(`> `)だと、その空白が次の行の先頭に付き、`key=oth` に `_aaaa_999_aaaaa` のような形が出る(動作には影響しない)。


## 付記(2026-11-25 天気・気温 第2段階: UI(アイコン・パレット・ホーム)。`docs/design.md` §22.8〜§22.10・§22.12 の H-X8〜H-X10・§22.16 の順 3〜4)

**状態: ホスト試験(S1〜S25)と RX 用 GCC の個別 `-c` まで実施。ヘッドレスビルド・書込み・実機・LCD の目視・カメラは未実施。** ネットワーク(取得・接続)は触っていない(`wifi_at.c` `wifi_core.c` `wx_core.c` `wifi_secret.h` は未変更・未読)。**製品への組み込み(`app_main.c` の `ui_set_wx` 呼び出し・`APP_WX`)は次の段階**。値は `ui_set_wx()`、または gdb / ホスト試験が `s_ui.wx` に直接書いて与える。

### 1. 変更内容

| ファイル | 内容 |
|---|---|
| `tools/gen_ui_assets.py`(変更) | (a) ホーム背景: 日付の焼き込みをやめ、「℃」「東京」「更新」と更新ボタンの面(28x28 r8、`#dcebf7`)を焼き込み。(b) スプライト 16 枚(天気 8 種 28x28、Wi-Fi 4 状態 20x20、更新の回転 4 段階 20x20。モック `wx_mock.py` の描き方を移した。下地に焼き込んだ不透明の 8bit)。(c) **共通パレットの再生成**(新しい固定色 5 = `SUN CLOUD CLOUD_DK RAIN ICE`、Wi-Fi・更新アイコンの平らな色も固定、スプライトの重み 20)。(d) 数字フォントに `-`(13。全フォント)と F14 だけ `()月日火水木金土`(14〜22。ほかのフォントは幅 0 の空の字形)。`UI_GLYPHS` = 23。(e) スロット 3 つ(`HOME_DATE` F14・`HOME_TEMP` F22 右詰め 右端 106・`HOME_UPD` F14)、`HOME_TEMP` は同じ下地で文字色が INK / SUB の 2 つ(ramp[0] = INK、ramp[1] = SUB。`ui_slot_alt` は -1)。(f) ボタン `H_WX`(`UI_BTN_H_WX` = 16、見た目 (248,158,28,28)、当たり判定 (228,136,60,52)、形 28x28 r8 = `UI_SHAPE_COUNT` 9)。(g) `--check` に、新しい色の厳密さ・スプライトの最大誤差 ≤ 3 段・使用添字 ≤ 256・スプライトの置き場所(画面内、Wi-Fi がヘッダー内、更新アイコンがボタンの中、日付が Wi-Fi に重ならない、気温が天気に重ならない)を追加。(h) プレビューの既定の出力先を `preview_wx` に(下の §5)。 |
| `src/app/ui_bg_*.c`、`ui_gfx_data.c`、`ui_layout.c`、`ui_assets.h`(**再生成**。LF) | パレット 256 色・背景 7 枚・ramp・remap・スプライト(`ui_spr_wx[8][784]` `ui_spr_wifi[4][400]` `ui_spr_ref[4][400]`)・`UI_WIFI_*` `UI_WX_*` `UI_REF_*` `UI_G_*`。 |
| `src/app/ui_logic.h/.c`(変更) | `ui_wx_t`(16 バイト、パディングなし。`rsv` = 0)・`UI_NET_*`・`ui_set_wx()`・`ui_t.wx` / `ui_t.spin`・`ui_key_t.wx` / `.spin`(表示キー)・`UI_CMD_WX`(ホームの `h_wx` の仮想タップ。デモ中・ホーム以外は無視、遷移中・押下表示中は `press_confirmed` が断る)・`clk_ext = 1` なら `v.clock_min = wx.clock_min`(0 は従来の 21:45 起点のまま)・`spin`(busy の間だけ `(now / 150) & 3`)。`ui_init` の既定は `clk_ext = 0`・OFFLINE・未取得(`upd_min` = `clock_min` = 0xFFFF)。`act()` の `UI_BTN_H_WX` は何もしない(`UI_EV_BTN_ACT` が出る)。 |
| `src/app/ui_render.c/.h`(変更) | ホーム: 日付(`clk_ext = 0` は 11月21日(土)、`clk_ext = 1` は wx の日付。未同期・範囲外は描かない)・気温(`--.-` / 符号つき 0.1℃ / ±99.9 に丸め。30 分超は ramp[1] で灰色)・更新時刻(未取得・0xFFFF・1440 以上は `--:--`)・天気スプライト(`have = 0` と範囲外は「不明」)・更新アイコン(`spin & 3`)。ホーム・湯はり・追い焚き・使用量のヘッダー (112,7) に Wi-Fi アイコン(待機には出さない)。時計は 1440 以上(0xFFFF)なら `--:--`(ホームと待機)。描く順 = 背景 → 数字 → バー → **スプライト** → 押下の remap → タグ(アイコンも押下で暗く・白くなる)。どの値でも安全(`net` `kind` `spin` `month` `day` `wday` `temp` の範囲外は丸めるか描かない)。 |
| `tools/fb_hostrender.c`(変更) | 状態ファイルに `wx.net busy have kind temp stale clkext upd clock month day wday spin`(無ければ既定 = 従来の状態ファイルのまま描ける)。 |
| `tools/fb2png.py`(変更) | gdb ログに `s_ui.wx` と `s_ui.spin` の print(`fb_phase` の次)があれば読んで状態ファイルに `wx.*` を書く(12 個の従来のログも読める)。 |
| `tools/c8png.py`(変更) | `gallery <fb_hostrender> <out.png>`: 天気 8 種・Wi-Fi 4 状態・回転 4・未取得・古い値・負の気温・旧時計の 21 状態を**ホストの C 描画**で描いて切り出した目視用の 1 枚(判定ではない)。 |
| `tools/wx_compare_p2b.py`(新規) | 背景 7 枚を P2b(コミット `e6eb603`)と並べた `compare_bg_*.png` と `compare_report.md`。 |
| `tools/wx_mock.py`(変更) | 設計時の試作。統合済みなので、`H_WX` が既に表にあれば何もしないで終わる(二重に足して壊れないように)。 |
| `tests/host/test_ui_wx.c`(新規)・`run.sh`(S25 追加)・`judge_p2b.py`・`test_p2b_indep.py`・`test_p2b_extra.c`・`test_ui_extra.c`(更新) | 下の §4。 |

### 2. 設計との差分(理由つき。差し戻しが要るものは無い)

| # | 差分 | 理由 |
|---|---|---|
| 1 | **スプライトの重みを 50 → 20** | 設計 §22.9 の掃引(10 / 20 / 30 / 50)を、この実装(スロット 3 つ・数字の ramp まで含む)でやり直した: 重み 50 だと静的な画面の不一致が 0.561%(§20.3.3 の目標 0.5% を超える)、20 だと 0.484%(スプライトの最大は 3 段のまま、背景の不一致は 0.248% → 0.227%)。10 は スプライトが 6 段になるので不可。スプライトの不一致画素は 70 → 226(9,472 画素の 2.4%。最大 3 段)に増える |
| 2 | `UI_EV_WX_REFRESH` は足さない。更新の押下は設計どおり **`UI_EV_BTN_ACT`(`a = UI_BTN_H_WX`)** | 設計 §22.8(`act()` は何もしない。`app_main` が BTN_ACT を見て `wx_request`)。ログの `[ev] btn=h_wx st=down/act` は `ui_btn_defs` の名前から既存の `log_event` がそのまま出す(`app_main.c` は変更なし) |
| 3 | UART `x`(`applog_cmd_parse` の `APPLOG_CMD_WX`・`?` の `x=wx`・`app_main` の `ui_cmd(UI_CMD_WX)`)と `[ui]` / `[wx]` の行は**入れていない**。`ui_cmd(UI_CMD_WX)` までは `ui_logic` にある | `x` を無視する文字とする既存の試験(UT5 の `x`、`test_logfmt` `test_app_main` `test_clut8_cfg`、`judge_ui.py` の help の正規表現)と製品の UART の出力を、この段階(UI だけ)で変えないため。設計 §22.16 の順 4・5(製品への組み込み)で、`app_logfmt` / `app_main` と試験を一緒に直す。`[ui]` の行は設計 design-uart §13 でも変更なし |
| 4 | `APP_WX` で UI を切り替えるコードは無い | 設計どおり `ui_init` の既定が `clk_ext = 0`(旧時計・旧日付・OFFLINE・未取得)なので、`app_main` が `ui_set_wx` を呼ばない(`APP_WX = 0`)限り従来の動作。**ただしパレットとアセットは 1 つなので `APP_WX = 0` でも新しいパレット・新しいホーム(天気の行つき)になる**。P2b との画素一致は崩れる(§3 の「P2b との差」) |
| 5 | **H-D4(S19)の基準**: ① P1 に無い矩形(ホームの日付 (8,6,96,26)・天気の行 (16,154,268,36)・4 画面の Wi-Fi (112,7,20,20))を比べない(スライド中は `xo` ぶんずらす。`judge_p2b.mask_of`)② 静的・スライド・デモの不一致の合計 ≤ 0.5% → **≤ 0.7%**(2段超 ≤ 0.01%、最大 ≤ 3 段、1状態 ≤ 1.0%、押下・白光の基準は変えない)。`p1_ref.npz` は変えない | ① 基準(P1)に無い要素。② ホームの背景の不一致が 0.550% → 0.666%(設計 §22.9 が許容した値。H-D1 の 1 枚 ≤ 1.0% の中)になり、この 23 状態は 20 状態のうち 14 がホーム(日付・時計の数字を含む)で合計に占める割合が大きい。実測 0.587%(P2b 時点 0.454%)・2段超 0・最大 2 段。S21(`test_p2b_indep.py`)も同じマスクと 0.7% |
| 6 | `--c8-preview-dir` の既定を `preview_p2b` → **`preview_wx`** | `preview_p2b/` は P2b 時点の記録として残す(新しいパレットで上書きしない)。新旧の比較は `compare_*` |
| 7 | 日付の旧「11月21日(土)」(`clk_ext = 0`)も**動的に描く**(焼き込みではなく F14 の字形) | 日付を焼き込みから外す設計(§22.8)のため。P1 の焼き込みの字と画素が違う(矩形をマスク) |
| 8 | `have = 0` の判断は**描画側**(未取得 = 気温 `--.-`・アイコン「不明」・更新時刻 `--:--`) | `ui_init` の既定の `kind` = 0(晴れ)でも未取得なら不明になるように。wx_core 側が `have = 0` の間に `kind` / `temp` を何に設定しても表示は同じ |
| 9 | `test_p2b_extra.c` の P2: 「`alt < 0` のスロットは 2 つ目の表が 0 埋め」から `HOME_TEMP` を除外し、`ui_slot_alt[FILL_PCT] == 6` を `ui_pal[alt] == 0xFF8CBEDE`(BAR_F の色)に | パレットの作り直しで添字が変わる。`HOME_TEMP` は 2 つ目の表に SUB の ramp を持つ |

### 3. 誤差の実測(`gen_ui_assets.py --check` / `preview_wx/error_table.md`。目標 = 設計 §22.9)

| 項目 | 実測 | 目標 | 判定 |
|---|---|---|---|
| universe / 固定色 / 使われる添字 | 836 色 / 31 / 246(256 以内) | 836 / 31 / 246 | 設計の実測と同じ |
| 背景 7 枚の不一致 | 2,078 / 913,920 = **0.227%**、1段超 19、最大 **2 段** | ≤ 0.3%、最大 ≤ 2 段 | OK |
| 背景 1 枚の最大 | home **0.666%**(fill_run 0.182、fill_done 0.126、reheat_run 0.236、reheat_done 0.157、usage 0.119、stby 0.106) | ≤ 1.0% | OK |
| スプライト 16 枚の最大誤差 | **3 段**(wx_partly 4 画素・wx_snow 6・wx_thunder 2 が 1 段超。ほか ≤ 2 段、平らな絵は 0)。不一致画素 226 / 9,472 | ≤ 3 段 | OK |
| 数字の ramp(静的な画面。Python 試作描画 15 画面) | 不一致 **0.484%**、最大 3 段(`fill_done` の満タンのバーの右端の角 4 画素。P2b も同じ)。数字・文字そのものは最大 2 段 | ≤ 0.5%、最大 3 段(§20.3.3) | OK(P2b 0.302%) |
| 押下 / 白光の remap(Python 試作描画 15 + 15 画面) | 押下 0.724%・2段超 993 画素(0.051%)・最大 6 段(角の縁のみ。設計どおり)、白光 0.706%・最大 3 段。**更新ボタン**: 押下 2段超 22 画素(最大 6 段 = 角の縁)、白光 2段超 2 画素・最大 3 段 | 2段超 ≤ 0.1%、最大 ≤ 8 段 | OK |
| スライド | 不一致 0.430%、最大 1 段 | — | OK |
| 実機の 23 状態(C 描画 vs P1。S19。マスクの外) | 静的・スライド・デモ 20 状態: 不一致 **0.587%**、1段超 239、**2段超 0**、最大 **2 段**。押下・白光 3 状態: 不一致 0.842%、2段超 78(0.022%)、最大 6 段 | 新基準 ≤ 0.7% / 2段超 ≤ 0.01% / ≤ 3 段、押下 2段超 ≤ 0.1% / ≤ 8 段 | OK(P2b: 0.454% / 4 画素 / 3 段、0.660% / 79 画素 / 6 段) |
| P2b との差(`compare_report.md`。背景 7 枚、home の日付・天気の行の矩形を除く) | 矩形の外の差 33〜536 画素(各背景 0.03〜0.41%。home 536 画素・1段超 43)、**最大 2 段** | — | `APP_WX = 0` でも新しいパレットになることの影響。目視で見分けにくい範囲 |

### 4. ホスト試験(`bash tests/host/run.sh`)

| 項目 | 結果 |
|---|---|
| 全部(S1〜S25) | **PASS=24 FAIL=0 SKIP=1**(S12 = SKIP: MinGW に libasan / libubsan が無い。**合格ではない**) |
| S25(新規。`test_ui_wx.c` 311 checks、0 failed) | X1 `ui_wx_t` 16 バイト・`ui_init` の既定 / X1b 表示キー(wx の全メンバーと spin の変化で再描画、変化なしなら再描画しない)/ X2 `clk_ext` 0・1 の時計 / X2b spin / X2c `UI_CMD_WX`(BTN_DOWN src=uart → BTN_ACT、画面・設定温度は変わらない)/ X2d ホーム以外・デモ中・押下表示中・遷移中は無視 / **X2e 実タッチの当たり判定の境界**(内側 6 点が h_wx、1 画素外 6 点は h_wx にならない)/ X2f デモの台本は h_wx を押さず 36 行のまま / **X3 天気 8 種・X3b Wi-Fi 4 状態(待機は出ない)・X3c 更新の回転 4 段階: スプライトの位置と、矩形の外が変わらないこと** / X3d 日付(旧日付・動的・未同期・範囲外・最長 12月30日・7 曜日がすべて別の絵)/ X3e 気温(右詰め ≤ 106・天気に重ならない・符号と桁・範囲外の丸め・`--.-`・灰色)/ X3f 更新時刻 / X3g 時計 `--:--` / X3h wx を全部変えても矩形の外は同じ / X3i 更新ボタンの押下・白光でアイコンも変わる / X3j スライド中の位置 / **X3k 全画素を書く**(面を 0x00 と 0xFF で埋め、5 画面 x 極端・範囲外の wx 5 通り x 遷移)/ X4 rsv。**効きの確認**: Wi-Fi アイコンの y を +1 → X3b が失敗、気温の丸めを外す → X3e が失敗(どちらも戻した)。`c8png.py gallery` で 21 状態の PNG(`tests/out/wx_gallery.png`) |
| S4(H-D1 / H-X10) | `gen_ui_assets.py --check` 成功、再実行で出力が同一、作業ツリーと一致(CR 以外)、`ui_bg_*.o` の `.rdata` が各 130,560、生成データの `.rodata` 合計が見積の ±30% 内 |
| S16 / S20 / S21 | `test_ui_extra.c` の X1: 更新ボタンだけ当たり判定が ±3 でない(228,136,60,52 を明示)・番号 16・`UI_BTN_COUNT` 17。**デモ 36 行は変わらない**(既存の UT13 相当が回帰なし)。S20 は §2 の 9。S21 はマスクと 0.7% |
| 既存の回帰 | S1〜S3、S5〜S11、S13〜S15、S17、S18、S22〜S24 はそのまま合格(`gfx.c` は無変更) |

### 5. プレビュー PNG(ユーザーが見る)

`tools/assets/bath/preview_wx/`: **`mock_home_c8.png`(ホームを 256 色パレットで 2 倍。実際の生成物から描いたもの)・`mock_home_true.png`(元の色)・`states.png`(同期済み晴れ ONLINE / 未取得 OFFLINE / エラー・古い値・雨 / 接続中・雪・負の気温・回転)・`wx_gallery.png`(ホストの C 描画で 21 状態。天気 8 種・Wi-Fi 4 状態・回転 4・未取得・古い値・負・-10.5・旧時計)・`icons_x4.png`(アイコン 16 枚の 4 倍)・`palette.png`(256 色の見本。左上のマゼンタの点 = 固定色)・`error_table.md`(誤差表。背景・画面・スプライト・パレット)・`compare_bg_*.png` と `compare_report.md`(P2b と新の並べ比較と差分)・`bg_*.png`(元の色 | パレット | 差分)・`scr_*.png`(ホーム 5 状態・湯はり・追い焚き・使用量・待機・スライド。Wi-Fi アイコンを含む)・`buttons_press.png` / `buttons_flash.png`(15 ボタン。**更新ボタンを含む**)・`digits.png`(15 スロット)。`palette_report.md` は設計時のモックの測定(重み 50)で、更新していない。`preview_p2b/` は P2b 時点の記録のまま。再生成: `python tools/gen_ui_assets.py`、`python tools/wx_compare_p2b.py`、`python tools/c8png.py gallery <fb_hostrender> <out.png>`。

### 6. 実行したこと(RX 用 GCC)

`rx-elf-gcc 14.2.0.202607 -Og -Wall -Wextra -Wstack-usage=100 -misa=v3 -mlittle-endian-data -std=gnu99 -ffunction-sections -fdata-sections -c`(個別。`-I` は `src/smc_gen` の各フォルダ)で、`ui_render.c` `ui_logic.c` `gfx.c` `app_main.c` `ui_layout.c` `ui_gfx_data.c` `ui_bg_home.c` `ui_bg_stby.c` を **`-DAPP_WX=0` と `-DAPP_WX=1` の両方: 警告 0**(スタック使用量の警告も 0)。`.o` の text: `ui_render` 2,567(P2b 1,895)、`ui_logic` 3,216、`ui_gfx_data` 197,453(P2b 184,157。+13,296 = スプライト 9,472 + 数字フォントの `-` と F14 の 9 字 + 形 + 表)、`ui_layout` 3,351(P2b 3,094)、`app_main` 2,409(変更なし)。ROM の見積(text + data): P2b 1,171,072 + 約 15KB = **約 1,186,000**(`APP_WX = 0`。実測はビルド担当)。`APP_WX = 1` は wifi / wx のコードが約 +12KB(設計 §22.10)で約 1,198,000 の見込み(基準 ≤ 1,250,000)。

### 7. 未実施・リスク

- **ヘッドレスビルド・書込み・実機・LCD の目視・カメラは未実施**。新規の `.c` は無い(生成物と既存の `ui_*.c` の変更)。`wx_core.c` は前段階で追加済み。`home_us` / `slide_us` の実測(目標 ≤ 1,400µs)は未実施(スプライト 3 枚 + 数字 3 スロットで +30〜50µs の見積)。
- **gdb スクリプト(`tools/gdb/mi-fbdump-c8*.gdb`)は未変更**。実機の状態と画素を比べる時は `print s_ui.wx` と `print s_ui.spin` を `fb_phase` の次(`g_app_dbg` の前)に足す(`fb2png.py` はその形のログを読む)。足さなければ、ホスト描画は既定の wx(OFFLINE・未取得・旧時計)で描く = `app_main` が `ui_set_wx` を呼ばない現状の実機と一致する。
- 色の見え方(アイコンの形・色、気温の数字、更新ボタンの押下・回転、灰色)は**ユーザーの目視**(`preview_wx/` と実機)。
- **表示キーに wx 全体と spin を入れた**ので、待機画面でも wx の変化(10 分ごと、取得中の spin 150ms ごと)で再描画 + 入れ替えが走る(無駄だが害はない。最小実装を優先)。
- `ui_t` / `ui_key_t` が大きくなった(wx 16 バイト x 2 + spin 2)。`.bss`(`s_ui`)の増加は約 +50 バイト(wx 16 + spin 1 と、表示キー 2 つぶんの wx 16 + spin 1)。
- 日付の `(` `)` と漢字は F14(400)の字形。モックの焼き込みの字(同じフォント・同じ大きさ)と形は同じだが、位置が整数で、画素が少し違う(目視で確認)。
- `ui_logic` は `have` / `stale` / `net` を計算しない(表示値を受け取るだけ)。30 分超の判定・`--:--` にする元の値は `wx_core` / `app_main` の責任。
- 設計 §22.8 の `UI_SL_COUNT` は 15 になった(`HOME_DATE` `HOME_TEMP` `HOME_UPD` を末尾に足したので、既存のスロット番号は変わらない)。


## 付記(2026-11-25 天気・気温 第3段階: 製品への組み込み(Wi-Fi の接続・取得・時刻同期)。`docs/design.md` §22.7・§22.11・§22.16 の順 5)

**状態: ホスト試験(S1〜S26)と RX 用 GCC の個別 `-c` まで実施。ヘッドレスビルド・書込み・実機・LCD の目視・カメラは未実施。** `wifi_secret.h` は開いていない・読んでいない(コンパイルに使っただけ。実値はどこにも書いていない)。**新規の .c は無い**(`wx_core.c` は第 1 段階で追加済み)ので、ビルドは前回の makefile のままでよい(ヘッドレスは `wx_core.c` が未登録なら必要。ビルド担当が確認)。**`APP_WX` の既定は 0 のまま**(設計 §22.0 の 4: 実機の受入に合格するまで 0)。製品は `-DAPP_WX=1`(`APP_WIFI_TEST` = 0)でビルドする。

### 1. 変更内容

| ファイル | 内容 |
|---|---|
| `wifi_sw.h` | `APP_WX_PRODUCT`(= `APP_WX` かつ `APP_WIFI_TEST` = 0。専用モードが優先)を追加。 |
| `wifi_at.c` / `wifi_at.h` | 共通部(SCI1・EN・`pump_rx`(1 周回 64 バイト)・`wx_report`(結果 → `[wx]` の行、`Date:` でソフト時計を合わせる)・`[err] code=10`(ms 基準に変更。名前は `s_rxerr_ms`))を専用モードと製品で共有。**製品 API**(`APP_WX_PRODUCT`): `wx_init(now_ms)`(`wifi_secret.h` があれば SCI1 を開く。無い・開けない = OFF = ESP32 に何もしない・EN も触らない)、`wx_poll(now_ms)`(毎周回: 受信 → `wifi_core_poll` → 結果の報告 → 完了を予定表へ → `wx_sched_poll` が返した動き(`WIFI_RANGE_CONNECT` か `WIFI_RANGE_WX`)を開始 → `[err] code=10` → `net` / `busy` が変わったら `[wx]` 状態行)、`wx_request(src, now_ms)`(更新ボタン・`x`。受け付けなければ `[wx] req=ignored/busy/off`)、`wx_ui(ui_wx_t *, now_ms)`(表示用の値)、`wx_log_state(now_ms)`(`s` の応答)。専用モードの `x` は `applog_cmd_parse` の `APPLOG_CMD_WX` で受ける(動きは前と同じ)。 |
| `wifi_core.h/.c` | `wifi_core_set_quiet(c, bool)`: 製品は `[wifi] step=... res=start` と `[wifiv] key=oth` を出さない(設計 §22.4 の 6)。既定は出す(専用モード)。ほかは変更なし。 |
| `app_logfmt.h/.c` | `APPLOG_CMD_WX`(`'x'`)。`[help] n=next p=prev s=state d=demo +=up -=down x=wx ?=help`。 |
| `app_main.c` | `x` → `ui_cmd(UI_CMD_WX)`。`APP_WX_PRODUCT`: `wx_init`([init] ×4 と [perf] の後)、`wx_poll`(メインループの毎周回、描画の前)、`tick_step` で `wx_ui` → `ui_set_wx`(`ui_update` の前)、`UI_EV_BTN_ACT` の `UI_BTN_H_WX`(src が touch か uart)→ `wx_request`(`[ev]` の行を出した後)、`s` の応答に `wx_log_state`(`[ui]` の次)、`wx_boot_view`(起動直後・`[perf]` の後の `ui_init` の直後に、旧時計 21:45 ではなく OFFLINE・`--:--` にする)。 |
| `tools/gdb/mi-fbdump-c8.gdb` / `-dev.gdb` | `fb_phase` の次(`g_app_dbg` の前)に `print s_ui.wx` と `print s_ui.spin`(各 7 か所)。`fb2png.py` が読む形。**ほかの `mi-*-fb-c8.gdb`(abort / demo / press / slide)は未変更**(必要なら同じ 2 行を足す)。 |
| テスト | `test_logfmt.c`(`x` と help)、`test_app_main.c`(M7 から `x` を外す、M8 の help、**M22**(`x` の `[ev]`・ホーム以外とデモ中は無視)、**M23a〜e**(`APP_WX` = 1 のビルド。wifi_at は偽物。呼ぶ順序・毎周回・`ui_set_wx` の値・更新ボタン → `wx_request` の src・`s` の行の順序))、`test_wifi_core.c`(**H-X5-14** quiet)、`test_wifi_at.c`(製品版 **P1〜P10**。偽の ESP32 の Date: を可変にし、枠の使い回し・join 失敗・1 tick の行数の記録を追加。`reset_all` の依存を整理)、`test_clut8_cfg.c`(コメントのみ)、`judge_ui.py`(`h_wx`・help・`[wifi]` `[wifiv]` `[wx]` の書式。**C5**: 製品のログに `res=start` と `key=oth` が無い・`[wx] res=ok` の値が範囲内)、`test_judge_ui.py`(`ProductWxTests` 7 件)、`run.sh`(**S26** 追加)。 |

### 2. 設計との差分(理由つき。差し戻しが要るものは無い)

| # | 差分 | 理由 |
|---|---|---|
| 1 | **`x` は `APP_WX` = 0 のビルドでも受け付ける**(`applog_cmd_parse` が `APPLOG_CMD_WX`、`app_main` が `ui_cmd(UI_CMD_WX)` = h_wx の仮想タップ。`[ev] btn=h_wx ...` が出るだけで `[wx]` は出ない)。**`[help]` の文字列は全ビルドで `x=wx` つき**。設計 design-uart §13 の冒頭は「`APP_WX = 0` かつ `APP_WIFI_TEST = 0` の出力・コマンドは変えない」 | 依頼(「`x` を `applog_cmd_parse`・help・`app_main` に通す」)。更新ボタン(h_wx)は `APP_WX` に依らずアセットに入っていて、`x` はそのボタンの仮想タップ(`+` `-` と同じ作り)。ビルドで help を変えると判定(`judge_ui.py` の正規表現)が 2 通りになる |
| 2 | 依頼の「`[ev] btn=wx`、`[ui]` の追記」: ボタン名は **`h_wx`**(`ui_btn_defs` の名前。`log_event` は変更なし)。**`[ui]` の書式は変えない**(設計 design-uart §13.0 の 2)。代わりに `s` で `[wx]` の状態行を `[ui]` の次に 1 行 | 設計どおり |
| 3 | `[wifi] res=start` / `key=oth` の抑止は**実行時のフラグ**(`wifi_core_set_quiet`)。コンパイル時の分岐にしない | ホスト試験が同じ `wifi_core.c` で両方を試験できる(H-X5-14)。専用モードの既定は変わらない |
| 4 | 接続の失敗(`join` など)で **`[wx] res=fail src=<..> err=8 http=0 ms=<n>`** を 1 行出す | design-uart §13.1 の `err=8`。`[wifi] result=fail stage=...` に続く。`wifi_core` は取得の結果(`wx_result_t`)を出さないので、`wifi_at.c` が作る |
| 5 | `wx_init` は `[init] stage=wifi` を出さない(専用モードは出す)。SCI1 を開けない時は `[err] code=9 detail=<sci_err_t>` だけ | 製品の `[init]` を 4 行のまま(UT10〜UT18 の判定を変えない) |
| 6 | **起動直後の表示**(`wx_boot_view`)を足した(設計に無い) | `APP_WX` = 1 で旧時計 21:45 が最初の約 0.3 秒(`[perf]` が無いビルドでは最初のタイマ開始まで)見えるのを避ける。`ui_set_wx` + `ui_update(NULL)` の 2 行。`frm` の回数(初回 1)も変えない |
| 7 | `s_rxerr_tick` → `s_rxerr_ms`(ms 基準。専用モードは `tick * 10`) | 製品は ms で呼ぶ。1 秒に 1 行の規則は同じ |
| 8 | `net` は予定表(`wx_sched_*`)の値をそのまま `UI_NET_*` にする。取得の失敗(20 秒の待ち)も `net=err`(設計 §22.7 の RETRY_WAIT = `err`)。**取得中(FETCH)は直前の `net` を保つ**(`err` のまま再取得中なら、アイコンは `err` + 更新アイコンが回る) | 設計どおり(予定表は第 1 段階で実装済み) |
| 9 | `wx_ui` の `stale` は `age > 1800 秒`(ちょうど 30 分は古くない)。`upd_min` は成功した時のソフト時計の分(`Date:` が無い取得は 0xFFFF のまま) | 設計 §22.5・§22.8 |

### 3. ホスト試験(`bash tests/host/run.sh`)

| 項目 | 結果 |
|---|---|
| 全部(S1〜S26) | **PASS=25 FAIL=0 SKIP=1**(S12 = SKIP: MinGW に libasan / libubsan が無い。**合格ではない**) |
| S26(新規) | 製品の `wifi_at.c`(秘密あり・なし)と `app_main.c` の `-fsyntax-only`、`wifi_at.o` に `wx_*` があり `wifi_test_main` が無いこと、`test_wifi_at.c` 製品版(秘密あり **242 checks**、`-DT_NOSECRET` **22 checks**)、`test_app_main.c` を `APP_WX` = 1 でビルド(**434 checks**。M1〜M21 は `APP_WX` = 0 と同じ判定のまま通る) |
| 製品版 P1〜P10(偽の ESP32) | P1 起動(SCI1 の開き方、EN の順序、3 秒待つ間は何も出さない・UI は接続中・未同期)/ P2 接続 8 step → 取得 5 step(`res=start` と `key=oth` が 0、`gmr` `lapopt` `scan` `dns` `ping` を送らない、`[wx] res=ok src=boot ... jst=132413`、状態行 3 本の順、`ui_wx_t` の値(日付 11/25 水・時計 13:24〜25・`upd_min` 13:24)、**1 tick の行数 ≤ 6**、`drop` 0、秘密・本文なし)/ P3 10 分(60000 tick ± 3)で 1 回だけ `src=timer`・再接続しない / P4 更新ボタン(`x` と `t`): 取得中 `req=busy`、5 秒未満 `req=ignored`、5 秒後は取得、**`Date:` の分・秒を変えると時計が合わせ直される**(`clk=13:40:5x`)/ P5 古い値(31 分で `stale=1`、値は残る、次の成功で戻る)/ P6 取得の失敗(`err=1` → 20 秒 x 2 → 3 回目で再接続、値は残る、直ったら `net=on`)/ P7 join の失敗(`err=8`、`net=err` が約 60 秒、再接続)/ P8 受信 64 バイト / 周回と `[err] code=10` / P9 SCI1 を開けない(`[err] code=9`、OFF、`req=off`)/ P10 秘密なし(SCI1・EN・送信が 0、`req=off`、`net=off`) |
| S22(`test_wifi_core.c` 15,460 checks) | 追加 H-X5-14: quiet で `res=start` と `key=oth` が 0、`res=ok` と `[wifiv] key=ip` と `result` は出る、戻すと出る |
| S8 / S11 / S17 | `test_logfmt` 14,400 checks(`x` → `APPLOG_CMD_WX`、総当たりで他は NONE)、`test_app_main` 392 checks(`APP_WX` = 0。M22 追加)、`judge_ui` + `test_judge_ui`(C5 と `ProductWxTests` を含む) |
| **変異による検出力**(本体は戻してあり `cmp` で確認) | **19 件中 19 件を検出**: ① 更新ボタンが `wx_request` を呼ばない(7 件失敗)② 時計を再同期しない(8)③ 製品で quiet にしない(23)④ `stale` を計算しない(2)⑤ `clk_ext` を 1 にしない(4)⑥ 取得の失敗を成功として返す(4)⑦ 秘密が無くても SCI1 を開く(18)⑧ `ui_set_wx` を呼ばない(6)⑨ `wx_poll` を呼ばない(2)⑩ 受信の上限を 250 に(3)⑪ `oth` の quiet を外す(2)⑫ `res=start` の quiet を外す(3)⑬ `x` を `app_main` が処理しない(**M22 が NULL を参照して異常終了**。ガードを足した)⑭ `s` で `wx_log_state` を呼ばない(4)⑮ `upd_min` を設定しない(1)⑯ 接続の失敗で `err=8` を出さない(1)⑰ 日付を設定しない(1)⑱ `applog_cmd_parse` が `x` を受けない(1)⑲ `[wx]` の行のほかに `Date:` を出す(10) |
| 試験の SSID・パスワード | `SSID_SENTINEL_7c1` / `PW_SENTINEL_9f3`(明らかなダミー。実物は読んでいない) |

### 4. RX 用 GCC での個別コンパイル

`rx-elf-gcc 14.2.0.202607 -Og -Wall -Wextra -Wstack-usage=100 -misa=v3 -mlittle-endian-data -std=gnu99 -ffunction-sections -fdata-sections -c`(`-I` は `src/smc_gen` の各フォルダ)で、`wifi_at.c` `wifi_core.c` `wx_core.c` `app_main.c` を **`APP_WIFI_TEST` 0 / 1 x `APP_WX` 0 / 1 の 4 通り: 警告 0**(スタック使用量の警告も 0)。`wifi_at.c` は `-DWIFI_AT_HOST_TEST`(秘密なし)・`-DAPP_WIFI_AUTOSTART=1` でも 0。`app_logfmt.c` `ui_logic.c` `clut8_test.c` も 0(`clut8_test.o` は 0 バイト)。

| オブジェクト(text / bss) | `TEST`=0 `WX`=0(従来の製品) | `TEST`=0 `WX`=1(製品 + 取得) | `TEST`=1(専用モード) |
|---|---|---|---|
| `wifi_at.o` | **0 / 0** | 1,680 / 2,534 | 1,572 / 2,558(`AUTOSTART` = 1: 1,612) |
| `wifi_core.o` | **0** | 7,791 | 7,791 |
| `wx_core.o` | **0** | 5,221 | 5,221 |
| `app_main.o` | 2,433 / 551(従来 2,409 + `x` の 1 case) | 2,590 / 567 | 2,433 / 551 |
| `app_logfmt.o` | 2,878(`x` の case と help の 5 バイト) | 同じ | 同じ |

ROM(text + data)の見積: UI 段階の約 1,186,000(`APP_WX` = 0)+ `wifi_core` 7.8KB + `wx_core` 5.2KB + `wifi_at` 1.7KB + `app_main` 0.16KB = **約 1,201,000**(基準 ≤ 1,250,000)。`.bss` は +2,534(`wifi_at`)+ 16(`app_main` の `s_wxv`)= 約 +2.6KB(基準の増加 ≤ 3.2KB)。**実測はビルド担当**。

### 5. ビルド担当への注意

- **新規の .c は無い**。`wx_core.c` は第 1 段階で追加済み(makefile に無ければヘッドレスで作り直す)。`APP_WX = 0` かつ `APP_WIFI_TEST = 0` では `wifi_core.o` `wx_core.o` `wifi_at.o` が 0 バイト(従来の ROM と同じはず。リンク後の map でも確認)。
- **製品(取得つき)のビルド = `-DAPP_WX=1`**(`APP_WIFI_TEST` は 0 のまま)。切り替えは `wifi_sw.h` の既定の値を書き換えるか、コンパイラの `-D`。**コミットする値は 0**(合格後に 1 にする。設計 §22.16 の順 8)。`APP_WIFI_TEST = 1`(専用モード)は従来どおり(`x` で取得)。
- ROM の確認基準は **text + data ≤ 1,250,000**(見積 約 1,201,000)、`.bss` の増加 ≤ 3.2KB。`wifi_secret.h` があると、その SSID・パスワードが **ELF・MOT・map に平文で入る**(他人に渡さない。`.gitignore` 済み)。`wifi_secret.h` が無いビルドは OFFLINE で動く(`__has_include`)。ログ・報告に SSID・パスワードを書かない。

### 6. デバッグ担当への注意(実機: 設計 §22.13 の X0〜X13、design-uart §13.5 の UX0〜UX6)

- **順序**: ① 専用モード(`-DAPP_WIFI_TEST=1`)で `d` → `x`(**X0。RW-X1 の `key=oth` の形を記録**。これが通らなければ製品へ進まない)→ ② 製品(`-DAPP_WX=1`)。
- **製品の `[wx]` を見る行**: `[wx] net=conn busy=1 ...`(接続開始)→ `[wifi] step=reset ... ip`(ok だけ。`res=start` は無い)→ `[wifi] result=pass` → `[wx] net=on busy=1 ...` → `[wifi] step=wx_mux ... wx_recv`(ok)→ `[wifi] result=pass` → **`[wx] res=ok src=boot temp10=.. wmo=.. kind=.. http=200 ms=.. sync=1 jst=hhmmss obs=hhmm closed=1`** → `[wx] net=on busy=0 have=1 ...`。`x`(UART)は `[ev] btn=h_wx st=down src=uart` → `st=act` → `[wx] res=ok src=uart`(5 秒以上あける。5 秒未満は `[wx] req=ignored src=uart`、取得中は `req=busy`)。`s` は `[st]` `[ui]` の次に `[wx] net=... clk=hhmmss date=yyyymmdd wd=<n>`。**`[hb]` は変えていない**(`err=0 drop=0 uf=0` の判定は UT10 と同じ)。`serial` ジョブ / `serial-read.ps1 -Stamp` の受信時刻(PC の JST)と `jst=` / `clk=` を比べて ±3 秒(**X3**)。系統的にずれたら `WX_SYNC_BIAS_MS`(wx_core.h。既定 700)を 1 回だけ調整。
- **PC の `curl` との照合(X4)**: 設計 §22.2 の URL を `curl` で叩き、`current.time` と `[wx]` の `obs` を比べて `temp10` `wmo` を照合(個人情報なし)。
- **`s_ui.wx` への注入(X7。エラー・古い値・OFFLINE の表示)**: `APP_WX` = 1 では `tick_step` が **10ms ごとに `ui_set_wx` で `s_ui.wx` を上書き**するので、`s_ui.wx` に書いても消える。**元の値を書く**(CPU を止めて `set var` → 再開 → 数十 ms 後に表示が変わる。`mi-fbdump-c8.gdb` の `-exec-interrupt` の後に `print s_ui.wx` で確認): OFFLINE = `set var s_sched.net = 0`、接続中 = `1`、ONLINE = `2`、エラー = `3`(`s_sched` は `wifi_at.c` の static。IDLE の間は次の変化まで保たれる。**`s_sched.state` は書かない**(取得が始まる))。古い値 = `set var s_wx_ok_ms = s_wx_ok_ms - 1860000`(31 分前。次の成功で戻る)。値 = `set var s_wx_have = 1`、`s_wx_temp = -34`(0.1℃)、`s_wx_kind = 4`(0 sun 1 partly 2 cloud 3 fog 4 rain 5 snow 6 thunder 7 unknown)。未同期の時計 = `set var s_clk.synced = 0`。**取得中(更新アイコンの回転)**は取得を実際に起こす(`x`)か、**`APP_WX` = 0 のビルド**で `s_ui.wx.busy = 1`(`APP_WX` = 0 は `ui_set_wx` が呼ばれないので `s_ui.wx` の書き込みが残る。`s_ui.wx.clk_ext = 1` も書けば `--:--`・日付の表示も見える)。`fb2png.py` は `print s_ui.wx` / `print s_ui.spin` のログを読んでホスト描画(`fb_hostrender`)に同じ状態を渡す。
- **デモ(33 秒)・`[perf]`**: UT10〜UT19 と `[perf]`(`home_us` ≤ 1,400)を `APP_WX` = 1 で取り直す。デモ中に定期取得(10 分)が重なる確認は X9。**`[err] code=10`**(SCI1 の受信異常)・`drop` が増えたら報告(ログのキューは 8 行。取得の完了時に 4 行が同じ tick に出る)。
- **秘密**: 受信ログに SSID・パスワード・応答の本文(`"latitude"` `"generationtime_ms"` など)・`Date:` が**無い**ことを一致数だけ報告(X13)。`wifi_secret.h` を開かない。

### 7. 未実施・リスク

- **ヘッドレスビルド・リンク・書込み・実機・LCD の目視・カメラは未実施**。ESP32 1.1.3.0 の TCP の書式(`>` の形、`Recv n bytes`、`+IPD`、`CONNECT`)と `Date:` の精度は、専用モードの X0 で確認するまで未確認(RW-X1・RW-X5)。
- **ログのキュー(8 行)**: 接続の終わり(`[wifiv] key=ip` + `step=ip ok` + `result` + `[wx] net=` + ...)と取得の終わり(`step=wx_recv ok` + `result` + `[wx] res=ok` + `[wx] net=`)で 1 tick に最大 4〜5 行(ホストの偽ログで ≤ 6 を確認)。**実機のキューは 800 バイトの送信で 1 行ずつ**なので、`drop` が増える可能性は X1・X9 で確認(増えたら状態行を間引く)。
- 取得中の 1 周回の CPU 時間・描画との競合(`[perf]` の悪化、`uf`)・SCI1 の受信バッファ 3500 バイトが足りるか(約 17 バイト / 1.5ms の見積)は実機で未確認(X10・X11)。
- `wx_boot_view` は `ui_update(NULL, now)` を `ui_init` 直後に呼ぶ。`APP_PERF_BENCH` = 1 の `[perf]` の測定中(`s_ui` を使う)は影響しない(`log_perf` の最後の `ui_init` の後で呼ぶ)。
- 更新ボタンの実タッチ・当たり判定(228,136,60,52)・UT15・UT19 は**ユーザーの協力**(X5・X9)。
- `x` は `APP_WX` = 0 でも help に出る・`[ev] btn=h_wx` を出す(差分 1)。ユーザーが `APP_WX` = 0 で help の文字列が変わるのを嫌う場合は設計役へ(`judge_ui.py` の 1 行と `app_logfmt` の定数)。
- Open-Meteo の利用条件(非商用の可能性。設計 §22.17 の 1)はユーザーの確認待ち(コードに影響しない)。
