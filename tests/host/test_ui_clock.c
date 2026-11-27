/* test_ui_clock.c - 時計・天気画面の描画(src/app/ui_clock.c + ui_render.c の分岐 + gfx.c + 生成データ)のホスト試験。docs/design.md §13.10 の H-C7。
 *   ① 全画素を書く(面を 0x00 と 0xFF で埋めて描き、左 480 列が一致)
 *   ② 面の外・列 480〜511・面の前後の番兵に 1 バイトも書かない
 *   ③ 12 状態で字・アイコン・パネルの矩形が互いに重ならず、時刻は画面内、日付はパネルの上(y < 168)、
 *      アイコン + 気温 + 天気名の全体 G がパネル(x 40〜440)に収まる(C8 の G = 397、x0 = 42〜右端 439)
 *   ④ 12 状態の CRC-32(ゴールデン。生成物を作り直して差が出たら見直す。--golden で表を出す)
 *   ⑤ Python のプレビューとの突き合わせは run.sh(fb_hostrender と gen_clock_assets.py --emit-c8 の cmp)で行う。ここでは clk_preview_view[] が
 *      状態ファイル(C0〜C11.state)と同じ内容であることを見る(-DAPP_CLK_PREVIEW=1 でビルドする)
 *   ⑥ 使う添字が、その天気の使う数(used_n = 119 / 179 / 126 / 127 / 147 / 131 / 178 / 156)未満だけ。パレットの未使用の添字は背景と同じ色
 *   ⑦ パレット 8 枚の背景(添字 0)・パネル(添字 1)の色が §13.4 の RGB565 後の色と一致。ui_pal_ptr の範囲
 *   ⑧ アイコン: スプライトの透明色(添字 1)の画素は面を変えず(パネルのまま)、それ以外が書かれる
 *   ⑨ HH:MM の分が変わった入力(09:41 → 09:42)で、時刻の領域だけが変わる
 *   ⑩ 時刻の字: 0 詰め(09:05)、--:--、1440〜65534 も --:--
 *   ほか: 天気だけ不明(have = 0)は kind に依らず unknown の画面、v / fb が NULL は何もしない
 * 使い方: test_ui_clock <states_dir> [--golden] */
#define HOSTRENDER_NO_MAIN
#include "../../tools/fb_hostrender.c"
#include <stddef.h>
#include "test_framework.h"

#define NPX ((size_t)GFX_STRIDE * GFX_H)
#define GUARD (1024u)
#define N_STATES (12u)

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

static int load_c(unsigned n, clk_view_t *v)
{
    char path[512];
    ui_t u;
    int rc;

    snprintf(path, sizeof(path), "%s/C%u.state", g_dir, n);
    ui_init(&u, 0u);
    rc = hostrender_load_ui(&u, path);
    *v = u.cview;
    return rc;
}

static void render_c(const clk_view_t *v, uint8_t *fb)
{
    ui_t u;

    ui_init(&u, 0u);
    ui_set_clk(&u, v);
    ui_set_screen(&u, UI_SCR_CLOCK, 0u);
    ui_render(&u, fb);
}

/* 12 状態の CRC-32(ゴールデン。Python のプレビューと 0 バイト差の描画の CRC)。--golden で作り直す */
static const uint32_t GOLDEN[N_STATES] = {
    0x3565FC30u,
    0xF6AA437Au,
    0x08D92F51u,
    0xEAE56A92u,
    0xAFE695FEu,
    0xBE797A94u,
    0xD8ADDA4Bu,
    0x6167E0B8u,
    0x08CC2826u,
    0xA345714Cu,
    0x1143D5B5u,
    0x3840A2A8u,
};

static const unsigned USED_N[8] = { 119u, 179u, 126u, 127u, 147u, 131u, 178u, 156u };
/* §13.4 の RGB565 後の背景・パネル(RGB888 = RGB565 の上位ビットを下位へ複製) */
static const uint32_t BG_RGB[8] = { 0xB55508u, 0x216DA5u, 0x4A5973u, 0x636973u, 0x214584u, 0x5A7DA5u, 0x4A3884u, 0x424952u };
static const uint32_t PN_RGB[8] = { 0x843C08u, 0x185173u, 0x394152u, 0x424D52u, 0x183063u, 0x42597Bu, 0x392863u, 0x293442u };

static void test_full_write_and_guard(void)
{
    unsigned n;
    static uint8_t fa[NPX], fb2[NPX];

    TEST("H-C7 ① 全画素を書く / ② 面の外・右の 32 列・前後の番兵に書かない(12 状態)");
    for (n = 0u; n < N_STATES; n++) {
        clk_view_t v;
        size_t r, c, i;
        int ok_full = 1, ok_guard = 1, ok_right = 1;

        CHECK_EQ(load_c(n, &v), 0);
        memset(fa, 0x00, NPX); memset(fb2, 0xFF, NPX);
        render_c(&v, fa);
        render_c(&v, fb2);
        for (r = 0u; r < GFX_H; r++) {
            for (c = 0u; c < GFX_W; c++) {
                if (fa[r * GFX_STRIDE + c] != fb2[r * GFX_STRIDE + c]) { ok_full = 0; }
            }
            for (c = GFX_W; c < GFX_STRIDE; c++) {
                if ((fa[r * GFX_STRIDE + c] != 0x00u) || (fb2[r * GFX_STRIDE + c] != 0xFFu)) { ok_right = 0; }
            }
        }
        memset(g_mem, 0xA5, sizeof(g_mem));
        memset(&g_mem[GUARD], 0x00, NPX);
        render_c(&v, &g_mem[GUARD]);
        for (i = 0u; i < GUARD; i++) {
            if ((g_mem[i] != 0xA5u) || (g_mem[GUARD + NPX + i] != 0xA5u)) { ok_guard = 0; }
        }
        for (r = 0u; r < GFX_H; r++) {
            for (c = 0u; c < GFX_W; c++) {
                if (g_mem[GUARD + r * GFX_STRIDE + c] != fa[r * GFX_STRIDE + c]) { ok_guard = 0; }
            }
        }
        if (!ok_full || !ok_guard || !ok_right) { printf("  状態 C%u: full=%d guard=%d right=%d\n", n, ok_full, ok_guard, ok_right); }
        CHECK(ok_full);
        CHECK(ok_right);
        CHECK(ok_guard);
    }
}

struct rect { int x0, y0, x1, y1; };

static int overlap(const struct rect *a, const struct rect *b)
{
    return (a->x0 < b->x1) && (b->x0 < a->x1) && (a->y0 < b->y1) && (b->y0 < a->y1);
}

/* 字番号の並びの外形(横 = 字の外形、縦 = ベースライン基準)。align_center なら x -= Σadv / 2 */
static struct rect ink(const ui_glyph_t *gl, const uint8_t *ids, uint8_t n, int x, int y, int center)
{
    struct rect r;
    int wd = 0, first = 1;
    uint8_t i;

    for (i = 0u; i < n; i++) { wd += gl[ids[i]].adv; }
    if (center) { x -= wd / 2; }
    r.x0 = r.y0 = r.x1 = r.y1 = 0;
    for (i = 0u; i < n; i++) {
        const ui_glyph_t *g = &gl[ids[i]];
        if (g->w != 0u) {
            int a0 = x + g->bx, b0 = y - g->by, a1 = a0 + g->w, b1 = b0 + g->h;
            if (first) { r.x0 = a0; r.y0 = b0; r.x1 = a1; r.y1 = b1; first = 0; }
            else {
                if (a0 < r.x0) r.x0 = a0;
                if (b0 < r.y0) r.y0 = b0;
                if (a1 > r.x1) r.x1 = a1;
                if (b1 > r.y1) r.y1 = b1;
            }
        }
        x += g->adv;
    }
    return r;
}

static int seq_w(const ui_glyph_t *gl, const uint8_t *ids, uint8_t n)
{
    int wd = 0;
    uint8_t i;
    for (i = 0u; i < n; i++) { wd += gl[ids[i]].adv; }
    return wd;
}

/* 状態 v の 5 つの外形と G・x0(ui_clock.c とは別に、生成物の表から計算する) */
static void layout(const clk_view_t *v, struct rect out[5], int *G, int *x0)
{
    uint8_t t[CLK_SEQ_TIME_MAX], d[CLK_SEQ_DATE_MAX], tp[CLK_SEQ_TEMP_MAX];
    uint8_t nt = clk_seq_time(t, v), nd = clk_seq_date(d, v), np = clk_seq_temp(tp, v);
    uint8_t kind = clk_view_kind(v);
    const clk_seq_t *nm = &clk_name_seq[kind];
    int tw = seq_w(clk_g72, tp, np), nw = seq_w(clk_g20, nm->ids, nm->n);
    int g = CLK_ICON_W + CLK_GAP + tw + CLK_GAP + nw;
    int xx = CLK_CX - g / 2;

    out[0] = ink(clk_g124, t, nt, CLK_CX, CLK_TIME_BASE, 1);
    out[1] = ink(clk_g20, d, nd, CLK_CX, CLK_DATE_BASE, 1);
    out[2].x0 = xx + clk_icon[kind].ox; out[2].y0 = CLK_ICON_Y + clk_icon[kind].oy;
    out[2].x1 = out[2].x0 + clk_icon[kind].w; out[2].y1 = out[2].y0 + clk_icon[kind].h;
    out[3] = ink(clk_g72, tp, np, xx + CLK_ICON_W + CLK_GAP, CLK_TEMP_BASE, 0);
    out[4] = ink(clk_g20, nm->ids, nm->n, xx + CLK_ICON_W + CLK_GAP + tw + CLK_GAP, CLK_NAME_BASE, 0);
    *G = g; *x0 = xx;
}

static void test_layout(void)
{
    unsigned n;
    int worst = 0;

    TEST("H-C7 ③ 12 状態の配置: 字・アイコンが重ならず、時刻は画面内、日付はパネルの上、G がパネル(x 40〜440)に収まる(C8 の G = 397)");
    for (n = 0u; n < N_STATES; n++) {
        clk_view_t v;
        struct rect r[5];
        int G, x0, i, j;
        static const char *nm[5] = { "時刻", "日付", "アイコン", "気温", "天気名" };

        CHECK_EQ(load_c(n, &v), 0);
        layout(&v, r, &G, &x0);
        if (G > worst) { worst = G; }
        CHECK((r[0].x0 >= 0) && (r[0].x1 <= GFX_W) && (r[0].y0 >= 0) && (r[0].y1 <= CLK_PANEL_Y));
        CHECK((r[1].x0 >= 0) && (r[1].x1 <= GFX_W) && (r[1].y0 >= 0) && (r[1].y1 <= CLK_PANEL_Y));   /* 日付はパネルの上(y < 168) */
        for (i = 2; i < 5; i++) {
            int in = (r[i].x0 >= CLK_PANEL_X) && (r[i].x1 <= CLK_PANEL_X + CLK_PANEL_W) && (r[i].y0 >= CLK_PANEL_Y) && (r[i].y1 <= CLK_PANEL_Y + CLK_PANEL_H);
            if (!in) { printf("  C%u: %s がパネルからはみ出す (%d..%d, %d..%d)\n", n, nm[i], r[i].x0, r[i].x1, r[i].y0, r[i].y1); }
            CHECK(in);
        }
        CHECK((x0 >= CLK_PANEL_X) && (x0 + G <= CLK_PANEL_X + CLK_PANEL_W));
        for (i = 0; i < 5; i++) {
            for (j = i + 1; j < 5; j++) {
                if (overlap(&r[i], &r[j])) { printf("  C%u: %s と %s が重なる\n", n, nm[i], nm[j]); }
                CHECK(!overlap(&r[i], &r[j]));
            }
        }
        if (n == 8u) { CHECK_EQ(G, 397); CHECK_EQ(x0, 42); CHECK_EQ(x0 + G, 439); }
    }
    CHECK_EQ(worst, 397);
}

static void test_golden(int make)
{
    unsigned n;
    static uint8_t fb[NPX];

    TEST("H-C7 ④ 12 状態の CRC-32(ゴールデン)");
    if (make) { printf("static const uint32_t GOLDEN[N_STATES] = {\n"); }
    for (n = 0u; n < N_STATES; n++) {
        clk_view_t v;
        uint32_t c;

        CHECK_EQ(load_c(n, &v), 0);
        memset(fb, 0, NPX);
        render_c(&v, fb);
        c = crc32_of(fb);
        if (make) {
            printf("    0x%08Xu,\n", c);
        } else {
            if (c != GOLDEN[n]) { printf("  状態 C%u: CRC %08X != %08X\n", n, c, GOLDEN[n]); }
            CHECK_EQ(c, GOLDEN[n]);
        }
    }
    if (make) { printf("};\n"); }
}

static void test_preview_data(void)
{
    unsigned n;

    TEST("H-C7 ⑤の前提: clk_preview_view[] が状態ファイル C0〜C11 と同じ内容(clk_view_t のパディングなし)");
    CHECK_EQ(sizeof(clk_view_t), 8u);
#if APP_CLK_PREVIEW
    CHECK_EQ(CLK_PREVIEW_N, N_STATES);
    for (n = 0u; n < N_STATES; n++) {
        clk_view_t v;
        CHECK_EQ(load_c(n, &v), 0);
        if (v.have == 0u) { v.kind = (uint8_t)CLK_UNKNOWN; v.temp = 0; }         /* 生成物は天気なしを unknown・0 にそろえる(clk_view_make と同じ) */
        if (memcmp(&v, &clk_preview_view[n], sizeof(v)) != 0) { printf("  C%u が違う\n", n); }
        CHECK(memcmp(&v, &clk_preview_view[n], sizeof(v)) == 0);
    }
#else
    (void)n;
    CHECK(0);          /* -DAPP_CLK_PREVIEW=1 でビルドすること */
#endif
}

static void test_palette(void)
{
    unsigned n, k, i, bad = 0u;
    static uint8_t fb[NPX];

    TEST("H-C7 ⑥ 使う添字が used_n 未満だけ。パレットの未使用の添字は背景と同じ色。前半 98 個の ramp は全天気で同じ配置");
    for (n = 0u; n < N_STATES; n++) {
        clk_view_t v;
        unsigned kind, mx = 0u;
        size_t r, c;

        CHECK_EQ(load_c(n, &v), 0);
        kind = clk_view_kind(&v);
        memset(fb, 0, NPX);
        render_c(&v, fb);
        for (r = 0u; r < GFX_H; r++) { for (c = 0u; c < GFX_W; c++) { if (fb[r * GFX_STRIDE + c] > mx) { mx = fb[r * GFX_STRIDE + c]; } } }
        if (mx >= USED_N[kind]) { printf("  C%u: 添字 %u >= used_n %u\n", n, mx, USED_N[kind]); }
        CHECK(mx < USED_N[kind]);
    }
    for (k = 0u; k < 8u; k++) {
        for (i = USED_N[k]; i < 256u; i++) { if (clk_pal[k][i] != clk_pal[k][0]) { bad++; } }
        for (i = 0u; i < 256u; i++) { if ((clk_pal[k][i] >> 24) != 0xFFu) { bad++; } }
    }
    CHECK_EQ(bad, 0);
    CHECK_EQ(CLK_FIXED_N, 98);
    CHECK_EQ(clk_ramp[CLK_R_BGW][0], 0); CHECK_EQ(clk_ramp[CLK_R_BGW][1], 2); CHECK_EQ(clk_ramp[CLK_R_BGW][32], 33);
    CHECK_EQ(clk_ramp[CLK_R_PNW][1], 34); CHECK_EQ(clk_ramp[CLK_R_PNW][32], 65);
    CHECK_EQ(clk_ramp[CLK_R_CORN][1], 66); CHECK_EQ(clk_ramp[CLK_R_CORN][32], 97);
    for (k = 0u; k < 8u; k++) { CHECK_EQ(clk_pal[k][clk_ramp[CLK_R_BGW][32]], 0xFFFFFFFFu); CHECK_EQ(clk_pal[k][clk_ramp[CLK_R_PNW][32]], 0xFFFFFFFFu); }   /* l = 32 は純白 */
    for (k = 0u; k < 8u; k++) { CHECK_EQ(clk_pal[k][clk_ramp[CLK_R_CORN][32]], clk_pal[k][0]); }   /* 角の l = 32 は背景 */

    TEST("H-C7 ⑦ パレット 8 枚の背景(添字 0)・パネル(添字 1)の色が §13.4 の RGB565 後の色。ui_pal_ptr の範囲(0 = 株価、1〜8 = clk_pal、範囲外は株価)");
    for (k = 0u; k < 8u; k++) {
        CHECK_EQ(clk_pal[k][0] & 0xFFFFFFu, BG_RGB[k]);
        CHECK_EQ(clk_pal[k][1] & 0xFFFFFFu, PN_RGB[k]);
        CHECK(ui_pal_ptr((uint8_t)(1u + k)) == clk_pal[k]);
    }
    CHECK(ui_pal_ptr(0u) == ui_pal);
    CHECK(ui_pal_ptr(9u) == ui_pal);
    CHECK(ui_pal_ptr(255u) == ui_pal);
}

static void test_icon_key(void)
{
    unsigned n;
    static uint8_t fb[NPX];

    TEST("H-C7 ⑧ アイコン: スプライトの透明色(添字 1)の画素はパネルのまま、それ以外はスプライトの添字が書かれる(12 状態)");
    for (n = 0u; n < N_STATES; n++) {
        clk_view_t v;
        struct rect r[5];
        int G, x0, px, py, bad = 0, keyn = 0, nkey = 0;
        unsigned kind;

        CHECK_EQ(load_c(n, &v), 0);
        kind = clk_view_kind(&v);
        memset(fb, 0, NPX);
        render_c(&v, fb);
        layout(&v, r, &G, &x0);
        for (py = 0; py < clk_icon[kind].h; py++) {
            for (px = 0; px < clk_icon[kind].w; px++) {
                uint8_t s = clk_icon[kind].px[py * clk_icon[kind].w + px];
                uint8_t g = fb[(size_t)(r[2].y0 + py) * GFX_STRIDE + (size_t)(r[2].x0 + px)];
                if (s == CLK_KEY) { keyn++; if (g != CLK_KEY) { bad++; } }       /* 透明 = パネル(角丸の外ではない所なので添字 1) */
                else { nkey++; if (g != s) { bad++; } }
            }
        }
        CHECK_EQ(bad, 0);
        CHECK(keyn > 0);
        CHECK(nkey > 100);
    }
}

static void bbox_diff(const uint8_t *a, const uint8_t *b, struct rect *r, unsigned *cnt)
{
    int x, y, first = 1;

    *cnt = 0u;
    r->x0 = r->y0 = r->x1 = r->y1 = 0;
    for (y = 0; y < GFX_H; y++) {
        for (x = 0; x < GFX_W; x++) {
            if (a[(size_t)y * GFX_STRIDE + (size_t)x] != b[(size_t)y * GFX_STRIDE + (size_t)x]) {
                (*cnt)++;
                if (first) { r->x0 = x; r->y0 = y; r->x1 = x + 1; r->y1 = y + 1; first = 0; }
                else { if (x < r->x0) r->x0 = x; if (y < r->y0) r->y0 = y; if (x + 1 > r->x1) r->x1 = x + 1; if (y + 1 > r->y1) r->y1 = y + 1; }
            }
        }
    }
}

static void test_time(void)
{
    static uint8_t fa[NPX], fb[NPX];
    clk_view_t v, w;
    struct rect r[5], d;
    unsigned cnt;
    int G, x0;

    TEST("H-C7 ⑨ 分が変わった入力(09:41 → 09:42)で、時刻の領域だけが変わる(日付・パネルは同じ画素)");
    CHECK_EQ(load_c(0u, &v), 0);
    w = v; w.clock_min = 582u;
    memset(fa, 0, NPX); memset(fb, 0, NPX);
    render_c(&v, fa); render_c(&w, fb);
    bbox_diff(fa, fb, &d, &cnt);
    layout(&v, r, &G, &x0);
    CHECK(cnt > 0u);
    {   /* 差は、新旧どちらかの時刻の外形の中(字の幅が違う 1 と 2 で、外形が少し変わる) */
        struct rect r2[5];
        int G2, x02;
        layout(&w, r2, &G2, &x02);
        CHECK((d.x0 >= ((r[0].x0 < r2[0].x0) ? r[0].x0 : r2[0].x0)) && (d.x1 <= ((r[0].x1 > r2[0].x1) ? r[0].x1 : r2[0].x1)));
        CHECK((d.y0 >= ((r[0].y0 < r2[0].y0) ? r[0].y0 : r2[0].y0)) && (d.y1 <= ((r[0].y1 > r2[0].y1) ? r[0].y1 : r2[0].y1)));
    }
    CHECK(d.y1 <= CLK_PANEL_Y);
    w = v; w.mon = 12u; w.day = 1u; w.wday = 1u;                       /* 日付が変わったら、日付の領域だけ */
    memset(fb, 0, NPX); render_c(&w, fb);
    bbox_diff(fa, fb, &d, &cnt);
    CHECK(cnt > 0u);
    CHECK((d.x0 >= r[1].x0 - 24) && (d.x1 <= r[1].x1 + 24) && (d.y0 >= r[1].y0) && (d.y1 <= r[1].y1));  /* 日付の領域(月日の桁が変わると幅も変わる = 左右 24px の余裕) */
    CHECK(d.y1 <= CLK_PANEL_Y);

    TEST("H-C7 ⑩ 時刻の字: 0 詰め(09:05)。1440〜65534 も --:--(未同期と同じ画素)。日付なしは 東京都 だけ");
    w = v; w.clock_min = 0xFFFFu;
    memset(fa, 0, NPX); render_c(&w, fa);
    w.clock_min = 1440u;  memset(fb, 0, NPX); render_c(&w, fb); CHECK(memcmp(fa, fb, NPX) == 0);
    w.clock_min = 65534u; memset(fb, 0, NPX); render_c(&w, fb); CHECK(memcmp(fa, fb, NPX) == 0);
    w.clock_min = 539u;   memset(fb, 0, NPX); render_c(&w, fb); CHECK(memcmp(fa, fb, NPX) != 0);       /* 09:59 は --:-- と違う */
    {   /* 09:05 を、字番号 [0, 9, :, 0, 5] から別に描いたものと一致(中央寄せ・ベースライン・ramp) */
        uint8_t ids[5] = { 0u, 9u, (uint8_t)CLK_G124_COLON, 0u, 5u };
        int wd = 0, x, i;
        static uint8_t fe[NPX];
        w = v; w.clock_min = 545u;                                     /* 09:05 */
        memset(fa, 0, NPX); render_c(&w, fa);
        memcpy(fe, fa, NPX);
        gfx_fill8(fe, 0, 0, 480, CLK_PANEL_Y, 0u);                    /* 上の領域(時刻・日付)を背景で消して、時刻だけを自前で描く */
        for (i = 0; i < 5; i++) { wd += clk_g124[ids[i]].adv; }
        x = CLK_CX - wd / 2;
        for (i = 0; i < 5; i++) {
            const ui_glyph_t *g = &clk_g124[ids[i]];
            if (g->w != 0u) { gfx_a8_ramp(fe, &clk_glyph_pool[g->off], g->w, g->h, x + g->bx, CLK_TIME_BASE - g->by, clk_ramp[CLK_R_BGW], clk_ramp[CLK_R_BGW], -1); }
            x += g->adv;
        }
        {   /* 日付の行(y = 130〜167 付近)は、fa の日付をそのまま使うため、時刻の領域(y < 130)だけを比べる */
            int y, xx, same = 1;
            for (y = 0; y < 128; y++) { for (xx = 0; xx < GFX_W; xx++) { if (fa[(size_t)y * GFX_STRIDE + (size_t)xx] != fe[(size_t)y * GFX_STRIDE + (size_t)xx]) { same = 0; } } }
            CHECK(same);
        }
    }
    w = v; w.mon = 0u; w.day = 0u; w.wday = 0u;
    memset(fa, 0, NPX); render_c(&v, fa);
    memset(fb, 0, NPX); render_c(&w, fb);
    bbox_diff(fa, fb, &d, &cnt);
    CHECK(cnt > 0u);
    layout(&w, r, &G, &x0);
    CHECK((r[1].x1 - r[1].x0) < 70);                                   /* 東京都 だけ(約 60px) */
    CHECK((d.y0 >= 130) && (d.y1 <= CLK_PANEL_Y));
}

static void test_have_and_null(void)
{
    static uint8_t fa[NPX], fb[NPX];
    clk_view_t v, w;
    unsigned k;

    TEST("天気だけ不明(have = 0)は kind に依らず unknown の画面(C7 と同じ画素)。範囲外の kind も unknown。v / fb が NULL は何もしない");
    CHECK_EQ(load_c(7u, &v), 0);
    memset(fa, 0, NPX); render_c(&v, fa);
    for (k = 0u; k < 8u; k++) { w = v; w.kind = (uint8_t)k; w.temp = 33; memset(fb, 0, NPX); render_c(&w, fb); CHECK(memcmp(fa, fb, NPX) == 0); }
    w = v; w.have = 1u; w.kind = 9u; w.temp = 0;
    memset(fb, 0, NPX); render_c(&w, fb);
    {   /* have = 1・kind 範囲外 = unknown のアイコンと名前。気温は 0° なので --° の C7 とは違う */
        CHECK(memcmp(fa, fb, NPX) != 0);
        CHECK_EQ(clk_view_kind(&w), CLK_UNKNOWN);
    }
    memset(fb, 0x5A, NPX);
    ui_render_clock(NULL, fb);
    { size_t i; int same = 1; for (i = 0u; i < NPX; i++) { if (fb[i] != 0x5Au) { same = 0; } } CHECK(same); }
    ui_render_clock(&v, NULL);
    CHECK(1);
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
    test_full_write_and_guard();
    test_layout();
    test_golden(0);
    test_preview_data();
    test_palette();
    test_icon_key();
    test_time();
    test_have_and_null();
    TEST_REPORT("test_ui_clock");
}
