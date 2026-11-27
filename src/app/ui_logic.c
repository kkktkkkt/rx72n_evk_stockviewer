/* ui_logic.c - 株価ビューの画面の状態(標準Cのみ。docs/design.md §6) */
#include <string.h>
#include "ui_logic.h"

enum { TS_IDLE = 0, TS_PEND, TS_HELD, TS_BLOCK };    /* 待機 / 押下確定待ち / 押下中 / 無視して離し待ち */

static void ev_push(ui_t *u, ui_ev_type_t type, ui_src_t src)
{
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
    (void)now_ms;
    memset(u, 0, sizeof(*u));
    u->view.net       = (uint8_t)STK_NET_OFF;
    u->view.clock_min = (uint16_t)STK_NONE16;
    u->view.upd_min   = (uint16_t)STK_NONE16;
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

bool ui_view_changed(const ui_t *u)
{
    return (u->drawn_valid == 0u) || (memcmp(&u->view, &u->drawn, sizeof(u->view)) != 0);
}

void ui_view_mark(ui_t *u)
{
    u->drawn = u->view;
    u->drawn_valid = 1u;
}
