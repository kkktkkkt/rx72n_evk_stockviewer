/* test_night_core.c - src/app/night_core.c の単体試験(docs/design.md §14.8 H-N1・H-N2。Phase 7a)。標準 C のみ。
 *   窓の判定(日をまたぐ・境界・未同期)、状態機械(窓に入る・出る・未同期・起こし・30 秒の境界・離すまで飲み込む・32bit の巻き戻り・強制)、乱数走行。 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "night_core.h"
#include "test_framework.h"

#define NIGHT_S  APP_NIGHT_START_MIN
#define NIGHT_E  APP_NIGHT_END_MIN

static night_t n;

static void fresh(void) { night_init(&n); }

int main(void)
{
    uint32_t t, t0;
    unsigned i, swallowed_seen_by_ui = 0u;

    TEST("H-N1 窓の判定: 既定 (1380, 360) の境界・昼の窓・start = end・未同期");
    CHECK_EQ(APP_NIGHT_START_MIN, 1380);
    CHECK_EQ(APP_NIGHT_END_MIN, 360);
    CHECK(!night_in_window(1379u, NIGHT_S, NIGHT_E));
    CHECK(night_in_window(1380u, NIGHT_S, NIGHT_E));       /* 23:00 ちょうどは窓の中 */
    CHECK(night_in_window(1439u, NIGHT_S, NIGHT_E));
    CHECK(night_in_window(0u, NIGHT_S, NIGHT_E));
    CHECK(night_in_window(359u, NIGHT_S, NIGHT_E));        /* 05:59 は窓の中 */
    CHECK(!night_in_window(360u, NIGHT_S, NIGHT_E));       /* 06:00 ちょうどは窓の外 */
    CHECK(!night_in_window(720u, NIGHT_S, NIGHT_E));
    CHECK(!night_in_window(59u, 60u, 120u));
    CHECK(night_in_window(60u, 60u, 120u));
    CHECK(night_in_window(119u, 60u, 120u));
    CHECK(!night_in_window(120u, 60u, 120u));
    CHECK(!night_in_window(500u, 500u, 500u));             /* start = end は窓なし */
    CHECK(!night_in_window(0u, 0u, 0u));
    CHECK(!night_in_window(1440u, NIGHT_S, NIGHT_E));      /* 未同期 */
    CHECK(!night_in_window(0xFFFEu, NIGHT_S, NIGHT_E));
    CHECK(!night_in_window(0xFFFFu, NIGHT_S, NIGHT_E));
    CHECK(!night_in_window(0xFFFFu, 60u, 120u));

    TEST("H-N2 ①②③④ 昼は点灯(boot)→ 窓に入る = 消灯(night)→ 窓が終わる = 点灯(day)。未同期のまま窓の時刻 = 点灯のまま");
    fresh();
    CHECK_EQ(n.mode, NIGHT_ON); CHECK_EQ(n.cause, NIGHT_C_BOOT);
    CHECK_EQ(night_level(&n), NIGHT_LEVEL_ON);
    CHECK(!night_update(&n, 10u, 720u));
    CHECK(!night_update(&n, 20u, 0xFFFFu));
    CHECK(!night_update(&n, 30u, 0xFFFFu));                    /* 未同期 */
    CHECK_EQ(n.mode, NIGHT_ON);
    CHECK(night_update(&n, 40u, 1380u));
    CHECK_EQ(n.mode, NIGHT_OFF); CHECK_EQ(n.cause, NIGHT_C_NIGHT); CHECK_EQ(night_level(&n), 0);
    CHECK(!night_update(&n, 50u, 1381u));                                       /* 同じ状態では false */
    CHECK(!night_update(&n, 60u, 359u));
    CHECK(night_update(&n, 70u, 360u));
    CHECK_EQ(n.mode, NIGHT_ON); CHECK_EQ(n.cause, NIGHT_C_DAY);
    CHECK(night_update(&n, 80u, 1380u));
    CHECK(night_update(&n, 90u, 0xFFFFu));                                      /* 一度同期した後に未同期に戻る = 点灯(day) */
    CHECK_EQ(n.mode, NIGHT_ON); CHECK_EQ(n.cause, NIGHT_C_DAY);
    CHECK_EQ(strcmp(night_mode_name(NIGHT_ON), "on"), 0);
    CHECK_EQ(strcmp(night_mode_name(NIGHT_OFF), "off"), 0);
    CHECK_EQ(strcmp(night_cause_name(NIGHT_C_TIMEOUT), "timeout"), 0);

    TEST("H-N2 ⑤⑨ 消灯中の押下: 渡さない・点灯(touch)・押している間は渡さない・離した後の次の押下は渡す。消灯に戻った直後の押下はまた飲み込む");
    fresh();
    (void)night_update(&n, 0u, 1400u);
    CHECK_EQ(n.mode, NIGHT_OFF);
    CHECK(!night_filter(&n, true, 1000u));
    CHECK(night_update(&n, 1000u, 1400u));
    CHECK_EQ(n.mode, NIGHT_ON); CHECK_EQ(n.cause, NIGHT_C_TOUCH);
    CHECK(!night_filter(&n, true, 1010u));                                      /* 押している間 */
    CHECK(!night_filter(&n, true, 1020u));
    CHECK(!night_update(&n, 1020u, 1400u));
    CHECK(night_filter(&n, false, 1030u));                                      /* 離した(標本は渡してよい。押下は渡っていない) */
    CHECK(night_filter(&n, true, 1500u));                                       /* 次の押下は点灯中なので渡す */
    CHECK(night_filter(&n, true, 1510u));
    CHECK(night_filter(&n, false, 1520u));
    CHECK(!night_update(&n, 1520u, 1400u));
    /* ⑥ 30 秒: 最後の押下 1510 から 29,999 は点灯のまま、30,000 で消灯(timeout) */
    CHECK(!night_update(&n, 1510u + 29999u, 1400u));
    CHECK_EQ(n.mode, NIGHT_ON);
    CHECK(night_update(&n, 1510u + 30000u, 1400u));
    CHECK_EQ(n.mode, NIGHT_OFF); CHECK_EQ(n.cause, NIGHT_C_TIMEOUT);
    CHECK(!night_filter(&n, true, 40000u));                                     /* ⑨ 消灯に戻った直後もまた飲み込む */
    CHECK(night_update(&n, 40000u, 1400u));
    CHECK_EQ(n.cause, NIGHT_C_TOUCH);

    TEST("H-N2 ⑥ 点灯中の押下(true)で 30 秒が延びる。触れ続けている間は消えない(30 秒を超えても)");
    t0 = 100000u;
    CHECK(night_filter(&n, false, t0));
    CHECK(night_filter(&n, true, t0 + 20000u));                                 /* 40000 で起こした後の押下 */
    CHECK(!night_update(&n, t0 + 20000u + 29999u, 1400u));
    for (t = t0 + 21000u; t < t0 + 120000u; t += 1000u) { (void)night_filter(&n, true, t); (void)night_update(&n, t, 1400u); }
    CHECK_EQ(n.mode, NIGHT_ON);
    (void)night_filter(&n, false, t0 + 120000u);                                /* 最後の押下は t0 + 119000 */
    CHECK(!night_update(&n, t0 + 119000u + 29999u, 1400u));
    CHECK(night_update(&n, t0 + 119000u + 30000u, 1400u));
    CHECK_EQ(n.mode, NIGHT_OFF);

    TEST("H-N2 ⑦ 32bit の now_ms の巻き戻りをまたいでも 30 秒の境界は同じ");
    fresh();
    (void)night_update(&n, 0xFFFFFFFFu - 100000u, 1400u);
    t = 0xFFFFFFFFu - 10000u;
    CHECK(!night_filter(&n, true, t));
    CHECK(night_update(&n, t, 1400u));
    CHECK(!night_update(&n, t + 29999u, 1400u));                                /* 巻き戻りの後 */
    CHECK_EQ(n.mode, NIGHT_ON);
    CHECK(night_update(&n, t + 30000u, 1400u));
    CHECK_EQ(n.mode, NIGHT_OFF);

    TEST("H-N2 ⑧ 起こしている間に窓が終わる = 点灯のまま(cause = day)。昼に起こしても何も変わらない。窓に入ったあとは 30 秒後に消灯");
    fresh();
    (void)night_update(&n, 0u, 1400u);
    CHECK(!night_filter(&n, true, 1000u));
    (void)night_update(&n, 1000u, 1400u);
    CHECK(!night_update(&n, 5000u, 360u));                                      /* 窓が終わった: 点灯のまま(false) */
    CHECK_EQ(n.mode, NIGHT_ON);
    CHECK(!night_update(&n, 1000u + 30000u, 360u));                             /* 30 秒後も点灯のまま */
    CHECK_EQ(n.mode, NIGHT_ON); CHECK_EQ(n.cause, NIGHT_C_DAY);
    CHECK(night_filter(&n, false, 49000u));                                     /* 起こした指は離れた */
    CHECK(night_filter(&n, true, 50000u));                                      /* 昼の押下は渡す */
    (void)night_filter(&n, false, 50010u);
    CHECK(night_update(&n, 60000u, 1380u));                                     /* 窓に入る */
    CHECK_EQ(n.mode, NIGHT_OFF);
    fresh();                                                                    /* 昼に起こした(押下)あと窓に入る → 30 秒後に消灯 */
    CHECK(night_filter(&n, true, 1000u));                                       /* 点灯中なので awake にならず渡す */
    CHECK_EQ(n.awake, 0);
    (void)night_filter(&n, false, 1010u);
    CHECK(night_update(&n, 2000u, 1380u));
    CHECK_EQ(n.mode, NIGHT_OFF);
    fresh();
    (void)night_update(&n, 0u, 700u);
    CHECK(night_filter(&n, true, 100u));
    (void)night_filter(&n, false, 110u);
    CHECK(night_update(&n, 200u, 1380u));                                       /* 昼のタッチの 100ms 後に窓に入る */
    CHECK_EQ(n.cause, NIGHT_C_NIGHT);

    TEST("H-N2 ⑩ b の巡回(自動 → 消灯 → 点灯 → 自動)。強制中は窓を見ない。強制の消灯でもタッチで 30 秒点灯");
    fresh();
    (void)night_update(&n, 0u, 720u);
    night_force_next(&n);                                                       /* 消灯を強制(昼) */
    CHECK(night_update(&n, 10u, 720u));
    CHECK_EQ(n.mode, NIGHT_OFF); CHECK_EQ(n.cause, NIGHT_C_UART); CHECK_EQ(n.force, 1);
    CHECK(!night_update(&n, 20u, 720u));
    CHECK(!night_filter(&n, true, 1000u));                                      /* 強制の消灯でもタッチで起きる */
    CHECK(night_update(&n, 1000u, 720u));
    CHECK_EQ(n.cause, NIGHT_C_TOUCH);
    (void)night_filter(&n, false, 1010u);
    CHECK(night_update(&n, 1000u + 30000u, 720u));
    CHECK_EQ(n.mode, NIGHT_OFF); CHECK_EQ(n.cause, NIGHT_C_TIMEOUT);
    night_force_next(&n);                                                       /* 点灯を強制 */
    CHECK(night_update(&n, 40000u, 1400u));
    CHECK_EQ(n.mode, NIGHT_ON); CHECK_EQ(n.cause, NIGHT_C_UART); CHECK_EQ(n.force, 3);
    CHECK(!night_update(&n, 50000u, 1400u));                                    /* 窓の中でも点灯を強制中は点灯 */
    night_force_next(&n);                                                       /* 自動 → 窓の中 = 消灯 */
    CHECK(night_update(&n, 60000u, 1400u));
    CHECK_EQ(n.mode, NIGHT_OFF); CHECK_EQ(n.cause, NIGHT_C_UART); CHECK_EQ(n.force, 0);
    night_force_next(&n); night_force_next(&n); night_force_next(&n);           /* 1 巡 = 元の状態。途中の update なし */
    CHECK(!night_update(&n, 70000u, 1400u));
    CHECK_EQ(n.force, 0); CHECK_EQ(n.mode, NIGHT_OFF);
    fresh();
    (void)night_update(&n, 0u, 720u);
    night_force_next(&n); (void)night_update(&n, 10u, 720u);
    night_force_next(&n); (void)night_update(&n, 20u, 720u);
    night_force_next(&n);                                                       /* 点灯 → 自動(昼)= 変わらない */
    CHECK(!night_update(&n, 30u, 720u));
    CHECK_EQ(n.mode, NIGHT_ON);
    CHECK_EQ(n.uart, 0);                                                        /* 変わらなくても uart の印は消える */
    CHECK(night_update(&n, 40u, 1400u));
    CHECK_EQ(n.cause, NIGHT_C_NIGHT);                                           /* 次の変化は uart でなく night */

    TEST("H-N2 ⑫ 乱数走行: 窓・時刻・押下・時間をランダムに進め、消灯中に渡った押下が 0、状態が壊れない");
    srand(12345);
    for (i = 0u; i < 400000u; i++) {
        static uint32_t now = 0xFFFF0000u;
        static uint16_t clk = 0u;
        static bool pressed = false;
        bool pass;
        bool dark_before;
        unsigned r = (unsigned)rand();

        if (i == 0u) { fresh(); }
        now += (r % 7u == 0u) ? (uint32_t)(rand() % 20000) : 10u;
        if (r % 97u == 0u) { clk = (uint16_t)(rand() % 1500) ; if (clk >= 1440u) { clk = 0xFFFFu; } }
        if (r % 53u == 0u) { pressed = !pressed; }
        if (r % 1009u == 0u) { night_force_next(&n); }
        dark_before = (n.mode == (uint8_t)NIGHT_OFF) && (n.awake == 0u);
        pass = night_filter(&n, pressed, now);
        if (pressed && dark_before && pass) { swallowed_seen_by_ui++; }          /* 消灯中の最初の押下が渡った = 違反 */
        (void)night_update(&n, now, clk);
        CHECK((n.mode == (uint8_t)NIGHT_ON) || (n.mode == (uint8_t)NIGHT_OFF));
        CHECK(!((n.mode == (uint8_t)NIGHT_OFF) && (n.awake != 0u)));            /* 起きている間は点灯 */
    }
    CHECK_EQ(swallowed_seen_by_ui, 0);

    TEST_REPORT("test_night_core");
}
