/* test_wifi_core.c - src/app/wifi_core.c の単体試験(docs/design.md §21.7 の H-W1〜H-W7、H-W10〜H-W13、H-W8 の一部と、株価の取得 H-10・H-11 の一部)。標準Cのみ。
 * 株価の取得(WX step・データモード・範囲のビットマスク・銘柄の選択・SSL のタイムアウト・ready = err 9)は H-X5-* / H-P3-* として下のほうにある(応答は Yahoo の chart API ふう)。
 * 2026-11-25 改訂: AT 1.1.3.0 向け(14 step。store を削除、qap / lapopt / scan を追加、ip は AT+CIFSR)。
 * -DAPP_WIFI_TEST=1(専用モード。t y g z の接続先も試験する)と -DAPP_WX=1(製品。接続先は Yahoo の SSL だけ)の 2 通りでコンパイルする(両方 0 の時 wifi_core.c は空)。
 * 偽の ESP32(下の esp_recv)に、台本(rule)どおりの応答を、遅れ(ms)つきで返させ、10ms ごとに wifi_core_poll を回す。
 * SSID・パスワードは、明らかなダミー(目印)だけを使う。 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "wifi_core.h"
#include "app_logfmt.h"      /* APPLOG_LINE_MAX だけ使う */
#include "test_framework.h"

#define SSID_MARK   "SSID_SENT_9a1"
#define PASS_MARK   "PW_SENTINEL_9f3"
#define NEIGH_MARK  "NEIGHBOR_SENTINEL"      /* 他の AP の名前(ログ・構造体に出てはいけない目印) */

/* ================================================================ 偽の世界 */

#define MAXLOG   (400)
static char     g_log[MAXLOG][WIFI_LOG_MAX];
static int      g_nlog;
static char     g_all[1 << 16];          /* 全シナリオのログを連結(H-W6) */
static size_t   g_all_n;
static int      g_en_n;
static int      g_en_lvl[16];
static uint32_t g_en_t[16];
static uint32_t g_now;
static int      g_send_mode;             /* 0 = OK、2 = FAIL */
static int      g_busy_left;             /* この回数だけ BUSY を返してから OK */
static int      g_nsend;
static char     g_sent[64][WIFI_CMD_MAX];
static int      g_esp_on;
static int      g_echo;
static int      g_ready_en;              /* EN が High になったら ready を返す */
static int      g_echo_fed_secret;       /* 偽の ESP が、秘密を含むエコーをコアへ流した回数 */

typedef struct { const char *cmd; int count; uint32_t delay; const char *reply; } rule_t;   /* cmd 末尾の * = 前方一致。count 0 = 無制限。reply NULL = 無応答 */
static rule_t g_rules[32];
static int    g_used[32];
static int    g_nrules;

typedef struct { uint32_t due; char data[6000]; size_t len; size_t pos; unsigned rate; } pend_t;   /* rate = 1 tick に渡す最大バイト数(0 = 全部) */
static unsigned g_rate;                  /* 次に作る応答の rate(WX の試験で使う) */
static int      g_busy_idx = -1;         /* g_nsend がこの値の送信を g_busy_n 回 BUSY にする */
static int      g_busy_n;
static pend_t g_pend[32];
static int    g_npend;

static wifi_core_t g_c;

static void pend_add(uint32_t due, const char *s1, const char *s2)
{
    pend_t *p = &g_pend[g_npend++];

    p->due = due;
    p->pos = 0u;
    p->rate = g_rate;
    p->len = (size_t)snprintf(p->data, sizeof p->data, "%s%s", s1, s2);
}

static void esp_recv(const char *d, size_t n)
{
    char     cmd[WIFI_CMD_MAX];
    int      i;
    size_t   cl;

    if (!g_esp_on) {
        return;
    }
    memcpy(cmd, d, n);
    cmd[n] = '\0';
    cl = n;
    while ((cl > 0u) && ((cmd[cl - 1u] == '\r') || (cmd[cl - 1u] == '\n'))) {
        cmd[--cl] = '\0';
    }
    for (i = 0; i < g_nrules; i++) {
        size_t rl = strlen(g_rules[i].cmd);
        int    hit;
        char   echo[WIFI_CMD_MAX + 4];

        if ((rl > 0u) && (g_rules[i].cmd[rl - 1u] == '*')) {
            hit = (strncmp(cmd, g_rules[i].cmd, rl - 1u) == 0);
        } else {
            hit = (strcmp(cmd, g_rules[i].cmd) == 0);
        }
        if (!hit || ((g_rules[i].count != 0) && (g_used[i] >= g_rules[i].count))) {
            continue;
        }
        g_used[i]++;
        if (g_rules[i].reply == NULL) {
            return;                                   /* 無応答 */
        }
        snprintf(echo, sizeof echo, "%s\r\r\n", cmd);
        if (strstr(echo, "SENT") != NULL) {
            g_echo_fed_secret++;
        }
        pend_add(g_now + g_rules[i].delay, (g_echo && (strncmp(cmd, "GET ", 4) != 0)) ? echo : "", g_rules[i].reply);   /* 要求本体(GET ...)はエコーされない */
        return;
    }
    pend_add(g_now + 10u, "", "ERROR\r\n");            /* 台本に無いコマンド */
}

static void f_en(int level)
{
    g_en_lvl[g_en_n] = level;
    g_en_t[g_en_n]   = g_now;
    g_en_n++;
    if ((level == 1) && g_ready_en) {
        pend_add(g_now + 300u, "", "ready\r\n");
    }
}

static int f_send(const char *d, size_t n)
{
    if (g_send_mode == WIFI_SEND_FAIL) {
        return WIFI_SEND_FAIL;
    }
    if ((g_nsend == g_busy_idx) && (g_busy_n > 0)) {
        g_busy_n--;
        return WIFI_SEND_BUSY;
    }
    if (g_busy_left > 0) {
        g_busy_left--;
        return WIFI_SEND_BUSY;
    }
    CHECK(n < WIFI_CMD_MAX);
    memcpy(g_sent[g_nsend], d, n);
    g_sent[g_nsend][n] = '\0';
    g_nsend++;
    esp_recv(d, n);
    return WIFI_SEND_OK;
}

static void f_log(const char *l, size_t n)
{
    CHECK_EQ(strlen(l), n);
    CHECK(n < APPLOG_LINE_MAX);
    CHECK(n >= 2u);
    if ((g_nlog < MAXLOG) && (n < WIFI_LOG_MAX)) {
        memcpy(g_log[g_nlog], l, n + 1u);
        g_nlog++;
    }
    if ((g_all_n + n + 1u) < sizeof g_all) {
        memcpy(g_all + g_all_n, l, n + 1u);
        g_all_n += n;
    }
}

static const wifi_ops_t g_ops = { f_en, f_send, f_log };

static void world_reset(void)
{
    g_nlog = 0; g_en_n = 0; g_nrules = 0; g_npend = 0; g_nsend = 0;
    g_send_mode = 0; g_busy_left = 0; g_esp_on = 1; g_echo = 1; g_ready_en = 1;
    g_now = 1000u;
    g_rate = 0u; g_busy_idx = -1; g_busy_n = 0;
    memset(g_used, 0, sizeof g_used);
}

static void rule(const char *cmd, int count, uint32_t delay, const char *reply)
{
    g_rules[g_nrules].cmd = cmd; g_rules[g_nrules].count = count;
    g_rules[g_nrules].delay = delay; g_rules[g_nrules].reply = reply;
    g_nrules++;
}

static void normal_rules(void)
{
    rule("AT",            0, 20,   "OK\r\n");
    rule("ATE0",          0, 20,   "OK\r\n");
    rule("AT+GMR",        0, 30,   "AT version:1.1.3.0(5a40576 - Nov 28 2018 12:50:55)\r\nSDK version:v3.0.7\r\n"
                                   "compile time:Nov 28 2018 13:02:40\r\n\r\nOK\r\n");
    rule("AT+CWMODE=1",   0, 20,   "OK\r\n");
    rule("AT+CWQAP",      0, 20,   "OK\r\n");
    rule("AT+CWLAPOPT=1,31", 0, 20, "OK\r\n");
    rule("AT+CWLAP",      0, 800,  "+CWLAP:(3,\"" NEIGH_MARK "_1\",-70,\"de:ad:be:ef:00:01\",1)\r\n"
                                   "+CWLAP:(3,\"" SSID_MARK "\",-52,\"de:ad:be:ef:00:02\",6)\r\n"
                                   "+CWLAP:(4,\"" NEIGH_MARK "_2\",-80,\"de:ad:be:ef:00:03\",11)\r\n\r\nOK\r\n");
    rule("AT+CWJAP=*",    0, 3000, "WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n");
    rule("AT+CIFSR",      0, 20,   "+CIFSR:APIP,\"192.168.4.1\"\r\n+CIFSR:APMAC,\"de:ad:be:ef:01:01\"\r\n+CIFSR:STAIP,\"192.168.1.23\"\r\n"
                                   "+CIFSR:STAMAC,\"de:ad:be:ef:02:02\"\r\n\r\nOK\r\n");
    rule("AT+CIPDOMAIN=\"www.google.com\"", 0, 100, "+CIPDOMAIN:\"142.250.0.1\"\r\n\r\nOK\r\n");
    rule("AT+PING=\"8.8.8.8\"",             0, 100, "+PING:23\r\n\r\nOK\r\n");
    rule("AT+PING=\"www.google.com\"",      0, 100, "+PING:31\r\n\r\nOK\r\n");
}

/* 10ms 進める: 期限の来た応答のバイトを渡してから poll */
static void tick(void)
{
    int i;

    g_now += 10u;
    for (i = 0; i < g_npend; i++) {
        pend_t *p = &g_pend[i];

        if (p->due <= g_now) {
            unsigned k = 0u;

            while ((p->pos < p->len) && ((p->rate == 0u) || (k < p->rate))) {
                wifi_core_rx(&g_c, (uint8_t)p->data[p->pos++], g_now);
                k++;
            }
        }
    }
    wifi_core_poll(&g_c, g_now);
}

static void begin(const char *ssid, const char *pass)
{
    world_reset();
    wifi_core_init(&g_c, &g_ops, ssid, pass);
}

static void run_until_done(uint32_t limit_ms)
{
    uint32_t t0 = g_now;

    while ((wifi_core_state(&g_c) == WIFI_STATE_RUN) && ((g_now - t0) < limit_ms)) {
        tick();
    }
    CHECK(wifi_core_state(&g_c) != WIFI_STATE_RUN);
}

static void run(wifi_range_t r, uint32_t limit_ms)
{
    CHECK(wifi_core_start(&g_c, r, g_now));
    run_until_done(limit_ms);
}

/* ログの検索 */
static int count_with(const char *sub)
{
    int i, n = 0;

    for (i = 0; i < g_nlog; i++) {
        if (strstr(g_log[i], sub) != NULL) {
            n++;
        }
    }
    return n;
}

static int first_with(const char *sub)
{
    int i;

    for (i = 0; i < g_nlog; i++) {
        if (strstr(g_log[i], sub) != NULL) {
            return i;
        }
    }
    return -1;
}

/* 偽の ESP へ送ったコマンドの中に sub があるか */
static int sent_has(const char *sub)
{
    int i;

    for (i = 0; i < g_nsend; i++) {
        if (strstr(g_sent[i], sub) != NULL) {
            return 1;
        }
    }
    return 0;
}

static int last_is(const char *line)
{
    return (g_nlog > 0) && (strcmp(g_log[g_nlog - 1], line) == 0);
}

/* コアの構造体のバイト列に needle がある */
static int core_has(const char *needle)
{
    const unsigned char *p = (const unsigned char *)&g_c;
    size_t i;
    size_t m = strlen(needle);

    for (i = 0u; (i + m) <= sizeof g_c; i++) {
        if (memcmp(p + i, needle, m) == 0) {
            return 1;
        }
    }
    return 0;
}

/* 秘密(パスワード・SSID・他の AP の名前・MAC の目印)が、コアの構造体のバイト列に残っている */
static int core_has_secret(void)
{
    return core_has("SENTINEL") || core_has("PW_") || core_has("SSID_SENT") || core_has("ssid_sent") || core_has("NEIGHBOR") || core_has("de:ad");
}

/* ログ(今のシナリオ)の行に、秘密の目印・MAC・生の CWLAP が1つでもあるか */
static int log_has_secret(void)
{
    static const char *const N[] = { "SENTINEL", "PW_", "SSID_SENT", "ssid_sent", "NEIGHBOR", "BADNAME", "de:ad", "CWLAP", "CWJAP=", "ready" };
    int    i;
    size_t k;

    for (i = 0; i < g_nlog; i++) {
        for (k = 0u; k < (sizeof N / sizeof N[0]); k++) {
            if (strstr(g_log[i], N[k]) != NULL) {
                return 1;
            }
        }
    }
    return 0;
}

/* ================================================================ H-W1 行の組み立て */

static void feed_n(const char *s, size_t n)
{
    size_t i;

    for (i = 0u; i < n; i++) {
        wifi_core_rx(&g_c, (uint8_t)s[i], g_now);
    }
}
#define FEED(lit) feed_n((lit), sizeof(lit) - 1u)

/* at の応答待ちにして(送信済み)、行の処理を試す */
static void at_wait(void)
{
    begin(SSID_MARK, PASS_MARK);
    g_esp_on = 0;
    CHECK(wifi_core_start(&g_c, WIFI_RANGE_AT, g_now));
    tick();                                         /* 送信 → WAIT */
    CHECK_EQ(g_nsend, 1);
    CHECK_EQ(count_with("step=at res=ok"), 0);
}

static void t_lines(void)
{
    char big[200];

    TEST("H-W1 行の組み立て");
    at_wait(); FEED("OK\r\n");                         CHECK_EQ(count_with("step=at res=ok"), 1);
    at_wait(); FEED("OK\n");                           CHECK_EQ(count_with("step=at res=ok"), 1);
    at_wait(); FEED("OK\r");                           CHECK_EQ(count_with("step=at res=ok"), 1);
    at_wait(); FEED("\r\n\r\n\n\rOK\r\n");             CHECK_EQ(count_with("step=at res=ok"), 1);
    at_wait(); FEED("O"); CHECK_EQ(count_with("step=at res=ok"), 0); FEED("K"); CHECK_EQ(count_with("step=at res=ok"), 0);
    FEED("\r\n");                                      CHECK_EQ(count_with("step=at res=ok"), 1);
    at_wait(); FEED("OK");                             CHECK_EQ(count_with("step=at res=ok"), 0);      /* 終わりが無い行は処理しない */
    at_wait(); FEED("OKK\r\n");                        CHECK_EQ(count_with("step=at res=ok"), 0);      /* 行全体の一致 */
    at_wait(); FEED("0K\r\nO K\r\n ok\r\n");           CHECK_EQ(count_with("step=at res=ok"), 0);
    /* 長すぎる行(OK で始まる 162 バイト)は丸ごと捨てる。次の行は壊れない */
    at_wait();
    memset(big, 'x', sizeof big);
    big[0] = 'O'; big[1] = 'K';
    feed_n(big, 162u);
    CHECK_EQ(count_with("step=at res=ok"), 0);
    FEED("\r\n");
    CHECK_EQ(count_with("step=at res=ok"), 0);
    CHECK_EQ(g_c.bad_lines, 1u);
    FEED("OK\r\n");
    CHECK_EQ(count_with("step=at res=ok"), 1);
    /* ちょうど 160 バイトの行は受け付ける(捨てない) */
    at_wait();
    memset(big, 'y', sizeof big);
    feed_n(big, 160u); FEED("\n");
    CHECK_EQ(g_c.bad_lines, 0u);
    CHECK_EQ(count_with("step=at res=ok"), 0);
    FEED("OK\n");
    CHECK_EQ(count_with("step=at res=ok"), 1);
    /* 161 バイト目で捨てる */
    at_wait();
    feed_n(big, 161u); FEED("\n");
    CHECK_EQ(g_c.bad_lines, 1u);
    FEED("OK\n");
    CHECK_EQ(count_with("step=at res=ok"), 1);
    /* NUL を含む行は丸ごと捨てる(OK\0 を OK と読まない) */
    at_wait(); FEED("OK\0junk\r\n");
    CHECK_EQ(count_with("step=at res=ok"), 0);
    CHECK_EQ(g_c.bad_lines, 1u);
    FEED("O\0K\n");
    CHECK_EQ(count_with("step=at res=ok"), 0);
    CHECK_EQ(g_c.bad_lines, 2u);
    FEED("OK\n");
    CHECK_EQ(count_with("step=at res=ok"), 1);
    /* 捨てた行の後ろの \r\n が連続しても、次の行が壊れない */
    at_wait(); FEED("\0\r\n\r\nOK\r\n");
    CHECK_EQ(count_with("step=at res=ok"), 1);
    /* バイト数の累計 */
    at_wait(); FEED("OK\r\n");
    CHECK_EQ(wifi_core_bytes(&g_c), 4u);
}

/* ================================================================ H-W2 / H-W3 分類と値 */

static void t_classify(void)
{
    static const struct { const char *line; wifi_line_t kind; } T[] = {
        { "OK", WIFI_L_OK }, { "ERROR", WIFI_L_ERROR }, { "FAIL", WIFI_L_FAIL }, { "ready", WIFI_L_READY },
        { "WIFI CONNECTED", WIFI_L_CONNECTED }, { "WIFI GOT IP", WIFI_L_GOT_IP }, { "WIFI DISCONNECT", WIFI_L_DISCONNECT },
        { "+CWJAP:2", WIFI_L_CWJAP }, { "+PING:23", WIFI_L_PING }, { "+PING:TIMEOUT", WIFI_L_PING_TIMEOUT },
        { "+CIPDOMAIN:\"142.250.0.1\"", WIFI_L_DOMAIN },
        { "+CIPSTA:ip:\"192.168.1.23\"", WIFI_L_STA_IP }, { "+CIPSTA:gateway:\"192.168.1.1\"", WIFI_L_STA_GW },
        { "+CIPSTA:netmask:\"255.255.255.0\"", WIFI_L_STA_MASK },
        { "AT version:2.2.0.0(s-ec2dec2 - ESP32 - Jun  7 2021 10:49:49)", WIFI_L_AT_VER },
        { "SDK version:v4.2.2-76-gefa6eca", WIFI_L_SDK_VER },
        /* AT 1.1.3.0 向け(2026-11-25 追加) */
        { "+CWLAP:(3,\"x\",-50,\"aa:bb:cc:dd:ee:ff\",6)", WIFI_L_CWLAP }, { "+CWLAP:3,\"x\",-50,\"aa:bb:cc:dd:ee:ff\",6", WIFI_L_CWLAP },
        { "+CWLAP:", WIFI_L_CWLAP },
        { "+CIFSR:STAIP,\"192.168.1.23\"", WIFI_L_STA_IP }, { "+CIFSR:STAIP,192.168.1.23", WIFI_L_STA_IP },
        { "+CIPDOMAIN:142.250.0.1", WIFI_L_DOMAIN },
        { "+CIFSR:APIP,\"192.168.4.1\"", WIFI_L_OTHER }, { "+CIFSR:APMAC,\"aa:bb:cc:dd:ee:ff\"", WIFI_L_OTHER },
        { "+CIFSR:STAMAC,\"aa:bb:cc:dd:ee:ff\"", WIFI_L_OTHER }, { "+CIFSR:STAIP,\"1.2.3", WIFI_L_OTHER },
        { "+CWLAPOPT:1", WIFI_L_OTHER }, { "+CWLAPX:(3)", WIFI_L_OTHER },
        { "AT+CWLAP", WIFI_L_ECHO }, { "AT+CWQAP", WIFI_L_ECHO }, { "AT+CWLAPOPT=1,31", WIFI_L_ECHO }, { "AT+CIFSR", WIFI_L_ECHO },
        /* エコーバック */
        { "AT", WIFI_L_ECHO }, { "ATE0", WIFI_L_ECHO }, { "AT+GMR", WIFI_L_ECHO }, { "AT+CWJAP=\"a\",\"b\"", WIFI_L_ECHO },
        { "AT+PING=\"8.8.8.8\"", WIFI_L_ECHO },
        /* 誤認しない */
        { "", WIFI_L_OTHER }, { "OK ", WIFI_L_OTHER }, { " OK", WIFI_L_OTHER }, { "ok", WIFI_L_OTHER }, { "BOOKS", WIFI_L_OTHER },
        { "+PING:OK", WIFI_L_OTHER }, { "SEND OK ", WIFI_L_OTHER }, { "ERROR1", WIFI_L_OTHER }, { "FAILED", WIFI_L_OTHER },
        { "ready!", WIFI_L_OTHER }, { "not ready", WIFI_L_OTHER }, { "WIFI GOT IPV6", WIFI_L_OTHER },
        { "busy p...", WIFI_L_OTHER }, { "busy s...", WIFI_L_OTHER }, { "ATX", WIFI_L_OTHER }, { "A", WIFI_L_OTHER }, { "AT ", WIFI_L_OTHER },
        { "compile time(3a696ba):Jun  7 2021 12:13:02", WIFI_L_OTHER }, { "Bin version:2.2.0(WROOM-32)", WIFI_L_OTHER },
        { "+CWJAP:\"ssid\",\"aa:bb:cc:dd:ee:ff\",6,-50,0,0,0", WIFI_L_OTHER },      /* CWJAP? の応答。SSID を含む */
        { "+CWJAP:", WIFI_L_OTHER }, { "+CWJAP:x", WIFI_L_OTHER }, { "+PING:", WIFI_L_OTHER }, { "+PING:abc", WIFI_L_OTHER },
        { "+CIPSTA:ip:", WIFI_L_OTHER }, { "+CIPSTA:mac:\"aa:bb\"", WIFI_L_OTHER }
    };
    wifi_ev_t ev;
    size_t    i;

    TEST("H-W2 分類");
    for (i = 0u; i < (sizeof T / sizeof T[0]); i++) {
        wifi_parse_line(T[i].line, &ev);
        if (ev.kind != T[i].kind) {
            printf("  line=[%s] kind=%d want=%d\n", T[i].line, (int)ev.kind, (int)T[i].kind);
        }
        CHECK_EQ(ev.kind, T[i].kind);
    }
    wifi_parse_line(NULL, &ev);
    CHECK_EQ(ev.kind, WIFI_L_OTHER);
}

static void t_values(void)
{
    wifi_ev_t ev;
    char      s[200];
    size_t    i;
    static const char *const BAD[] = {
        "+PING:", "+PING:abc", "+PING:12x", "+PING:-5", "+PING: 5", "+PING:1234567890", "+PING:99999999999999999999",
        "+CWJAP:", "+CWJAP:\"a\"", "+CWJAP:12345678901",
        "+CIPDOMAIN:\"142.250.0.1", "+CIPDOMAIN:142.250.0.1\"", "+CIPDOMAIN:\"142.250.0.256\"",
        "+CIFSR:STAIP,\"1.2.3", "+CIFSR:STAIP,1.2.3.4\"", "+CIFSR:STAIP,", "+CIFSR:STAIP,\"1.2.3.4\"x", "+CIFSR:STAIP,\"1.2.3.256\"",
        "+CIFSR:STAIP,1.2.3.4x", "+CIFSR:STAIP,1.2.3", "+CIPDOMAIN:\"1.2.3\"",
        "+CIPDOMAIN:\"1.2.3.4.5\"", "+CIPDOMAIN:\"1.2.3.4\"x", "+CIPDOMAIN:\"1234.2.3.4\"", "+CIPDOMAIN:\"::1\"", "+CIPDOMAIN:\"\"",
        "+CIPDOMAIN:\"1..3.4\"", "+CIPDOMAIN:\"a.b.c.d\"", "+CIPDOMAIN:\"1.2.3.4\",\"x\"", "+CIPSTA:ip:\"192.168.1.\"", "+CIPSTA:ip:\"-1.2.3.4\""
    };

    TEST("H-W3 値の取り出し");
    wifi_parse_line("+PING:23", &ev);                      CHECK_EQ(ev.kind, WIFI_L_PING); CHECK_EQ(ev.num, 23);
    wifi_parse_line("+PING:0", &ev);                       CHECK_EQ(ev.kind, WIFI_L_PING); CHECK_EQ(ev.num, 0);
    wifi_parse_line("+PING:123456789", &ev);               CHECK_EQ(ev.kind, WIFI_L_PING); CHECK_EQ(ev.num, 123456789);
    wifi_parse_line("+PING:TIMEOUT", &ev);                 CHECK_EQ(ev.kind, WIFI_L_PING_TIMEOUT);
    wifi_parse_line("+CWJAP:2", &ev);                      CHECK_EQ(ev.kind, WIFI_L_CWJAP); CHECK_EQ(ev.num, 2);
    wifi_parse_line("+CWJAP:4", &ev);                      CHECK_EQ(ev.num, 4);
    wifi_parse_line("+CIPDOMAIN:\"142.250.0.1\"", &ev);    CHECK_EQ(ev.kind, WIFI_L_DOMAIN); CHECK(strcmp(ev.text, "142.250.0.1") == 0);
    wifi_parse_line("+CIPDOMAIN:142.250.0.1", &ev);        CHECK_EQ(ev.kind, WIFI_L_DOMAIN); CHECK(strcmp(ev.text, "142.250.0.1") == 0);   /* 引用符なし(1.x) */
    wifi_parse_line("+CIFSR:STAIP,\"192.168.1.23\"", &ev); CHECK_EQ(ev.kind, WIFI_L_STA_IP); CHECK(strcmp(ev.text, "192.168.1.23") == 0);
    wifi_parse_line("+CIFSR:STAIP,192.168.1.23", &ev);     CHECK_EQ(ev.kind, WIFI_L_STA_IP); CHECK(strcmp(ev.text, "192.168.1.23") == 0);
    wifi_parse_line("+CIFSR:STAIP,\"0.0.0.0\"", &ev);      CHECK_EQ(ev.kind, WIFI_L_STA_IP); CHECK(strcmp(ev.text, "0.0.0.0") == 0);
    wifi_parse_line("+CWLAP:(3,\"" NEIGH_MARK "\",-50,\"de:ad:be:ef:00:01\",6)", &ev);
    CHECK_EQ(ev.kind, WIFI_L_CWLAP); CHECK_EQ(ev.text[0], 0);                          /* 分類だけ。SSID を ev に入れない */
    wifi_parse_line("+CIPSTA:ip:\"192.168.1.23\"", &ev);   CHECK_EQ(ev.kind, WIFI_L_STA_IP); CHECK(strcmp(ev.text, "192.168.1.23") == 0);
    wifi_parse_line("+CIPSTA:ip:\"0.0.0.0\"", &ev);        CHECK_EQ(ev.kind, WIFI_L_STA_IP); CHECK(strcmp(ev.text, "0.0.0.0") == 0);
    wifi_parse_line("+CIPSTA:gateway:\"192.168.1.1\"", &ev); CHECK(strcmp(ev.text, "192.168.1.1") == 0);
    wifi_parse_line("+CIPSTA:netmask:\"255.255.255.0\"", &ev); CHECK(strcmp(ev.text, "255.255.255.0") == 0);
    wifi_parse_line("AT version:2.2.0.0(s-ec2dec2 - ESP32 - Jun  7 2021 10:49:49)", &ev);
    CHECK_EQ(ev.kind, WIFI_L_AT_VER); CHECK_EQ(ev.num, 2); CHECK_EQ(ev.num2, 2);
    CHECK_EQ(strlen(ev.text), WIFI_VAL_MAX);
    CHECK(strncmp(ev.text, "2.2.0.0(s-ec2dec2_-_", 20) == 0);        /* 空白は _ になる。40 文字で切る */
    wifi_parse_line("AT version:3.1", &ev);                CHECK_EQ(ev.num, 3); CHECK_EQ(ev.num2, 1);
    wifi_parse_line("AT version:1.7.4.0(abc)", &ev);       CHECK_EQ(ev.num, 1); CHECK_EQ(ev.num2, 7);
    wifi_parse_line("AT version:12", &ev);                 CHECK_EQ(ev.num, 12); CHECK_EQ(ev.num2, 0);
    wifi_parse_line("AT version:2.", &ev);                 CHECK_EQ(ev.num, 2); CHECK_EQ(ev.num2, 0);
    wifi_parse_line("AT version:", &ev);                   CHECK_EQ(ev.kind, WIFI_L_AT_VER); CHECK_EQ(ev.num, 0); CHECK_EQ(ev.text[0], 0);
    wifi_parse_line("AT version:x.y", &ev);                CHECK_EQ(ev.kind, WIFI_L_AT_VER); CHECK_EQ(ev.num, 0);
    wifi_parse_line("AT version:99999999999.1", &ev);      CHECK_EQ(ev.kind, WIFI_L_AT_VER); CHECK_EQ(ev.num, 0);   /* 桁あふれ */
    wifi_parse_line("SDK version:v4.2.2-76-gefa6eca", &ev); CHECK_EQ(ev.kind, WIFI_L_SDK_VER); CHECK(strcmp(ev.text, "v4.2.2-76-gefa6eca") == 0);
    wifi_parse_line("SDK version:a\x01\x7f\xff" "b", &ev); CHECK(strcmp(ev.text, "a___b") == 0);
    for (i = 0u; i < (sizeof BAD / sizeof BAD[0]); i++) {
        wifi_parse_line(BAD[i], &ev);
        if (ev.kind != WIFI_L_OTHER) {
            printf("  line=[%s] kind=%d (OTHER のはず)\n", BAD[i], (int)ev.kind);
        }
        CHECK_EQ(ev.kind, WIFI_L_OTHER);
        CHECK_EQ(ev.text[0], 0);
    }
    /* 長い・壊れた入力で落ちない(ASAN が使えない環境では出力が正しいことだけ見る) */
    memset(s, 'A', sizeof s - 1u); s[sizeof s - 1u] = '\0';
    wifi_parse_line(s, &ev);                               CHECK_EQ(ev.kind, WIFI_L_OTHER);
    memcpy(s, "AT version:", 11u);
    wifi_parse_line(s, &ev);                               CHECK_EQ(ev.kind, WIFI_L_AT_VER); CHECK_EQ(strlen(ev.text), WIFI_VAL_MAX);
    memcpy(s, "+CIPDOMAIN:\"", 12u);
    wifi_parse_line(s, &ev);                               CHECK_EQ(ev.kind, WIFI_L_OTHER);
}

/* ================================================================ H-W4 AT+CWJAP */

static void t_cwjap(void)
{
    char   b[WIFI_CMD_MAX + 16];
    char   small[8];
    size_t n, L, k;
    const char *e;
    char   ss[40], pp[70];

    TEST("H-W4 wifi_build_cwjap");
    e = "AT+CWJAP=\"home\",\"pass\"";
    n = wifi_build_cwjap(b, sizeof b, "home", "pass");
    CHECK_EQ(n, strlen(e)); CHECK(strcmp(b, e) == 0);
    e = "AT+CWJAP=\"a\\,b\\\"c\\\\d\",\"x\\,y\\\"z\\\\w\"";
    n = wifi_build_cwjap(b, sizeof b, "a,b\"c\\d", "x,y\"z\\w");
    CHECK_EQ(n, strlen(e)); CHECK(strcmp(b, e) == 0);
    e = "AT+CWJAP=\"home\",\"\"";                                   /* パスワードが空(オープンな AP) */
    n = wifi_build_cwjap(b, sizeof b, "home", "");
    CHECK_EQ(n, strlen(e)); CHECK(strcmp(b, e) == 0);
    /* 空の SSID・NULL・制御文字は拒否 */
    memset(b, 'Q', sizeof b);
    CHECK_EQ(wifi_build_cwjap(b, sizeof b, "", "p"), 0); CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wifi_build_cwjap(b, sizeof b, NULL, "p"), 0);  CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wifi_build_cwjap(b, sizeof b, "s", NULL), 0);  CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wifi_build_cwjap(b, sizeof b, "a\nb", "p"), 0); CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wifi_build_cwjap(b, sizeof b, "s", "a\rb"), 0); CHECK_EQ(b[0], 0);
    CHECK_EQ(wifi_build_cwjap(NULL, 10u, "s", "p"), 0);
    CHECK_EQ(wifi_build_cwjap(small, 0u, "s", "p"), 0);
    /* バッファ不足: size = 1..L は全て 0 で buf[0] = 0、size = L + 1 で成功。ガード領域を壊さない */
    e = "AT+CWJAP=\"a\\,b\",\"c\\\"d\"";
    L = strlen(e);
    for (k = 1u; k <= (L + 1u); k++) {
        memset(b, 0x5A, sizeof b);
        n = wifi_build_cwjap(b, k, "a,b", "c\"d");
        if (k <= L) {
            CHECK_EQ(n, 0); CHECK_EQ(b[0], 0);
        } else {
            CHECK_EQ(n, L); CHECK(strcmp(b, e) == 0);
        }
        CHECK_EQ((unsigned char)b[k], 0x5Au);                       /* size の先は触らない */
    }
    /* 最長: SSID 32 文字・パスワード 63 文字の全部がエスケープされる = 10 + 64 + 3 + 126 + 1 = 204。\r\n を足しても WIFI_CMD_MAX に入る */
    memset(ss, 0, sizeof ss); memset(ss, '"', 32u);
    memset(pp, 0, sizeof pp); memset(pp, '\\', 63u);
    memset(b, 0x5A, sizeof b);
    n = wifi_build_cwjap(b, WIFI_CMD_MAX - 2u, ss, pp);
    CHECK_EQ(n, 204); CHECK_EQ(strlen(b), 204u); CHECK((n + 2u) <= WIFI_CMD_MAX);
    CHECK_EQ((unsigned char)b[WIFI_CMD_MAX - 2u], 0x5Au);
    memset(b, 0x5A, sizeof b);
    CHECK_EQ(wifi_build_cwjap(b, 204u, ss, pp), 0);                  /* NUL の分が足りない */
    CHECK_EQ((unsigned char)b[204], 0x5Au);
    CHECK_EQ(wifi_build_cwjap(b, 205u, ss, pp), 204);
}

/* ================================================================ H-W7 ログ行の書式 */

static const char *const STEPN[] = { "reset", "boot", "at", "echo", "gmr", "mode", "qap", "lapopt", "scan", "join", "ip", "dns", "ping_ip", "ping_dns" };
static const char *const RESN[]  = { "start", "ok", "fail", "timeout", "skip" };
static const char *const KEYN[]  = { "ver", "sdk", "ip", "gw", "mask", "dns", "ping", "scan", "ap", "oth" };
static const char *const STATN[] = { "idle", "run", "done" };

static int in_list(const char *s, const char *const *l, size_t n)
{
    size_t i;

    for (i = 0u; i < n; i++) {
        if (strcmp(s, l[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* design-uart.md §12.3 の正規表現と同じ集合を、「sscanf で読み、同じ書式で書き直して一致」で調べる(<regex.h> が無いため)。1 = 適合 */
static int fm_step(const char *s)
{
    char st[24], rs[24], again[160];
    unsigned long t, m, c;
    int n = -1;

    if (sscanf(s, "[wifi] step=%23s res=%23s try=%lu ms=%lu code=%lu%n", st, rs, &t, &m, &c, &n) != 5) { return 0; }
    if ((n < 0) || (strcmp(s + n, "\r\n") != 0)) { return 0; }
    if (!in_list(st, STEPN, 14u) || !in_list(rs, RESN, 5u)) { return 0; }
    snprintf(again, sizeof again, "[wifi] step=%s res=%s try=%lu ms=%lu code=%lu\r\n", st, rs, t, m, c);
    return strcmp(again, s) == 0;
}

static int fm_result(const char *s)
{
    char pf[8], st[24], again[160];
    unsigned long a, b;
    int n = -1;

    if (sscanf(s, "[wifi] result=%7s stage=%23s ping_ip=%lu ping_dns=%lu%n", pf, st, &a, &b, &n) != 4) { return 0; }
    if ((n < 0) || (strcmp(s + n, "\r\n") != 0)) { return 0; }
    if (((strcmp(pf, "pass") != 0) && (strcmp(pf, "fail") != 0)) || ((strcmp(st, "none") != 0) && !in_list(st, STEPN, 14u))) { return 0; }
    snprintf(again, sizeof again, "[wifi] result=%s stage=%s ping_ip=%lu ping_dns=%lu\r\n", pf, st, a, b);
    return strcmp(again, s) == 0;
}

static int fm_state(const char *s)
{
    char sv[8], st[24], again[160];
    unsigned long b;
    int n = -1;

    if (sscanf(s, "[wifi] state=%7s step=%23s bytes=%lu%n", sv, st, &b, &n) != 3) { return 0; }
    if ((n < 0) || (strcmp(s + n, "\r\n") != 0)) { return 0; }
    if (!in_list(sv, STATN, 3u) || ((strcmp(st, "none") != 0) && !in_list(st, STEPN, 14u))) { return 0; }
    snprintf(again, sizeof again, "[wifi] state=%s step=%s bytes=%lu\r\n", sv, st, b);
    return strcmp(again, s) == 0;
}

static int fm_val(const char *s)
{
    char key[12];
    int n = -1;
    size_t i, len;

    if (sscanf(s, "[wifiv] key=%11s val=%n", key, &n) != 1) { return 0; }
    if ((n < 0) || !in_list(key, KEYN, 10u)) { return 0; }
    len = strlen(s);
    if ((len < (size_t)n + 2u) || (strcmp(s + len - 2u, "\r\n") != 0) || ((len - 2u - (size_t)n) > WIFI_VAL_MAX)) { return 0; }
    for (i = (size_t)n; i < (len - 2u); i++) {
        if (((unsigned char)s[i] < 0x21u) || ((unsigned char)s[i] > 0x7Eu)) { return 0; }
    }
    return 1;
}

/* [wifiv] の1行が R-WIFIV(key の集合・val は 0x21〜0x7E で 40 文字以内)に適合 */
static int fm_val_line_ok(const char *s)
{
    return fm_val(s);
}

static void t_fmt(void)
{
    static const uint32_t V[] = { 0u, 1u, 12345u, 0xFFFFFFFFu };
    char   b[APPLOG_LINE_MAX];
    char   g[WIFI_LOG_MAX + 8];
    size_t n, k, maxlen = 0u;
    int    s, r, a, i;
    char   val41[48];

    TEST("H-W7 wifi_fmt_* の書式");
    for (s = 1; s <= 14; s++) {
        for (r = 0; r < 5; r++) {
            for (a = 0; a < 4; a++) {
                for (i = 0; i < 4; i++) {
                    n = wifi_fmt_step(b, sizeof b, (wifi_step_t)s, (wifi_res_t)r, V[a], V[i], V[(a + i) % 4]);
                    CHECK(n > 0u);
                    CHECK_EQ(strlen(b), n);
                    CHECK(fm_step(b));
                    if (n > maxlen) { maxlen = n; }
                }
            }
        }
    }
    CHECK(maxlen < WIFI_LOG_MAX);                                  /* コアの out[] に入る */
    CHECK(maxlen < APPLOG_LINE_MAX);
    for (s = 0; s < 15; s++) {
        for (a = 0; a < 4; a++) {
            n = wifi_fmt_result(b, sizeof b, (a & 1) != 0, (wifi_step_t)s, V[a], V[3 - a]);
            CHECK(n > 0u); CHECK(fm_result(b)); CHECK(n < WIFI_LOG_MAX);
            for (r = 0; r < 3; r++) {
                n = wifi_fmt_state(b, sizeof b, (wifi_state_t)r, (wifi_step_t)s, V[a]);
                CHECK(n > 0u); CHECK(fm_state(b)); CHECK(n < WIFI_LOG_MAX);
            }
        }
    }
    for (r = 0; r < 10; r++) {
        n = wifi_fmt_val(b, sizeof b, (wifi_key_t)r, "");               CHECK(n > 0u); CHECK(fm_val(b));
        n = wifi_fmt_val(b, sizeof b, (wifi_key_t)r, "192.168.1.23");   CHECK(n > 0u); CHECK(fm_val(b));
        memset(val41, 'v', 40u); val41[40] = '\0';
        n = wifi_fmt_val(b, sizeof b, (wifi_key_t)r, val41);            CHECK(n > 0u); CHECK(fm_val(b)); CHECK(n < WIFI_LOG_MAX);
        val41[40] = 'v'; val41[41] = '\0';
        CHECK_EQ(wifi_fmt_val(b, sizeof b, (wifi_key_t)r, val41), 0);   CHECK_EQ(b[0], 0);      /* 41 文字は拒否 */
    }
    /* 範囲外の文字は _ になり、改行を入れられない */
    n = wifi_fmt_val(b, sizeof b, WIFI_KEY_VER, "a b\r\n\x01\x7f\xff" "z");
    CHECK(n > 0u); CHECK(fm_val(b)); CHECK(strstr(b, "val=a_b_____z\r\n") != NULL);
    n = wifi_fmt_help(b, sizeof b);
    CHECK_EQ(n, strlen("[help] d=run n=boot p=at s=state ?=help\r\n")); CHECK(strcmp(b, "[help] d=run n=boot p=at s=state ?=help\r\n") == 0);
    /* 無効な引数・バッファ不足は 0 で buf[0] = 0 */
    memset(b, 'Q', sizeof b); CHECK_EQ(wifi_fmt_step(b, sizeof b, WIFI_STEP_NONE, WIFI_RES_OK, 1, 1, 1), 0); CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wifi_fmt_step(b, sizeof b, WIFI_STEP_END, WIFI_RES_OK, 1, 1, 1), 0);  CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wifi_fmt_step(b, sizeof b, WIFI_STEP_AT, WIFI_RES_END, 1, 1, 1), 0);  CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wifi_fmt_result(b, sizeof b, true, WIFI_STEP_END, 0, 0), 0);          CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wifi_fmt_state(b, sizeof b, WIFI_STATE_END, WIFI_STEP_AT, 0), 0);     CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wifi_fmt_val(b, sizeof b, WIFI_KEY_END, "x"), 0);                     CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wifi_fmt_val(b, sizeof b, WIFI_KEY_IP, NULL), 0);                     CHECK_EQ(b[0], 0);
    CHECK_EQ(wifi_fmt_step(NULL, 10u, WIFI_STEP_AT, WIFI_RES_OK, 1, 1, 1), 0);
    CHECK_EQ(wifi_fmt_step(b, 0u, WIFI_STEP_AT, WIFI_RES_OK, 1, 1, 1), 0);
    CHECK_EQ(wifi_fmt_val(NULL, 10u, WIFI_KEY_IP, "x"), 0);
    CHECK_EQ(wifi_fmt_help(NULL, 10u), 0);
    /* 大きさの境界: size = 1..len は 0(buf[0] = 0、ガード無傷)、len + 1 で成功 */
    {
        size_t len = wifi_fmt_step(b, sizeof b, WIFI_STEP_PING_DNS, WIFI_RES_TIMEOUT, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
        for (k = 1u; k <= (len + 1u); k++) {
            memset(g, 0x5A, sizeof g);
            n = wifi_fmt_step(g, k, WIFI_STEP_PING_DNS, WIFI_RES_TIMEOUT, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
            if (k <= len) { CHECK_EQ(n, 0); CHECK_EQ(g[0], 0); } else { CHECK_EQ(n, len); }
            CHECK_EQ((unsigned char)g[k], 0x5Au);
        }
        len = wifi_fmt_val(b, sizeof b, WIFI_KEY_MASK, "255.255.255.255");
        for (k = 1u; k <= (len + 1u); k++) {
            memset(g, 0x5A, sizeof g);
            n = wifi_fmt_val(g, k, WIFI_KEY_MASK, "255.255.255.255");
            if (k <= len) { CHECK_EQ(n, 0); CHECK_EQ(g[0], 0); } else { CHECK_EQ(n, len); }
            CHECK_EQ((unsigned char)g[k], 0x5Au);
        }
    }
    CHECK(strcmp(wifi_step_name(WIFI_STEP_PING_IP), "ping_ip") == 0);
    CHECK(strcmp(wifi_step_name(WIFI_STEP_NONE), "none") == 0);
    CHECK(strcmp(wifi_step_name(WIFI_STEP_SCAN), "scan") == 0);
    CHECK(strcmp(wifi_step_name(WIFI_STEP_QAP), "qap") == 0);
    CHECK(strcmp(wifi_step_name(WIFI_STEP_LAPOPT), "lapopt") == 0);
    CHECK_EQ(WIFI_STEP_END, 21);                                   /* [hb] の scr は 0〜20(取得の 5 step + wx_sslconf) */
    CHECK_EQ(WIFI_STEP_PING_DNS, 14);
    CHECK_EQ(WIFI_STEP_WX_MUX, 15);
    CHECK_EQ(WIFI_STEP_WX_SSLCONF, 17);
    CHECK_EQ(WIFI_STEP_WX_TCP, 18);
    CHECK_EQ(WIFI_STEP_WX_RECV, 20);
    CHECK(strcmp(wifi_step_name(WIFI_STEP_WX_SSLCONF), "wx_sslconf") == 0);
    CHECK_EQ(WIFI_STEP_SCAN, 9);
    CHECK(wifi_step_name(WIFI_STEP_END) == NULL);
}

/* ================================================================ H-W5 / H-W6 状態機械 */

static void t_normal(void)
{
    static const char *const OKS[] = { "step=reset res=ok", "step=boot res=ok", "step=at res=ok try=1", "step=echo res=ok", "step=gmr res=ok",
        "step=mode res=ok", "step=qap res=ok", "step=lapopt res=ok", "step=scan res=ok try=1", "step=join res=ok try=1", "step=ip res=ok",
        "step=dns res=ok", "step=ping_ip res=ok", "step=ping_dns res=ok" };
    int i, prev = -1, at;

    TEST("H-W5-1 正常系(14 step。store は無い)");
    begin(SSID_MARK, PASS_MARK);
    normal_rules();
    CHECK_EQ(wifi_core_state(&g_c), WIFI_STATE_IDLE);
    CHECK_EQ(wifi_core_step(&g_c), WIFI_STEP_NONE);
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    CHECK_EQ(count_with("res=start"), 14);
    for (i = 0; i < 14; i++) {
        at = first_with(OKS[i]);
        if (at <= prev) { printf("  順序・欠け: %s\n", OKS[i]); }
        CHECK(at > prev);
        prev = at;
    }
    CHECK_EQ(count_with("step=store"), 0);
    CHECK_EQ(count_with("res=fail"), 0);
    CHECK_EQ(count_with("res=timeout"), 0);
    CHECK_EQ(count_with("res=skip"), 0);
    CHECK_EQ(g_en_n, 2);
    CHECK_EQ(g_en_lvl[0], 0); CHECK_EQ(g_en_lvl[1], 1);
    CHECK((g_en_t[1] - g_en_t[0]) >= 20u); CHECK((g_en_t[1] - g_en_t[0]) < 40u);
    CHECK(first_with("[wifiv] key=ver val=1.1.3.0(5a40576_-_Nov_28_2018_12:50:55)\r\n") >= 0);
    CHECK(first_with("[wifiv] key=sdk val=v3.0.7\r\n") >= 0);
    CHECK(first_with("[wifiv] key=ip val=192.168.1.23\r\n") >= 0);
    CHECK_EQ(count_with("key=gw"), 0);                               /* AT+CIFSR には gw / mask が無い */
    CHECK_EQ(count_with("key=mask"), 0);
    CHECK(first_with("[wifiv] key=dns val=142.250.0.1\r\n") >= 0);
    CHECK(first_with("[wifiv] key=ping val=23\r\n") >= 0);
    CHECK(first_with("[wifiv] key=ping val=31\r\n") >= 0);
    /* scan: 自分の AP(rssi -52, ch 6, ecn 3)が見える。順序は scan → ap → step */
    CHECK(first_with("[wifiv] key=scan val=total=3,found=1,near=0,bad=0\r\n") >= 0);
    CHECK(first_with("[wifiv] key=ap val=ch=6,rssi=-52,ecn=3\r\n") >= 0);
    CHECK(first_with("step=scan res=ok try=1 ms=800 code=0") >= 0);
    CHECK(first_with("key=scan") < first_with("key=ap"));
    CHECK(first_with("key=ap") < first_with("step=scan res=ok"));
    /* oth(形): gmr の compile time 行 1 + ip の APIP・APMAC・STAMAC の 3 = 4。ほかの 8 本(ver sdk scan ap ip dns ping x2) */
    CHECK_EQ(count_with("key=oth"), 4);
    CHECK_EQ(count_with("[wifiv]"), 12);
    CHECK(first_with("step=join res=ok try=1 ms=3000 code=0") >= 0);
    CHECK(first_with("step=boot res=ok try=1 ms=300 code=0") >= 0);
    CHECK_EQ(g_nsend, 12);
    CHECK(strcmp(g_sent[0], "AT\r\n") == 0);
    CHECK(strcmp(g_sent[1], "ATE0\r\n") == 0);
    CHECK(strcmp(g_sent[2], "AT+GMR\r\n") == 0);
    CHECK(strcmp(g_sent[3], "AT+CWMODE=1\r\n") == 0);
    CHECK(strcmp(g_sent[4], "AT+CWQAP\r\n") == 0);
    CHECK(strcmp(g_sent[5], "AT+CWLAPOPT=1,31\r\n") == 0);
    CHECK(strcmp(g_sent[6], "AT+CWLAP\r\n") == 0);
    CHECK(strcmp(g_sent[7], "AT+CWJAP=\"" SSID_MARK "\",\"" PASS_MARK "\"\r\n") == 0);
    CHECK(strcmp(g_sent[8], "AT+CIFSR\r\n") == 0);
    CHECK(strcmp(g_sent[9], "AT+CIPDOMAIN=\"www.google.com\"\r\n") == 0);
    CHECK(strcmp(g_sent[10], "AT+PING=\"8.8.8.8\"\r\n") == 0);
    CHECK(strcmp(g_sent[11], "AT+PING=\"www.google.com\"\r\n") == 0);
    CHECK(!sent_has("SYSSTORE"));
    CHECK(!sent_has("CIPSTA"));
    CHECK_EQ(wifi_core_state(&g_c), WIFI_STATE_DONE);
    CHECK_EQ(wifi_core_step(&g_c), WIFI_STEP_NONE);
    CHECK(wifi_core_bytes(&g_c) > 100u);
    CHECK(!core_has_secret());                                      /* 送信後・エコー受信後・scan 後に、構造体に秘密が残らない */
    CHECK(!log_has_secret());
    CHECK(g_echo_fed_secret > 0);                                   /* 試験の前提: エコーは実際に流れた */
    /* 終わった後も、もう一度動かせる(同じコア) */
    g_nlog = 0;
    run(WIFI_RANGE_AT, 5000u);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));
}

static void t_at_retry(void)
{
    TEST("H-W5-2/3 AT の再試行と打ち切り");
    begin(SSID_MARK, PASS_MARK);
    rule("AT", 3, 0, NULL);                                          /* 3 回無応答 */
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=at res=timeout try=1 ms=1000 code=0") >= 0);
    CHECK(first_with("step=at res=timeout try=2 ms=1000 code=0") >= 0);
    CHECK(first_with("step=at res=timeout try=3 ms=1000 code=0") >= 0);
    CHECK(first_with("step=at res=start try=4") >= 0);
    CHECK(first_with("step=at res=ok try=4 ms=20") >= 0);
    CHECK_EQ(count_with("step=at res=start"), 4);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));

    begin(SSID_MARK, PASS_MARK);
    rule("AT", 0, 0, NULL);                                          /* 常に無応答 */
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK_EQ(count_with("step=at res=start"), 5);
    CHECK_EQ(count_with("step=at res=timeout"), 5);
    CHECK_EQ(count_with("step=echo"), 0);
    CHECK(last_is("[wifi] result=fail stage=at ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(wifi_core_state(&g_c), WIFI_STATE_DONE);
    /* 打ち切った後も、やり直せる */
    g_nrules = 0; memset(g_used, 0, sizeof g_used); g_nlog = 0;
    normal_rules();
    run(WIFI_RANGE_BOOT, 20000u);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));

    /* AT が ERROR を返す(ファームはあるが AT ではない): 5 回で打ち切り。間隔は 300ms */
    begin(SSID_MARK, PASS_MARK);
    rule("AT", 0, 10, "ERROR\r\n");
    run(WIFI_RANGE_AT, 60000u);
    CHECK_EQ(count_with("step=at res=fail"), 5);
    CHECK(last_is("[wifi] result=fail stage=at ping_ip=0 ping_dns=0\r\n"));
}

static void t_join(void)
{
    TEST("H-W5-4/5 join の失敗と再試行");
    /* +CWJAP:2(パスワード違い)= 再試行しない */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWJAP=*", 0, 500, "+CWJAP:2\r\n\r\nFAIL\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK_EQ(count_with("step=join res=start"), 1);
    CHECK(first_with("step=join res=fail try=1 ms=500 code=2") >= 0);
    CHECK(last_is("[wifi] result=fail stage=join ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(count_with("step=ip"), 0);
    /* +CWJAP:4 を 1 回 → 再試行して成功 */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWJAP=*", 1, 500, "+CWJAP:4\r\n\r\nFAIL\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=join res=fail try=1 ms=500 code=4") >= 0);
    CHECK(first_with("step=join res=start try=2") >= 0);
    CHECK(first_with("step=join res=ok try=2") >= 0);
    CHECK_EQ(count_with("step=join res=start"), 2);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    /* いつも +CWJAP:4 / :1 → 1 回だけ再試行して打ち切り */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWJAP=*", 0, 500, "+CWJAP:4\r\n\r\nFAIL\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK_EQ(count_with("step=join res=start"), 2);
    CHECK_EQ(count_with("step=join res=fail"), 2);
    CHECK(last_is("[wifi] result=fail stage=join ping_ip=0 ping_dns=0\r\n"));
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWJAP=*", 0, 500, "+CWJAP:1\r\n\r\nERROR\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK_EQ(count_with("step=join res=start"), 2);
    CHECK(first_with("step=join res=fail try=2 ms=500 code=1") >= 0);
    /* +CWJAP:3(AP が見つからない)は再試行しない */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWJAP=*", 0, 500, "+CWJAP:3\r\n\r\nFAIL\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK_EQ(count_with("step=join res=start"), 1);
    CHECK(first_with("step=join res=fail try=1 ms=500 code=3") >= 0);
    /* 無応答(コア側のタイムアウト 20 秒)は再試行しない */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWJAP=*", 0, 0, NULL);
    normal_rules();
    run(WIFI_RANGE_ALL, 90000u);
    CHECK_EQ(count_with("step=join res=start"), 1);
    CHECK(first_with("step=join res=timeout try=1 ms=20000 code=0") >= 0);
    CHECK(last_is("[wifi] result=fail stage=join ping_ip=0 ping_dns=0\r\n"));
    /* すでに接続済みで WIFI GOT IP が出ずに OK だけ来ても、join は成功(ip で確かめる) */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWJAP=*", 0, 100, "OK\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=join res=ok") >= 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
}

static void t_ping_dns_boot(void)
{
    TEST("H-W5-6/7/8 ping 失敗・dns 失敗・boot のタイムアウト");
    begin(SSID_MARK, PASS_MARK);
    rule("AT+PING=\"8.8.8.8\"", 0, 100, "+PING:TIMEOUT\r\n\r\nERROR\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=ping_ip res=fail try=1 ms=100 code=201") >= 0);        /* 対応しているが届かない */
    CHECK_EQ(count_with("step=ping_dns"), 0);
    CHECK(last_is("[wifi] result=fail stage=ping_ip ping_ip=0 ping_dns=0\r\n"));
    /* +PING: が来ずに ERROR だけ = AT+PING 未対応の疑い(code=200) */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+PING=\"8.8.8.8\"", 0, 100, "ERROR\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=ping_ip res=fail try=1 ms=100 code=200") >= 0);
    CHECK(last_is("[wifi] result=fail stage=ping_ip ping_ip=0 ping_dns=0\r\n"));
    begin(SSID_MARK, PASS_MARK);
    rule("AT+PING=\"www.google.com\"", 0, 100, "ERROR\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=ping_dns res=fail try=1 ms=100 code=200") >= 0);
    CHECK(last_is("[wifi] result=fail stage=ping_dns ping_ip=23 ping_dns=0\r\n"));
    /* +PING:TIMEOUT だけで ERROR が来ない(無応答でタイムアウト)でも、code は 201 */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+PING=\"8.8.8.8\"", 0, 100, "+PING:TIMEOUT\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 90000u);
    CHECK(first_with("step=ping_ip res=timeout try=1 ms=10000 code=201") >= 0);
    /* ERROR の code は ping 以外の step に漏れない: dns の ERROR は code=0 */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CIPDOMAIN=*", 0, 50, "ERROR\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=dns res=fail try=1 ms=50 code=0") >= 0);
    /* +PING: が無いまま OK は成功にしない */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+PING=\"8.8.8.8\"", 0, 100, "OK\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(last_is("[wifi] result=fail stage=ping_ip ping_ip=0 ping_dns=0\r\n"));
    /* ping_ip は通り、ping_dns が失敗 */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+PING=\"www.google.com\"", 0, 100, "+PING:TIMEOUT\r\n\r\nERROR\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(last_is("[wifi] result=fail stage=ping_dns ping_ip=23 ping_dns=0\r\n"));
    /* ping が無応答(タイムアウト)でも打ち切り(続行しない) */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+PING=\"8.8.8.8\"", 0, 0, NULL);
    normal_rules();
    run(WIFI_RANGE_ALL, 90000u);
    CHECK(first_with("step=ping_ip res=timeout try=1 ms=10000") >= 0);
    CHECK(last_is("[wifi] result=fail stage=ping_ip ping_ip=0 ping_dns=0\r\n"));
    /* dns が失敗しても ping_dns へ進む */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CIPDOMAIN=*", 0, 50, "ERROR\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=dns res=fail") >= 0);
    CHECK(first_with("step=ping_dns res=ok") >= 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    /* boot: ready が来なくても at へ進む */
    begin(SSID_MARK, PASS_MARK);
    g_ready_en = 0;
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=boot res=timeout try=1 ms=5000 code=0") >= 0);
    CHECK(first_with("step=at res=ok") >= 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    /* echo / gmr / qap / lapopt / ip が失敗でも続行 */
    begin(SSID_MARK, PASS_MARK);
    rule("ATE0", 0, 10, "ERROR\r\n");
    rule("AT+GMR", 0, 10, "ERROR\r\n");
    rule("AT+CWQAP", 0, 10, "ERROR\r\n");
    rule("AT+CWLAPOPT=1,31", 0, 10, "ERROR\r\n");
    rule("AT+CIFSR", 0, 10, "ERROR\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=echo res=fail") >= 0);
    CHECK(first_with("step=gmr res=fail") >= 0);
    CHECK(first_with("step=qap res=fail") >= 0);
    CHECK(first_with("step=lapopt res=fail") >= 0);
    CHECK(first_with("step=ip res=fail") >= 0);
    CHECK(first_with("step=scan res=ok") >= 0);                      /* qap / lapopt が失敗でも scan は動く */
    CHECK_EQ(count_with("key=ver"), 0);
    CHECK_EQ(count_with("key=ip"), 0);
    CHECK_EQ(count_with("[wifiv] key=dns"), 1);
    CHECK_EQ(count_with("[wifiv] key=ping"), 2);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    /* ip が OK だけ(STAIP なし)でも進む */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CIFSR", 0, 10, "OK\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=ip res=ok") >= 0);
    CHECK_EQ(count_with("key=ip"), 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    /* mode の失敗は打ち切り */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWMODE=1", 0, 10, "ERROR\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(last_is("[wifi] result=fail stage=mode ping_ip=0 ping_dns=0\r\n"));
}

static void t_unsolicited_late(void)
{
    TEST("H-W5-9/10 勝手に出る行・遅れて来た OK");
    begin(SSID_MARK, PASS_MARK);
    rule("AT", 1, 20, "WIFI DISCONNECT\r\nWIFI CONNECTED\r\nWIFI GOT IP\r\nOK\r\n");
    rule("AT+CWMODE=1", 1, 20, "WIFI DISCONNECT\r\nready\r\nbusy p...\r\nOK\r\n");
    rule("AT+CIFSR", 1, 20, "WIFI DISCONNECT\r\n+CIFSR:STAIP,\"1.2.3.4\"\r\nready\r\nOK\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    CHECK_EQ(count_with("res=fail"), 0);
    CHECK_EQ(count_with("res=timeout"), 0);
    /* gmr の OK が 2.2 秒に遅れる(タイムアウト 2 秒の後)。次の mode の成功に数えられない。mode は無応答(= 打ち切り) */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+GMR", 1, 2200, "OK\r\n");
    rule("AT+CWMODE=1", 1, 0, NULL);
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=gmr res=timeout try=1 ms=2000") >= 0);
    CHECK(first_with("step=mode res=timeout try=1 ms=2000") >= 0);
    CHECK_EQ(count_with("step=mode res=ok"), 0);
    CHECK(last_is("[wifi] result=fail stage=mode ping_ip=0 ping_dns=0\r\n"));
    /* 待っていない時(reset・boot の前後)に来た行は無視 */
    begin(SSID_MARK, PASS_MARK);
    normal_rules();
    CHECK(wifi_core_start(&g_c, WIFI_RANGE_BOOT, g_now));
    FEED("OK\r\nERROR\r\nFAIL\r\nWIFI DISCONNECT\r\n");           /* EN_LOW の間 */
    run_until_done(30000u);
    CHECK_EQ(count_with("res=fail"), 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));
}

static void t_ranges(void)
{
    TEST("H-W5-11/12 範囲指定と実行中の再開始");
    begin(SSID_MARK, PASS_MARK);
    normal_rules();
    run(WIFI_RANGE_AT, 20000u);
    CHECK_EQ(g_en_n, 0);
    CHECK_EQ(g_nsend, 1);
    CHECK_EQ(g_nlog, 3);
    CHECK(first_with("[wifi] step=at res=start try=1 ms=0 code=0") == 0);
    CHECK(first_with("step=at res=ok") == 1);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));

    begin(SSID_MARK, PASS_MARK);
    normal_rules();
    run(WIFI_RANGE_BOOT, 20000u);
    CHECK_EQ(g_en_n, 2);
    CHECK_EQ(g_nsend, 1);
    CHECK_EQ(count_with("res=start"), 3);
    CHECK_EQ(count_with("step=echo"), 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));

    /* 実行中の再開始は無視(ログも出さない) */
    begin(SSID_MARK, PASS_MARK);
    normal_rules();
    CHECK(wifi_core_start(&g_c, WIFI_RANGE_ALL, g_now));
    {
        int n0, i;
        for (i = 0; i < 20; i++) { tick(); }
        n0 = g_nlog;
        CHECK_EQ(wifi_core_state(&g_c), WIFI_STATE_RUN);
        CHECK(!wifi_core_start(&g_c, WIFI_RANGE_AT, g_now));
        CHECK(!wifi_core_start(&g_c, WIFI_RANGE_BOOT, g_now));
        CHECK(!wifi_core_start(&g_c, WIFI_RANGE_ALL, g_now));
        CHECK_EQ(g_nlog, n0);
        CHECK(wifi_core_step(&g_c) != WIFI_STEP_NONE);
        {   /* 途中の状態行(s コマンド)の整形 */
            char b[WIFI_LOG_MAX];
            CHECK(wifi_fmt_state(b, sizeof b, wifi_core_state(&g_c), wifi_core_step(&g_c), wifi_core_bytes(&g_c)) > 0u);
            CHECK(fm_state(b));
            CHECK(strstr(b, "state=run") != NULL);
        }
    }
    run_until_done(60000u);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    CHECK_EQ(count_with("res=start"), 14);

    /* 範囲外の値 */
    begin(SSID_MARK, PASS_MARK);
    CHECK(!wifi_core_start(&g_c, (wifi_range_t)9, g_now));
    CHECK_EQ(wifi_core_state(&g_c), WIFI_STATE_IDLE);
}

static void t_no_secret_and_errors(void)
{
    char longs[130];

    TEST("H-W5-13/14/15 秘密なし・組み立て失敗・送信失敗");
    begin(NULL, NULL);
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=join res=skip try=1 ms=0 code=0") >= 0);
    CHECK_EQ(count_with("step=ip"), 0);
    CHECK_EQ(g_nsend, 7);                                            /* at echo gmr mode qap lapopt scan(秘密が無くても scan は動く) */
    CHECK(first_with("[wifiv] key=scan val=total=3,found=0,near=0,bad=0") >= 0);   /* 照合しない: found = near = 0 */
    CHECK(first_with("step=scan res=ok try=1 ms=800 code=2") >= 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));
    begin(SSID_MARK, NULL);
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=join res=skip") >= 0);

    /* 組み立てられない(エスケープで 222 を超える)→ 送らずに code=100 */
    memset(longs, '"', 120u); longs[120] = '\0';
    begin(longs, PASS_MARK);
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=join res=fail try=1 ms=0 code=100") >= 0);
    CHECK(!sent_has("CWJAP"));
    CHECK_EQ(g_nsend, 7);
    CHECK(last_is("[wifi] result=fail stage=join ping_ip=0 ping_dns=0\r\n"));
    begin("", PASS_MARK);                                            /* 空の SSID も同じ */
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(first_with("step=join res=fail try=1 ms=0 code=100") >= 0);

    /* 送信失敗: at は 5 回とも code=101 */
    begin(SSID_MARK, PASS_MARK);
    g_send_mode = WIFI_SEND_FAIL;
    normal_rules();
    run(WIFI_RANGE_AT, 60000u);
    CHECK_EQ(count_with("step=at res=fail"), 5);
    CHECK(first_with("step=at res=fail try=1") >= 0);
    CHECK_EQ(count_with("code=101"), 5);
    CHECK(last_is("[wifi] result=fail stage=at ping_ip=0 ping_dns=0\r\n"));

    /* 送信 BUSY: 捨てずに待つ。待ち時間は応答の時間に数えない */
    begin(SSID_MARK, PASS_MARK);
    g_busy_left = 7;
    normal_rules();
    run(WIFI_RANGE_AT, 60000u);
    CHECK_EQ(g_nsend, 1);
    CHECK(first_with("step=at res=ok try=1 ms=20") >= 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));
    /* BUSY が続く間はタイムアウトしない */
    begin(SSID_MARK, PASS_MARK);
    g_busy_left = 500;                                               /* 5 秒 */
    normal_rules();
    run(WIFI_RANGE_AT, 60000u);
    CHECK_EQ(count_with("res=timeout"), 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));
}

/* ================================================================ H-W10 wifi_parse_cwlap */

#define CWL(ssid)  "+CWLAP:(3,\"" ssid "\",-52,\"de:ad:be:ef:00:01\",6)"

static void t_cwlap(void)
{
    static const char *const ME = "MySsid_SENT";
    static const char *const JP = "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88";          /* テスト(UTF-8) */
    static const char *const JP2 = "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x89";         /* 最後の 1 字が違う */
    static const struct { const char *ssid; unsigned want; } N[] = {
        { "MySsid_SENT", 1u },
        /* near: 大文字小文字・空白・全角 */
        { "mysSID_sent", 2u }, { "MYSSID_SENT", 2u }, { " MySsid_SENT", 2u }, { "MySsid_SENT ", 2u }, { "My Ssid_SENT", 2u },
        { "My  Ssid _SENT", 2u },
        { "\xEF\xBC\xAD" "ySsid_SENT", 2u },                       /* 全角 M(U+FF2D) */
        { "MySsid\xEF\xBC\xBF" "SENT", 2u },                       /* 全角 _(U+FF3F) */
        { "\xEF\xBD\x8D" "ySsid_SENT", 2u },                       /* 全角 m(U+FF4D) */
        { "MySsid_SENT\xE3\x80\x80", 2u },                         /* 末尾の全角空白(U+3000) */
        { "\xE3\x80\x80" "MySsid_SENT", 2u },
        /* 違う(部分一致・前方一致・後方一致を near にしない) */
        { "MySsid_SEN", 0u }, { "MySsid_SENTx", 0u }, { "xMySsid_SENT", 0u }, { "MySsid", 0u }, { "SENT", 0u }, { "MySsid_SENT_2", 0u },
        { "OtherNet", 0u }, { "", 0u }, { "MySsid-SENT", 0u }, { "MyS5id_SENT", 0u }, { "MySsid_SENT\xEF\xBC\x80", 0u }  /* U+FF00 は範囲外 */
    };
    static const char *const BAD[] = {
        "+CWLAP:(3,MySsid_SENT,-52,\"de:ad:be:ef:00:01\",6)",       /* 引用符なしの SSID(ガイドの記載どおり)。読めない */
        "+CWLAP:3,MySsid_SENT,-52,de:ad:be:ef:00:01,6",
        "+CWLAP:(,\"a\",-52,\"m\",6)", "+CWLAP:(x,\"a\",-52,\"m\",6)",
        "+CWLAP:(3,\"a\",52,\"m\",6)", "+CWLAP:(3,\"a\",-,\"m\",6)",                          /* rssi が正・数字なし */
        "+CWLAP:(3,\"a\",-1234567890,\"m\",6)", "+CWLAP:(3,\"a\",-1000,\"m\",6)",             /* 桁あふれ */
        "+CWLAP:(3,\"a\",-52,\"m\",1234567890)", "+CWLAP:(3,\"a\",-52,\"m\",1000)",
        "+CWLAP:(100,\"a\",-52,\"m\",6)", "+CWLAP:(3000000000,\"a\",-52,\"m\",6)",
        "+CWLAP:(3,\"a\",-52,\"m\")", "+CWLAP:(3,\"a\",-52,\"m\",)", "+CWLAP:(3,\"a\",-52,\"m\",x)",
        "+CWLAP:(3,\"a\",-52,\"mac", "+CWLAP:(3,\"a\",-52,", "+CWLAP:(3,\"a\",-52", "+CWLAP:(3,\"a\",-", "+CWLAP:(3,\"a\"",    /* 途中で切れた */
        "+CWLAP:(3,\"My", "+CWLAP:(3,\"a\\", "+CWLAP:(3,\"a\\\"", "+CWLAP:(3,", "+CWLAP:(3", "+CWLAP:(", "+CWLAP:",
        "+CWLAP:(3,\"a,-52,\"m\",6)",                               /* 閉じ引用符の位置がずれる */
        "+CWLAP:(3,\"123456789012345678901234567890123\",-52,\"m\",6)",     /* SSID が 33 バイト */
        "+CWLAPX:(3,\"a\",-52,\"m\",6)", "CWLAP:(3,\"a\",-52,\"m\",6)", " +CWLAP:(3,\"a\",-52,\"m\",6)", ""
    };
    wifi_ap_t ap;
    char      b[200];
    char      ss[40];
    size_t    i;

    TEST("H-W10 wifi_parse_cwlap(読み方・exact / near / 違う・壊れた行・SSID を保存しない)");
    /* (a) ふつうの形 */
    CHECK(wifi_parse_cwlap(CWL("MySsid_SENT"), ME, &ap));
    CHECK_EQ(ap.ecn, 3); CHECK_EQ(ap.rssi, -52); CHECK_EQ(ap.ch, 6); CHECK_EQ(ap.match, 1);
    /* (b) 括弧なし・MAC の引用符なし・2.x の追加項目つき・値の境界 */
    CHECK(wifi_parse_cwlap("+CWLAP:3,\"MySsid_SENT\",-52,\"de:ad:be:ef:00:01\",6", ME, &ap));
    CHECK_EQ(ap.ecn, 3); CHECK_EQ(ap.rssi, -52); CHECK_EQ(ap.ch, 6); CHECK_EQ(ap.match, 1);
    CHECK(wifi_parse_cwlap("+CWLAP:(3,\"MySsid_SENT\",-52,\"de:ad:be:ef:00:01\",6,-6,0,4,4,3,7,0)", ME, &ap));
    CHECK_EQ(ap.ecn, 3); CHECK_EQ(ap.rssi, -52); CHECK_EQ(ap.ch, 6); CHECK_EQ(ap.match, 1);
    CHECK(wifi_parse_cwlap("+CWLAP:(3,\"MySsid_SENT\",-52,de:ad:be:ef:00:01,6)", ME, &ap));
    CHECK_EQ(ap.ch, 6); CHECK_EQ(ap.match, 1);
    CHECK(wifi_parse_cwlap("+CWLAP:(0,\"MySsid_SENT\",-100,\"de:ad:be:ef:00:01\",13)", ME, &ap));
    CHECK_EQ(ap.ecn, 0); CHECK_EQ(ap.rssi, -100); CHECK_EQ(ap.ch, 13); CHECK_EQ(ap.match, 1);
    CHECK(wifi_parse_cwlap("+CWLAP:(5,\"MySsid_SENT\",-1,\"m\",999)", ME, &ap));
    CHECK_EQ(ap.ecn, 5); CHECK_EQ(ap.rssi, -1); CHECK_EQ(ap.ch, 999);
    CHECK(wifi_parse_cwlap("+CWLAP:(3,\"MySsid_SENT\",-52,\"de:ad:be:ef:00:01\",6", ME, &ap));    /* 閉じ括弧が無くても、項目が読めていれば可 */
    CHECK_EQ(ap.ch, 6);
    /* (c)(d) 一致の種類 */
    for (i = 0u; i < (sizeof N / sizeof N[0]); i++) {
        snprintf(b, sizeof b, "+CWLAP:(3,\"%s\",-52,\"de:ad:be:ef:00:01\",6)", N[i].ssid);
        CHECK(wifi_parse_cwlap(b, ME, &ap));
        if (ap.match != N[i].want) {
            printf("  ssid=[%s] match=%u want=%u\n", N[i].ssid, (unsigned)ap.match, N[i].want);
        }
        CHECK_EQ(ap.match, N[i].want);
        CHECK_EQ(ap.ch, 6);
    }
    /* 逆向きも同じ(ユーザーの SSID に大文字・空白・全角、AP が半角) */
    CHECK(wifi_parse_cwlap(CWL("MySsid_SENT"), "mY ssid_sent", &ap));       CHECK_EQ(ap.match, 2);
    CHECK(wifi_parse_cwlap(CWL("MySsid_SENT"), "\xEF\xBC\xAD" "ySsid_SENT", &ap));   CHECK_EQ(ap.match, 2);
    CHECK(wifi_parse_cwlap(CWL("MySsid_SENT"), "MySsid_SEN", &ap));         CHECK_EQ(ap.match, 0);
    /* (e) 日本語(非 ASCII)はバイト一致で exact。最後の 1 字違いは 0 */
    snprintf(b, sizeof b, "+CWLAP:(3,\"%s\",-60,\"de:ad:be:ef:00:01\",1)", JP);
    CHECK(wifi_parse_cwlap(b, JP, &ap));                                     CHECK_EQ(ap.match, 1);
    CHECK(wifi_parse_cwlap(b, JP2, &ap));                                    CHECK_EQ(ap.match, 0);
    CHECK(wifi_parse_cwlap(b, "x", &ap));                                    CHECK_EQ(ap.match, 0);
    /* (f) 32 バイト・NULL・空 */
    memset(ss, 'x', 32u); ss[32] = '\0';
    snprintf(b, sizeof b, "+CWLAP:(3,\"%s\",-52,\"m\",6)", ss);
    CHECK(wifi_parse_cwlap(b, ss, &ap));                                     CHECK_EQ(ap.match, 1);
    ss[32] = 'x'; ss[33] = '\0';
    snprintf(b, sizeof b, "+CWLAP:(3,\"%s\",-52,\"m\",6)", ss);
    CHECK(!wifi_parse_cwlap(b, ss, &ap));                                    CHECK_EQ(ap.ecn, 0); CHECK_EQ(ap.match, 0);   /* 33 バイトは読めない */
    CHECK(wifi_parse_cwlap(CWL("MySsid_SENT"), NULL, &ap));                  CHECK_EQ(ap.match, 0); CHECK_EQ(ap.ecn, 3);
    CHECK(wifi_parse_cwlap(CWL("MySsid_SENT"), "", &ap));                    CHECK_EQ(ap.match, 0);
    CHECK(wifi_parse_cwlap(CWL(""), "", &ap));                               CHECK_EQ(ap.match, 0);      /* 非公開 AP(SSID が空)と空のユーザー値は一致にしない */
    CHECK(wifi_parse_cwlap(CWL(""), " ", &ap));                              CHECK_EQ(ap.match, 0);      /* 正規化して空になるものは near にしない */
    CHECK(wifi_parse_cwlap(CWL("  "), " ", &ap));                            CHECK_EQ(ap.match, 0);
    /* (g) 壊れた行は false で落ちない。ap は 0 のまま */
    for (i = 0u; i < (sizeof BAD / sizeof BAD[0]); i++) {
        memset(&ap, 0x5A, sizeof ap);
        if (wifi_parse_cwlap(BAD[i], "x", &ap)) {
            printf("  line=[%s] が読めてしまった\n", BAD[i]);
            CHECK(0);
        }
        CHECK_EQ(ap.ecn, 0); CHECK_EQ(ap.rssi, 0); CHECK_EQ(ap.ch, 0); CHECK_EQ(ap.match, 0);
    }
    CHECK(!wifi_parse_cwlap(NULL, "x", &ap));
    /* (h) SSID 中の \" \\ \, */
    CHECK(wifi_parse_cwlap("+CWLAP:(3,\"a\\\"b\",-52,\"m\",6)", "a\"b", &ap));  CHECK_EQ(ap.match, 1);
    CHECK(wifi_parse_cwlap("+CWLAP:(3,\"a\\\\b\",-52,\"m\",6)", "a\\b", &ap));  CHECK_EQ(ap.match, 1);
    CHECK(wifi_parse_cwlap("+CWLAP:(3,\"a\\,b\",-52,\"m\",6)", "a,b", &ap));    CHECK_EQ(ap.match, 1);
    CHECK(wifi_parse_cwlap("+CWLAP:(3,\"a\\\"b\",-52,\"m\",6)", "a\\\"b", &ap)); CHECK_EQ(ap.match, 0);   /* 生の \ を含むユーザー値は違う */
    CHECK(wifi_parse_cwlap("+CWLAP:(3,\"a,b\",-52,\"m\",6)", "a,b", &ap));      CHECK_EQ(ap.match, 1);     /* エスケープなしの , も読める */
    /* SSID の 32 バイトはエスケープの \ を数えない */
    memset(ss, 0, sizeof ss); memset(ss, 'q', 31u);
    snprintf(b, sizeof b, "+CWLAP:(3,\"%s\\\"\",-52,\"m\",6)", ss);
    CHECK(wifi_parse_cwlap(b, "x", &ap));
}

/* ================================================================ H-W11 scan の集計と秘密 */

static char   g_sc[8192];
static size_t g_sc_n;

static void sc_reset(void)
{
    g_sc_n = 0u;
    g_sc[0] = '\0';
}

static void sc_raw(const char *line)
{
    g_sc_n += (size_t)snprintf(g_sc + g_sc_n, sizeof g_sc - g_sc_n, "%s\r\n", line);
}

static void sc_ap(const char *ssid, int ecn, int rssi, int ch, int idx)
{
    char l[160];

    snprintf(l, sizeof l, "+CWLAP:(%d,\"%s\",%d,\"de:ad:be:ef:00:%02d\",%d)", ecn, ssid, rssi, idx, ch);
    sc_raw(l);
}

static void sc_ok(void)
{
    sc_raw("");
    sc_raw("OK");
}

/* 自分の SSID が mine のとき、ここで作った一覧(g_sc)を AT+CWLAP の応答にして全部流す */
static void scan_run(const char *mine)
{
    begin(mine, PASS_MARK);
    rule("AT+CWLAP", 0, 800, g_sc);
    normal_rules();
    run(WIFI_RANGE_ALL, 120000u);
    CHECK(!core_has_secret());
    CHECK(!log_has_secret());
}

static void t_scan(void)
{
    char   nm[48];
    char   l[160];
    char   shp[48];
    char   want[96];
    int    i;
    int    base;

    TEST("H-W11 scan の集計と秘密(30 件・code 0/1/2/3・strongest・bad と oth・頭打ち・再試行・タイムアウト)");
    /* 30 件: 自分 1 + 近い名前 1 + 他の AP 28(目印つき) */
    sc_reset();
    for (i = 0; i < 28; i++) {
        snprintf(nm, sizeof nm, NEIGH_MARK "_%d", i);
        sc_ap(nm, 3, -60 - i, 1 + (i % 11), i);
    }
    sc_ap(SSID_MARK, 3, -52, 6, 40);
    sc_ap("ssid_sent_9A1", 4, -55, 1, 41);
    sc_ok();
    scan_run(SSID_MARK);
    CHECK(first_with("[wifiv] key=scan val=total=30,found=1,near=1,bad=0\r\n") >= 0);
    CHECK(first_with("[wifiv] key=ap val=ch=6,rssi=-52,ecn=3\r\n") >= 0);
    CHECK(first_with("step=scan res=ok try=1 ms=800 code=0") >= 0);
    CHECK_EQ(count_with("key=scan"), 1);
    CHECK_EQ(count_with("key=ap"), 1);
    CHECK(first_with("key=scan") < first_with("key=ap"));
    CHECK(first_with("key=ap") < first_with("step=scan res=ok"));
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    /* 50 件(約 4KB。受信量の確認): total=50 */
    sc_reset();
    for (i = 0; i < 49; i++) {
        snprintf(nm, sizeof nm, NEIGH_MARK "_%d", i);
        sc_ap(nm, 3, -60, 1 + (i % 11), i);
    }
    sc_ap(SSID_MARK, 3, -52, 6, 49);
    sc_ok();
    CHECK(g_sc_n > 3000u);
    scan_run(SSID_MARK);
    CHECK(first_with("[wifiv] key=scan val=total=50,found=1,near=0,bad=0\r\n") >= 0);
    /* code=1: 近い名前だけ(打ち間違いの疑い)。key=ap は出ない */
    sc_reset();
    sc_ap(NEIGH_MARK "_1", 3, -60, 1, 1);
    sc_ap("ssid_sent_9A1", 3, -55, 11, 2);
    sc_ok();
    scan_run(SSID_MARK);
    CHECK(first_with("[wifiv] key=scan val=total=2,found=0,near=1,bad=0\r\n") >= 0);
    CHECK_EQ(count_with("key=ap"), 0);
    CHECK(first_with("step=scan res=ok try=1 ms=800 code=1") >= 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));     /* found = 0 でも join へ進む */
    /* code=2: 他の AP だけ */
    sc_reset();
    sc_ap(NEIGH_MARK "_1", 3, -60, 1, 1);
    sc_ap(NEIGH_MARK "_2", 3, -70, 1, 2);
    sc_ok();
    scan_run(SSID_MARK);
    CHECK(first_with("[wifiv] key=scan val=total=2,found=0,near=0,bad=0\r\n") >= 0);
    CHECK(first_with("step=scan res=ok try=1 ms=800 code=2") >= 0);
    CHECK_EQ(count_with("key=ap"), 0);
    /* code=3: 1 件も見えない */
    sc_reset();
    sc_ok();
    scan_run(SSID_MARK);
    CHECK(first_with("[wifiv] key=scan val=total=0,found=0,near=0,bad=0\r\n") >= 0);
    CHECK(first_with("step=scan res=ok try=1 ms=800 code=3") >= 0);
    CHECK_EQ(count_with("key=ap"), 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    /* 同じ SSID が複数見える: rssi が最大の 1 件の ch・rssi・ecn */
    sc_reset();
    sc_ap(SSID_MARK, 4, -70, 1, 1);
    sc_ap(SSID_MARK, 3, -40, 11, 2);
    sc_ap(SSID_MARK, 2, -60, 3, 3);
    sc_ok();
    scan_run(SSID_MARK);
    CHECK(first_with("[wifiv] key=scan val=total=3,found=3,near=0,bad=0\r\n") >= 0);
    CHECK(first_with("[wifiv] key=ap val=ch=11,rssi=-40,ecn=3\r\n") >= 0);
    /* 特殊文字を含む SSID(AP の行はエスケープされて見える形): exact になる */
    sc_reset();
    sc_ap(NEIGH_MARK "_1", 3, -60, 1, 1);
    sc_raw("+CWLAP:(3,\"SSID_SENT_9a1\\,\\\"x\\\\y\",-48,\"de:ad:be:ef:00:07\",9)");
    sc_ok();
    scan_run("SSID_SENT_9a1,\"x\\y");
    CHECK(first_with("[wifiv] key=scan val=total=2,found=1,near=0,bad=0\r\n") >= 0);
    CHECK(first_with("[wifiv] key=ap val=ch=9,rssi=-48,ecn=3\r\n") >= 0);
    /* 自分の SSID を持たない(NULL)場合は照合しない: found = near = 0 */
    sc_reset();
    sc_ap(SSID_MARK, 3, -52, 6, 1);
    sc_ap(NEIGH_MARK "_1", 3, -60, 1, 2);
    sc_ok();
    begin(NULL, NULL);
    rule("AT+CWLAP", 0, 800, g_sc);
    normal_rules();
    run(WIFI_RANGE_ALL, 120000u);
    CHECK(first_with("[wifiv] key=scan val=total=2,found=0,near=0,bad=0\r\n") >= 0);
    CHECK(first_with("step=join res=skip") >= 0);
    CHECK(!log_has_secret());
    /* bad: 読めない行(引用符なし 5 件)と oth(形)は 1 step で最大 3 行。形に元の英数字が残らない */
    sc_reset();
    sc_ap(NEIGH_MARK "_1", 3, -60, 1, 1);
    for (i = 0; i < 5; i++) {
        snprintf(l, sizeof l, "+CWLAP:(3,BADNAME_SENTINEL_%d,-50,de:ad:be:ef:00:%02d,6)", i, i);
        sc_raw(l);
    }
    sc_ap(SSID_MARK, 3, -52, 6, 9);
    sc_ok();
    scan_run(SSID_MARK);
    CHECK(first_with("[wifiv] key=scan val=total=7,found=1,near=0,bad=5\r\n") >= 0);
    CHECK_EQ(count_with("key=oth"), 7);          /* scan の 3(上限)+ gmr の compile time 行 1 + ip の 3(APIP・APMAC・STAMAC) */
    base = first_with("step=scan res=start");
    CHECK(base >= 0);
    {
        int n_in_scan = 0;
        int end = first_with("step=scan res=ok");

        for (i = base; i < end; i++) {
            if (strstr(g_log[i], "key=oth") != NULL) { n_in_scan++; }
        }
        CHECK_EQ(n_in_scan, 3);
    }
    snprintf(l, sizeof l, "+CWLAP:(3,BADNAME_SENTINEL_%d,-50,de:ad:be:ef:00:%02d,6)", 0, 0);
    wifi_shape(shp, sizeof shp, l);
    snprintf(want, sizeof want, "[wifiv] key=oth val=%s\r\n", shp);
    CHECK(first_with(want) >= 0);
    CHECK_EQ(strlen(shp), WIFI_VAL_MAX);
    /* 頭打ち: 各 1100 行(読める他の AP・自分・近い名前・読めない行)→ 全部 999 */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWLAP", 0, 0, NULL);
    normal_rules();
    CHECK(wifi_core_start(&g_c, WIFI_RANGE_ALL, g_now));
    while ((g_nsend < 7) && (wifi_core_state(&g_c) == WIFI_STATE_RUN)) { tick(); }     /* AT+CWLAP を送った(応答なし) */
    CHECK_EQ(g_nsend, 7);
    CHECK_EQ(wifi_core_step(&g_c), WIFI_STEP_SCAN);
    for (i = 0; i < 1100; i++) {
        FEED("+CWLAP:(3,\"" NEIGH_MARK "_1\",-70,\"de:ad:be:ef:00:01\",1)\r\n");
        FEED("+CWLAP:(4,\"" SSID_MARK "\",-50,\"de:ad:be:ef:00:02\",3)\r\n");
        FEED("+CWLAP:(3,\"ssid_sent_9A1\",-60,\"de:ad:be:ef:00:03\",4)\r\n");
        FEED("+CWLAP:(3,BADNAME_SENTINEL,-60,de:ad:be:ef:00:04,5)\r\n");
    }
    FEED("OK\r\n");
    CHECK(first_with("[wifiv] key=scan val=total=999,found=999,near=999,bad=999\r\n") >= 0);
    CHECK(first_with("[wifiv] key=ap val=ch=3,rssi=-50,ecn=4\r\n") >= 0);
    CHECK_EQ(count_with("key=oth"), 3 + 1);      /* scan の 3 + gmr の 1(上限は step ごと) */
    tick();                                      /* join のコマンドを送らせる(組み立て後・送信前の cmd には秘密がある) */
    CHECK(!core_has_secret());
    CHECK(!log_has_secret());
    run_until_done(60000u);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    /* ERROR(1 回目。途中の行つき)→ 再試行 → OK: 集計は 2 回目の分だけ */
    sc_reset();
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWLAP", 1, 50, "+CWLAP:(3,\"" NEIGH_MARK "_1\",-70,\"de:ad:be:ef:00:01\",1)\r\n+CWLAP:(3,\"" SSID_MARK "\",-50,\"de:ad:be:ef:00:02\",3)\r\nERROR\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 120000u);
    CHECK(first_with("step=scan res=fail try=1 ms=50 code=0") >= 0);
    CHECK(first_with("step=scan res=start try=2") >= 0);
    CHECK(first_with("step=scan res=ok try=2 ms=800 code=0") >= 0);
    CHECK(first_with("[wifiv] key=scan val=total=3,found=1,near=0,bad=0\r\n") >= 0);      /* 1 回目の 2 行は数えない */
    CHECK_EQ(count_with("key=scan"), 1);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    /* いつも ERROR: 2 回で打ち切り、でも続行(join へ)。scan の値は出ない */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWLAP", 0, 50, "busy p...\r\n\r\nERROR\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 120000u);
    CHECK_EQ(count_with("step=scan res=start"), 2);
    CHECK_EQ(count_with("step=scan res=fail"), 2);
    CHECK_EQ(count_with("key=scan"), 0);
    CHECK(first_with("step=join res=ok") >= 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    /* 無応答: タイムアウト 15 秒 x 2 回 → 続行 */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWLAP", 0, 0, NULL);
    normal_rules();
    run(WIFI_RANGE_ALL, 120000u);
    CHECK(first_with("step=scan res=timeout try=1 ms=15000 code=0") >= 0);
    CHECK(first_with("step=scan res=timeout try=2 ms=15000 code=0") >= 0);
    CHECK_EQ(count_with("key=scan"), 0);
    CHECK(first_with("step=join res=ok") >= 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    /* scan の応答の遅れた OK が次の step(join)の成功に数えられない */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CWLAP", 2, 15200, "OK\r\n");
    rule("AT+CWJAP=*", 1, 0, NULL);
    normal_rules();
    run(WIFI_RANGE_ALL, 200000u);
    CHECK_EQ(count_with("step=join res=ok"), 0);
}

/* ================================================================ H-W12 wifi_shape と oth */

static int count_between(const char *a, const char *b, const char *sub)
{
    int i, n = 0, ia = first_with(a), ib = first_with(b);

    if ((ia < 0) || (ib < 0)) {
        return -1;
    }
    for (i = ia; i < ib; i++) {
        if (strstr(g_log[i], sub) != NULL) {
            n++;
        }
    }
    return n;
}

static void t_shape(void)
{
    char   o[64];
    char   all[256];
    size_t n, i;
    int    b, bad = 0;

    TEST("H-W12 wifi_shape と oth(英字 a・数字 9・記号そのまま・他 _。40 文字。元の英数字が残らない。1 step 最大 3 行)");
    n = wifi_shape(o, sizeof o, "+CWLAP:(3,\"Ab9-.\",-52)");
    CHECK_EQ(n, strlen(o)); CHECK(strcmp(o, "+aaaaa:(9,\"aa9-.\",-99)") == 0);
    CHECK(strcmp((wifi_shape(o, sizeof o, "AT version:1.1.3.0(5a40576)"), o), "aa_aaaaaaa:9.9.9.9(9a99999)") == 0);
    CHECK(strcmp((wifi_shape(o, sizeof o, "+ : , \" ( ) - . \\"), o), "+_:_,_\"_(_)_-_._\\") == 0);          /* 空白は _ */
    CHECK(strcmp((wifi_shape(o, sizeof o, "\xE3\x83\x86x\x01\x7F\xFF"), o), "___a___") == 0);                      /* 日本語・制御文字・0x7F・0xFF は 1 バイトずつ _ */
    CHECK_EQ(wifi_shape(o, sizeof o, "\xE3\x83\x86"), 3);   CHECK(strcmp(o, "___") == 0);                    /* 日本語は 1 バイトずつ _ */
    CHECK_EQ(wifi_shape(o, sizeof o, ""), 0);               CHECK_EQ(o[0], 0);
    CHECK_EQ(wifi_shape(o, sizeof o, NULL), 0);             CHECK_EQ(o[0], 0);
    CHECK_EQ(wifi_shape(NULL, 10u, "abc"), 0);
    CHECK_EQ(wifi_shape(o, 0u, "abc"), 0);
    /* 40 文字で切る・出力の大きさで切る */
    memset(all, 'Q', 100u); all[100] = '\0';
    CHECK_EQ(wifi_shape(o, sizeof o, all), 40);             CHECK_EQ(strlen(o), 40);
    memset(o, 0x5A, sizeof o);
    CHECK_EQ(wifi_shape(o, 6u, all), 5);                    CHECK_EQ(o[5], 0); CHECK_EQ((unsigned char)o[6], 0x5Au);
    memset(o, 0x5A, sizeof o);
    CHECK_EQ(wifi_shape(o, 1u, all), 0);                    CHECK_EQ(o[0], 0); CHECK_EQ((unsigned char)o[1], 0x5Au);
    /* 全バイト(1〜255)を入れても、英数字は a と 9 だけ・0x21〜0x7E か _ だけ */
    for (b = 1; b < 256; b++) {
        all[0] = (char)b; all[1] = (char)b; all[2] = '\0';
        wifi_shape(o, sizeof o, all);
        for (i = 0u; o[i] != '\0'; i++) {
            unsigned char c = (unsigned char)o[i];
            int alnum = ((c >= '0') && (c <= '9')) || ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'));
            if ((alnum && (c != 'a') && (c != '9')) || ((c < 0x21u) || (c > 0x7Eu))) { bad++; }
        }
        CHECK_EQ(strlen(o), 2);
    }
    CHECK_EQ(bad, 0);
    /* 状態機械: gmr に分類できない行が 5 本 → oth は 3 行まで。echo と空行は出さない */
    begin(SSID_MARK, PASS_MARK);
    rule("AT+GMR", 0, 30, "AT version:1.1.3.0(x)\r\nSDK version:v3.0.7\r\nl1 aa\r\nl2 bb\r\n\r\nl3 cc\r\nl4 dd\r\nl5 ee\r\n\r\nOK\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 120000u);
    CHECK_EQ(count_between("step=gmr res=start", "step=gmr res=ok", "key=oth"), 3);
    CHECK(first_with("[wifiv] key=oth val=a9_aa\r\n") >= 0);        /* l1 aa → a9_aa */
    CHECK(first_with("[wifiv] key=oth val=a9_aa\r\n") < first_with("step=gmr res=ok"));
    CHECK_EQ(count_with("[wifiv] key=oth val=a9_aa\r\n"), 3);        /* l1 l2 l3 の 3 本は同じ形。l4 l5 は出ない */
    CHECK(!log_has_secret());
    /* 次の step では数え直し: ip の 3 行 */
    CHECK_EQ(count_between("step=ip res=start", "step=ip res=ok", "key=oth"), 3);
    /* エコー・空行だけの応答では出ない */
    begin(SSID_MARK, PASS_MARK);
    rule("AT", 0, 10, "\r\n\r\nOK\r\n");
    run(WIFI_RANGE_AT, 5000u);
    CHECK_EQ(count_with("key=oth"), 0);
}

/* ================================================================ H-W13 新しい key の書式 */

static int fm_scan(const char *s)
{
    unsigned long a, b, c, d;
    char again[160];
    int  n = -1;

    if (sscanf(s, "[wifiv] key=scan val=total=%lu,found=%lu,near=%lu,bad=%lu%n", &a, &b, &c, &d, &n) != 4) { return 0; }
    if ((n < 0) || (strcmp(s + n, "\r\n") != 0) || (a > 999ul) || (b > 999ul) || (c > 999ul) || (d > 999ul)) { return 0; }
    snprintf(again, sizeof again, "[wifiv] key=scan val=total=%lu,found=%lu,near=%lu,bad=%lu\r\n", a, b, c, d);
    return strcmp(again, s) == 0;
}

static int fm_ap(const char *s)
{
    unsigned long ch, rs, ecn;
    char again[160];
    int  n = -1;

    if (sscanf(s, "[wifiv] key=ap val=ch=%lu,rssi=-%lu,ecn=%lu%n", &ch, &rs, &ecn, &n) != 3) { return 0; }
    if ((n < 0) || (strcmp(s + n, "\r\n") != 0) || (ch > 999ul) || (rs > 999ul) || (ecn > 99ul)) { return 0; }
    snprintf(again, sizeof again, "[wifiv] key=ap val=ch=%lu,rssi=-%lu,ecn=%lu\r\n", ch, rs, ecn);
    return strcmp(again, s) == 0;
}

/* 形: 1〜40 文字、英数字は a と 9 だけ(= [b-zA-Z0-8] が 0 個) */
static int fm_oth(const char *s)
{
    size_t i, len = strlen(s), p = strlen("[wifiv] key=oth val=");

    if (strncmp(s, "[wifiv] key=oth val=", p) != 0) { return 0; }
    if ((len < (p + 3u)) || (strcmp(s + len - 2u, "\r\n") != 0) || ((len - 2u - p) > WIFI_VAL_MAX)) { return 0; }
    for (i = p; i < (len - 2u); i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c < 0x21u) || (c > 0x7Eu)) { return 0; }
        if (((c >= 'b') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '8'))) { return 0; }
    }
    return 1;
}

static void t_newkeys(void)
{
    int  i, nscan = 0, nap = 0, noth = 0;
    char l[160];

    TEST("H-W13 [wifiv] の新しいキー(scan / ap / oth)の書式");
    sc_reset();
    sc_ap(NEIGH_MARK "_1", 3, -60, 1, 1);
    sc_ap(SSID_MARK, 3, -52, 6, 2);
    sc_raw("+CWLAP:(3,BADNAME_SENTINEL,-60,de:ad:be:ef:00:04,5)");
    sc_ok();
    scan_run(SSID_MARK);
    for (i = 0; i < g_nlog; i++) {
        if (strncmp(g_log[i], "[wifiv] key=scan ", 17) == 0) { nscan++; CHECK(fm_scan(g_log[i])); CHECK(fm_val_line_ok(g_log[i])); }
        if (strncmp(g_log[i], "[wifiv] key=ap ", 15) == 0)   { nap++;   CHECK(fm_ap(g_log[i]));   CHECK(fm_val_line_ok(g_log[i])); }
        if (strncmp(g_log[i], "[wifiv] key=oth ", 16) == 0)  { noth++;  CHECK(fm_oth(g_log[i]));  CHECK(fm_val_line_ok(g_log[i])); }
        CHECK(strlen(g_log[i]) < WIFI_LOG_MAX);
    }
    CHECK_EQ(nscan, 1); CHECK_EQ(nap, 1); CHECK(noth >= 1);
    /* 最大値の書式(各 999・rssi -999・ecn 99・ch 999) */
    CHECK(wifi_fmt_val(l, sizeof l, WIFI_KEY_SCAN, "total=999,found=999,near=999,bad=999") > 0u);  CHECK(fm_scan(l));
    CHECK(wifi_fmt_val(l, sizeof l, WIFI_KEY_AP, "ch=999,rssi=-999,ecn=99") > 0u);                  CHECK(fm_ap(l));
    CHECK(strlen(l) < WIFI_LOG_MAX);
    wifi_shape(l, sizeof l, CWL("x"));                                                             /* 40 文字の形 */
    CHECK(strlen(l) == WIFI_VAL_MAX);
    { char o2[WIFI_LOG_MAX]; CHECK(wifi_fmt_val(o2, sizeof o2, WIFI_KEY_OTH, l) > 0u);  CHECK(fm_oth(o2)); }
    /* 書式の逆: 範囲外の例は fm_* が拒否する(検出器が働く) */
    CHECK(!fm_scan("[wifiv] key=scan val=total=1000,found=0,near=0,bad=0\r\n"));
    CHECK(!fm_ap("[wifiv] key=ap val=ch=6,rssi=52,ecn=3\r\n"));
    CHECK(!fm_oth("[wifiv] key=oth val=+CWLAP:(3)\r\n"));
    CHECK(!fm_oth("[wifiv] key=oth val=a1\r\n"));
}

static void t_secret(void)
{
    static const char *const SS = "SSID_SENT_9a1,\"x\\y";
    static const char *const PW = "PW_SENTINEL_9f3,\"q\\r";

    TEST("H-W6 秘密がログに出ない");
    /* 特殊文字を含む値(エスケープされて送られ、エコーされる)+ CWJAP? 形式の応答(SSID を含む)+ 失敗の各種 */
    begin(SS, PW);
    rule("AT+CWJAP=*", 0, 1000, "+CWJAP:\"SSID_SENT_9a1\",\"aa:bb:cc:dd:ee:ff\",6,-50,0,0,0\r\nWIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(strcmp(g_sent[7], "AT+CWJAP=\"SSID_SENT_9a1\\,\\\"x\\\\y\",\"PW_SENTINEL_9f3\\,\\\"q\\\\r\"\r\n") == 0);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    CHECK(!core_has_secret());
    /* 失敗する join(パスワード違い。エコー付き) */
    begin(SS, PW);
    rule("AT+CWJAP=*", 0, 200, "+CWJAP:2\r\n\r\nFAIL\r\n");
    normal_rules();
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(last_is("[wifi] result=fail stage=join ping_ip=0 ping_dns=0\r\n"));
    CHECK(!core_has_secret());
    /* join が無応答のまま(送信直後の cmd が消えている) */
    begin(SS, PW);
    rule("AT+CWJAP=*", 0, 0, NULL);
    normal_rules();
    run(WIFI_RANGE_ALL, 90000u);
    CHECK(!core_has_secret());
    /* join の組み立て後、送信前の cmd にはパスワードがある(試験の検出器が働くことの確認)。送信が FAIL の時は消す */
    begin(SS, PW);
    normal_rules();
    CHECK(wifi_core_start(&g_c, WIFI_RANGE_ALL, g_now));
    while ((g_nsend < 7) && (wifi_core_state(&g_c) == WIFI_STATE_RUN)) { tick(); }       /* at echo gmr mode qap lapopt scan を送った */
    g_busy_left = 1000;                                                                  /* join の送信を BUSY にして cmd を残す */
    while ((wifi_core_step(&g_c) != WIFI_STEP_JOIN) && (wifi_core_state(&g_c) == WIFI_STATE_RUN)) { tick(); }
    tick(); tick();
    CHECK(core_has_secret());
    g_send_mode = WIFI_SEND_FAIL;
    tick(); tick();
    CHECK(first_with("step=join res=fail try=1") >= 0);
    CHECK(first_with("code=101") >= 0);
    CHECK(!core_has_secret());
    /* エコーバックを手で流す: join を送った後(無応答の ESP)に、秘密つきの行(途中で切れた行・\r だけの区切り含む)を渡しても、何も出ない */
    begin(SS, PW);
    rule("AT+CWJAP=*", 0, 0, NULL);
    normal_rules();
    CHECK(wifi_core_start(&g_c, WIFI_RANGE_ALL, g_now));
    while ((g_nsend < 8) && (wifi_core_state(&g_c) == WIFI_STATE_RUN)) { tick(); }
    CHECK_EQ(g_nsend, 8);
    FEED("AT+CWJAP=\"SSID_SENT_9a1\",\"PW_SENTINEL_9f3\"\r\r\n");
    FEED("AT+CWJAP=\"SSID_SENT_9a1\",\"PW_SEN"); FEED("TINEL_9f3\"\r");
    FEED("+CWJAP:\"SSID_SENT_9a1\",\"aa:bb\"\r\n");
    CHECK(!core_has_secret());
    CHECK_EQ(count_with("res=fail"), 0);
    FEED("ERROR\r\n");
    CHECK_EQ(count_with("res=fail"), 1);                             /* 最後の ERROR だけが失敗にした */
    CHECK(last_is("[wifi] result=fail stage=join ping_ip=0 ping_dns=0\r\n"));
    CHECK(!core_has_secret());

    /* 全シナリオ(上のテスト関数全部)のログを通して */
    CHECK(strstr(g_all, "SENTINEL") == NULL);
    CHECK(strstr(g_all, "SSID_SENT") == NULL);
    CHECK(strstr(g_all, "PW_") == NULL);
    CHECK(strstr(g_all, "9f3") == NULL);
    CHECK(strstr(g_all, "9a1") == NULL);
    CHECK(strstr(g_all, "CWJAP=") == NULL);                          /* 生のコマンド・エコー行を出さない */
    CHECK(strstr(g_all, "aa:bb") == NULL);                           /* BSSID */
    CHECK(strstr(g_all, "compile time") == NULL);                    /* 生の受信行を出さない */
    CHECK(strstr(g_all, "Bin version") == NULL);
    CHECK(strstr(g_all, "NEIGHBOR") == NULL);                        /* 他の AP の名前(scan の全シナリオを通して) */
    CHECK(strstr(g_all, "BADNAME") == NULL);
    CHECK(strstr(g_all, "ssid_sent") == NULL);
    CHECK(strstr(g_all, "de:ad") == NULL);                           /* MAC */
    CHECK(strstr(g_all, "CWLAP") == NULL);                           /* +CWLAP: の生の行 */
    CHECK(strstr(g_all, "BODY_SENTINEL") == NULL);                   /* 取得の応答の本文・ヘッダー(H-X6)。取得の全シナリオを通して */
    CHECK(strstr(g_all, "regularMarket") == NULL);                   /* 応答の本文のキー(chart API) */
    CHECK(strstr(g_all, "chartPreviousClose") == NULL);
    CHECK(strstr(g_all, "\"chart\"") == NULL);
    CHECK(strstr(g_all, "HTTP/1") == NULL);                          /* 状態行・ヘッダー */
    CHECK(strstr(g_all, "Date:") == NULL); CHECK(strstr(g_all, "date:") == NULL);
    CHECK(strstr(g_all, "query1") == NULL);                          /* 要求の中身・ホスト名もログに出ない */
    CHECK(strstr(g_all, "yahoo") == NULL);
    CHECK(strstr(g_all, "GET /") == NULL);
    CHECK(strstr(g_all, "+IPD") == NULL);
    CHECK(g_all_n > 2000u);                                          /* ログは実際に溜まっている */
}

/* ================================================================ H-X5 / H-X6 取得(WX step)・範囲 */

#define BODY_MARK "BODY_SENTINEL"

static char   g_http[4000];              /* 偽サーバーの応答(ヘッダー + 本文) */
static size_t g_http_n;
static char   g_reply[7000];             /* AT+CIPSEND の後に ESP が返す全部(Recv … SEND OK + +IPD + CLOSED) */
static size_t g_reply_n;

/* Yahoo の chart API ふうの応答(design.md §2.1 F2・F4)。price / prev = 値の文字列(そのまま埋める)。close 配列は [prev, price](直前の足 = prev)。
 * status = 状態行、extra = Content-Type の後に足すヘッダーの行(NULL 可。\r\n で終わる)。目印 BODY_MARK をヘッダーと本文の両方に入れる(消えているかの検査用) */
static void mk_http(const char *status, const char *extra, const char *price, const char *prev)
{
    g_http_n = (size_t)snprintf(g_http, sizeof g_http,
        "%s\r\ndate: Wed, 25 Nov 2026 04:24:13 GMT\r\nContent-Type: application/json;charset=utf-8\r\n%sX-Body: " BODY_MARK "\r\nConnection: close\r\n\r\n"
        "{\"chart\":{\"result\":[{\"meta\":{\"currency\":\"JPY\",\"symbol\":\"6723.T\",\"exchangeName\":\"JPX\",\"instrumentType\":\"EQUITY\",\"firstTradeDate\":-2230000000,"
        "\"regularMarketTime\":1795579752,\"gmtoffset\":32400,\"timezone\":\"JST\",\"exchangeTimezoneName\":\"Asia/Tokyo\",\"regularMarketPrice\":%s,"
        "\"fiftyTwoWeekHigh\":2999.0,\"fiftyTwoWeekLow\":1500.5,\"regularMarketDayHigh\":2450.0,\"regularMarketDayLow\":2400.0,\"regularMarketVolume\":1234567,"
        "\"longName\":\"" BODY_MARK "\",\"shortName\":\"" BODY_MARK "\",\"chartPreviousClose\":%s,\"priceHint\":1,"
        "\"currentTradingPeriod\":{\"pre\":{\"timezone\":\"JST\",\"start\":1795536000,\"end\":1795564800,\"gmtoffset\":32400},"
        "\"regular\":{\"timezone\":\"JST\",\"start\":1795564800,\"end\":1795588200,\"gmtoffset\":32400},"
        "\"post\":{\"timezone\":\"JST\",\"start\":1795588200,\"end\":1795600800,\"gmtoffset\":32400}},"
        "\"dataGranularity\":\"1d\",\"range\":\"2d\",\"validRanges\":[\"1d\",\"5d\",\"1mo\",\"3mo\",\"6mo\",\"1y\",\"2y\",\"5y\",\"10y\",\"ytd\",\"max\"]},"
        "\"timestamp\":[1795479000,1795579752],\"indicators\":{\"quote\":[{\"open\":[2380.0,2400.0],\"low\":[2370.0,2400.0],\"high\":[2440.0,2450.0],"
        "\"volume\":[5000000,1234567],\"close\":[%s,%s]}]}}],\"error\":null}}",
        status, (extra != NULL) ? extra : "", price, prev, prev, price);
}

static void rp_add(const char *s, size_t n)
{
    memcpy(g_reply + g_reply_n, s, n);
    g_reply_n += n;
    g_reply[g_reply_n] = '\0';
}

#define RP_LIT(l) rp_add((l), sizeof(l) - 1u)

static void rp_ipd(const char *data, size_t n, const char *info)    /* +IPD,<n>[,info]:<data> */
{
    char h[48];
    int  k = snprintf(h, sizeof h, "+IPD,%u%s:", (unsigned)n, (info != NULL) ? info : "");

    rp_add(h, (size_t)k);
    rp_add(data, n);
}

static void rp_start(void)
{
    g_reply_n = 0u;
    g_reply[0] = '\0';
    RP_LIT("\r\nRecv 144 bytes\r\n\r\nSEND OK\r\n\r\n");
}

/* 通常の返事: Recv … SEND OK の後に +IPD(split バイト目で 2 つに分ける。0 = 分けない)、CLOSED。closed = 0 なら CLOSED を出さない */
static void mk_reply(size_t split, const char *info, int closed)
{
    rp_start();
    if ((split == 0u) || (split >= g_http_n)) {
        rp_ipd(g_http, g_http_n, info);
    } else {
        rp_ipd(g_http, split, info);
        rp_ipd(g_http + split, g_http_n - split, info);
    }
    if (closed) {
        RP_LIT("CLOSED\r\n");
    }
}

/* 取得の台本(g_rules[0..4])。get_delay = 要求を送ってから返事までの ms */
static void wx_rules(uint32_t get_delay)
{
    rule("AT+CIPMUX=0",   0, 20, "OK\r\n");
    rule("AT+CIPCLOSE",   0, 20, "ERROR\r\n");
    rule("AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443", 0, 300, "CONNECT\r\n\r\nOK\r\n");
    rule("AT+CIPSEND=144", 0, 20, "\r\nOK\r\n> ");
    rule("GET *", 0, get_delay, g_reply);
}

static int wx_step_ok(const char *name)
{
    char k[64];

    snprintf(k, sizeof k, "step=%s res=ok", name);
    return count_with(k);
}

/* 正常終了の共通の確認(6723.T = 銘柄 0: 現在値 2,433.0・前日の終値 2,378.0・JST 13:24:13) */
static void expect_ok(unsigned closed)
{
    const wx_result_t *r = wifi_core_wx(&g_c);
    wx_date_t d;

    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));
    CHECK(wifi_core_pass(&g_c)); CHECK_EQ(wifi_core_fail_stage(&g_c), WIFI_STEP_NONE);
    CHECK_EQ(r->ok, 1); CHECK_EQ(r->err, 0); CHECK_EQ(r->http, 200);
    CHECK_EQ(r->sym, 0); CHECK_EQ(r->price_x100, 243300); CHECK_EQ(r->prev_x100, 237800); CHECK_EQ(r->has_bar_prev, 1); CHECK_EQ(r->cur, WX_CUR_JPY);
    CHECK_EQ(r->mtime, 1795579752u); CHECK_EQ(r->rs, 1795564800u); CHECK_EQ(r->re, 1795588200u);
    CHECK_EQ(r->have_date, 1); CHECK_EQ(r->closed, closed);
    wx_date_fields(r->jst_s, &d);
    CHECK_EQ(d.hour, 13); CHECK_EQ(d.min, 24); CHECK_EQ(d.sec, 13);
    CHECK(r->ms > 300u); CHECK(r->ms < 20000u);
    CHECK(r->first_ms > 1000u);                                      /* 最初のデータを受けた時刻(world の開始 1000 より後) */
    CHECK(!core_has(BODY_MARK)); CHECK(!core_has("regularMarket")); /* 本文は残らない */
    CHECK(!core_has_secret());
    CHECK_EQ(log_has_secret(), 0);
    CHECK_EQ(count_with(BODY_MARK), 0);
}

/* 5 step が全部 ok(quiet でない時) */
static void expect_5steps(void)
{
    CHECK_EQ(wx_step_ok("wx_mux"), 1); CHECK_EQ(wx_step_ok("wx_close"), 1); CHECK_EQ(wx_step_ok("wx_tcp"), 1);
    CHECK_EQ(wx_step_ok("wx_send"), 1); CHECK_EQ(wx_step_ok("wx_recv"), 1);
}

static void t_wx_normal(void)
{
    char req[200];

    TEST("H-X5-1 取得: 正常(5 step。要求は 144 バイトで CRLF を足さない。> は改行なし。既定の接続先は Yahoo の SSL・銘柄 0)");
    begin(SSID_MARK, PASS_MARK);
    wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);
    expect_5steps();
    CHECK_EQ(count_with("res=start"), 5);
    CHECK_EQ(g_nsend, 5);
    CHECK(strcmp(g_sent[0], "AT+CIPMUX=0\r\n") == 0);
    CHECK(strcmp(g_sent[1], "AT+CIPCLOSE\r\n") == 0);
    CHECK(strcmp(g_sent[2], "AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n") == 0);
    CHECK(strcmp(g_sent[3], "AT+CIPSEND=144\r\n") == 0);
    CHECK_EQ(wx_build_request(req, sizeof req, 0u), 144u);
    CHECK(strcmp(g_sent[4], req) == 0);                              /* 要求本体そのまま(\r\n を足さない。末尾は \r\n\r\n) */
    CHECK_EQ(strlen(g_sent[4]), 144u);
    CHECK_EQ(count_with("step=reset"), 0); CHECK_EQ(count_with("step=join"), 0);       /* 範囲外の step は動かない */
    CHECK_EQ(g_en_n, 0);                                                                /* EN に触れない */
    CHECK_EQ(wifi_core_wx_seq(&g_c), 1u); CHECK_EQ(wifi_core_done_seq(&g_c), 1u);
    CHECK(first_with("[wifiv] key=oth val=") >= 0);                  /* 想定外の行(Recv 144 bytes など)は形だけ記録する */
    CHECK(first_with("[wifiv] key=oth val=aaaa_999_aaaaa\r\n") >= 0);   /* "Recv 144 bytes" の形 */
    CHECK_EQ(count_with("res=fail"), 0); CHECK_EQ(count_with("res=timeout"), 0);

    TEST("H-X5-1b 2 回続けて取得(結果が置き換わり、seq が増える)");
    {
        uint32_t first1 = wifi_core_wx(&g_c)->first_ms;

        g_nsend = 0; g_npend = 0;
        mk_http("HTTP/1.0 200 OK", NULL, "57953.84", "57499.16");
        mk_reply(0u, NULL, 1);
        run(WIFI_RANGE_WX, 30000u);
        CHECK_EQ(wifi_core_wx_seq(&g_c), 2u); CHECK_EQ(wifi_core_done_seq(&g_c), 2u);
        CHECK_EQ(wifi_core_wx(&g_c)->price_x100, 5795384); CHECK_EQ(wifi_core_wx(&g_c)->prev_x100, 5749916);
        CHECK(wifi_core_wx(&g_c)->first_ms > first1);
        CHECK_EQ(g_nsend, 5);
    }
}

static void t_wx_split(void)
{
    static const size_t SPL[] = { 1u, 9u, 100u, 256u, 257u, 300u };
    size_t i;

    TEST("H-X5-2 +IPD が複数に分かれる(100 + 残り など)・1 tick に 1 バイト(見出しも本文も tick をまたぐ)・3 つに分ける");
    for (i = 0u; i < (sizeof SPL / sizeof SPL[0]); i++) {
        begin(SSID_MARK, PASS_MARK); wx_rules(100u);
        mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
        mk_reply(SPL[i], NULL, 1);
        run(WIFI_RANGE_WX, 30000u);
        expect_ok(1u);
    }
    begin(SSID_MARK, PASS_MARK);
    g_rate = 2u;                                                      /* 以降の返事は 1 tick(10ms)に 2 バイト。約 1,500 バイトで約 7.5 秒(wx_recv の 15 秒以内) */
    wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    mk_reply(100u, NULL, 1);
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);
    CHECK(wifi_core_wx(&g_c)->ms > 6000u);                           /* 少しずつ流れた証拠 */
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    rp_start();
    rp_ipd(g_http, 50u, NULL); rp_ipd(g_http + 50, 300u, NULL); rp_ipd(g_http + 350, g_http_n - 350u, NULL);
    RP_LIT("CLOSED\r\n");
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);
}

static void t_wx_order(void)
{
    TEST("H-X5-3 SEND OK より先に +IPD / CLOSED が来る");
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    g_reply_n = 0u; g_reply[0] = '\0';
    RP_LIT("\r\nRecv 144 bytes\r\n\r\n");
    rp_ipd(g_http, g_http_n, NULL);
    RP_LIT("\r\nSEND OK\r\n");                                       /* +IPD が先 */
    RP_LIT("CLOSED\r\n");
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    g_reply_n = 0u; g_reply[0] = '\0';
    RP_LIT("\r\nRecv 144 bytes\r\n\r\n");
    rp_ipd(g_http, g_http_n, NULL);
    RP_LIT("CLOSED\r\n");                                            /* CLOSED まで先。SEND OK は最後 */
    RP_LIT("\r\nSEND OK\r\n");
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);                                                    /* wx_recv は始まってすぐ終わる */
}

static void t_wx_data_mode(void)
{
    TEST("H-X5-4 データの中の OK / CLOSED / +IPD, / SEND OK / > を解釈しない(データモード)");
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", "X-Pad: a\r\nOK\r\nCLOSED\r\n0,CLOSED\r\n+IPD,5:xyzab\r\nSEND OK\r\nERROR\r\n>\r\nFAIL\r\nb\r\n", "2433.0", "2378.0");
    mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);
    CHECK(g_http_n > 400u);
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);                       /* 境目をデータの中の CLOSED の途中にする */
    mk_http("HTTP/1.0 200 OK", "X-Pad: a\r\nOK\r\nCLOSED\r\n+IPD,5:xyzab\r\nSEND OK\r\nb\r\n", "2433.0", "2378.0");
    mk_reply((size_t)(strstr(g_http, "CLOSED") - g_http) + 3u, NULL, 1);
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);

    TEST("H-X5-5 +IPD,<n>,<ip>,<port>:(AT+CIPDINFO=1 の形)");
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    mk_reply(300u, ",203.0.113.7,80", 1);
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);

    TEST("H-X5-5b 壊れた +IPD の行(数字なし・0・大きすぎる)は捨て、次の正しい +IPD は読む");
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    rp_start();
    RP_LIT("+IPD,:junk\r\n+IPD,0:junk\r\n+IPD,9999:junk\r\n+IPD,x:junk\r\n");
    rp_ipd(g_http, g_http_n, NULL);
    RP_LIT("CLOSED\r\n");
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);
    CHECK_EQ(g_c.bad_lines, 4u);
}

static void t_wx_tcp_fail(void)
{
    static const char *const R[] = { "ERROR\r\n", "DNS Fail\r\n\r\nERROR\r\n", "CLOSED\r\n", "FAIL\r\n", "0,CLOSED\r\n", NULL };
    size_t i;

    TEST("H-X5-6 wx_tcp の失敗(ERROR / DNS Fail / CLOSED / FAIL / 無応答 / CONNECT だけで OK なし)");
    for (i = 0u; i < (sizeof R / sizeof R[0]); i++) {
        begin(SSID_MARK, PASS_MARK);
        rule("AT+CIPMUX=0", 0, 20, "OK\r\n");
        rule("AT+CIPCLOSE", 0, 20, "OK\r\n");
        rule("AT+CIPSTART=*", 0, 300, R[i]);
        run(WIFI_RANGE_WX, 30000u);
        CHECK(last_is("[wifi] result=fail stage=wx_tcp ping_ip=0 ping_dns=0\r\n"));
        CHECK(!wifi_core_pass(&g_c)); CHECK_EQ(wifi_core_fail_stage(&g_c), WIFI_STEP_WX_TCP);
        CHECK_EQ(wifi_core_wx_seq(&g_c), 1u);
        CHECK_EQ(wifi_core_wx(&g_c)->ok, 0); CHECK_EQ(wifi_core_wx(&g_c)->err, 1); CHECK_EQ(wifi_core_wx(&g_c)->http, 0);
        CHECK_EQ(g_nsend, 3);                                                       /* CIPSEND へ進まない */
        CHECK_EQ(count_with("step=wx_tcp res=fail"), (R[i] != NULL) ? 1 : 0);
        CHECK_EQ(count_with("step=wx_tcp res=timeout"), (R[i] == NULL) ? 1 : 0);
        CHECK_EQ(count_with("step=wx_send"), 0);
        CHECK_EQ(log_has_secret(), 0);
    }
    begin(SSID_MARK, PASS_MARK);                                                   /* CONNECT だけ(OK が来ない) */
    rule("AT+CIPMUX=0", 0, 20, "OK\r\n"); rule("AT+CIPCLOSE", 0, 20, "OK\r\n"); rule("AT+CIPSTART=*", 0, 300, "CONNECT\r\n");
    run(WIFI_RANGE_WX, 30000u);
    CHECK(last_is("[wifi] result=fail stage=wx_tcp ping_ip=0 ping_dns=0\r\n")); CHECK_EQ(count_with("step=wx_tcp res=timeout"), 1);
    begin(SSID_MARK, PASS_MARK);                                                   /* OK が先、<n>,CONNECT が後 */
    wx_rules(100u); mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0"); mk_reply(0u, NULL, 1);
    g_rules[2].reply = "OK\r\n0,CONNECT\r\n";
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);
    begin(SSID_MARK, PASS_MARK);                                                   /* wx_mux が ERROR・wx_close が OK でも先へ進む */
    wx_rules(100u); mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0"); mk_reply(0u, NULL, 1);
    g_rules[0].reply = "ERROR\r\n"; g_rules[1].reply = "link is not valid\r\n\r\nOK\r\n";
    run(WIFI_RANGE_WX, 30000u);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(count_with("step=wx_mux res=fail"), 1);
    CHECK_EQ(wifi_core_wx(&g_c)->ok, 1);
}

static void t_wx_send_fail(void)
{
    TEST("H-X5-7 wx_send の失敗(ERROR / > が来ない / SEND FAIL / 要求を送る前の CLOSED / SEND OK が来ない)");
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    g_rules[3].reply = "ERROR\r\n";
    run(WIFI_RANGE_WX, 30000u);
    CHECK(last_is("[wifi] result=fail stage=wx_send ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(wifi_core_wx(&g_c)->err, 2); CHECK_EQ(wifi_core_wx_seq(&g_c), 1u); CHECK_EQ(g_nsend, 4);   /* 要求本体は送らない */
    CHECK_EQ(count_with("step=wx_recv"), 0);

    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    g_rules[3].reply = "\r\nOK\r\n";                                  /* > が来ない */
    run(WIFI_RANGE_WX, 30000u);
    CHECK(last_is("[wifi] result=fail stage=wx_send ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(count_with("step=wx_send res=timeout"), 1); CHECK_EQ(wifi_core_wx(&g_c)->err, 2); CHECK_EQ(g_nsend, 4);

    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    g_rules[3].reply = ">";                                          /* > だけで OK が来ない */
    run(WIFI_RANGE_WX, 30000u);
    CHECK(last_is("[wifi] result=fail stage=wx_send ping_ip=0 ping_dns=0\r\n")); CHECK_EQ(g_nsend, 4);

    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    g_rules[4].reply = "\r\nRecv 144 bytes\r\n\r\nSEND FAIL\r\n";
    run(WIFI_RANGE_WX, 30000u);
    CHECK(last_is("[wifi] result=fail stage=wx_send ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(wifi_core_wx(&g_c)->err, 2); CHECK_EQ(g_nsend, 5);

    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    g_rules[3].reply = "CLOSED\r\n";
    run(WIFI_RANGE_WX, 30000u);
    CHECK(last_is("[wifi] result=fail stage=wx_send ping_ip=0 ping_dns=0\r\n")); CHECK_EQ(wifi_core_wx(&g_c)->err, 2); CHECK_EQ(g_nsend, 4);
    CHECK_EQ(count_with("step=wx_send res=fail"), 1); CHECK_EQ(count_with("step=wx_send res=timeout"), 0);      /* すぐ失敗(5 秒待たない) */

    begin(SSID_MARK, PASS_MARK); wx_rules(100u);                      /* 要求を送った後、SEND OK が来ない */
    g_rules[4].reply = "\r\nRecv 144 bytes\r\n";
    run(WIFI_RANGE_WX, 30000u);
    CHECK(last_is("[wifi] result=fail stage=wx_send ping_ip=0 ping_dns=0\r\n")); CHECK_EQ(count_with("step=wx_send res=timeout"), 1);

    TEST("H-X5-7b 要求本体・CIPSEND の送信が BUSY の間は同じものを渡し直し、1 回だけ送る(op_send の約束)・送信 FAIL");
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0"); mk_reply(0u, NULL, 1);
    g_busy_idx = 4; g_busy_n = 5;                                       /* 5 つ目の送信(要求本体)を 5 回 BUSY */
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);
    CHECK_EQ(g_nsend, 5); CHECK_EQ(g_busy_n, 0);
    CHECK_EQ(strlen(g_sent[4]), 144u);

    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0"); mk_reply(0u, NULL, 1);
    g_busy_idx = 3; g_busy_n = 3;                                       /* AT+CIPSEND=144 も BUSY で待てる */
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);
    CHECK_EQ(g_nsend, 5);

    begin(SSID_MARK, PASS_MARK); wx_rules(100u);                        /* 要求本体の送信が FAIL */
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0"); mk_reply(0u, NULL, 1);
    {
        int guard = 0;

        CHECK(wifi_core_start(&g_c, WIFI_RANGE_WX, g_now));
        while ((g_nsend < 4) && (guard++ < 5000)) { tick(); }
        g_send_mode = WIFI_SEND_FAIL;
        run_until_done(30000u);
        CHECK(last_is("[wifi] result=fail stage=wx_send ping_ip=0 ping_dns=0\r\n"));
        CHECK_EQ(first_with("step=wx_send res=fail try=1") >= 0, 1); CHECK_EQ(first_with("code=101") >= 0, 1);
        CHECK_EQ(wifi_core_wx(&g_c)->err, 2);
    }
}

static void t_wx_recv(void)
{
    TEST("H-X5-8 CLOSED が来ずにタイムアウト + 解析成功 = 成功(closed=0)");
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0"); mk_reply(0u, NULL, 0);
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(0u);
    CHECK(wifi_core_wx(&g_c)->ms >= 15000u);                         /* wx_recv のタイムアウト 15 秒を待った(design.md §5.5.2 の 0-4) */
    CHECK_EQ(count_with("step=wx_recv res=ok"), 1);

    TEST("H-X5-9 HTTP の状態が 429 / 500 / 301(err=4。http と Date: は残る)");
    {
        static const char *const ST[] = { "HTTP/1.0 429 Too Many Requests", "HTTP/1.0 500 Internal Server Error", "HTTP/1.1 301 Moved Permanently" };
        static const unsigned     H[] = { 429u, 500u, 301u };
        size_t i;

        for (i = 0u; i < 3u; i++) {
            begin(SSID_MARK, PASS_MARK); wx_rules(100u);
            mk_http(ST[i], NULL, "2433.0", "2378.0"); mk_reply(0u, NULL, 1);
            run(WIFI_RANGE_WX, 30000u);
            CHECK(last_is("[wifi] result=fail stage=wx_recv ping_ip=0 ping_dns=0\r\n"));
            CHECK_EQ(wifi_core_wx(&g_c)->ok, 0); CHECK_EQ(wifi_core_wx(&g_c)->err, 4); CHECK_EQ(wifi_core_wx(&g_c)->http, H[i]);
            CHECK_EQ(wifi_core_wx(&g_c)->have_date, 1);              /* 時刻同期は失敗した取得でもできる */
            CHECK_EQ(wifi_core_wx(&g_c)->closed, 1);
            CHECK(first_with("step=wx_recv res=fail") >= 0);
            CHECK(!core_has(BODY_MARK));
            CHECK_EQ(log_has_secret(), 0);
        }
    }

    TEST("H-X5-10 キー・値の異常(err=5 / 6)と、応答が無い(err=3)・途中で CLOSED / 途中までしか来ない(err=3。成功にしない)・受信中の ERROR");
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "0.0"); mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 6); CHECK_EQ(wifi_core_wx(&g_c)->ok, 0);
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "null", "2378.0"); mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 5);
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);                       /* CLOSED だけ: データなし */
    rp_start();
    RP_LIT("CLOSED\r\n");
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 3); CHECK_EQ(wifi_core_wx(&g_c)->closed, 1);
    CHECK(last_is("[wifi] result=fail stage=wx_recv ping_ip=0 ping_dns=0\r\n"));
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);                       /* 何も来ない(SEND OK のみ) */
    rp_start();
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 3); CHECK_EQ(wifi_core_wx(&g_c)->closed, 0);
    CHECK_EQ(count_with("step=wx_recv res=timeout"), 1);
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);                       /* ヘッダーの途中までで止まる(空行が無い = 途切れ。Date: は拾う) */
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    rp_start();
    rp_ipd(g_http, 100u, NULL);
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->ok, 0); CHECK_EQ(wifi_core_wx(&g_c)->err, 3); CHECK_EQ(wifi_core_wx(&g_c)->have_date, 1); CHECK_EQ(wifi_core_wx(&g_c)->http, 200);
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);                       /* 本文の途中(close 配列の手前)で CLOSED: 閉じた JSON でない = 途切れ。成功にしない(前日比が誤る) */
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    rp_start();
    rp_ipd(g_http, g_http_n - 40u, NULL);
    RP_LIT("CLOSED\r\n");
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->ok, 0); CHECK_EQ(wifi_core_wx(&g_c)->err, 3); CHECK_EQ(wifi_core_wx(&g_c)->closed, 1); CHECK_EQ(wifi_core_wx(&g_c)->http, 200);
    CHECK_EQ(wifi_core_wx(&g_c)->len, g_http_n - 40u);
    CHECK(last_is("[wifi] result=fail stage=wx_recv ping_ip=0 ping_dns=0\r\n"));
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);                       /* 全体が届き、最後の 1 バイトだけ足りない(} が無い)+ CLOSED */
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    rp_start();
    rp_ipd(g_http, g_http_n - 1u, NULL);
    RP_LIT("CLOSED\r\n");
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->ok, 0); CHECK_EQ(wifi_core_wx(&g_c)->err, 3);
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);                       /* 受信中に ERROR */
    rp_start();
    RP_LIT("ERROR\r\n");
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 3); CHECK(last_is("[wifi] result=fail stage=wx_recv ping_ip=0 ping_dns=0\r\n"));
}

static void t_wx_overflow(void)
{
    static char pad[WX_BUF_SIZE + 100u];
    static char extra[WX_BUF_SIZE + 200u];
    size_t need;

    CHECK_EQ(WX_BUF_SIZE, 3072u);
    TEST("H-X5-11 バッファ(3072 バイト)ちょうどは成功、1 バイト超えたら err=7(キーが先頭にあっても失敗。http は残る)・巨大な +IPD");
    memset(pad, 'a', sizeof pad - 1u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    CHECK(g_http_n < WX_BUF_SIZE);
    need = WX_BUF_SIZE - g_http_n;                                     /* ヘッダーの X-Pad(7 + N + 2 バイト)で 3072 にする */
    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    (void)snprintf(extra, sizeof extra, "X-Pad: %.*s\r\n", (int)(need - 9u), pad);
    mk_http("HTTP/1.0 200 OK", extra, "2433.0", "2378.0");
    CHECK_EQ(g_http_n, WX_BUF_SIZE);
    mk_reply(1500u, NULL, 1);
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);
    CHECK_EQ(wifi_core_wx(&g_c)->len, WX_BUF_SIZE);
    CHECK_EQ(wifi_core_wx(&g_c)->price_x100, 243300);

    begin(SSID_MARK, PASS_MARK); wx_rules(100u);
    (void)snprintf(extra, sizeof extra, "X-Pad: %.*s\r\n", (int)(need - 8u), pad);
    mk_http("HTTP/1.0 200 OK", extra, "2433.0", "2378.0");
    CHECK_EQ(g_http_n, WX_BUF_SIZE + 1u);
    mk_reply(1500u, NULL, 1);
    run(WIFI_RANGE_WX, 30000u);
    CHECK(last_is("[wifi] result=fail stage=wx_recv ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(wifi_core_wx(&g_c)->err, 7); CHECK_EQ(wifi_core_wx(&g_c)->ok, 0);
    CHECK_EQ(wifi_core_wx(&g_c)->http, 200u); CHECK_EQ(wifi_core_wx(&g_c)->have_date, 1u);    /* 途切れても状態行と Date: は拾う(g の判定用) */
    CHECK_EQ(wifi_core_wx(&g_c)->len, WX_BUF_SIZE);
    CHECK(!core_has(BODY_MARK));

    begin(SSID_MARK, PASS_MARK); wx_rules(100u);                        /* 2048 バイトの +IPD を 2 回(合計 4096 バイト) */
    rp_start();
    {
        static char big[2048];

        memset(big, 'z', sizeof big);
        rp_ipd(big, sizeof big, NULL); rp_ipd(big, sizeof big, NULL);
    }
    RP_LIT("CLOSED\r\n");
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 7);
    CHECK_EQ(wifi_core_wx(&g_c)->closed, 1);                            /* 超過のデータを捨てても、その後の CLOSED は読める */
}

#if APP_WIFI_TEST   /* 専用モードの t y g z(製品には無い接続先) */
/* ---- Phase 0: TLS の検証(design.md §5.5.2): 接続先の選択・SSL のタイムアウト・z の sslconf・ready = err 9・len ---- */

/* Yahoo / Google ふうの応答(天気のキーが無い)。total バイトになるまで本文を埋める。本文の目印は BODY_MARK(消えているかの検査用) */
static void mk_http_plain(const char *status, size_t total)
{
    size_t n = (size_t)snprintf(g_http, sizeof g_http, "%s\r\ndate: Wed, 25 Nov 2026 04:24:13 GMT\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"chart\":\"" BODY_MARK, status);

    CHECK(n + 3u <= total);
    memset(g_http + n, 'a', total - n - 2u);
    g_http[total - 2u] = '"';
    g_http[total - 1u] = '}';
    g_http[total]      = '\0';
    g_http_n = total;
}

/* 取得の台本(接続先 t)。cipstart = AT+CIPSTART の全文(\r\n なし)、conn_delay = CONNECT までの ms。CIPSEND の長さは wx_build_request_t(g_c.sym の銘柄)から作る。
 * g_rules の順: 0 MUX、1 CLOSE、2 SSLCCONF、3 CIPSTART、4 CIPSEND、5 GET */
static void tgt_rules(wx_target_t t, const char *cipstart, uint32_t conn_delay, uint32_t get_delay)
{
    static char snd[40];
    char        req[300];

    CHECK(wx_build_request_t(req, sizeof req, t, g_c.sym) > 0u);
    (void)snprintf(snd, sizeof snd, "AT+CIPSEND=%u", (unsigned)strlen(req));
    rule("AT+CIPMUX=0",   0, 20, "OK\r\n");
    rule("AT+CIPCLOSE",   0, 20, "ERROR\r\n");
    rule("AT+CIPSSLCCONF=0,0,0", 0, 20, "ERROR\r\n");          /* AT 1.1.3.0 が受け付けるか未確認。ERROR でも先へ進むこと */
    rule(cipstart,        0, conn_delay, "CONNECT\r\n\r\nOK\r\n");
    rule(snd,             0, 20, "\r\nOK\r\n> ");
    rule("GET *",         0, get_delay, g_reply);
}

#define CIPSTART_YSSL "AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443"
#define CIPSTART_YTCP "AT+CIPSTART=\"TCP\",\"query1.finance.yahoo.com\",80"
#define CIPSTART_GSSL "AT+CIPSTART=\"SSL\",\"www.google.com\",443"

static void t_wx_target(void)
{
    TEST("P0-1 y(Yahoo SSL): CIPSTART=SSL 443・CIPSEND=145・要求は ^N225 の chart・http=200 err=5(天気のキーが無い)・len = 受信バイト数・本文は消える");
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_symbol(&g_c, 1u);                                    /* t y g z の要求は ^N225(Phase 0 と同じ) */
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_SSL);
    tgt_rules(WX_TGT_YAHOO_SSL, CIPSTART_YSSL, 3000u, 300u);
    mk_http_plain("HTTP/1.0 200 OK", 1840u);
    mk_reply(1000u, NULL, 1);
    run(WIFI_RANGE_WX, 60000u);
    CHECK_EQ(g_nsend, 5);
    CHECK(strcmp(g_sent[2], CIPSTART_YSSL "\r\n") == 0);
    CHECK(strcmp(g_sent[3], "AT+CIPSEND=145\r\n") == 0);
    CHECK(strcmp(g_sent[4], "GET /v8/finance/chart/%5EN225?range=2d&interval=1d HTTP/1.0\r\nHost: query1.finance.yahoo.com\r\n"
                            "User-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n") == 0);
    CHECK_EQ(count_with("step=wx_sslconf"), 0);
    CHECK(first_with("step=wx_tcp res=ok try=1 ms=3") >= 0);        /* SSL の接続は 3 秒かかってもよい(20 秒まで) */
    CHECK(last_is("[wifi] result=fail stage=wx_recv ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(wifi_core_wx(&g_c)->ok, 0); CHECK_EQ(wifi_core_wx(&g_c)->err, 5); CHECK_EQ(wifi_core_wx(&g_c)->http, 200u);
    CHECK_EQ(wifi_core_wx(&g_c)->len, 1840u); CHECK_EQ(wifi_core_wx(&g_c)->have_date, 1u); CHECK_EQ(wifi_core_wx(&g_c)->closed, 1u);
    CHECK(!core_has(BODY_MARK));
    CHECK(!core_has_secret());
    CHECK_EQ(log_has_secret(), 0);

    TEST("P0-2 t(Yahoo 平文): CIPSTART=TCP 80・301 → err=4 http=301・TCP の接続は 10 秒で時間切れ");
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_symbol(&g_c, 1u);                                    /* t y g z の要求は ^N225(Phase 0 と同じ) */
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_TCP);
    tgt_rules(WX_TGT_YAHOO_TCP, CIPSTART_YTCP, 300u, 300u);
    mk_http_plain("HTTP/1.1 301 Moved Permanently", 400u);
    mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_WX, 60000u);
    CHECK(strcmp(g_sent[2], CIPSTART_YTCP "\r\n") == 0);
    CHECK(strncmp(g_sent[4], "GET /v8/finance/chart/%5EN225?range=2d&interval=1d HTTP/1.0\r\n", 61u) == 0);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 4); CHECK_EQ(wifi_core_wx(&g_c)->http, 301u); CHECK_EQ(wifi_core_wx(&g_c)->len, 400u);
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_symbol(&g_c, 1u);                                    /* t y g z の要求は ^N225(Phase 0 と同じ) */
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_TCP);
    tgt_rules(WX_TGT_YAHOO_TCP, CIPSTART_YTCP, 12000u, 300u);
    run(WIFI_RANGE_WX, 60000u);
    CHECK(first_with("step=wx_tcp res=timeout try=1 ms=10000 code=1") >= 0);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 1); CHECK_EQ(wifi_core_wx(&g_c)->len, 0u);

    TEST("P0-3 SSL の接続は 20 秒まで待つ(19 秒で CONNECT は成功、25 秒は時間切れ err=1)");
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_symbol(&g_c, 1u);                                    /* t y g z の要求は ^N225(Phase 0 と同じ) */
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_SSL);
    tgt_rules(WX_TGT_YAHOO_SSL, CIPSTART_YSSL, 19000u, 300u);
    mk_http_plain("HTTP/1.0 200 OK", 1800u);
    mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_WX, 60000u);
    CHECK_EQ(wx_step_ok("wx_tcp"), 1); CHECK_EQ(wifi_core_wx(&g_c)->http, 200u);
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_symbol(&g_c, 1u);                                    /* t y g z の要求は ^N225(Phase 0 と同じ) */
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_SSL);
    tgt_rules(WX_TGT_YAHOO_SSL, CIPSTART_YSSL, 25000u, 300u);
    run(WIFI_RANGE_WX, 60000u);
    CHECK(first_with("step=wx_tcp res=timeout try=1 ms=20000 code=1") >= 0);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 1);
    CHECK(last_is("[wifi] result=fail stage=wx_tcp ping_ip=0 ping_dns=0\r\n"));

    TEST("P0-4 z: wx_close の後に AT+CIPSSLCCONF=0,0,0(ERROR でも先へ)→ y と同じ。時間切れでも先へ。y に戻すと走らない");
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_symbol(&g_c, 1u);                                    /* t y g z の要求は ^N225(Phase 0 と同じ) */
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_SSL_NOAUTH);
    tgt_rules(WX_TGT_YAHOO_SSL_NOAUTH, CIPSTART_YSSL, 2000u, 300u);
    mk_http_plain("HTTP/1.0 200 OK", 1850u);
    mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_WX, 60000u);
    CHECK_EQ(g_nsend, 6);
    CHECK(strcmp(g_sent[0], "AT+CIPMUX=0\r\n") == 0); CHECK(strcmp(g_sent[1], "AT+CIPCLOSE\r\n") == 0);
    CHECK(strcmp(g_sent[2], "AT+CIPSSLCCONF=0,0,0\r\n") == 0);
    CHECK(strcmp(g_sent[3], CIPSTART_YSSL "\r\n") == 0);
    CHECK_EQ(count_with("step=wx_sslconf res=ok"), 1); CHECK_EQ(count_with("step=wx_sslconf res=start"), 1);
    CHECK_EQ(wifi_core_wx(&g_c)->http, 200u); CHECK_EQ(wifi_core_wx(&g_c)->len, 1850u);
    begin(SSID_MARK, PASS_MARK);                                       /* ESP32 が無応答でも先へ(cont) */
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_SSL_NOAUTH);
    tgt_rules(WX_TGT_YAHOO_SSL_NOAUTH, CIPSTART_YSSL, 2000u, 300u);
    g_rules[2].reply = NULL;
    mk_http_plain("HTTP/1.0 200 OK", 1850u);
    mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_WX, 60000u);
    CHECK(first_with("step=wx_sslconf res=timeout") >= 0);
    CHECK_EQ(wx_step_ok("wx_tcp"), 1); CHECK_EQ(wifi_core_wx(&g_c)->http, 200u);
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_symbol(&g_c, 1u);                                    /* t y g z の要求は ^N225(Phase 0 と同じ) */
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_SSL_NOAUTH);
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_SSL);
    tgt_rules(WX_TGT_YAHOO_SSL, CIPSTART_YSSL, 2000u, 300u);
    mk_http_plain("HTTP/1.0 200 OK", 1850u);
    mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_WX, 60000u);
    CHECK_EQ(g_nsend, 5); CHECK_EQ(count_with("step=wx_sslconf"), 0);

    TEST("P0-5 g(Google SSL): GET / の要求。応答がバッファを超えても http=301 と len=3072(err=7)が残る");
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_symbol(&g_c, 1u);                                    /* t y g z の要求は ^N225(Phase 0 と同じ) */
    wifi_core_set_target(&g_c, WX_TGT_GOOGLE_SSL);
    tgt_rules(WX_TGT_GOOGLE_SSL, CIPSTART_GSSL, 2000u, 300u);
    mk_http_plain("HTTP/1.0 301 Moved Permanently", 3500u);
    mk_reply(1500u, NULL, 1);
    run(WIFI_RANGE_WX, 60000u);
    CHECK(strcmp(g_sent[2], CIPSTART_GSSL "\r\n") == 0);
    CHECK(strncmp(g_sent[4], "GET / HTTP/1.0\r\nHost: www.google.com\r\n", 38u) == 0);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 7); CHECK_EQ(wifi_core_wx(&g_c)->http, 301u); CHECK_EQ(wifi_core_wx(&g_c)->len, WX_BUF_SIZE);
    CHECK_EQ(wifi_core_wx(&g_c)->closed, 1u);

    TEST("P0-6 取得中に ready = ESP32 の再起動 → err=9(wx_recv・wx_tcp・wx_send)。len はそこまでに溜めた分。以後は無視");
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_symbol(&g_c, 1u);                                    /* t y g z の要求は ^N225(Phase 0 と同じ) */
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_SSL);
    tgt_rules(WX_TGT_YAHOO_SSL, CIPSTART_YSSL, 2000u, 300u);
    mk_http_plain("HTTP/1.0 200 OK", 1000u);
    rp_start();
    rp_ipd(g_http, 600u, NULL);
    RP_LIT("\r\nready\r\n");
    rp_ipd(g_http + 600, 400u, NULL);                                  /* 再起動の後に届くものは数えない */
    run(WIFI_RANGE_WX, 60000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 9); CHECK_EQ(wifi_core_wx(&g_c)->len, 600u); CHECK_EQ(wifi_core_wx(&g_c)->ok, 0);
    CHECK(first_with("step=wx_recv res=fail try=1") >= 0); CHECK(first_with("code=9") >= 0);
    CHECK(last_is("[wifi] result=fail stage=wx_recv ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(wifi_core_wx_seq(&g_c), 1u);
    CHECK(!core_has(BODY_MARK));
    begin(SSID_MARK, PASS_MARK);                                       /* wx_tcp(ハンドシェイク中の再起動) */
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_SSL);
    tgt_rules(WX_TGT_YAHOO_SSL, CIPSTART_YSSL, 2000u, 300u);
    g_rules[3].reply = "ready\r\n";
    run(WIFI_RANGE_WX, 60000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 9); CHECK_EQ(wifi_core_wx(&g_c)->len, 0u);
    CHECK(last_is("[wifi] result=fail stage=wx_tcp ping_ip=0 ping_dns=0\r\n"));
    begin(SSID_MARK, PASS_MARK);                                       /* wx_send(> を待っている間) */
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_SSL);
    tgt_rules(WX_TGT_YAHOO_SSL, CIPSTART_YSSL, 2000u, 300u);
    g_rules[4].reply = "ready\r\n";
    run(WIFI_RANGE_WX, 60000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 9);
    CHECK(last_is("[wifi] result=fail stage=wx_send ping_ip=0 ping_dns=0\r\n"));

    TEST("P0-7 wifi_core_set_target: 実行中・範囲外は無視。既定は YAHOO_SSL(CIPSTART は SSL 443・要求は 144 バイト)");
    begin(SSID_MARK, PASS_MARK);
    wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    mk_reply(0u, NULL, 1);
    CHECK(wifi_core_start(&g_c, WIFI_RANGE_WX, g_now));
    wifi_core_set_target(&g_c, WX_TGT_YAHOO_TCP);                      /* 実行中: 無視 */
    run_until_done(30000u);
    expect_ok(1u);
    CHECK(strcmp(g_sent[2], "AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n") == 0);
    wifi_core_set_target(&g_c, WX_TGT_END);                            /* 範囲外: 無視 */
    wifi_core_set_target(&g_c, (wx_target_t)200);
    g_nsend = 0; g_npend = 0; g_nlog = 0; memset(g_used, 0, sizeof g_used);
    mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_WX, 30000u);
    CHECK(strcmp(g_sent[2], "AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n") == 0);
}

#endif /* APP_WIFI_TEST */

static void t_wx_ranges(void)
{
    TEST("H-X5-12 範囲: CONNECT(8 step。gmr lapopt scan dns ping を省く)・WX(5 step)・既存の ALL は変わらない");
    begin(SSID_MARK, PASS_MARK);
    normal_rules();
    run(WIFI_RANGE_CONNECT, 60000u);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(count_with("res=start"), 8);
    CHECK_EQ(wx_step_ok("reset"), 1); CHECK_EQ(wx_step_ok("boot"), 1); CHECK_EQ(wx_step_ok("at"), 1); CHECK_EQ(wx_step_ok("echo"), 1);
    CHECK_EQ(wx_step_ok("mode"), 1); CHECK_EQ(wx_step_ok("qap"), 1); CHECK_EQ(wx_step_ok("join"), 1); CHECK_EQ(wx_step_ok("ip"), 1);
    CHECK_EQ(count_with("step=gmr"), 0); CHECK_EQ(count_with("step=lapopt"), 0); CHECK_EQ(count_with("step=scan"), 0);
    CHECK_EQ(count_with("step=dns"), 0); CHECK_EQ(count_with("step=ping"), 0); CHECK_EQ(count_with("step=wx_"), 0);
    CHECK_EQ(g_nsend, 6);                                            /* AT ATE0 CWMODE CWQAP CWJAP CIFSR */
    CHECK(strcmp(g_sent[0], "AT\r\n") == 0); CHECK(strcmp(g_sent[1], "ATE0\r\n") == 0); CHECK(strcmp(g_sent[2], "AT+CWMODE=1\r\n") == 0);
    CHECK(strcmp(g_sent[3], "AT+CWQAP\r\n") == 0); CHECK(strncmp(g_sent[4], "AT+CWJAP=", 9) == 0); CHECK(strcmp(g_sent[5], "AT+CIFSR\r\n") == 0);
    CHECK_EQ(g_en_n, 2);
    CHECK_EQ(wifi_core_wx_seq(&g_c), 0u);                            /* 取得の結果は出ない */
    CHECK(first_with("[wifiv] key=ip val=192.168.1.23\r\n") >= 0);
    CHECK_EQ(log_has_secret(), 0);
    CHECK(!core_has_secret());
    run(WIFI_RANGE_CONNECT, 60000u);                                 /* もう一度(done_seq が増える) */
    CHECK_EQ(wifi_core_done_seq(&g_c), 2u);

    begin(NULL, NULL);                                               /* 秘密なし: join を skip して pass */
    normal_rules();
    run(WIFI_RANGE_CONNECT, 60000u);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(count_with("step=join res=skip"), 1); CHECK_EQ(count_with("step=ip"), 0);

    begin(SSID_MARK, PASS_MARK);                                     /* join の失敗(パスワード違い) */
    rule("AT+CWJAP=*", 0, 100, "+CWJAP:2\r\n\r\nFAIL\r\n");
    normal_rules();
    run(WIFI_RANGE_CONNECT, 60000u);
    CHECK(last_is("[wifi] result=fail stage=join ping_ip=0 ping_dns=0\r\n"));
    CHECK(!wifi_core_pass(&g_c)); CHECK_EQ(wifi_core_fail_stage(&g_c), WIFI_STEP_JOIN);

    begin(SSID_MARK, PASS_MARK);                                     /* ALL → WX(同じコアで続けて) */
    normal_rules(); wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0"); mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_ALL, 60000u);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=23 ping_dns=31\r\n"));
    CHECK_EQ(count_with("res=start"), 14); CHECK_EQ(count_with("step=wx_"), 0);                    /* ALL は取得を含まない */
    CHECK_EQ(wifi_core_wx_seq(&g_c), 0u);
    run(WIFI_RANGE_WX, 30000u);
    expect_ok(1u);
    CHECK_EQ(wifi_core_done_seq(&g_c), 2u);
    CHECK_EQ(count_with("step=wx_recv res=ok"), 1);
}

static void t_wx_classify(void)
{
    static const struct { const char *line; wifi_line_t kind; } T[] = {
        { "CONNECT", WIFI_L_CONNECT }, { "0,CONNECT", WIFI_L_CONNECT }, { "4,CONNECT", WIFI_L_CONNECT },
        { "CLOSED", WIFI_L_CLOSED }, { "0,CLOSED", WIFI_L_CLOSED },
        { "SEND OK", WIFI_L_SEND_OK }, { "SEND FAIL", WIFI_L_SEND_FAIL }, { ">", WIFI_L_PROMPT }, { "DNS Fail", WIFI_L_DNS_FAIL },
        /* 誤認しない */
        { "CONNECTED", WIFI_L_OTHER }, { "CLOSED ", WIFI_L_OTHER }, { "closed", WIFI_L_OTHER }, { ",CONNECT", WIFI_L_OTHER }, { "x,CONNECT", WIFI_L_OTHER },
        { "10,CONNECT", WIFI_L_OTHER }, { "0,CONNECTX", WIFI_L_OTHER }, { "0, CLOSED", WIFI_L_OTHER }, { "SEND OK ", WIFI_L_OTHER },
        { "SEND", WIFI_L_OTHER }, { ">>", WIFI_L_OTHER }, { "> ", WIFI_L_OTHER }, { "Recv 144 bytes", WIFI_L_OTHER }, { "DNS FAIL", WIFI_L_OTHER },
        { "+IPD,5:abcde", WIFI_L_OTHER }, { "busy s...", WIFI_L_OTHER }
    };
    wifi_ev_t ev;
    size_t    i;

    TEST("H-X5-13 新しい行の分類(CONNECT / CLOSED / SEND OK / SEND FAIL / > / DNS Fail。行全体の一致)");
    for (i = 0u; i < (sizeof T / sizeof T[0]); i++) {
        wifi_parse_line(T[i].line, &ev);
        if (ev.kind != T[i].kind) { printf("  '%s' -> %d (want %d)\n", T[i].line, (int)ev.kind, (int)T[i].kind); }
        CHECK_EQ(ev.kind, T[i].kind);
    }
    CHECK(strcmp(wifi_step_name(WIFI_STEP_WX_MUX), "wx_mux") == 0);
    CHECK(strcmp(wifi_step_name(WIFI_STEP_WX_CLOSE), "wx_close") == 0);
    CHECK(strcmp(wifi_step_name(WIFI_STEP_WX_TCP), "wx_tcp") == 0);
    CHECK(strcmp(wifi_step_name(WIFI_STEP_WX_SEND), "wx_send") == 0);
    CHECK(strcmp(wifi_step_name(WIFI_STEP_WX_RECV), "wx_recv") == 0);
    {
        char   b[APPLOG_LINE_MAX];
        size_t n = wifi_fmt_step(b, sizeof b, WIFI_STEP_WX_RECV, WIFI_RES_TIMEOUT, 1u, 10000u, 3u);
        CHECK(n > 0u); CHECK(strcmp(b, "[wifi] step=wx_recv res=timeout try=1 ms=10000 code=3\r\n") == 0);
        n = wifi_fmt_result(b, sizeof b, false, WIFI_STEP_WX_TCP, 0u, 0u);
        CHECK(strcmp(b, "[wifi] result=fail stage=wx_tcp ping_ip=0 ping_dns=0\r\n") == 0);
    }
}

static void t_wx_quiet(void)
{
    TEST("H-X5-14 製品(wifi_core_set_quiet): res=start・res=ok と key=oth の行を出さない(失敗・skip・result・[wifiv] key=ip は出す)。既定(専用モード)は今までどおり出す");
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_quiet(&g_c, true);
    normal_rules();
    run(WIFI_RANGE_CONNECT, 60000u);
    CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));
    CHECK_EQ(count_with("res=start"), 0);
    CHECK_EQ(count_with("res=ok"), 0);
    CHECK(first_with("[wifiv] key=ip val=192.168.1.23\r\n") >= 0);
    CHECK_EQ(count_with("key=oth"), 0);
    CHECK_EQ(log_has_secret(), 0);
    g_nsend = 0; g_npend = 0;
    wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    mk_reply(0u, NULL, 1);
    g_nlog = 0;
    run(WIFI_RANGE_WX, 30000u);                                      /* 取得: Recv 144 bytes などの行がある(専用モードでは key=oth に形が出る) */
    expect_ok(1u);
    CHECK_EQ(g_nlog, 1);                                             /* 成功した取得は result=pass の 1 行だけ(1 銘柄 5 step の行を出さない) */
    CHECK_EQ(count_with("res="), 0);
    CHECK_EQ(count_with("key=oth"), 0);
    CHECK_EQ(wifi_core_wx_seq(&g_c), 1u);
    /* 失敗は出す(res=fail / res=timeout / res=skip) */
    g_nsend = 0; g_npend = 0; g_nlog = 0;
    rp_start();
    RP_LIT("CLOSED\r\n");                                            /* 本文なし = err 3 */
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(count_with("step=wx_recv res=fail"), 1);
    CHECK_EQ(count_with("res=start"), 0); CHECK_EQ(count_with("res=ok"), 0);
    CHECK(last_is("[wifi] result=fail stage=wx_recv ping_ip=0 ping_dns=0\r\n"));
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_quiet(&g_c, true);
    rule("AT+CIPMUX=0", 0, 20, "OK\r\n"); rule("AT+CIPCLOSE", 0, 20, "OK\r\n"); rule("AT+CIPSTART=*", 0, 300, NULL);
    run(WIFI_RANGE_WX, 60000u);
    CHECK_EQ(count_with("step=wx_tcp res=timeout"), 1); CHECK_EQ(count_with("res=start"), 0); CHECK_EQ(count_with("res=ok"), 0);
    begin(NULL, NULL);                                               /* 秘密なし: join の skip は出る */
    wifi_core_set_quiet(&g_c, true);
    normal_rules();
    run(WIFI_RANGE_CONNECT, 60000u);
    CHECK_EQ(count_with("step=join res=skip"), 1); CHECK_EQ(count_with("res=ok"), 0); CHECK_EQ(count_with("res=start"), 0);
    wifi_core_set_quiet(&g_c, false);                                /* 戻すと出る */
    begin(SSID_MARK, PASS_MARK);
    g_nsend = 0; g_npend = 0;
    wx_rules(100u);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0");
    mk_reply(0u, NULL, 1);
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(count_with("res=start"), 5); CHECK_EQ(count_with("res=ok"), 5);
    CHECK(first_with("[wifiv] key=oth val=") >= 0);
}

/* ================================================================ 銘柄・3 銘柄の連続(製品の使い方: 銘柄を選んで WIFI_RANGE_WX を 1 回ずつ。design.md §5.7・§5.9) */

/* 銘柄 sym の取得の台本。g_rules[0..4] = mux / close / cipstart / cipsend / GET。要求の長さと CIPSEND は wx_build_request から作る */
static void sym_rules(uint8_t sym, uint32_t get_delay)
{
    static char snd[40];
    char        req[200];
    size_t      n = wx_build_request(req, sizeof req, sym);

    CHECK(n > 0u);
    (void)snprintf(snd, sizeof snd, "AT+CIPSEND=%u", (unsigned)n);
    rule("AT+CIPMUX=0",   0, 20, "OK\r\n");
    rule("AT+CIPCLOSE",   0, 20, "ERROR\r\n");
    rule("AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443", 0, 300, "CONNECT\r\n\r\nOK\r\n");
    rule(snd,             0, 20, "\r\nOK\r\n> ");
    rule("GET *",         0, get_delay, g_reply);
}

static void t_wx_symbol(void)
{
    static const char *const PATH[3] = { "6723.T", "%5EN225", "%5EGSPC" };
    static const char *const PRICE[3] = { "2433.0", "57953.84", "7182.78" };
    static const char *const PREV[3]  = { "2378.0", "57499.16", "7201.09" };
    char   want[100];
    int    sym;

    TEST("H-P3-1 銘柄の選択: wifi_core_set_symbol(0 / 1 / 2)で CIPSEND = 144 / 145 / 145・要求のパス = 6723.T / %5EN225 / %5EGSPC・結果の sym が同じ。既定は 0");
    begin(SSID_MARK, PASS_MARK);
    CHECK_EQ(g_c.sym, 0);
    for (sym = 0; sym < 3; sym++) {
        begin(SSID_MARK, PASS_MARK);
        wifi_core_set_symbol(&g_c, (uint8_t)sym);
        mk_http("HTTP/1.0 200 OK", NULL, PRICE[sym], PREV[sym]);
        mk_reply(0u, NULL, 1);
        sym_rules((uint8_t)sym, 100u);
        run(WIFI_RANGE_WX, 30000u);
        (void)snprintf(want, sizeof want, "AT+CIPSEND=%d\r\n", (sym == 0) ? 144 : 145);
        CHECK(strcmp(g_sent[3], want) == 0);
        (void)snprintf(want, sizeof want, "GET /v8/finance/chart/%s?range=2d&interval=1d HTTP/1.0\r\n", PATH[sym]);
        CHECK(strncmp(g_sent[4], want, strlen(want)) == 0);
        CHECK_EQ(wifi_core_wx(&g_c)->ok, 1); CHECK_EQ(wifi_core_wx(&g_c)->sym, sym);
        CHECK(last_is("[wifi] result=pass stage=none ping_ip=0 ping_dns=0\r\n"));
    }
    begin(SSID_MARK, PASS_MARK);
    wifi_core_set_symbol(&g_c, 2u); wifi_core_set_symbol(&g_c, 3u); wifi_core_set_symbol(&g_c, 255u);   /* 範囲外は無視 */
    CHECK_EQ(g_c.sym, 2);
    mk_http("HTTP/1.0 200 OK", NULL, "2433.0", "2378.0"); mk_reply(0u, NULL, 1); sym_rules(1u, 100u);
    wifi_core_set_symbol(&g_c, 1u);
    CHECK(wifi_core_start(&g_c, WIFI_RANGE_WX, g_now));
    wifi_core_set_symbol(&g_c, 0u);                                                                       /* 実行中は無視 */
    CHECK_EQ(g_c.sym, 1);
    run_until_done(30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->sym, 1);

    TEST("H-P3-2 3 銘柄を続けて(製品の 1 周期): 銘柄ごとに mux → close → tcp → send → recv。結果が銘柄ごとに出る(seq +1)。本文は残らない。接続先は毎回 SSL");
    begin(SSID_MARK, PASS_MARK);
    for (sym = 0; sym < 3; sym++) {
        const wx_result_t *r;

        g_nsend = 0; g_npend = 0; memset(g_used, 0, sizeof g_used); g_nrules = 0;
        mk_http("HTTP/1.0 200 OK", NULL, PRICE[sym], PREV[sym]);
        mk_reply(0u, NULL, 1);
        sym_rules((uint8_t)sym, 100u);
        wifi_core_set_symbol(&g_c, (uint8_t)sym);
        run(WIFI_RANGE_WX, 30000u);
        r = wifi_core_wx(&g_c);
        CHECK_EQ(wifi_core_wx_seq(&g_c), (uint32_t)sym + 1u); CHECK_EQ(wifi_core_done_seq(&g_c), (uint32_t)sym + 1u);
        CHECK_EQ(r->ok, 1); CHECK_EQ(r->sym, sym); CHECK_EQ(g_nsend, 5);
        CHECK(strcmp(g_sent[1], "AT+CIPCLOSE\r\n") == 0);                                    /* 銘柄の間に前の接続を閉じる */
        CHECK(strcmp(g_sent[2], "AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n") == 0);
        CHECK(!core_has(BODY_MARK));
    }
    CHECK_EQ(wifi_core_wx(&g_c)->price_x100, 718278); CHECK_EQ(wifi_core_wx(&g_c)->prev_x100, 720109);
    CHECK_EQ(log_has_secret(), 0);

    TEST("H-P3-3 1 銘柄が失敗しても次の銘柄は取れる(失敗の後の状態が残らない): sym 1 の ready(err=9)・sym 1 の CLOSED だけ・TCP 失敗の後に sym 2 が成功");
    begin(SSID_MARK, PASS_MARK);
    g_nsend = 0; g_npend = 0; memset(g_used, 0, sizeof g_used); g_nrules = 0;
    mk_http("HTTP/1.0 200 OK", NULL, PRICE[0], PREV[0]); mk_reply(0u, NULL, 1); sym_rules(0u, 100u);
    wifi_core_set_symbol(&g_c, 0u); run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->ok, 1);
    g_nsend = 0; g_npend = 0; memset(g_used, 0, sizeof g_used); g_nrules = 0;                /* sym 1: 受信中に ready */
    mk_http("HTTP/1.0 200 OK", NULL, PRICE[1], PREV[1]);
    rp_start(); rp_ipd(g_http, 600u, NULL); RP_LIT("\r\nready\r\n");
    sym_rules(1u, 100u);
    wifi_core_set_symbol(&g_c, 1u); run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->ok, 0); CHECK_EQ(wifi_core_wx(&g_c)->err, 9); CHECK_EQ(wifi_core_wx(&g_c)->sym, 1); CHECK_EQ(wifi_core_wx(&g_c)->len, 600u);
    CHECK(last_is("[wifi] result=fail stage=wx_recv ping_ip=0 ping_dns=0\r\n")); CHECK(!wifi_core_pass(&g_c));
    CHECK(!core_has(BODY_MARK));
    g_nsend = 0; g_npend = 0; memset(g_used, 0, sizeof g_used); g_nrules = 0;                /* sym 1 のやり直し: CLOSED だけ(本文なし) */
    rp_start(); RP_LIT("CLOSED\r\n"); sym_rules(1u, 100u);
    wifi_core_set_symbol(&g_c, 1u); run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 3); CHECK_EQ(wifi_core_wx(&g_c)->len, 0u);
    g_nsend = 0; g_npend = 0; memset(g_used, 0, sizeof g_used); g_nrules = 0;                /* sym 2: TCP が張れない */
    rule("AT+CIPMUX=0", 0, 20, "OK\r\n"); rule("AT+CIPCLOSE", 0, 20, "OK\r\n"); rule("AT+CIPSTART=*", 0, 300, "DNS Fail\r\n\r\nERROR\r\n");
    wifi_core_set_symbol(&g_c, 2u); run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 1); CHECK_EQ(wifi_core_wx(&g_c)->sym, 2); CHECK_EQ(wifi_core_wx(&g_c)->http, 0);
    g_nsend = 0; g_npend = 0; memset(g_used, 0, sizeof g_used); g_nrules = 0;                /* 直った: sym 2 が成功 */
    mk_http("HTTP/1.0 200 OK", NULL, PRICE[2], PREV[2]); mk_reply(0u, NULL, 1); sym_rules(2u, 100u);
    wifi_core_set_symbol(&g_c, 2u); run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->ok, 1); CHECK_EQ(wifi_core_wx(&g_c)->sym, 2); CHECK_EQ(wifi_core_wx(&g_c)->price_x100, 718278);
    CHECK_EQ(wifi_core_wx_seq(&g_c), 5u);

    TEST("H-P3-4 429(状態 429 の応答)は err=4 http=429 で、その銘柄の結果に sym と http が残る。本文は残らない");
    begin(SSID_MARK, PASS_MARK);
    mk_http("HTTP/1.0 429 Too Many Requests", NULL, PRICE[1], PREV[1]); mk_reply(0u, NULL, 1); sym_rules(1u, 100u);
    wifi_core_set_symbol(&g_c, 1u); run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 4); CHECK_EQ(wifi_core_wx(&g_c)->http, 429); CHECK_EQ(wifi_core_wx(&g_c)->sym, 1); CHECK_EQ(wifi_core_wx(&g_c)->have_date, 1);
    CHECK(!core_has(BODY_MARK));

    TEST("H-P3-5 CLOSED が先に来る(送信前 = err 2、送信後 = 取得の終わり)・タイムアウト(wx_send 5 秒 / wx_recv 15 秒 / SSL の wx_tcp 20 秒)");
    begin(SSID_MARK, PASS_MARK);
    rule("AT+CIPMUX=0", 0, 20, "OK\r\n"); rule("AT+CIPCLOSE", 0, 20, "OK\r\n");
    rule("AT+CIPSTART=*", 0, 300, "CONNECT\r\n\r\nOK\r\n"); rule("AT+CIPSEND=144", 0, 20, "CLOSED\r\n");
    run(WIFI_RANGE_WX, 30000u);
    CHECK_EQ(wifi_core_wx(&g_c)->err, 2); CHECK_EQ(count_with("step=wx_send res=fail"), 1);
    begin(SSID_MARK, PASS_MARK);                                                                /* wx_send: > が来ないまま 5 秒 */
    sym_rules(0u, 100u); g_rules[3].reply = "\r\nOK\r\n";
    run(WIFI_RANGE_WX, 30000u);
    CHECK(first_with("step=wx_send res=timeout try=1 ms=5000 code=2") >= 0); CHECK_EQ(wifi_core_wx(&g_c)->err, 2);
    begin(SSID_MARK, PASS_MARK);                                                                /* wx_recv: 応答が来ないまま 15 秒 */
    sym_rules(0u, 100u); rp_start();
    run(WIFI_RANGE_WX, 60000u);
    CHECK(first_with("step=wx_recv res=timeout try=1 ms=15000 code=3") >= 0); CHECK_EQ(wifi_core_wx(&g_c)->err, 3); CHECK_EQ(wifi_core_wx(&g_c)->closed, 0);
    begin(SSID_MARK, PASS_MARK);                                                                /* wx_tcp: SSL は 20 秒 */
    sym_rules(0u, 100u); g_rules[2].delay = 25000u;
    run(WIFI_RANGE_WX, 60000u);
    CHECK(first_with("step=wx_tcp res=timeout try=1 ms=20000 code=1") >= 0); CHECK_EQ(wifi_core_wx(&g_c)->err, 1);
}

int main(void)
{
    t_lines();
    t_classify();
    t_values();
    t_cwjap();
    t_fmt();
    t_cwlap();
    t_shape();
    t_normal();
    t_at_retry();
    t_join();
    t_ping_dns_boot();
    t_unsolicited_late();
    t_ranges();
    t_no_secret_and_errors();
    t_scan();
    t_newkeys();
    t_wx_classify();
    t_wx_normal();
    t_wx_split();
    t_wx_order();
    t_wx_data_mode();
    t_wx_tcp_fail();
    t_wx_send_fail();
    t_wx_recv();
    t_wx_overflow();
#if APP_WIFI_TEST
    t_wx_target();
#endif
    t_wx_ranges();
    t_wx_quiet();
    t_wx_symbol();
    t_secret();                          /* 最後: g_all(全ログ)を調べる */
    TEST_REPORT("test_wifi_core");
}
