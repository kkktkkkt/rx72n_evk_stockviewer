/* test_p2b_extra.c - P2b(CLUT8)の追加試験(テスト担当。docs/test-report.md §11)。標準Cのみ。
 *   P1: パレット(ui_pal)の形: 256 エントリー、上位 8bit = 0xFF、RGB565 の 8bit 展開、すべて異なる、使用量の棒の2色が設計の値
 *   P2: 数字の段階の表の形: alt < 0 なら 2つ目の表は 0 埋め、alt >= 0 なら有効な添字。バーの表の長さ
 *   P3: 押下の暗転 / 白光を画素単位で検証: 「押下なしの描画」に、ボタンの形(A8 >= UI_REMAP_TH)の画素だけ ui_remap を適用したものと全画素一致。
 *       静的(全ボタン x 暗転/白光)と、スライド中(前進/後退 x dx x 古い画面のボタン/新しい画面のボタン/どちらでもない画面のボタン)。
 *       右の 32 列(480..511)は触らない。 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "ui_logic.h"
#include "ui_render.h"
#include "test_framework.h"

#define NPX ((size_t)GFX_STRIDE * GFX_H)
static uint8_t g_r0[NPX], g_ex[NPX], g_got[NPX];

static void mk(ui_t *u, uint8_t scr, int done)
{
    ui_init(u, 0u, 479u, 271u, 0u);
    u->v.scr = scr;
    u->v.clock_min = 7u * 60u + 5u;
    switch (scr) {
    case UI_SCR_FILL:   u->v.fill_x10 = done ? 1000u : 555u; break;
    case UI_SCR_REHEAT: u->v.bath_x10 = done ? 420u : 391u; break;
    case UI_SCR_USAGE:  u->v.flow_x10 = 87u; u->v.bar_ms = (uint16_t)(done ? UI_BAR_END_MS : 300u); break;
    default: break;
    }
}

/* ex(= 押下なしの描画)の、ボタン b の形の画素に ui_remap[ph] を適用する。xo = 画面の横のずれ、[c0, c1) = その画面が見えている列 */
static void apply_remap(uint8_t *ex, unsigned b, unsigned ph, int xo, int c0, int c1)
{
    const ui_btn_def_t *d = &ui_btn_defs[b];
    const ui_shape_t *sh = &ui_shapes[d->shape];
    int r, c;
    for (r = 0; r < (int)sh->h; r++) {
        for (c = 0; c < (int)sh->w; c++) {
            int x = d->vis.x + xo + c, y = d->vis.y + r;
            if ((x < 0) || (x >= GFX_W) || (x < c0) || (x >= c1) || (y < 0) || (y >= GFX_H)) { continue; }
            if (sh->a8[r * (int)sh->w + c] >= UI_REMAP_TH) {
                ex[(size_t)y * GFX_STRIDE + (size_t)x] = ui_remap[ph][ex[(size_t)y * GFX_STRIDE + (size_t)x]];
            }
        }
    }
}

static unsigned face_diff(const uint8_t *a, const uint8_t *b)
{
    size_t r, c; unsigned n = 0u;
    for (r = 0u; r < GFX_H; r++) { for (c = 0u; c < GFX_W; c++) { if (a[r * GFX_STRIDE + c] != b[r * GFX_STRIDE + c]) { n++; } } }
    return n;
}

static int right_untouched(const uint8_t *fb, uint8_t v)
{
    size_t r, c;
    for (r = 0u; r < GFX_H; r++) { for (c = GFX_W; c < GFX_STRIDE; c++) { if (fb[r * GFX_STRIDE + c] != v) { return 0; } } }
    return 1;
}

int main(void)
{
    static ui_t u;
    unsigned i, j, b, a, ph, bad, n;

    TEST("P1 パレット: 256 エントリー、上位 8bit = 0xFF、RGB565 の 8bit 展開(R8=(R5<<3)|(R5>>2) ほか)、すべて異なる色");
    {
        unsigned nalpha = 0u, nexp = 0u, ndup = 0u;
        for (i = 0u; i < UI_PAL_N; i++) {
            uint32_t p = ui_pal[i];
            unsigned r8 = (p >> 16) & 255u, g8 = (p >> 8) & 255u, b8 = p & 255u;
            unsigned r5 = r8 >> 3, g6 = g8 >> 2, b5 = b8 >> 3;
            if ((p >> 24) != 0xFFu) { nalpha++; }
            if ((r8 != ((r5 << 3) | (r5 >> 2))) || (g8 != ((g6 << 2) | (g6 >> 4))) || (b8 != ((b5 << 3) | (b5 >> 2)))) { nexp++; }
            for (j = 0u; j < i; j++) { if (ui_pal[j] == p) { ndup++; } }
        }
        CHECK_EQ(nalpha, 0);
        CHECK_EQ(nexp, 0);
        CHECK_EQ(ndup, 0);
        CHECK_EQ(UI_PAL_N, 256);
    }

    TEST("P1b 使用量の棒の2色は固定色で厳密(今日 = 0x6DD5、ほか = 0xCEFC。P1 の UI_C_BAR_U_TODAY / UI_C_BAR_U)");
    {
        uint32_t t = ui_pal[UI_I_BAR_U_TODAY], o = ui_pal[UI_I_BAR_U];
        unsigned t565 = (unsigned)((((t >> 19) & 31u) << 11) | (((t >> 10) & 63u) << 5) | ((t >> 3) & 31u));
        unsigned o565 = (unsigned)((((o >> 19) & 31u) << 11) | (((o >> 10) & 63u) << 5) | ((o >> 3) & 31u));
        CHECK_EQ(t565, 0x6DD5);
        CHECK_EQ(o565, 0xCEFC);
    }

    TEST("P2 段階の表の形: alt < 0 のスロットは 2つ目の表が 0 埋め、alt >= 0 なら 0..255 の添字で 2つ目の表に値がある。バーの表は 33 要素ずつ");
    {
        unsigned nalt = 0u;
        for (i = 0u; i < UI_SL_COUNT; i++) {
            unsigned nz = 0u;
            for (j = 0u; j < 33u; j++) { if (ui_slot_ramp[i][1][j] != 0u) { nz++; } }
            if (i == (unsigned)UI_SL_HOME_TEMP) { CHECK(ui_slot_alt[i] < 0); CHECK(nz > 0u); }     /* 同じ下地で文字色が 2 つ(INK / SUB)。2 つ目 = SUB */
            else if (ui_slot_alt[i] < 0) { CHECK_EQ(nz, 0); }
            else { nalt++; CHECK(ui_slot_alt[i] <= 255); CHECK(nz > 0u); }
            CHECK(ui_slot_ramp[i][0][32] != ui_slot_ramp[i][0][1]);      /* 最も濃い段と最も薄い段の色が違う */
        }
        CHECK_EQ(nalt, 1);                                               /* FILL_PCT だけ */
        CHECK_EQ(ui_pal[ui_slot_alt[UI_SL_FILL_PCT]], 0xFF8CBEDEu);                     /* BAR_F(進みの色)の添字(パレットの作り直しで番号は変わる) */
        CHECK(ui_slot_ramp[UI_SL_HOME_TEMP][0][32] != ui_slot_ramp[UI_SL_HOME_TEMP][1][32]);   /* 気温の INK と SUB の最も濃い段は別の色 */
        for (i = 0u; i < UI_BAR_COUNT; i++) { CHECK(ui_bar_ramp[i][1] != ui_bar_ramp[i][32]); }
        CHECK_EQ(sizeof(ui_slot_ramp), UI_SL_COUNT * 2u * 33u);
        CHECK_EQ(sizeof(ui_remap), 512);
    }

    TEST("P3a 押下・白光(静的): 押下なしの描画に、形の A8 >= UI_REMAP_TH の画素だけ ui_remap を適用したものと全画素が一致(全ボタン x 暗転/白光 x 状態 2 通り)。右の 32 列は不変");
    bad = 0u; n = 0u;
    for (b = 1u; b < UI_BTN_COUNT; b++) {
        const ui_btn_def_t *d = &ui_btn_defs[b];
        if (d->shape < 0) { continue; }
        for (i = 0u; i < 2u; i++) {
            for (ph = 0u; ph < 2u; ph++) {
                mk(&u, d->scr, (int)i);
                memset(g_r0, 0xA5, sizeof(g_r0)); ui_render(&u, g_r0);
                memcpy(g_ex, g_r0, sizeof(g_ex));
                apply_remap(g_ex, b, ph, 0, 0, GFX_W);
                u.fb_btn = (uint8_t)b; u.fb_phase = (uint8_t)(ph == 0u ? UI_PH_PRESS : UI_PH_FLASH);
                memset(g_got, 0xA5, sizeof(g_got)); ui_render(&u, g_got);
                if ((face_diff(g_ex, g_got) != 0u) || !right_untouched(g_got, 0xA5u)) { bad++; printf("  ボタン %u (%s) 状態 %u 相 %u: 不一致 %u\n", b, d->name, i, ph, face_diff(g_ex, g_got)); }
                if (memcmp(g_r0, g_got, sizeof(g_r0)) == 0) { bad++; printf("  ボタン %u: 押下で何も変わらない\n", b); }   /* 対照: 適用が空振りでない */
                n++;
            }
        }
    }
    printf("  (参考) 試した組合せ = %u\n", n);
    CHECK_EQ(bad, 0);

    TEST("P3b 押下の表示が別の画面のボタンなら何も変わらない(その画面に無いボタン)");
    bad = 0u;
    for (b = 1u; b < UI_BTN_COUNT; b++) {
        const ui_btn_def_t *d = &ui_btn_defs[b];
        if (d->shape < 0) { continue; }
        mk(&u, (uint8_t)((d->scr + 1u) % UI_SCR_COUNT), 0);
        ui_render(&u, g_r0);
        u.fb_btn = (uint8_t)b; u.fb_phase = UI_PH_PRESS;
        ui_render(&u, g_got);
        if (face_diff(g_r0, g_got) != 0u) { bad++; }
    }
    CHECK_EQ(bad, 0);

    TEST("P3c 押下・白光(スライド中): 古い画面のボタン / 新しい画面のボタン / どちらでもない画面のボタンで、見えている列だけ置き換わる(前進/後退 x dx)");
    {
        static const uint16_t dxs[] = { 1u, 2u, 47u, 120u, 239u, 240u, 241u, 360u, 478u, 479u, 480u };
        unsigned f, k;
        bad = 0u; n = 0u;
        for (a = 0u; a < UI_SCR_COUNT; a++) {
            for (j = 0u; j < UI_SCR_COUNT; j++) {                    /* j = 新しい画面 */
                if (j == a) { continue; }
                for (b = 1u; b < UI_BTN_COUNT; b++) {
                    const ui_btn_def_t *d = &ui_btn_defs[b];
                    if (d->shape < 0) { continue; }
                    if ((d->scr != a) && (d->scr != j) && ((b % 5u) != 0u)) { continue; }   /* どちらでもない画面のボタンは間引く */
                    for (f = 0u; f < 2u; f++) {
                        for (k = 0u; k < sizeof(dxs) / sizeof(dxs[0]); k++) {
                            static ui_t o;
                            unsigned dx = dxs[k];
                            int xo, c0, c1;
                            ph = (unsigned)((b + k) & 1u);
                            mk(&u, (uint8_t)j, (int)(k & 1u)); mk(&o, (uint8_t)a, (int)((k >> 1) & 1u));
                            u.ov = o.v; u.tr_on = 1u; u.tr_fwd = (uint8_t)f; u.tr_dx = (uint16_t)dx;
                            memset(g_r0, 0x5A, sizeof(g_r0)); ui_render(&u, g_r0);
                            memcpy(g_ex, g_r0, sizeof(g_ex));
                            if (d->scr == j) {            /* 新しい画面: 前進 = 480-dx から右、後退 = dx-480 から左(見える列 = [0, dx)) */
                                xo = (f != 0u) ? (int)(480u - dx) : (int)dx - 480;
                                c0 = (f != 0u) ? (int)(480u - dx) : 0; c1 = (f != 0u) ? 480 : (int)dx;
                                apply_remap(g_ex, b, ph, xo, c0, c1);
                            } else if (d->scr == a) {     /* 古い画面: 前進 = -dx、後退 = +dx(見える列 = 前進 [0, 480-dx)、後退 [dx, 480)) */
                                xo = (f != 0u) ? -(int)dx : (int)dx;
                                c0 = (f != 0u) ? 0 : (int)dx; c1 = (f != 0u) ? (int)(480u - dx) : 480;
                                apply_remap(g_ex, b, ph, xo, c0, c1);
                            }
                            u.fb_btn = (uint8_t)b; u.fb_phase = (uint8_t)(ph == 0u ? UI_PH_PRESS : UI_PH_FLASH);
                            memset(g_got, 0x5A, sizeof(g_got)); ui_render(&u, g_got);
                            if ((face_diff(g_ex, g_got) != 0u) || !right_untouched(g_got, 0x5Au)) {
                                bad++;
                                if (bad < 6u) { printf("  旧 %u 新 %u ボタン %u(画面 %u) fwd %u dx %u: 不一致 %u\n", a, j, b, d->scr, f, dx, face_diff(g_ex, g_got)); }
                            }
                            n++;
                        }
                    }
                }
            }
        }
        printf("  (参考) 試した組合せ = %u\n", n);
        CHECK_EQ(bad, 0);
    }

    TEST("P4 gfx の 8bpp 版: 座標・大きさが INT32 の端でも、面の外・右の 32 列(480..511)へ書かない。fill8 は遅い基準実装と全画素一致(X12 の 8bpp 版)");
    {
        enum { GUARD = 4096 };
        static uint8_t buf[GUARD + GFX_STRIDE * GFX_H + GUARD];
        static uint8_t ref[GFX_STRIDE * GFX_H];
        static uint8_t a8[64 * 64], src[64 * 64], rp[33], mp[256];
        static const int32_t EX[] = { INT32_MIN, INT32_MIN + 1, -481, -480, -64, -1, 0, 1, 479, 480, 511, 512, 1000000, INT32_MAX };
        const unsigned NE = (unsigned)(sizeof(EX) / sizeof(EX[0]));
        unsigned xi, yi, wi, k, op;
        uint8_t *fb = &buf[GUARD];
        bad = 0u;
        for (k = 0u; k < sizeof(a8); k++) { a8[k] = (uint8_t)(k * 7u + 3u); src[k] = (uint8_t)(k + 1u); }
        for (k = 0u; k < 33u; k++) { rp[k] = (uint8_t)(k + 1u); }
        for (k = 0u; k < 256u; k++) { mp[k] = (uint8_t)(k ^ 0x5Au); }
        for (xi = 0u; xi < NE; xi++) {
            for (yi = 0u; yi < NE; yi++) {
                for (wi = 0u; wi < NE; wi++) {
                    int32_t x = EX[xi], y = EX[yi], w = EX[wi], h = EX[(wi + yi + 3u) % NE];
                    int32_t r, c;
                    for (op = 0u; op < 4u; op++) {
                        memset(buf, 0xA5, sizeof(buf));
                        if (op == 0u) {          /* fill8: 全幅・全高の引数をそのまま。基準と一致 */
                            memset(ref, 0xA5, sizeof(ref));
                            gfx_fill8(fb, x, y, w, h, 0x34u);
                            if ((w > 0) && (h > 0)) {
                                for (r = 0; r < GFX_H; r++) {
                                    for (c = 0; c < GFX_W; c++) {
                                        if (((int64_t)c >= x) && ((int64_t)c < (int64_t)x + w) && ((int64_t)r >= y) && ((int64_t)r < (int64_t)y + h)) { ref[r * GFX_STRIDE + c] = 0x34u; }
                                    }
                                }
                            }
                            if (memcmp(fb, ref, sizeof(ref)) != 0) { bad++; }
                        } else if (op == 1u) { gfx_blit8(fb, src, 64, 0, 0, 64, 64, x, y); }
                        else if (op == 2u)   { gfx_a8_ramp(fb, a8, 64, 64, x, y, rp, NULL, -1); }
                        else                 { memset(fb, 0x77, sizeof(buf) - 2u * GUARD); gfx_remap8(fb, a8, 64, 64, x, y, mp, 128u); memset(&buf[0], 0xA5, GUARD); }
                        for (k = 0u; k < GUARD; k++) { if ((buf[k] != 0xA5u) || (buf[GUARD + GFX_STRIDE * GFX_H + k] != 0xA5u)) { bad++; break; } }
                        for (r = 0; r < GFX_H; r++) {
                            for (c = GFX_W; c < GFX_STRIDE; c++) {
                                if (fb[r * GFX_STRIDE + c] != ((op == 3u) ? 0x77u : 0xA5u)) { bad++; r = GFX_H; break; }
                            }
                        }
                    }
                }
            }
        }
        CHECK_EQ(bad, 0);
    }

    TEST_REPORT("test_p2b_extra");
}
