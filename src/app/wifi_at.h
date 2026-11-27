/* wifi_at.h - Wi-Fi(ESP32 AT)のハード側(SCI1・EN・メインループ)。docs/design.md §21.4・§22.7
 * APP_WIFI_ON(APP_WIFI_TEST か APP_WX)= 0 の時は wifi_at.c の中身が消える(wifi_sw.h)。 */
#ifndef WIFI_AT_H
#define WIFI_AT_H

#include "wifi_sw.h"

#if APP_WIFI_TEST
/* 専用モードの本体。戻らない。app_main() が [boot] を出した後に呼ぶ(LCD・タッチは動かさない) */
void wifi_test_main(void);
#endif

#if APP_WX_PRODUCT
#include <stdint.h>
#include "ui_logic.h"
#include "wx_core.h"

/* 製品(LCD・タッチ・デモが動く通常の起動)に組み込む。すべてメインループ専用。ブロックしない。now_ms = 単調増加の ms(10ms tick x 10)。
 * wifi_secret.h が無い・SCI1 を開けない時は OFF(ESP32 に触れない。UI は OFFLINE・未同期のまま) */
void     wx_init(uint32_t now_ms);                       /* [init] ×4 と [perf] の後に 1 回 */
void     wx_poll(uint32_t now_ms);                       /* メインループの毎周回 */
wx_req_t wx_request(wx_src_t src, uint32_t now_ms);      /* 更新ボタン(touch)・UART x。受け付けなければ [wx] req=... を 1 行(WX_SRC_TOUCH か WX_SRC_UART だけ) */
void     wx_ui(ui_wx_t *out, uint32_t now_ms);           /* 表示用の値(ui_set_wx へ。clk_ext = 1) */
void     wx_log_state(uint32_t now_ms);                  /* [wx] net=... の状態行(UART s の応答) */
#endif

#endif /* WIFI_AT_H */
