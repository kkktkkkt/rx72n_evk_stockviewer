/* wifi_sw.h - Wi-Fi(ESP32 AT)試作(docs/design.md §21)のビルドスイッチ。標準Cのみ(FIT を include しない)。
 * APP_WIFI_TEST = 0(既定)= 製品と同じ。wifi_core.c / wifi_at.c は中身が消える(ROM・コードの差 0。リンクに --gc-sections が無いため、消さないと残る)。
 * 1 = Wi-Fi 試作ファーム(LCD・タッチは動かさない)。切り替えは -DAPP_WIFI_TEST=1。コミットする値は 0。 */
#ifndef WIFI_SW_H
#define WIFI_SW_H

#include "clut8_sw.h"

#ifndef APP_WIFI_TEST
#define APP_WIFI_TEST        (0)
#endif

/* 天気・気温の取得と時刻同期(docs/design.md §22)。既定 0(実機の受入に合格するまで)。第1段階では専用モード(APP_WIFI_TEST = 1)だけが使う。
 * wifi_core.c / wx_core.c は APP_WIFI_ON(= APP_WIFI_TEST か APP_WX)の時だけ中身がある。両方 0 なら ROM の差 0。 */
#ifndef APP_WX
#define APP_WX               (0)
#endif
#define APP_WIFI_ON          ((APP_WIFI_TEST) || (APP_WX))
/* 製品(LCD・タッチ・デモが動く通常の起動)に取得を組み込む = APP_WX = 1 かつ APP_WIFI_TEST = 0(§22.7)。専用モードが優先 */
#define APP_WX_PRODUCT       ((APP_WX) && !(APP_WIFI_TEST))

/* 1 = ネットワークなしで、8 つの決まった状態(ui_preview.c = docs/design.md §3.12)を 3 秒ごとに LCD へ出す(実機の目視用。docs/design.md §7.4)。
 * ui_assets.h(生成物)がこのヘッダーを include するので、必ず ui_assets.h より先に定義される。切り替えは下の 1 行か -DAPP_STK_PREVIEW=1。コミットする値は 0 */
#ifndef APP_STK_PREVIEW
#define APP_STK_PREVIEW      (0)
#endif

/* 1 = [init] stage=wifi の 2 秒後に UART コマンド d と同じ(全部)を1回だけ実行する */
#ifndef APP_WIFI_AUTOSTART
#define APP_WIFI_AUTOSTART   (0)
#endif

/* ESP32 の AT 用 UART(SCI1)のボーレート。ESP-AT の既定は 115200 */
#ifndef WIFI_BAUD
#define WIFI_BAUD            (115200u)
#endif

#if APP_WIFI_TEST && APP_CLUT8_TEST
#error "APP_WIFI_TEST and APP_CLUT8_TEST must not both be 1"
#endif

#endif /* WIFI_SW_H */
