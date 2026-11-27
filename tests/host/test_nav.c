/* test_nav.c - nav_logic の単体テスト(設計 §14 H1〜H7)。標準Cのみ。
 * -DNAV_MODE=1 でビルドすると「どこでも次へ」モードの試験になる(test_nav_next)。 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "nav_logic.h"
#include "test_framework.h"

#define N_IMG 3u
#define XMAX  479u

/* 既知の制約: nav_logic は IDLE -> PRESS_PENDING で 1標本を消費し、確定判定は次の標本から行う。
 * このため NAV_PRESS_CONFIRM を 1 にしても 2標本必要なまま(docs/test-report.md の差し戻し #1)。
 * 1 に変えたくなったら nav_logic.c の修正が要る。変更を検知するためにここで固定する(C99互換の静的検査)。 */
typedef char nav_press_confirm_must_be_ge_2[(NAV_PRESS_CONFIRM >= 2u) ? 1 : -1];

typedef struct {
    nav_t    nav;
    uint32_t now;
    unsigned changes;
} ctx_t;

static void ctx_init(ctx_t *c, uint8_t n, uint16_t xmax, uint32_t start)
{
    nav_init(&c->nav, n, xmax);
    c->now     = start;
    c->changes = 0u;
}

static nav_sample_t smp(uint8_t pts, uint16_t x)
{
    nav_sample_t s;
    s.pressed = (pts > 0u);
    s.points  = pts;
    s.x       = x;
    s.y       = 100u;
    return s;
}

/* 1標本を現在時刻で渡し、時刻を 10ms 進める */
static bool step(ctx_t *c, uint8_t pts, uint16_t x)
{
    nav_sample_t s  = smp(pts, x);
    bool         ch = nav_update(&c->nav, &s, c->now);
    c->now += NAV_SAMPLE_MS;
    if (ch) {
        c->changes++;
    }
    return ch;
}

/* n 標本ぶん同じ入力。変化回数を返す */
static unsigned run(ctx_t *c, unsigned n, uint8_t pts, uint16_t x)
{
    unsigned k = 0u;
    while (n-- > 0u) {
        if (step(c, pts, x)) {
            k++;
        }
    }
    return k;
}

/* 「タップ」= 1点を press_n 標本、その後 離し 3標本 + 余裕 */
static unsigned tap(ctx_t *c, unsigned press_n, uint16_t x)
{
    unsigned k = run(c, press_n, 1u, x);
    k += run(c, 3u, 0u, 0u);
    return k;
}

/* 300ms 経過させる(離したまま) */
static void idle_ms(ctx_t *c, uint32_t ms)
{
    unsigned n = (unsigned)(ms / NAV_SAMPLE_MS);
    (void)run(c, n, 0u, 0u);
}

#if (NAV_MODE == NAV_MODE_HALVES)

static void test_h1_right(void)
{
    ctx_t c;
    TEST("H1 右半分を2標本(20ms)触れると +1");
    ctx_init(&c, N_IMG, XMAX, 1000u);
    CHECK(!step(&c, 1u, 400u));            /* 1標本目では動かない */
    CHECK_EQ(nav_index(&c.nav), 0);
    CHECK(step(&c, 1u, 400u));             /* 2標本目で動く */
    CHECK_EQ(nav_index(&c.nav), 1);
    CHECK_EQ(c.changes, 1);
}

static void test_h1_boundary(void)
{
    /* x_max=479 -> x_max/2=239。x<239 が左、x>=239 が右 */
    static const struct { uint16_t x; uint8_t expect_idx; } t[] = {
        {0u, 2u}, {1u, 2u}, {100u, 2u}, {237u, 2u}, {238u, 2u},
        {239u, 1u}, {240u, 1u}, {400u, 1u}, {479u, 1u}, {480u, 1u}, {4095u, 1u}, {65535u, 1u}
    };
    unsigned i;
    TEST("H1/H2 左右判定の境界(x_max=479 -> 238 が左, 239 が右)");
    for (i = 0u; i < sizeof(t) / sizeof(t[0]); i++) {
        ctx_t c;
        ctx_init(&c, N_IMG, XMAX, 1000u);
        (void)run(&c, 2u, 1u, t[i].x);
        if (nav_index(&c.nav) != t[i].expect_idx) {
            printf("  x=%u -> index %u (expected %u)\n", (unsigned)t[i].x,
                   (unsigned)nav_index(&c.nav), (unsigned)t[i].expect_idx);
        }
        CHECK_EQ(nav_index(&c.nav), t[i].expect_idx);
    }
}

static void test_h1_boundary_other_xmax(void)
{
    ctx_t c;
    TEST("左右判定: x_max が偶数/奇数/極小のとき");
    /* x_max=480 -> 240。239 が左、240 が右 */
    ctx_init(&c, N_IMG, 480u, 1000u); (void)run(&c, 2u, 1u, 239u); CHECK_EQ(nav_index(&c.nav), 2);
    ctx_init(&c, N_IMG, 480u, 1000u); (void)run(&c, 2u, 1u, 240u); CHECK_EQ(nav_index(&c.nav), 1);
    /* x_max=1 -> 0。x<0 は無いので常に右 */
    ctx_init(&c, N_IMG, 1u, 1000u);   (void)run(&c, 2u, 1u, 0u);   CHECK_EQ(nav_index(&c.nav), 1);
    /* x_max=0 -> 同上 */
    ctx_init(&c, N_IMG, 0u, 1000u);   (void)run(&c, 2u, 1u, 0u);   CHECK_EQ(nav_index(&c.nav), 1);
    /* x_max=4095(座標が12bit満幅のとき) -> 2047。1000 が左、3000 が右 */
    ctx_init(&c, N_IMG, 4095u, 1000u); (void)run(&c, 2u, 1u, 1000u); CHECK_EQ(nav_index(&c.nav), 2);
    ctx_init(&c, N_IMG, 4095u, 1000u); (void)run(&c, 2u, 1u, 3000u); CHECK_EQ(nav_index(&c.nav), 1);
}

static void test_h2_cycle(void)
{
    ctx_t c;
    unsigned i;
    TEST("H2 左で -1、端で循環(0 の左 -> 2、2 の右 -> 0)");
    ctx_init(&c, N_IMG, XMAX, 1000u);
    (void)tap(&c, 2u, 100u);                      /* 0 -> 2 */
    CHECK_EQ(nav_index(&c.nav), 2);
    idle_ms(&c, 300u);
    (void)tap(&c, 2u, 100u);                      /* 2 -> 1 */
    CHECK_EQ(nav_index(&c.nav), 1);
    idle_ms(&c, 300u);
    (void)tap(&c, 2u, 100u);                      /* 1 -> 0 */
    CHECK_EQ(nav_index(&c.nav), 0);
    idle_ms(&c, 300u);
    (void)tap(&c, 2u, 400u);                      /* 0 -> 1 */
    CHECK_EQ(nav_index(&c.nav), 1);
    idle_ms(&c, 300u);
    (void)tap(&c, 2u, 400u);                      /* 1 -> 2 */
    CHECK_EQ(nav_index(&c.nav), 2);
    idle_ms(&c, 300u);
    (void)tap(&c, 2u, 400u);                      /* 2 -> 0 (循環) */
    CHECK_EQ(nav_index(&c.nav), 0);

    /* 3回ずつで元に戻る */
    ctx_init(&c, N_IMG, XMAX, 1000u);
    for (i = 0u; i < 3u; i++) { (void)tap(&c, 2u, 400u); idle_ms(&c, 300u); }
    CHECK_EQ(nav_index(&c.nav), 0);
    for (i = 0u; i < 3u; i++) { (void)tap(&c, 2u, 100u); idle_ms(&c, 300u); }
    CHECK_EQ(nav_index(&c.nav), 0);
}

static void test_image_count_edge(void)
{
    ctx_t c;
    TEST("画像枚数 1 / 0 / 2 / 255 の端");
    ctx_init(&c, 1u, XMAX, 1000u);
    CHECK_EQ(run(&c, 2u, 1u, 400u), 0);           /* 1枚: 変化なし(false) */
    CHECK_EQ(nav_index(&c.nav), 0);
    ctx_init(&c, 0u, XMAX, 1000u);                /* 0枚 -> 1枚扱い */
    CHECK_EQ(run(&c, 2u, 1u, 400u), 0);
    CHECK_EQ(nav_index(&c.nav), 0);
    ctx_init(&c, 2u, XMAX, 1000u);                /* 2枚: 左右どちらでも +-1 = 反対側 */
    (void)tap(&c, 2u, 100u);
    CHECK_EQ(nav_index(&c.nav), 1);
    idle_ms(&c, 300u);
    (void)tap(&c, 2u, 400u);
    CHECK_EQ(nav_index(&c.nav), 0);
    ctx_init(&c, 255u, XMAX, 1000u);              /* uint8_t の上限: 0 の左 -> 254 */
    (void)tap(&c, 2u, 100u);
    CHECK_EQ(nav_index(&c.nav), 254);
    idle_ms(&c, 300u);
    (void)tap(&c, 2u, 400u);                      /* 254 の右 -> 0 */
    CHECK_EQ(nav_index(&c.nav), 0);
}

static void test_x_uses_latest_sample(void)
{
    ctx_t c;
    TEST("押下確定待ちの間にXが動いた場合は確定時(最新標本)のXを使う(設計に明記なし: 現状の挙動の記録)");
    ctx_init(&c, N_IMG, XMAX, 1000u);
    (void)step(&c, 1u, 100u);                     /* 左 */
    (void)step(&c, 1u, 400u);                     /* 右 */
    CHECK_EQ(nav_index(&c.nav), 1);
}

#endif /* NAV_MODE_HALVES */

static void test_h3_noise(void)
{
    ctx_t c;
    unsigned i;
    TEST("H3 押下1標本だけ(ノイズ)では動作しない");
    ctx_init(&c, N_IMG, XMAX, 1000u);
    CHECK(!step(&c, 1u, 400u));
    (void)run(&c, 5u, 0u, 0u);
    CHECK_EQ(c.changes, 0);
    CHECK_EQ(nav_index(&c.nav), 0);

    /* 触れる/離すの交互(2標本連続にならない) */
    for (i = 0u; i < 100u; i++) {
        (void)step(&c, 1u, 400u);
        (void)step(&c, 0u, 0u);
    }
    CHECK_EQ(c.changes, 0);
    CHECK_EQ(nav_index(&c.nav), 0);

    /* 2標本触れてから 1標本だけ抜け(離し確定前)、再び触れても追加動作しない */
    ctx_init(&c, N_IMG, XMAX, 1000u);
    (void)run(&c, 2u, 1u, 400u);
    CHECK_EQ(c.changes, 1);
    (void)step(&c, 0u, 0u);
    (void)run(&c, 5u, 1u, 400u);
    CHECK_EQ(c.changes, 1);
}

static void test_h4_hold(void)
{
    ctx_t c;
    TEST("H4 押しっぱなし(5秒 = 500標本)で動作は1回だけ");
    ctx_init(&c, N_IMG, XMAX, 1000u);
    CHECK_EQ(run(&c, 500u, 1u, 400u), 1);
    CHECK_EQ(nav_index(&c.nav), 1);
    /* 離して 300ms 以上あけて再度押せば再び動く */
    (void)run(&c, 3u, 0u, 0u);
    idle_ms(&c, 300u);
    CHECK_EQ(run(&c, 500u, 1u, 400u), 1);
    CHECK_EQ(nav_index(&c.nav), 2);
}

/* 1回動作させ、離し3標本のあと、dt ms 後に押下確定する。2回目が動いたか */
static bool second_press_acts(uint32_t a, uint32_t dt, bool *first_ok)
{
    nav_t        n;
    nav_sample_t t = smp(1u, 400u);
    nav_sample_t u = smp(0u, 0u);
    bool         f;

    nav_init(&n, N_IMG, XMAX);
    (void)nav_update(&n, &t, a - 10u);
    f = nav_update(&n, &t, a);                       /* 最初の動作: 時刻 a */
    if (first_ok != 0) {
        *first_ok = f;
    }
    (void)nav_update(&n, &u, a + 10u);
    (void)nav_update(&n, &u, a + 20u);
    (void)nav_update(&n, &u, a + 30u);
    (void)nav_update(&n, &t, a + dt - 10u);
    return nav_update(&n, &t, a + dt);               /* 2回目の押下確定: 時刻 a+dt */
}

static void test_h5_interval(void)
{
    static const uint32_t dts[]  = {50u, 100u, 200u, 290u, 299u, 300u, 301u, 400u, 1000u};
    static const uint32_t base[] = {5000u, 0u, 20u, 0x7FFFFFF0u, 0xFFFFFF00u, 0xFFFFFFF0u, 0xFFFFFFFFu, 0xFFFFFE00u};
    unsigned i, j;
    TEST("H5/H7 300ms 間隔(299 は無視、300 は動作)と uint32 一周をまたぐ場合");
    for (j = 0u; j < sizeof(base) / sizeof(base[0]); j++) {
        for (i = 0u; i < sizeof(dts) / sizeof(dts[0]); i++) {
            bool first = false;
            bool acts  = second_press_acts(base[j], dts[i], &first);
            bool want  = (dts[i] >= NAV_MIN_INTERVAL_MS);
            if (!first || (acts != want)) {
                printf("  base=0x%08X dt=%u first=%d acts=%d want=%d\n", (unsigned)base[j],
                       (unsigned)dts[i], (int)first, (int)acts, (int)want);
            }
            CHECK(first);
            CHECK(acts == want);
        }
    }
}

static void test_h5_wording_gap(void)
{
    ctx_t c;
    TEST("記録: 設計 §11.1(動作から300ms)と §14 H5(離しから300ms)の文言差。実装は §11.1(動作から)");
    ctx_init(&c, N_IMG, XMAX, 1000u);
    CHECK_EQ(run(&c, 100u, 1u, 400u), 1);         /* 1秒押し続ける(動作は最初の1回) */
    (void)run(&c, 3u, 0u, 0u);                    /* 離し確定の最短(30ms) */
    /* 離してから 50ms 程度で再押下。§11.1 では動作から 1秒以上経過なので動く。H5 の文言どおり(離しから300ms)なら無視される */
    CHECK_EQ(run(&c, 2u, 1u, 400u), 1);
    CHECK_EQ(nav_index(&c.nav), 2);
}

static void test_first_press_early(void)
{
    ctx_t c;
    TEST("起動直後(now_ms < 300)の最初の押下は動作する(前回動作なし)");
    ctx_init(&c, N_IMG, XMAX, 0u);
    CHECK_EQ(run(&c, 2u, 1u, 400u), 1);
    CHECK_EQ(nav_index(&c.nav), 1);
}

static void test_ignored_press_stays_ignored(void)
{
    ctx_t c;
    TEST("300ms 以内で無視された押下は、押し続けて 300ms を過ぎても動作しない");
    ctx_init(&c, N_IMG, XMAX, 1000u);
    CHECK_EQ(tap(&c, 2u, 400u), 1);               /* 動作 */
    CHECK_EQ(run(&c, 2u, 1u, 400u), 0);           /* すぐ再押下: 無視 */
    CHECK_EQ(run(&c, 100u, 1u, 400u), 0);         /* 1秒押し続けても動かない */
    CHECK_EQ(nav_index(&c.nav), 1);
}

static void test_h6_multi(void)
{
    ctx_t c;
    TEST("H6 2点以上を含む押下は動作しない。全部離れて30ms後に再び動作可能");

    /* (a) 最初から2点 */
    ctx_init(&c, N_IMG, XMAX, 1000u);
    CHECK_EQ(run(&c, 20u, 2u, 400u), 0);
    CHECK_EQ(nav_index(&c.nav), 0);
    (void)run(&c, 3u, 0u, 0u);                    /* 離し確定(30ms) */
    CHECK_EQ(run(&c, 2u, 1u, 400u), 1);           /* その後の単点で動く */
    CHECK_EQ(nav_index(&c.nav), 1);

    /* (b) 1点 -> 2点 -> 片指だけ残る -> 全部離す */
    ctx_init(&c, N_IMG, XMAX, 1000u);
    (void)step(&c, 1u, 400u);
    CHECK_EQ(run(&c, 5u, 2u, 400u), 0);
    CHECK_EQ(run(&c, 30u, 1u, 400u), 0);          /* 片指が残っても動かない */
    (void)run(&c, 3u, 0u, 0u);
    CHECK_EQ(run(&c, 2u, 1u, 400u), 1);
    CHECK_EQ(nav_index(&c.nav), 1);

    /* (c) 動作済みの押下中に2本目を置いても追加動作しない */
    ctx_init(&c, N_IMG, XMAX, 1000u);
    CHECK_EQ(run(&c, 2u, 1u, 400u), 1);
    CHECK_EQ(run(&c, 10u, 2u, 400u), 0);
    CHECK_EQ(run(&c, 10u, 1u, 400u), 0);
    CHECK_EQ(nav_index(&c.nav), 1);

    /* (d) 点数が極端な値(5, 6, 255)でも多点扱い */
    ctx_init(&c, N_IMG, XMAX, 1000u);
    CHECK_EQ(run(&c, 5u, 5u, 400u), 0);
    CHECK_EQ(run(&c, 5u, 6u, 400u), 0);
    CHECK_EQ(run(&c, 5u, 255u, 400u), 0);
    CHECK_EQ(nav_index(&c.nav), 0);

    /* (e) 離しは3標本連続が必要: 2標本だけ離して再び触れても「押下中」のまま(動作しない) */
    ctx_init(&c, N_IMG, XMAX, 1000u);
    CHECK_EQ(run(&c, 2u, 1u, 400u), 1);
    (void)run(&c, 40u, 1u, 400u);                 /* 押し続けて 300ms 以上経過させる */
    (void)run(&c, 2u, 0u, 0u);                    /* 離し 2標本 */
    CHECK_EQ(run(&c, 5u, 1u, 400u), 0);           /* 離し確定前に触れた -> 押下中継続 */
    /* 離し 3標本以上なら動く */
    (void)run(&c, 3u, 0u, 0u);
    CHECK_EQ(run(&c, 2u, 1u, 400u), 1);
}

static void test_flags_inconsistent(void)
{
    nav_t        n;
    nav_sample_t s;
    TEST("pressed と points が食い違う標本 / NULL 引数");
    nav_init(&n, N_IMG, XMAX);
    s.pressed = true;  s.points = 0u; s.x = 400u; s.y = 0u;
    CHECK(!nav_update(&n, &s, 1000u));
    CHECK(!nav_update(&n, &s, 1010u));
    CHECK(!nav_update(&n, &s, 1020u));
    CHECK_EQ(nav_index(&n), 0);
    s.pressed = false; s.points = 1u;
    CHECK(!nav_update(&n, &s, 1030u));
    CHECK(!nav_update(&n, &s, 1040u));
    CHECK_EQ(nav_index(&n), 0);
    CHECK(!nav_update(0, &s, 0u));
    CHECK(!nav_update(&n, 0, 0u));
    CHECK_EQ(nav_index(0), 0);
    nav_init(0, N_IMG, XMAX);                      /* 落ちないこと */
}

/* ---- 性質テスト: 乱数入力で不変条件を確認(実装の構造に依存しない形で書く) ---- */
#if (NAV_MODE == NAV_MODE_HALVES)
static uint32_t lcg(uint32_t *s)
{
    *s = (*s * 1664525u) + 1013904223u;
    return *s >> 8;
}

static void test_property_random(void)
{
    uint32_t seed;
    unsigned run_no;
    TEST("性質テスト: 乱数入力 6回 x 30万標本で不変条件");
    for (run_no = 0u; run_no < 6u; run_no++) {
        nav_t    n;
        uint32_t now   = (run_no < 3u) ? 0u : 0xFFFFF000u;   /* 後半は uint32 一周をまたぐ */
        uint32_t last_act = 0u;
        bool     have_act = false;
        unsigned nontouch_run = 0u, single_run = 0u;
        unsigned nontouch_since_act = 0u;                    /* 最後の動作以降の 最長 連続「触れていない」 */
        unsigned cur_nontouch = 0u;
        bool     multi_in_press = false;
        uint8_t  prev_idx = 0u;
        unsigned i, actions = 0u, bad = 0u;
        unsigned mode = 0u, mode_left = 0u;
        uint8_t  pts = 0u;
        uint16_t x = 0u;

        seed = 12345u + run_no * 7777u;
        nav_init(&n, N_IMG, XMAX);

        for (i = 0u; i < 300000u; i++) {
            nav_sample_t s;
            bool ch;
            bool touching;

            /* 入力生成: 数標本〜数百標本の塊(離し/1点/2点/ノイズ) */
            if (mode_left == 0u) {
                mode = lcg(&seed) % 6u;
                mode_left = 1u + (lcg(&seed) % ((mode == 5u) ? 4u : 120u));
                x = (uint16_t)(lcg(&seed) % 500u);
            }
            mode_left--;
            switch (mode) {
            case 0: pts = 0u; break;
            case 1: case 2: pts = 1u; break;
            case 3: pts = 2u; break;
            case 4: pts = (uint8_t)(lcg(&seed) % 6u); break;     /* 0..5 ランダム */
            default: pts = (uint8_t)(lcg(&seed) & 1u); break;    /* ノイズ 0/1 */
            }
            if ((lcg(&seed) % 4u) == 0u) { x = (uint16_t)(lcg(&seed) % 500u); }
            s = smp(pts, x);
            touching = (pts >= 1u);

            /* 抜き(時刻が 10ms ちょうどではない揺れも混ぜる) */
            now += NAV_SAMPLE_MS + ((lcg(&seed) % 8u == 0u) ? (lcg(&seed) % 25u) : 0u);

            ch = nav_update(&n, &s, now);

            /* 観測側の履歴更新(実装とは別に自前で数える) */
            if (touching) {
                cur_nontouch = 0u;
                nontouch_run = 0u;
                single_run = (pts == 1u) ? (single_run + 1u) : 0u;
                if (pts >= 2u) { multi_in_press = true; }
            } else {
                cur_nontouch++;
                nontouch_run++;
                single_run = 0u;
                if (nontouch_run >= NAV_RELEASE_CONFIRM) { multi_in_press = false; }
            }
            if (have_act && (cur_nontouch > nontouch_since_act)) {
                nontouch_since_act = cur_nontouch;
            }

            /* 不変条件 */
            if (nav_index(&n) >= N_IMG) { bad++; }
            if (ch) {
                uint8_t idx = nav_index(&n);
                actions++;
                if (!touching || pts != 1u) { bad++; }                              /* 単点の標本でのみ動く */
                if (single_run < NAV_PRESS_CONFIRM) { bad++; }                      /* 直前に単点が2標本以上連続 */
                if (multi_in_press) { bad++; }                                      /* 多点を含む押下では動かない */
                if (have_act && ((uint32_t)(now - last_act) < NAV_MIN_INTERVAL_MS)) { bad++; } /* 300ms */
                if (have_act && (nontouch_since_act < NAV_RELEASE_CONFIRM)) { bad++; }        /* 間に離し3標本 */
                /* 向き: 左(x<239)なら -1、右なら +1 (循環) */
                if (x < (XMAX / 2u)) {
                    if (idx != (uint8_t)((prev_idx + N_IMG - 1u) % N_IMG)) { bad++; }
                } else {
                    if (idx != (uint8_t)((prev_idx + 1u) % N_IMG)) { bad++; }
                }
                last_act = now;
                have_act = true;
                nontouch_since_act = 0u;
                cur_nontouch = 0u;
            } else if (nav_index(&n) != prev_idx) {
                bad++;                                                              /* false なのに番号が変わった */
            }
            prev_idx = nav_index(&n);
        }
        if (bad != 0u || actions == 0u) {
            printf("  run %u: bad=%u actions=%u\n", run_no, bad, actions);
        }
        CHECK_EQ(bad, 0);
        CHECK(actions > 100u);                    /* テスト自体が十分に動作を発生させていること */
    }
}
#endif /* NAV_MODE_HALVES */

int main(void)
{
#if (NAV_MODE == NAV_MODE_HALVES)
    test_h1_right();
    test_h1_boundary();
    test_h1_boundary_other_xmax();
    test_h2_cycle();
    test_image_count_edge();
    test_x_uses_latest_sample();
#else
    {
        ctx_t c;
        TEST("NAV_MODE_NEXT_ONLY: どこを触れても次へ");
        ctx_init(&c, N_IMG, XMAX, 1000u);
        (void)tap(&c, 2u, 10u);   CHECK_EQ(nav_index(&c.nav), 1);
        idle_ms(&c, 300u);
        (void)tap(&c, 2u, 450u);  CHECK_EQ(nav_index(&c.nav), 2);
        idle_ms(&c, 300u);
        (void)tap(&c, 2u, 10u);   CHECK_EQ(nav_index(&c.nav), 0);
    }
#endif
    test_h3_noise();
    test_h4_hold();
    test_h5_interval();
    test_h5_wording_gap();
    test_first_press_early();
    test_ignored_press_stays_ignored();
    test_h6_multi();
    test_flags_inconsistent();
#if (NAV_MODE == NAV_MODE_HALVES)
    test_property_random();
#endif
    TEST_REPORT((NAV_MODE == NAV_MODE_HALVES) ? "test_nav" : "test_nav_next");
}
