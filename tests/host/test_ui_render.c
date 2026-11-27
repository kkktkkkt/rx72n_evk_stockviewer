/* test_ui_render.c - src/app/ui_render.c(+ gfx.c, 生成データ)のホスト試験(設計 §12 H-G3、§20.9 H-D3)。面は 8bpp(CLUT8、行幅 GFX_STRIDE)。
 *   H-G3 / H-D3: 面を 0x00 と 0xFF で埋めてから描き、左 480 列の2回の結果が同じ(= 全画素を書いた)。右の 32 列は触らない。
 *               5画面 x 状態 x 遷移(dx = 0,1,120,240,479 / 前進・後退)。
 *   見た目の目視用に、ファームと同じ C コードで描いた画面を、パレットを通した PPM で出す(第1引数のディレクトリ。既定 tests/out)。
 * 追加: 押下の暗転・白光、デモ中タグ、進捗バー幅、棒グラフの高さの確認。 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "ui_logic.h"
#include "ui_render.h"
#include "test_framework.h"

#define NPX ((size_t)GFX_STRIDE * GFX_H)
static uint8_t g_a[NPX], g_b[NPX], g_c[NPX], g_d[NPX];

/* 面の (r, c) の添字を、パレットを通したパネルの色(RGB565 に切り捨て)にする */
static uint16_t pix565(const uint8_t *fb, size_t r, size_t c)
{
    uint32_t a = ui_pal[fb[r * GFX_STRIDE + c]];
    return (uint16_t)((((a >> 19) & 31u) << 11) | (((a >> 10) & 63u) << 5) | ((a >> 3) & 31u));
}

static void mk(ui_t *u, uint8_t scr, int done)
{
    ui_init(u, 0u, 479u, 271u, 0u);
    u->v.scr = scr;
    u->v.clock_min = 21u * 60u + 45u;
    switch (scr) {
    case UI_SCR_FILL:   u->v.fill_x10 = done ? 1000u : 555u; break;
    case UI_SCR_REHEAT: u->v.bath_x10 = done ? 420u : 391u; break;
    case UI_SCR_USAGE:  u->v.flow_x10 = 103u; u->v.bar_ms = (uint16_t)(done ? UI_BAR_END_MS : 200u); break;
    default: break;
    }
}

/* 左 480 列の全画素を書く / 右の 32 列(480..511)は 1 バイトも書かない */
static int all_written(const ui_t *u)
{
    size_t r, c;
    memset(g_a, 0x00, sizeof(g_a));
    memset(g_b, 0xFF, sizeof(g_b));
    ui_render(u, g_a);
    ui_render(u, g_b);
    for (r = 0u; r < GFX_H; r++) {
        if (memcmp(&g_a[r * GFX_STRIDE], &g_b[r * GFX_STRIDE], GFX_W) != 0) { return 0; }
        for (c = GFX_W; c < GFX_STRIDE; c++) {
            if ((g_a[r * GFX_STRIDE + c] != 0x00u) || (g_b[r * GFX_STRIDE + c] != 0xFFu)) { return 0; }
        }
    }
    return 1;
}

static void ppm(const char *dir, const char *name, const uint8_t *fb)
{
    char path[512];
    FILE *f;
    size_t i;
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    if (f == NULL) { printf("  (PPM を書けない: %s)\n", path); return; }
    fprintf(f, "P6\n480 272\n255\n");
    for (i = 0u; i < (size_t)GFX_W * GFX_H; i++) {
        uint16_t p = pix565(fb, i / GFX_W, i % GFX_W);
        unsigned char rgb[3];
        rgb[0] = (unsigned char)(((p >> 11) << 3) | ((p >> 13) & 7u));
        rgb[1] = (unsigned char)((((p >> 5) & 63u) << 2) | ((p >> 9) & 3u));
        rgb[2] = (unsigned char)(((p & 31u) << 3) | ((p >> 2) & 7u));
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static unsigned diff_count(const uint8_t *x, const uint8_t *y)
{
    size_t r, c; unsigned n = 0u;
    for (r = 0u; r < GFX_H; r++) { for (c = 0u; c < GFX_W; c++) { if (x[r * GFX_STRIDE + c] != y[r * GFX_STRIDE + c]) { n++; } } }
    return n;
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "tests/out";
    static ui_t u, o;
    unsigned a, b, d, fwd, bad = 0u, n = 0u;
    static const uint16_t dxs[] = { 0u, 1u, 120u, 240u, 479u, 480u };

    TEST("H-G3a 5画面 x 状態(running / done)で全画素を書く(0x00 / 0xFF の2回の結果が同じ)");
    for (a = 0u; a < UI_SCR_COUNT; a++) {
        for (d = 0u; d < 2u; d++) {
            mk(&u, (uint8_t)a, (int)d);
            CHECK(all_written(&u));
            u.demo = 1u;
            CHECK(all_written(&u));
        }
    }

    TEST("H-G3b 遷移(古い画面 x 新しい画面 x 状態 x dx x 前進/後退)で全画素を書く");
    for (a = 0u; a < UI_SCR_COUNT; a++) {
        for (b = 0u; b < UI_SCR_COUNT; b++) {
            for (d = 0u; d < 2u; d++) {
                for (fwd = 0u; fwd < 2u; fwd++) {
                    unsigned k;
                    for (k = 0u; k < sizeof(dxs) / sizeof(dxs[0]); k++) {
                        mk(&u, (uint8_t)b, (int)d);
                        mk(&o, (uint8_t)a, (int)(1u - d));
                        u.ov = o.v;
                        u.tr_on = 1u; u.tr_fwd = (uint8_t)fwd; u.tr_dx = dxs[k];
                        u.demo = (uint8_t)(k & 1u);
                        n++;
                        if (!all_written(&u)) { bad++; }
                    }
                }
            }
        }
    }
    printf("  (参考) 試した組合せ = %u\n", n);
    CHECK_EQ(bad, 0);

    TEST("H-G3c 値の極端(水位 0 / 1000、追い焚き 350..480、流量、時計 0:00 / 23:59、棒 0..640)でも全画素を書く");
    {
        uint16_t v;
        int ok = 1;
        for (v = 0u; v <= 1000u; v = (uint16_t)(v + 37u)) {
            mk(&u, UI_SCR_FILL, 0); u.v.fill_x10 = v; if (!all_written(&u)) { ok = 0; }
        }
        for (v = 350u; v <= 480u; v = (uint16_t)(v + 5u)) {
            mk(&u, UI_SCR_REHEAT, 0); u.v.bath_x10 = v; u.v.set = (uint8_t)(v / 10u); if (!all_written(&u)) { ok = 0; }
            mk(&u, UI_SCR_REHEAT, 0); u.v.bath_x10 = v; u.v.set = 48u; if (!all_written(&u)) { ok = 0; }
        }
        for (v = 0u; v <= UI_BAR_END_MS; v = (uint16_t)(v + 20u)) {
            mk(&u, UI_SCR_USAGE, 0); u.v.bar_ms = v; if (!all_written(&u)) { ok = 0; }
        }
        mk(&u, UI_SCR_HOME, 0); u.v.clock_min = 0u; if (!all_written(&u)) { ok = 0; }
        u.v.clock_min = 1439u; if (!all_written(&u)) { ok = 0; }
        mk(&u, UI_SCR_STANDBY, 0); u.v.clock_min = 0u; if (!all_written(&u)) { ok = 0; }
        CHECK(ok);
    }

    TEST("見た目の PPM を出力(各画面・押下・白光・スライド途中・デモ中)");
    mk(&u, UI_SCR_HOME, 0);     ui_render(&u, g_a); ppm(dir, "home", g_a);
    mk(&u, UI_SCR_FILL, 0);     ui_render(&u, g_a); ppm(dir, "fill_run", g_a);
    mk(&u, UI_SCR_FILL, 1);     ui_render(&u, g_a); ppm(dir, "fill_done", g_a);
    mk(&u, UI_SCR_REHEAT, 0);   ui_render(&u, g_a); ppm(dir, "reheat_run", g_a);
    mk(&u, UI_SCR_REHEAT, 1);   ui_render(&u, g_a); ppm(dir, "reheat_done", g_a);
    mk(&u, UI_SCR_USAGE, 1);    ui_render(&u, g_a); ppm(dir, "usage", g_a);
    mk(&u, UI_SCR_USAGE, 0);    ui_render(&u, g_a); ppm(dir, "usage_growing", g_a);
    mk(&u, UI_SCR_STANDBY, 0);  ui_render(&u, g_a); ppm(dir, "standby", g_a);
    mk(&u, UI_SCR_HOME, 0); u.fb_phase = UI_PH_PRESS; u.fb_btn = UI_BTN_H_FILL; ui_render(&u, g_a); ppm(dir, "home_press_fill", g_a);
    mk(&u, UI_SCR_HOME, 0); u.fb_phase = UI_PH_FLASH; u.fb_btn = UI_BTN_H_UP; ui_render(&u, g_a); ppm(dir, "home_flash_up", g_a);
    mk(&u, UI_SCR_HOME, 0); u.demo = 1u; ui_render(&u, g_a); ppm(dir, "home_demo", g_a);
    mk(&u, UI_SCR_FILL, 0); u.demo = 1u; ui_render(&u, g_a); ppm(dir, "fill_demo", g_a);
    mk(&u, UI_SCR_FILL, 0); mk(&o, UI_SCR_HOME, 0); u.ov = o.v; u.tr_on = 1u; u.tr_fwd = 1u; u.tr_dx = 240u;
    ui_render(&u, g_a); ppm(dir, "slide_home_to_fill_dx240", g_a);
    mk(&u, UI_SCR_HOME, 0); mk(&o, UI_SCR_REHEAT, 1); u.ov = o.v; u.tr_on = 1u; u.tr_fwd = 0u; u.tr_dx = 120u; u.demo = 1u;
    ui_render(&u, g_a); ppm(dir, "slide_reheat_to_home_dx120", g_a);
    CHECK(1);

    TEST("押下の暗転 / 離した時の白光: ボタンの中だけが変わり、外は変わらない。デモ中タグは (228,5) の 84x24 だけ");
    {
        unsigned r, c, outside = 0u, inside_dark = 0u, inside_light = 0u;
        const ui_btn_def_t *bd = &ui_btn_defs[UI_BTN_H_FILL];
        mk(&u, UI_SCR_HOME, 0); ui_render(&u, g_a);
        u.fb_phase = UI_PH_PRESS; u.fb_btn = UI_BTN_H_FILL; ui_render(&u, g_b);
        u.fb_phase = UI_PH_FLASH; ui_render(&u, g_c);
        for (r = 0u; r < GFX_H; r++) {
            for (c = 0u; c < GFX_W; c++) {
                int in = ((int)c >= bd->vis.x) && ((int)c < bd->vis.x + bd->vis.w) && ((int)r >= bd->vis.y) && ((int)r < bd->vis.y + bd->vis.h);
                size_t i = (size_t)r * GFX_STRIDE + c;
                if (!in) { if ((g_a[i] != g_b[i]) || (g_a[i] != g_c[i])) { outside++; } }
                else {
                    uint16_t pa = pix565(g_a, r, c), pb = pix565(g_b, r, c), pc = pix565(g_c, r, c);
                    unsigned ya = ((pa >> 11) & 31u) + ((pa >> 6) & 31u) + (pa & 31u);
                    unsigned yb = ((pb >> 11) & 31u) + ((pb >> 6) & 31u) + (pb & 31u);
                    unsigned yc = ((pc >> 11) & 31u) + ((pc >> 6) & 31u) + (pc & 31u);
                    if (yb < ya) { inside_dark++; }
                    if (yc > ya) { inside_light++; }
                }
            }
        }
        CHECK_EQ(outside, 0);
        CHECK(inside_dark > 5000u);       /* 106x64 = 6784 画素のほとんど */
        CHECK(inside_light > 3000u);       /* ボタン背景が薄い青なので、白光で明るくなる画素が多い(文字の濃い所も) */
        /* 角(外側の角丸の外)は変わらない */
        CHECK_EQ(g_a[(size_t)bd->vis.y * GFX_STRIDE + (size_t)bd->vis.x], g_b[(size_t)bd->vis.y * GFX_STRIDE + (size_t)bd->vis.x]);
        mk(&u, UI_SCR_HOME, 0); ui_render(&u, g_a);
        u.demo = 1u; ui_render(&u, g_b);
        outside = 0u;
        for (r = 0u; r < GFX_H; r++) {
            for (c = 0u; c < GFX_W; c++) {
                int in = ((int)c >= UI_TAG_X) && ((int)c < UI_TAG_X + UI_TAG_W) && ((int)r >= UI_TAG_Y) && ((int)r < UI_TAG_Y + UI_TAG_H);
                if (!in && (g_a[(size_t)r * GFX_STRIDE + c] != g_b[(size_t)r * GFX_STRIDE + c])) { outside++; }
            }
        }
        CHECK_EQ(outside, 0);
        CHECK(diff_count(g_a, g_b) > 500u);
        mk(&u, UI_SCR_STANDBY, 0); ui_render(&u, g_a); u.demo = 1u; ui_render(&u, g_b);
        CHECK_EQ(diff_count(g_a, g_b), 0);           /* 待機にはタグを出さない */
    }

    TEST("進捗バー: 幅 = 432 x 水位(最小 20)。右端の列が進みの色、その先は溝の色");
    {
        uint16_t w;
        int ok = 1;
        uint8_t groove, bar;
        mk(&u, UI_SCR_FILL, 0); u.v.fill_x10 = 0u; ui_render(&u, g_a);
        groove = g_a[(size_t)(94 + 19) * GFX_STRIDE + 24 + 300];                 /* 溝(バーの右の方) */
        mk(&u, UI_SCR_FILL, 0); u.v.fill_x10 = 800u; ui_render(&u, g_a);
        bar = g_a[(size_t)(94 + 30) * GFX_STRIDE + 24 + 200];                     /* 進み(文字から離れた下の方) */
        CHECK(bar != groove);
        for (w = 0u; w <= 1000u; w = (uint16_t)(w + 25u)) {
            unsigned px = ui_fill_w(w);
            unsigned eff = (px < 20u) ? 20u : px;
            mk(&u, UI_SCR_FILL, 0); u.v.fill_x10 = w; ui_render(&u, g_a);
            /* バーの下の方の行(y=94+34。数字「NN%」は y=105 より上)で、最右の「進みの色」の位置を探す */
            {
                unsigned c, last = 0u;
                for (c = 0u; c < 432u; c++) { if (g_a[(size_t)(94 + 33) * GFX_STRIDE + 24 + c] == bar) { last = c + 1u; } }
                if ((last + 3u < eff) || (last > eff)) { ok = 0; printf("  w=%u: 幅 %u を期待、実際の最右 %u\n", w, eff, last); }
            }
        }
        CHECK(ok);
    }

    TEST("使用量の棒グラフ: 最終の高さ = 設計の {42,34,50,31,47,54,44}、最初は 0 から伸びる");
    {
        unsigned i, c, r, h;
        int ok = 1;
        mk(&u, UI_SCR_USAGE, 1); ui_render(&u, g_a);
        for (i = 0u; i < UI_USAGE_N; i++) {
            c = 24u + 64u * i + 20u;
            h = 0u;   /* 土台(y = 205)から上へ、棒の色が続く行数(背景の文字の縁が同じ添字になることがあるので、全体は数えない) */
            for (r = 205u; (r < 206u) && (g_a[(size_t)r * GFX_STRIDE + c] == ((i == 6u) ? UI_I_BAR_U_TODAY : UI_I_BAR_U)); r--) { h++; }
            if (h != ui_usage_h[i]) { ok = 0; printf("  棒 %u: 高さ %u (期待 %u)\n", i, h, ui_usage_h[i]); }
        }
        CHECK(ok);
        CHECK_EQ(ui_bar_h(0u, 0u), 0);
        CHECK_EQ(ui_bar_h(6u, 239u), 0);
        CHECK_EQ(ui_bar_h(6u, 640u), 44);
        CHECK_EQ(ui_bar_h(0u, 400u), 42);
    }

    TEST("H-U5c スライド: 全画素が、古い画面と新しい画面の単独の描画を、設計どおりに平行移動して並べたものと一致する");
    {
        unsigned bad2 = 0u, cnt = 0u, k;
        for (a = 0u; a < UI_SCR_COUNT; a++) {
            for (b = 0u; b < UI_SCR_COUNT; b++) {
                for (fwd = 0u; fwd < 2u; fwd++) {
                    for (k = 1u; k < 480u; k += 119u) {
                        static ui_t p;
                        unsigned r, c;
                        mk(&u, (uint8_t)b, 1); mk(&o, (uint8_t)a, 0);
                        u.ov = o.v; u.tr_on = 1u; u.tr_fwd = (uint8_t)fwd; u.tr_dx = (uint16_t)k; u.demo = 1u;
                        ui_render(&u, g_a);
                        p = u; p.tr_on = 0u; p.v = u.ov; ui_render(&p, g_b);     /* 古い画面だけ */
                        p.v = u.v; ui_render(&p, g_c);                             /* 新しい画面だけ */
                        for (r = 0u; r < GFX_H; r++) {
                            for (c = 0u; c < GFX_W; c++) {
                                unsigned want;
                                if (fwd != 0u) { want = (c < 480u - k) ? g_b[(size_t)r * GFX_STRIDE + c + k] : g_c[(size_t)r * GFX_STRIDE + c - (480u - k)]; }
                                else           { want = (c < k) ? g_c[(size_t)r * GFX_STRIDE + c + 480u - k] : g_b[(size_t)r * GFX_STRIDE + c - k]; }
                                if (g_a[(size_t)r * GFX_STRIDE + c] != want) { bad2++; }
                            }
                        }
                        cnt++;
                    }
                }
            }
        }
        printf("  (参考) 試した組合せ = %u\n", cnt);
        CHECK_EQ(bad2, 0);
    }
    (void)g_d;

    TEST_REPORT("test_ui_render");
}
