/* test_nav_step.c - nav_step()(UART コマンドの画像切替)の単体テスト。設計 §3.3 / U-H8。標準Cのみ。
 * H1〜H7(nav_update)は test_nav.c(S2)。ここでは「nav_step が nav_update の挙動を変えない」ことを確かめる。 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "nav_logic.h"
#include "test_framework.h"

static nav_sample_t smp(uint8_t pts, uint16_t x)
{
    nav_sample_t s;
    s.pressed = (pts > 0u); s.points = pts; s.x = x; s.y = 100u;
    return s;
}

int main(void)
{
    nav_t nav;
    unsigned i;

    TEST("NS1 3枚: +1 は 0→1→2→0、-1 は 0→2→1→0");
    nav_init(&nav, 3u, 479u);
    CHECK(nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 1);
    CHECK(nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 2);
    CHECK(nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 0);
    CHECK(nav_step(&nav, -1)); CHECK_EQ(nav_index(&nav), 2);
    CHECK(nav_step(&nav, -1)); CHECK_EQ(nav_index(&nav), 1);
    CHECK(nav_step(&nav, -1)); CHECK_EQ(nav_index(&nav), 0);

    TEST("NS1b 4枚: +1 は 0→1→2→3→0、-1 は 0→3→2→1→0");
    nav_init(&nav, 4u, 479u);
    CHECK(nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 1);
    CHECK(nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 2);
    CHECK(nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 3);
    CHECK(nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 0);
    CHECK(nav_step(&nav, -1)); CHECK_EQ(nav_index(&nav), 3);
    CHECK(nav_step(&nav, -1)); CHECK_EQ(nav_index(&nav), 2);
    CHECK(nav_step(&nav, -1)); CHECK_EQ(nav_index(&nav), 1);
    CHECK(nav_step(&nav, -1)); CHECK_EQ(nav_index(&nav), 0);

    TEST("NS2 dir の値: 正なら次、負なら前(大きさは無関係)。0 は何もしない");
    nav_init(&nav, 3u, 479u);
    CHECK(nav_step(&nav, 127));  CHECK_EQ(nav_index(&nav), 1);
    CHECK(nav_step(&nav, -128)); CHECK_EQ(nav_index(&nav), 0);
    CHECK(nav_step(&nav, 2));    CHECK_EQ(nav_index(&nav), 1);
    CHECK(!nav_step(&nav, 0));   CHECK_EQ(nav_index(&nav), 1);

    TEST("NS3 画像1枚・0枚指定・NULL は false");
    nav_init(&nav, 1u, 479u);
    CHECK(!nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 0);
    CHECK(!nav_step(&nav, -1)); CHECK_EQ(nav_index(&nav), 0);
    nav_init(&nav, 0u, 479u);                          /* 0 枚指定は 1 枚として扱う */
    CHECK(!nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 0);
    CHECK(!nav_step(NULL, 1));
    CHECK(!nav_step(NULL, -1));
    CHECK(!nav_step(NULL, 0));

    TEST("NS4 2枚・255枚の循環");
    nav_init(&nav, 2u, 479u);
    CHECK(nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 1);
    CHECK(nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 0);
    CHECK(nav_step(&nav, -1)); CHECK_EQ(nav_index(&nav), 1);
    nav_init(&nav, 255u, 479u);
    CHECK(nav_step(&nav, -1)); CHECK_EQ(nav_index(&nav), 254);
    CHECK(nav_step(&nav, 1));  CHECK_EQ(nav_index(&nav), 0);
    for (i = 0u; i < 255u; i++) { CHECK(nav_step(&nav, 1)); CHECK(nav_index(&nav) < 255u); }
    CHECK_EQ(nav_index(&nav), 0);

    TEST("NS5 nav_step は index 以外のフィールドを変えない(状態機械・間隔規則に触らない)");
    {
        nav_t a, b;
        nav_sample_t s;
        nav_init(&a, 3u, 479u);
        /* 右半分を押し続けて HELD、動作済み(has_action=true, last_action_ms=...)にする */
        s = smp(1u, 400u);
        for (i = 0u; i < 4u; i++) { (void)nav_update(&a, &s, 1000u + 10u * i); }
        CHECK_EQ(a.state, NAV_ST_HELD);
        CHECK(a.has_action);
        b = a;
        CHECK(nav_step(&b, 1));
        b.index = a.index;                              /* index だけ戻して残りを全比較 */
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        /* 各状態(IDLE / PRESS_PENDING / HELD / RELEASE_PENDING)でも同じ */
        nav_init(&a, 3u, 479u);
        b = a; CHECK(nav_step(&b, -1)); b.index = a.index; CHECK(memcmp(&a, &b, sizeof(a)) == 0);       /* IDLE */
        s = smp(1u, 400u);
        (void)nav_update(&a, &s, 2000u);
        CHECK_EQ(a.state, NAV_ST_PRESS_PENDING);
        b = a; CHECK(nav_step(&b, 1)); b.index = a.index; CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        s = smp(0u, 0u);
        for (i = 0u; i < 3u; i++) { (void)nav_update(&a, &s, 3000u + 10u * i); }
        b = a; CHECK(nav_step(&b, 1)); b.index = a.index; CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }

    TEST("NS6 300ms 規則は nav_step に適用されない / タッチの 300ms 規則は nav_step の影響を受けない");
    {
        nav_t n2;
        nav_sample_t s = smp(1u, 400u);
        unsigned changes = 0u;
        nav_init(&n2, 3u, 479u);
        /* タッチで 1 回動作(押下確定) */
        for (i = 0u; i < 3u; i++) { if (nav_update(&n2, &s, 5000u + 10u * i)) { changes++; } }
        CHECK_EQ(changes, 1);
        CHECK_EQ(nav_index(&n2), 1);
        /* 直後(300ms 以内)でも nav_step は効く */
        CHECK(nav_step(&n2, 1));
        CHECK_EQ(nav_index(&n2), 2);
        CHECK(nav_step(&n2, 1));
        CHECK_EQ(nav_index(&n2), 0);
        /* 一度離して、300ms 以内に再押下: タッチは(nav_step があっても)まだ動作しない */
        s = smp(0u, 0u);
        for (i = 0u; i < 3u; i++) { (void)nav_update(&n2, &s, 5030u + 10u * i); }
        s = smp(1u, 400u);
        changes = 0u;
        for (i = 0u; i < 3u; i++) { if (nav_update(&n2, &s, 5060u + 10u * i)) { changes++; } }
        CHECK_EQ(changes, 0);                           /* 5000 から 300ms 未満 */
        CHECK_EQ(nav_index(&n2), 0);
        /* 300ms 経過後は動作し、nav_step で動かした index から続く(0 → 1) */
        s = smp(0u, 0u);
        for (i = 0u; i < 3u; i++) { (void)nav_update(&n2, &s, 5100u + 10u * i); }
        s = smp(1u, 400u);
        changes = 0u;
        for (i = 0u; i < 3u; i++) { if (nav_update(&n2, &s, 5400u + 10u * i)) { changes++; } }
        CHECK_EQ(changes, 1);
        CHECK_EQ(nav_index(&n2), 1);
    }

    TEST("NS7 差分試験: +1 と -1 を毎回挟んでも nav_update の戻り値・index が変わらない(乱数列 20000 標本)");
    {
        nav_t a, b;
        uint32_t rng = 777u, now = 0xFFFFF000u;         /* 時刻の折り返しも通す */
        unsigned k, mism = 0u, changes = 0u;
        nav_init(&a, 3u, 479u);
        nav_init(&b, 3u, 479u);
        for (k = 0u; k < 20000u; k++) {
            nav_sample_t s;
            bool ra, rb;
            unsigned r;
            rng = rng * 1664525u + 1013904223u;
            r = (rng >> 16) & 0xFFu;
            if (r < 100u)      { s = smp(1u, (uint16_t)((rng >> 4) % 480u)); }
            else if (r < 110u) { s = smp(2u, (uint16_t)((rng >> 4) % 480u)); }
            else               { s = smp(0u, 0u); }
            now += 10u;
            if ((r & 1u) != 0u) { (void)nav_step(&b, 1); (void)nav_step(&b, -1); }
            else                { (void)nav_step(&b, -1); (void)nav_step(&b, 1); }
            ra = nav_update(&a, &s, now);
            rb = nav_update(&b, &s, now);
            if ((ra != rb) || (nav_index(&a) != nav_index(&b)) || (memcmp(&a, &b, sizeof(a)) != 0)) { mism++; }
            if (ra) { changes++; }
        }
        CHECK_EQ(mism, 0);
        CHECK(changes > 10u);                           /* 試験が空振りでない(動作が何度か起きている) */
        printf("  (参考) nav_update の動作回数 = %u\n", changes);
    }

    TEST_REPORT("test_nav_step");
}
