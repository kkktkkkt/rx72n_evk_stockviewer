/* test_wx_core.c - src/app/wx_core.c の単体試験(docs/design.md の H-6(予定表)・H-11(UART 整形)・要求と接続先・時刻)。標準Cのみ。
 * -DAPP_WIFI_TEST=1(専用モードの接続先 t g z も試験する)と -DAPP_WX=1(製品。接続先は Yahoo の SSL だけ)の 2 通りでコンパイルする(0 の時 wx_core.c は空)。
 * 応答の解析は test_wx_stock.c(実応答)。ここは要求・書式・予定表・時刻。SSID・パスワード・応答の本文は扱わない。 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "wx_core.h"
#include "test_framework.h"

#define T_2026_11_25_132413_JST (1795580653u + 32400u)       /* 2026-11-25 13:24:13 JST の通算秒(JST の壁時計) */

/* ---------------------------------------------------------------- 要求・接続先 */

static void t_request(void)
{
    char   b[256];
    char   g[256];
    size_t n;
    int    sym;
    static const char *const ENC[3] = { "6723.T", "%5EN225", "%5EGSPC" };
    static const size_t      LEN[3] = { 144u, 145u, 145u };

    TEST("wx_build_cipstart(接続先・範囲外・小さな buf)と wx_target_ssl");
    n = wx_build_cipstart(b, sizeof b, WX_TGT_YAHOO_SSL);
    CHECK(strcmp(b, "AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n") == 0); CHECK_EQ(n, strlen(b));
#if APP_WIFI_TEST
    CHECK_EQ(WX_TGT_END, 5);                                                          /* + METEO_TCP(天気。製品にもある) */
    n = wx_build_cipstart(b, sizeof b, WX_TGT_YAHOO_TCP);
    CHECK(strcmp(b, "AT+CIPSTART=\"TCP\",\"query1.finance.yahoo.com\",80\r\n") == 0);
    n = wx_build_cipstart(b, sizeof b, WX_TGT_GOOGLE_SSL);
    CHECK(strcmp(b, "AT+CIPSTART=\"SSL\",\"www.google.com\",443\r\n") == 0);
    n = wx_build_cipstart(b, sizeof b, WX_TGT_YAHOO_SSL_NOAUTH);
    CHECK(strcmp(b, "AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n") == 0);
    CHECK(!wx_target_ssl(WX_TGT_YAHOO_TCP)); CHECK(wx_target_ssl(WX_TGT_GOOGLE_SSL)); CHECK(wx_target_ssl(WX_TGT_YAHOO_SSL_NOAUTH));
#else
    CHECK_EQ(WX_TGT_END, 2);                                                          /* 製品の接続先は Yahoo の SSL と天気の METEO_TCP */
#endif
    CHECK(wx_target_ssl(WX_TGT_YAHOO_SSL));
    CHECK(!wx_target_ssl(WX_TGT_END)); CHECK(!wx_target_ssl((wx_target_t)255));
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_cipstart(b, sizeof b, WX_TGT_END), 0u); CHECK_EQ(b[0], 0);
    CHECK_EQ(wx_build_cipstart(NULL, 10u, WX_TGT_YAHOO_SSL), 0u);
    CHECK_EQ(wx_build_cipstart(b, 0u, WX_TGT_YAHOO_SSL), 0u);
    n = wx_build_cipstart(b, sizeof b, WX_TGT_YAHOO_SSL);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_cipstart(b, n, WX_TGT_YAHOO_SSL), 0u); CHECK_EQ(b[0], 0);              /* NUL の分が足りない */
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_cipstart(b, n + 1u, WX_TGT_YAHOO_SSL), n); CHECK_EQ(b[n + 1u], 'Q');

    TEST("wx_build_request(製品: 3 銘柄の長さ 144 / 145 / 145・UA・range=2d・HTTP/1.0・改行・Host)");
    for (sym = 0; sym < 3; sym++) {
        char want[256];

        n = wx_build_request(b, sizeof b, (uint8_t)sym);
        (void)snprintf(want, sizeof want, "GET /v8/finance/chart/%s?range=2d&interval=1d HTTP/1.0\r\nHost: query1.finance.yahoo.com\r\n"
                       "User-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n", ENC[sym]);
        CHECK(strcmp(b, want) == 0);
        CHECK_EQ(n, LEN[sym]); CHECK_EQ(strlen(b), n);
        CHECK(strstr(b, "\r\n\r\n") == b + n - 4u);                                    /* 末尾は空行 1 つだけ */
        CHECK_EQ(wx_build_request_t(g, sizeof g, WX_TGT_YAHOO_SSL, (uint8_t)sym), n); CHECK(strcmp(g, b) == 0);
        memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_request(b, n, (uint8_t)sym), 0u); CHECK_EQ(b[0], 0);                /* NUL の分が足りない */
        memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_request(b, n + 1u, (uint8_t)sym), n); CHECK_EQ(b[n + 1u], 'Q');
#if APP_WIFI_TEST
        CHECK_EQ(wx_build_request_t(b, sizeof b, WX_TGT_YAHOO_TCP, (uint8_t)sym), n);                                   /* 平文も同じ要求 */
        CHECK_EQ(wx_build_request_t(b, sizeof b, WX_TGT_YAHOO_SSL_NOAUTH, (uint8_t)sym), n);
#endif
    }
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_request(b, sizeof b, 4u), 0u); CHECK_EQ(b[0], 0);    /* 銘柄の範囲外 */
    CHECK_EQ(wx_build_request(b, sizeof b, 255u), 0u);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_request_t(b, sizeof b, WX_TGT_END, 1u), 0u); CHECK_EQ(b[0], 0);
    CHECK_EQ(wx_build_request(NULL, 300u, 1u), 0u);
    CHECK_EQ(wx_build_request(b, 0u, 1u), 0u);
#if APP_WIFI_TEST
    n = wx_build_request_t(b, sizeof b, WX_TGT_GOOGLE_SSL, 0u);
    CHECK(strcmp(b, "GET / HTTP/1.0\r\nHost: www.google.com\r\nUser-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n") == 0);
    CHECK_EQ(wx_build_request_t(b, sizeof b, WX_TGT_GOOGLE_SSL, 200u), n);                                              /* g は銘柄を見ない */
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_build_request_t(b, 10u, WX_TGT_GOOGLE_SSL, 0u), 0u); CHECK_EQ(b[0], 0);
#endif
}

/* ---------------------------------------------------------------- 書式(H-11: design.md §7.5 の R-STK-*) */

/* line が「[stk] key=値 key=値 ... \r\n」で、key の並びと値の種類が fld どおりか。値の種類 cls は '|' 区切りの選択肢:
 * "#Dn" = 数字 1〜n 桁、"#En" = 数字ちょうど n 桁、"#Sn" = 符号(- 任意)つきの数字 1〜n 桁、それ以外 = その文字列そのもの */
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

static const char *const SRC5 = "boot|timer|touch|uart|retry";

static int is_ok(const char *l)
{
    static const fld_t F[] = { {"res","ok"}, {"sym","0|1|2"}, {"src","boot|timer|touch|uart|retry"}, {"p100","#D8"}, {"c100","#D8"}, {"bp","#S5"}, {"mkt","t|c|n"},
                               {"http","#E3"}, {"ms","#D10"}, {"sync","0|1"}, {"jst","#E6|none"}, {"len","#D5"}, {"closed","0|1"} };
    return line_ok(l, "[stk] ", F, sizeof F / sizeof F[0]);
}
static int is_fail(const char *l)
{
    static const fld_t F[] = { {"res","fail"}, {"sym","0|1|2"}, {"src","boot|timer|touch|uart|retry"}, {"err","1|2|3|4|5|6|7|8|9"}, {"http","#D3"}, {"ms","#D10"}, {"len","#D5"} };
    return line_ok(l, "[stk] ", F, sizeof F / sizeof F[0]);
}
static int is_cycle(const char *l)
{
    static const fld_t F[] = { {"cycle","#D10"}, {"ok","0|1|2|3|4|5|6|7"}, {"fail","0|1|2|3|4|5|6|7"}, {"ms","#D10"} };
    return line_ok(l, "[stk] ", F, sizeof F / sizeof F[0]);
}
static int is_req(const char *l)
{
    static const fld_t F[] = { {"req","ignored|busy|off"}, {"src","touch|uart"} };
    return line_ok(l, "[stk] ", F, sizeof F / sizeof F[0]);
}
static int is_net(const char *l)
{
    static const fld_t F[] = { {"net","off|conn|on|err"}, {"busy","0|1"}, {"have","0|1|2|3|4|5|6|7"}, {"stale","0|1|2|3|4|5|6|7"}, {"clk","#E6|none"}, {"upd","#E4|none"} };
    return line_ok(l, "[stk] ", F, sizeof F / sizeof F[0]);
}
static int is_sym(const char *l)
{
    static const fld_t F[] = { {"sym","0|1|2"}, {"have","0|1"}, {"p100","#D8"}, {"d100","#S8"}, {"bp","#S5"}, {"dir","u|d|f"}, {"mkt","t|c|n"}, {"age","-1|#D10"} };
    return line_ok(l, "[stk] ", F, sizeof F / sizeof F[0]);
}

static void t_fmt(void)
{
    char        b[300];
    char        g[300];
    wx_result_t r;
    size_t      n, k, maxlen = 0u;
    int         s, m;
    static const int32_t P[] = { 1, 243300, 5767436, 99999999 };
    static const int32_t C[] = { 1, 237800, 5749916, 99999999 };
    static const uint32_t MS[] = { 0u, 1234u, 0xFFFFFFFFu };

    TEST("書式の判定器そのものの確認(正しい行は合格、崩れた行は不合格)");
    CHECK(is_req("[stk] req=ignored src=uart\r\n"));
    CHECK(!is_req("[stk] req=ignored src=uart\r\nX"));       /* 余分な文字 */
    CHECK(!is_req("[stk] req=ignored src=uart"));            /* 改行なし */
    CHECK(!is_req("[stk] req=bad src=uart\r\n"));            /* 選択肢にない */
    CHECK(!is_req("[stk] src=uart req=busy\r\n"));           /* キーの順序 */
    CHECK(!is_cycle("[stk] cycle= ok=1 fail=0 ms=3\r\n"));   /* 数字 0 桁 */
    CHECK(!is_net("[stk] net=on busy=1 have=1 stale=0 clk=13241 upd=none\r\n"));   /* clk が 5 桁 */
    CHECK(is_net("[stk] net=on busy=1 have=1 stale=0 clk=132413 upd=1324\r\n"));

    TEST("wx_fmt_ok: [stk] res=ok(R-STK-OK)。全 src・全 sym・境界値");
    memset(&r, 0, sizeof r);
    r.ok = 1; r.http = 200; r.have_date = 1; r.jst_s = T_2026_11_25_132413_JST; r.ms = 1234u; r.len = 1832u; r.closed = 1u;
    r.sym = 1u; r.price_x100 = 243300; r.prev_x100 = 237800;
    n = wx_fmt_ok(b, sizeof b, WX_SRC_UART, &r, STK_MKT_TRADING);
    CHECK(strcmp(b, "[stk] res=ok sym=1 src=uart p100=243300 c100=237800 bp=231 mkt=t http=200 ms=1234 sync=1 jst=132413 len=1832 closed=1\r\n") == 0);
    CHECK_EQ(n, strlen(b));
    for (s = 0; s < (int)WX_SRC_END; s++) {
        for (m = 0; m < 3; m++) {
            for (k = 0u; k < 4u; k++) {
                r.sym = (uint8_t)(k % 3u); r.price_x100 = P[k]; r.prev_x100 = C[k]; r.ms = MS[k % 3u]; r.have_date = (uint8_t)(k != 2u); r.closed = (uint8_t)(k & 1u);
                r.len = (uint16_t)(k * 21845u);
                n = wx_fmt_ok(b, sizeof b, (wx_src_t)s, &r, (uint8_t)m);
                CHECK(n > 0u); CHECK_EQ(strlen(b), n); CHECK(is_ok(b));
                if (n > maxlen) { maxlen = n; }
            }
        }
    }
    CHECK(maxlen < 255u);
    r.price_x100 = 1; r.prev_x100 = 99999999;                                                  /* bp = -10000 近く(= -99.99%): 5 桁以内 */
    n = wx_fmt_ok(b, sizeof b, WX_SRC_BOOT, &r, STK_MKT_NONE); CHECK(n > 0u); CHECK(strstr(b, "bp=-10000 ") != NULL || strstr(b, "bp=-9999") != NULL); CHECK(is_ok(b));
    r.price_x100 = 99999999; r.prev_x100 = 9100000;                                            /* +998.90% = 99890 bp(範囲内の最大に近い) */
    n = wx_fmt_ok(b, sizeof b, WX_SRC_BOOT, &r, STK_MKT_NONE); CHECK(n > 0u); CHECK(is_ok(b)); CHECK(strstr(b, "bp=99890 ") != NULL);
    r.have_date = 0u; r.price_x100 = 243300; r.prev_x100 = 237800;
    n = wx_fmt_ok(b, sizeof b, WX_SRC_BOOT, &r, STK_MKT_NONE); CHECK(strstr(b, "mkt=n http=200 ms=4294967295 sync=0 jst=none") != NULL || strstr(b, "sync=0 jst=none") != NULL);
    /* 引数不正は 0 で buf[0] = 0 */
    r.have_date = 1u;
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_ok(b, sizeof b, WX_SRC_END, &r, 0u), 0u); CHECK_EQ(b[0], 0);
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_ok(b, sizeof b, WX_SRC_UART, &r, 3u), 0u); CHECK_EQ(b[0], 0);                  /* mkt が不正 */
    r.sym = 3u; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_ok(b, sizeof b, WX_SRC_UART, &r, 0u), 0u); CHECK_EQ(b[0], 0);
    r.sym = 0u; r.http = 0u; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_ok(b, sizeof b, WX_SRC_UART, &r, 0u), 0u); CHECK_EQ(b[0], 0);   /* http が 3 桁でない */
    r.http = 200u; r.prev_x100 = 0; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_ok(b, sizeof b, WX_SRC_UART, &r, 0u), 0u); CHECK_EQ(b[0], 0);  /* 計算できない */
    r.prev_x100 = 237800;
    CHECK_EQ(wx_fmt_ok(b, sizeof b, WX_SRC_UART, NULL, 0u), 0u);
    CHECK_EQ(wx_fmt_ok(NULL, 10u, WX_SRC_UART, &r, 0u), 0u);
    CHECK_EQ(wx_fmt_ok(b, 0u, WX_SRC_UART, &r, 0u), 0u);
    n = wx_fmt_ok(b, sizeof b, WX_SRC_UART, &r, 0u);
    for (k = 1u; k <= (n + 1u); k++) {                                                          /* 大きさの境界 */
        memset(g, 0x5A, sizeof g);
        s = (int)wx_fmt_ok(g, k, WX_SRC_UART, &r, 0u);
        if (k <= n) { CHECK_EQ(s, 0); CHECK_EQ(g[0], 0); } else { CHECK_EQ((size_t)s, n); }
        CHECK_EQ((unsigned char)g[k], 0x5Au);
    }

    TEST("wx_fmt_fail: [stk] res=fail(R-STK-FAIL)。err 1〜9・全 src・境界値・err=9 と len");
    memset(&r, 0, sizeof r);
    r.sym = 2u; r.err = 4; r.http = 429; r.ms = 5000u;
    n = wx_fmt_fail(b, sizeof b, WX_SRC_TIMER, &r);
    CHECK(strcmp(b, "[stk] res=fail sym=2 src=timer err=4 http=429 ms=5000 len=0\r\n") == 0);
    r.sym = 0u; r.err = 9; r.http = 0; r.ms = 12345u; r.len = 65535u;
    n = wx_fmt_fail(b, sizeof b, WX_SRC_RETRY, &r);
    CHECK(strcmp(b, "[stk] res=fail sym=0 src=retry err=9 http=0 ms=12345 len=65535\r\n") == 0);
    for (s = 0; s < (int)WX_SRC_END; s++) {
        for (k = 1u; k <= 9u; k++) {
            r.sym = (uint8_t)(k % 3u); r.err = (uint8_t)k; r.http = (uint16_t)(k * 100u); r.ms = MS[k % 3u]; r.len = (uint16_t)(k * 7000u);
            n = wx_fmt_fail(b, sizeof b, (wx_src_t)s, &r);
            CHECK(n > 0u); CHECK(is_fail(b)); CHECK_EQ(strlen(b), n);
        }
    }
    r.err = 0; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_fail(b, sizeof b, WX_SRC_UART, &r), 0u); CHECK_EQ(b[0], 0);
    r.err = 10; CHECK_EQ(wx_fmt_fail(b, sizeof b, WX_SRC_UART, &r), 0u);
    r.err = 3; r.http = 1000; CHECK_EQ(wx_fmt_fail(b, sizeof b, WX_SRC_UART, &r), 0u);
    r.http = 0; r.sym = 3u; CHECK_EQ(wx_fmt_fail(b, sizeof b, WX_SRC_UART, &r), 0u);
    r.sym = 0u; CHECK_EQ(wx_fmt_fail(b, sizeof b, WX_SRC_END, &r), 0u);
    CHECK_EQ(wx_fmt_fail(b, sizeof b, WX_SRC_UART, NULL), 0u);

    TEST("wx_fmt_cycle / wx_fmt_req: R-STK-CYCLE・R-STK-REQ");
    n = wx_fmt_cycle(b, sizeof b, 7u, 5u, 2u, 31000u);
    CHECK(strcmp(b, "[stk] cycle=7 ok=5 fail=2 ms=31000\r\n") == 0);
    n = wx_fmt_cycle(b, sizeof b, 0xFFFFFFFFu, 7u, 7u, 0xFFFFFFFFu); CHECK(n > 0u); CHECK(is_cycle(b));
    n = wx_fmt_cycle(b, sizeof b, 0u, 0u, 0u, 0u); CHECK(n > 0u); CHECK(is_cycle(b));
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_cycle(b, sizeof b, 1u, 8u, 0u, 0u), 0u); CHECK_EQ(b[0], 0);
    CHECK_EQ(wx_fmt_cycle(b, sizeof b, 1u, 0u, 8u, 0u), 0u);
    CHECK_EQ(wx_fmt_cycle(NULL, 10u, 1u, 0u, 0u, 0u), 0u);
    CHECK(strcmp((wx_fmt_req(b, sizeof b, WX_REQ_IGNORED, WX_SRC_UART), b), "[stk] req=ignored src=uart\r\n") == 0);
    CHECK(strcmp((wx_fmt_req(b, sizeof b, WX_REQ_BUSY, WX_SRC_TOUCH), b), "[stk] req=busy src=touch\r\n") == 0);
    CHECK(strcmp((wx_fmt_req(b, sizeof b, WX_REQ_OFF, WX_SRC_UART), b), "[stk] req=off src=uart\r\n") == 0);
    for (s = 1; s <= 3; s++) { CHECK(wx_fmt_req(b, sizeof b, (wx_req_t)s, WX_SRC_UART) > 0u); CHECK(is_req(b)); CHECK(wx_fmt_req(b, sizeof b, (wx_req_t)s, WX_SRC_TOUCH) > 0u); CHECK(is_req(b)); }
    memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_req(b, sizeof b, WX_REQ_OK, WX_SRC_UART), 0u); CHECK_EQ(b[0], 0);
    CHECK_EQ(wx_fmt_req(b, sizeof b, WX_REQ_BUSY, WX_SRC_TIMER), 0u);
    CHECK_EQ(wx_fmt_req(b, sizeof b, WX_REQ_BUSY, WX_SRC_BOOT), 0u);
    CHECK_EQ(wx_fmt_req(b, sizeof b, WX_REQ_BUSY, WX_SRC_RETRY), 0u);
    CHECK_EQ(wx_fmt_req(b, sizeof b, (wx_req_t)4, WX_SRC_UART), 0u);

    TEST("wx_fmt_net: [stk] net=(R-STK-NET)");
    {
        wx_netlog_t v;

        memset(&v, 0, sizeof v);
        v.net = WX_NET_OFF; v.upd_min = 0xFFFFu;
        n = wx_fmt_net(b, sizeof b, &v);
        CHECK(strcmp(b, "[stk] net=off busy=0 have=0 stale=0 clk=none upd=none\r\n") == 0);
        v.net = WX_NET_ON; v.busy = 1u; v.have_mask = 7u; v.stale_mask = 2u; v.synced = 1u; v.jst_s = T_2026_11_25_132413_JST; v.upd_min = (13u * 60u) + 24u;
        n = wx_fmt_net(b, sizeof b, &v);
        CHECK(strcmp(b, "[stk] net=on busy=1 have=7 stale=2 clk=132413 upd=1324\r\n") == 0);
        v.upd_min = 1439u; n = wx_fmt_net(b, sizeof b, &v); CHECK(strstr(b, "upd=2359") != NULL);
        v.upd_min = 0u;    n = wx_fmt_net(b, sizeof b, &v); CHECK(strstr(b, "upd=0000") != NULL);
        v.upd_min = 1440u; n = wx_fmt_net(b, sizeof b, &v); CHECK(strstr(b, "upd=none") != NULL);          /* 範囲外は none(崩れた字を出さない) */
        for (s = 0; s < 4; s++) {
            for (k = 0u; k < 8u; k++) {
                v.net = (uint8_t)s; v.busy = (uint8_t)(k & 1u); v.have_mask = (uint8_t)k; v.stale_mask = (uint8_t)(7u - k); v.synced = (uint8_t)((k >> 1) & 1u);
                v.jst_s = T_2026_11_25_132413_JST + (uint32_t)(k * 86400u); v.upd_min = (k == 3u) ? 0xFFFFu : (uint16_t)(k * 200u);
                n = wx_fmt_net(b, sizeof b, &v);
                CHECK(n > 0u); CHECK(is_net(b)); CHECK_EQ(strlen(b), n);
            }
        }
        v.net = 4; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_net(b, sizeof b, &v), 0u); CHECK_EQ(b[0], 0);
        v.net = 1; v.have_mask = 8u; CHECK_EQ(wx_fmt_net(b, sizeof b, &v), 0u);
        v.have_mask = 0u; v.stale_mask = 8u; CHECK_EQ(wx_fmt_net(b, sizeof b, &v), 0u);
        CHECK_EQ(wx_fmt_net(b, sizeof b, NULL), 0u);
        CHECK_EQ(wx_fmt_net(NULL, 10u, &v), 0u);
    }

    TEST("wx_fmt_sym: [stk] sym=(R-STK-SYM)。値なしは値 0・dir=f・mkt=n・age=-1");
    {
        wx_symlog_t v;

        memset(&v, 0, sizeof v);
        v.sym = 2u; v.age_s = 65;
        n = wx_fmt_sym(b, sizeof b, &v);
        CHECK(strcmp(b, "[stk] sym=2 have=0 p100=0 d100=0 bp=0 dir=f mkt=n age=-1\r\n") == 0);
        v.sym = 1u; v.have = 1u; v.p100 = 5767436; v.d100 = 17520; v.bp = 30; v.dir = STK_UP; v.mkt = STK_MKT_TRADING; v.age_s = 65;
        n = wx_fmt_sym(b, sizeof b, &v);
        CHECK(strcmp(b, "[stk] sym=1 have=1 p100=5767436 d100=17520 bp=30 dir=u mkt=t age=65\r\n") == 0);
        v.d100 = -1831; v.bp = -25; v.dir = STK_DOWN; v.mkt = STK_MKT_CLOSED; v.age_s = 0;
        n = wx_fmt_sym(b, sizeof b, &v);
        CHECK(strcmp(b, "[stk] sym=1 have=1 p100=5767436 d100=-1831 bp=-25 dir=d mkt=c age=0\r\n") == 0);
        for (s = 0; s < 3; s++) {
            for (m = 0; m < 3; m++) {
                for (k = 0u; k < 4u; k++) {
                    v.sym = (uint8_t)s; v.dir = (uint8_t)m; v.mkt = (uint8_t)m; v.p100 = P[k]; v.d100 = (k & 1u) ? -99999999 : 99999999; v.bp = (k & 1u) ? -99999 : 99999;
                    v.age_s = (k == 0u) ? 0 : (int32_t)(k * 100000000u / 4u); v.have = 1u;
                    n = wx_fmt_sym(b, sizeof b, &v); CHECK(n > 0u); CHECK(is_sym(b)); CHECK_EQ(strlen(b), n);
                }
            }
        }
        v.have = 1u; v.p100 = 100000000; memset(b, 'Q', sizeof b); CHECK_EQ(wx_fmt_sym(b, sizeof b, &v), 0u); CHECK_EQ(b[0], 0);
        v.p100 = 1; v.bp = 100000; CHECK_EQ(wx_fmt_sym(b, sizeof b, &v), 0u);
        v.bp = 0; v.d100 = 100000000; CHECK_EQ(wx_fmt_sym(b, sizeof b, &v), 0u);
        v.d100 = 0; v.dir = 3u; CHECK_EQ(wx_fmt_sym(b, sizeof b, &v), 0u);
        v.dir = 0u; v.mkt = 3u; CHECK_EQ(wx_fmt_sym(b, sizeof b, &v), 0u);
        v.mkt = 0u; v.age_s = -2; CHECK_EQ(wx_fmt_sym(b, sizeof b, &v), 0u);
        v.age_s = 0; v.sym = 3u; CHECK_EQ(wx_fmt_sym(b, sizeof b, &v), 0u);
        CHECK_EQ(wx_fmt_sym(b, sizeof b, NULL), 0u);
        v.sym = 0u; v.have = 0u; v.bp = 99999999; n = wx_fmt_sym(b, sizeof b, &v); CHECK(n > 0u); CHECK(is_sym(b));              /* 値なしの時は値を見ない */
    }
    (void)SRC5;
}

/* ---------------------------------------------------------------- H-6 予定表(design.md §5.7) */

static uint32_t   g_t;
static wx_sched_t g_s;

static wx_act_t pollat(uint32_t t) { g_t = t; return wx_sched_poll(&g_s, t); }

/* poll → FETCH(銘柄 sym、きっかけ src)→ 3 秒後に done(ok, http)。FETCH でなければ CHECK が落ちる */
static void fetch_at(uint32_t t, uint8_t sym, wx_src_t src, bool ok, uint16_t http)
{
    CHECK_EQ(pollat(t), WX_ACT_FETCH);
    CHECK_EQ(wx_sched_sym(&g_s), sym);
    CHECK_EQ(wx_sched_src(&g_s), src);
    CHECK(wx_sched_busy(&g_s));
    CHECK_EQ(pollat(t + 1u), WX_ACT_NONE);                                    /* done を待つ間は、次の動きを返さない */
    wx_sched_done(&g_s, WX_ACT_FETCH, ok, http, t + 3000u);
}

/* 起動 → 3 秒後に接続(7 秒)。戻り値 = 接続が終わった時刻。この後の最初の poll が周期の始まり */
static uint32_t boot_connect(uint32_t t0)
{
    wx_sched_init(&g_s, true, t0);
    CHECK_EQ(pollat(t0 + WX_BOOT_WAIT_MS - 1u), WX_ACT_NONE);
    CHECK_EQ(pollat(t0 + WX_BOOT_WAIT_MS), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, t0 + WX_BOOT_WAIT_MS + 7000u);
    return t0 + WX_BOOT_WAIT_MS + 7000u;
}

/* 3 銘柄が全部成功する 1 周期。開始 t、戻り値 = 終わりの時刻 */
static uint32_t good_cycle(uint32_t t, wx_src_t src)
{
    fetch_at(t, 0u, src, true, 200u);
    fetch_at(t + 3000u, 1u, src, true, 200u);
    fetch_at(t + 6000u, 2u, src, true, 200u);
    return t + 9000u;
}

/* 3 銘柄とも失敗する周期(やり直し 2 回を含む。失敗の http は引数)。開始 t、src。戻り値 = 周期の終わりの時刻 */
static uint32_t dead_cycle(uint32_t t, wx_src_t src, uint16_t http)
{
    uint32_t te;
    int      i;

    for (i = 0; i < 3; i++) { fetch_at(t + (uint32_t)i * 3000u, (uint8_t)i, src, false, http); }
    te = t + 9000u;
    for (i = 0; i < 3; i++) { fetch_at(te + WX_RETRY_MS + (uint32_t)i * 3000u, (uint8_t)i, WX_SRC_RETRY, false, http); }      /* やり直し 1 */
    te = te + WX_RETRY_MS + 9000u;
    for (i = 0; i < 3; i++) { fetch_at(te + WX_RETRY_MS + (uint32_t)i * 3000u, (uint8_t)i, WX_SRC_RETRY, false, http); }      /* やり直し 2 */
    return te + WX_RETRY_MS + 9000u;
}

static void t_sched(void)
{
    uint32_t t0 = 1000u;
    uint32_t tc;                   /* 周期の開始 */
    uint32_t te;                   /* 周期の終わり */
    int      i;

    TEST("H-6 ⓪ OFF: 何を呼んでも動かない・net=off");
    wx_sched_init(&g_s, false, t0);
    CHECK_EQ(pollat(t0 + 100000u), WX_ACT_NONE);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, t0 + 100000u), WX_REQ_OFF);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_OFF); CHECK(!wx_sched_busy(&g_s));
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, t0 + 100000u);            /* 始めていない動きの完了は無視 */
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_OFF); CHECK_EQ(wx_sched_cycle(&g_s)->n, 0u);

    TEST("H-6 ① 起動 3 秒後に接続 → 3 銘柄を順に(sym 0 → 1 → 2。src=boot)→ 周期の記録 → 10 分後に次の周期(src=timer)");
    wx_sched_init(&g_s, true, t0);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_CONN); CHECK(!wx_sched_busy(&g_s));
    CHECK_EQ(pollat(t0 + 2999u), WX_ACT_NONE);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, t0 + 2999u), WX_REQ_BUSY);                  /* 起動待ちは busy */
    CHECK_EQ(pollat(t0 + 3000u), WX_ACT_CONNECT);
    CHECK(wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_net(&g_s), WX_NET_CONN);                  /* 起動直後の接続の間は conn */
    CHECK_EQ(pollat(t0 + 3010u), WX_ACT_NONE);                                              /* 完了を待つ間は二重に始めない */
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, t0 + 3020u);                              /* 種類の違う完了は無視 */
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_CONN); CHECK(wx_sched_busy(&g_s));
    wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, t0 + 10000u);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    tc = t0 + 10000u;                                                                       /* 接続の直後に最初の周期 */
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 0u);
    fetch_at(tc, 0u, WX_SRC_BOOT, true, 200u);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 0u);
    fetch_at(tc + 3000u, 1u, WX_SRC_BOOT, true, 200u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 0u);                                                  /* まだ周期の途中 */
    fetch_at(tc + 6000u, 2u, WX_SRC_BOOT, true, 200u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 0u);
    CHECK_EQ(wx_sched_cycle(&g_s)->ms, 9000u);
    CHECK(!wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    CHECK_EQ(pollat(tc + 9001u), WX_ACT_NONE);
    CHECK_EQ(pollat(tc + 599999u), WX_ACT_NONE);                                            /* 周期の開始から 10 分(終わりからではない) */
    fetch_at(tc + 600000u, 0u, WX_SRC_TIMER, true, 200u);
    fetch_at(tc + 603000u, 1u, WX_SRC_TIMER, true, 200u);
    fetch_at(tc + 606000u, 2u, WX_SRC_TIMER, true, 200u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 2u);
    CHECK_EQ(pollat(tc + 1199999u), WX_ACT_NONE);
    CHECK_EQ(pollat(tc + 1200000u), WX_ACT_FETCH);                                          /* 周期のずれが積もらない(開始 + 10 分 x n) */
    CHECK_EQ(wx_sched_sym(&g_s), 0u);

    TEST("H-6 ② 1 銘柄(sym 1)だけ失敗 → 残りを取る → 20 秒後に失敗分だけ(src=retry)・最大 2 回 → 周期の記録 → 次の周期まで待つ");
    tc = boot_connect(t0);
    fetch_at(tc, 0u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 3000u, 1u, WX_SRC_BOOT, false, 404u);                                     /* 失敗しても止めない */
    CHECK_EQ(wx_sched_failed_mask(&g_s), 2u);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);                                                /* 1 銘柄の失敗は「オフライン」にしない */
    fetch_at(tc + 6000u, 2u, WX_SRC_BOOT, true, 200u);                                      /* 残りの銘柄は続けて取る */
    te = tc + 9000u;
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 0u);                                                  /* やり直しがあるので、周期はまだ終わっていない */
    CHECK(!wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON); CHECK_EQ(wx_sched_failed_mask(&g_s), 2u);
    CHECK_EQ(pollat(te + WX_RETRY_MS - 1u), WX_ACT_NONE);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, te + 100u), WX_REQ_BUSY);                 /* 周期の途中(やり直しの待ち)は busy */
    fetch_at(te + WX_RETRY_MS, 1u, WX_SRC_RETRY, false, 404u);                              /* やり直し 1 回目: 銘柄 1 だけ */
    te = te + WX_RETRY_MS + 3000u;
    CHECK_EQ(pollat(te + WX_RETRY_MS - 1u), WX_ACT_NONE);
    fetch_at(te + WX_RETRY_MS, 1u, WX_SRC_RETRY, false, 404u);                              /* 2 回目 */
    te = te + WX_RETRY_MS + 3000u;
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 5u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 2u);
    CHECK_EQ(wx_sched_cycle(&g_s)->ms, te - tc);                                            /* 周期の長さ = やり直しの待ちを含む */
    CHECK_EQ(wx_sched_failed_mask(&g_s), 2u);                                               /* 失敗は残る(表示は古い値・F5) */
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON); CHECK(!wx_sched_busy(&g_s));
    CHECK_EQ(pollat(te + WX_RETRY_MS), WX_ACT_NONE);                                        /* 3 回目はやり直さない(最大 2 回) */
    CHECK_EQ(pollat(te + 100000u), WX_ACT_NONE);
    CHECK_EQ(pollat(tc + 599999u), WX_ACT_NONE);                                            /* 次の周期は、失敗があっても開始から 10 分 */
    CHECK_EQ(pollat(tc + 600000u), WX_ACT_FETCH);
    CHECK_EQ(wx_sched_sym(&g_s), 0u); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TIMER);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 0u);                                               /* 新しい周期で失敗の印は消える */
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, tc + 603000u);
    fetch_at(tc + 603000u, 1u, WX_SRC_TIMER, true, 200u);                                   /* 失敗した銘柄も、もう一度取る */
    fetch_at(tc + 606000u, 2u, WX_SRC_TIMER, true, 200u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 2u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u);

    TEST("H-6 ②b やり直しが 1 回目で成功 → 周期が終わる(fail なし)。やり直しは失敗分だけ(sym 0 と 2 は取り直さない)・複数の失敗(bit0 と bit2)");
    tc = boot_connect(t0);
    fetch_at(tc, 0u, WX_SRC_BOOT, false, 500u);
    fetch_at(tc + 3000u, 1u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 6000u, 2u, WX_SRC_BOOT, false, 0u);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 5u);
    te = tc + 9000u;
    fetch_at(te + WX_RETRY_MS, 0u, WX_SRC_RETRY, true, 200u);                               /* 小さい番号から */
    CHECK_EQ(wx_sched_failed_mask(&g_s), 4u); CHECK_EQ(wx_sched_cycle(&g_s)->n, 0u);
    fetch_at(te + WX_RETRY_MS + 3000u, 2u, WX_SRC_RETRY, true, 200u);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 0u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 0u);
    CHECK_EQ(pollat(te + 100000u), WX_ACT_NONE);                                            /* 全部取れたので、やり直しは続かない */
    CHECK_EQ(pollat(tc + 600000u), WX_ACT_FETCH);

    TEST("H-6 ③ 429: その周期を打ち切り(残りは取らない)・300 秒後に周期を最初からやり直す(src=timer)・待ちの間の更新要求は無視");
    tc = boot_connect(t0);
    fetch_at(tc, 0u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 3000u, 1u, WX_SRC_BOOT, false, 429u);
    CHECK(!wx_sched_busy(&g_s));                                                            /* sym 2 は取らない */
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);                                                /* 値は残る・オフライン表示にしない */
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 6u);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 6u);
    te = tc + 6000u;
    CHECK_EQ(pollat(te + WX_RATE_MS - 1u), WX_ACT_NONE);
    CHECK_EQ(WX_RATE_MS, 300000u);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, te + 1000u), WX_REQ_IGNORED);             /* Yahoo に当てない */
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, te + WX_RATE_MS - 1u), WX_REQ_IGNORED);
    CHECK_EQ(pollat(te + WX_RATE_MS), WX_ACT_FETCH);
    CHECK_EQ(wx_sched_sym(&g_s), 0u); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TIMER);           /* 最初の銘柄から */
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, te + WX_RATE_MS + 3000u);
    fetch_at(te + WX_RATE_MS + 3000u, 1u, WX_SRC_TIMER, true, 200u);
    fetch_at(te + WX_RATE_MS + 6000u, 2u, WX_SRC_TIMER, true, 200u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 2u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u);
    CHECK_EQ(pollat(te + WX_RATE_MS + 599999u), WX_ACT_NONE);                               /* 次の周期は、やり直した周期の開始から 10 分 */
    CHECK_EQ(pollat(te + WX_RATE_MS + 600000u), WX_ACT_FETCH);
    /* 最初の銘柄で 429(残り 2 つは取らず、失敗は 3 つ) */
    tc = boot_connect(t0);
    fetch_at(tc, 0u, WX_SRC_BOOT, false, 429u);
    CHECK_EQ(wx_sched_failed_mask(&g_s), 7u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 0u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 7u);
    CHECK_EQ(pollat(tc + 3000u + WX_RATE_MS - 1u), WX_ACT_NONE);
    CHECK_EQ(pollat(tc + 3000u + WX_RATE_MS), WX_ACT_FETCH);
    /* 429 以外(500 など)は 429 の待ちにならない */
    tc = boot_connect(t0);
    fetch_at(tc, 0u, WX_SRC_BOOT, false, 500u); fetch_at(tc + 3000u, 1u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 6000u, 2u, WX_SRC_BOOT, true, 200u);
    CHECK_EQ(pollat(tc + 9000u + WX_RETRY_MS), WX_ACT_FETCH); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_RETRY);
    /* やり直しの最中の 429 も打ち切る */
    wx_sched_done(&g_s, WX_ACT_FETCH, false, 429u, tc + 9000u + WX_RETRY_MS + 3000u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_failed_mask(&g_s), 1u);
    CHECK_EQ(pollat(tc + 9000u + WX_RETRY_MS + 3000u + WX_RATE_MS - 1u), WX_ACT_NONE);
    CHECK_EQ(pollat(tc + 9000u + WX_RETRY_MS + 3000u + WX_RATE_MS), WX_ACT_FETCH); CHECK_EQ(wx_sched_sym(&g_s), 0u); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TIMER);

    TEST("H-6 ④ 全部失敗(応答なし): やり直し 2 回の後に再接続(net=err)。再接続の失敗は 60 / 120 / 300 / 300 秒。成功したら新しい周期");
    tc = boot_connect(t0);
    for (i = 0; i < 3; i++) { fetch_at(tc + (uint32_t)i * 3000u, (uint8_t)i, WX_SRC_BOOT, false, 0u); }
    CHECK_EQ(wx_sched_failed_mask(&g_s), 7u); CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    CHECK_EQ(pollat(tc + 9000u + WX_RETRY_MS), WX_ACT_FETCH); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_RETRY); CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);   /* やり直しの間は「オフライン」にしない */
    tc = boot_connect(t0);
    te = dead_cycle(tc, WX_SRC_BOOT, 0u);
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 0u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 7u);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ERR);
    CHECK(wx_sched_busy(&g_s));                                                              /* 再接続を始める状態 */
    CHECK_EQ(pollat(te + 1u), WX_ACT_CONNECT);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ERR);                                                /* 再接続の間も、できるまでオフライン表示(conn にちらつかせない) */
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, te + 8000u);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ERR); CHECK(!wx_sched_busy(&g_s));
    te += 8000u;
    CHECK_EQ(pollat(te + 59999u), WX_ACT_NONE); CHECK_EQ(pollat(te + 60000u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, te + 60000u + 8000u);
    te = te + 60000u + 8000u;
    CHECK_EQ(pollat(te + 119999u), WX_ACT_NONE); CHECK_EQ(pollat(te + 120000u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, te + 120000u + 8000u);
    te = te + 120000u + 8000u;
    CHECK_EQ(pollat(te + 299999u), WX_ACT_NONE); CHECK_EQ(pollat(te + 300000u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, te + 300000u + 8000u);
    te = te + 300000u + 8000u;
    CHECK_EQ(pollat(te + 299999u), WX_ACT_NONE); CHECK_EQ(pollat(te + 300000u), WX_ACT_CONNECT);          /* 300 秒のまま */
    wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, te + 300000u + 8000u);
    te = te + 300000u + 8000u;
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    good_cycle(te, WX_SRC_TIMER);                                                            /* 直ちに新しい周期(3 銘柄とも最初から。src は timer) */
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 2u); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 7u); CHECK_EQ(wx_sched_failed_mask(&g_s), 0u);
    /* 接続の失敗の回数は、成功で数え直す: もう一度全部失敗して再接続が失敗したら、待ちは 60 秒に戻る */
    te = dead_cycle(te + 600000u, WX_SRC_TIMER, 0u);
    CHECK_EQ(pollat(te + 1u), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, te + 8000u);
    CHECK_EQ(pollat(te + 8000u + 59999u), WX_ACT_NONE); CHECK_EQ(pollat(te + 8000u + 60000u), WX_ACT_CONNECT);

    TEST("H-6 ④b 全部失敗でも、応答があった失敗(http ≠ 0)が混じるなら再接続しない(10 分の間隔を守る)・1 つでも成功したら再接続しない");
    tc = boot_connect(t0);
    for (i = 0; i < 3; i++) { fetch_at(tc + (uint32_t)i * 3000u, (uint8_t)i, WX_SRC_BOOT, false, (i == 1) ? 403u : 0u); }
    te = tc + 9000u;
    for (i = 0; i < 6; i++) { fetch_at(te + WX_RETRY_MS + (uint32_t)i * 3000u + ((i >= 3) ? WX_RETRY_MS : 0u), (uint8_t)(i % 3), WX_SRC_RETRY, false, (i % 3 == 1) ? 403u : 0u); }
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON); CHECK(!wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 7u);
    CHECK_EQ(pollat(tc + 599999u), WX_ACT_NONE);
    te = dead_cycle(tc + 600000u, WX_SRC_TIMER, 404u);                                  /* 3 銘柄とも 404(応答はある): 再接続しない */
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON); CHECK(!wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_cycle(&g_s)->n, 2u);
    tc = boot_connect(t0);
    fetch_at(tc, 0u, WX_SRC_BOOT, true, 200u); fetch_at(tc + 3000u, 1u, WX_SRC_BOOT, false, 0u); fetch_at(tc + 6000u, 2u, WX_SRC_BOOT, false, 0u);
    te = tc + 9000u;
    fetch_at(te + WX_RETRY_MS, 1u, WX_SRC_RETRY, false, 0u); fetch_at(te + WX_RETRY_MS + 3000u, 2u, WX_SRC_RETRY, false, 0u);
    te = te + WX_RETRY_MS + 6000u;
    fetch_at(te + WX_RETRY_MS, 1u, WX_SRC_RETRY, false, 0u); fetch_at(te + WX_RETRY_MS + 3000u, 2u, WX_SRC_RETRY, false, 0u);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON); CHECK(!wx_sched_busy(&g_s)); CHECK_EQ(wx_sched_cycle(&g_s)->ok, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->fail, 6u);

    TEST("H-6 ⑤ now_ms が 32bit で巻き戻っても、周期は開始から 600,000ms・20 秒・300 秒・60 秒が同じ");
    tc = boot_connect(0xFFFFC000u);                                                          /* 周期の途中で 0 を越える */
    CHECK(tc > 0xF0000000u);                                                                 /* 周期の開始は巻き戻る前 */
    te = good_cycle(tc, WX_SRC_BOOT);
    CHECK(te < tc);                                                                          /* 終わりは巻き戻った後 */
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 1u); CHECK_EQ(wx_sched_cycle(&g_s)->ms, 9000u);
    CHECK_EQ(pollat(tc + 599999u), WX_ACT_NONE);
    fetch_at(tc + 600000u, 0u, WX_SRC_TIMER, false, 500u); fetch_at(tc + 603000u, 1u, WX_SRC_TIMER, true, 200u); fetch_at(tc + 606000u, 2u, WX_SRC_TIMER, true, 200u);
    CHECK_EQ(pollat(tc + 609000u + WX_RETRY_MS - 1u), WX_ACT_NONE); CHECK_EQ(pollat(tc + 609000u + WX_RETRY_MS), WX_ACT_FETCH);
    CHECK_EQ(wx_sched_src(&g_s), WX_SRC_RETRY); CHECK_EQ(wx_sched_sym(&g_s), 0u);
    wx_sched_done(&g_s, WX_ACT_FETCH, false, 429u, tc + 609000u + WX_RETRY_MS + 3000u);
    CHECK_EQ(pollat(tc + 609000u + WX_RETRY_MS + 3000u + WX_RATE_MS - 1u), WX_ACT_NONE);
    CHECK_EQ(pollat(tc + 609000u + WX_RETRY_MS + 3000u + WX_RATE_MS), WX_ACT_FETCH);
    tc = boot_connect(0xFFFF0000u);
    for (i = 0; i < 3; i++) { fetch_at(tc + (uint32_t)i * 3000u, (uint8_t)i, WX_SRC_BOOT, false, 0u); }
    CHECK_EQ(pollat(tc + 9000u + WX_RETRY_MS - 1u), WX_ACT_NONE); CHECK_EQ(pollat(tc + 9000u + WX_RETRY_MS), WX_ACT_FETCH);

    TEST("H-6 ⑥ 更新要求: 周期の開始から 5 秒未満は無視・5 秒で受付(src が変わる)・取得中は busy・接続の待ちは直ちに再接続");
    tc = boot_connect(t0);
    fetch_at(tc, 0u, WX_SRC_BOOT, true, 200u);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, tc + 3100u), WX_REQ_BUSY);                 /* 取得中(周期の途中)は busy */
    fetch_at(tc + 3000u, 1u, WX_SRC_BOOT, true, 200u);
    fetch_at(tc + 6000u, 2u, WX_SRC_BOOT, true, 200u);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, tc + 9500u), WX_REQ_OK);                   /* 周期の開始から 9.5 秒 ≧ 5 秒 */
    CHECK(wx_sched_busy(&g_s));
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, tc + 9600u), WX_REQ_BUSY);                  /* 受け付けた後の poll 前も busy */
    fetch_at(tc + 9700u, 0u, WX_SRC_TOUCH, true, 200u);                                       /* 新しい周期の開始 = tc + 9700 */
    fetch_at(tc + 12700u, 1u, WX_SRC_TOUCH, true, 200u);
    fetch_at(tc + 15700u, 2u, WX_SRC_TOUCH, true, 200u);
    tc = tc + 9700u;
    CHECK_EQ(wx_sched_cycle(&g_s)->n, 2u);
    CHECK_EQ(pollat(tc + 599999u), WX_ACT_NONE);                                              /* 次の定期は更新要求で始まった周期から 10 分 */
    CHECK_EQ(pollat(tc + 600000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TIMER);
    /* 5 秒の境界(取得が一瞬で終わった周期) */
    tc = boot_connect(t0);
    fetch_at(tc, 0u, WX_SRC_BOOT, true, 200u);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, tc);                                        /* 二重の完了は無視される(待ちの銘柄は進まない) */
    CHECK_EQ(pollat(tc + 3000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_sym(&g_s), 1u);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, tc + 3000u);
    CHECK_EQ(pollat(tc + 3000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_sym(&g_s), 2u);
    wx_sched_done(&g_s, WX_ACT_FETCH, true, 200u, tc + 3000u);                                /* 周期の開始 tc、終わり tc + 3000 */
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, tc + 4999u), WX_REQ_IGNORED);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, tc + 5000u), WX_REQ_OK);
    CHECK_EQ(pollat(tc + 5000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_UART); CHECK_EQ(wx_sched_sym(&g_s), 0u);
    CHECK_EQ(WX_MIN_GAP_MS, 5000u);
    /* オフライン(接続の待ち)の更新要求は直ちに再接続 */
    tc = boot_connect(t0);
    te = dead_cycle(tc, WX_SRC_BOOT, 0u);
    CHECK_EQ(pollat(te), WX_ACT_CONNECT);
    wx_sched_done(&g_s, WX_ACT_CONNECT, false, 0u, te + 8000u);
    te += 8000u;
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ERR); CHECK(!wx_sched_busy(&g_s));
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, te + 100u), WX_REQ_OK);                    /* 60 秒を待たない */
    CHECK_EQ(pollat(te + 100u), WX_ACT_CONNECT); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TOUCH);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_UART, te + 200u), WX_REQ_BUSY);
    wx_sched_done(&g_s, WX_ACT_CONNECT, true, 0u, te + 5000u);
    CHECK_EQ(wx_sched_net(&g_s), WX_NET_ON);
    CHECK_EQ(pollat(te + 5000u), WX_ACT_FETCH); CHECK_EQ(wx_sched_src(&g_s), WX_SRC_TOUCH); CHECK_EQ(wx_sched_sym(&g_s), 0u);
    /* 接続の待ち以外の待ち(起動待ち・接続中)は busy */
    wx_sched_init(&g_s, true, t0);
    CHECK_EQ(wx_sched_request(&g_s, WX_SRC_TOUCH, t0 + 10u), WX_REQ_BUSY);
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

/* H-6b: 同期済みの時計に対し、first_ms の 100 秒後(基準の瞬間 f2 - 700)に Date: = 今の時計 - d 秒 が来る。採用されたかは wx_clock_now(f2 - 700) で見る */
static uint32_t back_case(uint32_t jst, uint32_t f1, uint32_t d)
{
    wx_clock_t k;
    uint32_t   f2 = f1 + 100000u;                               /* uint32 の足し算(巻き戻ってよい) */
    uint32_t   s = 0u;

    wx_clock_init(&k);
    wx_clock_sync(&k, jst, f1);
    wx_clock_sync(&k, jst + 100u - d, f2);                      /* 時計は同じ瞬間に jst + 100 */
    CHECK(wx_clock_now(&k, f2 - WX_SYNC_BIAS_MS, &s));
    return s - jst;
}

static void t_clock_back(void)
{
    uint32_t jst = 1795580653u + 32400u;                        /* 2026-11-25 13:24:13 JST */
    wx_clock_t k;
    uint32_t   s = 0u;
    wx_date_t  d;

    TEST("H-6b 時計の同期: 未同期は採用・進む側は採用・遅れ 1〜30 秒は採用しない(戻らない)・31 秒以上は採用・32bit の巻き戻り・日付の変わり目");
    CHECK_EQ(WX_CLOCK_BACK_MAX_S, 30u);
    wx_clock_init(&k);
    wx_clock_sync(&k, jst, 100000u);                            /* 初回 */
    CHECK(wx_clock_now(&k, 100000u, &s)); CHECK_EQ(s, jst);
    CHECK_EQ(back_case(jst, 100000u, 0u), 100u);                /* 同じ */
    CHECK_EQ(back_case(jst, 100000u, 1u), 100u);                /* 1 秒の遅れ: 採用しない */
    CHECK_EQ(back_case(jst, 100000u, 7u), 100u);
    CHECK_EQ(back_case(jst, 100000u, 29u), 100u);
    CHECK_EQ(back_case(jst, 100000u, 30u), 100u);               /* ちょうど 30 秒: 採用しない */
    CHECK_EQ(back_case(jst, 100000u, 31u), 69u);                /* 31 秒: 採用 */
    CHECK_EQ(back_case(jst, 100000u, 100u), 0u);                /* 大きな遅れ: 採用 */
    CHECK_EQ(back_case(jst, 100000u, 99999u), 100u - 99999u);   /* 遅れが基準より大きい(基準より前の日時): 採用(uint32 の差) */
    /* 進み */
    {
        wx_clock_t k2;
        wx_clock_init(&k2);
        wx_clock_sync(&k2, jst, 100000u);
        wx_clock_sync(&k2, jst + 105u, 200000u);                /* 時計は jst + 100 → 5 秒進み */
        CHECK(wx_clock_now(&k2, 200000u - WX_SYNC_BIAS_MS, &s)); CHECK_EQ(s, jst + 105u);
        wx_clock_sync(&k2, jst + 5000u, 300000u);
        CHECK(wx_clock_now(&k2, 300000u - WX_SYNC_BIAS_MS, &s)); CHECK_EQ(s, jst + 5000u);
    }
    /* 32bit の巻き戻り(first_ms が 0xFFFFFFFF をまたぐ) */
    CHECK_EQ(back_case(jst, 0xFFFFFF00u, 10u), 100u);           /* 棄却 */
    CHECK_EQ(back_case(jst, 0xFFFFFF00u, 30u), 100u);
    CHECK_EQ(back_case(jst, 0xFFFFFF00u, 31u), 69u);            /* 採用 */
    CHECK_EQ(back_case(jst, 0xFFFFFFFFu - 99999u, 10u), 100u);  /* 2 回目の基準がちょうど巻き戻る位置 */
    /* 棄却したあとも、元の時計がそのまま進む */
    wx_clock_init(&k);
    wx_clock_sync(&k, jst, 100000u);
    wx_clock_sync(&k, jst + 8u, 200000u);                       /* 時計は jst + 100 のところへ jst + 8(92 秒遅れ)→ 採用 */
    wx_clock_sync(&k, jst + 8u + 99u, 300000u);                 /* 時計は jst + 8 + 100 → 1 秒遅れ: 棄却 */
    CHECK(wx_clock_now(&k, 300000u - WX_SYNC_BIAS_MS, &s)); CHECK_EQ(s, jst + 108u);
    CHECK(wx_clock_now(&k, 300000u - WX_SYNC_BIAS_MS + 10000u, &s)); CHECK_EQ(s, jst + 118u);
    /* 日付の変わり目: 2026-12-31 23:59:50 に合わせ、15 秒後 = 2027-01-01 00:00:05 */
    {
        uint32_t m = 1798729200u - 10u + 32400u;                /* 23:59:50 */
        wx_clock_init(&k);
        wx_clock_sync(&k, m, 5000u);
        wx_clock_sync(&k, m + 15u, 5000u + 15000u);             /* 進み: 採用 */
        CHECK(wx_clock_now(&k, 20000u - WX_SYNC_BIAS_MS, &s)); wx_date_fields(s, &d);
        CHECK_EQ(d.year, 2027); CHECK_EQ(d.month, 1); CHECK_EQ(d.day, 1); CHECK_EQ(d.hour, 0); CHECK_EQ(d.min, 0); CHECK_EQ(d.sec, 5);
        wx_clock_sync(&k, m + 8u, 20000u);                      /* 00:00:05 の時計に、日をまたいだ前の 23:59:58(7 秒遅れ)→ 棄却 */
        CHECK(wx_clock_now(&k, 20000u - WX_SYNC_BIAS_MS, &s)); wx_date_fields(s, &d);
        CHECK_EQ(d.year, 2027); CHECK_EQ(d.day, 1); CHECK_EQ(d.hour, 0); CHECK_EQ(d.sec, 5);
    }
}

int main(void)
{
    t_request();
    t_fmt();
    t_sched();
    t_date();
    t_clock();
    t_clock_back();
    TEST_REPORT("test_wx_core");
}
