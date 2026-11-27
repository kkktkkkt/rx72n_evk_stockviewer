/* test_ui_tap.c - H-12 タップ検出(docs/design.md §6.1・§8.1 H-12)。ui_logic.c だけをリンクする(標準 C)。
 *   2 標本で押下確定、3 標本で離し確定 → UI_EV_TAP 1 回。2 点以上は無視。3,000ms を超える押しっぱなしはタップにしない。連続タップ。
 *   座標は使わない。標本なし(NULL)は状態を変えない。キューは 8 件で、溢れたら新しい方を捨てて数える。 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ui_logic.h"
#include "test_framework.h"

#define T10 (NAV_SAMPLE_MS)

static nav_sample_t DN(uint8_t points, uint16_t x, uint16_t y)
{
    nav_sample_t s;
    memset(&s, 0, sizeof s);
    s.pressed = true; s.points = points; s.x = x; s.y = y;
    return s;
}

static nav_sample_t UP(void)
{
    nav_sample_t s;
    memset(&s, 0, sizeof s);
    return s;
}

static unsigned drain(ui_t *u)
{
    ui_ev_t ev;
    unsigned n = 0u;
    while (ui_event_pop(u, &ev)) {
        n++;
    }
    return n;
}

/* n 標本を T10 刻みで送り、最後の時刻を返す */
static uint32_t feed(ui_t *u, const nav_sample_t *s, unsigned n, uint32_t t)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        t += T10;
        ui_update(u, s, t);
    }
    return t;
}

int main(void)
{
    ui_t u;
    ui_ev_t ev;
    nav_sample_t d = DN(1u, 100u, 100u);
    nav_sample_t d2 = DN(2u, 10u, 10u);
    nav_sample_t d3 = DN(3u, 10u, 10u);
    nav_sample_t up = UP();
    uint32_t t, td;
    unsigned i;

    TEST("H-12 基本: 押下 2 標本・離し 3 標本で UI_EV_TAP 1 回(離しの確定でイベント)");
    ui_init(&u, 0u);
    t = feed(&u, &d, UI_TAP_DOWN_N, 0u);
    CHECK_EQ(drain(&u), 0);                                  /* 押している間は出ない */
    t = feed(&u, &d, 10u, t);
    CHECK_EQ(drain(&u), 0);
    t = feed(&u, &up, UI_TAP_UP_N - 1u, t);
    CHECK_EQ(drain(&u), 0);                                  /* 離し 2 標本: まだ */
    t = feed(&u, &up, 1u, t);
    CHECK(ui_event_pop(&u, &ev));
    CHECK_EQ(ev.type, UI_EV_TAP);
    CHECK_EQ(ev.src, UI_SRC_TOUCH);
    CHECK(!ui_event_pop(&u, &ev));
    t = feed(&u, &up, 20u, t);
    CHECK_EQ(drain(&u), 0);                                  /* 離したままでは増えない */

    TEST("H-12 押下確定の前に離れたらタップにならない(1 標本だけ・離れて再び)");
    ui_init(&u, 0u);
    t = feed(&u, &d, UI_TAP_DOWN_N - 1u, 0u);
    t = feed(&u, &up, 5u, t);
    CHECK_EQ(drain(&u), 0);
    t = feed(&u, &d, 1u, t);
    t = feed(&u, &up, 1u, t);
    t = feed(&u, &d, 1u, t);                                 /* d u d: 連続 2 標本に届かない */
    t = feed(&u, &up, 5u, t);
    CHECK_EQ(drain(&u), 0);

    TEST("H-12 押している間の途切れ(離し 1〜2 標本)は離しにならない。続けて押せば 1 回のタップ");
    ui_init(&u, 0u);
    t = feed(&u, &d, 5u, 0u);
    t = feed(&u, &up, UI_TAP_UP_N - 1u, t);                  /* 離し 2 標本 */
    t = feed(&u, &d, 3u, t);                                 /* また触れた: 離しのカウントは戻る */
    CHECK_EQ(drain(&u), 0);
    t = feed(&u, &up, UI_TAP_UP_N - 1u, t);
    CHECK_EQ(drain(&u), 0);
    t = feed(&u, &up, 1u, t);
    CHECK_EQ(drain(&u), 1);                                  /* 3 連続で初めて 1 回 */

    TEST("H-12 2 点以上のタッチは押下にしない(全部離れるまで無視。その後は普通に反応)");
    ui_init(&u, 0u);
    t = feed(&u, &d2, 10u, 0u);
    t = feed(&u, &up, 5u, t);
    CHECK_EQ(drain(&u), 0);
    t = feed(&u, &d3, 4u, t);
    t = feed(&u, &up, 5u, t);
    CHECK_EQ(drain(&u), 0);
    t = feed(&u, &d, UI_TAP_DOWN_N - 1u, t);                 /* 押下確定の前(1 標本)に 2 点目が触れた */
    t = feed(&u, &d2, 3u, t);                                /* → 無視。1 点に戻しても離し切るまで BLOCK のまま */
    t = feed(&u, &d, 3u, t);
    t = feed(&u, &up, 5u, t);
    CHECK_EQ(drain(&u), 0);
    t = feed(&u, &d, UI_TAP_DOWN_N, t);                      /* 離し切った後の 1 点 */
    t = feed(&u, &up, UI_TAP_UP_N, t);
    CHECK_EQ(drain(&u), 1);

    TEST("H-12 押しっぱなしの上限: 押下確定から離し確定までが 3,000ms ちょうどはタップ、10ms 超えたら無し");
    ui_init(&u, 0u);
    t = feed(&u, &d, UI_TAP_DOWN_N, 1000u);
    td = t;                                                  /* 押下確定の時刻 */
    t = feed(&u, &d, (UI_TAP_HOLD_MAX_MS / T10) - UI_TAP_UP_N, t);   /* 押下を td + 3000 - 30 まで続け、離し 3 標本の最後が td + 3000 */
    t = feed(&u, &up, UI_TAP_UP_N, t);
    CHECK_EQ(t - td, UI_TAP_HOLD_MAX_MS);
    CHECK_EQ(drain(&u), 1);
    ui_init(&u, 0u);
    t = feed(&u, &d, UI_TAP_DOWN_N, 1000u);
    td = t;
    t = feed(&u, &d, (UI_TAP_HOLD_MAX_MS / T10) - UI_TAP_UP_N + 1u, t);   /* 離し確定が 10ms 遅れる */
    t = feed(&u, &up, UI_TAP_UP_N, t);
    CHECK_EQ(t - td, UI_TAP_HOLD_MAX_MS + T10);
    CHECK_EQ(drain(&u), 0);

    TEST("H-12 長押し(3,000ms 超)はタップにせず、離した後はまた反応する");
    ui_init(&u, 0u);
    t = feed(&u, &d, 400u, 0u);                              /* 4 秒 */
    CHECK_EQ(drain(&u), 0);
    t = feed(&u, &up, 5u, t);
    CHECK_EQ(drain(&u), 0);
    t = feed(&u, &d, UI_TAP_DOWN_N, t);
    t = feed(&u, &up, UI_TAP_UP_N, t);
    CHECK_EQ(drain(&u), 1);

    TEST("H-12 連続タップ: 同じ操作を 5 回で 5 回(押下 2・離し 3 の最短でも)");
    ui_init(&u, 0u);
    for (i = 0u; i < 5u; i++) {
        t = feed(&u, &d, UI_TAP_DOWN_N, t);
        t = feed(&u, &up, UI_TAP_UP_N, t);
    }
    CHECK_EQ(drain(&u), 5);

    TEST("H-12 座標は使わない(画面の四隅・範囲外の座標でも 1 回)");
    {
        nav_sample_t c[5];
        c[0] = DN(1u, 0u, 0u); c[1] = DN(1u, 479u, 0u); c[2] = DN(1u, 0u, 271u); c[3] = DN(1u, 479u, 271u); c[4] = DN(1u, 65535u, 65535u);
        ui_init(&u, 0u);
        for (i = 0u; i < 5u; i++) {
            t = feed(&u, &c[i], UI_TAP_DOWN_N, t);
            t = feed(&u, &up, UI_TAP_UP_N, t);
        }
        CHECK_EQ(drain(&u), 5);
    }

    TEST("H-12 標本なし(NULL)は状態を変えない: 押下の途中・押している途中・離しの途中に挟んでも結果は同じ");
    ui_init(&u, 0u);
    ui_update(&u, NULL, 10u);
    t = feed(&u, &d, 1u, 100u);
    ui_update(&u, NULL, t + 5u);
    t = feed(&u, &d, 1u, t);
    ui_update(&u, NULL, t + 5u);
    t = feed(&u, &up, 2u, t);
    ui_update(&u, NULL, t + 5u);
    CHECK_EQ(drain(&u), 0);
    t = feed(&u, &up, 1u, t);
    CHECK_EQ(drain(&u), 1);

    TEST("H-12 時刻の 32bit の巻き戻りをまたいでも同じ(押下確定の直後に 0xFFFFFFFF → 0)");
    ui_init(&u, 0u);
    t = feed(&u, &d, UI_TAP_DOWN_N, 0xFFFFFFF0u - (UI_TAP_DOWN_N * T10) + T10);   /* 押下確定は 0xFFFFFFF0 + T10 付近 */
    t = feed(&u, &d, 5u, t);
    t = feed(&u, &up, UI_TAP_UP_N, t);
    CHECK(t < 0x100u);                                       /* 巻き戻った */
    CHECK_EQ(drain(&u), 1);

    TEST("H-12 UART の x(ui_tap_inject)はタップと同じイベント(src = UART)。キューは 8 件で、溢れたら捨てて数える");
    ui_init(&u, 0u);
    ui_tap_inject(&u, UI_SRC_UART);
    CHECK(ui_event_pop(&u, &ev));
    CHECK_EQ(ev.type, UI_EV_TAP);
    CHECK_EQ(ev.src, UI_SRC_UART);
    CHECK(!ui_event_pop(&u, &ev));
    for (i = 0u; i < (UI_EV_Q + 3u); i++) { ui_tap_inject(&u, (i < 4u) ? UI_SRC_UART : UI_SRC_TOUCH); }
    CHECK_EQ(u.ev_lost, 3);
    CHECK_EQ(drain(&u), UI_EV_Q);
    ui_tap_inject(&u, UI_SRC_TOUCH);                         /* 取り出した後はまた入る */
    CHECK_EQ(drain(&u), 1);
    CHECK(!ui_event_pop(&u, NULL));
    CHECK(!ui_event_pop(NULL, &ev));

    TEST_REPORT("test_ui_tap");
}
