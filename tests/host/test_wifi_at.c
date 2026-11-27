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
static uint32_t g_cipstart_tick[16];
static unsigned g_n_cipstart;

/* 要求本体を受け終わった: SEND OK、+IPD(HTTP 応答。分割)、CLOSED を返す。応答は設計 §22.1 の実測と同じ形(本文の目印 BODY_SENTINEL を足してある) */
static void esp_http(void)
{
    static char http[800];
    char        piece[320];
    unsigned    d = C.resp_delay_iters + (10u * g_ipt);
    size_t      n, k, m;

    n = (size_t)snprintf(http, sizeof(http),
        "HTTP/1.0 200 OK\r\nDate: Wed, 25 Nov 2026 04:%02u:%02u GMT\r\nContent-Type: application/json; charset=utf-8\r\nX-Body: BODY_SENTINEL\r\nConnection: close\r\n\r\n"
        "{\"latitude\":35.7,\"longitude\":139.75,\"generationtime_ms\":0.04,\"utc_offset_seconds\":32400,\"timezone\":\"Asia/Tokyo\","
        "\"current_units\":{\"time\":\"iso8601\",\"interval\":\"seconds\",\"temperature_2m\":\"C\",\"weather_code\":\"wmo code\"},"
        "\"current\":{\"time\":\"2026-11-25T13:30\",\"interval\":900,\"temperature_2m\":12.7,\"weather_code\":0}}", g_http_min % 100u, g_http_sec % 100u);
    if (g_tcp_nodata) {
        esp_say(d, "\r\nRecv 190 bytes\r\n\r\nSEND OK\r\n\r\nCLOSED\r\n");
        return;
    }
    snprintf(piece, sizeof(piece), "\r\nRecv 190 bytes\r\n\r\nSEND OK\r\n\r\n+IPD,%u:", (unsigned)n);
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
    else if (strncmp(cmd, "AT+CIPSTART=", 12) == 0) { if (g_n_cipstart < 16u) { g_cipstart_tick[g_n_cipstart] = g_hook_tick; } g_n_cipstart++; resp = g_tcp_fail ? "DNS Fail\r\n\r\nERROR" : "CONNECT\r\n\r\nOK"; d += 20u * g_ipt; }
    else if (strcmp(cmd, "AT+CIPSEND=190") == 0) { resp = "\r\nOK\r\n> "; g_data_left = 190u; }
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
    g_data_left = 0u; g_tcp_fail = false; g_tcp_nodata = false;
    s_clk.synced = 0u; s_wx_seen = 0u; s_wx_ok_ms = 0u; s_wx_temp = 0; s_wx_kind = 0u; s_wx_have = 0u;
#if APP_WIFI_TEST
    s_wx_last_ok = 0u;
#endif
    g_hook_tick = 0u; g_tick_seen = 0u; g_tick_lines = 0u; g_max_tick_lines = 0u; g_join_fail = false; g_http_min = 24u; g_http_sec = 13u; g_n_cipstart = 0u; memset(g_cipstart_tick, 0, sizeof g_cipstart_tick);
#if APP_WX_PRODUCT
    memset(&s_sched, 0, sizeof s_sched); s_act = 0u; s_act_t0 = 0u; s_done_seen = 0u; s_last_net = 0u; s_last_busy = 0u; s_upd_min = 0xFFFFu; s_res_ok = 0u; s_res_http = 0u;
#endif
    g_result_tick = 0u; g_result_seen = false; g_lastres[0] = '\0';
    C.stop_tick = 1000u; C.max_iter = 3000000u; C.esp = true; C.drain_iters = 1u;
}

#if APP_WX_PRODUCT
/* 製品のメインループの代わり(app_main.c の loop と同じ呼び方)。コマンド文字(1 tick に 1 文字):
 *   x = 更新ボタン(src=uart)、t = 更新ボタン(src=touch)、s = [wx] 状態行、
 *   z = 最後の成功を 31 分前にする(stale の確認。gdb の s_wx_ok_ms への書き込みと同じ)、
 *   a = 以後の応答の Date: を 04:40:50 UTC に(再同期の確認)、f = 以後 TCP を張れない、j = 以後 join に FAIL、g = 以後 join も TCP も正常 */
static ui_wx_t  g_ui;                       /* 最後の wx_ui */
static unsigned g_ui_net_seen[4];           /* 各 net の値が wx_ui に出た tick 数 */
static bool     g_ui_stale_seen, g_ui_busy_seen;
static unsigned g_req_ret[8], g_n_req;      /* wx_request の戻り値の列 */
static void product_main(void)
{
    uint32_t last = 0u;
    uint32_t now;
    int      ch;

    g_timer_on = true;
    memset(g_ui_net_seen, 0, sizeof g_ui_net_seen); g_ui_stale_seen = false; g_ui_busy_seen = false; g_n_req = 0u;
    memset(&g_ui, 0, sizeof g_ui);
    wx_init(0u);
    for (;;) {
        loop_hook();
        now = s_tick;
        if (now != last) {                  /* 10ms ごと(app_main の tick_step) */
            last = now;
            ch = app_log_getc();
            if ((ch == 'x') || (ch == 't')) {
                wx_req_t r = wx_request((ch == 'x') ? WX_SRC_UART : WX_SRC_TOUCH, now * 10u);
                if (g_n_req < 8u) { g_req_ret[g_n_req] = (unsigned)r; }
                g_n_req++;
            }
            else if (ch == 's') { wx_log_state(now * 10u); }
            else if (ch == 'z') { s_wx_ok_ms -= 1860000u; }
            else if (ch == 'a') { g_http_min = 40u; g_http_sec = 50u; }
            else if (ch == 'f') { g_tcp_fail = true; }
            else if (ch == 'j') { g_join_fail = true; }
            else if (ch == 'g') { g_join_fail = false; g_tcp_fail = false; }
            else { /* 無視 */ }
            wx_ui(&g_ui, now * 10u);
            if (g_ui.net < 4u) { g_ui_net_seen[g_ui.net]++; }
            if (g_ui.stale != 0u) { g_ui_stale_seen = true; }
            if (g_ui.busy != 0u) { g_ui_busy_seen = true; }
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
              strncmp(l, "[err] ", 6) == 0 || strncmp(l, "[wx] ", 5) == 0)) { ok_kind = false; printf("  unexpected line kind: %.20s\n", l); }
        if (strstr(l, "SENTINEL") != NULL || strstr(l, "NEIGHBOR") != NULL || strstr(l, "de:ad") != NULL || strstr(l, "CWLAP") != NULL || strstr(l, "CWJAP") != NULL || strstr(l, "PW_") != NULL || strstr(l, "SSID_") != NULL ||
            strstr(l, "BODY_SENTINEL") != NULL || strstr(l, "latitude") != NULL || strstr(l, "generationtime") != NULL || strstr(l, "GET ") != NULL || strstr(l, "Date:") != NULL ||
            strstr(l, "ready") != NULL || strstr(l, "ets ") != NULL) { no_secret = false; }
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
    TEST("F1 d の後に x: wx_mux〜wx_recv が ok、result=pass、[wx] res=ok(src=uart・jst・obs)、s に [wx] 状態行(時計)。本文・要求・Date は出ない");
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
    CHECK(tx_has("AT+CIPMUX=0\r\n") && tx_has("AT+CIPCLOSE\r\n") && tx_has("AT+CIPSTART=\"TCP\",\"api.open-meteo.com\",80\r\n") && tx_has("AT+CIPSEND=190\r\n"));
    CHECK(tx_has("GET /v1/forecast?latitude=35.68&longitude=139.76&current=temperature_2m,weather_code&timezone=Asia%2FTokyo HTTP/1.0\r\nHost: api.open-meteo.com\r\n"));
    CHECK(tx_has("Connection: close\r\n\r\n") && !tx_has("Connection: close\r\n\r\n\r\n"));            /* 要求の後に \r\n を足さない */
    k = find_idx("[wx] res=ok src=uart temp10=127 wmo=0 kind=sun http=200 ms=");
    CHECK(k >= 0);
    if (k >= 0) {
        CHECK(strstr(g_logs[k], " sync=1 jst=132413 obs=1330 closed=1\r\n") != NULL);
        CHECK(k > find_idx("[wifi] step=wx_recv res=ok "));
        CHECK(field(g_logs[k], " ms=") > 200u); CHECK(field(g_logs[k], " ms=") < 5000u);
    }
    CHECK_EQ(count_prefix("[wx] res="), 1);
    k = find_idx("[wx] net=");
    CHECK(k >= 0);
    if (k >= 0) {
        unsigned hh, mm, ss;
        const char *p = strstr(g_logs[k], " clk=");
        long expect_s, got_s;

        CHECK(strncmp(g_logs[k], "[wx] net=on busy=0 have=1 temp10=127 kind=sun age=", 50) == 0);
        CHECK(strstr(g_logs[k], " date=20261125 wd=3 sync=1\r\n") != NULL);
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
        CHECK(field(g_logs[k], " age=") >= 4u);
    }
    check_lines_clean();
    CHECK_EQ(g_log_drops, 0);
    CHECK_EQ(g_app_dbg.scr, 0);

    TEST("F2 x だけ(d なし。EN に触れず、reset は走らない)・x の連続(2 回とも取得して [wx] が 2 行)");
    reset_all(); C.stop_tick = 1300u;
    { static const cmdev_t c[] = { { 20u, 'x' }, { 400u, 'x' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    CHECK_EQ(count_prefix("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"), 2);
    CHECK_EQ(count_prefix("[wifi] step=reset"), 0);
    CHECK(strcmp(g_gpio_ev, "W1;D;") == 0);
    CHECK_EQ(count_prefix("[wx] res=ok src=uart temp10=127 wmo=0 kind=sun http=200 ms="), 2);
    CHECK_EQ(g_fs.send_ok, 10);                                                                     /* 5 回の送信 x 2 */
    check_lines_clean();
    CHECK_EQ(g_log_drops, 0);

    TEST("F3 実行中の x は無視(d の最中は取得を始めない・何も出さない)");
    reset_all(); C.stop_tick = 250u;
    { static const cmdev_t c[] = { { 20u, 'd' }, { 30u, 'x' }, { 40u, 'x' } }; C.cmds = c; C.n_cmds = 3u; run(); }
    CHECK_EQ(count_prefix("[wifi] step=wx_"), 0);
    CHECK_EQ(count_prefix("[wx] "), 0);
    CHECK(!tx_has("CIPSTART"));

    TEST("F4 TCP が張れない(DNS Fail): result=fail stage=wx_tcp と [wx] res=fail err=1。s の状態行は net=err have=0");
    reset_all(); g_tcp_fail = true; C.stop_tick = 600u;
    { static const cmdev_t c[] = { { 20u, 'x' }, { 400u, 's' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    CHECK_EQ(count_prefix("[wifi] result=fail stage=wx_tcp ping_ip=0 ping_dns=0\r\n"), 1);
    k = find_idx("[wx] res=fail src=uart err=1 http=0 ms=");
    CHECK(k >= 0);
    CHECK_EQ(count_prefix("[wx] res="), 1);
    CHECK_EQ(count_prefix("[wx] net=err busy=0 have=0 temp10=0 kind=none age=-1 clk=none date=none wd=9 sync=0\r\n"), 1);
    CHECK(!tx_has("CIPSEND"));
    check_lines_clean();

    TEST("F5 応答が無い(CLOSED だけ): err=3。時刻は同期しない");
    reset_all(); g_tcp_nodata = true; C.stop_tick = 600u;
    { static const cmdev_t c[] = { { 20u, 'x' }, { 400u, 's' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    CHECK(find_idx("[wx] res=fail src=uart err=3 http=0 ms=") >= 0);
    CHECK_EQ(count_prefix("[wx] net=err busy=0 have=0 temp10=0 kind=none age=-1 clk=none date=none wd=9 sync=0\r\n"), 1);
    check_lines_clean();

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
    TEST("P1 起動: SCI1 を 1 回開く(115200・8N1・優先度 2)。EN は先に High → 出力。3 秒待つ間は何も送らない・何も出さない。UI は接続中・未同期(clk_ext = 1、時計 0xFFFF、日付なし)");
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
    CHECK_EQ(g_ui.net, UI_NET_CONN);
    CHECK_EQ(g_ui.busy, 0); CHECK_EQ(g_ui.have, 0); CHECK_EQ(g_ui.stale, 0); CHECK_EQ(g_ui.clk_ext, 1);
    CHECK_EQ(g_ui.clock_min, 0xFFFF); CHECK_EQ(g_ui.upd_min, 0xFFFF); CHECK_EQ(g_ui.month, 0); CHECK_EQ(g_ui.day, 0);
    CHECK(!g_ui_busy_seen);

    TEST("P2 起動 3 秒後に接続(reset〜ip の 8 step)→ すぐ最初の取得(5 step)。res=start と key=oth は出ない。[wx] res=ok src=boot。状態行 3 本。時計・日付・UI の値。秘密と本文は出ない");
    reset_all(); C.stop_tick = 1800u;
    run();
    CHECK(strcmp(g_gpio_ev, "W1;D;W0;W1;") == 0);
    {
        static const char *const nm[] = { "reset", "boot", "at", "echo", "mode", "qap", "join", "ip", "wx_mux", "wx_close", "wx_tcp", "wx_send", "wx_recv" };
        char p[64];
        for (k = 0; k < 13; k++) { snprintf(p, sizeof(p), "[wifi] step=%s res=ok ", nm[k]); CHECK_EQ(count_prefix(p), 1); }
    }
    CHECK_EQ(count_prefix("[wifi] step="), 13);                                      /* ok だけ(start なし) */
    for (i = 0u; i < g_nlogs; i++) { CHECK(strstr(g_logs[i], " res=start") == NULL); CHECK(strstr(g_logs[i], "key=oth") == NULL); }
    CHECK_EQ(count_prefix("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"), 2);   /* 接続と取得 */
    CHECK_EQ(count_prefix("[wifiv] key=ip val=192.168.1.23"), 1);
    CHECK(!tx_has("AT+GMR") && !tx_has("AT+CWLAP") && !tx_has("AT+PING") && !tx_has("AT+CIPDOMAIN") && !tx_has("CWLAPOPT"));   /* 製品の接続は gmr lapopt scan dns ping を省く */
    CHECK(tx_has("AT+CWQAP\r\n") && tx_has("AT+CWJAP=\"SSID_SENTINEL_7c1\",\"PW_SENTINEL_9f3\"\r\n") && tx_has("AT+CIFSR\r\n"));
    CHECK(tx_has("AT+CIPSTART=\"TCP\",\"api.open-meteo.com\",80\r\n") && tx_has("AT+CIPSEND=190\r\n"));
    k = find_idx("[wx] res=ok src=boot temp10=127 wmo=0 kind=sun http=200 ms=");
    CHECK(k >= 0);
    if (k >= 0) { CHECK(strstr(g_logs[k], " sync=1 jst=132413 obs=1330 closed=1\r\n") != NULL); CHECK(k > find_idx("[wifi] step=wx_recv res=ok ")); }
    CHECK_EQ(count_prefix("[wx] res="), 1);
    CHECK_EQ(count_prefix("[wx] net="), 3);
    {
        int a = find_idx("[wx] net=conn busy=1 have=0 temp10=0 kind=none age=-1 clk=none date=none wd=9 sync=0\r\n");
        int b = find_idx("[wx] net=on busy=1 have=0 ");
        int c = find_idx("[wx] net=on busy=0 have=1 temp10=127 kind=sun age=");
        CHECK(a >= 0); CHECK(b > a); CHECK(c > b);
        CHECK(a < find_idx("[wifi] step=reset res=ok "));
        CHECK(c > find_idx("[wx] res=ok "));
    }
    CHECK_EQ(g_ui.net, UI_NET_ON); CHECK_EQ(g_ui.busy, 0); CHECK_EQ(g_ui.have, 1); CHECK_EQ(g_ui.kind, 0); CHECK_EQ(g_ui.temp_x10, 127);
    CHECK_EQ(g_ui.stale, 0); CHECK_EQ(g_ui.clk_ext, 1);
    CHECK_EQ(g_ui.month, 11); CHECK_EQ(g_ui.day, 25); CHECK_EQ(g_ui.wday, 3);                    /* 2026-11-25 水 */
    CHECK(g_ui.clock_min >= 13 * 60 + 24); CHECK(g_ui.clock_min <= 13 * 60 + 25);
    CHECK_EQ(g_ui.upd_min, 13 * 60 + 24);
    CHECK(g_ui_busy_seen); CHECK(g_ui_net_seen[UI_NET_CONN] > 0u); CHECK(g_ui_net_seen[UI_NET_ON] > 0u); CHECK_EQ(g_ui_net_seen[UI_NET_ERR], 0);
    CHECK_EQ(g_log_drops, 0);
    CHECK(g_max_tick_lines <= 6u);                                                   /* ログのキュー(8 行)に対して 1 tick の行数に余裕 */
    CHECK_EQ(count_prefix("[err] "), 0);
    CHECK_EQ(count_prefix("[hb] "), 0);                                              /* hb は app_main が出す */
    check_lines_clean();

    TEST("P3 定期取得: 前回の取得開始から 10 分(60000 tick ± 3)で 1 回だけ。src=timer。その間 [wifi] [wx] の行は増えない");
    reset_all(); C.stop_tick = 61800u;
    run();
    CHECK_EQ(g_n_cipstart, 2);
    CHECK(g_cipstart_tick[1] - g_cipstart_tick[0] >= 59997u); CHECK(g_cipstart_tick[1] - g_cipstart_tick[0] <= 60003u);
    CHECK_EQ(count_prefix("[wx] res=ok src=boot "), 1);
    CHECK_EQ(count_prefix("[wx] res=ok src=timer "), 1);
    CHECK_EQ(count_prefix("[wifi] step="), 13 + 5);                                  /* 2 回目は取得の 5 step だけ(再接続しない) */
    CHECK_EQ(count_prefix("[wx] net="), 3 + 2);                                      /* 取得ごとに busy 1 → 0 */
    CHECK_EQ(g_ui_net_seen[UI_NET_ERR], 0);
    CHECK_EQ(g_log_drops, 0);
    check_lines_clean();

    TEST("P4 更新ボタン(x = src=uart、t = src=touch): 取得中は req=busy、前回の開始から 5 秒未満は req=ignored、5 秒後は取得 → 時計を Date: で合わせ直す");
    reset_all(); C.stop_tick = 5200u;
    { static const cmdev_t c[] = { { 2500u, 'x' }, { 2501u, 'x' }, { 2502u, 'a' }, { 2800u, 'x' }, { 3200u, 'x' }, { 4200u, 't' }, { 4900u, 's' } };
      C.cmds = c; C.n_cmds = sizeof(c) / sizeof(c[0]); run(); }
    CHECK_EQ(count_prefix("[wx] res=ok src=boot "), 1);
    CHECK_EQ(count_prefix("[wx] res=ok src=uart "), 2);                              /* 2500 の x と 3200 の x(2800 は ignored) */
    CHECK_EQ(count_prefix("[wx] res=ok src=touch "), 1);
    CHECK_EQ(count_prefix("[wx] req=busy src=uart\r\n"), 1);                         /* 2501: 取得中 */
    CHECK_EQ(count_prefix("[wx] req=ignored src=uart\r\n"), 1);                      /* 2800: 2500 に始めた取得から 3 秒 */
    CHECK_EQ(count_prefix("[wx] req="), 2);
    CHECK_EQ(g_n_req, 5);
    CHECK_EQ(g_req_ret[0], WX_REQ_OK); CHECK_EQ(g_req_ret[1], WX_REQ_BUSY); CHECK_EQ(g_req_ret[2], WX_REQ_IGNORED); CHECK_EQ(g_req_ret[3], WX_REQ_OK); CHECK_EQ(g_req_ret[4], WX_REQ_OK);
    CHECK_EQ(g_n_cipstart, 4);                                                       /* boot + 2 uart + touch(2800 の x は取得しない) */
    CHECK_EQ(count_prefix("[wifi] step="), 13 + 15);                                 /* 再接続はしない */
    {
        unsigned n40 = 0u;
        for (i = 0u; i < g_nlogs; i++) { if ((strncmp(g_logs[i], "[wx] res=ok ", 12) == 0) && (strstr(g_logs[i], "jst=134050") != NULL)) { n40++; } }
        CHECK_EQ(n40, 3u);                                                           /* 'a' の後の 3 回は Date: = 04:40:50 UTC → 13:40:50 JST */
    }
    {
        int last = -1;
        for (i = 0u; i < g_nlogs; i++) { if (strncmp(g_logs[i], "[wx] net=on busy=0 ", 19) == 0) { last = (int)i; } }
        CHECK(last >= 0);
        if (last >= 0) {                                                             /* s(4900)の応答: 時計は 13:40:50 + (4900 - 最後の取得)の数秒 = 13:40:5x〜13:41:0x。再同期しなければ 13:24 台のまま */
            const char *p = strstr(g_logs[last], " clk=");
            unsigned hh = (unsigned)(p[5] - '0') * 10u + (unsigned)(p[6] - '0'), mm = (unsigned)(p[7] - '0') * 10u + (unsigned)(p[8] - '0');
            CHECK_EQ(hh, 13u); CHECK(mm == 40u || mm == 41u);
            CHECK(strstr(g_logs[last], " date=20261125 wd=3 sync=1\r\n") != NULL);
        }
    }
    CHECK_EQ(g_ui.clock_min >= 13 * 60 + 40, 1);
    CHECK_EQ(g_log_drops, 0);
    CHECK(g_max_tick_lines <= 6u);
    check_lines_clean();

    TEST("P5 古い値: 最後の成功から 30 分を超えると stale = 1(値は消えない)。次の成功で戻る");
    reset_all(); C.stop_tick = 2400u;
    { static const cmdev_t c[] = { { 1800u, 'z' }, { 2000u, 'x' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    CHECK(g_ui_stale_seen);
    CHECK_EQ(g_ui.stale, 0);                                                         /* x の取得に成功して戻った */
    CHECK_EQ(g_ui.have, 1); CHECK_EQ(g_ui.temp_x10, 127);
    reset_all(); C.stop_tick = 1900u;
    { static const cmdev_t c[] = { { 1800u, 'z' } }; C.cmds = c; C.n_cmds = 1u; run(); }
    CHECK_EQ(g_ui.stale, 1); CHECK_EQ(g_ui.have, 1); CHECK_EQ(g_ui.temp_x10, 127); CHECK_EQ(g_ui.net, UI_NET_ON);

    TEST("P6 取得の失敗: TCP が張れない → [wx] res=fail err=1、net=err、値は残る。20 秒後にやり直し(2 回まで)、3 回目の失敗で接続からやり直す。直ったら net=on");
    reset_all(); C.stop_tick = 8500u;
    { static const cmdev_t c[] = { { 1800u, 'f' }, { 2000u, 'x' }, { 6200u, 'g' } }; C.cmds = c; C.n_cmds = 3u; run(); }
    CHECK_EQ(count_prefix("[wx] res=fail src=uart err=1 http=0 ms="), 1);
    CHECK_EQ(count_prefix("[wx] res=fail src=timer err=1 http=0 ms="), 2);          /* 20 秒後に 2 回(やり直しは timer。2000 → 4000 → 6000 tick) */
    CHECK_EQ(count_prefix("[wifi] step=reset res=ok "), 2);                          /* 3 回目の失敗で再接続 */
    CHECK_EQ(g_ui_net_seen[UI_NET_ERR] > 0u, 1);
    CHECK_EQ(g_ui.have, 1); CHECK_EQ(g_ui.temp_x10, 127);                            /* 失敗でも古い値を残す */
    CHECK_EQ(g_ui.net, UI_NET_ON);                                                   /* g の後の取得に成功 */
    CHECK_EQ(count_prefix("[wx] res=ok src=timer "), 1);
    check_lines_clean();

    TEST("P7 接続の失敗(join が FAIL): [wifi] result=fail stage=join、[wx] res=fail src=boot err=8、net=err。60 秒後に再接続、直れば取得して net=on");
    reset_all(); g_join_fail = true; C.stop_tick = 9000u;
    { static const cmdev_t c[] = { { 4000u, 'g' } }; C.cmds = c; C.n_cmds = 1u; run(); }
    CHECK(count_prefix("[wifi] result=fail stage=join ") >= 1u);
    CHECK(find_idx("[wx] res=fail src=boot err=8 http=0 ms=") >= 0);
    CHECK(find_idx("[wx] net=err busy=0 have=0 ") >= 0);
    CHECK_EQ(count_prefix("[wifi] step=reset res=ok "), 2);                          /* 60 秒後に再接続(2 回目は成功) */
    CHECK(g_ui_net_seen[UI_NET_ERR] > 5000u);                                        /* 失敗から 60 秒(約 6000 tick)の間はエラー表示 */
    CHECK_EQ(g_ui.net, UI_NET_ON); CHECK_EQ(g_ui.have, 1);
    CHECK_EQ(count_prefix("[wx] res=ok src=boot "), 1);                              /* 再接続の後の最初の取得(取得のきっかけは起動のまま) */
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

    TEST("P9 SCI1 を開けない: [err] code=9 を 1 行、OFF(ESP32 に送らない)。x は req=off");
    reset_all(); C.esp = false; C.stop_tick = 800u;
    g_fs.open_ret = SCI_ERR_BAD_CHAN;
    { static const cmdev_t c[] = { { 100u, 'x' }, { 200u, 's' } }; C.cmds = c; C.n_cmds = 2u; run(); }
    { char exp[80]; snprintf(exp, sizeof(exp), "[err] code=9 detail=%d\r\n", (int)SCI_ERR_BAD_CHAN); CHECK(strcmp(g_logs[0], exp) == 0); }
    CHECK_EQ(count_prefix("[wifi] "), 0);
    CHECK_EQ(g_fs.send_calls, 0);
    CHECK_EQ(g_fs.recv_calls, 0);
    CHECK_EQ(count_prefix("[wx] req=off src=uart\r\n"), 1);
    CHECK_EQ(count_prefix("[wx] net=off busy=0 have=0 temp10=0 kind=none age=-1 clk=none date=none wd=9 sync=0\r\n"), 1);
    CHECK_EQ(g_ui.net, UI_NET_OFF); CHECK_EQ(g_ui.clk_ext, 1); CHECK_EQ(g_ui.clock_min, 0xFFFF);
#endif /* !T_NOSECRET */

#ifdef T_NOSECRET
    TEST("P10 wifi_secret.h が無い: SCI1 を開かない・EN に触れない・何も送らない・何も出さない。UI は OFFLINE・未同期。x は req=off、s は net=off");
    reset_all(); C.stop_tick = 1500u;
    { static const cmdev_t c[] = { { 100u, 'x' }, { 101u, 't' }, { 200u, 's' } }; C.cmds = c; C.n_cmds = 3u; run(); }
    CHECK_EQ(g_fs.open_calls, 0);
    CHECK_EQ(g_fs.pinset_calls, 0);
    CHECK_EQ(g_gpio_ev[0], '\0');                                                    /* EN の GPIO を一度も動かさない */
    CHECK_EQ(g_dir_out_calls, 0);
    CHECK_EQ(g_fs.send_calls, 0);
    CHECK_EQ(g_fs.recv_calls, 0);
    CHECK_EQ(count_prefix("[wifi] "), 0);
    CHECK_EQ(count_prefix("[wx] req=off src=uart\r\n"), 1);
    CHECK_EQ(count_prefix("[wx] req=off src=touch\r\n"), 1);
    CHECK_EQ(count_prefix("[wx] net=off busy=0 have=0 temp10=0 kind=none age=-1 clk=none date=none wd=9 sync=0\r\n"), 1);
    CHECK_EQ(g_nlogs, 3u);
    CHECK_EQ(g_ui.net, UI_NET_OFF); CHECK_EQ(g_ui.busy, 0); CHECK_EQ(g_ui.have, 0); CHECK_EQ(g_ui.clk_ext, 1);
    CHECK_EQ(g_ui.clock_min, 0xFFFF); CHECK_EQ(g_ui.month, 0); CHECK_EQ(g_ui.upd_min, 0xFFFF);
    CHECK(!g_ui_busy_seen);
    CHECK_EQ(g_ui_net_seen[UI_NET_OFF] > 1000u, 1);
    check_lines_clean();
#endif
#endif

    (void)i; (void)k;
    TEST_REPORT("test_wifi_at");
}
