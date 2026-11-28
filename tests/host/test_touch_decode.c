/* test_touch_decode.c - touch_decode の単体テスト(設計 §14 H8)。標準Cのみ。 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "touch_decode.h"
#include "ui_logic.h"
#include "test_framework.h"

static void fill_out(nav_sample_t *o)
{
    o->pressed = true; o->points = 99u; o->x = 0xAAAAu; o->y = 0x5555u;
}

static void test_known(void)
{
    /* raw = {タッチ数, XH, XL, YH, YL} */
    static const struct {
        uint8_t raw[5]; bool ok; bool pressed; uint8_t pts; uint16_t x; uint16_t y; const char *name;
    } t[] = {
        {{0x00u, 0x00u, 0x00u, 0x00u, 0x00u}, true,  false, 0u, 0u,     0u,     "触れていない"},
        {{0x01u, 0x00u, 0x00u, 0x00u, 0x00u}, true,  true,  1u, 0u,     0u,     "1点 (0,0)"},
        {{0x01u, 0x01u, 0xDFu, 0x01u, 0x0Fu}, true,  true,  1u, 0x1DFu, 0x10Fu, "1点 (479,271)"},
        {{0x01u, 0x00u, 0xEFu, 0x00u, 0x88u}, true,  true,  1u, 0xEFu,  0x88u,  "1点 (239,136)"},
        {{0x02u, 0x00u, 0x10u, 0x00u, 0x20u}, true,  true,  2u, 0x10u,  0x20u,  "2点(点1の座標)"},
        {{0x05u, 0x0Fu, 0xFFu, 0x0Fu, 0xFFu}, true,  true,  5u, 0xFFFu, 0xFFFu, "5点・12bit最大"},
        /* 上位ニブル(イベントフラグ/予約)は座標に入らない */
        {{0x01u, 0xC1u, 0x23u, 0xF4u, 0x56u}, true,  true,  1u, 0x123u, 0x456u, "XH/YH 上位ニブルをマスク"},
        {{0x01u, 0x80u, 0x00u, 0x40u, 0x00u}, true,  true,  1u, 0x000u, 0x000u, "イベントフラグだけ立つ"},
        /* タッチ数の上位ニブルは無視(下位4bitだけ使う) */
        {{0xF1u, 0x00u, 0x10u, 0x00u, 0x20u}, true,  true,  1u, 0x10u,  0x20u,  "raw[0] 上位ニブル無視"},
        {{0x10u, 0x00u, 0x10u, 0x00u, 0x20u}, true,  false, 0u, 0u,     0u,     "raw[0]=0x10 は点数0"},
        /* 異常: 点数 > 5 */
        {{0x06u, 0x00u, 0x10u, 0x00u, 0x20u}, false, false, 0u, 0u,     0u,     "点数6 -> 異常"},
        {{0x07u, 0x00u, 0x10u, 0x00u, 0x20u}, false, false, 0u, 0u,     0u,     "点数7 -> 異常"},
        {{0x0Fu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}, false, false, 0u, 0u,     0u,     "点数15 -> 異常"},
        {{0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}, false, false, 0u, 0u,     0u,     "全ビット1(バス浮き)-> 異常"},
    };
    unsigned i;
    TEST("H8 既知バイト列 -> 期待の (点数, x, y)。点数>5 は false");
    for (i = 0u; i < sizeof(t) / sizeof(t[0]); i++) {
        nav_sample_t o;
        bool r;
        fill_out(&o);
        r = touch_decode(t[i].raw, &o);
        if ((r != t[i].ok) || (o.pressed != t[i].pressed) || (o.points != t[i].pts) ||
            (o.x != t[i].x) || (o.y != t[i].y)) {
            printf("  NG: %s -> ok=%d pressed=%d pts=%u x=0x%X y=0x%X\n", t[i].name, (int)r, (int)o.pressed,
                   (unsigned)o.points, (unsigned)o.x, (unsigned)o.y);
        }
        CHECK(r == t[i].ok);
        CHECK(o.pressed == t[i].pressed);
        CHECK_EQ(o.points, t[i].pts);
        CHECK_EQ(o.x, t[i].x);
        CHECK_EQ(o.y, t[i].y);
    }
}

static void test_all_counts(void)
{
    unsigned n;
    TEST("タッチ数 0〜15 の全値(下位4bit): 0〜5 は true、6〜15 は false");
    for (n = 0u; n < 16u; n++) {
        uint8_t raw[5];
        nav_sample_t o;
        bool r;
        raw[0] = (uint8_t)n; raw[1] = 0x01u; raw[2] = 0x02u; raw[3] = 0x03u; raw[4] = 0x04u;
        fill_out(&o);
        r = touch_decode(raw, &o);
        CHECK(r == (n <= TOUCH_MAX_POINTS));
        if (n == 0u) {
            CHECK(!o.pressed);
        } else if (n <= TOUCH_MAX_POINTS) {
            CHECK(o.pressed);
            CHECK_EQ(o.points, n);
            CHECK_EQ(o.x, 0x102);
            CHECK_EQ(o.y, 0x304);
        } else {
            CHECK(!o.pressed);
            CHECK_EQ(o.points, 0);
        }
    }
}

static void test_null(void)
{
    uint8_t raw[5] = {1u, 0u, 1u, 0u, 1u};
    nav_sample_t o;
    TEST("NULL 引数");
    CHECK(!touch_decode(raw, 0));
    fill_out(&o);
    CHECK(!touch_decode(0, &o));
    CHECK(!o.pressed);
    CHECK_EQ(o.points, 0);
}

static void test_to_ui_integration(void)
{
    /* 異常標本(false)は ui_update に渡さない前提(app_main)。ここでは decode の出力が、画面のどこでもタップ 1 回になることを確認 */
    uint8_t raw[5] = {1u, 0x01u, 0xDFu, 0x00u, 0x10u};   /* x=479(右端) */
    uint8_t none[5] = {0u, 0u, 0u, 0u, 0u};
    nav_sample_t s, r;
    ui_t u;
    ui_ev_t ev;
    unsigned k;
    TEST("decode 出力を ui_update に通す(右端 x=479 の押下 2 標本 + 離し 3 標本 -> UI_EV_TAP 1 回。座標は使わない)");
    ui_init(&u, 0u);
    CHECK(touch_decode(raw, &s));
    CHECK(touch_decode(none, &r));
    CHECK(s.pressed && !r.pressed);
    for (k = 0u; k < UI_TAP_DOWN_N; k++) { ui_update(&u, &s, 1000u + (k * NAV_SAMPLE_MS)); }
    CHECK(!ui_event_pop(&u, &ev));
    for (k = 0u; k < UI_TAP_UP_N; k++) { ui_update(&u, &r, 1100u + (k * NAV_SAMPLE_MS)); }
    CHECK(ui_event_pop(&u, &ev));
    CHECK_EQ(ev.type, UI_EV_TAP);
    CHECK_EQ(ev.src, UI_SRC_TOUCH);
    CHECK(!ui_event_pop(&u, &ev));
}

int main(void)
{
    test_known();
    test_all_counts();
    test_null();
    test_to_ui_integration();
    TEST_REPORT("test_touch_decode");
}
