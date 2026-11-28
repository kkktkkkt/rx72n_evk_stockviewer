/* test_clk_core.c - src/app/clk_core.c のホスト試験(docs/design.md §13.10 の H-C1)。
 *   clk_kind_of(WMO 0〜99 の全コード・範囲外)/ clk_kind_name / clk_temp_int / clk_view_make(have の境界・ok_ms の巻き戻り・時計・日付の範囲)/
 *   clk_view_t のサイズ(8 バイト・パディングなし)/ 字番号の並び(時刻・日付・気温)。
 *   -DCLK_WX_STALE_S=120 でビルドすると、境界が動く(試験の中で期待を同じマクロから作る)。 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "clk_core.h"
#include "clk_assets.h"
#include "stk_core.h"
#include "test_framework.h"

static uint8_t expect_kind(uint32_t w)       /* §13.6 の表を別の書き方で(範囲の列挙)。表に無い 0〜99 は unknown */
{
    static const uint8_t rain[] = { 51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82 };
    static const uint8_t snow[] = { 71, 73, 75, 77, 85, 86 };
    unsigned i;

    if (w == 0u || w == 1u) { return CLK_SUN; }
    if (w == 2u) { return CLK_PARTLY; }
    if (w == 3u) { return CLK_CLOUD; }
    if (w == 45u || w == 48u) { return CLK_FOG; }
    for (i = 0u; i < sizeof rain; i++) { if (w == rain[i]) { return CLK_RAIN; } }
    for (i = 0u; i < sizeof snow; i++) { if (w == snow[i]) { return CLK_SNOW; } }
    if (w == 95u || w == 96u || w == 99u) { return CLK_THUNDER; }
    return CLK_UNKNOWN;
}

static void test_kind(void)
{
    uint32_t w;
    unsigned bad = 0u, n_unknown = 0u;
    static const char *const names[8] = { "sun", "partly", "cloud", "fog", "rain", "snow", "thunder", "unknown" };
    unsigned k;

    TEST("H-C1 clk_kind_of: WMO 0〜99 の全コードが §13.6 の表どおり。100・255・0xFFFFFFFF は unknown");
    for (w = 0u; w < 100u; w++) {
        if (clk_kind_of(w) != expect_kind(w)) { printf("  wmo %u: %u != %u\n", (unsigned)w, clk_kind_of(w), expect_kind(w)); bad++; }
        if (clk_kind_of(w) == CLK_UNKNOWN) { n_unknown++; }
    }
    CHECK_EQ(bad, 0);
    CHECK_EQ(n_unknown, 100u - (2u + 1u + 1u + 2u + 13u + 6u + 3u));          /* 表にある 28 コード以外が unknown */
    CHECK_EQ(clk_kind_of(100u), CLK_UNKNOWN);
    CHECK_EQ(clk_kind_of(255u), CLK_UNKNOWN);
    CHECK_EQ(clk_kind_of(0xFFFFFFFFu), CLK_UNKNOWN);
    CHECK_EQ(clk_kind_of(52u), CLK_UNKNOWN);
    CHECK_EQ(clk_kind_of(62u), CLK_UNKNOWN);

    TEST("H-C1 種類の番号(6b・6c・6d の約束)と clk_kind_name");
    CHECK_EQ(CLK_SUN, 0); CHECK_EQ(CLK_PARTLY, 1); CHECK_EQ(CLK_CLOUD, 2); CHECK_EQ(CLK_FOG, 3);
    CHECK_EQ(CLK_RAIN, 4); CHECK_EQ(CLK_SNOW, 5); CHECK_EQ(CLK_THUNDER, 6); CHECK_EQ(CLK_UNKNOWN, 7);
    CHECK_EQ(CLK_KINDS, 8);
    for (k = 0u; k < 8u; k++) { CHECK(clk_kind_name((uint8_t)k) != NULL && strcmp(clk_kind_name((uint8_t)k), names[k]) == 0); }
    CHECK(clk_kind_name(8u) == NULL);
    CHECK(clk_kind_name(255u) == NULL);
    CHECK_EQ(CLK_CLOCK_NONE, STK_NONE16);                                        /* stk_core.h と同じ値 */
    CHECK_EQ(CLK_MINUTES_MAX, STK_MINUTES_MAX);
}

static void test_temp(void)
{
    int t;

    TEST("H-C1 clk_temp_int: 絶対値の 0.5 を遠い方へ丸め、結果 0 は符号なし、±99 で頭打ち");
    CHECK_EQ(clk_temp_int(129), 13);
    CHECK_EQ(clk_temp_int(-4), 0);               /* −0.4 → 0°(−0° でない) */
    CHECK_EQ(clk_temp_int(-5), -1);              /* −0.5 → −1 */
    CHECK_EQ(clk_temp_int(5), 1);
    CHECK_EQ(clk_temp_int(4), 0);
    CHECK_EQ(clk_temp_int(0), 0);
    CHECK_EQ(clk_temp_int(994), 99);
    CHECK_EQ(clk_temp_int(995), 99);             /* 丸めた 100 を 99 に切る */
    CHECK_EQ(clk_temp_int(-995), -99);
    CHECK_EQ(clk_temp_int(600), 60);
    CHECK_EQ(clk_temp_int(-600), -60);
    CHECK_EQ(clk_temp_int(32767), 99);
    CHECK_EQ(clk_temp_int(-32768), -99);         /* int16 の最小(符号反転でも int32 で計算する) */
    CHECK_EQ(clk_temp_int(-15), -2);
    CHECK_EQ(clk_temp_int(-14), -1);
    for (t = -999; t <= 999; t++) {              /* 全範囲で別の式(浮動小数)と一致 */
        int a = (t < 0) ? -t : t, q = (a + 5) / 10, e;
        if (q > 99) { q = 99; }
        e = (t < 0) ? -q : q;
        if (clk_temp_int((int16_t)t) != e) { printf("  t10 %d: %d != %d\n", t, clk_temp_int((int16_t)t), e); CHECK(0); break; }
    }
}

static void test_view_make(void)
{
    clk_view_t v;
    clk_slot_t s;
    clk_date_t d;
    uint32_t stale_ms = (uint32_t)CLK_WX_STALE_S * 1000u;

    TEST("H-C1 clk_view_t: 8 バイト・パディングなし(memcmp で比べる)");
    CHECK_EQ(sizeof(clk_view_t), 8u);
    CHECK_EQ(offsetof(clk_view_t, clock_min), 0u);
    CHECK_EQ(offsetof(clk_view_t, kind), 2u);
    CHECK_EQ(offsetof(clk_view_t, wday), 7u);

    TEST("H-C1 clk_view_make: have の境界(最後の成功から CLK_WX_STALE_S - 1 秒 = 新しい、CLK_WX_STALE_S 秒 = 不明)");
    memset(&s, 0, sizeof s);
    s.have = 1u; s.kind = CLK_RAIN; s.temp_x10 = -45; s.ok_ms = 1000u;
    d.mon = 11u; d.day = 27u; d.wday = 5u;
    clk_view_make(&v, &s, 1000u + stale_ms - 1u, 581u, &d);
    CHECK_EQ(v.have, 1); CHECK_EQ(v.kind, CLK_RAIN); CHECK_EQ(v.temp, -5);
    CHECK_EQ(v.clock_min, 581); CHECK_EQ(v.mon, 11); CHECK_EQ(v.day, 27); CHECK_EQ(v.wday, 5);
    clk_view_make(&v, &s, 1000u + stale_ms - 1000u, 581u, &d);       /* 経過 (STALE - 1) 秒ちょうど */
    CHECK_EQ(v.have, 1);
    clk_view_make(&v, &s, 1000u + stale_ms - 1u, 581u, &d);          /* 経過 STALE - 0.001 秒(秒は切り捨て) */
    CHECK_EQ(v.have, 1);
    clk_view_make(&v, &s, 1000u + stale_ms, 581u, &d);               /* 経過 STALE 秒ちょうど */
    CHECK_EQ(v.have, 0); CHECK_EQ(v.kind, CLK_UNKNOWN); CHECK_EQ(v.temp, 0);
    CHECK_EQ(v.clock_min, 581); CHECK_EQ(v.mon, 11);                  /* 時計・日付は天気に依らない */
    clk_view_make(&v, &s, 1000u, 581u, &d);                          /* 経過 0 */
    CHECK_EQ(v.have, 1);
    memset(&v, 0xA5, sizeof v);
    clk_view_make(&v, &s, 1000u + stale_ms + 5000000u, 581u, NULL);   /* v は全部書く */
    CHECK_EQ(v.have, 0); CHECK_EQ(v.mon, 0); CHECK_EQ(v.day, 0); CHECK_EQ(v.wday, 0);
    {
        clk_view_t z;
        memset(&z, 0, sizeof z);
        z.clock_min = 581u; z.kind = CLK_UNKNOWN;
        CHECK(memcmp(&v, &z, sizeof v) == 0);
    }

    TEST("H-C1 clk_view_make: ok_ms の 32bit の巻き戻り(now_ms が ok_ms より小さくなっても差で扱う)");
    s.ok_ms = 0xFFFFF000u;
    clk_view_make(&v, &s, 0x00000100u, 0u, &d);                      /* 経過 0x1100 ms = 4 秒 */
    CHECK_EQ(v.have, 1);
    clk_view_make(&v, &s, 0xFFFFF000u + stale_ms, 0u, &d);          /* 巻き戻りをまたいで STALE 秒ちょうど */
    CHECK_EQ(v.have, 0);
    clk_view_make(&v, &s, 0xFFFFF000u + stale_ms - 1u, 0u, &d);
    CHECK_EQ(v.have, 1);

    TEST("H-C1 clk_view_make: slot->have = 0 / NULL / 種類が範囲外 → unknown。clock_min 1440〜0xFFFE は 0xFFFF にそろえる。date の範囲");
    memset(&s, 0, sizeof s);
    s.kind = CLK_SUN; s.temp_x10 = 200; s.ok_ms = 0u;
    clk_view_make(&v, &s, 0u, 100u, &d);
    CHECK_EQ(v.have, 0); CHECK_EQ(v.kind, CLK_UNKNOWN); CHECK_EQ(v.temp, 0);
    clk_view_make(&v, NULL, 0u, 100u, &d);
    CHECK_EQ(v.have, 0);
    s.have = 1u; s.kind = 8u;
    clk_view_make(&v, &s, 0u, 100u, &d);
    CHECK_EQ(v.have, 0);
    s.kind = CLK_UNKNOWN;                                              /* 種類が unknown でも気温はある(WMO の表に無いコード) */
    clk_view_make(&v, &s, 0u, 100u, &d);
    CHECK_EQ(v.have, 1); CHECK_EQ(v.kind, CLK_UNKNOWN); CHECK_EQ(v.temp, 20);
    clk_view_make(&v, &s, 0u, 1439u, &d);       CHECK_EQ(v.clock_min, 1439);
    clk_view_make(&v, &s, 0u, 1440u, &d);       CHECK_EQ(v.clock_min, 0xFFFF);
    clk_view_make(&v, &s, 0u, 0xFFFEu, &d);     CHECK_EQ(v.clock_min, 0xFFFF);
    clk_view_make(&v, &s, 0u, 0xFFFFu, &d);     CHECK_EQ(v.clock_min, 0xFFFF);
    clk_view_make(&v, &s, 0u, 0u, &d);          CHECK_EQ(v.clock_min, 0);
    d.mon = 0u;  clk_view_make(&v, &s, 0u, 0u, &d); CHECK_EQ(v.mon, 0); CHECK_EQ(v.day, 0);
    d.mon = 13u; clk_view_make(&v, &s, 0u, 0u, &d); CHECK_EQ(v.mon, 0);
    d.mon = 12u; d.day = 0u;  clk_view_make(&v, &s, 0u, 0u, &d); CHECK_EQ(v.mon, 0);
    d.day = 32u; clk_view_make(&v, &s, 0u, 0u, &d); CHECK_EQ(v.mon, 0);
    d.day = 31u; d.wday = 7u; clk_view_make(&v, &s, 0u, 0u, &d); CHECK_EQ(v.mon, 0);
    d.wday = 6u; clk_view_make(&v, &s, 0u, 0u, &d); CHECK_EQ(v.mon, 12); CHECK_EQ(v.day, 31); CHECK_EQ(v.wday, 6);
    d.mon = 1u; d.day = 1u; d.wday = 0u; clk_view_make(&v, &s, 0u, 0u, &d); CHECK_EQ(v.mon, 1); CHECK_EQ(v.day, 1); CHECK_EQ(v.wday, 0);
    clk_view_make(NULL, &s, 0u, 0u, &d);                              /* 何も起きない */

    TEST("H-C1 clk_view_kind: have = 0 や範囲外は unknown(パレット番号 pal_id = 1 + この値)");
    memset(&v, 0, sizeof v);
    v.kind = CLK_RAIN;
    CHECK_EQ(clk_view_kind(&v), CLK_UNKNOWN);       /* have = 0 */
    v.have = 1u;
    CHECK_EQ(clk_view_kind(&v), CLK_RAIN);
    v.kind = 8u;
    CHECK_EQ(clk_view_kind(&v), CLK_UNKNOWN);
}

static unsigned seq_eq(const uint8_t *got, uint8_t n, const uint8_t *want, unsigned wn)
{
    return (n == wn) && (memcmp(got, want, wn) == 0);
}

static void test_seq(void)
{
    uint8_t o[CLK_SEQ_DATE_MAX + 4u];
    clk_view_t v;
    uint8_t n;
    unsigned m;
    int ok = 1;

    TEST("H-C1 clk_seq_time: HH:MM を 0 詰め。未同期・1440〜65534 は --:--");
    memset(&v, 0, sizeof v);
    v.clock_min = 581u;  n = clk_seq_time(o, &v);  { const uint8_t w[] = { 0, 9, CLK_G124_COLON, 4, 1 }; CHECK(seq_eq(o, n, w, 5)); }
    v.clock_min = 0u;    n = clk_seq_time(o, &v);  { const uint8_t w[] = { 0, 0, CLK_G124_COLON, 0, 0 }; CHECK(seq_eq(o, n, w, 5)); }
    v.clock_min = 1439u; n = clk_seq_time(o, &v);  { const uint8_t w[] = { 2, 3, CLK_G124_COLON, 5, 9 }; CHECK(seq_eq(o, n, w, 5)); }
    v.clock_min = 305u;  n = clk_seq_time(o, &v);  { const uint8_t w[] = { 0, 5, CLK_G124_COLON, 0, 5 }; CHECK(seq_eq(o, n, w, 5)); }   /* 05:05 */
    { const uint8_t w[] = { CLK_G124_DASH, CLK_G124_DASH, CLK_G124_COLON, CLK_G124_DASH, CLK_G124_DASH };
      v.clock_min = 0xFFFFu; n = clk_seq_time(o, &v); CHECK(seq_eq(o, n, w, 5));
      v.clock_min = 1440u;   n = clk_seq_time(o, &v); CHECK(seq_eq(o, n, w, 5));
      v.clock_min = 0xFFFEu; n = clk_seq_time(o, &v); CHECK(seq_eq(o, n, w, 5)); }
    for (m = 0u; m <= 1439u; m++) {                /* 全分で桁が合う */
        v.clock_min = (uint16_t)m; n = clk_seq_time(o, &v);
        if ((n != 5u) || (o[0] * 10u + o[1] != m / 60u) || (o[3] * 10u + o[4] != m % 60u) || (o[2] != CLK_G124_COLON)) { ok = 0; }
    }
    CHECK(ok);

    TEST("H-C1 clk_seq_date: M月D日(曜)　東京都(月日は 0 詰めなし)。日付なし・範囲外は 東京都 だけ。最長 13 字");
    memset(&v, 0, sizeof v);
    v.mon = 11u; v.day = 27u; v.wday = 5u;
    n = clk_seq_date(o, &v);
    { const uint8_t w[] = { 1, 1, CLK_G20_MON, 2, 7, CLK_G20_DAY, CLK_G20_PAREN_L, 15, CLK_G20_PAREN_R, CLK_G20_SPACE, CLK_G20_TOKYO0, CLK_G20_TOKYO1, CLK_G20_TOKYO2 };
      CHECK(seq_eq(o, n, w, 13)); }
    v.mon = 1u; v.day = 5u; v.wday = 2u;
    n = clk_seq_date(o, &v);
    { const uint8_t w[] = { 1, CLK_G20_MON, 5, CLK_G20_DAY, CLK_G20_PAREN_L, 12, CLK_G20_PAREN_R, CLK_G20_SPACE, CLK_G20_TOKYO0, CLK_G20_TOKYO1, CLK_G20_TOKYO2 };
      CHECK(seq_eq(o, n, w, 11)); }
    v.mon = 12u; v.day = 30u; v.wday = 3u;
    n = clk_seq_date(o, &v);
    CHECK_EQ(n, CLK_SEQ_DATE_MAX);
    { const uint8_t w[] = { 1, 2, CLK_G20_MON, 3, 0, CLK_G20_DAY, CLK_G20_PAREN_L, 13, CLK_G20_PAREN_R, CLK_G20_SPACE, CLK_G20_TOKYO0, CLK_G20_TOKYO1, CLK_G20_TOKYO2 };
      CHECK(seq_eq(o, n, w, 13)); }                                              /* 12月30日(水)　東京都 */
    { unsigned wd;                                                              /* 曜日 0〜6 = 日月火水木金土(F20 の字番号) */
      static const uint8_t wg[7] = { 11, 10, 12, 13, 14, 15, 16 };
      for (wd = 0u; wd < 7u; wd++) { v.mon = 3u; v.day = 4u; v.wday = (uint8_t)wd; n = clk_seq_date(o, &v); if (o[5] != wg[wd]) { ok = 0; } }
      CHECK(ok); }
    { const uint8_t w[] = { CLK_G20_TOKYO0, CLK_G20_TOKYO1, CLK_G20_TOKYO2 };
      memset(&v, 0, sizeof v);                          n = clk_seq_date(o, &v); CHECK(seq_eq(o, n, w, 3));
      v.mon = 13u; v.day = 1u; v.wday = 0u;             n = clk_seq_date(o, &v); CHECK(seq_eq(o, n, w, 3));
      v.mon = 1u;  v.day = 0u;                          n = clk_seq_date(o, &v); CHECK(seq_eq(o, n, w, 3));
      v.day = 32u;                                      n = clk_seq_date(o, &v); CHECK(seq_eq(o, n, w, 3));
      v.day = 1u;  v.wday = 7u;                         n = clk_seq_date(o, &v); CHECK(seq_eq(o, n, w, 3)); }
    {                                                   /* 出力の範囲を超えて書かない(全組み合わせで番兵が無傷) */
      unsigned mo, da, wd; int sent = 1;
      for (mo = 0u; mo < 16u; mo++) { for (da = 0u; da < 40u; da += 3u) { for (wd = 0u; wd < 9u; wd += 2u) {
        memset(o, 0xA5, sizeof o);
        v.mon = (uint8_t)mo; v.day = (uint8_t)da; v.wday = (uint8_t)wd;
        n = clk_seq_date(o, &v);
        if ((n > CLK_SEQ_DATE_MAX) || (o[CLK_SEQ_DATE_MAX] != 0xA5u) || (o[CLK_SEQ_DATE_MAX + 3u] != 0xA5u)) { sent = 0; }
      } } }
      CHECK(sent); }

    TEST("H-C1 clk_seq_temp: 18° −2° 0° 99° −99° --°。範囲外の temp は ±99 に切る。最長 4 字");
    memset(&v, 0, sizeof v);
    v.have = 1u; v.temp = 18;   n = clk_seq_temp(o, &v); { const uint8_t w[] = { 1, 8, CLK_G72_DEG }; CHECK(seq_eq(o, n, w, 3)); }
    v.temp = -2;                n = clk_seq_temp(o, &v); { const uint8_t w[] = { CLK_G72_MINUS, 2, CLK_G72_DEG }; CHECK(seq_eq(o, n, w, 3)); }
    v.temp = 0;                 n = clk_seq_temp(o, &v); { const uint8_t w[] = { 0, CLK_G72_DEG }; CHECK(seq_eq(o, n, w, 2)); }
    v.temp = 99;                n = clk_seq_temp(o, &v); { const uint8_t w[] = { 9, 9, CLK_G72_DEG }; CHECK(seq_eq(o, n, w, 3)); }
    v.temp = -99;               n = clk_seq_temp(o, &v); { const uint8_t w[] = { CLK_G72_MINUS, 9, 9, CLK_G72_DEG }; CHECK(seq_eq(o, n, w, 4)); }
    v.temp = 5;                 n = clk_seq_temp(o, &v); { const uint8_t w[] = { 5, CLK_G72_DEG }; CHECK(seq_eq(o, n, w, 2)); }
    v.temp = -128;              n = clk_seq_temp(o, &v); { const uint8_t w[] = { CLK_G72_MINUS, 9, 9, CLK_G72_DEG }; CHECK(seq_eq(o, n, w, 4)); }
    v.temp = 127;               n = clk_seq_temp(o, &v); { const uint8_t w[] = { 9, 9, CLK_G72_DEG }; CHECK(seq_eq(o, n, w, 3)); }
    v.have = 0u; v.temp = 18;   n = clk_seq_temp(o, &v); { const uint8_t w[] = { CLK_G72_DASH, CLK_G72_DASH, CLK_G72_DEG }; CHECK(seq_eq(o, n, w, 3)); }

    TEST("H-C1 字番号の定数(生成物): 数字 0〜9 の字番号は数字そのもの(F124・F72・F20 とも)。F20 の月・日・括弧・東京都");
    CHECK_EQ(CLK_G124_N, 12); CHECK_EQ(CLK_G72_N, 13); CHECK_EQ(CLK_G20_N, 36);
    CHECK_EQ(CLK_G124_COLON, 10); CHECK_EQ(CLK_G124_DASH, 11);
    CHECK_EQ(CLK_G72_MINUS, 10); CHECK_EQ(CLK_G72_DEG, 11); CHECK_EQ(CLK_G72_DASH, 12);
    CHECK_EQ(CLK_G20_MON, 10); CHECK_EQ(CLK_G20_DAY, 11); CHECK_EQ(CLK_G20_PAREN_L, 17); CHECK_EQ(CLK_G20_PAREN_R, 18);
    CHECK_EQ(CLK_G20_TOKYO0, 19); CHECK_EQ(CLK_G20_SPACE, 35);
}

int main(void)
{
    test_kind();
    test_temp();
    test_view_make();
    test_seq();
    TEST_REPORT("test_clk_core");
}
