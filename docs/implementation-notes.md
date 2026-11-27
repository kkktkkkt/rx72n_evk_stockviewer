# 実装メモ(コーディング担当)

最終更新: 2026-11-27。状態: **Phase 0(TLS の実機検証用の最小の変更)・Phase 1(表示)・Phase 2(数値と解析)・Phase 3(取得と予定表)・Phase 4(結合)を実装済み。Phase 5(実機の試験)は未着手。** 実機ではビルドも実行もしていない(ホスト試験と、RX 用 GCC での単体のコンパイルだけ)。

## Phase 4(結合): 実装したこと

| ファイル | 内容 |
|---|---|
| `src/app/app_main.c` | `stk_ui()` の値を `ui_set_view` → `ui_render` へ(10ms ごと。変わった時だけ裏面に描いて入れ替え)。`view_from_wx`・`ui_wx_t`・`wx_ui` の暫定の経路を削除。`APP_CLUT8_TEST` の分岐を削除。タップ = `[ev] btn=tap src=<touch\|uart>`(`applog_fmt_ev_tap`)+ `wx_request(src)`。`APP_STK_PREVIEW` は今までどおり(取得より優先)。`APP_WX = 0` は `ui_init` の値(起動中・オフライン)のまま |
| `src/app/wifi_sw.h` | **`APP_WX` の既定を 1**(ただし `APP_STK_PREVIEW = 1` のときの既定は 0 = ネットワークなし。明示すれば上書きできる)。`APP_STK_FAULT`(0〜3。範囲外は `#error`)を追加。`clut8_sw.h` の include と `APP_CLUT8_TEST` の #error を削除。`wifi_secret.h` が無い・SCI1 を開けないときの OFF は `wifi_at.c` のまま(変更なし) |
| `src/app/wx_core.c` | `APP_STK_FAULT = n`(1〜3): 銘柄 n−1 の要求だけ、銘柄コードの後ろに `ZZ`(存在しない銘柄 → Yahoo の 404 を期待。**実機・Yahoo では未確認**)。0 のとき要求は今までと同じ(144 / 145 / 145)。`wx_fmt_ok` の `snprintf` を 2 回に分けた(下) |
| `src/app/wifi_at.h/.c` | `wx_ui` を削除(`ui_logic.h` の include も不要に) |
| `src/app/ui_logic.h` | `ui_wx_t`・`UI_NET_*` を削除(`ui_logic.c` は 104 行。§6.2 のとおり、タップ検出と表示キーだけ) |
| `src/app/app_logfmt.h/.c` | `applog_fmt_ev_tap`(`[ev] btn=tap src=<src>`)を追加。旧 UI の `applog_fmt_ev` `_ev_scr` `_ev_btn` `_ev_set` `_ev_done` `_ev_demo`・`applog_fmt_ui`・`applog_ui_t` を削除。`APPLOG_CMD_PLUS` `_MINUS`(`+` `-`)を削除(無視する文字になる)。**`[help]` を `s=state x=update ?=help` に変更**(製品では `n p d + -` が効かないため) |
| `src/app/nav_logic.h` | `nav_sample_t` と `NAV_SAMPLE_MS` だけ。`nav_logic.c` を削除 |
| `src/app/lcd_panel.c`、`board_pins.h` | `APP_CLUT8_TEST` の分岐・`clut8_sw.h` の include を削除 |
| 削除 | `src/app/clut8_pat.c/.h`・`clut8_test.c/.h`・`clut8_sw.h`・`nav_logic.c`、`tools/gen_ui_assets.py`・`fb2png.py`・`wx_compare_p2b.py`・`wx_mock.py`・`clut8_ref.c`、`tools/assets/bath/`(旧 UI の生成物・プレビュー)、`tests/host/test_nav.c`・`test_nav_step.c`・`test_clut8_cfg.c`・`test_clut8_pat.c`・`judge_ut1.py` |
| 試験 | `test_ui_tap.c`(H-12。S2)、`test_app_main.c`(結合。S34。5 通りのビルド)、`test_stk_fault.c`(S35)、S36(変異試験 4 件)、S37(旧 UI の残骸なし)。`test_logfmt.c`・`test_touch_decode.c`・`test_wifi_at.c`・`run.sh` を更新(S18 = CLUT8 を削除、S7 に H-13 のスイッチの組み合わせを追加) |

### スタック警告(`wx_fmt_ok`)

`wx_core.c` の `wx_fmt_ok`(`-Wstack-usage=100` に対して 116 バイト)は、15 個の引数を 1 回の `snprintf` に渡していたため(RX は 5 個目以降の引数をスタックに積む)。**2 回の `snprintf` に分けて 88 バイトにした。** RX 用 GCC 14.2(`HardwareDebug` と同じ `-Og -misa=v3 -mlittle-endian-data -std=gnu99 -Wstack-usage=100`)で `src/app/*.c` の全部を、既定 / `APP_WX=0` / `APP_STK_PREVIEW=1` / `APP_WIFI_TEST=1` / `APP_STK_FAULT=2` の 5 通りで 1 ファイルずつ `-c` でコンパイルして確かめた(警告 0、最大は `wx_fmt_ok` と `gfx_a8` の 88)。ヘッドレスビルド(リンク・ROM / RAM のサイズ)は行っていない。

### 設計から外れた点・判断が要る点

1. **`APP_STK_PREVIEW = 1` のときの `APP_WX` の既定は 0**(設計 §7.4 は `APP_WX` の既定 1、プレビューは「ネットワークなし」。既定 1 のままだとプレビューのビルドが Wi-Fi に接続してしまうため)。`-DAPP_WX=1` で明示すれば両方動く(S7 で確認)。
2. **`[help]` の文面を変えた**(設計 §7.3 は `x` `s` `?` だけとしているが、`[help]` の文面は規定なし)。
3. **`n` `p` `d` は `applog_cmd_parse` に残した**(専用モード `APP_WIFI_TEST` の `wifi_at.c` が使うため)。製品の `app_main.c` は `x` `s` `?` だけ処理し、ほかは無視する(試験 S34)。`+` `-` だけを削除した。
4. **`APP_STK_FAULT` の壊し方**: 銘柄コードの後ろに `ZZ`(`%5EN225ZZ` など)。Yahoo が 404 を返すかは未確認(U-6 の最初に見る)。返さなければ、`wx_build_request_t` の 1 行を直す。
5. **`tools/img2c.py`・`tools/prepare_slide.py` とその試験(S5)は残した**(旧ギャラリー用だが、設計 §7.1 の削除の一覧に無く、旧 UI 専用とは言い切れない。不要なら削除)。`tools/c8png.py` は `png` `cmp` `cmpface` `sheet` で使うので残した。`tools/gdb/` の旧 UI 用スクリプト(`mi-demo-fb*` `mi-press-fb*` `mi-slide-fb-c8` ほか)は、デバッグ担当の持ち物として触っていない(`fb2png.py` を参照するコメントが残る)。`tests/host/last-run.log` は旧 UI 時代の古い実行ログ(`run.sh` は書かない)。
6. 製品で押下中の 2 点目(押下確定後に 2 点目が触れた場合)は、`ui_update` が無視せず、そのままタップとして扱う(設計 §6.1 は押下の開始時の 2 点以上だけを無視と読んだ)。変更していない。

### Phase 5 に残したこと

- **`HardwareDebug` のビルド生成物(`subdir.mk` など)が、削除したファイル(`clut8_*.c` `nav_logic.c`)を参照したまま**。ビルドの前に e² studio / ヘッドレスのプロジェクト更新(refresh)で makefile を作り直す(ビルド担当)。`stk_core.c` `wx_core.c` などの追加分も同じ。
- RX 用 GCC のリンク・ROM / RAM(P-1〜P-4)。実機の U-1〜U-10・G-1・G-2・V-1〜V-4。`APP_STK_FAULT = 2` の U-6(404 になること)。`[help]` の文面変更に合わせた判定スクリプト(あれば)。


## Phase 0: 変更したファイル(`docs/design.md` §5.5.2 の 0-1〜0-6)

| ファイル | 変更 |
|---|---|
| `src/app/wx_core.h` / `.c` | `WX_BUF_SIZE` 1536 → 3072(0-3)。`wx_target_t`(`METEO` 既定 / `YAHOO_TCP` / `YAHOO_SSL` / `GOOGLE_SSL` / `YAHOO_SSL_NOAUTH`)、`wx_build_cipstart`、`wx_target_ssl` を追加。`wx_build_request(buf, size, target, sym)` に変更(0-2)。`wx_result_t` に `len`。`wx_fmt_fail` に `len=<n>` を追加し、`err` を 1〜9 に(0-5)。`WX_RANGE_STR` `WX_UA` を追加 |
| `src/app/wifi_core.h` / `.c` | `AT+CIPSTART` を実行時に組み立て(0-1)。`wifi_core_set_target`。SSL の `wx_tcp` は 20,000ms、`wx_recv` は 15,000ms。取得中の `ready` で `err=9`(0-4)。応答が溢れても `http` と `Date:` は拾う(`err=7` のまま)。新しい step `wx_sslconf`(`z` のときだけ。`AT+CIPSSLCCONF=0,0,0`、`OK` でも `ERROR` でも先へ) |
| `src/app/wifi_at.c` | 専用モードの UART コマンド `t` `y` `g` `z`(0-6)。`x` は最後に選んだ対象でもう一度(何も選んでいなければ今までどおり天気) |
| `tests/host/test_wifi_core.c` `test_wx_core.c` `test_wifi_at.c` `test_wx_indep.c` | 新しい関数の試験を追加(P0-1〜P0-7、P0-A〜P0-C、F6)。呼び出しの更新(`wx_build_request`)、バッファ 3072・`WIFI_STEP_END` = 21・`wx_recv` 15 秒への追従 |

0-7(`tools/job-runner.ps1` の `tx` 許可リストに `t` `y` `g` `z`)は **デバッグ担当の作業**。まだ。

## 既定のビルドへの影響

(Phase 0 当時の記述。**Phase 4 で `APP_WX` の既定は 1 になった**。「消える」のは `APP_WX = 0` かつ `APP_WIFI_TEST = 0` のとき。)

`APP_WIFI_TEST = 0` かつ `APP_WX = 0`(既定)では、`wifi_core.c` `wx_core.c` `wifi_at.c` は中身が消える。**ROM・RAM・動作の差は 0**(ホスト試験 S22・S23 が「シンボルが残らない」を確認)。`wifi_sw.h`・`app_main.c` は変えていない。

`APP_WX = 1`(天気の製品版。既定は 0)では、`.bss` が +1,536 バイト(バッファ)、天気の `[wx] res=fail` に `len=` が付く。

## Phase 0 用のビルドに切り替える方法

ヘッドレスビルドに `-D` を渡す口が無いので、`src/app/wifi_sw.h` の既定値を書き換える(複製元の運用と同じ。**ビルドの後に必ず 0 へ戻す。コミットする値は 0**)。

| マクロ | 既定 | Phase 0 |
|---|---|---|
| `APP_WIFI_TEST` | `(0)` | `(1)`(LCD・タッチは動かさない。Wi-Fi の専用ループ) |
| `APP_WIFI_AUTOSTART` | `(0)` | `(1)`(起動 2 秒後に `d` を 1 回) |

`APP_WX` は 0 のまま。`wifi_secret.h` が要る(開かない)。

## 専用モードの UART コマンド(Phase 0)

| 文字 | 対象 | 備考 |
|---|---|---|
| `d` `n` `p` | 接続の試験 | 今までどおり |
| `t` | Yahoo 平文(TCP 80)。要求は ^N225 | 対照 1。期待 `http=301` |
| `y` | Yahoo SSL(443)。同じ要求 | 本番。期待 `http=200` かつ `len` 1,700〜2,100 |
| `g` | Google SSL(443)、`GET /` | 対照 2。`len` は 3,072 で頭打ち(`err=7` でもよい) |
| `z` | `y` の前に `AT+CIPSSLCCONF=0,0,0` | `y` が失敗したときだけ。**設定は ESP32 の NVS に残る**(認証なしだけ。秘密ではない) |
| `x` | 最後に選んだ対象でもう一度 | 何も選んでいなければ天気 |
| `s` `?` | 状態・ヘルプ | 今までどおり。ヘルプの文面は変えていない |

実行中(`d` の最中や取得中)の `t` `y` `g` `z` `x` は無視。

## ログ

`[wx] res=fail src=<src> err=<1..9> http=<n> ms=<n> len=<n>`。`err=9` = 取得中に `ready`(ESP32 が再起動)。`len` = バッファに溜めたバイト数(最大 3,072。本文は出さない)。`[wifi] step=wx_sslconf ...` が `z` のときだけ出る。

## Phase 3(取得と予定表): 実装したこと

| ファイル | 内容 |
|---|---|
| `src/app/wx_core.h/.c` | 天気を全部削除(`wx_parse_weather`・`wx_kind_*`・`WX_KIND_*`・`wx_view_t`・`WX_REQ_LEN`・`wx_result_t` の `wmo` `kind` `temp_x10` `obs`)。`wx_build_request(buf, size, sym)`(製品。Yahoo の SSL。144 / 145 / 145)と `wx_build_request_t(buf, size, target, sym)`。`wx_target_t` は `YAHOO_SSL`(= 0。製品)と、`APP_WIFI_TEST` の `YAHOO_TCP` `GOOGLE_SSL` `YAHOO_SSL_NOAUTH`。`WX_SRC_RETRY` を追加。ログ行を `[stk]` に(`wx_fmt_ok` / `fail` / `cycle` / `req` / `net` / `sym`。§7.5)。予定表 `wx_sched_*` を 3 銘柄に(下) |
| `src/app/wifi_core.h/.c` | `wifi_core_set_symbol`(銘柄ごとに `WIFI_RANGE_WX` を 1 回)。結果に `sym`。`wx_parse_http` に切り替え(溢れた応答は `err=7`)。`quiet` は `res=start` に加えて **`res=ok` も出さない**(失敗・時間切れ・skip・`result=` は出す)。`z` の step は製品の範囲に入れない(`MASK_WX_SSLCONF` は `APP_WIFI_TEST` のみ。step 番号は変えない) |
| `src/app/wifi_at.c/.h` | 銘柄ごとの最後の成功(`stk_slot_t s_slot[3]`)と更新時刻を保持。`wx_report`(成功時に `stk_calc`・`stk_market` で記録)、`[stk] res=` / `cycle=` / `net=`(`net` / `busy` が変わった時)/ `sym=` ×3(`s` の時)。**`stk_ui(stk_view_t *, now_ms)`**(= `stk_view_make`)を追加。専用モードは `t y g z x`(銘柄は ^N225)のまま `[stk]` を出す |
| `src/app/stk_core.h/.c` | `stk_slot_t`、`stk_view_make`(設計 §7.1 の組み立て)、`STK_STALE_S` = 1,800 |
| `tests/host/` | `test_wx_core.c`(要求・書式 R-STK-*・予定表 H-6 を作り直し)、`test_wifi_core.c`(応答を Yahoo ふうに。銘柄・err=9・タイムアウト・CLOSED の途切れ・quiet。専用モードと製品の 2 通りで実行)、`test_wifi_at.c`(偽の ESP32 が銘柄ごとの応答。製品の 3 銘柄の周期・失敗分のやり直し・全部失敗の再接続)、`test_wx_indep.c`(予定表の乱数走行を 3 銘柄用のモデルに)、`test_stk_core.c`(`stk_view_make`)、`tests/stubs/fake_sci.h`(受信バッファを読み切ったら先頭に戻す。長い走行用)、`run.sh`(S22・S24 を 2 通りに、S33 = 変異試験を追加) |

### 予定表の決め(設計 §5.7 を実装した形)

- 1 周期 = 銘柄 0 → 1 → 2 を `wx_sched_poll` が 1 つずつ `WX_ACT_FETCH`(`wx_sched_sym` で銘柄)で返し、`wx_sched_done` を銘柄ごとに受ける。周期の開始 = 最初の銘柄の `poll`。定期 = 開始から 600,000ms、更新要求の最小間隔 5,000ms も開始から。
- 失敗した銘柄だけを 20 秒後(`WX_RETRY_MS`)に `src=retry` で、周期あたり最大 2 回(`WX_RETRY_MAX`)。やり直しの間も `net` は `on`。周期の記録(`wx_sched_cycle`: `n` `ok` `fail` `ms`)は、やり直しまで含めた周期の終わりに 1 回(`[stk] cycle=`)。
- 429: 周期を打ち切り(取っていない銘柄は失敗扱い)、300 秒後に周期を最初から(`src=timer`)。待ちの間の更新要求は `req=ignored`。
- 全部失敗: やり直しの後も 3 銘柄とも失敗**で、すべて応答なし(`http=0`)** なら、`net=err` にして接続からやり直す(接続の失敗は 60 → 120 → 300 秒。待ち中の更新要求は直ちに再接続)。応答がある失敗(403・404 など)が混じる時は再接続せず、次の定期の周期まで待つ(10 分の間隔を守るため)。
- `net`: 接続の試行中は、失敗の後(`err`)ならそのまま `err`(オフライン表示を `conn` にちらつかせない)。起動直後だけ `conn`。

### 設計から外れた点・判断が要る点(設計役の確認が要る)

1. **全部失敗の再接続の条件**: 設計 §5.7 は「3 銘柄すべてが失敗(`err` 1〜3 または 9)」。`wx_sched_done` が受けるのは `ok` と `http` だけなので、「すべて `http=0`」で近似した(上のとおり。`err=3` でも状態行が読めていれば `http≠0` で、再接続しない)。
2. **`quiet` が `res=ok` も止める**: 設計 §5.9 のとおりに、接続の step も含めて全部。設計 §8.3 U-1 の「`[wifi]` の `join` ok」は見えなくなる(`[wifi] result=pass` と `[stk] net=` で代える)。
3. **接続の失敗の `[stk] res=fail`**: `err=8` の行の `sym` は 0(銘柄がない。R-STK-FAIL が `sym=[0-2]` のため)。
4. **更新要求の返値**: 失敗分のやり直しの待ち = `req=busy`、429 の待ち = `req=ignored`(設計に規定なし)。
5. **`wx_ui` と `ui_wx_t` を残した**(`net` `busy` `clock_min` `upd_min` だけ。`app_main.c` が使うため。Phase 4 で `app_main` が `stk_ui` に切り替えたら、`ui_logic.h` の `ui_wx_t` と一緒に消す)。`app_main.c` / `ui_logic.*` / `wifi_sw.h` は変えていない。
6. `stk_view_make` を `stk_core` に置いた(設計 §7.1)。`row.stale` / `stale_all` は「1,800 秒以上」(以前の `>` ではなく設計の「以上」)。

### Phase 4 に残したこと(→ 上の「Phase 4(結合)」で実装済み)

`app_main.c` の `stk_ui` への切り替え、`[ev] btn=tap src=...`、`APP_STK_FAULT`、`APP_WX = 1` の既定、`ui_logic` の縮小、旧 UI のファイルの削除。`docs/design-uart.md` §13(`[wx]`)は旧のまま(設計 §7.5 が優先)。

### 確かめていないこと

RX 用 GCC でのビルド(`-Wstack-usage=100` を含む)。S12(ASan / UBSan)は libasan が無く SKIP。実機での実行(ESP32 の再起動後の再接続・SSL の分割受信の粒度など)。

## Phase 2(数値と解析): 実装したこと

| ファイル | 内容 |
|---|---|
| `src/app/stk_core.h/.c`(新規。標準 C。float なし) | `stk_calc`(×100 の整数 → 前日比・騰落率(0.01% 単位。四捨五入は絶対値の 0.5 を遠い方へ。int64 の割り算 1 か所)・`dir`)、`stk_fmt_price` / `stk_fmt_pct` / `stk_fmt_amt`(カンマ区切り・小数 2 桁・▲ ▼ − の記号 `\x01` `\x02` `\x03`)、`stk_row_kind`、`stk_market`(取引中 / 終値 / 不明。§5.4)。型と定数(`stk_view_t` など)は Phase 1 のまま |
| `src/app/ui_render.c` | 暫定の `fmt_*` を削除し、`stk_fmt_*` を呼ぶ。**描画は 1 画素も変わらない**(`test_ui_stock` の 24 状態の CRC と、C / Python の 0 バイト差が合格) |
| `src/app/wx_core.h/.c` | `wx_parse_http` を Yahoo の chart API(`range=2d`)用に書き換え(§5.2。価格・前日の終値・`mtime`・`rs`・`re`・currency・HTTP の状態・`Date:` を整数で。本文は結果に残さない)。`wx_result_t` に `sym`・`has_bar_prev`・`cur`(`wx_cur_t`)・`price_x100`・`prev_x100`・`mtime`・`rs`・`re` を追加。旧 `wx_parse_http` は **`wx_parse_weather`** に改名して残した(下の「設計から外れた点」の 1) |
| `src/app/wifi_core.c` | 呼び出し 2 行だけ `wx_parse_weather` に変更(動作は今までどおり。Phase 3 で `wx_parse_http` へ切り替える) |
| `tests/host/test_stk_core.c`、`test_wx_stock.c`(新規)、`run.sh`(S29・S30、既存の link 行に `stk_core.c`) | H-1・H-2・H-5(S29)、H-3・H-4(S30) |
| `tests/host/data/yahoo_{6723T,N225,GSPC}_{2d,1d}.http`(新規。6 つ) | 2026-11-27 13:03〜13:05 JST に PC から `curl --http1.0`(`User-Agent: rx72n-stockview/1`)で取った実応答。状態行・`date`・`content-type`・本文だけ(それ以外のヘッダーは捨てた)。S30 がヘッダーの内容を検査する |
| `test_wx_core.c`、`test_wx_indep.c` | 天気の解析の試験は `wx_parse_weather` の呼び出しに名前だけ変更 |

### 公開関数(stk_core.h)

| 関数 | 内容 |
|---|---|
| `bool stk_calc(price_x100, prev_x100, stk_calc_t *out)` | 価格・前日の終値は 1〜99,999,999。`|pct_bp| ≦ 99,999`。外れたら false で `out` は 0・`STK_FLAT` |
| `size_t stk_fmt_price(buf, size, price_x100, uint8_t kind)` | 円は整数円(0.5 円は切り上げ)、指数は小数 2 桁 |
| `size_t stk_fmt_pct(buf, size, pct_bp, uint8_t dir)` | `+2.31%`、`\x03` + `0.25%`、変化なしは `0.00%`。符号は `dir` で決める |
| `size_t stk_fmt_amt(buf, size, diff_x100, uint8_t kind)` | `\x01` / `\x02` + 数字。0 は矢印なしの `0` / `0.00` |
| `uint8_t stk_row_kind(uint8_t row)` | 行 0 = `STK_KIND_YEN`、それ以外 = `STK_KIND_INDEX` |
| `uint8_t stk_market(const stk_sym_t *sym, bool synced, now_jst, mtime, rs, re)` | 未同期 = `STK_MKT_NONE`。32bit の差で比べる(下の「Phase 2 の修正」の規則) |

整形の失敗(範囲外・size 不足・kind / dir が不正)は 0 を返して `buf[0] = '\0'`。

### 設計から外れた点・設計に無い判断(設計役の確認が要る)

1. **旧天気の解析を `wx_parse_weather` として残した**。設計 §5.9 は `wx_result_t` の天気の項目と天気の解析を削除するが、`wifi_core.c` の `wx_publish`・`wifi_at.c`・`wx_fmt_ok`・`wx_fmt_state` と、それらの試験(`test_wifi_core.c`・`test_wifi_at.c`)がまだ天気の結果を使うため、Phase 3 の前に消すと既存の試験と `APP_WX = 1`(天気の製品版)が壊れる。**Phase 3 で `wifi_core` を `wx_parse_http` に切り替えるときに、`wx_parse_weather`・`wmo` `kind` `temp_x10` `obs`・`WX_KIND_*`・`wx_fmt_ok` の天気項目を削除する**。
2. **`stk_market` の引数に `synced` を足した**(設計 §5.4・H-5 の「時計が未同期 → NONE」を表す手段が無かったため)。now_jst はソフト時計の JST の通算秒。
3. **`stk_fmt_pct` / `stk_fmt_amt` の `dir`・`kind` は `uint8_t`**(設計 §4.3 は `int8_t`。`-Wconversion` で警告になるため)。`stk_fmt_amt` は `dir` を取らず、`diff_x100` の符号から ▲ ▼ を決める(設計どおり)。
4. **`STK_FRESH_S`(7,200 に改訂)を `stk_core.h` に置いた**(設計 §5.9 は `WX_FRESH_S` を `wx_core.h` に追加するとしているが、`stk_core` が `wx_core` に依存しないようにするため)。`WX_FRESH_S` は作っていない。
5. **`wx_parse_http` が `stk_core.c` の `stk_calc` を呼ぶ**(段 7 の範囲検査を 1 か所にするため)。`wx_core.c` を link する試験は `stk_core.c` も link する(`run.sh` を直した)。RX 側は e² studio が `src/app/*.c` を拾うので、**新しい `stk_core.c` を `HardwareDebug` の makefile へ反映するため、プロジェクトの更新(refresh)が要る**(ビルド担当)。
6. **`currency` は必須にしない**(設計 §5.2 の段 4 のキー一覧に無いため)。無い・3 文字でない = `WX_CUR_NONE`、成功のまま。`cur` は今は表示に使わない(Phase 3 以降で銘柄の種類との照合に使える)。
7. **close 配列は最大 5 要素ではなく、`]` までの全要素を読んで「最後から 2 番目」を取る**(設計は「最大 5 要素まで」)。`]` が無い(途切れ)・読めない要素・末尾のカンマは「直前の足なし」(= `chartPreviousClose`)。バッファ 3,072 の範囲に収まるので、作業量は変わらない。
8. **同じキーが 2 回あれば最初の出現を使う**(設計に規定なし。`meta` は本文の先頭にある)。`regularMarketPrice`・`chartPreviousClose`・`regularMarketTime`・`regular` の `start` `end` は**どれか 1 つ欠けても `err = 5`**(直前の足があっても `chartPreviousClose` が無ければ失敗。設計 §5.2 段 4 のとおり)。
9. **直前の足が範囲外(0 など)のときは `err = 6`**(`chartPreviousClose` へ倒さない。設計 §5.2 段 5〜7 のとおり)。
10. §5.2 段 8(バッファ超過 = 常に err 7)は Phase 3 の wifi_core の仕事として手を付けていない(wifi_core は今も旧の天気の解析を呼び、溢れたら err 7 にする)。wx_parse_http 単独では、全長より短い応答はどの長さでも成功にならない(下の「Phase 2 の修正」)。

### 実応答から分かったこと(設計 §2.1 の F10 との差)

- 2026-11-27 13:03 JST の取得では、**`^N225` の `range=2d` も足が 2 本**(設計 F10 では 1 本)。ただし `1d` では 1 本なので、`has_bar_prev = 0` の経路は 1d の 3 応答で試験している。
- **`^GSPC` の `range=2d`**: `chartPreviousClose` = 7,237.17 だが、直前の足 = 7,201.09(`fulldayChange` −18.31 と一致)。設計 §5.3 の (b) を実応答で確認した(前日比 −25 = −0.25%、Yahoo の −0.254% と一致)。`1d` の `chartPreviousClose` は 7,201.09。
- 日本株・日経平均の `regularMarketTime` は `Date:` より 約 15 分(901〜902 秒)前 → `stk_market` は「取引中」(取引時間帯の中で、遅れ ≦ 7,200)。

### 確かめていないこと

- RX 用 GCC でのビルド(`-Wstack-usage=100` を含む)。ホストの `-fstack-usage` は `wx_parse_http` が x86 で 176 バイト(旧 `wx_parse_http` = 今の `wx_parse_weather` は 144)。RX の値は別なので、ビルド担当の結果を見る。
- S12(ASan / UBSan)はこの環境では libasan が無く SKIP。代わりに `test_wx_stock` が禁止ページ(あらゆる長さに切る + 変異ファズ 60,000 回)で範囲外の読み出しを検出する。S12 の対象には `test_stk_core`・`test_wx_stock` も足してある(libasan のある環境で実行される)。
- 実機での実行(Phase 5)。

## Phase 2 の修正(テスト担当 S32 の指摘 → 設計の改訂 → 実装を合わせた)

| 変更 | 内容 |
|---|---|
| `wx_parse_http`(§5.2 段 3・3b) | 空行(CRLF CRLF)が無ければ `err = 3`。本文の最初の空白でない文字が `{` でなければ `err = 5`。`stk_json_complete`(入れ子の深さが 0 に戻り、その後が空白だけ)が偽なら `err = 3`。**どの長さで切れた応答も成功にならない**(全長だけが成功)。`close` が 1 本で閉じている応答は成功(`has_bar_prev = 0`)。LF だけの応答は空行なしで `err = 3`(以前は成功にしていた)。`stk_json_complete` は `wx_core.h` に公開(ホスト試験のため) |
| `stk_calc` / `stk_fmt_pct` / `stk_fmt_amt`(§4.2・§4.3) | `dir` は丸めた `pct_bp` の符号(0 → `STK_FLAT`)。`stk_fmt_pct(buf, size, pct_bp)`(`dir` 引数を廃止。0 は `0.00%`)、`stk_fmt_amt(buf, size, diff, kind, dir)`(矢印は `dir` が上昇・下落で丸めた金額 ≠ 0 のときだけ。変化なしでも金額はそのまま、例: `0.50`)。`ui_row_dir` も `pct_bp` の符号 |
| `stk_market`(§5.4) | `stk_market(const stk_sym_t *sym, bool synced, now_jst, mtime, rs, re)`。取引時間帯の中で、昼休み(`stk_sym_t.lunch_s / lunch_e`。6723.T と ^N225 = 11:30〜12:30、^GSPC = なし)でなく、`data_ok`(`mtime ≧ rs` かつ遅れ ≦ 7,200 秒)または `open_grace`(`mtime < rs` かつ `now − rs ≦ 1,800`)なら取引中。`stk_sym(row)` の銘柄表を追加。定数 `STK_FRESH_S` = 7200、`STK_OPEN_GRACE_S` = 1800(`wx_core.h` に `WX_FRESH_S` `WX_OPEN_GRACE_S` として別名) |
| `ui_render.c` | `fmt_hhmm`: 1440〜65534 も `--:--`(`clock_min` / `upd_min`) |
| `tools/gen_stock_assets.py` | `row_dir` = `bp` の符号、`fmt_pct(bp)`、`fmt_amt(diff, dir, yen)`、`hhmm` を C と同じ規則に(S0〜S7 のプレビュー PNG は変わらない。S28 合格)。`diff ≠ 0`・`bp = 0` と `bp ≠ 0` で円の金額 0 の状態を作って C(`fb_hostrender`)と Python の出力を比べ、0 バイト差を確認した(一時の状態ファイル。リポジトリには入れていない) |
| 試験 | `test_stk_core.c`・`test_wx_stock.c`・`test_ui_stock.c`・`test_stk_indep.c` を新しい規則に更新。`test_ui_stock` の 24 状態の CRC は **状態 17(円の diff +1、bp 0)だけが変わった**(赤 `+0.00%` `▲` → 灰 `0.00%`・金額 `0`。意図どおり)。`test_stk_indep` の SUSPECT(S32)は 0 になり、試験を通常の S31 へ格上げ(S32 は廃止) |

設計に無い判断: `stk_market` の `synced` 引数(設計の署名 `stk_market(sym, now_jst, mtime, rs, re)` には未同期の表し方が無い。H-5 の「時計未同期 → NONE」のため、`sym` の次に足した)。

## 設計から外れた点・設計役への差し戻し(Phase 0)

1. **`wx_build_request` の引数**: 設計 0-2 は `(buf, size, sym)`。Phase 0 は接続先が要るので `(buf, size, target, sym)`。Phase 3 で `target` を外す。
2. **step の追加**: `z` のために `WIFI_STEP_WX_SSLCONF` を `wx_close` と `wx_tcp` の間(17)に入れた。`wx_tcp` / `wx_send` / `wx_recv` の `[hb] scr` が 17/18/19 → 18/19/20 に、`WIFI_STEP_END` が 20 → 21 になる。**`docs/design-uart.md` の `scr` の範囲(0〜19)の更新が要る**。
3. **`ready` の扱い**: 設計 0-4 は「受信中」。**`wx_tcp`(ハンドシェイク中の再起動)・`wx_send` でも `err=9`** にした(SSL のメモリ不足は接続中に起きやすいため)。
4. **溢れた応答**: 設計 §5.2 の段 8(溢れてもキーがそろえば成功)は Phase 3。Phase 0 は `g` の判定に `http` が要るので、溢れたときも `wx_parse_http` を呼んで `http` と `Date:` を拾う(結果は `err=7` のまま)。
5. **設計 §5.5.4 の T-0**: `t` の期待を「`err=5 http=301`」としているが、`wx_parse_http` は 200 以外で `err=4`(設計 §5.2 も `err=4`)。**判定は `http=301`(`err=4`)で行う**ものとして、設計書の直しを依頼。
6. **`docs/design-uart.md` §13 の `R-WX-FAIL`**(`err=([1-8])`、末尾 `ms=(\d+)`)は、`err` 1〜9 と末尾の `len=(\d+)` に直す必要がある(判定スクリプト用)。
7. `AT+CIPSSLCCONF=0,0,0` の書式は AT 1.1.3.0 で**未確認**(設計どおりに送る。`ERROR` でも先へ進む)。

## Phase 1(表示): 実装したこと

| ファイル | 内容 |
|---|---|
| `tools/gen_stock_assets.py` | 帯・グリフ・shape・ramp・パレット・Wi-Fi アイコンの生成、`--check`(H-9)、プレビュー PNG、`--emit-c8`、`--emit-c`(C の生成物) |
| `src/app/ui_assets.h` `ui_layout.c` `ui_gfx_data.c` `ui_strips.c` `ui_preview.c` | **生成物。手で編集しない**(`python tools/gen_stock_assets.py --emit-c`)。旧 UI の `ui_bg_*.c` 7 本は削除 |
| `src/app/stk_core.h` | `stk_view_t` の型と定数だけ(計算は Phase 2) |
| `src/app/ui_render.c/.h` | 設計 §3.13 の描画。**数値の整形(`fmt_*`)は暫定で `ui_render.c` の中**(Phase 2 で `stk_core` の `stk_fmt_*` へ) |
| `src/app/ui_logic.c/.h` | タップ検出と表示キーだけに縮小(§6.2)。旧 `ui_wx_t` は Phase 3 まで暫定で残す(`wifi_at.c` の `wx_ui` が使う) |
| `src/app/app_main.c` | 暫定の最小変更。Wi-Fi・時計だけ `stk_view_t` に写す。タップ = `wx_request` |
| `tools/fb_hostrender.c`、`tools/c8png.py`(`sheet`) | ホスト描画(§7.7) |
| `tests/host/test_ui_stock.c`、`run.sh`(S14・S28) | H-8、H-9、H-12 の一部 |

## プレビュー構成(8 状態を 3 秒ごとに LCD へ出す)でビルドする

ヘッドレスビルドに `-D` を渡す口が無いので、**手書きの `src/app/wifi_sw.h` の 1 行を書き換える**(コミットする値は `(0)`)。

```c
#define APP_STK_PREVIEW      (1)     /* wifi_sw.h。既定は (0) */
```

`ui_assets.h`(生成物)は先頭で `wifi_sw.h` を include するので、二重定義にならず、必ず先に定義される。ホストでは `-DAPP_STK_PREVIEW=1` でも切り替えられる(`#ifndef`)。
`APP_STK_PREVIEW = 1` のとき `ui_preview.c` の 8 状態(S0〜S7。元は `tests/host/data/states/`)が `app_main` で 3 秒ごとに切り替わる。ネットワークは使わない(`APP_WX = 0` のままでよい)。
`APP_STK_PREVIEW = 0` では `ui_preview.c` の中身は消える(型の宣言 1 行だけ)。

## 残作業(Phase 1 で意図して残したもの)

Phase 4 で全部実施(旧 UI のファイルの削除、`ui_wx_t` と `wx_ui` の廃止、`[ev] btn=tap src=...`、`app_main` の結合)。`[perf]` の `slide` は 0 のまま(書式は変えない)。

## 未実装(Phase 1 以降)

Phase 5(実機の試験)だけ。

## Phase 5 の所見による修正(2026-11-27)

| ファイル | 内容 |
|---|---|
| `src/app/wifi_core.c` | `emit_val` が `quiet` のとき何も出さない。製品(`wifi_core_set_quiet(true)`)は `[wifiv]` を 1 行も出さない(`key=ip` の IP アドレスなど。設計 §7.5・§7.8)。専用モード(`APP_WIFI_TEST`。quiet にしない)は従来どおり出す |
| `src/app/wx_core.h/.c` | `WX_CLOCK_BACK_MAX_S`(30)を追加。`wx_clock_sync` は、同期済みで、新しい `Date:` が**同じ瞬間の今の時計**(`first_ms - WX_SYNC_BIAS_MS` での時刻)より 1〜30 秒遅いとき採用しない(設計 §5.9、F6-3)。ちょうど 30 秒は棄却、31 秒は採用。未同期・進み・同じ値は採用。差は uint32 で比べるので 32bit の巻き戻りでも正しい |
| `tests/host/test_wx_core.c` | `t_clock_back`(H-6b。境界 0・1・29・30・31・100 秒、進み、初回、32bit の巻き戻り、棄却後も時計が進む、日付の変わり目) |
| `tests/host/test_wifi_core.c` `test_wifi_at.c` | quiet では `[wifiv]` が出ない・非 quiet では出る(H-X5-14)。P2 は `[wifiv]` 0 行。P4 の偽サーバーは、`a` のあとの `Date:` を実時間どおり進める(以前は固定で、新しい規則だと 30 秒以内の遅れとして棄却されるため。本物の Yahoo の挙動に近づけた) |

H-6⑥(更新要求の返値)の試験の期待値は、設計どおり(取得中・やり直し待ち = `busy`、周期の開始から 5 秒未満・429 待ち = `ignored`)で既に合っていたため、変更していない。

### `APP_STK_BADAP`(実機でオフライン表示と再接続を試す。試験専用)

- 切り替え: `src/app/wifi_sw.h` の `#define APP_STK_BADAP (0)` の 0 を n(1〜9)にするか、`-DAPP_STK_BADAP=n`。範囲外は `#error`。コミットする値は 0。0 のときは `wifi_core.c` のコードも文字列も消える(ホスト試験 S22 が `wifi_core.o` にダミーの文字列が無いことを確認)。`wifi_core_t` に 2 バイト(`badap_n`、`badap_on`)が増える(常に存在。RAM のみ)。
- 動作: **最初の n 回の接続だけ**、`wifi_sw.h` に固定のダミー(`APP_STK_BADAP_SSID` / `APP_STK_BADAP_PASS`。存在しない AP と誤ったパスワード)で `AT+CWJAP` する。n + 1 回目から本物の `WIFI_SSID` / `WIFI_PASS`。ダミーも本物もログに出ない。
- **1 回の数え方**: `wifi_core` が `join` の step に入るたびに 1 回(= `WIFI_RANGE_CONNECT` の実行 1 回 = `wx_sched` の再接続 1 周期)。同じ実行の中での `join` のやり直し(タイムアウト・接続失敗の `try=2`)は数えず、ダミーのまま。範囲 `ALL` など `join` を含む実行も 1 回。
- 実機での使い方(`APP_STK_BADAP = 2` の例): ビルドして書き込み、UART(COM7)を見る。期待するログ: 起動 3 秒後に `[wifi] step=join res=fail try=1 ms=… code=…`(接続失敗は `code=4` かタイムアウト `1`、パスワード違いは `2`)→ やり直しでも失敗 → `[wifi] result=fail stage=join` → `[stk] res=fail sym=0 src=boot err=8` → `[stk] net=err`(LCD はオフライン表示)。約 60 秒後に 2 回目の接続(ダミー。同じ失敗)→ さらに約 120 秒後の 3 回目から本物で接続 → `[stk] net=on` と `[stk] res=ok`(3 銘柄)。再接続の間隔は設計 §5.7 の 60 / 120 / 300 秒(更新要求 `x` / `t` で待ちを飛ばして直ちに再接続もできる。その場合も 1 回に数える)。**実機では未実行**。

## Phase 6b(時計・天気画面の描画と、株価画面との切り替え。ネットワークなし): 実装したこと

実機・ヘッドレスビルドは動かしていない(コーディング担当の範囲外)。ホスト試験(`bash tests/host/run.sh`)と、RX 用 GCC 14.2 の単独のコンパイル(`-c`。`-Wstack-usage=100 -Wall -Wextra`)で、警告なしを確認した。**実機の色化け・ちらつき(R14)は未確認 = ビルド担当 → デバッグ担当 → ユーザーの V-C1 へ**。

### ファイル

| 区分 | ファイル | 内容 |
|---|---|---|
| 生成(新規) | `tools/gen_clock_assets.py` に `--emit-c` / `--out-dir`(既定 `src/app`)を追加 → `src/app/clk_assets.h` `clk_data.c` `clk_preview.c` | 1 行目に生成元とフォントの sha256。手で編集しない。`gen_stock_assets.py` は無変更(`--check` の sha256 は Phase 5 の `205b6010…` のまま) |
| 新規 | `src/app/clk_core.h/.c` | 天気の種類 `CLK_SUN`〜`CLK_UNKNOWN`、`clk_view_t`(8 バイト)、`clk_slot_t`、`clk_date_t`、`clk_kind_of`、`clk_kind_name`、`clk_temp_int`、`clk_view_make`、`clk_view_kind`(inline)、字番号の並び `clk_seq_time` / `clk_seq_date` / `clk_seq_temp`。標準 C のみ |
| 新規 | `src/app/ui_clock.c` | `ui_render_clock(const clk_view_t *, uint8_t *fb)`(設計 §13.4 の描画の順。座標・字の並びは生成物の定数だけ) |
| 変更 | `src/app/gfx.h/.c` | `gfx_blit8_key`(透明色を飛ばして重ねる)を追加。既存の関数は無変更 |
| 変更 | `src/app/ui_render.h/.c` | `ui_render` の先頭に時計・天気画面の分岐 1 か所。`ui_pal_ptr(id)`(0 = `ui_pal`、1〜8 = `clk_pal[id-1]`)。株価の描画コードは無変更 |
| 変更 | `src/app/ui_logic.h/.c` | `UI_SCR_STOCK/CLOCK`、`APP_DWELL_STOCK_MS`(10,000)/ `APP_DWELL_CLOCK_MS`(5,000)(`#ifndef`。-D で上書き可)、`ui_t` に `screen` `drawn_screen` `t_scr` `now` `cview` `cdrawn`、`ui_set_clk` `ui_screen_step` `ui_set_screen` `ui_pal_id`。タップは `ev_push` の 1 か所で株価へ戻す。表示キーは画面ごと |
| 変更 | `src/app/lcd_panel.h/.c` | `lcd_flip_pal(i, pal)`(`R_GLCDC_ClutUpdate_NoReflect` を 1 回 → 成功なら続けて `R_GLCDC_BufferChange`。1 つの関数に閉じる)。`lcd_build_cfg` `lcd_init` `lcd_flip` は無変更 |
| 変更 | `src/app/app_debug.h` | `APP_ERR_LCD_CLUT`(= 11) |
| 変更 | `src/app/app_logfmt.h/.c` | `applog_fmt_scr`(`[scr] to=… pal=… clk=… kind=… temp=… tick=…`) |
| 変更 | `src/app/app_main.c` | `draw_step`: パレットが今表示中と違うときだけ `lcd_flip_pal`、同じなら `lcd_flip`。受け付けたら `[scr]` を 1 行。完了で `s_pal_shown = s_pal_next`。`tick_step`: `ui_screen_step`(プレビュー 1 と `APP_STK_PREVIEW` では呼ばない)、`g_app_dbg.scr = s_ui.screen` |
| 変更 | `src/app/wifi_sw.h` | `APP_CLK_PREVIEW`(0〜2。既定 0)。`APP_STK_PREVIEW` と同時に 1 以上は `#error`。1 以上の `APP_WX` の既定は 0 |
| 変更 | `tools/fb_hostrender.c` `tools/c8png.py` | 状態ファイルに `scr` と `clk.*`、`--pal` は画面のパレット(`ui_pal_ptr(ui_pal_id)`)、`c8png.py sheet … C` で C0〜C11 を 3 列 x 4 行 |
| 試験 | `tests/host/test_clk_core.c`(H-C1)`test_ui_screen.c`(H-C6)`test_ui_clock.c`(H-C7)、`test_gfx.c`(`gfx_blit8_key`)、`test_lcd_panel.c`(H-C9)、`test_logfmt.c`(H-C5 の `[scr]`)、`test_app_main.c`(3 つの新しいビルドの種類)、`run.sh` の S39〜S42 | 下の表 |

### `APP_CLK_PREVIEW`(ネットワークなし。実機の目視・gdb 用)

| 値 | 動き |
|---|---|
| 1 | **設計どおり**(§13.2・§13.3): 300 tick(3 秒)ごとに 偶数番 = 株価(状態 S1 と同じ固定の値)、奇数番 = 時計・天気 C(番 / 2 mod 12)。画面は tick で決める(タップしても次の周期で元に戻る)。毎回パレットが入れ替わる(V-C1・G-C) |
| 2 | **依頼文の「固定の時刻・天気・気温を 10 秒 / 5 秒」**: 固定の値(C0 = 09:41 晴れ 18°)を、本物の `ui_screen_step`(株価 10 秒 / 時計・天気 5 秒)で出す。タップは株価へ戻す |

`APP_STK_PREVIEW`(株価 8 状態を 3 秒ごと)とは同時に使えない(`#error`)。株価の固定の値は `app_main.c` の `s_clk_stock`(`ui_preview_view` は `APP_STK_PREVIEW = 1` でしか作られないため)。

### 設計から外れた点・判断が要る点(設計役の確認が要る)

1. **`APP_CLK_PREVIEW` の 1 と 2**: 設計は 3 秒ごと(1)だけ。依頼文は 10 秒 / 5 秒の固定値(2)。両方入れた。どちらかに絞るなら設計役が決める。
2. **`clk_wday_g20[7]`(`clk_data.c` の配列)を作らず、`clk_assets.h` のマクロ `CLK_WDAY_G20` にした**: 字番号の並びを作る `clk_core.c` を、生成データにリンクせずホストで試験できるようにするため。`clk_corner_x/y` は設計の「`CLK_CORNER_X/Y[4]`(ヘッダーの定数)」を `clk_data.c` の `const int16_t clk_corner_x[4]` `clk_corner_y[4]` にした。
3. **`applog_fmt_scr` の種類の名前の表を `app_logfmt.c` にも持つ**(`clk_kind_name` と同じ 8 つ)。`app_logfmt.c` を標準 C だけで自己完結させ、S8 などのリンクを変えないため。`test_clk_core.c` と `test_logfmt.c` が同じ名前を別々に固定する。
4. **`[scr]` は、既定のビルドでも出る**(株価画面の描き直しのたびに `[scr] to=stock pal=0 clk=none kind=none temp=none tick=…`)。設計 §13.9(受け付けるたびに 1 行)のとおり。Phase 5 の判定スクリプトの行の正規表現に当てはまらない行が増える(`[hb]` `[stk]` など既存の書式は変えていない)。
5. **既定のビルド(`APP_CLK_PREVIEW = 0`)で時計・天気画面へは切り替わらない**(`cview` は未同期のまま = `ui_screen_step` が待つ。6d で `clk_ui` → `ui_set_clk` をつなぐまで)。実機の既定の動作は変わらない。`g_app_dbg.scr` と `[hb] scr=` は 0 のまま。
6. `ui_tap_inject`(UART の `x`)は、直前の `ui_update` の時刻を `t_scr` にする(設計 §13.3 の「`ui->now`」)。`uart_command` が `ui_update` より前に呼ばれるので、10ms 古い。次の `to=clock` までは `x` の tick から 999(U-C6 の 1,000 ± 3 に収まる)。
7. `clk_core.h` に `CLK_CLOCK_NONE`(0xFFFF)`CLK_MINUTES_MAX`(1439)を持つ(`stk_core.h` の同じ値と、`test_clk_core.c` で一致を確認)。

### 試験(ホスト。実機ではない)

| 項目 | 試験 |
|---|---|
| H-C1 | `test_clk_core.c`(S39)。`-DCLK_WX_STALE_S=120` でも通る |
| H-C5(`[scr]`)| `test_logfmt.c`(S8)。全組み合わせで R-SCR に一致・小バッファ・引数不正 |
| H-C6 | `test_ui_screen.c`(S40)。⑧(`-DAPP_STK_PREVIEW` / `APP_CLK_PREVIEW` の `-fsyntax-only` と `#error`)は S7・S22 |
| H-C7 | `test_ui_clock.c`(S41)。⑤ の C と Python の全画素一致は S41 の `cmp`(C0〜C11 とも 139,264 バイトが 0 バイト差。パレット 1,024 バイトも同じ) |
| H-C9 | `test_lcd_panel.c`(S6)。(i)〜(vi) |
| H-C10 | S1(`ui_clock` `clk_core` を追加)・S6・S7・S14・S28・S34・S35・S36・S37 が合格のまま。S38 は株価の sha256 が Phase 5 の値のままであることも見る |
| 結合 | S34: `APP_CLK_PREVIEW = 1`(24 回の入れ替えの面の中身・パレット・`[scr]` の tick)、`= 2`(本物の切り替えとタップ)、`-DAPP_WX=0 -DT_CLK_INJECT`(時計・天気の値を注入。分だけ変わると同じパレット = `lcd_flip`)。S34 の既定の製品は `[scr]` が株価だけで `lcd_flip_pal` を呼ばない |
| 変異 | S42: 株価の表示時間 10 → 9 秒、判定の `>=` → `>`、時刻未同期でも切り替える、タップで戻さない、`ClutUpdate_NoReflect` → 反映つき、`s_pal_shown` を更新しない、同じパレットでも `lcd_flip_pal`、気温の x を 1 画素ずらす(CRC と Python との 0 バイト差) |

### ROM・スタック(RX 用 GCC 14.2、`-Og`、単独の `-c`。リンクはしていない)

- `.text` の増分(`app_main` `app_logfmt` `gfx` `lcd_panel` `ui_logic` `ui_render` + `clk_core` `ui_clock`): 6,898 → 9,840 = **+2,942 バイト**。`.rodata` 相当(`clk_data.o`): **135,773 バイト**(`gen_clock_assets.py --check` の 135,602 + 字の表など)。合計 **約 138,700 バイト**(設計の見積 約 143,000 より少ない)。`.bss`: +30 バイト(`ui_t` と `s_pal_*`)。
- `-Wstack-usage=100` の警告なし。最大は `draw_seq` の 80、`gfx_blit8_key` 72、`applog_fmt_scr` 68。

### 6c・6d に残したこと

- 天気の取得(`wx_parse_weather`・`wx_sched` の天気・`wifi_at.c` の天気・`APP_STK_FAULT = 4`)は未着手。
- 6d: `clk_ui(&s_cview, now_ms)` と `ui_set_clk(&s_ui, &s_cview)` を `tick_step` へ(いまは `APP_CLK_PREVIEW` だけが値を渡す)。
- ビルド担当: `HardwareDebug` の makefile へ新しい `.c`(`clk_core.c` `ui_clock.c` `clk_data.c` `clk_preview.c`)の反映。
