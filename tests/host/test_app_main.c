/* test_app_main.c - src/app/app_main.c(Phase 4 の結合。docs/design.md §7.2・§7.3・§7.5・§8.1 H-12・H-13)を、偽の周辺で動かす結合試験。
 *   - 実 platform.h と FIT の実ヘッダを通し、app_main.c をそのまま #include する(test_wifi_at.c と同じ流儀)。
 *     本物をリンクするのは ui_render.c / ui_logic.c / stk_core.c / gfx.c / app_logfmt.c / 生成データ。偽物: SCI2 のログ(app_log_*)・LCD(lcd_*)・タッチ(touch_*)・CMT・wifi_at の API。
 *   - app_main() の無限ループは、ループ先頭の app_log_poll() をマクロで差し替えたフック(loop_hook)から longjmp で抜ける。
 *     フックは 1 周回 = 10ms の tick として CMT のコールバックを呼び、スクリプト(タッチ・UART 文字・取得の表示値)を進める。
 *   - 見るもの: 起動の行の順序と、面 0 を lcd_init の前に描くこと / stk_ui の値が ui_render で描かれて入れ替わること(変わった時だけ・1 回ずつ・左 480 列が ui_render と一致)/
 *     タップ(touch・UART x)= [ev] btn=tap src=... 1 行 + wx_request(src) 1 回 / s ? の応答 / n p d + - は無視 / [hb] の周期 /
 *     ビルドの種類: 既定(製品 APP_WX = 1)、-DAPP_WX=0(取得なし = 起動中・オフラインのまま)、-DAPP_STK_PREVIEW=1(8 状態を 3 秒ごと)、
 *     -DT_TOUCH_FAIL / -DT_LCD_FAIL(初期化の失敗でも続行)。
 *   試験できるのは論理。GLCDC・I2C・割り込み・実時間は試験できない。 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <setjmp.h>
#include "test_framework.h"

#pragma GCC diagnostic ignored "-Wunused-variable"      /* ビルドの種類によって使わない補助がある */
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

#include "platform.h"
#include "r_cmt_rx_if.h"
#include "../../src/app/app_debug.h"
#include "../../src/app/app_logfmt.h"
#include "../../src/app/lcd_panel.h"
#include "../../src/app/touch_ft5x.h"
#include "../../src/app/wifi_at.h"
#include "../../src/app/ui_logic.h"
#include "../../src/app/ui_render.h"
#include "../../src/app/ui_assets.h"
#include "../../src/app/gfx.h"

#define FB_BYTES  ((size_t)GFX_STRIDE * GFX_H)
#define MAX_LINES 512

/* ------------------------------------------------------------ 偽物: ログ・g_app_dbg */
static char     g_lines[MAX_LINES][APPLOG_LINE_MAX];
static unsigned g_nlines;
static unsigned g_drops;
volatile app_dbg_t g_app_dbg;

void app_debug_init(void) { memset((void *)&g_app_dbg, 0, sizeof g_app_dbg); }
void app_debug_set_err(uint32_t code, uint32_t detail) { g_app_dbg.err_code = code; g_app_dbg.err_detail = detail; }
void app_debug_touch(const uint16_t x, const uint16_t y, const uint8_t points) { g_app_dbg.raw_x = x; g_app_dbg.raw_y = y; g_app_dbg.raw_points = points; }
void app_log_init(void) { }
void app_log_line(const char *s, size_t n)
{
    if ((s == NULL) || (n == 0u) || (n >= APPLOG_LINE_MAX) || (g_nlines >= MAX_LINES)) { g_drops++; return; }
    memcpy(g_lines[g_nlines], s, n);
    g_lines[g_nlines][n] = '\0';
    g_nlines++;
}
void app_log_poll(void) { }
uint32_t app_log_drops(void) { return g_drops; }
uint32_t app_log_rx_event(void) { return 0u; }

/* UART 受信文字のスクリプト(tick に達した時に 1 文字。1 tick に 1 文字) */
typedef struct { uint32_t tick; char ch; } cmd_t;
static const cmd_t *g_cmds; static unsigned g_ncmds, g_cmd_i;
static uint32_t g_tick_now;                       /* 偽物が見る「今の tick」(フックが s_tick を写す) */
int app_log_getc(void)
{
    if ((g_cmd_i < g_ncmds) && (g_tick_now >= g_cmds[g_cmd_i].tick)) { return (int)(unsigned char)g_cmds[g_cmd_i++].ch; }
    return -1;
}

/* ------------------------------------------------------------ 偽物: CMT・LCD・タッチ */
static void (*g_cb)(void *);
bool R_CMT_CreatePeriodic(uint32_t frequency_hz, void (*callback)(void *pdata), uint32_t *channel)
{
    CHECK_EQ(frequency_hz, 100);
    g_cb = callback;
    *channel = 0u;
    return true;
}

static uint8_t g_fb[2][FB_BYTES];
static uint8_t g_fb0_at_init[FB_BYTES];           /* lcd_init が呼ばれた時の面 0 */
static unsigned g_n_lcd_init, g_n_touch_init, g_n_touch_read;
static int      g_busy;                           /* 入れ替えの反映待ち(周回数) */
static uint8_t  g_pending, g_front;               /* 受け付けた面 / 今の表示面 */
static unsigned g_flip_n; static uint8_t g_flip_log[64];
static const uint32_t *g_flip_pal_log[512];       /* 受け付けた入れ替えのパレット(lcd_flip = NULL、lcd_flip_pal = 渡された配列) */
static const uint32_t *g_pending_pal, *g_front_pal;   /* 受け付けたパレット(NULL = 変えない)/ 今表示しているパレット */
static unsigned g_n_flip_pal, g_n_pal_same;       /* lcd_flip_pal の回数 / 今表示中と同じパレットでの lcd_flip_pal(§13.2 の 4 の違反) */
#if APP_CLK_PREVIEW
#define SNAP_N (32u)
static uint8_t g_snap[SNAP_N][FB_BYTES];          /* 入れ替えを受け付けた時の面の中身(i 番目の入れ替え = g_snap[i]) */
#define SNAP(i, n) do { if ((n) < SNAP_N) { memcpy(g_snap[(n)], g_fb[(i) & 1u], FB_BYTES); } } while (0)
#else
#define SNAP(i, n) ((void)0)
#endif

bool lcd_init(void)
{
    g_n_lcd_init++;
    g_front_pal = ui_pal;                             /* R_GLCDC_Open が ui_pal を書く(起動時の表示) */
    memcpy(g_fb0_at_init, g_fb[0], FB_BYTES);
#ifdef T_LCD_FAIL
    app_debug_set_err(APP_ERR_LCD_OPEN, 3u);
    return false;
#else
    return true;
#endif
}
uint8_t *lcd_fb(uint8_t i) { return g_fb[i & 1u]; }
lcd_flip_t lcd_flip(uint8_t i)
{
    if (g_busy > 0) { return LCD_FLIP_BUSY; }
    g_busy = 2;
    g_pending = i;
    g_pending_pal = NULL;
    SNAP(i, g_flip_n);
    if (g_flip_n < sizeof g_flip_log) { g_flip_log[g_flip_n] = i; }
    if (g_flip_n < sizeof g_flip_pal_log / sizeof g_flip_pal_log[0]) { g_flip_pal_log[g_flip_n] = NULL; }
    g_flip_n++;
    return LCD_FLIP_OK;
}
lcd_flip_t lcd_flip_pal(uint8_t i, const uint32_t *pal)
{
    if (g_busy > 0) { return LCD_FLIP_BUSY; }
    g_n_flip_pal++;
    if (pal == g_front_pal) { g_n_pal_same++; }
    g_busy = 2;
    g_pending = i;
    g_pending_pal = pal;
    SNAP(i, g_flip_n);
    if (g_flip_n < sizeof g_flip_log) { g_flip_log[g_flip_n] = i; }
    if (g_flip_n < sizeof g_flip_pal_log / sizeof g_flip_pal_log[0]) { g_flip_pal_log[g_flip_n] = pal; }
    g_flip_n++;
    return LCD_FLIP_OK;
}
bool lcd_flip_busy(void) { return g_busy > 0; }
uint32_t lcd_underflow_poll(void) { return 0u; }
static unsigned g_n_bl; static uint8_t g_bl_lv[32];       /* lcd_bl_set の呼び出し(7a) */
void lcd_bl_set(uint8_t level) { if (g_n_bl < sizeof g_bl_lv) { g_bl_lv[g_n_bl] = level; } g_n_bl++; }

static const nav_sample_t *g_touch_script;        /* 1 tick に 1 標本。tick 番号 = 配列の添字 + g_touch_t0 */
static unsigned g_touch_n; static uint32_t g_touch_t0;
bool touch_init(void)
{
    g_n_touch_init++;
#ifdef T_TOUCH_FAIL
    app_debug_set_err(APP_ERR_TOUCH_OPEN, 1u);
    return false;
#else
    return true;
#endif
}
bool touch_read(nav_sample_t *out)
{
    memset(out, 0, sizeof *out);
    g_n_touch_read++;
    if ((g_tick_now >= g_touch_t0) && ((g_tick_now - g_touch_t0) < g_touch_n)) { *out = g_touch_script[g_tick_now - g_touch_t0]; }
    return true;
}

/* ------------------------------------------------------------ 偽物: wifi_at の API(製品のとき) */
static unsigned g_n_wx_init, g_n_wx_poll, g_n_wx_state, g_n_req, g_lines_at_wx_init;
static uint32_t g_wx_poll_ms, g_wx_init_ms, g_ui_ms;
static unsigned g_req_src[16];
static stk_view_t g_stk;                          /* stk_ui が返す表示値(フックが書き換える) */
static clk_view_t g_clk = { .clock_min = 0xFFFFu, .kind = (uint8_t)CLK_UNKNOWN };   /* clk_ui が返す表示値(既定 = 時刻未同期。フックが書き換える) */
static unsigned g_n_clk_ui; static uint32_t g_clk_ui_ms;
#if APP_WX_PRODUCT
void wx_init(uint32_t now_ms) { g_n_wx_init++; g_wx_init_ms = now_ms; g_lines_at_wx_init = g_nlines; }
void wx_poll(uint32_t now_ms) { g_n_wx_poll++; g_wx_poll_ms = now_ms; }
wx_req_t wx_request(wx_src_t src, uint32_t now_ms) { (void)now_ms; if (g_n_req < 16u) { g_req_src[g_n_req] = (unsigned)src; } g_n_req++; return WX_REQ_OK; }
void stk_ui(stk_view_t *out, uint32_t now_ms) { *out = g_stk; g_ui_ms = now_ms; }
void wx_log_state(uint32_t now_ms) { (void)now_ms; g_n_wx_state++; }
void clk_ui(clk_view_t *out, uint32_t now_ms) { *out = g_clk; g_clk_ui_ms = now_ms; g_n_clk_ui++; }
#endif

/* ------------------------------------------------------------ 被試験物 */
static jmp_buf g_jb;
static unsigned g_iter, g_max_iter;
static void loop_hook(void);
#define app_log_poll() (loop_hook(), app_log_poll())
#include "../../src/app/app_main.c"
#undef app_log_poll

static void (*g_on_tick)(uint32_t);               /* 各 tick の頭(タッチ・取得の表示値の更新) */

static void loop_hook(void)
{
    g_iter++;
    if (g_busy > 0) {
        g_busy--;
        if (g_busy == 0) { g_front = g_pending; if (g_pending_pal != NULL) { g_front_pal = g_pending_pal; } }
    }
    if (g_iter > g_max_iter) { longjmp(g_jb, 1); }
    if (g_cb != NULL) {                           /* 1 周回 = 10ms */
        g_cb(NULL);
        g_tick_now = s_tick;
        if (g_on_tick != NULL) { g_on_tick(g_tick_now); }
    }
}

/* ------------------------------------------------------------ 補助 */
static unsigned count_prefix(const char *p)
{
    unsigned i, n = 0u;
    for (i = 0u; i < g_nlines; i++) { if (strncmp(g_lines[i], p, strlen(p)) == 0) { n++; } }
    return n;
}
static int find_line(const char *p, unsigned from)
{
    unsigned i;
    for (i = from; i < g_nlines; i++) { if (strncmp(g_lines[i], p, strlen(p)) == 0) { return (int)i; } }
    return -1;
}

/* view を ui_render した基準(左 480 列だけ比べる) */
static uint8_t g_ref[FB_BYTES];
static void render_ref(const stk_view_t *v)
{
    static ui_t r;
    ui_init(&r, 0u);
    ui_set_view(&r, v);
    memset(g_ref, 0xA5, sizeof g_ref);
    ui_render(&r, g_ref);
}
static bool same_left480(const uint8_t *a, const uint8_t *b)
{
    unsigned y;
    for (y = 0u; y < GFX_H; y++) { if (memcmp(a + ((size_t)y * GFX_STRIDE), b + ((size_t)y * GFX_STRIDE), GFX_W) != 0) { return false; } }
    return true;
}
static bool front_is(const stk_view_t *v) { render_ref(v); return same_left480(g_fb[g_front], g_ref); }

/* i 番目の [scr] 行(0 起点)を探して、期待の前半と tick を調べる。tick は want_tick */
static bool scr_line_is(unsigned idx, bool to_clock, const clk_view_t *c, uint32_t want_tick)
{
    char want[160];
    int at = -1;
    unsigned i, seen = 0u;
    size_t n;
    const char *tk;

    for (i = 0u; i < g_nlines; i++) { if (strncmp(g_lines[i], "[scr] ", 6) == 0) { if (seen == idx) { at = (int)i; break; } seen++; } }
    if (at < 0) { return false; }
    n = applog_fmt_scr(want, sizeof want, to_clock, (uint8_t)(to_clock ? (1u + clk_view_kind(c)) : 0u), c->clock_min, clk_view_kind(c), c->have != 0u, c->temp, want_tick);
    return (n != 0u) && (strcmp(g_lines[at], want) == 0) && ((tk = strstr(g_lines[at], "tick=")) != NULL) && ((uint32_t)strtoul(tk + 5, NULL, 10) == want_tick);
}

static void run(unsigned max_iter)
{
    g_max_iter = max_iter;
    if (setjmp(g_jb) == 0) { app_main(); }
}

static void check_boot_lines(void)
{
    int b = find_line("[boot] fw=", 0), l = find_line("[init] stage=lcd ", 0), t = find_line("[init] stage=touch ", 0);
    int m = find_line("[init] stage=timer ", 0), p = find_line("[init] stage=loop ", 0);

    CHECK(b == 0);
    CHECK(l > b); CHECK(t > l); CHECK(m > t); CHECK(p > m);
    CHECK_EQ(g_n_lcd_init, 1); CHECK_EQ(g_n_touch_init, 1);
#ifdef T_LCD_FAIL
    CHECK(strncmp(g_lines[l], "[init] stage=lcd ok=0 err=1 detail=3", 36) == 0);
#else
    CHECK(strncmp(g_lines[l], "[init] stage=lcd ok=1 err=0 detail=0", 36) == 0);
#endif
#ifdef T_TOUCH_FAIL
    CHECK(strncmp(g_lines[t], "[init] stage=touch ok=0 err=3 detail=1", 38) == 0);
    CHECK_EQ(g_n_touch_read, 0);                  /* 失敗したタッチは読まない */
#else
    CHECK(strncmp(g_lines[t], "[init] stage=touch ok=1 err=0 detail=0", 38) == 0);
#endif
    CHECK(strncmp(g_lines[m], "[init] stage=timer ok=1 ", 24) == 0);
#if APP_PERF_BENCH && !defined(T_LCD_FAIL)
    CHECK(find_line("[perf] n=", p) == p + 1);                              /* [perf] は LCD が使える時だけ */
#endif
}

/* ================================================================ 初期化の失敗(製品。LCD またはタッチが開けなくても続行する) */
#if defined(T_LCD_FAIL) || defined(T_TOUCH_FAIL)

int main(void)
{
    static const cmd_t cmds[] = { { 20u, 'x' }, { 30u, 's' } };
    stk_view_t v;
    unsigned lcd_ok;

    memset(&v, 0, sizeof v);
    v.net = STK_NET_ON; v.have_any = 1u; v.clock_min = 555u; v.upd_min = 552u;
    v.row[0].have = 1u; v.row[0].price_x100 = 243300; v.row[0].diff_x100 = 5500; v.row[0].pct_bp = 231; v.row[0].mkt = STK_MKT_TRADING;
    g_stk = v;
    g_cmds = cmds; g_ncmds = 2u;
    memset(g_fb, 0xCC, sizeof g_fb);
    run(100u);

    TEST("初期化の失敗: [init] に ok=0 を出しても、ループ(wx_poll・UART・[hb])は続く。LCD が使えない時は描かず入れ替えない");
    check_boot_lines();
#ifdef T_LCD_FAIL
    lcd_ok = 0u;
    CHECK_EQ(g_flip_n, 0);
    CHECK_EQ(count_prefix("[perf] "), 0);
#else
    lcd_ok = 1u;
    CHECK(g_flip_n >= 1u);
    CHECK(front_is(&v));
#endif
    (void)lcd_ok;
    CHECK_EQ(count_prefix("[ev] btn=tap src=uart\r\n"), 1);
    CHECK_EQ(g_n_req, 1);
    CHECK_EQ(g_n_wx_init, 1);
    CHECK_EQ(g_n_wx_poll, g_iter - 1u);
    CHECK_EQ(count_prefix("[hb] "), 1);
    CHECK_EQ(g_drops, 0);
    CHECK_EQ(g_n_bl, 0);                          /* 昼なので変化なし。LCD が使えない時は lcd_bl_set を呼ばない(7a) */
    TEST_REPORT("test_app_main(初期化の失敗)");
}

/* ================================================================ 6d 結合(製品 + clk_ui の値の注入。docs/design.md §13.3・§13.5・§13.8) */
#elif defined(T_CLK_PRODUCT)

static nav_sample_t g_tpc[6200];
static unsigned g_clock_scr_1499, g_n_req_at_3600;

static clk_view_t clk_in(uint16_t m, uint8_t have, uint8_t kind)
{
    clk_view_t c;
    memset(&c, 0, sizeof c);
    c.clock_min = m; c.have = have; c.kind = kind; c.temp = have ? 18 : 0; c.mon = 11u; c.day = 27u; c.wday = 5u;
    return c;
}

static void clkprod_tick(uint32_t t)
{
    if (t == 1499u) { g_clock_scr_1499 = count_prefix("[scr] to=clock"); }
    if (t == 1500u) { g_clk = clk_in(581u, 1u, CLK_SUN); }              /* 時刻が分かった(株価の表示時間 10 秒は過ぎている → すぐ時計・天気へ) */
    if (t == 4600u) { g_clk = clk_in(582u, 0u, CLK_UNKNOWN); }           /* 天気が取れなくなった(60 分たった / 一度も取れていない) */
    if (t == 5300u) { g_clk = clk_in(0xFFFFu, 0u, CLK_UNKNOWN); }        /* 時刻が使えなくなった(株価画面にいる間) */
}

static bool has_scr(bool to_clock, const clk_view_t *c, uint32_t tick)
{
    char want[160];

    return (applog_fmt_scr(want, sizeof want, to_clock, (uint8_t)(to_clock ? (1u + clk_view_kind(c)) : 0u), c->clock_min, clk_view_kind(c), c->have != 0u, c->temp, tick) != 0u)
           && (find_line(want, 0) >= 0);
}

int main(void)
{
    stk_view_t v;
    unsigned i;
    const clk_view_t c0 = clk_in(581u, 1u, CLK_SUN), c1 = clk_in(582u, 0u, CLK_UNKNOWN), c2 = clk_in(0xFFFFu, 0u, CLK_UNKNOWN);

    memset(&v, 0, sizeof v);
    v.net = STK_NET_ON; v.have_any = 1u; v.clock_min = 555u; v.upd_min = 552u;
    v.row[0].have = 1u; v.row[0].price_x100 = 243300; v.row[0].diff_x100 = 5500; v.row[0].pct_bp = 231; v.row[0].mkt = STK_MKT_TRADING;
    g_stk = v;
    for (i = 0u; i < 6200u; i++) { g_tpc[i].pressed = false; g_tpc[i].points = 0u; g_tpc[i].x = 100u; g_tpc[i].y = 100u; }
    g_tpc[2500].pressed = true; g_tpc[2500].points = 1u; g_tpc[2501] = g_tpc[2500];      /* 株価画面でのタップ(離しの確定 = tick 2504) */
    g_tpc[3700] = g_tpc[2500]; g_tpc[3701] = g_tpc[2500];                                /* 時計・天気画面でのタップ(確定 = tick 3704) */
    g_touch_script = g_tpc; g_touch_n = 6200u; g_touch_t0 = 0u;
    g_on_tick = clkprod_tick;
    memset(g_fb, 0xCC, sizeof g_fb);

    run(7000u);

    TEST("6d 結合(製品 + clk_ui): 時刻が分かるまで(tick 1500 まで)は、株価の表示時間 10 秒を過ぎても時計・天気画面へ切り替えない。分かった tick で切り替える(パレット 1 + kind)");
    check_boot_lines();
    CHECK_EQ(g_clock_scr_1499, 0);
    CHECK_EQ(g_n_clk_ui, g_iter - 1u);
    CHECK(has_scr(true, &c0, 1500u));                                    /* [scr] to=clock pal=1 clk=0941 kind=sun temp=18 tick=1500 */
    CHECK(find_line("[scr] to=clock pal=1 clk=0941 kind=sun temp=18 tick=1500\r\n", 0) >= 0);

    TEST("6d 時計・天気画面は 5 秒で株価へ(tick 2000)、株価 10 秒で戻る。株価画面でのタップ(tick 2504)は切り替えタイマを最初から(次の時計・天気は 3504。3000 では出ない)+ 更新要求");
    CHECK(has_scr(false, &c0, 2000u));                                   /* 株価画面でも clk / kind / temp が付く */
    CHECK(find_line("[scr] to=stock pal=0 clk=0941 kind=sun temp=18 tick=2000\r\n", 0) >= 0);
    CHECK(find_line("[scr] to=clock pal=1 clk=0941 kind=sun temp=18 tick=3000\r\n", 0) < 0);
    CHECK(find_line("[scr] to=clock pal=1 clk=0941 kind=sun temp=18 tick=3504\r\n", 0) >= 0);   /* t_scr は直前の ui_update の時刻 = tick 2504 */

    TEST("6d 時計・天気画面でのタップ(tick 3704)は株価画面へ戻す([scr] to=stock)+ 更新要求(wx_request 2 回目)。次の時計・天気は 3704 + 1000 = 4704。天気なし(have=0)は pal=8・kind=none temp=none");
    CHECK_EQ(count_prefix("[ev] btn=tap src=touch\r\n"), 2);
    CHECK_EQ(g_n_req, 2);
    CHECK_EQ(g_req_src[0], WX_SRC_TOUCH); CHECK_EQ(g_req_src[1], WX_SRC_TOUCH);
    CHECK(find_line("[scr] to=stock pal=0 clk=0941 kind=sun temp=18 tick=3704\r\n", 0) >= 0);
    CHECK(find_line("[scr] to=clock pal=8 clk=0942 kind=none temp=none tick=4704\r\n", 0) >= 0);
    CHECK(has_scr(true, &c1, 4704u));
    CHECK(find_line("[scr] to=stock pal=0 clk=0942 kind=none temp=none tick=5204\r\n", 0) >= 0);

    TEST("6d 時刻が使えなくなる(tick 5300)と、株価の表示時間が過ぎても時計・天気画面へ出ない(最後の to=clock は 4704)");
    CHECK_EQ(count_prefix("[scr] to=clock"), 3);                         /* 1500・3504・4704 */
    CHECK_EQ(s_ui.screen, UI_SCR_STOCK);
    CHECK_EQ(g_app_dbg.scr, 0);
    CHECK_EQ(s_ui.cview.clock_min, 0xFFFF);
    CHECK_EQ(g_n_flip_pal, 6);                                            /* 1500・2000・3504・3704・4704・5204(画面が変わるたびにパレットが入れ替わる) */
    CHECK_EQ(g_n_pal_same, 0);
    CHECK_EQ(g_drops, 0);
    CHECK_EQ(count_prefix("[err] "), 0);
    (void)c2;
    TEST_REPORT("test_app_main(6d 結合: 製品 + clk_ui)");
}

/* ================================================================ 7a 夜間のバックライト(製品 + 窓の注入。docs/design.md §14.8 H-N2 の結合) */
#elif defined(T_NIGHT)

static nav_sample_t g_tp[3700];
static unsigned g_flips_100, g_flips_200, g_polls_100;

static void night_tick(uint32_t t)
{
    if (t == 50u)   { g_stk.clock_min = 720u; }                 /* 時刻が分かった(昼) */
    if (t == 100u)  { g_stk.clock_min = 1380u; g_flips_100 = g_flip_n; g_polls_100 = g_n_wx_poll; }   /* 23:00 = 窓に入る */
    if (t == 200u)  { g_flips_200 = g_flip_n; }
    if (t == 7000u) { g_stk.clock_min = 720u; }                 /* 窓が終わる */
}

int main(void)
{
    static const cmd_t cmds[] = { { 6800u, 'x' }, { 7100u, 'b' }, { 7200u, 'b' }, { 7300u, 'b' }, { 7400u, 's' } };
    stk_view_t v;
    unsigned i, nb_before;
    int a, b2;

    memset(&v, 0, sizeof v);
    v.net = STK_NET_ON; v.have_any = 1u; v.clock_min = (uint16_t)STK_NONE16; v.upd_min = 552u;
    v.row[0].have = 1u; v.row[0].price_x100 = 243300; v.row[0].diff_x100 = 5500; v.row[0].pct_bp = 231; v.row[0].mkt = STK_MKT_TRADING;
    g_stk = v;
    for (i = 0u; i < 3700u; i++) { g_tp[i].pressed = false; g_tp[i].points = 0u; g_tp[i].x = 0u; g_tp[i].y = 0u; }
    g_tp[200].pressed = true; g_tp[200].points = 1u; g_tp[201] = g_tp[200];       /* 消灯中の押下(2 標本 + 離し)= 起こすだけ */
    g_tp[3400] = g_tp[200]; g_tp[3401] = g_tp[200];                                /* 消灯に戻った直後の押下 = また起こすだけ */
    g_tp[3600] = g_tp[200]; g_tp[3601] = g_tp[200];                                /* 点灯中の押下 = 普通のタップ */
    g_touch_script = g_tp; g_touch_n = 3700u; g_touch_t0 = 0u;
    g_cmds = cmds; g_ncmds = sizeof cmds / sizeof cmds[0];
    g_on_tick = night_tick;
    memset(g_fb, 0xCC, sizeof g_fb);

    run(7600u);

    TEST("7a 起動: [bl] mode=on cause=boot が [perf] の後に 1 行。時刻が未同期の間(tick 0〜49)と昼(50〜99)は消えない");
    check_boot_lines();
    CHECK_EQ(find_line("[bl] mode=on cause=boot pct=100 clk=none tick=0\r\n", 0), find_line("[perf] n=", 0) + 1);   /* [perf] の次(wx_init の偽物は行を出さない)*/
    CHECK_EQ(find_line("[bl] ", 0), find_line("[bl] mode=on cause=boot", 0));
    a = find_line("[bl] mode=off cause=night pct=0 clk=2300 tick=100\r\n", 0);
    CHECK(a > 0);
    CHECK_EQ(g_bl_lv[0], 0);                                                   /* 最初の lcd_bl_set = 消灯(level 0) */

    TEST("7a 窓の始まり(tick 100): 消灯。取得(wx_poll)・描画(入れ替え)・時計は止めない");
    CHECK_EQ(g_flips_100 >= 1u, 1);
    CHECK(g_flip_n > g_flips_100);                                              /* 消灯後も表示は更新される(時刻の変化で描き直した) */
    CHECK_EQ(g_n_wx_poll, g_iter - 1u);
    CHECK(g_n_wx_poll > g_polls_100 + 1000u);

    TEST("7a 起こし: 消灯中の押下(tick 200・201)は [bl] mode=on cause=touch(tick 200)。[ev] も wx_request も出ない(飲み込まれた)");
    a = find_line("[bl] mode=on cause=touch pct=100 clk=2300 tick=200\r\n", 0);
    CHECK(a > 0);
    CHECK_EQ(g_bl_lv[1], LCD_BL_STEPS);
    CHECK(find_line("[ev] ", 0) > find_line("[bl] mode=on cause=touch pct=100 clk=2300 tick=3400", 0));   /* 最初の [ev] は 3400 の起こしより後 */

    TEST("7a 30 秒後(最後の押下 tick 201 + 3000 = tick 3201)に消灯 cause=timeout。3200 では点灯のまま");
    a = find_line("[bl] mode=off cause=timeout pct=0 clk=2300 tick=3201\r\n", 0);
    CHECK(a > 0);

    TEST("7a 消灯に戻った直後の押下(tick 3400)もまた起こすだけ(飲み込む)。点灯中の押下(tick 3600)は普通のタップ = [ev] btn=tap src=touch 1 行 + wx_request 1 回");
    CHECK(find_line("[bl] mode=on cause=touch pct=100 clk=2300 tick=3400\r\n", 0) > 0);
    CHECK_EQ(count_prefix("[ev] btn=tap src=touch\r\n"), 1);
    CHECK(find_line("[bl] mode=off cause=timeout pct=0 clk=2300 tick=6601\r\n", 0) > find_line("[ev] btn=tap src=touch\r\n", 0));   /* 3601 + 3000 = 6601 の 30 秒後 */

    TEST("7a UART x(tick 6800)は消灯中でも普通に効く(src=uart + wx_request)。起こさない([bl] は増えない)");
    CHECK_EQ(count_prefix("[ev] btn=tap src=uart\r\n"), 1);
    CHECK_EQ(g_n_req, 2);
    CHECK_EQ(g_req_src[0], WX_SRC_TOUCH);
    CHECK_EQ(g_req_src[1], WX_SRC_UART);

    TEST("7a 窓の終わり(tick 7000)で点灯 cause=day。b: 消灯(uart)→ 点灯(uart)→ 自動(昼 = 変わらず行なし)。s は cause=state");
    CHECK(find_line("[bl] mode=on cause=day pct=100 clk=1200 tick=7000\r\n", 0) > 0);
    CHECK(find_line("[bl] mode=off cause=uart pct=0 clk=1200 tick=7100\r\n", 0) > 0);
    CHECK(find_line("[bl] mode=on cause=uart pct=100 clk=1200 tick=7200\r\n", 0) > 0);
    CHECK(find_line("[bl] mode=on cause=state pct=100 clk=1200 tick=7400\r\n", 0) > 0);
    CHECK_EQ(count_prefix("[bl] "), 10);                                        /* boot, night, touch, timeout, touch, timeout, day, uart, uart, state */
    CHECK_EQ(g_n_bl, 8);                                                        /* [bl] のうち state・boot 以外 */
    b2 = 0; for (i = 0u; i < g_n_bl && i < sizeof g_bl_lv; i++) { if (g_bl_lv[i] != 0u && g_bl_lv[i] != LCD_BL_STEPS) { b2 = 1; } }
    CHECK_EQ(b2, 0);
    CHECK_EQ(g_bl_lv[0], 0); CHECK_EQ(g_bl_lv[1], LCD_BL_STEPS); CHECK_EQ(g_bl_lv[2], 0); CHECK_EQ(g_bl_lv[3], LCD_BL_STEPS);
    CHECK_EQ(g_bl_lv[4], 0); CHECK_EQ(g_bl_lv[5], LCD_BL_STEPS); CHECK_EQ(g_bl_lv[6], 0); CHECK_EQ(g_bl_lv[7], LCD_BL_STEPS);
    nb_before = 0u; (void)nb_before;
    CHECK_EQ(g_drops, 0);
    CHECK_EQ(count_prefix("[err] "), 0);
    TEST_REPORT("test_app_main(夜間のバックライト)");
}

/* ================================================================ 製品(APP_WX = 1) */
#elif APP_WX_PRODUCT

static stk_view_t view_ok(int32_t p0)
{
    stk_view_t v;
    memset(&v, 0, sizeof v);
    v.net = STK_NET_ON; v.have_any = 1u; v.clock_min = 555u; v.upd_min = 552u;
    v.row[0].have = 1u; v.row[0].mkt = STK_MKT_TRADING; v.row[0].price_x100 = p0;     v.row[0].diff_x100 = 5500;   v.row[0].pct_bp = 231;
    v.row[1].have = 1u; v.row[1].mkt = STK_MKT_TRADING; v.row[1].price_x100 = 5754123; v.row[1].diff_x100 = -13313; v.row[1].pct_bp = -23;
    v.row[2].have = 1u; v.row[2].mkt = STK_MKT_CLOSED;  v.row[2].price_x100 = 718278; v.row[2].diff_x100 = 0;      v.row[2].pct_bp = 0;
    return v;
}

static stk_view_t g_v_boot, g_v_a, g_v_b;
static unsigned g_flips_at[8];

static void on_tick(uint32_t t)
{
    if (t == 40u)  { g_stk = g_v_a; }            /* 取れた: 値が出る */
    if (t == 60u)  { g_stk = g_v_a; }            /* 同じ値: 描き直さない */
    if (t == 100u) { g_stk = g_v_b; }            /* 1 行だけ変わった */
    if ((t == 39u) || (t == 99u) || (t == 150u)) { g_flips_at[(t == 39u) ? 0 : ((t == 99u) ? 1 : 2)] = g_flip_n; }
}

int main(void)
{
    static const nav_sample_t DN = { true, 1u, 240u, 130u };
    static const nav_sample_t UPS = { false, 0u, 0u, 0u };
    nav_sample_t tp[5];
    static const cmd_t cmds[] = {
        { 300u, 'x' },                            /* 更新(タップと同じ) */
        { 310u, 'n' }, { 311u, 'p' }, { 312u, 'd' }, { 313u, '+' }, { 314u, '-' }, { 315u, 'X' },   /* 製品では全部無視 */
        { 320u, 's' }, { 330u, '?' }
    };
    unsigned i, n_ev;

    memset(&g_v_boot, 0, sizeof g_v_boot);
    g_v_boot.net = STK_NET_CONN; g_v_boot.busy = 1u; g_v_boot.clock_min = (uint16_t)STK_NONE16; g_v_boot.upd_min = (uint16_t)STK_NONE16;
    g_v_a = view_ok(243300);
    g_v_b = view_ok(243400);
    g_stk = g_v_boot;
    tp[0] = DN; tp[1] = DN; tp[2] = UPS; tp[3] = UPS; tp[4] = UPS;       /* tick 200,201 = 押下、202〜204 = 離し → 離しの確定(tick 204)で 1 回 */
    g_touch_script = tp; g_touch_n = 5u; g_touch_t0 = 200u;
    g_cmds = cmds; g_ncmds = sizeof cmds / sizeof cmds[0];
    g_on_tick = on_tick;
    memset(g_fb, 0xCC, sizeof g_fb);

    run(700u);

    TEST("起動: [boot] → [init] lcd / touch / timer / loop(この順)→ [perf]。面 0 を lcd_init の前に描く(起動中の画面)。wx_init は [perf] の後に 1 回");
    check_boot_lines();
    {
        stk_view_t def;
        ui_t u0;
        ui_init(&u0, 0u);
        def = u0.view;
        render_ref(&def);
        CHECK(same_left480(g_fb0_at_init, g_ref));                        /* lcd_init の時点の面 0 = ui_init の表示値の描画 */
    }
    CHECK_EQ(g_n_wx_init, 1);
    CHECK_EQ(g_lines_at_wx_init, (unsigned)find_line("[perf] n=", 0) + 1u);        /* [perf] の次に wx_init */
    CHECK_EQ(g_wx_init_ms, 0);
    CHECK_EQ(g_n_wx_poll, g_iter - 1u);                                   /* wx_poll は毎周回(最後の周回は longjmp で抜けた) */
    CHECK_EQ(g_wx_poll_ms, (s_tick - 0u) * 10u);                          /* 時刻 = tick × 10ms(wx_poll は tick の更新後の値) */

    TEST("表示: stk_ui の値が ui_render で描かれ、変わった時だけ裏面 → 入れ替え(面は 1,0,1,… の順)。同じ値では描き直さない");
    CHECK(g_flip_n >= 3u);
    CHECK_EQ(g_flips_at[0], 1);                                           /* tick 39 まで: 取得前の表示値(CONN・取得中)で 1 回 */
    CHECK_EQ(g_flips_at[1], 2);                                           /* tick 99 まで: g_v_a で +1(tick 60 の同じ値では増えない) */
    CHECK_EQ(g_flips_at[2], 3);                                           /* tick 150 まで: g_v_b で +1 */
    CHECK_EQ(g_flip_n, 3);
    for (i = 0u; i < g_flip_n; i++) { CHECK_EQ(g_flip_log[i], (i + 1u) & 1u); }
    CHECK(front_is(&g_v_b));
    CHECK(!front_is(&g_v_a));
    CHECK_EQ(g_ui_ms, g_tick_now * 10u);
    CHECK(g_app_dbg.frames >= 3u);

    TEST("タップ: touch(押下 2 標本・離し 3 標本)= [ev] btn=tap src=touch を 1 行 + wx_request(WX_SRC_TOUCH) 1 回。UART x = src=uart + WX_SRC_UART 1 回");
    CHECK_EQ(count_prefix("[ev] "), 2);
    CHECK_EQ(count_prefix("[ev] btn=tap src=touch\r\n"), 1);
    CHECK_EQ(count_prefix("[ev] btn=tap src=uart\r\n"), 1);
    CHECK(find_line("[ev] btn=tap src=touch\r\n", 0) < find_line("[ev] btn=tap src=uart\r\n", 0));
    CHECK_EQ(g_n_req, 2);
    CHECK_EQ(g_req_src[0], WX_SRC_TOUCH);
    CHECK_EQ(g_req_src[1], WX_SRC_UART);
    CHECK_EQ(count_prefix("[ui] "), 0);                                   /* 旧 UI の [ui] 行は無い */
    CHECK_EQ(count_prefix("[ev] img="), 0);
    CHECK_EQ(count_prefix("[ev] scr="), 0);
    CHECK_EQ(g_app_dbg.raw_points, 0);                                    /* 離した後は 0 */

    TEST("[scr](§13.9): 入れ替えを受け付けるたびに 1 行。既定の製品(時計・天気の値を渡さない 6b)は株価画面のまま = to=stock pal=0 clk=none。lcd_flip_pal は呼ばない");
    CHECK_EQ(count_prefix("[scr] "), g_flip_n);
    CHECK_EQ(count_prefix("[scr] to=stock pal=0 clk=none kind=none temp=none tick="), g_flip_n);
    CHECK_EQ(g_n_flip_pal, 0);
    CHECK_EQ(g_app_dbg.scr, 0);
    CHECK_EQ(s_ui.screen, UI_SCR_STOCK);
    CHECK_EQ(s_pal_shown, 0);
    CHECK_EQ(g_n_clk_ui, g_iter - 1u);                                    /* clk_ui は 10ms ごと(tick_step)。時刻が未同期(clk_ui が 0xFFFF)の間は 700 tick たっても切り替わらない */
    CHECK_EQ(g_clk_ui_ms, g_tick_now * 10u);
    CHECK_EQ(s_ui.cview.clock_min, 0xFFFF);

    TEST("UART: s = [st] + wx_log_state 1 回、? = 新しい [help]、n p d + - X は無視(行も更新要求も出ない)");
    CHECK_EQ(count_prefix("[st] "), 1);
    CHECK_EQ(g_n_wx_state, 1);
    CHECK_EQ(count_prefix("[help] s=state x=update ?=help\r\n"), 1);
    CHECK_EQ(count_prefix("[help] "), 1);
    n_ev = count_prefix("[ev] ");
    CHECK_EQ(n_ev, 2);
    CHECK_EQ(g_cmd_i, g_ncmds);                                           /* 文字は全部読まれた */

    TEST("7a 夜間のバックライト: 起動直後に [bl] mode=on cause=boot を 1 行([perf]・wx_init の後)。s で cause=state。昼の間は lcd_bl_set を呼ばない。APP_NIGHT = 0 では [bl] も lcd_bl_set も無い");
#if APP_NIGHT
    CHECK_EQ(count_prefix("[bl] "), 2);
    CHECK(find_line("[bl] mode=on cause=boot pct=100 clk=none tick=0\r\n", 0) > find_line("[perf] n=", 0));
    CHECK(find_line("[bl] mode=on cause=state pct=100 clk=0915 tick=320\r\n", 0) > 0);
#else
    CHECK_EQ(count_prefix("[bl] "), 0);
#endif
    CHECK_EQ(g_n_bl, 0);

    TEST("[hb] は 100 tick ごと(700 tick で 7 行)、[err] なし、drop なし");
    CHECK_EQ(count_prefix("[hb] "), 7);
    CHECK_EQ(count_prefix("[err] "), 0);
    CHECK_EQ(g_drops, 0);

    TEST_REPORT("test_app_main(製品)");
}

/* ================================================================ 時計・天気のプレビュー(APP_CLK_PREVIEW = 1: 3 秒ごと / 2: 本物の切り替え) */
#elif APP_CLK_PREVIEW

static bool snap_is_stock(unsigned n) { render_ref(&s_clk_stock); return same_left480(g_snap[n], g_ref); }
static bool snap_is_clock(unsigned n, const clk_view_t *c)
{
    static ui_t r;
    ui_init(&r, 0u);
    ui_set_clk(&r, c);
    ui_set_screen(&r, UI_SCR_CLOCK, 0u);
    memset(g_ref, 0xA5, sizeof g_ref);
    ui_render(&r, g_ref);
    return same_left480(g_snap[n], g_ref);
}

#if APP_CLK_PREVIEW == 1
int main(void)
{
    unsigned n, bad_snap = 0u, bad_pal = 0u, bad_scr = 0u;
    const unsigned flips = 2u * CLK_PREVIEW_N;                             /* 24。偶数番 = 株価、奇数番 = 時計・天気 C((番 - 1) / 2) */
    const unsigned ticks = flips * APP_PREVIEW_TICKS + 50u;

    memset(g_fb, 0xCC, sizeof g_fb);
    run(ticks);

    TEST("APP_CLK_PREVIEW = 1: 面 0 は株価(S1)。300 tick ごとに 株価 ↔ 時計・天気(C0〜C11 を順に)。毎回パレットが入れ替わる(lcd_flip_pal)");
    check_boot_lines();
    render_ref(&s_clk_stock);
    CHECK(same_left480(g_fb0_at_init, g_ref));
    CHECK_EQ(g_flip_n, flips);
    CHECK_EQ(g_n_flip_pal, flips);                                          /* どの入れ替えもパレットが変わる(間に株価か時計・天気が入る) */
    CHECK_EQ(g_n_pal_same, 0);                                              /* 表示中と同じパレットで lcd_flip_pal を呼ばない */
    for (n = 1u; n <= flips; n++) {
        bool clock = ((n & 1u) != 0u);
        const clk_view_t *c = &clk_preview_view[(n / 2u) % CLK_PREVIEW_N];
        if (g_flip_log[n - 1u] != (n & 1u)) { bad_snap++; }                 /* 面は 1,0,1,… */
        if (g_flip_pal_log[n - 1u] != ui_pal_ptr((uint8_t)(clock ? (1u + clk_view_kind(c)) : 0u))) { bad_pal++; }
        if (clock ? !snap_is_clock(n - 1u, c) : !snap_is_stock(n - 1u)) { bad_snap++; }
        if (!scr_line_is(n - 1u, clock, c, n * APP_PREVIEW_TICKS)) { bad_scr++; }
    }
    CHECK_EQ(bad_snap, 0);
    CHECK_EQ(bad_pal, 0);
    CHECK_EQ(bad_scr, 0);
    CHECK_EQ(count_prefix("[scr] "), flips);
    CHECK_EQ(g_front_pal, ui_pal);                                          /* 最後(24 番 = 偶数)は株価 */
    CHECK_EQ(s_pal_shown, 0);
    CHECK_EQ(g_app_dbg.scr, 0);
    CHECK(front_is(&s_clk_stock));
    CHECK_EQ(g_n_wx_init + g_n_wx_poll + g_n_req, 0);
    CHECK_EQ(g_drops, 0);
    CHECK_EQ(count_prefix("[err] "), 0);
    TEST_REPORT("test_app_main(時計・天気のプレビュー 1)");
}
#else
int main(void)
{
    static const cmd_t cmds[] = { { 1100u, 'x' } };                         /* 時計・天気画面(tick 1000〜1500)の途中でタップ */
    unsigned bad_snap = 0u, bad_scr = 0u, i;
    static const struct { uint32_t tick; bool clock; } want[] = {
        { 1000u, true }, { 1100u, false }, { 2099u, true }, { 2599u, false }, { 3599u, true }, { 4099u, false }   /* x の t_scr は直前の ui_update の時刻(10ms 前)= 設計 §13.3。tap から 999 tick 後 */
    };
    const unsigned nflip = sizeof want / sizeof want[0];

    g_cmds = cmds; g_ncmds = 1u;
    memset(g_fb, 0xCC, sizeof g_fb);
    run(4200u);

    TEST("APP_CLK_PREVIEW = 2: 固定の値(C0)を本物の切り替えで。株価 10 秒 → 時計・天気 5 秒 → …。時計・天気でのタップ(x)は株価へ戻し、次の時計・天気までが 10 秒");
    check_boot_lines();
    CHECK_EQ(g_flip_n, nflip);
    CHECK_EQ(g_n_flip_pal, nflip);
    CHECK_EQ(g_n_pal_same, 0);
    for (i = 0u; i < nflip; i++) {
        if (g_flip_log[i] != ((i + 1u) & 1u)) { bad_snap++; }
        if (want[i].clock ? !snap_is_clock(i, &clk_preview_view[0]) : !snap_is_stock(i)) { bad_snap++; }
        if (g_flip_pal_log[i] != ui_pal_ptr(want[i].clock ? (uint8_t)(1u + CLK_SUN) : 0u)) { bad_snap++; }
        if (!scr_line_is(i, want[i].clock, &clk_preview_view[0], want[i].tick)) { bad_scr++; }
    }
    CHECK_EQ(bad_snap, 0);
    CHECK_EQ(bad_scr, 0);
    if (bad_scr != 0u) { for (i = 0u; i < g_nlines; i++) { if (strncmp(g_lines[i], "[scr] ", 6) == 0) { printf("  %s", g_lines[i]); } } }
    CHECK_EQ(count_prefix("[scr] "), nflip);
    CHECK_EQ(count_prefix("[ev] btn=tap src=uart\r\n"), 1);
    CHECK_EQ(g_app_dbg.scr, 0);
    CHECK_EQ(g_drops, 0);
    TEST_REPORT("test_app_main(時計・天気のプレビュー 2)");
}
#endif

/* ================================================================ 時計・天気の値を注入(既定の製品に近い。取得なし + 本物の ui_screen_step)。同じパレットの再描画は lcd_flip */
#elif defined(T_CLK_INJECT)

static clk_view_t g_inj(uint16_t m, uint8_t kind)
{
    clk_view_t c;
    memset(&c, 0, sizeof c);
    c.clock_min = m; c.kind = kind; c.have = 1u; c.temp = 18; c.mon = 11u; c.day = 27u; c.wday = 5u;
    return c;
}

static void inject_tick(uint32_t t)
{
    clk_view_t c = g_inj(581u, CLK_SUN);

    if (t == 100u)  { ui_set_clk(&s_ui, &c); }                         /* 時刻が分かった(株価画面にいる間は見えない) */
    if (t == 1200u) { c = g_inj(582u, CLK_SUN);  ui_set_clk(&s_ui, &c); }    /* 分が変わった(時計・天気画面。同じパレット) */
    if (t == 1300u) { c = g_inj(582u, CLK_RAIN); ui_set_clk(&s_ui, &c); }    /* 天気が変わった(別のパレット) */
    if (t == 1600u) { c = g_inj(583u, CLK_RAIN); ui_set_clk(&s_ui, &c); }    /* 株価画面にいる間の変化(再描画しない) */
}

int main(void)
{
    const clk_view_t c0 = g_inj(581u, CLK_SUN), c1 = g_inj(582u, CLK_SUN), c2 = g_inj(582u, CLK_RAIN);

    g_on_tick = inject_tick;
    memset(g_fb, 0xCC, sizeof g_fb);
    run(2000u);

    TEST("時計・天気の値を渡すと(6d の結合の前段): 時刻が分かるまで切り替わらず、10 秒後に時計・天気へ(lcd_flip_pal)、同じパレットの再描画は lcd_flip、天気が変わればパレット入れ替え、5 秒後に株価へ");
    check_boot_lines();
    CHECK_EQ(g_flip_n, 4);
    CHECK_EQ(g_n_flip_pal, 3);                                              /* 1000(株価 → 時計・天気)、1300(天気が変わる)、1500(→ 株価) */
    CHECK_EQ(g_n_pal_same, 0);
    CHECK(g_flip_pal_log[0] == ui_pal_ptr(1u + CLK_SUN));
    CHECK(g_flip_pal_log[1] == NULL);                                       /* 1200: 分だけ変わった = 同じパレット = lcd_flip */
    CHECK(g_flip_pal_log[2] == ui_pal_ptr(1u + CLK_RAIN));
    CHECK(g_flip_pal_log[3] == ui_pal_ptr(0u));
    CHECK(scr_line_is(0u, true, &c0, 1000u));
    CHECK(scr_line_is(1u, true, &c1, 1200u));
    CHECK(scr_line_is(2u, true, &c2, 1300u));
    CHECK(scr_line_is(3u, false, &c2, 1500u));                              /* 株価画面でも clk / kind / temp が付く(その時の時計・天気の値) */
    CHECK_EQ(count_prefix("[scr] "), 4);
    CHECK_EQ(g_front_pal, ui_pal);
    CHECK_EQ(s_pal_shown, 0);
    CHECK_EQ(g_app_dbg.scr, 0);
    CHECK_EQ(g_drops, 0);
    TEST_REPORT("test_app_main(時計・天気の値を注入)");
}

/* ================================================================ 取得なし(APP_WX = 0。プレビューでもない) */
#elif !APP_STK_PREVIEW

int main(void)
{
    static const cmd_t cmds[] = { { 20u, 'x' }, { 30u, 's' } };
    stk_view_t def;
    ui_t u0;

    ui_init(&u0, 0u);
    def = u0.view;
    g_cmds = cmds; g_ncmds = 2u;
    memset(g_fb, 0xCC, sizeof g_fb);
    run(100u);

    TEST("APP_WX = 0: 取得を呼ばない。表示は起動中・オフラインのまま(入れ替えなし)。x はタップと同じ [ev] だけ");
    check_boot_lines();
    CHECK_EQ(g_flip_n, 0);
    render_ref(&def);
    CHECK(same_left480(g_fb0_at_init, g_ref));
    CHECK_EQ(count_prefix("[ev] btn=tap src=uart\r\n"), 1);
    CHECK_EQ(count_prefix("[st] "), 1);
    CHECK_EQ(g_drops, 0);
    CHECK_EQ(g_n_wx_init + g_n_wx_poll + g_n_req + g_n_wx_state, 0);
    TEST_REPORT("test_app_main(取得なし)");
}

/* ================================================================ プレビュー(APP_STK_PREVIEW = 1: 8 状態を 3 秒ごと) */
#else

int main(void)
{
    unsigned i;
    const unsigned ticks = (UI_PREVIEW_N * APP_PREVIEW_TICKS) + 50u;

    memset(g_fb, 0xCC, sizeof g_fb);
    run(ticks);

    TEST("APP_STK_PREVIEW = 1: 面 0 は ui_preview_view[0]。300 tick ごとに次の状態へ入れ替え(8 状態で 1 周して S0 に戻る)。取得は動かない");
    check_boot_lines();
    render_ref(&ui_preview_view[0]);
    CHECK(same_left480(g_fb0_at_init, g_ref));
    CHECK_EQ(g_flip_n, UI_PREVIEW_N);                                     /* S1〜S7 と S0 */
    for (i = 0u; i < g_flip_n; i++) { CHECK_EQ(g_flip_log[i], (i + 1u) & 1u); }
    CHECK(front_is(&ui_preview_view[0]));
    CHECK(!front_is(&ui_preview_view[1]));
    CHECK_EQ(count_prefix("[scr] to=stock pal=0 clk=none kind=none temp=none tick="), g_flip_n);   /* 株価だけ(時計・天気へは切り替わらない) */
    CHECK_EQ(g_n_flip_pal, 0);
    CHECK_EQ(g_n_wx_init + g_n_wx_poll + g_n_req, 0);
    CHECK_EQ(g_drops, 0);
    TEST_REPORT("test_app_main(プレビュー)");
}
#endif
