/* test_gfx.c - src/app/gfx.c の単体テスト(設計 §12 H-G1, H-G2、§20.9 H-D2)。
 *   H-G1: gfx_a8 が浮動小数の基準値と各チャンネル ±2(565 の各チャンネルの単位)以内で一致。scale256=0 で変化なし、
 *         scale256=256 かつ濃度 255 で文字色。画面の外へ 1画素も書かない(前後に番兵つきの配列)。
 *   H-G2: gfx_blit565 / gfx_fillrect の切り取り(x が負、はみ出し、幅 0 など)。
 *   H-D2: 8bpp 版 gfx_blit8 / gfx_fill8 / gfx_a8_ramp / gfx_remap8: クリップ、面の前後と右の 32 列(480..511)へ 1 バイトも書かない、
 *         l = 0 で不変、alt の分岐、th の境界(127 / 128)。 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gfx.h"
#include "test_framework.h"

#define GUARD   (64u)
#define N       ((size_t)GFX_W * GFX_H)
#define POISON  (0xA5A5u)

/* 前後に番兵つきの面 */
typedef struct { uint16_t pre[GUARD]; uint16_t px[GFX_W * GFX_H]; uint16_t post[GUARD]; } surf_t;
static surf_t g_s;

static void surf_fill(uint16_t v)
{
    size_t i;
    for (i = 0u; i < GUARD; i++) { g_s.pre[i] = POISON; g_s.post[i] = POISON; }
    for (i = 0u; i < N; i++) { g_s.px[i] = v; }
}
static int guard_ok(void)
{
    size_t i;
    for (i = 0u; i < GUARD; i++) { if ((g_s.pre[i] != POISON) || (g_s.post[i] != POISON)) { return 0; } }
    return 1;
}
static unsigned count_not(uint16_t v)
{
    size_t i; unsigned n = 0u;
    for (i = 0u; i < N; i++) { if (g_s.px[i] != v) { n++; } }
    return n;
}

/* 8bpp の面(行の長さ GFX_STRIDE)。前後に番兵。右の 32 列(480..511)は fill で下地と同じ値にし、書かれたら margin8_ok が偽になる */
typedef struct { uint8_t pre[GUARD]; uint8_t px[GFX_STRIDE * GFX_H]; uint8_t post[GUARD]; } surf8_t;
static surf8_t g_s8;

static void surf8_fill(uint8_t v)
{
    size_t i;
    for (i = 0u; i < GUARD; i++) { g_s8.pre[i] = 0xA5u; g_s8.post[i] = 0xA5u; }
    for (i = 0u; i < sizeof(g_s8.px); i++) { g_s8.px[i] = v; }
}
static int guard8_ok(void)
{
    size_t i;
    for (i = 0u; i < GUARD; i++) { if ((g_s8.pre[i] != 0xA5u) || (g_s8.post[i] != 0xA5u)) { return 0; } }
    return 1;
}
static unsigned count_not8(uint8_t v)             /* 左 480 列で v でない画素 */
{
    size_t r, c; unsigned n = 0u;
    for (r = 0u; r < GFX_H; r++) { for (c = 0u; c < GFX_W; c++) { if (g_s8.px[r * GFX_STRIDE + c] != v) { n++; } } }
    return n;
}
static int margin8_ok(uint8_t v)                  /* 右の 32 列が v のまま */
{
    size_t r, c;
    for (r = 0u; r < GFX_H; r++) { for (c = GFX_W; c < GFX_STRIDE; c++) { if (g_s8.px[r * GFX_STRIDE + c] != v) { return 0; } } }
    return 1;
}

/* 565 の基準値(浮動小数)との差 */
static int ch_r(uint16_t p) { return (p >> 11) & 31; }
static int ch_g(uint16_t p) { return (p >> 5) & 63; }
static int ch_b(uint16_t p) { return p & 31; }
static int maxdiff(uint16_t got, uint16_t bg, uint16_t fg, double alpha)
{
    double er = ch_r(bg) + (ch_r(fg) - ch_r(bg)) * alpha;
    double eg = ch_g(bg) + (ch_g(fg) - ch_g(bg)) * alpha;
    double eb = ch_b(bg) + (ch_b(fg) - ch_b(bg)) * alpha;
    double d, m = 0.0;
    d = er - ch_r(got); if (d < 0) { d = -d; } if (d > m) { m = d; }
    d = eg - ch_g(got); if (d < 0) { d = -d; } if (d > m) { m = d; }
    d = eb - ch_b(got); if (d < 0) { d = -d; } if (d > m) { m = d; }
    return (int)(m + 0.999);   /* 切り上げ: 差 2.0 以下を合格にする */
}

int main(void)
{
    static uint8_t a8[256];
    unsigned i, k, bad, worst;
    uint16_t bgs[] = { 0x0000u, 0xFFFFu, 0xF7BEu, 0x1F1Fu, 0x07E0u, 0xF800u, 0x001Fu, 0x3A0Au, 0xDF7Du };
    uint16_t fgs[] = { 0xFFFFu, 0x0000u, 0x3B72u, 0xB327u, 0xF81Fu, 0x07FFu };
    unsigned bi, fi;
    uint32_t sc;
    static const uint32_t scales[] = { 0u, 1u, 52u, 128u, 154u, 255u, 256u };

    TEST("H-G1a gfx_a8: 濃度 0..255 x scale x 色の組合せで、基準値との差が各チャンネル 2 以内");
    for (i = 0u; i < 256u; i++) { a8[i] = (uint8_t)i; }
    worst = 0u; bad = 0u;
    for (bi = 0u; bi < sizeof(bgs) / sizeof(bgs[0]); bi++) {
        for (fi = 0u; fi < sizeof(fgs) / sizeof(fgs[0]); fi++) {
            for (k = 0u; k < sizeof(scales) / sizeof(scales[0]); k++) {
                sc = scales[k];
                surf_fill(bgs[bi]);
                gfx_a8(g_s.px, a8, 256, 1, 10, 10, fgs[fi], sc);
                for (i = 0u; i < 256u; i++) {
                    double alpha = ((double)i / 255.0) * ((double)sc / 256.0);
                    int d = maxdiff(g_s.px[10u * GFX_W + 10u + i], bgs[bi], fgs[fi], alpha);
                    if ((unsigned)d > worst) { worst = (unsigned)d; }
                    if (d > 2) { bad++; }
                }
            }
        }
    }
    printf("  (参考) 最大差 = %u\n", worst);
    CHECK_EQ(bad, 0);
    CHECK(guard_ok());

    TEST("H-G1b scale256 = 0 で変化なし / 濃度 0 で変化なし / scale256=256 かつ濃度 255 で文字色 / 範囲外の scale は 256 に丸める");
    surf_fill(0x1234u);
    gfx_a8(g_s.px, a8, 256, 1, 0, 0, 0xFFFFu, 0u);
    CHECK_EQ(count_not(0x1234u), 0);
    memset(a8, 0, sizeof(a8));
    gfx_a8(g_s.px, a8, 16, 16, 0, 0, 0xFFFFu, 256u);
    CHECK_EQ(count_not(0x1234u), 0);
    memset(a8, 255, sizeof(a8));
    gfx_a8(g_s.px, a8, 16, 16, 100, 50, 0xABCDu, 256u);
    CHECK_EQ(count_not(0x1234u), 256);
    CHECK_EQ(g_s.px[50u * GFX_W + 100u], 0xABCD);
    CHECK_EQ(g_s.px[65u * GFX_W + 115u], 0xABCD);
    CHECK_EQ(g_s.px[66u * GFX_W + 115u], 0x1234);
    surf_fill(0x1234u);
    gfx_a8(g_s.px, a8, 16, 16, 100, 50, 0xABCDu, 100000u);
    CHECK_EQ(g_s.px[50u * GFX_W + 100u], 0xABCD);
    CHECK(guard_ok());

    TEST("H-G1c gfx_a8 の切り取り: 全方向のはみ出しで、面の外へ書かず、中は a8 の正しい位置の画素になる");
    {
        static uint8_t m[40 * 30];
        int32_t xs[] = { -50, -39, -20, -1, 0, 1, 440, 441, 479, 480, 600 };
        int32_t ys[] = { -40, -29, -10, -1, 0, 1, 242, 243, 271, 272, 400 };
        unsigned xi, yi, r, c;
        int ok = 1;
        for (i = 0u; i < sizeof(m); i++) { m[i] = 255; }
        m[0] = 0u;                                   /* 左上だけ濃度 0(位置の確認用) */
        for (xi = 0u; xi < sizeof(xs) / sizeof(xs[0]); xi++) {
            for (yi = 0u; yi < sizeof(ys) / sizeof(ys[0]); yi++) {
                surf_fill(0u);
                gfx_a8(g_s.px, m, 40, 30, xs[xi], ys[yi], 0xFFFFu, 256u);
                if (!guard_ok()) { ok = 0; }
                for (r = 0u; r < GFX_H; r++) {
                    for (c = 0u; c < GFX_W; c++) {
                        int32_t mx = (int32_t)c - xs[xi], my = (int32_t)r - ys[yi];
                        int inside = (mx >= 0) && (mx < 40) && (my >= 0) && (my < 30);
                        uint16_t want = (inside && !((mx == 0) && (my == 0))) ? 0xFFFFu : 0u;
                        if (g_s.px[r * GFX_W + c] != want) { ok = 0; }
                    }
                }
            }
        }
        CHECK(ok);
    }

    TEST("H-G1d gfx_a8: w/h が 0 以下・NULL は何もしない");
    surf_fill(7u);
    gfx_a8(g_s.px, a8, 0, 5, 0, 0, 1u, 256u);
    gfx_a8(g_s.px, a8, 5, 0, 0, 0, 1u, 256u);
    gfx_a8(g_s.px, a8, -5, 5, 0, 0, 1u, 256u);
    gfx_a8(g_s.px, NULL, 5, 5, 0, 0, 1u, 256u);
    gfx_a8(NULL, a8, 5, 5, 0, 0, 1u, 256u);
    CHECK_EQ(count_not(7u), 0);
    CHECK(guard_ok());

    TEST("H-G2a gfx_fillrect: 範囲どおり塗る / 切り取り / 幅 0・負 / NULL");
    surf_fill(0u);
    gfx_fillrect(g_s.px, 10, 20, 30, 40, 0xFFFFu);
    CHECK_EQ(count_not(0u), 1200);
    CHECK_EQ(g_s.px[20u * GFX_W + 10u], 0xFFFF);
    CHECK_EQ(g_s.px[59u * GFX_W + 39u], 0xFFFF);
    CHECK_EQ(g_s.px[60u * GFX_W + 39u], 0);
    CHECK_EQ(g_s.px[59u * GFX_W + 40u], 0);
    surf_fill(0u);
    gfx_fillrect(g_s.px, -10, -10, 20, 20, 1u);          /* 左上にはみ出し: 10x10 */
    CHECK_EQ(count_not(0u), 100);
    surf_fill(0u);
    gfx_fillrect(g_s.px, 470, 262, 50, 50, 1u);          /* 右下にはみ出し: 10x10 */
    CHECK_EQ(count_not(0u), 100);
    CHECK_EQ(g_s.px[271u * GFX_W + 479u], 1);
    surf_fill(0u);
    gfx_fillrect(g_s.px, -1000, -1000, 5000, 5000, 2u);  /* 全面 */
    CHECK_EQ(count_not(2u), 0);
    surf_fill(0u);
    gfx_fillrect(g_s.px, 5, 5, 0, 10, 1u);
    gfx_fillrect(g_s.px, 5, 5, 10, 0, 1u);
    gfx_fillrect(g_s.px, 5, 5, -3, 10, 1u);
    gfx_fillrect(g_s.px, 480, 0, 10, 10, 1u);            /* 完全に外 */
    gfx_fillrect(g_s.px, -10, 0, 10, 10, 1u);
    gfx_fillrect(g_s.px, 0, 272, 10, 10, 1u);
    gfx_fillrect(g_s.px, 0, -10, 10, 10, 1u);
    gfx_fillrect(NULL, 0, 0, 10, 10, 1u);
    CHECK_EQ(count_not(0u), 0);
    gfx_fillrect(g_s.px, 2147483000, 0, 2147483000, 10, 1u);   /* 加算あふれを起こしうる値 */
    CHECK(guard_ok());

    TEST("H-G2b gfx_blit565: 範囲どおり・stride・ソース位置の切り取り(左/上にはみ出すとソースも詰める)");
    {
        static uint16_t src[100 * 60];
        unsigned r, c;
        int ok = 1;
        for (r = 0u; r < 60u; r++) { for (c = 0u; c < 100u; c++) { src[r * 100u + c] = (uint16_t)(r * 256u + c + 1u); } }
        surf_fill(0u);
        gfx_blit565(g_s.px, src, 100, 0, 0, 100, 60, 200, 100);
        for (r = 0u; r < 60u; r++) { for (c = 0u; c < 100u; c++) { if (g_s.px[(100u + r) * GFX_W + 200u + c] != src[r * 100u + c]) { ok = 0; } } }
        CHECK(ok);
        CHECK_EQ(count_not(0u), 6000);
        surf_fill(0u);                                          /* 左上にはみ出し: ソースの (30,20) から */
        gfx_blit565(g_s.px, src, 100, 0, 0, 100, 60, -30, -20);
        CHECK_EQ(count_not(0u), 70 * 40);
        CHECK_EQ(g_s.px[0], src[20u * 100u + 30u]);
        CHECK_EQ(g_s.px[39u * GFX_W + 69u], src[59u * 100u + 99u]);
        surf_fill(0u);                                          /* 右下にはみ出し */
        gfx_blit565(g_s.px, src, 100, 0, 0, 100, 60, 440, 250);
        CHECK_EQ(count_not(0u), 40 * 22);
        CHECK_EQ(g_s.px[250u * GFX_W + 440u], src[0]);
        CHECK_EQ(g_s.px[271u * GFX_W + 479u], src[21u * 100u + 39u]);
        surf_fill(0u);                                          /* ソースの一部だけ(sx, sy) */
        gfx_blit565(g_s.px, src, 100, 10, 5, 20, 10, 0, 0);
        CHECK_EQ(count_not(0u), 200);
        CHECK_EQ(g_s.px[0], src[5u * 100u + 10u]);
        CHECK_EQ(g_s.px[9u * GFX_W + 19u], src[14u * 100u + 29u]);
        surf_fill(0u);                                          /* 幅 0・負・完全に外・NULL */
        gfx_blit565(g_s.px, src, 100, 0, 0, 0, 10, 0, 0);
        gfx_blit565(g_s.px, src, 100, 0, 0, 10, -1, 0, 0);
        gfx_blit565(g_s.px, src, 100, 0, 0, 10, 10, 480, 0);
        gfx_blit565(g_s.px, src, 100, 0, 0, 10, 10, -10, 0);
        gfx_blit565(g_s.px, src, 100, 0, 0, 10, 10, 0, 272);
        gfx_blit565(g_s.px, NULL, 100, 0, 0, 10, 10, 0, 0);
        gfx_blit565(NULL, src, 100, 0, 0, 10, 10, 0, 0);
        CHECK_EQ(count_not(0u), 0);
        CHECK(guard_ok());
    }

    /* ================================================================ H-D2: 8bpp 版 */
    TEST("H-D2a gfx_fill8: 範囲どおり / 切り取り / 幅 0・負 / NULL / 右の 32 列と面の前後へ書かない");
    surf8_fill(0u);
    gfx_fill8(g_s8.px, 10, 20, 30, 40, 0xFFu);
    CHECK_EQ(count_not8(0u), 1200);
    CHECK_EQ(g_s8.px[20u * GFX_STRIDE + 10u], 0xFF);
    CHECK_EQ(g_s8.px[59u * GFX_STRIDE + 39u], 0xFF);
    CHECK_EQ(g_s8.px[60u * GFX_STRIDE + 39u], 0);
    CHECK_EQ(g_s8.px[59u * GFX_STRIDE + 40u], 0);
    surf8_fill(0u);
    gfx_fill8(g_s8.px, -10, -10, 20, 20, 1u);            /* 左上にはみ出し: 10x10 */
    CHECK_EQ(count_not8(0u), 100);
    surf8_fill(0u);
    gfx_fill8(g_s8.px, 470, 262, 50, 50, 1u);            /* 右下にはみ出し: 10x10。右の 32 列には出ない */
    CHECK_EQ(count_not8(0u), 100);
    CHECK_EQ(g_s8.px[271u * GFX_STRIDE + 479u], 1);
    CHECK(margin8_ok(0u));
    surf8_fill(0u);
    gfx_fill8(g_s8.px, -1000, -1000, 5000, 5000, 2u);    /* 全面(480 列だけ) */
    CHECK_EQ(count_not8(2u), 0);
    CHECK(margin8_ok(0u));
    surf8_fill(0u);
    gfx_fill8(g_s8.px, 5, 5, 0, 10, 1u);
    gfx_fill8(g_s8.px, 5, 5, 10, 0, 1u);
    gfx_fill8(g_s8.px, 5, 5, -3, 10, 1u);
    gfx_fill8(g_s8.px, 480, 0, 10, 10, 1u);              /* 完全に外(480 以上は右の 32 列だが書かない) */
    gfx_fill8(g_s8.px, -10, 0, 10, 10, 1u);
    gfx_fill8(g_s8.px, 0, 272, 10, 10, 1u);
    gfx_fill8(g_s8.px, 0, -10, 10, 10, 1u);
    gfx_fill8(NULL, 0, 0, 10, 10, 1u);
    CHECK_EQ(count_not8(0u), 0);
    gfx_fill8(g_s8.px, 2147483000, 0, 2147483000, 10, 1u);   /* 加算あふれを起こしうる値 */
    CHECK(guard8_ok());
    CHECK(margin8_ok(0u));

    TEST("H-D2b gfx_blit8: 範囲どおり・stride・ソース位置の切り取り(左/上にはみ出すとソースも詰める)・右の 32 列へ書かない");
    {
        static uint8_t src[100 * 60];
        unsigned r, c;
        int ok = 1;
        for (r = 0u; r < 60u; r++) { for (c = 0u; c < 100u; c++) { src[r * 100u + c] = (uint8_t)((r * 3u + c) % 250u + 1u); } }
        surf8_fill(0u);
        gfx_blit8(g_s8.px, src, 100, 0, 0, 100, 60, 200, 100);
        for (r = 0u; r < 60u; r++) { for (c = 0u; c < 100u; c++) { if (g_s8.px[(100u + r) * GFX_STRIDE + 200u + c] != src[r * 100u + c]) { ok = 0; } } }
        CHECK(ok);
        CHECK_EQ(count_not8(0u), 6000);
        surf8_fill(0u);                                         /* 左上にはみ出し: ソースの (30,20) から */
        gfx_blit8(g_s8.px, src, 100, 0, 0, 100, 60, -30, -20);
        CHECK_EQ(count_not8(0u), 70 * 40);
        CHECK_EQ(g_s8.px[0], src[20u * 100u + 30u]);
        CHECK_EQ(g_s8.px[39u * GFX_STRIDE + 69u], src[59u * 100u + 99u]);
        surf8_fill(0u);                                         /* 右下にはみ出し(480..511 へは出さない) */
        gfx_blit8(g_s8.px, src, 100, 0, 0, 100, 60, 440, 250);
        CHECK_EQ(count_not8(0u), 40 * 22);
        CHECK_EQ(g_s8.px[250u * GFX_STRIDE + 440u], src[0]);
        CHECK_EQ(g_s8.px[271u * GFX_STRIDE + 479u], src[21u * 100u + 39u]);
        CHECK(margin8_ok(0u));
        surf8_fill(0u);                                         /* ソースの一部だけ(sx, sy) */
        gfx_blit8(g_s8.px, src, 100, 10, 5, 20, 10, 0, 0);
        CHECK_EQ(count_not8(0u), 200);
        CHECK_EQ(g_s8.px[0], src[5u * 100u + 10u]);
        CHECK_EQ(g_s8.px[9u * GFX_STRIDE + 19u], src[14u * 100u + 29u]);
        surf8_fill(0u);                                         /* 幅 0・負・完全に外・NULL */
        gfx_blit8(g_s8.px, src, 100, 0, 0, 0, 10, 0, 0);
        gfx_blit8(g_s8.px, src, 100, 0, 0, 10, -1, 0, 0);
        gfx_blit8(g_s8.px, src, 100, 0, 0, 10, 10, 480, 0);
        gfx_blit8(g_s8.px, src, 100, 0, 0, 10, 10, -10, 0);
        gfx_blit8(g_s8.px, src, 100, 0, 0, 10, 10, 0, 272);
        gfx_blit8(g_s8.px, NULL, 100, 0, 0, 10, 10, 0, 0);
        gfx_blit8(NULL, src, 100, 0, 0, 10, 10, 0, 0);
        CHECK_EQ(count_not8(0u), 0);
        CHECK(guard8_ok());
        CHECK(margin8_ok(0u));
    }

    TEST("H-D2c gfx_a8_ramp: 濃度 a = 0..255 の全値で、l = (a + 4) >> 3 が 0 なら不変(a <= 3)、それ以外は ramp0[l]。alt の分岐");
    {
        static uint8_t a8r[256];
        static uint8_t rp0[33], rp1[33];
        unsigned a, l, bad0 = 0u, bad1 = 0u;
        for (l = 0u; l < 33u; l++) { rp0[l] = (uint8_t)(100u + l); rp1[l] = (uint8_t)(200u + l); }
        for (a = 0u; a < 256u; a++) { a8r[a] = (uint8_t)a; }
        surf8_fill(7u);                                         /* 下地 7(alt = -1: ramp0 だけ) */
        gfx_a8_ramp(g_s8.px, a8r, 256, 1, 10, 10, rp0, NULL, -1);
        for (a = 0u; a < 256u; a++) {
            unsigned want = (((a + 4u) >> 3) == 0u) ? 7u : rp0[(a + 4u) >> 3];
            if (g_s8.px[10u * GFX_STRIDE + 10u + a] != want) { bad0++; }
        }
        CHECK_EQ(bad0, 0);
        CHECK_EQ(g_s8.px[10u * GFX_STRIDE + 10u + 3u], 7);       /* a = 3 -> l = 0 */
        CHECK_EQ(g_s8.px[10u * GFX_STRIDE + 10u + 4u], 100 + 1); /* a = 4 -> l = 1 */
        CHECK_EQ(g_s8.px[10u * GFX_STRIDE + 10u + 255u], 100 + 32);
        CHECK_EQ(count_not8(7u), 252);
        /* alt = 50: 下地が 50 の画素だけ ramp1。それ以外(7)は ramp0 */
        surf8_fill(7u);
        for (a = 0u; a < 256u; a++) { g_s8.px[20u * GFX_STRIDE + 10u + a] = (uint8_t)((a & 1u) ? 50u : 7u); }
        gfx_a8_ramp(g_s8.px, a8r, 256, 1, 10, 20, rp0, rp1, 50);
        for (a = 0u; a < 256u; a++) {
            unsigned under = (a & 1u) ? 50u : 7u;
            unsigned lv = (a + 4u) >> 3;
            unsigned want = (lv == 0u) ? under : ((under == 50u) ? rp1[lv] : rp0[lv]);
            if (g_s8.px[20u * GFX_STRIDE + 10u + a] != want) { bad1++; }
        }
        CHECK_EQ(bad1, 0);
        /* alt = 0 も有効な添字(alt >= 0)。-1 だけが「なし」 */
        surf8_fill(0u);
        gfx_a8_ramp(g_s8.px, a8r, 256, 1, 10, 10, rp0, rp1, 0);
        CHECK_EQ(g_s8.px[10u * GFX_STRIDE + 10u + 255u], 200 + 32);
        /* 不正な引数は何もしない: NULL、alt >= 0 で ramp1 が NULL、w/h <= 0 */
        surf8_fill(7u);
        gfx_a8_ramp(NULL, a8r, 256, 1, 0, 0, rp0, rp1, 0);
        gfx_a8_ramp(g_s8.px, NULL, 256, 1, 0, 0, rp0, rp1, 0);
        gfx_a8_ramp(g_s8.px, a8r, 256, 1, 0, 0, NULL, rp1, 0);
        gfx_a8_ramp(g_s8.px, a8r, 256, 1, 0, 0, rp0, NULL, 5);
        gfx_a8_ramp(g_s8.px, a8r, 0, 1, 0, 0, rp0, rp1, 0);
        gfx_a8_ramp(g_s8.px, a8r, 5, -1, 0, 0, rp0, rp1, 0);
        CHECK_EQ(count_not8(7u), 0);
        CHECK(guard8_ok());
    }

    TEST("H-D2d gfx_a8_ramp / gfx_remap8 の切り取り: 全方向のはみ出しで、面の外と右の 32 列へ書かず、中は a8 の正しい位置の画素になる");
    {
        static uint8_t m[40 * 30];
        static uint8_t rp[33], mp[256];
        int32_t xs[] = { -50, -39, -20, -1, 0, 1, 440, 441, 479, 480, 600 };
        int32_t ys[] = { -40, -29, -10, -1, 0, 1, 242, 243, 271, 272, 400 };
        unsigned xi, yi, r, c, which;
        int ok = 1;
        for (c = 0u; c < 33u; c++) { rp[c] = 0xC0u; }
        for (c = 0u; c < 256u; c++) { mp[c] = 0xC0u; }
        for (c = 0u; c < sizeof(m); c++) { m[c] = 255u; }
        m[0] = 0u;                                   /* 左上だけ濃度 0(位置の確認用) */
        for (which = 0u; which < 2u; which++) {
            for (xi = 0u; xi < sizeof(xs) / sizeof(xs[0]); xi++) {
                for (yi = 0u; yi < sizeof(ys) / sizeof(ys[0]); yi++) {
                    surf8_fill(0u);
                    if (which == 0u) { gfx_a8_ramp(g_s8.px, m, 40, 30, xs[xi], ys[yi], rp, NULL, -1); }
                    else             { gfx_remap8(g_s8.px, m, 40, 30, xs[xi], ys[yi], mp, 128u); }
                    if (!guard8_ok() || !margin8_ok(0u)) { ok = 0; }
                    for (r = 0u; r < GFX_H; r++) {
                        for (c = 0u; c < GFX_W; c++) {
                            int32_t mx = (int32_t)c - xs[xi], my = (int32_t)r - ys[yi];
                            int inside = (mx >= 0) && (mx < 40) && (my >= 0) && (my < 30);
                            uint8_t want = (inside && !((mx == 0) && (my == 0))) ? 0xC0u : 0u;
                            if (g_s8.px[r * GFX_STRIDE + c] != want) { ok = 0; }
                        }
                    }
                }
            }
        }
        CHECK(ok);
    }

    TEST("H-D2e gfx_remap8: a8 >= th の画素だけ map[p] にする。境界 127 / 128、th = 0 / 255、map は 256 要素すべて引ける、不正な引数は何もしない");
    {
        static uint8_t a8r[256];
        static uint8_t mp[256];
        unsigned a, p, bad = 0u;
        for (a = 0u; a < 256u; a++) { a8r[a] = (uint8_t)a; mp[a] = (uint8_t)(a ^ 0x55u); }
        surf8_fill(0u);
        for (a = 0u; a < 256u; a++) { g_s8.px[10u * GFX_STRIDE + 10u + a] = (uint8_t)a; }       /* 下地 = 添字 a */
        gfx_remap8(g_s8.px, a8r, 256, 1, 10, 10, mp, 128u);
        for (a = 0u; a < 256u; a++) {
            unsigned want = (a >= 128u) ? (a ^ 0x55u) : a;
            if (g_s8.px[10u * GFX_STRIDE + 10u + a] != want) { bad++; }
        }
        CHECK_EQ(bad, 0);
        CHECK_EQ(g_s8.px[10u * GFX_STRIDE + 10u + 127u], 127);               /* 127 は変えない */
        CHECK_EQ(g_s8.px[10u * GFX_STRIDE + 10u + 128u], 128 ^ 0x55);         /* 128 は変える */
        surf8_fill(0u);
        for (p = 0u; p < 256u; p++) { g_s8.px[30u * GFX_STRIDE + p] = (uint8_t)p; a8r[p] = 255u; }
        gfx_remap8(g_s8.px, a8r, 256, 1, 0, 30, mp, 255u);                    /* th = 255: 濃度 255 は全部変える。添字 0..255 すべてが map を引く */
        for (p = 0u; p < 256u; p++) { if (g_s8.px[30u * GFX_STRIDE + p] != (p ^ 0x55u)) { bad++; } }
        CHECK_EQ(bad, 0);
        surf8_fill(9u);
        a8r[0] = 0u;
        gfx_remap8(g_s8.px, a8r, 256, 1, 0, 30, mp, 0u);                      /* th = 0: 濃度 0 も含め全部変える */
        CHECK_EQ(g_s8.px[30u * GFX_STRIDE + 0u], 9 ^ 0x55);
        surf8_fill(9u);
        gfx_remap8(NULL, a8r, 256, 1, 0, 0, mp, 128u);
        gfx_remap8(g_s8.px, NULL, 256, 1, 0, 0, mp, 128u);
        gfx_remap8(g_s8.px, a8r, 256, 1, 0, 0, NULL, 128u);
        gfx_remap8(g_s8.px, a8r, 0, 1, 0, 0, mp, 128u);
        gfx_remap8(g_s8.px, a8r, 5, 0, 0, 0, mp, 128u);
        CHECK_EQ(count_not8(9u), 0);
        CHECK(guard8_ok());
    }

    TEST_REPORT("test_gfx");
}
