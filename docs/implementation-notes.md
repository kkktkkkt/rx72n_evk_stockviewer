# 実装メモ(コーディング担当)

最終更新: 2026-11-27。状態: **Phase 0(TLS の実機検証用の最小の変更)と Phase 1(表示)を実装済み。Phase 2 以降は未着手。** 実機ではビルドも実行もしていない(ホスト試験のみ)。

## Phase 0: 変更したファイル(`docs/design.md` §5.5.2 の 0-1〜0-6)

| ファイル | 変更 |
|---|---|
| `src/app/wx_core.h` / `.c` | `WX_BUF_SIZE` 1536 → 3072(0-3)。`wx_target_t`(`METEO` 既定 / `YAHOO_TCP` / `YAHOO_SSL` / `GOOGLE_SSL` / `YAHOO_SSL_NOAUTH`)、`wx_build_cipstart`、`wx_target_ssl` を追加。`wx_build_request(buf, size, target, sym)` に変更(0-2)。`wx_result_t` に `len`。`wx_fmt_fail` に `len=<n>` を追加し、`err` を 1〜9 に(0-5)。`WX_RANGE_STR` `WX_UA` を追加 |
| `src/app/wifi_core.h` / `.c` | `AT+CIPSTART` を実行時に組み立て(0-1)。`wifi_core_set_target`。SSL の `wx_tcp` は 20,000ms、`wx_recv` は 15,000ms。取得中の `ready` で `err=9`(0-4)。応答が溢れても `http` と `Date:` は拾う(`err=7` のまま)。新しい step `wx_sslconf`(`z` のときだけ。`AT+CIPSSLCCONF=0,0,0`、`OK` でも `ERROR` でも先へ) |
| `src/app/wifi_at.c` | 専用モードの UART コマンド `t` `y` `g` `z`(0-6)。`x` は最後に選んだ対象でもう一度(何も選んでいなければ今までどおり天気) |
| `tests/host/test_wifi_core.c` `test_wx_core.c` `test_wifi_at.c` `test_wx_indep.c` | 新しい関数の試験を追加(P0-1〜P0-7、P0-A〜P0-C、F6)。呼び出しの更新(`wx_build_request`)、バッファ 3072・`WIFI_STEP_END` = 21・`wx_recv` 15 秒への追従 |

0-7(`tools/job-runner.ps1` の `tx` 許可リストに `t` `y` `g` `z`)は **デバッグ担当の作業**。まだ。

## 既定のビルドへの影響

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

## 設計から外れた点・設計役への差し戻し

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

- **Phase 4 で削除する**: 旧 UI の `nav_logic.c`(`nav_t`・`nav_update`・`nav_step`。`app_main.c` が `nav_logic.h` を include するため ELF に残る)、`clut8_*`、`tools/gen_ui_assets.py`(**実行すると `src/app` の生成物を壊す**)、`tests/host/test_nav*.c`、`test_clut8_*.c`、`tools/fb2png.py`・`tools/wx_compare_p2b.py`(旧 UI 用)。
- Phase 2: `stk_fmt_*` を作って `ui_render.c` の暫定整形と置き換える。Phase 3: `ui_wx_t` と `wx_ui` の廃止、`[ev] btn=tap src=...` の書式(今は `btn=tap st=act`)。Phase 4: `app_main` の結合、`[perf]` の `slide`(今は 0)。

## 未実装(Phase 1 以降)

表示・数値整形・`wx_parse_http` の Yahoo 対応・銘柄ごとの取得と予定表・`[stk]` ログ・結合。
