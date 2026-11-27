/* test_ui_geom.c - 天気・Wi-Fi の UI(docs/design.md §22.8)の幾何と生成物の独立試験(テスト担当が追加)。標準C。
 *   G1 当たり判定: 全画面・全ボタンで、画面の内側・見た目が当たり判定の内側・同じ画面で同時に出るボタンの当たり判定が重ならない(h_wx を含む)
 *   G2 要素の配置: 日付・気温・更新時刻の文字の外接矩形を、取り得る全文字列でフォントの字形表から計算し、隣の要素(Wi-Fi アイコン・天気アイコン・更新ボタン)と重ならない
 *   G3 スプライト: 周囲 1 画素の輪が、置かれる背景の同じ位置の添字と一致(下地に焼き込み済み = 継ぎ目なし)、種類ごとに十分違う(区別できる)、更新アイコン 4 枚が 90 度ずつの回転
 * ui_*.o(生成データ)とリンクする。 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ui_assets.h"
#include "test_framework.h"

typedef struct { int x0, y0, x1, y1; } box_t;       /* [x0,x1) x [y0,y1) */

static int inter(const ui_rect_t *a, const ui_rect_t *b)
{
    return (a->x < b->x + b->w) && (b->x < a->x + a->w) && (a->y < b->y + b->h) && (b->y < a->y + a->h);
}
static int inside(const ui_rect_t *in, int x0, int y0, int x1, int y1)
{
    return in->x >= x0 && in->y >= y0 && in->x + in->w <= x1 && in->y + in->h <= y1;
}

static void t_hit(void)
{
    unsigned i, j;
    int bad_in = 0, bad_vis = 0, bad_ov = 0, wx = -1;

    TEST("G1 当たり判定: 画面の内側・見た目 ⊂ 当たり判定・同じ画面で同時に出る(cond が同じか ALWAYS を含む)ボタンの当たり判定が重ならない");
    for (i = 1u; i < UI_BTN_COUNT; i++) {
        const ui_btn_def_t *a = &ui_btn_defs[i];
        if (!inside(&a->hit, 0, 0, UI_W, UI_H)) { bad_in++; printf("  画面外: %s\n", a->name); }
        if (!(a->vis.x >= a->hit.x && a->vis.y >= a->hit.y && a->vis.x + a->vis.w <= a->hit.x + a->hit.w && a->vis.y + a->vis.h <= a->hit.y + a->hit.h)) { bad_vis++; printf("  見た目が当たり判定の外: %s\n", a->name); }
        if (strcmp(a->name, "h_wx") == 0) { wx = (int)i; }
        for (j = i + 1u; j < UI_BTN_COUNT; j++) {
            const ui_btn_def_t *b = &ui_btn_defs[j];
            if (a->scr != b->scr) { continue; }
            if (!(a->cond == b->cond || a->cond == UI_COND_ALWAYS || b->cond == UI_COND_ALWAYS)) { continue; }
            if (inter(&a->hit, &b->hit)) { bad_ov++; printf("  重なり: %s と %s\n", a->name, b->name); }
        }
    }
    CHECK_EQ(bad_in, 0); CHECK_EQ(bad_vis, 0); CHECK_EQ(bad_ov, 0);
    CHECK_EQ(wx, UI_BTN_H_WX);
    if (wx > 0) {
        const ui_btn_def_t *w = &ui_btn_defs[wx];
        CHECK_EQ(w->scr, UI_SCR_HOME); CHECK_EQ(w->cond, UI_COND_ALWAYS);
        CHECK_EQ(w->vis.x, 248); CHECK_EQ(w->vis.y, 158); CHECK_EQ(w->vis.w, 28); CHECK_EQ(w->vis.h, 28);
        CHECK_EQ(w->hit.x, 228); CHECK_EQ(w->hit.y, 136); CHECK_EQ(w->hit.w, 60); CHECK_EQ(w->hit.h, 52);
        CHECK(UI_REF_X >= w->vis.x && UI_REF_Y >= w->vis.y && UI_REF_X + UI_REF_W <= w->vis.x + w->vis.w && UI_REF_Y + UI_REF_H <= w->vis.y + w->vis.h);
    }
}

/* ---- 文字列の外接矩形(ui_render.c の draw_text と同じ規則: 左 / 右詰め、glyph_index、adv、bx、by) ---- */
static int gidx(char c)
{
    unsigned char u = (unsigned char)c;
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (u >= 1u && u <= 7u) { return (int)UI_G_MON + (int)u - 1; }
    switch (c) { case ':': return 10; case '.': return 11; case '%': return 12; case '-': return (int)UI_G_MINUS; case '(': return (int)UI_G_LPAR; case ')': return (int)UI_G_RPAR; default: return -1; }
}
static void text_box(unsigned slot, const char *s, box_t *bb)
{
    const ui_slot_t  *sl = &ui_slots[slot];
    const ui_glyph_t *g  = ui_glyphs[sl->font];
    int x = sl->x, w = 0, i, k;
    bb->x0 = 100000; bb->y0 = 100000; bb->x1 = -100000; bb->y1 = -100000;
    if (sl->align != UI_ALIGN_LEFT) {
        for (i = 0; s[i]; i++) { k = gidx(s[i]); w += (k >= 0) ? g[k].adv : 0; }
        x -= (sl->align == UI_ALIGN_CENTER) ? (w / 2) : w;
    }
    for (i = 0; s[i]; i++) {
        k = gidx(s[i]);
        if (k < 0) { continue; }
        if (g[k].w > 0u && g[k].h > 0u) {
            int gx0 = x + g[k].bx, gy0 = sl->y - (int)g[k].by;
            if (gx0 < bb->x0) { bb->x0 = gx0; }
            if (gy0 < bb->y0) { bb->y0 = gy0; }
            if (gx0 + (int)g[k].w > bb->x1) { bb->x1 = gx0 + (int)g[k].w; }
            if (gy0 + (int)g[k].h > bb->y1) { bb->y1 = gy0 + (int)g[k].h; }
        }
        x += g[k].adv;
    }
}
static void uni(box_t *u, const box_t *b)
{
    if (b->x0 < u->x0) { u->x0 = b->x0; }
    if (b->y0 < u->y0) { u->y0 = b->y0; }
    if (b->x1 > u->x1) { u->x1 = b->x1; }
    if (b->y1 > u->y1) { u->y1 = b->y1; }
}
static int box_vs_rect(const box_t *b, int x, int y, int w, int h)
{
    return (b->x0 < x + w) && (x < b->x1) && (b->y0 < y + h) && (y < b->y1);
}

static void t_layout(void)
{
    box_t date = { 100000, 100000, -100000, -100000 }, temp = date, upd = date, b;
    char  s[24];
    int   m, d, wd, t, hh, mm;
    const ui_btn_def_t *wxb = NULL;
    unsigned i;
    static const char WD[7] = { 2, 1, 3, 4, 5, 6, 7 };

    TEST("G2 配置: 日付(全 12x31x7 の文字列)・気温(-99.9〜99.9 と --.-)・更新時刻(00:00〜23:59 と --:--)の外接矩形が、隣の要素と重ならない");
    for (i = 1u; i < UI_BTN_COUNT; i++) { if (strcmp(ui_btn_defs[i].name, "h_wx") == 0) { wxb = &ui_btn_defs[i]; } }
    CHECK(wxb != NULL);
    for (m = 1; m <= 12; m++) { for (d = 1; d <= 31; d++) { for (wd = 0; wd < 7; wd++) {
        (void)snprintf(s, sizeof s, "%d\x01%d\x02(%c)", m, d, WD[wd]);
        text_box(UI_SL_HOME_DATE, s, &b); uni(&date, &b);
    } } }
    for (t = -999; t <= 999; t++) {
        int a = t < 0 ? -t : t;
        (void)snprintf(s, sizeof s, "%s%d.%d", t < 0 ? "-" : "", a / 10, a % 10);
        text_box(UI_SL_HOME_TEMP, s, &b); uni(&temp, &b);
    }
    text_box(UI_SL_HOME_TEMP, "--.-", &b); uni(&temp, &b);
    for (hh = 0; hh < 24; hh++) { for (mm = 0; mm < 60; mm++) {
        (void)snprintf(s, sizeof s, "%02d:%02d", hh, mm);
        text_box(UI_SL_HOME_UPD, s, &b); uni(&upd, &b);
    } }
    text_box(UI_SL_HOME_UPD, "--:--", &b); uni(&upd, &b);
    printf("  日付 x=[%d,%d) y=[%d,%d)  気温 x=[%d,%d) y=[%d,%d)  更新時刻 x=[%d,%d) y=[%d,%d)\n", date.x0, date.x1, date.y0, date.y1, temp.x0, temp.x1, temp.y0, temp.y1, upd.x0, upd.x1, upd.y0, upd.y1);
    /* 日付: 画面の内側。Wi-Fi アイコン (112,7,20,20) と重ならない。デモボタンの当たり判定(216,0,108,44)の左 */
    CHECK(date.x0 >= 0 && date.y0 >= 0 && date.x1 <= UI_W);
    CHECK(!box_vs_rect(&date, UI_WIFI_X, UI_WIFI_Y, UI_WIFI_W, UI_WIFI_H));
    CHECK(date.x1 <= 216);
    /* 気温: 天気アイコン (20,158,28,28) と重ならない。「℃」(x=108 から)の左。画面の内側 */
    CHECK(temp.x0 >= UI_WX_X + UI_WX_W);
    CHECK(temp.x1 <= 108);
    CHECK(temp.y0 >= 0 && temp.y1 <= UI_H);
    CHECK(!box_vs_rect(&temp, UI_WX_X, UI_WX_Y, UI_WX_W, UI_WX_H));
    /* 更新時刻: 更新ボタンの見た目 (248,158,28,28) と重ならない */
    if (wxb != NULL) {
        CHECK(!box_vs_rect(&upd, wxb->vis.x, wxb->vis.y, wxb->vis.w, wxb->vis.h));
        printf("  (参考) 更新時刻の右端 x=%d、更新ボタンの当たり判定の左端 x=%d(%s)\n", upd.x1, wxb->hit.x, (upd.x1 > wxb->hit.x) ? "文字の右端が当たり判定に入る(押せる範囲の一部)" : "当たり判定の外");
    }
    /* アイコン同士・既存ボタンの見た目と重ならない(ホーム) */
    for (i = 1u; i < UI_BTN_COUNT; i++) {
        const ui_btn_def_t *o = &ui_btn_defs[i];
        if (o->scr != UI_SCR_HOME || o == wxb) { continue; }
        CHECK(!(UI_WX_X < o->vis.x + o->vis.w && o->vis.x < UI_WX_X + UI_WX_W && UI_WX_Y < o->vis.y + o->vis.h && o->vis.y < UI_WX_Y + UI_WX_H));
        CHECK(!box_vs_rect(&upd, o->vis.x, o->vis.y, o->vis.w, o->vis.h));
        CHECK(!box_vs_rect(&temp, o->vis.x, o->vis.y, o->vis.w, o->vis.h));
        CHECK(!box_vs_rect(&date, o->vis.x, o->vis.y, o->vis.w, o->vis.h));
    }
    /* Wi-Fi アイコンが、各画面の既存ボタンの見た目・当たり判定と重ならない(待機以外の 4 画面) */
    for (i = 1u; i < UI_BTN_COUNT; i++) {
        const ui_btn_def_t *o = &ui_btn_defs[i];
        if (o->scr == UI_SCR_STANDBY) { continue; }
        CHECK(!(UI_WIFI_X < o->vis.x + o->vis.w && o->vis.x < UI_WIFI_X + UI_WIFI_W && UI_WIFI_Y < o->vis.y + o->vis.h && o->vis.y < UI_WIFI_Y + UI_WIFI_H));
        if (o->scr == UI_SCR_HOME && o->hit.x < UI_WIFI_X + UI_WIFI_W && UI_WIFI_X < o->hit.x + o->hit.w && o->hit.y < UI_WIFI_Y + UI_WIFI_H && UI_WIFI_Y < o->hit.y + o->hit.h) {
            printf("  注意: Wi-Fi アイコンがホームの %s の当たり判定に入る\n", o->name);
        }
    }
}

static int bg_of(unsigned scr_bg, int x, int y) { return ui_bgi[scr_bg][y * UI_W + x]; }

static int ring_bad(const uint8_t *spr, int w, int h, unsigned bg, int ox, int oy)
{
    int x, y, bad = 0;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
                if (spr[y * w + x] != bg_of(bg, ox + x, oy + y)) { bad++; }
            }
        }
    }
    return bad;
}
static int diff_px(const uint8_t *a, const uint8_t *b, int n)
{
    int i, c = 0;
    for (i = 0; i < n; i++) { if (a[i] != b[i]) { c++; } }
    return c;
}

static void t_sprites(void)
{
    unsigned k, k2, bg;
    int bad = 0, mind = 100000, x, y, rot_bad[4] = { 0, 0, 0, 0 };
    static const unsigned HDR_BGS[] = { UI_BG_HOME, UI_BG_FILL_RUN, UI_BG_FILL_DONE, UI_BG_REHEAT_RUN, UI_BG_REHEAT_DONE, UI_BG_USAGE };

    TEST("G3a スプライトの継ぎ目: 天気 8 種の輪(外周 1 画素)がホーム背景の同じ位置と一致、Wi-Fi 4 種の輪が待機以外の 6 背景と一致、更新アイコン 4 種の輪がホーム背景(ボタンの面)と一致");
    /* 天気は絵が縁に届く(太陽の光線・雲)ので、輪は全部一致しない。4 隅が一致し、輪の不一致が 16 画素以下で、全体の 3 割以上が背景と同じ(下地に焼き込み済み) */
    for (k = 0u; k < UI_SPR_WX_N; k++) {
        int same = 0, ring = ring_bad(ui_spr_wx[k], UI_WX_W, UI_WX_H, UI_BG_HOME, UI_WX_X, UI_WX_Y);
        for (y = 0; y < UI_WX_H; y++) { for (x = 0; x < UI_WX_W; x++) { if (ui_spr_wx[k][y * UI_WX_W + x] == bg_of(UI_BG_HOME, UI_WX_X + x, UI_WX_Y + y)) { same++; } } }
        CHECK(ring <= 16); CHECK(same * 10 >= UI_WX_W * UI_WX_H * 3);
        CHECK_EQ(ui_spr_wx[k][0], bg_of(UI_BG_HOME, UI_WX_X, UI_WX_Y));
        CHECK_EQ(ui_spr_wx[k][UI_WX_W - 1], bg_of(UI_BG_HOME, UI_WX_X + UI_WX_W - 1, UI_WX_Y));
        CHECK_EQ(ui_spr_wx[k][(UI_WX_H - 1) * UI_WX_W], bg_of(UI_BG_HOME, UI_WX_X, UI_WX_Y + UI_WX_H - 1));
        CHECK_EQ(ui_spr_wx[k][UI_WX_H * UI_WX_W - 1], bg_of(UI_BG_HOME, UI_WX_X + UI_WX_W - 1, UI_WX_Y + UI_WX_H - 1));
        printf("  天気 %u: 輪の不一致 %d、背景と同じ %d / %d 画素\n", k, ring, same, UI_WX_W * UI_WX_H);
    }
    for (k = 0u; k < UI_SPR_WIFI_N; k++) { for (bg = 0u; bg < 6u; bg++) { bad += ring_bad(ui_spr_wifi[k], UI_WIFI_W, UI_WIFI_H, HDR_BGS[bg], UI_WIFI_X, UI_WIFI_Y); } }
    CHECK_EQ(bad, 0); bad = 0;
    for (k = 0u; k < UI_SPR_REF_N; k++) { bad += ring_bad(ui_spr_ref[k], UI_REF_W, UI_REF_H, UI_BG_HOME, UI_REF_X, UI_REF_Y); }
    CHECK_EQ(bad, 0);

    TEST("G3b 区別: 天気 8 種は互いに 40 画素以上違う、Wi-Fi 4 種・更新 4 種は互いに 20 画素以上違う");
    for (k = 0u; k < UI_SPR_WX_N; k++) { for (k2 = k + 1u; k2 < UI_SPR_WX_N; k2++) { int c = diff_px(ui_spr_wx[k], ui_spr_wx[k2], UI_WX_W * UI_WX_H); if (c < mind) { mind = c; } if (c < 40) { printf("  天気 %u と %u が似ている(%d 画素)\n", k, k2, c); bad++; } } }
    printf("  天気の最小の差 = %d 画素\n", mind);
    mind = 100000;
    for (k = 0u; k < UI_SPR_WIFI_N; k++) { for (k2 = k + 1u; k2 < UI_SPR_WIFI_N; k2++) { int c = diff_px(ui_spr_wifi[k], ui_spr_wifi[k2], UI_WIFI_W * UI_WIFI_H); if (c < mind) { mind = c; } if (c < 20) { printf("  Wi-Fi %u と %u が似ている(%d 画素)\n", k, k2, c); bad++; } } }
    printf("  Wi-Fi の最小の差 = %d 画素\n", mind);
    mind = 100000;
    for (k = 0u; k < UI_SPR_REF_N; k++) { for (k2 = k + 1u; k2 < UI_SPR_REF_N; k2++) { int c = diff_px(ui_spr_ref[k], ui_spr_ref[k2], UI_REF_W * UI_REF_H); if (c < mind) { mind = c; } if (c < 20) { printf("  更新 %u と %u が似ている(%d 画素)\n", k, k2, c); bad++; } } }
    printf("  更新の最小の差 = %d 画素\n", mind);
    CHECK_EQ(bad, 0);

    TEST("G3c 更新アイコン: 4 枚が 90 度ずつの回転(ref[k] = ref[0] を時計回りに k x 90 度。許す不一致は 1 割まで)");
    for (k = 1u; k < UI_SPR_REF_N; k++) {
        for (y = 0; y < UI_REF_H; y++) { for (x = 0; x < UI_REF_W; x++) {
            int sx = x, sy = y, r;
            for (r = 0; r < (int)k; r++) { int tx = UI_REF_H - 1 - sy, ty = sx; sx = tx; sy = ty; }   /* 時計回り 90 度 */
            if (ui_spr_ref[k][sy * UI_REF_W + sx] != ui_spr_ref[0][y * UI_REF_W + x]) { rot_bad[k]++; }
        } }
        printf("  ref[%u] の回転の不一致 = %d / %d 画素\n", k, rot_bad[k], UI_REF_W * UI_REF_H);
        CHECK(rot_bad[k] * 10 <= UI_REF_W * UI_REF_H);
    }
}

int main(void)
{
    t_hit();
    t_layout();
    t_sprites();
    TEST_REPORT("test_ui_geom");
}
