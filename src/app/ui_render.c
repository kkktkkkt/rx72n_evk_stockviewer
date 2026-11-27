/* ui_render.c - stk_view_t -> 480x272 の 8bpp(CLUT8)の面(標準Cのみ。docs/design.md §3.9・§3.13)。数値の整形は stk_core の stk_fmt_*
 * 規則は tools/gen_stock_assets.py の render() と同じ(ホスト試験 H-8 で全画素を突き合わせる)。 */
#include <stddef.h>
#include "gfx.h"
#include "ui_assets.h"
#include "stk_core.h"
#include "ui_render.h"

/* 文字 -> グリフ番号(§3.6)。\x01 = ▲、\x02 = ▼、\x03 = −(U+2212)。表に無い字は -1(飛ばす) */
static int glyph_index(char ch)
{
    if ((ch >= '0') && (ch <= '9')) {
        return (int)(ch - '0');
    }
    switch (ch) {
    case ',':    return 10;
    case '.':    return 11;
    case '+':    return 12;
    case '\x03': return 13;
    case '%':    return 14;
    case '\x01': return 15;
    case '\x02': return 16;
    case ':':    return 17;
    case '-':    return 18;
    default:     return -1;
    }
}

/* 文字列を描く(§3.13): RIGHT は x -= Σadv、CENTER は x -= Σadv / 2。各グリフは gfx_a8_ramp(... ramp, ramp, -1) */
static void draw_text(uint8_t *fb, uint8_t font, const char *s, int32_t x, int32_t y, uint8_t align, const uint8_t *ramp)
{
    const ui_glyph_t *gl = ui_glyphs[font];
    const char *p;
    int32_t wd = 0;
    int g;

    for (p = s; *p != '\0'; p++) {
        g = glyph_index(*p);
        if (g >= 0) {
            wd += gl[g].adv;
        }
    }
    if (align == (uint8_t)UI_ALIGN_RIGHT) {
        x -= wd;
    } else if (align == (uint8_t)UI_ALIGN_CENTER) {
        x -= wd / 2;
    } else {
        /* LEFT */
    }
    for (p = s; *p != '\0'; p++) {
        g = glyph_index(*p);
        if (g < 0) {
            continue;
        }
        if (gl[g].w != 0u) {
            gfx_a8_ramp(fb, &ui_glyph_pool[gl[g].off], gl[g].w, gl[g].h, x + gl[g].bx, y - gl[g].by, ramp, ramp, -1);
        }
        x += gl[g].adv;
    }
}

static void fmt_hhmm(char *out, uint16_t m)
{
    if (m > (uint16_t)STK_MINUTES_MAX) {                    /* 0xFFFF = なし、1440〜0xFFFE = 範囲外。どちらも --:-- */
        out[0] = '-'; out[1] = '-'; out[2] = ':'; out[3] = '-'; out[4] = '-'; out[5] = '\0';
        return;
    }
    out[0] = (char)('0' + ((m / 60u) / 10u));
    out[1] = (char)('0' + ((m / 60u) % 10u));
    out[2] = ':';
    out[3] = (char)('0' + ((m % 60u) / 10u));
    out[4] = (char)('0' + ((m % 60u) % 10u));
    out[5] = '\0';
}

/* ---- 表示の決定(§3.9) ---- */
uint8_t ui_row_dir(const stk_row_t *r)
{
    if (r->have == 0u) {
        return (uint8_t)STK_FLAT;
    }
    return (r->pct_bp > 0) ? (uint8_t)STK_UP : ((r->pct_bp < 0) ? (uint8_t)STK_DOWN : (uint8_t)STK_FLAT);   /* 丸めた騰落率の符号(§4.2) */
}

uint8_t ui_foot_index(const stk_view_t *v)
{
    if ((v->net == (uint8_t)STK_NET_OFF) || (v->net == (uint8_t)STK_NET_ERR)) {
        return 4u;
    }
    if (v->have_any == 0u) {
        return 1u;
    }
    if (v->busy != 0u) {
        return 2u;
    }
    if (v->stale_all != 0u) {
        return 3u;
    }
    return (v->partial != 0u) ? 5u : 0u;
}

uint8_t ui_icon_index(const stk_view_t *v)
{
    if ((v->net == (uint8_t)STK_NET_OFF) || (v->net == (uint8_t)STK_NET_ERR)) {
        return 3u;
    }
    if (v->net == (uint8_t)STK_NET_CONN) {
        return 1u;
    }
    return (v->stale_all != 0u) ? 2u : 0u;
}

static void draw_shape(uint8_t *fb, uint8_t shape, int32_t y0, const uint8_t *ramp)
{
    const ui_shape_t *s = &ui_shapes[shape];

    gfx_a8_ramp(fb, s->a8, s->w, s->h, s->x, y0 + s->y, ramp, ramp, -1);
}

static void draw_row(uint8_t *fb, const stk_row_t *r, uint8_t row, uint8_t dir)
{
    int32_t y0 = ui_row_y0[row];
    uint8_t kind = stk_row_kind(row);
    char buf[16];
    const ui_slot_t *sp = &ui_slots[(uint8_t)UI_SL_PRICE0 + row];
    const ui_slot_t *sc = &ui_slots[(uint8_t)UI_SL_PCT0 + row];
    const ui_slot_t *sa = &ui_slots[(uint8_t)UI_SL_AMT0 + row];
    uint8_t k_main, k_acc;

    /* 6: 丸と状態の文字(ramp は行の下地の色 = dir) */
    if (r->mkt == (uint8_t)STK_MKT_TRADING) {
        draw_shape(fb, (uint8_t)UI_SH_DOT, y0, ui_ramp[UI_R_DOTG][dir]);
        draw_shape(fb, (uint8_t)UI_SH_TRADING, y0, ui_ramp[UI_R_SUB][dir]);
    } else if (r->mkt == (uint8_t)STK_MKT_CLOSED) {
        draw_shape(fb, (uint8_t)UI_SH_DOT, y0, ui_ramp[UI_R_STALE][dir]);
        draw_shape(fb, (uint8_t)UI_SH_CLOSED, y0, ui_ramp[UI_R_SUB][dir]);
    } else if (r->fail != 0u) {
        draw_shape(fb, (uint8_t)UI_SH_DOT, y0, ui_ramp[UI_R_STALE][dir]);
        draw_shape(fb, (uint8_t)UI_SH_FAIL, y0, ui_ramp[UI_R_SUB][dir]);
    } else {
        /* 何も描かない */
    }
    /* 7: 現在値・騰落率・金額 */
    if (r->have == 0u) {
        draw_text(fb, sp->font, "---", sp->x, sp->y, sp->align, ui_ramp[UI_R_STALE][dir]);
        draw_text(fb, sc->font, "---", sc->x, sc->y, sc->align, ui_ramp[UI_R_STALE][dir]);
        return;
    }
    k_main = (r->stale != 0u) ? (uint8_t)UI_R_STALE : (uint8_t)UI_R_PRICE;
    k_acc  = (r->stale != 0u) ? (uint8_t)UI_R_STALE : (uint8_t)UI_R_ACC;
    (void)stk_fmt_price(buf, sizeof buf, r->price_x100, kind);
    draw_text(fb, sp->font, buf, sp->x, sp->y, sp->align, ui_ramp[k_main][dir]);
    (void)stk_fmt_pct(buf, sizeof buf, r->pct_bp);
    draw_text(fb, sc->font, buf, sc->x, sc->y, sc->align, ui_ramp[k_acc][dir]);
    (void)stk_fmt_amt(buf, sizeof buf, r->diff_x100, kind, dir);
    draw_text(fb, sa->font, buf, sa->x, sa->y, sa->align, ui_ramp[k_acc][dir]);
}

void ui_render(const ui_t *ui, uint8_t *fb)
{
    const stk_view_t *v = &ui->view;
    uint8_t dir[STK_ROWS];
    char buf[8];
    uint8_t i;

    gfx_blit8(fb, ui_hdr, UI_W, 0, 0, UI_W, UI_HDR_H, 0, 0);                                   /* 1 */
    for (i = 0u; i < (uint8_t)STK_ROWS; i++) {                                                   /* 2 */
        dir[i] = ui_row_dir(&v->row[i]);
        gfx_blit8(fb, ui_strip[i][dir[i]], UI_W, 0, 0, UI_W, UI_ROW_H, 0, ui_row_y0[i]);
    }
    gfx_blit8(fb, ui_foot[ui_foot_index(v)], UI_W, 0, 0, UI_W, UI_FOOT_H, 0, UI_FOOT_Y);        /* 3 */
    gfx_blit8(fb, ui_spr_wifi[ui_icon_index(v)], UI_WIFI_W, 0, 0, UI_WIFI_W, UI_WIFI_H, UI_WIFI_X, UI_WIFI_Y);   /* 4 */
    fmt_hhmm(buf, v->clock_min);                                                                 /* 5 */
    draw_text(fb, ui_slots[UI_SL_CLOCK].font, buf, ui_slots[UI_SL_CLOCK].x, ui_slots[UI_SL_CLOCK].y,
              ui_slots[UI_SL_CLOCK].align, ui_ramp_hdr[UI_RH_CLOCK]);
    fmt_hhmm(buf, v->upd_min);
    draw_text(fb, ui_slots[UI_SL_UPD].font, buf, ui_slots[UI_SL_UPD].x, ui_slots[UI_SL_UPD].y,
              ui_slots[UI_SL_UPD].align, ui_ramp_hdr[UI_RH_UPD]);
    for (i = 0u; i < (uint8_t)STK_ROWS; i++) {                                                   /* 6, 7 */
        draw_row(fb, &v->row[i], i, dir[i]);
    }
}
