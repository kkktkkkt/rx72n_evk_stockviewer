# Smart Configurator 生成状態の確認結果

確認日時: 2026-11-19 22:15(ユーザーがSCで生成した後の `rx72n_gcc.scfg` と `src/smc_gen/` を読んで確認)

## 1. 結論

| 項目 | 状態 |
|---|---|
| 生成とビルド | 済み(`HardwareDebug/rx72n_gcc.elf` あり。ユーザーがデバッガ接続も成功) |
| LCDの端子 | **想定どおり**。20信号(CLK, TCON0/2/3, DATA0-15)が `docs/hardware-notes.md` の表と一致 |
| UART(r_sci_rx) | **要確認**。SCI1 の端子が **Wi-Fiモジュール用の配線**と一致。PCに繋がるUARTではない可能性が高い |
| タッチ用I2C | **未追加**。`r_sci_rx` はUARTでI2Cは扱えない。`r_sci_iic_rx`(SCI簡易I2C)またはRIICが必要 |
| QE for Display モジュール | 追加済みだが、UARTチャネル設定(SCI0)と有効なSCI(SCI1)が**不一致** |
| GLCDC設定 | 既定値のまま。パネル値との照合が必要(設計担当が調査中) |
| バックライト/DISP/タッチRSTの端子 | **未設定**(GPIO。ポート名が未確認) |

## 2. 追加されているコンポーネント(`.scfg`)

| コンポーネント | バージョン | 備考 |
|---|---|---|
| r_bsp | 7.71 | |
| r_glcdc_rx | 1.62 | `GLCDC_CFG_CONFIGURATION_MODE = 0`(構造体をユーザーコードで埋める方式) |
| r_qe_display_rx | 1.12 | QE for Display[RX]用。`QE_DISPLAY_CFG_UART_CH = 0`、115200bps、デバッグ出力 無効 |
| r_sci_rx + r_byteq | 5.61 / 2.21 | SCI1のみ有効(調歩同期)。バッファ80バイト |
| r_gpio_rx | 5.31 | |
| r_cmt_rx | 5.81 | |

## 3. 端子の生成結果

LCD(全て機能コード 0x25): PB5=LCD_CLK、PB4=TCON0、PB2=TCON2、PB1=TCON3、PB0=DATA0、PA7〜PA0=DATA1〜8、PE7〜PE1=DATA9〜15。

SCI1(調歩同期): `.scfg` の割り当てと、ボードの配線名の対応は次のとおり。

| `.scfg` の端子 | ピン番号 | そのピンのボード信号名 | 意味 |
|---|---|---|---|
| RXD1(P30) | 29 | WiFi-TXD | Wi-Fiモジュールの送信線 |
| TXD1(P26) | 31 | WiFi-RXD | Wi-Fiモジュールの受信線 |
| CTS1#(P31系) | 28 | WiFi-RTS | Wi-Fiモジュールのフロー制御線 |

→ SCI1はオンボードのWi-Fiモジュール(ESP32)に繋がる。**PCへのログ出力には使えない可能性が高い**。PCに繋がるUART(USBシリアル)があるかどうかは未確認(`docs/board-setup-notes.md` §3)。

## 4. 見つかった問題と対処案

| # | 問題 | 対処案 | 担当 |
|---|---|---|---|
| 1 | SCI1がWi-Fi配線で、PCに繋がらない | PCに繋がるUARTがあれば、そのチャネルへ変更。無ければログ出力はE2 Lite経由(デバッガ)か不要とする | 設計 → ユーザー(SC操作) |
| 2 | `QE_DISPLAY_CFG_UART_CH` = 0 だが SCI0 は無効 | QE for Display を使うなら、PC接続のUARTチャネルに合わせる。使わないなら `r_qe_display_rx` を外してよい | 設計 |
| 3 | タッチ用I2Cドライバが無い | `r_sci_iic_rx`(チャネル6 = P33/P32 の仮説)を追加。要確認 | 設計 → ユーザー(SC操作) |
| 4 | GPIO端子(バックライト、DISP、タッチRST)の設定が無い | ポート確認後、コードで `R_GPIO_*` を使うか、SCの端子タブで設定 | 設計 → コーディング |
| 5 | GLCDCの既定タイミングがパネルに合う保証がない | パネルのデータシートと照合。ずれていれば QE for Display で調整 | 設計 |

## 5. QE for Display を使うべきか

| 観点 | 内容 |
|---|---|
| QE for Display とは | e² studio の拡張ツール。LCDのタイミングや画像レイヤをGUIで調整し、`r_lcd_timing.h` と `r_image_config.h` を生成する。`r_qe_display_rx` を使うと、PC上のQEとUARTで接続して実機の表示をその場で調整できる |
| LLMから操作できるか | **できない**(GUIで、e² studioへの入力が現状届かない)。ただし生成物はただのヘッダなので、値さえ分かれば手で同じ設定にできる |
| 推奨 | **まず使わない**。パネルのデータシートの値で設定して表示を確認する。画面が乱れる・映らないときだけ、ユーザーがQEで調整する。その場合はPCに繋がるUARTが必要 |

---

## 追記: 2026-11-19 22:27 SC再生成後の確認(ユーザーが r_sci_iic_rx 追加+SCI2設定をして再生成)

| 項目 | 結果 |
|---|---|
| `r_sci_iic_rx` | 追加済み(2.91)。`SCI_IIC_CFG_CH6_INCLUDED = 1`(他は0)。ビットレート 384000bps(CH6)。`SCI_IIC_CFG_PORT_SETTING_PROCESSING = 1`(ポート設定はドライバが行う) |
| SCI6 端子 | `Pin.c` で P33 = SSCL6、P32 = SSDA6(機能コード 0x0A) ◎ 回路図どおり |
| `r_sci_rx` | CH1 と **CH2 が有効**。`R_SCI_PinSet_SCI2()` で P12 = RXD2、P13 = TXD2 ◎ 回路図どおり。CH1(Wi-Fi配線)は有効のまま残るが害はない(P30/P26 を割り当てるだけ) |
| `r_qe_display_rx` | `QE_DISPLAY_CFG_UART_CH = 2`(SCI2に一致) ◎ |
| `main` | 空のまま(`src/rx72n_gcc.c`)。アプリ未接続 |
| ビルド | **未実施**。`HardwareDebug/rx72n_gcc.elf` は 13:07 のまま(再生成前)。ビルド結果は未確認 |
| 注意 | `R_Pins_Create()` と `R_GLCDC_PinSet()` が別に存在する。設計書 §15 のとおり、アプリが `R_GLCDC_PinSet()` を呼ぶ必要があるか、`R_Pins_Create()` がLCD端子も設定済みか(上の `Pin.c` に LCD_TCON3 設定あり)は、コーディング担当が実在を確認する |
