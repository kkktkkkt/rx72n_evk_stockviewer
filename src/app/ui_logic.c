/* ui_logic.c - 標準Cのみ(FITを include しない)。仕様: docs/design.md v2 */
#include <string.h>
#include "ui_logic.h"

enum { TS_IDLE = 0, TS_PEND, TS_HELD, TS_BLOCK };    /* 実タッチの状態(待機 / 押下確定待ち / 押下中 / 無視して離し待ち) */

/* ---- デモ台本(§6.2): 仮想タッチの押下時刻(デモ開始からの ms)とボタン。最後の NONE が終了 ---- */
typedef struct { uint16_t t_ms; uint8_t btn; } demo_step_t;
static const demo_step_t DEMO_SCRIPT[] = {
    { 800u,  UI_BTN_H_UP },    { 1500u,  UI_BTN_H_UP },    { 2200u,  UI_BTN_H_DN },
    { 3200u, UI_BTN_H_FILL },  { 12800u, UI_BTN_F_DONE },  { 14200u, UI_BTN_H_REHEAT },
    { 20800u, UI_BTN_R_DONE }, { 22400u, UI_BTN_H_USAGE }, { 27000u, UI_BTN_U_BACK },
    { 28600u, UI_BTN_H_POWER }, { 31750u, UI_BTN_S_WAKE }, { (uint16_t)UI_DEMO_END_MS, UI_BTN_NONE }
};
#define DEMO_N ((uint8_t)(sizeof(DEMO_SCRIPT) / sizeof(DEMO_SCRIPT[0])))

static const int8_t FLOW_T[16] = { 0, 7, 13, 17, 18, 17, 13, 7, 0, -7, -13, -17, -18, -17, -13, -7 };

/* ------------------------------------------------------------------ 式 */
uint16_t ui_fill_x10(uint32_t e_ms, uint32_t speed)
{
    uint32_t v;

    if (e_ms > 3600000u) {
        e_ms = 3600000u;                 /* 桁あふれ防止。32秒で 100% なので影響しない */
    }
    v = e_ms * speed / 32u;
    return (uint16_t)((v > 1000u) ? 1000u : v);
}

uint16_t ui_bath_x10(uint32_t e_ms, uint32_t speed, uint8_t set)
{
    uint32_t v, lim = (uint32_t)set * 10u;

    if (e_ms > 3600000u) {
        e_ms = 3600000u;
    }
    v = UI_REHEAT_START_X10 + e_ms * speed / 400u;
    return (uint16_t)((v > lim) ? lim : v);
}

uint16_t ui_flow_x10(uint32_t e_ms)
{
    return (uint16_t)(85 + FLOW_T[(e_ms / 800u) & 15u]);
}

uint16_t ui_clock_min(uint32_t now_ms)
{
    return (uint16_t)((21u * 60u + 45u + now_ms / 60000u) % 1440u);
}

uint16_t ui_fill_remain_min(uint16_t fill_x10)
{
    uint32_t pct = (uint32_t)fill_x10 / 10u;

    return (uint16_t)(((100u - ((pct > 100u) ? 100u : pct)) * 8u + 99u) / 100u);
}

uint16_t ui_reheat_remain_min(uint16_t bath_x10, uint8_t set)
{
    uint32_t lim = (uint32_t)set * 10u;

    if (bath_x10 >= lim) {
        return 0u;
    }
    return (uint16_t)(((lim - bath_x10) * 15u + 99u) / 100u);
}

uint16_t ui_fill_w(uint16_t fill_x10)
{
    return (uint16_t)(432u * ((fill_x10 > 1000u) ? 1000u : fill_x10) / 1000u);
}

uint16_t ui_reheat_w(uint16_t bath_x10, uint8_t set)
{
    uint32_t lim = (uint32_t)set * 10u;

    if ((bath_x10 >= lim) || (lim <= 360u)) {
        return 432u;
    }
    return (uint16_t)(432u * ((bath_x10 > 360u) ? (bath_x10 - 360u) : 0u) / (lim - 360u));
}

uint16_t ui_bar_h(uint8_t i, uint32_t e_ms)
{
    uint32_t e, u;

    if (i >= UI_USAGE_N) {
        return 0u;
    }
    e = (e_ms > (40u * i)) ? (e_ms - 40u * i) : 0u;
    u = 256u - ((e >= 400u) ? 256u : (e * 256u / 400u));
    return (uint16_t)((uint32_t)ui_usage_h[i] * (65536u - u * u) >> 16);
}

uint16_t ui_slide_dx(uint32_t e_ms)
{
    uint32_t u;

    if (e_ms >= UI_TR_MS) {
        return 480u;
    }
    u = 256u - (e_ms * 256u / UI_TR_MS);
    return (uint16_t)(480u * (65536u - u * u) >> 16);
}

void ui_touch_map(const ui_t *ui, uint16_t raw_x, uint16_t raw_y, int32_t *px, int32_t *py)
{
    uint32_t xm = (ui->x_max == 0u) ? 1u : ui->x_max;
    uint32_t ym = (ui->y_max == 0u) ? 1u : ui->y_max;
    uint32_t a = (raw_x > xm) ? xm : raw_x;
    uint32_t b = (raw_y > ym) ? ym : raw_y;
    int32_t  x, y;

    if ((ui->flags & UI_TF_SWAP_XY) != 0u) {
        x = (int32_t)(b * 479u / ym);       /* 生 Y 軸が画面の横方向 */
        y = (int32_t)(a * 271u / xm);
    } else {
        x = (int32_t)(a * 479u / xm);
        y = (int32_t)(b * 271u / ym);
    }
    if ((ui->flags & UI_TF_FLIP_X) != 0u) {
        x = 479 - x;
    }
    if ((ui->flags & UI_TF_FLIP_Y) != 0u) {
        y = 271 - y;
    }
    *px = x;
    *py = y;
}

/* ------------------------------------------------------------------ イベント */
static void ev_push(ui_t *u, ui_ev_type_t type, uint8_t a, uint8_t b, uint8_t src)
{
    if (u->ev_n >= UI_EV_Q) {
        u->ev_lost++;                    /* 満杯なら新しいものを捨てる */
        return;
    }
    u->ev[u->ev_h].type = (uint8_t)type;
    u->ev[u->ev_h].a    = a;
    u->ev[u->ev_h].b    = b;
    u->ev[u->ev_h].src  = src;
    u->ev_h = (uint8_t)((u->ev_h + 1u) % UI_EV_Q);
    u->ev_n++;
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

/* ------------------------------------------------------------------ 値 */
static bool reheat_done(const ui_vals_t *v)
{
    return v->bath_x10 >= ((uint16_t)v->set * 10u);
}

static void values_step(ui_t *u, uint32_t now)
{
    uint32_t e = now - u->enter_ms;

    u->v.clock_min = (u->wx.clk_ext != 0u) ? u->wx.clock_min : ui_clock_min(now);
    u->spin = (u->wx.busy != 0u) ? (uint8_t)((now / UI_SPIN_MS) & 3u) : 0u;
    switch (u->v.scr) {
    case UI_SCR_FILL:
        u->v.fill_x10 = ui_fill_x10(e, u->speed);
        if ((u->fill_sent == 0u) && (u->v.fill_x10 >= 1000u)) {
            u->fill_sent = 1u;
            ev_push(u, UI_EV_DONE, UI_DONE_FILL, 0u, 0u);
        }
        break;
    case UI_SCR_REHEAT:
        u->v.bath_x10 = ui_bath_x10(e, u->speed, u->v.set);
        if ((u->reheat_sent == 0u) && reheat_done(&u->v)) {
            u->reheat_sent = 1u;
            ev_push(u, UI_EV_DONE, UI_DONE_REHEAT, 0u, 0u);
        }
        break;
    case UI_SCR_USAGE:
        u->v.flow_x10 = ui_flow_x10(e);
        u->v.bar_ms   = (uint16_t)((e > UI_BAR_END_MS) ? UI_BAR_END_MS : e);
        break;
    default:
        break;
    }
}

/* 画面を変える。instant = スライドなし。待機への出入りは常に即切替 */
static void goto_screen(ui_t *u, uint8_t to, ui_src_t src, uint32_t now, bool instant)
{
    uint8_t from = u->v.scr;

    if (to == from) {
        return;
    }
    u->ov       = u->v;                  /* 古い画面は動き始めの値で止める */
    u->v.scr    = to;
    u->enter_ms = now;
    if (to == UI_SCR_FILL) {
        u->v.fill_x10 = 0u;
        u->fill_sent  = 0u;
    } else if (to == UI_SCR_REHEAT) {
        u->v.bath_x10 = UI_REHEAT_START_X10;
        u->reheat_sent = 0u;
    } else if (to == UI_SCR_USAGE) {
        u->v.bar_ms = 0u;
    }
    if (instant || (from == UI_SCR_STANDBY) || (to == UI_SCR_STANDBY)) {
        u->tr_on = 0u;
    } else {
        u->tr_on    = 1u;
        u->tr_start = now;
        u->tr_dx    = 0u;
        u->tr_fwd   = (to > from) ? 1u : 0u;
    }
    ev_push(u, UI_EV_SCR, to, from, (uint8_t)src);
    values_step(u, now);
}

/* アニメーションなしでホームへ(デモの開始・中断) */
static void home_instant(ui_t *u, ui_src_t src, uint32_t now)
{
    u->tr_on = 0u;                       /* 進行中のスライドも取り消す(ホームへ戻るスライド中でも即ホーム) */
    if (u->v.scr != UI_SCR_HOME) {
        u->fb_phase = UI_PH_NONE;
        u->fb_btn   = UI_BTN_NONE;
        goto_screen(u, UI_SCR_HOME, src, now, true);
    }
}

/* ------------------------------------------------------------------ デモ */
static void demo_start(ui_t *u, ui_src_t src, uint32_t now)
{
    if (u->demo != 0u) {
        return;
    }
    u->demo    = 1u;
    u->demo_t0 = now;
    u->demo_i  = 0u;
    u->vt_on   = 0u;
    if (u->fb_phase == UI_PH_PRESS) {    /* 開始前の押下(UART の + - など)は、離す人がいなくなるので取り消す。FLASH は残す */
        u->fb_phase = UI_PH_NONE;
        u->fb_btn   = UI_BTN_NONE;
    }
    ev_push(u, UI_EV_DEMO, UI_DEMO_START, 0u, (uint8_t)src);
    home_instant(u, src, now);
    u->speed      = (uint8_t)UI_DEMO_SPEED;
    u->v.set      = (uint8_t)UI_SET_INIT;
    u->v.bath_x10 = (uint16_t)UI_BATH_INIT_X10;
}

static void demo_abort(ui_t *u, ui_src_t src, uint32_t now)
{
    if (u->demo == 0u) {
        return;
    }
    u->demo  = 0u;
    u->speed = 1u;
    u->vt_on = 0u;
    u->fb_phase = UI_PH_NONE;            /* 仮想押下の途中で中断しても PRESS を残さない(3秒後の強制実行を防ぐ) */
    u->fb_btn   = UI_BTN_NONE;
    ev_push(u, UI_EV_DEMO, UI_DEMO_ABORT, 0u, (uint8_t)src);
    home_instant(u, src, now);
}

/* ------------------------------------------------------------------ ボタン */
static bool cond_ok(const ui_t *u, uint8_t cond)
{
    switch (cond) {
    case UI_COND_FILL_RUN:    return u->v.fill_x10 < 1000u;
    case UI_COND_FILL_DONE:   return u->v.fill_x10 >= 1000u;
    case UI_COND_REHEAT_RUN:  return !reheat_done(&u->v);
    case UI_COND_REHEAT_DONE: return reheat_done(&u->v);
    default:                  return true;
    }
}

static uint8_t hit_test(const ui_t *u, int32_t x, int32_t y)
{
    uint8_t b;
    const ui_btn_def_t *d;

    for (b = 1u; b < (uint8_t)UI_BTN_COUNT; b++) {
        d = &ui_btn_defs[b];
        if ((d->scr == u->v.scr) && cond_ok(u, d->cond) &&
            (x >= d->hit.x) && (x < (d->hit.x + d->hit.w)) && (y >= d->hit.y) && (y < (d->hit.y + d->hit.h))) {
            return b;
        }
    }
    return UI_BTN_NONE;
}

static void set_add(ui_t *u, int32_t d, uint8_t src)
{
    int32_t n = (int32_t)u->v.set + d;

    if ((n < (int32_t)UI_SET_MIN) || (n > (int32_t)UI_SET_MAX)) {
        return;                          /* 上下限では変わらない(イベントも出さない) */
    }
    u->v.set = (uint8_t)n;
    ev_push(u, UI_EV_SET, (uint8_t)n, 0u, src);
}

/* ボタンの動作(§3.2)。押下表示は FLASH に移る */
static void act(ui_t *u, uint32_t now)
{
    uint8_t     b   = u->fb_btn;
    ui_src_t    src = (ui_src_t)u->fb_src;

    u->fb_phase   = UI_PH_FLASH;
    u->fb_t_flash = now;
    ev_push(u, UI_EV_BTN_ACT, b, 0u, (uint8_t)src);
    switch (b) {
    case UI_BTN_H_DN:     set_add(u, -1, (uint8_t)src); break;
    case UI_BTN_H_UP:     set_add(u, 1, (uint8_t)src); break;
    case UI_BTN_H_FILL:   goto_screen(u, UI_SCR_FILL, src, now, false); break;
    case UI_BTN_H_REHEAT: goto_screen(u, UI_SCR_REHEAT, src, now, false); break;
    case UI_BTN_H_USAGE:  goto_screen(u, UI_SCR_USAGE, src, now, false); break;
    case UI_BTN_H_POWER:  goto_screen(u, UI_SCR_STANDBY, src, now, false); break;
    case UI_BTN_H_DEMO:   demo_start(u, src, now); break;
    case UI_BTN_F_DONE:
    case UI_BTN_F_HOME: {
        bool full = (u->v.fill_x10 >= 1000u);
        goto_screen(u, UI_SCR_HOME, src, now, false);
        if (full) {
            u->v.bath_x10 = (uint16_t)UI_BATH_INIT_X10;       /* 湯はり完了で浴槽は 40.0℃ */
        }
        break;
    }
    case UI_BTN_F_STOP:
    case UI_BTN_R_STOP:
    case UI_BTN_R_DONE:
    case UI_BTN_R_HOME:
    case UI_BTN_U_BACK:   goto_screen(u, UI_SCR_HOME, src, now, false); break;
    default:              break;
    }
}

/* 押下が確定した。押された(受け付けた)ボタン番号を返す。受け付けなければ 0 */
static uint8_t press_confirmed(ui_t *u, int32_t x, int32_t y, ui_src_t src, uint32_t now)
{
    uint8_t b;

    if ((src == UI_SRC_TOUCH) && (u->demo != 0u)) {
        demo_abort(u, src, now);         /* 実タッチはデモを止める。そのタッチは動作にしない */
        return UI_BTN_NONE;
    }
    if ((u->tr_on != 0u) || (u->fb_phase == UI_PH_PRESS)) {
        return UI_BTN_NONE;
    }
    b = hit_test(u, x, y);
    if (b == UI_BTN_NONE) {
        return UI_BTN_NONE;
    }
    ev_push(u, UI_EV_BTN_DOWN, b, 0u, (uint8_t)src);
    if (b == UI_BTN_S_WAKE) {            /* 待機中のタップは押下確定で復帰(押下表示なし) */
        goto_screen(u, UI_SCR_HOME, src, now, true);
        return b;
    }
    u->fb_btn    = b;
    u->fb_phase  = UI_PH_PRESS;
    u->fb_src    = (uint8_t)src;
    u->fb_rel    = 0u;
    u->fb_t_down = now;
    return b;
}

static void release_confirmed(ui_t *u, ui_src_t src)
{
    if ((u->fb_phase == UI_PH_PRESS) && (u->fb_src == (uint8_t)src)) {
        u->fb_rel = 1u;
    }
}

/* 仮想タッチ: ボタンの中心を UI_VT_HOLD_MS だけ押す。受け付けたボタン番号を返す */
static uint8_t vt_tap(ui_t *u, uint8_t btn, ui_src_t src, uint32_t now)
{
    const ui_btn_def_t *d = &ui_btn_defs[btn];
    uint8_t got = press_confirmed(u, d->vis.x + d->vis.w / 2, d->vis.y + d->vis.h / 2, src, now);

    if (got != UI_BTN_NONE) {
        u->vt_on     = 1u;
        u->vt_src    = (uint8_t)src;
        u->vt_rel_at = now + UI_VT_HOLD_MS;
    }
    return got;
}

/* ------------------------------------------------------------------ 周期処理 */
static void touch_step(ui_t *u, const nav_sample_t *s, uint32_t now)
{
    int32_t px, py;
    bool    down  = s->pressed;
    bool    multi = down && (s->points >= 2u);

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
            if (u->tcnt >= UI_TOUCH_DOWN_N) {
                u->ts = TS_HELD;
                u->tcnt = 0u;
                ui_touch_map(u, s->x, s->y, &px, &py);
                (void)press_confirmed(u, px, py, UI_SRC_TOUCH, now);
            }
        }
    } else if (down) {                   /* HELD / BLOCK: 触れている間は離しのカウントを戻す */
        u->tcnt = 0u;
    } else {
        u->tcnt++;
        if (u->tcnt >= UI_TOUCH_UP_N) {
            if (u->ts == TS_HELD) {
                release_confirmed(u, UI_SRC_TOUCH);
            }
            u->ts = TS_IDLE;
            u->tcnt = 0u;
        }
    }
}

static void demo_step(ui_t *u, uint32_t now)
{
    uint8_t want;

    if ((u->vt_on != 0u) && ((int32_t)(now - u->vt_rel_at) >= 0)) {
        u->vt_on = 0u;
        release_confirmed(u, (ui_src_t)u->vt_src);
    }
    while ((u->demo != 0u) && (u->demo_i < DEMO_N) && ((now - u->demo_t0) >= DEMO_SCRIPT[u->demo_i].t_ms)) {
        want = DEMO_SCRIPT[u->demo_i].btn;
        u->demo_i++;
        if (want == UI_BTN_NONE) {       /* 台本の終わり: ホームで止まる */
            u->demo  = 0u;
            u->speed = 1u;
            ev_push(u, UI_EV_DEMO, UI_DEMO_END, 0u, (uint8_t)UI_SRC_DEMO);
        } else if (vt_tap(u, want, UI_SRC_DEMO, now) != want) {
            demo_abort(u, UI_SRC_DEMO, now);   /* 台本どおりに押せない */
        }
    }
}

static void fb_step(ui_t *u, uint32_t now)
{
    uint32_t held;

    if (u->fb_phase == UI_PH_PRESS) {
        held = now - u->fb_t_down;
        if (((u->fb_rel != 0u) && (held >= UI_PRESS_MIN_MS)) || (held >= UI_HOLD_MAX_MS)) {
            act(u, now);
        }
    } else if ((u->fb_phase == UI_PH_FLASH) && ((now - u->fb_t_flash) >= UI_FLASH_MS)) {
        u->fb_phase = UI_PH_NONE;
        u->fb_btn   = UI_BTN_NONE;
    }
}

static void kv_make(const ui_vals_t *v, ui_kv_t *k)
{
    memset(k, 0, sizeof(*k));
    k->scr = v->scr;
    switch (v->scr) {
    case UI_SCR_HOME:
        k->w[0] = v->clock_min;
        k->w[1] = v->set;
        k->w[2] = (uint16_t)(v->bath_x10 / 10u);
        break;
    case UI_SCR_FILL:
        k->w[0] = (uint16_t)(v->bath_x10 / 10u);
        k->w[1] = ui_fill_w(v->fill_x10);
        k->w[2] = (uint16_t)(v->fill_x10 / 10u);
        break;
    case UI_SCR_REHEAT:
        k->w[0] = v->bath_x10;
        k->w[1] = v->set;
        k->w[2] = ui_reheat_w(v->bath_x10, v->set);
        break;
    case UI_SCR_USAGE:
        k->w[0] = v->flow_x10;
        k->w[1] = v->bar_ms;
        break;
    default:
        k->w[0] = v->clock_min;
        break;
    }
}

static void key_make(ui_t *u)
{
    memset(&u->key, 0, sizeof(u->key));
    u->key.wx   = u->wx;
    u->key.spin = u->spin;
    kv_make(&u->v, &u->key.cur);
    if (u->tr_on != 0u) {
        kv_make(&u->ov, &u->key.old);
    }
    u->key.dx       = (u->tr_on != 0u) ? u->tr_dx : 0u;
    u->key.tr_on    = u->tr_on;
    u->key.tr_fwd   = u->tr_fwd;
    u->key.fb_btn   = (u->fb_phase == UI_PH_NONE) ? 0u : u->fb_btn;
    u->key.fb_phase = u->fb_phase;
    u->key.demo     = u->demo;
}

/* ------------------------------------------------------------------ 公開 */
void ui_init(ui_t *u, uint32_t now_ms, uint16_t x_max, uint16_t y_max, uint8_t flags)
{
    memset(u, 0, sizeof(*u));
    u->v.scr       = UI_SCR_HOME;
    u->v.set       = (uint8_t)UI_SET_INIT;
    u->v.bath_x10  = (uint16_t)UI_BATH_INIT_X10;
    u->v.flow_x10  = 85u;
    u->speed       = 1u;
    u->enter_ms    = now_ms;
    u->x_max       = x_max;
    u->y_max       = y_max;
    u->flags       = flags;
    u->wx.upd_min   = 0xFFFFu;           /* 未取得・未同期 */
    u->wx.clock_min = 0xFFFFu;
    values_step(u, now_ms);
    key_make(u);
}

void ui_update(ui_t *u, const nav_sample_t *real, uint32_t now)
{
    if (real != NULL) {
        touch_step(u, real, now);
    }
    demo_step(u, now);
    fb_step(u, now);
    if (u->tr_on != 0u) {
        if ((now - u->tr_start) >= UI_TR_MS) {
            u->tr_on = 0u;
            u->tr_dx = 480u;
        } else {
            u->tr_dx = ui_slide_dx(now - u->tr_start);
        }
    }
    values_step(u, now);
    key_make(u);
}

void ui_cmd(ui_t *u, ui_cmd_t cmd, uint32_t now)
{
    uint8_t to;

    switch (cmd) {
    case UI_CMD_DEMO:
        if (u->demo != 0u) {
            demo_abort(u, UI_SRC_UART, now);
        } else {
            demo_start(u, UI_SRC_UART, now);
        }
        break;
    case UI_CMD_NEXT:
    case UI_CMD_PREV:
        if ((u->demo != 0u) || (u->tr_on != 0u) || (u->fb_phase == UI_PH_PRESS)) {
            break;
        }
        to = (cmd == UI_CMD_NEXT) ? (uint8_t)((u->v.scr + 1u) % UI_SCR_COUNT)
                                  : (uint8_t)((u->v.scr + UI_SCR_COUNT - 1u) % UI_SCR_COUNT);
        goto_screen(u, to, UI_SRC_UART, now, false);
        break;
    case UI_CMD_PLUS:
    case UI_CMD_MINUS:
        if ((u->demo == 0u) && (u->v.scr == UI_SCR_HOME)) {
            (void)vt_tap(u, (cmd == UI_CMD_PLUS) ? (uint8_t)UI_BTN_H_UP : (uint8_t)UI_BTN_H_DN, UI_SRC_UART, now);
        }
        break;
    case UI_CMD_WX:                      /* 更新ボタンの仮想タップ。遷移中・押下表示中は press_confirmed が断る */
        if ((u->demo == 0u) && (u->v.scr == UI_SCR_HOME)) {
            (void)vt_tap(u, (uint8_t)UI_BTN_H_WX, UI_SRC_UART, now);
        }
        break;
    default:
        break;
    }
}

void ui_set_wx(ui_t *u, const ui_wx_t *w)
{
    u->wx     = *w;
    u->wx.rsv = 0u;
}

bool ui_view_changed(const ui_t *u)
{
    return (u->drawn_valid == 0u) || (memcmp(&u->key, &u->drawn, sizeof(u->key)) != 0);
}

void ui_view_mark(ui_t *u)
{
    memcpy(&u->drawn, &u->key, sizeof(u->key));
    u->drawn_valid = 1u;
}
