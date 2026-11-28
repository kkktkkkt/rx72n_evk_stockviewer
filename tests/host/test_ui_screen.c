/* test_ui_screen.c - src/app/ui_logic.c の画面の切り替え(docs/design.md §13.3 と §13.10 の H-C6)のホスト試験。
 *   ① 株価 → 時計・天気が APP_DWELL_STOCK_MS ちょうどで切り替わる(1 ms 手前は切り替わらない)。時計・天気 → 株価が APP_DWELL_CLOCK_MS ちょうど
 *   ② 32bit の now_ms の巻き戻りをまたいでも同じ
 *   ③ 時刻未同期(clock_min = 0xFFFF や 1440)なら切り替わらず、同期した次の ui_screen_step で切り替わる(t_scr は動かない)
 *   ④ 時計・天気画面で時刻が使えなくなったら株価へ
 *   ⑤ タップ(実タッチ・ui_tap_inject)で、時計・天気画面なら株価へ戻り t_scr が更新される。株価画面なら画面は変わらず t_scr だけ更新される
 *   ⑥ 表示キー(ui_view_changed / ui_view_mark): 画面が変わると再描画、見えていない側の値が変わっても再描画しない、見えている側が変わると再描画
 *   ⑦ ui_pal_id(ui_pal_ptr の範囲は test_ui_clock.c)
 *   ほか: ui_init の初期値(株価・時刻未同期・天気なし)、ui_set_screen の範囲外、ui_update の now の記録(標本なしでも)
 * 既定の APP_DWELL_*(10,000 / 5,000)とは別の値で -D してもよい(期待は同じマクロから作る)。 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "ui_logic.h"
#include "test_framework.h"

#define S_MS ((uint32_t)APP_DWELL_STOCK_MS)
#define C_MS ((uint32_t)APP_DWELL_CLOCK_MS)

static clk_view_t cv(uint16_t clock_min, uint8_t have, uint8_t kind)
{
    clk_view_t v;
    memset(&v, 0, sizeof v);
    v.clock_min = clock_min; v.have = have; v.kind = kind; v.temp = have ? 18 : 0;
    return v;
}

/* 10ms ずつ now を進めて ui_screen_step を呼ぶ。最初に切り替わった時刻を返す(0xFFFFFFFF = 切り替わらなかった) */
static uint32_t run_until_change(ui_t *u, uint32_t t0, uint32_t span)
{
    uint8_t scr0 = u->screen;
    uint32_t t;

    for (t = t0; (uint32_t)(t - t0) <= span; t += 10u) {
        ui_screen_step(u, t);
        if (u->screen != scr0) { return t; }
    }
    return 0xFFFFFFFFu;
}

static void test_init(void)
{
    ui_t u;
    clk_view_t z;

    TEST("ui_init: 株価画面・時計・天気は未同期・天気なし(unknown)・t_scr = now_ms。パレット番号 0");
    ui_init(&u, 1234u);
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    CHECK_EQ(u.t_scr, 1234u);
    CHECK_EQ(u.now, 1234u);
    memset(&z, 0, sizeof z);
    z.clock_min = 0xFFFFu; z.kind = CLK_UNKNOWN;
    CHECK(memcmp(&u.cview, &z, sizeof z) == 0);
    CHECK_EQ(ui_pal_id(&u), 0);
    CHECK(!(u.cview.clock_min <= CLK_MINUTES_MAX));         /* 未同期 = 使えない */
    CHECK_EQ(UI_SCR_STOCK, 0); CHECK_EQ(UI_SCR_CLOCK, 1);
    CHECK_EQ(S_MS, 10000u); CHECK_EQ(C_MS, 5000u);          /* 既定(-D で変えたビルドでは外れる。run.sh は既定でビルドする) */
}

static void test_dwell(void)
{
    ui_t u;
    clk_view_t c = cv(581u, 1u, CLK_SUN);
    uint32_t t, base = 777770u;
    int ok = 1;

    TEST("H-C6 ①: 株価 → 時計・天気が APP_DWELL_STOCK_MS ちょうどで切り替わる(手前は切り替わらない)。時計・天気 → 株価が APP_DWELL_CLOCK_MS ちょうど");
    ui_init(&u, base);
    ui_set_clk(&u, &c);
    ui_screen_step(&u, base + S_MS - 1u);
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    ui_screen_step(&u, base + S_MS);
    CHECK_EQ(u.screen, UI_SCR_CLOCK);
    CHECK_EQ(u.t_scr, base + S_MS);
    ui_screen_step(&u, base + S_MS + C_MS - 1u);
    CHECK_EQ(u.screen, UI_SCR_CLOCK);
    ui_screen_step(&u, base + S_MS + C_MS);
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    CHECK_EQ(u.t_scr, base + S_MS + C_MS);
    for (t = 0u; t < 6u; t++) {                              /* 周期が崩れない(10ms 刻みで 5 周) */
        uint32_t a = run_until_change(&u, u.t_scr, S_MS + 100u);
        uint32_t b;
        if (a == 0xFFFFFFFFu) { ok = 0; break; }
        b = run_until_change(&u, a, C_MS + 100u);
        if (b == 0xFFFFFFFFu) { ok = 0; break; }
    }
    CHECK(ok);
    {
        uint32_t t0 = u.t_scr, a, b;
        a = run_until_change(&u, t0, S_MS + 100u);
        b = run_until_change(&u, a, C_MS + 100u);
        CHECK_EQ(a - t0, S_MS);
        CHECK_EQ(b - a, C_MS);
    }

    TEST("H-C6 ②: 32bit の now_ms の巻き戻りをまたいでも同じ(t_scr が 0xFFFFFFFF 付近)");
    {
        uint32_t t0 = 0xFFFFFFFFu - 3000u;
        ui_init(&u, t0);
        ui_set_clk(&u, &c);
        ui_screen_step(&u, t0 + S_MS - 1u);                   /* 巻き戻って小さい値になっている */
        CHECK_EQ(u.screen, UI_SCR_STOCK);
        ui_screen_step(&u, t0 + S_MS);
        CHECK_EQ(u.screen, UI_SCR_CLOCK);
        CHECK_EQ(u.t_scr, (uint32_t)(t0 + S_MS));
        ui_screen_step(&u, u.t_scr + C_MS - 1u);
        CHECK_EQ(u.screen, UI_SCR_CLOCK);
        ui_screen_step(&u, u.t_scr + C_MS);
        CHECK_EQ(u.screen, UI_SCR_STOCK);
    }
}

static void test_unsynced(void)
{
    ui_t u;
    clk_view_t un = cv(0xFFFFu, 0u, CLK_UNKNOWN);
    clk_view_t ok = cv(1439u, 1u, CLK_RAIN);
    uint32_t t;

    TEST("H-C6 ③: 時刻未同期(0xFFFF・1440)は切り替わらず t_scr も動かない。同期した次の ui_screen_step で切り替わる");
    ui_init(&u, 0u);
    ui_set_clk(&u, &un);
    for (t = 0u; t < 60000u; t += 10u) { ui_screen_step(&u, t); }
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    CHECK_EQ(u.t_scr, 0u);                                    /* 待つ間は動かさない */
    un.clock_min = 1440u; ui_set_clk(&u, &un);
    ui_screen_step(&u, 70000u);
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    un.clock_min = 0xFFFEu; ui_set_clk(&u, &un);
    ui_screen_step(&u, 70010u);
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    ui_set_clk(&u, &ok);                                      /* 同期した */
    ui_screen_step(&u, 70020u);
    CHECK_EQ(u.screen, UI_SCR_CLOCK);                         /* 株価の表示時間がとうに過ぎているので、すぐ */
    CHECK_EQ(u.t_scr, 70020u);

    TEST("H-C6 ④: 時計・天気画面で時刻が使えなくなったら株価へ(その時点で)");
    ui_set_clk(&u, &un);
    ui_screen_step(&u, 70030u);
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    CHECK_EQ(u.t_scr, 70030u);
    ui_set_clk(&u, &ok);
    ui_screen_step(&u, 70030u + S_MS);                        /* 株価の表示時間は t_scr から数え直す */
    CHECK_EQ(u.screen, UI_SCR_CLOCK);
    ui_init(&u, 0u);
    ui_set_clk(&u, &ok);
    ui_set_screen(&u, UI_SCR_CLOCK, 100u);                    /* 同期済みで時計・天気 → 5 秒で株価 */
    ui_screen_step(&u, 100u + C_MS - 1u);
    CHECK_EQ(u.screen, UI_SCR_CLOCK);
    ui_screen_step(&u, 100u + C_MS);
    CHECK_EQ(u.screen, UI_SCR_STOCK);
}

static nav_sample_t dn(void) { nav_sample_t s; memset(&s, 0, sizeof s); s.pressed = true; s.points = 1u; s.x = 100u; s.y = 100u; return s; }
static nav_sample_t up(void) { nav_sample_t s; memset(&s, 0, sizeof s); return s; }

static void test_tap(void)
{
    ui_t u;
    clk_view_t ok = cv(581u, 1u, CLK_SNOW);
    nav_sample_t d = dn(), p = up();
    ui_ev_t ev;
    uint32_t t;

    TEST("H-C6 ⑤: 実タッチのタップ(押下 2・離し 3 標本)で、時計・天気画面なら株価へ戻り t_scr が更新される");
    ui_init(&u, 0u);
    ui_set_clk(&u, &ok);
    ui_screen_step(&u, S_MS);
    CHECK_EQ(u.screen, UI_SCR_CLOCK);
    t = S_MS + 100u;
    ui_update(&u, &d, t += 10u);
    ui_update(&u, &d, t += 10u);
    ui_update(&u, &p, t += 10u);
    ui_update(&u, &p, t += 10u);
    CHECK_EQ(u.screen, UI_SCR_CLOCK);                         /* 離し確定の前はそのまま */
    ui_update(&u, &p, t += 10u);                              /* 離し確定 */
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    CHECK_EQ(u.t_scr, t);
    CHECK(ui_event_pop(&u, &ev));
    CHECK_EQ(ev.type, UI_EV_TAP); CHECK_EQ(ev.src, UI_SRC_TOUCH);

    TEST("H-C6 ⑤: 株価画面でのタップは画面を変えず t_scr だけ更新(次の切り替えがタップから APP_DWELL_STOCK_MS 後)");
    ui_update(&u, &d, t += 4000u);
    ui_update(&u, &d, t += 10u);
    ui_update(&u, &p, t += 10u);
    ui_update(&u, &p, t += 10u);
    ui_update(&u, &p, t += 10u);
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    CHECK_EQ(u.t_scr, t);
    CHECK(ui_event_pop(&u, &ev));
    ui_screen_step(&u, t + S_MS - 1u);
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    ui_screen_step(&u, t + S_MS);
    CHECK_EQ(u.screen, UI_SCR_CLOCK);

    TEST("H-C6 ⑤: UART の x(ui_tap_inject)も同じ。ui_update が受けた最後の now_ms を使う(標本なし NULL でも now は進む)");
    ui_update(&u, NULL, 123456u);
    CHECK_EQ(u.now, 123456u);
    ui_tap_inject(&u, UI_SRC_UART);
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    CHECK_EQ(u.t_scr, 123456u);
    CHECK(ui_event_pop(&u, &ev));
    CHECK_EQ(ev.src, UI_SRC_UART);
    ui_update(&u, NULL, 123500u);
    ui_tap_inject(&u, UI_SRC_UART);                           /* 株価画面: 画面はそのまま、t_scr だけ */
    CHECK_EQ(u.screen, UI_SCR_STOCK);
    CHECK_EQ(u.t_scr, 123500u);

    TEST("H-C6 ⑤: イベントキューが満杯でもタップで株価へ戻る(更新要求の取りこぼしと画面は別)");
    ui_init(&u, 0u);
    {
        unsigned i;
        for (i = 0u; i < UI_EV_Q; i++) { ui_tap_inject(&u, UI_SRC_UART); }
        ui_set_screen(&u, UI_SCR_CLOCK, 5u);
        ui_tap_inject(&u, UI_SRC_UART);
        CHECK_EQ(u.screen, UI_SCR_STOCK);
        CHECK_EQ(u.ev_lost, 1);
    }
}

static void test_key(void)
{
    ui_t u;
    clk_view_t c = cv(581u, 1u, CLK_SUN);
    stk_view_t sv;

    TEST("H-C6 ⑥: 表示キー: 画面が変わると再描画、見えていない側の値が変わっても再描画しない、見えている側が変わると再描画、ui_view_mark の後は変わらない");
    ui_init(&u, 0u);
    ui_set_clk(&u, &c);
    CHECK(ui_view_changed(&u));                               /* 未描画 */
    ui_view_mark(&u);
    CHECK(!ui_view_changed(&u));
    c.clock_min = 582u; ui_set_clk(&u, &c);                   /* 株価画面を見ている間の、時計・天気の値の変化は無視 */
    CHECK(!ui_view_changed(&u));
    ui_set_screen(&u, UI_SCR_CLOCK, 0u);                      /* 画面が変わる */
    CHECK(ui_view_changed(&u));
    ui_view_mark(&u);
    CHECK(!ui_view_changed(&u));
    CHECK(memcmp(&u.cdrawn, &c, sizeof c) == 0);              /* 見えている側(時計・天気)の drawn が更新された */
    sv = u.view; sv.net = STK_NET_ON; ui_set_view(&u, &sv);   /* 時計・天気を見ている間の、株価の値の変化は無視 */
    CHECK(!ui_view_changed(&u));
    c.clock_min = 583u; ui_set_clk(&u, &c);                   /* 見えている側の変化 */
    CHECK(ui_view_changed(&u));
    ui_view_mark(&u);
    CHECK(!ui_view_changed(&u));
    c.temp = 19; ui_set_clk(&u, &c); CHECK(ui_view_changed(&u)); ui_view_mark(&u);
    c.kind = CLK_RAIN; ui_set_clk(&u, &c); CHECK(ui_view_changed(&u)); ui_view_mark(&u);
    c.mon = 3; ui_set_clk(&u, &c); CHECK(ui_view_changed(&u)); ui_view_mark(&u);
    ui_set_clk(&u, &c); CHECK(!ui_view_changed(&u));          /* 同じ値は変わらない */
    ui_set_screen(&u, UI_SCR_STOCK, 0u);                      /* 株価へ戻る: 値が同じでも画面が変わったので再描画 */
    CHECK(ui_view_changed(&u));
    ui_view_mark(&u);
    CHECK(!ui_view_changed(&u));
    CHECK(memcmp(&u.drawn, &u.view, sizeof u.view) == 0);     /* 株価の drawn は、時計・天気を見ている間に変わった値になっている */
    sv.row[1].price_x100 = 5; ui_set_view(&u, &sv);
    CHECK(ui_view_changed(&u));
    ui_view_mark(&u);
    ui_set_screen(&u, UI_SCR_STOCK, 5u);                      /* 同じ画面を決め直しても再描画しない(t_scr だけ) */
    CHECK(!ui_view_changed(&u));
    CHECK_EQ(u.t_scr, 5u);

    TEST("ui_set_screen: 範囲外の画面番号は株価");
    ui_set_screen(&u, 2u, 9u); CHECK_EQ(u.screen, UI_SCR_STOCK);
    ui_set_screen(&u, 255u, 9u); CHECK_EQ(u.screen, UI_SCR_STOCK);
    ui_set_screen(&u, UI_SCR_CLOCK, 9u); CHECK_EQ(u.screen, UI_SCR_CLOCK);
    ui_set_clk(&u, NULL);                                     /* NULL は何もしない */
    CHECK(memcmp(&u.cview, &c, sizeof c) == 0);
}

static void test_pal_id(void)
{
    ui_t u;
    clk_view_t c;
    unsigned k;

    TEST("H-C6 ⑦: ui_pal_id: 株価 = 0、時計・天気は have なら 1 + kind、have = 0 なら 8(kind が何でも)");
    ui_init(&u, 0u);
    c = cv(581u, 1u, CLK_FOG);
    ui_set_clk(&u, &c);
    CHECK_EQ(ui_pal_id(&u), 0);                               /* 株価画面は、時計・天気の値に依らず 0 */
    ui_set_screen(&u, UI_SCR_CLOCK, 0u);
    for (k = 0u; k < 8u; k++) { c = cv(581u, 1u, (uint8_t)k); ui_set_clk(&u, &c); CHECK_EQ(ui_pal_id(&u), 1u + k); }
    for (k = 0u; k < 8u; k++) { c = cv(581u, 0u, (uint8_t)k); ui_set_clk(&u, &c); CHECK_EQ(ui_pal_id(&u), 8); }
    c = cv(581u, 1u, 9u); ui_set_clk(&u, &c); CHECK_EQ(ui_pal_id(&u), 8);       /* 範囲外の kind も unknown */
    c = cv(0xFFFFu, 0u, CLK_UNKNOWN); ui_set_clk(&u, &c); CHECK_EQ(ui_pal_id(&u), 8);   /* 未同期の画面(C9) */
}

int main(void)
{
    test_init();
    test_dwell();
    test_unsynced();
    test_tap();
    test_key();
    test_pal_id();
    TEST_REPORT("test_ui_screen");
}
