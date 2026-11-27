/* test_stk_core.c - 株価の数値(src/app/stk_core.c)のホスト試験。docs/design.md §8 の H-1(stk_calc)、H-2(整形)、H-5(stk_market)。
 *   H-1  前日比・騰落率: 設計の例・四捨五入の境界・負の値・ゼロ・範囲外・最大値(オーバーフローなし)。dir は丸めた pct_bp の符号(0.00% は FLAT)。
 *        「最も近い整数で、同点は絶対値の大きい方」を、実装と別の式(誤差の不等式。全 diff / prev の掃引)でも確かめる
 *   H-2  整形: 文字列の完全一致(円・指数・％・金額・変化なし)、最大の長さ ≦ maxch(現在値 10 / 騰落率 8 / 金額 12)、
 *        +0.00% / −0.00% が出ない・矢印は方向があり金額が 0 でないときだけ、
 *        バッファ不足は 0 で buf[0] = '\0'(1 バイトも余計に書かない)、実装と別の整形(snprintf + 文字列へのカンマ挿入)との乱数比較
 *   H-5  stk_market(§5.4): 現在時刻が取引時間帯なら取引中。昼休み(日本株だけ)は終値。寄り付き後 30 分(mtime < rs)と後場の開始直後は値が古くても取引中。
 *        1 日を 1 秒刻みで(§5.4 の時間帯の表と一致)、境界(now = rs / re、now − mtime = 7200 / 7201、now − rs = 1800 / 1801、昼休みの前後)、時計未同期、32bit の巻き戻り
 * 浮動小数点を使っていないこと(stk_core.c に float / double が無い)は run.sh が grep で見る。 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "stk_core.h"
#include "test_framework.h"

#define UP   "\x01"      /* ▲ */
#define DN   "\x02"      /* ▼ */
#define MN   "\x03"      /* −(U+2212) */

static uint32_t g_rs = 0x2468ACE1u;
static uint32_t rnd(void) { g_rs ^= g_rs << 13; g_rs ^= g_rs >> 17; g_rs ^= g_rs << 5; return g_rs; }

static uint8_t dir_of(int32_t v) { return (v > 0) ? (uint8_t)STK_UP : ((v < 0) ? (uint8_t)STK_DOWN : (uint8_t)STK_FLAT); }

/* ---------------------------------------------------------------- H-1 */
static void calc_ok(int32_t price, int32_t prev, int32_t diff, int32_t bp, uint8_t dir)
{
    stk_calc_t c;
    bool ok = stk_calc(price, prev, &c);

    if (!ok || (c.diff_x100 != diff) || (c.pct_bp != bp) || (c.dir != dir)) {
        printf("  stk_calc(%ld, %ld): ok=%d diff=%ld bp=%ld dir=%d (want %ld %ld %d)\n", (long)price, (long)prev, (int)ok, (long)c.diff_x100, (long)c.pct_bp, (int)c.dir,
               (long)diff, (long)bp, (int)dir);
    }
    CHECK(ok); CHECK_EQ(c.diff_x100, diff); CHECK_EQ(c.pct_bp, bp); CHECK_EQ(c.dir, dir);
}

static void calc_ng(int32_t price, int32_t prev)
{
    stk_calc_t c;
    bool ok;

    c.diff_x100 = 77; c.pct_bp = 77; c.dir = 9u;
    ok = stk_calc(price, prev, &c);
    if (ok) { printf("  stk_calc(%ld, %ld) が成功した\n", (long)price, (long)prev); }
    CHECK(!ok); CHECK_EQ(c.diff_x100, 0); CHECK_EQ(c.pct_bp, 0); CHECK_EQ(c.dir, STK_FLAT);       /* 失敗では 0・変化なしにする */
}

static void t_calc(void)
{
    stk_calc_t c;
    int32_t    prev, d;
    long       bad = 0;

    TEST("H-1 stk_calc: 設計の例(Yahoo の 2.313% / 0.305% / -0.254% と小数 2 桁で一致)");
    calc_ok(243300, 237800, 5500, 231, STK_UP);               /* 6723.T の例 */
    calc_ok(5767436, 5749916, 17520, 30, STK_UP);
    calc_ok(718278, 720109, -1831, -25, STK_DOWN);
    calc_ok(256450, 243300, 13150, 540, STK_UP);              /* 実応答(2026-11-27)の 6723.T。Yahoo の表示 +5.404% */
    calc_ok(5854187, 5749916, 104271, 181, STK_UP);           /* ^N225 +1.813% */
    TEST("H-1 四捨五入(絶対値の 0.5 の境界は遠い方へ)・負の値は対称");
    calc_ok(20001, 20000, 1, 1, STK_UP);                      /* 0.5 → 1 */
    calc_ok(19999, 20000, -1, -1, STK_DOWN);                  /* -0.5 → -1 */
    calc_ok(40003, 40000, 3, 1, STK_UP);                      /* 0.75 → 1 */
    calc_ok(39997, 40000, -3, -1, STK_DOWN);
    calc_ok(3003, 3000, 3, 10, STK_UP);                       /* 10.0 ちょうど */
    calc_ok(3001, 3000, 1, 3, STK_UP);                        /* 3.33 → 3 */
    calc_ok(3002, 3000, 2, 7, STK_UP);                        /* 6.67 → 7 */
    calc_ok(2998, 3000, -2, -7, STK_DOWN);
    calc_ok(30000, 30000, 0, 0, STK_FLAT);                    /* 変化なし */
    calc_ok(1, 1, 0, 0, STK_FLAT);
    calc_ok(3, 2, 1, 5000, STK_UP);                           /* +50% */
    calc_ok(1, 3, -2, -6667, STK_DOWN);                       /* -66.67% */
    TEST("H-1 dir は丸めた pct_bp の符号: diff ≠ 0 でも 0.00% に丸まれば STK_FLAT(S32 の O-1・O-2)。pct_bp ≠ 0 なら円の金額が 0 でも上昇・下落のまま");
    calc_ok(40001, 40000, 1, 0, STK_FLAT);                    /* 0.25 → 0。diff +1 でも変化なし */
    calc_ok(39999, 40000, -1, 0, STK_FLAT);                   /* -0.25 → 0。diff -1 でも変化なし */
    calc_ok(5000050, 5000000, 50, 0, STK_FLAT);               /* 設計 H-1 の例: 日経平均 diff +0.50、bp 0 */
    calc_ok(4999950, 5000000, -50, 0, STK_FLAT);
    calc_ok(5002500, 5000000, 2500, 5, STK_UP);               /* +0.05% は上昇 */
    calc_ok(5000250, 5000000, 250, 1, STK_UP);                /* 0.5 → 1(遠い方): 最小の上昇 */
    calc_ok(200030, 200000, 30, 2, STK_UP);                   /* 設計 §4.2 の例: 円の diff +0.3 円、bp 2(1.5 → 2)→ 上昇のまま */
    calc_ok(199970, 200000, -30, -2, STK_DOWN);
    calc_ok(200029, 200000, 29, 1, STK_UP);                   /* 1.45 → 1 */
    calc_ok(200024, 200000, 24, 1, STK_UP);                   /* 1.2 → 1 */
    calc_ok(200012, 200000, 12, 1, STK_UP);                   /* 0.6 → 1 */
    calc_ok(200010, 200000, 10, 1, STK_UP);                   /* 0.5 → 1(遠い方) */
    calc_ok(200009, 200000, 9, 0, STK_FLAT);                  /* 0.45 → 0 */
    TEST("H-1 範囲: 1〜99,999,999。外れ・|pct_bp| > 99,999 は false(出力は 0 / STK_FLAT)");
    calc_ng(0, 100); calc_ng(100, 0); calc_ng(-1, 100); calc_ng(100, -1); calc_ng(0, 0);
    calc_ng(100000000, 100); calc_ng(100, 100000000); calc_ng(INT32_MAX, 100); calc_ng(100, INT32_MAX); calc_ng(INT32_MIN, 100); calc_ng(100, INT32_MIN);
    calc_ng(99999999, 1);                                     /* 例: prev = 1、price = 99,999,999 */
    calc_ok(109999, 10000, 99999, 99999, STK_UP);             /* ちょうど +999.99% */
    calc_ng(110000, 10000);                                   /* +1000.00% */
    calc_ok(1, 10000, -9999, -9999, STK_DOWN);                /* 下落は -99.99% が下限(価格 ≧ 1 のため) */
    calc_ok(1, 99999999, -99999998, -10000, STK_DOWN);        /* -100.00%(丸めて -10000) */
    calc_ng(99999999, 999);                                   /* 巨大 */
    TEST("H-1 最大値: 99,999,999 同士・最大の diff でオーバーフローなし");
    calc_ok(99999999, 99999999, 0, 0, STK_FLAT);
    calc_ok(99999999, 90000000, 9999999, 1111, STK_UP);       /* 設計 §3.12 の S7 の r2 */
    calc_ok(1, 99999999, -99999998, -10000, STK_DOWN);
    calc_ok(99999999, 50000000, 49999999, 10000, STK_UP);     /* 99.999998 → 10000(= 100.00%) */
    CHECK(!stk_calc(100, 100, NULL));                         /* out が NULL */
    TEST("H-1 掃引: 小さい prev・全 diff で、bp が diff / prev x 10000 に最も近い整数(同点は絶対値の大きい方)・dir が bp の符号どおり");
    for (prev = 1; prev <= 400; prev++) {
        for (d = -prev + 1; d <= 3 * prev; d++) {
            int32_t price = prev + d;
            int64_t x2 = 2 * (int64_t)((d < 0) ? -d : d) * 10000;       /* 2 * |diff| * 10000 */
            int64_t b2, err2;
            bool    ok = stk_calc(price, prev, &c);
            int64_t ab;

            if (!ok) { if (((d < 0) ? -(int64_t)d : (int64_t)d) * 10000 / prev <= 99999) { bad++; } continue; }
            ab = (c.pct_bp < 0) ? -(int64_t)c.pct_bp : (int64_t)c.pct_bp;
            b2 = 2 * ab * (int64_t)prev;                                  /* 2 * |bp| * prev */
            err2 = (b2 > x2) ? (b2 - x2) : (x2 - b2);
            if ((err2 > (int64_t)prev) || ((err2 == (int64_t)prev) && (b2 < x2))) { bad++; }   /* 誤差 ≦ 0.5、同点で誤差ちょうど 0.5 なら上(絶対値が大きい方) */
            if ((d > 0) && (c.pct_bp < 0)) { bad++; }
            if ((d < 0) && (c.pct_bp > 0)) { bad++; }
            if ((d == 0) && (c.pct_bp != 0)) { bad++; }
            if (c.dir != dir_of(c.pct_bp)) { bad++; }                     /* dir は丸めた bp の符号 */
            if (c.diff_x100 != d) { bad++; }
        }
    }
    CHECK_EQ(bad, 0);
}

/* ---------------------------------------------------------------- H-2 */
static void str_eq(size_t n, const char *buf, const char *want, const char *what)
{
    if (strcmp(buf, want) != 0) { printf("  %s: [%s] want [%s]\n", what, buf, want); }
    CHECK(strcmp(buf, want) == 0);
    CHECK_EQ(n, strlen(want));
}

static void price_eq(int32_t p, uint8_t kind, const char *want)
{
    char b[24];
    size_t n = stk_fmt_price(b, sizeof b, p, kind);
    str_eq(n, b, want, "stk_fmt_price");
}

static void pct_eq(int32_t bp, const char *want)
{
    char b[24];
    size_t n = stk_fmt_pct(b, sizeof b, bp);
    str_eq(n, b, want, "stk_fmt_pct");
}

static void amt_eq(int32_t d, uint8_t kind, uint8_t dir, const char *want)
{
    char b[24];
    size_t n = stk_fmt_amt(b, sizeof b, d, kind, dir);
    str_eq(n, b, want, "stk_fmt_amt");
}

/* 実装と別の整形: snprintf の整数 + 文字列へのカンマ挿入 */
static void ref_commas(char *o, unsigned long n)
{
    char t[32];
    size_t len, i, k = 0;

    snprintf(t, sizeof t, "%lu", n);
    len = strlen(t);
    for (i = 0; i < len; i++) {
        o[k++] = t[i];
        if (((len - 1 - i) % 3 == 0) && (i != len - 1)) { o[k++] = ','; }
    }
    o[k] = '\0';
}

static void ref_x100(char *o, unsigned long v)
{
    char t[32];
    ref_commas(t, v / 100);
    snprintf(o, 40, "%s.%02lu", t, v % 100);
}

static void t_fmt(void)
{
    char  b[40], r[40], big[64];
    size_t n, size, i;
    long  bad = 0;
    static const int32_t edge[] = { 0, 1, 9, 10, 49, 50, 51, 99, 100, 101, 149, 150, 151, 999, 1000, 4999, 5000, 9999, 10000, 99949, 99950, 99999, 100000, 100049, 100050,
                                    999999, 1000000, 99999949, 99999950, 99999998, 99999999 };

    TEST("H-2 stk_fmt_price: 円は整数円(0.5 円は切り上げ)・指数は小数 2 桁・カンマ区切り");
    price_eq(243300, STK_KIND_YEN, "2,433"); price_eq(256450, STK_KIND_YEN, "2,565"); price_eq(256449, STK_KIND_YEN, "2,564");
    price_eq(11000, STK_KIND_YEN, "110"); price_eq(100, STK_KIND_YEN, "1"); price_eq(49, STK_KIND_YEN, "0"); price_eq(50, STK_KIND_YEN, "1"); price_eq(0, STK_KIND_YEN, "0");
    price_eq(99999999, STK_KIND_YEN, "1,000,000"); price_eq(99999949, STK_KIND_YEN, "999,999");
    price_eq(5795384, STK_KIND_INDEX, "57,953.84"); price_eq(718278, STK_KIND_INDEX, "7,182.78"); price_eq(5, STK_KIND_INDEX, "0.05"); price_eq(0, STK_KIND_INDEX, "0.00");
    price_eq(10000000, STK_KIND_INDEX, "100,000.00"); price_eq(99999999, STK_KIND_INDEX, "999,999.99"); price_eq(100, STK_KIND_INDEX, "1.00"); price_eq(99999, STK_KIND_INDEX, "999.99");
    price_eq(100000, STK_KIND_INDEX, "1,000.00");
    TEST("H-2 stk_fmt_pct: 符号は pct_bp から。0 は 0.00%(+0.00% −0.00% は出ない)");
    pct_eq(231, "+2.31%"); pct_eq(-25, MN "0.25%"); pct_eq(0, "0.00%"); pct_eq(2, "+0.02%"); pct_eq(-1, MN "0.01%");
    pct_eq(99890, "+998.90%"); pct_eq(99999, "+999.99%"); pct_eq(-99999, MN "999.99%"); pct_eq(5, "+0.05%"); pct_eq(100, "+1.00%"); pct_eq(-100, MN "1.00%");
    TEST("H-2 stk_fmt_amt: 丸めた金額。矢印(▲ ▼。空白なし)は dir が上昇・下落で金額が 0 でないときだけ。0 は 0 / 0.00");
    amt_eq(5500, STK_KIND_YEN, STK_UP, UP "55"); amt_eq(5450, STK_KIND_YEN, STK_UP, UP "55"); amt_eq(5449, STK_KIND_YEN, STK_UP, UP "54");
    amt_eq(-5450, STK_KIND_YEN, STK_DOWN, DN "55"); amt_eq(-5449, STK_KIND_YEN, STK_DOWN, DN "54");
    amt_eq(50, STK_KIND_YEN, STK_UP, UP "1"); amt_eq(-50, STK_KIND_YEN, STK_DOWN, DN "1");
    amt_eq(49, STK_KIND_YEN, STK_UP, "0"); amt_eq(-49, STK_KIND_YEN, STK_DOWN, "0"); amt_eq(30, STK_KIND_YEN, STK_UP, "0");   /* 金額が 0 に丸まる円は、色付きでも矢印なし(H-7) */
    amt_eq(9999, STK_KIND_YEN, STK_UP, UP "100"); amt_eq(99999949, STK_KIND_YEN, STK_UP, UP "999,999"); amt_eq(-99999999, STK_KIND_YEN, STK_DOWN, DN "1,000,000");
    amt_eq(-1831, STK_KIND_INDEX, STK_DOWN, DN "18.31"); amt_eq(17520, STK_KIND_INDEX, STK_UP, UP "175.20"); amt_eq(1, STK_KIND_INDEX, STK_UP, UP "0.01"); amt_eq(-1, STK_KIND_INDEX, STK_DOWN, DN "0.01");
    amt_eq(1000, STK_KIND_INDEX, STK_UP, UP "10.00"); amt_eq(9999999, STK_KIND_INDEX, STK_UP, UP "99,999.99"); amt_eq(99999999, STK_KIND_INDEX, STK_UP, UP "999,999.99");
    amt_eq(-99999999, STK_KIND_INDEX, STK_DOWN, DN "999,999.99");
    amt_eq(50, STK_KIND_INDEX, STK_FLAT, "0.50"); amt_eq(-50, STK_KIND_INDEX, STK_FLAT, "0.50");            /* 変化なし(bp = 0)でも金額はそのまま・矢印なし(設計 §4.2 の例) */
    amt_eq(5500, STK_KIND_YEN, STK_FLAT, "55"); amt_eq(-123456, STK_KIND_INDEX, STK_FLAT, "1,234.56"); amt_eq(49, STK_KIND_YEN, STK_FLAT, "0");
    amt_eq(0, STK_KIND_YEN, STK_FLAT, "0"); amt_eq(0, STK_KIND_INDEX, STK_FLAT, "0.00");
    amt_eq(0, STK_KIND_YEN, STK_UP, "0"); amt_eq(0, STK_KIND_INDEX, STK_DOWN, "0.00");                        /* 方向があっても金額 0 は矢印なし */
    TEST("H-2 範囲外・不正な引数は 0 で buf[0] = '\\0'");
    {
        static const struct { int which; int32_t v; uint8_t k; uint8_t d; } NG[] = {
            { 0, -1, STK_KIND_YEN, 0 }, { 0, 100000000, STK_KIND_YEN, 0 }, { 0, 100, 2, 0 }, { 0, 100, 255, 0 }, { 0, INT32_MIN, STK_KIND_INDEX, 0 }, { 0, INT32_MAX, STK_KIND_INDEX, 0 },
            { 1, 100000, 0, 0 }, { 1, -100000, 0, 0 }, { 1, INT32_MAX, 0, 0 }, { 1, INT32_MIN, 0, 0 },
            { 2, 100000000, STK_KIND_YEN, STK_UP }, { 2, -100000000, STK_KIND_INDEX, STK_DOWN }, { 2, 100, 2, STK_UP }, { 2, 100, STK_KIND_YEN, 3 }, { 2, 100, STK_KIND_YEN, 255 },
            { 2, INT32_MIN, STK_KIND_YEN, STK_UP }, { 2, INT32_MAX, STK_KIND_YEN, STK_UP }
        };
        for (i = 0; i < sizeof NG / sizeof NG[0]; i++) {
            memset(b, 'Q', sizeof b);
            n = (NG[i].which == 0) ? stk_fmt_price(b, sizeof b, NG[i].v, NG[i].k) : (NG[i].which == 1) ? stk_fmt_pct(b, sizeof b, NG[i].v)
                                                                                                          : stk_fmt_amt(b, sizeof b, NG[i].v, NG[i].k, NG[i].d);
            if (n != 0u || b[0] != '\0') { printf("  NG[%u] が失敗しなかった\n", (unsigned)i); }
            CHECK_EQ(n, 0); CHECK_EQ(b[0], 0);
        }
        memset(b, 'Q', sizeof b);
        CHECK_EQ(stk_fmt_price(NULL, 16, 100, STK_KIND_YEN), 0); CHECK_EQ(stk_fmt_pct(NULL, 16, 1), 0); CHECK_EQ(stk_fmt_amt(NULL, 16, 1, STK_KIND_YEN, STK_UP), 0);
        CHECK_EQ(stk_fmt_price(b, 0, 100, STK_KIND_YEN), 0); CHECK_EQ(stk_fmt_pct(b, 0, 1), 0); CHECK_EQ(stk_fmt_amt(b, 0, 1, STK_KIND_YEN, STK_UP), 0);
        CHECK_EQ(b[0], 'Q');                                   /* size = 0 は 1 バイトも書かない */
    }
    TEST("H-2 バッファ不足: size < 長さ + 1 は 0 で buf[0] = '\\0'、番兵(buf の後ろ)を壊さない。size = 長さ + 1 はちょうど入る");
    {
        static const struct { int which; int32_t v; uint8_t k; uint8_t d; const char *want; } T[] = {
            { 0, 5795384, STK_KIND_INDEX, 0, "57,953.84" }, { 0, 243300, STK_KIND_YEN, 0, "2,433" }, { 0, 99999999, STK_KIND_INDEX, 0, "999,999.99" },
            { 1, 231, 0, 0, "+2.31%" }, { 1, -99999, 0, 0, MN "999.99%" }, { 1, 0, 0, 0, "0.00%" },
            { 2, 5500, STK_KIND_YEN, STK_UP, UP "55" }, { 2, -99999999, STK_KIND_INDEX, STK_DOWN, DN "999,999.99" }, { 2, 0, STK_KIND_INDEX, STK_FLAT, "0.00" },
            { 2, 0, STK_KIND_YEN, STK_FLAT, "0" }, { 2, 50, STK_KIND_INDEX, STK_FLAT, "0.50" }
        };
        size_t t;
        for (t = 0; t < sizeof T / sizeof T[0]; t++) {
            size_t len = strlen(T[t].want);
            for (size = 1; size <= len + 2; size++) {
                memset(big, 'Z', sizeof big);
                n = (T[t].which == 0) ? stk_fmt_price(big, size, T[t].v, T[t].k) : (T[t].which == 1) ? stk_fmt_pct(big, size, T[t].v) : stk_fmt_amt(big, size, T[t].v, T[t].k, T[t].d);
                if (size >= len + 1) {
                    if (n != len || strcmp(big, T[t].want) != 0) { bad++; }
                } else if (n != 0u || big[0] != '\0') { bad++; }
                for (i = size; i < sizeof big; i++) { if (big[i] != 'Z') { bad++; break; } }   /* size の先は 1 バイトも書かない */
            }
        }
        CHECK_EQ(bad, 0);
    }
    TEST("H-2 最大の長さ ≦ maxch(現在値 10 / 騰落率 8 / 金額 12。設計 §3.7 のスロット)と、別の整形との一致(端の値 + 乱数 300,000 個)。pct = 0 に符号が付かない");
    bad = 0;
    for (i = 0; i < sizeof edge / sizeof edge[0] + 300000u; i++) {
        int32_t v = (i < sizeof edge / sizeof edge[0]) ? edge[i] : (int32_t)(rnd() % 100000000u);
        int32_t bp = (int32_t)(rnd() % 199999u) - 99999;
        int32_t dd = (rnd() & 1u) ? v : -v;
        unsigned long a, amt;
        uint8_t dr;
        char w[48];

        if (i < sizeof edge / sizeof edge[0]) { bp = (int32_t)(v % 100000); dd = (i & 1u) ? v : -v; }
        else if ((rnd() & 15u) == 0u) { bp = 0; }
        n = stk_fmt_price(b, sizeof b, v, STK_KIND_INDEX); ref_x100(r, (unsigned long)v);
        if (n != strlen(r) || strcmp(b, r) != 0 || n > 10u) { bad++; }
        n = stk_fmt_price(b, sizeof b, v, STK_KIND_YEN); ref_commas(r, (unsigned long)(v / 100 + ((v % 100) >= 50 ? 1 : 0)));
        if (n != strlen(r) || strcmp(b, r) != 0 || n > 10u) { bad++; }
        a = (unsigned long)((bp < 0) ? -bp : bp);
        n = stk_fmt_pct(b, sizeof b, bp); ref_x100(r, a);
        snprintf(w, sizeof w, "%s%s%%", (bp < 0) ? MN : ((bp > 0) ? "+" : ""), r);
        if (n != strlen(w) || strcmp(b, w) != 0 || n > 8u) { bad++; }
        /* 金額: dir は rnd(UP / DOWN / FLAT)。矢印は dir が UP / DOWN で A ≠ 0 のときだけ */
        dr = (uint8_t)(rnd() % 3u);
        a = (unsigned long)((dd < 0) ? -dd : dd);
        n = stk_fmt_amt(b, sizeof b, dd, STK_KIND_INDEX, dr); ref_x100(r, a);
        snprintf(w, sizeof w, "%s%s", ((dr != STK_FLAT) && (a != 0ul)) ? ((dr == STK_UP) ? UP : DN) : "", r);
        if (n != strlen(w) || strcmp(b, w) != 0 || n > 12u) { bad++; }
        amt = a / 100 + ((a % 100) >= 50 ? 1u : 0u);
        n = stk_fmt_amt(b, sizeof b, dd, STK_KIND_YEN, dr); ref_commas(r, amt);
        snprintf(w, sizeof w, "%s%s", ((dr != STK_FLAT) && (amt != 0ul)) ? ((dr == STK_UP) ? UP : DN) : "", r);
        if (n != strlen(w) || strcmp(b, w) != 0 || n > 12u) { bad++; }
    }
    CHECK_EQ(bad, 0);
    TEST("H-2 stk_row_kind・stk_sym: 行 0 = 円、1・2 = 指数。昼休みは 6723.T と ^N225 が 11:30〜12:30、^GSPC は無し。範囲外は NULL");
    CHECK_EQ(stk_row_kind(0), STK_KIND_YEN); CHECK_EQ(stk_row_kind(1), STK_KIND_INDEX); CHECK_EQ(stk_row_kind(2), STK_KIND_INDEX);
    CHECK(stk_sym(3) == NULL); CHECK(stk_sym(255) == NULL);
    CHECK_EQ(stk_sym(0)->kind, STK_KIND_YEN); CHECK_EQ(stk_sym(0)->lunch_s, 41400u); CHECK_EQ(stk_sym(0)->lunch_e, 45000u);
    CHECK_EQ(stk_sym(1)->kind, STK_KIND_INDEX); CHECK_EQ(stk_sym(1)->lunch_s, 41400u); CHECK_EQ(stk_sym(1)->lunch_e, 45000u);
    CHECK_EQ(stk_sym(2)->kind, STK_KIND_INDEX); CHECK_EQ(stk_sym(2)->lunch_s, stk_sym(2)->lunch_e);
}

/* ---------------------------------------------------------------- H-5 */
#define JST_S 32400u

/* now_utc で渡す(JST = UTC + 32400)。row = 銘柄の行 */
static void mk_eq(uint8_t row, bool sy, uint32_t now_utc, uint32_t mt, uint32_t rs, uint32_t re, uint8_t want, const char *what)
{
    uint8_t g = stk_market(stk_sym(row), sy, now_utc + JST_S, mt, rs, re);
    if (g != want) { printf("  %s: got %u want %u (row=%u now=%lu mt=%lu rs=%lu re=%lu)\n", what, (unsigned)g, (unsigned)want, (unsigned)row, (unsigned long)now_utc, (unsigned long)mt, (unsigned long)rs, (unsigned long)re); }
    CHECK_EQ(g, want);
}

static void t_market(void)
{
    const uint32_t RS = 1795737600u, RE = 1795761000u;     /* 6723.T の regular(2026-11-27 09:00〜15:30 JST。RS は UTC の 0 時) */
    const uint32_t GS = 1795617000u, GE = 1795640400u;     /* ^GSPC の regular(2026-11-25 09:30〜16:00 EST) */
    uint32_t       now, t;
    long           bad = 0;

    TEST("H-5 実測の値: 6723.T 取引中(now = rs + 18 分、mtime = rs + 3 分)、^GSPC 終値(now > re)");
    mk_eq(0, true, RS + 18u * 60u, RS + 3u * 60u, RS, RE, STK_MKT_TRADING, "6723.T");
    mk_eq(0, true, 1795752236u, 1795751335u, RS, RE, STK_MKT_TRADING, "実応答の 6723.T(now = Date: の UTC、遅れ 901 秒)");
    mk_eq(1, true, 1795752244u, 1795751340u, RS, RE, STK_MKT_TRADING, "実応答の ^N225");
    mk_eq(2, true, 1795752253u, 1795642326u, GS, GE, STK_MKT_CLOSED, "実応答の ^GSPC(翌々日の朝。区間の外)");
    mk_eq(2, true, GE + 1u, GE - 100u, GS, GE, STK_MKT_CLOSED, "引け後");
    TEST("H-5 区間の境界: now = rs は取引中、now = rs - 1 は終値、now = re - 1 は取引中、now = re は終値");
    mk_eq(0, true, RS, RS, RS, RE, STK_MKT_TRADING, "now = rs");
    mk_eq(0, true, RS - 1u, RS, RS, RE, STK_MKT_CLOSED, "now = rs - 1");
    mk_eq(0, true, RE - 1u, RE - 100u, RS, RE, STK_MKT_TRADING, "now = re - 1");
    mk_eq(0, true, RE, RE - 100u, RS, RE, STK_MKT_CLOSED, "now = re");
    mk_eq(0, true, RE + 3600u, RE - 100u, RS, RE, STK_MKT_CLOSED, "引け後 1 時間");
    mk_eq(0, true, RS - 3600u, RS - 86400u, RS, RE, STK_MKT_CLOSED, "寄り付き前(前日の足)");
    TEST("H-5 データの鮮度: now − mtime = 7200 は取引中、7201 は終値(取引時間帯の中でも、データが 2 時間より古い・休日の足)");
    now = RS + 4u * 3600u;                                   /* 13:00 JST */
    mk_eq(0, true, now, now - 7200u, RS, RE, STK_MKT_TRADING, "7200");
    mk_eq(0, true, now, now - 7201u, RS, RE, STK_MKT_CLOSED, "7201");
    mk_eq(0, true, now, now - 1801u, RS, RE, STK_MKT_TRADING, "1801(旧規則では終値。今は取引中)");
    mk_eq(0, true, now, now - 0u, RS, RE, STK_MKT_TRADING, "0");
    mk_eq(0, true, now, now + 5u, RS, RE, STK_MKT_TRADING, "mtime が now より 5 秒先(時計のずれ)");
    mk_eq(0, true, now, RS, RS, RE, STK_MKT_CLOSED, "mtime = rs で now − mtime = 14400(2 時間より古い)");
    TEST("H-5 寄り付き後の猶予: mtime < rs でも now − rs ≦ 1800 は取引中、1801 は終値");
    mk_eq(0, true, RS + 1u, RS - 70000u, RS, RE, STK_MKT_TRADING, "now = rs + 1、前日の足");
    mk_eq(0, true, RS + 899u, RS - 70000u, RS, RE, STK_MKT_TRADING, "09:14:59(旧規則では終値。今は取引中)");
    mk_eq(0, true, RS + 1800u, RS - 1u, RS, RE, STK_MKT_TRADING, "now − rs = 1800");
    mk_eq(0, true, RS + 1801u, RS - 1u, RS, RE, STK_MKT_CLOSED, "now − rs = 1801");
    mk_eq(0, true, RS + 100u, 0u, RS, RE, STK_MKT_TRADING, "mtime = 0(猶予の中)");
    mk_eq(0, true, RS + 1801u, 0u, RS, RE, STK_MKT_CLOSED, "mtime = 0、猶予の後");
    mk_eq(0, true, RS + 100u, RS, RS, RE, STK_MKT_TRADING, "mtime = rs(猶予ではなく data_ok)");
    TEST("H-5 昼休み(6723.T・^N225): JST 11:29:59 は取引中、11:30:00〜12:29:59 は終値、12:30:00 は取引中。^GSPC には昼休みが無い");
    {
        const uint32_t L0 = RS + 9000u, L1 = RS + 12600u;    /* 11:30 / 12:30 JST */
        uint8_t row;
        for (row = 0u; row < 2u; row++) {
            mk_eq(row, true, L0 - 1u, L0 - 900u, RS, RE, STK_MKT_TRADING, "11:29:59");
            mk_eq(row, true, L0, L0 - 900u, RS, RE, STK_MKT_CLOSED, "11:30:00");
            mk_eq(row, true, L0 + 1u, L0 - 900u, RS, RE, STK_MKT_CLOSED, "11:30:01");
            mk_eq(row, true, L1 - 1u, L0, RS, RE, STK_MKT_CLOSED, "12:29:59");
            mk_eq(row, true, L1, L0, RS, RE, STK_MKT_TRADING, "12:30:00(mtime = 11:30 のまま。旧規則では終値)");
            mk_eq(row, true, L1 + 899u, L0, RS, RE, STK_MKT_TRADING, "12:44:59(旧規則では終値)");
        }
        mk_eq(2, true, L0, L0 - 900u, RS, RE, STK_MKT_TRADING, "^GSPC 11:30:00(昼休みなし。同じ区間を使う試験用)");
        mk_eq(2, true, L0 + 1800u, L0 - 900u, RS, RE, STK_MKT_TRADING, "^GSPC 12:00");
    }
    TEST("H-5 1 日を 1 秒刻み(足は 15 分遅れ、昼休みは 11:30 で止まる)。設計 §5.4 の表: 09:00〜11:30 と 12:30〜15:30 が取引中、ほかは終値(6723.T・^N225)");
    {
        uint8_t row;
        for (row = 0u; row < 3u; row++) {
            bad = 0;
            for (t = 0u; t < 86400u; t++) {                  /* t = UTC の 00:00 からの秒 = JST 09:00 から */
                uint32_t nowu = RS + t, mt, feed;
                uint8_t want, got;
                int in_sess = (t < 23400u);
                int in_lunch = (t >= 9000u) && (t < 12600u);
                feed = (t >= 900u) ? (t - 900u) : 0u;        /* 足の時刻(RS からの秒) */
                if (t < 900u) { mt = RS - 70000u; }          /* 最初の足がまだ届かない: 前日の足 */
                else if (feed <= 9000u) { mt = RS + feed; }
                else if (feed < 12600u) { mt = RS + 9000u; } /* 前場の最後の足のまま */
                else if (feed <= 23400u) { mt = RS + feed; }
                else { mt = RS + 23400u; }
                want = (in_sess && !(in_lunch && (row != 2u))) ? STK_MKT_TRADING : STK_MKT_CLOSED;
                got = stk_market(stk_sym(row), true, nowu + JST_S, mt, RS, RE);
                if (got != want) { bad++; if (bad < 4) { printf("  row %u t=%lu: got %u want %u\n", (unsigned)row, (unsigned long)t, (unsigned)got, (unsigned)want); } }
            }
            CHECK_EQ(bad, 0);
        }
    }
    TEST("H-5 ニューヨーク(足の遅れなし): 14:30〜21:00 UTC だけ取引中(冬時間 = 09:30〜16:00 EST)。開場直後は猶予で取引中");
    {
        bad = 0;
        for (t = 0u; t < 86400u; t += 7u) {
            uint32_t nowu = GS - 20000u + t;
            uint8_t want = ((nowu >= GS) && (nowu < GE)) ? STK_MKT_TRADING : STK_MKT_CLOSED;
            uint32_t mt = (nowu >= GS) ? ((nowu < GE) ? nowu : GE) : (GS - 3600u);
            if (stk_market(stk_sym(2), true, nowu + JST_S, mt, GS, GE) != want) { bad++; }
            if ((nowu >= GS) && (nowu < GE) && (stk_market(stk_sym(2), true, nowu + JST_S, GS - 3600u, GS, GE) != ((nowu - GS <= 1800u) ? STK_MKT_TRADING : STK_MKT_CLOSED))) { bad++; }
        }
        CHECK_EQ(bad, 0);
    }
    TEST("H-5 時計が未同期・sym が NULL は NONE(ほかの値に依らない)");
    mk_eq(0, false, RS + 18u * 60u, RS + 3u * 60u, RS, RE, STK_MKT_NONE, "未同期(取引中の値)");
    mk_eq(0, false, 0u, 0u, 0u, 0u, STK_MKT_NONE, "未同期(0)");
    mk_eq(0, false, RE + 1u, RS, RS, RE, STK_MKT_NONE, "未同期(引け後)");
    CHECK_EQ(stk_market(NULL, true, RS + JST_S + 100u, RS, RS, RE), STK_MKT_NONE);
    TEST("H-5 不正な区間: re = rs、re < rs、区間が 2^31 以上は終値");
    mk_eq(0, true, RS, RS, RS, RS, STK_MKT_CLOSED, "re = rs");
    mk_eq(0, true, RS, RS, RS, RS - 1u, STK_MKT_CLOSED, "re < rs");
    mk_eq(2, true, 100u, 100u, 0u, 0x80000000u, STK_MKT_CLOSED, "区間 2^31");
    mk_eq(2, true, 100u, 100u, 0u, 0x7FFFFFFFu, STK_MKT_TRADING, "区間 2^31 - 1");
    TEST("H-5 32bit の巻き戻り: 区間が 0xFFFFFFFF をまたいでも差で比べる(^GSPC = 昼休みなし)");
    {
        const uint32_t S = 0xFFFFFF00u, E = 0x00000100u;     /* 区間 0x200 */
        mk_eq(2, true, 0xFFFFFFF0u, 0xFFFFFF80u, S, E, STK_MKT_TRADING, "wrap: now が巻き戻り前");
        mk_eq(2, true, 0x00000010u, 0xFFFFFFF0u, S, E, STK_MKT_TRADING, "wrap: now が巻き戻り後、mtime が前(差 32)");
        mk_eq(2, true, 0x000000FFu, 0xFFFFFF00u, S, E, STK_MKT_TRADING, "wrap: mtime = rs、now − mtime = 511");
        mk_eq(2, true, 0x00000100u, 0x000000F0u, S, E, STK_MKT_CLOSED, "wrap: now = re");
        mk_eq(2, true, 0xFFFFFEFFu, 0xFFFFFEF0u, S, E, STK_MKT_CLOSED, "wrap: now = rs - 1");
        mk_eq(2, true, 0x00000020u, 0xFFFFFEFFu, S, E, STK_MKT_TRADING, "wrap: mtime = rs - 1、now − rs = 288(猶予の中)");
        mk_eq(2, true, 0x00000020u, 0xFFFFFF00u, S, E, STK_MKT_TRADING, "wrap: mtime = rs");
        mk_eq(2, true, 0x000001FFu, 0xFFFFFEFFu, S, E, STK_MKT_CLOSED, "wrap: now が区間の外");
    }
}

int main(void)
{
    t_calc();
    t_fmt();
    t_market();
    TEST_REPORT("test_stk_core");
}
