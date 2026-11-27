/* wifi_core.c - Wi-Fi(ESP32 AT)試作の論理部。標準Cのみ。FITのヘッダを include しない。
 * 仕様: docs/design.md §21.4(Wi-Fi)・§5(株価の取得)、docs/design-uart.md §12。
 * SSID・パスワードは wifi_build_cwjap の入力と、送信コマンドの組み立て中の c->cmd にだけ現れる(送信後・受信行の処理後に消す)。ログ関数は秘密を引数に取らない。
 * scan(AT+CWLAP)は、受けた各行の SSID を c->ssid と比べるだけ。AP の名前・MAC は保存せず、ログにも出さない(出すのは件数と自分の AP の ch / rssi / ecn、分類できなかった行の「形」)。 */
#include <stdio.h>
#include <string.h>
#include "wifi_core.h"
#include "wx_core.h"
#include "wifi_sw.h"

#if APP_WIFI_ON   /* 0 の時は中身を消す(wifi_sw.h。APP_WIFI_TEST か APP_WX) */

#define EN_LOW_MS        (20u)    /* reset: EN を Low にしておく時間 */
#define QUIET_TIMEOUT_MS (500u)   /* タイムアウトの後、遅れて来る応答を捨てる時間 */
#define QUIET_FAIL_MS    (300u)   /* ERROR / FAIL の後(at の再試行の間隔) */

enum { PH_IDLE = 0, PH_EN_LOW, PH_SEND, PH_WAIT, PH_QUIET, PH_SEND_DATA };    /* PH_SEND_DATA = wx_send の要求本体(> の後)を送る */
enum { AFTER_RETRY = 0, AFTER_NEXT };

/* wx_flags(取得の間の印)。F_OK .. F_SENT は step ごとに消す。F_CLOSED / F_OVER / F_FIRST は取得(wx_mux)の頭で消す */
enum { RECV_CLOSED = 0, RECV_TIMEOUT, RECV_ERROR };    /* wx_recv の終わり方 */
enum { F_OK = 0x01, F_CONN = 0x02, F_PROMPT = 0x04, F_REQ = 0x08, F_SENT = 0x10, F_CLOSED = 0x20, F_OVER = 0x40, F_FIRST = 0x80 };
#define F_STEP_KEEP   ((uint8_t)(F_CLOSED | F_OVER | F_FIRST))
#define IPD_MAX       (2048u)
#define SSL_TCP_TIMEOUT_MS (20000u)                                    /* wx_tcp の SSL の時(ハンドシェイク。design.md §5.5.2 の 0-1)。TCP は k_def のまま */

#define M(s)          (1u << (unsigned)(s))
#define MASK_ALL      (0x7FFEu)                                         /* reset 〜 ping_dns(bit 1〜14) */
#define MASK_BOOT     (M(WIFI_STEP_RESET) | M(WIFI_STEP_BOOT) | M(WIFI_STEP_AT))
#define MASK_AT       (M(WIFI_STEP_AT))
#define MASK_CONNECT  (MASK_BOOT | M(WIFI_STEP_ECHO) | M(WIFI_STEP_MODE) | M(WIFI_STEP_QAP) | M(WIFI_STEP_JOIN) | M(WIFI_STEP_IP))
#define MASK_WX       (M(WIFI_STEP_WX_MUX) | M(WIFI_STEP_WX_CLOSE) | M(WIFI_STEP_WX_TCP) | M(WIFI_STEP_WX_SEND) | M(WIFI_STEP_WX_RECV))
#if APP_WIFI_TEST
#define MASK_WX_SSLCONF (MASK_WX | M(WIFI_STEP_WX_SSLCONF))            /* 専用モードの z(WX_TGT_YAHOO_SSL_NOAUTH)だけ。製品の範囲には入れない(design.md §5.9) */
#endif

/* step の表(AT 1.1.3.0 と 2.x で書式が同じコマンドだけ。版では切り替えない)。コマンドを変える時はここだけ直す。cmd = NULL の step は個別に処理する */
typedef struct {
    const char *cmd;          /* \r\n 込み */
    uint16_t    timeout_ms;
    uint8_t     max_try;
    uint8_t     cont;         /* 1 = 失敗・タイムアウトでも次へ進む */
} step_def_t;

static const step_def_t k_def[WIFI_STEP_END] = {
    /* none     */ { NULL,                                    0u,     0u, 0u },
    /* reset    */ { NULL,                                    0u,     1u, 0u },
    /* boot     */ { NULL,                                    5000u,  1u, 1u },
    /* at       */ { "AT\r\n",                                1000u,  5u, 0u },
    /* echo     */ { "ATE0\r\n",                              1000u,  1u, 1u },
    /* gmr      */ { "AT+GMR\r\n",                            2000u,  1u, 1u },
    /* mode     */ { "AT+CWMODE=1\r\n",                       2000u,  1u, 0u },
    /* qap      */ { "AT+CWQAP\r\n",                          2000u,  1u, 1u },
    /* lapopt   */ { "AT+CWLAPOPT=1,31\r\n",                  1000u,  1u, 1u },
    /* scan     */ { "AT+CWLAP\r\n",                          15000u, 2u, 1u },
    /* join     */ { NULL,                                    20000u, 2u, 0u },
    /* ip       */ { "AT+CIFSR\r\n",                          2000u,  1u, 1u },
    /* dns      */ { "AT+CIPDOMAIN=\"www.google.com\"\r\n",   15000u, 1u, 1u },
    /* ping_ip  */ { "AT+PING=\"8.8.8.8\"\r\n",               10000u, 1u, 0u },
    /* ping_dns */ { "AT+PING=\"www.google.com\"\r\n",        15000u, 1u, 0u },
    /* wx_mux   */ { "AT+CIPMUX=0\r\n",                         1000u,  1u, 1u },
    /* wx_close */ { "AT+CIPCLOSE\r\n",                         2000u,  1u, 1u },
    /* wx_sslconf */ { "AT+CIPSSLCCONF=0,0,0\r\n",              2000u,  1u, 1u },
    /* wx_tcp   */ { NULL,                                    10000u, 1u, 0u },        /* AT+CIPSTART は wx_build_cipstart(接続先) */
    /* wx_send  */ { NULL,                                    5000u,  1u, 0u },
    /* wx_recv  */ { NULL,                                    15000u, 1u, 0u }
};

static const char *const k_step_name[WIFI_STEP_END] = {
    "none", "reset", "boot", "at", "echo", "gmr", "mode", "qap", "lapopt", "scan", "join", "ip", "dns", "ping_ip", "ping_dns",
    "wx_mux", "wx_close", "wx_sslconf", "wx_tcp", "wx_send", "wx_recv"
};
static const char *const k_res_name[WIFI_RES_END]     = { "start", "ok", "fail", "timeout", "skip" };
static const char *const k_key_name[WIFI_KEY_END]     = { "ver", "sdk", "ip", "gw", "mask", "dns", "ping", "scan", "ap", "oth" };
static const char *const k_state_name[WIFI_STATE_END] = { "idle", "run", "done" };

/* ================================================================ ログ行の整形 */

/* snprintf の結果を「入りきった長さ」に正規化。失敗・切り詰めは buf を空にして 0 */
static size_t fin(char *buf, size_t size, int n)
{
    if ((n <= 0) || ((size_t)n >= size)) {
        buf[0] = '\0';
        return 0u;
    }
    return (size_t)n;
}

static bool arg_ok(char *buf, size_t size)
{
    if ((buf == NULL) || (size == 0u)) {
        return false;
    }
    buf[0] = '\0';
    return true;
}

const char *wifi_step_name(wifi_step_t step)
{
    return ((unsigned)step < (unsigned)WIFI_STEP_END) ? k_step_name[step] : NULL;
}

size_t wifi_fmt_step(char *buf, size_t size, wifi_step_t step, wifi_res_t res, uint32_t try_n, uint32_t ms, uint32_t code)
{
    if (!arg_ok(buf, size) || ((unsigned)step < 1u) || ((unsigned)step >= (unsigned)WIFI_STEP_END)
        || ((unsigned)res >= (unsigned)WIFI_RES_END)) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "[wifi] step=%s res=%s try=%lu ms=%lu code=%lu\r\n",
                                   k_step_name[step], k_res_name[res],
                                   (unsigned long)try_n, (unsigned long)ms, (unsigned long)code));
}

size_t wifi_fmt_result(char *buf, size_t size, bool pass, wifi_step_t stage, uint32_t ping_ip, uint32_t ping_dns)
{
    if (!arg_ok(buf, size) || ((unsigned)stage >= (unsigned)WIFI_STEP_END)) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "[wifi] result=%s stage=%s ping_ip=%lu ping_dns=%lu\r\n",
                                   pass ? "pass" : "fail", k_step_name[stage],
                                   (unsigned long)ping_ip, (unsigned long)ping_dns));
}

size_t wifi_fmt_state(char *buf, size_t size, wifi_state_t st, wifi_step_t step, uint32_t bytes)
{
    if (!arg_ok(buf, size) || ((unsigned)st >= (unsigned)WIFI_STATE_END) || ((unsigned)step >= (unsigned)WIFI_STEP_END)) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "[wifi] state=%s step=%s bytes=%lu\r\n",
                                   k_state_name[st], k_step_name[step], (unsigned long)bytes));
}

size_t wifi_fmt_val(char *buf, size_t size, wifi_key_t key, const char *val)
{
    size_t   pos;
    size_t   i;
    size_t   vl;
    int      n;
    unsigned c;

    if (!arg_ok(buf, size) || ((unsigned)key >= (unsigned)WIFI_KEY_END) || (val == NULL)) {
        return 0u;
    }
    vl = strlen(val);
    if (vl > WIFI_VAL_MAX) {
        return 0u;
    }
    n = snprintf(buf, size, "[wifiv] key=%s val=", k_key_name[key]);
    if ((n <= 0) || (((size_t)n + vl + 2u) >= size)) {
        buf[0] = '\0';
        return 0u;
    }
    pos = (size_t)n;
    for (i = 0u; i < vl; i++) {
        c = (unsigned)(unsigned char)val[i];
        buf[pos++] = ((c >= 0x21u) && (c <= 0x7Eu)) ? (char)c : '_';
    }
    buf[pos++] = '\r';
    buf[pos++] = '\n';
    buf[pos]   = '\0';
    return pos;
}

size_t wifi_fmt_help(char *buf, size_t size)
{
    if (!arg_ok(buf, size)) {
        return 0u;
    }
    return fin(buf, size, snprintf(buf, size, "[help] d=run n=boot p=at s=state ?=help\r\n"));
}

/* ================================================================ AT+CWJAP の組み立て */

static bool put_ch(char *buf, size_t size, size_t *pos, char ch)
{
    if ((*pos + 1u) >= size) {
        return false;
    }
    buf[*pos] = ch;
    (*pos)++;
    buf[*pos] = '\0';
    return true;
}

static bool put_str(char *buf, size_t size, size_t *pos, const char *s)
{
    while (*s != '\0') {
        if (!put_ch(buf, size, pos, *s)) {
            return false;
        }
        s++;
    }
    return true;
}

/* " , \ の前に \ を付けて書く。制御文字は拒否 */
static bool put_esc(char *buf, size_t size, size_t *pos, const char *s)
{
    for (; *s != '\0'; s++) {
        if ((unsigned char)*s < 0x20u) {
            return false;
        }
        if ((*s == '"') || (*s == ',') || (*s == '\\')) {
            if (!put_ch(buf, size, pos, '\\')) {
                return false;
            }
        }
        if (!put_ch(buf, size, pos, *s)) {
            return false;
        }
    }
    return true;
}

size_t wifi_build_cwjap(char *buf, size_t size, const char *ssid, const char *pwd)
{
    size_t pos = 0u;

    if (!arg_ok(buf, size) || (ssid == NULL) || (ssid[0] == '\0') || (pwd == NULL)) {
        return 0u;
    }
    if (!put_str(buf, size, &pos, "AT+CWJAP=\"") || !put_esc(buf, size, &pos, ssid)
        || !put_str(buf, size, &pos, "\",\"") || !put_esc(buf, size, &pos, pwd)
        || !put_str(buf, size, &pos, "\"")) {
        buf[0] = '\0';
        return 0u;
    }
    return pos;
}

/* ================================================================ 受信行の分類 */

static const struct { const char *s; wifi_line_t kind; } k_exact[] = {
    { "OK",              WIFI_L_OK },
    { "ERROR",           WIFI_L_ERROR },
    { "FAIL",            WIFI_L_FAIL },
    { "ready",           WIFI_L_READY },
    { "WIFI CONNECTED",  WIFI_L_CONNECTED },
    { "WIFI GOT IP",     WIFI_L_GOT_IP },
    { "WIFI DISCONNECT", WIFI_L_DISCONNECT },
    { "CONNECT",         WIFI_L_CONNECT },
    { "CLOSED",          WIFI_L_CLOSED },
    { "SEND OK",         WIFI_L_SEND_OK },
    { "SEND FAIL",       WIFI_L_SEND_FAIL },
    { ">",               WIFI_L_PROMPT },
    { "DNS Fail",        WIFI_L_DNS_FAIL }
};

static bool has_prefix(const char *s, const char *prefix, const char **rest)
{
    size_t n = strlen(prefix);

    if (strncmp(s, prefix, n) != 0) {
        return false;
    }
    *rest = s + n;
    return true;
}

/* 先頭の 10 進数(桁数 1〜9)を読む。*end = 数字の次。数字が無い・10 桁以上なら false */
static bool read_num(const char *s, uint32_t *v, const char **end)
{
    uint32_t x = 0u;
    unsigned n = 0u;

    while ((*s >= '0') && (*s <= '9')) {
        if (n >= 9u) {
            return false;
        }
        x = (x * 10u) + (uint32_t)(*s - '0');
        n++;
        s++;
    }
    if (n == 0u) {
        return false;
    }
    *v = x;
    *end = s;
    return true;
}

/* 全体が数字だけ(1〜9 桁)なら true */
static bool parse_u32(const char *s, uint32_t *v)
{
    const char *e;

    return read_num(s, v, &e) && (*e == '\0');
}

/* s = a.b.c.d または "a.b.c.d" の形(引用符は任意。付けるなら両側。各 0〜255・後ろに何も無い)なら true。out = a.b.c.d(最大 15 文字 + NUL) */
static bool parse_ipv4(const char *s, char *out)
{
    unsigned part;
    size_t   pos = 0u;
    uint32_t v;
    const char *e;
    bool     q = (*s == '"');

    if (q) {
        s++;
    }
    for (part = 0u; part < 4u; part++) {
        if (!read_num(s, &v, &e) || (v > 255u) || ((e - s) > 3)) {
            return false;
        }
        while (s < e) {
            out[pos++] = *s++;
        }
        if (part < 3u) {
            if (*s != '.') {
                return false;
            }
            out[pos++] = *s++;
        }
    }
    if (q) {
        if (*s != '"') {
            return false;
        }
        s++;
    }
    if (*s != '\0') {
        return false;
    }
    out[pos] = '\0';
    return true;
}

/* s から最大 WIFI_VAL_MAX 文字を、0x21〜0x7E 以外を '_' にして text へ */
static void copy_val(char *text, const char *s)
{
    size_t   i;
    unsigned c;

    for (i = 0u; (i < WIFI_VAL_MAX) && (s[i] != '\0'); i++) {
        c = (unsigned)(unsigned char)s[i];
        text[i] = ((c >= 0x21u) && (c <= 0x7Eu)) ? (char)c : '_';
    }
    text[i] = '\0';
}

void wifi_parse_line(const char *line, wifi_ev_t *ev)
{
    const char *r;
    uint32_t    v;
    size_t      i;

    ev->kind    = WIFI_L_OTHER;
    ev->num     = 0u;
    ev->num2    = 0u;
    ev->text[0] = '\0';
    if (line == NULL) {
        return;
    }
    for (i = 0u; i < (sizeof k_exact / sizeof k_exact[0]); i++) {
        if (strcmp(line, k_exact[i].s) == 0) {
            ev->kind = k_exact[i].kind;
            return;
        }
    }
    if ((line[0] >= '0') && (line[0] <= '9') && (line[1] == ',')) {         /* <n>,CONNECT / <n>,CLOSED(複数接続の形) */
        if (strcmp(line + 2, "CONNECT") == 0) {
            ev->kind = WIFI_L_CONNECT;
            return;
        }
        if (strcmp(line + 2, "CLOSED") == 0) {
            ev->kind = WIFI_L_CLOSED;
            return;
        }
    }
    if (has_prefix(line, "AT version:", &r)) {
        ev->kind = WIFI_L_AT_VER;
        copy_val(ev->text, r);
        if (read_num(r, &v, &r)) {
            ev->num = v;
            if ((*r == '.') && read_num(r + 1, &v, &r)) {
                ev->num2 = v;
            }
        }
    } else if (has_prefix(line, "SDK version:", &r)) {
        ev->kind = WIFI_L_SDK_VER;
        copy_val(ev->text, r);
    } else if (has_prefix(line, "+PING:", &r)) {
        if (strcmp(r, "TIMEOUT") == 0) {
            ev->kind = WIFI_L_PING_TIMEOUT;
        } else if (parse_u32(r, &v)) {
            ev->kind = WIFI_L_PING;
            ev->num  = v;
        }
    } else if (has_prefix(line, "+CWJAP:", &r)) {
        if (parse_u32(r, &v)) {
            ev->kind = WIFI_L_CWJAP;
            ev->num  = v;
        }
    } else if (has_prefix(line, "+CIPDOMAIN:", &r)) {
        if (parse_ipv4(r, ev->text)) {
            ev->kind = WIFI_L_DOMAIN;
        }
    } else if (has_prefix(line, "+CIFSR:STAIP,", &r)) {
        if (parse_ipv4(r, ev->text)) {
            ev->kind = WIFI_L_STA_IP;
        }
    } else if (has_prefix(line, "+CWLAP:", &r)) {
        ev->kind = WIFI_L_CWLAP;       /* 中身は見ない(SSID を ev に入れない)。wifi_parse_cwlap が読む */
    } else if (has_prefix(line, "+CIPSTA:ip:", &r)) {
        if (parse_ipv4(r, ev->text)) {
            ev->kind = WIFI_L_STA_IP;
        }
    } else if (has_prefix(line, "+CIPSTA:gateway:", &r)) {
        if (parse_ipv4(r, ev->text)) {
            ev->kind = WIFI_L_STA_GW;
        }
    } else if (has_prefix(line, "+CIPSTA:netmask:", &r)) {
        if (parse_ipv4(r, ev->text)) {
            ev->kind = WIFI_L_STA_MASK;
        }
    } else if ((line[0] == 'A') && (line[1] == 'T') && ((line[2] == '\0') || (line[2] == '+') || (line[2] == 'E'))) {
        ev->kind = WIFI_L_ECHO;     /* AT / AT+... / ATE0。エコーバック */
    } else {
        /* OTHER */
    }
    if (ev->kind == WIFI_L_OTHER) {
        ev->text[0] = '\0';        /* 値の途中で失敗した時の書きかけを残さない */
    }
}

/* ================================================================ AT+CWLAP の1行・形 */

/* 文字列の読み取り位置。quoted = "..." の中(\x は x、" で終わり)。SSID は配列に写さず、この読み取りだけで比べる */
typedef struct {
    const char *p;
    bool        quoted;
} sit_t;

/* 次の1バイト(1〜255)。終わり(NUL・閉じ引用符・末尾の \)は -1 で、p は終わりの位置のまま */
static int sit_raw(sit_t *s)
{
    unsigned c = (unsigned)(unsigned char)*s->p;

    if (c == 0u) {
        return -1;
    }
    if (s->quoted) {
        if (c == (unsigned)'"') {
            return -1;
        }
        if (c == (unsigned)'\\') {
            s->p++;
            c = (unsigned)(unsigned char)*s->p;
            if (c == 0u) {
                return -1;
            }
        }
    }
    s->p++;
    return (int)c;
}

/* 正規化した次の1バイト: 英大文字 -> 小文字、空白(0x20)と全角空白(U+3000 = E3 80 80)は捨てる、全角英数記号(U+FF01〜FF5E = EF BC 81 〜 EF BD 9E)は半角に。終わりは -1 */
static int sit_norm(sit_t *s)
{
    int   c;
    int   b1;
    int   b2;
    sit_t t;

    for (;;) {
        c = sit_raw(s);
        if (c == 0x20) {
            continue;
        }
        if ((c == 0xE3) || (c == 0xEF)) {
            t  = *s;
            b1 = sit_raw(&t);
            b2 = sit_raw(&t);
            if ((c == 0xE3) && (b1 == 0x80) && (b2 == 0x80)) {
                *s = t;
                continue;
            }
            if ((c == 0xEF) && (((b1 == 0xBC) && (b2 >= 0x81) && (b2 <= 0xBF)) || ((b1 == 0xBD) && (b2 >= 0x80) && (b2 <= 0x9E)))) {
                *s = t;
                c  = ((b1 == 0xBC) ? 0x20 : 0x60) + (b2 - 0x80);
            }
        }
        if ((c >= (int)'A') && (c <= (int)'Z')) {
            c += 0x20;
        }
        return c;
    }
}

/* 0 = 違う、1 = exact(バイト一致)、2 = near(正規化したら一致。正規化後に 1 文字以上)。a は値渡し(読み取り位置のコピー) */
static uint8_t ssid_match(sit_t a, const char *mine)
{
    sit_t      x = a;
    sit_t      y;
    const char *m = mine;
    int        cx;
    int        cy;
    unsigned   n = 0u;

    for (;;) {
        cx = sit_raw(&x);
        cy = (int)(unsigned char)*m;
        if (cx < 0) {
            if (cy == 0) {
                return 1u;
            }
            break;
        }
        if (cx != cy) {
            break;
        }
        m++;
    }
    x = a;
    y.p      = mine;
    y.quoted = false;
    for (;;) {
        cx = sit_norm(&x);
        cy = sit_norm(&y);
        if (cx != cy) {
            return 0u;
        }
        if (cx < 0) {
            return (n > 0u) ? 2u : 0u;
        }
        n++;
    }
}

/* *s の 10 進数(max 以下)を読む */
static bool read_max(const char **s, uint32_t max, uint32_t *v)
{
    const char *e;

    if (!read_num(*s, v, &e) || (*v > max)) {
        return false;
    }
    *s = e;
    return true;
}

bool wifi_parse_cwlap(const char *line, const char *my_ssid, wifi_ap_t *ap)
{
    const char *r;
    uint32_t    ecn;
    uint32_t    rssi;
    uint32_t    ch;
    sit_t       a;
    sit_t       e;
    unsigned    n = 0u;

    ap->ecn   = 0u;
    ap->rssi  = 0;
    ap->ch    = 0u;
    ap->match = 0u;
    if ((line == NULL) || !has_prefix(line, "+CWLAP:", &r)) {
        return false;
    }
    if (*r == '(') {
        r++;
    }
    if (!read_max(&r, 99u, &ecn) || (r[0] != ',') || (r[1] != '"')) {
        return false;
    }
    a.p      = r + 2;
    a.quoted = true;
    e        = a;
    while (sit_raw(&e) >= 0) {
        n++;                                   /* SSID のバイト数(エスケープの \ は数えない) */
    }
    if ((n > 32u) || (*e.p != '"')) {
        return false;                          /* 長すぎる・閉じ引用符が無い */
    }
    r = e.p + 1;
    if ((r[0] != ',') || (r[1] != '-')) {
        return false;                          /* rssi は負 */
    }
    r += 2;
    if (!read_max(&r, 999u, &rssi) || (*r != ',')) {
        return false;
    }
    r++;
    if (*r == '"') {                           /* MAC(引用符は任意)。読み飛ばすだけ */
        r++;
        while ((*r != '"') && (*r != '\0')) {
            r++;
        }
        if (*r != '"') {
            return false;
        }
        r++;
    } else {
        while ((*r != ',') && (*r != '\0')) {
            r++;
        }
    }
    if (*r != ',') {
        return false;
    }
    r++;
    if (!read_max(&r, 999u, &ch)) {
        return false;
    }
    ap->ecn  = ecn;
    ap->rssi = -(int32_t)rssi;
    ap->ch   = ch;
    if ((my_ssid != NULL) && (my_ssid[0] != '\0')) {
        ap->match = ssid_match(a, my_ssid);
    }
    return true;
}

size_t wifi_shape(char *out, size_t size, const char *line)
{
    size_t   n = 0u;
    unsigned c;

    if ((out == NULL) || (size == 0u)) {
        return 0u;
    }
    out[0] = '\0';
    if (line == NULL) {
        return 0u;
    }
    while ((n < WIFI_VAL_MAX) && ((n + 1u) < size) && (line[n] != '\0')) {
        c = (unsigned)(unsigned char)line[n];
        if (((c >= (unsigned)'A') && (c <= (unsigned)'Z')) || ((c >= (unsigned)'a') && (c <= (unsigned)'z'))) {
            out[n] = 'a';
        } else if ((c >= (unsigned)'0') && (c <= (unsigned)'9')) {
            out[n] = '9';
        } else {
            out[n] = ((c >= 0x21u) && (c <= 0x7Eu)) ? (char)c : '_';
        }
        n++;
    }
    out[n] = '\0';
    return n;
}

/* ================================================================ 状態機械 */

static void log_out(wifi_core_t *c, size_t n)
{
    if (n > 0u) {
        c->ops->log(c->out, n);
    }
}

static void emit_step(wifi_core_t *c, wifi_res_t res, uint32_t ms, uint32_t code)
{
    if ((c->quiet != 0u) && ((res == WIFI_RES_START) || (res == WIFI_RES_OK))) {
        return;                                                 /* 製品: 失敗・時間切れ・skip だけ出す(design.md §5.9。行キューの drop を避ける) */
    }
    log_out(c, wifi_fmt_step(c->out, sizeof c->out, (wifi_step_t)c->step, res, c->try_n, ms, code));
}

static void emit_val(wifi_core_t *c, wifi_key_t key, const char *val)
{
    log_out(c, wifi_fmt_val(c->out, sizeof c->out, key, val));
}

/* 分類できなかった行の「形」(英数字を a / 9 にしたもの)。1 step に最大 3 行。行は c->line(呼び出し後に消される)。ev.text は作業用 */
static void emit_oth(wifi_core_t *c)
{
    if ((c->quiet != 0u) || (c->oth_n >= 3u)) {
        return;
    }
    c->oth_n++;
    if (wifi_shape(c->ev.text, sizeof c->ev.text, c->line) > 0u) {
        emit_val(c, WIFI_KEY_OTH, c->ev.text);
    }
    c->ev.text[0] = '\0';
}

static void sat_inc(uint16_t *v)
{
    if (*v < 999u) {
        (*v)++;
    }
}

/* scan 中の +CWLAP: 1 行。SSID は照合したら捨てる(集計は件数と自分の AP の ch / rssi / ecn だけ) */
static void scan_line(wifi_core_t *c)
{
    wifi_ap_t ap;
    bool      first;

    sat_inc(&c->scan_total);
    if (!wifi_parse_cwlap(c->line, c->ssid, &ap)) {
        sat_inc(&c->scan_bad);
        emit_oth(c);
        return;
    }
    if (ap.match == 1u) {
        first = (c->scan_found == 0u);
        sat_inc(&c->scan_found);
        if (first || (ap.rssi > (int32_t)c->ap_rssi)) {     /* 同じ SSID が複数見えたら rssi が最大の1件 */
            c->ap_ecn  = (uint8_t)ap.ecn;
            c->ap_rssi = (int16_t)ap.rssi;
            c->ap_ch   = (uint16_t)ap.ch;
        }
    } else if (ap.match == 2u) {
        sat_inc(&c->scan_near);
    } else {
        /* 他の AP: 数えるだけ(total) */
    }
}

/* 表示は 999 まで(sat_inc と同じ上限。コンパイラに桁数を示す) */
static unsigned long u3(uint16_t v)
{
    return (v > 999u) ? 999ul : (unsigned long)v;
}

/* scan の OK: [wifiv] key=scan と key=ap を出し、step=scan の code(0〜3)を返す */
static uint32_t scan_report(wifi_core_t *c)
{
    char *t = c->ev.text;

    (void)snprintf(t, sizeof c->ev.text, "total=%lu,found=%lu,near=%lu,bad=%lu", u3(c->scan_total),
                   u3(c->scan_found), u3(c->scan_near), u3(c->scan_bad));
    emit_val(c, WIFI_KEY_SCAN, t);
    if (c->scan_found > 0u) {
        (void)snprintf(t, sizeof c->ev.text, "ch=%lu,rssi=%ld,ecn=%lu", (unsigned long)c->ap_ch, (long)c->ap_rssi, (unsigned long)c->ap_ecn);
        emit_val(c, WIFI_KEY_AP, t);
    }
    t[0] = '\0';
    if (c->scan_found > 0u) {
        return 0u;
    }
    if (c->scan_near > 0u) {
        return 1u;
    }
    return (c->scan_total > 0u) ? 2u : 3u;
}

static void finish(wifi_core_t *c, bool pass, wifi_step_t stage)
{
    log_out(c, wifi_fmt_result(c->out, sizeof c->out, pass, stage, c->ping_ip, c->ping_dns));
    c->phase     = PH_IDLE;
    c->step      = (uint8_t)WIFI_STEP_NONE;
    c->done      = 1u;
    c->ipd_left  = 0u;
    c->res_pass  = pass ? 1u : 0u;
    c->res_stage = (uint8_t)stage;
    c->done_seq++;
}

/* 取得の結果を公開する(本文は消す。残すのは wx の数値だけ) */
static void wx_publish(wifi_core_t *c, uint32_t now)
{
    c->wx.ms       = now - c->wx_t0;
    c->wx.first_ms = c->wx_first_ms;
    c->wx.sym      = c->sym;
    memset(c->wx_buf, 0, sizeof c->wx_buf);
    c->wx_len   = 0u;
    c->ipd_left = 0u;
    c->wx_seq++;
}

/* 解析まで届かなかった失敗(TCP が張れない = 1、送信できない = 2) */
static void wx_fail(wifi_core_t *c, uint32_t now, uint8_t err)
{
    memset(&c->wx, 0, sizeof c->wx);
    c->wx.err  = err;
    wx_publish(c, now);
}

/* 取得中に ready を受けた = ESP32 が再起動した(SSL のメモリ不足の疑い)。err=9 で打ち切る(design.md §5.5.2 の 0-4) */
static void wx_reboot(wifi_core_t *c, uint32_t now)
{
    const wifi_step_t st = (wifi_step_t)c->step;

    emit_step(c, WIFI_RES_FAIL, now - c->t0, 9u);
    memset(&c->wx, 0, sizeof c->wx);
    c->wx.err  = 9u;
    c->wx.len  = c->wx_len;
    wx_publish(c, now);
    finish(c, false, st);
}

static void begin_try(wifi_core_t *c, uint32_t now);
static void recv_done(wifi_core_t *c, uint32_t now, int how);

static void next_step(wifi_core_t *c, uint32_t now)
{
    unsigned s = (unsigned)c->step + 1u;

    while ((s < (unsigned)WIFI_STEP_END) && (((c->mask >> s) & 1u) == 0u)) {
        s++;
    }
    if (s >= (unsigned)WIFI_STEP_END) {
        finish(c, true, WIFI_STEP_NONE);
        return;
    }
    c->step  = (uint8_t)s;
    c->try_n = 1u;
    if ((c->step == (uint8_t)WIFI_STEP_JOIN) && ((c->ssid == NULL) || (c->pass == NULL))) {
        emit_step(c, WIFI_RES_SKIP, 0u, 0u);     /* wifi_secret.h が無い: join 以降は飛ばす */
        finish(c, true, WIFI_STEP_NONE);
        return;
    }
    begin_try(c, now);
}

static void end_ok(wifi_core_t *c, uint32_t now, uint32_t code)
{
    emit_step(c, WIFI_RES_OK, now - c->t0, code);
    if (c->step == (uint8_t)WIFI_STEP_PING_IP) {
        c->ping_ip = c->ping_ms;
    } else if (c->step == (uint8_t)WIFI_STEP_PING_DNS) {
        c->ping_dns = c->ping_ms;
    } else {
        /* 他の step は値なし */
    }
    next_step(c, now);
}

static uint32_t step_timeout(const wifi_core_t *c)
{
    if ((c->step == (uint8_t)WIFI_STEP_WX_TCP) && wx_target_ssl((wx_target_t)c->target)) {
        return SSL_TCP_TIMEOUT_MS;
    }
    return (uint32_t)k_def[c->step].timeout_ms;
}

static bool can_retry(const wifi_core_t *c)
{
    if (c->try_n >= k_def[c->step].max_try) {
        return false;
    }
    if (c->step == (uint8_t)WIFI_STEP_JOIN) {
        return (c->fail_code == 1u) || (c->fail_code == 4u);     /* タイムアウト・接続失敗の時だけ。2 = パスワード違いはやり直さない */
    }
    return true;
}

static void end_fail(wifi_core_t *c, uint32_t now, wifi_res_t res, uint32_t code)
{
    emit_step(c, res, now - c->t0, code);
    if (can_retry(c)) {
        c->after = AFTER_RETRY;
    } else if (k_def[c->step].cont != 0u) {
        c->after = AFTER_NEXT;
    } else {
        if (c->step == (uint8_t)WIFI_STEP_WX_TCP) {
            wx_fail(c, now, 1u);
        } else if (c->step == (uint8_t)WIFI_STEP_WX_SEND) {
            wx_fail(c, now, 2u);
        } else {
            /* 取得の結果は出さない */
        }
        finish(c, false, (wifi_step_t)c->step);
        return;
    }
    /* 遅れて来る応答(前のコマンドの OK)を次の step の成功に数えないよう、しばらく行を捨てる */
    c->t0       = now;
    c->quiet_ms = (uint16_t)((res == WIFI_RES_TIMEOUT) ? QUIET_TIMEOUT_MS : QUIET_FAIL_MS);
    c->phase    = PH_QUIET;
}

static void begin_try(wifi_core_t *c, uint32_t now)
{
    size_t n;

    c->t0         = now;
    c->fail_code  = 0u;
    c->ping_seen  = 0u;
    c->oth_n      = 0u;
    c->scan_total = 0u;
    c->scan_found = 0u;
    c->scan_near  = 0u;
    c->scan_bad   = 0u;
    c->ap_ch      = 0u;
    c->ap_rssi    = 0;
    c->ap_ecn     = 0u;
    c->wx_flags  &= F_STEP_KEEP;
    if (c->step == (uint8_t)WIFI_STEP_WX_MUX) {                 /* 取得の頭 */
        c->wx_flags    = 0u;
        c->wx_len      = 0u;
        c->ipd_left    = 0u;
        c->wx_first_ms = 0u;
        c->wx_t0       = now;
    } else if (c->step == (uint8_t)WIFI_STEP_WX_TCP) {
        c->fail_code = 1u;                                      /* err=1: TCP の接続に失敗 */
    } else if (c->step == (uint8_t)WIFI_STEP_WX_SEND) {
        c->fail_code = 2u;                                      /* err=2: 送信に失敗 */
    } else {
        /* 他の step は何もしない */
    }
    emit_step(c, WIFI_RES_START, 0u, 0u);
    if (c->step == (uint8_t)WIFI_STEP_RESET) {
        c->ops->en(0);
        c->phase = PH_EN_LOW;
        return;
    }
    if (c->step == (uint8_t)WIFI_STEP_BOOT) {
        c->phase = PH_WAIT;
        return;
    }
    if (c->step == (uint8_t)WIFI_STEP_WX_RECV) {                /* 送るものは無い。CLOSED を待つ(送信中に来ていたら、すぐ終わり) */
        c->phase = PH_WAIT;
        if ((c->wx_flags & F_CLOSED) != 0u) {
            recv_done(c, now, RECV_CLOSED);
        }
        return;
    }
    if (c->step == (uint8_t)WIFI_STEP_WX_SEND) {                /* AT+CIPSEND=<要求の長さ> */
        n = wx_build_request_t(c->cmd, sizeof c->cmd, (wx_target_t)c->target, c->sym);
        if (n == 0u) {
            end_fail(c, now, WIFI_RES_FAIL, 100u);
            return;
        }
        n = (size_t)snprintf(c->cmd, sizeof c->cmd, "AT+CIPSEND=%lu\r\n", (unsigned long)n);
        c->cmdlen = (uint16_t)n;
        c->phase  = PH_SEND;
        return;
    }
    if (c->step == (uint8_t)WIFI_STEP_WX_TCP) {                 /* AT+CIPSTART="TCP"|"SSL",<host>,<port> */
        n = wx_build_cipstart(c->cmd, sizeof c->cmd, (wx_target_t)c->target);
        if (n == 0u) {
            end_fail(c, now, WIFI_RES_FAIL, 100u);
            return;
        }
        c->cmdlen = (uint16_t)n;
        c->phase  = PH_SEND;
        return;
    }
    if (c->step == (uint8_t)WIFI_STEP_JOIN) {
        n = wifi_build_cwjap(c->cmd, WIFI_CMD_MAX - 2u, c->ssid, c->pass);
        if (n == 0u) {
            end_fail(c, now, WIFI_RES_FAIL, 100u);      /* 組み立てられない(長すぎる等)。送らない */
            return;
        }
        c->cmd[n]      = '\r';
        c->cmd[n + 1u] = '\n';
        c->cmdlen      = (uint16_t)(n + 2u);
    } else {
        n = strlen(k_def[c->step].cmd);
        memcpy(c->cmd, k_def[c->step].cmd, n);
        c->cmdlen = (uint16_t)n;
    }
    c->phase = PH_SEND;
}

void wifi_core_init(wifi_core_t *c, const wifi_ops_t *ops, const char *ssid, const char *pass)
{
    memset(c, 0, sizeof *c);
    c->ops  = ops;
    c->ssid = ssid;
    c->pass = pass;
}

void wifi_core_set_quiet(wifi_core_t *c, bool quiet)
{
    c->quiet = quiet ? 1u : 0u;
}

void wifi_core_set_target(wifi_core_t *c, wx_target_t t)
{
    if ((c->phase == PH_IDLE) && ((unsigned)t < (unsigned)WX_TGT_END)) {
        c->target = (uint8_t)t;
    }
}

void wifi_core_set_symbol(wifi_core_t *c, uint8_t sym)
{
    if ((c->phase == PH_IDLE) && (sym < (uint8_t)WX_NSYM)) {
        c->sym = sym;
    }
}

bool wifi_core_start(wifi_core_t *c, wifi_range_t range, uint32_t now_ms)
{
    if (c->phase != PH_IDLE) {
        return false;
    }
    switch (range) {
    case WIFI_RANGE_ALL:     c->mask = MASK_ALL;     c->step = (uint8_t)WIFI_STEP_RESET;  break;
    case WIFI_RANGE_BOOT:    c->mask = MASK_BOOT;    c->step = (uint8_t)WIFI_STEP_RESET;  break;
    case WIFI_RANGE_AT:      c->mask = MASK_AT;      c->step = (uint8_t)WIFI_STEP_AT;     break;
    case WIFI_RANGE_CONNECT: c->mask = MASK_CONNECT; c->step = (uint8_t)WIFI_STEP_RESET;  break;
    case WIFI_RANGE_WX:
#if APP_WIFI_TEST
        c->mask = (c->target == (uint8_t)WX_TGT_YAHOO_SSL_NOAUTH) ? MASK_WX_SSLCONF : MASK_WX;
#else
        c->mask = MASK_WX;
#endif
        c->step = (uint8_t)WIFI_STEP_WX_MUX;
        break;
    default:                 return false;
    }
    c->ping_ip  = 0u;
    c->ping_dns = 0u;
    c->llen     = 0u;
    c->lbad     = 0u;
    c->ipd_left = 0u;
    c->try_n    = 1u;
    begin_try(c, now_ms);
    return true;
}

/* wx_recv の終わり(CLOSED を受けた・時間切れ・ERROR)。応答を解析して、成功なら次へ、失敗なら打ち切り。結果は wx に残す(本文は消す) */
static void recv_done(wifi_core_t *c, uint32_t now, int how)
{
    wx_result_t *r  = &c->wx;
    bool         ok = false;

    if ((c->wx_flags & F_OVER) != 0u) {
        (void)wx_parse_http(c->wx_buf, c->wx_len, r);        /* http と Date: だけ拾う。溢れた応答は、キーがそろっていても成功にしない(design.md §5.2 段 8) */
        r->ok  = 0u;
        r->err = 7u;
    } else if (c->wx_len == 0u) {
        memset(r, 0, sizeof *r);
        r->err = 3u;
    } else {
        ok = wx_parse_http(c->wx_buf, c->wx_len, r);
    }
    r->closed = (how == RECV_CLOSED) ? 1u : 0u;
    r->len    = c->wx_len;
    wx_publish(c, now);
    if (ok) {
        emit_step(c, WIFI_RES_OK, now - c->t0, 0u);
        next_step(c, now);
    } else {
        emit_step(c, (how == RECV_TIMEOUT) ? WIFI_RES_TIMEOUT : WIFI_RES_FAIL, now - c->t0, r->err);
        finish(c, false, WIFI_STEP_WX_RECV);
    }
}

/* wx_send の > を受けた後の要求本体を送る準備(c->cmd に要求を入れて PH_SEND_DATA へ) */
static void start_request(wifi_core_t *c, uint32_t now)
{
    size_t n = wx_build_request_t(c->cmd, sizeof c->cmd, (wx_target_t)c->target, c->sym);

    c->wx_flags |= F_REQ;
    if (n == 0u) {
        end_fail(c, now, WIFI_RES_FAIL, 100u);
        return;
    }
    c->cmdlen = (uint16_t)n;
    c->phase  = PH_SEND_DATA;
}

/* 取得の step(wx_mux 〜 wx_recv)が受けた 1 行。e は分類済み */
static void wx_line(wifi_core_t *c, uint32_t now)
{
    const wifi_line_t k = c->ev.kind;
    const bool        bad = (k == WIFI_L_ERROR) || (k == WIFI_L_FAIL);

    switch ((wifi_step_t)c->step) {
    case WIFI_STEP_WX_MUX:
        if (k == WIFI_L_OK) {
            end_ok(c, now, 0u);
        } else if (bad) {
            end_fail(c, now, WIFI_RES_FAIL, 0u);
        } else if (k == WIFI_L_OTHER) {
            emit_oth(c);
        } else {
            /* 無視 */
        }
        break;
    case WIFI_STEP_WX_CLOSE:                       /* 前回の接続が無ければ ERROR。どちらでも成功 */
    case WIFI_STEP_WX_SSLCONF:                     /* 認証なしの設定(z)。OK でも ERROR でも先へ */
        if ((k == WIFI_L_OK) || bad) {
            end_ok(c, now, 0u);
        } else if (k == WIFI_L_OTHER) {
            emit_oth(c);
        } else {
            /* 無視(CLOSED など) */
        }
        break;
    case WIFI_STEP_WX_TCP:
        if (k == WIFI_L_CONNECT) {
            c->wx_flags |= F_CONN;
        } else if (k == WIFI_L_OK) {
            c->wx_flags |= F_OK;
        } else if (bad || (k == WIFI_L_CLOSED) || (k == WIFI_L_DNS_FAIL)) {
            end_fail(c, now, WIFI_RES_FAIL, c->fail_code);
            return;
        } else if (k == WIFI_L_READY) {
            wx_reboot(c, now);
            return;
        } else if (k == WIFI_L_OTHER) {
            emit_oth(c);
        } else {
            /* 無視 */
        }
        if ((c->wx_flags & (F_OK | F_CONN)) == (F_OK | F_CONN)) {
            end_ok(c, now, 0u);
        }
        break;
    case WIFI_STEP_WX_SEND:
        if (k == WIFI_L_OK) {
            c->wx_flags |= F_OK;
        } else if (k == WIFI_L_PROMPT) {
            c->wx_flags |= F_PROMPT;
        } else if (bad || (k == WIFI_L_SEND_FAIL)) {
            end_fail(c, now, WIFI_RES_FAIL, c->fail_code);
            return;
        } else if (k == WIFI_L_READY) {
            wx_reboot(c, now);
            return;
        } else if (k == WIFI_L_CLOSED) {
            if ((c->wx_flags & F_SENT) == 0u) {    /* 要求を送る前に閉じられた = 失敗。送った後なら、フラグだけ(wx_recv が終わりにする) */
                end_fail(c, now, WIFI_RES_FAIL, c->fail_code);
                return;
            }
            c->wx_flags |= F_CLOSED;
        } else if ((k == WIFI_L_SEND_OK) && ((c->wx_flags & F_SENT) != 0u)) {
            end_ok(c, now, 0u);
            return;
        } else if (k == WIFI_L_OTHER) {
            emit_oth(c);
        } else {
            /* 無視 */
        }
        if (((c->wx_flags & (F_OK | F_PROMPT | F_REQ)) == (F_OK | F_PROMPT)) && (c->phase == PH_WAIT)) {
            start_request(c, now);
        }
        break;
    case WIFI_STEP_WX_RECV:
        if (k == WIFI_L_CLOSED) {
            recv_done(c, now, RECV_CLOSED);
        } else if (bad) {
            recv_done(c, now, RECV_ERROR);
        } else if (k == WIFI_L_READY) {
            wx_reboot(c, now);
        } else if (k == WIFI_L_OTHER) {
            emit_oth(c);
        } else {
            /* 無視(OK、SEND OK の遅れなど) */
        }
        break;
    default:
        break;
    }
}

/* 待ち中の step が受けた1行を処理する。c->line は NUL 終端 */
static void on_line(wifi_core_t *c, uint32_t now)
{
    wifi_ev_t *e = &c->ev;

    if (c->phase != PH_WAIT) {
        return;
    }
    wifi_parse_line(c->line, e);
    if (c->step >= (uint8_t)WIFI_STEP_WX_MUX) {
        wx_line(c, now);
        return;
    }
    if (c->step == (uint8_t)WIFI_STEP_BOOT) {
        if (e->kind == WIFI_L_READY) {
            end_ok(c, now, 0u);
        }
        return;
    }
    switch (e->kind) {
    case WIFI_L_OK:
        /* ping は +PING:<n> を受けていなければ成功にしない。join は WIFI GOT IP を条件にしない(既に接続済みだと出ないことがあるため。次の ip で確かめる) */
        if (((c->step == (uint8_t)WIFI_STEP_PING_IP) || (c->step == (uint8_t)WIFI_STEP_PING_DNS)) && (c->ping_seen == 0u)) {
            end_fail(c, now, WIFI_RES_FAIL, c->fail_code);
        } else if (c->step == (uint8_t)WIFI_STEP_SCAN) {
            end_ok(c, now, scan_report(c));
        } else {
            end_ok(c, now, 0u);
        }
        break;
    case WIFI_L_ERROR:
    case WIFI_L_FAIL:
        if (((c->step == (uint8_t)WIFI_STEP_PING_IP) || (c->step == (uint8_t)WIFI_STEP_PING_DNS)) && (c->ping_seen == 0u) && (c->fail_code == 0u)) {
            c->fail_code = 200u;                    /* +PING: が1行も来ずに ERROR = AT+PING 未対応の疑い */
        }
        end_fail(c, now, WIFI_RES_FAIL, c->fail_code);
        break;
    case WIFI_L_CWJAP:
        if (c->step == (uint8_t)WIFI_STEP_JOIN) {
            c->fail_code = e->num;
        }
        break;
    case WIFI_L_PING_TIMEOUT:
        if ((c->step == (uint8_t)WIFI_STEP_PING_IP) || (c->step == (uint8_t)WIFI_STEP_PING_DNS)) {
            c->fail_code = 201u;                    /* 対応しているが届かない(次に ERROR が来る) */
        }
        break;
    case WIFI_L_CWLAP:
        if (c->step == (uint8_t)WIFI_STEP_SCAN) {
            scan_line(c);
        }
        break;
    case WIFI_L_PING:
        if ((c->step == (uint8_t)WIFI_STEP_PING_IP) || (c->step == (uint8_t)WIFI_STEP_PING_DNS)) {
            char num[12];

            c->ping_seen = 1u;
            c->ping_ms   = e->num;
            (void)snprintf(num, sizeof num, "%lu", (unsigned long)e->num);
            emit_val(c, WIFI_KEY_PING, num);
        }
        break;
    case WIFI_L_AT_VER:
        if (c->step == (uint8_t)WIFI_STEP_GMR) {
            emit_val(c, WIFI_KEY_VER, e->text);
        }
        break;
    case WIFI_L_SDK_VER:
        if (c->step == (uint8_t)WIFI_STEP_GMR) {
            emit_val(c, WIFI_KEY_SDK, e->text);
        }
        break;
    case WIFI_L_STA_IP:
        if (c->step == (uint8_t)WIFI_STEP_IP) {
            emit_val(c, WIFI_KEY_IP, e->text);
        }
        break;
    case WIFI_L_STA_GW:
        if (c->step == (uint8_t)WIFI_STEP_IP) {
            emit_val(c, WIFI_KEY_GW, e->text);
        }
        break;
    case WIFI_L_STA_MASK:
        if (c->step == (uint8_t)WIFI_STEP_IP) {
            emit_val(c, WIFI_KEY_MASK, e->text);
        }
        break;
    case WIFI_L_DOMAIN:
        if (c->step == (uint8_t)WIFI_STEP_DNS) {
            emit_val(c, WIFI_KEY_DNS, e->text);
        }
        break;
    case WIFI_L_OTHER:                              /* 分類できなかった行は「形」だけ記録(最大 3 行 / step) */
        emit_oth(c);
        break;
    default:       /* ECHO、WIFI CONNECTED / GOT IP / DISCONNECT、ready、その他の分類済みの行は無視 */
        break;
    }
}

/* 受信データ(+IPD の中身)を取り込める時 = wx_send / wx_recv の応答待ち */
static bool wx_rx_active(const wifi_core_t *c)
{
    return (c->phase == PH_WAIT) && ((c->step == (uint8_t)WIFI_STEP_WX_SEND) || (c->step == (uint8_t)WIFI_STEP_WX_RECV));
}

/* データモードの 1 バイト。行として扱わない(本文の OK / CLOSED / +IPD を解釈しない)。バッファを超えた分は捨てて、超過の印だけ残す */
static void ipd_byte(wifi_core_t *c, uint8_t b, uint32_t now_ms)
{
    c->ipd_left--;
    if (!wx_rx_active(c)) {
        return;
    }
    if (c->wx_len >= WX_BUF_SIZE) {
        c->wx_flags |= F_OVER;
        return;
    }
    if ((c->wx_flags & F_FIRST) == 0u) {
        c->wx_flags   |= F_FIRST;
        c->wx_first_ms = now_ms;
    }
    c->wx_buf[c->wx_len] = (char)b;
    c->wx_len++;
}

/* 行が "+IPD,<n>[,<ip>,<port>]:" の形で、今 ':' を受けた。n バイトのデータモードへ。形が違えば、その行の残りを捨てる */
static void ipd_start(wifi_core_t *c)
{
    const char *p = &c->line[5];
    uint32_t    n = 0u;
    unsigned    d = 0u;

    while ((*p >= '0') && (*p <= '9') && (d < 5u)) {
        n = (n * 10u) + (uint32_t)(*p - '0');
        d++;
        p++;
    }
    if ((d > 0u) && (n >= 1u) && (n <= IPD_MAX) && ((*p == ':') || ((*p == ',') && (c->llen <= (5u + d + 1u + 40u))))) {
        c->ipd_left = (uint16_t)n;
    } else {
        c->lbad = 1u;
        c->bad_lines++;
    }
    memset(c->line, 0, sizeof c->line);
    c->llen = 0u;
}

void wifi_core_rx(wifi_core_t *c, uint8_t b, uint32_t now_ms)
{
    c->rx_bytes++;
    if (c->ipd_left > 0u) {
        ipd_byte(c, b, now_ms);
        return;
    }
    if ((b == (uint8_t)'\r') || (b == (uint8_t)'\n')) {
        if (c->lbad != 0u) {
            c->lbad = 0u;
        } else if (c->llen > 0u) {
            c->line[c->llen] = '\0';
            on_line(c, now_ms);
        } else {
            /* 空行は捨てる */
        }
        memset(c->line, 0, sizeof c->line);      /* エコーバックのコマンド行(AT+CWJAP="...")を残さない */
        c->llen = 0u;
        return;
    }
    if (c->lbad != 0u) {
        return;
    }
    if ((b == 0u) || (c->llen >= WIFI_LINE_MAX)) {
        c->lbad = 1u;                            /* 長すぎる行・NUL を含む行は、終わりまで丸ごと捨てる */
        c->bad_lines++;
        memset(c->line, 0, sizeof c->line);
        c->llen = 0u;
        return;
    }
    c->line[c->llen] = (char)b;
    c->llen++;
    if (!wx_rx_active(c)) {
        return;
    }
    if ((b == (uint8_t)'>') && (c->llen == 1u) && (c->step == (uint8_t)WIFI_STEP_WX_SEND)) {
        c->line[1] = '\0';
        on_line(c, now_ms);                      /* プロンプトは改行で終わらない。行の先頭の > だけで 1 行 */
        memset(c->line, 0, sizeof c->line);
        c->llen = 0u;
    } else if ((b == (uint8_t)':') && (c->llen >= 6u) && (memcmp(c->line, "+IPD,", 5u) == 0)) {
        ipd_start(c);
    } else {
        /* 行モードのまま */
    }
}

void wifi_core_poll(wifi_core_t *c, uint32_t now_ms)
{
    switch (c->phase) {
    case PH_EN_LOW:
        if ((uint32_t)(now_ms - c->t0) >= EN_LOW_MS) {
            c->ops->en(1);
            end_ok(c, now_ms, 0u);
        }
        break;
    case PH_SEND:
        switch (c->ops->send(c->cmd, c->cmdlen)) {
        case WIFI_SEND_OK:
            memset(c->cmd, 0, sizeof c->cmd);      /* 送ったら消す(join のコマンドにパスワードが入っている) */
            c->t0    = now_ms;                     /* 応答の待ち時間は、送れた時から数える */
            c->phase = PH_WAIT;
            break;
        case WIFI_SEND_BUSY:
            break;
        default:
            memset(c->cmd, 0, sizeof c->cmd);
            end_fail(c, now_ms, WIFI_RES_FAIL, 101u);
            break;
        }
        break;
    case PH_SEND_DATA:                              /* wx_send の要求本体。BUSY なら同じものを次の周回で渡す */
        switch (c->ops->send(c->cmd, c->cmdlen)) {
        case WIFI_SEND_OK:
            memset(c->cmd, 0, sizeof c->cmd);
            c->wx_flags |= F_SENT;
            c->t0        = now_ms;
            c->phase     = PH_WAIT;
            break;
        case WIFI_SEND_BUSY:
            break;
        default:
            memset(c->cmd, 0, sizeof c->cmd);
            end_fail(c, now_ms, WIFI_RES_FAIL, 101u);
            break;
        }
        break;
    case PH_WAIT:
        if ((uint32_t)(now_ms - c->t0) >= step_timeout(c)) {
            if (c->step == (uint8_t)WIFI_STEP_WX_RECV) {
                recv_done(c, now_ms, RECV_TIMEOUT);       /* CLOSED が来なくても、解析できていれば成功 */
            } else {
                end_fail(c, now_ms, WIFI_RES_TIMEOUT, c->fail_code);
            }
        }
        break;
    case PH_QUIET:
        if ((uint32_t)(now_ms - c->t0) >= (uint32_t)c->quiet_ms) {
            if (c->after == AFTER_RETRY) {
                c->try_n++;
                begin_try(c, now_ms);
            } else {
                next_step(c, now_ms);
            }
        }
        break;
    default:
        break;
    }
}

wifi_state_t wifi_core_state(const wifi_core_t *c)
{
    if (c->phase != PH_IDLE) {
        return WIFI_STATE_RUN;
    }
    return (c->done != 0u) ? WIFI_STATE_DONE : WIFI_STATE_IDLE;
}

wifi_step_t wifi_core_step(const wifi_core_t *c)
{
    return (wifi_step_t)c->step;
}

uint32_t wifi_core_bytes(const wifi_core_t *c)
{
    return c->rx_bytes;
}

uint32_t wifi_core_wx_seq(const wifi_core_t *c)
{
    return c->wx_seq;
}

const wx_result_t *wifi_core_wx(const wifi_core_t *c)
{
    return &c->wx;
}

uint32_t wifi_core_done_seq(const wifi_core_t *c)
{
    return c->done_seq;
}

bool wifi_core_pass(const wifi_core_t *c)
{
    return c->res_pass != 0u;
}

wifi_step_t wifi_core_fail_stage(const wifi_core_t *c)
{
    return (wifi_step_t)c->res_stage;
}

#endif /* APP_WIFI_ON */
