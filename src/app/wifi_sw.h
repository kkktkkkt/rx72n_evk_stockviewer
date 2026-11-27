/* wifi_sw.h - ビルドスイッチ(docs/design.md §7.4)。標準Cのみ(FIT を include しない)。
 * 切り替えは、下の既定値を書き換えるか -D<名前>=<値>。コミットする値は下の既定のまま(APP_WIFI_TEST・APP_STK_PREVIEW・APP_STK_FAULT = 0)。
 * wifi_core.c / wx_core.c / wifi_at.c は APP_WIFI_ON(= APP_WIFI_TEST か APP_WX)が 0 の時は中身が消える(リンクに --gc-sections が無いため、消さないと ROM に残る)。 */
#ifndef WIFI_SW_H
#define WIFI_SW_H

/* 1 = ネットワークなしで、8 つの決まった状態(ui_preview.c = docs/design.md §3.12)を 3 秒ごとに LCD へ出す(実機の目視用)。
 * ui_assets.h(生成物)がこのヘッダーを include するので、必ず ui_assets.h より先に定義される。 */
#ifndef APP_STK_PREVIEW
#define APP_STK_PREVIEW      (0)
#endif

/* 1 = Wi-Fi / TLS の検証専用モード(LCD・タッチは動かさない。Phase 0 で使った) */
#ifndef APP_WIFI_TEST
#define APP_WIFI_TEST        (0)
#endif

/* 1 = 製品に株価の取得を組み込む(既定)。0 = ESP32 に触れず、オフライン表示のまま。
 * wifi_secret.h が無い・SCI1 を開けない時は、1 でも OFF(wifi_at.c)。プレビュー(ネットワークなし)の既定は 0 */
#ifndef APP_WX
#if APP_STK_PREVIEW
#define APP_WX               (0)
#else
#define APP_WX               (1)
#endif
#endif
#define APP_WIFI_ON          ((APP_WIFI_TEST) || (APP_WX))
/* 製品(LCD・タッチが動く通常の起動)に取得を組み込む = APP_WX = 1 かつ APP_WIFI_TEST = 0。専用モードが優先 */
#define APP_WX_PRODUCT       ((APP_WX) && !(APP_WIFI_TEST))

/* n(1〜3)= 銘柄 n-1 の要求のパスを壊す(404 を返させて err=4 を起こす。失敗の試験用 = 受入 U-6)。0 = 壊さない。製品は 0 */
#ifndef APP_STK_FAULT
#define APP_STK_FAULT        (0)
#endif
#if ((APP_STK_FAULT) < 0) || ((APP_STK_FAULT) > 3)
#error "APP_STK_FAULT must be 0..3"
#endif

/* n(1〜9)= 最初の n 回の接続(WIFI_RANGE_CONNECT の join。再接続の 1 周期 = 1 回。join のやり直しは数えない)だけ、下のダミーの SSID・パスワードで
 * AT+CWJAP する(必ず失敗する)。n + 1 回目からは本物。オフライン表示と再接続を実機で試す用(使い方: docs/implementation-notes.md)。0 = 使わない(コードも消える)。製品は 0 */
#ifndef APP_STK_BADAP
#define APP_STK_BADAP        (0)
#endif
#if ((APP_STK_BADAP) < 0) || ((APP_STK_BADAP) > 9)
#error "APP_STK_BADAP must be 0..9"
#endif
#define APP_STK_BADAP_SSID   "stk-badap-no-such-ap"
#define APP_STK_BADAP_PASS   "stk-badap-no-such-pw"

/* 1 = 起動 2 秒後に UART コマンド d と同じ(全部)を 1 回だけ実行する(APP_WIFI_TEST = 1 の試験用) */
#ifndef APP_WIFI_AUTOSTART
#define APP_WIFI_AUTOSTART   (0)
#endif

/* ESP32 の AT 用 UART(SCI1)のボーレート。ESP-AT の既定は 115200 */
#ifndef WIFI_BAUD
#define WIFI_BAUD            (115200u)
#endif

#endif /* WIFI_SW_H */
