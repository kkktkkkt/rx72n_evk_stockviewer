/* test_wifi_at.c - src/app/wifi_at.c(SCI1・EN・専用モードのメインループ・UART コマンド・[hb])を、偽物の周辺で動かす結合試験。
 *   - 実 platform.h と FIT の実ヘッダを使い、wifi_at.c をそのまま #include する(S10/S11 と同じ流儀)。wifi_core.c / app_logfmt.c は本物をリンクする。
 *   - r_sci_rx は tests/stubs/fake_sci.h(SCI1 専用に使う)、EN(R_GPIO_*)・CMT・app_log_*(SCI2 のログ)は、この .c の偽物。
 *   - wifi_test_main() の無限ループは、ループ先頭の app_log_poll() をマクロで差し替えたフック(loop_hook)から longjmp で抜ける。
 *     フックが「10ms の tick」「送信完了」「偽の ESP32 の応答」「UART の受信文字」「SCI のエラーコールバック」を模擬する。
 *   - SSID・パスワードは実物(wifi_secret.h)を読まない(WIFI_AT_HOST_TEST)。この .c が目印の文字列を WIFI_SSID / WIFI_PASS に定義する。
 *   - 試験できるのは論理(R_SCI_Open の引数、EN の順序、受信の上限、送信 BUSY の再送、行の出力、コマンド、[err])。
 *     実機の割り込み・実時間・ESP32 の本当の応答・基板の配線は試験できない。
 *   ビルドの種類: 既定 / -DT_NOSECRET(秘密なし。join を skip)/ -DAPP_WIFI_AUTOSTART=1(以上は APP_WIFI_TEST = 1 の専用モード)、
 *   -DAPP_WX=1(製品。wx_init / wx_poll をメインループの代わりの product_main から呼ぶ。-DT_NOSECRET で OFF)*/
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <setjmp.h>
#include "test_framework.h"

#define WIFI_SSID_T "SSID_SENTINEL_7c1"        /* 偽 ESP の scan 一覧に載せる自分の AP(T_NOSECRET でも同じ) */
#ifndef T_NOSECRET
#define WIFI_SSID  WIFI_SSID_T
#define WIFI_PASS  "PW_SENTINEL_9f3"
#endif

#include "platform.h"
#include "r_cmt_rx_if.h"
#include "r_gpio_rx_if.h"
#include "../../src/app/app_debug.h"
#include "../../src/app/app_logfmt.h"
#include "../stubs/fake_sci.h"

/* ビルドの種類によって使わない補助がある(RXDEF が __attribute__ を空にするので pragma で止める) */
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-const-variable"

/* ------------------------------------------------------------ 偽物: SCI2 のログ(app_log_*)・g_app_dbg */
#define MAX_LOGS 1024
static char     g_logs[MAX_LOGS][APPLOG_LINE_MAX];
static unsigned g_nlogs;
static unsigned g_log_drops;
static uint32_t g_hook_tick;               /* loop_hook が毎周回書く(s_tick の写し) */
static uint32_t g_tick_seen, g_tick_lines, g_max_tick_lines;   /* 1 tick(10ms)に出た行数の最大 */
static char     g_cmd[64];                 /* UART 受信コマンド(1 tick に 1 文字読まれる) */
static unsigned g_cmd_n, g_cmd_r;
volatile app_dbg_t g_app_dbg;

void app_debug_set_err(uint32_t code, uint32_t detail) { g_app_dbg.err_code = code; g_app_dbg.err_detail = detail; }
void app_log_line(const char *s, size_t n)
{
    if ((s == NULL) || (n == 0u) || (n >= APPLOG_LINE_MAX)) { g_log_drops++; return; }
    if (g_hook_tick != g_tick_seen) { g_tick_seen = g_hook_tick; g_tick_lines = 0u; }
    g_tick_lines++;
    if (g_tick_lines > g_max_tick_lines) { g_max_tick_lines = g_tick_lines; }
    if (g_nlogs < MAX_LOGS) {
        memcpy(g_logs[g_nlogs], s, n);
        g_logs[g_nlogs][n] = '\0';
        g_nlogs++;
    }
}
void app_log_poll(void) { }
int  app_log_getc(void) { return (g_cmd_r < g_cmd_n) ? (int)(unsigned char)g_cmd[g_cmd_r++] : -1; }
uint32_t app_log_drops(void) { return g_log_drops; }

/* ------------------------------------------------------------ 偽物: EN(GPIO)・CMT */
static char     g_gpio_ev[64];             /* "W1;D;W0;W1;" の形で順序を記録 */
static unsigned g_en_low_iter, g_en_high_iter, g_en_edges;
static unsigned g_iter, g_ipt;
static bool     g_timer_on, g_cmt_ok;
static unsigned g_dir_out_calls;

void R_GPIO_PinWrite(gpio_port_pin_t pin, gpio_level_t level)
{
    size_t k = strlen(g_gpio_ev);
    CHECK_EQ(pin, GPIO_PORT_4_PIN_3);
    if (k + 4u < sizeof(g_gpio_ev)) { g_gpio_ev[k] = 'W'; g_gpio_ev[k + 1u] = (level == GPIO_LEVEL_HIGH) ? '1' : '0'; g_gpio_ev[k + 2u] = ';'; g_gpio_ev[k + 3u] = '\0'; }
    if (level == GPIO_LEVEL_LOW) { g_en_low_iter = g_iter; g_en_edges++; } else { g_en_high_iter = g_iter; }
}
void R_GPIO_PinDirectionSet(gpio_port_pin_t pin, gpio_dir_t dir)
{
    size_t k = strlen(g_gpio_ev);
    CHECK_EQ(pin, GPIO_PORT_4_PIN_3);
    CHECK_EQ(dir, GPIO_DIRECTION_OUTPUT);
    g_dir_out_calls++;
    if (k + 3u < sizeof(g_gpio_ev)) { g_gpio_ev[k] = 'D'; g_gpio_ev[k + 1u] = ';'; g_gpio_ev[k + 2u] = '\0'; }
}
bool R_CMT_CreatePeriodic(uint32_t frequency_hz, void (*callback)(void *pdata), uint32_t *channel)
{
    (void)callback;
    CHECK_EQ(frequency_hz, 100);
    *channel = 0u;
    if (g_cmt_ok) { g_timer_on = true; }
    return g_cmt_ok;
}

/* ------------------------------------------------------------ 被試験物 */
static void loop_hook(void);
#define app_log_poll() (loop_hook(), app_log_poll())
#include "../../src/app/wifi_at.c"
#undef app_log_poll

#if APP_WX_PRODUCT
static volatile uint32_t s_tick;                /* 製品の s_tick は app_main.c にある。ここでは loop_hook が進める */
#endif

/* ------------------------------------------------------------ シナリオ */
typedef struct { unsigned tick; char ch; } cmdev_t;

static struct {
    bool     esp;                  /* 偽の ESP32 が応答する */
    unsigned resp_delay_iters;     /* 応答までのループ周回数 */
    unsigned drain_iters;          /* 1 回の Send が終わるまでのループ周回数(BUSY の長さ) */
    uint32_t stop_tick;
    bool     stop_on_result;
    const cmdev_t *cmds; unsigned n_cmds;
    uint32_t cb_ticks[8]; unsigned n_cb;   /* この tick に SCI_EVT_OVFL_ERR のコールバックを起こす */
    unsigned rx_probe_calls;       /* >0: ループ呼び出し n 回目までの wifi_core_bytes を記録 */
    unsigned max_iter;
} C;

static jmp_buf  g_jb;
static unsigned g_busy_iters;
static size_t   g_scan;            /* 偽 ESP が読んだ tx の位置 */
static bool     g_echo_on;
static uint32_t g_probe[16];
static unsigned g_result_tick;
static bool     g_result_seen;
static char     g_lastres[APPLOG_LINE_MAX];
typedef struct { unsigned due; char text[400]; } pend_t;
static pend_t   g_pend[32];
static unsigned g_npend;

static void esp_say(unsigned delay, const char *text)
{
    unsigned i;
    for (i = 0u; i < g_npend; i++) { if (g_pend[i].due == 0u) { break; } }   /* 配信済みの枠を使い回す(長い走行) */
    if (i < 32u) {
        g_pend[i].due = g_iter + delay;
        snprintf(g_pend[i].text, sizeof(g_pend[i].text), "%s", text);
        if (i == g_npend) { g_npend++; }
    }
}

static unsigned g_data_left;       /* AT+CIPSEND の後、要求本体(生のバイト)として読み飛ばす残り */
static bool     g_tcp_fail;        /* AT+CIPSTART に ERROR を返す */
static bool     g_tcp_nodata;      /* 要求を受けても +IPD を返さず CLOSED だけ */
static bool     g_join_fail;       /* AT+CWJAP に FAIL を返す */
static unsigned g_http_min = 24u, g_http_sec = 13u;   /* 応答の Date: の UTC の分・秒(04:mm:ss) */
static uint32_t g_http_a_tick = 0u;                    /* 0 以外 = a の時の tick。その後の Date: は実時間どおり進む(本物の Yahoo と同じ。時計が小さな遅れを採用しない規則 §5.9 と両立させる) */
static uint32_t g_cipstart_tick[32];
static unsigned g_n_cipstart;
static bool     g_conn_tcp;        /* 直近の AT+CIPSTART が "TCP"(平文 80。Yahoo は 301 を返す) */
static int      g_fail_sym = -1;   /* この銘柄の要求には 404 を返す(-1 = なし。APP_STK_FAULT と同じ試験) */
static bool     g_wx_fail;         /* 天気の要求(GET /v1/forecast)には 404 を返す(APP_STK_FAULT = 4 と同じ試験) */
static char     g_req[300];        /* 直近の要求本体(銘柄の判定用) */
static unsigned g_req_n;
static unsigned g_req_len;         /* 直近の AT+CIPSEND=<n> の n */

/* 3 銘柄の Yahoo ふうの値(design.md §2.1。2026-11-25 13:24 JST 時点)。6723.T と ^N225 は東京の取引時間帯の中(遅れ 15 分)、^GSPC は閉場 */
static const char *const K_PRICE[3] = { "2433.0", "57953.84", "7182.78" };
static const char *const K_PREV[3]  = { "2378.0", "57499.16", "7201.09" };
static const char *const K_CUR[3]   = { "JPY", "JPY", "USD" };
static const char *const K_ENC[3]   = { "6723.T", "%5EN225", "%5EGSPC" };
static const unsigned long K_MT[3]  = { 1795579752ul, 1795579752ul, 1795553940ul };
static const unsigned long K_RS[3]  = { 1795564800ul, 1795564800ul, 1795617000ul };
static const unsigned long K_RE[3]  = { 1795588200ul, 1795588200ul, 1795640400ul };

/* 直近の要求の銘柄(0〜2)。chart の要求でなければ -1(Google の GET / など) */
static int req_sym(void)
{
    int i;

    if (strstr(g_req, "GET /v8/finance/chart/") == NULL) { return -1; }
    for (i = 0; i < 3; i++) {
        char key[40];

        snprintf(key, sizeof key, "chart/%s?", K_ENC[i]);
        if (strstr(g_req, key) != NULL) { return i; }
    }
    return -1;
}

/* 要求本体を受け終わった: SEND OK、+IPD(HTTP 応答。分割)、CLOSED を返す。応答は Yahoo の chart API ふう(本文・ヘッダーに目印 BODY_SENTINEL を入れてある) */
static void esp_http(void)
{
    static char http[2600];
    char        piece[320];
    unsigned    d = C.resp_delay_iters + (10u * g_ipt);
    size_t      n, k, m;
    int         sym = req_sym();
    unsigned    http_min = g_http_min, http_sec = g_http_sec;

    if (g_http_a_tick != 0u) {
        unsigned t = (unsigned)((((uint32_t)s_tick - g_http_a_tick) / 100u) + (g_http_min * 60u) + g_http_sec);

        http_min = t / 60u; http_sec = t % 60u;
    }
    if (strstr(g_req, "GET /v1/forecast?") != NULL) {   /* 天気(Open-Meteo。平文 80): 本物と同じ形(HTTP/1.1 200・Content-Length なし・current は current_units の後)。目印 BODY_SENTINEL */
        if (g_wx_fail) {
            n = (size_t)snprintf(http, sizeof(http), "HTTP/1.1 404 Not Found\r\nDate: Wed, 25 Nov 2026 04:%02u:%02u GMT\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\n\r\n"
                                 "{\"error\":true,\"reason\":\"BODY_SENTINEL\"}", http_min % 100u, http_sec % 100u);
        } else {
            n = (size_t)snprintf(http, sizeof(http), "HTTP/1.1 200 OK\r\nDate: Wed, 25 Nov 2026 04:%02u:%02u GMT\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\n\r\n"
                                 "{\"latitude\":35.7,\"longitude\":139.8,\"generationtime_ms\":0.05,\"utc_offset_seconds\":32400,\"timezone\":\"BODY_SENTINEL\",\"elevation\":40.0,"
                                 "\"current_units\":{\"time\":\"iso8601\",\"interval\":\"seconds\",\"temperature_2m\":\"\xc2\xb0""C\",\"weather_code\":\"wmo code\"},"
                                 "\"current\":{\"time\":\"2026-11-25T13:15\",\"interval\":900,\"temperature_2m\":12.9,\"weather_code\":61}}", http_min % 100u, http_sec % 100u);
        }
    } else if (g_conn_tcp) {                            /* 平文 80: Yahoo は HTTPS へ転送(301) */
        n = (size_t)snprintf(http, sizeof(http), "HTTP/1.1 301 Moved Permanently\r\ndate: Wed, 25 Nov 2026 04:%02u:%02u GMT\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<html>BODY_SENTINEL</html>", http_min % 100u, http_sec % 100u);
    } else if (sym < 0) {                               /* Phase 0 の g(Google): chart のキーが無い 200 */
        n = (size_t)snprintf(http, sizeof(http), "HTTP/1.0 200 OK\r\ndate: Wed, 25 Nov 2026 04:%02u:%02u GMT\r\nConnection: close\r\n\r\n{\"chart\":\"BODY_SENTINEL\"}", http_min % 100u, http_sec % 100u);
    } else if (sym == g_fail_sym) {                     /* この銘柄だけ失敗(404) */
        n = (size_t)snprintf(http, sizeof(http), "HTTP/1.0 404 Not Found\r\ndate: Wed, 25 Nov 2026 04:%02u:%02u GMT\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n"
                             "{\"chart\":{\"result\":null,\"error\":{\"code\":\"Not Found\",\"description\":\"BODY_SENTINEL\"}}}", http_min % 100u, http_sec % 100u);
    } else {
        n = (size_t)snprintf(http, sizeof(http),
            "HTTP/1.0 200 OK\r\ndate: Wed, 25 Nov 2026 04:%02u:%02u GMT\r\nContent-Type: application/json;charset=utf-8\r\nX-Body: BODY_SENTINEL\r\nConnection: close\r\n\r\n"
            "{\"chart\":{\"result\":[{\"meta\":{\"currency\":\"%s\",\"symbol\":\"%s\",\"exchangeName\":\"JPX\",\"instrumentType\":\"EQUITY\",\"firstTradeDate\":-2230000000,"
            "\"regularMarketTime\":%lu,\"gmtoffset\":32400,\"timezone\":\"JST\",\"exchangeTimezoneName\":\"Asia/Tokyo\",\"regularMarketPrice\":%s,"
            "\"fiftyTwoWeekHigh\":2999.0,\"fiftyTwoWeekLow\":1500.5,\"regularMarketDayHigh\":2450.0,\"regularMarketDayLow\":2400.0,\"regularMarketVolume\":1234567,"
            "\"longName\":\"BODY_SENTINEL\",\"shortName\":\"BODY_SENTINEL\",\"chartPreviousClose\":%s,\"priceHint\":1,"
            "\"currentTradingPeriod\":{\"pre\":{\"timezone\":\"JST\",\"start\":1795536000,\"end\":%lu,\"gmtoffset\":32400},"
            "\"regular\":{\"timezone\":\"JST\",\"start\":%lu,\"end\":%lu,\"gmtoffset\":32400},"
            "\"post\":{\"timezone\":\"JST\",\"start\":%lu,\"end\":1795600800,\"gmtoffset\":32400}},"
            "\"dataGranularity\":\"1d\",\"range\":\"2d\",\"validRanges\":[\"1d\",\"5d\",\"1mo\",\"3mo\",\"6mo\",\"1y\",\"2y\",\"5y\",\"10y\",\"ytd\",\"max\"]},"
            "\"timestamp\":[1795479000,%lu],\"indicators\":{\"quote\":[{\"open\":[2380.0,2400.0],\"low\":[2370.0,2400.0],\"high\":[2440.0,2450.0],"
            "\"volume\":[5000000,1234567],\"close\":[%s,%s]}]}}],\"error\":null}}",
            http_min % 100u, http_sec % 100u, K_CUR[sym], K_ENC[sym], K_MT[sym], K_PRICE[sym], K_PREV[sym], K_RS[sym], K_RS[sym], K_RE[sym], K_RE[sym], K_MT[sym], K_PREV[sym], K_PRICE[sym]);
    }
    CHECK(n < sizeof(http));
    if (g_tcp_nodata) {
        esp_say(d, "\r\nRecv 144 bytes\r\n\r\nSEND OK\r\n\r\nCLOSED\r\n");
        return;
    }
    snprintf(piece, sizeof(piece), "\r\nRecv 144 bytes\r\n\r\nSEND OK\r\n\r\n+IPD,%u:", (unsigned)n);
    esp_say(d, piece);
    for (k = 0u; k < n; k += m) {
        m = ((n - k) > 300u) ? 300u : (n - k);
        memcpy(piece, &http[k], m);
        piece[m] = '\0';
        esp_say(d, piece);
    }
    esp_say(d, "CLOSED\r\n");
}

static void esp_cmd(const char *cmd)
{
    char buf[400];
    const char *resp = "OK";
    unsigned d = C.resp_delay_iters;

    if (strcmp(cmd, "ATE0") == 0) { resp = "OK"; }
    else if (strcmp(cmd, "AT+GMR") == 0) { resp = "AT version:1.1.3.0(5a40576 - Nov 28 2018 12:50:55)\r\nSDK version:v3.0.7\r\nOK"; }
    else if (strncmp(cmd, "AT+CWJAP=", 9) == 0) { resp = g_join_fail ? "+CWJAP:3\r\n\r\nFAIL" : "WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK"; d += 300u * g_ipt; }   /* 接続に 3 秒 */
    else if (strcmp(cmd, "AT+CWLAP") == 0) { resp = "+CWLAP:(3,\"NEIGHBOR_SENT_1\",-70,\"de:ad:be:ef:00:01\",1)\r\n+CWLAP:(3,\"" WIFI_SSID_T "\",-52,\"de:ad:be:ef:00:02\",6)\r\nOK"; d += 20u * g_ipt; }
    else if (strcmp(cmd, "AT+CIFSR") == 0) { resp = "+CIFSR:APIP,\"192.168.4.1\"\r\n+CIFSR:APMAC,\"de:ad:be:ef:01:01\"\r\n+CIFSR:STAIP,\"192.168.1.23\"\r\n+CIFSR:STAMAC,\"de:ad:be:ef:02:02\"\r\nOK"; }
    else if (strncmp(cmd, "AT+CIPDOMAIN=", 13) == 0) { resp = "+CIPDOMAIN:\"142.250.0.1\"\r\nOK"; }
    else if (strcmp(cmd, "AT+PING=\"8.8.8.8\"") == 0) { resp = "+PING:23\r\nOK"; }
    else if (strncmp(cmd, "AT+PING=", 8) == 0) { resp = "+PING:31\r\nOK"; }
    else if (strcmp(cmd, "AT+CIPMUX=0") == 0) { resp = "OK"; }
    else if (strcmp(cmd, "AT+CIPCLOSE") == 0) { resp = "ERROR"; }                                         /* つながっていない */
    else if (strncmp(cmd, "AT+CIPSTART=", 12) == 0) { if (g_n_cipstart < 32u) { g_cipstart_tick[g_n_cipstart] = g_hook_tick; } g_n_cipstart++; g_conn_tcp = (strstr(cmd, "\"TCP\"") != NULL); resp = g_tcp_fail ? "DNS Fail\r\n\r\nERROR" : "CONNECT\r\n\r\nOK"; d += 20u * g_ipt; }
    else if (strncmp(cmd, "AT+CIPSEND=", 11) == 0) { resp = "\r\nOK\r\n> "; g_req_len = (unsigned)strtoul(cmd + 11, NULL, 10); g_data_left = g_req_len; g_req_n = 0u; g_req[0] = '\0'; }
    else if (strcmp(cmd, "AT+CIPSSLCCONF=0,0,0") == 0) { resp = "OK"; }
    snprintf(buf, sizeof(buf), "%s%s%s%s", g_echo_on ? cmd : "", g_echo_on ? "\r\r\n" : "", resp, (g_data_left > 0u) ? "" : "\r\n");   /* > の後は改行なし */
    if (strcmp(cmd, "ATE0") == 0) { g_echo_on = false; }
    esp_say(d, buf);
}

static void esp_scan(void)
{
    char line[300];
    size_t i, n = 0u;

    for (i = g_scan; i < g_fs.tx_len; i++) {
        if (g_data_left > 0u) {                         /* 要求本体(生のバイト。行として読まない) */
            if ((g_req_n + 1u) < sizeof(g_req)) { g_req[g_req_n++] = (char)g_fs.tx[i]; g_req[g_req_n] = '\0'; }
            g_data_left--;
            g_scan = i + 1u;
            n = 0u;
            if (g_data_left == 0u) { esp_http(); }
            continue;
        }
        if ((g_fs.tx[i] == '\r') && ((i + 1u) < g_fs.tx_len) && (g_fs.tx[i + 1u] == '\n')) {
            line[n] = '\0';
            esp_cmd(line);
            n = 0u;
            i++;
            g_scan = i + 1u;
        } else if (n < sizeof(line) - 1u) {
            line[n++] = (char)g_fs.tx[i];
        }
    }
}

static void loop_hook(void)
{
    unsigned k;
    uint32_t t;
    sci_cb_args_t a;

    g_iter++;
    g_hook_tick = s_tick;
    if (g_iter > C.max_iter) { longjmp(g_jb, 1); }
    if (!g_fs.tx_idle) { g_busy_iters++; if (g_busy_iters >= C.drain_iters) { fake_sci_tx_done(); g_busy_iters = 0u; } }
    else { g_busy_iters = 0u; }
    if (C.rx_probe_calls > 0u) {
        if (g_iter <= C.rx_probe_calls) { g_probe[g_iter] = wifi_core_bytes(&s_core); }
        if (g_iter >= C.rx_probe_calls) { longjmp(g_jb, 1); }
        return;
    }
    if (g_timer_on && ((g_iter % g_ipt) == 0u)) {         /* 10ms の tick */
        s_tick++;
        t = s_tick;
        if (t >= C.stop_tick) { longjmp(g_jb, 1); }
        for (k = 0u; k < C.n_cmds; k++) {
            if (C.cmds[k].tick == t) { if (g_cmd_n < sizeof(g_cmd)) { g_cmd[g_cmd_n++] = C.cmds[k].ch; } }
        }
        for (k = 0u; k < C.n_cb; k++) {
            if (C.cb_ticks[k] == t) {
                memset(&a, 0, sizeof(a));
                a.event = SCI_EVT_OVFL_ERR;
                sci_cb(&a);
                a.event = SCI_EVT_RX_CHAR;             /* これは無視されるはず */
                sci_cb(&a);
            }
        }
    }
    if (C.esp) {
        esp_scan();
        for (k = 0u; k < g_npend; k++) {
            if ((g_pend[k].due != 0u) && (g_pend[k].due <= g_iter)) {
                const char *p = g_pend[k].text;
                while (*p != '\0') { fake_sci_rx_put((uint8_t)*p); p++; }
                g_pend[k].due = 0u;
            }
        }
        /* EN が Low → High になったら、少し後に ready(ESP32 の起動) */
        if ((g_en_high_iter > g_en_low_iter) && (g_en_low_iter != 0u) && (g_en_edges > 0u)) {
            esp_say(30u * g_ipt, "\r\nets Jun  8 2016 00:22:57\r\n\r\nready\r\n");
            g_en_edges = 0u;
            g_echo_on = true;
        }
    }
    /* 結果行が出たら、少し待って(出力を流し切って)終わる */
    if (C.stop_on_result && g_nlogs > 0u) {
        unsigned i;
        for (i = 0u; i < g_nlogs; i++) {
            if (strncmp(g_logs[i], "[wifi] result=", 14) == 0) {
                if (!g_result_seen) { g_result_seen = true; g_result_tick = s_tick; snprintf(g_lastres, sizeof(g_lastres), "%s", g_logs[i]); }
                break;
            }
        }
        if (g_result_seen && (s_tick >= g_result_tick + 30u)) { longjmp(g_jb, 1); }
    }
}

static void reset_all(void)
{
    fake_sci_reset();
    g_fs.auto_idle = false;
    memset(&C, 0, sizeof(C));
    memset((void *)&g_app_dbg, 0, sizeof(g_app_dbg));
    memset(&s_core, 0, sizeof(s_core));
    memset(g_logs, 0, sizeof(g_logs));
    memset(g_gpio_ev, 0, sizeof(g_gpio_ev));
    memset(g_pend, 0, sizeof(g_pend));
    memset(g_cmd, 0, sizeof(g_cmd));
    memset(g_probe, 0, sizeof(g_probe));
    s_hdl = NULL; s_ok = false; s_open_err = 0u; s_tick = 0u;
    s_rx_ev_val = 0u; s_rx_ev_seq = 0u; s_rx_ev_seen = 0u; s_rxerr_ms = 0u; s_rxerr_sent = false;
#if APP_WIFI_TEST && APP_WIFI_AUTOSTART
    s_auto_done = false;
#endif
    g_nlogs = 0u; g_log_drops = 0u; g_cmd_n = 0u; g_cmd_r = 0u;
    g_en_low_iter = 0u; g_en_high_iter = 0u; g_en_edges = 0u; g_dir_out_calls = 0u;
    g_iter = 0u; g_ipt = 3u; g_timer_on = false; g_cmt_ok = true;
    g_busy_iters = 0u; g_scan = 0u; g_echo_on = true; g_npend = 0u;
    g_data_left = 0u; g_tcp_fail = false; g_tcp_nodata = false; g_req_len = 0u; g_conn_tcp = false; g_fail_sym = -1; g_req_n = 0u; g_req[0] = '\0';
    s_wx_seen = 0u; memset(s_slot, 0, sizeof s_slot); s_upd_min = STK_NONE16; memset(&s_wslot, 0, sizeof s_wslot); s_wmo = 0u; g_wx_fail = false;
#if APP_WIFI_TEST
    s_wx_last_ok = 0u;
#endif
    g_hook_tick = 0u; g_tick_seen = 0u; g_tick_lines = 0u; g_max_tick_lines = 0u; g_join_fail = false; g_http_min = 24u; g_http_sec = 13u; g_http_a_tick = 0u; g_n_cipstart = 0u; memset(g_cipstart_tick, 0, sizeof g_cipstart_tick);
    wx_clock_init(&s_clk);
#if APP_WX_PRODUCT
    memset(&s_sched, 0, sizeof s_sched); s_act = 0u; s_act_t0 = 0u; s_done_seen = 0u; s_cycle_seen = 0u; s_last_net = 0u; s_last_busy = 0u; s_res_ok = 0u; s_res_http = 0u;
#endif
    g_result_tick = 0u; g_result_seen = false; g_lastres[0] = '\0';
    C.stop_tick = 1000u; C.max_iter = 3000000u; C.esp = true; C.drain_iters = 1u;
}

#if APP_WX_PRODUCT
/* 製品のメインループの代わり(app_main.c の loop と同じ呼び方)。コマンド文字(1 tick に 1 文字):
 *   x = 更新(src=uart)、t = 更新(src=touch)、s = [stk] 状態行(net と sym 3 本)、
 *   z = 最後の成功を 31 分前にする(stale の確認。gdb の s_slot[].ok_ms への書き込みと同じ)、
 *   a = 以後の応答の Date: を 04:40:50 UTC に(再同期の確認)、f = 以後 TCP を張れない、j = 以後 join に FAIL、g = 以後 join も TCP も正常、
 *   k = 以後 ^N225(銘柄 1)の要求に 404、h = 銘柄 1 の失敗を直す、
 *   w = 以後 天気の要求に 404、v = 天気の失敗を直す、y = 天気の最後の成功を 60 分前にする(天気の「不明」の確認) */
static stk_view_t g_v;                      /* 最後の stk_ui */
static clk_view_t g_c;                      /* 最後の clk_ui */
static uint32_t   g_c_sync_tick, g_c_have_tick, g_v_have_tick;   /* 時刻が分かった / 天気が分かった / 株価が 1 つでも出た最初の tick(0 = まだ) */
static unsigned g_v_net_seen[4];            /* 各 net の値が stk_ui に出た tick 数 */
static bool     g_v_stale_seen, g_v_busy_seen, g_v_partial_seen;
static unsigned g_req_ret[8], g_n_req;      /* wx_request の戻り値の列 */
static void product_main(void)
{
    uint32_t last = 0u;
    uint32_t now;
    int      ch;

    g_timer_on = true;
    memset(g_v_net_seen, 0, sizeof g_v_net_seen); g_v_stale_seen = false; g_v_busy_seen = false; g_v_partial_seen = false; g_n_req = 0u;
    memset(&g_v, 0, sizeof g_v); memset(&g_c, 0, sizeof g_c); g_c.clock_min = 0xFFFFu; g_c_sync_tick = 0u; g_c_have_tick = 0u; g_v_have_tick = 0u;
    wx_init(0u);
    for (;;) {
        loop_hook();
        now = s_tick;
        if (now != last) {                  /* 10ms ごと(app_main の tick_step) */
            unsigned i;

            last = now;
            ch = app_log_getc();
            if ((ch == 'x') || (ch == 't')) {
                wx_req_t r = wx_request((ch == 'x') ? WX_SRC_UART : WX_SRC_TOUCH, now * 10u);
                if (g_n_req < 8u) { g_req_ret[g_n_req] = (unsigned)r; }
                g_n_req++;
            }
            else if (ch == 's') { wx_log_state(now * 10u); }
            else if (ch == 'z') { for (i = 0u; i < 3u; i++) { s_slot[i].ok_ms -= 1860000u; } }
            else if (ch == 'a') { g_http_min = 40u; g_http_sec = 50u; g_http_a_tick = (now != 0u) ? now : 1u; }
            else if (ch == 'f') { g_tcp_fail = true; }
            else if (ch == 'j') { g_join_fail = true; }
            else if (ch == 'g') { g_join_fail = false; g_tcp_fail = false; }
            else if (ch == 'k') { g_fail_sym = 1; }
            else if (ch == 'h') { g_fail_sym = -1; }
            else if (ch == 'w') { g_wx_fail = true; }
            else if (ch == 'v') { g_wx_fail = false; }
            else if (ch == 'y') { s_wslot.ok_ms -= 3600000u; }
            else { /* 無視 */ }
            stk_ui(&g_v, now * 10u);
            clk_ui(&g_c, now * 10u);
            if ((g_c.clock_min != 0xFFFFu) && (g_c_sync_tick == 0u)) { g_c_sync_tick = now; }
            if ((g_c.have != 0u) && (g_c_have_tick == 0u)) { g_c_have_tick = now; }
            if ((g_v.have_any != 0u) && (g_v_have_tick == 0u)) { g_v_have_tick = now; }
            if (g_v.net < 4u) { g_v_net_seen[g_v.net]++; }
            if (g_v.stale_all != 0u) { g_v_stale_seen = true; }
            if (g_v.busy != 0u) { g_v_busy_seen = true; }
            if (g_v.partial != 0u) { g_v_partial_seen = true; }
        }
        wx_poll(now * 10u);
    }
}
#endif

static void run(void)
{
#if APP_WX_PRODUCT
    if (setjmp(g_jb) == 0) { product_main(); }
#else
    if (setjmp(g_jb) == 0) { wifi_test_main(); }
#endif
}

/* ------------------------------------------------------------ 補助(ビルドの種類によっては使わない) */
static unsigned count_prefix(const char *prefix)
{
    unsigned i, n = 0u;
    for (i = 0u; i < g_nlogs; i++) { if (strncmp(g_logs[i], prefix, strlen(prefix)) == 0) { n++; } }
    return n;
}
static int find_idx(const char *prefix)
{
    unsigned i;
    for (i = 0u; i < g_nlogs; i++) { if (strncmp(g_logs[i], prefix, strlen(prefix)) == 0) { return (int)i; } }
    return -1;
}
static unsigned long field(const char *line, const char *key)
{
    const char *p = strstr(line, key);
    return (p == NULL) ? 0xFFFFFFFFul : strtoul(p + strlen(key), NULL, 10);
}
/* 失敗の調査用: 環境変数 T_DUMP があれば、今のシナリオのログを全部出す(秘密は偽の目印だけ) */
static int starts(const char *l, const char *prefix) { return strncmp(l, prefix, strlen(prefix)) == 0; }
static void dump_logs(void)
{
    unsigned i;
    if (getenv("T_DUMP") == NULL) { return; }
    for (i = 0u; i < g_nlogs; i++) { printf("    | %s", g_logs[i]); }
}
static unsigned tx_count(const char *s)
{
    size_t   i, n = strlen(s);
    unsigned c = 0u;
    for (i = 0u; (i + n) <= g_fs.tx_len; i++) { if (memcmp(&g_fs.tx[i], s, n) == 0) { c++; } }
    return c;
}
static bool tx_has(const char *s)
{
    size_t i, n = strlen(s);
    for (i = 0u; (i + n) <= g_fs.tx_len; i++) { if (memcmp(&g_fs.tx[i], s, n) == 0) { return true; } }
    return false;
}
/* 全ログ行が許された種類で、秘密も生の応答も出ていない */
static void check_lines_clean(void)
{
    unsigned i;
    bool ok_kind = true, no_secret = true;
    for (i = 0u; i < g_nlogs; i++) {
        const char *l = g_logs[i];
        if (!(strncmp(l, "[init] ", 7) == 0 || strncmp(l, "[hb] ", 5) == 0 || strncmp(l, "[wifi] ", 7) == 0 ||
              strncmp(l, "[wifiv] ", 8) == 0 || strncmp(l, "[st] ", 5) == 0 || strncmp(l, "[help] ", 7) == 0 ||
              strncmp(l, "[err] ", 6) == 0 || strncmp(l, "[stk] ", 6) == 0 || strncmp(l, "[wx] ", 5) == 0)) { ok_kind = false; printf("  unexpected line kind: %.20s\n", l); }
        if (strstr(l, "SENTINEL") != NULL || strstr(l, "NEIGHBOR") != NULL || strstr(l, "de:ad") != NULL || strstr(l, "CWLAP") != NULL || strstr(l, "CWJAP") != NULL || strstr(l, "PW_") != NULL || strstr(l, "SSID_") != NULL ||
            strstr(l, "BODY_SENTINEL") != NULL || strstr(l, "regularMarket") != NULL || strstr(l, "chartPrevious") != NULL || strstr(l, "\"chart\"") != NULL || strstr(l, "HTTP/1") != NULL || strstr(l, "query1") != NULL ||
            strstr(l, "+IPD") != NULL || strstr(l, "GET ") != NULL || strstr(l, "Date:") != NULL || strstr(l, "date:") != NULL ||
            strstr(l, "ready") != NULL || strstr(l, "ets ") != NULL ||
            strstr(l, "\"current\"") != NULL || strstr(l, "latitude") != NULL || strstr(l, "generationtime") != NULL || strstr(l, "temperature_2m") != NULL || strstr(l, "weather_code") != NULL || strstr(l, "open-meteo") != NULL) { no_secret = false; }
        if (strstr(l, "\r\n") != l + strlen(l) - 2) { ok_kind = false; }
    }
    CHECK(ok_kind);
    CHECK(no_secret);
}

static const cmdev_t k_d_at20[] = { { 20u, 'd' } };

int main(void)
{
    unsigned i;
    int k;

#if APP_WIFI_TEST
    TEST("A1 起動: PinSet は Open より前、SCI1・115200・8N1・優先度2・コールバックあり。EN は先に High、次に出力");
    reset_all(); C.esp = false; C.stop_tick = 5u;
    run();
    CHECK_EQ(g_fs.open_calls, 1);
    CHECK_EQ(g_fs.pinset_calls, 1);
    CHECK_EQ(g_fs.pinset_at_open, 1);
    CHECK_EQ(g_fs.open_chan, 1);
    CHECK_EQ(g_fs.open_mode, SCI_MODE_ASYNC);
    CHECK_EQ(g_fs.open_cfg.async.baud_rate, 115200);
    CHECK_EQ(g_fs.open_cfg.async.data_size, SCI_DATA_8BIT);
    CHECK_EQ(g_fs.open_cfg.async.parity_en, SCI_PARITY_OFF);
    CHECK_EQ(g_fs.open_cfg.async.stop_bits, SCI_STOPBITS_1);
    CHECK_EQ(g_fs.open_cfg.async.clk_src, SCI_CLK_INT);
    CHECK_EQ(g_fs.open_cfg.async.int_priority, 2);
    CHECK(g_fs.open_cb_set);
    CHECK(strcmp(g_gpio_ev, "W1;D;") == 0);              /* 開始時は何も送らず、EN も動かさない(d を受けるまで) */
    CHECK_EQ(g_fs.send_calls, 0);
    CHECK(strncmp(g_logs[0], "[init] stage=wifi ok=1 err=0 detail=0", 36) == 0);
    CHECK_EQ(count_prefix("[init] "), 1);                /* lcd / touch / timer の [init] は出ない(専用モード) */
    CHECK_EQ(count_prefix("[wifi] "), 0);                /* 自動開始なしなら、コマンドが来るまで何もしない */
    CHECK_EQ(WIFI_BAUD, 115200);
    CHECK(g_timer_on);

#if !APP_WIFI_AUTOSTART && !defined(T_NOSECRET)
    TEST("B1 d で全部: EN Low ≧ 20ms → High、ready、AT 〜 ping_dns。result=pass。[hb] は止まらない。秘密は出ない");
    reset_all(); C.stop_on_result = true; C.stop_tick = 3000u; C.cmds = k_d_at20; C.n_cmds = 1u;
    run();
    CHECK(g_result_seen);
    CHECK(strncmp(g_lastres, "[wifi] result=pass stage=none ping_ip=23 ping_dns=31", 51) == 0);
    CHECK(strcmp(g_gpio_ev, "W1;D;W0;W1;") == 0);        /* 起動時 High → 出力、reset で Low → High */
    CHECK((g_en_high_iter - g_en_low_iter) >= (2u * g_ipt));  /* 20ms 以上(2 tick) */
    for (k = 1; k <= 14; k++) {
        static const char *const nm[] = { "", "reset", "boot", "at", "echo", "gmr", "mode", "qap", "lapopt", "scan", "join", "ip", "dns", "ping_ip", "ping_dns" };
        char p[64];
        snprintf(p, sizeof(p), "[wifi] step=%s res=ok ", nm[k]);
        CHECK_EQ(count_prefix(p), 1);
    }
    CHECK_EQ(count_prefix("[wifi] step="), 28);          /* 14 step x (start + ok)。failed / timeout なし */
    CHECK_EQ(count_prefix("[wifi] step=store"), 0);
    CHECK(find_idx("[wifiv] key=scan val=total=2,found=1,near=0,bad=0\r\n") >= 0);
    CHECK(find_idx("[wifiv] key=ap val=ch=6,rssi=-52,ecn=3\r\n") >= 0);
    CHECK(find_idx("[wifi] step=scan res=ok try=1 ms=") >= 0);
    CHECK(tx_has("AT+CWQAP\r\n") && tx_has("AT+CWLAPOPT=1,31\r\n") && tx_has("AT+CWLAP\r\n") && tx_has("AT+CIFSR\r\n"));
    CHECK(!tx_has("SYSSTORE") && !tx_has("CIPSTA"));
    CHECK(tx_has("AT+CWJAP=\"SSID_SENTINEL_7c1\",\"PW_SENTINEL_9f3\"\r\n"));   /* join のコマンドが正しく送られた */
    CHECK(tx_has("AT+PING=\"www.google.com\"\r\n"));
    CHECK(find_idx("[wifiv] key=ver val=1.1.3.0") >= 0);
    CHECK(find_idx("[wifiv] key=ip val=192.168.1.23") >= 0);
    CHECK(find_idx("[wifiv] key=ping val=23") >= 0);
    CHECK(find_idx("[wifiv] key=ping val=31") >= 0);
    CHECK(find_idx("[wifiv] key=dns val=142.250.0.1") >= 0);
    check_lines_clean();
    CHECK_EQ(g_log_drops, 0);
    CHECK(g_result_tick >= 300u);                        /* join に 3 秒かかる模擬を含む */
    CHECK(count_prefix("[hb] ") >= (g_result_tick / 100u) - 1u);   /* 1 秒ごとに途切れず出る(join の待ちの間も) */
    for (i = 0u; i < g_nlogs; i++) { if (strncmp(g_logs[i], "[hb] ", 5) == 0) { CHECK_EQ(field(g_logs[i], " drop="), 0); CHECK_EQ(field(g_logs[i], " err="), 0); CHECK_EQ(field(g_logs[i], " demo="), 0); } }
    k = find_idx("[wifi] result=");
    CHECK(k > find_idx("[wifi] step=ping_dns res=ok "));
    CHECK_EQ(g_app_dbg.scr, 0);                          /* 終了後は 0 */
    CHECK(g_app_dbg.frames > 100u);                      /* frm = ESP32 から受けたバイト数 */
    CHECK_EQ(g_app_dbg.frames, wifi_core_bytes(&s_core));

    TEST("B2 送信が BUSY の間は同じコマンドを再度渡し、1回だけ送る(応答が先に来ても進む)");
    reset_all(); C.stop_on_result = true; C.stop_tick = 3000u; C.cmds = k_d_at20; C.n_cmds = 1u;
    C.drain_iters = 4u * g_ipt; C.resp_delay_iters = 0u;
    run();
    CHECK(g_result_seen);
    CHECK(strncmp(g_lastres, "[wifi] result=pass", 18) == 0);
    CHECK(g_fs.send_busy_returns > 0u);
    CHECK_EQ(g_fs.send_ok, 12);                          /* reset と boot は送らない。at echo gmr mode qap lapopt scan join ip dns ping_ip ping_dns */
    check_lines_clean();

    TEST("B3 ESP32 が無応答: AT を 5 回送って result=fail stage=at。join 以降は送らない");
    reset_all(); C.esp = false; C.stop_on_result = true; C.stop_tick = 3000u; C.cmds = k_d_at20; C.n_cmds = 1u;
    run();
    CHECK(g_result_seen);
    CHECK(strncmp(g_lastres, "[wifi] result=fail stage=at ", 28) == 0);
    CHECK_EQ(count_prefix("[wifi] step=at res=timeout "), 5);
    CHECK_EQ(g_fs.send_ok, 5);
    CHECK(!tx_has("CWJAP"));
    CHECK_EQ(count_prefix("[wifi] step=boot res=timeout "), 1);   /* ready が無くても at へ進む */
    check_lines_clean();

    TEST("B4 n(reset〜at)と p(at だけ。EN は動かさない)");
    reset_all(); C.stop_on_result = true; C.stop_tick = 3000u;
    { static const cmdev_t c[] = { { 20u, 'p' } }; C.cmds = c; C.n_cmds = 1u; run(); }
    CHECK(strncmp(g_lastres, "[wifi] result=pass", 18) == 0);
    CHECK_EQ(count_prefix("[wifi] step="), 2);
    CHECK(strcmp(g_gpio_ev, "W1;D;") == 0);              /* EN を動かしていない */
    CHECK_EQ(g_fs.send_ok, 1);
    reset_all(); C.stop_on_result = true; C.stop_tick = 3000u;
    { static const cmdev_t c[] = { { 20u, 'n' } }; C.cmds = c; C.n_cmds = 1u; run(); }
    CHECK(strncmp(g_lastres, "[wifi] result=pass", 18) == 0);
    CHECK_EQ(count_prefix("[wifi] step="), 6);           /* reset boot at の start + ok */
    CHECK(strcmp(g_gpio_ev, "W1;D;W0;W1;") == 0);

    TEST("B5 コマンド: 実行中の d n p x は無視、s ? は応答、+ - は無視、実行後はもう一度 d できる");
    reset_all(); C.stop_tick = 4000u;
    { static const cmdev_t c[] = { { 20u, 'd' }, { 21u, 'd' }, { 22u, 'n' }, { 23u, 'p' }, { 24u, 'x' }, { 25u, '+' }, { 26u, '-' }, { 27u, 's' }, { 28u, '?' },
                                   { 3000u, 's' }, { 3010u, 'd' } };
      C.cmds = c; C.n_cmds = sizeof(c) / sizeof(c[0]); run(); }
    CHECK_EQ(count_prefix("[wifi] step=reset res=start"), 2);   /* 20 と 3010 の d だけ */
    CHECK_EQ(count_prefix("[wifi] result="), 2);
    k = find_idx("[wifi] state=");
    CHECK(k >= 0);
    CHECK(strncmp(g_logs[k], "[wifi] state=run step=boot bytes=0", 33) == 0);    /* 27 tick: EN リセットの後、ready 待ち */
    CHECK(strncmp(g_logs[k + 1], "[st] ", 5) == 0);
    CHECK_EQ(count_prefix("[help] d=run n=boot p=at s=state ?=help\r\n"), 1);
    CHECK_EQ(count_prefix("[wifi] state=done step=none "), 1);
    check_lines_clean();
#endif

#if !APP_WIFI_AUTOSTART
    TEST("F1 d の後に x: wx_mux〜wx_recv が ok、result=pass、[stk] res=ok(sym=1・src=uart・jst)、s に [stk] net= と sym= 3 本(時計)。本文・要求・Date は出ない");
    reset_all(); C.stop_tick = 1500u;
    { static const cmdev_t c[] = { { 20u, 'd' }, { 700u, 'x' }, { 1200u, 's' } }; C.cmds = c; C.n_cmds = 3u; run(); }
    CHECK_EQ(count_prefix("[wifi] result="), 2);
    CHECK_EQ(count_prefix("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n") >= 1u, 1);    /* x の結果(ping は取得の範囲に無いので 0 / 0) */
    {
        static const char *const nm[] = { "wx_mux", "wx_close", "wx_tcp", "wx_send", "wx_recv" };
        char p[64];
        for (k = 0; k < 5; k++) {
            snprintf(p, sizeof(p), "[wifi] step=%s res=start ", nm[k]); CHECK_EQ(count_prefix(p), 1);
            snprintf(p, sizeof(p), "[wifi] step=%s res=ok try=1 ", nm[k]); CHECK_EQ(count_prefix(p), 1);
        }
    }
    CHECK(tx_has("AT+CIPMUX=0\r\n") && tx_has("AT+CIPCLOSE\r\n") && tx_has("AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n") && tx_has("AT+CIPSEND=145\r\n"));
    CHECK(tx_has("GET /v8/finance/chart/%5EN225?range=2d&interval=1d HTTP/1.0\r\nHost: query1.finance.yahoo.com\r\nUser-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n"));
    CHECK(tx_has("Connection: close\r\n\r\n") && !tx_has("Connection: close\r\n\r\n\r\n"));            /* 要求の後に \r\n を足さない */
    k = find_idx("[stk] res=ok sym=1 src=uart p100=5795384 c100=5749916 bp=79 mkt=t http=200 ms=");
    CHECK(k >= 0);
    if (k >= 0) {
        CHECK(strstr(g_logs[k], " sync=1 jst=132413 len=") != NULL); CHECK(strstr(g_logs[k], " closed=1\r\n") != NULL);
        CHECK(k > find_idx("[wifi] step=wx_recv res=ok "));
        CHECK(field(g_logs[k], " ms=") > 200u); CHECK(field(g_logs[k], " ms=") < 5000u);
        CHECK(field(g_logs[k], " len=") > 1200u); CHECK(field(g_logs[k], " len=") < 2000u);
    }
    CHECK_EQ(count_prefix("[stk] res="), 1);
    k = find_idx("[stk] net=");
    CHECK(k >= 0);
    if (k >= 0) {
        unsigned hh, mm, ss;
        const char *p = strstr(g_logs[k], " clk=");
        long expect_s, got_s;

        CHECK(starts(g_logs[k], "[stk] net=on busy=0 have=2 stale=0 clk="));          /* 銘柄 1 だけ取れた */
        CHECK(strstr(g_logs[k], " upd=1324\r\n") != NULL);
        CHECK(p != NULL);
        if (p != NULL) {
            hh = (unsigned)(p[5] - '0') * 10u + (unsigned)(p[6] - '0'); mm = (unsigned)(p[7] - '0') * 10u + (unsigned)(p[8] - '0'); ss = (unsigned)(p[9] - '0') * 10u + (unsigned)(p[10] - '0');
            got_s = (long)(hh * 3600u + mm * 60u + ss);
            /* ソフト時計 = 13:24:13 + (s を受けた時刻 − 最初のデータの時刻 + 700ms) / 1000(10ms tick の誤差 ±1 秒を許す) */
            expect_s = (13L * 3600L + 24L * 60L + 13L) + (long)(((1200u * 10u) - wifi_core_wx(&s_core)->first_ms + WX_SYNC_BIAS_MS) / 1000u);
            if ((got_s < (expect_s - 1)) || (got_s > (expect_s + 1))) { printf("  clk=%ld expect=%ld\n", got_s, expect_s); }
            CHECK(got_s >= (expect_s - 1)); CHECK(got_s <= (expect_s + 1));
            CHECK(got_s > (13L * 3600L + 24L * 60L + 13L + 3L));                                           /* 時計は進んでいる */
        }
        CHECK(k + 3 < (int)g_nlogs);
        CHECK(starts(g_logs[k + 1], "[stk] sym=0 have=0 p100=0 d100=0 bp=0 dir=f mkt=n age=-1\r\n"));
        CHECK(starts(g_logs[k + 2], "[stk] sym=1 have=1 p100=5795384 d100=45468 bp=79 dir=u mkt=t age="));
        CHECK(starts(g_logs[k + 3], "[stk] sym=2 have=0 "));
    }
    check_lines_clean();
    CHECK_EQ(g_log_drops, 0);
    CHECK_EQ(g_app_dbg.scr, 0);

    TEST("F2 x だけ(d なし。EN に触れず、reset は走らない)・x の連続(2 回とも取得して [stk] res=ok が 2 行)");
    reset_all(); C.stop_tick = 1300u;
    { static const cmdev_t c[] = { { 20u, 'x' }, { 400u, 'x' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    CHECK_EQ(count_prefix("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"), 2);
    CHECK_EQ(count_prefix("[wifi] step=reset"), 0);
    CHECK(strcmp(g_gpio_ev, "W1;D;") == 0);
    CHECK_EQ(count_prefix("[stk] res=ok sym=1 src=uart p100=5795384 c100=5749916 bp=79 mkt="), 2);
    CHECK_EQ(g_fs.send_ok, 10);                                                                     /* 5 回の送信 x 2 */
    check_lines_clean();
    CHECK_EQ(g_log_drops, 0);

    TEST("F3 実行中の x は無視(d の最中は取得を始めない・何も出さない)");
    reset_all(); C.stop_tick = 250u;
    { static const cmdev_t c[] = { { 20u, 'd' }, { 30u, 'x' }, { 40u, 'x' } }; C.cmds = c; C.n_cmds = 3u; run(); }
    CHECK_EQ(count_prefix("[wifi] step=wx_"), 0);
    CHECK_EQ(count_prefix("[stk] "), 0);
    CHECK(!tx_has("CIPSTART"));

    TEST("F4 TCP が張れない(DNS Fail): result=fail stage=wx_tcp と [stk] res=fail err=1。s の net= は err・have=0");
    reset_all(); g_tcp_fail = true; C.stop_tick = 600u;
    { static const cmdev_t c[] = { { 20u, 'x' }, { 400u, 's' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    CHECK_EQ(count_prefix("[wifi] result=fail stage=wx_tcp ping_ip=0 ping_dns=0\r\n"), 1);
    k = find_idx("[stk] res=fail sym=1 src=uart err=1 http=0 ms=");
    CHECK(k >= 0);
    CHECK_EQ(count_prefix("[stk] res="), 1);
    CHECK_EQ(count_prefix("[stk] net=err busy=0 have=0 stale=0 clk=none upd=none\r\n"), 1);
    CHECK(!tx_has("CIPSEND"));
    check_lines_clean();

    TEST("F5 応答が無い(CLOSED だけ): err=3。時刻は同期しない");
    reset_all(); g_tcp_nodata = true; C.stop_tick = 600u;
    { static const cmdev_t c[] = { { 20u, 'x' }, { 400u, 's' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    CHECK(find_idx("[stk] res=fail sym=1 src=uart err=3 http=0 ms=") >= 0);
    CHECK_EQ(count_prefix("[stk] net=err busy=0 have=0 stale=0 clk=none upd=none\r\n"), 1);
    check_lines_clean();

    TEST("F6 TLS の検証(design.md §5.5.2): t y g z は取得の接続先を選ぶ。y = SSL 443・CIPSEND=145・^N225 の chart・応答は ok。t = 平文 80 で 301(err=4 http=301)、g = chart のキーが無い 200(err=5)。x は最後の対象でもう一度");
    reset_all(); C.stop_tick = 700u;
    { static const cmdev_t c[] = { { 20u, 'y' }, { 400u, 'x' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    CHECK(tx_has("AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n") && tx_has("AT+CIPSEND=145\r\n"));
    CHECK(tx_has("GET /v8/finance/chart/%5EN225?range=2d&interval=1d HTTP/1.0\r\nHost: query1.finance.yahoo.com\r\nUser-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n"));
    CHECK(!tx_has("api.open-meteo.com") && !tx_has("CIPSSLCCONF") && !tx_has("\"TCP\""));
    CHECK_EQ(count_prefix("[stk] res=ok sym=1 src=uart p100=5795384 "), 2);                        /* y と、x(最後に選んだ対象でもう一度) */
    CHECK_EQ(count_prefix("[stk] res=fail"), 0);
    check_lines_clean();
    CHECK_EQ(g_log_drops, 0);

    reset_all(); C.stop_tick = 500u;
    { static const cmdev_t c[] = { { 20u, 't' } }; C.cmds = c; C.n_cmds = 1u; run(); }
    CHECK(tx_has("AT+CIPSTART=\"TCP\",\"query1.finance.yahoo.com\",80\r\n") && tx_has("AT+CIPSEND=145\r\n") && !tx_has("\"SSL\""));
    CHECK_EQ(count_prefix("[stk] res=fail sym=1 src=uart err=4 http=301 ms="), 1);

    reset_all(); C.stop_tick = 500u;
    { static const cmdev_t c[] = { { 20u, 'g' } }; C.cmds = c; C.n_cmds = 1u; run(); }
    CHECK(tx_has("AT+CIPSTART=\"SSL\",\"www.google.com\",443\r\n") && tx_has("GET / HTTP/1.0\r\nHost: www.google.com\r\nUser-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n"));
    CHECK_EQ(count_prefix("[stk] res=fail sym=1 src=uart err=5 http=200 ms="), 1);
    check_lines_clean();

    reset_all(); C.stop_tick = 500u;
    { static const cmdev_t c[] = { { 20u, 'z' } }; C.cmds = c; C.n_cmds = 1u; run(); }
    CHECK(tx_has("AT+CIPCLOSE\r\nAT+CIPSSLCCONF=0,0,0\r\nAT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n"));
    CHECK_EQ(count_prefix("[wifi] step=wx_sslconf res=ok "), 1);
    CHECK_EQ(count_prefix("[stk] res=ok sym=1 src=uart p100=5795384 "), 1);
    CHECK_EQ(g_app_dbg.scr, 0);

    reset_all(); C.stop_tick = 300u;                                   /* 実行中(d の最中)の t y g z は無視。何も送らない */
    { static const cmdev_t c[] = { { 20u, 'd' }, { 30u, 't' }, { 31u, 'y' }, { 32u, 'g' }, { 33u, 'z' } }; C.cmds = c; C.n_cmds = 5u; run(); }
    CHECK(!tx_has("CIPSTART") && !tx_has("CIPSSLCCONF"));
    CHECK_EQ(count_prefix("[stk] "), 0);

    TEST("C1 受信は 1 周回に 64 バイトまで(200 バイトは 64 / 128 / 192 / 200)");
    reset_all(); C.esp = false;
    for (i = 0u; i < 200u; i++) { fake_sci_rx_put((uint8_t)'x'); }
    C.rx_probe_calls = 6u;
    run();
    CHECK_EQ(g_probe[1], 0);
    CHECK_EQ(g_probe[2], 64);
    CHECK_EQ(g_probe[3], 128);
    CHECK_EQ(g_probe[4], 192);
    CHECK_EQ(g_probe[5], 200);
    CHECK_EQ(g_probe[6], 200);

    TEST("C2 SCI1 の受信異常: [err] code=10 detail=8 は 1 秒に 1 行まで。RX_CHAR は無視。g_app_dbg.err_code は変えない");
    reset_all(); C.esp = false; C.stop_tick = 400u;
    C.cb_ticks[0] = 50u; C.cb_ticks[1] = 60u; C.cb_ticks[2] = 100u; C.n_cb = 3u;
    run();
    CHECK_EQ(count_prefix("[err] code=10 detail=8\r\n"), 2);  /* 50 で 1 行。60・100 の分は 150(=50+100)以降に 1 行 */
    CHECK_EQ(count_prefix("[err] "), 2);
    CHECK_EQ(g_app_dbg.err_code, 0);
    k = find_idx("[err] ");
    CHECK(k >= 0);

    TEST("C3 SCI1 を開けない: [init] ok=0 err=9 detail=<sci_err_t> と [err] code=9。コマンドは無視、hb は続く、s は idle");
    reset_all(); C.esp = false; C.stop_tick = 300u;
    g_fs.open_ret = SCI_ERR_BAD_CHAN;
    { static const cmdev_t c[] = { { 20u, 'd' }, { 21u, 'n' }, { 22u, 'p' }, { 23u, 's' } }; C.cmds = c; C.n_cmds = 4u; run(); }
    { char exp[80]; snprintf(exp, sizeof(exp), "[init] stage=wifi ok=0 err=9 detail=%d\r\n", (int)SCI_ERR_BAD_CHAN); CHECK(strcmp(g_logs[0], exp) == 0);
      snprintf(exp, sizeof(exp), "[err] code=9 detail=%d\r\n", (int)SCI_ERR_BAD_CHAN); CHECK(strcmp(g_logs[1], exp) == 0); }
    CHECK_EQ(count_prefix("[wifi] step="), 0);
    CHECK_EQ(g_fs.send_calls, 0);
    CHECK_EQ(g_fs.recv_calls, 0);
    CHECK(strcmp(g_gpio_ev, "W1;D;") == 0);
    CHECK(count_prefix("[hb] ") >= 2u);
    CHECK_EQ(count_prefix("[wifi] state=idle step=none bytes=0"), 1);

    TEST("C4 タイマが作れない: [init] stage=timer ok=0 err=4 と [err] code=4、止まる");
    reset_all(); C.esp = false; g_cmt_ok = false; C.max_iter = 100u;
    run();
    CHECK(!g_timer_on);
    CHECK_EQ(count_prefix("[init] stage=timer ok=0 err=4 detail=0\r\n"), 1);
    CHECK_EQ(count_prefix("[err] code=4 detail=0\r\n"), 1);
    CHECK_EQ(g_app_dbg.err_code, 4);
#endif

#if APP_WIFI_AUTOSTART
    TEST("D1 自動開始: 起動から 2 秒後(tick 200)に d と同じを 1 回だけ。完了後も繰り返さない");
    reset_all(); C.stop_on_result = true; C.stop_tick = 3000u;
    run();
    CHECK(g_result_seen);
    CHECK_EQ(count_prefix("[wifi] step=reset res=start"), 1);
    CHECK(strncmp(g_lastres, "[wifi] result=pass", 18) == 0);
    reset_all(); C.esp = false; C.stop_tick = 190u;
    run();
    CHECK_EQ(count_prefix("[wifi] "), 0);                /* 2 秒前は何もしない */
    reset_all(); C.stop_tick = 205u;
    run();
    CHECK_EQ(count_prefix("[wifi] step=reset res=start"), 1);
    reset_all(); C.stop_tick = 2500u; C.esp = false;
    run();
    CHECK_EQ(count_prefix("[wifi] step=reset res=start"), 1);   /* 無応答で失敗に終わった後も、もう一度は始めない */
    CHECK_EQ(count_prefix("[wifi] result=fail stage=at"), 1);
#endif

#ifdef T_NOSECRET
    TEST("E1 秘密なし: join は skip して result=pass。CWJAP は送らない");
    reset_all(); C.stop_on_result = true; C.stop_tick = 3000u; C.cmds = k_d_at20; C.n_cmds = 1u;
    run();
    CHECK(strncmp(g_lastres, "[wifi] result=pass", 18) == 0);
    CHECK_EQ(count_prefix("[wifi] step=join res=skip "), 1);
    CHECK_EQ(count_prefix("[wifi] step=mode res=ok "), 1);
    CHECK(!tx_has("CWJAP"));
    CHECK_EQ(count_prefix("[wifi] step=ping_ip"), 0);
#endif

#endif /* APP_WIFI_TEST */

#if APP_WX_PRODUCT
#ifndef T_NOSECRET
    TEST("P1 起動: SCI1 を 1 回開く(115200・8N1・優先度 2)。EN は先に High → 出力。3 秒待つ間は何も送らない・何も出さない。表示値は接続中・未同期(時計・更新時刻 0xFFFF)");
    reset_all(); C.stop_tick = 250u;
    run();
    CHECK_EQ(g_fs.open_calls, 1);
    CHECK_EQ(g_fs.pinset_at_open, 1);
    CHECK_EQ(g_fs.open_chan, 1);
    CHECK_EQ(g_fs.open_cfg.async.baud_rate, 115200);
    CHECK_EQ(g_fs.open_cfg.async.int_priority, 2);
    CHECK(strcmp(g_gpio_ev, "W1;D;") == 0);
    CHECK_EQ(g_fs.send_calls, 0);
    CHECK_EQ(g_nlogs, 0);
    CHECK_EQ(g_v.net, STK_NET_CONN);
    CHECK_EQ(g_v.busy, 0); CHECK_EQ(g_v.have_any, 0); CHECK_EQ(g_v.stale_all, 0); CHECK_EQ(g_v.partial, 0);
    CHECK_EQ(g_v.clock_min, 0xFFFF); CHECK_EQ(g_v.upd_min, 0xFFFF);
    CHECK_EQ(g_v.row[0].have, 0); CHECK_EQ(g_v.row[1].have, 0); CHECK_EQ(g_v.row[2].have, 0);
    CHECK(!g_v_busy_seen);

    TEST("P2 起動 3 秒後に接続(reset〜ip の 8 step)→ すぐ最初の周期(天気 1 回 + 3 銘柄 x 5 step)。res=start・res=ok・key=oth は出ない。[wx] res=ok src=boot が [stk] res=ok sym=0 より前(時刻と天気が先にそろう)。[stk] res=ok sym=0,1,2 src=boot・cycle・net= 3 行。時計・表示値・時計・天気画面の値。秘密と本文は出ない");
    reset_all(); C.stop_tick = 2400u;
    run();
    dump_logs();
    CHECK(strcmp(g_gpio_ev, "W1;D;W0;W1;") == 0);
    CHECK_EQ(count_prefix("[wifi] step="), 0);                                       /* 失敗がないので、step の行は 1 本も出ない */
    for (i = 0u; i < g_nlogs; i++) { CHECK(strstr(g_logs[i], " res=start") == NULL); CHECK(strstr(g_logs[i], "key=oth") == NULL); }
    CHECK_EQ(count_prefix("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"), 1 + 4);   /* 接続 1 回と、天気 + 3 銘柄の取得 */
    CHECK_EQ(count_prefix("[wifiv] "), 0);                                           /* 製品は [wifiv] を 1 行も出さない(IP アドレスを UART に出さない。design.md §7.5・§7.8) */
    CHECK(!tx_has("AT+GMR") && !tx_has("AT+CWLAP") && !tx_has("AT+PING") && !tx_has("AT+CIPDOMAIN") && !tx_has("CWLAPOPT"));   /* 製品の接続は gmr lapopt scan dns ping を省く */
    CHECK(tx_has("AT+CWQAP\r\n") && tx_has("AT+CWJAP=\"SSID_SENTINEL_7c1\",\"PW_SENTINEL_9f3\"\r\n") && tx_has("AT+CIFSR\r\n"));
    CHECK_EQ(tx_count("AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n"), 3);   /* 銘柄ごとに TCP/SSL を開く */
    CHECK_EQ(tx_count("AT+CIPCLOSE\r\n"), 4); CHECK_EQ(tx_count("AT+CIPMUX=0\r\n"), 4);
    CHECK(tx_has("AT+CIPSEND=144\r\n") && tx_has("AT+CIPSEND=145\r\n") && tx_has("AT+CIPSEND=195\r\n"));
    CHECK(!tx_has("CIPSSLCCONF"));
    CHECK_EQ(tx_count("AT+CIPSTART=\"TCP\",\"api.open-meteo.com\",80\r\n"), 1);                  /* 天気は平文 80(1 周期に 1 回。最初) */
    CHECK_EQ(tx_count("\"TCP\""), 1);
    CHECK(tx_has("GET /v1/forecast?latitude=35.68&longitude=139.76&current=temperature_2m,weather_code&timezone=Asia%2FTokyo HTTP/1.0\r\nHost: api.open-meteo.com\r\nUser-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n"));
    CHECK_EQ(tx_count("GET /v8/finance/chart/6723.T?range=2d"), 1); CHECK_EQ(tx_count("GET /v8/finance/chart/%5EN225?range=2d"), 1); CHECK_EQ(tx_count("GET /v8/finance/chart/%5EGSPC?range=2d"), 1);
    {
        int a0 = find_idx("[stk] res=ok sym=0 src=boot p100=243300 c100=237800 bp=231 mkt=t http=200 ms=");
        int a1 = find_idx("[stk] res=ok sym=1 src=boot p100=5795384 c100=5749916 bp=79 mkt=t http=200 ms=");
        int a2 = find_idx("[stk] res=ok sym=2 src=boot p100=718278 c100=720109 bp=-25 mkt=c http=200 ms=");

        CHECK(a0 >= 0); CHECK(a1 > a0); CHECK(a2 > a1);                               /* 6723.T → ^N225 → ^GSPC の順 */
        if (a0 >= 0) { CHECK(strstr(g_logs[a0], " sync=1 jst=132413 len=") != NULL); CHECK(strstr(g_logs[a0], " closed=1\r\n") != NULL); }
    }
    CHECK_EQ(count_prefix("[stk] res="), 3);
    {
        int w = find_idx("[wx] res=ok src=boot temp10=129 wmo=61 kind=rain http=200 ms=");

        CHECK(w >= 0);
        CHECK(w < find_idx("[stk] res=ok sym=0 src=boot "));                                 /* 天気が最初 */
        if (w >= 0) {
            CHECK(strstr(g_logs[w], " sync=1 jst=132413 obs=1315 len=") != NULL); CHECK(strstr(g_logs[w], " closed=1\r\n") != NULL);
            CHECK(field(g_logs[w], " ms=") > 200u); CHECK(field(g_logs[w], " ms=") < 5000u);
            CHECK(field(g_logs[w], " len=") > 300u); CHECK(field(g_logs[w], " len=") < 1000u);
        }
    }
    CHECK_EQ(count_prefix("[wx] "), 1);                                                  /* 天気は [wx] 1 行だけ([stk] には出ない) */
    CHECK_EQ(count_prefix("[stk] cycle=1 ok=7 fail=0 ms="), 1);
    CHECK_EQ(count_prefix("[stk] cycle="), 1);
    CHECK(find_idx("[stk] cycle=") > find_idx("[stk] res=ok sym=2 "));
    CHECK_EQ(count_prefix("[stk] net="), 3);
    {
        int a = find_idx("[stk] net=conn busy=1 have=0 stale=0 clk=none upd=none\r\n");
        int b = find_idx("[stk] net=on busy=1 have=0 ");
        int c = find_idx("[stk] net=on busy=0 have=7 stale=0 clk=");
        CHECK(a >= 0); CHECK(b > a); CHECK(c > b);
        CHECK(a < find_idx("[wifi] result=pass "));
        CHECK(c > find_idx("[stk] res=ok sym=2 "));
        CHECK(strstr(g_logs[c], " upd=1324\r\n") != NULL);
    }
    CHECK_EQ(g_v.net, STK_NET_ON); CHECK_EQ(g_v.busy, 0); CHECK_EQ(g_v.have_any, 1); CHECK_EQ(g_v.stale_all, 0); CHECK_EQ(g_v.partial, 0);
    CHECK_EQ(g_v.row[0].have, 1); CHECK_EQ(g_v.row[0].price_x100, 243300); CHECK_EQ(g_v.row[0].diff_x100, 5500); CHECK_EQ(g_v.row[0].pct_bp, 231); CHECK_EQ(g_v.row[0].mkt, STK_MKT_TRADING);
    CHECK_EQ(g_v.row[1].have, 1); CHECK_EQ(g_v.row[1].price_x100, 5795384); CHECK_EQ(g_v.row[1].diff_x100, 45468); CHECK_EQ(g_v.row[1].pct_bp, 79); CHECK_EQ(g_v.row[1].mkt, STK_MKT_TRADING);
    CHECK_EQ(g_v.row[2].have, 1); CHECK_EQ(g_v.row[2].price_x100, 718278); CHECK_EQ(g_v.row[2].diff_x100, -1831); CHECK_EQ(g_v.row[2].pct_bp, -25); CHECK_EQ(g_v.row[2].mkt, STK_MKT_CLOSED);
    CHECK_EQ(g_v.row[0].stale, 0); CHECK_EQ(g_v.row[0].fail, 0);
    CHECK(g_v.clock_min >= 13 * 60 + 24); CHECK(g_v.clock_min <= 13 * 60 + 25);
    CHECK_EQ(g_v.upd_min, 13 * 60 + 24);
    CHECK(g_v_busy_seen); CHECK(g_v_net_seen[STK_NET_CONN] > 0u); CHECK(g_v_net_seen[STK_NET_ON] > 0u); CHECK_EQ(g_v_net_seen[STK_NET_ERR], 0);
    CHECK(!g_v_partial_seen);
    /* 時計・天気画面の値(clk_ui)。時刻と天気は、株価が 1 つでも出るより前にそろう */
    CHECK(g_c_sync_tick > 0u); CHECK(g_c_have_tick > 0u); CHECK(g_v_have_tick > 0u);
    CHECK(g_c_sync_tick < g_v_have_tick); CHECK(g_c_have_tick < g_v_have_tick);
    CHECK_EQ(g_c.have, 1); CHECK_EQ(g_c.kind, CLK_RAIN); CHECK_EQ(g_c.temp, 13);         /* 12.9℃ → 13 */
    CHECK(g_c.clock_min >= 13 * 60 + 24); CHECK(g_c.clock_min <= 13 * 60 + 25);
    CHECK_EQ(g_c.mon, 11); CHECK_EQ(g_c.day, 25); CHECK_EQ(g_c.wday, 3);                 /* 2026-11-25 は水曜 */
    CHECK_EQ(g_log_drops, 0);
    CHECK(g_max_tick_lines <= 6u);                                                   /* ログのキュー(8 行)に対して 1 tick の行数に余裕 */
    CHECK_EQ(count_prefix("[err] "), 0);
    CHECK_EQ(count_prefix("[hb] "), 0);                                              /* hb は app_main が出す */
    check_lines_clean();

    TEST("P3 定期取得: 周期の開始から 10 分(60000 tick ± 3)で 1 周期(天気 + 3 銘柄)。src=timer。その間 [wifi] [stk] [wx] の行は増えない。周期はずれない");
    reset_all(); C.stop_tick = 61800u;
    run();
    CHECK_EQ(g_n_cipstart, 8);
    for (i = 0u; i < 4u; i++) { CHECK(g_cipstart_tick[4u + i] - g_cipstart_tick[i] >= 59997u); CHECK(g_cipstart_tick[4u + i] - g_cipstart_tick[i] <= 60003u); }
    CHECK_EQ(count_prefix("[stk] res=ok sym="), 6);
    CHECK_EQ(count_prefix("[stk] res=ok sym=0 src=boot "), 1); CHECK_EQ(count_prefix("[stk] res=ok sym=0 src=timer "), 1);
    CHECK_EQ(count_prefix("[stk] res=ok sym=2 src=timer "), 1);
    CHECK_EQ(count_prefix("[stk] cycle="), 2);
    CHECK_EQ(count_prefix("[stk] cycle=2 ok=7 fail=0 ms="), 1);
    CHECK_EQ(count_prefix("[wx] res=ok src=boot "), 1); CHECK_EQ(count_prefix("[wx] res=ok src=timer "), 1); CHECK_EQ(count_prefix("[wx] "), 2);
    CHECK_EQ(count_prefix("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"), 1 + 4 + 4);   /* 2 回目は取得だけ(再接続しない) */
    CHECK_EQ(count_prefix("[stk] net="), 3 + 2);                                     /* 周期ごとに busy 1 → 0 */
    CHECK_EQ(g_v_net_seen[STK_NET_ERR], 0);
    CHECK_EQ(g_log_drops, 0);
    check_lines_clean();

    TEST("P4 更新(x = src=uart、t = src=touch): 取得中は req=busy、周期の開始から 5 秒未満は req=ignored、5 秒後は 天気 + 3 銘柄を取る(更新の周期にも天気が入る)→ 時計を Date: で合わせ直す。s は net= と sym= 3 本と [wx] have= 1 本");
    reset_all(); C.stop_tick = 5200u;
    { static const cmdev_t c[] = { { 2500u, 'x' }, { 2501u, 'x' }, { 2502u, 'a' }, { 2800u, 'x' }, { 3200u, 'x' }, { 4200u, 't' }, { 4900u, 's' } };
      C.cmds = c; C.n_cmds = sizeof(c) / sizeof(c[0]); run(); }
    CHECK_EQ(count_prefix("[stk] res=ok sym=0 src=boot "), 1);
    CHECK_EQ(count_prefix("[stk] res=ok sym=0 src=uart "), 2);                       /* 2500 の x と 3200 の x(2800 は ignored) */
    CHECK_EQ(count_prefix("[stk] res=ok sym=2 src=uart "), 2);
    CHECK_EQ(count_prefix("[stk] res=ok sym=0 src=touch "), 1);
    CHECK_EQ(count_prefix("[stk] req=busy src=uart\r\n"), 1);                        /* 2501: 取得中 */
    CHECK_EQ(count_prefix("[stk] req=ignored src=uart\r\n"), 1);                     /* 2800: 2500 に始めた周期から 3 秒 */
    CHECK_EQ(count_prefix("[stk] req="), 2);
    CHECK_EQ(g_n_req, 5);
    CHECK_EQ(g_req_ret[0], WX_REQ_OK); CHECK_EQ(g_req_ret[1], WX_REQ_BUSY); CHECK_EQ(g_req_ret[2], WX_REQ_IGNORED); CHECK_EQ(g_req_ret[3], WX_REQ_OK); CHECK_EQ(g_req_ret[4], WX_REQ_OK);
    CHECK_EQ(g_n_cipstart, 16);                                                      /* boot + 2 uart + touch の 4 周期 x(天気 + 3 銘柄)(2800 の x は取得しない) */
    CHECK_EQ(count_prefix("[wx] res=ok src=boot "), 1); CHECK_EQ(count_prefix("[wx] res=ok src=uart "), 2); CHECK_EQ(count_prefix("[wx] res=ok src=touch "), 1);
    CHECK_EQ(count_prefix("[wx] res="), 4); CHECK_EQ(count_prefix("[wx] have="), 1);
    CHECK_EQ(count_prefix("[stk] cycle="), 4);
    CHECK_EQ(tx_count("AT+CWQAP\r\n"), 1);                                           /* 再接続はしない */
    {
        unsigned n40 = 0u;
        for (i = 0u; i < g_nlogs; i++) { if ((strncmp(g_logs[i], "[stk] res=ok ", 13) == 0) && (strstr(g_logs[i], "jst=1340") != NULL || strstr(g_logs[i], "jst=1341") != NULL)) { n40++; } }
        CHECK_EQ(n40, 9u);                                                           /* 'a' の後の 3 周期(9 回)の Date: は 04:40:50 UTC から実時間どおり進む → 13:40:50 JST 以降(13:40〜13:41 台) */
    }
    {
        int last = -1;
        for (i = 0u; i < g_nlogs; i++) { if (strncmp(g_logs[i], "[stk] net=on busy=0 ", 20) == 0) { last = (int)i; } }
        CHECK(last >= 0);
        if (last >= 0) {                                                             /* s(4900)の応答: 時計は 13:40:50 + (4900 - 最後の取得)の数秒 = 13:40:5x〜13:41:0x。再同期しなければ 13:24 台のまま */
            const char *p = strstr(g_logs[last], " clk=");
            unsigned hh = (unsigned)(p[5] - '0') * 10u + (unsigned)(p[6] - '0'), mm = (unsigned)(p[7] - '0') * 10u + (unsigned)(p[8] - '0');
            CHECK_EQ(hh, 13u); CHECK(mm == 40u || mm == 41u);
            CHECK(starts(g_logs[last], "[stk] net=on busy=0 have=7 stale=0 "));
            CHECK(strstr(g_logs[last], " upd=1340\r\n") != NULL || strstr(g_logs[last], " upd=1341\r\n") != NULL);
            CHECK(last + 3 < (int)g_nlogs);
            CHECK(starts(g_logs[last + 1], "[stk] sym=0 have=1 p100=243300 d100=5500 bp=231 dir=u mkt=t age="));
            CHECK(starts(g_logs[last + 2], "[stk] sym=1 have=1 p100=5795384 d100=45468 bp=79 dir=u mkt=t age="));
            CHECK(starts(g_logs[last + 3], "[stk] sym=2 have=1 p100=718278 d100=-1831 bp=-25 dir=d mkt=c age="));
            CHECK(last + 4 < (int)g_nlogs);
            CHECK(starts(g_logs[last + 4], "[wx] have=1 temp10=129 wmo=61 kind=rain age="));
        }
    }
    CHECK_EQ(g_v.clock_min >= 13 * 60 + 40, 1);
    CHECK_EQ(g_log_drops, 0);
    CHECK(g_max_tick_lines <= 6u);
    check_lines_clean();

    TEST("P5 古い値: 最後の成功から 30 分以上で stale(行ごと・全体)。値は消えない。次の成功で戻る");
    reset_all(); C.stop_tick = 2400u;
    { static const cmdev_t c[] = { { 1800u, 'z' }, { 2000u, 'x' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    CHECK(g_v_stale_seen);
    CHECK_EQ(g_v.stale_all, 0);                                                      /* x の取得に成功して戻った */
    CHECK_EQ(g_v.row[0].stale, 0); CHECK_EQ(g_v.row[1].stale, 0); CHECK_EQ(g_v.row[2].stale, 0);
    CHECK_EQ(g_v.have_any, 1); CHECK_EQ(g_v.row[0].price_x100, 243300);
    reset_all(); C.stop_tick = 1900u;
    { static const cmdev_t c[] = { { 1800u, 'z' } }; C.cmds = c; C.n_cmds = 1u; run(); }
    CHECK_EQ(g_v.stale_all, 1); CHECK_EQ(g_v.have_any, 1); CHECK_EQ(g_v.row[0].price_x100, 243300); CHECK_EQ(g_v.net, STK_NET_ON);
    CHECK_EQ(g_v.row[0].stale, 1); CHECK_EQ(g_v.row[1].stale, 1); CHECK_EQ(g_v.row[2].stale, 1);
    CHECK_EQ(g_v.row[0].have, 1); CHECK_EQ(g_v.row[1].have, 1); CHECK_EQ(g_v.row[2].have, 1);

    TEST("P6 全部失敗(TCP が張れない): 3 銘柄とも [stk] res=fail err=1 → 20 秒後にやり直し(src=retry。2 回まで) → 周期の記録(ok=0 fail=7)→ 再接続(net=err)→ 直ったら新しい周期。値は残る");
    reset_all(); C.stop_tick = 9000u;
    { static const cmdev_t c[] = { { 1800u, 'f' }, { 2000u, 'x' }, { 6400u, 'g' } }; C.cmds = c; C.n_cmds = 3u; run(); }
    CHECK_EQ(count_prefix("[stk] res=fail sym=0 src=uart err=1 http=0 ms="), 1);
    CHECK_EQ(count_prefix("[stk] res=fail sym=1 src=uart err=1 http=0 ms="), 1);
    CHECK_EQ(count_prefix("[stk] res=fail sym=2 src=uart err=1 http=0 ms="), 1);
    CHECK_EQ(count_prefix("[stk] res=fail sym=0 src=retry err=1 http=0 ms="), 2);       /* 20 秒後に 2 回(2000 → 4000 → 6000 tick) */
    CHECK_EQ(count_prefix("[stk] res=fail sym=2 src=retry err=1 http=0 ms="), 2);
    CHECK_EQ(count_prefix("[stk] res=fail"), 9);
    CHECK_EQ(count_prefix("[wx] res=fail src=uart err=1 http=0 ms="), 1);                   /* 天気も同じ接続の失敗([stk] には出ない) */
    CHECK_EQ(count_prefix("[wx] res=fail src=retry err=1 http=0 ms="), 2);
    CHECK_EQ(count_prefix("[stk] cycle=2 ok=0 fail=7 ms="), 1);
    CHECK_EQ(tx_count("AT+CWQAP\r\n"), 2);                                              /* 3 周目に失敗が残ったので再接続 */
    CHECK_EQ(g_v_net_seen[STK_NET_ERR] > 0u, 1);
    CHECK(g_v_partial_seen);
    CHECK_EQ(g_v.have_any, 1); CHECK_EQ(g_v.row[0].price_x100, 243300);                 /* 失敗でも古い値を残す */
    CHECK_EQ(g_v.net, STK_NET_ON);                                                      /* g の後の取得に成功 */
    CHECK_EQ(g_v.partial, 0);
    CHECK_EQ(count_prefix("[stk] res=ok sym=0 src=timer "), 1);                         /* 再接続の後の周期(きっかけは timer) */
    CHECK_EQ(count_prefix("[stk] cycle=3 ok=7 fail=0 ms="), 1);
    check_lines_clean();

    TEST("P6b 1 銘柄(^N225)だけ失敗(404): 他の 2 銘柄は ok。20 秒後に失敗分だけやり直し(src=retry)・2 回。cycle ok=5 fail=2。表示値は partial=1・row1.fail=1・他は通常。net は on のまま。直れば次のやり直しで ok");
    reset_all(); C.stop_tick = 6000u;
    { static const cmdev_t c[] = { { 100u, 'k' } }; C.cmds = c; C.n_cmds = 1u; run(); }
    CHECK_EQ(count_prefix("[stk] res=ok sym=0 src=boot "), 1); CHECK_EQ(count_prefix("[stk] res=ok sym=2 src=boot "), 1);
    CHECK_EQ(count_prefix("[stk] res=fail sym=1 src=boot err=4 http=404 ms="), 1);
    CHECK_EQ(count_prefix("[stk] res=fail sym=1 src=retry err=4 http=404 ms="), 2);   /* 失敗した銘柄だけ・2 回 */
    CHECK_EQ(count_prefix("[stk] res=ok src=retry"), 0); CHECK_EQ(count_prefix("[stk] res=ok sym=0 src=retry"), 0); CHECK_EQ(count_prefix("[stk] res=ok sym=2 src=retry"), 0);
    CHECK_EQ(count_prefix("[stk] cycle=1 ok=5 fail=2 ms="), 1);
    CHECK_EQ(tx_count("AT+CWQAP\r\n"), 1);                                              /* 再接続しない */
    CHECK_EQ(g_v.net, STK_NET_ON); CHECK_EQ(g_v_net_seen[STK_NET_ERR], 0);
    CHECK_EQ(g_v.partial, 1); CHECK_EQ(g_v.row[1].fail, 1); CHECK_EQ(g_v.row[1].have, 0); CHECK_EQ(g_v.row[0].fail, 0); CHECK_EQ(g_v.row[2].fail, 0);
    CHECK_EQ(g_v.row[0].have, 1); CHECK_EQ(g_v.row[2].have, 1); CHECK_EQ(g_v.have_any, 1);
    CHECK(count_prefix("[stk] net=on busy=0 have=5 ") >= 1u);
    check_lines_clean();
    reset_all(); C.stop_tick = 6000u;                                                   /* 1 回目のやり直しの前に直す → やり直しで ok */
    { static const cmdev_t c[] = { { 100u, 'k' }, { 1500u, 'h' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    CHECK_EQ(count_prefix("[stk] res=fail sym=1 src=boot err=4 http=404 ms="), 1);
    CHECK_EQ(count_prefix("[stk] res=ok sym=1 src=retry "), 1);
    CHECK_EQ(count_prefix("[stk] res=fail"), 1);
    CHECK_EQ(count_prefix("[stk] cycle=1 ok=7 fail=0 ms="), 1);
    CHECK_EQ(g_v.partial, 0); CHECK_EQ(g_v.row[1].have, 1); CHECK_EQ(g_v.row[1].fail, 0); CHECK_EQ(g_v.row[1].price_x100, 5795384);

    TEST("P6c 天気だけ失敗(404): 株価 3 つは ok。[wx] res=fail src=boot err=4 http=404 → 20 秒後に src=retry で天気だけ 2 回。[stk] cycle= ok=7 fail=0。株価のフッター(partial)・net=err に出ない。時計・天気画面は「不明」(have=0・unknown)だが時刻は出る。直せば次のやり直しで [wx] res=ok");
    reset_all(); C.stop_tick = 6000u;
    { static const cmdev_t c[] = { { 100u, 'w' } }; C.cmds = c; C.n_cmds = 1u; run(); }
    CHECK_EQ(count_prefix("[wx] res=fail src=boot err=4 http=404 ms="), 1);
    CHECK_EQ(count_prefix("[wx] res=fail src=retry err=4 http=404 ms="), 2);
    CHECK_EQ(count_prefix("[wx] res=ok"), 0);
    CHECK_EQ(count_prefix("[wx] "), 3);
    CHECK_EQ(count_prefix("[stk] res=ok sym=0 src=boot "), 1); CHECK_EQ(count_prefix("[stk] res=ok sym=1 src=boot "), 1); CHECK_EQ(count_prefix("[stk] res=ok sym=2 src=boot "), 1);
    CHECK_EQ(count_prefix("[stk] res=fail"), 0);                                         /* 天気の失敗は [stk] に出ない */
    CHECK_EQ(count_prefix("[stk] cycle=1 ok=7 fail=0 ms="), 1);                          /* 周期の記録は株価の 3 ビットだけ */
    CHECK_EQ(g_v.partial, 0); CHECK(!g_v_partial_seen);                                  /* フッター「一部の銘柄を取得できませんでした」に出ない */
    CHECK_EQ(g_v.net, STK_NET_ON); CHECK_EQ(g_v_net_seen[STK_NET_ERR], 0);
    CHECK_EQ(g_v.have_any, 1); CHECK_EQ(g_v.row[0].fail, 0); CHECK_EQ(g_v.row[1].fail, 0); CHECK_EQ(g_v.row[2].fail, 0);
    CHECK_EQ(tx_count("AT+CWQAP\r\n"), 1);                                              /* 再接続しない */
    CHECK(g_c_sync_tick > 0u);                                                           /* 時刻は株価の Date: で分かる */
    CHECK_EQ(g_c.have, 0); CHECK_EQ(g_c.kind, CLK_UNKNOWN); CHECK_EQ(g_c.temp, 0);
    CHECK(g_c.clock_min != 0xFFFF); CHECK_EQ(g_c.mon, 11);
    check_lines_clean();
    reset_all(); C.stop_tick = 6000u;                                                    /* 1 回目のやり直しの前に直す → やり直しで ok */
    { static const cmdev_t c[] = { { 100u, 'w' }, { 1700u, 'v' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    CHECK_EQ(count_prefix("[wx] res=fail src=boot err=4 http=404 ms="), 1);
    CHECK_EQ(count_prefix("[wx] res=ok src=retry temp10=129 wmo=61 kind=rain http=200 ms="), 1);
    CHECK_EQ(count_prefix("[wx] "), 2);
    CHECK_EQ(count_prefix("[stk] cycle=1 ok=7 fail=0 ms="), 1);
    CHECK_EQ(g_c.have, 1); CHECK_EQ(g_c.kind, CLK_RAIN); CHECK_EQ(g_c.temp, 13); CHECK(g_c_have_tick > g_v_have_tick);

    TEST("P6d 天気の「古い」: 最後の成功から 60 分(3,600 秒)以上で時計・天気画面は「不明」(have=0)。s の [wx] have= は値と age を出す。株価の stale・net には影響しない");
    reset_all(); C.stop_tick = 2400u;
    { static const cmdev_t c[] = { { 1800u, 's' }, { 1900u, 'y' }, { 2000u, 's' } }; C.cmds = c; C.n_cmds = 3u; run(); }
    {
        int a = find_idx("[wx] have=1 temp10=129 wmo=61 kind=rain age=");
        unsigned i2, n_have = 0u;
        unsigned long age_first = 0u, age_second = 0u;

        for (i2 = 0u; i2 < g_nlogs; i2++) {
            if (strncmp(g_logs[i2], "[wx] have=1 ", 12) == 0) { unsigned long age = field(g_logs[i2], " age="); if (n_have == 0u) { age_first = age; } else { age_second = age; } n_have++; }
        }
        CHECK(a >= 0);
        CHECK_EQ(n_have, 2u);
        CHECK(age_first < 3600u);                                                        /* 1800 tick の s: 取得から数十秒 */
        CHECK(age_second >= 3600u);                                                      /* 2000 tick の s: 60 分 + 数十秒 */
        CHECK(age_second < 3700u);
    }
    CHECK_EQ(g_c.have, 0); CHECK_EQ(g_c.kind, CLK_UNKNOWN); CHECK_EQ(g_c.temp, 0);       /* 古い天気は出さない */
    CHECK(g_c.clock_min != 0xFFFF);
    CHECK_EQ(g_v.stale_all, 0); CHECK_EQ(g_v.net, STK_NET_ON);                           /* 株価は影響されない */
    check_lines_clean();

    TEST("P7 接続の失敗(join が FAIL): [wifi] result=fail stage=join、[stk] res=fail sym=0 src=boot err=8、net=err。60 秒後に再接続、直れば 3 銘柄を取って net=on");
    reset_all(); g_join_fail = true; C.stop_tick = 9000u;
    { static const cmdev_t c[] = { { 4000u, 'g' } }; C.cmds = c; C.n_cmds = 1u; run(); }
    CHECK(count_prefix("[wifi] result=fail stage=join ") >= 1u);
    CHECK(find_idx("[stk] res=fail sym=0 src=boot err=8 http=0 ms=") >= 0);
    CHECK(find_idx("[stk] net=err busy=0 have=0 stale=0 clk=none upd=none\r\n") >= 0);
    CHECK_EQ(tx_count("AT+CWQAP\r\n"), 2);                                              /* 60 秒後に再接続(2 回目は成功) */
    CHECK(g_v_net_seen[STK_NET_ERR] > 5000u);                                           /* 失敗から 60 秒(約 6000 tick)の間はエラー表示 */
    CHECK_EQ(g_v.net, STK_NET_ON); CHECK_EQ(g_v.have_any, 1);
    CHECK_EQ(count_prefix("[stk] res=ok sym=0 src=boot "), 1);                          /* 再接続の後の最初の周期(きっかけは起動のまま) */
    CHECK_EQ(count_prefix("[stk] res=ok sym=2 src=boot "), 1);
    check_lines_clean();

    TEST("P8 受信は 1 周回に 64 バイトまで、SCI1 の受信異常は [err] code=10 を 1 秒に 1 行");
    reset_all(); C.esp = false;
    for (i = 0u; i < 200u; i++) { fake_sci_rx_put((uint8_t)'x'); }
    C.rx_probe_calls = 6u;
    run();
    CHECK_EQ(g_probe[1], 0); CHECK_EQ(g_probe[2], 64); CHECK_EQ(g_probe[3], 128); CHECK_EQ(g_probe[4], 192); CHECK_EQ(g_probe[5], 200);
    reset_all(); C.esp = false; C.stop_tick = 400u;
    C.cb_ticks[0] = 50u; C.cb_ticks[1] = 60u; C.cb_ticks[2] = 100u; C.n_cb = 3u;
    run();
    CHECK_EQ(count_prefix("[err] code=10 detail=8\r\n"), 2);

    TEST("P9 SCI1 を開けない: [err] code=9 を 1 行、OFF(ESP32 に送らない)。x は req=off。s は net=off と sym 3 本");
    reset_all(); C.esp = false; C.stop_tick = 800u;
    g_fs.open_ret = SCI_ERR_BAD_CHAN;
    { static const cmdev_t c[] = { { 100u, 'x' }, { 200u, 's' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    { char exp[80]; snprintf(exp, sizeof(exp), "[err] code=9 detail=%d\r\n", (int)SCI_ERR_BAD_CHAN); CHECK(strcmp(g_logs[0], exp) == 0); }
    CHECK_EQ(count_prefix("[wifi] "), 0);
    CHECK_EQ(g_fs.send_calls, 0);
    CHECK_EQ(g_fs.recv_calls, 0);
    CHECK_EQ(count_prefix("[stk] req=off src=uart\r\n"), 1);
    CHECK_EQ(count_prefix("[stk] net=off busy=0 have=0 stale=0 clk=none upd=none\r\n"), 1);
    CHECK_EQ(count_prefix("[stk] sym="), 3);
    CHECK_EQ(count_prefix("[wx] have=0 temp10=0 wmo=0 kind=none age=-1\r\n"), 1);
    CHECK_EQ(g_v.net, STK_NET_OFF); CHECK_EQ(g_v.clock_min, 0xFFFF); CHECK_EQ(g_v.have_any, 0);
    CHECK_EQ(g_c.clock_min, 0xFFFF); CHECK_EQ(g_c.have, 0); CHECK_EQ(g_c.kind, CLK_UNKNOWN); CHECK_EQ(g_c.mon, 0);
#endif /* !T_NOSECRET */

#ifdef T_NOSECRET
    TEST("P10 wifi_secret.h が無い: SCI1 を開かない・EN に触れない・何も送らない。表示値は OFFLINE(net=off)・未同期。x は req=off、s は net=off と sym 3 本");
    reset_all(); C.stop_tick = 1500u;
    { static const cmdev_t c[] = { { 100u, 'x' }, { 101u, 't' }, { 200u, 's' } }; C.cmds = c; C.n_cmds = 3u; run(); }
    CHECK_EQ(g_fs.open_calls, 0);
    CHECK_EQ(g_fs.pinset_calls, 0);
    CHECK_EQ(g_gpio_ev[0], '\0');                                                    /* EN の GPIO を一度も動かさない */
    CHECK_EQ(g_dir_out_calls, 0);
    CHECK_EQ(g_fs.send_calls, 0);
    CHECK_EQ(g_fs.recv_calls, 0);
    CHECK_EQ(count_prefix("[wifi] "), 0);
    CHECK_EQ(count_prefix("[stk] req=off src=uart\r\n"), 1);
    CHECK_EQ(count_prefix("[stk] req=off src=touch\r\n"), 1);
    CHECK_EQ(count_prefix("[stk] net=off busy=0 have=0 stale=0 clk=none upd=none\r\n"), 1);
    CHECK_EQ(count_prefix("[stk] sym="), 3);
    CHECK_EQ(count_prefix("[wx] have=0 temp10=0 wmo=0 kind=none age=-1\r\n"), 1);
    CHECK_EQ(g_nlogs, 7u);
    CHECK_EQ(g_c.clock_min, 0xFFFF); CHECK_EQ(g_c.have, 0);
    CHECK_EQ(g_v.net, STK_NET_OFF); CHECK_EQ(g_v.busy, 0); CHECK_EQ(g_v.have_any, 0); CHECK_EQ(g_v.partial, 0);
    CHECK_EQ(g_v.clock_min, 0xFFFF); CHECK_EQ(g_v.upd_min, 0xFFFF);
    CHECK(!g_v_busy_seen);
    CHECK_EQ(g_v_net_seen[STK_NET_OFF] > 1000u, 1);
    check_lines_clean();
#endif
#endif

    (void)i; (void)k;
    TEST_REPORT("test_wifi_at");
}
