/* test_ui_logic.c - src/app/ui_logic.c の単体テスト(設計 §12 H-U1〜H-U7)。標準Cのみ + 生成データ(ui_layout.c)。
 * 時刻は仮想(10ms ごとに ui_update)。実機・実時間・LCD は試験できない。 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ui_logic.h"
#include "test_framework.h"

static ui_t     U;
static uint32_t T;
typedef struct { uint32_t t; char s[48]; } rec_t;
static rec_t    R[2048];
static unsigned nR;
static const nav_sample_t UP = { false, 0u, 0u, 0u };

static const char *src_s(uint8_t s) { return (s == UI_SRC_TOUCH) ? "touch" : ((s == UI_SRC_UART) ? "uart" : "demo"); }
static void ev_str(const ui_ev_t *e, char *o, size_t n)
{
    switch (e->type) {
    case UI_EV_SCR:      snprintf(o, n, "scr=%s from=%s src=%s", ui_scr_name[e->a], ui_scr_name[e->b], src_s(e->src)); break;
    case UI_EV_BTN_DOWN: snprintf(o, n, "btn=%s st=down src=%s", ui_btn_defs[e->a].name, src_s(e->src)); break;
    case UI_EV_BTN_ACT:  snprintf(o, n, "btn=%s st=act src=%s", ui_btn_defs[e->a].name, src_s(e->src)); break;
    case UI_EV_SET:      snprintf(o, n, "set=%u src=%s", e->a, src_s(e->src)); break;
    case UI_EV_DONE:     snprintf(o, n, "done=%s", (e->a == UI_DONE_FILL) ? "fill" : "reheat"); break;
    case UI_EV_DEMO:
        if (e->a == UI_DEMO_END) { snprintf(o, n, "demo=end"); }
        else { snprintf(o, n, "demo=%s src=%s", (e->a == UI_DEMO_START) ? "start" : "abort", src_s(e->src)); }
        break;
    default:             snprintf(o, n, "?"); break;
    }
}
static void drain(void)
{
    ui_ev_t e;
    while (ui_event_pop(&U, &e)) {
        if (nR < 2048u) { R[nR].t = T; ev_str(&e, R[nR].s, sizeof(R[nR].s)); nR++; }
    }
}
static void step(const nav_sample_t *s) { T += 10u; ui_update(&U, s, T); drain(); }
static void run(uint32_t ms) { uint32_t i; for (i = 0u; i < ms / 10u; i++) { step(&UP); } }
static void reset(uint8_t flags)
{
    T = 1000u; nR = 0u;
    ui_init(&U, T, 479u, 271u, flags);
}
static void cmd(ui_cmd_t c) { ui_cmd(&U, c, T); drain(); }
static nav_sample_t pt(uint8_t n, int32_t x, int32_t y)
{
    nav_sample_t s;
    s.pressed = true; s.points = n; s.x = (uint16_t)x; s.y = (uint16_t)y;
    return s;
}
static void center(uint8_t b, int32_t *x, int32_t *y)
{
    *x = ui_btn_defs[b].vis.x + ui_btn_defs[b].vis.w / 2;
    *y = ui_btn_defs[b].vis.y + ui_btn_defs[b].vis.h / 2;
}
/* 実タッチ(2標本で押下確定 → hold_ms 押す → 3標本で離し確定)。押下確定の時刻を返す。実行の後、さらに settle_ms 待つ */
static uint32_t tap_xy(int32_t x, int32_t y, uint32_t hold_ms, uint32_t settle_ms)
{
    nav_sample_t s = pt(1u, x, y);
    uint32_t td, i;
    step(&s);
    step(&s);
    td = T;
    for (i = 0u; i < hold_ms / 10u; i++) { step(&s); }
    step(&UP); step(&UP); step(&UP);
    run(60u + settle_ms);                                /* 離し確定(td+30)の後、実行(td+90)まで待つ */
    return td;
}
static uint32_t tap(uint8_t b, uint32_t settle_ms)
{
    int32_t x, y;
    center(b, &x, &y);
    return tap_xy(x, y, 0u, settle_ms);
}
static int nth_is(unsigned i, const char *s) { return (i < nR) && (strcmp(R[i].s, s) == 0); }
static unsigned count_pfx(const char *p) { unsigned i, n = 0u; for (i = 0u; i < nR; i++) { if (strncmp(R[i].s, p, strlen(p)) == 0) { n++; } } return n; }
static int find(const char *s) { unsigned i; for (i = 0u; i < nR; i++) { if (strcmp(R[i].s, s) == 0) { return (int)i; } } return -1; }
static void goto_scr(uint8_t b_btn, uint32_t settle)   /* HOME のボタンで画面へ */
{
    (void)tap(b_btn, settle);
}

int main(void)
{
    unsigned i;

    /* ================================================================ H-U2 式 */
    TEST("H-U2a 水位: e x speed / 32(0.1%単位)、1000 で頭打ち。speed=4 で 8.0 秒");
    CHECK_EQ(ui_fill_x10(0u, 1u), 0);
    CHECK_EQ(ui_fill_x10(1000u, 1u), 31);
    CHECK_EQ(ui_fill_x10(8000u, 1u), 250);
    CHECK_EQ(ui_fill_x10(31999u, 1u), 999);
    CHECK_EQ(ui_fill_x10(32000u, 1u), 1000);
    CHECK_EQ(ui_fill_x10(100000u, 1u), 1000);
    CHECK_EQ(ui_fill_x10(7999u, 4u), 999);
    CHECK_EQ(ui_fill_x10(8000u, 4u), 1000);
    CHECK_EQ(ui_fill_x10(0xFFFFFFFFu, 4u), 1000);          /* 桁あふれしない */
    CHECK_EQ(ui_fill_remain_min(0u), 8);
    CHECK_EQ(ui_fill_remain_min(10u), 8);                   /* 1%: (99*8+99)/100 = 8 */
    CHECK_EQ(ui_fill_remain_min(500u), 4);
    CHECK_EQ(ui_fill_remain_min(990u), 1);
    CHECK_EQ(ui_fill_remain_min(999u), 1);                  /* pct = 99 */
    CHECK_EQ(ui_fill_remain_min(1000u), 0);
    CHECK_EQ(ui_fill_w(0u), 0);
    CHECK_EQ(ui_fill_w(500u), 216);
    CHECK_EQ(ui_fill_w(1000u), 432);
    CHECK_EQ(ui_fill_w(5000u), 432);

    TEST("H-U2b 浴槽温度(追い焚き): 380 + e x speed / 400、設定 x 10 で頭打ち。追い焚き残り / バー幅");
    CHECK_EQ(ui_bath_x10(0u, 1u, 42u), 380);
    CHECK_EQ(ui_bath_x10(4000u, 1u, 42u), 390);
    CHECK_EQ(ui_bath_x10(15999u, 1u, 42u), 419);
    CHECK_EQ(ui_bath_x10(16000u, 1u, 42u), 420);
    CHECK_EQ(ui_bath_x10(99999u, 1u, 42u), 420);
    CHECK_EQ(ui_bath_x10(4999u, 4u, 43u), 429);
    CHECK_EQ(ui_bath_x10(5000u, 4u, 43u), 430);
    CHECK_EQ(ui_bath_x10(0u, 1u, 35u), 350);                /* 設定 35 は開始値 380 より低い: 設定で止まる */
    CHECK_EQ(ui_bath_x10(0xFFFFFFFFu, 4u, 48u), 480);
    CHECK_EQ(ui_reheat_remain_min(380u, 43u), 8);           /* 設計 §3.4: set=43 で 8 分 */
    CHECK_EQ(ui_reheat_remain_min(430u, 43u), 0);
    CHECK_EQ(ui_reheat_remain_min(380u, 48u), 15);
    CHECK_EQ(ui_reheat_remain_min(429u, 43u), 1);
    CHECK_EQ(ui_reheat_w(360u, 42u), 0);
    CHECK_EQ(ui_reheat_w(390u, 42u), 216);                  /* 432 x 30 / 60 */
    CHECK_EQ(ui_reheat_w(420u, 42u), 432);
    CHECK_EQ(ui_reheat_w(350u, 35u), 432);                  /* 完了 */
    CHECK_EQ(ui_reheat_w(380u, 36u), 432);                  /* 設定が低く、分母が 0 以下 */

    TEST("H-U2c 流量: 85 + T[(e/800)&15](67..103)、時計: 21:45 起点で実時間、24時間で巻き戻る");
    CHECK_EQ(ui_flow_x10(0u), 85);
    CHECK_EQ(ui_flow_x10(799u), 85);
    CHECK_EQ(ui_flow_x10(800u), 92);
    CHECK_EQ(ui_flow_x10(3200u), 103);
    CHECK_EQ(ui_flow_x10(6400u), 85);
    CHECK_EQ(ui_flow_x10(9600u), 67);
    CHECK_EQ(ui_flow_x10(12800u), 85);                      /* 16 x 800 ms で一周 */
    {
        unsigned lo = 999u, hi = 0u;
        for (i = 0u; i < 16u; i++) { unsigned f = ui_flow_x10(i * 800u); if (f < lo) { lo = f; } if (f > hi) { hi = f; } }
        CHECK_EQ(lo, 67); CHECK_EQ(hi, 103);
    }
    CHECK_EQ(ui_clock_min(0u), 21 * 60 + 45);
    CHECK_EQ(ui_clock_min(59999u), 21 * 60 + 45);
    CHECK_EQ(ui_clock_min(60000u), 21 * 60 + 46);
    CHECK_EQ(ui_clock_min(15u * 60000u), 22 * 60);
    CHECK_EQ(ui_clock_min(134u * 60000u), 23 * 60 + 59);
    CHECK_EQ(ui_clock_min(135u * 60000u), 0);               /* 24:00 -> 0:00 */
    CHECK_EQ(ui_clock_min(1440u * 60000u), 21 * 60 + 45);   /* 24時間後 */

    TEST("H-U5a スライド量: e=0 で 0、125 で 360、250 以上で 480、単調増加");
    CHECK_EQ(ui_slide_dx(0u), 0);
    CHECK_EQ(ui_slide_dx(125u), 360);
    CHECK_EQ(ui_slide_dx(250u), 480);
    CHECK_EQ(ui_slide_dx(100000u), 480);
    {
        unsigned prev = 0u; int mono = 1;
        for (i = 0u; i <= 250u; i++) { unsigned d = ui_slide_dx(i); if (d < prev) { mono = 0; } prev = d; }
        CHECK(mono);
    }

    TEST("棒グラフの高さ: 40ms x i 遅れて 400ms で伸びる(終わり際がゆっくり)");
    CHECK_EQ(ui_bar_h(0u, 0u), 0);
    CHECK_EQ(ui_bar_h(0u, 400u), 42);
    CHECK_EQ(ui_bar_h(3u, 120u), 0);
    CHECK_EQ(ui_bar_h(3u, 520u), 31);
    CHECK_EQ(ui_bar_h(6u, 639u) < 44u, 1);
    CHECK_EQ(ui_bar_h(6u, 640u), 44);
    CHECK_EQ(ui_bar_h(7u, 640u), 0);

    /* ================================================================ H-U4 タッチ座標・エッジ */
    TEST("H-U4a 座標変換: px = raw_x x 479 / X_MAX、py = raw_y x 271 / Y_MAX。範囲外は丸める");
    {
        int32_t px, py;
        ui_init(&U, 0u, 799u, 479u, 0u);
        ui_touch_map(&U, 0u, 0u, &px, &py);             CHECK_EQ(px, 0);   CHECK_EQ(py, 0);
        ui_touch_map(&U, 799u, 479u, &px, &py);         CHECK_EQ(px, 479); CHECK_EQ(py, 271);
        ui_touch_map(&U, 400u, 240u, &px, &py);         CHECK_EQ(px, 400 * 479 / 799); CHECK_EQ(py, 240 * 271 / 479);
        ui_touch_map(&U, 60000u, 60000u, &px, &py);     CHECK_EQ(px, 479); CHECK_EQ(py, 271);
        ui_init(&U, 0u, 479u, 271u, UI_TF_FLIP_X);
        ui_touch_map(&U, 10u, 20u, &px, &py);           CHECK_EQ(px, 469); CHECK_EQ(py, 20);
        ui_init(&U, 0u, 479u, 271u, UI_TF_FLIP_Y);
        ui_touch_map(&U, 10u, 20u, &px, &py);           CHECK_EQ(px, 10);  CHECK_EQ(py, 251);
        ui_init(&U, 0u, 271u, 479u, UI_TF_SWAP_XY);     /* 生 X 軸が画面の縦(0..271)、生 Y 軸が画面の横(0..479) */
        ui_touch_map(&U, 271u, 0u, &px, &py);           CHECK_EQ(px, 0);   CHECK_EQ(py, 271);
        ui_touch_map(&U, 0u, 479u, &px, &py);           CHECK_EQ(px, 479); CHECK_EQ(py, 0);
        ui_init(&U, 0u, 271u, 479u, UI_TF_SWAP_XY | UI_TF_FLIP_X | UI_TF_FLIP_Y);
        ui_touch_map(&U, 271u, 0u, &px, &py);           CHECK_EQ(px, 479); CHECK_EQ(py, 0);
        ui_init(&U, 0u, 0u, 0u, 0u);                    /* 最大値 0 でも落ちない */
        ui_touch_map(&U, 5u, 5u, &px, &py);             CHECK(px >= 0 && px <= 479 && py >= 0 && py <= 271);
    }

    TEST("H-U4b 座標の向きの設定(flags)で、反転した位置を押すと同じボタンが押される");
    reset(UI_TF_FLIP_X | UI_TF_FLIP_Y);
    run(100);
    (void)tap_xy(479 - (ui_btn_defs[UI_BTN_H_UP].vis.x + 5), 271 - (ui_btn_defs[UI_BTN_H_UP].vis.y + 5), 0u, 300u);
    CHECK(nth_is(0u, "btn=h_up st=down src=touch"));
    CHECK_EQ(U.v.set, 43);

    TEST("H-U4c 押下確定 = 1点が2標本連続。1標本だけ(ノイズ)は押下にならない");
    reset(0u);
    {
        nav_sample_t s = pt(1u, 420, 150);
        step(&s); step(&UP); step(&s); step(&UP); step(&s); step(&UP);
        run(300u);
        CHECK_EQ(nR, 0);
        step(&s);
        CHECK_EQ(U.fb_phase, UI_PH_NONE);
        step(&s);
        CHECK_EQ(U.fb_phase, UI_PH_PRESS);
        CHECK(nth_is(0u, "btn=h_up st=down src=touch"));
    }

    TEST("H-U4d 離し確定 = 触れていない標本が3標本連続。2標本の途切れでは離しにならない");
    reset(0u);
    {
        nav_sample_t s = pt(1u, 420, 150);
        uint32_t td;
        step(&s); step(&s); td = T;
        step(&UP); step(&UP); step(&s);                 /* 2標本の途切れ → 続けて押している扱い */
        for (i = 0u; i < 40u; i++) { step(&s); }        /* 400ms 押し続ける */
        CHECK_EQ(count_pfx("btn=h_up st=act"), 0);
        step(&UP); step(&UP);
        CHECK_EQ(count_pfx("btn=h_up st=act"), 0);
        step(&UP);                                       /* 3標本目で離し確定 → すぐ実行(90ms 以上経過) */
        CHECK_EQ(count_pfx("btn=h_up st=act"), 1);
        CHECK(R[1].t > td + 400u);
    }

    TEST("H-U4e 2点以上: 押下にならない。1点に戻っても、全部離れるまで押下にならない。押下中の2点目は無視");
    reset(0u);
    {
        nav_sample_t s2 = pt(2u, 420, 150), s1 = pt(1u, 420, 150);
        step(&s2); step(&s2); step(&s1); step(&s1); step(&s1); step(&s1);
        CHECK_EQ(nR, 0);
        CHECK_EQ(U.fb_phase, UI_PH_NONE);
        step(&UP); step(&UP); step(&UP);                 /* 全部離れた → 再び押せる */
        step(&s1); step(&s1);
        CHECK_EQ(U.fb_phase, UI_PH_PRESS);
        step(&s2); step(&s2); step(&s1);                 /* 押下中に2点目 → 無視(押下は続く) */
        CHECK_EQ(U.fb_phase, UI_PH_PRESS);
        CHECK_EQ(count_pfx("btn="), 1);
    }

    TEST("H-U4f NULL(標本なし)は状態を変えない: 押下確定の途中・押下中・離しの途中のどれでも");
    reset(0u);
    {
        nav_sample_t s = pt(1u, 420, 150);
        step(&s); for (i = 0u; i < 5u; i++) { step(NULL); }
        step(&s);                                        /* NULL を挟んでも2標本目で確定 */
        CHECK_EQ(U.fb_phase, UI_PH_PRESS);
        for (i = 0u; i < 20u; i++) { step(NULL); }       /* NULL だけ続いても離しにならない(押下中のまま) */
        CHECK_EQ(U.fb_phase, UI_PH_PRESS);
        CHECK_EQ(count_pfx("btn=h_up st=act"), 0);
        step(&UP); step(&UP); step(NULL); step(NULL);    /* 離しの途中の NULL もカウントしない */
        CHECK_EQ(count_pfx("btn=h_up st=act"), 0);
        step(&UP);
        CHECK_EQ(count_pfx("btn=h_up st=act"), 1);
    }

    TEST("H-U4g ボタン外のタッチ: 押下表示も [ev] も出ない(待機以外)");
    reset(0u);
    (void)tap_xy(100, 100, 100u, 300u);
    CHECK_EQ(nR, 0);
    CHECK_EQ(U.fb_phase, UI_PH_NONE);
    (void)tap_xy(0, 0, 0u, 300u);
    CHECK_EQ(nR, 0);

    /* ================================================================ H-U3 押下表示 */
    TEST("H-U3a 短いタップ: 押下確定で PRESS、離しは 30ms 後だが実行は押下から 90ms 後、実行の 60ms 後に FLASH 終了");
    reset(0u);
    {
        nav_sample_t s = pt(1u, 420, 150);
        uint32_t td;
        step(&s); step(&s); td = T;
        CHECK_EQ(U.fb_phase, UI_PH_PRESS);
        CHECK_EQ(U.fb_btn, UI_BTN_H_UP);
        step(&UP); step(&UP); step(&UP);                 /* td + 30 に離し確定 */
        CHECK_EQ(U.fb_phase, UI_PH_PRESS);
        while ((T - td) < 80u) { step(&UP); }
        CHECK_EQ(U.fb_phase, UI_PH_PRESS);               /* 80ms: まだ実行しない */
        CHECK_EQ(U.v.set, 42);
        step(&UP);                                       /* 90ms */
        CHECK_EQ(T, td + 90u);
        CHECK_EQ(U.fb_phase, UI_PH_FLASH);
        CHECK_EQ(U.v.set, 43);
        CHECK(nth_is(1u, "btn=h_up st=act src=touch"));
        CHECK_EQ(R[1].t, td + 90u);
        CHECK(nth_is(2u, "set=43 src=touch"));
        while ((T - td) < 140u) { step(&UP); }
        CHECK_EQ(U.fb_phase, UI_PH_FLASH);               /* 実行 +50ms: まだ白光 */
        step(&UP);                                       /* 実行 +60ms */
        CHECK_EQ(T, td + 150u);
        CHECK_EQ(U.fb_phase, UI_PH_NONE);
        CHECK_EQ(U.fb_btn, UI_BTN_NONE);
    }

    TEST("H-U3b 長押し(300ms): 離し確定(離してから30ms後)で実行。3秒押しっぱなしで強制実行");
    reset(0u);
    {
        uint32_t td = tap_xy(420, 150, 300u, 0u);        /* 押下確定から 300ms 押し、3標本で離し確定 */
        CHECK_EQ(R[1].t, td + 330u);
        reset(0u);
        {
            nav_sample_t s = pt(1u, 420, 150);
            step(&s); step(&s); td = T;
            while ((T - td) < 2990u) { step(&s); }
            CHECK_EQ(count_pfx("btn=h_up st=act"), 0);
            step(&s);
            CHECK_EQ(count_pfx("btn=h_up st=act"), 1);   /* 3.0秒で実行 */
            CHECK_EQ(R[1].t, td + 3000u);
            CHECK_EQ(U.v.set, 43);
            for (i = 0u; i < 50u; i++) { step(&s); }     /* 触り続けても二重に実行しない */
            CHECK_EQ(count_pfx("btn=h_up st=act"), 1);
            step(&UP); step(&UP); step(&UP); run(200u);
            CHECK_EQ(count_pfx("btn=h_up st=act"), 1);
            CHECK_EQ(U.v.set, 43);
        }
    }

    TEST("H-U3c 押下表示中(PRESS)の別の押下は無視。FLASH 中の押下は受け付ける");
    reset(0u);
    {
        nav_sample_t a = pt(1u, 420, 150);
        step(&a); step(&a);
        CHECK_EQ(U.fb_btn, UI_BTN_H_UP);
        cmd(UI_CMD_MINUS);                               /* UART の仮想タップも無視される */
        CHECK_EQ(U.fb_btn, UI_BTN_H_UP);
        CHECK_EQ(count_pfx("btn=h_dn"), 0);
        step(&UP); step(&UP); step(&UP); run(100u);      /* act → FLASH */
        CHECK_EQ(U.v.set, 43);
        {
            nav_sample_t b = pt(1u, 334, 155);           /* h_dn の上 */
            step(&b); step(&b);
            CHECK_EQ(U.fb_btn, UI_BTN_H_DN);
            CHECK_EQ(U.fb_phase, UI_PH_PRESS);
        }
    }

    /* ================================================================ H-U1 画面 x イベント */
    TEST("H-U1a ホーム: h_dn / h_up で設定温度 ±1、35 と 48 で止まる(止まった時は [ev] set を出さない)");
    reset(0u);
    run(100u);
    for (i = 0u; i < 9u; i++) { (void)tap(UI_BTN_H_DN, 200u); }
    CHECK_EQ(U.v.set, 35);
    CHECK_EQ(count_pfx("set="), 7);
    CHECK_EQ(count_pfx("btn=h_dn st=act"), 9);           /* 押した [ev] は止まっても出る */
    nR = 0u;
    for (i = 0u; i < 15u; i++) { (void)tap(UI_BTN_H_UP, 200u); }
    CHECK_EQ(U.v.set, 48);
    CHECK_EQ(count_pfx("set="), 13);
    CHECK(find("set=48 src=touch") >= 0);
    CHECK_EQ(U.v.scr, UI_SCR_HOME);

    TEST("H-U1b ホーム → 湯はり: 右から入るスライド、水位 0%、スライド 250ms で終わる");
    reset(0u);
    run(100u);
    {
        uint32_t td = tap(UI_BTN_H_FILL, 0u);
        CHECK(nth_is(0u, "btn=h_fill st=down src=touch"));
        CHECK(nth_is(1u, "btn=h_fill st=act src=touch"));
        CHECK(nth_is(2u, "scr=fill from=home src=touch"));
        CHECK_EQ(R[1].t, td + 90u);
        CHECK_EQ(U.v.scr, UI_SCR_FILL);
        CHECK_EQ(U.ov.scr, UI_SCR_HOME);
        CHECK_EQ(U.tr_on, 1);
        CHECK_EQ(U.tr_fwd, 1);
        run(300u);
        CHECK_EQ(U.tr_on, 0);
        CHECK_EQ(U.tr_dx, 480);
    }

    TEST("H-U1c 湯はり: 水位が 100% で done(背景切替・[ev] done=fill)、f_stop / f_done / f_home の動作と bath");
    reset(0u);
    run(100u);
    U.v.bath_x10 = 415u;                                 /* ホームの浴槽温度を 41.5 にしておく */
    goto_scr(UI_BTN_H_FILL, 400u);
    CHECK_EQ(U.v.scr, UI_SCR_FILL);
    run(10000u);
    CHECK(U.v.fill_x10 > 300u && U.v.fill_x10 < 340u);   /* 約 10.4 秒: 32 秒で 100% */
    {
        int32_t x, y;
        nR = 0u;
        center(UI_BTN_F_STOP, &x, &y);                    /* 水位 100% 未満: f_stop */
        (void)tap_xy(x, y, 0u, 400u);
        CHECK(nth_is(0u, "btn=f_stop st=down src=touch"));
        CHECK(nth_is(2u, "scr=home from=fill src=touch"));
        CHECK_EQ(U.v.scr, UI_SCR_HOME);
        CHECK_EQ(U.v.bath_x10, 415);                     /* 水位 < 100% の停止では bath はそのまま */
    }
    goto_scr(UI_BTN_H_FILL, 400u);
    nR = 0u;
    run(32000u);
    CHECK_EQ(U.v.fill_x10, 1000);
    CHECK_EQ(count_pfx("done=fill"), 1);
    {
        int32_t x, y;
        center(UI_BTN_F_DONE, &x, &y);                    /* 100%: 同じ場所が f_done */
        nR = 0u;
        (void)tap_xy(x, y, 0u, 400u);
        CHECK(nth_is(0u, "btn=f_done st=down src=touch"));
        CHECK(nth_is(2u, "scr=home from=fill src=touch"));
        CHECK_EQ(U.v.bath_x10, 400);                     /* 湯はり完了で 40.0 */
    }
    U.v.bath_x10 = 415u;
    goto_scr(UI_BTN_H_FILL, 400u);
    run(33000u);
    nR = 0u;
    (void)tap(UI_BTN_F_HOME, 400u);                      /* 100% の f_home も bath = 400 */
    CHECK(nth_is(0u, "btn=f_home st=down src=touch"));
    CHECK_EQ(U.v.scr, UI_SCR_HOME);
    CHECK_EQ(U.v.bath_x10, 400);
    U.v.bath_x10 = 415u;
    goto_scr(UI_BTN_H_FILL, 400u);
    run(3000u);
    (void)tap(UI_BTN_F_HOME, 400u);                      /* 途中の f_home は bath そのまま */
    CHECK_EQ(U.v.scr, UI_SCR_HOME);
    CHECK_EQ(U.v.bath_x10, 415);
    goto_scr(UI_BTN_H_FILL, 400u);
    CHECK_EQ(U.v.fill_x10 < 100u, 1);                    /* 入り直すと 0 から */

    TEST("H-U1d 追い焚き: 380 から。bath >= set x 10 で done、r_stop / r_done / r_home でホームへ(bath はそのまま)");
    reset(0u);
    run(100u);
    (void)tap(UI_BTN_H_REHEAT, 400u);
    CHECK_EQ(U.v.scr, UI_SCR_REHEAT);
    CHECK(U.v.bath_x10 >= 380u && U.v.bath_x10 < 392u);
    nR = 0u;
    run(14000u);
    CHECK_EQ(count_pfx("done=reheat"), 0);
    CHECK(U.v.bath_x10 > 410u && U.v.bath_x10 < 420u);
    {
        int32_t x, y;
        uint16_t b;
        center(UI_BTN_R_STOP, &x, &y);
        (void)tap_xy(x, y, 0u, 400u);
        CHECK(nth_is(0u, "btn=r_stop st=down src=touch"));
        CHECK(nth_is(2u, "scr=home from=reheat src=touch"));
        b = U.v.bath_x10;
        CHECK(b > 410u && b <= 420u);                     /* そのまま(40.0 に戻さない) */
        (void)tap(UI_BTN_H_REHEAT, 400u);
        CHECK_EQ(U.v.bath_x10 >= 380u && U.v.bath_x10 < 392u, 1);   /* 入り直すと 380 から */
    }
    run(17000u);
    CHECK_EQ(U.v.bath_x10, 420);
    CHECK_EQ(count_pfx("done=reheat"), 1);
    {
        int32_t x, y;
        center(UI_BTN_R_DONE, &x, &y);
        nR = 0u;
        (void)tap_xy(x, y, 0u, 400u);
        CHECK(nth_is(0u, "btn=r_done st=down src=touch"));
        CHECK_EQ(U.v.scr, UI_SCR_HOME);
        CHECK_EQ(U.v.bath_x10, 420);
    }
    (void)tap(UI_BTN_H_REHEAT, 400u);
    nR = 0u;
    (void)tap(UI_BTN_R_HOME, 400u);
    CHECK(nth_is(0u, "btn=r_home st=down src=touch"));
    CHECK_EQ(U.v.scr, UI_SCR_HOME);

    TEST("H-U1e 設定温度が低い時(35): 追い焚きは開始と同時に done");
    reset(0u);
    run(100u);
    U.v.set = 35u;
    (void)tap(UI_BTN_H_REHEAT, 400u);
    CHECK_EQ(U.v.bath_x10, 350);
    CHECK_EQ(count_pfx("done=reheat"), 1);

    TEST("H-U1f 使用量: 棒が 0 から伸びる、流量が変わる、u_back でホームへ");
    reset(0u);
    run(100u);
    (void)tap(UI_BTN_H_USAGE, 0u);
    CHECK_EQ(U.v.scr, UI_SCR_USAGE);
    CHECK_EQ(U.v.bar_ms < 60u, 1);
    run(2000u);
    CHECK_EQ(U.v.bar_ms, UI_BAR_END_MS);
    CHECK(U.v.flow_x10 >= 67u && U.v.flow_x10 <= 103u);
    nR = 0u;
    (void)tap(UI_BTN_U_BACK, 400u);
    CHECK(nth_is(2u, "scr=home from=usage src=touch"));
    CHECK_EQ(U.tr_fwd, 0);

    TEST("H-U1g 電源 → 待機(即切替)。待機のどこを押しても押下確定で復帰し、離すまで他のボタンは受け付けない");
    reset(0u);
    run(100u);
    nR = 0u;
    (void)tap(UI_BTN_H_POWER, 0u);
    CHECK_EQ(U.v.scr, UI_SCR_STANDBY);
    CHECK_EQ(U.tr_on, 0);                                 /* スライドなし */
    CHECK(find("scr=standby from=home src=touch") >= 0);
    run(300u);
    {
        nav_sample_t s = pt(1u, 70, 220);                 /* ホームなら h_fill の位置 */
        nR = 0u;
        step(&s);
        CHECK_EQ(U.v.scr, UI_SCR_STANDBY);
        step(&s);                                          /* 押下確定の時点で復帰 */
        CHECK_EQ(U.v.scr, UI_SCR_HOME);
        CHECK_EQ(U.tr_on, 0);
        CHECK(nth_is(0u, "btn=s_wake st=down src=touch"));
        CHECK(nth_is(1u, "scr=home from=standby src=touch"));
        for (i = 0u; i < 60u; i++) { step(&s); }           /* 触り続ける: 下の h_fill は押されない */
        CHECK_EQ(nR, 2);
        CHECK_EQ(U.fb_phase, UI_PH_NONE);
        step(&UP); step(&UP); step(&UP); run(300u);
        CHECK_EQ(nR, 2);
        CHECK_EQ(U.v.scr, UI_SCR_HOME);
        (void)tap(UI_BTN_H_UP, 300u);                      /* 離した後は通常どおり */
        CHECK_EQ(U.v.set, 43);
    }

    TEST("H-U1h 全ボタン: 15個のボタンの中心を押すと、その画面でそのボタンの [ev] が出る(当たり判定の表との整合)");
    {
        uint8_t b;
        int ok = 1;
        for (b = 1u; b < UI_BTN_COUNT; b++) {
            const ui_btn_def_t *d = &ui_btn_defs[b];
            int32_t x, y;
            char want[48];
            reset(0u);
            run(100u);
            U.v.scr = d->scr;                              /* 画面を直接作る(条件つきボタンは値も合わせる) */
            U.v.set = 42u;
            U.enter_ms = ((d->cond == UI_COND_FILL_DONE) || (d->cond == UI_COND_REHEAT_DONE)) ? (T - 100000u) : T;   /* 経過時間から値が決まる */
            U.fill_sent = 1u; U.reheat_sent = 1u;
            /* 四隅の1画素内側と中心で押せる */
            {
                int32_t xs[3], ys[3];
                int k;
                xs[0] = d->hit.x; xs[1] = d->hit.x + d->hit.w - 1; xs[2] = d->hit.x + d->hit.w / 2;
                ys[0] = d->hit.y; ys[1] = d->hit.y + d->hit.h - 1; ys[2] = d->hit.y + d->hit.h / 2;
                for (k = 0; k < 3; k++) {
                    ui_update(&U, &UP, T += 10u);
                    snprintf(want, sizeof(want), "btn=%s st=down src=touch", d->name);
                    nR = 0u;
                    x = xs[k]; y = ys[k];
                    {
                        nav_sample_t s = pt(1u, x, y);
                        ui_update(&U, &s, T += 10u); drain(); ui_update(&U, &s, T += 10u); drain();
                    }
                    if (!nth_is(0u, want)) { ok = 0; printf("  %s (%d,%d) で押下にならない\n", d->name, (int)x, (int)y); }
                    ui_update(&U, &UP, T += 10u); ui_update(&U, &UP, T += 10u); ui_update(&U, &UP, T += 10u);
                    drain();
                    U.v.scr = d->scr; U.fb_phase = UI_PH_NONE; U.tr_on = 0u; U.ts = 0u; U.tcnt = 0u;   /* 次の位置の試験のため戻す */
                    if (d->scr == UI_SCR_STANDBY) { U.v.scr = UI_SCR_STANDBY; }
                }
            }
        }
        CHECK(ok);
        /* 当たり判定の外(上下左右 1画素)では押せない */
        reset(0u);
        run(100u);
        {
            const ui_btn_def_t *d = &ui_btn_defs[UI_BTN_H_FILL];
            nR = 0u;
            (void)tap_xy(d->hit.x - 1, d->hit.y + 10, 0u, 300u);
            (void)tap_xy(d->hit.x + d->hit.w, d->hit.y + 10, 0u, 300u);
            (void)tap_xy(d->hit.x + 10, d->hit.y - 1, 0u, 300u);
            (void)tap_xy(d->hit.x + 10, d->hit.y + d->hit.h, 0u, 300u);
            CHECK_EQ(nR, 0);
        }
    }

    TEST("H-U1i 当たり判定は押下確定の座標で1回だけ(指が滑って外れても実行は取り消さない)");
    reset(0u);
    run(100u);
    {
        nav_sample_t a = pt(1u, 420, 150), b = pt(1u, 100, 100);
        step(&a); step(&a); step(&b); step(&b); step(&b); step(&UP); step(&UP); step(&UP); run(200u);
        CHECK_EQ(U.v.set, 43);
    }

    /* ================================================================ H-U5 遷移中の扱い */
    TEST("H-U5b 遷移中(250ms)の押下は受け付けない。古い画面の値の写しは遷移中に変わらない");
    reset(0u);
    run(100u);
    goto_scr(UI_BTN_H_FILL, 400u);
    run(10000u);                                           /* 水位を進める */
    U.v.bath_x10 = 407u;
    {
        uint16_t fill_at_leave;
        int32_t x, y;
        center(UI_BTN_F_HOME, &x, &y);
        {
            nav_sample_t s = pt(1u, x, y);
            step(&s); step(&s); step(&UP); step(&UP); step(&UP); step(&UP); step(&UP); step(&UP); step(&UP); step(&UP);
            step(&UP);                                      /* 90ms 後に実行 → スライド開始 */
        }
        CHECK_EQ(U.tr_on, 1);
        CHECK_EQ(U.ov.scr, UI_SCR_FILL);
        fill_at_leave = U.ov.fill_x10;
        CHECK(fill_at_leave > 300u);
        nR = 0u;
        {
            nav_sample_t s = pt(1u, 420, 150);              /* ホームなら h_up の位置: 遷移中なので無視 */
            step(&s); step(&s); step(&s); step(&s);
            CHECK_EQ(nR, 0);
            CHECK_EQ(U.ov.fill_x10, fill_at_leave);          /* 古い画面は止めたまま */
            for (i = 0u; i < 40u; i++) { step(&s); }         /* 触り続ける: 遷移が終わっても動作にならない */
            CHECK_EQ(U.tr_on, 0);
            CHECK_EQ(count_pfx("btn="), 0);
            CHECK_EQ(U.v.set, 42);
            step(&UP); step(&UP); step(&UP); run(200u);
            CHECK_EQ(count_pfx("btn="), 0);
        }
        CHECK_EQ(U.v.scr, UI_SCR_HOME);
        cmd(UI_CMD_NEXT);                                    /* n は遷移後なら受け付ける */
        CHECK_EQ(count_pfx("scr=fill from=home src=uart"), 1);
        cmd(UI_CMD_NEXT); cmd(UI_CMD_PREV);                  /* 遷移中の n p は無視 */
        CHECK_EQ(count_pfx("scr="), 1);                      /* n による fill の1本だけ */
    }

    TEST("H-U5c 遷移の向き: ホーム→子は前進、子→ホームは後退。待機への出入りは即切替");
    reset(0u);
    run(100u);
    cmd(UI_CMD_NEXT);       CHECK_EQ(U.v.scr, UI_SCR_FILL);    CHECK_EQ(U.tr_fwd, 1); CHECK_EQ(U.tr_on, 1); run(300u);
    cmd(UI_CMD_NEXT);       CHECK_EQ(U.v.scr, UI_SCR_REHEAT);  CHECK_EQ(U.tr_fwd, 1); run(300u);
    cmd(UI_CMD_PREV);       CHECK_EQ(U.v.scr, UI_SCR_FILL);    CHECK_EQ(U.tr_fwd, 0); run(300u);
    cmd(UI_CMD_PREV);       CHECK_EQ(U.v.scr, UI_SCR_HOME);    CHECK_EQ(U.tr_fwd, 0); run(300u);
    cmd(UI_CMD_PREV);       CHECK_EQ(U.v.scr, UI_SCR_STANDBY); CHECK_EQ(U.tr_on, 0);          /* 即切替 */
    cmd(UI_CMD_PREV);       CHECK_EQ(U.v.scr, UI_SCR_USAGE);   CHECK_EQ(U.tr_on, 0);          /* 待機から出るのも即切替 */
    run(300u);

    /* ================================================================ H-U7 UART コマンド */
    TEST("H-U7a n: home→fill→reheat→usage→standby→home、p: 逆。値の初期化はボタンで入った時と同じ");
    reset(0u);
    run(100u);
    nR = 0u;
    for (i = 0u; i < 5u; i++) { cmd(UI_CMD_NEXT); run(300u); }
    CHECK(nth_is(0u, "scr=fill from=home src=uart"));
    CHECK(nth_is(1u, "scr=reheat from=fill src=uart"));
    CHECK(nth_is(2u, "scr=usage from=reheat src=uart"));
    CHECK(nth_is(3u, "scr=standby from=usage src=uart"));
    CHECK(nth_is(4u, "scr=home from=standby src=uart"));
    CHECK_EQ(nR, 5);
    nR = 0u;
    for (i = 0u; i < 5u; i++) { cmd(UI_CMD_PREV); run(300u); }
    CHECK(nth_is(0u, "scr=standby from=home src=uart"));
    CHECK(nth_is(1u, "scr=usage from=standby src=uart"));
    CHECK(nth_is(2u, "scr=reheat from=usage src=uart"));
    CHECK(nth_is(3u, "scr=fill from=reheat src=uart"));
    CHECK(nth_is(4u, "scr=home from=fill src=uart"));
    cmd(UI_CMD_NEXT);
    CHECK_EQ(U.v.fill_x10, 0);                              /* fill に入ると 0% */
    run(300u);
    cmd(UI_CMD_NEXT);
    CHECK_EQ(U.v.bath_x10, 380);                            /* reheat に入ると 380 */
    CHECK_EQ(count_pfx("btn="), 0);                         /* n p は仮想押下を作らない */

    TEST("H-U7b + / -: h_up / h_dn の仮想タップ(押下表示 → 実行 → set)。ホーム以外・遷移中・押下表示中は無視");
    reset(0u);
    run(100u);
    nR = 0u;
    cmd(UI_CMD_PLUS);
    CHECK_EQ(U.fb_phase, UI_PH_PRESS);
    run(400u);
    CHECK(nth_is(0u, "btn=h_up st=down src=uart"));
    CHECK(nth_is(1u, "btn=h_up st=act src=uart"));
    CHECK(nth_is(2u, "set=43 src=uart"));
    CHECK_EQ(R[1].t - R[0].t, 150);                         /* 150ms 押す */
    cmd(UI_CMD_MINUS); run(400u);
    CHECK(nth_is(5u, "set=42 src=uart"));
    CHECK_EQ(nR, 6);
    cmd(UI_CMD_NEXT); nR = 0u;
    cmd(UI_CMD_PLUS); cmd(UI_CMD_MINUS);                    /* 遷移中 */
    run(300u);
    cmd(UI_CMD_PLUS);                                        /* ホーム以外 */
    CHECK_EQ(nR, 0);
    cmd(UI_CMD_PLUS); run(300u);
    CHECK_EQ(U.v.set, 42);
    reset(0u);
    run(100u);
    for (i = 0u; i < 9u; i++) { cmd(UI_CMD_MINUS); run(200u); }
    CHECK_EQ(U.v.set, 35);
    CHECK_EQ(count_pfx("set="), 7);

    TEST("H-U7c 連続して + を送っても押下中は無視される(UT17 相当: 6 以下の set、48 を超えない)");
    reset(0u);
    run(100u);
    nR = 0u;
    for (i = 0u; i < 400u; i++) { cmd(UI_CMD_PLUS); step(&UP); }   /* 10ms ごとに 400 個 */
    CHECK(U.v.set <= 48u);
    CHECK(count_pfx("set=") >= 1u);
    CHECK_EQ(count_pfx("btn=h_up st=down"), count_pfx("btn=h_up st=act") + (U.fb_phase == UI_PH_PRESS ? 1u : 0u));

    /* ================================================================ H-U6 デモ */
    TEST("H-U6a デモ全走(UART d): 36 本の [ev] が §6.2 の時刻(±10ms)・順序で出る。終了後ホームで止まり speed=1");
    {
        static const struct { uint32_t t; const char *s; } W[] = {
            {    0, "demo=start src=uart" },
            {  800, "btn=h_up st=down src=demo" },    {  950, "btn=h_up st=act src=demo" },    {  950, "set=43 src=demo" },
            { 1500, "btn=h_up st=down src=demo" },    { 1650, "btn=h_up st=act src=demo" },    { 1650, "set=44 src=demo" },
            { 2200, "btn=h_dn st=down src=demo" },    { 2350, "btn=h_dn st=act src=demo" },    { 2350, "set=43 src=demo" },
            { 3200, "btn=h_fill st=down src=demo" },  { 3350, "btn=h_fill st=act src=demo" },  { 3350, "scr=fill from=home src=demo" },
            { 11350, "done=fill" },
            { 12800, "btn=f_done st=down src=demo" }, { 12950, "btn=f_done st=act src=demo" }, { 12950, "scr=home from=fill src=demo" },
            { 14200, "btn=h_reheat st=down src=demo" }, { 14350, "btn=h_reheat st=act src=demo" }, { 14350, "scr=reheat from=home src=demo" },
            { 19350, "done=reheat" },
            { 20800, "btn=r_done st=down src=demo" }, { 20950, "btn=r_done st=act src=demo" }, { 20950, "scr=home from=reheat src=demo" },
            { 22400, "btn=h_usage st=down src=demo" }, { 22550, "btn=h_usage st=act src=demo" }, { 22550, "scr=usage from=home src=demo" },
            { 27000, "btn=u_back st=down src=demo" }, { 27150, "btn=u_back st=act src=demo" }, { 27150, "scr=home from=usage src=demo" },
            { 28600, "btn=h_power st=down src=demo" }, { 28750, "btn=h_power st=act src=demo" }, { 28750, "scr=standby from=home src=demo" },
            { 31750, "btn=s_wake st=down src=demo" }, { 31750, "scr=home from=standby src=demo" },
            { 33000, "demo=end" }
        };
        unsigned n = (unsigned)(sizeof(W) / sizeof(W[0])), bad = 0u;
        uint32_t d0;
        unsigned frames_changed = 0u;
        reset(0u);
        run(100u);
        U.v.set = 45u; U.v.bath_x10 = 391u;                  /* デモは set=42、bath=400 から始める */
        ui_view_mark(&U);
        d0 = T;
        nR = 0u;
        cmd(UI_CMD_DEMO);
        CHECK_EQ(U.demo, 1);
        CHECK_EQ(U.speed, 4);
        CHECK_EQ(U.v.set, 42);
        CHECK_EQ(U.v.bath_x10, 400);
        while ((T - d0) < 34000u) {
            step(&UP);
            if (ui_view_changed(&U)) { frames_changed++; ui_view_mark(&U); }
        }
        CHECK_EQ(nR, 36);
        CHECK_EQ(n, 36);                                      /* 期待表の行数(s_wake は down + scr の 2 行) */
        for (i = 0u; (i < n) && (i < nR); i++) {
            uint32_t d = (R[i].t > d0 + W[i].t) ? (R[i].t - d0 - W[i].t) : (d0 + W[i].t - R[i].t);
            if ((strcmp(R[i].s, W[i].s) != 0) || (d > 10u)) { bad++; printf("  #%u: 実際 %u %s / 期待 %u %s\n", i, (unsigned)(R[i].t - d0), R[i].s, (unsigned)W[i].t, W[i].s); }
        }
        CHECK_EQ(bad, 0);
        CHECK_EQ(U.demo, 0);
        CHECK_EQ(U.speed, 1);
        CHECK_EQ(U.v.scr, UI_SCR_HOME);
        CHECK_EQ(U.v.set, 43);
        CHECK(frames_changed > 200u && frames_changed < 3400u);
        printf("  (参考) 表示キーが変わった回数(描画の要求回数) = %u / 33秒\n", frames_changed);
        CHECK_EQ(count_pfx("btn="), 21);
        CHECK_EQ(count_pfx("set="), 3);
        CHECK_EQ(count_pfx("scr="), 8);
        CHECK_EQ(count_pfx("done="), 2);
        CHECK_EQ(count_pfx("demo="), 2);
        CHECK_EQ(U.ev_lost, 0);
    }

    TEST("H-U6b デモ(ホームの「デモ」ボタン): btn=h_demo の後に demo=start src=touch。同じ経路");
    reset(0u);
    run(100u);
    U.v.set = 40u;
    nR = 0u;
    (void)tap(UI_BTN_H_DEMO, 0u);
    CHECK(nth_is(0u, "btn=h_demo st=down src=touch"));
    CHECK(nth_is(1u, "btn=h_demo st=act src=touch"));
    CHECK(nth_is(2u, "demo=start src=touch"));
    CHECK_EQ(U.demo, 1);
    CHECK_EQ(U.v.set, 42);
    run(1000u);
    CHECK(find("btn=h_up st=down src=demo") >= 0);

    TEST("H-U6c 実タッチで中断: 即ホーム + demo 終了 + speed=1。そのタッチは動作にならない。離した後は通常操作");
    reset(0u);
    run(100u);
    cmd(UI_CMD_DEMO);
    run(8000u);                                              /* 湯はりの途中(3350 から 4倍速) */
    CHECK_EQ(U.v.scr, UI_SCR_FILL);
    nR = 0u;
    {
        nav_sample_t s = pt(1u, 100, 100);
        step(&s); step(&s);
        CHECK(nth_is(0u, "demo=abort src=touch"));
        CHECK(nth_is(1u, "scr=home from=fill src=touch"));
        CHECK_EQ(nR, 2);
        CHECK_EQ(U.v.scr, UI_SCR_HOME);
        CHECK_EQ(U.tr_on, 0);                                 /* アニメーションなし */
        CHECK_EQ(U.demo, 0);
        CHECK_EQ(U.speed, 1);
        CHECK_EQ(U.fb_phase, UI_PH_NONE);
        for (i = 0u; i < 30u; i++) { step(&s); }
        step(&UP); step(&UP); step(&UP);
        run(2000u);
        CHECK_EQ(count_pfx("btn="), 0);                       /* そのタッチは btn にならない・デモも再開しない */
        CHECK_EQ(nR, 2);
        (void)tap(UI_BTN_H_UP, 300u);
        CHECK_EQ(count_pfx("btn=h_up st=act src=touch"), 1);
    }
    /* ボタンの上を触って中断する場合も、そのボタンは動作にしない */
    reset(0u);
    run(100u);
    cmd(UI_CMD_DEMO);
    run(500u);
    nR = 0u;
    (void)tap(UI_BTN_H_FILL, 500u);
    CHECK(nth_is(0u, "demo=abort src=touch"));
    CHECK_EQ(nR, 1);
    CHECK_EQ(U.v.scr, UI_SCR_HOME);

    TEST("H-U6d UART d で開始 / 実行中に d で中断(即ホーム、demo=abort src=uart + scr=home ...)。n p + - は demo 中は無視");
    reset(0u);
    run(100u);
    nR = 0u;
    cmd(UI_CMD_DEMO);
    run(5600u);
    CHECK_EQ(U.v.scr, UI_SCR_FILL);
    cmd(UI_CMD_NEXT); cmd(UI_CMD_PREV); cmd(UI_CMD_PLUS); cmd(UI_CMD_MINUS);
    CHECK_EQ(U.v.scr, UI_SCR_FILL);
    nR = 0u;
    cmd(UI_CMD_DEMO);
    CHECK(nth_is(0u, "demo=abort src=uart"));
    CHECK(nth_is(1u, "scr=home from=fill src=uart"));
    CHECK_EQ(U.tr_on, 0);
    CHECK_EQ(U.demo, 0);
    CHECK_EQ(U.speed, 1);
    run(3000u);
    CHECK_EQ(nR, 2);
    reset(0u);                                                /* 別画面からの開始: 即ホームに戻してから台本 */
    run(100u);
    cmd(UI_CMD_NEXT); run(300u);
    nR = 0u;
    cmd(UI_CMD_DEMO);
    CHECK(nth_is(0u, "demo=start src=uart"));
    CHECK(nth_is(1u, "scr=home from=fill src=uart"));
    CHECK_EQ(U.tr_on, 0);
    CHECK_EQ(U.v.scr, UI_SCR_HOME);

    TEST("H-U6e 台本どおりに押せない時(想定外)は demo=abort src=demo");
    reset(0u);
    run(100u);
    nR = 0u;
    cmd(UI_CMD_DEMO);
    run(12700u);                                              /* 湯はり完了表示のはず */
    CHECK_EQ(count_pfx("done=fill"), 1);
    U.enter_ms = T;                                           /* 湯はりを入り直した扱い(水位 0%)→ f_done の位置は f_stop になる */
    run(300u);
    CHECK(find("demo=abort src=demo") >= 0);
    CHECK_EQ(U.demo, 0);
    CHECK_EQ(U.v.scr, UI_SCR_HOME);
    CHECK_EQ(U.speed, 1);

    TEST("H-U6f デモ中は、デモ用のタグ表示キーが立つ(demo=1)。終了で消える");
    reset(0u);
    run(100u);
    ui_view_mark(&U);
    cmd(UI_CMD_DEMO); step(&UP);
    CHECK(ui_view_changed(&U));
    ui_view_mark(&U);
    run(34000u);
    CHECK_EQ(U.demo, 0);

    /* ================================================================ 表示キー・イベント待ち行列 */
    TEST("表示キー: 初回は変化あり。マーク後は、何も変わらなければ変化なし。値・押下表示・遷移で変化");
    reset(0u);
    CHECK(ui_view_changed(&U));
    ui_view_mark(&U);
    CHECK(!ui_view_changed(&U));
    run(1000u);
    CHECK(!ui_view_changed(&U));                              /* 時計の分が変わらない間は静止 */
    run(60000u);
    CHECK(ui_view_changed(&U));                               /* 時計が 1分進んだ */
    ui_view_mark(&U);
    (void)tap(UI_BTN_H_UP, 300u);
    CHECK(ui_view_changed(&U));
    ui_view_mark(&U);
    run(1000u);
    CHECK(!ui_view_changed(&U));
    cmd(UI_CMD_NEXT); step(&UP);
    CHECK(ui_view_changed(&U));
    ui_view_mark(&U);
    step(&UP);
    CHECK(ui_view_changed(&U));                               /* スライド中は毎周期変わる */
    run(400u);
    ui_view_mark(&U);
    run(300u);
    CHECK(ui_view_changed(&U));                               /* 水位が進む */

    TEST("イベント待ち行列: 8個まで。満杯なら新しいものを捨てて ev_lost に数える。pop は古い順");
    reset(0u);
    run(100u);
    {
        ui_ev_t e;
        nav_sample_t s = pt(1u, 420, 150);
        unsigned k, j;
        for (k = 0u; k < 4u; k++) {                           /* h_up を4回: 各 3 イベント(down, act, set) = 12 個、pop しない */
            T += 10u; ui_update(&U, &s, T);
            T += 10u; ui_update(&U, &s, T);
            for (j = 0u; j < 23u; j++) { T += 10u; ui_update(&U, &UP, T); }
        }
        CHECK_EQ(U.ev_n, 8);
        CHECK(U.ev_lost >= 4u);
        CHECK(ui_event_pop(&U, &e));
        CHECK_EQ(e.type, UI_EV_BTN_DOWN);                     /* 最初のイベントが残っている(古い方を残す) */
        CHECK_EQ(e.a, UI_BTN_H_UP);
        for (k = 0u; k < 7u; k++) { CHECK(ui_event_pop(&U, &e)); }
        CHECK(!ui_event_pop(&U, &e));
        CHECK(!ui_event_pop(&U, NULL));
    }
    (void)goto_scr;

    TEST_REPORT("test_ui_logic");
}
