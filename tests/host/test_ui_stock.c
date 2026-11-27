/* test_ui_stock.c - 株価ビューの描画(src/app/ui_render.c + ui_logic.c + gfx.c + 生成データ)のホスト試験。docs/design.md §8 の H-8、H-12 の一部。
 *   ① 全画素を書く(面を 0x00 と 0xFF で埋めて描き、左 480 列が一致)
 *   ② 面の外・列 480〜511・面の前後の番兵に 1 バイトも書かない
 *   ③ 最悪の幅の文字列で、グリフ表から計算した矩形が重ならず、行の中に収まる(§3.4)
 *   ④ 代表 24 状態の描画結果の CRC-32(ゴールデン。生成物を作り直して差が出たら見直す)
 *   ⑤ 3 行で現在値の x がそろう(同じ桁数のとき、最初の画素の列が同じ)
 *   ⑥ Python のプレビューとの突き合わせは run.sh(fb_hostrender と gen_stock_assets.py --emit-c8 の cmp)で行う。ここでは
 *      ui_preview_view[] が状態ファイル(S0〜S7.state)と同じ内容であることを見る
 *   ほか: stk_view_t にパディングが無い、表示の決定(フッター・アイコン・dir の全組み合わせ)、色の向き(上昇 = 赤・下落 = 緑・変化なし = 灰)、
 *         ui_logic のタップ検出(2 標本で押下、3 標本で離し、2 点以上・3,000ms 超は無視)
 * 使い方: test_ui_stock <states_dir> [--golden](--golden = 24 状態の CRC を表の形で出す) */
#define HOSTRENDER_NO_MAIN
#include "../../tools/fb_hostrender.c"
#include <stddef.h>
#include "test_framework.h"

#define NPX ((size_t)GFX_STRIDE * GFX_H)
#define GUARD (1024u)
#define N_STATES (24u)

static uint8_t g_mem[GUARD + NPX + GUARD];
static const char *g_dir;

static uint32_t crc32_of(const uint8_t *fb)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t r, x, k;

    for (r = 0u; r < GFX_H; r++) {
        for (x = 0u; x < GFX_W; x++) {
            c ^= fb[r * GFX_STRIDE + x];
            for (k = 0u; k < 8u; k++) {
                c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
            }
        }
    }
    return ~c;
}

static stk_row_t row(uint8_t have, uint8_t stale, uint8_t fail, uint8_t mkt, int32_t price, int32_t diff, int32_t bp)
{
    stk_row_t r;
    memset(&r, 0, sizeof(r));
    r.have = have; r.stale = stale; r.fail = fail; r.mkt = mkt; r.price_x100 = price; r.diff_x100 = diff; r.pct_bp = bp;
    return r;
}

static stk_view_t view_base(void)
{
    stk_view_t v;
    memset(&v, 0, sizeof(v));
    v.net = STK_NET_ON; v.have_any = 1u; v.clock_min = 555u; v.upd_min = 552u;
    v.row[0] = row(1, 0, 0, STK_MKT_TRADING, 243300, 5500, 231);
    v.row[1] = row(1, 0, 0, STK_MKT_TRADING, 5754123, -13313, -23);
    v.row[2] = row(1, 0, 0, STK_MKT_CLOSED, 718278, 0, 0);
    return v;
}

/* 代表 24 状態: 0〜7 = 状態ファイル、8〜23 = 合成(フッター・アイコンの全種、行ごとの組み合わせ、境界の値) */
static int make_state(unsigned n, stk_view_t *v)
{
    char path[512];

    if (n < 8u) {
        snprintf(path, sizeof(path), "%s/S%u.state", g_dir, n);
        return hostrender_load(v, path);
    }
    *v = view_base();
    switch (n) {
    case 8:  v->busy = 1u; break;                                                     /* F2 */
    case 9:  v->net = STK_NET_CONN; break;                                            /* CONN のアイコン */
    case 10: v->net = STK_NET_OFF; break;
    case 11: v->stale_all = 1u; v->partial = 1u; break;                               /* F3 が F5 に勝つ */
    case 12: v->partial = 1u; break;                                                  /* F5 */
    case 13: v->busy = 1u; v->stale_all = 1u; break;                                  /* F2 が F3 に勝つ */
    case 14: v->have_any = 0u; v->row[0] = row(0, 0, 1, 0, 0, 0, 0); v->row[1] = row(0, 0, 1, 0, 0, 0, 0); v->row[2] = row(0, 0, 1, 0, 0, 0, 0); break;
    case 15: v->row[0].stale = 1u; break;                                             /* 行ごとの古い */
    case 16: v->row[0].mkt = STK_MKT_CLOSED; v->row[1] = row(1, 0, 1, 0, 5754123, -13313, -23); v->row[2].mkt = STK_MKT_TRADING; break;
    case 17: v->row[0] = row(1, 0, 0, STK_MKT_TRADING, 243300, 1, 0); break;          /* +0.00% */
    case 18: v->row[0] = row(1, 0, 0, STK_MKT_CLOSED, 5050, 50, 99); v->row[1] = row(1, 0, 0, STK_MKT_CLOSED, 5049, -49, -97); break;   /* 円の丸め */
    case 19: v->row[1] = row(1, 0, 0, STK_MKT_TRADING, 1, -1, -1); v->row[2] = row(1, 0, 0, STK_MKT_TRADING, 100, 1, 100); break;
    case 20: v->clock_min = 0u; v->upd_min = 1439u; break;
    case 21: v->clock_min = STK_NONE16; v->upd_min = 480u; break;
    case 22: v->row[0].diff_x100 = 0; v->row[0].pct_bp = 0; v->row[1].diff_x100 = 0; v->row[1].pct_bp = 0; break;   /* 全部 変化なし */
    case 23: v->net = STK_NET_ERR; v->have_any = 0u; v->row[0] = row(0, 0, 0, 0, 0, 0, 0); v->row[1] = row(0, 0, 0, 0, 0, 0, 0); v->row[2] = row(0, 0, 0, 0, 0, 0, 0); break;
    default: break;
    }
    return 0;
}

static void render_to(const stk_view_t *v, uint8_t *fb)
{
    ui_t u;

    ui_init(&u, 0u);
    ui_set_view(&u, v);
    ui_render(&u, fb);
}

/* 最初の画素(= 帯の背景と違う画素)の列。行 r の y 範囲 [ya, yb) と x 範囲 [xa, xb) */
static int first_col(const uint8_t *fb, uint8_t r, uint8_t dir, int ya, int yb, int xa, int xb)
{
    int x, y;

    for (x = xa; x < xb; x++) {
        for (y = ya; y < yb; y++) {
            if (fb[(size_t)(ui_row_y0[r] + y) * GFX_STRIDE + (size_t)x] != ui_strip[r][dir][(size_t)y * UI_W + (size_t)x]) {
                return x;
            }
        }
    }
    return -1;
}

struct rect { int x0, y0, x1, y1; };

static struct rect text_rect(uint8_t slot, const char *s)     /* 行の上端 = 0 に直した矩形(スロットの y は画面の y) */
{
    const ui_slot_t *sl = &ui_slots[slot];
    const ui_glyph_t *gl = ui_glyphs[sl->font];
    struct rect r;
    int wd = 0, top = 1000, bot = -1000;
    const char *p;
    static const char map[] = "0123456789,.+\x03%\x01\x02:-";

    for (p = s; *p; p++) {
        const char *q = strchr(map, *p);
        const ui_glyph_t *g;
        if (!q) { continue; }
        g = &gl[q - map];
        wd += g->adv;
        if (sl->y - g->by < top) { top = sl->y - g->by; }
        if (sl->y - g->by + g->h > bot) { bot = sl->y - g->by + g->h; }
    }
    r.x0 = (sl->align == UI_ALIGN_RIGHT) ? sl->x - wd : (sl->align == UI_ALIGN_CENTER) ? sl->x - wd / 2 : sl->x;
    r.x1 = r.x0 + wd;
    r.y0 = top; r.y1 = bot;
    return r;
}

static int overlap(const struct rect *a, const struct rect *b)
{
    return (a->x0 < b->x1) && (b->x0 < a->x1) && (a->y0 < b->y1) && (b->y0 < a->y1);
}

/* 24 状態の CRC-32(ゴールデン)。--golden で作り直す */
static const uint32_t GOLDEN[N_STATES] = {
    0xFB539220u,
    0x953E5E22u,
    0x1C4F671Cu,
    0xD5A64471u,
    0x01DD82C5u,
    0xF2B278ACu,
    0x77108B31u,
    0x785D864Bu,
    0xD433DE94u,
    0x5DDF808Bu,
    0x24B6EB37u,
    0x42B69DC0u,
    0xD4624E7Bu,
    0xF9443697u,
    0xA5B048D1u,
    0x859388B2u,
    0x44B1D032u,
    0xB0B77855u,
    0xFD6185CBu,
    0xC99D3874u,
    0xB869F269u,
    0x5F7C072Du,
    0x1625B094u,
    0xA636F584u,
};

static void test_struct(void)
{
    TEST("stk_view_t にパディングが無い(memcmp で比べるため)");
    CHECK_EQ(sizeof(stk_row_t), 16u);
    CHECK_EQ(sizeof(stk_view_t), 12u + 3u * 16u);
    CHECK_EQ(offsetof(stk_view_t, row), 12u);
    CHECK_EQ(offsetof(stk_view_t, clock_min), 6u);
    CHECK_EQ(offsetof(stk_view_t, upd_min), 8u);
    CHECK_EQ(offsetof(stk_view_t, rsv2), 10u);
}

static void test_full_write_and_guard(void)
{
    unsigned n;
    static uint8_t fa[NPX], fb2[NPX];

    TEST("H-8 ① 全画素を書く / ② 面の外・右の 32 列・前後の番兵に書かない(24 状態)");
    for (n = 0u; n < N_STATES; n++) {
        stk_view_t v;
        size_t r, c, i;
        int ok_full = 1, ok_guard = 1, ok_right = 1;

        CHECK_EQ(make_state(n, &v), 0);
        memset(fa, 0x00, NPX); memset(fb2, 0xFF, NPX);
        render_to(&v, fa);
        render_to(&v, fb2);
        for (r = 0u; r < GFX_H; r++) {
            for (c = 0u; c < GFX_W; c++) {
                if (fa[r * GFX_STRIDE + c] != fb2[r * GFX_STRIDE + c]) { ok_full = 0; }
            }
            for (c = GFX_W; c < GFX_STRIDE; c++) {
                if ((fa[r * GFX_STRIDE + c] != 0x00u) || (fb2[r * GFX_STRIDE + c] != 0xFFu)) { ok_right = 0; }
            }
        }
        /* 番兵: GUARD バイトずつ前後に 0xA5 を置いた領域の中に描く */
        memset(g_mem, 0xA5, sizeof(g_mem));
        memset(&g_mem[GUARD], 0x00, NPX);
        render_to(&v, &g_mem[GUARD]);
        for (i = 0u; i < GUARD; i++) {
            if ((g_mem[i] != 0xA5u) || (g_mem[GUARD + NPX + i] != 0xA5u)) { ok_guard = 0; }
        }
        for (r = 0u; r < GFX_H; r++) {
            for (c = 0u; c < GFX_W; c++) {
                if (g_mem[GUARD + r * GFX_STRIDE + c] != fa[r * GFX_STRIDE + c]) { ok_guard = 0; }
            }
        }
        if (!ok_full || !ok_guard || !ok_right) { printf("  状態 %u: full=%d guard=%d right=%d\n", n, ok_full, ok_guard, ok_right); }
        CHECK(ok_full);
        CHECK(ok_right);
        CHECK(ok_guard);
    }
}

static void test_worst_widths(void)
{
    struct rect a[7];
    const struct rect *e;
    int i, j;
    static const char *names[7] = { "price", "pct", "amt", "dot", "status", "name", "code" };

    TEST("H-8 ③ 最悪の幅の文字列で矩形が重ならず、行の中に収まる");
    a[0] = text_rect(UI_SL_PRICE0, "100,000.00");
    a[1] = text_rect(UI_SL_PCT0, "\x03" "999.99%");
    a[2] = text_rect(UI_SL_AMT0, "\x01" "999,999.99");
    a[3].x0 = ui_shapes[UI_SH_DOT].x; a[3].y0 = ui_shapes[UI_SH_DOT].y; a[3].x1 = a[3].x0 + ui_shapes[UI_SH_DOT].w; a[3].y1 = a[3].y0 + ui_shapes[UI_SH_DOT].h;
    a[4].x0 = ui_shapes[UI_SH_FAIL].x; a[4].y0 = ui_shapes[UI_SH_FAIL].y; a[4].x1 = a[4].x0 + ui_shapes[UI_SH_FAIL].w; a[4].y1 = a[4].y0 + ui_shapes[UI_SH_FAIL].h;
    a[5].x0 = 16; a[5].y0 = 9; a[5].x1 = 175; a[5].y1 = 21;      /* 焼き込みの銘柄名・コード(gen_stock_assets.py --check の実測) */
    a[6].x0 = 188; a[6].y0 = 10; a[6].x1 = 237; a[6].y1 = 21;
    for (i = 0; i < 3; i++) {                                       /* スロットの y は画面の y。行の上端からの距離に直す */
        a[i].y0 -= ui_row_y0[0]; a[i].y1 -= ui_row_y0[0];
    }
    for (i = 0; i < 7; i++) {
        e = &a[i];
        if ((e->x0 < 0) || (e->x1 > UI_W) || (e->y0 < 0) || (e->y1 > UI_ROW_H - 3)) { printf("  行の外: %s (%d..%d, %d..%d)\n", names[i], e->x0, e->x1, e->y0, e->y1); }
        CHECK((e->x0 >= 0) && (e->x1 <= UI_W) && (e->y0 >= 0) && (e->y1 <= UI_ROW_H - 3));   /* 区切り線(最下行)と重ならない */
        for (j = i + 1; j < 7; j++) {
            if (overlap(&a[i], &a[j])) { printf("  重なる: %s と %s\n", names[i], names[j]); }
            CHECK(!overlap(&a[i], &a[j]));
        }
    }
    CHECK_EQ(a[0].x1 - a[0].x0, 190);          /* 設計 §3.6: 100,000.00 = 190 */
    CHECK_EQ(a[1].x1 - a[1].x0, 147);          /* −999.99% = 147 */
    CHECK_EQ(a[2].x1 - a[2].x0, 99);           /* ▲999,999.99 = 99 */
}

static void test_golden(int make)
{
    unsigned n;
    static uint8_t fb[NPX];

    TEST("H-8 ④ 代表 24 状態の CRC-32(ゴールデン)");
    if (make) { printf("static const uint32_t GOLDEN[N_STATES] = {\n"); }
    for (n = 0u; n < N_STATES; n++) {
        stk_view_t v;
        uint32_t c;

        CHECK_EQ(make_state(n, &v), 0);
        memset(fb, 0, NPX);
        render_to(&v, fb);
        c = crc32_of(fb);
        if (make) {
            printf("    0x%08Xu,\n", c);
        } else {
            if (c != GOLDEN[n]) { printf("  状態 %u: CRC %08X != %08X\n", n, c, GOLDEN[n]); }
            CHECK_EQ(c, GOLDEN[n]);
        }
    }
    if (make) { printf("};\n"); }
}

static void test_price_columns(void)
{
    static uint8_t fb[NPX];
    stk_view_t v = view_base();
    int x0, x1, x2, k;
    static const int32_t ps[3] = { 12300, 12300, 12300 };

    TEST("H-8 ⑤ 上昇・下落・変化なしの 3 行で、同じ桁数の現在値の x がそろう");
    for (k = 0; k < 3; k++) {
        v.row[k].price_x100 = ps[k];
        v.row[k].mkt = STK_MKT_NONE;
    }
    v.row[0].diff_x100 = 100; v.row[0].pct_bp = 8;        /* 円: 123 */
    v.row[1].diff_x100 = -100; v.row[1].pct_bp = -8;      /* 指数: 123.00 */
    v.row[2].diff_x100 = 0; v.row[2].pct_bp = 0;
    render_to(&v, fb);
    x0 = first_col(fb, 0, STK_UP, 35, 71, 0, 100);
    x1 = first_col(fb, 1, STK_DOWN, 35, 71, 0, 100);
    x2 = first_col(fb, 2, STK_FLAT, 35, 71, 0, 100);
    printf("  最初の画素の列: %d %d %d\n", x0, x1, x2);
    CHECK(x0 >= 16);
    CHECK_EQ(x0, x1);
    CHECK_EQ(x1, x2);
}

static void test_decisions(void)
{
    unsigned net, busy, have_any, stale_all, partial;
    int8_t ok = 1;
    stk_row_t r;

    TEST("§3.9 表示の決定: フッター・アイコンの優先順位(全組み合わせ)、dir");
    for (net = 0u; net < 4u; net++) {
        for (busy = 0u; busy < 2u; busy++) {
            for (have_any = 0u; have_any < 2u; have_any++) {
                for (stale_all = 0u; stale_all < 2u; stale_all++) {
                    for (partial = 0u; partial < 2u; partial++) {
                        stk_view_t v = view_base();
                        unsigned f, ic;
                        v.net = (uint8_t)net; v.busy = (uint8_t)busy; v.have_any = (uint8_t)have_any;
                        v.stale_all = (uint8_t)stale_all; v.partial = (uint8_t)partial;
                        /* 優先順位: オフライン(F4)> 起動中(F1)> 取得中(F2)> 古い(F3)> 一部失敗(F5)> 通常(F0) */
                        if ((net == 0u) || (net == 3u)) f = 4u;
                        else if (!have_any) f = 1u;
                        else if (busy) f = 2u;
                        else if (stale_all) f = 3u;
                        else if (partial) f = 5u;
                        else f = 0u;
                        /* アイコン: オフライン(OFF)> 接続中(CONN)> 古い(STALE)> ON */
                        if ((net == 0u) || (net == 3u)) ic = 3u;
                        else if (net == 1u) ic = 1u;
                        else if (stale_all) ic = 2u;
                        else ic = 0u;
                        if ((ui_foot_index(&v) != f) || (ui_icon_index(&v) != ic)) { ok = 0; }
                    }
                }
            }
        }
    }
    CHECK(ok);
    memset(&r, 0, sizeof(r));
    CHECK_EQ(ui_row_dir(&r), STK_FLAT);                    /* 値なし */
    r.have = 1u; r.diff_x100 = 1;  CHECK_EQ(ui_row_dir(&r), STK_UP);      /* 丸めが 0.00% でも diff != 0 なら上昇 */
    r.diff_x100 = -1;              CHECK_EQ(ui_row_dir(&r), STK_DOWN);
    r.diff_x100 = 0;               CHECK_EQ(ui_row_dir(&r), STK_FLAT);
    r.have = 0u; r.diff_x100 = 500; CHECK_EQ(ui_row_dir(&r), STK_FLAT);
}

/* 色の向き: パレットを通した ARGB の R / G / B */
static void argb(uint8_t idx, int *r, int *g, int *b)
{
    uint32_t a = ui_pal[idx];
    *r = (int)((a >> 16) & 255u); *g = (int)((a >> 8) & 255u); *b = (int)(a & 255u);
}

static void test_colors(void)
{
    static uint8_t fb[NPX];
    stk_view_t v = view_base();
    int r, g, b, y;
    unsigned k;

    TEST("色: 上昇 = 赤、下落 = 緑、変化なし = 灰(バー・文字)");
    v.row[0].diff_x100 = 100; v.row[1].diff_x100 = -100; v.row[2].diff_x100 = 0;
    render_to(&v, fb);
    y = ui_row_y0[0] + 10; argb(fb[(size_t)y * GFX_STRIDE + 2u], &r, &g, &b);
    CHECK((r > g + 60) && (r > b + 60));
    y = ui_row_y0[1] + 10; argb(fb[(size_t)y * GFX_STRIDE + 2u], &r, &g, &b);
    CHECK((g > r + 40) && (g > b));
    y = ui_row_y0[2] + 10; argb(fb[(size_t)y * GFX_STRIDE + 2u], &r, &g, &b);
    CHECK((abs(r - g) < 40) && (b >= r));
    argb(ui_ramp[UI_R_ACC][UI_C_UP][32], &r, &g, &b);       CHECK((r > 200) && (g < 150) && (b < 150));
    argb(ui_ramp[UI_R_ACC][UI_C_DOWN][32], &r, &g, &b);     CHECK((g > 180) && (r < 120));
    argb(ui_ramp[UI_R_ACC][UI_C_FLAT][32], &r, &g, &b);     CHECK((abs(r - g) < 40) && (r > 160));
    /* 区切り線: 各帯の最下行は 480 画素とも同じ色で、その上の行と違う */
    for (k = 0u; k < 9u; k++) {
        const uint8_t *s = ui_strip[k / 3u][k % 3u];
        int x, same = 1, differ = 1;
        for (x = 0; x < UI_W; x++) {
            if (s[(UI_ROW_H - 1) * UI_W + x] != s[(UI_ROW_H - 1) * UI_W]) { same = 0; }
            if (s[(UI_ROW_H - 2) * UI_W + x] == s[(UI_ROW_H - 1) * UI_W]) { differ = 0; }
        }
        CHECK(same);
        CHECK(differ);
    }
}

static void test_preview_data(void)
{
    unsigned n;

    TEST("H-8 ⑥の前提: ui_preview_view[] が状態ファイル S0〜S7 と同じ内容");
#if APP_STK_PREVIEW
    for (n = 0u; n < UI_PREVIEW_N; n++) {
        stk_view_t v;
        CHECK_EQ(make_state(n, &v), 0);
        if (memcmp(&v, &ui_preview_view[n], sizeof(v)) != 0) { printf("  S%u が違う\n", n); }
        CHECK(memcmp(&v, &ui_preview_view[n], sizeof(v)) == 0);
    }
#else
    (void)n;
    CHECK(0);          /* APP_STK_PREVIEW = 1 でビルドすること */
#endif
}

static void test_ui_logic(void)
{
    ui_t u;
    nav_sample_t dn, up;
    ui_ev_t ev;
    uint32_t t = 0u;
    int i;

    TEST("H-12 の一部: タップ検出(2 標本で押下、3 標本で離し → UI_EV_TAP 1 回)");
    memset(&dn, 0, sizeof(dn)); memset(&up, 0, sizeof(up));
    dn.pressed = true; dn.points = 1u; dn.x = 100u; dn.y = 100u;
    ui_init(&u, 0u);
    ui_update(&u, &dn, t += 10u);
    ui_update(&u, &up, t += 10u);                       /* 押下確定の前に離れた: タップにならない */
    for (i = 0; i < 3; i++) { ui_update(&u, &up, t += 10u); }
    CHECK(!ui_event_pop(&u, &ev));
    ui_update(&u, &dn, t += 10u);
    ui_update(&u, &dn, t += 10u);                       /* 押下確定 */
    CHECK(!ui_event_pop(&u, &ev));
    ui_update(&u, &up, t += 10u);
    ui_update(&u, &up, t += 10u);                       /* 離し 2 標本: まだ */
    CHECK(!ui_event_pop(&u, &ev));
    ui_update(&u, &up, t += 10u);                       /* 離し確定 */
    CHECK(ui_event_pop(&u, &ev));
    CHECK_EQ(ev.type, UI_EV_TAP);
    CHECK_EQ(ev.src, UI_SRC_TOUCH);
    CHECK(!ui_event_pop(&u, &ev));
    ui_update(&u, NULL, t += 10u);                      /* 標本なしは状態を変えない */
    /* 2 点以上は無視 */
    dn.points = 2u;
    for (i = 0; i < 5; i++) { ui_update(&u, &dn, t += 10u); }
    for (i = 0; i < 5; i++) { ui_update(&u, &up, t += 10u); }
    CHECK(!ui_event_pop(&u, &ev));
    /* 3,000ms を超える押しっぱなしはタップにしない */
    dn.points = 1u;
    for (i = 0; i < 301; i++) { ui_update(&u, &dn, t += 10u); }
    for (i = 0; i < 5; i++) { ui_update(&u, &up, t += 10u); }
    CHECK(!ui_event_pop(&u, &ev));
    /* ちょうど 3,000ms までの押しっぱなしはタップ。連続タップ */
    for (i = 0; i < 2; i++) {
        int k;
        for (k = 0; k < 2; k++) { ui_update(&u, &dn, t += 10u); }
        for (k = 0; k < 3; k++) { ui_update(&u, &up, t += 10u); }
    }
    CHECK(ui_event_pop(&u, &ev));
    CHECK(ui_event_pop(&u, &ev));
    CHECK(!ui_event_pop(&u, &ev));
    ui_tap_inject(&u, UI_SRC_UART);
    CHECK(ui_event_pop(&u, &ev));
    CHECK_EQ(ev.src, UI_SRC_UART);
    /* 表示キー */
    ui_init(&u, 0u);
    CHECK(ui_view_changed(&u));
    ui_view_mark(&u);
    CHECK(!ui_view_changed(&u));
    {
        stk_view_t v = u.view;
        v.row[1].price_x100 = 5;
        ui_set_view(&u, &v);
        CHECK(ui_view_changed(&u));
        ui_view_mark(&u);
        CHECK(!ui_view_changed(&u));
    }
}

int main(int argc, char **argv)
{
    int make = 0, i;

    g_dir = (argc > 1) ? argv[1] : "tests/host/data/states";
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--golden")) { make = 1; }
    }
    if (make) {
        test_golden(1);
        return 0;
    }
    test_struct();
    test_full_write_and_guard();
    test_worst_widths();
    test_golden(0);
    test_price_columns();
    test_decisions();
    test_colors();
    test_preview_data();
    test_ui_logic();
    TEST_REPORT("test_ui_stock");
}
