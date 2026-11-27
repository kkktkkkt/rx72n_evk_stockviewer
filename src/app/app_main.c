/* app_main.c - 製品デモ(浴室リモコン。docs/design.md v2)
 * 起動: app_log_init -> [boot] -> ui_init + 面0に描画 -> lcd_init -> touch_init -> 10msタイマ -> [perf] -> ループ
 * ループ: app_log_poll -> 反映待ちの完了確認 -> (10msごと)[hb]/[err]/UART1文字/touch/ui_update/[ev] -> 天気の取得(APP_WX_PRODUCT) -> 描画(裏面 -> 入れ替え)
 * UART出力の仕様: docs/design-uart.md §10。ログ用の大きいバッファは static(メインループ専用。割り込みから使わない)。 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "platform.h"
#include "r_cmt_rx_if.h"
#include "board_pins.h"
#include "app_debug.h"
#include "app_logfmt.h"
#include "nav_logic.h"
#include "touch_decode.h"
#include "touch_ft5x.h"
#include "lcd_panel.h"
#include "gfx.h"
#include "ui_logic.h"
#include "ui_render.h"
#include "app_main.h"
#if APP_CLUT8_TEST
#include "clut8_test.h"
#endif
#if APP_WIFI_ON
#include "wifi_at.h"
#endif

#define APP_TICK_HZ        (1000u / NAV_SAMPLE_MS)    /* 100Hz = 10ms */
#define APP_HB_TICKS       (APP_TICK_HZ)              /* [hb] 周期 = 1秒(100tick) */
#define APP_ERR_A_TICKS    (10u)                      /* [err] (A) は100msに1行まで */
#define APP_ERR_B_TICKS    (APP_TICK_HZ)              /* [err] (B) は1秒に1行まで */
#define APP_TOUCH_FLAGS    ((TOUCH_SWAP_XY ? UI_TF_SWAP_XY : 0u) | (TOUCH_FLIP_X ? UI_TF_FLIP_X : 0u) | (TOUCH_FLIP_Y ? UI_TF_FLIP_Y : 0u))

static volatile uint32_t s_tick;                 /* 10ms ごとに +1(CMT割り込み) */

static ui_t    s_ui;                             /* 状態機械(メインループ専用) */
#if APP_WX_PRODUCT
static ui_wx_t s_wxv;                            /* 表示用の天気・時計の値(10ms ごとに ui_set_wx へ。gdb で s_ui.wx を書いても次の tick で上書きされる) */
#endif
static uint8_t s_back;                           /* 次に描く面(表示中でない方)。表示中 = s_back ^ 1 */
static bool    s_flip_pending;                   /* lcd_flip を受け付けた。表示面が変わるまで裏面に書かない */
static bool    s_draw_ok;                        /* LCD が使える(lcd_init 成功、flip の致命エラーなし) */

/* ログ用(メインループ専用) */
static char           s_line[APPLOG_LINE_MAX];
static applog_state_t s_st;
static uint8_t        s_scan[APP_DBG_FOUND_MAX];
static uint32_t       s_err_code;                /* 最後に [err] (A) で出した値 */
static uint32_t       s_err_detail;
static uint32_t       s_err_tick;
static bool           s_err_sent;                /* (A) を一度でも出したか */
static uint32_t       s_rxerr_tick;
static bool           s_rxerr_sent;

#if APP_WX_PRODUCT
/* ui_init の直後の表示: 旧時計(21:45)ではなく、未同期(--:--)・OFFLINE にする(wx_ui が毎 tick で本当の値に置き換える) */
static void wx_boot_view(uint32_t now_ms)
{
    static const ui_wx_t off = { .clk_ext = 1u, .upd_min = 0xFFFFu, .clock_min = 0xFFFFu };

    ui_set_wx(&s_ui, &off);
    ui_update(&s_ui, NULL, now_ms);
}
#endif

static void tick_callback(void *pdata)
{
    (void)pdata;
    s_tick++;
}

/* s_line に整形済みの n バイトを送る(n == 0 は整形失敗として drop に数える) */
static void emit(size_t n)
{
    app_log_line(s_line, n);
}

static void log_state(const char *tag, bool with_range)
{
    s_st.tick            = g_app_dbg.tick;
    s_st.err_code        = g_app_dbg.err_code;
    s_st.err_detail      = g_app_dbg.err_detail;
    s_st.i2c_fail_total  = g_app_dbg.i2c_fail_total;
    s_st.drop            = app_log_drops();
    s_st.frames          = g_app_dbg.frames;
    s_st.uf              = g_app_dbg.uf;
    s_st.raw_x           = g_app_dbg.raw_x;
    s_st.raw_y           = g_app_dbg.raw_y;
    s_st.x_min           = g_app_dbg.x_min;
    s_st.x_max           = g_app_dbg.x_max;
    s_st.y_min           = g_app_dbg.y_min;
    s_st.y_max           = g_app_dbg.y_max;
    s_st.raw_points      = g_app_dbg.raw_points;
    s_st.scr             = g_app_dbg.scr;
    s_st.demo            = g_app_dbg.demo;
    s_st.i2c_fail_streak = g_app_dbg.i2c_fail_streak;
    s_st.found_count     = g_app_dbg.found_count;
    emit(applog_fmt_state(s_line, sizeof(s_line), tag, &s_st, with_range));
}

static void log_scan(void)
{
    uint8_t i;
    uint8_t n = g_app_dbg.found_count;

    if (n > APP_DBG_FOUND_MAX) {
        n = (uint8_t)APP_DBG_FOUND_MAX;
    }
    for (i = 0u; i < n; i++) {
        s_scan[i] = g_app_dbg.found_addr[i];
    }
    emit(applog_fmt_scan(s_line, sizeof(s_line), n, s_scan));
}

/* [ui] 行(s コマンドの時だけ) */
static void log_ui(void)
{
    applog_ui_t u;

    u.scr    = ui_scr_name[s_ui.v.scr];
    u.btn    = (s_ui.fb_phase == UI_PH_NONE) ? "none" : ui_btn_defs[s_ui.fb_btn].name;
    u.ph     = (s_ui.fb_phase == UI_PH_PRESS) ? "press" : ((s_ui.fb_phase == UI_PH_FLASH) ? "flash" : "none");
    u.set    = s_ui.v.set;
    u.bath10 = s_ui.v.bath_x10;
    u.fill10 = s_ui.v.fill_x10;
    u.flow10 = s_ui.v.flow_x10;
    u.tr     = s_ui.tr_on;
    emit(applog_fmt_ui(s_line, sizeof(s_line), &u));
}

/* ok=1 のときは err=0 detail=0 固定。ok=0 のときは g_app_dbg の値 */
static void log_init(const char *stage, bool ok)
{
    emit(applog_fmt_init(s_line, sizeof(s_line), stage, ok,
                         ok ? 0u : g_app_dbg.err_code, ok ? 0u : g_app_dbg.err_detail));
}

static void log_err(uint32_t code, uint32_t detail)
{
    emit(applog_fmt_err(s_line, sizeof(s_line), code, detail));
}

/* ui_logic のイベントを [ev] 行にする(設計 design-uart.md §10.3) */
static const char *src_name(uint8_t src)
{
    return (src == (uint8_t)UI_SRC_TOUCH) ? "touch" : ((src == (uint8_t)UI_SRC_UART) ? "uart" : "demo");
}

static const char *scr_name(uint8_t scr)
{
    return (scr < (uint8_t)UI_SCR_COUNT) ? ui_scr_name[scr] : "?";
}

static const char *btn_name(uint8_t btn)
{
    return (btn < (uint8_t)UI_BTN_COUNT) ? ui_btn_defs[btn].name : "?";
}

static void log_event(const ui_ev_t *e)
{
    switch (e->type) {
    case UI_EV_SCR:
        emit(applog_fmt_ev_scr(s_line, sizeof(s_line), scr_name(e->a), scr_name(e->b), src_name(e->src)));
        break;
    case UI_EV_BTN_DOWN:
        emit(applog_fmt_ev_btn(s_line, sizeof(s_line), btn_name(e->a), "down", src_name(e->src)));
        break;
    case UI_EV_BTN_ACT:
        emit(applog_fmt_ev_btn(s_line, sizeof(s_line), btn_name(e->a), "act", src_name(e->src)));
        break;
    case UI_EV_SET:
        emit(applog_fmt_ev_set(s_line, sizeof(s_line), e->a, src_name(e->src)));
        break;
    case UI_EV_DONE:
        emit(applog_fmt_ev_done(s_line, sizeof(s_line), (e->a == (uint8_t)UI_DONE_FILL) ? "fill" : "reheat"));
        break;
    case UI_EV_DEMO:
        emit(applog_fmt_ev_demo(s_line, sizeof(s_line),
                                (e->a == (uint8_t)UI_DEMO_START) ? "start" : ((e->a == (uint8_t)UI_DEMO_END) ? "end" : "abort"),
                                (e->a == (uint8_t)UI_DEMO_END) ? NULL : src_name(e->src)));
        break;
    default:
        break;
    }
}

/* [err] (A) g_app_dbg の (err_code, err_detail) の変化 / (B) UART受信異常 */
static void log_errors(uint32_t now)
{
    uint32_t code   = g_app_dbg.err_code;
    uint32_t detail = g_app_dbg.err_detail;
    uint32_t ev;

    if ((applog_err_changed(code, detail, s_err_code, s_err_detail) != 0) &&
        (!s_err_sent || (applog_due(now, s_err_tick, APP_ERR_A_TICKS) != 0))) {
        s_err_code   = code;
        s_err_detail = detail;
        s_err_tick   = now;
        s_err_sent   = true;
        log_err(code, detail);
    }
    if (!s_rxerr_sent || (applog_due(now, s_rxerr_tick, APP_ERR_B_TICKS) != 0)) {
        ev = app_log_rx_event();
        if (ev != 0u) {
            s_rxerr_tick = now;
            s_rxerr_sent = true;
            log_err(APP_ERR_UART_RX, ev);
        }
    }
}

/* 受信コマンド(1 tick に1文字): n p + - d x は ui_cmd、s ? は応答。それ以外は無視 */
static void uart_command(uint32_t now_ms)
{
    int ch = app_log_getc();

    if (ch < 0) {
        return;
    }
    switch (applog_cmd_parse(ch)) {
    case APPLOG_CMD_NEXT:  ui_cmd(&s_ui, UI_CMD_NEXT, now_ms); break;
    case APPLOG_CMD_PREV:  ui_cmd(&s_ui, UI_CMD_PREV, now_ms); break;
    case APPLOG_CMD_PLUS:  ui_cmd(&s_ui, UI_CMD_PLUS, now_ms); break;
    case APPLOG_CMD_MINUS: ui_cmd(&s_ui, UI_CMD_MINUS, now_ms); break;
    case APPLOG_CMD_DEMO:  ui_cmd(&s_ui, UI_CMD_DEMO, now_ms); break;
    case APPLOG_CMD_WX:    ui_cmd(&s_ui, UI_CMD_WX, now_ms); break;     /* 更新ボタンの仮想タップ。取得は BTN_ACT を見て tick_step が行う */
    case APPLOG_CMD_STATE:
        log_state("st", true);
        if (g_app_dbg.found_count > 0u) {
            log_scan();
        }
        log_ui();
#if APP_WX_PRODUCT
        wx_log_state(now_ms);
#endif
        break;
    case APPLOG_CMD_HELP:
        emit(applog_fmt_help(s_line, sizeof(s_line)));
        break;
    default:
        break;                   /* 未知の文字は無視 */
    }
}

/* 10ms ごとの処理 */
static void tick_step(uint32_t now, uint32_t *last_hb, uint32_t *last_uf, bool touch_ok)
{
    static nav_sample_t smp;
    const nav_sample_t *real = NULL;
    uint32_t now_ms = now * NAV_SAMPLE_MS;
    ui_ev_t  ev;

    g_app_dbg.tick = now;
    if (applog_due(now, *last_hb, APP_HB_TICKS) != 0) {
        *last_hb = now;
        log_state("hb", false);  /* タッチ処理より前。touch_ok や読取失敗で飛ばされない */
    }
    log_errors(now);
    uart_command(now_ms);        /* 1 tick に1文字。touch_ok 判定より前 */

    /* 失敗した周期は標本なし(NULL)として渡す(状態を壊さない)。touch_ok が偽でも ui_update は毎周期呼ぶ */
    if (touch_ok && touch_read(&smp)) {
        if (smp.pressed) {
            app_debug_touch(smp.x, smp.y, smp.points);
        } else {
            g_app_dbg.raw_points = 0u;
        }
        real = &smp;
    }
#if APP_WX_PRODUCT
    wx_ui(&s_wxv, now_ms);       /* 表示用の値(Wi-Fi・天気・時計・日付)。ui_update の前(時計が clock_min を使う) */
    ui_set_wx(&s_ui, &s_wxv);
#endif
    ui_update(&s_ui, real, now_ms);
    g_app_dbg.scr  = s_ui.v.scr;
    g_app_dbg.demo = s_ui.demo;
    while (ui_event_pop(&s_ui, &ev)) {
        log_event(&ev);
#if APP_WX_PRODUCT
        if ((ev.type == (uint8_t)UI_EV_BTN_ACT) && (ev.a == (uint8_t)UI_BTN_H_WX) && (ev.src != (uint8_t)UI_SRC_DEMO)) {
            (void)wx_request((ev.src == (uint8_t)UI_SRC_TOUCH) ? WX_SRC_TOUCH : WX_SRC_UART, now_ms);   /* 更新ボタン */
        }
#endif
    }
    if (applog_due(now, *last_uf, APP_HB_TICKS) != 0) {
        *last_uf = now;
        g_app_dbg.uf += lcd_underflow_poll();
    }
}

/* 描画: 表示キーが変わり、前回の入れ替えが済んでいれば、裏面に全画面を描いて入れ替えを依頼する */
static void draw_step(void)
{
    if (!s_draw_ok || s_flip_pending || !ui_view_changed(&s_ui)) {
        return;
    }
    ui_render(&s_ui, lcd_fb(s_back));
    switch (lcd_flip(s_back)) {
    case LCD_FLIP_OK:
        s_flip_pending = true;
        ui_view_mark(&s_ui);
        break;
    case LCD_FLIP_FAIL:
        s_draw_ok = false;       /* 致命: 描画を止める(表示は直前の絵のまま)。APP_ERR_LCD_FLIP は lcd_flip が記録 */
        break;
    default:
        break;                   /* BUSY: 次の周回でもう一度 */
    }
}

#if APP_PERF_BENCH
/* 描画の所要時間(design-uart.md §10.5)。裏面だけに描くので表示は乱れない。10msカウンタの差から 1回あたり µs を出す */
static uint32_t bench_us(void (*fn)(void))
{
    uint32_t t0 = s_tick;
    uint32_t i;

    for (i = 0u; i < APP_PERF_N; i++) {
        fn();
    }
    return (s_tick - t0) * (NAV_SAMPLE_MS * 1000u) / APP_PERF_N;
}

static void bench_copy(void)   { gfx_blit8(lcd_fb(s_back), ui_bgi[UI_BG_HOME], UI_W, 0, 0, UI_W, UI_H, 0, 0); }
static void bench_render(void) { ui_render(&s_ui, lcd_fb(s_back)); }

static void log_perf(void)
{
    uint32_t copy_us, home_us, slide_us;

    copy_us = bench_us(bench_copy);
    home_us = bench_us(bench_render);
    s_ui.ov = s_ui.v;                    /* 前へ進むスライドの途中(dx = 240)を作る */
    s_ui.ov.scr = (uint8_t)UI_SCR_FILL;
    s_ui.tr_on = 1u;
    s_ui.tr_fwd = 1u;
    s_ui.tr_dx = 240u;
    slide_us = bench_us(bench_render);
    ui_init(&s_ui, s_tick * NAV_SAMPLE_MS, (uint16_t)TOUCH_X_MAX, (uint16_t)TOUCH_Y_MAX, (uint8_t)APP_TOUCH_FLAGS);
#if APP_WX_PRODUCT
    wx_boot_view(s_tick * NAV_SAMPLE_MS);
#endif
    emit(applog_fmt_perf(s_line, sizeof(s_line), APP_PERF_N, copy_us, home_us, slide_us));
}
#endif

void app_main(void)
{
    uint32_t     cmt_ch;
    uint32_t     last = 0u;
    uint32_t     last_hb = 0u;
    uint32_t     last_uf = 0u;
    uint32_t     now;
    bool         touch_ok;

#if APP_CLUT8_TEST
    clut8_test_main();           /* 戻らない。以降の製品コードには到達しない(docs/design.md §19) */
#endif
    app_debug_init();
    app_log_init();
    emit(applog_fmt_boot(s_line, sizeof(s_line), __DATE__ " " __TIME__,
                         (uint8_t)APP_LOG_SCI_CH, (uint32_t)APP_LOG_BAUD));
#if APP_WIFI_TEST
    wifi_test_main();            /* 戻らない。LCD・タッチ・製品 UI は動かさない(docs/design.md §21.4.1) */
#endif

    /* 面0にホームを描いてから GLCDC を起動する(RAM2 は起動前に書ける) */
    ui_init(&s_ui, 0u, (uint16_t)TOUCH_X_MAX, (uint16_t)TOUCH_Y_MAX, (uint8_t)APP_TOUCH_FLAGS);
#if APP_WX_PRODUCT
    wx_boot_view(0u);
#endif
    ui_render(&s_ui, lcd_fb(0u));
    ui_view_mark(&s_ui);
    s_back = 1u;

    s_draw_ok = lcd_init();      /* 失敗しても続行(原因は g_app_dbg.err_code) */
    if (s_draw_ok) {
        (void)lcd_underflow_poll();  /* 結果は捨てる: 表示開始時の1回は GLCDC の初回検出(実機8回で再現)。uf に数えない */
    }
    log_init("lcd", s_draw_ok);
    touch_ok = touch_init();     /* 失敗時はタッチ無効のまま(ui_update は標本なしで動かす) */
    log_init("touch", touch_ok);

    if (!R_CMT_CreatePeriodic(APP_TICK_HZ, tick_callback, &cmt_ch)) {
        app_debug_set_err(APP_ERR_TIMER, 0u);
        log_init("timer", false);
        log_err(APP_ERR_TIMER, 0u);
        for (;;) {
            /* タイマが無いと周期が作れない。止まるが、上の行を出し切るため送信は進める */
            app_log_poll();
        }
    }
    log_init("timer", true);
    log_init("loop", true);
#if APP_PERF_BENCH
    if (s_draw_ok) {
        log_perf();
    }
#endif
#if APP_WX_PRODUCT
    wx_init(s_tick * NAV_SAMPLE_MS);   /* [init] ×4 と [perf] の後。3 秒後に接続を始める(wifi_secret.h が無ければ OFF) */
#endif

    for (;;) {
        app_log_poll();          /* 送信キューを進める(非ブロッキング。毎周回) */

        if (s_flip_pending && !lcd_flip_busy()) {   /* 入れ替わった: 前面 = 今まで裏面だった方 */
            s_flip_pending = false;
            s_back ^= 1u;
            g_app_dbg.frames++;
        }
        now = s_tick;
        if (now != last) {       /* 10ms の刻みでなければタッチ・UART は進めない */
            last = now;
            tick_step(now, &last_hb, &last_uf, touch_ok);
        }
#if APP_WX_PRODUCT
        wx_poll(now * NAV_SAMPLE_MS);   /* 受信の汲み上げと取得の予定表(毎周回。ブロックしない) */
#endif
        draw_step();
    }
}
