/* test_clut8_pat.c - src/app/clut8_pat.c のホスト試験(docs/design.md §19.3.6 の H-C8-1〜4、H-C8-7。H-C8-5 は run.sh S18 の grep)。
 * 期待値は設計 §19.2 の表から、clut8_pat.c とは別の書き方(画素ごとの優先順位)で作る。
 * ビルド: gcc -DAPP_CLUT8_TEST=1 ... (clut8_sw.h: 0 の時 clut8_pat.c は中身が消える) */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#include "test_framework.h"
#include "clut8_pat.h"

#define FB_N   (512u * 272u)
#define PX(f, x, y) ((f)[((y) * 512u) + (x)])

static uint8_t  g_a[FB_N], g_b[FB_N];
static uint32_t g_pa[256], g_pb[256], g_pbg[256];

/* §19.2 の表を、優先順位の高い方から見る形で書いた期待値 */
static uint8_t exp_a(unsigned x, unsigned y)
{
    unsigned k;

    if (x >= 480u) { return 9u; }                                                   /* 7 余り */
    if ((x < 2u) || (x >= 478u) || (y < 2u) || (y >= 270u)) { return 1u; }          /* 6 枠 */
    if ((x >= 432u) && (x <= 471u) && (y >= 240u) && (y <= 263u)) { return 10u; }   /* 5 マーカー */
    if ((y >= 120u) && (((x % 16u) == 0u) || (((y - 120u) % 16u) == 0u))) { return 1u; }   /* 4 格子 */
    if ((y >= 48u) && (y <= 111u)) {                                                /* 3 箱 */
        static const uint8_t v[8] = { 2, 3, 4, 5, 6, 7, 1, 8 };
        for (k = 0u; k < 8u; k++) {
            if ((x >= (8u + (58u * k))) && (x <= (8u + (58u * k) + 53u))) { return v[k]; }
        }
    }
    if ((y >= 8u) && (y <= 39u)) { return (uint8_t)(16u + (x / 2u)); }              /* 2 階段 */
    return 0u;                                                                      /* 1 全体 */
}

static uint16_t rgb565(uint32_t argb)
{
    return (uint16_t)((((argb >> 19) & 31u) << 11) | (((argb >> 10) & 63u) << 5) | ((argb >> 3) & 31u));
}

static void test_face(void)
{
    unsigned x, y, bad = 0u, k;
    bool     seen[256];

    c8pat_face(g_a, false);
    c8pat_face(g_b, true);

    TEST("H-C8-1 面A: 全画素が §19.2 の表どおり(512 x 272)");
    for (y = 0u; y < 272u; y++) {
        for (x = 0u; x < 512u; x++) {
            if (PX(g_a, x, y) != exp_a(x, y)) { if (bad < 5u) { printf("  差 x=%u y=%u: %u vs %u\n", x, y, PX(g_a, x, y), exp_a(x, y)); } bad++; }
        }
    }
    CHECK_EQ(bad, 0);

    TEST("H-C8-1 階段: 17〜254 の 238 個が出る(16 と 255 は左右の枠 x = 0,1 / 478,479 が上書きして面Aに出ない = 設計 §19.2 の差。255 は面Bの背景で読まれる)。2画素ごとに 1段");
    memset(seen, 0, sizeof(seen));
    for (x = 0u; x < 480u; x++) { seen[PX(g_a, x, 20u)] = true; }
    for (k = 17u; k < 255u; k++) { CHECK(seen[k]); }
    CHECK(!seen[16] && !seen[255]);
    CHECK_EQ(PX(g_a, 0, 20), 1);                     /* 枠 */
    CHECK_EQ(PX(g_a, 2, 20), 17);
    CHECK_EQ(PX(g_a, 3, 20), 17);
    CHECK_EQ(PX(g_a, 477, 20), 254);
    CHECK_EQ(PX(g_a, 478, 20), 1);                   /* 枠(x = 478, 479)が優先 */

    TEST("H-C8-1 箱8個・マーカー・余り");
    for (k = 0u; k < 8u; k++) {
        static const uint8_t v[8] = { 2, 3, 4, 5, 6, 7, 1, 8 };
        unsigned x0 = 8u + (58u * k);
        CHECK_EQ(PX(g_a, x0, 48), v[k]);
        CHECK_EQ(PX(g_a, x0 + 53u, 111), v[k]);
        CHECK_EQ(PX(g_a, x0 + 54u, 80), 0);          /* 箱の間は黒 */
        CHECK_EQ(PX(g_a, x0 + 27u, 80), v[k]);       /* 中心(c8png.py sample の位置) */
        CHECK_EQ(c8pat_box_idx[k], v[k]);
    }
    CHECK_EQ(PX(g_a, 432, 240), 10);
    CHECK_EQ(PX(g_a, 471, 263), 10);
    CHECK_EQ(PX(g_a, 431, 250), 0);                  /* マーカーの外は黒(432 = 27 x 16 の縦線はマーカーが上書き) */
    for (y = 0u; y < 272u; y++) { for (x = 480u; x < 512u; x++) { CHECK_EQ(PX(g_a, x, y), 9); } }

    TEST("H-C8-2 面B = 全バイト 255 - 面A(余りは 246)");
    bad = 0u;
    for (x = 0u; x < FB_N; x++) { if (g_b[x] != (uint8_t)(255u - g_a[x])) { bad++; } }
    CHECK_EQ(bad, 0);
    CHECK_EQ(PX(g_b, 500, 100), 246);
}

static void test_pal(void)
{
    unsigned i, x, y, diff = 0u, cross = 0u, total = 0u;
    static const uint32_t col[16] = { 0xFF000000u, 0xFFFFFFFFu, 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFF00u,
                                      0xFF00FFFFu, 0xFFFF00FFu, 0xFF808080u, 0xFFFF8000u, 0xFFFF0000u,
                                      0xFF000000u, 0xFF000000u, 0xFF000000u, 0xFF000000u, 0xFF000000u };

    c8pat_face(g_a, false);
    c8pat_face(g_b, true);
    c8pat_pal(g_pa, false, false);
    c8pat_pal(g_pb, true, false);
    c8pat_pal(g_pbg, true, true);

    TEST("H-C8-4 パレットA: idx 0〜15 の色、16〜255 は単調増加の灰(R = G = B)、A = FF");
    for (i = 0u; i < 16u; i++) { CHECK_EQ(g_pa[i], col[i]); }
    for (i = 16u; i < 256u; i++) {
        uint32_t g = (g_pa[i] >> 8) & 0xFFu;
        CHECK_EQ(g_pa[i], 0xFF000000u | (g << 16) | (g << 8) | g);
        CHECK_EQ(g, (long long)floor(((double)(i - 16u) * 255.0 / 239.0) + 0.5));
        if (i > 16u) { CHECK(g >= (((g_pa[i - 1u]) >> 8) & 0xFFu)); }
    }
    CHECK_EQ(g_pa[16], 0xFF000000u);
    CHECK_EQ(g_pa[255], 0xFFFFFFFFu);
    for (i = 0u; i < 256u; i++) { CHECK_EQ(g_pa[i] >> 24, 0xFF); }

    TEST("H-C8-2 パレットB[255 - i] = パレットA[i](全256個)。marker_green は b = false では無効");
    for (i = 0u; i < 256u; i++) { CHECK_EQ(g_pb[255u - i], g_pa[i]); }
    c8pat_pal(g_pbg, false, true);
    CHECK(memcmp(g_pbg, g_pa, sizeof(g_pa)) == 0);
    c8pat_pal(g_pbg, true, true);
    for (i = 0u; i < 256u; i++) { if (g_pbg[i] != g_pb[i]) { diff++; CHECK_EQ(i, 245); } }
    CHECK_EQ(diff, 1);
    CHECK_EQ(g_pbg[245], 0xFF00FF00u);

    TEST("H-C8-3 不変条件: 全 480 x 272 画素で palA[faceA] == palB[faceB](マーカー赤)");
    diff = 0u;
    for (y = 0u; y < 272u; y++) { for (x = 0u; x < 480u; x++) { if (g_pa[PX(g_a, x, y)] != g_pb[PX(g_b, x, y)]) { diff++; } } }
    CHECK_EQ(diff, 0);
    TEST("H-C8-3 マーカー緑: 差はマーカーの 40 x 24 = 960 画素だけ");
    diff = 0u;
    for (y = 0u; y < 272u; y++) {
        for (x = 0u; x < 480u; x++) {
            if (g_pa[PX(g_a, x, y)] != g_pbg[PX(g_b, x, y)]) {
                diff++;
                CHECK(x >= 432u && x <= 471u && y >= 240u && y <= 263u);
            }
        }
    }
    CHECK_EQ(diff, 960);

    TEST("H-C8-4 組み違い(面A + パレットB): RGB565 に丸めて 80% 以上の画素が違う色");
    for (y = 0u; y < 272u; y++) {
        for (x = 0u; x < 480u; x++) {
            total++;
            if (rgb565(g_pa[PX(g_a, x, y)]) != rgb565(g_pb[PX(g_a, x, y)])) { cross++; }
        }
    }
    printf("  組み違いで違う色の画素: %u / %u (%.1f%%)\n", cross, total, 100.0 * cross / total);
    CHECK(cross * 10u >= total * 8u);
    cross = 0u;
    for (y = 0u; y < 272u; y++) {
        for (x = 0u; x < 480u; x++) { if (rgb565(g_pa[PX(g_b, x, y)]) != rgb565(g_pb[PX(g_b, x, y)])) { cross++; } }
    }
    CHECK(cross * 10u >= total * 8u);                /* 面B + パレットA も同じ */
}

/* [clut] 行を読み直して、同じ文字列に戻せれば R-CLUT に fullmatch(余分な空白・先頭の 0 が無い) */
static const char *k_ev[] = { "start", "flip", "nr", "commit", "stress_end", "wr_unused", "wr_inuse", "wr_back",
                              "bench", "nr1", "nr2", "settle", "done", "state" };

static bool fullmatch(const char *s, size_t len)
{
    unsigned step, sel, ven;
    unsigned long rc, n, m, uf;
    char ev[32], fb, pal, re[256];
    int  used = 0;

    if (sscanf(s, "[clut] step=%u ev=%31[a-z_0-9] fb=%c pal=%c sel=%u ven=%u rc=%lu n=%lu m=%lu uf=%lu%n",
               &step, ev, &fb, &pal, &sel, &ven, &rc, &n, &m, &uf, &used) != 10) { return false; }
    if ((size_t)used + 2u != len) { return false; }
    if ((s[used] != '\r') || (s[used + 1] != '\n')) { return false; }
    snprintf(re, sizeof(re), "[clut] step=%u ev=%s fb=%c pal=%c sel=%u ven=%u rc=%lu n=%lu m=%lu uf=%lu\r\n",
             step, ev, fb, pal, sel, ven, rc, n, m, uf);
    return strcmp(re, s) == 0;
}

static void test_fmt(void)
{
    char   buf[256];
    size_t n, len, longest = 0u, i, s;
    int    e;

    TEST("H-C8-7 c8pat_fmt_clut: 全 ev 名・境界値が R-CLUT に fullmatch。最長 192 バイト未満");
    for (e = 0; e < 14; e++) {
        for (i = 0u; i < 4u; i++) {
            static const uint32_t big[4] = { 0u, 1u, 2000u, 0xFFFFFFFFu };
            n = c8pat_fmt_clut(buf, sizeof(buf), (uint8_t)((i == 0u) ? 0u : 5u), k_ev[e], (i & 1u) ? 'b' : 'a',
                               (i & 2u) ? 'b' : 'a', (uint8_t)(i & 1u), (uint8_t)((i >> 1) & 1u), big[i], big[3u - i], big[(i + 1u) & 3u], 0xFFFFFFFFu);
            CHECK(n > 0u);
            CHECK_EQ(n, strlen(buf));
            CHECK(fullmatch(buf, n));
            if (n > longest) { longest = n; }
        }
    }
    printf("  最長 = %u バイト\n", (unsigned)longest);
    CHECK(longest < 192u);
    n = c8pat_fmt_clut(buf, sizeof(buf), 1, "flip", 'b', 'a', 1, 1, 0, 3, 0, 7);
    CHECK(strcmp(buf, "[clut] step=1 ev=flip fb=b pal=a sel=1 ven=1 rc=0 n=3 m=0 uf=7\r\n") == 0);

    TEST("H-C8-7 バッファ不足は 0 で buf[0] = 0 / 引数不正は 0");
    len = strlen("[clut] step=1 ev=flip fb=b pal=a sel=1 ven=1 rc=0 n=3 m=0 uf=7\r\n");
    for (s = 1u; s <= len; s++) {                      /* len ちょうども NUL が入らないので不足 */
        memset(buf, 'X', sizeof(buf));
        CHECK_EQ(c8pat_fmt_clut(buf, s, 1, "flip", 'b', 'a', 1, 1, 0, 3, 0, 7), 0);
        CHECK_EQ(buf[0], 0);
    }
    CHECK_EQ(c8pat_fmt_clut(buf, len + 1u, 1, "flip", 'b', 'a', 1, 1, 0, 3, 0, 7), len);
    CHECK_EQ(c8pat_fmt_clut(NULL, 10, 1, "flip", 'b', 'a', 1, 1, 0, 3, 0, 7), 0);
    CHECK_EQ(c8pat_fmt_clut(buf, 0, 1, "flip", 'b', 'a', 1, 1, 0, 3, 0, 7), 0);
    buf[0] = 'X'; CHECK_EQ(c8pat_fmt_clut(buf, sizeof(buf), 6, "flip", 'b', 'a', 1, 1, 0, 3, 0, 7), 0); CHECK_EQ(buf[0], 0);
    buf[0] = 'X'; CHECK_EQ(c8pat_fmt_clut(buf, sizeof(buf), 1, "bogus", 'b', 'a', 1, 1, 0, 3, 0, 7), 0); CHECK_EQ(buf[0], 0);
    buf[0] = 'X'; CHECK_EQ(c8pat_fmt_clut(buf, sizeof(buf), 1, NULL, 'b', 'a', 1, 1, 0, 3, 0, 7), 0); CHECK_EQ(buf[0], 0);
    buf[0] = 'X'; CHECK_EQ(c8pat_fmt_clut(buf, sizeof(buf), 1, "flip", 'c', 'a', 1, 1, 0, 3, 0, 7), 0); CHECK_EQ(buf[0], 0);
    buf[0] = 'X'; CHECK_EQ(c8pat_fmt_clut(buf, sizeof(buf), 1, "flip", 'b', 'x', 1, 1, 0, 3, 0, 7), 0); CHECK_EQ(buf[0], 0);
    buf[0] = 'X'; CHECK_EQ(c8pat_fmt_clut(buf, sizeof(buf), 1, "flip", 'b', 'a', 2, 1, 0, 3, 0, 7), 0); CHECK_EQ(buf[0], 0);
    buf[0] = 'X'; CHECK_EQ(c8pat_fmt_clut(buf, sizeof(buf), 1, "flip", 'b', 'a', 1, 2, 0, 3, 0, 7), 0); CHECK_EQ(buf[0], 0);
    buf[0] = 'X'; CHECK_EQ(c8pat_fmt_clut(buf, sizeof(buf), 1, "flipx", 'b', 'a', 1, 1, 0, 3, 0, 7), 0); CHECK_EQ(buf[0], 0);
}

int main(void)
{
    test_face();
    test_pal();
    test_fmt();
    TEST_REPORT("test_clut8_pat");
}
