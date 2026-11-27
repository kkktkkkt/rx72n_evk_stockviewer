/* ui_render.c - 標準Cのみ(FITを include しない) */
#include <string.h>
#include "gfx.h"
#include "ui_render.h"

/* 文字 -> 数字フォントの番号(0-9 : . % - ( ) と、単独バイト \x01..\x07 = 月日火水木金土)。無ければ -1。( ) と漢字は F14 だけ(日付) */
static int glyph_index(char c)
{
    unsigned char u = (unsigned char)c;

    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    if ((u >= 1u) && (u <= 7u)) {
        return (int)UI_G_MON + (int)u - 1;
    }
    switch (c) {
    case ':': return 10;
    case '.': return 11;
    case '%': return 12;
    case '-': return (int)UI_G_MINUS;
    case '(': return (int)UI_G_LPAR;
    case ')': return (int)UI_G_RPAR;
    default:  return -1;
    }
}

/* sel = 2つ目の表を使う(alt < 0 のスロットだけ。HOME_TEMP の古い値の灰色) */
static void draw_text(uint8_t *fb, uint8_t slot, const char *s, int32_t xo, uint8_t sel)
{
    const ui_slot_t  *sl = &ui_slots[slot];
    const ui_glyph_t *g  = ui_glyphs[sl->font];
    int32_t x = sl->x + xo;
    int     i, k;

    if (sl->align != UI_ALIGN_LEFT) {
        int32_t w = 0;
        for (i = 0; s[i] != '\0'; i++) {
            k = glyph_index(s[i]);
            w += (k >= 0) ? g[k].adv : 0;
        }
        x -= (sl->align == UI_ALIGN_CENTER) ? (w / 2) : w;
    }
    for (i = 0; s[i] != '\0'; i++) {
        k = glyph_index(s[i]);
        if (k < 0) {
            continue;
        }
        gfx_a8_ramp(fb, &ui_glyph_pool[g[k].off], g[k].w, g[k].h, x + g[k].bx, sl->y - g[k].by,
                    ui_slot_ramp[slot][((sel != 0u) && (ui_slot_alt[slot] < 0)) ? 1 : 0], ui_slot_ramp[slot][1], ui_slot_alt[slot]);
        x += g[k].adv;
    }
}

/* ---- 文字列の組み立て(桁数は固定、または可変。\0 終端) ---- */
static char *put2(char *p, uint32_t n)
{
    *p++ = (char)('0' + (n / 10u) % 10u);
    *p++ = (char)('0' + n % 10u);
    return p;
}

static char *putn(char *p, uint32_t n)
{
    if (n >= 100u) {
        *p++ = (char)('0' + (n / 100u) % 10u);
    }
    if (n >= 10u) {
        *p++ = (char)('0' + (n / 10u) % 10u);
    }
    *p++ = (char)('0' + n % 10u);
    return p;
}

static void num2(uint8_t *fb, uint8_t slot, uint32_t n, int32_t xo)
{
    char b[4];
    *put2(b, n) = '\0';
    draw_text(fb, slot, b, xo, 0u);
}

static void numn(uint8_t *fb, uint8_t slot, uint32_t n, const char *suffix, int32_t xo)
{
    char b[8];
    char *p = putn(b, n);
    while (*suffix != '\0') {
        *p++ = *suffix++;
    }
    *p = '\0';
    draw_text(fb, slot, b, xo, 0u);
}

/* 時刻(分)。1440 以上(0xFFFF = 未同期・未取得)は --:-- */
static void clock_text(uint8_t *fb, uint8_t slot, uint32_t min, int32_t xo)
{
    char b[8];
    char *p;

    if (min >= 1440u) {
        b[0] = '-'; b[1] = '-'; b[2] = ':'; b[3] = '-'; b[4] = '-'; b[5] = '\0';
    } else {
        p = put2(b, min / 60u);
        *p++ = ':';
        *put2(p, min % 60u) = '\0';
    }
    draw_text(fb, slot, b, xo, 0u);
}

static void tenths(uint8_t *fb, uint8_t slot, uint32_t x10, int32_t xo)
{
    char b[8];
    char *p = putn(b, x10 / 10u);
    *p++ = '.';
    *p++ = (char)('0' + x10 % 10u);
    *p = '\0';
    draw_text(fb, slot, b, xo, 0u);
}

/* ---- ホームの天気の行と Wi-Fi アイコン(design §22.8) ---- */
static void put_spr(uint8_t *fb, const uint8_t *spr, int32_t w, int32_t h, int32_t x, int32_t y, int32_t xo)
{
    gfx_blit8(fb, spr, w, 0, 0, w, h, x + xo, y);
}

/* 日付「M月D日(曜)」。clk_ext = 0 は旧デモの 11月21日(土)。未同期(month = 0)や範囲外は描かない */
static void date_text(uint8_t *fb, const ui_wx_t *w, int32_t xo)
{
    static const char WD[7] = { 2, 1, 3, 4, 5, 6, 7 };     /* 日 月 火 水 木 金 土 の文字コード */
    char    b[16];
    char   *p;
    uint8_t m = 11u, d = 21u, wd = 6u;

    if (w->clk_ext != 0u) {
        m = w->month; d = w->day; wd = w->wday;
        if ((m < 1u) || (m > 12u) || (d < 1u) || (d > 31u) || (wd > 6u)) {
            return;
        }
    }
    p = putn(b, m);
    *p++ = 1;                                                /* 月 */
    p = putn(p, d);
    *p++ = 2;                                                /* 日 */
    *p++ = '(';
    *p++ = WD[wd];
    *p++ = ')';
    *p = '\0';
    draw_text(fb, UI_SL_HOME_DATE, b, xo, 0u);
}

/* 気温。未取得は --.-、負は -、絶対値は 99.9 まで */
static void temp_text(uint8_t *fb, const ui_wx_t *w, int32_t xo)
{
    char     b[8];
    char    *p = b;
    int32_t  t = w->temp_x10;
    uint32_t m;

    if (w->have == 0u) {
        b[0] = '-'; b[1] = '-'; b[2] = '.'; b[3] = '-'; b[4] = '\0';
    } else {
        if (t < 0) {
            *p++ = '-';
            t = -t;
        }
        m = (uint32_t)((t > 999) ? 999 : t);
        p = putn(p, m / 10u);
        *p++ = '.';
        *p++ = (char)('0' + m % 10u);
        *p = '\0';
    }
    draw_text(fb, UI_SL_HOME_TEMP, b, xo, (w->stale != 0u) ? 1u : 0u);
}

static void draw_home_wx(uint8_t *fb, const ui_wx_t *w, uint8_t spin, int32_t xo)
{
    uint32_t kind = ((w->have != 0u) && (w->kind < UI_SPR_WX_N)) ? w->kind : (UI_SPR_WX_N - 1u);   /* 未取得・範囲外 = 不明 */

    date_text(fb, w, xo);
    temp_text(fb, w, xo);
    clock_text(fb, UI_SL_HOME_UPD, (w->have != 0u) ? w->upd_min : 0xFFFFu, xo);
    put_spr(fb, ui_spr_wx[kind], UI_WX_W, UI_WX_H, UI_WX_X, UI_WX_Y, xo);
    put_spr(fb, ui_spr_ref[spin & 3u], UI_REF_W, UI_REF_H, UI_REF_X, UI_REF_Y, xo);
}

/* 進捗バー(§2.4): 満タン画像の左から (w - r) 列をコピーし、右端の角丸(A8)を段階の表で重ねる */
static void draw_bar(uint8_t *fb, uint8_t bar, int32_t w, int32_t xo)
{
    const ui_bar_t *b = &ui_bars[bar];

    if (w < b->min_w) {
        w = b->min_w;
    }
    if (w > b->w) {
        w = b->w;
    }
    gfx_blit8(fb, b->full, b->w, 0, 0, w - b->r, b->h, b->x + xo, b->y);
    gfx_a8_ramp(fb, b->cap, b->r, b->h, b->x + xo + w - b->r, b->y, ui_bar_ramp[bar], NULL, -1);
}

/* 使用量の棒(§2.5): 上の2角を簡易に丸める */
static void draw_usage_bars(uint8_t *fb, uint32_t e_ms, int32_t xo)
{
    uint8_t  i;
    int32_t  x, h, y;
    uint8_t  c;

    for (i = 0u; i < (uint8_t)UI_USAGE_N; i++) {
        h = ui_bar_h(i, e_ms);
        if (h <= 0) {
            continue;
        }
        x = UI_USAGE_X0 + UI_USAGE_PITCH * (int32_t)i + xo;
        y = UI_USAGE_BASE - h;
        c = (i == (uint8_t)(UI_USAGE_N - 1u)) ? (uint8_t)UI_I_BAR_U_TODAY : (uint8_t)UI_I_BAR_U;
        if (h >= 3) {
            gfx_fill8(fb, x + 2, y, UI_USAGE_W - 4, 1, c);
            gfx_fill8(fb, x + 1, y + 1, UI_USAGE_W - 2, 1, c);
            gfx_fill8(fb, x, y + 2, UI_USAGE_W, h - 2, c);
        } else {
            gfx_fill8(fb, x, y, UI_USAGE_W, h, c);
        }
    }
}

static void draw_screen(uint8_t *fb, const ui_vals_t *v, const ui_wx_t *w, uint8_t spin, int32_t xo)
{
    bool done;

    switch (v->scr) {
    case UI_SCR_HOME:
        gfx_blit8(fb, ui_bgi[UI_BG_HOME], UI_W, 0, 0, UI_W, UI_H, xo, 0);
        clock_text(fb, UI_SL_HOME_CLOCK, v->clock_min, xo);
        num2(fb, UI_SL_HOME_SET, v->set, xo);
        num2(fb, UI_SL_HOME_BATH, v->bath_x10 / 10u, xo);
        draw_home_wx(fb, w, spin, xo);
        break;
    case UI_SCR_FILL:
        done = (v->fill_x10 >= 1000u);
        gfx_blit8(fb, ui_bgi[done ? UI_BG_FILL_DONE : UI_BG_FILL_RUN], UI_W, 0, 0, UI_W, UI_H, xo, 0);
        num2(fb, UI_SL_FILL_BATH, v->bath_x10 / 10u, xo);
        draw_bar(fb, UI_BAR_FILL, ui_fill_w(v->fill_x10), xo);
        numn(fb, UI_SL_FILL_PCT, v->fill_x10 / 10u, "%", xo);
        if (!done) {
            numn(fb, UI_SL_FILL_REMAIN, ui_fill_remain_min(v->fill_x10), "", xo);
        }
        break;
    case UI_SCR_REHEAT:
        done = (v->bath_x10 >= ((uint32_t)v->set * 10u));
        gfx_blit8(fb, ui_bgi[done ? UI_BG_REHEAT_DONE : UI_BG_REHEAT_RUN], UI_W, 0, 0, UI_W, UI_H, xo, 0);
        num2(fb, UI_SL_REHEAT_SETHDR, v->set, xo);
        tenths(fb, UI_SL_REHEAT_BATH, v->bath_x10, xo);
        num2(fb, UI_SL_REHEAT_SET, v->set, xo);
        draw_bar(fb, UI_BAR_REHEAT, ui_reheat_w(v->bath_x10, v->set), xo);
        if (!done) {
            numn(fb, UI_SL_REHEAT_REMAIN, ui_reheat_remain_min(v->bath_x10, v->set), "", xo);
        }
        break;
    case UI_SCR_USAGE:
        gfx_blit8(fb, ui_bgi[UI_BG_USAGE], UI_W, 0, 0, UI_W, UI_H, xo, 0);
        tenths(fb, UI_SL_USAGE_FLOW, v->flow_x10, xo);
        draw_usage_bars(fb, v->bar_ms, xo);
        break;
    default:
        gfx_blit8(fb, ui_bgi[UI_BG_STBY], UI_W, 0, 0, UI_W, UI_H, xo, 0);
        clock_text(fb, UI_SL_STBY_CLOCK, v->clock_min, xo);
        return;                          /* 待機には Wi-Fi アイコンを出さない */
    }
    put_spr(fb, ui_spr_wifi[(w->net < UI_SPR_WIFI_N) ? w->net : UI_NET_OFF], UI_WIFI_W, UI_WIFI_H, UI_WIFI_X, UI_WIFI_Y, xo);
}

/* 押下の暗転(黒)/ 離した時の白光。ボタンの角丸の形(A8)が UI_REMAP_TH 以上の画素の添字を置き換える */
static void draw_feedback(uint8_t *fb, const ui_t *u, uint8_t scr, int32_t xo)
{
    const ui_btn_def_t *d;
    const ui_shape_t   *sh;

    if (u->fb_phase == UI_PH_NONE) {
        return;
    }
    d = &ui_btn_defs[u->fb_btn];
    if ((d->scr != scr) || (d->shape < 0)) {
        return;
    }
    sh = &ui_shapes[d->shape];
    if (u->fb_phase == UI_PH_PRESS) {
        gfx_remap8(fb, sh->a8, sh->w, sh->h, d->vis.x + xo, d->vis.y, ui_remap[0], UI_REMAP_TH);
    } else {
        gfx_remap8(fb, sh->a8, sh->w, sh->h, d->vis.x + xo, d->vis.y, ui_remap[1], UI_REMAP_TH);
    }
}

static void draw_one(uint8_t *fb, const ui_t *u, const ui_vals_t *v, int32_t xo)
{
    draw_screen(fb, v, &u->wx, u->spin, xo);
    draw_feedback(fb, u, v->scr, xo);
    if ((u->demo != 0u) && (v->scr != UI_SCR_STANDBY)) {
        gfx_blit8(fb, ui_tag_pix, UI_TAG_W, 0, 0, UI_TAG_W, UI_TAG_H, UI_TAG_X + xo, UI_TAG_Y);
    }
}

void ui_render(const ui_t *u, uint8_t *fb)
{
    int32_t dx;

    if (u->tr_on == 0u) {
        draw_one(fb, u, &u->v, 0);
        return;
    }
    dx = u->tr_dx;                       /* 0..480。前へ: 古い -dx / 新しい 480-dx。戻る: 古い +dx / 新しい dx-480 */
    if (u->tr_fwd != 0u) {
        draw_one(fb, u, &u->ov, -dx);
        draw_one(fb, u, &u->v, UI_W - dx);
    } else {
        draw_one(fb, u, &u->ov, dx);
        draw_one(fb, u, &u->v, dx - UI_W);
    }
}
