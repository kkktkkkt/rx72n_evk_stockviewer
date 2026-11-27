# 実装メモ(コーディング担当)

最終更新: 2026-11-27。状態: **Phase 0(TLS の実機検証用の最小の変更)だけ実装済み。Phase 1 以降は未着手。** 実機ではビルドも実行もしていない(ホスト試験のみ)。

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

## 未実装(Phase 1 以降)

表示・数値整形・`wx_parse_http` の Yahoo 対応・銘柄ごとの取得と予定表・`[stk]` ログ・結合。
