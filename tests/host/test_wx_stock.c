/* test_wx_stock.c - 株価の応答の解析(src/app/wx_core.c の wx_parse_http)のホスト試験。docs/design.md §8 の H-3、H-4。
 *   H-3  実応答(tests/host/data/yahoo_*.http。2026-11-27 に PC から取得。状態行・date・content-type・本文だけ。個人に紐づくヘッダーは取り除いてある)の
 *        6 つ(6723.T・^N225・^GSPC x range=2d・1d)を、期待値(同じ本文を別の言語で独立に読んで作った値)と完全一致で解析する。
 *        ^N225 の regular は end が start より先。2d の直前の足、1d の chartPreviousClose
 *   H-4  頑健性: ① あらゆる長さに切った応答を、データの末尾/先頭を禁止ページに接して解析(ASan が使えない環境の代わり。範囲外を 1 バイトでも読むと異常終了)
 *        ② 合成の応答(null・指数・小数だけ・8 桁・キー 2 回・close の形・キー欠落・範囲外・騰落率の上限・currency・regularMarketTime)
 *        ③ HTTP の状態(301 / 429 / 404 / 500 / HTTP/1.1 / HTTP/2 / 形の違う状態行)④ date: の大文字小文字・壊れた date
 *        ⑤ 変異ファズ(1〜4 か所を壊す・切る)の不変条件 ⑥ 本文が結果に残らない
 * 使い方: test_wx_stock <data_dir>。-DAPP_WIFI_TEST=1 でコンパイルする(wx_core.c は APP_WIFI_ON = 0 では中身が消える)。 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "wx_core.h"
#include "stk_core.h"
#include "test_framework.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#define PG (4096u)
#define H200 "HTTP/1.0 200 OK\r\ncontent-type: application/json;charset=utf-8\r\ndate: Fri, 27 Nov 2026 04:04:13 GMT\r\n\r\n"
#define UTC_H200 (1795752253u)                 /* 上の date の UTC 秒 */

static const char *g_dir;
static uint32_t g_rs = 0x9E3779B9u;
static uint32_t rnd(void) { g_rs ^= g_rs << 13; g_rs ^= g_rs >> 17; g_rs ^= g_rs << 5; return g_rs; }

/* ---------------------------------------------------------------- ガードページ([禁止][データ][禁止]) */
static unsigned char *g_pg;

static int guard_init(void)
{
#ifdef _WIN32
    DWORD o;
    g_pg = (unsigned char *)VirtualAlloc(NULL, 3u * PG, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_pg == NULL) { return 0; }
    VirtualProtect(g_pg, PG, PAGE_NOACCESS, &o);
    VirtualProtect(g_pg + 2u * PG, PG, PAGE_NOACCESS, &o);
#else
    g_pg = (unsigned char *)mmap(NULL, 3u * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_pg == MAP_FAILED) { g_pg = NULL; return 0; }
    mprotect(g_pg, PG, PROT_NONE);
    mprotect(g_pg + 2u * PG, PG, PROT_NONE);
#endif
    return 1;
}

static const char *place(const void *src, size_t n, int at_end)       /* at_end = 1: データの終わりが禁止ページに接する */
{
    static char fallback[8192];
    unsigned char *d;

    if (g_pg == NULL) { d = (unsigned char *)fallback; } else { d = at_end ? (g_pg + 2u * PG - n) : (g_pg + PG); }
    if (n > 0u) { memcpy(d, src, n); }
    return (const char *)d;
}

/* ---------------------------------------------------------------- 実応答の読み込み(改行は CRLF にそろえる。git の autocrlf で LF になっていても通す) */
static size_t load(const char *name, char *out, size_t cap)
{
    char   path[600];
    char   raw[4096];
    FILE  *f;
    size_t n, i, k = 0u;

    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    f = fopen(path, "rb");
    if (f == NULL) { printf("  開けない: %s\n", path); return 0u; }
    n = fread(raw, 1u, sizeof raw, f);
    fclose(f);
    for (i = 0u; i < n; i++) {
        if (raw[i] == '\r') { continue; }
        if (raw[i] == '\n') { out[k++] = '\r'; }
        out[k++] = raw[i];
        if (k + 2u >= cap) { return 0u; }
    }
    out[k] = '\0';
    return k;
}

/* ---------------------------------------------------------------- 実応答の期待値(別の言語で本文を読んで作った。tests/host/data の本文と対) */
typedef struct {
    const char *file;
    int32_t     price, prev, meta_prev, bp;
    uint8_t     bar, cur, mkt;
    uint32_t    mtime, rs, re, utc;
} fx_t;

static const fx_t FX[] = {
    /* file                     price    prev    meta_prev  bp   bar cur         mkt                 mtime       rs          re          date(UTC)  */
    { "yahoo_6723T_2d.http",   256450,  243300, 243300,    540,  1, WX_CUR_JPY, STK_MKT_TRADING, 1795751335u, 1795737600u, 1795761000u, 1795752236u },
    { "yahoo_N225_2d.http",   5854187, 5749916, 5749916,   181,  1, WX_CUR_JPY, STK_MKT_TRADING, 1795751340u, 1795737600u, 1795761000u, 1795752244u },
    { "yahoo_GSPC_2d.http",    718278,  720109, 723717,    -25,  1, WX_CUR_USD, STK_MKT_CLOSED,  1795642326u, 1795617000u, 1795640400u, 1795752253u },
    { "yahoo_6723T_1d.http",   256200,  243300, 243300,    530,  0, WX_CUR_JPY, STK_MKT_TRADING, 1795751391u, 1795737600u, 1795761000u, 1795752293u },
    { "yahoo_N225_1d.http",   5853442, 5749916, 5749916,   180,  0, WX_CUR_JPY, STK_MKT_TRADING, 1795751400u, 1795737600u, 1795761000u, 1795752303u },
    { "yahoo_GSPC_1d.http",    718278,  720109, 720109,    -25,  0, WX_CUR_USD, STK_MKT_CLOSED,  1795642326u, 1795617000u, 1795640400u, 1795752312u }
};
#define NFX (sizeof FX / sizeof FX[0])

static char   g_fx[NFX][4096];
static size_t g_fxn[NFX];

static void t_h3(void)
{
    size_t i;

    TEST("H-3 実応答 6 つ(3 銘柄 x range=2d / 1d)の期待値との完全一致");
    for (i = 0u; i < NFX; i++) {
        const fx_t *f = &FX[i];
        wx_result_t r;
        stk_calc_t  c;
        bool        ok;

        g_fxn[i] = load(f->file, g_fx[i], sizeof g_fx[i]);
        CHECK(g_fxn[i] > 1000u);
        memset(&r, 0xA5, sizeof r);
        ok = wx_parse_http(place(g_fx[i], g_fxn[i], 1), g_fxn[i], &r);
        if (!ok) { printf("  %s: 失敗 err=%u http=%u\n", f->file, (unsigned)r.err, (unsigned)r.http); }
        CHECK(ok); CHECK_EQ(r.ok, 1); CHECK_EQ(r.err, 0); CHECK_EQ(r.http, 200);
        CHECK_EQ(r.price_x100, f->price); CHECK_EQ(r.prev_x100, f->prev); CHECK_EQ(r.has_bar_prev, f->bar);
        CHECK_EQ(r.mtime, f->mtime); CHECK_EQ(r.rs, f->rs); CHECK_EQ(r.re, f->re); CHECK_EQ(r.cur, f->cur);
        CHECK_EQ(r.have_date, 1); CHECK_EQ(r.jst_s, f->utc + 32400u);
        CHECK(stk_calc(r.price_x100, r.prev_x100, &c)); CHECK_EQ(c.pct_bp, f->bp);
        CHECK_EQ(stk_market(stk_sym((uint8_t)(i % 3u)), true, f->utc + 32400u, r.mtime, r.rs, r.re), f->mkt);   /* 並びは 6723.T・^N225・^GSPC の繰り返し */
        /* 解析が触らない項目(wifi_core が入れる)は元のまま */
        CHECK_EQ(r.sym, 0xA5); CHECK_EQ(r.closed, 0xA5); CHECK_EQ(r.ms, 0xA5A5A5A5u); CHECK_EQ(r.first_ms, 0xA5A5A5A5u); CHECK_EQ(r.len, 0xA5A5);
        /* バッファに NUL 終端が無くてよい(末尾を禁止ページに接して渡している)。先頭に接しても同じ */
        memset(&r, 0, sizeof r);
        CHECK(wx_parse_http(place(g_fx[i], g_fxn[i], 0), g_fxn[i], &r)); CHECK_EQ(r.price_x100, f->price); CHECK_EQ(r.prev_x100, f->prev);
    }
    TEST("H-3 ^N225 の regular は end が start より先(順序に依らず読める)。2d は直前の足、1d は chartPreviousClose");
    CHECK(strstr(g_fx[1], "\"regular\":{\"timezone\":\"JST\",\"end\":") != NULL);          /* ^N225 は end が先 */
    CHECK(strstr(g_fx[0], "\"regular\":{\"timezone\":\"JST\",\"start\":") != NULL);        /* 6723.T は start が先 */
    CHECK_EQ(FX[2].prev, 720109); CHECK(FX[2].meta_prev != FX[2].prev);                    /* ^GSPC 2d: 直前の足(7201.09)≠ chartPreviousClose(7237.17) */
}

/* ---------------------------------------------------------------- H-4 ① あらゆる長さに切る */
static void t_h4_cut(void)
{
    size_t i, len, bad_n;
    int    at;
    long   bad = 0;
    int    full_ok = 0;

    TEST("H-4 ① 6 つの実応答を 0〜全長に切って、末尾/先頭を禁止ページに接して解析(範囲外を読むと異常終了)。全長より短い先頭部分は、どの長さでも成功にならない(S32 の SUSPECT-1)");
    if (!guard_init()) { printf("  (ガードページを作れない: 普通のバッファで試験する)\n"); }
    for (i = 0u; i < NFX; i++) {
        const fx_t *f = &FX[i];
        const char *t = g_fx[i];
        size_t hdr_end = (size_t)(strstr(t, "\r\n\r\n") - t) + 4u;      /* 本文の始まり */
        size_t cl = (size_t)(strstr(strstr(t, "\"indicators\":"), "\"close\":[") - t) + 9u;   /* close 配列の中身の始まり */
        size_t br = (size_t)(strchr(t + cl, ']') - t);                    /* close 配列の ] の位置 */
        size_t n_cl3 = 0u;

        CHECK(hdr_end > 100u && cl > hdr_end && br > cl);
        for (len = 0u; len <= g_fxn[i]; len++) {
            for (at = 0; at < 2; at++) {
                wx_result_t r;
                bool ok = wx_parse_http(place(t, len, at), len, &r);

                if (len == g_fxn[i]) {                                   /* 全長は成功 */
                    if (!ok || r.ok != 1u || r.err != 0u || r.http != 200u || r.price_x100 != f->price || r.prev_x100 != f->prev || r.has_bar_prev != f->bar) { bad++; }
                    full_ok++;
                    continue;
                }
                if (ok || (r.ok != 0u)) { bad++; continue; }              /* どの途切れも成功にしない */
                if ((r.err != 3u) && (r.err != 5u)) { bad++; continue; }
                if ((len >= 12u) && (r.http != 200u)) { bad++; }
                /* 段 3: 状態行まで届かない・空行が無い = 3。本文が 0 バイト = 5(JSON でない)。本文が { で始まって閉じない = 3 */
                if ((len < hdr_end) && (r.err != 3u) && (len != hdr_end)) { bad++; }
                if ((len == hdr_end) && (r.err != 5u)) { bad++; }
                if ((len > hdr_end) && (r.err != 3u)) { bad++; }
                if ((len >= cl) && (len <= br) && (r.err == 3u)) { n_cl3++; }
            }
        }
        bad_n = (br - cl + 1u) * 2u;                                     /* close 配列の中身の途中(] の手前まで)で切った長さ x 2 通りの置き方 */
        if (bad != 0) { printf("  %s: 不一致 %ld 件\n", f->file, bad); }
        CHECK_EQ(bad, 0);
        CHECK_EQ(n_cl3, bad_n);                                          /* それらはすべて err = 3(GSPC_2d では 31 通りの ] の手前を含む) */
        bad = 0;
    }
    CHECK_EQ(full_ok, (int)(NFX * 2u));
    TEST("H-4 ① 全長 + 末尾の空白・改行は成功。全長 + 余計な文字は失敗(err = 3)");
    {
        static char big[4200];
        size_t k;
        for (k = 0u; k < NFX; k++) {
            wx_result_t r;
            size_t n = g_fxn[k];
            memcpy(big, g_fx[k], n);
            memcpy(big + n, " \r\n\t ", 5u);
            CHECK(wx_parse_http(place(big, n + 5u, 1), n + 5u, &r)); CHECK_EQ(r.price_x100, FX[k].price);
            big[n] = '}';
            CHECK(!wx_parse_http(place(big, n + 1u, 1), n + 1u, &r)); CHECK_EQ(r.err, 3);
            big[n] = 'x';
            CHECK(!wx_parse_http(place(big, n + 1u, 1), n + 1u, &r)); CHECK_EQ(r.err, 3);
        }
    }
}

/* stk_json_complete(設計 §5.2 段 3b)の単体 */
static void t_json_complete(void)
{
    static const struct { const char *s; int want; } J[] = {
        { "", 0 }, { "{", 0 }, { "}", 0 }, { "{}", 1 }, { " {} ", 1 }, { "{}\r\n", 1 }, { "{} x", 0 }, { "{}}", 0 }, { "{}{}", 0 }, { "{} {", 0 },
        { "{\"a\":\"}\"}", 1 }, { "{\"a\":\"}\"", 0 }, { "{\"a\":\"{\"}", 1 }, { "{\"a\":\"[\"}", 1 },
        { "{\"a\":\"\\\"}", 0 },                 /* {"a":"\"}  = 文字列が閉じない */
        { "{\"a\":\"\\\\\"}", 1 },               /* {"a":"\\"} = 閉じる */
        { "{\"a\":[1,2]}", 1 }, { "{\"a\":[1,2}", 0 }, { "{\"a\":[1,2", 0 }, { "{\"a\":[", 0 }, { "[", 0 }, { "]", 0 }, { "[]", 1 }, { "[[]]", 1 }, { "[[]", 0 },
        { "{\"a\":{\"b\":{\"c\":[{}]}}}", 1 }, { "{\"a\":{\"b\":{\"c\":[{}]}}", 0 }, { "\"x\"", 0 }, { "123", 0 }, { "{\"a\"", 0 }, { "{\"a\":\"", 0 }
    };
    size_t i;
    char   deep[300];

    TEST("H-4 ① stk_json_complete の単体: 空・{・{}・文字列の中の }・エスケープ・余分な閉じ・閉じた後の文字・入れ子・配列だけ");
    for (i = 0u; i < sizeof J / sizeof J[0]; i++) {
        bool g = stk_json_complete(J[i].s, strlen(J[i].s));
        if ((int)g != J[i].want) { printf("  [%s]: got %d want %d\n", J[i].s, (int)g, J[i].want); }
        CHECK_EQ((int)g, J[i].want);
    }
    CHECK(stk_json_complete("{}xxxx", 2u));                         /* n で止まる(NUL 終端を仮定しない) */
    CHECK(!stk_json_complete("{}", 1u));
    memset(deep, '{', 100); memset(deep + 100, '}', 100);
    CHECK(stk_json_complete(deep, 200u)); CHECK(!stk_json_complete(deep, 199u)); CHECK(!stk_json_complete(deep, 100u));
    memset(deep, '[', 120); memset(deep + 120, ']', 119); deep[239] = 'x';
    CHECK(!stk_json_complete(deep, 240u));                          /* 閉じが 1 つ足りない */
}

/* ---------------------------------------------------------------- H-4 ② 合成の応答 */
typedef struct {
    const char *hdr;
    const char *cur_kv;       /* "currency":"JPY",(空 = キーなし) */
    const char *mtime, *price, *prev, *rs, *re, *close;
    int         end_first;
} tpl_t;

static const tpl_t T0 = { H200, "\"currency\":\"JPY\",", "1795751335", "2564.5", "2433.0", "1795737600", "1795761000", "[2433.0,2564.5]", 0 };

static size_t build(char *b, size_t cap, const tpl_t *t)
{
    char reg[96];

    if (t->end_first) { snprintf(reg, sizeof reg, "\"end\":%s,\"start\":%s", t->re, t->rs); }
    else              { snprintf(reg, sizeof reg, "\"start\":%s,\"end\":%s", t->rs, t->re); }
    return (size_t)snprintf(b, cap, "%s{\"chart\":{\"result\":[{\"meta\":{%s\"symbol\":\"X\",\"regularMarketTime\":%s,\"regularMarketPrice\":%s,"
                            "\"chartPreviousClose\":%s,\"priceHint\":2,\"currentTradingPeriod\":{\"pre\":{\"timezone\":\"JST\",\"start\":1,\"end\":2,\"gmtoffset\":0},"
                            "\"regular\":{\"timezone\":\"JST\",%s,\"gmtoffset\":0},\"post\":{\"timezone\":\"JST\",\"start\":3,\"end\":4,\"gmtoffset\":0}},"
                            "\"dataGranularity\":\"1d\"},\"timestamp\":[1,2],\"indicators\":{\"quote\":[{\"close\":%s}],\"adjclose\":[{\"adjclose\":[7,8]}]}}],\"error\":null}}",
                            t->hdr, t->cur_kv, t->mtime, t->price, t->prev, reg, t->close);
}

static bool P(const tpl_t *t, wx_result_t *r)
{
    char   b[1800];
    size_t n = build(b, sizeof b, t);
    return wx_parse_http(place(b, n, 1), n, r);
}

static bool Praw(const char *s, wx_result_t *r)
{
    size_t n = strlen(s);
    return wx_parse_http(place(s, n, 1), n, r);
}

#define EXPECT_OK(t, pr, pv, bar) do { wx_result_t r_; bool ok_ = P((t), &r_); \
    if (!ok_ || r_.price_x100 != (pr) || r_.prev_x100 != (pv) || r_.has_bar_prev != (bar)) { printf("  line %d: ok=%d err=%u price=%ld prev=%ld bar=%u\n", __LINE__, (int)ok_, (unsigned)r_.err, (long)r_.price_x100, (long)r_.prev_x100, (unsigned)r_.has_bar_prev); } \
    CHECK(ok_); CHECK_EQ(r_.price_x100, (pr)); CHECK_EQ(r_.prev_x100, (pv)); CHECK_EQ(r_.has_bar_prev, (bar)); } while (0)
#define EXPECT_ERR(t, e) do { wx_result_t r_; bool ok_ = P((t), &r_); \
    if (ok_ || r_.err != (e) || r_.ok != 0u) { printf("  line %d: ok=%d err=%u (want %d)\n", __LINE__, (int)ok_, (unsigned)r_.err, (int)(e)); } \
    CHECK(!ok_); CHECK_EQ(r_.err, (e)); CHECK_EQ(r_.ok, 0); } while (0)

static void t_h4_syn(void)
{
    tpl_t t;
    wx_result_t r;

    TEST("H-4 ② 基準の合成応答は成功(price 2564.5 → 256450、直前の足 2433.0 → 243300)");
    t = T0; EXPECT_OK(&t, 256450, 243300, 1);
    t = T0; t.end_first = 1; EXPECT_OK(&t, 256450, 243300, 1);
    t = T0; CHECK(P(&t, &r)); CHECK_EQ(r.rs, 1795737600u); CHECK_EQ(r.re, 1795761000u);     /* pre / post の start・end(1,2,3,4)ではなく regular の値 */
    t = T0; t.end_first = 1; CHECK(P(&t, &r)); CHECK_EQ(r.rs, 1795737600u); CHECK_EQ(r.re, 1795761000u);

    TEST("H-4 ② 価格の読み取り: 小数 3 桁目の四捨五入(絶対値)、4 桁目以降は無視、桁数、符号");
    {
        static const struct { const char *s; int32_t v; } PR[] = {
            { "2564.5", 256450 }, { "2564", 256400 }, { "2564.0", 256400 }, { "2564.05", 256405 }, { "2564.50", 256450 }, { "2564.504", 256450 }, { "2564.505", 256451 },
            { "2564.994", 256499 }, { "2564.995", 256500 }, { "2564.999", 256500 }, { "2564.9951234", 256500 }, { "57674.359375", 5767436 }, { "7201.08984375", 720109 },
            { "7182.77978515625", 718278 }, { "0.01", 1 }, { "0.005", 1 }, { "0.0049999", 0 + 0 }, { "0.5", 50 }, { "007.5", 750 }, { "999999.99", 99999999 },
            { "999999.994", 99999999 }, { "5000.4449", 500044 }, { "5000.445", 500045 }
        };
        size_t k;
        for (k = 0u; k < sizeof PR / sizeof PR[0]; k++) {
            t = T0; t.price = PR[k].s; t.prev = PR[k].s; t.close = "[null,null]";          /* prev も同じ値(騰落率の上限に掛からない) */
            if (PR[k].v < 1) { EXPECT_ERR(&t, 6u); } else { EXPECT_OK(&t, PR[k].v, PR[k].v, 0); }
        }
    }
    TEST("H-4 ② 価格が読めない(null・指数・数字なし・小数だけ・8 桁・符号だけ・文字列・空)は err=5");
    {
        static const char *const BAD[] = { "null", "1e3", "1E3", "2564.5e0", "-", "", ".5", "5.", "-.5", "12345678", "12345678.5", "\"2564.5\"", "true", "[1]", "{}", "NaN", "0x10", "+5", "--1", "1.2.3" };
        size_t k;
        for (k = 0u; k < sizeof BAD / sizeof BAD[0]; k++) {
            t = T0; t.price = BAD[k]; EXPECT_ERR(&t, 5u);
        }
    }
    TEST("H-4 ② 範囲外は err=6: 価格 0・負・100,000,000 以上(7 桁の整数部)、前日の終値 0・負・巨大、|騰落率| 上限");
    t = T0; t.price = "0"; EXPECT_ERR(&t, 6u);
    t = T0; t.price = "0.004"; EXPECT_ERR(&t, 6u);
    t = T0; t.price = "-1.5"; EXPECT_ERR(&t, 6u);
    t = T0; t.price = "-0"; EXPECT_ERR(&t, 6u);
    t = T0; t.price = "1000000"; EXPECT_ERR(&t, 6u);               /* 7 桁の整数部は読める(×100 = 100,000,000)が範囲外 */
    t = T0; t.price = "1000000.00"; EXPECT_ERR(&t, 6u);
    t = T0; t.price = "9999999.99"; EXPECT_ERR(&t, 6u);
    t = T0; t.prev = "0"; t.close = "[null,null]"; EXPECT_ERR(&t, 6u);                     /* 前日の終値 0(chartPreviousClose が採用される) */
    t = T0; t.prev = "-5.0"; t.close = "[null,2564.5]"; EXPECT_ERR(&t, 6u);
    t = T0; t.prev = "1000000"; t.close = "[null,null]"; EXPECT_ERR(&t, 6u);
    t = T0; t.close = "[0,2564.5]"; EXPECT_ERR(&t, 6u);                                    /* 直前の足が 0(採用されて範囲外) */
    t = T0; t.close = "[0.004,2564.5]"; EXPECT_ERR(&t, 6u);
    t = T0; t.price = "1099.99"; t.prev = "100.00"; t.close = "[null,null]"; EXPECT_OK(&t, 109999, 10000, 0);       /* +999.99% ちょうど */
    t = T0; t.price = "1100.00"; t.prev = "100.00"; t.close = "[null,null]"; EXPECT_ERR(&t, 6u);                    /* +1000.00% */
    t = T0; t.price = "999999.99"; t.prev = "0.01"; t.close = "[null,null]"; EXPECT_ERR(&t, 6u);
    t = T0; t.price = "0.01"; t.prev = "999999.99"; t.close = "[null,null]"; EXPECT_OK(&t, 1, 99999999, 0);        /* 下落は -100.00% が下限 */
    t = T0; t.price = "999999.99"; t.prev = "999999.99"; t.close = "[null,null]"; EXPECT_OK(&t, 99999999, 99999999, 0);

    TEST("H-4 ② 同じキーが 2 回: 最初の出現を使う(meta は本文の先頭にある)");
    t = T0; t.price = "100.0,\"regularMarketPrice\":2564.5"; EXPECT_OK(&t, 10000, 243300, 1);
    t = T0; t.prev = "2433.0,\"chartPreviousClose\":1.0"; t.close = "[null,null]"; EXPECT_OK(&t, 256450, 243300, 0);
    t = T0; t.mtime = "1795751335,\"regularMarketTime\":5"; CHECK(P(&t, &r)); CHECK_EQ(r.mtime, 1795751335u);

    TEST("H-4 ② close 配列: 最後から 2 番目が数値なら直前の足。null・1 要素・空・途切れ・読めない要素は chartPreviousClose");
    t = T0; t.close = "[null,null]"; EXPECT_OK(&t, 256450, 243300, 0);
    t = T0; t.close = "[2400,null]"; EXPECT_OK(&t, 256450, 240000, 1);                           /* 最後が null でも、直前の足が数値なら使う */
    t = T0; t.close = "[null,5]"; EXPECT_OK(&t, 256450, 243300, 0);
    t = T0; t.close = "[2433.0]"; EXPECT_OK(&t, 256450, 243300, 0);
    t = T0; t.close = "[]"; EXPECT_OK(&t, 256450, 243300, 0);
    t = T0; t.close = "[2400,2410,2420]"; EXPECT_OK(&t, 256450, 241000, 1);                            /* 要素が 3 つ以上でも、最後から 2 番目 */
    t = T0; t.close = "[2400,2401,2402,2403,2404,2405,2406,2407,2408]"; EXPECT_OK(&t, 256450, 240700, 1);
    t = T0; t.close = "[null,2400,null]"; EXPECT_OK(&t, 256450, 240000, 1);
    t = T0; t.close = "[2400,null,2420]"; EXPECT_OK(&t, 256450, 243300, 0);                      /* 最後から 2 番目が null */
    t = T0; t.close = "[2433.0,abc]"; EXPECT_OK(&t, 256450, 243300, 0);                    /* 読めない要素 */
    t = T0; t.close = "[1e3,2]"; EXPECT_OK(&t, 256450, 243300, 0);
    t = T0; t.close = "[12345678,2]"; EXPECT_OK(&t, 256450, 243300, 0);                    /* 整数部 8 桁の要素 */
    t = T0; t.close = "[2433.0,2564.5"; EXPECT_ERR(&t, 3u);                               /* ] が無い = JSON が閉じない = 途切れ(段 3b。S32 の SUSPECT-1) */
    t = T0; t.close = "[2433.0 ,2564.5]"; EXPECT_OK(&t, 256450, 243300, 0);                /* 空白は読めない(Yahoo の出力にはない)→ 直前の足なし */
    t = T0; t.close = "[2433.0, 2564.5]"; EXPECT_OK(&t, 256450, 243300, 0);                /* 要素の前の空白は読めない(Yahoo の出力にはない)→ 直前の足なし */
    t = T0; t.close = "null"; EXPECT_OK(&t, 256450, 243300, 0);                            /* 配列でない */
    t = T0; t.close = "[2433.0,2564.5,]"; EXPECT_OK(&t, 256450, 243300, 0);                /* 末尾のカンマ(JSON として不正) */
    t = T0; t.close = "[2433.0,2564.5]]"; EXPECT_ERR(&t, 3u);                              /* 余分な閉じ = 閉じた後に文字が続く(段 3b) */
    t = T0; t.close = "[2433.0]"; EXPECT_OK(&t, 256450, 243300, 0);                        /* 1 本だけの足が閉じている(range=2d の ^N225)は正当 */    {
        char b[1800];
        size_t n = build(b, sizeof b, &T0);
        memcpy(strstr(b, "\"indicators\":") + 1, "X", 1u);                                 /* indicators が無い */
        CHECK(wx_parse_http(place(b, n, 1), n, &r)); CHECK_EQ(r.has_bar_prev, 0); CHECK_EQ(r.prev_x100, 243300);
        n = build(b, sizeof b, &T0);
        memcpy(strstr(b, "\"close\":[") + 1, "X", 1u);                                     /* close が無い */
        CHECK(wx_parse_http(place(b, n, 1), n, &r)); CHECK_EQ(r.has_bar_prev, 0); CHECK_EQ(r.prev_x100, 243300);
    }

    TEST("H-4 ② キー欠落(価格・chartPreviousClose・regularMarketTime・regular・start・end)は err=5。currency の欠落は成功(cur = NONE)");
    {
        static const char *const KEY[] = { "\"regularMarketPrice\":", "\"chartPreviousClose\":", "\"regularMarketTime\":", "\"regular\":{" };
        char b[1800];
        size_t n, k;
        for (k = 0u; k < 4u; k++) {
            n = build(b, sizeof b, &T0);
            memcpy(strstr(b, KEY[k]) + 1, "X", 1u);                                       /* "regularMarketPrice" → "Xegular..." */
            CHECK(!wx_parse_http(place(b, n, 1), n, &r)); CHECK_EQ(r.err, 5); CHECK_EQ(r.http, 200); CHECK_EQ(r.ok, 0);
        }
        n = build(b, sizeof b, &T0);
        memcpy(strstr(strstr(b, "\"regular\":{"), "\"start\":") + 1, "X", 1u);
        CHECK(!wx_parse_http(place(b, n, 1), n, &r)); CHECK_EQ(r.err, 5);
        n = build(b, sizeof b, &T0);
        memcpy(strstr(strstr(b, "\"regular\":{"), "\"end\":") + 1, "X", 1u);
        CHECK(!wx_parse_http(place(b, n, 1), n, &r)); CHECK_EQ(r.err, 5);               /* pre / post の end は使わない */
        n = build(b, sizeof b, &T0);
        memcpy(strstr(b, "\"currency\":") + 1, "X", 1u);
        CHECK(wx_parse_http(place(b, n, 1), n, &r)); CHECK_EQ(r.cur, WX_CUR_NONE);
    }
    TEST("H-4 ② regularMarketTime / start / end: UTC 秒(符号・小数なし。uint32 に収まる 1〜10 桁)");
    t = T0; t.mtime = "4294967295"; CHECK(P(&t, &r)); CHECK_EQ(r.mtime, 4294967295u);
    t = T0; t.mtime = "0"; CHECK(P(&t, &r)); CHECK_EQ(r.mtime, 0u);
    t = T0; t.mtime = "4294967296"; EXPECT_ERR(&t, 5u);
    t = T0; t.mtime = "12345678901"; EXPECT_ERR(&t, 5u);
    t = T0; t.mtime = "-1"; EXPECT_ERR(&t, 5u);
    t = T0; t.mtime = "1795751335.5"; EXPECT_ERR(&t, 5u);
    t = T0; t.mtime = "1.7957e9"; EXPECT_ERR(&t, 5u);
    t = T0; t.mtime = "null"; EXPECT_ERR(&t, 5u);
    t = T0; t.mtime = ""; EXPECT_ERR(&t, 5u);
    t = T0; t.rs = "null"; EXPECT_ERR(&t, 5u);
    t = T0; t.re = "-5"; EXPECT_ERR(&t, 5u);
    t = T0; t.re = "17957.5"; EXPECT_ERR(&t, 5u);
    t = T0; t.rs = "4294967295"; t.re = "4294967296"; EXPECT_ERR(&t, 5u);

    TEST("H-4 ② currency: JPY / USD / その他 / キーなし / 3 文字でない");
    t = T0; CHECK(P(&t, &r)); CHECK_EQ(r.cur, WX_CUR_JPY);
    t = T0; t.cur_kv = "\"currency\":\"USD\","; CHECK(P(&t, &r)); CHECK_EQ(r.cur, WX_CUR_USD);
    t = T0; t.cur_kv = "\"currency\":\"EUR\","; CHECK(P(&t, &r)); CHECK_EQ(r.cur, WX_CUR_OTHER);
    t = T0; t.cur_kv = ""; CHECK(P(&t, &r)); CHECK_EQ(r.cur, WX_CUR_NONE); CHECK_EQ(r.ok, 1);
    t = T0; t.cur_kv = "\"currency\":\"JPYX\","; CHECK(P(&t, &r)); CHECK_EQ(r.cur, WX_CUR_NONE);
    t = T0; t.cur_kv = "\"currency\":null,"; CHECK(P(&t, &r)); CHECK_EQ(r.cur, WX_CUR_NONE);
}

/* ---------------------------------------------------------------- H-4 ③ HTTP の状態・④ date: */
static void t_h4_http(void)
{
    wx_result_t r;
    char b[2000];
    static const char BODY[] = "{\"chart\":{\"result\":[{\"meta\":{\"currency\":\"JPY\",\"regularMarketTime\":1795751335,\"regularMarketPrice\":2564.5,"
                               "\"chartPreviousClose\":2433.0,\"currentTradingPeriod\":{\"regular\":{\"start\":1795737600,\"end\":1795761000}}},"
                               "\"indicators\":{\"quote\":[{\"close\":[2433.0,2564.5]}]}}]}}";
    static const struct { const char *st; uint16_t http; uint8_t err; } S[] = {
        { "HTTP/1.0 301 Moved Permanently", 301, 4 }, { "HTTP/1.0 429 Too Many Requests", 429, 4 }, { "HTTP/1.1 404 Not Found", 404, 4 },
        { "HTTP/1.0 500 Internal Server Error", 500, 4 }, { "HTTP/1.0 503 Service Unavailable", 503, 4 }, { "HTTP/1.0 403 Forbidden", 403, 4 },
        { "HTTP/1.0 204 No Content", 204, 4 }, { "HTTP/1.0 206 Partial Content", 206, 4 }, { "HTTP/1.0 100 Continue", 100, 4 }, { "HTTP/1.0 999 X", 999, 4 },
        { "HTTP/1.0 000 X", 0, 4 }
    };
    size_t i;

    TEST("H-4 ③ HTTP の状態: 200 以外は err=4 で http を記録(本文が正しくても成功にしない)。Date: は拾う");
    for (i = 0u; i < sizeof S / sizeof S[0]; i++) {
        snprintf(b, sizeof b, "%s\r\ndate: Fri, 27 Nov 2026 04:04:13 GMT\r\ncontent-type: application/json\r\n\r\n%s", S[i].st, BODY);
        CHECK(!Praw(b, &r)); CHECK_EQ(r.err, S[i].err); CHECK_EQ(r.http, S[i].http); CHECK_EQ(r.ok, 0);
        CHECK_EQ(r.have_date, 1); CHECK_EQ(r.jst_s, UTC_H200 + 32400u);
    }
    TEST("H-4 ③ 429(本文は短い文字列)・301(本文なし)");
    CHECK(!Praw("HTTP/1.0 429 Too Many Requests\r\ncontent-type: text/plain\r\ndate: Fri, 27 Nov 2026 04:04:13 GMT\r\n\r\nToo Many Requests", &r));
    CHECK_EQ(r.err, 4); CHECK_EQ(r.http, 429); CHECK_EQ(r.have_date, 1);
    CHECK(!Praw("HTTP/1.0 301 Moved Permanently\r\nlocation: https://x/\r\ndate: Fri, 27 Nov 2026 04:04:13 GMT\r\n\r\n", &r));
    CHECK_EQ(r.err, 4); CHECK_EQ(r.http, 301);
    TEST("H-4 ③ HTTP/1.1 の 200 は成功。HTTP/2・形の違う状態行は err=4(http = 0)。12 バイト未満・NULL は err=3");
    snprintf(b, sizeof b, "HTTP/1.1 200 OK\r\ndate: Fri, 27 Nov 2026 04:04:13 GMT\r\n\r\n%s", BODY);
    CHECK(Praw(b, &r)); CHECK_EQ(r.price_x100, 256450); CHECK_EQ(r.prev_x100, 243300); CHECK_EQ(r.has_bar_prev, 1); CHECK_EQ(r.http, 200);
    snprintf(b, sizeof b, "HTTP/2 200 \r\n\r\n%s", BODY);
    CHECK(!Praw(b, &r)); CHECK_EQ(r.err, 4); CHECK_EQ(r.http, 0);
    snprintf(b, sizeof b, "HTTP/1.2 200 OK\r\n\r\n%s", BODY);
    CHECK(!Praw(b, &r)); CHECK_EQ(r.err, 4);
    snprintf(b, sizeof b, "HTTP/1.0200 OK\r\n\r\n%s", BODY);
    CHECK(!Praw(b, &r)); CHECK_EQ(r.err, 4);
    snprintf(b, sizeof b, "XTTP/1.0 200 OK\r\n\r\n%s", BODY);
    CHECK(!Praw(b, &r)); CHECK_EQ(r.err, 4);
    snprintf(b, sizeof b, "HTTP/1.0 20 OK\r\n\r\n%s", BODY);
    CHECK(!Praw(b, &r)); CHECK_EQ(r.err, 4);
    CHECK(!Praw("", &r)); CHECK_EQ(r.err, 3);
    CHECK(!Praw("HTTP/1.0 200", &r)); CHECK_EQ(r.err, 3); CHECK_EQ(r.http, 200);
    CHECK(!Praw("HTTP/1.0 20", &r)); CHECK_EQ(r.err, 3);
    CHECK(!wx_parse_http(NULL, 0u, &r)); CHECK_EQ(r.err, 3); CHECK_EQ(r.ok, 0);
    TEST("H-4 ③ 200 でも本文が無い・JSON でない・Yahoo のエラー本文は err=5");
    CHECK(!Praw("HTTP/1.0 200 OK\r\ndate: Fri, 27 Nov 2026 04:04:13 GMT\r\n\r\n", &r)); CHECK_EQ(r.err, 5); CHECK_EQ(r.http, 200); CHECK_EQ(r.have_date, 1);
    CHECK(!Praw("HTTP/1.0 200 OK\r\ndate: Fri, 27 Nov 2026 04:04:13 GMT\r\n", &r)); CHECK_EQ(r.err, 3); CHECK_EQ(r.http, 200); CHECK_EQ(r.have_date, 1);   /* 空行まで届かない(段 3) */
    CHECK(!Praw("HTTP/1.0 200 OK\r\n\r\n<html><body>Error</body></html>", &r)); CHECK_EQ(r.err, 5);
    CHECK(!Praw("HTTP/1.0 200 OK\r\n\r\n{\"chart\":{\"result\":null,\"error\":{\"code\":\"Not Found\",\"description\":\"No data found\"}}}", &r)); CHECK_EQ(r.err, 5);
    CHECK(!Praw("HTTP/1.0 200 OK", &r)); CHECK_EQ(r.err, 3);
    CHECK(!Praw("HTTP/1.0 200 OK\r\n\r\n   ", &r)); CHECK_EQ(r.err, 5);                       /* 本文が空白だけ(JSON でない) */
    CHECK(!Praw("HTTP/1.0 200 OK\r\n\r\n{\"chart\":", &r)); CHECK_EQ(r.err, 3);              /* { で始まるが閉じない */
    CHECK(!Praw("HTTP/1.0 200 OK\r\n\r\n[]", &r)); CHECK_EQ(r.err, 5);                       /* { で始まらない */

    TEST("H-4 ④ date: の大文字小文字(date: / Date: / DATE:)は区別しない。壊れた・無い date: は have_date = 0 でも株価は成功");
    {
        static const char *const D[] = { "date: Fri, 27 Nov 2026 04:04:13 GMT", "Date: Fri, 27 Nov 2026 04:04:13 GMT", "DATE: Fri, 27 Nov 2026 04:04:13 GMT", "dAtE:Fri, 27 Nov 2026 04:04:13 GMT" };
        static const char *const X[] = { "date: garbage", "date: Fri, 27 Nov 2026 04:04:13", "date: Fri, 32 Nov 2026 04:04:13 GMT", "date: Fri, 27 Xxx 2026 04:04:13 GMT", "x-date: Fri, 27 Nov 2026 04:04:13 GMT", "x-note: none" };
        size_t k;
        for (k = 0u; k < sizeof D / sizeof D[0]; k++) {
            snprintf(b, sizeof b, "HTTP/1.0 200 OK\r\n%s\r\ncontent-type: x\r\n\r\n%s", D[k], BODY);
            CHECK(Praw(b, &r)); CHECK_EQ(r.have_date, 1); CHECK_EQ(r.jst_s, UTC_H200 + 32400u);
        }
        for (k = 0u; k < sizeof X / sizeof X[0]; k++) {
            snprintf(b, sizeof b, "HTTP/1.0 200 OK\r\n%s\r\ncontent-type: x\r\n\r\n%s", X[k], BODY);
            CHECK(Praw(b, &r)); CHECK_EQ(r.have_date, 0); CHECK_EQ(r.jst_s, 0); CHECK_EQ(r.price_x100, 256450);
        }
        /* 本文の中の date: は読まない */
        snprintf(b, sizeof b, "HTTP/1.0 200 OK\r\ncontent-type: x\r\n\r\n{\"note\":\"\r\ndate: Fri, 27 Nov 2026 04:04:13 GMT\",%s", BODY + 1);
        CHECK(Praw(b, &r)); CHECK_EQ(r.have_date, 0); CHECK_EQ(r.price_x100, 256450);
    }
}

/* ---------------------------------------------------------------- H-4 ⑤ 変異ファズ・⑥ 本文が結果に残らない */
static int inv(const wx_result_t *r, bool ok)
{
    stk_calc_t c;

    if (ok) {
        return (r->ok == 1u) && (r->err == 0u) && (r->http == 200u) && (r->price_x100 >= 1) && (r->price_x100 <= STK_X100_MAX)
               && (r->prev_x100 >= 1) && (r->prev_x100 <= STK_X100_MAX) && stk_calc(r->price_x100, r->prev_x100, &c) && (r->cur <= (uint8_t)WX_CUR_OTHER)
               && (r->has_bar_prev <= 1u);
    }
    return (r->ok == 0u) && (r->err >= 3u) && (r->err <= 6u);
}

static void t_h4_fuzz(void)
{
    static const char poison[] = { '0', '9', '-', '.', '"', ':', ',', '}', '{', ']', '[', '\r', '\n', ' ', 'e', 'E', 'n', 'u', 'l', '\0', (char)0xFF, 'D', '+' };
    static char m[4096];
    long   bad = 0, succ = 0;
    size_t i;

    TEST("H-4 ⑤ 変異ファズ 60,000 回(実応答 6 つ + 合成から 1〜6 か所を壊す・切る)。成功なら値が範囲内、失敗なら err 3〜6、範囲外を読まない");
    for (i = 0u; i < 60000u; i++) {
        size_t v = rnd() % (NFX + 1u);
        size_t n, k, nm = 1u + (rnd() % 6u);
        wx_result_t r;
        bool ok;

        if (v < NFX) { n = g_fxn[v]; memcpy(m, g_fx[v], n); } else { n = build(m, sizeof m, &T0); }
        for (k = 0u; k < nm; k++) {
            if ((rnd() & 3u) == 0u) { m[rnd() % (uint32_t)n] = (char)(rnd() & 0xFFu); } else { m[rnd() % (uint32_t)n] = poison[rnd() % sizeof poison]; }
        }
        if ((rnd() & 3u) == 0u) { n = rnd() % (uint32_t)(n + 1u); }
        ok = wx_parse_http(place(m, n, (int)(rnd() & 1u)), n, &r);
        if (!inv(&r, ok)) { bad++; }
        if (ok) { succ++; }
    }
    CHECK_EQ(bad, 0);
    CHECK(succ > 1000);                                          /* 成功が全く無い試験にならない */

    TEST("H-4 ⑥ 本文は結果に残らない(本文の目印を currency・symbol・longName に入れて、wx_result_t のバイト列に無い)");
    {
        wx_result_t r;
        char b[2000];
        size_t k, n;
        n = (size_t)snprintf(b, sizeof b, H200 "{\"chart\":{\"result\":[{\"meta\":{\"currency\":\"BODY_SENTINEL\",\"symbol\":\"BODY_SENTINEL\",\"regularMarketTime\":1795751335,"
                             "\"regularMarketPrice\":2564.5,\"longName\":\"BODY_SENTINEL\",\"chartPreviousClose\":2433.0,\"currentTradingPeriod\":{\"regular\":"
                             "{\"start\":1795737600,\"end\":1795761000}}},\"indicators\":{\"quote\":[{\"close\":[2433.0,2564.5]}]}}]}}");
        CHECK(wx_parse_http(place(b, n, 1), n, &r)); CHECK_EQ(r.cur, WX_CUR_NONE);          /* 3 文字でない currency は NONE */
        {
            const unsigned char *u = (const unsigned char *)&r;
            int found = 0;
            for (k = 0u; k + 4u <= sizeof r; k++) { if (memcmp(u + k, "BODY", 4u) == 0 || memcmp(u + k, "SENT", 4u) == 0) { found = 1; } }
            CHECK_EQ(found, 0);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("使い方: test_wx_stock <data_dir>\n"); return 2; }
    g_dir = argv[1];
    t_h3();
    t_h4_cut();
    t_json_complete();
    t_h4_syn();
    t_h4_http();
    t_h4_fuzz();
    TEST_REPORT("test_wx_stock");
}
