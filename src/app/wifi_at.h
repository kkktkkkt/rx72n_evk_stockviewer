/* wifi_at.h - Wi-Fi(ESP32 AT)のハード側(SCI1・EN・メインループ)。docs/design.md §21.4・§5.7・§5.9
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
#include "stk_core.h"
#include "wx_core.h"

/* 製品(LCD・タッチ・デモが動く通常の起動)に組み込む。すべてメインループ専用。ブロックしない。now_ms = 単調増加の ms(10ms tick x 10)。
 * wifi_secret.h が無い・SCI1 を開けない時は OFF(ESP32 に触れない。UI は OFFLINE・未同期のまま) */
void     wx_init(uint32_t now_ms);                       /* [init] ×4 と [perf] の後に 1 回 */
void     wx_poll(uint32_t now_ms);                       /* メインループの毎周回 */
wx_req_t wx_request(wx_src_t src, uint32_t now_ms);      /* 更新(タップ・UART x)。受け付けなければ [stk] req=... を 1 行(WX_SRC_TOUCH か WX_SRC_UART だけ) */
void     stk_ui(stk_view_t *out, uint32_t now_ms);       /* 表示用の値(ui_set_view へ。3 銘柄の最後の成功・市場の状態・Wi-Fi の状態・時計・更新時刻。design.md §3.9・§4.4) */
void     wx_log_state(uint32_t now_ms);                  /* [stk] net= の 1 行と sym= の 3 行(UART s の応答) */
/* 暫定(Phase 4 で app_main が stk_ui に切り替えたら ui_wx_t と一緒に消す): 旧 UI の枠(net・busy・時計・更新時刻)だけ */
void     wx_ui(ui_wx_t *out, uint32_t now_ms);
#endif

#endif /* WIFI_AT_H */
