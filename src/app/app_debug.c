/* app_debug.c */
#include "app_debug.h"

volatile app_dbg_t g_app_dbg;

void app_debug_init(void)
{
    uint32_t i;

    g_app_dbg.tick            = 0u;
    g_app_dbg.err_code        = APP_ERR_NONE;
    g_app_dbg.err_detail      = 0u;
    g_app_dbg.i2c_fail_total  = 0u;
    g_app_dbg.raw_x           = 0u;
    g_app_dbg.raw_y           = 0u;
    g_app_dbg.x_min           = 0xFFFFu;
    g_app_dbg.x_max           = 0u;
    g_app_dbg.y_min           = 0xFFFFu;
    g_app_dbg.y_max           = 0u;
    g_app_dbg.raw_points      = 0u;
    g_app_dbg.frames          = 0u;
    g_app_dbg.uf              = 0u;
    g_app_dbg.scr             = 0u;
    g_app_dbg.demo            = 0u;
    g_app_dbg.i2c_fail_streak = 0u;
    g_app_dbg.found_count     = 0u;
    for (i = 0u; i < APP_DBG_FOUND_MAX; i++) {
        g_app_dbg.found_addr[i] = 0u;
    }
}

void app_debug_set_err(uint32_t code, uint32_t detail)
{
    g_app_dbg.err_code   = code;
    g_app_dbg.err_detail = detail;
}

/* 触れている間だけ呼ぶ。最新座標と範囲を更新 */
void app_debug_touch(const uint16_t x, const uint16_t y, const uint8_t points)
{
    g_app_dbg.raw_x      = x;
    g_app_dbg.raw_y      = y;
    g_app_dbg.raw_points = points;
    if (x < g_app_dbg.x_min) { g_app_dbg.x_min = x; }
    if (x > g_app_dbg.x_max) { g_app_dbg.x_max = x; }
    if (y < g_app_dbg.y_min) { g_app_dbg.y_min = y; }
    if (y > g_app_dbg.y_max) { g_app_dbg.y_max = y; }
}

/* ------------------------------------------------------------------ */
/* UARTログ。APP_LOG_ENABLE = 1 のときだけコンパイルされる(仕様 docs/design-uart.md)。
 * 送信: 行キュー(app_logq、メインループ専用)。r_sci_rx は SCI_CFG_USE_CIRCULAR_BUFFER = 0 のため、
 *       送信中の R_SCI_Send は SCI_ERR_XCVR_BUSY。BUSY は捨てずに残し、app_log_poll() が再試行する。
 * 受信: r_sci_rx の受信キュー(割り込みで充填)を app_log_getc() でポーリング。
 *       コールバックは異常イベントの値を書くだけ。 */
#if APP_LOG_ENABLE

#include "platform.h"
#include "r_sci_rx_if.h"
#include "r_sci_rx_pinset.h"
#include "app_logq.h"

#if (APP_LOG_SCI_CH != 1) && (APP_LOG_SCI_CH != 2)
#error "APP_LOG_SCI_CH は 1 か 2"
#endif

#define APP_LOG_INT_PRIORITY (2u)    /* 設計 §4.4: タッチI2C(SCI6)と同じ。SCの生成物は変えずここで指定 */

static sci_hdl_t  s_log_hdl;
static bool       s_log_ready;
static bool       s_log_tried;
static applog_q_t s_q;               /* 約1.5KB。メインループ専用 */

/* コールバック(割り込み内)用。値を書くだけ。seq を最後に増やす(1書き手・1読み手でロック不要) */
static volatile uint32_t s_rx_ev_val;
static volatile uint32_t s_rx_ev_seq;
static uint32_t          s_rx_ev_seen;

static void log_cb(void *p_args)
{
    const sci_cb_args_t *a = (const sci_cb_args_t *)p_args;

    if (a == NULL) {
        return;
    }
    if ((a->event == SCI_EVT_RXBUF_OVFL) || (a->event == SCI_EVT_FRAMING_ERR) ||
        (a->event == SCI_EVT_PARITY_ERR) || (a->event == SCI_EVT_OVFL_ERR)) {
        s_rx_ev_val = (uint32_t)a->event;
        s_rx_ev_seq = s_rx_ev_seq + 1u;
    }
    /* SCI_EVT_RX_CHAR などは何もしない(バイトは r_sci_rx のキューに入る) */
}

void app_log_init(void)
{
    sci_cfg_t cfg;

    if (s_log_tried) {
        return;
    }
    s_log_tried = true;
    applog_q_init(&s_q);
#if (APP_LOG_SCI_CH == 2)
    R_SCI_PinSet_SCI2();
#else
    R_SCI_PinSet_SCI1();
#endif
    cfg.async.baud_rate    = APP_LOG_BAUD;
    cfg.async.clk_src      = SCI_CLK_INT;
    cfg.async.data_size    = SCI_DATA_8BIT;
    cfg.async.parity_en    = SCI_PARITY_OFF;
    cfg.async.parity_type  = 0u;
    cfg.async.stop_bits    = SCI_STOPBITS_1;
    cfg.async.int_priority = APP_LOG_INT_PRIORITY;
    if (R_SCI_Open(APP_LOG_SCI_CH, SCI_MODE_ASYNC, &cfg, log_cb, &s_log_hdl) == SCI_SUCCESS) {
        s_log_ready = true;
    }
}

void app_log_poll(void)
{
    const char *p;
    size_t      n;
    sci_err_t   e;

    if (!s_log_ready) {
        return;
    }
    p = applog_q_peek(&s_q, &n);
    if (p == NULL) {
        return;
    }
    e = R_SCI_Send(s_log_hdl, (uint8_t *)p, (uint16_t)n);
    if (e == SCI_ERR_XCVR_BUSY) {
        return;                      /* 送信中: 捨てずに次回再試行 */
    }
    if (e != SCI_SUCCESS) {
        applog_q_note_drop(&s_q);    /* BUSY以外の失敗: その行を捨てる */
    }
    applog_q_pop(&s_q);              /* 成功時は r_sci_rx のバッファへコピー済み */
}

void app_log_line(const char *s, size_t n)
{
    if (!s_log_tried) {
        app_log_init();
    }
    if (!s_log_ready) {
        return;                      /* 開けていない: 出力なし。数えない */
    }
    if (s == NULL) {
        n = 0u;
    }
    if (applog_q_push(&s_q, s, n)) { /* 失敗(n==0、満杯、長すぎ)は push が drop を数える */
        app_log_poll();              /* 空いていればすぐ送り始める */
    }
}

int app_log_getc(void)
{
    uint8_t c;

    if (!s_log_ready) {
        return -1;
    }
    if (R_SCI_Receive(s_log_hdl, &c, 1u) == SCI_SUCCESS) {
        return (int)c;
    }
    return -1;
}

uint32_t app_log_drops(void)
{
    return applog_q_drops(&s_q);
}

uint32_t app_log_rx_event(void)
{
    uint32_t seq = s_rx_ev_seq;

    if (seq == s_rx_ev_seen) {
        return 0u;
    }
    s_rx_ev_seen = seq;
    return s_rx_ev_val;
}

#endif /* APP_LOG_ENABLE */
