/* ui_logic.c - 株価ビューの画面の状態(標準Cのみ。docs/design.md §6・§13.3) */
#include <string.h>
#include "ui_logic.h"

enum { TS_IDLE = 0, TS_PEND, TS_HELD, TS_BLOCK };    /* 待機 / 押下確定待ち / 押下中 / 無視して離し待ち */

static void ev_push(ui_t *u, ui_ev_type_t type, ui_src_t src)
{
    if (type == UI_EV_TAP) {
        ui_set_screen(u, (uint8_t)UI_SCR_STOCK, u->now);       /* タップ = 株価画面へ戻す(§13.3)。更新要求は app_main が UI_EV_TAP を見て行う */
    }
    if (u->ev_n >= UI_EV_Q) {
        u->ev_lost++;                    /* 満杯なら新しいものを捨てる */
        return;
    }
    u->ev[u->ev_h].type = (uint8_t)type;
    u->ev[u->ev_h].src  = (uint8_t)src;
    u->ev_h = (uint8_t)((u->ev_h + 1u) % UI_EV_Q);
    u->ev_n++;
}

void ui_init(ui_t *u, uint32_t now_ms)
{
    memset(u, 0, sizeof(*u));
    u->view.net       = (uint8_t)STK_NET_OFF;
    u->view.clock_min = (uint16_t)STK_NONE16;
    u->view.upd_min   = (uint16_t)STK_NONE16;
    u->cview.clock_min = (uint16_t)CLK_CLOCK_NONE;
    u->cview.kind      = (uint8_t)CLK_UNKNOWN;
    u->t_scr = now_ms;
    u->now   = now_ms;
}

bool ui_event_pop(ui_t *u, ui_ev_t *out)
{
    if ((u == NULL) || (u->ev_n == 0u)) {
        return false;
    }
    if (out != NULL) {
        *out = u->ev[u->ev_t];
    }
    u->ev_t = (uint8_t)((u->ev_t + 1u) % UI_EV_Q);
    u->ev_n--;
    return true;
}

void ui_tap_inject(ui_t *u, ui_src_t src)
{
    ev_push(u, UI_EV_TAP, src);
}

void ui_update(ui_t *u, const nav_sample_t *s, uint32_t now)
{
    bool down, multi;

    u->now = now;                        /* 標本なしでも更新する(ui_tap_inject が使う) */
    if (s == NULL) {
        return;                          /* 標本なし: 状態を変えない */
    }
    down  = s->pressed;
    multi = down && (s->points >= 2u);
    if ((u->ts == TS_IDLE) || (u->ts == TS_PEND)) {
        if (!down) {
            u->ts = TS_IDLE;
            u->tcnt = 0u;
        } else if (multi) {
            u->ts = TS_BLOCK;            /* 2点以上は押下にしない。全部離れるまで無視 */
            u->tcnt = 0u;
        } else {
            u->ts = TS_PEND;
            u->tcnt++;
            if (u->tcnt >= UI_TAP_DOWN_N) {
                u->ts = TS_HELD;
                u->tcnt = 0u;
                u->t_down = now;
            }
        }
    } else if (down) {                   /* HELD / BLOCK: 触れている間は離しのカウントを戻す */
        u->tcnt = 0u;
        if ((u->ts == TS_HELD) && ((now - u->t_down) > UI_TAP_HOLD_MAX_MS)) {
            u->ts = TS_BLOCK;            /* 長押しはタップにしない。離れるまで待つ */
        }
    } else {
        u->tcnt++;
        if (u->tcnt >= UI_TAP_UP_N) {
            if ((u->ts == TS_HELD) && ((now - u->t_down) <= UI_TAP_HOLD_MAX_MS)) {
                ev_push(u, UI_EV_TAP, UI_SRC_TOUCH);
            }
            u->ts = TS_IDLE;
            u->tcnt = 0u;
        }
    }
}

void ui_set_view(ui_t *u, const stk_view_t *v)
{
    if (v != NULL) {
        u->view = *v;
    }
}

void ui_set_clk(ui_t *u, const clk_view_t *v)
{
    if (v != NULL) {
        u->cview = *v;
    }
}

void ui_set_screen(ui_t *u, uint8_t scr, uint32_t now_ms)
{
    u->screen = (scr == (uint8_t)UI_SCR_CLOCK) ? (uint8_t)UI_SCR_CLOCK : (uint8_t)UI_SCR_STOCK;
    u->t_scr  = now_ms;
}

void ui_screen_step(ui_t *u, uint32_t now_ms)
{
    uint32_t el = (uint32_t)(now_ms - u->t_scr);
    bool usable = (u->cview.clock_min <= (uint16_t)CLK_MINUTES_MAX);

    if (u->screen == (uint8_t)UI_SCR_CLOCK) {
        if (!usable || (el >= (uint32_t)APP_DWELL_CLOCK_MS)) {
            ui_set_screen(u, (uint8_t)UI_SCR_STOCK, now_ms);
        }
    } else if (usable && (el >= (uint32_t)APP_DWELL_STOCK_MS)) {
        ui_set_screen(u, (uint8_t)UI_SCR_CLOCK, now_ms);        /* 使えない間は t_scr を動かさず待つ */
    } else {
        /* そのまま */
    }
}

uint8_t ui_pal_id(const ui_t *u)
{
    return (u->screen == (uint8_t)UI_SCR_CLOCK) ? (uint8_t)(1u + clk_view_kind(&u->cview)) : 0u;
}

bool ui_view_changed(const ui_t *u)
{
    if ((u->drawn_valid == 0u) || (u->screen != u->drawn_screen)) {
        return true;
    }
    if (u->screen == (uint8_t)UI_SCR_CLOCK) {
        return memcmp(&u->cview, &u->cdrawn, sizeof(u->cview)) != 0;
    }
    return memcmp(&u->view, &u->drawn, sizeof(u->view)) != 0;
}

void ui_view_mark(ui_t *u)
{
    if (u->screen == (uint8_t)UI_SCR_CLOCK) {
        u->cdrawn = u->cview;
    } else {
        u->drawn = u->view;
    }
    u->drawn_screen = u->screen;
    u->drawn_valid = 1u;
}
