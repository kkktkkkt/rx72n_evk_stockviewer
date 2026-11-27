/* ui_clock.c - clk_view_t -> 480x272 の 8bpp(CLUT8)の面(時計・天気画面。標準Cのみ。docs/design.md §13.4 の描画の順)
 * 座標・字の並びは生成物(clk_assets.h / clk_data.c)の定数だけを使う。規則は tools/gen_clock_assets.py の render() と同じ
 * (ホスト試験 H-C7 ⑤ で C0〜C11 の全画素を突き合わせる)。 */
#include <stddef.h>
#include "gfx.h"
#include "clk_core.h"
#include "clk_assets.h"
#include "ui_render.h"

enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1 };

/* 字番号の並びの送り幅の合計 */
static int32_t seq_width(const ui_glyph_t *gl, const uint8_t *ids, uint8_t n)
{
    int32_t wd = 0;
    uint8_t i;

    for (i = 0u; i < n; i++) {
        wd += (int32_t)gl[ids[i]].adv;
    }
    return wd;
}

/* 字番号の並びを描く(ui_render.c の draw_text と同じ規則): CENTER は x -= Σadv / 2。各字は gfx_a8_ramp(... ramp, ramp, -1)。戻り値 = 送り幅の合計 */
static int32_t draw_seq(uint8_t *fb, const ui_glyph_t *gl, const uint8_t *ids, uint8_t n, int32_t x, int32_t y, uint8_t align, const uint8_t *ramp)
{
    int32_t wd = seq_width(gl, ids, n);
    const ui_glyph_t *g;
    uint8_t i;

    if (align == (uint8_t)ALIGN_CENTER) {
        x -= wd / 2;
    }
    for (i = 0u; i < n; i++) {
        g = &gl[ids[i]];
        if (g->w != 0u) {
            gfx_a8_ramp(fb, &clk_glyph_pool[g->off], g->w, g->h, x + g->bx, y - g->by, ramp, ramp, -1);
        }
        x += (int32_t)g->adv;
    }
    return wd;
}

static void draw_time(uint8_t *fb, const clk_view_t *v)
{
    uint8_t ids[CLK_SEQ_TIME_MAX];
    uint8_t n = clk_seq_time(ids, v);

    (void)draw_seq(fb, clk_g124, ids, n, CLK_CX, CLK_TIME_BASE, (uint8_t)ALIGN_CENTER, clk_ramp[CLK_R_BGW]);
}

static void draw_date(uint8_t *fb, const clk_view_t *v)
{
    uint8_t ids[CLK_SEQ_DATE_MAX];
    uint8_t n = clk_seq_date(ids, v);

    (void)draw_seq(fb, clk_g20, ids, n, CLK_CX, CLK_DATE_BASE, (uint8_t)ALIGN_CENTER, clk_ramp[CLK_R_BGW]);
}

/* スプライトを、透明色(パネルの添字)を飛ばして重ねる。x0 = アイコンの箱の左 */
static void draw_icon(uint8_t *fb, const clk_icon_t *ic, int32_t x0)
{
    gfx_blit8_key(fb, ic->px, (int32_t)ic->w, 0, 0, (int32_t)ic->w, (int32_t)ic->h,
                  x0 + (int32_t)ic->ox, (int32_t)CLK_ICON_Y + (int32_t)ic->oy, (uint8_t)CLK_KEY);
}

/* パネルの中: アイコン・気温・天気名を横に並べて中央に(全体の幅 G = アイコン + 気温 + 天気名 + すきま 2 つ、x0 = CX - G / 2) */
static void draw_panel_body(uint8_t *fb, const clk_view_t *v)
{
    uint8_t temp[CLK_SEQ_TEMP_MAX];
    uint8_t kind = clk_view_kind(v);
    const clk_seq_t *nm = &clk_name_seq[kind];
    uint8_t tn = clk_seq_temp(temp, v);
    int32_t tw = seq_width(clk_g72, temp, tn);
    int32_t xt = (int32_t)CLK_ICON_W + (int32_t)CLK_GAP;                  /* 気温の左 = x0 + xt */
    int32_t x0 = CLK_CX - ((xt + tw + (int32_t)CLK_GAP + seq_width(clk_g20, nm->ids, nm->n)) / 2);

    draw_icon(fb, &clk_icon[kind], x0);
    (void)draw_seq(fb, clk_g72, temp, tn, x0 + xt, CLK_TEMP_BASE, (uint8_t)ALIGN_LEFT, clk_ramp[CLK_R_PNW]);
    (void)draw_seq(fb, clk_g20, nm->ids, nm->n, x0 + xt + tw + (int32_t)CLK_GAP, CLK_NAME_BASE, (uint8_t)ALIGN_LEFT, clk_ramp[CLK_R_PNW]);
}

void ui_render_clock(const clk_view_t *v, uint8_t *fb)
{
    uint8_t i;

    if ((v == NULL) || (fb == NULL)) {
        return;
    }
    gfx_fill8(fb, 0, 0, UI_W, UI_H, 0u);                                                          /* 1 背景(添字 0。左 480 列の全画素) */
    gfx_fill8(fb, CLK_PANEL_X, CLK_PANEL_Y, CLK_PANEL_W, CLK_PANEL_H, (uint8_t)CLK_KEY);          /* 2 パネル(添字 1) */
    for (i = 0u; i < 4u; i++) {                                                                   /* 3 角 */
        gfx_a8_ramp(fb, clk_corner[i], 16, 16, clk_corner_x[i], clk_corner_y[i], clk_ramp[CLK_R_CORN], clk_ramp[CLK_R_CORN], -1);
    }
    draw_time(fb, v);                                                                             /* 4 時刻 */
    draw_date(fb, v);                                                                             /* 5 日付・地名 */
    draw_panel_body(fb, v);                                                                       /* 6〜8 アイコン・気温・天気名 */
}
