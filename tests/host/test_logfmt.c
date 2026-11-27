/* test_logfmt.c - src/app/app_logfmt.c の単体テスト(UARTログ: 行の組み立て・コマンド解釈・周期判定)。標準Cのみ(正規表現の検査は自前のテンプレート)。
 * 基準: docs/design-uart.md(最優先)。設計書 §5.4 の U-H1〜U-H6 と、§6.4 の正規表現(PC側の合否判定に使う)に対する適合検査。
 * 設計書から外れた実装が見つかった場合は CHECK が失敗する(= 不合格)。
 * 作成: 2026-11-20 テスト担当 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "app_logfmt.h"
#include "test_framework.h"

#define GUARD  (16u)
#define POISON (0xA5)

/* ---- 行の共通検査: ASCII 0x20-0x7E と、末尾の \r\n だけ(設計 §2.1) ---- */
static int line_ascii_ok(const char *s, size_t n)
{
    size_t i;
    if ((n < 2u) || (s[n - 2u] != '\r') || (s[n - 1u] != '\n') || (s[n] != '\0')) { return 0; }
    for (i = 0u; i < n - 2u; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c < 0x20u) || (c > 0x7Eu)) { return 0; }
    }
    return 1;
}

/* ---- PC側の判定(design-uart.md §6.4 / §10.9 の正規表現)に相当する簡易テンプレート。この環境には <regex.h> が無いため自前。
 *   '#' = 数字1文字以上、'@' = 空白以外1文字以上、それ以外は一字一句一致。全体(
 を除く)に一致する必要がある。
 *   RX_SCAN だけは「 a<d>=0x<hex2>」の0回以上の繰り返しを許すので専用の関数で検査する。 */
static const char *RX_BOOT = "[boot] fw=rx72n_gcc build=@ ch=2 baud=115200";
static const char *RX_INIT = "[init] stage=@ ok=# err=# detail=#";
static const char *RX_HB   = "[hb] tick=# scr=# demo=# frm=# uf=# err=# detail=# pts=# x=# y=# i2c_fail=# streak=# found=# drop=#";
static const char *RX_ST   = "[st] tick=# scr=# demo=# frm=# uf=# err=# detail=# pts=# x=# y=# i2c_fail=# streak=# found=# drop=# xmin=# xmax=# ymin=# ymax=#";
static const char *RX_EV   = "[ev] btn=tap src=@";
static const char *RX_ERR  = "[err] code=# detail=#";
static const char *RX_HELP = "[help] s=state x=update ?=help";
static const char *RX_SCAN = "[scan] n=#";                      /* + 繰り返し部(専用検査) */
static const char *RX_PERF = "[perf] n=# copy_us=# home_us=# slide_us=#";

static int tmpl(const char *t, const char *s)
{
    while (*t != '\0') {
        if (*t == '#') {
            if (!((*s >= '0') && (*s <= '9'))) { return 0; }
            while ((*s >= '0') && (*s <= '9')) { s++; }
            t++;
        } else if (*t == '@') {
            if ((*s == '\0') || (*s == ' ')) { return 0; }
            while ((*s != '\0') && (*s != ' ')) { s++; }
            t++;
        } else {
            if (*t != *s) { return 0; }
            t++; s++;
        }
    }
    return *s == '\0';
}

static int rx_match(const char *pat, const char *line, size_t n)
{
    char tmp[512];
    if (n < 2u || n >= sizeof(tmp)) { return 0; }
    memcpy(tmp, line, n - 2u);            /* 
 を除く */
    tmp[n - 2u] = '\0';
    if (pat == RX_SCAN) {
        const char *p = tmp + strlen("[scan] n=");
        size_t k;
        if (strncmp(tmp, "[scan] n=", 9) != 0) { return 0; }
        if (!((*p >= '0') && (*p <= '9'))) { return 0; }
        while ((*p >= '0') && (*p <= '9')) { p++; }
        while (*p != '\0') {            /* " a<d>=0x<hex><hex>" */
            if (!((p[0] == ' ') && (p[1] == 'a') && (p[2] >= '0') && (p[2] <= '9') && (p[3] == '=') && (p[4] == '0') && (p[5] == 'x'))) { return 0; }
            for (k = 6u; k < 8u; k++) { if (!(((p[k] >= '0') && (p[k] <= '9')) || ((p[k] >= 'a') && (p[k] <= 'f')))) { return 0; } }
            p += 8;
        }
        return 1;
    }
    return tmpl(pat, tmp);
}

/* ---- 呼び出しの共通化 ---- */
typedef struct { const char *tag; const applog_state_t *s; bool range; } st_ctx_t;
typedef struct { const char *src; } ev_ctx_t;
typedef struct { const char *build; uint8_t ch; uint32_t baud; } boot_ctx_t;
typedef struct { const char *stage; bool ok; uint32_t err, detail; } init_ctx_t;
typedef struct { uint8_t count; const uint8_t *addr; } scan_ctx_t;
typedef struct { uint32_t code, detail; } err_ctx_t;
typedef size_t (*fmt_fn)(char *buf, size_t size, const void *ctx);

static size_t w_boot(char *b, size_t n, const void *c) { const boot_ctx_t *x = (const boot_ctx_t *)c; return applog_fmt_boot(b, n, x->build, x->ch, x->baud); }
static size_t w_init(char *b, size_t n, const void *c) { const init_ctx_t *x = (const init_ctx_t *)c; return applog_fmt_init(b, n, x->stage, x->ok, x->err, x->detail); }
static size_t w_state(char *b, size_t n, const void *c) { const st_ctx_t *x = (const st_ctx_t *)c; return applog_fmt_state(b, n, x->tag, x->s, x->range); }
static size_t w_scan(char *b, size_t n, const void *c) { const scan_ctx_t *x = (const scan_ctx_t *)c; return applog_fmt_scan(b, n, x->count, x->addr); }
static size_t w_ev(char *b, size_t n, const void *c) { const ev_ctx_t *x = (const ev_ctx_t *)c; return applog_fmt_ev_tap(b, n, x->src); }
static size_t w_err(char *b, size_t n, const void *c) { const err_ctx_t *x = (const err_ctx_t *)c; return applog_fmt_err(b, n, x->code, x->detail); }
static size_t w_help(char *b, size_t n, const void *c) { (void)c; return applog_fmt_help(b, n); }
typedef struct { uint32_t n, a, b, c; } perf_t;
static size_t w_perf(char *b, size_t n, const void *c) { const perf_t *x = (const perf_t *)c; return applog_fmt_perf(b, n, x->n, x->a, x->b, x->c); }

/* size = 0 .. 必要長+4 の全部で呼び、戻り値・内容・範囲外への書込み(番兵)・失敗時の buf[0] を検査する(U-H1, U-H3) */
static void sweep(const char *name, fmt_fn fn, const void *ctx, const char *expected, const char *rx)
{
    size_t   need = strlen(expected);
    size_t   size;
    unsigned fail_before = (unsigned)g_tests_failed;

    CHECK(line_ascii_ok(expected, need));          /* 期待値そのものが「ASCII + 末尾 \r\n」(試験の自己点検) */
    for (size = 0u; size <= need + 4u; size++) {
        unsigned char *m = (unsigned char *)malloc(size + GUARD);   /* ヒープ: ASan が範囲外を検出 */
        size_t ret, i;
        int guard_ok = 1;

        if (m == NULL) { CHECK(0); return; }
        memset(m, POISON, size + GUARD);
        ret = fn((char *)m, size, ctx);
        for (i = size; i < size + GUARD; i++) { if (m[i] != POISON) { guard_ok = 0; } }
        if (!guard_ok) { printf("  (%s size=%zu: バッファの外が書き換えられた)\n", name, size); }
        CHECK(guard_ok);
        if (size >= need + 1u) {
            CHECK_EQ(ret, need);
            CHECK(memcmp(m, expected, need) == 0);
            CHECK(m[need] == 0);
        } else {
            CHECK_EQ(ret, 0);                       /* バッファ不足は 0 */
            if (size > 0u) {
                CHECK(m[0] == 0);                   /* 設計 §5.1: 失敗時は buf[0]='\0'(部分行を残さない) */
            } else {
                CHECK(m[0] == POISON);              /* size==0 では何も書かない */
            }
        }
        free(m);
    }
    if (rx != NULL) { CHECK(rx_match(rx, expected, need)); }
    if ((unsigned)g_tests_failed != fail_before) { printf("  ^ 上記は %s\n", name); }
}

static applog_state_t st_make(uint32_t tick, uint8_t scr, uint32_t ec, uint32_t ed, uint32_t i2c, uint8_t streak,
                              uint32_t drop, uint16_t x, uint16_t y, uint8_t pts, uint8_t found)
{
    applog_state_t s;
    memset(&s, 0, sizeof(s));
    s.tick = tick; s.scr = scr; s.err_code = ec; s.err_detail = ed; s.i2c_fail_total = i2c;
    s.i2c_fail_streak = streak; s.drop = drop;
    s.raw_x = x; s.raw_y = y; s.raw_points = pts; s.found_count = found;
    s.x_min = 0xFFFFu; s.x_max = 0u; s.y_min = 0xFFFFu; s.y_max = 0u;
    return s;
}

static char g_one[APPLOG_LINE_MAX];

int main(void)
{
    /* ===== U-H1: 各行の正確な文字列 ===== */
    TEST("U-H1 boot");
    {
        boot_ctx_t c;
        c.build = "Nov_20_2026_11:10:04"; c.ch = 2u; c.baud = 115200u;
        sweep("boot", w_boot, &c, "[boot] fw=rx72n_gcc build=Nov_20_2026_11:10:04 ch=2 baud=115200\r\n", RX_BOOT);
        c.build = "Nov 20 2026 11:10:04";
        sweep("boot(空白)", w_boot, &c, "[boot] fw=rx72n_gcc build=Nov_20_2026_11:10:04 ch=2 baud=115200\r\n", RX_BOOT);
        c.build = NULL;
        sweep("boot(NULL)", w_boot, &c, "[boot] fw=rx72n_gcc build=unknown ch=2 baud=115200\r\n", RX_BOOT);
        c.build = "x"; c.ch = 255u; c.baud = 4294967295u;
        sweep("boot(最大)", w_boot, &c, "[boot] fw=rx72n_gcc build=x ch=255 baud=4294967295\r\n", NULL);
        c.ch = 0u; c.baud = 0u;
        sweep("boot(0)", w_boot, &c, "[boot] fw=rx72n_gcc build=x ch=0 baud=0\r\n", NULL);
    }

    TEST("U-H1 init");
    {
        init_ctx_t c;
        c.stage = "lcd"; c.ok = true; c.err = 0u; c.detail = 0u;
        sweep("init lcd ok", w_init, &c, "[init] stage=lcd ok=1 err=0 detail=0\r\n", RX_INIT);
        c.stage = "touch"; c.ok = false; c.err = 3u; c.detail = 5u;
        sweep("init touch ng", w_init, &c, "[init] stage=touch ok=0 err=3 detail=5\r\n", RX_INIT);
        c.stage = "timer"; c.ok = false; c.err = 4u; c.detail = 0u;
        sweep("init timer ng", w_init, &c, "[init] stage=timer ok=0 err=4 detail=0\r\n", RX_INIT);
        c.stage = "loop"; c.ok = true; c.err = 4294967295u; c.detail = 4294967295u;   /* 渡された値をそのまま出す */
        sweep("init 最大値", w_init, &c, "[init] stage=loop ok=1 err=4294967295 detail=4294967295\r\n", RX_INIT);
        c.stage = NULL;
        sweep("init stage=NULL", w_init, &c, "[init] stage=? ok=1 err=4294967295 detail=4294967295\r\n", NULL);
    }

    TEST("U-H1 hb / st: 代表値と境界値");
    {
        applog_state_t s;
        st_ctx_t c;
        const char *hb_max =
            "[hb] tick=4294967295 scr=4 demo=1 frm=4294967295 uf=4294967295 err=4294967295 detail=4294967295 pts=255 x=65535 y=65535 i2c_fail=4294967295 streak=255 found=255 drop=4294967295\r\n";
        const char *st_max =
            "[st] tick=4294967295 scr=4 demo=1 frm=4294967295 uf=4294967295 err=4294967295 detail=4294967295 pts=255 x=65535 y=65535 i2c_fail=4294967295 streak=255 found=255 drop=4294967295"
            " xmin=65535 xmax=65535 ymin=65535 ymax=65535\r\n";
        c.s = &s;
        s = st_make(123u, 1u, 0u, 0u, 0u, 0u, 0u, 120u, 88u, 1u, 1u);
        c.tag = "hb"; c.range = false;
        sweep("hb 代表値", w_state, &c, "[hb] tick=123 scr=1 demo=0 frm=0 uf=0 err=0 detail=0 pts=1 x=120 y=88 i2c_fail=0 streak=0 found=1 drop=0\r\n", RX_HB);
        c.tag = "st"; c.range = true;
        sweep("st 代表値(未タッチの範囲)", w_state, &c,
              "[st] tick=123 scr=1 demo=0 frm=0 uf=0 err=0 detail=0 pts=1 x=120 y=88 i2c_fail=0 streak=0 found=1 drop=0 xmin=65535 xmax=0 ymin=65535 ymax=0\r\n", RX_ST);
        s.x_min = 3u; s.x_max = 476u; s.y_min = 2u; s.y_max = 269u;
        sweep("st 範囲あり", w_state, &c,
              "[st] tick=123 scr=1 demo=0 frm=0 uf=0 err=0 detail=0 pts=1 x=120 y=88 i2c_fail=0 streak=0 found=1 drop=0 xmin=3 xmax=476 ymin=2 ymax=269\r\n", RX_ST);

        s = st_make(0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
        s.x_min = 0u; s.y_min = 0u;
        c.tag = "hb"; c.range = false;
        sweep("hb 全0", w_state, &c, "[hb] tick=0 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0\r\n", RX_HB);

        s = st_make(4294967295u, 4u, 4294967295u, 4294967295u, 4294967295u, 255u, 4294967295u, 65535u, 65535u, 255u, 255u);
        s.x_max = 65535u; s.y_max = 65535u; s.demo = 1u; s.frames = 4294967295u; s.uf = 4294967295u;
        c.tag = "hb"; c.range = false;
        sweep("hb 全最大", w_state, &c, hb_max, RX_HB);
        c.tag = "st"; c.range = true;
        sweep("st 全最大", w_state, &c, st_max, RX_ST);

        /* U-H2: 最長の長さ。設計書(design-uart.md §10.2)の計算値は hb=178 / st=222。上限 256(NUL込み)を超えない */
        printf("  (実測) hb 最長 = %zu バイト、st 最長 = %zu バイト、APPLOG_LINE_MAX = %u\n", strlen(hb_max), strlen(st_max), (unsigned)APPLOG_LINE_MAX);
        CHECK_EQ(strlen(hb_max), 178);
        CHECK_EQ(strlen(st_max), 222);
        CHECK(strlen(hb_max) + 1u <= APPLOG_LINE_MAX);
        CHECK(strlen(st_max) + 1u <= APPLOG_LINE_MAX);
        CHECK_EQ(APPLOG_LINE_MAX, 256);
        s.demo = 255u;                                         /* demo が 3 桁でも(実際は 0/1)256 を超えない */
        CHECK(applog_fmt_state(g_one, sizeof(g_one), "st", &s, true) + 1u <= APPLOG_LINE_MAX);
        s.demo = 1u;
        c.tag = "hb"; c.range = false;
        CHECK_EQ(applog_fmt_state(g_one, sizeof(g_one), "hb", &s, false), strlen(hb_max));
        CHECK_EQ(applog_fmt_state(g_one, sizeof(g_one), "st", &s, true), strlen(st_max));
        /* ちょうど APPLOG_LINE_MAX バイトのバッファで最長の st が入る */
        CHECK_EQ(applog_fmt_state(g_one, APPLOG_LINE_MAX, "st", &s, true), strlen(st_max));
    }

    TEST("U-H1 hb: 各項目を1つずつ境界値にする(取り違え・桁あふれ・符号の検出)");
    {
        st_ctx_t c;
        applog_state_t s;
        c.tag = "hb"; c.s = &s; c.range = false;
#define HB1(label, st, expect) do { s = (st); sweep(label, w_state, &c, expect, RX_HB); } while (0)
        HB1("tick=max",   st_make(4294967295u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u), "[hb] tick=4294967295 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0\r\n");
        HB1("img=255",    st_make(0u,255u,0u,0u,0u,0u,0u,0u,0u,0u,0u),        "[hb] tick=0 scr=255 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0\r\n");
        HB1("err=max",    st_make(0u,0u,4294967295u,0u,0u,0u,0u,0u,0u,0u,0u), "[hb] tick=0 scr=0 demo=0 frm=0 uf=0 err=4294967295 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0\r\n");
        HB1("detail=max", st_make(0u,0u,0u,4294967295u,0u,0u,0u,0u,0u,0u,0u), "[hb] tick=0 scr=0 demo=0 frm=0 uf=0 err=0 detail=4294967295 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0\r\n");
        HB1("i2c_fail=max", st_make(0u,0u,0u,0u,4294967295u,0u,0u,0u,0u,0u,0u), "[hb] tick=0 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=4294967295 streak=0 found=0 drop=0\r\n");
        HB1("streak=255", st_make(0u,0u,0u,0u,0u,255u,0u,0u,0u,0u,0u),        "[hb] tick=0 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=255 found=0 drop=0\r\n");
        HB1("drop=max",   st_make(0u,0u,0u,0u,0u,0u,4294967295u,0u,0u,0u,0u), "[hb] tick=0 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=4294967295\r\n");
        HB1("x=65535",    st_make(0u,0u,0u,0u,0u,0u,0u,65535u,0u,0u,0u),      "[hb] tick=0 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=65535 y=0 i2c_fail=0 streak=0 found=0 drop=0\r\n");
        HB1("y=65535",    st_make(0u,0u,0u,0u,0u,0u,0u,0u,65535u,0u,0u),      "[hb] tick=0 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=0 y=65535 i2c_fail=0 streak=0 found=0 drop=0\r\n");
        HB1("pts=255",    st_make(0u,0u,0u,0u,0u,0u,0u,0u,0u,255u,0u),        "[hb] tick=0 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=255 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0\r\n");
        HB1("found=255",  st_make(0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,255u),        "[hb] tick=0 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=255 drop=0\r\n");
        /* 最上位ビットが立つ値が負にならない(符号付き変換のバグ検出) */
        HB1("MSBあり", st_make(0x80000000u,128u,0x80000000u,0x80000001u,0xFFFFFFFEu,129u,0x80000002u,0x8000u,0xFFFEu,130u,131u),
            "[hb] tick=2147483648 scr=128 demo=0 frm=0 uf=0 err=2147483648 detail=2147483649 pts=130 x=32768 y=65534 i2c_fail=4294967294 streak=129 found=131 drop=2147483650\r\n");
#undef HB1
    }

    TEST("U-H1 [st] の範囲項目(xmin xmax ymin ymax)の取り違え検出");
    {
        applog_state_t s = st_make(1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
        st_ctx_t c;
        c.tag = "st"; c.s = &s; c.range = true;
        s.x_min = 1u; s.x_max = 2u; s.y_min = 3u; s.y_max = 4u;
        sweep("st 1234", w_state, &c, "[st] tick=1 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0 xmin=1 xmax=2 ymin=3 ymax=4\r\n", RX_ST);
        /* with_range=false なら範囲を出さない */
        c.range = false;
        sweep("st 範囲なし", w_state, &c, "[st] tick=1 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0\r\n", NULL);
    }

    TEST("U-H1 scan");
    {
        scan_ctx_t c;
        uint8_t a[4];
        a[0] = 0x38u; a[1] = 0x0Au; a[2] = 0xFFu; a[3] = 0x00u;
        c.addr = a;
        c.count = 1u; sweep("scan 1", w_scan, &c, "[scan] n=1 a0=0x38\r\n", RX_SCAN);
        c.count = 4u; sweep("scan 4", w_scan, &c, "[scan] n=4 a0=0x38 a1=0x0a a2=0xff a3=0x00\r\n", RX_SCAN);
        c.count = 0u; sweep("scan 0", w_scan, &c, "[scan] n=0\r\n", RX_SCAN);
        c.count = 5u; sweep("scan 5 → 4 に丸め", w_scan, &c, "[scan] n=4 a0=0x38 a1=0x0a a2=0xff a3=0x00\r\n", RX_SCAN);
        c.count = 255u; sweep("scan 255 → 4 に丸め", w_scan, &c, "[scan] n=4 a0=0x38 a1=0x0a a2=0xff a3=0x00\r\n", RX_SCAN);
        c.addr = NULL; c.count = 0u; sweep("scan 0 (addr=NULL)", w_scan, &c, "[scan] n=0\r\n", RX_SCAN);
        {   /* count>0 かつ addr==NULL は失敗(0)、buf[0]='\0' */
            char b[64]; memset(b, POISON, sizeof(b));
            CHECK_EQ(applog_fmt_scan(b, sizeof(b), 1u, NULL), 0);
            CHECK(b[0] == '\0');
            memset(b, POISON, sizeof(b));
            CHECK_EQ(applog_fmt_scan(b, sizeof(b), 9u, NULL), 0);
            CHECK(b[0] == '\0');
        }
    }

    TEST("U-H1 ev / err / help");
    {
        ev_ctx_t e; err_ctx_t r;
        e.src = "touch"; sweep("ev tap touch", w_ev, &e, "[ev] btn=tap src=touch\r\n", RX_EV);
        e.src = "uart";  sweep("ev tap uart",  w_ev, &e, "[ev] btn=tap src=uart\r\n", RX_EV);
        e.src = NULL;    sweep("ev tap src=NULL", w_ev, &e, "[ev] btn=tap src=?\r\n", NULL);
        r.code = 0u; r.detail = 0u; sweep("err 0/0", w_err, &r, "[err] code=0 detail=0\r\n", RX_ERR);
        r.code = 7u; r.detail = 2u; sweep("err 7/2", w_err, &r, "[err] code=7 detail=2\r\n", RX_ERR);
        r.code = 4294967295u; r.detail = 4294967295u; sweep("err 最大", w_err, &r, "[err] code=4294967295 detail=4294967295\r\n", RX_ERR);
        sweep("help", w_help, NULL, "[help] s=state x=update ?=help\r\n", RX_HELP);
    }

    TEST("U-H1 [perf]");
    {
        perf_t p;
        p.n = 50u; p.a = 4000u; p.b = 8200u; p.c = 9600u;
        sweep("perf", w_perf, &p, "[perf] n=50 copy_us=4000 home_us=8200 slide_us=9600\r\n", RX_PERF);
        p.n = 4294967295u; p.a = 4294967295u; p.b = 0u; p.c = 1u;
        sweep("perf 最大", w_perf, &p, "[perf] n=4294967295 copy_us=4294967295 home_us=0 slide_us=1\r\n", RX_PERF);
    }

    TEST("U-H1 新しい行の長さ: すべて APPLOG_LINE_MAX 未満(最大値で)");
    {
        char b[APPLOG_LINE_MAX];
        perf_t p;
        size_t n;
        n = applog_fmt_ev_tap(b, sizeof(b), "touch"); CHECK(n > 0u && n < APPLOG_LINE_MAX);
        p.n = p.a = p.b = p.c = 4294967295u; n = applog_fmt_perf(b, sizeof(b), p.n, p.a, p.b, p.c); CHECK(n > 0u && n < APPLOG_LINE_MAX);
    }

    TEST("U-H3 引数不正(NULL / size=0)と、失敗時の buf[0]");
    {
        char buf[64];
        applog_state_t s = st_make(1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
        uint8_t a[1];
        a[0] = 0x38u;
        CHECK_EQ(applog_fmt_boot(NULL, 64u, "x", 2u, 115200u), 0);
        CHECK_EQ(applog_fmt_init(NULL, 64u, "lcd", true, 0u, 0u), 0);
        CHECK_EQ(applog_fmt_state(NULL, 64u, "hb", &s, false), 0);
        CHECK_EQ(applog_fmt_scan(NULL, 64u, 1u, a), 0);
        CHECK_EQ(applog_fmt_ev_tap(NULL, 64u, "uart"), 0);
        CHECK_EQ(applog_fmt_err(NULL, 64u, 1u, 1u), 0);
        CHECK_EQ(applog_fmt_help(NULL, 64u), 0);
        CHECK_EQ(applog_fmt_perf(NULL, 64u, 1u, 1u, 1u, 1u), 0);
        memset(buf, POISON, sizeof(buf));
        CHECK_EQ(applog_fmt_state(buf, sizeof(buf), "hb", NULL, false), 0);      /* 必須ポインタ NULL */
        CHECK(buf[0] == '\0');
        /* tag == NULL は "?" で整形 */
        CHECK_EQ(applog_fmt_state(g_one, sizeof(g_one), NULL, &s, false),
                 strlen("[?] tick=1 scr=0 demo=0 frm=0 uf=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0\r\n"));
        CHECK(strncmp(g_one, "[?] tick=1", 10) == 0);
        /* 失敗後に成功すれば、前の失敗の影響が残らない */
        CHECK_EQ(applog_fmt_help(g_one, sizeof(g_one)), strlen("[help] s=state x=update ?=help\r\n"));
    }

    TEST("U-H4 boot: build の空白・制御文字・長さ・NULL");
    {
        char buf[APPLOG_LINE_MAX];
        char longb[200];
        const char *pre = "[boot] fw=rx72n_gcc build=";
        size_t n, i;
        char in[8];

        /* 空白は '_'、31文字で切る */
        n = applog_fmt_boot(buf, sizeof(buf), "a b c", 2u, 115200u);
        CHECK_EQ(n, strlen("[boot] fw=rx72n_gcc build=a_b_c ch=2 baud=115200\r\n"));
        CHECK(strncmp(buf + strlen(pre), "a_b_c ch=2", 10) == 0);
        /* 0x00-0x20 と 0x7F-0xFF が '_' になる(1文字ずつ) */
        for (i = 0u; i <= 255u; i++) {
            const char *p;
            in[0] = 'a'; in[1] = (char)i; in[2] = 'b'; in[3] = '\0';
            if (i == 0u) { continue; }   /* NUL は文字列の終端 */
            n = applog_fmt_boot(buf, sizeof(buf), in, 2u, 115200u);
            CHECK(n > 0u);
            p = buf + strlen(pre);
            if ((i >= 0x21u) && (i <= 0x7Eu)) {
                CHECK(p[1] == (char)i);
            } else {
                CHECK(p[1] == '_');
            }
            CHECK(line_ascii_ok(buf, n));
            CHECK(rx_match(RX_BOOT, buf, n));
        }
        /* 長さ: 31文字ちょうど / 32文字 / 200文字 */
        memset(longb, 'A', sizeof(longb) - 1u); longb[sizeof(longb) - 1u] = '\0';
        longb[31] = '\0';
        n = applog_fmt_boot(buf, sizeof(buf), longb, 2u, 115200u);
        CHECK_EQ(n, strlen(pre) + 31u + strlen(" ch=2 baud=115200\r\n"));
        longb[31] = 'A'; longb[32] = '\0';
        n = applog_fmt_boot(buf, sizeof(buf), longb, 2u, 115200u);
        CHECK_EQ(n, strlen(pre) + 31u + strlen(" ch=2 baud=115200\r\n"));   /* 32文字目は切られる */
        memset(longb, 'B', sizeof(longb) - 1u); longb[sizeof(longb) - 1u] = '\0';
        n = applog_fmt_boot(buf, sizeof(buf), longb, 2u, 115200u);
        CHECK_EQ(n, strlen(pre) + 31u + strlen(" ch=2 baud=115200\r\n"));
        CHECK(rx_match(RX_BOOT, buf, n));
        /* 0x7F と 0x80 以上(符号付き char として負)も '_' */
        in[0] = (char)0x7F; in[1] = (char)0x80; in[2] = (char)0xFF; in[3] = '\0';
        n = applog_fmt_boot(buf, sizeof(buf), in, 2u, 115200u);
        CHECK(strncmp(buf + strlen(pre), "___ ch=2", 8) == 0);
        /* 実際の __DATE__ " " __TIME__ */
        n = applog_fmt_boot(buf, sizeof(buf), __DATE__ " " __TIME__, 2u, 115200u);
        CHECK(n > 0u);
        CHECK(rx_match(RX_BOOT, buf, n));
        CHECK(strchr(buf + strlen(pre), ' ') != NULL);   /* build の後の空白(区切り)は残る */
        /* 空文字: build= の値が空になり R-BOOT(build=\S+)に合わない。実際の呼び出しでは起きない(参考) */
        n = applog_fmt_boot(buf, sizeof(buf), "", 2u, 115200u);
        printf("  (参考) build=\"\" の出力 = %s", buf);
        CHECK(n > 0u);
    }

    /* ===== U-H5: コマンド解釈 ===== */
    TEST("U-H5 applog_cmd_parse: n p s ? d x と その他の全値(+ - は製品から廃止 = NONE)");
    {
        int ch, other_nonnone = 0;
        CHECK_EQ(applog_cmd_parse('n'), APPLOG_CMD_NEXT);
        CHECK_EQ(applog_cmd_parse('p'), APPLOG_CMD_PREV);
        CHECK_EQ(applog_cmd_parse('s'), APPLOG_CMD_STATE);
        CHECK_EQ(applog_cmd_parse('?'), APPLOG_CMD_HELP);
        CHECK_EQ(applog_cmd_parse('d'), APPLOG_CMD_DEMO);
        CHECK_EQ(applog_cmd_parse('+'), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse('-'), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse('D'), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse('='), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse('_'), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse('N'), APPLOG_CMD_NONE);        /* 大文字は無視 */
        CHECK_EQ(applog_cmd_parse('P'), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse('S'), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse('\r'), APPLOG_CMD_NONE);       /* 改行は無視 */
        CHECK_EQ(applog_cmd_parse('\n'), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse(' '), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse('x'), APPLOG_CMD_WX);          /* 更新ボタン h_wx の仮想タップ(UT5 では無視だった。design-uart.md §13.2) */
        CHECK_EQ(applog_cmd_parse('X'), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse(0x00), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse(0xFF), APPLOG_CMD_NONE);       /* 受信バイト 0xFF (int 255) */
        CHECK_EQ(applog_cmd_parse(-1), APPLOG_CMD_NONE);         /* 受信なし */
        CHECK_EQ(applog_cmd_parse('0'), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse('1'), APPLOG_CMD_NONE);
        /* -300 .. 600 を総当たり */
        for (ch = -300; ch <= 600; ch++) {
            if ((ch == 'n') || (ch == 'p') || (ch == 's') || (ch == '?') || (ch == 'd') || (ch == 'x')) { continue; }
            if (applog_cmd_parse(ch) != APPLOG_CMD_NONE) { other_nonnone++; }
        }
        CHECK_EQ(other_nonnone, 0);
        /* 範囲外の int(下位8bitが 'n' になる値など)を誤認しない */
        CHECK_EQ(applog_cmd_parse('n' + 256), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse('n' - 256), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse('?' + 256), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse(0x7FFFFFFF), APPLOG_CMD_NONE);
        CHECK_EQ(applog_cmd_parse(-0x7FFFFFFF - 1), APPLOG_CMD_NONE);
    }

    /* ===== U-H6: 周期判定 ===== */
    TEST("U-H6 applog_due: 100 tick 毎、折り返し");
    {
        CHECK_EQ(applog_due(0u, 0u, 100u), 0);
        CHECK_EQ(applog_due(99u, 0u, 100u), 0);
        CHECK_EQ(applog_due(100u, 0u, 100u), 1);
        CHECK_EQ(applog_due(101u, 0u, 100u), 1);
        CHECK_EQ(applog_due(199u, 100u, 100u), 0);
        CHECK_EQ(applog_due(200u, 100u, 100u), 1);
        CHECK_EQ(applog_due(5u, 5u, 100u), 0);
        CHECK_EQ(applog_due(5u, 5u, 0u), 1);                      /* 周期 0 は常に出る */
        /* 折り返し: last=0xFFFFFFCE(= -50) から now=49 は 99 tick、now=50 は 100 tick */
        CHECK_EQ(applog_due(49u, 0xFFFFFFCEu, 100u), 0);
        CHECK_EQ(applog_due(50u, 0xFFFFFFCEu, 100u), 1);
        CHECK_EQ((uint32_t)(99u - 0xFFFFFFFFu), 100u);             /* 算術の前提 */
        CHECK_EQ(applog_due(98u, 0xFFFFFFFFu, 100u), 0);           /* 99 tick */
        CHECK_EQ(applog_due(99u, 0xFFFFFFFFu, 100u), 1);           /* 100 tick */
        CHECK_EQ(applog_due(0xFFFFFFFFu, 0xFFFFFFFFu, 100u), 0);
        CHECK_EQ(applog_due(0xFFFFFFFFu, 0u, 100u), 1);
        CHECK_EQ(applog_due(0u, 0xFFFFFFFFu, 100u), 0);            /* 1 tick */
        /* 周期が最大のとき */
        CHECK_EQ(applog_due(0xFFFFFFFEu, 0u, 0xFFFFFFFFu), 0);
        CHECK_EQ(applog_due(0xFFFFFFFFu, 0u, 0xFFFFFFFFu), 1);
        /* [err] (A)=10 tick, (B)=100 tick の周期 */
        CHECK_EQ(applog_due(9u, 0u, 10u), 0);
        CHECK_EQ(applog_due(10u, 0u, 10u), 1);
    }

    TEST("U-H6 周期のシミュレーション: 1000 tick で 10 回、折り返しをまたいでも同じ");
    {
        uint32_t starts[5];
        unsigned si;
        starts[0] = 0u; starts[1] = 0xFFFFFF00u; starts[2] = 0xFFFFFFFFu; starts[3] = 12345u; starts[4] = 0xFFFFFF9Cu;
        for (si = 0u; si < 5u; si++) {
            uint32_t now = starts[si], last_hb = starts[si], prev_hit = starts[si];
            unsigned hits = 0u, k;
            int spacing_ok = 1;
            for (k = 0u; k < 1000u; k++) {
                now++;
                if (applog_due(now, last_hb, 100u) != 0) {
                    last_hb = now;
                    if ((uint32_t)(now - prev_hit) != 100u) { spacing_ok = 0; }
                    prev_hit = now;
                    hits++;
                }
            }
            CHECK_EQ(hits, 10);
            CHECK(spacing_ok);
        }
        /* app_main は last_hb=0 から始まる。tick が 0xFFFFFF00 付近から始まる場合: 初回は直ちに出て、その後は100毎 */
        {
            uint32_t now = 0xFFFFFF00u, last_hb = 0u, first = 0u;
            unsigned hits = 0u, k;
            for (k = 0u; k < 1000u; k++) {
                now++;
                if (applog_due(now, last_hb, 100u) != 0) { if (hits == 0u) { first = now; } last_hb = now; hits++; }
            }
            CHECK_EQ(first, 0xFFFFFF01u);
            CHECK_EQ(hits, 10);
        }
        /* tick が飛んだ(メインループが詰まった)場合: 1回だけ出し、まとめ出しはしない */
        {
            uint32_t last_hb = 0u, now; unsigned hits = 0u;
            for (now = 5u; now <= 1000u; now += 250u) {     /* 5(まだ), 255, 505, 755 */
                if (applog_due(now, last_hb, 100u) != 0) { last_hb = now; hits++; }
            }
            CHECK_EQ(hits, 3);
        }
    }

    TEST("applog_err_changed");
    {
        CHECK_EQ(applog_err_changed(0u, 0u, 0u, 0u), 0);
        CHECK_EQ(applog_err_changed(5u, 0u, 0u, 0u), 1);
        CHECK_EQ(applog_err_changed(0u, 7u, 0u, 0u), 1);
        CHECK_EQ(applog_err_changed(5u, 7u, 5u, 7u), 0);
        CHECK_EQ(applog_err_changed(5u, 7u, 5u, 8u), 1);
        CHECK_EQ(applog_err_changed(5u, 7u, 6u, 7u), 1);
        CHECK_EQ(applog_err_changed(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu), 0);
        CHECK_EQ(applog_err_changed(0u, 0u, 0xFFFFFFFFu, 0xFFFFFFFFu), 1);
        CHECK_EQ(applog_err_changed(1u, 2u, 2u, 1u), 1);                 /* 入れ替えを同じと誤認しない */
    }

    TEST_REPORT("test_logfmt");
}
