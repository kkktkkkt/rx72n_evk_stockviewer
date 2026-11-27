# 設計書: UART状態出力と遠隔操作(rx72n_gcc)

作成: 2026-11-20 / 更新: 2026-11-20 (画像4枚化 `APP_IMAGE_COUNT=4` に伴う UT4/UT6/U-B3 更新) / 設計担当 / 状態: **v1 設計(実機未検証)**
位置づけ: **UART出力・受信コマンドの最優先仕様**。`docs/design.md` の §4 B と §10(UARTログは「任意・既定で無効」)は、本書で「**既定で有効**」に置き換える。
前提資料: `docs/design.md`, `docs/sc-generated-state.md`, `docs/cli-debug.md`, `docs/schematic-findings.md`, `docs/implementation-notes.md`(付記 2026-11-20)
**2026-11-21 追記: §10(製品デモ向けの追加仕様)を末尾に追加。製品デモ(`docs/design.md` v2 の P1)では、§2.2 の `[hb]` `[ev]` `[help]`、§2.3、§3 の `n` `p` の意味、§6.4 の正規表現、UT4 / UT6 / UT7 は §10 が優先する(置き換わる)。P2 のギャラリー(CLUT8)では `[ev] img=N src=...` と `n` `p` は従来どおり。§1・§2.1・§2.4・§2.5・§4・§5・§6.1〜§6.3 は変更なし。**
**2026-11-21 追記(2): §11(CLUT8 の実機実験 = `docs/design.md` §19。`APP_CLUT8_TEST = 1` のファームだけ)を末尾に追加。製品デモ(= 0)の出力・コマンドは変えない。**
**2026-11-25 追記(3): §12(Wi-Fi の ESP32 AT 試作 = `docs/design.md` §21。`APP_WIFI_TEST = 1` のファームだけ)を末尾に追加。製品デモの出力・コマンドは変えない。**
確認元: 実ファイル `src/app/*`、`src/smc_gen/r_sci_rx/*`、`src/smc_gen/r_config/*`(2026-11-20 11:14 時点)。「計算」と書いた数値は手計算で、実測ではない。

---

## 0. 結論(まず読む)

| # | 決定 | 内容 |
|---|---|---|
| 1 | やること | SCI2(CN8、PCの COM7)へ、**1行1イベントのテキスト**を出す。起動時に数行、**1秒ごとにハートビート `[hb]`**、画像切替 `[ev]` とエラー `[err]` は起きたときだけ |
| 2 | 受信 | **1文字コマンド** `n` `p` `s` `?`。改行不要。未知の文字は無視 |
| 3 | 形式 | `[タグ] key=value key=value ...`、行末 `\r\n`、ASCIIのみ、**最長 192 バイト(NUL込み)** |
| 4 | あふれ | 送信キュー(8行)が満杯・整形に失敗した行は**捨てる**。捨てた累計を `[hb]` の `drop=N` に載せる |
| 5 | 受信の処理 | コールバックでは**何もしない**(エラー印を立てるだけ)。メインループで **1tick(10ms)に1文字**だけ処理 |
| 6 | 画像切替の経路 | タッチも UART も **`nav_step()`/`nav_update()` → 共通関数 `apply_image()`**(`lcd_show` + `g_app_dbg.image_index` + `[ev]`)。LCDも切り替わる |
| 7 | 純粋関数 | 既存の `app_logfmt.h/.c` を**拡張**(整形・コマンド解釈・周期判定)+ 新規 `app_logq.h/.c`(行キュー。ホストでテスト可) |
| 8 | SCI2 割り込み優先度 | **2**(現実装は 3)。タッチI2C(SCI6、優先度2)の割り込みを UART が追い越さないようにする |
| 9 | PC側 | `tools/job-runner.ps1` の `serial` に**送信機能**(`tx` `txAtSec` `txGapMs` `dtr`)を足す(§6.1)。今のランナーは**受信専用** |
| 10 | UARTで判定できないこと | **画面に実際に何が映っているか**(色・欠け・ちらつき)。`[init] stage=lcd ok=1` は「GLCDCの起動関数が成功した」までしか言えない。目視(B1〜B3)は残る |

**現在のコーディング担当の実装(2026-11-20 11:10)との差分は §8。** 主な差: タグ `[img]`→`[ev]`、`err=0/0`→`err=0 detail=0`、`[init]`/`[err]`/`[help]`/`[scan]` の追加、`drop=`、`?` コマンド、`nav_step` 経由、1tick1文字、キュー 4行→8行・行長 120→192、build 値の空白を `_` に。

---

## 1. 目的と判定基準

### 1.1 目的

| 項目 | 内容 |
|---|---|
| 課題 | LCDの表示だけでは、Claude Code が実機の動作を判定できない。CLIから「実行再開」もできない(`docs/cli-debug.md` §7.9)ので、gdb で変数を読む方法も使いにくい |
| 解決 | UART で状態を文字で出す。PC側ランナー(`tools/submit-job.sh serial ...`)が受信し、Claude Code が正規表現で合否を判定する |
| 遠隔操作 | UART で `n`/`p` を送ると画像が切り替わる。タッチなしでテストできる |
| 使えない場面 | **gdb を繋ぐとCPUが止まる**(`docs/debug-report.md` §4)。`serial` ジョブと gdb ジョブを同時に使わない。順序は「書込み → ユーザーがリセット → serial」 |

### 1.2 「この出力が見えたら、何が動いている」対応表

| 段階 | 見える出力 | 判定できること | 判定できないこと |
|---|---|---|---|
| A. 電源・配線 | 何か1行でも正しく読める(文字化けなし) | CN8 → RL78 → SCI2 の経路、COM7、ボーレートが生きている | — |
| B. 起動 | `[boot] fw=rx72n_gcc build=... ch=2 baud=115200` | `app_main` に到達。`R_SCI_Open`(SCI2)成功。端子 P12/P13 の設定が有効 | LCD・タッチは未初期化の時点 |
| C. LCD初期化 | `[init] stage=lcd ok=1 err=0 detail=0` | `R_GLCDC_Open` と `START_DISPLAY` が成功し、DISP/バックライトのGPIOを操作した | **映像が正しいか**(目視) |
| C'. LCD失敗 | `[init] stage=lcd ok=0 err=1 detail=N`(Open失敗)または `err=2`(Start失敗) | どの段階で失敗したか。`detail` = `glcdc_err_t` | — |
| D. タッチ初期化 | `[init] stage=touch ok=1 err=0 detail=0` | `R_SCI_IIC_Open`(SCI6)成功 | **タッチICが応答するか**(次の行) |
| D'. タッチ初期化失敗 | `[init] stage=touch ok=0 err=3 detail=N` | I2Cドライバを開けない。タッチ無効の静止画で続行 | — |
| E. タイマ | `[init] stage=timer ok=1 err=0 detail=0` | `R_CMT_CreatePeriodic`(10ms)成功 | — |
| E'. タイマ失敗 | `[init] stage=timer ok=0 err=4 ...` + `[err] code=4 ...`、以後 `[hb]` が出ない | 周期が作れず停止している | — |
| F. メインループ | `[init] stage=loop ok=1 ...` の後、**約1秒おきの `[hb]`**、`tick` が +100 ずつ増える | CMT割り込みとメインループが走っている(ハングしていない)。`tick` は10ms単位 | — |
| G. タッチI2C | 連続する `[hb]` で `i2c_fail` が**変わらず**、`streak=0` | I2C読み出しが失敗していない(段階Dが成功している前提) | ICの値が正しいか(指を置くまで不明) |
| G'. I2C異常 | `i2c_fail` が増える、`streak` が増える、`[err] code=5/6 ...` | タイムアウト(5)または開始失敗/NACK(6)。10回連続失敗で `[scan]` 行(応答アドレス) | — |
| H. タッチ検出 | 指を置いている間の `[hb]` で `pts=1`、`x` `y` が変化。`s` の後の `[st]` に `xmin..ymax` | タッチICが座標を返している。座標範囲(B8)も読める | 短いタップは1秒周期の `[hb]` に写らない(`[ev]` で見る) |
| I. 画像切替 | `[ev] img=N src=touch` または `src=uart` と、次の `[hb]` の `img=N` | `nav` の状態更新と `lcd_show`(RAM2へのコピー)が**完了**した(`[ev]` は `lcd_show` の後に出す) | **LCDに正しい絵が出たか**(目視) |

### 1.3 `err` の読み方(重要)

| 事実 | 意味 |
|---|---|
| `g_app_dbg.err_code` は**最新の異常で上書きされ、成功しても 0 に戻らない**(`touch_ft5x.c` を確認: 成功時は `i2c_fail_streak = 0` だけ) | `[hb]` の `err=5` は「過去に1回タイムアウトした」の意味で、「今も失敗中」ではない |
| 「今も失敗中か」の判定 | **`i2c_fail` が次の `[hb]` で増えているか**、`streak` が 0 でないか。`err` だけで判断しない |
| 起動時の失敗 | `[init]` の `ok=0` 行と、その直後の `[err]` の両方に出る(重複は仕様) |

---

## 2. 出力形式

### 2.1 共通ルール

| 項目 | 規則 |
|---|---|
| 文字 | ASCII 0x20〜0x7E と行末のみ。日本語・タブ・制御文字は出さない |
| 行末 | `\r\n` |
| 1行の長さ | 最大 **191 バイト + NUL**(= `APPLOG_LINE_MAX` 192)。`\r\n` を含む |
| 区切り | タグの後に空白、`key=value` を空白1つで区切る。値に空白を含めない。キーの**順序は固定**(下表のとおり) |
| 数値 | 10進、符号なし、先頭ゼロなし |
| 1イベント1行 | 複数行にまたがるイベントは作らない(例外: `s` は `[st]` と、あれば `[scan]` の2行) |
| 部分行を出さない | 整形に失敗した行(バッファ不足)は**まるごと捨てる**。途中で切れた行を送らない |

### 2.2 行の種類

| タグ | 形式 | いつ出す | 備考 |
|---|---|---|---|
| `[boot]` | `[boot] fw=rx72n_gcc build=<id> ch=2 baud=115200` | `app_main` の最初(UARTを開いた直後。LCD/タッチ初期化の前)に**1回** | `<id>` = `__DATE__` と `__TIME__` をつないだ文字列の**空白を `_` に置換**(例 `Nov_20_2026_11:10:04`)。最大31文字、範囲外の文字も `_` |
| `[init]` | `[init] stage=<lcd\|touch\|timer\|loop> ok=<0\|1> err=<n> detail=<n>` | `lcd_init`/`touch_init`/タイマ作成の各**直後**に1行ずつ。ループに入る直前に `stage=loop ok=1` | `ok=1` のときは `err=0 detail=0` 固定。`ok=0` のときは `g_app_dbg.err_code/err_detail` の値 |
| `[hb]` | `[hb] tick=<n> img=<n> err=<n> detail=<n> pts=<n> x=<n> y=<n> i2c_fail=<n> streak=<n> found=<n> drop=<n>` | **100 tick(= 1秒)ごと** | 項目の出どころは §2.3。最長 144 バイト(計算) |
| `[st]` | `[st] ` + `[hb]` と同じ11項目 + ` xmin=<n> xmax=<n> ymin=<n> ymax=<n>` | `s` を受けたとき即時 | 触れたことがなければ `xmin=65535 xmax=0 ymin=65535 ymax=0`。最長 188 バイト(計算) |
| `[scan]` | `[scan] n=<n> a0=0x38 a1=0x..` | `s` を受けたとき、`found>0` の場合だけ `[st]` の直後に1行 | `found_addr[]`(最大4個)。アドレスは小文字16進2桁 |
| `[ev]` | `[ev] img=<N> src=<touch\|uart>` | **画像番号が変わったとき**。`lcd_show` が戻った**後**に出す | 画像が1枚のとき(番号が変わらない)は出さない |
| `[err]` | `[err] code=<n> detail=<n>` | §2.4 の条件 | `code` は `APP_ERR_*`。7 は UART 受信異常(§2.4) |
| `[help]` | `[help] n=next p=prev s=state ?=help` | `?` を受けたとき | 固定文字列 |

### 2.3 `[hb]` の項目(`g_app_dbg` の既存フィールドのみ。`drop` だけ UART自身の値)

| キー | 出どころ | 備考 |
|---|---|---|
| `tick` | `g_app_dbg.tick` | **10ms単位**。1秒で +100 |
| `img` | `g_app_dbg.image_index` | |
| `err` / `detail` | `err_code` / `err_detail` | 最新の異常。0 に戻らない(§1.3) |
| `pts` | `raw_points` | 触れていないとき 0 |
| `x` / `y` | `raw_x` / `raw_y` | 最後に触れた座標のまま残る |
| `i2c_fail` | `i2c_fail_total` | 累計。**増加=今失敗している** |
| `streak` | `i2c_fail_streak` | 連続失敗数(最大255)。成功で 0 |
| `found` | `found_count` | アドレススキャンで応答した数 |
| `drop` | UART送信キューの破棄**累計**(`app_logq` の破棄カウンタ) | 0 から始まり、減らない。リセットで 0 に戻る |

- `[st]` の `xmin` `xmax` `ymin` `ymax` は `x_min` `x_max` `y_min` `y_max`。
- **`drop` を累計にする理由**: `[hb]` 自体が捨てられても、次の `[hb]` で取りこぼしが分かる。2本の `[hb]` の `drop` が同じなら、その間に捨てた行は無い。

### 2.4 `[err]` を出す条件

| 条件 | 出す内容 | 頻度の上限 |
|---|---|---|
| (A) `g_app_dbg` の `(err_code, err_detail)` が、**最後に出した値**(初期値 `0,0`)と違う | `[err] code=<err_code> detail=<err_detail>` | 100ms(10tick)に1行まで。保留中は最新値だけ出す |
| (B) UART受信ドライバの異常(コールバックが `SCI_EVT_RXBUF_OVFL`=2、`SCI_EVT_FRAMING_ERR`=3、`SCI_EVT_PARITY_ERR`=4、`SCI_EVT_OVFL_ERR`=8 を受けた) | `[err] code=7 detail=<イベント値>` | 1秒に1行まで |
| (C) タイマ作成失敗(`APP_ERR_TIMER`) | (A)と同じ書式で `[err] code=4 detail=0` を出してから停止 | 停止前に1回 |

- (B) の `code=7` は **`g_app_dbg.err_code` に書かない**(I2C等の状態を隠さないため)。`app_debug.h` に `APP_ERR_UART_RX (7u)` の定数だけ足す。
- イベント値(2,3,4,8)は `r_sci_rx_if.h` の `sci_cb_evt_t` の並び順(TEI=0, RX_CHAR=1, RXBUF_OVFL=2, FRAMING_ERR=3, PARITY_ERR=4, RX_CHAR_MATCH=5, XFER_DONE=6, XFER_ABORTED=7, OVFL_ERR=8)から数えた値(ヘッダを読んで確認、◎)。実装が列挙子を直接使う場合は数値を信じず列挙子を使う。
- `SCI_EVT_RX_CHAR`(1バイト受信のたびに呼ばれる)では何もしない。

### 2.5 バッファあふれ・失敗時の扱い

| 事象 | 扱い | 数える? |
|---|---|---|
| 行キュー(8行)が満杯のとき新しい行を追加 | **新しい行を捨てる**(古い行は残す) | `drop` +1 |
| 整形関数が 0 を返した(バッファ不足・引数不正) | 何も送らない | `drop` +1 |
| `R_SCI_Send` が `SCI_ERR_XCVR_BUSY`(送信中) | **捨てない**。行を先頭に残し、次のループで再試行 | 数えない |
| `R_SCI_Send` が BUSY 以外のエラー(`INSUFFICIENT_SPACE` など) | その行を捨てて次へ | `drop` +1 |
| `R_SCI_Open` 失敗 | UART機能を無効のまま続行(出力なし)。LCD/タッチ動作は妨げない | 数えない(キューを使わない) |
| `drop` が 2^32 に達する | 0xFFFFFFFF で止める(飽和) | — |

- `SCI_CFG_USE_CIRCULAR_BUFFER = 0`(`r_sci_rx_config.h` で確認)のため、**`R_SCI_Send` は送信中は BUSY を返す**(`r_sci_rx.c` の `sci_send_async_data` を読んで確認)。そのため、アプリ側の行キューで1行ずつ渡す。
- `tx_idle` が true に戻るのは、キューの**最後の1バイトを送信レジスタに移した時点**(`sci_transfer`)。次の行を渡せるのは、前の行のほぼ全部が出た後。

---

## 3. 受信コマンド

### 3.1 コマンド一覧

| 送る文字(1バイト) | 動作 | 出力 |
|---|---|---|
| `n` | 次の画像(端は循環。0→1→2→3→0)。LCDも切り替わる | `[ev] img=<N> src=uart` |
| `p` | 前の画像(0→3→2→1→0) | `[ev] img=<N> src=uart` |
| `s` | 状態を即時出力 | `[st] ...`、`found>0` なら続けて `[scan] ...` |
| `?` | ヘルプ | `[help] n=next p=prev s=state ?=help` |
| 上記以外(大文字、改行 `\r` `\n`、空白、数字、文字化けなど) | **無視**。応答なし、エラーにしない | なし |

- 改行は要らない。端末ソフトが Enter で `\r\n` を送っても、無視されるだけで害はない。
- 大文字(`N` など)は**受け付けない**(誤動作を避けるため)。
- 画像が1枚だけ(`APP_IMAGE_COUNT == 1`)のとき `n`/`p` は何も変えず、`[ev]` も出さない。

### 3.2 処理の流れ(割り込みを軽く保つ)

| 段 | 場所 | やること | やらないこと |
|---|---|---|---|
| 1 | SCI2 RXI 割り込み(`r_sci_rx` 内) | 受信バイトを `r_sci_rx` の受信キュー(CH2 = **800バイト**)へ入れる | — |
| 2 | アプリのコールバック(割り込み内) | 異常イベント(§2.4 B)のときだけ、`volatile` 変数に**イベント値を書く**。それだけ | `[err]` の出力、`R_SCI_Send`、整形、`lcd_show`、ループ、ログ |
| 3 | メインループ(10ms の tick を検出した後) | `R_SCI_Receive(hdl, &c, 1)` を**1回だけ**呼ぶ。`SCI_SUCCESS` なら1文字をコマンドに変換して実行。`SCI_ERR_INSUFFICIENT_DATA`(空)なら何もしない | 1 tick に2文字以上処理すること |
| 4 | 実行 | `n`/`p` → §3.3、`s`/`?` → 行を組み立ててキューへ | — |

- **1 tick に1文字**にする理由: `n` を連打されても、`lcd_show`(261,120バイトのコピー)が 100回/秒を超えない。タッチの10ms周期が深刻に遅れるのを防ぐ。連打の溜まりは受信キュー(800文字)が受ける。
- 受信キューがあふれたとき(800文字超): 古い文字は残り、新しい文字が捨てられ、`SCI_EVT_RXBUF_OVFL` で `[err] code=7 detail=2`(§2.4 B)。

### 3.3 画像切替の共通経路(タッチと UART を同じにする)

| 項目 | 規則 |
|---|---|
| 状態の持ち主 | **`nav_t.index` だけ**が画像番号の正。UART も `nav_t` を通して変える(`nav->index` を外から直接書かない。現実装の `nav->index = idx;` は禁止) |
| 新規関数(純粋・標準Cのみ) | `bool nav_step(nav_t *nav, int8_t dir);` を `nav_logic.h/.c` に追加。`dir > 0` で次、`dir < 0` で前、`dir == 0` は何もしない。番号が変わったら true。`nav == NULL` は false。画像数1なら false |
| 既存コードの整理 | `nav_act` の「±1して循環」の部分を `nav_step` と**同じ内部関数**にする(計算を1か所にする)。`nav_update` の挙動(H1〜H7)は**変えない** |
| 間隔規則(300ms) | **`nav_step` には適用しない**(遠隔テストが無視されないように)。`last_action_ms` と状態機械(`IDLE` 等)も**変えない**。指が触れている最中に `n` が来ても、タッチ側の判定は独立に続く |
| 共通関数 | `app_main.c` 内の `static void apply_image(nav_t *nav, const char *src)`: ① `lcd_show(g_images[nav_index(nav)])` ② `g_app_dbg.image_index = nav_index(nav)` ③ `[ev] img=N src=<src>` をキューへ。**タッチ(`nav_update` が true)も UART(`nav_step` が true)も、必ずこれを呼ぶ** |
| 出力のタイミング | `[ev]` は `lcd_show` が**戻った後**(コピー完了後)に出す |
| `applog_step_index`(現 `app_logfmt`) | **廃止**(`nav_step` に一本化) |

---

## 4. 負荷とタイミング

### 4.1 数値(計算。実測ではない)

| 項目 | 値 | 根拠 |
|---|---|---|
| 1バイトの送信時間 | 約 86.8 µs | 10bit(開始1+データ8+停止1) / 115200 |
| 典型的な `[hb]` | 92 バイト → 約 8.0 ms | 例 `tick=123456 ... drop=0` を数えた |
| 最長の `[hb]` / `[st]` | 144 / 188 バイト → 約 12.5 / 16.3 ms | 全項目が最大値のとき |
| `[ev]` | 22 バイト → 約 1.9 ms | |
| 通常時の回線使用率 | 約 0.8%(`[hb]` 毎秒 1行) | 8.0ms / 1000ms |
| `r_sci_rx` 送信バッファ(CH2) | 800バイト、約 69 ms 分 | `SCI_CFG_CH2_TX_BUFSIZ` |
| アプリの行キュー | 8行 × 192バイト = 1,536バイト | RAM(512KB)に対し無視できる |
| ボーレート誤差 | 約 **-0.22%** | PCLKA = 120MHz(`r_bsp_config.h` のコメント)、ドライバの式(`r_sci_rx72n.c`: 除数6、BRR=173)から手計算。相手側(RL78)の誤差は未確認 |

### 4.2 メインループの順序(これを守る)

| 順 | 処理 | 備考 |
|---|---|---|
| 1 | `app_log_poll()` | **毎周回**呼ぶ。キュー先頭を `R_SCI_Send`。BUSY なら即戻る(待たない) |
| 2 | 10ms の tick が進んでいなければ `continue` | 現実装どおり |
| 3 | `g_app_dbg.tick` 更新 | |
| 4 | ハートビート判定(`applog_due(now, last_hb, 100)`)→ `[hb]` をキューへ | **タッチ処理より前**(`touch_ok` 偽・`touch_read` 失敗の `continue` に飛ばされない) |
| 5 | `[err]` 判定(§2.4 A, B) | |
| 6 | UART受信を **1文字**(§3.2) | `touch_ok` 判定より前 |
| 7 | `touch_ok` が偽なら `continue` / `touch_read` / `nav_update` / 変化があれば `apply_image` | 既存どおり |

- **タイマ失敗の停止ループ**(`for (;;)`)でも `app_log_poll()` を呼ぶ。そうしないと `[init] stage=timer ok=0` と `[err] code=4` が出ない。
- **起動時**: `app_log_init()` → `[boot]` → `lcd_init()` → `[init] lcd` → `touch_init()` → `[init] touch` → タイマ作成 → `[init] timer` → `[init] loop`。各行をキューへ入れた直後に `app_log_poll()` が走る(`app_log_line` の中で1回呼ぶ。現実装どおり)ので、`touch_init` の約310msの待ちの間も、送信は割り込みで進む。
- 起動直後の最大行数は 5行(boot + init×4)。キュー8行で足りる。

### 4.3 ノンブロッキングの規則

| 規則 | 内容 |
|---|---|
| 送信 | 完了を**待たない**。`while` で `tx_idle` や TEI を待たない。`SCI_CMD_TX_Q_FLUSH` などの制御もしない |
| 割り込み内 | `R_SCI_Send`・整形(`snprintf`)・キュー操作を呼ばない。キューは**メインループ専用**(割り込みから触らない) |
| 整形のコスト | `[hb]` は毎秒1回。`snprintf` の所要時間は**未測定**(10ms周期に対し十分小さいはずだが未確認)。毎周回の仕事は `app_log_poll()` の分岐だけ |
| 既存の詰まり要因 | `touch_read` 最大約20ms、`touch_init` 約310ms(起動時のみ)、`iic_scan` は最悪 **約2.2秒**(0x08〜0x77 = 112アドレス × 20ms、計算。連続10回失敗後に1回だけ)、`lcd_show` は**未測定**。これらの間は `[hb]` が遅れる(§6 の合否基準に反映) |

### 4.4 割り込み優先度(`.scfg` と `r_sci_rx_config.h` で確認した現状)

| 要因 | 優先度 | 出典 |
|---|---|---|
| CMT0(10ms tick) / GLCDC | 5 | `.scfg` |
| **SCI2 RXI/TXI(UART)** | 現状 3 → **設計値 2**(`R_SCI_Open` の `cfg.async.int_priority = 2u`) | `.scfg`(3)、`app_debug.c`(3) |
| SCI6 TXI(タッチI2C) | 2 | `.scfg` |
| ERI/TEI グループ(UARTエラー) | 3(`SCI_CFG_ERI_TEI_PRIORITY`。変更しない) | `r_sci_rx_config.h` |

- 理由: UART割り込み(数µs、**未測定**)がI2Cの割り込みを追い越す現状(3 > 2)を、同順位(2)にして避ける。CMT と GLCDC(5)は UART と I2C の両方を中断できるので、10ms tick の精度は保たれる。
- 受信バイト間隔は、PCが1バイトずつ送る限り 87µs 以上。SCI2 にFIFOは無い(`r_sci_rx_config.h` にFIFO設定があるのは CH7〜11 のみ)ので、RXIが 87µs 以内に処理されないと受信オーバーラン(`SCI_EVT_OVFL_ERR`)になりうる。通常のテスト(間隔 ≥ 数十 ms)では問題ない。

---

## 5. ホストでテストできる純粋関数

方針: **標準Cのみ**(`stdint.h` `stddef.h` `stdbool.h` `stdio.h` の `snprintf` まで。FIT・`platform.h` を include しない)。`tests/host/run.sh` の S1 と同じ検査(警告ゼロ、データシンボル無し)に通す。

### 5.1 `app_logfmt.h/.c`(既存を拡張)

| 関数(案) | 出力 / 動作 |
|---|---|
| `size_t applog_fmt_boot(char *buf, size_t size, const char *build, uint8_t ch, uint32_t baud);` | `[boot] fw=rx72n_gcc build=<id> ch=<ch> baud=<baud>\r\n`。`build` は 0x21〜0x7E 以外(空白含む)を `_` にし、31文字で切る。`NULL` は `unknown` |
| `size_t applog_fmt_init(char *buf, size_t size, const char *stage, bool ok, uint32_t err, uint32_t detail);` | `[init] stage=<stage> ok=<0\|1> err=<err> detail=<detail>\r\n`。`ok` が真なら呼び出し側が `err=0, detail=0` を渡す(関数は渡された値を出す) |
| `size_t applog_fmt_state(char *buf, size_t size, const char *tag, const applog_state_t *s, bool with_range);` | `tag` = `"hb"` / `"st"`。`with_range` が真なら `xmin xmax ymin ymax` を末尾に付ける |
| `size_t applog_fmt_scan(char *buf, size_t size, uint8_t count, const uint8_t *addr);` | `[scan] n=<count> a0=0x38 ...`。`count` は 4 を超えたら 4 に丸める |
| `size_t applog_fmt_ev(char *buf, size_t size, uint8_t index, const char *src);` | `[ev] img=<index> src=<src>\r\n`(現 `applog_fmt_img` の名前・タグ変更) |
| `size_t applog_fmt_err(char *buf, size_t size, uint32_t code, uint32_t detail);` | `[err] code=<code> detail=<detail>\r\n` |
| `size_t applog_fmt_help(char *buf, size_t size);` | `[help] n=next p=prev s=state ?=help\r\n` |
| `applog_cmd_t applog_cmd_parse(int ch);` | `'n'`→NEXT、`'p'`→PREV、`'s'`→STATE、`'?'`→HELP、それ以外(`-1`、`0`〜`255` の他の値、範囲外)→NONE。**列挙子 `APPLOG_CMD_HELP` を追加** |
| `int applog_due(uint32_t now, uint32_t last, uint32_t period);` | `(uint32_t)(now - last) >= period` なら 1。32bit一周でも成立(現 `applog_hb_due` の名前変更。旧名はマクロで残してよい) |
| `int applog_err_changed(uint32_t code, uint32_t detail, uint32_t last_code, uint32_t last_detail);` | どちらかが違えば 1 |

`applog_state_t` に追加する項目: `i2c_fail_streak`(uint8_t)、`drop`(uint32_t)、`x_min` `x_max` `y_min` `y_max`(uint16_t)。`APPLOG_LINE_MAX` は **192**。

| 共通の挙動(すべての `applog_fmt_*`) | 規則 |
|---|---|
| 戻り値 | 書いたバイト数(NULを除く、`\r\n` を含む)。**失敗は 0** |
| バッファ不足(出力が `size - 1` バイトを超える) | **0 を返し、`size > 0` なら `buf[0] = '\0'`**。部分的な行を有効なものとして残さない。バッファの外は書かない |
| 引数不正(`buf == NULL`、`size == 0`、必須ポインタが `NULL`) | 0 を返す。`size > 0` かつ `buf != NULL` なら `buf[0] = '\0'` |
| 呼び出し側 | 0 のときは送らず `drop` を +1 する |
| `tag`/`stage`/`src` | プログラム内の定数文字列のみを渡す(整形側は空白などの検査をしない。`NULL` は `?`) |

### 5.2 `app_logq.h/.c`(新規。行キュー。現 `app_debug.c` の4行キューを切り出す)

| 関数(案) | 動作 |
|---|---|
| `void applog_q_init(applog_q_t *q);` | 空にする。破棄カウンタを 0 に |
| `bool applog_q_push(applog_q_t *q, const char *line, size_t len);` | 満杯、`len == 0`、`len >= APPLOG_LINE_MAX`、引数不正なら **false を返し、破棄カウンタ +1(飽和)**。成功なら true |
| `const char *applog_q_peek(const applog_q_t *q, size_t *len);` | 先頭の行。空なら `NULL`(`*len = 0`) |
| `void applog_q_pop(applog_q_t *q);` | 先頭を捨てる(空なら何もしない) |
| `void applog_q_note_drop(applog_q_t *q);` | 整形失敗・送信エラーによる破棄を数える(飽和) |
| `uint32_t applog_q_drops(const applog_q_t *q);` | 破棄累計。`[hb]` の `drop=` の値 |
| `size_t applog_q_count(const applog_q_t *q);` | 溜まっている行数 |

- 容量: `APPLOG_Q_LINES = 8`。1行あたり `APPLOG_LINE_MAX`。
- **このキューはメインループ専用**。排他制御は不要(割り込みから呼ばない)。

### 5.3 `nav_logic.h/.c`(追加)

`bool nav_step(nav_t *nav, int8_t dir);`(§3.3)。

### 5.4 ホストテストの項目(テスト担当向け)

| ID | 検証 |
|---|---|
| U-H1 | 各 `applog_fmt_*` が、既知の入力で**期待どおりの文字列**を返す(1文字単位で一致。`\r\n` 終わり) |
| U-H2 | 全項目が最大値のとき `[hb]` = 144、`[st]` = 188 バイト。どちらも `APPLOG_LINE_MAX`(192)未満 |
| U-H3 | `size` が 0、1、必要長-1、必要長 のとき、戻り値と `buf[0]`、境界の外が書かれていないこと(番兵バイトで確認) |
| U-H4 | `build` に空白・制御文字・長い文字列・`NULL` を渡したとき `[boot]` の値が空白を含まず31文字以内 |
| U-H5 | `applog_cmd_parse`: `n p s ?` 以外の 0〜255 と -1 がすべて NONE |
| U-H6 | `applog_due`: 100未満は0、100以上は1、`now` が `0xFFFFFFFF` をまたいでも成立 |
| U-H7 | `applog_q`: 8行まで push 成功、9行目は false で `drops == 1`。pop 後に再び push 可。`len >= 192` は false。`drops` の飽和 |
| U-H8 | `nav_step`: +1 / -1 の循環(3枚: 0→1→2→0・0→2→1→0、4枚: 0→1→2→3→0・0→3→2→1→0)、画像1枚と `NULL` で false、`dir == 0` で false。`nav_step` の後に `nav_update` の動作(300ms規則、状態機械)が変わらないこと |
| U-H9 | 既存の H1〜H7(`test_nav.c`)が**そのまま合格**(`nav_act` の整理で壊れていない) |
| U-H10 | `app_logfmt.c` / `app_logq.c` / `nav_logic.c` が S1 の条件(gnu99 / c99 -pedantic-errors / c11、警告ゼロ、`platform.h` 非依存)を満たす |
| U-H11 | `app_debug.c` / `app_main.c` が S7(`-fsyntax-only`、`APP_LOG_ENABLE` = 0 / 1、`APP_LOG_SCI_CH` = 1 / 2)を通る。`APP_LOG_ENABLE=0` で `app_logq` などが未使用でも警告が出ない |

---

## 6. PC側の確認手順

### 6.1 ランナーの拡張(必須。今の `serial` は**受信専用**)

`tools/job-runner.ps1` の `serial` を次のとおり拡張する。担当: デバッグ担当(または主セッション)。コードは本書では書かない。

| 引数 | 型・既定 | 内容 |
|---|---|---|
| `port` `baud` `seconds` | 既存のまま | `port` は `^COM\d{1,3}$`、`seconds` ≦ 600 |
| `tx` | 文字列・省略可 | 送る文字。**許可リスト `^[nps?x]{1,200}$`**(これ以外は `throw`)。`x` は「未知の文字は無視される」試験用 |
| `txAtSec` | 数・既定 2 | ポートを開いてから送信を始めるまでの秒数。`0 ≦ txAtSec < seconds` |
| `txGapMs` | 数・既定 50 | 1文字ごとの間隔。**0 のときは全文字を1回の書込みで送る**(連打試験)。0〜1000 |
| `dtr` | 真偽・既定 false | 真なら `DtrEnable = true`(§7 U4) |

| 規則 | 内容 |
|---|---|
| 受信 | 送信中・送信後も `seconds` の終わりまで受信を続け、**受信した文字だけ**をログへ書く(送信の記録は入れない。行の途中に割り込ませないため) |
| 文字コード | 既定の ASCII のまま(0x80以上は `?` になる)。出力は ASCII だけなので問題ない |
| 反映 | `.ps1` を直したら `tools/submit-job.sh restart`(ランナーが終了コード75で終わり、`start-runner.cmd` が再起動する。**未検証**)。動かなければユーザーが `start-runner.cmd` を起動し直す |
| 実行順 | ランナーは**1ジョブずつ**順に実行する。`serial` の間は他のジョブ(gdb 等)が待たされる |

### 6.2 事前チェック(ユーザー)

| # | 内容 |
|---|---|
| 1 | **ECN1 と CN8 の両方**のUSBをPCに接続(CN8側のRL78は他と絶縁されていて、ECN1 から給電される。`docs/design.md` §5.1) |
| 2 | SW3 の2ch が **OFF**(今の位置を変えない。位置の確認はユーザーがマニュアルの表5-2で行う) |
| 3 | `tools\start-runner.cmd` が動いている。**COM7 を他のソフト(ターミナル等)が開いていない** |
| 4 | UART付きのビルドを書込み済み。**書込みの後、ユーザーがリセットボタンを押す**(CLIから実行再開はできない。`docs/debug-report.md` §4) |
| 5 | **gdb ジョブを同時に走らせない**(接続するとCPUが止まり、`[hb]` が止まる) |

### 6.3 受信ログの読み方(判定側の約束)

| 規則 | 内容 |
|---|---|
| 行の分け方 | `\r?\n` で分ける。先頭に BOM(U+FEFF)があれば取る(PowerShell 5.1 の `Out-File -Encoding UTF8` は BOM を付ける。**未確認**) |
| 先頭と末尾の行 | 受信開始のタイミングで**途中から始まる行**と、終了時に**途中で切れる行**がありうる。正規表現に合わなくても、最初の行と最後の行は無視する |
| 異物の検出 | 上記以外で `[` で始まらない非空行は「異物」。ボーレート不一致・配線異常のとき文字化けとして現れる |
| `[hb]` の間隔 | 連続する `[hb]` の `tick` の差で見る(10ms単位。壁時計ではない)。**壁時計は受信本数**(`seconds` 秒で `seconds`±2 本)で確認する |

### 6.4 正規表現(Python の `re`、1行ずつ `fullmatch`)

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

### 6.5 試験項目(UT)

ジョブの例は `tools/submit-job.sh serial '<JSON>' <待ち秒>`(待ち秒 = `seconds` + 25 程度)。`b` = 送信前の最後の `[hb]` の `img`。画像は4枚(`APP_IMAGE_COUNT = 4`)。

| ID | 目的 | ジョブ(JSON) | 人の操作 | 合格条件 |
|---|---|---|---|---|
| UT1 | 受信のみ。配線・ボーレート・メインループ・I2C健全性 | `{"port":"COM7","baud":115200,"seconds":15}` | なし | (1) R-HB が **13〜16 本** (2) 連続する R-HB の `tick` 差が **100〜110**(110超〜130は警告、130超は不合格。ただし直前で `i2c_fail` が10以上増えていれば `iic_scan` の停止として警告) (3) 異物の行が **0** (4) `drop` がすべて 0 (5) `err=0`、`i2c_fail` が全 R-HB で同じ値、`streak=0` |
| UT2 | 起動順序(要リセット) | `{"port":"COM7","baud":115200,"seconds":20}` | ジョブ開始から**5秒以内にリセットボタン**を押す | 次の順に各1回: R-BOOT → R-INIT lcd(`ok=1 err=0 detail=0`) → R-INIT touch(`ok=1`) → R-INIT timer(`ok=1`) → R-INIT loop(`ok=1`) → 最初の R-HB(`tick` が **100〜112**)。`ok=0` があれば、その行を不具合として報告(§1.2 の C'/D'/E') |
| UT3 | `s` と `?` | `{"port":"COM7","baud":115200,"seconds":12,"tx":"s?","txAtSec":3,"txGapMs":300}` | なし | R-ST が1本、R-HELP が1本(送信から1秒以内)。R-ST の `img` `err` `i2c_fail` が前後の R-HB と一致(大きく矛盾しない)。`found>0` なら R-SCAN が R-ST の直後に1本 |
| UT4 | `n`/`p` の遠隔切替 | `{"port":"COM7","baud":115200,"seconds":12,"tx":"nnnp","txAtSec":3,"txGapMs":400}` | **LCDを見て**、絵が切り替わるか教えてもらう(UARTでは判定不能) | R-EV が4本、すべて `src=uart`、`img` が順に `(b+1)%4` `(b+2)%4` `(b+3)%4` `(b+2)%4`。直後の R-HB の `img` が最後の値と一致 |
| UT5 | 未知の文字を無視 | `{"port":"COM7","baud":115200,"seconds":10,"tx":"xxxx","txAtSec":3,"txGapMs":100}` | なし | R-EV / R-ST / R-HELP / R-ERR が**0本**。R-HB は続き、`img` は不変、`drop` は不変 |
| UT6 | 連打でも壊れない | `{"port":"COM7","baud":115200,"seconds":14,"tx":"nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn","txAtSec":3,"txGapMs":0}`(`n` を60個) | なし | R-EV が**ちょうど60本**、最後の `img` = `(b+60)%4` = `b`。R-ERR の `code=7` が 0本。`drop` がすべて 0。連続する R-HB の `tick` 差 ≦ 130。最初の R-EV から最後の R-EV までが約 0.6秒以上(= 1 tick に1文字の制限が効いている) |
| UT7 | タッチ(B4〜B6の裏付け) | `{"port":"COM7","baud":115200,"seconds":25}` | 開始後、**右半分を1回タップ → 2秒待つ → 左半分を1回タップ → 2秒待つ → 右半分を3秒押し続ける → 離す → 2本指で触る** | R-EV が `src=touch` で順に `(b+1)%4` → `b` → `(b+1)%4`、**押し続けでも追加の R-EV なし**、2本指では R-EV なし。押している間の R-HB に `pts=1` と `x` `y` の変化 |
| UT8 | 座標範囲(B8) | `{"port":"COM7","baud":115200,"seconds":40,"tx":"s","txAtSec":32}` | 開始後 30秒以内に**四隅を順に1回ずつ**触る | R-ST の `xmin xmax ymin ymax` を読んで `TOUCH_X_MAX` を決める(`docs/design.md` §9.2) |
| UT9 | 長時間(B7の一部) | `{"port":"COM7","baud":115200,"seconds":600}`(待ち 625) | なし(画面は目視で任意) | R-HB が 598〜602 本、`tick` が単調増加、`drop` 不変、`i2c_fail` 不変、R-ERR なし |

- 人の操作が要る項目は、**ジョブを出す前に**ユーザーへ「何を、いつ、どの順に」を伝える。
- 失敗時はログ全文(`jobs/outbox/<id>.log`)をそのまま報告に添える。

---

## 7. リスクと未確認事項(確認方法つき)

| ID | リスク / 未確認 | 影響 | 確認方法 | 外れたときの対処 |
|---|---|---|---|---|
| U1 | SCI2 の端子設定が有効か。SC生成の `R_SCI_PinSet_SCI2()` は `P12PFS = 0x0A`、`P13PFS = 0x0A`(`r_sci_rx_pinset.c` で確認 ◎)。**データシートの端子機能表との照合は未実施**。`R_Pins_Create()` は誰も呼ばない(`implementation-notes.md` §5)ので、`app_log_init()` が `R_SCI_PinSet_SCI2()` を呼ぶことが必須 | 何も出ない | UT1 で R-HB が来るか。来なければ gdb で `MPC.P12PFS` `MPC.P13PFS` `PORT1.PMR` `SCI2.SCR` を読む(`iodefine.h`: `MPC` = 0x8C100、`PORT1` = 0x8C001、`SCI2` = 0x8A040)。**gdb接続でCPUは止まるが、レジスタ値は読める** | 端子設定を直す(設計に差し戻し) |
| U2 | CN8 の接続先が RX72N(SW3 2ch = OFF)か。回路図・マニュアルは SCI2(P13/P12)で一致 ◎(`schematic-findings.md`)。**実機のスイッチ位置は未確認** | 何も出ない | ユーザーがマニュアルの表5-2と照らして SW3 2ch を確認。LLMは位置を推測で指示しない | ON なら OFF へ(ユーザー作業) |
| U3 | ボーレート誤差。MCU側は約 -0.22%(計算、§4.1)。**RL78(USB-シリアル変換)側の誤差と、PCの設定をRL78が追従するか(固定か)は未確認** | 文字化け(異物行) | UT1 の「異物の行が 0」 | 57600 に下げて再試験(`APP_LOG_BAUD` とジョブの `baud` の2か所を同時に変える) |
| U4 | USB-CDC の RL78 が DTR を見て送受信を制御している可能性(未確認) | 受信 0 バイト、または PC→MCU が届かない | UT1 で 0バイトのとき、MCUが走っている確証(ユーザーが画面の動作を確認)があれば、`"dtr":true` を付けて再試験 | `dtr` を既定にする |
| U5 | `err` が成功で戻らない(§1.3) | `[hb]` の `err` を「現在の異常」と誤解 | 判定は `i2c_fail` の増減で行う | — |
| U6 | `snprintf` が RX 用GCC(newlib)でリンク・動作するか。ヒープ 0x400、ユーザースタック 0x1000(`r_bsp_config.h`)。`BSP_CFG_IO_LIB_ENABLE = 0`。整形はスタック上の `char[192]`。**未ビルド** | ビルド失敗、またはスタックあふれ | ユーザーのビルドと UT1 | 整形関数の中身を自前の10進変換に置き換える(**関数の署名・出力は変えない**ので、呼び出し側とホストテストはそのまま) |
| U7 | `lcd_show`(ROM→RAM2 261,120バイト)の所要時間が**未測定** | 連打時に `[hb]`・タッチが遅れる | UT6 の `tick` 差と、最初〜最後の `[ev]` の所要 | 1 tick に1文字の制限は維持。遅すぎるなら2面化(`docs/design.md` §15 #6) |
| U8 | `iic_scan` が最悪約 2.2秒メインループを止める(計算) | その間 `[hb]` が出ない(§4.3) | UT1 の (2) の警告条件 | 判定基準で許容済み |
| U9 | タッチICが応答していても、**成功回数のカウンタが無い**ため、「I2C読出しが成功し続けている」は「失敗が増えていない」からの間接判定 | G の判定が弱い | UT7(指を置いて `pts` `x` `y` が変わる)で補強 | 拡張案(§9)の `i2c_ok_total` |
| U10 | 起動バナーはリセット時に1回だけ。ジョブ開始が遅れると見逃す | UT2 は人の操作が要る | UT1/UT3 では `tick` が小さい(リセット直後)ことで「最近リセットされた」と分かる | 拡張案(§9)のソフトリセットコマンド |
| U11 | `build` 値は `app_main.c` を**コンパイルし直した時刻**。他のファイルだけ変えた増分ビルドでは変わらない | 古いELFが走っているかの判定が弱い | 変更後に `app_main.c` も再コンパイルされる設定にするか、`build` が最新時刻か目視 | 拡張案(§9) |
| U12 | PCがポートを開く・閉じる瞬間に、RL78 から**ゴミ文字**がMCUへ届く可能性(未確認) | 未知の文字は無視されるが、偶然 `n` `p` `s` `?` なら動作 | 実害は小さい。UT5 で未知文字が無視されることを確認 | — |
| U13 | `gdb` 接続でCPUが止まる(観察済み)。UARTと同時に使うと `[hb]` が止まって見える | 誤判定 | §6.2 の #5 | `tick` が止まったら、先に gdb が接続していないか確認 |
| U14 | PC側ランナーの BOM・文字コード・行の途中開始(§6.3) | 先頭行の判定ミス | §6.3 の規則で吸収 | — |
| U15 | UART を開く前の `R_SCI_Open` 失敗(`APP_LOG_ENABLE=1` のままチャネルが開けない等)はUARTから見えない | 何も出ない(原因が分からない) | `g_app_dbg` で見るなら gdb。新しい変数は増やさない(既存フィールドのみ)ので、UARTが無音のときは U1/U2 から順に | — |

---

## 8. 現在の実装(2026-11-20 11:10)との差分: コーディング担当への指示

状態: 本書が最優先。現実装は「依頼文の案」で書かれており、次を直す。設計に疑問があれば**勝手に変えず**設計役へ差し戻す。

| # | ファイル | 直すこと |
|---|---|---|
| 1 | `src/app/app_logfmt.h/.c` | §5.1 のとおり: タグ `[boot]`/`[init]`/`[hb]`/`[st]`/`[scan]`/`[ev]`/`[err]`/`[help]`。`[rx72n] boot ok` → `[boot] fw=rx72n_gcc build=... ch=.. baud=..`。`err=%lu/%lu` → `err=%lu detail=%lu`。`i2cfail=` → `i2c_fail=`。`streak` `drop` を追加。`[img]` → `[ev]`。`applog_step_index` を廃止。`APPLOG_CMD_HELP` 追加。`applog_hb_due` → `applog_due`。失敗時 `buf[0]='\0'`。`APPLOG_LINE_MAX` = 192。build の空白を `_` に |
| 2 | `src/app/app_logq.h/.c`(新規) | §5.2。`app_debug.c` の4行キュー(`s_q*`)をここへ移す。8行 |
| 3 | `src/app/app_debug.h/.c` | `APP_ERR_UART_RX (7u)` を追加。`app_log_printf` と `APP_LOG(...)` は**廃止**(自由書式は出力形式を保証できないため)。キューを `app_logq` に置き換え。破棄(キュー満杯、BUSY以外の送信エラー)は `drop` に数える。`cfg.async.int_priority` を **2** に。コールバックは `SCI_EVT_RXBUF_OVFL` `SCI_EVT_FRAMING_ERR` `SCI_EVT_PARITY_ERR` `SCI_EVT_OVFL_ERR` のとき `volatile` 変数へイベント値を書くだけ。公開関数に `uint32_t app_log_drops(void)` と `uint32_t app_log_rx_event(void)`(読んで0に戻す。0 = 無し)を追加。`APP_LOG_ENABLE = 0` の空関数も同じ名前で用意 |
| 4 | `src/app/nav_logic.h/.c` | `nav_step()` を追加(§3.3)。`nav_act` の循環計算を共通化。**`nav_update` の挙動は変えない** |
| 5 | `src/app/app_main.c` | §4.2 の順序。`apply_image()`(タッチ・UART 共通)。`nav->index` の直接書込みを廃止。`[init]` 各行。`[err]` 判定(A, B, C)。タイマ失敗の停止ループで `app_log_poll()`。受信は **1 tick に1文字**。`?` と `s` の出力(`s` は `[st]`、`found>0` なら `[scan]`)。UART割り込み優先度の変更は `app_debug.c` 側 |
| 6 | `tests/host/`(テスト担当) | §5.4 の U-H1〜U-H11 を追加。`run.sh` の S1 / S7 の対象に `app_logfmt.c` `app_logq.c` を足す |
| 7 | `tools/job-runner.ps1`(デバッグ担当) | §6.1 の `serial` 拡張。`docs/cli-debug.md` §2 の `serial` 行も更新 |
| 8 | `docs/implementation-notes.md` | 付記(2026-11-20)を本書に合わせて更新 |

**変えないもの**: SC設定(`r_sci_rx` CH2 は有効、TX/RX バッファ各800、`R_SCI_PinSet_SCI2` あり。**SCの再生成は不要**)、`app_debug_t` の構造(**フィールドを増やさない**)、`src/app/` 以外、`APP_LOG_SCI_CH`(既定2)・`APP_LOG_BAUD`(既定115200)・`APP_LOG_ENABLE`(既定1)のマクロ名と既定値。

### 8.1 受入基準(実機。UT の合格 = 以下を満たす)

| ID | 基準 | 試験 |
|---|---|---|
| U-B1 | リセット後 5秒以内に `[boot]` と `[init]` 4行が順に出る | UT2 |
| U-B2 | `[hb]` が毎秒1本、`tick` 差 100〜110、異物なし、`drop=0` | UT1, UT9 |
| U-B3 | `n` `p` で `[ev] src=uart` が返り、画像番号が `(b±1)%4` で進み、**LCDの絵も切り替わる**(目視) | UT4 |
| U-B4 | `s` `?` に応答し、未知の文字は無視 | UT3, UT5 |
| U-B5 | 60連打で60本の `[ev]`、`drop=0`、`[hb]` が止まらない | UT6 |
| U-B6 | タッチ操作が `[ev] src=touch` として出る(押し続け・2本指で出ない) | UT7 |
| U-B7 | UART を付けても B1〜B6(`docs/design.md` §14.3)の目視結果が変わらない(タッチの反応が遅くならない) | ユーザー目視 |

---

## 9. 今回は実装しない拡張案(価値と理由)

| 案 | 内容 | 理由で見送り |
|---|---|---|
| `lcd_stage` | `g_app_dbg` に LCD 初期化の進行段階(GPIO/PinSet/Open/Start/DISP/BL)を足し、`[init]` に載せる | 「既存フィールドのみ」の制約。今は `err=1`(Open失敗)と `err=2`(Start失敗)の2段階だけ分かる |
| `i2c_ok_total` | I2C読出し成功の累計。「読めている」を直接判定できる(U9) | 同上 |
| ソフトリセットコマンド(例 `r`) | PCから再起動して `[boot]` を取り直す。**CLIから実行再開できない現状では価値が高い**が、誤送信で再起動するリスクがある(U10) | 要件にない。必要なら次版で許可リスト付きで |
| `build` の確実な更新 | リンク時刻などを埋める | ビルド設定の変更が要る(U11) |
| 行ごとのチェックサム | 文字化け検出の強化 | UT1 の異物検出で足りる |

---

## 10. 製品デモ(`docs/design.md` v2 の P1)向けの追加仕様(2026-11-21)

状態: **設計(未実装・未検証)**。画像4枚ビューアが「画面5つの製品デモ」に変わるのに合わせ、ログとコマンドを足す。
**判定できないこと(変わらない)**: 画面に実際に何が映っているか(色・欠け・ちらつき・動きの滑らかさ)は目視(`design.md` §12.4 の V 項目)。UART で言えるのは「ソフトの状態がこうなった」まで。

### 10.0 結論(まず読む)

| # | 決定 | 内容 |
|---|---|---|
| 1 | `[hb]` | `img=` を `scr=`(画面番号 0〜4)に変え、`demo=`(デモ中 0/1)`frm=`(画面の入れ替え回数の累計)`uf=`(GLCDC アンダーフロー検出の累計)を足す |
| 2 | `[ev]` | 画面遷移・ボタン押下/実行・温度変更・完了・デモの開始/終了/中断を出す(§10.3)。ボタンは仮想タッチ(デモ・UART)も同じ形で出る |
| 3 | コマンド | 追加 `d`(デモ開始/中断)`+`(温度+1)`-`(温度-1)。`n` `p` は **画面の順送り / 逆送り** に変更(タッチ無しで全画面を巡れる) |
| 4 | 新しい行 | `[ui]`(`s` の時、`[st]` の次)、`[perf]`(起動時に1回、描画の所要時間) |
| 5 | 行の長さ | 最大 **255 バイト + NUL**(`APPLOG_LINE_MAX` 192 → 256)。`[hb]` 最長 178、`[st]` 最長 222(計算) |
| 6 | 変えないもの | 行の形式(`[タグ] key=value ...`、`\r\n`、ASCIIのみ)、あふれ規則、受信は1 tick に1文字、割り込みでは何もしない、ボーレート、SCI2 |

### 10.1 置き換わるもの

| 旧(§2〜§6) | 新 |
|---|---|
| `[hb] ... img=<n> ...` | `scr=<n>` に置換(下の §10.2)。P2 のギャラリー時は `scr` = 画像番号 |
| `[ev] img=<N> src=<touch\|uart>` | §10.3 の各形式。P2 のギャラリー時のみ従来の `img=` 形式 |
| `n` = 次の画像、`p` = 前の画像 | `n` = 次の画面、`p` = 前の画面(§10.4)。P2 のギャラリー時は従来どおり |
| `[help] n=next p=prev s=state ?=help` | `[help] n=next p=prev s=state d=demo +=up -=down ?=help` |
| UT4(n p で画像)、UT6(n を60個)、UT7(タッチで画像) | UT10〜UT19(§10.8)に置き換え。P2 で再開 |
| `app_dbg_t.image_index` | `scr` に名前変更。`demo`(uint8)、`frames`(uint32)、`uf`(uint32)を追加 |

### 10.2 `[hb]` と `[st]`

```
[hb] tick=<n> scr=<n> demo=<0|1> frm=<n> uf=<n> err=<n> detail=<n> pts=<n> x=<n> y=<n> i2c_fail=<n> streak=<n> found=<n> drop=<n>
[st] (上と同じ項目) xmin=<n> xmax=<n> ymin=<n> ymax=<n>
```

| キー | 意味 | 出どころ |
|---|---|---|
| `scr` | 画面番号: 0 = home、1 = fill、2 = reheat、3 = usage、4 = standby(P2 ギャラリーでは画像番号) | `g_app_dbg.scr` |
| `demo` | デモ実行中 = 1 | `g_app_dbg.demo` |
| `frm` | **表示の入れ替えが完了した回数**の累計(反映待ちが終わるたびに +1)。動きがあれば増え、静止中は増えない | `g_app_dbg.frames` |
| `uf` | GLCDC GR2 アンダーフロー(表示用のデータが間に合わなかった)の検出回数の累計。**合格条件 = 起動後の増分が 0**。起動時の初回検出 1回(`tick=200` の `[hb]` で `uf=1` になる。実機で 8回のリセットすべてで確認)は捨てる(§10.2 注)。増分が出たら RAM2 の帯域不足を疑う | `g_app_dbg.uf`(1秒ごとに `lcd_underflow_poll` で読む) |

- **注(2026-11-21 実機)**: GLCDC の表示開始時に GR2 アンダーフロー検出が **1回だけ立つ**(`touch_init` の直後で `GLCDC.STMON = 0x4` を gdb で確認。最初の `lcd_underflow_poll` が読んでクリアして `uf` = 1 になる。その後はデモ・連打・画面送りでも増えない)。扱い: (a) 製品コードは `lcd_init` の直後に `lcd_underflow_poll()` を 1回呼んで捨てる(コーディング担当が修正)。(b) **それまでの判定は「`uf` の増分が 0」**(最初の `[hb]` と `tick=200` の `[hb]` の差は見ない。`tick=200` 以降の `uf` が一定)。修正後は `uf=0` が全 `[hb]` で成り立つ。
- 最長(計算): `[hb]` = 144 − 2 + 36 = **178** バイト、`[st]` = 188 − 2 + 36 = **222** バイト(CRLF 込み)。255 未満。
- `APPLOG_LINE_MAX` を **256** にする。`r_sci_rx` の送信バッファ(CH2、800バイト)に収まる。222 バイトの送信時間は約 19ms(115200bps)。行キューは 8行 × 256 = 2,048 バイト。

### 10.3 `[ev]` の形式(製品デモ)

| 形式 | いつ出すか |
|---|---|
| `[ev] scr=<名前> from=<名前> src=<src>` | 画面が変わる時。スライドは **動き始めた時**、即切替(待機への出入り)は切替時。名前 = `home` `fill` `reheat` `usage` `standby` |
| `[ev] btn=<ボタン名> st=down src=<src>` | ボタンの押下が確定した時(当たり判定に入った時だけ。ボタン外のタッチは出さない) |
| `[ev] btn=<ボタン名> st=act src=<src>` | ボタンの動作が実行された時(離し確定、または押下から 90ms の遅い方)。直後に `scr=` か `set=` が続く |
| `[ev] set=<n> src=<src>` | 設定温度が **実際に変わった時だけ**(35 / 48 の端で変わらない時は出さない) |
| `[ev] done=<fill\|reheat>` | 湯はりの水位が 100% に達した時 / 追い焚きの温度が設定に達した時 |
| `[ev] demo=start src=<src>` | デモ開始(ホームの「デモ」ボタン = `touch`、UART の `d` = `uart`) |
| `[ev] demo=end` | 台本が最後まで終わった時 |
| `[ev] demo=abort src=<src>` | 中断(実タッチ = `touch`、UART の `d` = `uart`、台本が続けられない = `demo`)。直後に、ホーム以外にいた場合は `scr=home from=<名前> src=<src>` |
| `[ev] img=<N> src=<touch\|uart>` | **P2 ギャラリーのみ**(従来と同じ) |

- `<src>` = `touch`(実タッチ)/ `uart`(UART コマンドによる仮想タッチ・画面送り)/ `demo`(デモ台本の仮想タッチ)。
- ボタン名 = `h_dn` `h_up` `h_fill` `h_reheat` `h_usage` `h_power` `h_demo` `f_stop` `f_done` `f_home` `r_stop` `r_done` `r_home` `u_back` `s_wake`(`design.md` §4.1)。
- 順序の例(ホームで「湯はり」を押す): `btn=h_fill st=down` → (90ms 以上後)`btn=h_fill st=act` → `scr=fill from=home`。
- `s_wake`(待機中のタップ)は押下確定でその場で復帰するので、`btn=s_wake st=down` の直後に `scr=home from=standby` が続く(`st=act` は出さない)。
- `n` `p`(画面送り)は仮想押下を作らないので `btn=` は出ず、`scr=` だけ出る。
- 1回のデモ全体で `[ev]` は **36 行**(§10.8 UT13)。行キュー 8行・送信 約 4ms/行なので、`drop` は増えないはず(未確認)。

### 10.4 受信コマンド(1文字、改行不要)

| 文字 | 動作 | 出力 |
|---|---|---|
| `n` | 画面を **順送り**: home → fill → reheat → usage → standby → home。仮想押下は作らず、画面の遷移だけ(スライド / 待機は即切替。`design.md` §5.1)。画面の値の初期化は、ボタンで入った時と同じ | `[ev] scr=... src=uart` |
| `p` | 画面を **逆送り**: home → standby → usage → reheat → fill → home | 同上 |
| `+` | ホームの h_up を **仮想タップ**(150ms 押す)。押下表示・実行・`[ev]` は実タッチと同じ。**ホーム以外・画面遷移中・デモ中・押下表示中は無視** | `btn=h_up st=down` `st=act` `set=<n>`(いずれも `src=uart`) |
| `-` | h_dn の仮想タップ。条件は `+` と同じ | `btn=h_dn ...` `set=<n>` |
| `d` | **デモ開始 / 中断**(切替)。開始時は、どの画面にいても **アニメーションなしで即ホームに戻して**から台本を始める(set=42、bath=400、4倍速)。実行中に送ると中断(即ホーム) | `demo=start src=uart` / `demo=abort src=uart`(+ `scr=home ...`) |
| `s` | 状態を即時出力 | `[st]`、`found>0` なら `[scan]`、続けて `[ui]` |
| `?` | ヘルプ | `[help] n=next p=prev s=state d=demo +=up -=down ?=help` |
| 上記以外(`x`、大文字、改行、数字など) | **無視**(応答なし) | なし |

- **画面遷移中(250ms)、デモ中は、`n` `p` `+` `-` を無視する**(受け付けない。`[ev]` も出さない)。そのため、**連続して送る時は 300ms 以上あける**(`txGapMs` ≥ 300)。`d` `s` `?` はいつでも受け付ける。
- 1 tick に1文字だけ処理する規則は従来どおり(連打は受信キュー 800文字に溜まり、処理が遅れるだけ)。
- `n` `p` の遷移は、ボタンで遷移した時と同じ値の初期化を行う(例: `n` で fill に入ると水位 0%)。

### 10.5 `[ui]` と `[perf]`

```
[ui] scr=<名前> set=<n> bath10=<n> fill10=<n> flow10=<n> tr=<0|1> btn=<ボタン名|none> ph=<none|press|flash>
[perf] n=<N> copy_us=<n> home_us=<n> slide_us=<n>
```

| 行 | 意味 |
|---|---|
| `[ui]` | `s` を受けた時だけ、`[st]`(と `[scan]`)の次に1行。`set` = 設定温度(℃)、`bath10` = 浴槽温度(0.1℃単位)、`fill10` = 水位(0.1%単位)、`flow10` = 流量(0.1 L/分単位)、`tr` = 画面遷移中、`btn` / `ph` = 押下表示中のボタンと段階。最長 約 110 バイト |
| `[perf]` | 起動時に **1回**(`[init] stage=loop` の後、`APP_PERF_BENCH` = 1 の時)。`copy_us` = 480x272 RGB565 の全画面コピー(ROM → RAM2)1回、`home_us` = ホーム画面の描画 1回、`slide_us` = スライド途中(dx=240)の描画 1回。**各 N=50 回を連続で実行し、10ms カウンタの差から 1回あたり µs を出す**(分解能 200µs)。裏面に描くので表示は乱れない。`lcd_init` 失敗時は出さない |

- `[perf]` の目標(`design.md` §7.6 の判断基準): `copy_us` ≤ 8,000、`home_us` ≤ 12,000、`slide_us` ≤ 12,000。超えたら後退案へ。
- **P2a(ROM の背景を CLUT8 にした版。`design.md` §18)以降、`copy_us` の意味は「背景1枚(ホーム)を CLUT8 から RGB565 の面へ展開する時間」になる**(項目・書式・目標は同じ。§10.12)。

### 10.6 ホストで試験できる関数の変更(`app_logfmt` / `app_logq`)

| 関数 | 変更 |
|---|---|
| `applog_state_t` | `image_index` → `scr`(uint8_t)、`demo`(uint8_t)、`frames`(uint32_t)、`uf`(uint32_t)を追加 |
| `applog_fmt_state` | §10.2 の項目順 |
| `applog_fmt_ev_scr(buf, size, to, from, src)` / `applog_fmt_ev_btn(buf, size, name, st, src)` / `applog_fmt_ev_set(buf, size, n, src)` / `applog_fmt_ev_done(buf, size, what)` / `applog_fmt_ev_demo(buf, size, what, src)` | 新規(§10.3)。`src` が NULL の時は ` src=` を付けない(`demo=end`)。文字列引数はプログラム内の定数のみ |
| `applog_fmt_ev`(img) | 残す(P2 ギャラリー用) |
| `applog_fmt_ui(buf, size, ...)` / `applog_fmt_perf(buf, size, n, copy_us, home_us, slide_us)` | 新規 |
| `applog_fmt_help` | 新しい文字列 |
| `applog_cmd_parse` | `d` → `APPLOG_CMD_DEMO`、`+` → `APPLOG_CMD_PLUS`、`-` → `APPLOG_CMD_MINUS` を追加。それ以外は従来どおり NONE |
| `APPLOG_LINE_MAX` | 256 |
| 共通の挙動 | 従来どおり(失敗は 0、`buf[0] = '\0'`、範囲の外を書かない) |

ホストテスト(テスト担当): U-H1(全形式の文字列)、U-H2(最長: `[hb]` 178、`[st]` 222 が 256 未満)、U-H3(境界)、U-H5(`n p s d + - ?` 以外が NONE)、U-H7(行キューは `APPLOG_LINE_MAX` 256 で `len >= 256` が false)、U-H10・U-H11(S1 / S7 の条件)を新しい関数に合わせて更新。

### 10.7 `tools/job-runner.ps1`(デバッグ担当)

| 項目 | 変更 |
|---|---|
| `tx` の許可リスト | `^[nps?x]{1,200}$` → **`^[npsd+\-?x]{1,200}$`** |
| `judge_ut1.py` の `R-HB` | §10.9 の新しい正規表現 |
| 備考 | `txGapMs` の上限は 1000 のまま。長い待ちは `x`(無視される文字)を挟んで作る(UT14) |

### 10.8 試験項目(P1。UT1〜UT3、UT5、UT8、UT9 は §10.9 の正規表現に直した上でそのまま使える)

**リセット直後(ホーム、set=42)から始める**こと。

| ID | 目的 | ジョブ(JSON) | 人の操作 | 合格条件 |
|---|---|---|---|---|
| UT10 | 画面の順送り | `{"port":"COM7","baud":115200,"seconds":14,"tx":"nnnnn","txAtSec":3,"txGapMs":700}` | 目視: スライドして 湯はり → 追い焚き → 使用量 → 待機(即切替)→ ホームと変わるか | `[ev] scr=` が5本、`to` が順に fill / reheat / usage / standby / home、`from` が home / fill / reheat / usage / standby、すべて `src=uart`。最後の `[hb]` が `scr=0`。`drop=0`、`uf` の増分 0(起動時の1回を除く。§10.2 注) |
| UT11 | 逆送り | `"tx":"ppppp"` 他は UT10 と同じ | 同上 | `to` が順に standby / usage / reheat / fill / home |
| UT12 | 温度の上下と上下限 | `{"port":"COM7","baud":115200,"seconds":16,"tx":"++++++++--s","txAtSec":3,"txGapMs":500}` | 目視: ボタンが暗くなり、離すと白く光り、数字が変わるか | `btn=h_up st=down` と `st=act` が8組、`set=` が 43 44 45 46 47 48 の6本(7回目・8回目の `+` では出ない)、続いて `-` で `set=47`、`set=46`。`[ui] ... set=46` |
| UT13 | デモの全走 | `{"port":"COM7","baud":115200,"seconds":42,"tx":"d","txAtSec":3}` | 目視: デモの一連の動き(`design.md` §6.2) | `demo=start src=uart` の後、`[ev]` が **36 行**(btn 21、set 3、scr 8、done 2、demo 2)で、順序が §6.2 のとおり(`src=demo`)。`scr=` は fill / home / reheat / home / usage / home / standby / home の順。`[hb]` の `demo=1` が **31〜35 本連続**し、その後 `demo=0`。デモ中の `frm` の増加が **200〜900(実測 約620。設計の旧見積 200〜600 は誤り)**。`uf` の増分 0、`drop=0`、`[err]` なし。最後の `[hb]` が `scr=0 demo=0` |
| UT14 | デモの中断(UART) | `{"port":"COM7","baud":115200,"seconds":16,"tx":"dxxxxxxxd","txAtSec":3,"txGapMs":700}` | なし | 3.0秒に `demo=start`、8.6秒(開始から約 5.6秒 = 湯はり中)に `demo=abort src=uart` と `scr=home from=fill src=uart`。以後の `[hb]` が `scr=0 demo=0`。それ以降の `[ev]` なし |
| UT15 | デモの中断(実タッチ) | `{"port":"COM7","baud":115200,"seconds":30,"tx":"d","txAtSec":3}` | デモ開始の **8〜14秒後**(湯はりの画面)に、**画面のどこかを1回触る** | `demo=abort src=touch` と `scr=home from=fill src=touch`。そのタッチによる `btn=` の行が **出ない**。以後 `demo=0`、`set=` なし |
| UT16 | 通常速度での完了 | `{"port":"COM7","baud":115200,"seconds":30,"tx":"nn","txAtSec":3,"txGapMs":700}` | なし | `scr=fill`、`scr=reheat`。`scr=reheat` から `done=reheat` までの `[hb]` 本数が **16 ± 2**(設定 42℃: 4.0℃ ÷ 0.25℃/秒)。`done=fill` は出ない(32秒未満で離れるため) |
| UT17 | 連打でも壊れない | `{"port":"COM7","baud":115200,"seconds":14,"tx":"++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++","txAtSec":3,"txGapMs":0}` | なし | `[hb]` が 13〜16 本、連続する `tick` の差 ≤ 130。`[err]` の `code=7` なし。`drop=0`、`uf` の増分 0(起動時の1回を除く。§10.2 注)。`set=` の数は 1〜6(押下中は無視されるため一定でない)。48 を超えない |
| UT18 | `[perf]` と起動 | `{"port":"COM7","baud":115200,"seconds":20}` | ジョブ開始から5秒以内にリセットボタン | `[boot]` → `[init]` 4行 → `[perf]` が1本。`copy_us` ≤ 8000、`home_us` ≤ 12000、`slide_us` ≤ 12000(超えたら不合格ではなく「後退案の判断材料」として報告) |
| UT19 | タッチでの操作(V0 の裏付け) | `{"port":"COM7","baud":115200,"seconds":40}` | ホームで「＋」「湯はり」を順に押し、「ホーム」で戻る。ホームの四隅付近を押す | `btn=h_up st=down` → `st=act` → `set=43`、`btn=h_fill ...` → `scr=fill from=home src=touch`、`btn=f_home ...` → `scr=home ...`。ボタン外のタッチで `btn=` が出ない |

- 人の操作が要る項目は、**ジョブを出す前に**ユーザーへ「何を、いつ、どの順に」を伝える(§6.5 と同じ)。
- 失敗時はログ全文(`jobs/outbox/<id>.log`)を添えて報告する。

### 10.9 正規表現(Python の `re`、1行ずつ `fullmatch`)。§6.4 のうち下記だけ置き換え・追加

```
R-HB   : \[hb\] tick=(\d+) scr=(\d+) demo=([01]) frm=(\d+) uf=(\d+) err=(\d+) detail=(\d+) pts=(\d+) x=(\d+) y=(\d+) i2c_fail=(\d+) streak=(\d+) found=(\d+) drop=(\d+)
R-ST   : (R-HB の [hb] を [st] にしたもの) xmin=(\d+) xmax=(\d+) ymin=(\d+) ymax=(\d+)
R-UI   : \[ui\] scr=(home|fill|reheat|usage|standby) set=(\d+) bath10=(\d+) fill10=(\d+) flow10=(\d+) tr=([01]) btn=(\w+) ph=(none|press|flash)
R-PERF : \[perf\] n=(\d+) copy_us=(\d+) home_us=(\d+) slide_us=(\d+)
R-EVS  : \[ev\] scr=(home|fill|reheat|usage|standby) from=(home|fill|reheat|usage|standby) src=(touch|uart|demo)
R-EVB  : \[ev\] btn=(h_dn|h_up|h_fill|h_reheat|h_usage|h_power|h_demo|f_stop|f_done|f_home|r_stop|r_done|r_home|u_back|s_wake) st=(down|act) src=(touch|uart|demo)
R-EVV  : \[ev\] set=(\d+) src=(touch|uart|demo)
R-EVD  : \[ev\] done=(fill|reheat)
R-EVM  : \[ev\] demo=(start|abort) src=(touch|uart|demo)
R-EVE  : \[ev\] demo=end
R-HELP : \[help\] n=next p=prev s=state d=demo \+=up -=down \?=help
(P2 ギャラリーのみ) R-EV : \[ev\] img=(\d+) src=(touch|uart)
```

### 10.10 実装の差分(コーディング担当への指示。疑問は設計へ差し戻す)

| # | ファイル | 直すこと |
|---|---|---|
| 1 | `src/app/app_logfmt.h/.c` | §10.6 |
| 2 | `src/app/app_logq.h` | `APPLOG_LINE_MAX` に追従(256) |
| 3 | `src/app/app_debug.h/.c` | `g_app_dbg` の項目(§10.2)。`APP_ERR_LCD_FLIP (8u)` |
| 4 | `src/app/app_main.c` | `ui_event_pop` の結果を §10.3 の行にして出す。UART コマンドは `ui_cmd` へ(`n p + - d`)、`s` は `[st]` + `[scan]` + `[ui]`、`?` は新しい `[help]`。`[hb]` の項目。起動時に `[perf]`。1秒ごとに `lcd_underflow_poll` |
| 5 | `tools/job-runner.ps1` | §10.7 |
| 6 | `tests/host/`(テスト担当) | §10.6 のテスト、UT10〜UT19 の判定スクリプト(`judge_ut1.py` を拡張) |

### 10.11 リスク

| ID | リスク | 確認 | 対処 |
|---|---|---|---|
| UD1 | デモ中の `[ev]` が短時間に集中して行キュー(8行)があふれる | UT13 の `drop=0` | キューを 16行に増やす(`app_logq.h`) |
| UD2 | `frm` の増加量。旧見積 200〜600 は計算の誤りで、**実機の実測は約 620**(湯はり中は約 55コマ/秒で、パネルのリフレッシュ 59.46Hz にほぼ張り付く。画面遷移は約 15コマ/スライド)。許容範囲を **200〜900** に更新(`tests/host/judge_ui.py` の `FRM_MIN` / `FRM_HARD_MAX` と同じ。`FRM_SOFT_MAX` = 600 は警告のみ) | UT13 | 解決(2026-11-21)。上限は 900 のまま。今後 `frm` が 900 超なら、更新が無駄に走っていないか(表示キーの変化)を調べる |
| UD3 | 画面遷移中の `n` `p` を無視する仕様で、速く送るとコマンドが捨てられる | `txGapMs` ≥ 300 を守る | 受信側で保留する案(未採用。最小実装のため) |
| UD4 | `uf` の読み出しが 1秒に1回で、起動時に初回検出が 1回付く(実機確認。映像への影響は未確認)。短い乱れは `uf` の増加としてしか見えない(時刻は分からない)。また `judge_ui.py` の C 項目は現状「`uf` がすべて 0」(修正前の実機では不合格になる)ので、**起動時の1回を許す判定に直す**(テスト担当)。ほかに、、短い乱れは `uf` の増加としてしか見えない(時刻は分からない) | UT13 の `uf=0` | 必要なら `[ev] uf=` を足す |
| UD5 | `[perf]` の分解能が 200µs(10ms カウンタ ÷ 50回) | 値が小さい時は N を増やす | `APP_PERF_N` で変更 |

### 10.12 P2a(ROM の背景を CLUT8 にする。`design.md` §18)での UART の変更

**結論: 新しい行・項目・コマンドは無い。** `[boot]` `[init]` `[hb]` `[ev]` `[st]` `[ui]` `[help]` の書式、§10.9 の正規表現(R-PERF を含む)、判定スクリプトは変えない。変わるのは `[perf]` の `copy_us` の意味と、UT18 の報告だけ。

| 項目 | 内容 |
|---|---|
| `copy_us` の意味 | P1 = RGB565 の全画面 memcpy(ROM → RAM2、実測 800µs)。**P2a = `gfx_blit_c8` で背景1枚(ホーム、`xo` = 0)を CLUT8 から RGB565 の面へ展開する時間**。どちらも「ROM の背景を面へ出す時間」 |
| `home_us` / `slide_us` | 書式・測り方は同じ(`ui_render` 1回。`slide_us` は dx = 240)。中身の背景の描き方だけが変わる |
| 新しい項目を足さない理由 | 展開の時間は `copy_us` そのもの。P1 との比較は、P1 の実測(800 / 2000 / 1800、`debug-report.md`)と並べて報告すれば足りる。R-PERF と判定スクリプトを変えずに済む |
| 目標 | P1 と同じ: `copy_us` ≤ 8,000、`home_us` ≤ 12,000、`slide_us` ≤ 12,000 |
| 見積(△) | `copy_us` 3,300〜6,500、`home_us` 4,500〜7,700、`slide_us` 4,300〜7,500(`design.md` §18.5)。`copy_us` > 6,000 なら後退案(§18.5)を試す |
| **整合の検査**(UT18 の報告に書く) | `home_us ≈ 2,000 − 800 + copy_us`、`slide_us ≈ 1,800 − 800 + copy_us`(±400µs)。外れたら展開以外の変化(数字・バー)を疑う |
| 分解能 | 10ms カウンタ ÷ N=50 = 200µs(UD5)。`copy_us` が数 ms なので足りる。足りない時だけ `APP_PERF_N` を 200 に |
| UT18 の合格条件 | §10.8 のまま。**報告に P1 との比(`copy_us`、`home_us`、`slide_us`)を添える** |
| `uf` | 展開は CPU が RAM2 に書く時間が P1 より長い(約 4〜8 倍 △)。**UT10・UT13・UT17 の `uf` の増分(起動時の1回を除く)が 0 のままか**を P2a の合格条件にする(`design.md` §18.8 U-C3) |

- コーディング担当の変更: `app_main.c` の `bench_copy` を `gfx_blit_c8(lcd_fb(s_back), &ui_bg[UI_BG_HOME], 0)` の1行にする(`design.md` §18.5)。`app_logfmt` は変更なし。


### 10.13 P2b(製品 UI を GLCDC の CLUT8 表示にする。`design.md` §20)での UART の変更

**結論: 新しい行・項目・コマンドは無い。** §10.12(P2a)と同じ。`[boot]` `[init]` `[hb]` `[ev]` `[st]` `[ui]` `[help]` の書式、§10.9 の正規表現(R-PERF を含む)、判定スクリプトは変えない。変わるのは `[perf]` の値の目安と、UT18 の報告の比較先だけ。

| 項目 | 内容 |
|---|---|
| `copy_us` の意味 | 「背景(ホーム)1枚を裏面へ書く時間」のまま。P1 = RGB565 の全画面 memcpy(実測 800µs)。**P2b = 8bit の背景(130,560 バイト)を、行ごとの `gfx_blit8`(`memcpy` 272 回)で 512 バイト幅の面へ書く時間**(展開は無い) |
| `home_us` / `slide_us` | 書式・測り方は同じ(`ui_render` 1回。`slide_us` は dx = 240) |
| 目標 | **P1 以下**: `copy_us` ≤ 800、`home_us` ≤ 2,000、`slide_us` ≤ 1,800(見積 △: 400〜500 / 1,200〜1,800 / 1,100〜1,500。`design.md` §20.7)。**超えても不合格ではなく所見**。絶対上限は P1 と同じ(8,000 / 12,000 / 12,000)で、超えたら不合格 |
| 整合の検査 | `home_us ≈ 2,000 − 800 + copy_us`、`slide_us ≈ 1,800 − 800 + copy_us`(± 400µs) |
| `uf` | UT10・UT13・UT17 の増分(起動時の1回を除く)が 0 のまま(8bit の面へのバイト書込みでの `uf` は未確認。`design.md` §20.11 R-D2) |
| ログに足さないもの | パレット・面の種類の表示は `[init] stage=lcd ok=1` で足りる。表示の確認は gdb(`GR2FLM6` = `0x50000000`、パレット = `ui_pal`。`design.md` §20.9 G-D1) |

- 起動時の `uf` 初回検出は P1・実験と同じ(`lcd_init` の直後に 1回捨てる)。

---

## 11. CLUT8 の実機実験(`docs/design.md` §19)での UART(2026-11-21)

状態: **設計(未実装・未検証)**。**`APP_CLUT8_TEST = 1` のファームだけ**に当てはまる。製品デモ(= 0)の出力・コマンドは変えない。
**判定できないこと(変わらない)**: 画面に実際に何が映っているか(色・反転・ちらつき)は目視(`design.md` §19.5)。UART で言えるのは「ソフトがどの順に何を呼んだか」と、レジスタ `SEL` `VEN` の値まで。

### 11.0 結論(まず読む)

| # | 決定 | 内容 |
|---|---|---|
| 1 | 新しい行 | `[clut]` の1種類(§11.1)。`[help]` は実験用の文字列(§11.3) |
| 2 | 使い回す行 | `[boot]` `[init]` `[hb]`。`[hb]` の書式・正規表現 R-HB は §10.9 のまま。項目の中身だけ変わる(§11.4) |
| 3 | コマンド | **`n` `p` `d` `s` `?` だけ**(既存の許可文字。`job-runner.ps1`・`serial-read.ps1`・`gdb-run.ps1` の許可リストを**変えない**)。`+` `-` と他の文字は無視 |
| 4 | 行の組み立て | `c8pat_fmt_clut`(`clut8_pat.c`、標準C。ホストで試験できる。§11.6)。`app_logfmt` は変更しない |
| 5 | 長さ | `[clut]` は最長 約 110 バイト(`APPLOG_LINE_MAX` 256 に収まる) |

### 11.1 `[clut]` の書式

```
[clut] step=<0-5> ev=<名前> fb=<a|b> pal=<a|b> sel=<0|1> ven=<0|1> rc=<n> n=<n> m=<n> uf=<n>
```

| キー | 意味 |
|---|---|
| `step` | 段階番号(`design.md` §19.3.4)。0〜5 |
| `ev` | 出来事の名前(§11.2) |
| `fb` | ソフトが最後に `BufferChange` へ渡した面(a = `0x00800000`、b = `0x00822000`) |
| `pal` | ソフトが最後に `NoReflect` へ渡したパレット(a / b。段階5の P1・P2 は a の派生なので a) |
| `sel` | **ev を出す瞬間に読んだ** `GLCDC.GR2CLUTINT.BIT.SEL`(CPU が書いた値。画面に反映済みとは限らない) |
| `ven` | 同じく `GLCDC.GR2VEN.BIT.VEN`(1 = 反映待ち) |
| `rc` | その出来事で呼んだ FIT の API の、**最初の 0 でない** `glcdc_err_t`(0 = すべて成功。`GLCDC_ERR_INVALID_UPDATE_TIMING` = 10) |
| `n` `m` | 出来事ごとの数(§11.2) |
| `uf` | GR2 アンダーフロー検出の累計(起動時の1回は捨てたあと) |

### 11.2 `ev` の一覧

| ev | いつ出すか | `n` | `m` |
|---|---|---|---|
| `start` | 段階の最初の `settle(A, パレットA)` の後(`fb=a pal=a rc=0 ven=0` が期待値) | 0 | 0 |
| `flip` | 段階1: `flip` を呼んだ直後 | 段階内の通し番号(1〜6) | 0 |
| `nr` | 段階2: `NoReflect` だけを呼んだ直後(`pal` が変わり、`fb` は変わらない) | 0 | 0 |
| `commit` | 段階2: `BufferChange` だけを呼んだ直後(`fb` が変わる) | 0 | 0 |
| `stress_end` | 段階3: 毎垂直同期の `flip` を止めた時 | 成功した `flip` の数 | 失敗した `flip` の数(`rc ≠ 0`) |
| `wr_unused` | 段階4: **未使用セット**の idx 2 を直接書いた直後 | 書いたセット番号(0 / 1。`1 − sel` のはず) | 0 |
| `wr_inuse` | 段階4: **使用中セット**の idx 2 を直接書いた直後 | 書いたセット番号(`sel` と同じはず) | 0 |
| `wr_back` | 段階4: 両セットの idx 2 を赤に戻した直後 | 0 | 0 |
| `bench` | 段階4: `NoReflect` を 2000回続けた後 | 2000 | 経過 ms(10ms 刻み) |
| `nr1` / `nr2` | 段階5: P1 / P2 を `NoReflect` した直後(**VEN なしで連続**) | 0 | 0 |
| `settle` | 段階5: 正しく戻した後 | 0 | 0 |
| `done` | 段階の終わり(段階0は出さない) | 0 | 0 |
| `state` | `s` を受けた時(現在の値) | 0 | 0 |

- 段階0に入った時は `start` だけ(`done` は出さない)。
- 段階3の途中では `[clut]` を出さない(1秒に 59 行になるのを避ける)。`start` と `stress_end` と `done` だけ。
- **1段階あたり最大 8 行程度**。行キュー(8行)の `drop` は増えないはず(未確認。`drop=0` を合格条件にする)。

### 11.3 受信コマンド(1文字、改行不要。1 tick に1文字)

| 文字 | 動作 | 出力 |
|---|---|---|
| `n` | 次の段階(5 の次は 0)。**実行中の段階を打ち切って**新しい段階の `settle` から始める | `[clut] ... ev=start` |
| `p` | 前の段階(0 の前は 5) | 同上 |
| `d` | 今の段階をやり直す | 同上 |
| `s` | 状態を即時出力 | `[clut] ... ev=state` と `[st]`(`applog_fmt_state`。`scr` = 段階番号) |
| `?` | ヘルプ | `[help] n=next p=prev d=again s=state ?=help` |
| それ以外(`+` `-` を含む) | **無視** | なし |

- `x` は無視される文字なので、待ち時間を作るのに使う(UT14 と同じ。`txGapMs` ≤ 1000)。

### 11.4 `[hb]`(§10.2 の書式のまま)

| キー | 実験での中身 |
|---|---|
| `scr` | 段階番号(0〜5) |
| `demo` | 0 |
| `frm` | 成功した `flip` の累計(`BufferChange` を呼んだ回数) |
| `uf` | `[clut]` の `uf` と同じ |
| `pts` `x` `y` `i2c_fail` `streak` `found` | 0(タッチは初期化しない) |
| `err` `detail` `drop` | 従来どおり |

### 11.5 正規表現(Python の `re`、1行ずつ `fullmatch`)

```
R-CLUT  : \[clut\] step=([0-5]) ev=(start|flip|nr|commit|stress_end|wr_unused|wr_inuse|wr_back|bench|nr1|nr2|settle|done|state) fb=([ab]) pal=([ab]) sel=([01]) ven=([01]) rc=(\d+) n=(\d+) m=(\d+) uf=(\d+)
R-HELPC : \[help\] n=next p=prev d=again s=state \?=help
```

### 11.6 ホストで試験する関数(`clut8_pat.c`。`design.md` §19.3.6 の H-C8-7)

| 関数 | 内容 |
|---|---|
| `size_t c8pat_fmt_clut(char *buf, size_t size, uint8_t step, const char *ev, char fb, char pal, uint8_t sel, uint8_t ven, uint32_t rc, uint32_t n, uint32_t m, uint32_t uf)` | §11.1 の1行を作る(`\r\n` 込み)。戻り値 = 書いたバイト数、失敗(バッファ不足・引数不正)は 0 で `buf[0] = '\0'`(`applog_fmt_*` と同じ約束)。`ev` は §11.2 の定数だけ。**R-CLUT に fullmatch する**ことを全 `ev`・境界値(`uf` = 0xFFFFFFFF など)で試験。最長が 192 バイト未満 |

### 11.7 試験項目(実機。UC0〜UC5。`design.md` §19.6 の C8-U1 / E2-U / E3-U / E4)

**共通の前提**: 実験ファームを書込み済み。UART は `serial-read.ps1 -Stamp -Tx ...`(`design.md` §19.4.5)。`[boot]` が見えてから判定する(UART が無音の時は判定しない)。段階の時刻は、その段階に入る `n` を受けた時刻(最初の `ev=start`)からの経過。**ホスト側の時刻は ±100 ms の誤差**を許す。

| ID | 段階 | 合格条件(UART) |
|---|---|---|
| UC0 | 起動 | `[boot]` → `[init] stage=lcd ok=1 err=0 detail=0` → `[init] stage=timer ok=1` → `[init] stage=loop ok=1` → `[clut] step=0 ev=start fb=a pal=a sel=? ven=0 rc=0 n=0 m=0 uf=0`。続く `[hb]` が `scr=0 demo=0 err=0 drop=0 uf=0`、`tick` が +100 ずつ。`sel` は 0 か 1(記録する。`design.md` §19.8 C3) |
| UC1 | 1 | `ev=start` の後、`ev=flip` が**ちょうど6行**、`n` = 1〜6、`fb` が b, a, b, a, b, a、`pal` が `fb` と同じ、**`sel` が毎回反転**(前の行の `sel` と違う)、`rc=0`、`ven=1`。間隔 1000 ± 100 ms。7000 ms 後に `ev=done`(`fb=a pal=a`)。`uf` = 0 |
| UC2 | 2 | `ev=start` の後、1000 ms 後に `ev=nr`(`pal=b`、`fb=a`、`sel` が `start` の値と違う、`ven=0`、`rc=0`)。**3000 ± 100 ms 後**に `ev=commit`(`fb=b`、`pal=b`)。さらに 3000 ± 100 ms 後に `ev=nr`(`pal=a`)、3000 ± 100 ms 後に `ev=commit`(`fb=a`)。1000 ms 後に `ev=done`。`uf` = 0 |
| UC3 | 3 | `ev=start` の 11000 ± 200 ms 後に `ev=stress_end`、**`n` = 500〜600**、**`m` = 0**。さらに 1000 ± 100 ms 後に `ev=done`(`fb=a pal=a`)。`uf` = 0(**E4 の合否**)。この間の `[hb]` が連続し、`frm` が `n` だけ増える(±2)。`drop` = 0 |
| UC4 | 4 | `ev=wr_unused` の `n` = `1 − sel`、`ev=wr_inuse` の `n` = `sel`、**2つの行の `sel` が同じ**(書込みでは `SEL` は変わらない)、`rc=0`。`ev=wr_back` の後 `ev=bench`(`n` = 2000、`m` = 記録。**合否にしない**。見込み 約 40〜80 ms △)。`ev=done`(`fb=a pal=a`)。`uf` = 0 |
| UC5 | 5 | `ev=nr1` の `sel` = 開始時の `sel` の**反転**、**`ev=nr2` の `sel` = 開始時の `sel`**(= 落とし穴が `SEL` の値で確認できる)、どちらも `ven=0`、`rc=0`。`ev=settle`(`fb=a pal=a`、`sel` は開始時の反転)。`ev=done`。`uf` = 0 |

- 段階の途中で `n` を送る試験(打ち切り)は、`UC3` の `stress_end` を出さなくてよいことだけ確認する(`n` を段階3の途中で送り、`ev=start step=4` が出る)。
- 失敗時はログ全文(`jobs/outbox/<id>.log`、または `serial-read.ps1` の出力)を添えて報告する。


---

## 12. Wi-Fi(ESP32 AT)試作(`docs/design.md` §21)での UART(2026-11-25)

**改訂(2026-11-25): AT 1.1.3.0 を実機で確認したことによる**(`design.md` §21 冒頭)。`store` を削除、`qap` `lapopt` `scan` を追加(step 14 個)、`[wifiv]` に `scan` `ap` `oth` を追加、試験を UW0〜UW6 に見直した。**旧 UW3〜UW5 は新 UW4〜UW6**。
状態: **設計(改訂版は未実装・未検証。旧設計の UW0〜UW2 は実機で合格済み)**。**`APP_WIFI_TEST = 1` のファームだけ**に当てはまる。製品デモ(= 0)の出力・コマンドは変えない。
**判定できないこと**: 画面は使わない(専用モード)。UART で言えるのは「ソフトが ESP32 に何を送り、何が返ったか(分類した結果)」まで。**ESP32 の実際の電波の状態**は見えない。
**秘密の扱い**: **SSID・パスワード・BSSID は、どの行にも出さない。ESP32 から受けた生の行も出さない**(エコーバックにパスワードが混ざりうる)。

### 12.0 結論(まず読む)

| # | 決定 | 内容 |
|---|---|---|
| 1 | 新しい行 | `[wifi]`(step の経過と最終結果)と `[wifiv]`(値)の2種類。書式は §12.1 |
| 2 | 使い回す行 | `[boot]`(`ch=2`)、`[init]`(`stage=wifi` を足す)、`[hb]`、`[err]`、`[st]`、`[help]`。書式・正規表現 R-HB は §10.9 のまま |
| 3 | コマンド | **`d` `n` `p` `s` `?` だけ**(既存の許可文字。`job-runner.ps1`・`serial-read.ps1` の許可リストを**変えない**)。`+` `-` と他の文字は無視 |
| 4 | ログの経路 | **SCI2(CN8 → COM7、115200)のまま**。SCI1 は ESP32 専用 |
| 5 | 行の組み立て | `wifi_fmt_*`(`wifi_core.c`。標準C。ホストで試験)。**秘密を引数に取らない** |
| 6 | **`scan` の結果(改訂)** | **自分の SSID が見えるか**を `[wifiv] key=scan`(件数)と `key=ap`(ch・rssi・ecn)で出す。**SSID・他の AP の名前・MAC は、どの行にも出さない**(`+CWLAP:` の生の行は出さない・保存しない)。**分類できなかった行は「形」だけ**(`key=oth`。英字 → `a`、数字 → `9`) |

### 12.1 書式

```
[wifi] step=<reset|boot|at|echo|gmr|mode|qap|lapopt|scan|join|ip|dns|ping_ip|ping_dns> res=<start|ok|fail|timeout|skip> try=<n> ms=<n> code=<n>
[wifi] result=<pass|fail> stage=<step 名|none> ping_ip=<n> ping_dns=<n>
[wifi] state=<idle|run|done> step=<step 名|none> bytes=<n>
[wifiv] key=<ver|sdk|ip|gw|mask|dns|ping|scan|ap|oth> val=<ASCII>
[init] stage=wifi ok=<0|1> err=<n> detail=<n>
```

| キー | 意味 |
|---|---|
| `step` | `design.md` §21.4.4 の step 名 |
| `res` | `start` = 開始、`ok` = 成功、`fail` = 失敗(`ERROR` / `FAIL` / 送信失敗)、`timeout` = 時間切れ、`skip` = 飛ばした(`wifi_secret.h` が無い時の `join` 以降) |
| `try` | その step の何回目か(1 から。`at` は最大 5、`join` は最大 2) |
| `ms` | その step の開始から終わりまでの時間(10ms 刻み)。`res=start` の行は 0 |
| `code` | 失敗の理由。`join` の `+CWJAP:<n>` の `n`(1 タイムアウト、2 パスワード違い、3 AP が見つからない、4 接続失敗)。**100 = コマンドが長すぎて組み立てられない、101 = 送信失敗(`SCI_ERR_INSUFFICIENT_SPACE` など)**。**`ping_ip` / `ping_dns`: 200 = `+PING:` が来ずに `ERROR`(`AT+PING` 未対応の疑い)、201 = `+PING:TIMEOUT` を受けた**。**`step=scan res=ok` の時だけ別の意味**: 0 = 自分の SSID が見える、1 = 見えないが近い名前がある(打ち間違いの疑い)、2 = ほかの AP は見えるが自分のは見えない、3 = 1件も見えない。それ以外は 0 |
| `result` | `pass` = 範囲の全 step が成功か、続行可の step だけが失敗。`fail` = 打ち切り |
| `stage` | `fail` の時に打ち切った step。`pass` は `none` |
| `ping_ip` `ping_dns` | 応答時間(ms)。未実施・失敗は 0 |
| `state` | `s` を受けた時の状態。`bytes` = ESP32 から受けたバイト数の累計 |
| `[wifiv] key` | `ver` = `AT version:` の次から最大 40 文字、`sdk` = `SDK version:` の次から最大 40 文字(**ASCII 0x21〜0x7E 以外(空白を含む)は `_` に置換**)、`ip` = `AT+CIFSR` の `STAIP`(**`gw` `mask` は 1.x では出ない**。キーだけ残す)、`dns` = `AT+CIPDOMAIN` の結果、`ping` = 直前の ping の ms。**値に空白を含めない**。**`scan` = `total=<n>,found=<n>,near=<n>,bad=<n>`(各 0〜999)、`ap` = `ch=<n>,rssi=<-n>,ecn=<n>`(`found` ≧ 1 の時だけ。rssi が最大の1件)、`oth` = 分類できなかった行の「形」(最大 40 文字。1 step に最大 3 行)** |

- 共通ルールは §2.1(ASCII、`\r\n`、最長 255 バイト + NUL)のまま。
- **`[hb]` の中身**(§10.2 の書式のまま): `scr` = 今の step 番号(0 = 待機、1 = reset … 14 = ping_dns。**改訂後。`scan` = 9**)、`demo` = 0、`frm` = ESP32 から受けたバイト数の累計の下位 32 bit、`uf` = 0、`pts` `x` `y` `i2c_fail` `streak` `found` = 0(タッチは初期化しない)、`err` `detail` `drop` は従来どおり。
- **`[err]`**: `code=9`(`APP_ERR_WIFI_OPEN`: SCI1 を開けない。`detail` = `sci_err_t`)、`code=10`(`APP_ERR_WIFI_RX`: SCI1 の受信異常。`detail` = `sci_cb_evt_t` の 2・3・4・8。1秒に1行まで)。書式は §2.4 のまま。
- **1 step あたり最大 2 行**(`start` と終わり)+ `[wifiv]` が数行。`scan` は終わりに `scan` `ap` `step` の 3 行を続けて出し、`oth` が最大 3 行(合計 約 6 行のまとまり)。`join` の間は `[hb]` だけが出る(20 秒で 20 本)。行キュー(8行)の `drop` は増えないはず(未確認。`drop=0` を合格条件にする。**増えたら `oth` を 2 行に減らす**)。
- **秘密**: `scan` の間に受けた `+CWLAP:` の行(他人の SSID・MAC を含む)は、**ホスト側のコードが照合・集計して捨てる**。UART に出るのは上の数と、自分の AP の ch・rssi・ecn、`oth` の形だけ。

### 12.2 受信コマンド(1文字、改行不要。1 tick に1文字)

| 文字 | 動作 | 出力 |
|---|---|---|
| `d` | **全部**(`reset` から `ping_dns` まで)。EN リセットから始める | `[wifi] step=reset res=start ...` |
| `n` | `reset` から `at` まで(起動と AT の応答だけ) | 同上 |
| `p` | `at` を1回だけ(リセットなし) | `[wifi] step=at res=start ...` |
| `s` | 状態を即時出力 | `[wifi] state=...` と `[st]` |
| `?` | ヘルプ | `[help] d=run n=boot p=at s=state ?=help` |
| それ以外 | **無視** | なし |

- **実行中の `d` `n` `p` は無視**(何も出さない)。`s` `?` はいつでも受け付ける。
- `x` は無視される文字なので、待ち時間を作るのに使う(UT14 と同じ。`txGapMs` ≦ 1000)。
- `APP_WIFI_AUTOSTART = 1` のビルドは、`[init] stage=wifi ok=1` の **2 秒後に `d` と同じ**を1回実行する。`serial` ジョブに `tx` を付けなくてよい。

### 12.3 正規表現(Python の `re`、1行ずつ `fullmatch`)

```
R-WIFI-STEP   : \[wifi\] step=(reset|boot|at|echo|gmr|mode|qap|lapopt|scan|join|ip|dns|ping_ip|ping_dns) res=(start|ok|fail|timeout|skip) try=(\d+) ms=(\d+) code=(\d+)
R-WIFI-RESULT : \[wifi\] result=(pass|fail) stage=(none|reset|boot|at|echo|gmr|mode|qap|lapopt|scan|join|ip|dns|ping_ip|ping_dns) ping_ip=(\d+) ping_dns=(\d+)
R-WIFI-STATE  : \[wifi\] state=(idle|run|done) step=(none|reset|boot|at|echo|gmr|mode|qap|lapopt|scan|join|ip|dns|ping_ip|ping_dns) bytes=(\d+)
R-WIFIV       : \[wifiv\] key=(ver|sdk|ip|gw|mask|dns|ping|scan|ap|oth) val=([\x21-\x7e]{0,40})
R-WIFIV-SCAN  : \[wifiv\] key=scan val=total=(\d{1,3}),found=(\d{1,3}),near=(\d{1,3}),bad=(\d{1,3})
R-WIFIV-AP    : \[wifiv\] key=ap val=ch=(\d{1,3}),rssi=(-\d{1,3}),ecn=(\d{1,2})
R-WIFIV-OTH   : \[wifiv\] key=oth val=[\x21-\x7e]{1,40}          ← 形。英数字は `a` と `9` だけが現れる(判定: `[b-zA-Z0-8]` が 0 個)
R-INIT-WIFI   : \[init\] stage=wifi ok=([01]) err=(\d+) detail=(\d+)
R-HELPW       : \[help\] d=run n=boot p=at s=state \?=help
```

### 12.4 ホストで試験する関数(`wifi_core.c`。`design.md` §21.7)

| 関数 | 内容 |
|---|---|
| `wifi_fmt_step(buf, size, step, res, try, ms, code)` / `wifi_fmt_result(buf, size, pass, stage, ping_ip, ping_dns)` / `wifi_fmt_state(...)` / `wifi_fmt_val(buf, size, key, val)` | §12.1 の1行を作る(`\r\n` 込み)。戻り値 = 書いたバイト数。失敗(バッファ不足・引数不正・`val` が長すぎる)は 0 で `buf[0] = '\0'`(`applog_fmt_*` と同じ約束)。**R-WIFI-* に fullmatch する**ことを全 step・全 res・境界値(`ms` = 0xFFFFFFFF など)で試験。**SSID・パスワードを引数に取らない** |
| **秘密の混入の検査**(H-W6) | 目印の文字列をパスワード・SSID にして全シナリオを流し、**全ログ行に含まれない**こと |
| `wifi_parse_cwlap(line, my_ssid, &ap)` / `wifi_shape(out, size, line)`(**改訂で追加**。`design.md` §21.4.8・H-W10〜H-W13) | `+CWLAP:` の1行を読み、`ecn` `rssi` `ch` と `match`(なし / exact / near)を返す。**SSID を保存しない**。`wifi_shape` は英字 → `a`、数字 → `9`、他の印字可能 ASCII はそのまま、他は `_`(最大 40 文字)。**`key=scan` `key=ap` `key=oth` の値が R-WIFIV-* に fullmatch** |

### 12.5 試験項目(実機。UW0〜UW6。`design.md` §21.8 の W0〜W13 と対応。2026-11-25 改訂)

**前提**: SW3 の 2ch = OFF。CN8 と ECN1 の両方に給電。`wifi_secret.h` を作成済み(ユーザー)。基板はルーターの近く。`serial-read.ps1 -Stamp`(または `serial` ジョブ、`seconds` = **120**)。**`[boot]` が見えてから判定する**(UART が無音なら、SW3 の 2ch・COM7 の占有・ECN1 の給電を確認。判定しない)。**自動開始のビルド(`-DAPP_WIFI_AUTOSTART=1`)でなければ、`tx` で `d` を送る**(前回はこちら)。

| ID | 対応 | 合格条件(UART) | 状態(2026-11-25) |
|---|---|---|---|
| UW0 | W0 | `[boot]` → `[init] stage=lcd` は**出ない**(専用モード)→ `[init] stage=wifi ok=1 err=0 detail=0`。続く `[hb]` が `demo=0 err=0 drop=0`、`tick` が +100 ずつ | **合格(旧設計で実機確認済み)** |
| UW1 | W1・W2 | 開始から: `step=reset res=start` → `res=ok` → `step=boot`(`ok` か `timeout`)→ `step=at res=ok`(`try` ≦ 5)。`ms` は §21.8 の目安 | **合格(旧設計で実機確認済み。`boot` = 700ms)** |
| UW2 | W3・W4 | `step=echo` → `step=gmr res=ok` → `[wifiv] key=ver` と `key=sdk`(**版は記録だけ。数字を合否に使わない**。**`step=store` は出ない**)→ `step=mode res=ok` → `step=qap`(`ok` か `fail`)→ `step=lapopt`(`ok` か `fail`。どちらでも先へ) | 旧設計では `store` が `fail`、版の基準が不合格だった → **基準を見直した(再試験)** |
| **UW3** | **W5(新)** | **`step=scan res=ok`** の直前に `[wifiv] key=scan val=total=<n>,found=<n>,near=<n>,bad=<n>`(R-WIFIV-SCAN)。**`total` ≧ 1、`bad` = 0**。`found` ≧ 1 の時は `[wifiv] key=ap`(R-WIFIV-AP)。`step=scan` の `code` が 0〜3(意味は §12.1)。**`key=oth` は 3 行まで**、R-WIFIV-OTH に一致。**`CWLAP` の文字列がログのどこにも無い**。`[hb]` が途切れない(`scan` の最中も 1 秒ごと)、`drop=0`、`[err] code=10` が出ない | 未(新規) |
| UW4 | W6・W7 | `step=join res=ok`(失敗時は `code=` を **UW3 の `code` と組にして**記録)→ `step=ip res=ok` と `[wifiv] key=ip val=<0.0.0.0 でない>`(`STAIP` が取れない版は `val` なしで可。その時は `join` の `ok` と `key=oth` の形を根拠にする) | 旧 UW3。`join` が `code=3` で不合格(再試験) |
| UW5 | W8〜W11 | `step=dns` → `step=ping_ip res=ok` と `[wifiv] key=ping val=<1〜1000>` → `step=ping_dns res=ok` → `[wifi] result=pass stage=none ping_ip=<n> ping_dns=<n>`(**ゴール**)。**`ping_ip` が `res=fail code=200` なら、AT+PING 未対応の疑い**(`design.md` §21.9 RW16 へ) | 旧 UW4。未(`join` で打ち切り) |
| UW6 | W12・W13 | 約 70 秒後に `tx` で `d` を送り、**もう一度 `result=pass`**。全ログに SSID・パスワードが含まれない(一致数だけ報告)。**`CWLAP` が 0 件** | 旧 UW5。未 |

- 「書式」の判定は R-WIFI-* の fullmatch。**`[hb]` が途切れない**(`join` と `scan` の最中も 1 秒ごとに出る)ことも見る(メインループがブロックしていない証拠)。
- **UW3 の読み方(原因の切り分け)**は `design.md` §21.4.7 (d)。**`code=3`(`total=0`)で、UW4 の失敗を「AP の問題」と決めない**。
- 失敗時はログ全文(`jobs/outbox/<id>.log`)を添えて報告する。**ただし秘密の文字列が含まれていないかを、添える前に確認する**。**`key=oth` の「形」は、英数字を含まないので添えてよい**。


---

## 13. 天気・気温の取得と時刻同期(`docs/design.md` §22)での UART(2026-11-25)

**2026-11-27 注(rx72n_stockview)**: この §13 は複製元(給湯器デモ + 天気)の仕様。**株価ビューの製品は `[wx]`(天気)を `[stk]` に置き換える。書式・正規表現・判定は `docs/design.md`(rx72n_stockview)§7.5 が優先**(§10〜§12 の `[hb]` `[init]` `[boot]` `[err]` `[st]` `[perf]` `[wifi]` `[wifiv]` は書式を変えない)。**Phase 0(TLS の実機検証。専用モード `APP_WIFI_TEST = 1`)では、この §13 の `[wx] res=fail` が使われた**(下の `R-WX-FAIL` を 2026-11-27 に `err` 1〜9・末尾 `len=` へ直した)。**(2026-11-27 追記: 時計・天気画面を足したため、`[wx]` の名前を天気の取得で再び使う。書式・正規表現・判定は §14 が優先。ここ §13 の R-WX-* は使わない。)** 専用モードの UART コマンドに **`t`(Yahoo 平文)`y`(Yahoo SSL)`g`(Google SSL)`z`(`y` の前に `AT+CIPSSLCCONF=0,0,0`)** が増えた(`tools/job-runner.ps1` の `tx` の許可文字 `[npsd+?xtygz-]`)。

状態: **設計(未実装・未検証)**。**`APP_WX = 1`(製品)と `APP_WIFI_TEST = 1`(専用モード。`x` だけ)に当てはまる**。`APP_WX = 0` かつ `APP_WIFI_TEST = 0` の出力・コマンドは変えない。
**判定できないこと**: 画面の見た目(アイコンの形・色)は目視(カメラは補助)。表示面の画素の判定は gdb(`design.md` §22.13 の X7)。UART で言えるのは「ソフトが何を取得し、どの状態にしたか」まで。
**秘密・プライバシー**: **SSID・パスワード・BSSID・応答の本文は、どの行にも出さない**。出すのは数値(気温・コード・時刻・状態の番号)と名前だけ。

### 13.0 結論(まず読む)

| # | 決定 | 内容 |
|---|---|---|
| 1 | 新しい行 | **`[wx]`** の 4 形式(結果 成功 / 結果 失敗 / 要求の無視 / 状態)。書式は §13.1 |
| 2 | 変えない行 | **`[hb]` は変えない**(R-HB・`drop` の判定をそのまま使う)。`[st]` `[ui]` `[perf]` `[init]` `[err]` `[boot]` も書式を変えない |
| 3 | `[ev]` | ボタン名に **`h_wx`**(更新ボタン)が増える。`btn=h_wx st=down` / `st=act`。**画面は変わらない**(`scr=` は続かない) |
| 4 | コマンド | **`x` = 更新ボタンの仮想タップ**(ホームで、遷移中・デモ中・押下表示中でない時)。**`job-runner.ps1` の `tx` の許可リスト `[npsd+?x-]` に `x` は既にあるので、変更不要**。`applog_cmd_parse` に `'x'` を足す(今は無視される文字) |
| 5 | `[wifi]` の行 | 製品(`APP_WX = 1` かつ `APP_WIFI_TEST = 0`)では **`res=start` の行を出さない**・**`[wifiv] key=oth` を出さない**(行キュー 8 行の `drop` を避ける)。専用モードは §12 のまま |
| 6 | step 名 | §12 の `step=` に **`wx_mux` `wx_close` `wx_tcp` `wx_send` `wx_recv`** が増える(番号 15〜19。**2026-11-27 改訂: Phase 0 で step `wx_sslconf`(17。`AT+CIPSSLCCONF=0,0,0`。専用モードの `z` だけ)が `wx_close`(16)と `wx_tcp` の間に入り、`wx_tcp` = 18、`wx_send` = 19、`wx_recv` = 20、`WIFI_STEP_END` = 21。`[hb]` の `scr` は専用モードで 0〜20**) |
| 7 | 秒まで見える時刻 | `[wx] ... jst=<hhmmss>`(`Date:` から作った JST)と、状態行の `clk=<hhmmss>`(ソフト時計)。**PC の時刻と ±3 秒で比べる**ための項目 |

### 13.1 書式

```
[wx] res=ok src=<boot|timer|touch|uart> temp10=<n> wmo=<n> kind=<sun|partly|cloud|fog|rain|snow|thunder|unknown> http=<n> ms=<n> sync=<0|1> jst=<hhmmss|none> obs=<hhmm> closed=<0|1>
[wx] res=fail src=<boot|timer|touch|uart> err=<n> http=<n> ms=<n>
[wx] req=<ignored|busy|off> src=<touch|uart>
[wx] net=<off|conn|on|err> busy=<0|1> have=<0|1> temp10=<n> kind=<名前|none> age=<n> clk=<hhmmss|none> date=<yyyymmdd|none> wd=<0..6|9> sync=<0|1>
```

| キー | 意味 |
|---|---|
| `res` | `ok` = 取得して解析できた / `fail` = 失敗(画面の値は変えない) |
| `src` | 取得のきっかけ: `boot` = 起動後の最初 / `timer` = 10 分ごと(やり直しを含む)/ `touch` = 更新ボタンの実タッチ / `uart` = `x` |
| `temp10` | 気温(0.1℃。**符号つき整数**。例 `127` = 12.7℃、`-34` = −3.4℃)。範囲 −600〜600 |
| `wmo` | WMO の天気コード(0〜99) |
| `kind` | アイコンの種類(`design.md` §22.5 の表)。状態行の `have=0` の時は `none` |
| `http` | HTTP の状態(200 など)。応答が無い時は 0 |
| `ms` | 取得の開始(`wx_mux`)から結果まで(10ms 刻み) |
| `sync` | `Date:` が読めて、ソフト時計を合わせたら 1。`Date:` が無い・壊れていたら 0 |
| `jst` | `Date:`(UTC)+ 9 時間の時刻(`hhmmss`)。`sync=0` の時は `none` |
| `obs` | 応答の `current.time` の `hhmm`(JST。15 分刻みの「データの時刻」)。無ければ `0000`。**PC の `curl` との照合用** |
| `closed` | `CLOSED` を受けて終わったら 1、タイムアウトで終わったら 0 |
| `err` | **1** = TCP の接続に失敗(`wx_tcp`)/ **2** = 送信に失敗(`wx_send`)/ **3** = 応答が無い・途切れ(`wx_recv`)/ **4** = HTTP の状態が 200 でない(`http` を見る)/ **5** = キー・値が無い・読めない / **6** = 値が範囲外 / **7** = 受信バッファ超過 / **8** = 接続(`join`)に失敗した(この時の `http` は 0) |
| `req` | 更新要求を受け付けなかった理由: `ignored` = 前回の取得開始から 5 秒未満 / `busy` = 取得中か接続中 / `off` = Wi-Fi が使えない(OFF) |
| `net` | `off` = `wifi_secret.h` が無い等(ESP32 に触れない)/ `conn` = 接続中(最初の接続前を含む)/ `on` = 接続済み / `err` = 失敗して待っている(再接続・再取得) |
| `busy` | 接続中・取得中 = 1(更新アイコンが回る) |
| `have` | 一度でも取得できた = 1 |
| `age` | 最後の成功からの秒(未取得は **-1**) |
| `clk` | ソフト時計(JST)の `hhmmss`。未同期は `none` |
| `date` | ソフト時計の日付 `yyyymmdd`(JST)。未同期は `none` |
| `wd` | 曜日(0 = 日曜 … 6 = 土曜)。未同期は **9** |

- **共通ルールは §2.1**(ASCII、`\r\n`、最長 255 バイト + NUL)のまま。最長の行(状態行)は約 190 バイト。
- **出す時**: 結果行 = 取得 1 回に 1 行(成功か失敗)。要求の無視 = 無視した時に 1 行。**状態行 = `s` を受けた時と、`net` か `busy` が変わった時**(取得 1 回で 2〜4 行)。
- **出さないもの**: 応答の本文・ヘッダー・SSID・パスワード・IP・MAC。

### 13.2 `[ev]` と受信コマンド

| 項目 | 内容 |
|---|---|
| `[ev] btn=h_wx st=down src=<touch\|uart>` | 更新ボタンの押下が確定した時(当たり判定 (228,136,60,52) に入った時) |
| `[ev] btn=h_wx st=act src=<touch\|uart>` | 動作の実行(離し確定、または押下から 90ms の遅い方)。**続く `[wx] req=...` か `[wx] res=...` で、取得を始めたか分かる** |
| `x` | **h_wx の仮想タップ**(150ms 押す)。押下表示・実行・`[ev]` は実タッチと同じ(`src=uart`)。**ホーム以外・画面遷移中・デモ中・押下表示中は無視**(`[ev]` も出ない)。**連続して送る時は 300ms 以上あける**(`+` `-` と同じ) |
| 専用モード(`APP_WIFI_TEST = 1`)の `x` | `WIFI_RANGE_WX`(接続済みの時だけ。`d` の後)。`[wifi] step=wx_mux …` から `[wifi] result=pass` と `[wx] res=...`。**実行中の `x` は無視**。専用モードの `s` は `[wifi] state=` と `[st]` のまま |
| `s`(製品) | 従来の `[st]` `[scan]` `[ui]` の次に、**`[wx]` 状態行を 1 行** |
| `?` | 製品: `[help] n=next p=prev s=state d=demo +=up -=down x=wx ?=help`(`x=wx` が増える)。専用モード: `[help] d=run n=boot p=at s=state ?=help` のまま |
| ボタン名の一覧(§10.3) | `h_dn h_up h_fill h_reheat h_usage h_power h_demo` + **`h_wx`** + `f_stop ...`(ボタン番号 16 = `UI_BTN_H_WX`) |

### 13.3 正規表現(Python の `re`、1行ずつ `fullmatch`)

```
R-WX-OK    : \[wx\] res=ok src=(boot|timer|touch|uart) temp10=(-?\d{1,3}) wmo=(\d{1,2}) kind=(sun|partly|cloud|fog|rain|snow|thunder|unknown) http=(\d{3}) ms=(\d+) sync=([01]) jst=(\d{6}|none) obs=(\d{4}) closed=([01])
R-WX-FAIL  : \[wx\] res=fail src=(boot|timer|touch|uart) err=([1-9]) http=(\d{1,3}) ms=(\d+) len=(\d+)      ← 2026-11-27 改訂(Phase 0): err は 1〜9(9 = 取得中に ESP32 が再起動〔ready〕)、末尾に len(バッファに溜めたバイト数。最大 3072。本文は出さない)
R-WX-REQ   : \[wx\] req=(ignored|busy|off) src=(touch|uart)
R-WX-STATE : \[wx\] net=(off|conn|on|err) busy=([01]) have=([01]) temp10=(-?\d{1,3}) kind=(sun|partly|cloud|fog|rain|snow|thunder|unknown|none) age=(-1|\d+) clk=(\d{6}|none) date=(\d{8}|none) wd=([0-69]) sync=([01])
R-EV-BTN   : (§10.9 の R-EV-BTN のボタン名に h_wx を足す)
R-WIFI-STEP: (§12.3 の step の選択肢に wx_mux|wx_close|wx_tcp|wx_send|wx_recv を足す。製品では res=start が出ない)
R-HELP-X   : \[help\] n=next p=prev s=state d=demo \+=up -=down x=wx \?=help
```

### 13.4 ホストで試験する関数(`wx_core.c`。`design.md` §22.12)

| 関数 | 内容 |
|---|---|
| `wx_fmt_ok(buf, size, src, &result, jst, closed)` / `wx_fmt_fail(...)` / `wx_fmt_req(...)` / `wx_fmt_state(...)` | §13.1 の 1 行(`\r\n` 込み)。失敗(バッファ不足・引数不正)は 0 で `buf[0] = '\0'`(`wifi_fmt_*` と同じ約束)。**R-WX-* に fullmatch** することを、全 `src`・全 `kind`・境界値(`temp10` = −600 / 600、`ms` = 0xFFFFFFFF、`age` = −1)で試験。**秘密(SSID・パスワード・本文の目印)を引数に取らない**(型で防ぐ) |
| 秘密の混入の検査(H-X6) | 本文の目印 `BODY_SENTINEL` を応答に入れて流し、**全ログ行に出ない** |

### 13.5 試験項目(実機。UX0〜UX6。`design.md` §22.13 の X0〜X13 と対応)

**前提**: SW3 の 2ch = OFF、給電、`wifi_secret.h` あり、COM7 を他が開いていない、**PC の時計が NTP で合っている**。`serial-read.ps1 -Stamp`(または `serial` ジョブ。**接続 + 最初の取得を見る回は 60 秒、定期取得を見る回は 720 秒**)。**`[boot]` が見えてから判定する**。

| ID | 対応 | 合格条件(UART) |
|---|---|---|
| UX0 | X0 | **専用モード**: `tx=d`(全部)で `result=pass` → `tx=x` で `step=wx_mux`〜`wx_recv` が `res=ok`(各 `try=1`)→ **`[wx] res=ok src=uart ... http=200`**。`key=oth` の形を記録(RW-X1)。R-WX-OK に一致 |
| UX1 | X1・X2 | **製品**: `[init]` ×4 ok → `[perf]` → 約 3 秒後から `[wifi] step=reset res=ok` … `step=ip res=ok` → `[wifi] result=pass` → `[wx] res=ok src=boot`。`temp10` ∈ [−600, 600]、`wmo` ∈ [0, 99]、`kind` が表どおり、`ms` ≦ 5000、接続から最初の取得まで 20 秒以内。`[hb]` が +100 ずつ、`err=0 drop=0 uf=0` |
| UX2 | X3 | `[wx] sync=1 jst=hhmmss` と**その行の受信時刻(PC の JST)**の差が ±3 秒以内。`tx=s` の `[wx] net=on ... clk=hhmmss date=yyyymmdd wd=<n>` の `clk` と PC の差が ±3 秒、`date` `wd` が一致。5 回の取得で毎回 |
| UX3 | X4 | 同じ URL を PC の `curl` で取得し、`obs`(機器)= `current.time`(PC)なら `temp10` と `wmo` が完全一致。違う時は気温差 ≦ 1.5℃・`kind` が同じか隣。**`obs` が PC の JST より未来か 30 分以上前なら不合格** |
| UX4 | X5 | `tx=x`(5 秒以上あけて 2 回)→ `[ev] btn=h_wx st=down src=uart` → `st=act` → `[wx] res=ok src=uart`。**5 秒未満の 2 回目は `[wx] req=ignored src=uart`**。取得中の `x` は `req=busy`。デモ中・ホーム以外の `x` は何も出ない。**実タッチ**(ユーザー): `[ev] btn=h_wx ... src=touch` → `[wx] res=ok src=touch` |
| UX5 | X6 | `[wx] res=ok src=boot` の後、**約 10 分(600 ± 2 秒)後に `[wx] res=ok src=timer`**(前回の取得開始から)。その間 `[hb]` が途切れない |
| UX6 | X8・X9 | **OFFLINE**: `wifi_secret.h` が無いビルドで `tx=s` → `[wx] net=off busy=0 have=0 ... clk=none date=none wd=9 sync=0`、`[wifi]` の行が出ない。**エラー**: AP を止めて `[wifi] result=fail stage=join` → `[wx] net=err`、約 60 秒後に `net=conn`。AP を戻すと `net=on`。**デモ 33 秒の回帰**: `[ev]` 36 行・`drop=0`・`uf=0`・`[perf]` が §22.13 の X10 以内 |

- **判定の順**: 書式(R-WX-*)→ 値の範囲 → 時刻の照合(UX2)→ PC との照合(UX3)。
- **秘密の確認**: 受信ログ(`jobs/outbox/<id>.log`)に SSID・パスワード・`"latitude"` `"generationtime_ms"` などの本文の語が**無い**こと(一致数だけ報告)。
- 失敗時はログ全文を添えて報告する(**添える前に、秘密の文字列が含まれていないか確認**)。

---

## 14. 時計・天気画面と自動切り替え(rx72n_stockview `docs/design.md` §13)での UART(2026-11-27)

状態: **設計(未実装・未検証)**。**`APP_WX_PRODUCT = 1`(製品)に当てはまる。`[scr]` は `APP_CLK_PREVIEW = 1`(ネットワークなしの切り替えの試験)でも出る**。専用モード(`APP_WIFI_TEST = 1`)の出力は変えない。
**この §14 は、§13 の `[wx]`(複製元の天気)を、株価ビューの製品で天気の取得に再び使うための仕様**(`[wx]` の書式は §13 に `len=` と `src=retry` を足した形。**正規表現は下の R-WXW-*。§13.3 の R-WX-* は使わない**)。株価の `[stk]` の書式は変えない(`docs/design.md` §7.5)。
**判定できないこと**: 画面の見た目(色・形・ちらつき)は目視(`design.md` §13.10 の V-C0〜V-C3)と gdb(G-C)。UART で言えるのは、「ソフトが何を取得し、どの画面・どのパレットを出す入れ替えを受け付けたか」まで。
**秘密・プライバシー**: **SSID・パスワード・IP・MAC・応答の本文(位置・気温・時刻の JSON)は、どの行にも出さない**。出すのは数値(気温・WMO コード・時刻・番号)と名前だけ。

### 14.0 結論(まず読む)

| # | 決定 | 内容 |
|---|---|---|
| 1 | 新しい行 | **`[wx]` の 3 形式**(天気の結果 成功 / 失敗、状態)と **`[scr]`**(表示面の入れ替え)。書式は §14.1 |
| 2 | 変えない行 | **`[hb]` `[init]` `[boot]` `[err]` `[st]` `[perf]` `[wifi]` `[stk]` `[ev]` の書式は変えない**。`[stk] cycle=` の `ok` `fail` は株価の 3 ビット(0〜7)のまま |
| 3 | `[hb]` の `scr` | **製品では今の画面**(0 = 株価、1 = 時計・天気)。書式(`scr=(\d+)`)は変えない。専用モードでは今までどおり step 番号 |
| 4 | コマンド | **変更なし**(`x` `s` `?`)。`x` は時計・天気画面でもタップと同じ(株価画面へ戻して更新)。`s` は `[stk]` の行に続けて **`[wx] have=...` を 1 行**足す。`job-runner.ps1` の許可リストは変更不要 |
| 5 | `[err]` | **`code=11`(`APP_ERR_LCD_CLUT`)**が増える(パレットの書込みの失敗。`detail` = `glcdc_err_t`。起きたら描画が止まる)。書式は変えない |
| 6 | 切り替えの間隔を測る | `[scr]` の **`tick`(`[hb]` と同じ 10ms カウンタ)**の差で測る(`to` が変わる行どうし) |
| 7 | ログの量 | 通常 1 分に数行(`[scr]` は入れ替えを受け付けるたび。画面が変わらないときは分の変わり目だけ)。**`[wifi]` の `res=ok` は製品では出さない(従来どおり)**ので、1 周期で `[stk]` 最大 5 行 + `[wx]` 1 行 + `[scr]` 数行。行キュー 8 行の `drop` は §8.3 の書き方 |

### 14.1 書式

```
[wx] res=ok src=<boot|timer|touch|uart|retry> temp10=<n> wmo=<n> kind=<sun|partly|cloud|fog|rain|snow|thunder|unknown> http=<n> ms=<n> sync=<0|1> jst=<hhmmss|none> obs=<hhmm> len=<n> closed=<0|1>
[wx] res=fail src=<boot|timer|touch|uart|retry> err=<1..9> http=<n> ms=<n> len=<n>
[wx] have=<0|1> temp10=<n> wmo=<n> kind=<名前|none> age=<-1|n>
[scr] to=<stock|clock> pal=<0..8> clk=<hhmm|none> kind=<名前|none> temp=<-99..99|none> tick=<n>
```

| キー | 意味 |
|---|---|
| `temp10` | 気温(0.1℃。符号つき整数。`129` = 12.9℃、`-4` = −0.4℃)。範囲 −600〜600。**画面の整数の気温は、これを絶対値の 0.5 を遠い方へ丸めたもの**(`-4` → `0`、`-5` → `-1`、`129` → `13`。±99 で頭打ち) |
| `wmo` | WMO の天気コード(0〜99) |
| `kind` | 天気の種類(`design.md` §13.6 の表)。`[wx] have=0` と、`[scr]` の天気なしは `none` |
| `http` `ms` `sync` `jst` `closed` `len` `err` | §13.1 と同じ意味(`err` 1〜9、`len` = バッファに溜めたバイト数。**本文は出さない**)。`ms` = 取得の開始(`wx_mux`)から結果まで |
| `obs` | 応答の `current.time` の `hhmm`(JST。15 分刻みの「データの時刻」)。無ければ `0000`。**PC の `curl` との照合用** |
| `src` | §13.1 と同じ。**`retry` = 失敗した取得のやり直し**(天気にも株価にも付く) |
| `have`(`[wx]` 状態行) | 天気の最後の成功がある = 1。**0 のとき `temp10` = 0、`wmo` = 0、`kind` = none、`age` = -1 にそろえる** |
| `age` | 最後の成功からの秒(未取得は `-1`)。**3,600 以上は画面では「不明」**(`CLK_WX_STALE_S`) |
| `[scr] to` | **入れ替えを受け付けた画面**(`stock` = 株価、`clock` = 時計・天気) |
| `[scr] pal` | 使うパレット番号: **`to=stock` は 0**、**`to=clock` は 1〜8(`1 + kind`。天気なしは 8)** |
| `[scr] clk` | 時計・天気画面の時刻 `hhmm`(`clock_min` から。未同期は `none`)。**株価画面のときも付く**(分の変わり目の照合に使う) |
| `[scr] kind` `temp` | 時計・天気画面の天気の種類と整数の気温(天気なしは `none`)。株価画面のときも付く |
| `[scr] tick` | `[hb]` の `tick` と同じ 10ms カウンタ(その入れ替えを受け付けた時) |

- **共通ルールは §2.1**(ASCII、`\r\n`、最長 255 バイト + NUL)のまま。最長の行(`[wx] res=ok`)は約 150 バイト。
- **`[wx] res=ok / fail`**: 天気の取得 1 回に 1 行(周期の最初)。**`[wx] have=...`**: `s` を受けた時だけ(`[stk] sym=` の 3 行の次)。
- **`[scr]`**: **`lcd_flip` / `lcd_flip_pal` が受け付けられた(`LCD_FLIP_OK`)たびに 1 行**。出る場面: ① 画面が変わった(株価 ↔ 時計・天気)② 時計・天気画面で分・日付・天気が変わって描き直した ③ 株価画面で表示値が変わって描き直した(取得中の表示・更新時刻・値)。**BUSY で受け付けられなかった入れ替えは出さない**(受け付けた時に 1 回)。
- **出さないもの**: 応答の本文・ヘッダー・SSID・パスワード・IP・MAC・座標(`latitude` `longitude`)。

### 14.2 `[ev]` と受信コマンド

| 項目 | 内容 |
|---|---|
| `[ev] btn=tap src=<touch\|uart>` | 今までどおり(§10 の旧ボタン名は無い)。**時計・天気画面でのタップは、続けて `[scr] to=stock ...` が出る**(株価画面へ戻る。`[stk] net=on busy=1` と取得が続く) |
| `x` | 更新(タップと同じ。**株価 3 銘柄 + 天気**の周期。時計・天気画面にいれば株価画面へ戻る)。`[ev] btn=tap src=uart` |
| `s` | `[st]` `[stk] net=` `[stk] sym=` x 3 の次に **`[wx] have=...`**(1 行) |
| `?` | `[help] s=state x=update ?=help`(変更なし) |

### 14.3 正規表現(Python の `re`、1行ずつ `fullmatch`)

```
R-WXW-OK    : \[wx\] res=ok src=(boot|timer|touch|uart|retry) temp10=(-?\d{1,3}) wmo=(\d{1,2}) kind=(sun|partly|cloud|fog|rain|snow|thunder|unknown) http=(\d{3}) ms=(\d+) sync=([01]) jst=(\d{6}|none) obs=(\d{4}) len=(\d+) closed=([01])
R-WXW-FAIL  : \[wx\] res=fail src=(boot|timer|touch|uart|retry) err=([1-9]) http=(\d{1,3}) ms=(\d+) len=(\d+)
R-WXW-STATE : \[wx\] have=([01]) temp10=(-?\d{1,3}) wmo=(\d{1,2}) kind=(sun|partly|cloud|fog|rain|snow|thunder|unknown|none) age=(-1|\d+)
R-SCR       : \[scr\] to=(stock|clock) pal=([0-8]) clk=(\d{4}|none) kind=(sun|partly|cloud|fog|rain|snow|thunder|unknown|none) temp=(-?\d{1,2}|none) tick=(\d+)
R-HB        : (§10.9 のまま。`scr=(\d+)` は 0 か 1)
```

**行の中の整合(正規表現のほかに判定するもの)**: `[scr]` で `to=stock` なら `pal=0`、`to=clock` なら `pal` が 1〜8 で **`pal` = 1 + (`kind` の番号。`kind=none` は 7 = unknown → `pal=8`)**。`kind=none` と `temp=none` は同時。`[wx] have=0` のとき `temp10=0 wmo=0 kind=none age=-1`。`[wx] res=ok` の `temp10` ∈ [−600, 600]、`wmo` ∈ [0, 99]、`kind` が WMO の表どおり。

### 14.4 ホストで試験する関数(`design.md` §13.10 の H-C5)

| 関数 | 内容 |
|---|---|
| `wx_fmt_wx_ok(buf, size, src, &result, closed)` / `wx_fmt_wx_fail(buf, size, src, &result)` / `wx_fmt_wx_state(buf, size, &slot, now_ms)` | §14.1 の 3 行(`\r\n` 込み)。**全 `src`・全 `kind`・境界値(`temp10` = −600 / 600、`ms` = 0xFFFFFFFF、`age` = −1)で R-WXW-* に fullmatch**。失敗(バッファ不足・引数不正)は 0 で `buf[0] = '\0'` |
| `applog_fmt_scr(buf, size, to_clock, pal, clk_min, kind, have, temp, tick)` | `[scr]` 1 行。**`to_clock` = 0 なら `pal` を 0 にそろえる**。`pal` > 8、`kind` > 7(`have` = 1 のとき)は 0 で `buf[0] = '\0'`。境界値(`tick` = 0xFFFFFFFF、`temp` = ±99、`clk_min` = 0xFFFF / 1440)で R-SCR に fullmatch |
| 秘密の混入の検査 | 応答に目印の語(`BODY_SENTINEL`)と本文の語を入れて流し、**全ログ行に `"current"` `latitude` `generationtime` `temperature_2m` `weather_code` `open-meteo` が出ない** |

### 14.5 試験項目(実機。UK1〜UK10。`design.md` §13.10 の U-C1〜U-C10 と対応)

**前提**: §13.5 と同じ(SW3 の 2ch = OFF、給電、`wifi_secret.h` あり、COM7 を他が開いていない、**PC の時計が NTP で合っている**)。`serial` ジョブ(`serial-read.ps1 -Stamp`)。**起動直後を見る回は 60 秒、切り替えの間隔を見る回は 330 秒、周期を見る回は 6,100 秒(10 周期)**。**`[boot]` が見えてから判定する**。

| ID | 対応 | 合格条件(UART) |
|---|---|---|
| UK1 | U-C1 | 起動: `[init]` ×4 ok → `[stk] net=conn` → **`[wx] res=ok src=boot` が `[stk] res=ok sym=0` より前**、`sync=1`、`kind` が名前、`ms` ≦ 5,000。**最初の `[scr] to=clock` が `[boot]` の約 10〜12 秒後**(`tick` の差。`[boot]` の時刻を 0 として)。その `[scr]` の `clk` `kind` `temp` が `none` でない |
| UK2 | U-C2 | PC の `curl` との照合: `obs` が同じなら `temp10` `wmo` が完全一致。違うとき気温差 ≦ 1.5℃・`kind` が同じか隣。`obs` が PC の JST より未来、または 30 分以上前なら不合格 |
| UK3 | U-C3 | 330 秒録音。`[scr]` の `to` が変わる行どうしの `tick` の差: **`to=clock` → `to=stock` = 500 ± 3、`to=stock` → `to=clock` = 1,000 ± 3**。20 回以上連続。`[hb]` の `uf` が増えない、`drop` が増えない、`err=0` |
| UK4 | U-C4 | 6,100 秒録音。`[stk] cycle=` が 600 ± 3 秒おき。**毎周期の最初に `[wx] res=ok`**、`cycle ms` ≦ 90,000。10 周期連続で天気と株価 3 つが `res=ok`、`err=9` なし |
| UK5 | U-C5 | `[scr]` の `clk` が変わる行(どちらの画面でも)の受信時刻(PC の JST)と、その分の 0 秒の差が ±3 秒以内(5 回)。`clk` が逆戻りしない |
| UK6 | U-C6 | 時計・天気画面にいる間に `tx=x`: `[ev] btn=tap src=uart` → `[scr] to=stock`(50ms 以内)→ `[stk] net=on busy=1` → **`[wx] res=ok src=uart` → `[stk] res=ok sym=0〜2`**。株価画面にいる間の `x`: `to` が変わらず、次の `to=clock` が `x` の `tick` から 1,000 ± 3。取得中の 2 回目は `[stk] req=busy src=uart`。実タッチ(ユーザー)も同じ(`src=touch`) |
| UK7 | U-C7 | `APP_STK_FAULT = 4`: `[wx] res=fail ... err=4 http=404`、株価 3 つは `res=ok`、約 20 秒後に `[wx] res=fail src=retry`(最大 2 回)、`[stk] cycle= ok=7 fail=0`。時計・天気画面の `[scr]` が `kind=none temp=none pal=8` |
| UK8 | U-C8 | `APP_STK_BADAP = 2`: 時刻が同期するまで `[scr] to=clock` が 1 行も出ない。復帰後 最初の取得の約 10 秒後に `to=clock`。`[hb]` が途切れない |
| UK9 | U-C9 | `-DCLK_WX_STALE_S=120` の試験ビルドで、天気を止めて 120 秒以上後に `[scr]` が `kind=none temp=none pal=8`(`s` の `[wx] have=1 ... age=` が 120 以上)。製品は 3,600。省略可(H-C1 で足りる) |
| UK10 | U-C10 | 受信ログに、U-9 の語・IP・MAC・`[wifiv]` に加え、`"current"` `latitude` `generationtime` `temperature_2m` `weather_code` `open-meteo` が **0 件**(一致数だけ報告)。SSID・パスワードは手元で(`design.md` §7.8) |

- **判定の順**: 書式(R-WXW-*・R-SCR)→ 行の中の整合(§14.3)→ 値の範囲 → 時刻の照合(UK5)→ PC との照合(UK2)→ 間隔(UK3)。
- **秘密の確認**: 受信ログに SSID・パスワード・本文の語が**無い**こと(一致数だけ報告)。失敗時はログ全文を添える前に、秘密の文字列が含まれていないか確認する。

## 15. 夜間のバックライト(rx72n_stockview `docs/design.md` §14。Phase 7a。2026-11-28 実装)での UART

```
[bl] mode=<on|dim|off> cause=<boot|night|day|touch|timeout|uart|state> pct=<0..100> clk=<hhmm|none> tick=<n>
```
R-BL: `\[bl\] mode=(on|dim|off) cause=(boot|night|day|touch|timeout|uart|state) pct=(\d{1,3}) clk=(\d{4}|none) tick=(\d+)`。整合: `mode=on` なら `pct=100`、`off` なら `0`(7a は `dim` を出さない)。
- 出す時: `mode` が変わるたびに 1 行。起動直後(`[init] stage=loop` と `[perf]` の後)に `mode=on cause=boot`。`s` を受けたとき `cause=state` を 1 行。
- 試験用コマンド `b`(`[help]` には載せない): 自動 → 消灯 → 点灯 → 自動。1 回ごとに 2 秒以上あける。
- 消灯中の最初のタッチは `[ev] btn=tap` を出さない(起こすだけ)。UART `x` は消灯中でも効き、起こさない。
- 出さないもの: SSID・パスワード・IP・応答の本文。
