/* test_ui_extra.c - テスト担当が追加した試験(2026-11-21)。既存の test_ui_logic / test_gfx / test_ui_render が
 * 持たない観点だけ: 当たり判定の幾何、UT10〜UT17 に相当する UART 操作の [ev] 列、更新間隔のばらつき・32bit 折り返し、
 * デモの再開、状態不変条件のファズ、gfx / ui_render の極端な引数と書込み範囲、タッチ座標の変換の端。
 * 製品コードは変更しない。標準C + 生成データ(ui_layout.c ほか)。実機・実時間・LCD は試験できない。
 *
 * D1〜D3(デモ中断・開始まわりの不具合。docs/test-report.md §10)は 2026-11-21 に ui_logic.c で修正済み。
 * 以前 KNOWN-DEFECT だった検査は、通常の CHECK(失敗すれば不合格。STRICT_XFAIL は不要)になっている。 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_logic.h"
#include "ui_render.h"
#include "gfx.h"
#include "test_framework.h"

/* ---------------------------------------------------------------- 共通の小道具(test_ui_logic.c と同じ作り) */
static ui_t     U;
static uint32_t T;
typedef struct { uint32_t t; char s[48]; } rec_t;
static rec_t    R[4096];
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
        if (nR < 4096u) { R[nR].t = T; ev_str(&e, R[nR].s, sizeof(R[nR].s)); nR++; }
    }
}
static void step(const nav_sample_t *s) { T += 10u; ui_update(&U, s, T); drain(); }
static void run(uint32_t ms) { uint32_t i; for (i = 0u; i < ms / 10u; i++) { step(&UP); } }
static void reset_at(uint32_t t0)
{
    T = t0; nR = 0u;
    ui_init(&U, T, 479u, 271u, 0u);
}
static void reset(void) { reset_at(1000u); }
static void cmd(ui_cmd_t c) { ui_cmd(&U, c, T); drain(); }
static nav_sample_t pt(uint8_t n, int32_t x, int32_t y)
{
    nav_sample_t s;
    s.pressed = true; s.points = n; s.x = (uint16_t)x; s.y = (uint16_t)y;
    return s;
}
static unsigned count_pfx(const char *p) { unsigned i, n = 0u; for (i = 0u; i < nR; i++) { if (strncmp(R[i].s, p, strlen(p)) == 0) { n++; } } return n; }
static int find(const char *s) { unsigned i; for (i = 0u; i < nR; i++) { if (strcmp(R[i].s, s) == 0) { return (int)i; } } return -1; }
static int nth_is(unsigned i, const char *s) { return (i < nR) && (strcmp(R[i].s, s) == 0); }

/* 台本どおりの [ev] 列(design.md §6.2 / design-uart.md §10.8 UT13。start は src=uart) */
static const char *const DEMO_EV[36] = {
    "demo=start src=uart",
    "btn=h_up st=down src=demo", "btn=h_up st=act src=demo", "set=43 src=demo",
    "btn=h_up st=down src=demo", "btn=h_up st=act src=demo", "set=44 src=demo",
    "btn=h_dn st=down src=demo", "btn=h_dn st=act src=demo", "set=43 src=demo",
    "btn=h_fill st=down src=demo", "btn=h_fill st=act src=demo", "scr=fill from=home src=demo",
    "done=fill",
    "btn=f_done st=down src=demo", "btn=f_done st=act src=demo", "scr=home from=fill src=demo",
    "btn=h_reheat st=down src=demo", "btn=h_reheat st=act src=demo", "scr=reheat from=home src=demo",
    "done=reheat",
    "btn=r_done st=down src=demo", "btn=r_done st=act src=demo", "scr=home from=reheat src=demo",
    "btn=h_usage st=down src=demo", "btn=h_usage st=act src=demo", "scr=usage from=home src=demo",
    "btn=u_back st=down src=demo", "btn=u_back st=act src=demo", "scr=home from=usage src=demo",
    "btn=h_power st=down src=demo", "btn=h_power st=act src=demo", "scr=standby from=home src=demo",
    "btn=s_wake st=down src=demo", "scr=home from=standby src=demo",
    "demo=end"
};
static unsigned demo_mismatch(void)   /* R[0..35] が DEMO_EV と一致しない数(件数違いも数える) */
{
    unsigned i, bad = (nR != 36u) ? 1u : 0u;
    for (i = 0u; (i < 36u) && (i < nR); i++) { if (strcmp(R[i].s, DEMO_EV[i]) != 0) { bad++; printf("  #%u: 実際 '%s' / 期待 '%s'\n", i, R[i].s, DEMO_EV[i]); } }
    return bad;
}

static uint32_t rng_s = 12345u;
static uint32_t rnd(void) { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 17; rng_s ^= rng_s << 5; return rng_s; }

int main(void)
{
    unsigned i, j;

    /* ================================================================ X1 当たり判定の幾何(design §4.1) */
    TEST("X1 当たり判定: 画面内・見た目を含む・見た目 ±3(デモとs_wake以外)・同じ画面で同時に成り立つ2つが重ならない");
    for (i = 1u; i < (unsigned)UI_BTN_COUNT; i++) {
        const ui_btn_def_t *d = &ui_btn_defs[i];
        CHECK(d->hit.x >= 0 && d->hit.y >= 0 && (d->hit.x + d->hit.w) <= 480 && (d->hit.y + d->hit.h) <= 272);
        CHECK(d->hit.x <= d->vis.x && d->hit.y <= d->vis.y &&
              (d->hit.x + d->hit.w) >= (d->vis.x + d->vis.w) && (d->hit.y + d->hit.h) >= (d->vis.y + d->vis.h));
        if ((i != (unsigned)UI_BTN_H_DEMO) && (i != (unsigned)UI_BTN_S_WAKE) && (i != (unsigned)UI_BTN_H_WX)) {
            int x0 = d->vis.x - 3, y0 = d->vis.y - 3, x1 = d->vis.x + d->vis.w + 3, y1 = d->vis.y + d->vis.h + 3;
            if (x0 < 0) { x0 = 0; }
            if (y0 < 0) { y0 = 0; }
            if (x1 > 480) { x1 = 480; }
            if (y1 > 272) { y1 = 272; }
            CHECK_EQ(d->hit.x, x0); CHECK_EQ(d->hit.y, y0);
            CHECK_EQ(d->hit.x + d->hit.w, x1); CHECK_EQ(d->hit.y + d->hit.h, y1);
        }
    }
    CHECK_EQ(ui_btn_defs[UI_BTN_H_DEMO].hit.x, 216); CHECK_EQ(ui_btn_defs[UI_BTN_H_DEMO].hit.w, 108);   /* デモだけ大きい */
    CHECK_EQ(ui_btn_defs[UI_BTN_H_DEMO].hit.h, 44);
    /* 更新ボタン(design §22.8): 見た目 (248,158,28,28)、当たり判定 (228,136,60,52)。ボタン番号 16、ほかのボタンと重ならない(下の総当たり) */
    CHECK_EQ(UI_BTN_H_WX, 16); CHECK_EQ(UI_BTN_COUNT, 17);
    CHECK_EQ(ui_btn_defs[UI_BTN_H_WX].vis.x, 248); CHECK_EQ(ui_btn_defs[UI_BTN_H_WX].vis.y, 158);
    CHECK_EQ(ui_btn_defs[UI_BTN_H_WX].vis.w, 28);  CHECK_EQ(ui_btn_defs[UI_BTN_H_WX].vis.h, 28);
    CHECK_EQ(ui_btn_defs[UI_BTN_H_WX].hit.x, 228); CHECK_EQ(ui_btn_defs[UI_BTN_H_WX].hit.y, 136);
    CHECK_EQ(ui_btn_defs[UI_BTN_H_WX].hit.w, 60);  CHECK_EQ(ui_btn_defs[UI_BTN_H_WX].hit.h, 52);
    CHECK_EQ(ui_btn_defs[UI_BTN_H_WX].scr, UI_SCR_HOME); CHECK_EQ(ui_btn_defs[UI_BTN_H_WX].cond, UI_COND_ALWAYS);
    CHECK(strcmp(ui_btn_defs[UI_BTN_H_WX].name, "h_wx") == 0);
    CHECK(ui_btn_defs[UI_BTN_H_WX].shape >= 0);
    for (i = 1u; i < (unsigned)UI_BTN_COUNT; i++) {
        for (j = i + 1u; j < (unsigned)UI_BTN_COUNT; j++) {
            const ui_btn_def_t *a = &ui_btn_defs[i], *b = &ui_btn_defs[j];
            int excl = ((a->cond == UI_COND_FILL_RUN) && (b->cond == UI_COND_FILL_DONE)) ||
                       ((a->cond == UI_COND_REHEAT_RUN) && (b->cond == UI_COND_REHEAT_DONE));
            int ov = (a->hit.x < (b->hit.x + b->hit.w)) && (b->hit.x < (a->hit.x + a->hit.w)) &&
                     (a->hit.y < (b->hit.y + b->hit.h)) && (b->hit.y < (a->hit.y + a->hit.h));
            if ((a->scr == b->scr) && !excl) {
                if (ov) { printf("  重なり: %s と %s\n", a->name, b->name); }
                CHECK(!ov);
            }
        }
    }
    /* 境界の1画素: 当たり判定の内側は押せて、外側1画素は押せない(h_fill: x=13..124, y=189..258) */
    {
        static const struct { int x, y, hit; } P[] = {
            { 13, 189, 1 }, { 124, 258, 1 }, { 12, 189, 0 }, { 13, 188, 0 }, { 125, 258, 0 }, { 124, 259, 0 }
        };
        for (i = 0u; i < sizeof(P) / sizeof(P[0]); i++) {
            nav_sample_t s = pt(1u, P[i].x, P[i].y);
            reset();
            step(&s); step(&s);
            CHECK_EQ(find("btn=h_fill st=down src=touch") >= 0, P[i].hit);
        }
    }
    /* ホームの全面(480x272)を走査: 押せる点がどのボタンの当たり判定にも属し、それ以外はボタンにならない */
    {
        unsigned wrong = 0u;
        int x, y;
        for (y = 0; y < 272; y += 3) {
            for (x = 0; x < 480; x += 3) {
                nav_sample_t s = pt(1u, x, y);
                unsigned expect = 0u;
                for (i = 1u; i < (unsigned)UI_BTN_COUNT; i++) {
                    const ui_btn_def_t *d = &ui_btn_defs[i];
                    if ((d->scr == UI_SCR_HOME) && (x >= d->hit.x) && (x < d->hit.x + d->hit.w) && (y >= d->hit.y) && (y < d->hit.y + d->hit.h)) { expect = 1u; }
                }
                reset();
                step(&s); step(&s);
                if ((count_pfx("btn=") == 1u) != (expect == 1u)) { wrong++; }
            }
        }
        CHECK_EQ(wrong, 0);
    }

    /* ================================================================ X2 UT10 / UT11 画面の順送り・逆送り */
    TEST("X2 UT10/UT11: n x5(700ms 間隔)で to = fill reheat usage standby home、from = home fill reheat usage standby、すべて src=uart。p は逆。btn は出ない");
    {
        static const char *const N5[5] = { "scr=fill from=home src=uart", "scr=reheat from=fill src=uart", "scr=usage from=reheat src=uart",
                                           "scr=standby from=usage src=uart", "scr=home from=standby src=uart" };
        static const char *const P5[5] = { "scr=standby from=home src=uart", "scr=usage from=standby src=uart", "scr=reheat from=usage src=uart",
                                           "scr=fill from=reheat src=uart", "scr=home from=fill src=uart" };
        reset(); run(300u);
        for (i = 0u; i < 5u; i++) { cmd(UI_CMD_NEXT); run(700u); }
        CHECK_EQ(nR, 5);
        for (i = 0u; i < 5u; i++) { CHECK(nth_is(i, N5[i])); }
        CHECK_EQ(U.v.scr, UI_SCR_HOME);
        CHECK_EQ(count_pfx("btn="), 0);
        reset(); run(300u);
        for (i = 0u; i < 5u; i++) { cmd(UI_CMD_PREV); run(700u); }
        CHECK_EQ(nR, 5);
        for (i = 0u; i < 5u; i++) { CHECK(nth_is(i, P5[i])); }
        CHECK_EQ(U.v.scr, UI_SCR_HOME);
        /* 300ms 間隔でも取りこぼさない(遷移 250ms) */
        reset(); run(300u);
        for (i = 0u; i < 5u; i++) { cmd(UI_CMD_NEXT); run(300u); }
        CHECK_EQ(nR, 5);
        /* 250ms 未満で続けて送ると、遷移中なので無視される(UD3) */
        reset(); run(300u);
        cmd(UI_CMD_NEXT); run(100u); cmd(UI_CMD_NEXT); run(500u);
        CHECK_EQ(nR, 1);
    }

    /* ================================================================ X3 UT12 温度の上下と上下限 */
    TEST("X3 UT12: + x8, - x2(500ms 間隔): btn h_up の down/act が8組、set = 43..48 の6本(7・8回目は出ない)、続いて 47 46、h_dn が2組");
    {
        static const char *const EXP[] = {
            "btn=h_up st=down src=uart", "btn=h_up st=act src=uart", "set=43 src=uart",
            "btn=h_up st=down src=uart", "btn=h_up st=act src=uart", "set=44 src=uart",
            "btn=h_up st=down src=uart", "btn=h_up st=act src=uart", "set=45 src=uart",
            "btn=h_up st=down src=uart", "btn=h_up st=act src=uart", "set=46 src=uart",
            "btn=h_up st=down src=uart", "btn=h_up st=act src=uart", "set=47 src=uart",
            "btn=h_up st=down src=uart", "btn=h_up st=act src=uart", "set=48 src=uart",
            "btn=h_up st=down src=uart", "btn=h_up st=act src=uart",
            "btn=h_up st=down src=uart", "btn=h_up st=act src=uart",
            "btn=h_dn st=down src=uart", "btn=h_dn st=act src=uart", "set=47 src=uart",
            "btn=h_dn st=down src=uart", "btn=h_dn st=act src=uart", "set=46 src=uart"
        };
        reset(); run(300u);
        for (i = 0u; i < 8u; i++) { cmd(UI_CMD_PLUS); run(500u); }
        for (i = 0u; i < 2u; i++) { cmd(UI_CMD_MINUS); run(500u); }
        CHECK_EQ(nR, sizeof(EXP) / sizeof(EXP[0]));
        for (i = 0u; (i < nR) && (i < sizeof(EXP) / sizeof(EXP[0])); i++) { CHECK(nth_is(i, EXP[i])); }
        CHECK_EQ(U.v.set, 46);
        CHECK_EQ(U.fb_phase, UI_PH_NONE);
    }

    /* ================================================================ X4 UT16 通常速度の完了 */
    TEST("X4 UT16: n n(700ms)で fill → reheat。reheat から done=reheat まで 16.0 秒(設定 42℃)。done=fill は出ない(32秒未満で離れる)");
    {
        uint32_t t_scr, t_done;
        int a, b;
        reset(); run(300u);
        cmd(UI_CMD_NEXT); run(700u); cmd(UI_CMD_NEXT);
        run(20000u);
        a = find("scr=reheat from=fill src=uart");
        b = find("done=reheat");
        CHECK(a >= 0 && b > a);
        if ((a >= 0) && (b > a)) {
            t_scr = R[a].t; t_done = R[b].t;
            CHECK(t_done - t_scr >= 15990u && t_done - t_scr <= 16010u);
        }
        CHECK_EQ(find("done=fill"), -1);
        CHECK_EQ(count_pfx("scr="), 2);
        /* 設定温度を変えると所要時間が変わる: 設定 48 なら (480-380) x 40ms = 40 秒 */
        reset(); run(100u);
        U.v.set = 48u;
        cmd(UI_CMD_NEXT); run(700u); cmd(UI_CMD_NEXT);
        run(45000u);
        a = find("scr=reheat from=fill src=uart"); b = find("done=reheat");
        CHECK(a >= 0 && b > a);
        if ((a >= 0) && (b > a)) { CHECK(R[b].t - R[a].t >= 39990u && R[b].t - R[a].t <= 40010u); }
    }

    /* ================================================================ X5 UT17 連打 */
    TEST("X5 UT17: + を 10ms ごとに 60 個(1 tick に1文字): set の [ev] は 1〜6 本、48 を超えない、押下表示が残らない");
    {
        unsigned sets = 0u, k;
        reset(); run(300u);
        for (k = 0u; k < 60u; k++) { cmd(UI_CMD_PLUS); step(&UP); }
        run(1000u);
        for (k = 0u; k < nR; k++) {
            if (strncmp(R[k].s, "set=", 4) == 0) { sets++; CHECK(atoi(R[k].s + 4) <= 48); }
        }
        CHECK(sets >= 1u && sets <= 6u);
        CHECK(U.v.set >= 43u && U.v.set <= 48u);
        CHECK_EQ(U.fb_phase, UI_PH_NONE);
        CHECK_EQ(U.ev_lost, 0);
        printf("  (参考) 60 個の + で set の [ev] = %u 本、最終 set=%u\n", sets, U.v.set);
        /* 長く打ち続けても 48 で止まり、その後 - で 1 ずつ戻る */
        reset(); run(100u);
        for (k = 0u; k < 600u; k++) { cmd(UI_CMD_PLUS); step(&UP); }
        run(500u);
        CHECK_EQ(U.v.set, 48);
        for (k = 0u; k < 600u; k++) { cmd(UI_CMD_MINUS); step(&UP); }
        run(500u);
        CHECK_EQ(U.v.set, 35);
        CHECK_EQ(U.ev_lost, 0);
    }

    /* ================================================================ X6 更新間隔のばらつき */
    TEST("X6 ui_update の呼び出しが 10ms 刻みでなくても(メインが詰まって 20〜40ms 飛ぶ)、デモは同じ 36 本が同じ順序で出る");
    {
        static const uint32_t DT[] = { 10u, 20u, 30u, 40u, 10u, 10u, 20u };
        unsigned k = 0u;
        uint32_t d0;
        reset(); run(100u);
        d0 = T; nR = 0u;
        cmd(UI_CMD_DEMO);
        while ((T - d0) < 36000u) { T += DT[k++ % (sizeof(DT) / sizeof(DT[0]))]; ui_update(&U, &UP, T); drain(); }
        CHECK_EQ(demo_mismatch(), 0);
        CHECK_EQ(U.demo, 0); CHECK_EQ(U.speed, 1); CHECK_EQ(U.v.scr, UI_SCR_HOME);
    }

    /* ================================================================ X7 32bit 折り返し */
    TEST("X7 now_ms が 2^32 を折り返す瞬間をまたいでも、デモが同じ 36 本・順序で出る(unsigned の引き算)");
    {
        uint32_t d0;
        reset_at(0xFFFFFC00u - 100u);
        run(100u);
        d0 = T; nR = 0u;
        cmd(UI_CMD_DEMO);
        while ((uint32_t)(T - d0) < 34000u) { step(&UP); }
        CHECK(T < d0);                                       /* 途中で折り返した */
        CHECK_EQ(demo_mismatch(), 0);
        CHECK_EQ(U.demo, 0);
        CHECK_EQ(U.v.scr, UI_SCR_HOME);
    }

    /* ================================================================ X8 デモの再開 */
    TEST("X8 デモを UART で中断してすぐ d で再開: 台本が最初から(set=42、4倍速)。終了後にもう一度 d でも同じ36本");
    {
        uint32_t d0;
        reset(); run(100u);
        cmd(UI_CMD_DEMO); run(5600u);
        CHECK_EQ(U.v.scr, UI_SCR_FILL);
        cmd(UI_CMD_DEMO);                                    /* 中断 */
        CHECK_EQ(U.demo, 0);
        U.v.set = 40u;
        nR = 0u;
        cmd(UI_CMD_DEMO);                                    /* 再開 */
        CHECK_EQ(U.demo, 1); CHECK_EQ(U.v.set, 42); CHECK_EQ(U.speed, 4);
        d0 = T;
        run(34000u);
        /* 再開の [ev] は start のみ(ホームにいたので scr は出ない)。この 36 本は DEMO_EV と一致するはず */
        CHECK_EQ(demo_mismatch(), 0);
        CHECK_EQ(T - d0, 34000);
        nR = 0u;                                             /* 終了後にもう一度 */
        cmd(UI_CMD_DEMO);
        run(34000u);
        CHECK_EQ(demo_mismatch(), 0);
    }

    /* ================================================================ X9 仮想押下中の中断(D1〜D3 修正確認) */
    TEST("X9 [D1 修正済み] デモの仮想押下(150ms)の最中に実タッチで中断すると、押下表示が離し待ちのまま残り、3秒後に中断済みのはずのボタンが動作する");
    {
        nav_sample_t s = pt(1u, 240, 100);
        unsigned k;
        reset(); run(100u);
        cmd(UI_CMD_DEMO);
        run(840u);                                           /* デモ 0.84 秒: h_up の押下表示中(押下 800 → 実行 950) */
        CHECK_EQ(U.fb_phase, UI_PH_PRESS);
        nR = 0u;
        step(&s); step(&s);                                  /* 実タッチ → 中断 */
        CHECK(find("demo=abort src=touch") >= 0);
        CHECK_EQ(U.demo, 0);
        for (k = 0u; k < 4u; k++) { step(&s); }
        step(&UP); step(&UP); step(&UP);                     /* 指を離す */
        run(200u);
        CHECK(U.fb_phase != UI_PH_PRESS);   /* D1a 中断後 0.3 秒で押下表示(PRESS)が残っていない */
        /* 中断された後に、実際のボタンが「実行」されてしまわないこと */
        run(3500u);
        CHECK(find("btn=h_up st=act src=demo") < 0);   /* D1b 中断後に btn=h_up st=act(デモ起源)が出ない */
        CHECK(find("set=43 src=demo") < 0);   /* D1c 中断後に set=43 が出ない(デモ終了後に設定が変わらない) */
    }
    TEST("X9b [D1 修正済み] 同じ状況で、押下表示が残る間は実タッチのボタンが効かない(最大 3 秒)");
    {
        nav_sample_t s = pt(1u, 240, 100);
        reset(); run(100u);
        cmd(UI_CMD_DEMO);
        run(840u);
        step(&s); step(&s); step(&UP); step(&UP); step(&UP);   /* 中断 */
        run(300u);
        nR = 0u;
        {
            int32_t x = ui_btn_defs[UI_BTN_H_FILL].vis.x + 5, y = ui_btn_defs[UI_BTN_H_FILL].vis.y + 5;
            nav_sample_t t = pt(1u, x, y);
            step(&t); step(&t);
            CHECK(count_pfx("btn=h_fill st=down src=touch") == 1u);   /* D1d 中断の 0.4 秒後でも「湯はり」の押下が受け付けられる */
        }
    }
    TEST("X9c [D1 修正済み] UART の d で中断した場合も同じ(仮想押下の最中)");
    {
        reset(); run(100u);
        cmd(UI_CMD_DEMO);
        run(840u);
        CHECK_EQ(U.fb_phase, UI_PH_PRESS);
        cmd(UI_CMD_DEMO);                                    /* 中断 */
        CHECK_EQ(U.demo, 0);
        run(3500u);
        CHECK(find("btn=h_up st=act src=demo") < 0);   /* D1e UART 中断の後に btn=h_up st=act src=demo が出ない */
        CHECK(find("set=43 src=demo") < 0);   /* D1f UART 中断の後に set=43 が出ない */
    }
    TEST("X9d [D2 修正済み] + を送った直後(150ms 以内)に d で開始すると、仮想押下がデモの途中で実行される");
    {
        reset(); run(100u);
        cmd(UI_CMD_PLUS);
        run(50u);
        CHECK_EQ(U.fb_phase, UI_PH_PRESS);
        nR = 0u;
        cmd(UI_CMD_DEMO);
        run(3200u);
        CHECK(find("btn=h_up st=act src=uart") < 0);   /* D2a デモ開始の前の押下(src=uart)が、デモ中に act として出ない */
    }
    TEST("X9e [D3 修正済み] 画面が戻るスライド中(ホームに戻る途中)に実タッチで中断しても、スライドが続く(設計 §6.1 は「アニメーションなしで即ホーム」)");
    {
        nav_sample_t s = pt(1u, 240, 100);
        reset(); run(100u);
        cmd(UI_CMD_DEMO);
        run(12950u + 100u);                                  /* f_done の実行(12950)の 100ms 後 = 戻るスライドの途中 */
        CHECK_EQ(U.v.scr, UI_SCR_HOME);
        CHECK_EQ(U.tr_on, 1);
        step(&s); step(&s);
        CHECK_EQ(U.demo, 0);
        CHECK(U.tr_on == 0u);   /* D3a 中断の直後にスライドが止まる(tr_on = 0) */
    }

    /* ================================================================ X10 タッチ座標の変換の端 */
    TEST("X10 ui_touch_map: x_max / y_max = 0 でもゼロ割りせず画面内に収まる。生座標 0xFFFF も収まる。入れ替え時は軸ごとの最大値で正規化");
    {
        ui_t u;
        int32_t px, py;
        static const uint16_t RAWS[] = { 0u, 1u, 270u, 271u, 272u, 478u, 479u, 480u, 4095u, 0xFFFFu };
        unsigned fl, a, b;
        for (fl = 0u; fl < 8u; fl++) {
            static const uint16_t XM[] = { 0u, 1u, 271u, 479u, 4095u, 0xFFFFu };
            unsigned xi;
            for (xi = 0u; xi < sizeof(XM) / sizeof(XM[0]); xi++) {
                ui_init(&u, 0u, XM[xi], XM[(xi + 2u) % 6u], (uint8_t)fl);
                for (a = 0u; a < sizeof(RAWS) / sizeof(RAWS[0]); a++) {
                    for (b = 0u; b < sizeof(RAWS) / sizeof(RAWS[0]); b++) {
                        ui_touch_map(&u, RAWS[a], RAWS[b], &px, &py);
                        CHECK(px >= 0 && px <= 479 && py >= 0 && py <= 271);
                    }
                }
            }
        }
        ui_init(&u, 0u, 271u, 479u, (uint8_t)UI_TF_SWAP_XY);       /* 生X軸 0..271 が画面の縦、生Y軸 0..479 が横 */
        ui_touch_map(&u, 271u, 479u, &px, &py); CHECK_EQ(px, 479); CHECK_EQ(py, 271);
        ui_touch_map(&u, 0u, 0u, &px, &py);     CHECK_EQ(px, 0);   CHECK_EQ(py, 0);
        ui_touch_map(&u, 0u, 479u, &px, &py);   CHECK_EQ(px, 479); CHECK_EQ(py, 0);
        ui_init(&u, 0u, 479u, 271u, (uint8_t)(UI_TF_FLIP_X | UI_TF_FLIP_Y));
        ui_touch_map(&u, 0u, 0u, &px, &py);     CHECK_EQ(px, 479); CHECK_EQ(py, 271);
        ui_touch_map(&u, 479u, 271u, &px, &py); CHECK_EQ(px, 0);   CHECK_EQ(py, 0);
        /* 単調: 生 x が増えると px が減らない */
        ui_init(&u, 0u, 1000u, 600u, 0u);
        for (a = 0u, j = 0u; a <= 1000u; a++) { ui_touch_map(&u, (uint16_t)a, 0u, &px, &py); CHECK((uint32_t)px >= j); j = (uint32_t)px; }
        CHECK_EQ((int)j, 479);
    }
    TEST("X10b 式の端: ui_bar_h(i >= 7) = 0、棒の高さは時間で単調増加して最終値に一致、遷移量は単調、flow は 67..103");
    {
        uint32_t k;
        CHECK_EQ(ui_bar_h(7u, 10000u), 0); CHECK_EQ(ui_bar_h(255u, 10000u), 0);
        for (i = 0u; i < UI_USAGE_N; i++) {
            uint32_t prev = 0u;
            CHECK_EQ(ui_bar_h((uint8_t)i, 0u), 0);
            for (k = 0u; k <= 700u; k++) { uint32_t h = ui_bar_h((uint8_t)i, k); CHECK(h >= prev); prev = h; }
            CHECK_EQ(ui_bar_h((uint8_t)i, 640u), ui_usage_h[i]);
            CHECK_EQ(ui_bar_h((uint8_t)i, 0xFFFFFFFFu), ui_usage_h[i]);
        }
        for (k = 0u; k < 100000u; k += 97u) { uint32_t f = ui_flow_x10(k); CHECK(f >= 67u && f <= 103u); }
        CHECK(ui_flow_x10(0xFFFFFFFFu) >= 67u);
        CHECK_EQ(ui_clock_min(0u), 21 * 60 + 45);
        CHECK_EQ(ui_clock_min(60000u * 135u), 0);            /* 21:45 + 2:15 = 24:00 → 00:00 */
        CHECK(ui_clock_min(0xFFFFFFFFu) < 1440u);
    }

    /* ================================================================ X11 状態の不変条件(ファズ) */
    TEST("X11 ファズ: 乱数の標本(NULL・2点・範囲外座標・押しっぱなし)と UART コマンドを 40 万 tick 与えても、不変条件が崩れない");
    {
        uint32_t t, bad = 0u, press_ms = 0u, max_press_ms = 0u, d1_hits = 0u;
        unsigned ev_total = 0u;
        nav_sample_t s;
        int holding = 0;
        reset_at(0xFFFFF000u);
        for (t = 0u; t < 400000u; t++) {
            uint32_t r = rnd();
            ui_ev_t ev;
            if ((r & 0x3FFu) == 0u) { holding = !holding; }
            if ((r & 0xFFu) < 3u) { cmd((ui_cmd_t)((r >> 8) % 5u)); }
            T += 10u;
            if ((r >> 20) % 11u == 0u) {
                ui_update(&U, NULL, T);
            } else {
                s.pressed = holding ? ((r & 7u) != 0u) : ((r & 63u) == 0u);
                s.points = (uint8_t)(s.pressed ? (((r >> 24) % 9u == 0u) ? 2u + (r >> 28) % 3u : 1u) : 0u);
                s.x = (uint16_t)((r >> 3) % 700u); s.y = (uint16_t)((r >> 12) % 500u);
                if ((r >> 16) % 211u == 0u) { s.x = 0xFFFFu; s.y = 0xFFFFu; }
                ui_update(&U, &s, T);
            }
            while (ui_event_pop(&U, &ev)) {
                ev_total++;
                if (!((ev.type >= UI_EV_SCR) && (ev.type <= UI_EV_DEMO))) { bad++; }
                if ((ev.type == UI_EV_SCR) && ((ev.a >= UI_SCR_COUNT) || (ev.b >= UI_SCR_COUNT) || (ev.a == ev.b) || (ev.src > UI_SRC_DEMO))) { bad++; }
                if (((ev.type == UI_EV_BTN_DOWN) || (ev.type == UI_EV_BTN_ACT)) && ((ev.a == UI_BTN_NONE) || (ev.a >= UI_BTN_COUNT))) { bad++; }
                if ((ev.type == UI_EV_SET) && ((ev.a < UI_SET_MIN) || (ev.a > UI_SET_MAX))) { bad++; }
                if ((ev.type == UI_EV_DONE) && (ev.a > UI_DONE_REHEAT)) { bad++; }
            }
            if (U.v.scr >= UI_SCR_COUNT) { bad++; }
            if ((U.v.set < UI_SET_MIN) || (U.v.set > UI_SET_MAX)) { bad++; }
            if (U.v.fill_x10 > 1000u) { bad++; }
            if ((U.v.bath_x10 < 350u) || (U.v.bath_x10 > 480u)) { bad++; }
            if ((U.speed != 1u) && (U.speed != UI_DEMO_SPEED)) { bad++; }
            if ((U.demo == 0u) && (U.speed != 1u)) { bad++; }
            if (U.tr_dx > 480u) { bad++; }
            if (U.fb_btn >= UI_BTN_COUNT) { bad++; }
            if (U.fb_phase > UI_PH_FLASH) { bad++; }
            if ((U.fb_phase != UI_PH_NONE) && (U.fb_btn == UI_BTN_NONE)) { bad++; }
            if (U.ev_n > UI_EV_Q) { bad++; }
            if (U.tr_on && ((U.v.scr == UI_SCR_STANDBY) || (U.ov.scr == UI_SCR_STANDBY))) { bad++; }
            if (U.v.scr == UI_SCR_STANDBY) { if ((U.fb_phase != UI_PH_NONE) && (U.fb_btn != UI_BTN_NONE) && (ui_btn_defs[U.fb_btn].scr == UI_SCR_STANDBY)) { bad++; } }
            if (U.fb_phase == UI_PH_PRESS) { press_ms += 10u; if (press_ms > max_press_ms) { max_press_ms = press_ms; } } else { press_ms = 0u; }
            if ((U.demo == 0u) && (U.fb_phase == UI_PH_PRESS) && (U.fb_src == UI_SRC_DEMO)) { d1_hits++; }
        }
        CHECK_EQ(bad, 0);
        CHECK(max_press_ms <= 3020u);                        /* PRESS が3秒を超えて続かない */
        CHECK(d1_hits == 0u);   /* D1g ファズ: デモが終わった(中断された)後に、デモ起源の PRESS が残らない */
        printf("  (参考) イベント総数 %u、最長の PRESS %u ms、デモ中断後に残った PRESS の tick 数 %u\n", ev_total, (unsigned)max_press_ms, (unsigned)d1_hits);
        CHECK(ev_total > 1000u);                             /* ファズが十分に動いた */
    }

    /* ================================================================ X12 gfx の極端な引数 */
    TEST("X12 gfx: 座標・大きさが INT32 の端でも面の外へ書かず(番兵)、fillrect は遅い基準実装と一致する");
    {
        enum { GUARD = 4096 };
        static uint16_t buf[GUARD + 480 * 272 + GUARD];
        static uint16_t ref[480 * 272];
        static uint8_t  a8[64 * 64];
        static const int32_t EX[] = { INT32_MIN, INT32_MIN + 1, -481, -480, -1, 0, 1, 479, 480, 1000000, INT32_MAX };
        unsigned xi, yi, wi, k, bad = 0u;
        uint16_t *fb = &buf[GUARD];
        for (k = 0u; k < sizeof(a8); k++) { a8[k] = (uint8_t)(k * 7u); }
        for (xi = 0u; xi < sizeof(EX) / sizeof(EX[0]); xi++) {
            for (yi = 0u; yi < sizeof(EX) / sizeof(EX[0]); yi++) {
                for (wi = 0u; wi < sizeof(EX) / sizeof(EX[0]); wi++) {
                    int32_t x = EX[xi], y = EX[yi], w = EX[wi], h = EX[(wi + yi + 3u) % (sizeof(EX) / sizeof(EX[0]))];
                    int32_t r, c;
                    for (k = 0u; k < sizeof(buf) / sizeof(buf[0]); k++) { buf[k] = 0xA5A5u; }
                    memset(ref, 0xA5, sizeof(ref));
                    gfx_fillrect(fb, x, y, w, h, 0x1234u);
                    if ((w > 0) && (h > 0)) {
                        for (r = 0; r < 272; r++) {
                            for (c = 0; c < 480; c++) {
                                if (((int64_t)c >= x) && ((int64_t)c < (int64_t)x + w) && ((int64_t)r >= y) && ((int64_t)r < (int64_t)y + h)) { ref[r * 480 + c] = 0x1234u; }
                            }
                        }
                    }
                    for (k = 0u; k < GUARD; k++) { if ((buf[k] != 0xA5A5u) || (buf[GUARD + 480 * 272 + k] != 0xA5A5u)) { bad++; break; } }
                    if (memcmp(fb, ref, sizeof(ref)) != 0) { bad++; }
                    /* a8 と blit は面の外へ書かないことだけ(w,h を 64 以下に絞って読み出し範囲も守る) */
                    for (k = 0u; k < sizeof(buf) / sizeof(buf[0]); k++) { buf[k] = 0xA5A5u; }
                    gfx_a8(fb, a8, 64, 64, x, y, 0xFFFFu, 256u);
                    gfx_blit565(fb, &buf[GUARD], 480, 0, 0, 64, 64, x, y);
                    for (k = 0u; k < GUARD; k++) { if ((buf[k] != 0xA5A5u) || (buf[GUARD + 480 * 272 + k] != 0xA5A5u)) { bad++; break; } }
                }
            }
        }
        CHECK_EQ(bad, 0);
    }

    /* ================================================================ X13 ui_render の極端な値 */
    TEST("X13 ui_render: 値が範囲外(時計 65535 分・水位/浴槽/流量 65535・設定 0..255・棒 65535・画面番号 9)でも面の前後と右の 32 列へ書かず、左 480 列の全画素を書く");
    {
        enum { GUARD = 4096, NPX = GFX_STRIDE * 272 };
        static uint8_t buf[GUARD + NPX + GUARD];
        static uint8_t alt[NPX];
        static ui_t u;
        uint8_t *fb = &buf[GUARD];
        unsigned n, k, r0, c0, bad = 0u, holes = 0u, margin = 0u;
        for (n = 0u; n < 400u; n++) {
            uint32_t r = rnd();
            ui_init(&u, 0u, 479u, 271u, 0u);
            u.v.scr = (uint8_t)((n < 20u) ? (n % 10u) : ((r >> 3) % 6u));
            u.ov.scr = (uint8_t)((r >> 6) % 5u);
            u.v.set = (uint8_t)((r >> 9) % 256u); u.v.bath_x10 = (uint16_t)(rnd() & 0xFFFFu);
            u.v.fill_x10 = (uint16_t)(rnd() & 0xFFFFu); u.v.flow_x10 = (uint16_t)(rnd() & 0xFFFFu);
            u.v.clock_min = (uint16_t)(rnd() & 0xFFFFu); u.v.bar_ms = (uint16_t)(rnd() & 0xFFFFu);
            u.ov.set = (uint8_t)(rnd() % 256u); u.ov.bath_x10 = (uint16_t)(rnd() & 0xFFFFu);
            u.ov.fill_x10 = (uint16_t)(rnd() & 0xFFFFu); u.ov.flow_x10 = (uint16_t)(rnd() & 0xFFFFu);
            u.ov.clock_min = (uint16_t)(rnd() & 0xFFFFu); u.ov.bar_ms = (uint16_t)(rnd() & 0xFFFFu);
            u.tr_on = (uint8_t)(r & 1u); u.tr_fwd = (uint8_t)((r >> 1) & 1u); u.tr_dx = (uint16_t)(rnd() % 481u);
            u.demo = (uint8_t)((r >> 2) & 1u);
            u.fb_phase = (uint8_t)((r >> 12) % 3u); u.fb_btn = (uint8_t)(1u + (r >> 14) % 15u);
            memset(buf, 0xA5, sizeof(buf));
            memset(alt, 0x5A, sizeof(alt));
            ui_render(&u, fb);
            ui_render(&u, alt);
            for (k = 0u; k < GUARD; k++) { if ((buf[k] != 0xA5u) || (buf[GUARD + NPX + k] != 0xA5u)) { bad++; break; } }
            for (r0 = 0u; r0 < 272u; r0++) {
                for (c0 = 480u; c0 < GFX_STRIDE; c0++) {
                    if ((fb[r0 * GFX_STRIDE + c0] != 0xA5u) || (alt[r0 * GFX_STRIDE + c0] != 0x5Au)) { margin++; }   /* 右の 32 列は触らない */
                }
                if (memcmp(&fb[r0 * GFX_STRIDE], &alt[r0 * GFX_STRIDE], 480u) != 0) { holes++; }   /* 2通りの下地で結果が同じ = 全画素を書いた */
            }
        }
        CHECK_EQ(bad, 0);
        CHECK_EQ(margin, 0);
        CHECK_EQ(holes, 0);
    }

    TEST_REPORT("test_ui_extra");
}
