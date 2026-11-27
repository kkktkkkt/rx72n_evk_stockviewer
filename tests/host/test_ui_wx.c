/* test_ui_wx.c - 天気・Wi-Fi の UI(docs/design.md §22.8、§22.12 の H-X8 / H-X9)のホスト試験。標準Cのみ。
 *   X1 ui_wx_t のパディングなし・表示キー(wx と spin の変化で再描画、変化なしなら再描画しない)
 *   X2 ui_logic: clk_ext 0 / 1 の時計、UI_CMD_WX(ホームだけ・デモ中と遷移中は無視・BTN_DOWN / BTN_ACT・画面は変わらない)、実タッチの当たり判定の境界、spin
 *   X3 ui_render: スプライトの位置(天気 8 種・Wi-Fi 4 状態・更新の回転 4 段階)が期待どおり、変化は矩形の内側だけ、待機に Wi-Fi なし、
 *      日付・気温・更新時刻の位置と右詰め、30 分超の灰色、未取得の --.- / --:--、clk_ext = 0 の旧日付、範囲外の値でも安全、全画素を書く、スライド中の位置 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "ui_logic.h"
#include "ui_render.h"
#include "test_framework.h"

#define NPX ((size_t)GFX_STRIDE * GFX_H)
static uint8_t g_a[NPX], g_b[NPX], g_c[NPX];

typedef struct { int x, y, w, h; } rc_t;
static const rc_t R_WIFI = { UI_WIFI_X, UI_WIFI_Y, UI_WIFI_W, UI_WIFI_H };
static const rc_t R_WX   = { UI_WX_X, UI_WX_Y, UI_WX_W, UI_WX_H };
static const rc_t R_REF  = { UI_REF_X, UI_REF_Y, UI_REF_W, UI_REF_H };
static const rc_t R_DATE = { 8, 6, 96, 26 };           /* 日付(F14、x = 16、ベースライン 23)を含む矩形 */
static const rc_t R_TEMP = { 48, 158, 60, 32 };        /* 気温(F22、右端 106、ベースライン 181)を含む矩形 */
static const rc_t R_UPD  = { 190, 160, 56, 24 };       /* 更新時刻(F14、x = 196、ベースライン 177) */

static int inside(const rc_t *r, int x, int y) { return (x >= r->x) && (x < r->x + r->w) && (y >= r->y) && (y < r->y + r->h); }

static void mk(ui_t *u, uint8_t scr)
{
    ui_init(u, 0u, 479u, 271u, 0u);
    u->v.scr = scr;
    u->v.clock_min = 13u * 60u + 24u;
    u->v.fill_x10 = 555u;
    u->v.bath_x10 = 391u;
    u->v.flow_x10 = 87u;
    u->v.bar_ms = 300u;
}

static void wx_std(ui_t *u)    /* 同期済み・晴れ・ONLINE・12.6℃・11月25日(水) */
{
    ui_wx_t w;
    memset(&w, 0, sizeof(w));
    w.net = UI_NET_ON; w.have = 1u; w.kind = 0u; w.temp_x10 = 126; w.clk_ext = 1u; w.month = 11u; w.day = 25u; w.wday = 3u;
    w.upd_min = 13u * 60u + 24u; w.clock_min = 13u * 60u + 24u;
    ui_set_wx(u, &w);
}

static void rend(const ui_t *u, uint8_t *fb) { memset(fb, 0xA5, NPX); ui_render(u, fb); }

/* 2 つの面で違う画素のうち、どの矩形にも入らないものの数(rs = 許す矩形) */
static unsigned diff_outside(const uint8_t *p, const uint8_t *q, const rc_t *const *rs, unsigned n)
{
    unsigned bad = 0u, k;
    int x, y;
    for (y = 0; y < GFX_H; y++) {
        for (x = 0; x < GFX_W; x++) {
            int in = 0;
            if (p[y * GFX_STRIDE + x] == q[y * GFX_STRIDE + x]) { continue; }
            for (k = 0u; k < n; k++) { if (inside(rs[k], x, y)) { in = 1; } }
            if (!in) { bad++; }
        }
    }
    return bad;
}

static unsigned diff_in(const uint8_t *p, const uint8_t *q, const rc_t *r)
{
    unsigned n = 0u;
    int x, y;
    for (y = r->y; y < r->y + r->h; y++) { for (x = r->x; x < r->x + r->w; x++) { if (p[y * GFX_STRIDE + x] != q[y * GFX_STRIDE + x]) { n++; } } }
    return n;
}

/* 面の矩形 r が、スプライト spr(r->w x r->h)と一致するか(xo = 横のずれ) */
static int is_spr(const uint8_t *fb, const rc_t *r, const uint8_t *spr, int xo)
{
    int x, y;
    for (y = 0; y < r->h; y++) { for (x = 0; x < r->w; x++) { if (fb[(r->y + y) * GFX_STRIDE + r->x + xo + x] != spr[y * r->w + x]) { return 0; } } }
    return 1;
}

static int right_untouched(const uint8_t *fb, uint8_t v)
{
    size_t r, c;
    for (r = 0u; r < GFX_H; r++) { for (c = GFX_W; c < GFX_STRIDE; c++) { if (fb[r * GFX_STRIDE + c] != v) { return 0; } } }
    return 1;
}

/* 矩形 r の中で、背景(ui_bgi[bg])と違う画素の x の最大 / 最小(無ければ -1) */
static int ink_xmax(const uint8_t *fb, const uint8_t *bg, const rc_t *r)
{
    int x, y, m = -1;
    for (y = r->y; y < r->y + r->h; y++) { for (x = r->x; x < r->x + r->w; x++) { if ((fb[y * GFX_STRIDE + x] != bg[y * GFX_W + x]) && (x > m)) { m = x; } } }
    return m;
}
static int ink_xmin(const uint8_t *fb, const uint8_t *bg, const rc_t *r)
{
    int x, y, m = 1000;
    for (y = r->y; y < r->y + r->h; y++) { for (x = r->x; x < r->x + r->w; x++) { if ((fb[y * GFX_STRIDE + x] != bg[y * GFX_W + x]) && (x < m)) { m = x; } } }
    return (m == 1000) ? -1 : m;
}
static unsigned ink_count(const uint8_t *fb, const uint8_t *bg, const rc_t *r)
{
    unsigned n = 0u; int x, y;
    for (y = r->y; y < r->y + r->h; y++) { for (x = r->x; x < r->x + r->w; x++) { if (fb[y * GFX_STRIDE + x] != bg[y * GFX_W + x]) { n++; } } }
    return n;
}

/* ---- ui_logic 側の補助 ---- */
static ui_t U;
static unsigned n_ev(uint8_t type, uint8_t a, uint8_t src)      /* 取り出して数える */
{
    ui_ev_t e; unsigned n = 0u;
    while (ui_event_pop(&U, &e)) { if ((e.type == type) && ((a == 255u) || (e.a == a)) && ((src == 255u) || (e.src == src))) { n++; } }
    return n;
}

static void run_to(uint32_t from, uint32_t to) { uint32_t t; for (t = from; t <= to; t += 10u) { ui_update(&U, NULL, t); } }

static void touch_tap(int x, int y, uint32_t t0)    /* 実タッチ: 100ms 押して離す */
{
    nav_sample_t s;
    uint32_t t;
    memset(&s, 0, sizeof(s));
    s.pressed = true; s.points = 1u; s.x = (uint16_t)x; s.y = (uint16_t)y;
    for (t = t0; t < t0 + 100u; t += 10u) { ui_update(&U, &s, t); }
    s.pressed = false; s.points = 0u;
    for (t = t0 + 100u; t < t0 + 400u; t += 10u) { ui_update(&U, &s, t); }
}

int main(void)
{
    static ui_t a, b;
    unsigned i, k, bad;

    TEST("X1 ui_wx_t は 16 バイト(パディングなし。表示キーを memcmp で比べるため)。ui_init の既定 = OFFLINE・未取得・旧時計");
    CHECK_EQ(sizeof(ui_wx_t), 16);
    ui_init(&a, 0u, 479u, 271u, 0u);
    CHECK_EQ(a.wx.net, UI_NET_OFF); CHECK_EQ(a.wx.have, 0); CHECK_EQ(a.wx.clk_ext, 0); CHECK_EQ(a.wx.busy, 0);
    CHECK_EQ(a.wx.upd_min, 0xFFFF); CHECK_EQ(a.wx.clock_min, 0xFFFF); CHECK_EQ(a.spin, 0);
    CHECK_EQ(a.v.clock_min, 21 * 60 + 45);                 /* 旧「21:45 起点」の時計のまま(clk_ext = 0) */

    TEST("X1b 表示キー: wx のどのメンバーの変化でも再描画、変化なしなら再描画しない、spin も");
    ui_init(&a, 0u, 479u, 271u, 0u);
    ui_view_mark(&a);
    ui_update(&a, NULL, 10u);
    CHECK(!ui_view_changed(&a));                           /* 何も変えなければ変わらない */
    {
        ui_wx_t w0 = a.wx, w;
        unsigned off;
        static const unsigned offs[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14 };      /* net busy have kind stale clk_ext month day wday temp upd clock の先頭バイト */
        for (i = 0u; i < sizeof(offs) / sizeof(offs[0]); i++) {
            off = offs[i];
            w = w0;
            ((uint8_t *)&w)[off] = (uint8_t)(((uint8_t *)&w)[off] + 1u);
            ui_view_mark(&a);
            ui_set_wx(&a, &w);
            ui_update(&a, NULL, 20u);
            CHECK(ui_view_changed(&a));
            ui_set_wx(&a, &w0);
            ui_update(&a, NULL, 30u);
            ui_view_mark(&a);
        }
        w = w0; w.busy = 1u;
        ui_set_wx(&a, &w);
        ui_update(&a, NULL, 1000u);
        ui_view_mark(&a);
        ui_update(&a, NULL, 1000u + UI_SPIN_MS);           /* 取得中は 150ms ごとに spin が進み、再描画になる */
        CHECK(ui_view_changed(&a));
    }

    TEST("X2 時計: clk_ext = 0 は旧時計(21:45 + 経過分)、clk_ext = 1 は wx.clock_min(0xFFFF = 未同期)");
    ui_init(&a, 0u, 479u, 271u, 0u);
    ui_update(&a, NULL, 120000u);
    CHECK_EQ(a.v.clock_min, 21 * 60 + 45 + 2);
    wx_std(&a);
    ui_update(&a, NULL, 120010u);
    CHECK_EQ(a.v.clock_min, 13 * 60 + 24);
    a.wx.clock_min = 0xFFFFu;
    ui_update(&a, NULL, 120020u);
    CHECK_EQ(a.v.clock_min, 0xFFFF);
    a.wx.clk_ext = 0u;                                     /* 戻せる */
    ui_update(&a, NULL, 120030u);
    CHECK_EQ(a.v.clock_min, 21 * 60 + 45 + 2);

    TEST("X2b spin: busy の間だけ (now / 150) & 3、busy = 0 なら 0");
    ui_init(&a, 0u, 479u, 271u, 0u);
    wx_std(&a);
    for (i = 0u; i < 20u; i++) { ui_update(&a, NULL, 5000u + i * 10u); CHECK_EQ(a.spin, 0); }
    a.wx.busy = 1u;
    bad = 0u;
    for (i = 0u; i < 200u; i++) { uint32_t t = 5000u + i * 10u; ui_update(&a, NULL, t); if (a.spin != ((t / UI_SPIN_MS) & 3u)) { bad++; } }
    CHECK_EQ(bad, 0);
    a.wx.busy = 0u; ui_update(&a, NULL, 9000u); CHECK_EQ(a.spin, 0);

    TEST("X2c UI_CMD_WX: ホームで h_wx の仮想タップ(BTN_DOWN src=uart → 90ms 以上後に BTN_ACT)。画面は変わらない。BTN_ACT の a = UI_BTN_H_WX");
    ui_init(&U, 0u, 479u, 271u, 0u);
    wx_std(&U);
    ui_cmd(&U, UI_CMD_WX, 1000u);
    run_to(1010u, 1500u);
    {
        ui_ev_t e; unsigned down = 0u, act = 0u, other = 0u;
        while (ui_event_pop(&U, &e)) {
            if ((e.type == UI_EV_BTN_DOWN) && (e.a == UI_BTN_H_WX) && (e.src == UI_SRC_UART)) { down++; }
            else if ((e.type == UI_EV_BTN_ACT) && (e.a == UI_BTN_H_WX) && (e.src == UI_SRC_UART)) { act++; }
            else { other++; }
        }
        CHECK_EQ(down, 1); CHECK_EQ(act, 1); CHECK_EQ(other, 0);          /* 画面遷移・設定温度の変化なし */
    }
    CHECK_EQ(U.v.scr, UI_SCR_HOME); CHECK_EQ(U.v.set, UI_SET_INIT); CHECK_EQ(U.tr_on, 0); CHECK_EQ(U.fb_phase, UI_PH_NONE);

    TEST("X2d UI_CMD_WX は、ホーム以外・デモ中・押下表示中・遷移中は何も起こさない(イベントなし)");
    for (k = 0u; k < 4u; k++) {
        ui_init(&U, 0u, 479u, 271u, 0u);
        wx_std(&U);
        if (k == 0u) { U.v.scr = UI_SCR_FILL; }
        else if (k == 1u) { ui_cmd(&U, UI_CMD_DEMO, 100u); (void)n_ev(255u, 255u, 255u); }
        else if (k == 2u) { ui_cmd(&U, UI_CMD_PLUS, 100u); (void)n_ev(255u, 255u, 255u); }      /* + の押下表示中 */
        else { ui_cmd(&U, UI_CMD_NEXT, 100u); (void)n_ev(255u, 255u, 255u); U.v.scr = UI_SCR_HOME; }   /* 遷移中(tr_on) */
        ui_cmd(&U, UI_CMD_WX, 110u);
        CHECK_EQ(n_ev(UI_EV_BTN_DOWN, UI_BTN_H_WX, 255u), 0);
    }

    TEST("X2e 実タッチ: 当たり判定 (228,136,60,52) の境界の内側は h_wx(BTN_DOWN + BTN_ACT src=touch)、1 画素外は h_wx にならない");
    {
        static const int in_pts[][2] = { {228, 136}, {287, 136}, {228, 187}, {287, 187}, {262, 172}, {250, 160} };
        static const int out_pts[][2] = { {227, 136}, {288, 136}, {228, 135}, {228, 188}, {287, 188}, {227, 187} };
        for (i = 0u; i < 6u; i++) {
            ui_init(&U, 0u, 479u, 271u, 0u);
            touch_tap(in_pts[i][0], in_pts[i][1], 1000u);
            { ui_ev_t e; unsigned d = 0u, ac = 0u; while (ui_event_pop(&U, &e)) { if ((e.a == UI_BTN_H_WX) && (e.src == UI_SRC_TOUCH)) { if (e.type == UI_EV_BTN_DOWN) { d++; } if (e.type == UI_EV_BTN_ACT) { ac++; } } }
              CHECK_EQ(d, 1); CHECK_EQ(ac, 1); }
            CHECK_EQ(U.v.scr, UI_SCR_HOME);
        }
        for (i = 0u; i < 6u; i++) {
            ui_init(&U, 0u, 479u, 271u, 0u);
            touch_tap(out_pts[i][0], out_pts[i][1], 1000u);
            CHECK_EQ(n_ev(UI_EV_BTN_DOWN, UI_BTN_H_WX, 255u), 0);
        }
    }

    TEST("X2f デモの台本は h_wx を押さない(36 行のうち h_wx が 0)。デモ中の実タッチは中断のまま");
    ui_init(&U, 0u, 479u, 271u, 0u);
    wx_std(&U);
    ui_cmd(&U, UI_CMD_DEMO, 100u);
    {
        ui_ev_t e; unsigned total = 0u, wx = 0u; uint32_t t;
        for (t = 110u; t <= 34000u; t += 10u) {                    /* 待ち行列は 8 件なので、毎周期取り出す */
            ui_update(&U, NULL, t);
            while (ui_event_pop(&U, &e)) { total++; if ((e.a == UI_BTN_H_WX) && ((e.type == UI_EV_BTN_DOWN) || (e.type == UI_EV_BTN_ACT))) { wx++; } }
        }
        CHECK_EQ(wx, 0); CHECK_EQ(total, 36);
    }

    TEST("X3 天気 8 種: 矩形 (20,158,28,28) がスプライトと一致、矩形の外は変わらない(未取得の間はどの kind でも「不明」)");
    mk(&a, UI_SCR_HOME); wx_std(&a);
    for (k = 0u; k < 8u; k++) {
        const rc_t *rs[1]; rs[0] = &R_WX;
        b = a; b.wx.kind = (uint8_t)k;
        rend(&a, g_a); rend(&b, g_b);
        CHECK(is_spr(g_b, &R_WX, ui_spr_wx[k], 0));
        CHECK_EQ(diff_outside(g_a, g_b, rs, 1u), 0);
        b.wx.have = 0u;
        rend(&b, g_c);
        CHECK(is_spr(g_c, &R_WX, ui_spr_wx[UI_SPR_WX_N - 1u], 0));
    }
    for (i = 0u; i < 8u; i++) { for (k = i + 1u; k < 8u; k++) { CHECK(memcmp(ui_spr_wx[i], ui_spr_wx[k], UI_WX_W * UI_WX_H) != 0); } }   /* 8 種がすべて別の絵 */

    TEST("X3b Wi-Fi 4 状態: 待機以外の 4 画面で (112,7,20,20) がスプライトと一致、矩形の外は変わらない。待機には出ない");
    for (i = 0u; i < 5u; i++) {
        mk(&a, (uint8_t)i); wx_std(&a);
        for (k = 0u; k < 4u; k++) {
            const rc_t *rs[1]; rs[0] = &R_WIFI;
            b = a; b.wx.net = (uint8_t)k;
            rend(&a, g_a); rend(&b, g_b);
            if (i == (unsigned)UI_SCR_STANDBY) { CHECK(memcmp(g_a, g_b, NPX) == 0); }
            else { CHECK(is_spr(g_b, &R_WIFI, ui_spr_wifi[k], 0)); CHECK_EQ(diff_outside(g_a, g_b, rs, 1u), 0); }
        }
    }
    for (i = 0u; i < 4u; i++) { for (k = i + 1u; k < 4u; k++) { CHECK(memcmp(ui_spr_wifi[i], ui_spr_wifi[k], UI_WIFI_W * UI_WIFI_H) != 0); } }

    TEST("X3c 更新アイコン: busy の回転 4 段階が (252,162,20,20) に出る(busy = 0 は 0 段階)。矩形の外は変わらない");
    mk(&a, UI_SCR_HOME); wx_std(&a);
    for (k = 0u; k < 4u; k++) {
        const rc_t *rs[1]; rs[0] = &R_REF;
        b = a; b.wx.busy = 1u; b.spin = (uint8_t)k;
        rend(&b, g_b); rend(&a, g_a);
        CHECK(is_spr(g_b, &R_REF, ui_spr_ref[k], 0));
        if (k == 0u) { CHECK_EQ(diff_outside(g_a, g_b, rs, 1u), 0); CHECK(is_spr(g_a, &R_REF, ui_spr_ref[0], 0)); }
    }
    for (i = 0u; i < 4u; i++) { for (k = i + 1u; k < 4u; k++) { CHECK(memcmp(ui_spr_ref[i], ui_spr_ref[k], UI_REF_W * UI_REF_H) != 0); } }

    TEST("X3d 日付: clk_ext = 0 は旧日付(11月21日(土))で wx の日付は無視、clk_ext = 1 は wx の日付。未同期(month = 0)・範囲外は空白(背景のまま)。変化は日付の矩形だけ");
    mk(&a, UI_SCR_HOME); wx_std(&a);
    {
        const rc_t *rs[1]; rs[0] = &R_DATE;
        b = a; b.wx.month = 3u; b.wx.day = 7u; b.wx.wday = 0u;
        rend(&a, g_a); rend(&b, g_b);
        CHECK(diff_in(g_a, g_b, &R_DATE) > 20u);
        CHECK_EQ(diff_outside(g_a, g_b, rs, 1u), 0);
        b = a; b.wx.month = 0u; rend(&b, g_b);                                  /* 未同期 = 空白 */
        CHECK_EQ(ink_count(g_b, ui_bgi[UI_BG_HOME], &R_DATE), 0);
        for (i = 0u; i < 6u; i++) {                                               /* 範囲外も空白 */
            b = a; if (i == 0u) { b.wx.month = 13u; } else if (i == 1u) { b.wx.day = 0u; } else if (i == 2u) { b.wx.day = 32u; } else if (i == 3u) { b.wx.wday = 7u; } else if (i == 4u) { b.wx.month = 255u; } else { b.wx.wday = 255u; }
            rend(&b, g_b);
            CHECK_EQ(ink_count(g_b, ui_bgi[UI_BG_HOME], &R_DATE), 0);
        }
        CHECK(ink_count(g_a, ui_bgi[UI_BG_HOME], &R_DATE) > 100u);
        CHECK(ink_xmax(g_a, ui_bgi[UI_BG_HOME], &R_DATE) < UI_WIFI_X - 4);          /* Wi-Fi アイコンに重ならない(最長 12月30日(水) でも。下で確認) */
        b = a; b.wx.month = 12u; b.wx.day = 30u; b.wx.wday = 4u; rend(&b, g_b);
        CHECK(ink_xmax(g_b, ui_bgi[UI_BG_HOME], &R_DATE) < UI_WIFI_X - 4);
        /* 7 つの曜日がすべて別の絵 */
        for (i = 0u; i < 7u; i++) { for (k = i + 1u; k < 7u; k++) {
            b = a; b.wx.wday = (uint8_t)i; rend(&b, g_b); b.wx.wday = (uint8_t)k; rend(&b, g_c);
            CHECK(memcmp(g_b, g_c, NPX) != 0); } }
        /* clk_ext = 0: 旧日付。wx の日付を変えても同じ */
        b = a; b.wx.clk_ext = 0u; b.wx.month = 1u; b.wx.day = 1u; b.wx.wday = 1u; rend(&b, g_b);
        b.wx.month = 9u; b.wx.day = 9u; b.wx.wday = 5u; rend(&b, g_c);
        CHECK(memcmp(g_b, g_c, NPX) == 0);
        CHECK(ink_count(g_b, ui_bgi[UI_BG_HOME], &R_DATE) > 100u);
    }

    TEST("X3e 気温: 右詰め(右端 x <= 106)、符号・桁(12.6 / -1.2 / -10.5 / 0.0 / 99.9)、未取得は --.-、30 分超は灰色(同じ形で色だけ違う)。変化は気温の矩形だけ");
    mk(&a, UI_SCR_HOME); wx_std(&a);
    {
        static const int16_t T[] = { 126, -12, -105, 0, 999, -999, 5, -5, 100, 1000, -1000, 32767, -32768 };
        const rc_t *rs[1]; rs[0] = &R_TEMP;
        rend(&a, g_a);
        for (i = 0u; i < sizeof(T) / sizeof(T[0]); i++) {
            b = a; b.wx.temp_x10 = T[i]; rend(&b, g_b);
            CHECK_EQ(diff_outside(g_a, g_b, rs, 1u), 0);
            CHECK(ink_xmax(g_b, ui_bgi[UI_BG_HOME], &R_TEMP) <= 106);
            CHECK(ink_xmin(g_b, ui_bgi[UI_BG_HOME], &R_TEMP) >= UI_WX_X + UI_WX_W);                  /* 天気アイコンに重ならない */
        }
        b = a; b.wx.temp_x10 = -105; rend(&b, g_b);
        CHECK(ink_xmin(g_b, ui_bgi[UI_BG_HOME], &R_TEMP) >= UI_WX_X + UI_WX_W);
        b = a; b.wx.have = 0u; rend(&b, g_b);                                          /* --.-: ハイフン 2 本と点 */
        CHECK(ink_count(g_b, ui_bgi[UI_BG_HOME], &R_TEMP) > 10u);
        CHECK(memcmp(g_a, g_b, NPX) != 0);
        /* 範囲外は 99.9 に丸める */
        { ui_t c = a; c.wx.temp_x10 = 1000; rend(&c, g_b); c.wx.temp_x10 = 999; rend(&c, g_c); CHECK(memcmp(g_b, g_c, NPX) == 0); }
        { ui_t c = a; c.wx.temp_x10 = -32768; rend(&c, g_b); c.wx.temp_x10 = -999; rend(&c, g_c); CHECK(memcmp(g_b, g_c, NPX) == 0); }
        /* 古い値の灰色: 画素が変わる場所の数は同じ(形が同じ)で、色が違う */
        b = a; rend(&b, g_a); b.wx.stale = 1u; rend(&b, g_b);
        CHECK(memcmp(g_a, g_b, NPX) != 0);
        CHECK(diff_in(g_a, g_b, &R_TEMP) > 20u);
        { const rc_t *r2[1]; r2[0] = &R_TEMP; CHECK_EQ(diff_outside(g_a, g_b, r2, 1u), 0); }
    }

    TEST("X3f 更新時刻(x = 196): have = 0 は --:--、have = 1 は upd_min。0xFFFF・1440 以上も --:--。変化は矩形だけ");
    mk(&a, UI_SCR_HOME); wx_std(&a);
    {
        const rc_t *rs[1]; rs[0] = &R_UPD;
        rend(&a, g_a);
        b = a; b.wx.upd_min = 7u * 60u + 5u; rend(&b, g_b);
        CHECK(diff_in(g_a, g_b, &R_UPD) > 10u); CHECK_EQ(diff_outside(g_a, g_b, rs, 1u), 0);
        b = a; b.wx.have = 0u; rend(&b, g_b);
        { ui_t c = a; c.wx.upd_min = 0xFFFFu; rend(&c, g_c); CHECK_EQ(diff_in(g_b, g_c, &R_UPD), 0); }
        { ui_t c = a; c.wx.upd_min = 1440u; rend(&c, g_c); CHECK_EQ(diff_in(g_b, g_c, &R_UPD), 0); }
        CHECK(ink_xmin(g_a, ui_bgi[UI_BG_HOME], &R_UPD) >= 196);
    }

    TEST("X3g 時計: clock_min = 0xFFFF(未同期)はホームの大きい時計も待機の時計も --:--(正常な時刻と違う絵)");
    for (i = 0u; i < 5u; i += 4u) {
        mk(&a, (uint8_t)i); wx_std(&a);
        b = a; b.v.clock_min = 0xFFFFu; rend(&b, g_b);
        { ui_t c = a; c.v.clock_min = 1440u; rend(&c, g_c); CHECK(memcmp(g_b, g_c, NPX) == 0); }
        rend(&a, g_a); CHECK(memcmp(g_a, g_b, NPX) != 0);
    }

    TEST("X3h 他の要素は変わらない: ホームの wx を全部変えても、日付・気温・更新時刻・天気・更新・Wi-Fi の矩形の外は同じ(時計・設定カード・メニュー・補助文)");
    mk(&a, UI_SCR_HOME); wx_std(&a);
    {
        const rc_t *rs[6]; ui_wx_t w;
        rs[0] = &R_WIFI; rs[1] = &R_WX; rs[2] = &R_REF; rs[3] = &R_DATE; rs[4] = &R_TEMP; rs[5] = &R_UPD;
        memset(&w, 0, sizeof(w)); w.net = UI_NET_ERR; w.kind = 6u; w.temp_x10 = -105; w.stale = 1u; w.upd_min = 1u; w.month = 1u; w.day = 1u; w.wday = 0u; w.clk_ext = 1u; w.have = 1u; w.busy = 1u;
        w.clock_min = 13u * 60u + 24u;
        b = a; ui_set_wx(&b, &w); b.spin = 2u; rend(&a, g_a); rend(&b, g_b);
        CHECK_EQ(diff_outside(g_a, g_b, rs, 6u), 0);
    }

    TEST("X3i 押下・白光の更新ボタン: アイコンも一緒に暗く・白くなる(アイコンは remap の前に描く)。形の外は変わらない");
    mk(&a, UI_SCR_HOME); wx_std(&a);
    {
        const ui_btn_def_t *d = &ui_btn_defs[UI_BTN_H_WX];
        const rc_t vis = { d->vis.x, d->vis.y, d->vis.w, d->vis.h };
        const rc_t *rs[1]; rs[0] = &vis;
        for (k = 0u; k < 2u; k++) {
            b = a; b.fb_btn = UI_BTN_H_WX; b.fb_phase = (uint8_t)((k == 0u) ? UI_PH_PRESS : UI_PH_FLASH);
            rend(&a, g_a); rend(&b, g_b);
            CHECK_EQ(diff_outside(g_a, g_b, rs, 1u), 0);
            CHECK(diff_in(g_a, g_b, &R_REF) > 50u);                       /* アイコンの画素も変わる */
        }
    }

    TEST("X3j スライド中: Wi-Fi アイコンは画面と一緒に動く(新しい画面 xo、古い画面 xo)。ホームの天気の行も");
    {
        static const int dxs[] = { 1, 100, 240, 300, 479 };
        for (k = 0u; k < 2u; k++) {          /* 0 = 前進(ホーム → 湯はり)、1 = 後退(湯はり → ホーム) */
            for (i = 0u; i < 5u; i++) {
                int dx = dxs[i], xo_old = (k == 0u) ? -dx : dx, xo_new = (k == 0u) ? 480 - dx : dx - 480;
                int x_old = UI_WIFI_X + xo_old, x_new = UI_WIFI_X + xo_new;
                mk(&a, (k == 0u) ? UI_SCR_HOME : UI_SCR_FILL); wx_std(&a);
                mk(&b, (k == 0u) ? UI_SCR_FILL : UI_SCR_HOME); wx_std(&b);
                b.wx.net = UI_NET_CONN; b.ov = a.v; b.tr_on = 1u; b.tr_fwd = (uint8_t)(k == 0u); b.tr_dx = (uint16_t)dx;
                rend(&b, g_b);
                if ((x_new >= 0) && (x_new + UI_WIFI_W <= 480)) { CHECK(is_spr(g_b, &R_WIFI, ui_spr_wifi[UI_NET_CONN], xo_new)); }
                if ((x_old >= 0) && (x_old + UI_WIFI_W <= 480)) { CHECK(is_spr(g_b, &R_WIFI, ui_spr_wifi[UI_NET_CONN], xo_old)); }
                CHECK(right_untouched(g_b, 0xA5u));
            }
        }
    }

    TEST("X3k 全画素を書く: 面を 0x00 と 0xFF で埋めて描き、左 480 列が同一・右 32 列は不変。5 画面 x wx の状態(極端な値・範囲外を含む)x 遷移");
    {
        static const ui_wx_t W[] = {
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFFFF, 0xFFFF },
            { 2, 1, 1, 7, 0, 1, 11, 25, 3, 0, 126, 804, 804 },
            { 3, 0, 1, 4, 1, 1, 12, 31, 6, 0, -999, 0, 1439 },
            { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 32767, 65535, 65535 },
            { 1, 1, 1, 8, 1, 1, 0, 0, 7, 0, -32768, 1440, 1440 },
        };
        unsigned s, w, f, dxi;
        static const uint16_t dxs[] = { 0u, 1u, 120u, 240u, 479u, 480u };
        bad = 0u;
        for (s = 0u; s < 5u; s++) { for (w = 0u; w < 5u; w++) { for (f = 0u; f < 2u; f++) { for (dxi = 0u; dxi < 6u; dxi++) {
            static ui_t u;
            mk(&u, (uint8_t)s); ui_set_wx(&u, &W[w]); u.spin = (uint8_t)(w * 3u);           /* spin は 3 以上でも安全 */
            if (dxi != 0u) { static ui_t o; mk(&o, (uint8_t)((s + 1u) % 5u)); u.ov = o.v; u.tr_on = 1u; u.tr_fwd = (uint8_t)f; u.tr_dx = dxs[dxi]; }
            memset(g_a, 0x00, NPX); memset(g_b, 0xFF, NPX);
            ui_render(&u, g_a); ui_render(&u, g_b);
            { size_t r; for (r = 0u; r < GFX_H; r++) { if (memcmp(&g_a[r * GFX_STRIDE], &g_b[r * GFX_STRIDE], GFX_W) != 0) { bad++; break; } } }
            if (!right_untouched(g_a, 0x00u) || !right_untouched(g_b, 0xFFu)) { bad++; }
        } } } }
        CHECK_EQ(bad, 0);
    }

    TEST("X4 ui_set_wx は rsv を 0 にする(表示キーの安定)。ui_wx_t の同じ値なら ui_view_changed が偽");
    ui_init(&a, 0u, 479u, 271u, 0u);
    {
        ui_wx_t w; memset(&w, 0xFF, sizeof(w)); w.rsv = 0x55u;
        ui_set_wx(&a, &w); CHECK_EQ(a.wx.rsv, 0);
        ui_update(&a, NULL, 10u); ui_view_mark(&a);
        ui_set_wx(&a, &w); ui_update(&a, NULL, 20u); CHECK(!ui_view_changed(&a));
    }

    TEST_REPORT("test_ui_wx");
}
