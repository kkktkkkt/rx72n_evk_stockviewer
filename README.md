# rx72n_evk_stockviewer

RX72N Envision Kit のLCD(480x272)に、ルネサスエレクトロニクス(6723)、日経平均、S&P500(ドル)の現在値と前日比(金額・％)を表示するガジェットです。基板のESP32(ATコマンド)でWi-Fiにつなぎ、Yahoo Financeから10分ごとに取得します。

- 上昇は赤、下落は緑、変化なしは灰です。色だけでなく、▲/▼と＋/−も付けます。
- 日本株と日経平均は約15分遅れの値です(Yahooの仕様)。画面のフッターにも「15分遅れ」と出ます。
- 画面のどこかをタッチすると、すぐ更新します(5秒以内の連続は無視)。
- 取得に失敗した銘柄だけ「取得失敗」と出し、古い値は残します。Wi-Fiが切れたときは「オフライン」と出して再接続します。

## 必要なもの

- RX72N Envision Kit(ESP32のATファームは `1.1.3.0` の出荷時のもので動作確認)
- e² studio(Smart Configurator込み)と GCC for Renesas RX 14.2
- 2.4GHzのWi-Fi(WPA2-PSK)
- 書き込み用のUSB(E2 Lite)と、ログ用のUSB(CN8、COM、115200bps)

## ビルド

1. e² studio で、このフォルダを既存のプロジェクトとして取り込みます(プロジェクト名は `rx72n_stockview`、構成は `HardwareDebug`)。
2. `src/app/wifi_secret.h.example` を同じフォルダに `wifi_secret.h` としてコピーし、`WIFI_SSID` と `WIFI_PASS` を自分のアクセスポイントの値に書き換えます。
3. ビルドして、基板に書き込みます。

`wifi_secret.h` は `.gitignore` 済みでコミットされません。SSIDとパスワードは、ELFとMOTの中に平文で入ります。それらのファイルを他人に渡さないでください。`wifi_secret.h` が無い場合の動作(Wi-Fiを使わず「オフライン」のまま)は、ソースのコメントにある設計で、実機では確認していません。

コマンドラインでビルドや書き込みをする手順は `docs/build-and-debug.md` と `docs/cli-debug.md` にあります。

## ビルドスイッチ(`src/app/wifi_sw.h`)

| スイッチ | 既定 | 内容 |
|---|---|---|
| `APP_WX` | 1 | Wi-Fiで株価を取得する |
| `APP_STK_PREVIEW` | 0 | 1にすると、ネットワークなしで8つの決まった状態を3秒ごとに表示する(見た目の確認用) |
| `APP_STK_FAULT` | 0 | 1〜3にすると、その番号の銘柄の要求をわざと壊して失敗させる(試験用) |
| `APP_STK_BADAP` | 0 | n(1〜9)にすると、最初のn回のWi-Fi接続をダミーのSSIDで失敗させる(試験用) |
| `APP_WIFI_TEST` | 0 | Wi-FiとTLSの診断専用モード(LCDは動かさない) |

## 試験

PC上で、Cのコードを試験します(Git Bashのgccで実行)。

```sh
bash tests/host/run.sh
```

数値の計算、Yahooの応答の解析、取得の予定表、LCD描画(PythonのプレビューとC描画の全画素一致)などを確認します。結果と、実機での確認状況は `docs/test-report.md` と `docs/debug-report.md` にあります。

## 画面の画像と描画

背景の画像(帯、パレット、数字のフォント)は `tools/gen_stock_assets.py` が作ります。生成されるCファイルは手で編集しません。プレビューのPNGは `tools/assets/stock/preview/` にあります。

## 構成

| 場所 | 内容 |
|---|---|
| `src/app/` | アプリのコード(取得、計算、描画、Wi-Fi) |
| `src/smc_gen/` | Smart Configuratorの生成コード(手で編集しない) |
| `tests/host/` | PC上の試験 |
| `tools/` | 画像の生成、ビルド・書き込み・ログ取得のスクリプト |
| `docs/design.md` | 設計書(表示、数値、取得、受入基準) |

## 注意

- 取得先のYahoo Financeのchart APIは、非公式です。仕様の変更や利用の制限があり得ます。個人利用の範囲で、取得間隔は10分以上にしてください。
- TLSでは、ESP32側が証明書を検証しません(AT 1.x の既定)。送るのは銘柄名の要求だけで、秘密情報は送りません。
- 株価の表示は、投資の判断のための情報ではありません。

## 開発の進め方

設計、コーディング、ビルド、テスト、デバッグを、役割ごとに別のサブエージェントに任せて進めました。役割と手順は `CLAUDE.md` と `agents/` にあります。

## ライセンス

BSD 3-Clause(`LICENSE`)。`src/smc_gen/` のルネサスのコードは、それぞれのライセンスに従います。
