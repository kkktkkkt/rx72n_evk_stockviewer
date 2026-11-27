/* test_wx_weather.c - 天気の取得の論理(src/app/wx_core.c)のホスト試験。docs/design.md §13.6・§13.10 の H-C2(解析)・H-C3(予定表)・H-C5(`[wx]` の書式)。
 *   A  要求・接続先(wx_build_request の sym = 3 = 195 バイト・METEO_TCP・APP_STK_FAULT = 4 は test_stk_fault.c)
 *   B  wx_parse_weather: 実応答(tests/host/data/openmeteo_*.http。PC から取得)・全 WMO コード・気温の境界・キー欠落・null・重複キー・状態行・Date:・
 *      あらゆる長さに切った応答を禁止ページに接して解析・株価の応答との取り違え・変異ファズ・本文が結果に残らない
 *   C  `[wx]` の 3 形式(境界値・小さな buf・不正な引数・秘密と本文の混入なし)
 *   D  予定表(APP_WX_WEATHER = 1): 取る順 3 → 0 → 1 → 2・天気だけの失敗・株価の失敗・429(天気は普通の失敗、株価は打ち切り)・全部失敗の再接続(株価 3 つだけで判定)・
 *      周期の時間・更新要求の返値・32bit の巻き戻り。乱数走行は test_wx_indep.c(-DAPP_WX_WEATHER=1 でも作る)
 * 使い方: test_wx_weather <data_dir>。-DAPP_WX=1 -DAPP_WX_WEATHER=1 でコンパイルする。 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "wx_core.h"
#include "test_framework.h"

#if !APP_WX_WEATHER
#error "test_wx_weather needs -DAPP_WX_WEATHER=1"
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#define PG (4096u)

static const char *g_dir;
static uint32_t g_rs = 0x2545F491u;
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

/* ---------------------------------------------------------------- 合成の応答 */
#define SENTINEL "BODY_SENTINEL"
#define HDR200 "HTTP/1.1 200 OK\r\nDate: Fri, 27 Nov 2026 12:57:50 GMT\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\n\r\n"
#define UTC_HDR200 (1795784270u)               /* 上の Date の UTC 秒(wx_parse_date で確かめる) */

static char   g_buf[4096];
static size_t g_n;

/* 本文(current の気温・天気コード・時刻の文字列をそのまま埋める)。prefix は Date: などを含むヘッダー */
static size_t mk(const char *head, const char *temp, const char *code, const char *tm)
{
    g_n = (size_t)snprintf(g_buf, sizeof g_buf,
        "%s{\"latitude\":35.7,\"longitude\":139.75,\"generationtime_ms\":0.0514,\"utc_offset_seconds\":32400,\"timezone\":\"Asia/Tokyo\","
        "\"timezone_abbreviation\":\"GMT+9\",\"elevation\":12.0,\"" SENTINEL "\":\"" SENTINEL "\","
        "\"current_units\":{\"time\":\"iso8601\",\"interval\":\"seconds\",\"temperature_2m\":\"\\u00b0C\",\"weather_code\":\"wmo code\"},"
        "\"current\":{\"time\":\"%s\",\"interval\":900,\"temperature_2m\":%s,\"weather_code\":%s}}",
        head, tm, temp, code);
    return g_n;
}

static size_t mk_raw(const char *s)
{
    g_n = (size_t)snprintf(g_buf, sizeof g_buf, "%s", s);
    return g_n;
}

static bool parse(wx_result_t *r)
{
    memset(r, 0xA5, sizeof *r);
    return wx_parse_weather(place(g_buf, g_n, 1), g_n, r);
}

/* 期待どおりに成功 / 失敗(err)するか */
static void expect_ok(const char *temp, const char *code, int t10, int wmo)
{
    wx_result_t r;

    mk(HDR200, temp, code, "2026-11-27T21:45");
    CHECK(parse(&r));
    CHECK_EQ(r.ok, 1); CHECK_EQ(r.err, 0); CHECK_EQ(r.http, 200);
    CHECK_EQ(r.temp_x10, t10); CHECK_EQ(r.wmo, wmo); CHECK_EQ(r.kind, clk_kind_of((uint32_t)wmo));
}

static void expect_err(const char *temp, const char *code, int err)
{
    wx_result_t r;

    mk(HDR200, temp, code, "2026-11-27T21:45");
    CHECK(!parse(&r));
    CHECK_EQ(r.ok, 0); CHECK_EQ(r.err, err);
    CHECK_EQ(r.temp_x10, 0); CHECK_EQ(r.wmo, 0); CHECK_EQ(r.kind, CLK_UNKNOWN);          /* 失敗したら値を残さない */
    CHECK_EQ(r.http, 200);                                                               /* http と Date: は分かった範囲で入る */
    CHECK_EQ(r.have_date, 1);
}

/* 独立の WMO → 種類の表(設計 §13.6。0 sun / 1 partly / 2 cloud / 3 fog / 4 rain / 5 snow / 6 thunder / 7 unknown) */
static int exp_kind(int w)
{
    switch (w) {
    case 0: case 1: return 0;
    case 2: return 1;
    case 3: return 2;
    case 45: case 48: return 3;
    case 51: case 53: case 55: case 56: case 57: case 61: case 63: case 65: case 66: case 67: case 80: case 81: case 82: return 4;
    case 71: case 73: case 75: case 77: case 85: case 86: return 5;
    case 95: case 96: case 99: return 6;
    default: return 7;
    }
}

/* ================================================================ A 要求・接続先 */
static void t_request(void)
{
    char b[300];
    char want[300];
    size_t n;

    TEST("A 天気の要求: sym = 3(WX_WEATHER)は 195 バイトの固定文字列(HTTP/1.0・Connection: close・UA・Host)。0〜2 は 144 / 145 / 145 のまま");
    (void)snprintf(want, sizeof want, "GET /v1/forecast%s?latitude=35.68&longitude=139.76&current=temperature_2m,weather_code&timezone=Asia%%2FTokyo HTTP/1.0\r\n"
                   "Host: api.open-meteo.com\r\nUser-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n", ((unsigned)APP_STK_FAULT == 4u) ? "ZZ" : "");
    n = wx_build_request(b, sizeof b, (uint8_t)WX_WEATHER);
    CHECK(strcmp(b, want) == 0); CHECK_EQ(n, strlen(want)); CHECK_EQ(n, (APP_STK_FAULT == 4) ? 197 : 195);
    CHECK(strstr(b, "\r\n\r\n") == b + n - 4u);                                                  /* 末尾は空行 1 つだけ */
    CHECK(strstr(b, "HTTP/1.0\r\n") != NULL); CHECK(strstr(b, "Connection: close\r\n") != NULL);
    CHECK_EQ(wx_build_request_t(b, sizeof b, WX_TGT_METEO_TCP, (uint8_t)WX_WEATHER), n);
    CHECK_EQ(wx_build_request(b, sizeof b, 0u), 144u); CHECK_EQ(wx_build_request(b, sizeof b, 1u), 145u); CHECK_EQ(wx_build_request(b, sizeof b, 2u), 145u);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_request(b, n, (uint8_t)WX_WEATHER), 0u); CHECK_EQ(b[0], 0);          /* NUL の分が足りない */
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_request(b, n + 1u, (uint8_t)WX_WEATHER), n); CHECK_EQ(b[n + 1u], 'Q');
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_request(b, sizeof b, 4u), 0u); CHECK_EQ(b[0], 0);                  /* 取得 ID の範囲外 */
    CHECK_EQ(wx_build_request(b, sizeof b, 255u), 0u);
    CHECK_EQ(wx_build_request(NULL, 300u, (uint8_t)WX_WEATHER), 0u); CHECK_EQ(wx_build_request(b, 0u, (uint8_t)WX_WEATHER), 0u);
    /* 接続先と sym の組み合わせ: 天気の要求は METEO_TCP だけ、Yahoo の要求は sym 0〜2 だけ */
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_request_t(b, sizeof b, WX_TGT_METEO_TCP, 0u), 0u); CHECK_EQ(b[0], 0);
    CHECK_EQ(wx_build_request_t(b, sizeof b, WX_TGT_METEO_TCP, 2u), 0u);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_request_t(b, sizeof b, WX_TGT_YAHOO_SSL, (uint8_t)WX_WEATHER), 0u); CHECK_EQ(b[0], 0);

    TEST("A 天気の接続先: 既定は AT+CIPSTART=\"TCP\",\"api.open-meteo.com\",80(平文)。WX_METEO_SSL = 1 なら SSL 443。Yahoo は変わらない");
    n = wx_build_cipstart(b, sizeof b, WX_TGT_METEO_TCP);
#if WX_METEO_SSL
    CHECK(strcmp(b, "AT+CIPSTART=\"SSL\",\"api.open-meteo.com\",443\r\n") == 0); CHECK(wx_target_ssl(WX_TGT_METEO_TCP));
#else
    CHECK(strcmp(b, "AT+CIPSTART=\"TCP\",\"api.open-meteo.com\",80\r\n") == 0); CHECK(!wx_target_ssl(WX_TGT_METEO_TCP));
#endif
    CHECK_EQ(n, strlen(b));
    (void)wx_build_cipstart(b, sizeof b, WX_TGT_YAHOO_SSL);
    CHECK(strcmp(b, "AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n") == 0); CHECK(wx_target_ssl(WX_TGT_YAHOO_SSL));
    CHECK_EQ(WX_WEATHER, 3); CHECK_EQ(WX_NFETCH, 4); CHECK_EQ(WX_FETCH_ALL, 0x0F); CHECK_EQ(WX_SYM_ALL, 7);
}

/* ================================================================ B 解析 */
static char   g_real[4096];
static size_t g_realn;

static void t_real(void)
{
    wx_result_t r;
    wx_date_t   d;
    uint32_t    utc = 0u;

    TEST("H-C2 実応答(PC から保存: openmeteo_tokyo_1.http。HTTP/1.1 200・Date・Content-Type・本文)の期待値との完全一致(本文を別に読んで作った値: 13.2℃・コード 3 = くもり)");
    g_realn = load("openmeteo_tokyo_1.http", g_real, sizeof g_real);
    CHECK(g_realn > 400u);
    CHECK(strncmp(g_real, "HTTP/1.1 200 OK\r\n", 17u) == 0);
    CHECK(strstr(g_real, "\"current\":{\"time\":\"2026-11-28T00:00\",\"interval\":900,\"temperature_2m\":13.2,\"weather_code\":3}") != NULL);
    memset(&r, 0xA5, sizeof r);
    CHECK(wx_parse_weather(place(g_real, g_realn, 1), g_realn, &r));
    CHECK_EQ(r.ok, 1); CHECK_EQ(r.err, 0); CHECK_EQ(r.http, 200);
    CHECK_EQ(r.temp_x10, 132); CHECK_EQ(r.wmo, 3); CHECK_EQ(r.kind, CLK_CLOUD); CHECK_EQ(r.obs, 0);     /* current.time = 00:00 */
    CHECK_EQ(r.have_date, 1);
    CHECK(wx_parse_date(" Fri, 27 Nov 2026 15:10:07 GMT", 30u, &utc));
    CHECK_EQ(r.jst_s, utc + 32400u);
    wx_date_fields(r.jst_s, &d);
    CHECK_EQ(d.year, 2026); CHECK_EQ(d.month, 11); CHECK_EQ(d.day, 28); CHECK_EQ(d.hour, 0); CHECK_EQ(d.min, 10); CHECK_EQ(d.sec, 7);
    /* 解析が触らない項目(wifi_core が入れる)は元のまま。株価の項目は 0 */
    CHECK_EQ(r.sym, 0xA5); CHECK_EQ(r.closed, 0xA5); CHECK_EQ(r.ms, 0xA5A5A5A5u); CHECK_EQ(r.first_ms, 0xA5A5A5A5u); CHECK_EQ(r.len, 0xA5A5);
    CHECK_EQ(r.price_x100, 0); CHECK_EQ(r.prev_x100, 0); CHECK_EQ(r.mtime, 0u); CHECK_EQ(r.rs, 0u); CHECK_EQ(r.re, 0u); CHECK_EQ(r.has_bar_prev, 0); CHECK_EQ(r.cur, WX_CUR_NONE);
    memset(&r, 0, sizeof r);
    CHECK(wx_parse_weather(place(g_real, g_realn, 0), g_realn, &r)); CHECK_EQ(r.temp_x10, 132);   /* 先頭を禁止ページに接しても同じ */

    TEST("H-C2 実応答 2 つ目(openmeteo_tokyo_2.http。同じ日の約 15 分後: 13.2℃・コード 53 = 霧雨 = 雨・obs = 00:15・Date 15:26:27 GMT)");
    {
        char   b2[4096];
        size_t n2 = load("openmeteo_tokyo_2.http", b2, sizeof b2);

        CHECK(n2 > 400u);
        CHECK(strstr(b2, "\"current\":{\"time\":\"2026-11-28T00:15\",\"interval\":900,\"temperature_2m\":13.2,\"weather_code\":53}") != NULL);
        memset(&r, 0xA5, sizeof r);
        CHECK(wx_parse_weather(place(b2, n2, 1), n2, &r));
        CHECK_EQ(r.temp_x10, 132); CHECK_EQ(r.wmo, 53); CHECK_EQ(r.kind, CLK_RAIN); CHECK_EQ(r.obs, 15); CHECK_EQ(r.http, 200); CHECK_EQ(r.have_date, 1);
        CHECK(wx_parse_date(" Fri, 27 Nov 2026 15:26:27 GMT", 30u, &utc));
        CHECK_EQ(r.jst_s, utc + 32400u);
        wx_date_fields(r.jst_s, &d);
        CHECK_EQ(d.day, 28); CHECK_EQ(d.hour, 0); CHECK_EQ(d.min, 26); CHECK_EQ(d.sec, 27);
    }

    TEST("H-C2 合成の応答の基準(設計 §13.1 W4 と同じ形: 12.9℃・コード 1・time = 21:45)→ temp10 = 129・wmo = 1・kind = sun・obs = 2145・Date は UTC 12:57:50 → JST 21:57:50");
    mk(HDR200, "12.9", "1", "2026-11-27T21:45");
    CHECK(parse(&r));
    CHECK_EQ(r.temp_x10, 129); CHECK_EQ(r.wmo, 1); CHECK_EQ(r.kind, CLK_SUN); CHECK_EQ(r.obs, 2145); CHECK_EQ(r.http, 200);
    CHECK(wx_parse_date(" Fri, 27 Nov 2026 12:57:50 GMT", 30u, &utc)); CHECK_EQ(utc, UTC_HDR200);
    CHECK_EQ(r.jst_s, UTC_HDR200 + 32400u);
}

static void t_codes(void)
{
    int  w;
    char c[8];

    TEST("H-C2 WMO コード 0〜99 の全部: 成功し、kind が設計 §13.6 の表(独立に書いた表)と clk_kind_of の両方と一致");
    for (w = 0; w < 100; w++) {
        wx_result_t r;

        (void)snprintf(c, sizeof c, "%d", w);
        mk(HDR200, "10.0", c, "2026-11-27T21:45");
        CHECK(parse(&r));
        CHECK_EQ(r.wmo, w); CHECK_EQ(r.kind, exp_kind(w)); CHECK_EQ(r.kind, clk_kind_of((uint32_t)w)); CHECK_EQ(r.temp_x10, 100);
    }
    TEST("H-C2 天気コードの範囲外・形の違い: 100・255・負・null・文字列・小数・指数・巨大 → err = 6(値は残さない)");
    expect_err("10.0", "100", 6); expect_err("10.0", "255", 6); expect_err("10.0", "-1", 6); expect_err("10.0", "-0", 6);
    expect_err("10.0", "null", 6); expect_err("10.0", "\"3\"", 6); expect_err("10.0", "3.0", 6); expect_err("10.0", "1e1", 6);
    expect_err("10.0", "99999999999999999999", 6); expect_err("10.0", "true", 6);
    expect_ok("10.0", "99", 100, 99); expect_ok("10.0", "0", 100, 0); expect_ok("10.0", "03", 100, 3);
}

static void t_temps(void)
{
    TEST("H-C2 気温: 境界(±60.0)・四捨五入(2 桁目)・符号・整数だけ・−0.4 → temp_x10 = −4");
    expect_ok("13.2", "3", 132, 3); expect_ok("-0.4", "3", -4, 3); expect_ok("0.0", "3", 0, 3); expect_ok("-0.0", "3", 0, 3); expect_ok("0", "3", 0, 3);
    expect_ok("60.0", "3", 600, 3); expect_ok("-60.0", "3", -600, 3); expect_ok("60", "3", 600, 3); expect_ok("-60", "3", -600, 3);
    expect_ok("12", "3", 120, 3); expect_ok("5.5", "3", 55, 3); expect_ok("-5.5", "3", -55, 3);
    expect_ok("12.94", "3", 129, 3); expect_ok("12.95", "3", 130, 3); expect_ok("-12.95", "3", -130, 3); expect_ok("12.999", "3", 130, 3);
    expect_ok("60.04", "3", 600, 3); expect_ok("59.96", "3", 600, 3); expect_ok("-0.04", "3", 0, 3);
    TEST("H-C2 気温の範囲外 → err = 6: 60.05(丸めて 60.1)・61.0・−61・100.0(整数部 3 桁)・巨大な数(桁あふれしない)");
    expect_err("60.05", "3", 6); expect_err("-60.05", "3", 6); expect_err("61.0", "3", 6); expect_err("-61", "3", 6); expect_err("100.0", "3", 6);
    expect_err("-100.0", "3", 6); expect_err("999", "3", 6); expect_err("99999999999999999999999.9", "3", 6); expect_err("007.0", "3", 6);
    TEST("H-C2 気温が読めない → err = 5: null・文字列・指数・小数点だけ・ \"-\" だけ・\".5\"・\"5.\"・true・空");
    expect_err("null", "3", 5); expect_err("\"13.2\"", "3", 5); expect_err("1e1", "3", 5); expect_err("1E1", "3", 5); expect_err("-", "3", 5);
    expect_err(".5", "3", 5); expect_err("5.", "3", 5); expect_err("true", "3", 5); expect_err("", "3", 5); expect_err("+5", "3", 5); expect_err("NaN", "3", 5);
    expect_err("--5", "3", 5); expect_err("1.2.3", "3", 5);
}

static void t_keys(void)
{
    wx_result_t r;

    TEST("H-C2 obs(current.time の HHMM): 正常・00:00・23:59・無い(0)・形が違う(0)・時刻が 24:00 / 12:60(0)");
    mk(HDR200, "10.0", "3", "2026-11-27T21:45"); CHECK(parse(&r)); CHECK_EQ(r.obs, 2145);
    mk(HDR200, "10.0", "3", "2026-11-27T00:00"); CHECK(parse(&r)); CHECK_EQ(r.obs, 0);
    mk(HDR200, "10.0", "3", "2026-11-27T23:59"); CHECK(parse(&r)); CHECK_EQ(r.obs, 2359);
    mk(HDR200, "10.0", "3", "garbage"); CHECK(parse(&r)); CHECK_EQ(r.obs, 0); CHECK_EQ(r.ok, 1);
    mk(HDR200, "10.0", "3", "2026-11-27T24:00"); CHECK(parse(&r)); CHECK_EQ(r.obs, 0);
    mk(HDR200, "10.0", "3", "2026-11-27T12:60"); CHECK(parse(&r)); CHECK_EQ(r.obs, 0);
    mk(HDR200, "10.0", "3", "2026/11/27 21:45"); CHECK(parse(&r)); CHECK_EQ(r.obs, 0);
    mk_raw(HDR200 "{\"current\":{\"temperature_2m\":1.5,\"weather_code\":2}}"); CHECK(parse(&r)); CHECK_EQ(r.ok, 1); CHECK_EQ(r.obs, 0); CHECK_EQ(r.temp_x10, 15); CHECK_EQ(r.kind, CLK_PARTLY);

    TEST("H-C2 キー: `\"current\":` が無い(current_units だけ)・null・オブジェクトでない・空 → err = 5。気温・コードのキー欠落 → err = 5。current の外の値は読まない");
    mk_raw(HDR200 "{\"current_units\":{\"time\":\"iso8601\",\"temperature_2m\":\"C\",\"weather_code\":\"w\"}}");
    CHECK(!parse(&r)); CHECK_EQ(r.err, 5);
    mk_raw(HDR200 "{\"current\":null,\"temperature_2m\":1.0,\"weather_code\":1}"); CHECK(!parse(&r)); CHECK_EQ(r.err, 5);
    mk_raw(HDR200 "{\"current\":5,\"temperature_2m\":1.0,\"weather_code\":1}"); CHECK(!parse(&r)); CHECK_EQ(r.err, 5);
    mk_raw(HDR200 "{\"current\":{}}"); CHECK(!parse(&r)); CHECK_EQ(r.err, 5);
    mk_raw(HDR200 "{\"current\":{\"weather_code\":1}}"); CHECK(!parse(&r)); CHECK_EQ(r.err, 5);
    mk_raw(HDR200 "{\"current\":{\"temperature_2m\":1.0}}"); CHECK(!parse(&r)); CHECK_EQ(r.err, 5);
    mk_raw(HDR200 "{\"current\":{\"time\":\"2026-11-27T21:45\"},\"temperature_2m\":1.0,\"weather_code\":1}"); CHECK(!parse(&r)); CHECK_EQ(r.err, 5);      /* current の外 */
    mk_raw(HDR200 "{\"current_units\":{\"temperature_2m\":\"C\",\"weather_code\":\"w\"},\"current\":{\"time\":\"2026-11-27T21:45\",\"interval\":900}}");
    CHECK(!parse(&r)); CHECK_EQ(r.err, 5);                                                                             /* current_units の文字列を気温にしない */
    mk_raw(HDR200 "{\"current\":{\"weather_code\":2,\"temperature_2m\":-3.5,\"time\":\"2026-11-27T06:05\"}}");          /* キーの順序は問わない */
    CHECK(parse(&r)); CHECK_EQ(r.temp_x10, -35); CHECK_EQ(r.wmo, 2); CHECK_EQ(r.obs, 605);
    mk_raw(HDR200 "{\"current\": {\"temperature_2m\":7.5,\"weather_code\":61}}");                                        /* current の後ろの空白は可 */
    CHECK(parse(&r)); CHECK_EQ(r.temp_x10, 75); CHECK_EQ(r.wmo, 61); CHECK_EQ(r.kind, CLK_RAIN);

    TEST("H-C2 重複キー: `\"current\":` が 2 回なら最初を使う。天気以外のキー(株価の regularMarketPrice など)があっても成功にしない");
    mk_raw(HDR200 "{\"current\":{\"temperature_2m\":1.0,\"weather_code\":0},\"current\":{\"temperature_2m\":9.0,\"weather_code\":95}}");
    CHECK(parse(&r)); CHECK_EQ(r.temp_x10, 10); CHECK_EQ(r.wmo, 0);
    mk_raw(HDR200 "{\"current\":{\"temperature_2m\":1.0,\"temperature_2m\":9.0,\"weather_code\":2,\"weather_code\":95}}");
    CHECK(parse(&r)); CHECK_EQ(r.temp_x10, 10); CHECK_EQ(r.wmo, 2);
    mk_raw(HDR200 "{\"meta\":{\"regularMarketPrice\":2433.0,\"chartPreviousClose\":2378.0}}"); CHECK(!parse(&r)); CHECK_EQ(r.err, 5);
}

static void t_http(void)
{
    wx_result_t r;

    TEST("H-C2 HTTP の状態: 429・301・404・500 → err = 4(http が入る。Date: は読める)・HTTP/1.0 / HTTP/1.1 の両方・HTTP/2・形の違う状態行 → err = 4");
    mk("HTTP/1.1 429 Too Many Requests\r\nDate: Fri, 27 Nov 2026 12:57:50 GMT\r\n\r\n", "10.0", "3", "2026-11-27T21:45");
    CHECK(!parse(&r)); CHECK_EQ(r.err, 4); CHECK_EQ(r.http, 429); CHECK_EQ(r.have_date, 1); CHECK_EQ(r.jst_s, UTC_HDR200 + 32400u); CHECK_EQ(r.ok, 0);
    mk_raw("HTTP/1.1 404 Not Found\r\nDate: Fri, 27 Nov 2026 12:57:50 GMT\r\n\r\n{\"error\":true,\"reason\":\"Not Found\"}");
    CHECK(!parse(&r)); CHECK_EQ(r.err, 4); CHECK_EQ(r.http, 404);
    mk_raw("HTTP/1.1 301 Moved\r\n\r\n"); CHECK(!parse(&r)); CHECK_EQ(r.err, 4); CHECK_EQ(r.http, 301); CHECK_EQ(r.have_date, 0);
    mk_raw("HTTP/1.1 500 x\r\n\r\n{}"); CHECK(!parse(&r)); CHECK_EQ(r.err, 4); CHECK_EQ(r.http, 500);
    mk("HTTP/1.0 200 OK\r\nDate: Fri, 27 Nov 2026 12:57:50 GMT\r\n\r\n", "10.0", "3", "2026-11-27T21:45"); CHECK(parse(&r)); CHECK_EQ(r.ok, 1);
    mk("HTTP/2 200 \r\n\r\n", "10.0", "3", "2026-11-27T21:45"); CHECK(!parse(&r)); CHECK_EQ(r.err, 4);
    mk("HTTP/1.1  200 OK\r\n\r\n", "10.0", "3", "2026-11-27T21:45"); CHECK(!parse(&r)); CHECK_EQ(r.err, 4);
    mk("garbage garbage garbage\r\n\r\n", "10.0", "3", "2026-11-27T21:45"); CHECK(!parse(&r)); CHECK_EQ(r.err, 4);

    TEST("H-C2 Date:(大文字小文字を区別しない・無くても成功・壊れていても成功)と、本文の形(空・HTML・途中)");
    mk("HTTP/1.1 200 OK\r\ndate: Fri, 27 Nov 2026 12:57:50 GMT\r\n\r\n", "10.0", "3", "2026-11-27T21:45"); CHECK(parse(&r)); CHECK_EQ(r.have_date, 1); CHECK_EQ(r.jst_s, UTC_HDR200 + 32400u);
    mk("HTTP/1.1 200 OK\r\nDATE: Fri, 27 Nov 2026 12:57:50 GMT\r\n\r\n", "10.0", "3", "2026-11-27T21:45"); CHECK(parse(&r)); CHECK_EQ(r.have_date, 1);
    mk("HTTP/1.1 200 OK\r\n\r\n", "10.0", "3", "2026-11-27T21:45"); CHECK(parse(&r)); CHECK_EQ(r.have_date, 0); CHECK_EQ(r.jst_s, 0u); CHECK_EQ(r.ok, 1);
    mk("HTTP/1.1 200 OK\r\nDate: not a date\r\n\r\n", "10.0", "3", "2026-11-27T21:45"); CHECK(parse(&r)); CHECK_EQ(r.have_date, 0);
    mk_raw("HTTP/1.1 200 OK\r\n\r\n"); CHECK(!parse(&r)); CHECK_EQ(r.err, 5);                                          /* 本文が空 */
    mk_raw("HTTP/1.1 200 OK\r\n\r\n<html>no</html>"); CHECK(!parse(&r)); CHECK_EQ(r.err, 5);
    mk_raw("HTTP/1.1 200 OK\r\nDate: Fri, 27 Nov 2026 12:57:50 GMT\r\nContent-Type: x"); CHECK(!parse(&r)); CHECK_EQ(r.err, 3);   /* 空行まで届かない */
    mk_raw("HTTP/1.1 200"); CHECK(!parse(&r)); CHECK_EQ(r.err, 3);                                                     /* 状態行 12 バイト未満 */
    mk_raw("HTTP/1.1 2"); CHECK(!parse(&r)); CHECK_EQ(r.err, 3);
    CHECK(!wx_parse_weather(NULL, 100u, &r)); CHECK_EQ(r.err, 3);
    mk("HTTP/1.1 200 OK\r\n\r\n", "10.0", "3", "2026-11-27T21:45"); strcat(g_buf, "\r\n"); g_n += 2u; CHECK(parse(&r)); CHECK_EQ(r.ok, 1);   /* 閉じた後の空白は可 */
    mk("HTTP/1.1 200 OK\r\n\r\n", "10.0", "3", "2026-11-27T21:45"); strcat(g_buf, "x"); g_n += 1u; CHECK(!parse(&r)); CHECK_EQ(r.err, 3);   /* 閉じた後のゴミは不可 */
}

static void t_cut(void)
{
    size_t i;
    int    at;
    long   bad = 0;
    long   full_ok = 0;

    TEST("H-C2 あらゆる長さに切った実応答(+ 合成)を、データの末尾 / 先頭を禁止ページに接して解析: 範囲外を読まない(読めば異常終了)・全長だけが成功・err は 3・4・5 のどれか");
    if (!guard_init()) { printf("  (ガードページを作れない: 普通のバッファで試験する)\n"); }
    for (at = 0; at < 2; at++) {
        for (i = 0u; i <= g_realn; i++) {
            wx_result_t r;

            memset(&r, 0, sizeof r);
            if (wx_parse_weather(place(g_real, i, at), i, &r)) {
                if (i != g_realn) { bad++; } else { full_ok++; }
            } else if ((r.err != 3u) && (r.err != 4u) && (r.err != 5u)) {
                bad++;
            } else if (r.ok != 0u) {
                bad++;
            } else {
                /* 失敗した */
            }
        }
    }
    CHECK_EQ(bad, 0); CHECK_EQ(full_ok, 2);
    mk(HDR200, "-12.5", "71", "2026-11-27T21:45");
    for (at = 0; at < 2; at++) {
        for (i = 0u; i <= g_n; i++) {
            wx_result_t r;
            char        keep[4096];

            memcpy(keep, g_buf, g_n);
            memset(&r, 0, sizeof r);
            if (wx_parse_weather(place(keep, i, at), i, &r)) {
                if (i != g_n) { bad++; }
            } else if (((r.err != 3u) && (r.err != 4u) && (r.err != 5u)) || (r.ok != 0u)) {
                bad++;
            } else {
                /* 失敗した */
            }
        }
    }
    CHECK_EQ(bad, 0);
}

static void t_cross(void)
{
    char        yb[4096];
    size_t      yn;
    wx_result_t r;

    TEST("H-C2 取り違え: 株価の応答を wx_parse_weather に渡すと失敗し、天気の応答を wx_parse_http に渡すと失敗する(どちらも err = 5。成功にならない)");
    yn = load("yahoo_6723T_2d.http", yb, sizeof yb);
    CHECK(yn > 1000u);
    memset(&r, 0, sizeof r);
    CHECK(wx_parse_http(place(yb, yn, 1), yn, &r)); CHECK_EQ(r.ok, 1);                                                  /* 前提: 株価の解析は成功する */
    memset(&r, 0, sizeof r);
    CHECK(!wx_parse_weather(place(yb, yn, 1), yn, &r)); CHECK_EQ(r.err, 5); CHECK_EQ(r.ok, 0);
    memset(&r, 0, sizeof r);
    CHECK(!wx_parse_http(place(g_real, g_realn, 1), g_realn, &r)); CHECK_EQ(r.err, 5); CHECK_EQ(r.ok, 0);
    mk(HDR200, "10.0", "3", "2026-11-27T21:45");
    memset(&r, 0, sizeof r);
    CHECK(!wx_parse_http(place(g_buf, g_n, 1), g_n, &r)); CHECK_EQ(r.err, 5);
    memset(&r, 0, sizeof r);
    CHECK(wx_parse_http(place(yb, yn, 1), yn, &r)); CHECK_EQ(r.temp_x10, 0); CHECK_EQ(r.wmo, 0); CHECK_EQ(r.kind, CLK_UNKNOWN); CHECK_EQ(r.obs, 0);   /* 株価の解析は天気の項目を 0 にそろえる */
}

static void t_fuzz(void)
{
    int i;
    long bad = 0, okn = 0;

    TEST("H-C2 変異ファズ(実応答の 1〜4 か所を壊す・切る。禁止ページに接して 5,000 回): 範囲外を読まない・成功なら気温 ±600・コード 0〜99・kind が一致・失敗なら err 3〜6");
    (void)guard_init();
    for (i = 0; i < 5000; i++) {
        char        t[4096];
        size_t      n = g_realn;
        wx_result_t r;
        int         k = 1 + (int)(rnd() % 4u);
        bool        ok;

        memcpy(t, g_real, n);
        while (k-- > 0) {
            size_t p = rnd() % n;
            switch (rnd() % 4u) {
            case 0: t[p] = (char)rnd(); break;
            case 1: t[p] = (char)('0' + (rnd() % 10u)); break;
            case 2: t[p] = (rnd() & 1u) ? '-' : '.'; break;
            default: if ((rnd() % 3u) == 0u) { n = p + 1u; } else { t[p] = ','; } break;
            }
        }
        memset(&r, 0, sizeof r);
        ok = wx_parse_weather(place(t, n, (int)(rnd() & 1u)), n, &r);
        if (ok) {
            okn++;
            if ((r.ok != 1u) || (r.err != 0u) || (r.temp_x10 < -600) || (r.temp_x10 > 600) || (r.wmo > 99u) || (r.kind != clk_kind_of(r.wmo)) || (r.http != 200u)) { bad++; }
        } else if ((r.ok != 0u) || (r.err < 3u) || (r.err > 6u)) {
            bad++;
        } else {
            /* 失敗 */
        }
    }
    CHECK_EQ(bad, 0); CHECK(okn > 100); CHECK(okn < 5000);                                                               /* 成功も失敗も実際に起きた */
}

static void t_nobody(void)
{
    wx_result_t r;
    const unsigned char *p = (const unsigned char *)&r;
    size_t i;
    int found = 0;

    TEST("H-C2 本文が結果に残らない: 本文の目印(BODY_SENTINEL)・\"latitude\"・\"current\" が wx_result_t のバイト列に無い");
    mk(HDR200, "10.0", "3", "2026-11-27T21:45");
    memset(&r, 0, sizeof r);
    CHECK(wx_parse_weather(place(g_buf, g_n, 1), g_n, &r));
    for (i = 0u; (i + 8u) <= sizeof r; i++) {
        if ((memcmp(p + i, "BODY_SEN", 8u) == 0) || (memcmp(p + i, "latitude", 8u) == 0) || (memcmp(p + i, "\"current", 8u) == 0)) { found++; }
    }
    CHECK_EQ(found, 0);
    CHECK(sizeof r <= 96u);
}

/* ================================================================ C `[wx]` の書式 */
typedef struct { const char *key; const char *cls; } fld_t;

static int val_is(const char *v, size_t vn, const char *cls)
{
    const char *c = cls;

    while (*c != '\0') {
        const char *e = strchr(c, '|');
        size_t      cn = (e != NULL) ? (size_t)(e - c) : strlen(c);
        int         ok = 0;

        if ((c[0] == '#') && ((c[1] == 'D') || (c[1] == 'E') || (c[1] == 'S'))) {
            size_t i = 0u;
            size_t digits;
            size_t n = (size_t)strtoul(c + 2, NULL, 10);

            if ((c[1] == 'S') && (vn > 0u) && (v[0] == '-')) { i = 1u; }
            digits = vn - i;
            ok = (digits >= 1u) && (digits <= n) && ((c[1] != 'E') || (digits == n));
            for (; ok && (i < vn); i++) { if ((v[i] < '0') || (v[i] > '9')) { ok = 0; } }
        } else {
            ok = (vn == cn) && (memcmp(v, c, cn) == 0);
        }
        if (ok) { return 1; }
        if (e == NULL) { break; }
        c = e + 1;
    }
    return 0;
}

static int line_ok(const char *line, const char *tag, const fld_t *f, size_t nf)
{
    const char *p = line;
    size_t      i;

    if ((strncmp(p, tag, strlen(tag)) != 0)) { return 0; }
    p += strlen(tag);
    for (i = 0u; i < nf; i++) {
        size_t kn = strlen(f[i].key);
        const char *e;

        if ((strncmp(p, f[i].key, kn) != 0) || (p[kn] != '=')) { return 0; }
        p += kn + 1u;
        e = p;
        while ((*e != ' ') && (*e != '\r') && (*e != '\0')) { e++; }
        if (!val_is(p, (size_t)(e - p), f[i].cls)) { return 0; }
        p = e;
        if (i + 1u < nf) {
            if (*p != ' ') { return 0; }
            p++;
        }
    }
    return strcmp(p, "\r\n") == 0;
}

#define KINDS "sun|partly|cloud|fog|rain|snow|thunder|unknown"
static int is_wx_ok(const char *l)
{
    static const fld_t F[] = { {"res","ok"}, {"src","boot|timer|touch|uart|retry"}, {"temp10","#S3"}, {"wmo","#D2"}, {"kind", KINDS}, {"http","#E3"}, {"ms","#D10"},
                               {"sync","0|1"}, {"jst","#E6|none"}, {"obs","#E4"}, {"len","#D5"}, {"closed","0|1"} };
    return line_ok(l, "[wx] ", F, sizeof F / sizeof F[0]);
}
static int is_wx_fail(const char *l)
{
    static const fld_t F[] = { {"res","fail"}, {"src","boot|timer|touch|uart|retry"}, {"err","1|2|3|4|5|6|7|8|9"}, {"http","#D3"}, {"ms","#D10"}, {"len","#D5"} };
    return line_ok(l, "[wx] ", F, sizeof F / sizeof F[0]);
}
static int is_wx_state(const char *l)
{
    static const fld_t F[] = { {"have","0|1"}, {"temp10","#S3"}, {"wmo","#D2"}, {"kind", KINDS "|none"}, {"age","-1|#D10"} };
    return line_ok(l, "[wx] ", F, sizeof F / sizeof F[0]);
}

static const char *const SRCN[5] = { "boot", "timer", "touch", "uart", "retry" };

static void fill_ok(wx_result_t *r, int t10, unsigned wmo)
{
    memset(r, 0, sizeof *r);
    r->ok = 1u; r->http = 200u; r->sym = (uint8_t)WX_WEATHER; r->temp_x10 = (int16_t)t10; r->wmo = (uint8_t)wmo; r->kind = clk_kind_of(wmo);
    r->have_date = 1u; r->jst_s = UTC_HDR200 + 32400u; r->ms = 812u; r->len = 629u; r->closed = 1u; r->obs = 2145u;
}

static void t_fmt(void)
{
    char        b[300];
    wx_result_t r;
    wx_wlog_t   v;
    size_t      n, k;
    int         s, w;
    char        all[40000];
    size_t      alln = 0u;

    TEST("H-C5 [wx] res=ok: 基準の文字列・全 src・全 WMO コードの kind・境界値(temp10 = ±600、ms = 0xFFFFFFFF、len = 65535、http = 100 / 999、obs = 0 / 2359、jst なし)");
    fill_ok(&r, 129, 1u);
    n = wx_fmt_wx_ok(b, sizeof b, WX_SRC_BOOT, &r);
    CHECK(strcmp(b, "[wx] res=ok src=boot temp10=129 wmo=1 kind=sun http=200 ms=812 sync=1 jst=215750 obs=2145 len=629 closed=1\r\n") == 0); CHECK_EQ(n, strlen(b));
    for (s = 0; s < 5; s++) {
        fill_ok(&r, -4, 3u);
        n = wx_fmt_wx_ok(b, sizeof b, (wx_src_t)s, &r); CHECK(n > 0u); CHECK(is_wx_ok(b)); CHECK(strstr(b, SRCN[s]) != NULL);
    }
    for (w = 0; w < 100; w++) {
        fill_ok(&r, 100, (unsigned)w);
        n = wx_fmt_wx_ok(b, sizeof b, WX_SRC_TIMER, &r); CHECK(n > 0u); CHECK(is_wx_ok(b));
        k = (size_t)exp_kind(w);
        { static const char *const KN[8] = { "kind=sun ", "kind=partly ", "kind=cloud ", "kind=fog ", "kind=rain ", "kind=snow ", "kind=thunder ", "kind=unknown " }; CHECK(strstr(b, KN[k]) != NULL); }
    }
    fill_ok(&r, 600, 99u); r.ms = 0xFFFFFFFFu; r.len = 65535u; r.http = 999u; r.obs = 2359u; r.closed = 0u; r.have_date = 0u;
    n = wx_fmt_wx_ok(b, sizeof b, WX_SRC_RETRY, &r); CHECK(n > 0u); CHECK(is_wx_ok(b)); CHECK(strstr(b, "temp10=600 ") != NULL); CHECK(strstr(b, "sync=0 jst=none ") != NULL); CHECK(strstr(b, "ms=4294967295 ") != NULL);
    fill_ok(&r, -600, 0u); r.http = 100u; r.obs = 0u;
    n = wx_fmt_wx_ok(b, sizeof b, WX_SRC_UART, &r); CHECK(n > 0u); CHECK(is_wx_ok(b)); CHECK(strstr(b, "temp10=-600 ") != NULL); CHECK(strstr(b, "obs=0000 ") != NULL);
    fill_ok(&r, 0, 0u); r.jst_s = 0u;
    n = wx_fmt_wx_ok(b, sizeof b, WX_SRC_TOUCH, &r); CHECK(n > 0u); CHECK(is_wx_ok(b)); CHECK(strstr(b, "jst=000000 ") != NULL);                         /* jst_s = 0(1970-01-01 00:00:00)は 000000 */

    TEST("H-C5 [wx] res=ok の不正な引数(範囲外の値・src・NULL・小さな buf)は 0 で buf[0] = '\\0'。ちょうどの大きさは成功・1 バイト足りないは失敗");
    fill_ok(&r, 129, 1u);
    n = wx_fmt_wx_ok(b, sizeof b, WX_SRC_BOOT, &r);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_wx_ok(b, n, WX_SRC_BOOT, &r), 0u); CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_wx_ok(b, n + 1u, WX_SRC_BOOT, &r), n); CHECK_EQ(b[n + 1u], 'Q');
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_wx_ok(b, 20u, WX_SRC_BOOT, &r), 0u); CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_wx_ok(b, 60u, WX_SRC_BOOT, &r), 0u); CHECK_EQ(b[0], 0);                         /* 1 つ目の snprintf は入るが 2 つ目が入らない */
    CHECK_EQ(wx_fmt_wx_ok(NULL, 100u, WX_SRC_BOOT, &r), 0u); CHECK_EQ(wx_fmt_wx_ok(b, 0u, WX_SRC_BOOT, &r), 0u); CHECK_EQ(wx_fmt_wx_ok(b, sizeof b, WX_SRC_BOOT, NULL), 0u);
    CHECK_EQ(wx_fmt_wx_ok(b, sizeof b, WX_SRC_END, &r), 0u); CHECK_EQ(b[0], 0);
    fill_ok(&r, 601, 1u); CHECK_EQ(wx_fmt_wx_ok(b, sizeof b, WX_SRC_BOOT, &r), 0u);
    fill_ok(&r, -601, 1u); CHECK_EQ(wx_fmt_wx_ok(b, sizeof b, WX_SRC_BOOT, &r), 0u);
    fill_ok(&r, 1, 1u); r.wmo = 100u; CHECK_EQ(wx_fmt_wx_ok(b, sizeof b, WX_SRC_BOOT, &r), 0u);
    fill_ok(&r, 1, 1u); r.kind = 8u; CHECK_EQ(wx_fmt_wx_ok(b, sizeof b, WX_SRC_BOOT, &r), 0u);
    fill_ok(&r, 1, 1u); r.http = 99u; CHECK_EQ(wx_fmt_wx_ok(b, sizeof b, WX_SRC_BOOT, &r), 0u);
    fill_ok(&r, 1, 1u); r.http = 1000u; CHECK_EQ(wx_fmt_wx_ok(b, sizeof b, WX_SRC_BOOT, &r), 0u);
    fill_ok(&r, 1, 1u); r.obs = 2360u; CHECK_EQ(wx_fmt_wx_ok(b, sizeof b, WX_SRC_BOOT, &r), 0u);

    TEST("H-C5 [wx] res=fail: 全 src・err 1〜9・境界値(http = 0 / 999、ms = 0xFFFFFFFF、len = 0 / 65535)。err = 0 / 10・http = 1000・NULL・小さな buf は 0");
    memset(&r, 0, sizeof r);
    for (s = 0; s < 5; s++) {
        int e;
        for (e = 1; e <= 9; e++) {
            r.err = (uint8_t)e; r.http = (uint16_t)(e * 100 % 1000); r.ms = 12345u; r.len = 0u;
            n = wx_fmt_wx_fail(b, sizeof b, (wx_src_t)s, &r); CHECK(n > 0u); CHECK(is_wx_fail(b)); CHECK(strstr(b, SRCN[s]) != NULL);
        }
    }
    r.err = 4u; r.http = 429u; r.ms = 4300u; r.len = 120u;
    n = wx_fmt_wx_fail(b, sizeof b, WX_SRC_RETRY, &r);
    CHECK(strcmp(b, "[wx] res=fail src=retry err=4 http=429 ms=4300 len=120\r\n") == 0); CHECK_EQ(n, strlen(b));
    r.err = 9u; r.http = 0u; r.ms = 0xFFFFFFFFu; r.len = 65535u;
    n = wx_fmt_wx_fail(b, sizeof b, WX_SRC_BOOT, &r); CHECK(n > 0u); CHECK(is_wx_fail(b));
    r.err = 0u; CHECK_EQ(wx_fmt_wx_fail(b, sizeof b, WX_SRC_BOOT, &r), 0u); CHECK_EQ(b[0], 0);
    r.err = 10u; CHECK_EQ(wx_fmt_wx_fail(b, sizeof b, WX_SRC_BOOT, &r), 0u);
    r.err = 4u; r.http = 1000u; CHECK_EQ(wx_fmt_wx_fail(b, sizeof b, WX_SRC_BOOT, &r), 0u);
    r.http = 404u; CHECK_EQ(wx_fmt_wx_fail(NULL, 100u, WX_SRC_BOOT, &r), 0u); CHECK_EQ(wx_fmt_wx_fail(b, sizeof b, WX_SRC_BOOT, NULL), 0u); CHECK_EQ(wx_fmt_wx_fail(b, sizeof b, WX_SRC_END, &r), 0u);
    n = wx_fmt_wx_fail(b, sizeof b, WX_SRC_BOOT, &r);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_wx_fail(b, n, WX_SRC_BOOT, &r), 0u); CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_wx_fail(b, n + 1u, WX_SRC_BOOT, &r), n);

    TEST("H-C5 [wx] have=: have = 0 は temp10 = 0・wmo = 0・kind = none・age = -1 にそろう(値を見ない)・have = 1 は値どおり・境界値・不正は 0");
    memset(&v, 0, sizeof v);
    v.have = 0u; v.temp_x10 = 555; v.wmo = 77u; v.kind = 6u; v.age_s = 123;
    n = wx_fmt_wx_state(b, sizeof b, &v); CHECK(is_wx_state(b)); CHECK(strcmp(b, "[wx] have=0 temp10=0 wmo=0 kind=none age=-1\r\n") == 0); CHECK_EQ(n, strlen(b));
    v.have = 1u; v.temp_x10 = -4; v.wmo = 3u; v.kind = CLK_CLOUD; v.age_s = 0;
    n = wx_fmt_wx_state(b, sizeof b, &v); CHECK(strcmp(b, "[wx] have=1 temp10=-4 wmo=3 kind=cloud age=0\r\n") == 0); CHECK(is_wx_state(b));
    for (k = 0u; k < 8u; k++) { v.kind = (uint8_t)k; n = wx_fmt_wx_state(b, sizeof b, &v); CHECK(n > 0u); CHECK(is_wx_state(b)); }
    v.temp_x10 = 600; v.age_s = 0x7FFFFFFF; v.wmo = 99u; n = wx_fmt_wx_state(b, sizeof b, &v); CHECK(n > 0u); CHECK(is_wx_state(b));
    v.temp_x10 = -600; n = wx_fmt_wx_state(b, sizeof b, &v); CHECK(n > 0u); CHECK(is_wx_state(b));
    v.temp_x10 = 601; CHECK_EQ(wx_fmt_wx_state(b, sizeof b, &v), 0u); CHECK_EQ(b[0], 0);
    v.temp_x10 = 0; v.wmo = 100u; CHECK_EQ(wx_fmt_wx_state(b, sizeof b, &v), 0u);
    v.wmo = 1u; v.kind = 8u; CHECK_EQ(wx_fmt_wx_state(b, sizeof b, &v), 0u);
    v.kind = 1u; v.age_s = -1; CHECK_EQ(wx_fmt_wx_state(b, sizeof b, &v), 0u);                                               /* have = 1 で age = -1 は矛盾 */
    CHECK_EQ(wx_fmt_wx_state(NULL, 100u, &v), 0u); CHECK_EQ(wx_fmt_wx_state(b, 0u, &v), 0u); CHECK_EQ(wx_fmt_wx_state(b, sizeof b, NULL), 0u);
    v.age_s = 5; n = wx_fmt_wx_state(b, sizeof b, &v);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_wx_state(b, n, &v), 0u); CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_wx_state(b, n + 1u, &v), n);

    TEST("H-C5 秘密・本文の混入なし: 本文に目印(BODY_SENTINEL)・位置・時刻を入れた応答を解析して整形した全行に、本文の語が 1 件も無い");
    {
        wx_result_t pr;
        static const char *const BAD[] = { SENTINEL, "\"current\"", "current", "latitude", "longitude", "generationtime", "temperature_2m", "weather_code", "open-meteo", "35.7", "139.75", "Asia/Tokyo" };
        size_t bi;
        int    hits = 0;

        mk(HDR200, "-12.5", "71", "2026-11-27T21:45");
        CHECK(parse(&pr));
        pr.sym = (uint8_t)WX_WEATHER; pr.ms = 700u; pr.len = (uint16_t)g_n; pr.closed = 1u;
        for (s = 0; s < 5; s++) { n = wx_fmt_wx_ok(b, sizeof b, (wx_src_t)s, &pr); memcpy(all + alln, b, n); alln += n; }
        pr.ok = 0u; pr.err = 5u;
        for (s = 0; s < 5; s++) { n = wx_fmt_wx_fail(b, sizeof b, (wx_src_t)s, &pr); memcpy(all + alln, b, n); alln += n; }
        memset(&v, 0, sizeof v); v.have = 1u; v.temp_x10 = pr.temp_x10; v.wmo = pr.wmo; v.kind = pr.kind; v.age_s = 10;
        n = wx_fmt_wx_state(b, sizeof b, &v); memcpy(all + alln, b, n); alln += n;
        all[alln] = '\0';
        CHECK(alln > 500u);
        for (bi = 0u; bi < (sizeof BAD / sizeof BAD[0]); bi++) { if (strstr(all, BAD[bi]) != NULL) { hits++; printf("  混入: %s\n", BAD[bi]); } }
        CHECK_EQ(hits, 0);
    }
}

/* ================================================================ D 予定表(APP_WX_WEATHER = 1) */
static uint32_t   g_t;
static wx_sched_t g_s;

static wx_act_t pollat(uint32_t t) { g_t = t; return wx_sched_poll(&g_s, t); }

/* poll → FETCH(取得 ID sym、きっかけ src)→ dur ms 後に done(ok, http)。FETCH でなければ CHECK が落ちる */
static void fetch_d(uint32_t t, uint8_t sym, wx_src_t src, bool ok, uint16_t http, uint32_t dur)
{
    CHECK_EQ(pollat(t), WX_ACT_FETCH);
    CHECK_EQ(wx_sched_sym(&g_s), sym);
    CHECK_EQ(wx_sched_src(&g_s), src);
    CHECK(wx_sched_busy(&g_s));
    CHECK_EQ(pollat(t + 1u), WX_ACT_NONE);                                    /* done を待つ間は、次の動きを返さない */
    wx_sched_done(&g_s, WX_ACT_FETCH, ok, http, t + dur);
}
static void fetch_at(uint32_t t, uint8_t sym, wx_src_t src, bool ok, uint16_t http) { fetch_d(t, sym, src, ok, http, 3000u); }

static uint32_t boot_connect(uint32_t t0)
{
    wx_sched_init(&g_s, true, t0);
    CHECK_EQ(pollat(t0 + WX_BOOT_WAIT_MS - 1u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + WX_BOOT_WAIT_MS), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, t0 + WX_BOOT_WAIT_MS + 7000u);
    return t0 + WX_BOOT_WAIT_MS + 7000u;
}

/* 全部成功する 1 周期(天気 → 0 → 1 → 2)。戻り値 = 終わりの時刻 */
static uint32_t good_cycle(uint32_t t, wx_src_t src)
{
    fetch_at(t, 3u, src, true, 200u);
    fetch_at(t + 3000u, 0u, src, true, 200u);
    fetch_at(t + 6000u, 1u, src, true, 200u);
    fetch_at(t + 9000u, 2u, src, true, 200u);
    return t + 12000u;
}

static void t_sched_order(void)
{
    uint32_t tc, te;

    TEST("H-C3 ① 周期の取得の順は 3(天気)→ 0 → 1 → 2。起動の周期は src=boot、10 分後は src=timer。周期の開始 = 天気の取得の開始。周期の記録は株価の 3 ビット(ok = 7・fail = 0)");
    tc = boot_connect(1000u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 0u);
    fetch_at(tc, 3u, WX_SRC_BOOT, true, 200u);
    CHECK_EQ(g_s.t_fetch0, tc);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);                                  /* 天気の成功でも net = on */
    fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 6000u, 1u, WX_SRC_BOOT, true, 200u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 0u);
    fetch_at(tc + 9000u, 2u, WX_SRC_BOOT, true, 200u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 0u); CHECK_EQ(wx_sched_cycle(&g_s)->ms, 12000u);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 0u); CHECK(!wx_sched_busy(&g_s));
    CHECK_EQ(pollat(tc + 599999u), WX_ACT_NONE);                              /* 周期の開始から 10 分(終わりからではない) */
    te = good_cycle(tc + 600000u, WX_SRC_TIMER);
    CHECK_EQ(te, tc + 612000u); CHECK_EQ(wx_sched_cycle(&g_s)->n, 2u);
    CHECK_EQ(pollat(tc + 1199999u), WX_ACT_NONE); CHECK_EQ(pollat(tc + 1200000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_sym(&g_s), 3u);   /* 次の周期もまず天気 */
}

static void t_sched_wx_fail(void)
{
    uint32_t tc, te;
    int      i;

    TEST("H-C3 ② 天気だけ失敗(http 404): 株価 3 つは続けて取る → 20 秒後に天気だけやり直す(src=retry・最大 2 回)→ 周期の終わり。failed_mask は常に 0(partial が立たない)・cyc.fail = 0・net = on");
    tc = boot_connect(1000u);
    fetch_at(tc, 3u, WX_SRC_BOOT, false, 404u);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 0u); CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);          /* 天気の失敗はオフライン表示にしない */
    fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 6000u, 1u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 9000u, 2u, WX_SRC_BOOT, true, 200u);
    te = tc + 12000u;
    CHECK_EQ(wx_sched_failed_mask(&g_s), 0u); CHECK_EQ(wx_sched_cycle(&g_s)->n, 0u);            /* 周期はまだ終わっていない(天気のやり直しが残る) */
    CHECK(!wx_sched_busy(&g_s)); CHECK_EQ(pollat(te + WX_RETRY_MS - 1u), WX_ACT_NONE);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, te + 100u), WX_REQ_BUSY);                     /* やり直しの待ちは busy */
    fetch_at(te + WX_RETRY_MS, 3u, WX_SRC_RETRY, false, 404u);                                  /* 天気だけ(株価は取り直さない) */
    CHECK_EQ(wx_sched_failed_mask(&g_s), 0u);
    te = te + WX_RETRY_MS + 3000u;
    CHECK_EQ(pollat(te + WX_RETRY_MS - 1u), WX_ACT_NONE);
    fetch_at(te + WX_RETRY_MS, 3u, WX_SRC_RETRY, false, 404u);
    te = te + WX_RETRY_MS + 3000u;
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 0u); CHECK_EQ(wx_sched_cycle(&g_s)->ms, te - tc);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 0u); CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON); CHECK(!wx_sched_busy(&g_s));
    CHECK_EQ(pollat(te + WX_RETRY_MS), WX_ACT_NONE);                                            /* 3 回目はやり直さない */
    CHECK_EQ(pollat(tc + 599999u), WX_ACT_NONE);
    CHECK_EQ(pollat(tc + 600000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_sym(&g_s), 3u); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TIMER);   /* 次の定期の周期でもう一度試す */
    CHECK_EQ(wx_sched_failed_mask(&g_s), 0u);

    TEST("H-C3 ②b 天気のやり直しが 1 回目で成功 → 周期が終わる。天気が応答なし(http = 0)でも、株価が取れていれば再接続しない");
    tc = boot_connect(1000u);
    fetch_at(tc, 3u, WX_SRC_BOOT, false, 0u);
    fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 6000u, 1u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 9000u, 2u, WX_SRC_BOOT, true, 200u);
    te = tc + 12000u;
    fetch_at(te + WX_RETRY_MS, 3u, WX_SRC_RETRY, true, 200u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 0u);
    CHECK_EQ(pollat(te + 100000u), WX_ACT_NONE); CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    tc = boot_connect(1000u);                                                                    /* 天気が 3 回とも応答なし */
    fetch_at(tc, 3u, WX_SRC_BOOT, false, 0u);
    fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 6000u, 1u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 9000u, 2u, WX_SRC_BOOT, true, 200u);
    te = tc + 12000u;
    fetch_at(te + WX_RETRY_MS, 3u, WX_SRC_RETRY, false, 0u); te = te + WX_RETRY_MS + 3000u;
    fetch_at(te + WX_RETRY_MS, 3u, WX_SRC_RETRY, false, 0u); te = te + WX_RETRY_MS + 3000u;
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON); CHECK(!wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(pollat(te + 1u), WX_ACT_NONE);   /* 再接続しない */

    TEST("H-C3 ②c 天気の失敗が周期の途中で何度起きても(乱数 200 回の成否)、株価の取得の順序・回数は変わらない: 各周期の最初の 4 回は 3 → 0 → 1 → 2");
    for (i = 0; i < 200; i++) {
        bool wok = ((rnd() % 3u) == 0u);

        tc = boot_connect(5000u);
        fetch_at(tc, 3u, WX_SRC_BOOT, wok, wok ? 200u : (uint16_t)((rnd() & 1u) ? 404u : 0u));
        fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 6000u, 1u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 9000u, 2u, WX_SRC_BOOT, true, 200u);
        CHECK_EQ(wx_sched_failed_mask(&g_s), 0u);
    }
}

static void t_sched_stock_fail(void)
{
    uint32_t tc, te;

    TEST("H-C3 ③ 株価 1 つ(sym 1)が失敗し天気が成功 → 従来どおり株価だけやり直し・failed_mask = 2・cyc.fail = 2。天気と株価が両方失敗 → やり直しは 3 → 1 の順(同じパス)");
    tc = boot_connect(1000u);
    fetch_at(tc, 3u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 6000u, 1u, WX_SRC_BOOT, false, 404u);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 2u);
    fetch_at(tc + 9000u, 2u, WX_SRC_BOOT, true, 200u);
    te = tc + 12000u;
    fetch_at(te + WX_RETRY_MS, 1u, WX_SRC_RETRY, false, 404u); te = te + WX_RETRY_MS + 3000u;
    fetch_at(te + WX_RETRY_MS, 1u, WX_SRC_RETRY, false, 404u); te = te + WX_RETRY_MS + 3000u;
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 5u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 2u); CHECK_EQ(wx_sched_failed_mask(&g_s), 2u);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    tc = boot_connect(1000u);
    fetch_at(tc, 3u, WX_SRC_BOOT, false, 500u);
    fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 6000u, 1u, WX_SRC_BOOT, false, 500u);
    fetch_at(tc + 9000u, 2u, WX_SRC_BOOT, true, 200u);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 2u);                                                   /* 株価の 1 つだけ(天気は含めない) */
    te = tc + 12000u;
    CHECK_EQ(pollat(te + WX_RETRY_MS), WX_ACT_FETCH); CHECK_EQ(wx_sched_sym(&g_s), 3u); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_RETRY);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, te + WX_RETRY_MS + 3000u);                    /* 天気がやり直しで成功 */
    CHECK_EQ(wx_sched_failed_mask(&g_s), 2u);
    CHECK_EQ(pollat(te + WX_RETRY_MS + 3000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_sym(&g_s), 1u);  /* パスの途中は直ちに次 */
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, te + WX_RETRY_MS + 6000u);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 0u); CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 0u);
}

static void t_sched_429(void)
{
    uint32_t tc, te;

    TEST("H-C3 ④ 天気の 429 は普通の失敗: 周期を打ち切らず、株価 3 つを取り、20 秒後に天気だけやり直す(300 秒待たない)。更新要求は busy(ignored でない)");
    tc = boot_connect(1000u);
    fetch_at(tc, 3u, WX_SRC_BOOT, false, 429u);
    CHECK(!g_s.rate); CHECK_EQ(wx_sched_cycle(&g_s)->n, 0u); CHECK_EQ(wx_sched_failed_mask(&g_s), 0u);
    fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 6000u, 1u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 9000u, 2u, WX_SRC_BOOT, true, 200u);
    te = tc + 12000u;
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, te + 10u), WX_REQ_BUSY);
    CHECK_EQ(pollat(te + WX_RETRY_MS - 1u), WX_ACT_NONE);
    fetch_at(te + WX_RETRY_MS, 3u, WX_SRC_RETRY, false, 429u); te = te + WX_RETRY_MS + 3000u;      /* やり直しの 429 も普通の失敗 */
    fetch_at(te + WX_RETRY_MS, 3u, WX_SRC_RETRY, true, 200u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 0u);

    TEST("H-C3 ④b 株価の 429: 周期を打ち切り(残りは取らない)・天気は取得済みのまま・300 秒後に周期を最初(天気)からやり直す。待ちの間の更新要求は ignored");
    tc = boot_connect(1000u);
    fetch_at(tc, 3u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 6000u, 1u, WX_SRC_BOOT, false, 429u);
    CHECK(!wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 6u); CHECK_EQ(wx_sched_failed_mask(&g_s), 6u);
    te = tc + 9000u;
    CHECK_EQ(pollat(te + WX_RATE_MS - 1u), WX_ACT_NONE);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, te + 1000u), WX_REQ_IGNORED); CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, te + WX_RATE_MS - 1u), WX_REQ_IGNORED);
    CHECK_EQ(pollat(te + WX_RATE_MS), WX_ACT_FETCH); CHECK_EQ(wx_sched_sym(&g_s), 3u); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TIMER);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, te + WX_RATE_MS + 3000u);
    fetch_at(te + WX_RATE_MS + 3000u, 0u, WX_SRC_TIMER, true, 200u); fetch_at(te + WX_RATE_MS + 6000u, 1u, WX_SRC_TIMER, true, 200u); fetch_at(te + WX_RATE_MS + 9000u, 2u, WX_SRC_TIMER, true, 200u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 2u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u);
    CHECK_EQ(pollat(te + WX_RATE_MS + 599999u), WX_ACT_NONE); CHECK_EQ(pollat(te + WX_RATE_MS + 600000u), WX_ACT_FETCH);

    TEST("H-C3 ④c 天気が失敗している周期で株価が 429 → 取っていない取得はすべて失敗扱い(failed_mask は株価の bit だけ)・300 秒後に最初から。天気の bit は周期の頭で消える");
    tc = boot_connect(1000u);
    fetch_at(tc, 3u, WX_SRC_BOOT, false, 500u);
    fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, false, 429u);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 7u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 0u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 7u);
    CHECK_EQ(pollat(tc + 3000u + 3000u + WX_RATE_MS), WX_ACT_FETCH); CHECK_EQ(wx_sched_sym(&g_s), 3u); CHECK_EQ(wx_sched_failed_mask(&g_s), 0u);
}

static void t_sched_reconnect(void)
{
    uint32_t tc, te;
    int      i;

    TEST("H-C3 ⑤ 全部失敗の再接続は株価 3 つが応答なし(http = 0)のときだけ。天気が成功していても再接続する・天気だけ応答なしなら再接続しない・株価 1 つが応答ありなら再接続しない");
    tc = boot_connect(1000u);
    fetch_at(tc, 3u, WX_SRC_BOOT, true, 200u);                                                  /* 天気は成功・株価 3 つが応答なし */
    for (i = 0; i < 3; i++) { fetch_at(tc + 3000u + (uint32_t)i * 3000u, (uint8_t)i, WX_SRC_BOOT, false, 0u); }
    te = tc + 12000u;
    for (i = 0; i < 3; i++) { fetch_at(te + WX_RETRY_MS + (uint32_t)i * 3000u, (uint8_t)i, WX_SRC_RETRY, false, 0u); }       /* やり直しは株価だけ(天気は取り直さない) */
    te = te + WX_RETRY_MS + 9000u;
    for (i = 0; i < 3; i++) { fetch_at(te + WX_RETRY_MS + (uint32_t)i * 3000u, (uint8_t)i, WX_SRC_RETRY, false, 0u); }
    te = te + WX_RETRY_MS + 9000u;
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 0u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 7u);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ERR); CHECK(wx_sched_busy(&g_s));
    CHECK_EQ(pollat(te + 1u), WX_ACT_CONNECT);

    tc = boot_connect(1000u);                                                                   /* 天気も株価も応答なし */
    fetch_at(tc, 3u, WX_SRC_BOOT, false, 0u);
    for (i = 0; i < 3; i++) { fetch_at(tc + 3000u + (uint32_t)i * 3000u, (uint8_t)i, WX_SRC_BOOT, false, 0u); }
    te = tc + 12000u;
    for (i = 0; i < 2; i++) {
        fetch_at(te + WX_RETRY_MS, 3u, WX_SRC_RETRY, false, 0u);                                 /* やり直しのパスは 3 → 0 → 1 → 2 */
        fetch_at(te + WX_RETRY_MS + 3000u, 0u, WX_SRC_RETRY, false, 0u);
        fetch_at(te + WX_RETRY_MS + 6000u, 1u, WX_SRC_RETRY, false, 0u);
        fetch_at(te + WX_RETRY_MS + 9000u, 2u, WX_SRC_RETRY, false, 0u);
        te = te + WX_RETRY_MS + 12000u;
    }
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ERR); CHECK_EQ(pollat(te + 1u), WX_ACT_CONNECT);

    tc = boot_connect(1000u);                                                                   /* 株価の 1 つ(sym 1)が応答あり(403)なら再接続しない */
    fetch_at(tc, 3u, WX_SRC_BOOT, false, 0u);
    fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, false, 0u); fetch_at(tc + 6000u, 1u, WX_SRC_BOOT, false, 403u); fetch_at(tc + 9000u, 2u, WX_SRC_BOOT, false, 0u);
    te = tc + 12000u;
    for (i = 0; i < 2; i++) {
        fetch_at(te + WX_RETRY_MS, 3u, WX_SRC_RETRY, false, 0u);
        fetch_at(te + WX_RETRY_MS + 3000u, 0u, WX_SRC_RETRY, false, 0u);
        fetch_at(te + WX_RETRY_MS + 6000u, 1u, WX_SRC_RETRY, false, 403u);
        fetch_at(te + WX_RETRY_MS + 9000u, 2u, WX_SRC_RETRY, false, 0u);
        te = te + WX_RETRY_MS + 12000u;
    }
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON); CHECK(!wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 7u); CHECK_EQ(pollat(te + 1u), WX_ACT_NONE);

    tc = boot_connect(1000u);                                                                   /* 株価 1 つが成功なら再接続しない(天気は失敗) */
    fetch_at(tc, 3u, WX_SRC_BOOT, false, 0u);
    fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 6000u, 1u, WX_SRC_BOOT, false, 0u); fetch_at(tc + 9000u, 2u, WX_SRC_BOOT, false, 0u);
    te = tc + 12000u;
    for (i = 0; i < 2; i++) {
        fetch_at(te + WX_RETRY_MS, 3u, WX_SRC_RETRY, false, 0u);
        fetch_at(te + WX_RETRY_MS + 3000u, 1u, WX_SRC_RETRY, false, 0u);
        fetch_at(te + WX_RETRY_MS + 6000u, 2u, WX_SRC_RETRY, false, 0u);
        te = te + WX_RETRY_MS + 9000u;
    }
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON); CHECK(!wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 6u);

    TEST("H-C3 ⑤b 再接続が成功したら直ちに新しい周期(天気から。src は timer)・接続の失敗の待ちは 60 / 120 / 300 秒");
    tc = boot_connect(1000u);
    fetch_at(tc, 3u, WX_SRC_BOOT, false, 0u);
    for (i = 0; i < 3; i++) { fetch_at(tc + 3000u + (uint32_t)i * 3000u, (uint8_t)i, WX_SRC_BOOT, false, 0u); }
    te = tc + 12000u;
    for (i = 0; i < 2; i++) {
        fetch_at(te + WX_RETRY_MS, 3u, WX_SRC_RETRY, false, 0u); fetch_at(te + WX_RETRY_MS + 3000u, 0u, WX_SRC_RETRY, false, 0u);
        fetch_at(te + WX_RETRY_MS + 6000u, 1u, WX_SRC_RETRY, false, 0u); fetch_at(te + WX_RETRY_MS + 9000u, 2u, WX_SRC_RETRY, false, 0u);
        te = te + WX_RETRY_MS + 12000u;
    }
    CHECK_EQ(pollat(te + 1u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, te + 8000u); te += 8000u;
    CHECK_EQ(pollat(te + 59999u), WX_ACT_NONE); CHECK_EQ(pollat(te + 60000u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, te + 60000u + 8000u);
    te = te + 60000u + 8000u;
    good_cycle(te, WX_SRC_TIMER);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 2u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u); CHECK_EQ(wx_sched_failed_mask(&g_s), 0u);
}

static void t_sched_time(void)
{
    uint32_t tc, te;

    TEST("H-C3 ⑥ now_ms が 32bit で巻き戻っても、周期の開始から 600,000ms・20 秒・300 秒が同じ(天気を含めた周期)");
    tc = boot_connect(0xFFFFC000u);
    CHECK(tc > 0xF0000000u);
    te = good_cycle(tc, WX_SRC_BOOT);
    CHECK(te < tc);                                                                              /* 周期の途中で 0 を越える */
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ms, 12000u);
    CHECK_EQ(pollat(tc + 599999u), WX_ACT_NONE);
    fetch_at(tc + 600000u, 3u, WX_SRC_TIMER, false, 500u); fetch_at(tc + 603000u, 0u, WX_SRC_TIMER, true, 200u);
    fetch_at(tc + 606000u, 1u, WX_SRC_TIMER, true, 200u); fetch_at(tc + 609000u, 2u, WX_SRC_TIMER, true, 200u);
    CHECK_EQ(pollat(tc + 612000u + WX_RETRY_MS - 1u), WX_ACT_NONE); CHECK_EQ(pollat(tc + 612000u + WX_RETRY_MS), WX_ACT_FETCH);
    CHECK_EQ(wx_sched_src(&g_s), WX_SRC_RETRY); CHECK_EQ(wx_sched_sym(&g_s), 3u);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, tc + 612000u + WX_RETRY_MS + 3000u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 2u);
    CHECK_EQ(pollat(tc + 1199999u), WX_ACT_NONE); CHECK_EQ(pollat(tc + 1200000u), WX_ACT_FETCH);                        /* 周期のずれが積もらない */
    tc = boot_connect(0xFFFF0000u);
    fetch_at(tc, 3u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 3000u, 0u, WX_SRC_BOOT, false, 429u);
    CHECK_EQ(pollat(tc + 6000u + WX_RATE_MS - 1u), WX_ACT_NONE); CHECK_EQ(pollat(tc + 6000u + WX_RATE_MS), WX_ACT_FETCH); CHECK_EQ(wx_sched_sym(&g_s), 3u);
}

static void t_sched_req(void)
{
    uint32_t tc;

    TEST("H-C3 ⑦ 更新要求: 4 回の周期(天気 + 株価 3)が始まる・5 秒未満は ignored・取得中は busy・OFF は off。受け付けた周期の最初は天気(src = touch / uart)");
    tc = boot_connect(1000u);
    fetch_d(tc, 3u, WX_SRC_BOOT, true, 200u, 100u);                                              /* 取得が一瞬で終わる周期 */
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, tc + 150u), WX_REQ_BUSY);                       /* 周期の途中 */
    fetch_d(tc + 100u, 0u, WX_SRC_BOOT, true, 200u, 100u);
    fetch_d(tc + 200u, 1u, WX_SRC_BOOT, true, 200u, 100u);
    fetch_d(tc + 300u, 2u, WX_SRC_BOOT, true, 200u, 100u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ms, 400u);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, tc + 4999u), WX_REQ_IGNORED);                    /* 周期の開始から 5 秒未満 */
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, tc + 5000u), WX_REQ_OK);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, tc + 5001u), WX_REQ_BUSY);                      /* 受け付けた後の poll 前も busy */
    fetch_d(tc + 5100u, 3u, WX_SRC_UART, true, 200u, 100u);
    fetch_d(tc + 5200u, 0u, WX_SRC_UART, true, 200u, 100u);
    fetch_d(tc + 5300u, 1u, WX_SRC_UART, true, 200u, 100u);
    fetch_d(tc + 5400u, 2u, WX_SRC_UART, true, 200u, 100u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 2u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u);
    CHECK_EQ(pollat(tc + 5100u + 599999u), WX_ACT_NONE);                                          /* 次の定期は、更新要求で始まった周期から 10 分 */
    CHECK_EQ(pollat(tc + 5100u + 600000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TIMER); CHECK_EQ(wx_sched_sym(&g_s), 3u);
    wx_sched_init(&g_s, false, 0u);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, 100000u), WX_REQ_OFF); CHECK_EQ(pollat(100000u), WX_ACT_NONE);

    TEST("H-C3 ⑦b オフライン(接続の待ち)の更新要求は直ちに再接続 → 成功したら天気から。起動待ち・接続中は busy");
    wx_sched_init(&g_s, true, 1000u);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, 1010u), WX_REQ_BUSY);
    tc = boot_connect(1000u);
    fetch_at(tc, 3u, WX_SRC_BOOT, false, 0u);
    {
        int i;
        for (i = 0; i < 3; i++) { fetch_at(tc + 3000u + (uint32_t)i * 3000u, (uint8_t)i, WX_SRC_BOOT, false, 0u); }
        tc += 12000u;
        for (i = 0; i < 2; i++) {
            fetch_at(tc + WX_RETRY_MS, 3u, WX_SRC_RETRY, false, 0u); fetch_at(tc + WX_RETRY_MS + 3000u, 0u, WX_SRC_RETRY, false, 0u);
            fetch_at(tc + WX_RETRY_MS + 6000u, 1u, WX_SRC_RETRY, false, 0u); fetch_at(tc + WX_RETRY_MS + 9000u, 2u, WX_SRC_RETRY, false, 0u);
            tc = tc + WX_RETRY_MS + 12000u;
        }
    }
    CHECK_EQ(pollat(tc), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, tc + 8000u); tc += 8000u;
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, tc + 100u), WX_REQ_OK);                         /* 60 秒を待たない */
    CHECK_EQ(pollat(tc + 100u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, tc + 5000u);
    CHECK_EQ(pollat(tc + 5000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_sym(&g_s), 3u); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TOUCH);
}

int main(int argc, char **argv)
{
    g_dir = (argc > 1) ? argv[1] : "tests/host/data";
    t_request();
    t_real();
    t_codes();
    t_temps();
    t_keys();
    t_http();
    t_cut();
    t_cross();
    t_fuzz();
    t_nobody();
    t_fmt();
    t_sched_order();
    t_sched_wx_fail();
    t_sched_stock_fail();
    t_sched_429();
    t_sched_reconnect();
    t_sched_time();
    t_sched_req();
    TEST_REPORT("test_wx_weather");
}
