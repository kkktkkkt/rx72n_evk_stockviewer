/* test_wx_core.c - src/app/wx_core.c の単体試験(docs/design.md §22.12 の H-X1〜H-X4、H-X7、H-X6 の書式側)。標準Cのみ。
 * -DAPP_WIFI_TEST=1 でコンパイルする(0 の時 wx_core.c は空)。
 * 試験で使う応答は、設計 §22.1 の 5(PC から実測した Open-Meteo の応答)の形。SSID・パスワードは扱わない(wx_core は持たない)。 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "wx_core.h"
#include "test_framework.h"

/* ---------------------------------------------------------------- 応答の作成 */

#define DEG "\xC2\xB0" "C"
#define HDR_OK "HTTP/1.0 200 OK\r\nDate: Wed, 25 Nov 2026 04:24:13 GMT\r\nContent-Type: application/json; charset=utf-8\r\n" \
               "X-RateLimit-Limit: 600\r\nConnection: close\r\n\r\n"

/* 本文。temp / code は値の文字列(そのまま埋める) */
static size_t body(char *b, size_t sz, const char *temp, const char *code)
{
    return (size_t)snprintf(b, sz,
        "{\"latitude\":35.7,\"longitude\":139.75,\"generationtime_ms\":0.04,\"utc_offset_seconds\":32400,\"timezone\":\"Asia/Tokyo\","
        "\"timezone_abbreviation\":\"GMT+9\",\"elevation\":12.0,\"current_units\":{\"time\":\"iso8601\",\"interval\":\"seconds\","
        "\"temperature_2m\":\"" DEG "\",\"weather_code\":\"wmo code\"},\"current\":{\"time\":\"2026-11-25T13:30\",\"interval\":900,"
        "\"temperature_2m\":%s,\"weather_code\":%s}}", temp, code);
}

static size_t resp(char *b, size_t sz, const char *hdr, const char *temp, const char *code)
{
    size_t n = (size_t)snprintf(b, sz, "%s", hdr);

    return n + body(b + n, sz - n, temp, code);
}

static wx_result_t g_r;

static bool parse_str(const char *s)
{
    return wx_parse_weather(s, strlen(s), &g_r);
}

/* ---------------------------------------------------------------- H-X0 要求 */

static void t_request(void)
{
    char   b[256];
    size_t n;

    TEST("H-X0 wx_build_request(190 バイト・固定)");
    memset(b, 'Q', sizeof b);
    n = wx_build_request(b, sizeof b, WX_TGT_METEO, 0);
    CHECK_EQ(n, 190u);
    CHECK_EQ(n, WX_REQ_LEN);
    CHECK_EQ(strlen(b), 190u);
    CHECK(strncmp(b, "GET /v1/forecast?latitude=35.68&longitude=139.76&current=temperature_2m,weather_code&timezone=Asia%2FTokyo HTTP/1.0\r\n", 117u) == 0);
    CHECK(strstr(b, "\r\nHost: api.open-meteo.com\r\n") != NULL);
    CHECK(strstr(b, "Accept-Encoding") == NULL);
    CHECK(strcmp(b + n - 4u, "\r\n\r\n") == 0);
    CHECK(strstr(b, "HTTP/1.0") != NULL);
    CHECK(strstr(b, "Connection: close\r\n") != NULL);
    memset(b, 'Q', sizeof b);
    CHECK_EQ(wx_build_request(b, 190u, WX_TGT_METEO, 0), 0u);                    /* NUL の分が足りない */
    CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b);
    CHECK_EQ(wx_build_request(b, 191u, WX_TGT_METEO, 0), 190u);
    CHECK_EQ(b[191], 'Q');                                      /* 越えて書かない */
    CHECK_EQ(wx_build_request(NULL, 191u, WX_TGT_METEO, 0), 0u);
    CHECK_EQ(wx_build_request(b, 0u, WX_TGT_METEO, 0), 0u);
}

/* ---------------------------------------------------------------- Phase 0: 接続先 */

static void t_target(void)
{
    char   b[300];
    size_t n;
    unsigned sym;
    static const size_t LEN[3] = { 144u, 145u, 145u };            /* design.md §5.1(○ 計算)を、ここで実測 */

    TEST("P0-A wx_build_cipstart(5 つの接続先・範囲外・小さな buf)と wx_target_ssl");
    n = wx_build_cipstart(b, sizeof b, WX_TGT_METEO);
    CHECK(strcmp(b, "AT+CIPSTART=\"TCP\",\"api.open-meteo.com\",80\r\n") == 0); CHECK_EQ(n, strlen(b));
    n = wx_build_cipstart(b, sizeof b, WX_TGT_YAHOO_TCP);
    CHECK(strcmp(b, "AT+CIPSTART=\"TCP\",\"query1.finance.yahoo.com\",80\r\n") == 0); CHECK_EQ(n, strlen(b));
    n = wx_build_cipstart(b, sizeof b, WX_TGT_YAHOO_SSL);
    CHECK(strcmp(b, "AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n") == 0); CHECK_EQ(n, strlen(b));
    n = wx_build_cipstart(b, sizeof b, WX_TGT_GOOGLE_SSL);
    CHECK(strcmp(b, "AT+CIPSTART=\"SSL\",\"www.google.com\",443\r\n") == 0); CHECK_EQ(n, strlen(b));
    n = wx_build_cipstart(b, sizeof b, WX_TGT_YAHOO_SSL_NOAUTH);
    CHECK(strcmp(b, "AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n") == 0);
    memset(b, 'Q', sizeof b);
    CHECK_EQ(wx_build_cipstart(b, sizeof b, WX_TGT_END), 0u); CHECK_EQ(b[0], 0);
    CHECK_EQ(wx_build_cipstart(NULL, 10u, WX_TGT_YAHOO_SSL), 0u);
    CHECK_EQ(wx_build_cipstart(b, 0u, WX_TGT_YAHOO_SSL), 0u);
    n = strlen("AT+CIPSTART=\"SSL\",\"www.google.com\",443\r\n");
    memset(b, 'Q', sizeof b);
    CHECK_EQ(wx_build_cipstart(b, n, WX_TGT_GOOGLE_SSL), 0u); CHECK_EQ(b[0], 0);         /* NUL の分が足りない */
    CHECK_EQ(wx_build_cipstart(b, n + 1u, WX_TGT_GOOGLE_SSL), n); CHECK_EQ(b[n + 1u], 'Q');
    CHECK(!wx_target_ssl(WX_TGT_METEO)); CHECK(!wx_target_ssl(WX_TGT_YAHOO_TCP));
    CHECK(wx_target_ssl(WX_TGT_YAHOO_SSL)); CHECK(wx_target_ssl(WX_TGT_GOOGLE_SSL)); CHECK(wx_target_ssl(WX_TGT_YAHOO_SSL_NOAUTH));
    CHECK(!wx_target_ssl(WX_TGT_END)); CHECK(!wx_target_ssl((wx_target_t)255));

    TEST("P0-B wx_build_request(Yahoo: 3 銘柄の長さ 144 / 145 / 145・UA・range=2d・HTTP/1.0・改行)");
    for (sym = 0u; sym < 3u; sym++) {
        static const char *const ENC[3] = { "6723.T", "%5EN225", "%5EGSPC" };
        char want[200];

        memset(b, 'Q', sizeof b);
        n = wx_build_request(b, sizeof b, WX_TGT_YAHOO_SSL, (uint8_t)sym);
        (void)snprintf(want, sizeof want, "GET /v8/finance/chart/%s?range=2d&interval=1d HTTP/1.0\r\nHost: query1.finance.yahoo.com\r\n"
                       "User-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n", ENC[sym]);
        CHECK(strcmp(b, want) == 0); CHECK_EQ(n, strlen(want)); CHECK_EQ(n, LEN[sym]);
        CHECK_EQ(wx_build_request(b, sizeof b, WX_TGT_YAHOO_TCP, (uint8_t)sym), n);          /* 平文も同じ要求 */
        CHECK_EQ(wx_build_request(b, sizeof b, WX_TGT_YAHOO_SSL_NOAUTH, (uint8_t)sym), n);
        memset(b, 'Q', sizeof b);
        CHECK_EQ(wx_build_request(b, n, WX_TGT_YAHOO_SSL, (uint8_t)sym), 0u); CHECK_EQ(b[0], 0);   /* NUL の分が足りない */
        CHECK_EQ(wx_build_request(b, n + 1u, WX_TGT_YAHOO_SSL, (uint8_t)sym), n); CHECK_EQ(b[n + 1u], 'Q');
    }
    memset(b, 'Q', sizeof b);
    CHECK_EQ(wx_build_request(b, sizeof b, WX_TGT_YAHOO_SSL, 3u), 0u); CHECK_EQ(b[0], 0);    /* 銘柄の範囲外 */
    CHECK_EQ(wx_build_request(b, sizeof b, WX_TGT_YAHOO_SSL, 255u), 0u);
    CHECK_EQ(wx_build_request(b, sizeof b, WX_TGT_END, 1u), 0u); CHECK_EQ(b[0], 0);
    CHECK_EQ(wx_build_request(NULL, 300u, WX_TGT_YAHOO_SSL, 1u), 0u);
    CHECK_EQ(wx_build_request(b, 0u, WX_TGT_YAHOO_SSL, 1u), 0u);
    CHECK_EQ(wx_build_request(b, sizeof b, WX_TGT_METEO, 200u), WX_REQ_LEN);               /* 天気は sym を見ない */

    TEST("P0-C wx_build_request(Google: GET / ・Host・UA・Connection: close・改行で終わる)");
    n = wx_build_request(b, sizeof b, WX_TGT_GOOGLE_SSL, 0u);
    CHECK(strcmp(b, "GET / HTTP/1.0\r\nHost: www.google.com\r\nUser-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n") == 0);
    CHECK_EQ(n, strlen(b));
    CHECK_EQ(wx_build_request(b, 10u, WX_TGT_GOOGLE_SSL, 0u), 0u); CHECK_EQ(b[0], 0);
}

/* ---------------------------------------------------------------- H-X1 解析 */

static void t_parse_real(void)
{
    char       b[1200];
    char       c[1300];
    size_t     n, m;
    wx_date_t  d;

    TEST("H-X1-1 実測の応答(HTTP/1.0)");
    n = resp(b, sizeof b, HDR_OK, "12.7", "0");
    CHECK(wx_parse_weather(b, n, &g_r));
    CHECK_EQ(g_r.ok, 1); CHECK_EQ(g_r.err, 0); CHECK_EQ(g_r.http, 200);
    CHECK_EQ(g_r.temp_x10, 127); CHECK_EQ(g_r.wmo, 0); CHECK_EQ(g_r.kind, WX_KIND_SUN);
    CHECK_EQ(g_r.obs, 1330);
    CHECK_EQ(g_r.have_date, 1);
    wx_date_fields(g_r.jst_s, &d);                              /* 04:24:13 GMT = 13:24:13 JST */
    CHECK_EQ(d.year, 2026); CHECK_EQ(d.month, 11); CHECK_EQ(d.day, 25); CHECK_EQ(d.wday, 3);
    CHECK_EQ(d.hour, 13); CHECK_EQ(d.min, 24); CHECK_EQ(d.sec, 13);

    TEST("H-X1-2 chunked(本文の前に 175 と改行)も同じ値");
    m = (size_t)snprintf(c, sizeof c, "%s", "HTTP/1.1 200 OK\r\nDate: Wed, 25 Nov 2026 04:24:13 GMT\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n175\r\n");
    m += body(c + m, sizeof c - m, "12.7", "0");
    m += (size_t)snprintf(c + m, sizeof c - m, "\r\n0\r\n\r\n");
    CHECK(wx_parse_weather(c, m, &g_r));
    CHECK_EQ(g_r.temp_x10, 127); CHECK_EQ(g_r.wmo, 0); CHECK_EQ(g_r.obs, 1330); CHECK_EQ(g_r.have_date, 1);
}

static void t_parse_values(void)
{
    static const struct { const char *t; int v; } T[] = {
        { "-3.4", -34 }, { "0.0", 0 }, { "-0.0", 0 }, { "38.9", 389 }, { "12.65", 126 }, { "12", 120 }, { "60.0", 600 }, { "-60.0", -600 },
        { "5", 50 }, { "-5", -50 }, { "0", 0 }, { "12.7", 127 }, { "-12.7", -127 }, { "0.5", 5 }, { "-0.1", -1 }, { "9.99", 99 }, { "35.04", 350 }
    };
    static const struct { const char *t; uint8_t e; } E[] = {
        { "null", 5 }, { "\"" DEG "\"", 5 }, { "-", 5 }, { "abc", 5 }, { "", 5 }, { "true", 5 }, { "1e1", 5 }, { "12x", 5 },
        { "99.9", 6 }, { "-61", 6 }, { "60.1", 6 }, { "-60.1", 6 }, { "100", 6 }, { "123.4", 6 }, { "-999.9", 6 }
    };
    static const struct { const char *c; uint8_t e; } CE[] = {
        { "null", 6 }, { "-1", 6 }, { "100", 6 }, { "255", 6 }, { "1000", 6 }, { "\"x\"", 6 }, { "abc", 6 }, { "9x", 6 }
    };
    char   b[1200];
    size_t i, n;

    TEST("H-X1-3 気温(符号・小数・桁・範囲の境界)");
    for (i = 0u; i < (sizeof T / sizeof T[0]); i++) {
        n = resp(b, sizeof b, HDR_OK, T[i].t, "3");
        CHECK(wx_parse_weather(b, n, &g_r));
        if (g_r.temp_x10 != T[i].v) { printf("  temp %s -> %d\n", T[i].t, g_r.temp_x10); }
        CHECK_EQ(g_r.temp_x10, T[i].v);
        CHECK_EQ(g_r.kind, WX_KIND_CLOUD);
    }
    TEST("H-X1-4 異常(null・文字列・数字なし・範囲外)は失敗で、値を残さない");
    for (i = 0u; i < (sizeof E / sizeof E[0]); i++) {
        n = resp(b, sizeof b, HDR_OK, E[i].t, "0");
        CHECK(!wx_parse_weather(b, n, &g_r));
        if (g_r.err != E[i].e) { printf("  temp '%s' -> err %d\n", E[i].t, g_r.err); }
        CHECK_EQ(g_r.err, E[i].e);
        CHECK_EQ(g_r.ok, 0);
        CHECK_EQ(g_r.have_date, 1);                             /* 天気が失敗でも Date: は読める(時刻同期できる) */
        CHECK_EQ(g_r.http, 200);
    }
    for (i = 0u; i < (sizeof CE / sizeof CE[0]); i++) {
        n = resp(b, sizeof b, HDR_OK, "12.7", CE[i].c);
        CHECK(!wx_parse_weather(b, n, &g_r));
        if (g_r.err != CE[i].e) { printf("  code '%s' -> err %d\n", CE[i].c, g_r.err); }
        CHECK_EQ(g_r.err, CE[i].e);
    }
    n = resp(b, sizeof b, HDR_OK, "12.7", "99");
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.wmo, 99); CHECK_EQ(g_r.kind, WX_KIND_THUNDER);
    n = resp(b, sizeof b, HDR_OK, "12.7", "61");
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.wmo, 61); CHECK_EQ(g_r.kind, WX_KIND_RAIN);
}

static void t_parse_units(void)
{
    char   b[1500];
    size_t n;
    const char *tail;

    TEST("H-X1-5 current_units の中の \"temperature_2m\":\"°C\" を気温として読まない");
    n = resp(b, sizeof b, HDR_OK, "12.7", "0");
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.temp_x10, 127);
    /* current に気温が無く、current_units にだけある → 失敗(units を読まない) */
    n = (size_t)snprintf(b, sizeof b, "%s{\"current_units\":{\"temperature_2m\":\"" DEG "\",\"weather_code\":\"wmo code\"},\"current\":{\"time\":\"2026-11-25T13:30\",\"weather_code\":0}}", HDR_OK);
    CHECK(!wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.err, 5);
    /* current_units だけで current が無い */
    n = (size_t)snprintf(b, sizeof b, "%s{\"current_units\":{\"temperature_2m\":55.5,\"weather_code\":1}}", HDR_OK);
    CHECK(!wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.err, 5);
    /* units に数値っぽい値があっても、current の値を使う */
    n = (size_t)snprintf(b, sizeof b, "%s{\"current_units\":{\"temperature_2m\":55.5,\"weather_code\":7},\"current\":{\"temperature_2m\":1.5,\"weather_code\":3}}", HDR_OK);
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.temp_x10, 15); CHECK_EQ(g_r.wmo, 3);

    TEST("H-X1-9 ヘッダーに current という語があっても本文だけを見る");
    tail = "{\"current\":{\"time\":\"2026-11-25T13:30\",\"temperature_2m\":-1.2,\"weather_code\":71}}";
    n = (size_t)snprintf(b, sizeof b, "HTTP/1.0 200 OK\r\nDate: Wed, 25 Nov 2026 04:24:13 GMT\r\nX-Note: \"current\":{\"temperature_2m\":99.9,\"weather_code\":99}\r\n\r\n%s", tail);
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.temp_x10, -12); CHECK_EQ(g_r.wmo, 71); CHECK_EQ(g_r.kind, WX_KIND_SNOW);
    n = (size_t)snprintf(b, sizeof b, "HTTP/1.0 200 OK\r\nX-Current: current temperature_2m\r\n\r\n%s", tail);
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.temp_x10, -12);
    CHECK_EQ(g_r.have_date, 0);

    TEST("H-X1 obs(current.time)が無くても成功。空行が無くても全体を探す");
    n = (size_t)snprintf(b, sizeof b, "%s{\"current\":{\"temperature_2m\":3.0,\"weather_code\":2}}", HDR_OK);
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.obs, 0); CHECK_EQ(g_r.kind, WX_KIND_PARTLY);
    n = (size_t)snprintf(b, sizeof b, "HTTP/1.0 200 OK\r\nDate: Wed, 25 Nov 2026 04:24:13 GMT\r\n{\"current\":{\"time\":\"2026-11-25T09:05\",\"temperature_2m\":3.0,\"weather_code\":2}}");
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.obs, 905); CHECK_EQ(g_r.have_date, 1);
}

static void t_parse_status(void)
{
    static const int ST[] = { 301, 429, 500, 404, 403, 503, 100, 204 };
    char   b[1200];
    size_t i, n;

    TEST("H-X1-6 状態が 200 でない(http を記録して失敗。Date: は残す)");
    for (i = 0u; i < (sizeof ST / sizeof ST[0]); i++) {
        n = (size_t)snprintf(b, sizeof b, "HTTP/1.1 %d X\r\nDate: Wed, 25 Nov 2026 04:24:13 GMT\r\nConnection: close\r\n\r\nplease use https", ST[i]);
        CHECK(!wx_parse_weather(b, n, &g_r));
        CHECK_EQ(g_r.err, 4); CHECK_EQ(g_r.http, ST[i]); CHECK_EQ(g_r.have_date, 1); CHECK_EQ(g_r.ok, 0);
    }
    /* 状態行が HTTP でない・短い */
    CHECK(!parse_str("XTTP/1.0 200 OK\r\n\r\n{}")); CHECK_EQ(g_r.err, 4); CHECK_EQ(g_r.http, 0);
    CHECK(!parse_str("HTTP/2.0 200 OK\r\n\r\n{}"));   CHECK_EQ(g_r.err, 4);
    CHECK(!parse_str("HTTP/1.0 2x0 OK\r\n\r\n{}"));   CHECK_EQ(g_r.err, 4);
    CHECK(!parse_str("HTTP/1.0 20"));                CHECK_EQ(g_r.err, 3);
    CHECK(!parse_str(""));                           CHECK_EQ(g_r.err, 3);
    CHECK(!wx_parse_weather(NULL, 0u, &g_r));           CHECK_EQ(g_r.err, 3);
    CHECK(!parse_str("HTTP/1.0 200 OK"));            CHECK_EQ(g_r.err, 5);     /* 状態行だけ。キー無し */
}

static void t_parse_date_header(void)
{
    char   b[1200];
    size_t n;

    TEST("H-X1-8 Date: が無い・壊れている・小文字");
    n = resp(b, sizeof b, "HTTP/1.0 200 OK\r\nContent-Type: x\r\n\r\n", "12.7", "0");
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.have_date, 0); CHECK_EQ(g_r.ok, 1);       /* 天気は成功 */
    n = resp(b, sizeof b, "HTTP/1.0 200 OK\r\nDate: Wed, 25 Xxx 2026 04:24:13 GMT\r\n\r\n", "12.7", "0");
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.have_date, 0);
    n = resp(b, sizeof b, "HTTP/1.0 200 OK\r\nDate: Wed, 25 Nov 2026 04:24:13\r\n\r\n", "12.7", "0");      /* GMT なし */
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.have_date, 0);
    n = resp(b, sizeof b, "HTTP/1.0 200 OK\r\ndate: Wed, 25 Nov 2026 04:24:13 GMT\r\n\r\n", "12.7", "0");
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.have_date, 1);
    n = resp(b, sizeof b, "HTTP/1.0 200 OK\r\nDATE:Wed, 25 Nov 2026 04:24:13 GMT\r\n\r\n", "12.7", "0");
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.have_date, 1);
    n = resp(b, sizeof b, "HTTP/1.0 200 OK\r\nX-Date: Wed, 25 Nov 2026 04:24:13 GMT\r\n\r\n", "12.7", "0");   /* 行頭が Date: でない */
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.have_date, 0);
    /* 本文の中の Date: は見ない */
    n = (size_t)snprintf(b, sizeof b, "HTTP/1.0 200 OK\r\n\r\nDate: Wed, 25 Nov 2026 04:24:13 GMT\r\n{\"current\":{\"temperature_2m\":1.0,\"weather_code\":0}}");
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.have_date, 0);
    n = resp(b, sizeof b, "HTTP/1.0 200 OK\r\nDate: Wed, 25 Nov 2019 04:24:13 GMT\r\n\r\n", "12.7", "0");  /* 範囲外(2019) */
    CHECK(wx_parse_weather(b, n, &g_r)); CHECK_EQ(g_r.have_date, 0);
}

static void t_parse_truncate(void)
{
    char       full[1200];
    char       a[1200];
    char       p[1200];
    size_t     n, len;
    wx_result_t ra, rp;
    int        succ = 0;

    TEST("H-X1-7 途中で切れた応答: あらゆる長さで、落ちず、バッファの外を読まず、成功した時は全体と同じ値");
    n = resp(full, sizeof full, HDR_OK, "-3.4", "61");
    for (len = 0u; len <= n; len++) {
        memcpy(a, full, n);                                     /* A: 切った後ろに本物の続きがある */
        memcpy(p, full, len);
        memset(p + len, 'Z', n - len);                          /* B: 切った後ろが毒(Z) */
        memset(&ra, 0xA5, sizeof ra); memset(&rp, 0x5A, sizeof rp);
        (void)wx_parse_weather(a, len, &ra);
        (void)wx_parse_weather(p, len, &rp);
        CHECK_EQ(ra.ok, rp.ok); CHECK_EQ(ra.err, rp.err); CHECK_EQ(ra.http, rp.http); CHECK_EQ(ra.have_date, rp.have_date);
        CHECK_EQ(ra.temp_x10, rp.temp_x10); CHECK_EQ(ra.wmo, rp.wmo); CHECK_EQ(ra.obs, rp.obs); CHECK_EQ(ra.jst_s, rp.jst_s);
        if (rp.ok != 0u) {
            succ++;
            CHECK_EQ(rp.temp_x10, -34); CHECK_EQ(rp.wmo, 61); CHECK_EQ(rp.kind, WX_KIND_RAIN);
        } else {
            CHECK(rp.err >= 3u); CHECK(rp.err <= 6u);
        }
    }
    CHECK(succ >= 1);
    CHECK(succ < (int)n);                                       /* 短く切れたものは失敗にしている */
    /* 気温の数字の途中で切れた時に、前半の値で成功にしない(-3.4 の -3 など) */
    {
        const char *k = "\"temperature_2m\":-3.4";
        size_t at = (size_t)(strstr(full, k) - full) + strlen("\"temperature_2m\":");
        CHECK(!wx_parse_weather(full, at + 2u, &g_r));             /* "-3" */
        CHECK(!wx_parse_weather(full, at + 4u, &g_r));             /* "-3.4"(区切りが無い) */
    }
    TEST("H-X1-10 巨大な応答・ゴミ");
    {
        static char big[8000];

        memset(big, 'A', sizeof big);
        CHECK(!wx_parse_weather(big, sizeof big, &g_r)); CHECK_EQ(g_r.err, 4);
        n = resp(full, sizeof full, HDR_OK, "12.7", "0");
        memset(big, ' ', sizeof big);
        memcpy(big, full, n);
        CHECK(wx_parse_weather(big, sizeof big, &g_r));            /* 後ろが空白でも読める */
        CHECK_EQ(g_r.temp_x10, 127);
        for (len = 0u; len < 300u; len++) {                     /* ランダムなバイト列でも落ちない */
            size_t k;
            for (k = 0u; k < 200u; k++) { big[k] = (char)(rand() & 0xFF); }
            (void)wx_parse_weather(big, (len % 200u), &g_r);
        }
    }
}

/* ---------------------------------------------------------------- H-X2 時刻 */

static void t_date(void)
{
    uint32_t  u = 0u;
    wx_date_t d;
    char      b[64];
    time_t    tt;
    struct tm *g;
    uint32_t  secs;
    long      i;
    int       bad = 0;
    static const char *const WD[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const MN[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const char *const REJ[] = {
        " Wed, 25 Nov 2026 24:00:00 GMT", " Wed, 25 Nov 2026 23:60:00 GMT", " Wed, 25 Nov 2026 23:59:60 GMT", " Wed, 25 Foo 2026 04:24:13 GMT",
        " Wed, 0 Nov 2026 04:24:13 GMT", " Wed, 32 Nov 2026 04:24:13 GMT", " Wed, 31 Nov 2026 04:24:13 GMT", " Thu, 29 Feb 2025 04:24:13 GMT",
        " Mon, 31 Apr 2026 04:24:13 GMT", " Wed, 25 Nov 2019 04:24:13 GMT", " Fri, 01 Jan 2100 00:00:00 GMT", " Wed, 25 Nov 26 04:24:13 GMT",
        " Wed, 25 Nov 2026 04:24:13", " Wed, 25 Nov 2026 04:24:13 GM", " Wed 25 Nov 2026 04:24:13 GMT", " Wed, 25 Nov 2026 4:24:13 GMT",
        " Wed, 25 Nov 2026 04-24-13 GMT", "", " ", "Wed,", " 25 Nov 2026 04:24:13 GMT", " We, 25 Nov 2026 04:24:13 GMT", " Wed, ab Nov 2026 04:24:13 GMT"
    };
    size_t k;

    TEST("H-X2 Date: → 通算秒(うるう年・月末・年末・範囲外の拒否)");
    CHECK(wx_parse_date(" Wed, 25 Nov 2026 04:24:13 GMT", 30u, &u)); CHECK_EQ(u, 1795580653u);
    CHECK(wx_parse_date("Wed, 25 Nov 2026 04:24:13 GMT", 29u, &u));  CHECK_EQ(u, 1795580653u);     /* 先頭の空白なし */
    CHECK(wx_parse_date(" Thu, 29 Feb 2024 12:00:00 GMT", 30u, &u)); CHECK_EQ(u, 1709208000u);      /* うるう日 */
    CHECK(wx_parse_date(" Thu, 31 Dec 2026 23:59:59 GMT", 30u, &u)); CHECK_EQ(u, 1798761599u);
    wx_date_fields(u + 32400u, &d);                                                                  /* JST で翌年の 1 月 1 日 08:59:59 */
    CHECK_EQ(d.year, 2027); CHECK_EQ(d.month, 1); CHECK_EQ(d.day, 1); CHECK_EQ(d.hour, 8); CHECK_EQ(d.min, 59); CHECK_EQ(d.sec, 59); CHECK_EQ(d.wday, 5);
    CHECK(wx_parse_date(" Thu, 31 Dec 2026 15:00:00 GMT", 30u, &u)); CHECK_EQ(u, 1798729200u);
    wx_date_fields(u + 32400u, &d);                                                                  /* UTC 15:00 = JST 翌日 00:00:00 */
    CHECK_EQ(d.year, 2027); CHECK_EQ(d.month, 1); CHECK_EQ(d.day, 1); CHECK_EQ(d.hour, 0); CHECK_EQ(d.min, 0);
    CHECK(wx_parse_date(" Thu, 31 Dec 2026 14:59:59 GMT", 30u, &u));
    wx_date_fields(u + 32400u, &d); CHECK_EQ(d.year, 2026); CHECK_EQ(d.month, 12); CHECK_EQ(d.day, 31); CHECK_EQ(d.hour, 23); CHECK_EQ(d.sec, 59);
    CHECK(wx_parse_date(" Thu, 31 Dec 2099 23:59:59 GMT", 30u, &u)); CHECK_EQ(u, 4102444799u);
    CHECK(wx_parse_date(" Wed, 01 Jan 2020 00:00:00 GMT", 30u, &u)); CHECK_EQ(u, 1577836800u);
    CHECK(wx_parse_date(" Wed, 1 Jan 2020 00:00:00 GMT", 29u, &u));  CHECK_EQ(u, 1577836800u);      /* 日が 1 桁 */
    for (k = 0u; k < (sizeof REJ / sizeof REJ[0]); k++) {
        u = 77u;
        if (wx_parse_date(REJ[k], strlen(REJ[k]), &u)) { printf("  受理してしまった: '%s'\n", REJ[k]); }
        CHECK(!wx_parse_date(REJ[k], strlen(REJ[k]), &u));
    }
    CHECK(!wx_parse_date(NULL, 5u, &u));
    CHECK(!wx_parse_date(" Wed, 25 Nov 2026 04:24:13 GMT", 29u, &u));                               /* len で止まる(GMT の T が欠ける) */
    /* 曜日・うるう年の規則(wx_date_fields) */
    wx_date_fields(0u, &d);           CHECK_EQ(d.year, 1970); CHECK_EQ(d.month, 1); CHECK_EQ(d.day, 1); CHECK_EQ(d.wday, 4);
    wx_date_fields(946684800u, &d);   CHECK_EQ(d.year, 2000); CHECK_EQ(d.month, 1); CHECK_EQ(d.day, 1); CHECK_EQ(d.wday, 6);
    wx_date_fields(1795580653u + 32400u, &d); CHECK_EQ(d.wday, 3);
    wx_date_fields(4107542400u - 1u, &d);     CHECK_EQ(d.year, 2100); CHECK_EQ(d.month, 2); CHECK_EQ(d.day, 28);    /* 2100 はうるう年でない */
    wx_date_fields(4107542400u, &d);          CHECK_EQ(d.year, 2100); CHECK_EQ(d.month, 3); CHECK_EQ(d.day, 1); CHECK_EQ(d.wday, 1);
    wx_date_fields(951782400u, &d);           CHECK_EQ(d.year, 2000); CHECK_EQ(d.month, 2); CHECK_EQ(d.day, 29);    /* 2000 はうるう年 */
    /* ホストの gmtime との全域の突き合わせ: 2020〜2099 を約 1 万 7 千秒刻みで、フィールドと Date: の往復が一致 */
    for (i = 0; i < 4000000; i += 1) {
        secs = 1577836800u + (uint32_t)i * 17341u;
        if (secs > 4102444799u) { break; }
        tt = (time_t)secs;
        g  = gmtime(&tt);
        wx_date_fields(secs, &d);
        if (g == NULL || d.year != (uint16_t)(g->tm_year + 1900) || d.month != (uint8_t)(g->tm_mon + 1) || d.day != (uint8_t)g->tm_mday
            || d.wday != (uint8_t)g->tm_wday || d.hour != (uint8_t)g->tm_hour || d.min != (uint8_t)g->tm_min || d.sec != (uint8_t)g->tm_sec) {
            bad++;
        } else {
            (void)snprintf(b, sizeof b, " %s, %02u %s %04u %02u:%02u:%02u GMT", WD[d.wday], (unsigned)d.day, MN[d.month - 1u], (unsigned)d.year,
                           (unsigned)d.hour, (unsigned)d.min, (unsigned)d.sec);
            if (!wx_parse_date(b, strlen(b), &u) || (u != secs)) { bad++; }
        }
    }
    CHECK_EQ(bad, 0);
}

/* ---------------------------------------------------------------- H-X4 WMO */

static void t_kind(void)
{
    uint32_t w;
    uint8_t  want;
    int      bad = 0;

    TEST("H-X4 WMO コード → アイコン(0〜99 の全部と範囲外)");
    for (w = 0u; w < 100u; w++) {
        want = (uint8_t)WX_KIND_UNKNOWN;
        if ((w == 0u) || (w == 1u)) { want = (uint8_t)WX_KIND_SUN; }
        else if (w == 2u) { want = (uint8_t)WX_KIND_PARTLY; }
        else if (w == 3u) { want = (uint8_t)WX_KIND_CLOUD; }
        else if ((w == 45u) || (w == 48u)) { want = (uint8_t)WX_KIND_FOG; }
        else if ((w == 51u) || (w == 53u) || (w == 55u) || (w == 56u) || (w == 57u) || (w == 61u) || (w == 63u) || (w == 65u) || (w == 66u) || (w == 67u)
                 || (w == 80u) || (w == 81u) || (w == 82u)) { want = (uint8_t)WX_KIND_RAIN; }
        else if ((w == 71u) || (w == 73u) || (w == 75u) || (w == 77u) || (w == 85u) || (w == 86u)) { want = (uint8_t)WX_KIND_SNOW; }
        else if ((w == 95u) || (w == 96u) || (w == 99u)) { want = (uint8_t)WX_KIND_THUNDER; }
        if (wx_kind_of(w) != want) { printf("  wmo %u -> %u (want %u)\n", (unsigned)w, (unsigned)wx_kind_of(w), (unsigned)want); bad++; }
    }
    CHECK_EQ(bad, 0);
    CHECK_EQ(wx_kind_of(100u), WX_KIND_UNKNOWN); CHECK_EQ(wx_kind_of(255u), WX_KIND_UNKNOWN); CHECK_EQ(wx_kind_of(0xFFFFFFFFu), WX_KIND_UNKNOWN);
    CHECK_EQ(wx_kind_of(4u), WX_KIND_UNKNOWN); CHECK_EQ(wx_kind_of(46u), WX_KIND_UNKNOWN); CHECK_EQ(wx_kind_of(60u), WX_KIND_UNKNOWN);
    CHECK(strcmp(wx_kind_name(WX_KIND_SUN), "sun") == 0); CHECK(strcmp(wx_kind_name(WX_KIND_PARTLY), "partly") == 0);
    CHECK(strcmp(wx_kind_name(WX_KIND_CLOUD), "cloud") == 0); CHECK(strcmp(wx_kind_name(WX_KIND_FOG), "fog") == 0);
    CHECK(strcmp(wx_kind_name(WX_KIND_RAIN), "rain") == 0); CHECK(strcmp(wx_kind_name(WX_KIND_SNOW), "snow") == 0);
    CHECK(strcmp(wx_kind_name(WX_KIND_THUNDER), "thunder") == 0); CHECK(strcmp(wx_kind_name(WX_KIND_UNKNOWN), "unknown") == 0);
    CHECK(wx_kind_name(WX_KIND_END) == NULL); CHECK(wx_kind_name(255) == NULL);
}

/* ---------------------------------------------------------------- H-X3 ソフト時計 */

static void t_clock(void)
{
    wx_clock_t k;
    uint32_t   s = 0u;
    wx_date_t  d;
    uint32_t   jst = 1795580653u + 32400u;                      /* 2026-11-25 13:24:13 JST */

    TEST("H-X3 ソフト時計");
    wx_clock_init(&k);
    CHECK(!wx_clock_now(&k, 12345u, &s));
    CHECK_EQ(wx_clock_min(&k, 12345u), 0xFFFFu);
    wx_clock_sync(&k, jst, 100000u);                            /* 最初のデータ = 100000ms → Date: の秒の頭 = 99300ms */
    CHECK(wx_clock_now(&k, 100000u, &s)); CHECK_EQ(s, jst);                                  /* 700ms 後でもまだ同じ秒 */
    CHECK(wx_clock_now(&k, 99300u + 999u, &s)); CHECK_EQ(s, jst);
    CHECK(wx_clock_now(&k, 99300u + 1000u, &s)); CHECK_EQ(s, jst + 1u);
    CHECK(wx_clock_now(&k, 99300u + 60000u, &s)); CHECK_EQ(s, jst + 60u);
    CHECK_EQ(wx_clock_min(&k, 100000u), (13u * 60u) + 24u);
    CHECK_EQ(wx_clock_min(&k, 99300u + 47000u), (13u * 60u) + 25u);                         /* 13:24:13 + 47s = 13:25:00 */
    /* 再同期で基準が置き換わる */
    wx_clock_sync(&k, jst + 600u, 700000u);
    CHECK(wx_clock_now(&k, 700000u, &s)); CHECK_EQ(s, jst + 600u);
    /* ms が 32bit で巻き戻っても差が正しい */
    wx_clock_sync(&k, jst, 0xFFFFFF00u);                        /* 基準 = 0xFFFFFF00 - 700 */
    CHECK(wx_clock_now(&k, 0xFFFFFF00u, &s)); CHECK_EQ(s, jst);
    CHECK(wx_clock_now(&k, 0xFFFFFF00u + 299u, &s)); CHECK_EQ(s, jst);
    CHECK(wx_clock_now(&k, 0xFFFFFF00u + 300u, &s)); CHECK_EQ(s, jst + 1u);                 /* 0xFFFFFF00 + 300 は巻き戻って小さい値 */
    CHECK(0xFFFFFF00u + 300u < 0xFFFFFF00u);
    CHECK(wx_clock_now(&k, 0xFFFFFF00u + 3599300u, &s)); CHECK_EQ(s, jst + 3600u);
    /* 最初のデータが 700ms より前(ms が小さい)でも、巻き戻りで正しい */
    wx_clock_sync(&k, jst, 100u);
    CHECK(wx_clock_now(&k, 100u + 299u, &s)); CHECK_EQ(s, jst);
    CHECK(wx_clock_now(&k, 100u + 300u, &s)); CHECK_EQ(s, jst + 1u);
    /* 日をまたぐ: 2026-12-31 23:59:58 JST に合わせると、1 秒後 23:59:59、3 秒後は 2027-01-01 00:00:01(金) */
    wx_clock_sync(&k, 1798729200u - 2u + 32400u, 5000u);
    CHECK(wx_clock_now(&k, 4300u + 1000u, &s)); wx_date_fields(s, &d);
    CHECK_EQ(d.year, 2026); CHECK_EQ(d.month, 12); CHECK_EQ(d.day, 31); CHECK_EQ(d.hour, 23); CHECK_EQ(d.min, 59); CHECK_EQ(d.sec, 59);
    CHECK(wx_clock_now(&k, 4300u + 3000u, &s)); wx_date_fields(s, &d);
    CHECK_EQ(d.year, 2027); CHECK_EQ(d.month, 1); CHECK_EQ(d.day, 1); CHECK_EQ(d.wday, 5); CHECK_EQ(d.hour, 0); CHECK_EQ(d.sec, 1);
    CHECK_EQ(WX_SYNC_BIAS_MS, 700u);
}

/* ---------------------------------------------------------------- ログ行 */

static int tok(const char *line, const char *key, char *out, size_t sz)    /* "key=値" の値を out へ(空白か \r まで)。無ければ 0 */
{
    char        pat[32];
    const char *p;
    size_t      n = 0u;

    (void)snprintf(pat, sizeof pat, " %s=", key);
    p = strstr(line, pat);
    if (p == NULL) { return 0; }
    p += strlen(pat);
    while ((*p != ' ') && (*p != '\r') && (*p != '\0') && (n + 1u < sz)) { out[n++] = *p++; }
    out[n] = '\0';
    return 1;
}

static int is_num(const char *s, int maxd, int neg)
{
    int n = 0;

    if (neg && (*s == '-')) { s++; }
    while ((*s >= '0') && (*s <= '9')) { s++; n++; }
    return (n >= 1) && (n <= maxd) && (*s == '\0');
}

static int in_set(const char *s, const char *const *set, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) { if (strcmp(s, set[i]) == 0) { return 1; } }
    return 0;
}

static const char *const SRC4[] = { "boot", "timer", "touch", "uart" };
static const char *const KIND8[] = { "sun", "partly", "cloud", "fog", "rain", "snow", "thunder", "unknown" };
static const char *const KIND9[] = { "sun", "partly", "cloud", "fog", "rain", "snow", "thunder", "unknown", "none" };
static const char *const NET4[] = { "off", "conn", "on", "err" };

/* R-WX-OK に相当 */
static int fm_ok(const char *l)
{
    char v[32];
    size_t n = strlen(l);

    if ((n < 8u) || (strncmp(l, "[wx] res=ok src=", 16u) != 0) || (n < 2u) || (strcmp(l + n - 2u, "\r\n") != 0) || (strchr(l, '\n') != l + n - 1u)) { return 0; }
    if (!tok(l, "src", v, sizeof v) || !in_set(v, SRC4, 4u)) { return 0; }
    if (!tok(l, "temp10", v, sizeof v) || !is_num(v, 3, 1)) { return 0; }
    if (!tok(l, "wmo", v, sizeof v) || !is_num(v, 2, 0)) { return 0; }
    if (!tok(l, "kind", v, sizeof v) || !in_set(v, KIND8, 8u)) { return 0; }
    if (!tok(l, "http", v, sizeof v) || !is_num(v, 3, 0) || (strlen(v) != 3u)) { return 0; }
    if (!tok(l, "ms", v, sizeof v) || !is_num(v, 10, 0)) { return 0; }
    if (!tok(l, "sync", v, sizeof v) || !((strcmp(v, "0") == 0) || (strcmp(v, "1") == 0))) { return 0; }
    if (!tok(l, "jst", v, sizeof v) || !((strcmp(v, "none") == 0) || (is_num(v, 6, 0) && (strlen(v) == 6u)))) { return 0; }
    if (!tok(l, "obs", v, sizeof v) || !is_num(v, 4, 0) || (strlen(v) != 4u)) { return 0; }
    if (!tok(l, "closed", v, sizeof v) || !((strcmp(v, "0") == 0) || (strcmp(v, "1") == 0))) { return 0; }
    return 1;
}

static int fm_fail(const char *l)
{
    char v[32];
    size_t n = strlen(l);

    if ((strncmp(l, "[wx] res=fail src=", 18u) != 0) || (n < 2u) || (strcmp(l + n - 2u, "\r\n") != 0)) { return 0; }
    if (!tok(l, "src", v, sizeof v) || !in_set(v, SRC4, 4u)) { return 0; }
    if (!tok(l, "err", v, sizeof v) || !is_num(v, 1, 0) || (v[0] < '1') || (v[0] > '9')) { return 0; }
    if (!tok(l, "http", v, sizeof v) || !is_num(v, 3, 0)) { return 0; }
    if (!tok(l, "ms", v, sizeof v) || !is_num(v, 10, 0)) { return 0; }
    if (!tok(l, "len", v, sizeof v) || !is_num(v, 5, 0)) { return 0; }
    return 1;
}

static int fm_state(const char *l)
{
    char v[32];
    size_t n = strlen(l);

    if ((strncmp(l, "[wx] net=", 9u) != 0) || (n < 2u) || (strcmp(l + n - 2u, "\r\n") != 0)) { return 0; }
    if ((strncmp(l + 5, "net=", 4u) != 0)) { return 0; }
    {
        char net[8]; size_t i = 0u; const char *p = l + 9;
        while ((*p != ' ') && (i < 7u)) { net[i++] = *p++; }
        net[i] = '\0';
        if (!in_set(net, NET4, 4u)) { return 0; }
    }
    if (!tok(l, "busy", v, sizeof v) || !((strcmp(v, "0") == 0) || (strcmp(v, "1") == 0))) { return 0; }
    if (!tok(l, "have", v, sizeof v) || !((strcmp(v, "0") == 0) || (strcmp(v, "1") == 0))) { return 0; }
    if (!tok(l, "temp10", v, sizeof v) || !is_num(v, 3, 1)) { return 0; }
    if (!tok(l, "kind", v, sizeof v) || !in_set(v, KIND9, 9u)) { return 0; }
    if (!tok(l, "age", v, sizeof v) || !((strcmp(v, "-1") == 0) || is_num(v, 10, 0))) { return 0; }
    if (!tok(l, "clk", v, sizeof v) || !((strcmp(v, "none") == 0) || (is_num(v, 6, 0) && (strlen(v) == 6u)))) { return 0; }
    if (!tok(l, "date", v, sizeof v) || !((strcmp(v, "none") == 0) || (is_num(v, 8, 0) && (strlen(v) == 8u)))) { return 0; }
    if (!tok(l, "wd", v, sizeof v) || !is_num(v, 1, 0) || (v[0] == '7') || (v[0] == '8')) { return 0; }
    if (!tok(l, "sync", v, sizeof v) || !((strcmp(v, "0") == 0) || (strcmp(v, "1") == 0))) { return 0; }
    return 1;
}

static void t_fmt(void)
{
    char        b[300];
    char        g[300];
    wx_result_t r;
    wx_view_t   v;
    size_t      n, k, maxlen = 0u;
    int         s, kd;
    static const int T10[] = { -600, -34, 0, 127, 600 };
    static const uint32_t MS[] = { 0u, 1234u, 0xFFFFFFFFu };

    TEST("H-X6/H-X(書式) wx_fmt_*: R-WX-* に適合、境界値、小バッファ、引数不正");
    memset(&r, 0, sizeof r);
    r.ok = 1; r.http = 200; r.have_date = 1; r.jst_s = 1795580653u + 32400u; r.obs = 1330; r.wmo = 0; r.kind = WX_KIND_SUN; r.temp_x10 = 127; r.ms = 1234u;
    n = wx_fmt_ok(b, sizeof b, WX_SRC_UART, &r);
    CHECK(n > 0u);
    CHECK(strcmp(b, "[wx] res=ok src=uart temp10=127 wmo=0 kind=sun http=200 ms=1234 sync=1 jst=132413 obs=1330 closed=0\r\n") == 0);
    for (s = 0; s < (int)WX_SRC_END; s++) {
        for (kd = 0; kd < (int)WX_KIND_END; kd++) {
            for (k = 0u; k < 5u; k++) {
                r.kind = (uint8_t)kd; r.temp_x10 = (int16_t)T10[k]; r.ms = MS[k % 3u]; r.closed = (uint8_t)(k & 1u); r.have_date = (uint8_t)(k != 2u); r.wmo = (uint8_t)(kd * 11);
                n = wx_fmt_ok(b, sizeof b, (wx_src_t)s, &r);
                CHECK(n > 0u); CHECK_EQ(strlen(b), n); CHECK(fm_ok(b)); CHECK(n < 190u);
                if (n > maxlen) { maxlen = n; }
            }
        }
    }
    CHECK(maxlen < 255u);
    /* sync=0 は jst=none */
    r.have_date = 0; r.temp_x10 = -34; r.kind = WX_KIND_RAIN; r.wmo = 61;
    n = wx_fmt_ok(b, sizeof b, WX_SRC_BOOT, &r);
    CHECK(strstr(b, "temp10=-34 wmo=61 kind=rain http=200 ms=4294967295 sync=0 jst=none obs=1330 closed=1") != NULL || strstr(b, "sync=0 jst=none") != NULL);
    /* 範囲外・引数不正は 0 で buf[0] = 0 */
    r.have_date = 1; r.temp_x10 = 601; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_ok(b, sizeof b, WX_SRC_UART, &r), 0u); CHECK_EQ(b[0], 0);
    r.temp_x10 = -601; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_ok(b, sizeof b, WX_SRC_UART, &r), 0u); CHECK_EQ(b[0], 0);
    r.temp_x10 = 0; r.kind = WX_KIND_END; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_ok(b, sizeof b, WX_SRC_UART, &r), 0u); CHECK_EQ(b[0], 0);
    r.kind = WX_KIND_SUN; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_ok(b, sizeof b, WX_SRC_END, &r), 0u); CHECK_EQ(b[0], 0);
    CHECK_EQ(wx_fmt_ok(b, sizeof b, WX_SRC_UART, NULL), 0u);
    CHECK_EQ(wx_fmt_ok(NULL, 10u, WX_SRC_UART, &r), 0u);
    CHECK_EQ(wx_fmt_ok(b, 0u, WX_SRC_UART, &r), 0u);
    /* 大きさの境界 */
    n = wx_fmt_ok(b, sizeof b, WX_SRC_UART, &r);
    for (k = 1u; k <= (n + 1u); k++) {
        memset(g, 0x5A, sizeof g);
        s = (int)wx_fmt_ok(g, k, WX_SRC_UART, &r);
        if (k <= n) { CHECK_EQ(s, 0); CHECK_EQ(g[0], 0); } else { CHECK_EQ((size_t)s, n); }
        CHECK_EQ((unsigned char)g[k], 0x5Au);
    }

    /* 失敗 */
    memset(&r, 0, sizeof r);
    r.err = 4; r.http = 429; r.ms = 5000u;
    n = wx_fmt_fail(b, sizeof b, WX_SRC_TIMER, &r);
    CHECK(strcmp(b, "[wx] res=fail src=timer err=4 http=429 ms=5000 len=0\r\n") == 0);
    r.err = 9; r.http = 0; r.ms = 12345u; r.len = 65535u;                /* err=9(ESP32 の再起動)と len(Phase 0。design.md §5.5.2 の 0-5) */
    n = wx_fmt_fail(b, sizeof b, WX_SRC_UART, &r);
    CHECK(strcmp(b, "[wx] res=fail src=uart err=9 http=0 ms=12345 len=65535\r\n") == 0);
    r.err = 5; r.http = 200; r.ms = 4000u; r.len = 1840u;
    n = wx_fmt_fail(b, sizeof b, WX_SRC_UART, &r);
    CHECK(strcmp(b, "[wx] res=fail src=uart err=5 http=200 ms=4000 len=1840\r\n") == 0);
    r.len = 0u;
    for (s = 0; s < (int)WX_SRC_END; s++) {
        for (k = 1u; k <= 9u; k++) {
            r.err = (uint8_t)k; r.http = (uint16_t)(k * 100u); r.ms = MS[k % 3u];
            n = wx_fmt_fail(b, sizeof b, (wx_src_t)s, &r);
            CHECK(n > 0u); CHECK(fm_fail(b)); CHECK_EQ(strlen(b), n);
        }
    }
    r.err = 0; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_fail(b, sizeof b, WX_SRC_UART, &r), 0u); CHECK_EQ(b[0], 0);
    r.err = 10; CHECK_EQ(wx_fmt_fail(b, sizeof b, WX_SRC_UART, &r), 0u);
    r.err = 3; r.http = 1000; CHECK_EQ(wx_fmt_fail(b, sizeof b, WX_SRC_UART, &r), 0u);

    /* 要求の無視 */
    n = wx_fmt_req(b, sizeof b, WX_REQ_IGNORED, WX_SRC_UART);
    CHECK(strcmp(b, "[wx] req=ignored src=uart\r\n") == 0);
    CHECK(strcmp((wx_fmt_req(b, sizeof b, WX_REQ_BUSY, WX_SRC_TOUCH), b), "[wx] req=busy src=touch\r\n") == 0);
    CHECK(strcmp((wx_fmt_req(b, sizeof b, WX_REQ_OFF, WX_SRC_UART), b), "[wx] req=off src=uart\r\n") == 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_req(b, sizeof b, WX_REQ_OK, WX_SRC_UART), 0u); CHECK_EQ(b[0], 0);
    CHECK_EQ(wx_fmt_req(b, sizeof b, WX_REQ_BUSY, WX_SRC_TIMER), 0u);
    CHECK_EQ(wx_fmt_req(b, sizeof b, WX_REQ_BUSY, WX_SRC_BOOT), 0u);

    /* 状態行 */
    memset(&v, 0, sizeof v);
    v.net = WX_NET_OFF; v.have = 0; v.age_s = -1; v.synced = 0;
    n = wx_fmt_state(b, sizeof b, &v);
    CHECK(strcmp(b, "[wx] net=off busy=0 have=0 temp10=0 kind=none age=-1 clk=none date=none wd=9 sync=0\r\n") == 0);
    v.net = WX_NET_ON; v.busy = 1; v.have = 1; v.kind = WX_KIND_RAIN; v.temp_x10 = -34; v.age_s = 65; v.synced = 1; v.jst_s = 1795580653u + 32400u;
    n = wx_fmt_state(b, sizeof b, &v);
    CHECK(strcmp(b, "[wx] net=on busy=1 have=1 temp10=-34 kind=rain age=65 clk=132413 date=20261125 wd=3 sync=1\r\n") == 0);
    for (s = 0; s < 4; s++) {
        for (kd = 0; kd < 8; kd++) {
            for (k = 0u; k < 5u; k++) {
                v.net = (uint8_t)s; v.kind = (uint8_t)kd; v.temp_x10 = (int16_t)T10[k]; v.age_s = (k == 0u) ? -1 : (int32_t)(k * 1000u); v.synced = (uint8_t)(k & 1u);
                v.jst_s = 1795580653u + 32400u + (uint32_t)(k * 86400u); v.have = (uint8_t)(k != 0u);
                n = wx_fmt_state(b, sizeof b, &v);
                CHECK(n > 0u); CHECK(fm_state(b)); CHECK_EQ(strlen(b), n); CHECK(n < 200u);
            }
        }
    }
    v.net = 4; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_state(b, sizeof b, &v), 0u); CHECK_EQ(b[0], 0);
    CHECK_EQ(wx_fmt_state(b, sizeof b, NULL), 0u);
    v.net = 1; v.have = 1; v.kind = WX_KIND_END; CHECK_EQ(wx_fmt_state(b, sizeof b, &v), 0u);
    v.kind = 0; v.temp_x10 = 700; CHECK_EQ(wx_fmt_state(b, sizeof b, &v), 0u);
}

/* ---------------------------------------------------------------- H-X7 予定表 */

static uint32_t g_t;
static wx_sched_t g_s;

static wx_act_t pollat(uint32_t t) { g_t = t; return wx_sched_poll(&g_s, t); }

static void t_sched(void)
{
    uint32_t t0 = 1000u;
    uint32_t tf;
    int      i;

    TEST("H-X7 予定表: OFF");
    wx_sched_init(&g_s, false, t0);
    CHECK_EQ(pollat(t0 + 100000u), WX_ACT_NONE);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, t0 + 100000u), WX_REQ_OFF);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_OFF); CHECK(!wx_sched_busy(&g_s));

    TEST("H-X7 予定表: 起動 3 秒後に接続 → 最初の取得 → 10 分後の取得");
    wx_sched_init(&g_s, true, t0);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_CONN); CHECK(!wx_sched_busy(&g_s));
    CHECK_EQ(pollat(t0 + 2999u), WX_ACT_NONE);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, t0 + 2999u), WX_REQ_BUSY);                  /* 起動待ちは busy */
    CHECK_EQ(pollat(t0 + 3000u), WX_ACT_CONNECT);
    CHECK(wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_net(&g_s), WX_NET_CONN);
    CHECK_EQ(pollat(t0 + 3010u), WX_ACT_NONE);                                                /* 二重に開始しない */
    CHECK_EQ(pollat(t0 + 3020u), WX_ACT_NONE);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, t0 + 3020u), WX_REQ_BUSY);
    wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, t0 + 10000u);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    CHECK_EQ(pollat(t0 + 10000u), WX_ACT_FETCH);                                              /* 直ちに最初の取得 */
    CHECK_EQ(wx_sched_src(&g_s), WX_SRC_BOOT);
    tf = t0 + 10000u;
    CHECK_EQ(pollat(tf + 10u), WX_ACT_NONE);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, tf + 20u), WX_REQ_BUSY);                    /* 取得中 */
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, tf + 1500u);
    CHECK(!wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    CHECK_EQ(pollat(tf + 599999u), WX_ACT_NONE);
    CHECK_EQ(pollat(tf + 600000u), WX_ACT_FETCH);                                             /* 取得開始から 10 分 */
    CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TIMER);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, tf + 601500u);
    CHECK_EQ(pollat(tf + 600000u + 599999u), WX_ACT_NONE);
    CHECK_EQ(pollat(tf + 1200000u), WX_ACT_FETCH);

    TEST("H-X7 予定表: 更新要求(IDLE で即時、5 秒未満は無視、取得中は busy)");
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, tf + 1201000u);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, tf + 1200000u + 4999u), WX_REQ_IGNORED);
    CHECK_EQ(pollat(tf + 1200000u + 5000u), WX_ACT_NONE);                                    /* 無視した要求は動かさない */
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, tf + 1200000u + 5000u), WX_REQ_OK);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, tf + 1200000u + 5001u), WX_REQ_BUSY);        /* 連打: 二重に開始しない */
    CHECK_EQ(pollat(tf + 1200000u + 5010u), WX_ACT_FETCH);
    CHECK_EQ(wx_sched_src(&g_s), WX_SRC_UART);
    CHECK_EQ(pollat(tf + 1200000u + 5020u), WX_ACT_NONE);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, tf + 1200000u + 6000u);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, tf + 1200000u + 5010u + 4999u), WX_REQ_IGNORED);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, tf + 1200000u + 5010u + 5000u), WX_REQ_OK);
    CHECK_EQ(pollat(tf + 1200000u + 10020u), WX_ACT_FETCH); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TOUCH);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, tf + 1200000u + 11000u);
    /* 手動の取得の後の定期取得は、その取得開始から 10 分 */
    CHECK_EQ(pollat(tf + 1200000u + 10020u + 599999u), WX_ACT_NONE);
    CHECK_EQ(pollat(tf + 1200000u + 10020u + 600000u), WX_ACT_FETCH);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, t0);

    TEST("H-X7 予定表: 取得の失敗 → 20 秒後に 2 回やり直し → 再接続。429 は 300 秒");
    wx_sched_init(&g_s, true, t0);
    CHECK_EQ(pollat(t0 + 3000u), WX_ACT_CONNECT); wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, t0 + 9000u);
    CHECK_EQ(pollat(t0 + 9000u), WX_ACT_FETCH);
    wx_sched_done(&g_s, WX_ACT_FETCH, false, 0u, t0 + 11000u);                               /* 1 回目の失敗 */
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ERR); CHECK(!wx_sched_busy(&g_s));
    CHECK_EQ(pollat(t0 + 11000u + 19999u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + 11000u + 20000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TIMER);
    wx_sched_done(&g_s, WX_ACT_FETCH, false, 500u, t0 + 33000u);                             /* 2 回目 */
    CHECK_EQ(pollat(t0 + 33000u + 19999u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + 33000u + 20000u), WX_ACT_FETCH);
    wx_sched_done(&g_s, WX_ACT_FETCH, false, 0u, t0 + 55000u);                               /* 3 回目 → 再接続 */
    CHECK_EQ(pollat(t0 + 55000u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, t0 + 62000u);
    CHECK_EQ(pollat(t0 + 62000u), WX_ACT_FETCH);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, t0 + 63000u);                              /* 成功で連続の失敗が戻る */
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, t0 + 70000u), WX_REQ_OK);
    CHECK_EQ(pollat(t0 + 70000u), WX_ACT_FETCH);
    wx_sched_done(&g_s, WX_ACT_FETCH, false, 429u, t0 + 71000u);                             /* 429: 300 秒 */
    CHECK_EQ(pollat(t0 + 71000u + 299999u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + 71000u + 300000u), WX_ACT_FETCH);
    wx_sched_done(&g_s, WX_ACT_FETCH, false, 429u, t0 + 372000u);                            /* 429 が続いても再接続しない */
    wx_sched_done(&g_s, WX_ACT_FETCH, false, 429u, t0 + 372000u);                            /* 始めていない動きの完了は無視 */
    CHECK_EQ(pollat(t0 + 372000u + 300000u), WX_ACT_FETCH);
    wx_sched_done(&g_s, WX_ACT_FETCH, false, 429u, t0 + 700000u);
    CHECK_EQ(pollat(t0 + 700000u + 299999u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + 700000u + 300000u), WX_ACT_FETCH);                                  /* 3 回続いても CONNECT でない */

    TEST("H-X7 予定表: 接続の失敗 60 / 120 / 300 / 300 秒、エラー中の更新要求は直ちに再接続");
    wx_sched_init(&g_s, true, t0);
    CHECK_EQ(pollat(t0 + 3000u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, t0 + 20000u);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ERR); CHECK(!wx_sched_busy(&g_s));
    CHECK_EQ(pollat(t0 + 20000u + 59999u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + 20000u + 60000u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, t0 + 90000u);
    CHECK_EQ(pollat(t0 + 90000u + 119999u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + 90000u + 120000u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, t0 + 300000u);
    CHECK_EQ(pollat(t0 + 300000u + 299999u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + 300000u + 300000u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, t0 + 700000u);
    CHECK_EQ(pollat(t0 + 700000u + 299999u), WX_ACT_NONE);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, t0 + 700000u + 1000u), WX_REQ_OK);          /* エラー中 → 直ちに再接続 */
    CHECK_EQ(pollat(t0 + 701001u), WX_ACT_CONNECT);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_CONN);
    wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, t0 + 708000u);
    CHECK_EQ(pollat(t0 + 708000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TOUCH);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, t0 + 709000u);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    /* 接続に成功すれば、次に失敗した時の待ちは 60 秒に戻る */
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, t0 + 730000u), WX_REQ_OK);
    CHECK_EQ(pollat(t0 + 730000u), WX_ACT_FETCH);
    for (i = 0; i < 3; i++) { wx_sched_done(&g_s, WX_ACT_FETCH, false, 0u, t0 + 731000u); (void)pollat(t0 + 731000u + 20000u); }
    CHECK_EQ(pollat(t0 + 731000u + 20001u), WX_ACT_NONE);        /* 3 回目の失敗の直後に CONNECT を返し済み(上の poll) */
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, t0 + 760000u);
    CHECK_EQ(pollat(t0 + 760000u + 59999u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + 760000u + 60000u), WX_ACT_CONNECT);

    TEST("H-X7 予定表: now が 32bit で巻き戻っても動く");
    t0 = 0xFFFFF000u;
    wx_sched_init(&g_s, true, t0);
    CHECK_EQ(pollat(t0 + 2999u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + 3000u), WX_ACT_CONNECT);                                             /* t0 + 3000 は巻き戻って小さい値 */
    wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, t0 + 4000u);
    CHECK_EQ(pollat(t0 + 4000u), WX_ACT_FETCH);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, t0 + 5000u);
    CHECK_EQ(pollat(t0 + 4000u + 599999u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + 4000u + 600000u), WX_ACT_FETCH);
    wx_sched_done(&g_s, WX_ACT_FETCH, false, 0u, t0 + 700000u);
    CHECK_EQ(pollat(t0 + 700000u + 19999u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + 700000u + 20000u), WX_ACT_FETCH);
}

int main(void)
{
    t_request();
    t_target();
    t_parse_real();
    t_parse_values();
    t_parse_units();
    t_parse_status();
    t_parse_date_header();
    t_parse_truncate();
    t_date();
    t_kind();
    t_clock();
    t_fmt();
    t_sched();
    TEST_REPORT("test_wx_core");
}
