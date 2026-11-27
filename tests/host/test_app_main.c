/* test_app_main.c - src/app/app_main.c(メインループ。製品デモ v2)を、偽物の周辺(r_sci_rx / CMT / lcd / touch)と
 * 本物の ui_logic / ui_render / gfx / 生成データで動かす結合試験。
 *   - app_main.c と app_debug.c をそのまま #include。app_main() の無限ループは、ループ先頭の app_log_poll() を
 *     マクロで差し替えたフック(loop_hook)から longjmp で抜ける。フックが「10ms の tick」「送信完了」「受信文字」
 *     「垂直同期(入れ替えの反映)」を模擬する。
 *   - 試験できるのは論理(出力する行・順序・間引き・コマンド処理・描画と入れ替えの手順)。実機の割り込み・実時間・
 *     GLCDC の実際の挙動・LCD表示は試験できない。
 * 作成: 2026-11-20 テスト担当 / 2026-11-21 v2(製品デモ)に合わせて書き直し */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdlib.h>
#include "test_framework.h"

#include "../../src/app/app_debug.c"
#include "../stubs/fake_sci.h"

static void loop_hook(void);
#define app_log_poll() (loop_hook(), app_log_poll())
#include "../../src/app/app_main.c"
#undef app_log_poll

/* ------------------------------------------------------------ シナリオ設定 */
typedef struct { uint32_t tick; uint8_t byte; } rx_ev_t;
typedef struct { uint32_t t0, t1; uint16_t x, y; uint8_t pts; } press_t;
typedef struct { uint32_t tick; uint32_t ev; } cbev_t;
typedef struct { uint32_t tick; uint32_t code, detail; } seterr_t;

typedef struct {
    bool     lcd_ok, touch_init_ok, cmt_ok, touch_read_ok, sci_open_ok;
    unsigned drain_iters;            /* 1 回の Send が終わるまでのループ周回数(BUSY の長さ) */
    unsigned iters_per_tick;         /* 1 tick あたりのループ周回数 */
    unsigned vsync_iters;            /* lcd_flip を受け付けてから表示面が変わるまでのループ周回数 */
    uint32_t stop_tick;              /* 生成する tick の数(1 tick = 10ms) */
    uint32_t start_tick;             /* s_tick の初期値(折り返し試験) */
    uint32_t jump_at, jump_by;       /* その tick に達したとき +jump_by(メインが詰まった模擬) */
    uint8_t  found;                  /* touch_init が g_app_dbg.found_count に入れる値 */
    bool     flip_fail;              /* lcd_flip が FAIL(+ APP_ERR_LCD_FLIP) を返す */
    unsigned flip_busy_left;         /* 最初の n 回の lcd_flip は BUSY を返す */
    uint32_t uf_per_poll;            /* lcd_underflow_poll の戻り値 */
    uint32_t uf_boot_only;           /* 最初の1回(起動時)だけ返す値。以後は uf_per_poll */
    bool     check_pending;          /* 反映待ちの面が書き換えられていないか毎周回確認(重い) */
    const rx_ev_t   *rx;   unsigned n_rx;
    const press_t   *press; unsigned n_press;
    const cbev_t    *cb;   unsigned n_cb;      /* コールバック(割り込み)で起こす受信異常 */
    const seterr_t  *se;   unsigned n_se;      /* touch_read 内で app_debug_set_err する(I2C異常の模擬) */
    uint32_t stop_iter_when_no_timer;
} cfg_t;

static cfg_t   C;
static void  (*g_send_hook_user)(const uint8_t *p, uint16_t len);   /* trace_ui.c(ログ生成)が差し込む。通常の試験では NULL */
static jmp_buf g_jb;
static unsigned g_iter, g_sub, g_busy_iters, g_ticks_gen;
static bool    g_timer_on;
static unsigned g_tr_calls;
static size_t   g_tx_at_lcd_init, g_tx_at_touch_init;
static unsigned g_trace_n;
static char     g_trace[4096];
static unsigned g_pushed_at_tr[4096];       /* tick ごとの「その tick の touch_read 時点までに積まれた行数」 */
static bool     g_tr_seen[4096];
static int      g_lcd_init_calls, g_touch_init_calls;

/* 表示面の模擬 */
static uint8_t  g_fb[2][512 * 272];          /* CLUT8: 行幅 512 */
static bool     g_ven;                       /* 反映待ち */
static unsigned g_ven_left;
static uint8_t  g_pend_surf;
static uint32_t g_pend_hash;
static int      g_displayed;                 /* 今表示されている面(-1 = まだ無い) */
static unsigned g_flip_calls, g_flip_ok, g_flip_while_busy, g_pend_violation, g_uf_polls;
static uint8_t  g_flip_seq[4096];
static uint32_t g_flip_tick[4096];
static uint32_t g_shown_hash[4096];          /* 反映された面の内容のハッシュ(反映ごと) */
static unsigned g_shown_n;
static bool     g_fb0_drawn_at_init;

static uint32_t hash_fb(const uint8_t *p)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0u; i < 512u * 272u; i++) { h = (h ^ p[i]) * 16777619u; }
    return h;
}
static void trace(const char *s)
{
    size_t n = strlen(s);
    if (g_trace_n + n < sizeof(g_trace)) { memcpy(&g_trace[g_trace_n], s, n); g_trace_n += (unsigned)n; g_trace[g_trace_n] = '\0'; }
}
static unsigned pushed_total(void) { return g_fs.nsend + (unsigned)applog_q_count(&s_q) + app_log_drops(); }

#if APP_WX_PRODUCT
/* ------------------------------------------------------------ 偽物: wifi_at.c の製品 API(本物は S26 で test_wifi_at.c が試験する)。呼ばれ方だけを記録する */
static unsigned g_wx_init_n, g_wx_poll_n, g_wx_req_n, g_wx_state_n, g_wx_ui_n;
static uint32_t g_wx_init_ms, g_wx_poll_last_ms, g_wx_poll_back;
static size_t   g_tx_at_wx_init;
static wx_src_t g_wx_req_src[16];
static uint32_t g_wx_req_ms[16];
static wx_req_t g_wx_req_ret;
static ui_wx_t  g_wx_val = { .clk_ext = 1u, .upd_min = 0xFFFFu, .clock_min = 0xFFFFu };   /* wx_ui が返す値(既定 = 製品の OFFLINE・未同期) */
static char     g_wx_state_txt[128];
void wx_init(uint32_t now_ms)
{
    g_wx_init_n++; g_wx_init_ms = now_ms; g_tx_at_wx_init = pushed_total(); trace("WXINIT;");   /* その時までに積んだ行数 */
}
void wx_poll(uint32_t now_ms)
{
    if (g_wx_poll_n > 0u && (uint32_t)(now_ms - g_wx_poll_last_ms) >= 0x80000000u) { g_wx_poll_back++; }
    g_wx_poll_n++; g_wx_poll_last_ms = now_ms;
}
wx_req_t wx_request(wx_src_t src, uint32_t now_ms)
{
    if (g_wx_req_n < 16u) { g_wx_req_src[g_wx_req_n] = src; g_wx_req_ms[g_wx_req_n] = now_ms; }
    g_wx_req_n++;
    return g_wx_req_ret;
}
void wx_ui(ui_wx_t *out, uint32_t now_ms)
{
    (void)now_ms; g_wx_ui_n++; *out = g_wx_val;
}
void wx_log_state(uint32_t now_ms)
{
    size_t n;
    g_wx_state_n++;
    n = (size_t)snprintf(g_wx_state_txt, sizeof g_wx_state_txt, "[wx] net=on busy=0 have=0 temp10=0 kind=none age=%lu clk=none date=none wd=0 sync=0\r\n", (unsigned long)now_ms);
    app_log_line(g_wx_state_txt, n);
}
#endif

/* ------------------------------------------------------------ 偽物 */
bool lcd_init(void)
{
    size_t i;
    g_lcd_init_calls++;
    trace("LCDINIT;");
    g_tx_at_lcd_init = g_fs.tx_len;
    g_fb0_drawn_at_init = false;
    for (i = 0u; i < 512u * 272u; i++) { if (g_fb[0][i] != 0u) { g_fb0_drawn_at_init = true; break; } }
    fake_sci_tx_done();                       /* 実機では lcd_init の間に TX 割り込みが送り終える */
    if (!C.lcd_ok) { app_debug_set_err(APP_ERR_LCD_OPEN, 3u); }
    return C.lcd_ok;
}
uint8_t *lcd_fb(uint8_t i) { return g_fb[(i == 0u) ? 0 : 1]; }
lcd_flip_t lcd_flip(uint8_t i)
{
    g_flip_calls++;
    if (g_ven) { g_flip_while_busy++; return LCD_FLIP_BUSY; }
    if (C.flip_busy_left > 0u) { C.flip_busy_left--; return LCD_FLIP_BUSY; }
    if (C.flip_fail) { app_debug_set_err(APP_ERR_LCD_FLIP, 7u); return LCD_FLIP_FAIL; }
    g_ven = true; g_ven_left = C.vsync_iters;
    g_pend_surf = i; g_pend_hash = hash_fb(g_fb[i & 1u]);
    if (g_flip_ok < 4096u) { g_flip_seq[g_flip_ok] = i; g_flip_tick[g_flip_ok] = s_tick; }
    g_flip_ok++;
    return LCD_FLIP_OK;
}
bool lcd_flip_busy(void) { return g_ven; }
uint32_t lcd_underflow_poll(void) { g_uf_polls++; return (g_uf_polls == 1u) ? (C.uf_boot_only + C.uf_per_poll) : C.uf_per_poll; }
bool touch_init(void)
{
    g_touch_init_calls++;
    trace("TOUCHINIT;");
    g_tx_at_touch_init = g_fs.tx_len;
    fake_sci_tx_done();
    g_app_dbg.found_count = C.found;
    if (C.found > 0u) { g_app_dbg.found_addr[0] = 0x38u; g_app_dbg.found_addr[1] = 0x1Au; }
    if (!C.touch_init_ok) { app_debug_set_err(APP_ERR_TOUCH_OPEN, 5u); }
    return C.touch_init_ok;
}
bool touch_read(nav_sample_t *out)
{
    unsigned k;
    uint32_t t = s_tick;
    g_tr_calls++;
    if (t < 4096u) { g_pushed_at_tr[t] = pushed_total(); g_tr_seen[t] = true; }
    for (k = 0u; k < C.n_se; k++) { if (C.se[k].tick == t) { app_debug_set_err(C.se[k].code, C.se[k].detail); } }
    if (!C.touch_read_ok) { return false; }
    out->pressed = false; out->points = 0u; out->x = 0u; out->y = 0u;
    for (k = 0u; k < C.n_press; k++) {
        if ((t >= C.press[k].t0) && (t <= C.press[k].t1)) {
            out->pressed = true; out->points = C.press[k].pts; out->x = C.press[k].x; out->y = C.press[k].y;
        }
    }
    return true;
}
bool R_CMT_CreatePeriodic(uint32_t frequency_hz, void (*callback)(void *pdata), uint32_t *channel)
{
    (void)callback;
    CHECK_EQ(frequency_hz, 100);
    *channel = 0u;
    if (C.cmt_ok) { g_timer_on = true; }
    return C.cmt_ok;
}

/* ループ先頭ごとに呼ばれる: 送信完了・垂直同期・tick・受信文字・コールバックを模擬し、終了条件で longjmp */
static void loop_hook(void)
{
    unsigned k;
    g_iter++;
    if (!g_fs.tx_idle) { g_busy_iters++; if (g_busy_iters >= C.drain_iters) { fake_sci_tx_done(); g_busy_iters = 0u; } }
    else { g_busy_iters = 0u; }
    if (g_ven) {                                           /* 垂直同期の模擬 */
        if (C.check_pending && (hash_fb(g_fb[g_pend_surf & 1u]) != g_pend_hash)) { g_pend_violation++; }
        if (g_ven_left > 0u) { g_ven_left--; }
        if (g_ven_left == 0u) {
            g_ven = false; g_displayed = (int)(g_pend_surf & 1u);
            if (g_shown_n < 4096u) { g_shown_hash[g_shown_n] = g_pend_hash; }
            g_shown_n++;
        }
    }
    if (!g_timer_on) {
        if (g_iter > C.stop_iter_when_no_timer) { longjmp(g_jb, 1); }
        return;
    }
    g_sub++;
    if (g_sub >= C.iters_per_tick) {
        g_sub = 0u;
        tick_callback(NULL);
        g_ticks_gen++;
        if (g_ticks_gen > C.stop_tick) { longjmp(g_jb, 1); }   /* stop_tick = 生成する tick 数(その tick を処理し終えてから止まる) */
        if ((C.jump_by != 0u) && (s_tick == C.jump_at)) { s_tick += C.jump_by; }
        for (k = 0u; k < C.n_rx; k++) { if (C.rx[k].tick == s_tick) { fake_sci_rx_put(C.rx[k].byte); } }
        for (k = 0u; k < C.n_cb; k++) {
            if (C.cb[k].tick == s_tick) { sci_cb_args_t a; memset(&a, 0, sizeof(a)); a.event = (sci_cb_evt_t)C.cb[k].ev; log_cb(&a); }
        }
    }
}

/* ------------------------------------------------------------ 実行と出力の解析 */
static void run_scenario(void)
{
    fake_sci_reset();
    g_fs.send_hook = g_send_hook_user;
    g_fs.auto_idle = false;
    if (!C.sci_open_ok) { g_fs.open_ret = SCI_ERR_BAD_CHAN; }
    memset((void *)&g_app_dbg, 0, sizeof(g_app_dbg));
    s_log_tried = false; s_log_ready = false; s_log_hdl = NULL;
    applog_q_init(&s_q);
    s_rx_ev_val = 0u; s_rx_ev_seq = 0u; s_rx_ev_seen = 0u;
    s_tick = C.start_tick;
    s_err_code = 0u; s_err_detail = 0u; s_err_tick = 0u; s_err_sent = false; s_rxerr_tick = 0u; s_rxerr_sent = false;
    s_flip_pending = false; s_draw_ok = false; s_back = 0u;
#if APP_WX_PRODUCT
    g_wx_init_n = 0u; g_wx_poll_n = 0u; g_wx_req_n = 0u; g_wx_state_n = 0u; g_wx_ui_n = 0u; g_wx_poll_back = 0u; g_wx_poll_last_ms = 0u; g_tx_at_wx_init = 0u;
#endif
    memset(&s_ui, 0, sizeof(s_ui));
    g_iter = 0u; g_ticks_gen = 0u; g_sub = 0u; g_busy_iters = 0u; g_timer_on = false;
    g_tr_calls = 0u; g_tx_at_lcd_init = 0u; g_tx_at_touch_init = 0u; g_trace_n = 0u; g_trace[0] = '\0';
    memset(g_pushed_at_tr, 0, sizeof(g_pushed_at_tr)); memset(g_tr_seen, 0, sizeof(g_tr_seen));
    g_lcd_init_calls = 0; g_touch_init_calls = 0;
    memset(g_fb, 0, sizeof(g_fb));
    g_ven = false; g_ven_left = 0u; g_displayed = -1; g_pend_surf = 0u; g_pend_hash = 0u;
    g_flip_calls = 0u; g_flip_ok = 0u; g_flip_while_busy = 0u; g_pend_violation = 0u; g_uf_polls = 0u; g_shown_n = 0u;
    g_fb0_drawn_at_init = false;
    if (setjmp(g_jb) == 0) { app_main(); }
    g_fs.tx[(g_fs.tx_len < FAKE_LOG_MAX) ? g_fs.tx_len : FAKE_LOG_MAX - 1u] = 0u;
}

static void cfg_default(void)
{
    memset(&C, 0, sizeof(C));
    C.lcd_ok = true; C.touch_init_ok = true; C.cmt_ok = true; C.touch_read_ok = true; C.sci_open_ok = true;
    C.drain_iters = 1u; C.iters_per_tick = 3u; C.vsync_iters = 5u; C.stop_tick = 1000u;
    C.stop_iter_when_no_timer = 200u;
}

/* 受信全体を行に分ける。行は必ず \r\n 終わり、途中切れは不合格 */
#define MAXL 8192
static char     *g_ln[MAXL];
static unsigned  g_nl;
static int       g_split_ok;
static char      g_buf[FAKE_LOG_MAX + 1];
static void split_lines(void)
{
    size_t i = 0u, start = 0u;
    memcpy(g_buf, g_fs.tx, g_fs.tx_len); g_buf[g_fs.tx_len] = '\0';
    g_nl = 0u; g_split_ok = 1;
    while (i < g_fs.tx_len) {
        if ((g_buf[i] == '\r') && ((i + 1u) < g_fs.tx_len) && (g_buf[i + 1u] == '\n')) {
            g_buf[i] = '\0';
            if (g_nl < MAXL) { g_ln[g_nl++] = &g_buf[start]; }
            i += 2u; start = i;
        } else {
            unsigned char c = (unsigned char)g_buf[i];
            if ((c < 0x20u) || (c > 0x7Eu)) { g_split_ok = 0; }
            i++;
        }
    }
    if (start != g_fs.tx_len) { g_split_ok = 0; }           /* 末尾が \r\n で終わっていない */
}
static unsigned count_pfx(const char *pfx)
{
    unsigned k, n = 0u;
    for (k = 0u; k < g_nl; k++) { if (strncmp(g_ln[k], pfx, strlen(pfx)) == 0) { n++; } }
    return n;
}
static const char *nth_pfx(const char *pfx, unsigned nth)
{
    unsigned k;
    for (k = 0u; k < g_nl; k++) { if (strncmp(g_ln[k], pfx, strlen(pfx)) == 0) { if (nth == 0u) { return g_ln[k]; } nth--; } }
    return NULL;
}
static int find_idx(const char *pfx, unsigned nth)
{
    unsigned k;
    for (k = 0u; k < g_nl; k++) { if (strncmp(g_ln[k], pfx, strlen(pfx)) == 0) { if (nth == 0u) { return (int)k; } nth--; } }
    return -1;
}
static uint32_t field(const char *line, const char *key)
{
    const char *p = (line == NULL) ? NULL : strstr(line, key);
    return (p == NULL) ? 0xDEADBEEFu : (uint32_t)strtoul(p + strlen(key), NULL, 10);
}

int main(void)
{
    unsigned k;

    /* ================================================================ M1 起動順序 */
    TEST("M1 起動: [boot] → 面0に描画 → LCD初期化 → [init lcd] → タッチ初期化 → [init touch] → [init timer] → [init loop] → [perf] → [hb]");
    cfg_default();
    C.stop_tick = 1000u;
    run_scenario();
    split_lines();
    CHECK(g_split_ok);
    CHECK_EQ(g_lcd_init_calls, 1);
    CHECK_EQ(g_touch_init_calls, 1);
    CHECK(g_nl >= 7u);
    CHECK(strncmp(g_ln[0], "[boot] fw=rx72n_gcc build=", 26) == 0);
    CHECK(strstr(g_ln[0], " ch=2 baud=115200") != NULL);
    CHECK(strchr(g_ln[0] + 26, ' ') == strstr(g_ln[0], " ch=2"));       /* build 値に空白が無い */
    CHECK_EQ(strlen(g_ln[0]) + 2u, g_tx_at_lcd_init);                    /* lcd_init の時点で [boot] だけが出ている */
    CHECK(g_fb0_drawn_at_init);                                          /* lcd_init の前に面0へ描いてある(v2 設計 §7.5) */
    CHECK(strcmp(g_ln[1], "[init] stage=lcd ok=1 err=0 detail=0") == 0);
    CHECK_EQ(strlen(g_ln[0]) + 2u + strlen(g_ln[1]) + 2u, g_tx_at_touch_init);   /* touch_init の時点で boot+lcd */
    CHECK(strcmp(g_ln[2], "[init] stage=touch ok=1 err=0 detail=0") == 0);
    CHECK(strcmp(g_ln[3], "[init] stage=timer ok=1 err=0 detail=0") == 0);
    CHECK(strcmp(g_ln[4], "[init] stage=loop ok=1 err=0 detail=0") == 0);
    CHECK(strncmp(g_ln[5], "[perf] n=50 copy_us=", 20) == 0);             /* loop の後に1本。テストでは tick が進まないので 0 */
    CHECK(strcmp(g_ln[5], "[perf] n=50 copy_us=0 home_us=0 slide_us=0") == 0);
    CHECK(strncmp(g_ln[6], "[hb] tick=100 ", 14) == 0);
    CHECK(strncmp(g_trace, "LCDINIT;TOUCHINIT;", 18) == 0);
    CHECK_EQ(count_pfx("[boot]"), 1);
    CHECK_EQ(count_pfx("[init]"), 4);
    CHECK_EQ(count_pfx("[perf]"), 1);
    CHECK_EQ(g_fs.pinset_at_open, 1);                                    /* PinSet が Open より前 */
    CHECK_EQ(g_fs.open_calls, 1);

    /* ================================================================ M2 ハートビート */
    TEST("M2 [hb]: 1000 tick で 10 本、tick=100,200,...,1000、drop=0、scr=0 demo=0 uf=0、全行が整形済み");
    CHECK_EQ(count_pfx("[hb] "), 10);
    for (k = 0u; k < 10u; k++) {
        const char *h = nth_pfx("[hb] ", k);
        CHECK(h != NULL);
        if (h != NULL) {
            CHECK_EQ(field(h, "tick="), (k + 1u) * 100u);
            CHECK_EQ(field(h, "drop="), 0);
            CHECK_EQ(field(h, "scr="), 0);
            CHECK_EQ(field(h, "demo="), 0);
            CHECK_EQ(field(h, "uf="), 0);
            CHECK_EQ(field(h, "err="), 0);
        }
    }
    /* frm=1: [perf] の後に画面を作り直すので、最初に1回描いて入れ替える。以後は静止なので増えない */
    CHECK(strcmp(nth_pfx("[hb] ", 0), "[hb] tick=100 scr=0 demo=0 frm=1 uf=0 err=0 detail=0 pts=0 x=0 y=0 i2c_fail=0 streak=0 found=0 drop=0") == 0);
    CHECK_EQ(field(nth_pfx("[hb] ", 9), "frm="), 1);
    CHECK_EQ(count_pfx("[err]"), 0);
    CHECK_EQ(app_log_drops(), 0);
    CHECK_EQ(g_flip_ok, 1);                                              /* 静止中は描かない */
    CHECK_EQ(g_flip_while_busy, 0);

    /* ================================================================ M3 hb が touch_read より前 */
    TEST("M3 [hb] はその tick の touch_read より前にキューへ積まれる");
    CHECK(g_tr_seen[100]);
    CHECK(g_tr_seen[99]);
    CHECK(g_pushed_at_tr[100] > g_pushed_at_tr[99]);                    /* tick 100 の touch_read 時点で hb が積まれている */
    CHECK_EQ(g_pushed_at_tr[99], g_pushed_at_tr[98]);                   /* 他の tick では増えない */
    CHECK_EQ(g_tr_calls, 1000);                                         /* 毎 tick 1 回 touch_read */

    /* ================================================================ M4 touch_init 失敗・touch_read 失敗でも hb */
    TEST("M4 touch_init 失敗: [init touch ok=0 err=3 detail=5] と [err]。hb は止まらず、UARTコマンドも効く");
    cfg_default();
    C.touch_init_ok = false;
    {
        static const rx_ev_t rx[] = { {20u, 'n'}, {80u, 's'} };
        C.rx = rx; C.n_rx = 2u;
    }
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    CHECK(strcmp(g_ln[2], "[init] stage=touch ok=0 err=3 detail=5") == 0);
    CHECK_EQ(count_pfx("[hb] "), 10);
    CHECK_EQ(g_tr_calls, 0);                                            /* touch_read を呼ばない */
    CHECK_EQ(count_pfx("[ev] scr=fill from=home src=uart"), 1);         /* UART は効く */
    CHECK_EQ(count_pfx("[st] "), 1);
    CHECK(count_pfx("[err] code=3 detail=5") == 1);                     /* (A) の変化検出 */
    CHECK_EQ(field(nth_pfx("[hb] ", 1), "scr="), 1);

    TEST("M5 touch_read が常に失敗: hb は毎秒出る。ui_update は標本なしで動き続ける(デモが進む)");
    cfg_default();
    C.touch_read_ok = false;
    { static const rx_ev_t rx[] = { {10u, 'd'} }; C.rx = rx; C.n_rx = 1u; }
    C.stop_tick = 200u;
    run_scenario(); split_lines();
    CHECK_EQ(count_pfx("[hb] "), 2);
    CHECK_EQ(g_tr_calls, 200);
    CHECK_EQ(count_pfx("[ev] btn=h_up st=down src=demo"), 2);           /* 0.8 秒と 1.5 秒の仮想タッチ */
    CHECK_EQ(count_pfx("[ev] set=43 src=demo"), 1);

    /* ================================================================ M6 コマンド */
    TEST("M6 n/p: 画面の順送り home→fill→reheat→usage→standby→home、p は逆送り。表示面が切り替わる。[ev] src=uart");
    cfg_default();
    {
        static rx_ev_t rx[10];
        for (k = 0u; k < 10u; k++) { rx[k].tick = 10u + 60u * k; rx[k].byte = (k < 5u) ? 'n' : 'p'; }
        C.rx = rx; C.n_rx = 10u;
    }
    C.stop_tick = 700u;
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    CHECK_EQ(count_pfx("[ev] "), 10);
    {
        static const char *exp[10] = {
            "[ev] scr=fill from=home src=uart", "[ev] scr=reheat from=fill src=uart", "[ev] scr=usage from=reheat src=uart",
            "[ev] scr=standby from=usage src=uart", "[ev] scr=home from=standby src=uart",
            "[ev] scr=standby from=home src=uart", "[ev] scr=usage from=standby src=uart", "[ev] scr=reheat from=usage src=uart",
            "[ev] scr=fill from=reheat src=uart", "[ev] scr=home from=fill src=uart"
        };
        for (k = 0u; k < 10u; k++) {
            const char *e = nth_pfx("[ev] ", k);
            CHECK(e != NULL && strcmp(e, exp[k]) == 0);
        }
    }
    CHECK_EQ(g_app_dbg.scr, 0);
    CHECK_EQ(field(nth_pfx("[hb] ", 6), "scr="), 0);
    CHECK(g_shown_n >= 10u);                                            /* 画面が変わるたびに入れ替わった */
    CHECK(find_idx("[ev] ", 0) > find_idx("[init] stage=loop", 0));
    CHECK_EQ(g_flip_while_busy, 0);

    TEST("M7 未知の文字(大文字・改行・空白・0x00・0xFF・数字)は無視: 出力なし・画面変化なし・drop 不変");
    cfg_default();
    {
        static const rx_ev_t rx[] = { {10u,'N'},{11u,'P'},{12u,'S'},{13u,'\r'},{14u,'\n'},{15u,' '},{16u,0x00u},{17u,0xFFu},{18u,'X'},{19u,'0'},{20u,0x80u},{21u,'D'},{22u,'='} };
        C.rx = rx; C.n_rx = 13u;
    }
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    CHECK_EQ(count_pfx("[ev]"), 0);
    CHECK_EQ(count_pfx("[st]"), 0);
    CHECK_EQ(count_pfx("[help]"), 0);
    CHECK_EQ(count_pfx("[ui]"), 0);
    CHECK_EQ(count_pfx("[err]"), 0);
    CHECK_EQ(count_pfx("[hb] "), 10);
    CHECK_EQ(g_flip_ok, 1);
    CHECK_EQ(g_app_dbg.scr, 0);
    CHECK_EQ(app_log_drops(), 0);
    CHECK_EQ(g_fs.rx_r, g_fs.rx_w);                                    /* 全部読んで捨てた */

    TEST("M8 s と ?: [st](範囲つき)の後に found>0 なら [scan]、続けて [ui]。? は新しい [help]");
    cfg_default();
    C.found = 2u;
    {
        static const rx_ev_t rx[] = { {20u,'s'},{30u,'?'} };
        C.rx = rx; C.n_rx = 2u;
    }
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    CHECK_EQ(count_pfx("[st] "), 1);
    CHECK_EQ(count_pfx("[scan]"), 1);
    CHECK_EQ(count_pfx("[ui]"), 1);
    CHECK_EQ(count_pfx("[help]"), 1);
    CHECK(strcmp(nth_pfx("[scan]", 0), "[scan] n=2 a0=0x38 a1=0x1a") == 0);
    CHECK_EQ(find_idx("[scan]", 0), find_idx("[st] ", 0) + 1);          /* [st] の直後 */
    CHECK_EQ(find_idx("[ui]", 0), find_idx("[scan]", 0) + 1);           /* その次に [ui] */
    CHECK(strcmp(nth_pfx("[ui]", 0), "[ui] scr=home set=42 bath10=400 fill10=0 flow10=85 tr=0 btn=none ph=none") == 0);
    CHECK(strcmp(nth_pfx("[help]", 0), "[help] n=next p=prev s=state d=demo +=up -=down x=wx ?=help") == 0);
    CHECK(strstr(nth_pfx("[st] ", 0), " xmin=65535 xmax=0 ymin=65535 ymax=0") != NULL);   /* 触れていない */
    CHECK(strncmp(nth_pfx("[st] ", 0), "[st] tick=20 scr=0 demo=0 frm=1 uf=0 ", 36) == 0);
    /* found=0 なら [scan] は出ない([ui] は出る) */
    cfg_default();
    { static const rx_ev_t rx[] = { {20u,'s'} }; C.rx = rx; C.n_rx = 1u; }
    run_scenario(); split_lines();
    CHECK_EQ(count_pfx("[st] "), 1);
    CHECK_EQ(count_pfx("[scan]"), 0);
    CHECK_EQ(count_pfx("[ui]"), 1);

    /* ================================================================ M9 + - d */
    TEST("M9 + と -: h_up / h_dn の仮想タップ([ev] btn down → act → set、src=uart)。表示面も更新される");
    cfg_default();
    {
        static const rx_ev_t rx[] = { {10u,'+'},{60u,'+'},{110u,'-'},{160u,'s'} };
        C.rx = rx; C.n_rx = 4u;
    }
    C.stop_tick = 300u;
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    {
        static const char *exp[9] = {
            "[ev] btn=h_up st=down src=uart", "[ev] btn=h_up st=act src=uart", "[ev] set=43 src=uart",
            "[ev] btn=h_up st=down src=uart", "[ev] btn=h_up st=act src=uart", "[ev] set=44 src=uart",
            "[ev] btn=h_dn st=down src=uart", "[ev] btn=h_dn st=act src=uart", "[ev] set=43 src=uart"
        };
        CHECK_EQ(count_pfx("[ev] "), 9);
        for (k = 0u; k < 9u; k++) { const char *e = nth_pfx("[ev] ", k); CHECK(e != NULL && strcmp(e, exp[k]) == 0); }
    }
    CHECK(strncmp(nth_pfx("[ui]", 0), "[ui] scr=home set=43 ", 21) == 0);
    CHECK(g_shown_n >= 7u);                                              /* 押下の暗転・白光・数字の変化ごとに入れ替わる */

    TEST("M10 デモ(d): 36 本の [ev] が台本どおりの順序、[hb] の demo=1 が 33 本、終了後 scr=0 demo=0。drop=0 uf=0");
    cfg_default();
    {
        static const rx_ev_t rx[] = { {10u,'d'} };
        C.rx = rx; C.n_rx = 1u;
    }
    C.stop_tick = 3500u;
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    {
        static const char *exp[36] = {
            "demo=start src=uart",
            "btn=h_up st=down src=demo", "btn=h_up st=act src=demo", "set=43 src=demo",
            "btn=h_up st=down src=demo", "btn=h_up st=act src=demo", "set=44 src=demo",
            "btn=h_dn st=down src=demo", "btn=h_dn st=act src=demo", "set=43 src=demo",
            "btn=h_fill st=down src=demo", "btn=h_fill st=act src=demo", "scr=fill from=home src=demo", "done=fill",
            "btn=f_done st=down src=demo", "btn=f_done st=act src=demo", "scr=home from=fill src=demo",
            "btn=h_reheat st=down src=demo", "btn=h_reheat st=act src=demo", "scr=reheat from=home src=demo", "done=reheat",
            "btn=r_done st=down src=demo", "btn=r_done st=act src=demo", "scr=home from=reheat src=demo",
            "btn=h_usage st=down src=demo", "btn=h_usage st=act src=demo", "scr=usage from=home src=demo",
            "btn=u_back st=down src=demo", "btn=u_back st=act src=demo", "scr=home from=usage src=demo",
            "btn=h_power st=down src=demo", "btn=h_power st=act src=demo", "scr=standby from=home src=demo",
            "btn=s_wake st=down src=demo", "scr=home from=standby src=demo", "demo=end"
        };
        char line[96];
        unsigned bad = 0u, ne = count_pfx("[ev] ");
        CHECK_EQ(ne, 36);
        for (k = 0u; (k < 36u) && (k < ne); k++) {
            snprintf(line, sizeof(line), "[ev] %s", exp[k]);
            if (strcmp(nth_pfx("[ev] ", k), line) != 0) { bad++; printf("  #%u: 実際 %s / 期待 %s\n", k, nth_pfx("[ev] ", k), line); }
        }
        CHECK_EQ(bad, 0);
    }
    {
        unsigned nd = 0u, last_demo_hb = 0u;
        for (k = 0u; k < count_pfx("[hb] "); k++) {
            const char *h = nth_pfx("[hb] ", k);
            if (field(h, "demo=") == 1u) { nd++; last_demo_hb = (unsigned)field(h, "tick="); }
        }
        CHECK_EQ(nd, 33);
        CHECK_EQ(last_demo_hb, 3300);
    }
    {   /* 台本の画面: hb の scr */
        struct { unsigned tick; unsigned scr; } w[] = { {200u,0u}, {600u,1u}, {1100u,1u}, {1400u,0u}, {1500u,2u}, {2000u,2u}, {2200u,0u}, {2300u,3u}, {2700u,3u}, {2900u,4u}, {3000u,4u}, {3300u,0u}, {3400u,0u} };
        unsigned j;
        for (j = 0u; j < sizeof(w) / sizeof(w[0]); j++) {
            char key[32]; int idx;
            snprintf(key, sizeof(key), "[hb] tick=%u ", w[j].tick);
            idx = find_idx(key, 0);
            CHECK(idx >= 0);
            if (idx >= 0) { CHECK_EQ(field(g_ln[idx], "scr="), w[j].scr); }
        }
    }
    CHECK_EQ(field(nth_pfx("[hb] ", 33), "demo="), 0);
    CHECK_EQ(app_log_drops(), 0);
    CHECK_EQ(g_app_dbg.uf, 0);
    CHECK_EQ(count_pfx("[err]"), 0);
    CHECK_EQ(g_flip_while_busy, 0);
    CHECK(g_app_dbg.frames > 200u);
    printf("  (参考) 33 秒のデモで入れ替えた回数 frm = %u(模擬の垂直同期 = 5 周回 = 約 17 ms)\n", (unsigned)g_app_dbg.frames);
    CHECK_EQ(s_ui.demo, 0);
    CHECK_EQ(s_ui.speed, 1);

    TEST("M10b デモ中に実タッチ: 即ホーム・demo=abort src=touch・そのタッチは btn にならない");
    cfg_default();
    {
        static const rx_ev_t rx[] = { {10u,'d'} };
        static const press_t pr[] = { {800u, 830u, 100u, 100u, 1u} };
        C.rx = rx; C.n_rx = 1u; C.press = pr; C.n_press = 1u;
    }
    C.stop_tick = 1200u;
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    CHECK_EQ(count_pfx("[ev] demo=abort src=touch"), 1);
    CHECK_EQ(count_pfx("[ev] scr=home from=fill src=touch"), 1);
    CHECK_EQ(count_pfx("[ev] btn=h_up st=down src=touch"), 0);
    CHECK_EQ(g_app_dbg.demo, 0);
    CHECK_EQ(g_app_dbg.scr, 0);
    CHECK_EQ(find_idx("[ev] scr=home from=fill src=touch", 0), find_idx("[ev] demo=abort src=touch", 0) + 1);

    /* ================================================================ M11 タッチ */
    TEST("M11 タッチ: ホームの「湯はり」を押すと btn down(2標本)→ act(押下から 90ms)→ scr=fill。ボタン外は何も起きない。2本指は無視");
    cfg_default();
    {
        static const press_t pr[] = { {50u, 53u, 69u, 224u, 1u}, {300u, 310u, 200u, 100u, 1u}, {400u, 410u, 69u, 224u, 2u} };
        C.press = pr; C.n_press = 3u;
    }
    C.stop_tick = 500u;
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    CHECK_EQ(count_pfx("[ev] "), 3);
    CHECK(strcmp(nth_pfx("[ev] ", 0), "[ev] btn=h_fill st=down src=touch") == 0);
    CHECK(strcmp(nth_pfx("[ev] ", 1), "[ev] btn=h_fill st=act src=touch") == 0);
    CHECK(strcmp(nth_pfx("[ev] ", 2), "[ev] scr=fill from=home src=touch") == 0);
    CHECK_EQ(g_app_dbg.scr, 1);
    CHECK_EQ(field(nth_pfx("[hb] ", 0), "pts="), 0);
    {   /* 触れた範囲が [st] に載る */
        cfg_default();
        { static const press_t pr[] = { {50u, 60u, 400u, 100u, 1u} }; static const rx_ev_t rx[] = { {300u,'s'} }; C.press = pr; C.n_press = 1u; C.rx = rx; C.n_rx = 1u; }
        C.stop_tick = 400u;
        run_scenario(); split_lines();
        CHECK(strstr(nth_pfx("[st] ", 0), " xmin=400 xmax=400 ymin=100 ymax=100") != NULL);
        CHECK_EQ(count_pfx("[ev] "), 0);                                /* (400,100) はボタンの外 */
    }
    {   /* 長押し中の hb に pts=1 と x */
        cfg_default();
        { static const press_t pr[] = { {50u, 250u, 400u, 100u, 1u} }; C.press = pr; C.n_press = 1u; }
        C.stop_tick = 300u;
        run_scenario(); split_lines();
        CHECK_EQ(field(nth_pfx("[hb] ", 1), "tick="), 200);
        CHECK_EQ(field(nth_pfx("[hb] ", 1), "pts="), 1);
        CHECK_EQ(field(nth_pfx("[hb] ", 1), "x="), 400);
    }

    /* ================================================================ M12 描画と入れ替え */
    TEST("M12 描画: 静止中は描かない。表示が変わった時だけ裏面に描いて入れ替える(面は交互)。反映待ちの面は書き換えない。反映待ち中に lcd_flip を呼ばない");
    cfg_default();
    C.check_pending = true;
    C.stop_tick = 300u;
    { static const rx_ev_t rx[] = { {10u,'p'} }; C.rx = rx; C.n_rx = 1u; }      /* ホーム → 待機(即切替。以後は静止) */
    run_scenario(); split_lines();
    CHECK_EQ(g_flip_ok, 2);                                                       /* 作り直し後の最初の1回 + 待機 */
    CHECK_EQ(g_flip_seq[0], 1);                                                   /* 最初の裏面は面1(面0 は起動前に描いた) */
    CHECK_EQ(g_flip_seq[1], 0);
    CHECK_EQ(g_flip_while_busy, 0);
    CHECK_EQ(g_pend_violation, 0);
    CHECK_EQ(g_shown_n, 2);
    CHECK_EQ(g_app_dbg.frames, 2);
    CHECK(g_shown_hash[0] != g_shown_hash[1]);                                    /* ホームと待機で絵が違う */
    CHECK_EQ(g_displayed, 0);
    CHECK_EQ(g_flip_tick[1] < 30u, 1);                                            /* 'p' の直後(tick 10)に反映される */

    cfg_default();
    C.check_pending = true;
    C.stop_tick = 250u;
    { static const rx_ev_t rx[] = { {10u,'n'} }; C.rx = rx; C.n_rx = 1u; }      /* ホーム → 湯はり(スライド 250ms)+ 水位が進む */
    run_scenario(); split_lines();
    CHECK(g_flip_ok > 20u);                                                       /* スライド中は毎周期近く描く */
    CHECK_EQ(g_flip_while_busy, 0);
    CHECK_EQ(g_pend_violation, 0);
    {
        int alt = 1;
        for (k = 1u; (k < g_flip_ok) && (k < 4096u); k++) { if (g_flip_seq[k] == g_flip_seq[k - 1u]) { alt = 0; } }
        CHECK(alt);                                                               /* 面は 1,0,1,0,... と交互 */
    }
    CHECK(g_app_dbg.frames == g_flip_ok || g_app_dbg.frames + 1u == g_flip_ok);
    printf("  (参考) 2.5 秒のスライド + 水位で入れ替えた回数 = %u\n", g_flip_ok);

    TEST("M12b lcd_flip が BUSY を返す間は次の周回で再試行(面は進めない)。反映後に frm が増える");
    cfg_default();
    C.flip_busy_left = 7u;
    C.stop_tick = 200u;
    run_scenario(); split_lines();
    CHECK_EQ(g_flip_ok, 1);
    CHECK_EQ(g_flip_calls, 8);                                                    /* BUSY 7 回 + 成功 1 回 */
    CHECK_EQ(g_app_dbg.frames, 1);
    CHECK_EQ(g_flip_seq[0], 1);

    TEST("M12c lcd_flip が FAIL: [err] code=8 を出し、以後描かない。UART・タッチ・hb は動き続ける");
    cfg_default();
    C.flip_fail = true;
    C.stop_tick = 400u;
    { static const rx_ev_t rx[] = { {100u,'n'} }; C.rx = rx; C.n_rx = 1u; }
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    CHECK_EQ(g_flip_calls, 1);                                                    /* 1回だけ呼んで止まる */
    CHECK_EQ(g_flip_ok, 0);
    CHECK_EQ(count_pfx("[err] code=8 detail=7"), 1);
    CHECK_EQ(count_pfx("[ev] scr=fill from=home src=uart"), 1);                  /* UI は動く */
    CHECK_EQ(count_pfx("[hb] "), 4);
    CHECK_EQ(g_app_dbg.frames, 0);

    TEST("M12d アンダーフロー: 1秒ごとに lcd_underflow_poll を呼び、検出を uf に足す(hb の uf=)");
    cfg_default();
    C.uf_per_poll = 1u;
    run_scenario(); split_lines();
    CHECK_EQ(g_uf_polls, 11);                                                    /* 起動時の1回(捨てる)+ 毎秒 10 回 */
    for (k = 0u; k < 10u; k++) { CHECK_EQ(field(nth_pfx("[hb] ", k), "uf="), k); }   /* hb(tick=100k)の後に poll → hb は k 回分 */
    CHECK_EQ(g_app_dbg.uf, 10);

    TEST("M12e 起動時の GLCDC 初回検出(lcd_init の直後の1回)は捨てる: 起動後の uf 増分は 0。失敗時は poll しない");
    cfg_default();
    C.uf_boot_only = 1u;                                                          /* 起動時の poll だけ 1 を返す(実機の再現) */
    run_scenario(); split_lines();
    CHECK_EQ(g_uf_polls, 11);
    CHECK_EQ(g_app_dbg.uf, 0);
    for (k = 0u; k < 10u; k++) { CHECK_EQ(field(nth_pfx("[hb] ", k), "uf="), 0); }
    CHECK_EQ(count_pfx("[hb] "), 10);
    {   /* 起動時の poll は lcd_init の後・[init touch] の前(ループに入る前) */
        CHECK(strncmp(g_trace, "LCDINIT;TOUCHINIT;", 18) == 0);
        CHECK(find_idx("[init] stage=lcd", 0) >= 0);
    }
    cfg_default();
    C.lcd_ok = false;
    run_scenario();
    CHECK_EQ(g_uf_polls, 10);                                                     /* lcd_init 失敗: 起動時の poll なし(毎秒の 10 回だけ) */

    /* ================================================================ M13 連打・送信が遅い回線 */
    TEST("M13 + を同時に 60 個: 1 tick に1文字、押下中は無視。[hb] 継続、drop=0、set は 48 を超えない");
    cfg_default();
    {
        static rx_ev_t rx[60];
        for (k = 0u; k < 60u; k++) { rx[k].tick = 10u; rx[k].byte = '+'; }
        C.rx = rx; C.n_rx = 60u;
    }
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    CHECK_EQ(count_pfx("[hb] "), 10);
    CHECK(count_pfx("[ev] set=") >= 1u);
    CHECK(count_pfx("[ev] set=") <= 6u);
    CHECK(s_ui.v.set <= 48u);
    CHECK_EQ(app_log_drops(), 0);
    CHECK_EQ(count_pfx("[err]"), 0);
    CHECK_EQ(g_fs.rx_r, g_fs.rx_w);

    TEST("M14 送信が遅い回線(1行に 5 tick)で s を40個: キュー満杯 → 新しい行を捨てる、drop が [hb] に載る、部分行なし");
    cfg_default();
    C.drain_iters = 15u;                                                /* 3周/tick × 5tick */
    C.stop_tick = 1500u;
    {
        static rx_ev_t rx[40];
        for (k = 0u; k < 40u; k++) { rx[k].tick = 10u; rx[k].byte = 's'; }
        C.rx = rx; C.n_rx = 40u;
    }
    run_scenario(); split_lines();
    CHECK(g_split_ok);                                                  /* 途中切れの行が無い */
    CHECK(app_log_drops() > 0u);
    {
        uint32_t prev = 0u; unsigned mono = 1u, nh = count_pfx("[hb] ");
        for (k = 0u; k < nh; k++) { uint32_t d = field(nth_pfx("[hb] ", k), "drop="); if (d < prev) { mono = 0u; } prev = d; }
        CHECK(mono);
        CHECK(nh >= 1u);
        CHECK_EQ(prev, app_log_drops());                                /* 最後の hb の drop が最終累計と一致(以後増えていない) */
    }
    /* 保存則: 積もうとした行 = 送った行 + 破棄 + キューに残り。送った行数は g_fs.nsend */
    CHECK_EQ(pushed_total(), g_fs.nsend + (unsigned)applog_q_count(&s_q) + app_log_drops());

    /* ================================================================ M15 [err] */
    TEST("M15 [err] (A): g_app_dbg.err の変化で1行、100ms に1行まで、同じ値は再送しない");
    cfg_default();
    {
        static const seterr_t se[] = { {30u, 5u, 0u}, {31u, 6u, 4u}, {32u, 5u, 0u}, {45u, 5u, 0u}, {60u, 6u, 4u} };
        C.se = se; C.n_se = 5u;
    }
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    /* tick30 で (5,0)。30〜39 は最新値保留。tick 40 で最新 (5,0)=直前に出した値と同じ → 出さない。tick 60 で (6,4) */
    CHECK_EQ(count_pfx("[err]"), 2);
    CHECK(strcmp(nth_pfx("[err]", 0), "[err] code=5 detail=0") == 0);
    CHECK(strcmp(nth_pfx("[err]", 1), "[err] code=6 detail=4") == 0);
    CHECK_EQ(field(nth_pfx("[hb] ", 0), "err="), 6);                    /* g_app_dbg の err は成功で 0 に戻らない */
    CHECK_EQ(field(nth_pfx("[hb] ", 0), "detail="), 4);

    TEST("M15b 100ms 間引き: 変化が速くても (A) は10tick に1行");
    cfg_default();
    {
        static seterr_t se[40];
        for (k = 0u; k < 40u; k++) { se[k].tick = 100u + k; se[k].code = 5u + (k & 1u); se[k].detail = k; }
        C.se = se; C.n_se = 40u;
    }
    C.stop_tick = 300u;
    run_scenario(); split_lines();
    {
        unsigned ne = count_pfx("[err]");
        CHECK(ne >= 3u);
        CHECK(ne <= 5u);                                                /* 40tick の間に 10tick 間隔なら 4 本 */
        printf("  (参考) 40tick の間の [err] 本数 = %u\n", ne);
    }

    TEST("M16 [err] (B): UART受信異常は code=7 detail=<event>、1秒に1行、g_app_dbg.err_code には書かない");
    cfg_default();
    {
        static const cbev_t cb[] = { {40u, 2u}, {45u, 3u}, {50u, 8u}, {250u, 4u} };
        C.cb = cb; C.n_cb = 4u;
    }
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    CHECK_EQ(count_pfx("[err] code=7"), 3);
    CHECK(strcmp(nth_pfx("[err] code=7", 0), "[err] code=7 detail=2") == 0);        /* tick40 で即時 */
    CHECK_EQ(count_pfx("[err] code=7 detail=8"), 1);
    CHECK_EQ(count_pfx("[err] code=7 detail=3"), 0);
    CHECK_EQ(count_pfx("[err] code=7 detail=4"), 1);
    CHECK(find_idx("[err] code=7 detail=8", 0) > find_idx("[hb] tick=100 ", 0));
    CHECK(find_idx("[err] code=7 detail=8", 0) < find_idx("[hb] tick=200 ", 0));
    CHECK(find_idx("[err] code=7 detail=4", 0) > find_idx("[hb] tick=200 ", 0));
    CHECK(find_idx("[err] code=7 detail=4", 0) < find_idx("[hb] tick=300 ", 0));
    CHECK_EQ(g_app_dbg.err_code, 0);
    CHECK_EQ(field(nth_pfx("[hb] ", 0), "err="), 0);

    /* ================================================================ M17 起動失敗 */
    TEST("M17 タイマ作成失敗: [init timer ok=0 err=4] と [err] code=4 を出し切って停止(hb なし、loop なし、perf なし)");
    cfg_default();
    C.cmt_ok = false;
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    CHECK(strcmp(g_ln[3], "[init] stage=timer ok=0 err=4 detail=0") == 0);
    CHECK(strcmp(g_ln[4], "[err] code=4 detail=0") == 0);
    CHECK_EQ(g_nl, 5);
    CHECK_EQ(count_pfx("[hb] "), 0);
    CHECK_EQ(count_pfx("[init] stage=loop"), 0);
    CHECK_EQ(count_pfx("[perf]"), 0);
    CHECK_EQ(applog_q_count(&s_q), 0);                                  /* 停止ループで送信キューが空になる */

    TEST("M18 LCD 初期化失敗: [init lcd ok=0 err=1 detail=3] を出して続行(描画しない・perf なし。タッチ・タイマ・hb・UART は動く)");
    cfg_default();
    C.lcd_ok = false;
    { static const rx_ev_t rx[] = { {10u,'n'} }; C.rx = rx; C.n_rx = 1u; }
    run_scenario(); split_lines();
    CHECK(strcmp(g_ln[1], "[init] stage=lcd ok=0 err=1 detail=3") == 0);
    CHECK(strcmp(g_ln[2], "[init] stage=touch ok=1 err=0 detail=0") == 0);
    CHECK_EQ(count_pfx("[hb] "), 10);
    CHECK_EQ(count_pfx("[err] code=1 detail=3"), 1);
    CHECK_EQ(count_pfx("[perf]"), 0);
    CHECK_EQ(g_flip_calls, 0);                                           /* 描画しない */
    CHECK_EQ(count_pfx("[ev] scr=fill from=home src=uart"), 1);          /* UI は動く */

    TEST("M19 R_SCI_Open 失敗: UART は無音、LCD/タッチ(画面操作)・タイマは動き続ける");
    cfg_default();
    C.sci_open_ok = false;
    {
        static const press_t pr[] = { {50u, 53u, 420u, 150u, 1u} };         /* h_up */
        static const rx_ev_t rx[] = { {10u,'n'} };
        C.press = pr; C.n_press = 1u; C.rx = rx; C.n_rx = 1u;
    }
    run_scenario();
    CHECK_EQ(g_fs.tx_len, 0);
    CHECK_EQ(g_fs.send_calls, 0);
    CHECK_EQ(g_lcd_init_calls, 1);
    CHECK_EQ(g_tr_calls, 1000);
    CHECK_EQ(s_ui.v.set, 43);                                            /* タッチの1回(UART の 'n' は届かない) */
    CHECK(g_flip_ok >= 3u);                                              /* 表示は更新された */
    CHECK_EQ(app_log_drops(), 0);

    /* ================================================================ M20 tick の折り返し・飛び */
    TEST("M20 s_tick が 0xFFFFFF9C から始まり 32bit を折り返しても、hb は 100 tick 毎(間隔がすべて 100)。画面の時刻も壊れない");
    cfg_default();
    C.start_tick = 0xFFFFFF9Cu;                                         /* 実機は 0 から始まるが、折り返しの経路を通す */
    C.stop_tick = 1000u;
    { static const rx_ev_t rx[] = { {0xFFFFFFC0u, 'n'}, {0x00000010u, 'n'} }; C.rx = rx; C.n_rx = 2u; }
    run_scenario(); split_lines();
    CHECK_EQ(count_pfx("[hb] "), 11);                                   /* 最初の周回で1本(last_hb=0 起点) + 100 tick 毎に10本 */
    {
        uint32_t prev = field(nth_pfx("[hb] ", 0), "tick="); int spacing_ok = 1; int wrapped = 0;
        CHECK_EQ(prev, 0xFFFFFF9Cu);
        for (k = 1u; k < 11u; k++) {
            uint32_t t = field(nth_pfx("[hb] ", k), "tick=");
            if ((uint32_t)(t - prev) != 100u) { spacing_ok = 0; }
            if (t < prev) { wrapped = 1; }
            prev = t;
        }
        CHECK(spacing_ok);
        CHECK(wrapped);                                                 /* 折り返しを実際にまたいだ */
    }
    CHECK_EQ(count_pfx("[ev] scr=fill from=home src=uart"), 1);         /* 折り返しの前後の 'n' が効く */
    CHECK_EQ(count_pfx("[ev] scr=reheat from=fill src=uart"), 1);

    TEST("M21 メインが詰まって tick が 250 飛んだ場合: hb は1本だけ(まとめて出さない)");
    cfg_default();
    C.jump_at = 150u; C.jump_by = 250u; C.stop_tick = 450u;
    run_scenario(); split_lines();
    /* tick 100 で1本、tick 150→400 に飛んで(last_hb=100)1本、以後 500, 600, 700 */
    CHECK_EQ(count_pfx("[hb] "), 5);
    CHECK_EQ(field(nth_pfx("[hb] ", 1), "tick="), 400);
    CHECK_EQ(field(nth_pfx("[hb] ", 2), "tick="), 500);

    /* ================================================================ M22 x(更新ボタンの仮想タップ)。design-uart.md §13.2 */
    TEST("M22 x: h_wx の仮想タップ([ev] btn=h_wx down → act、src=uart)。画面・設定温度は変わらない。ホーム以外・デモ中は無視");
    cfg_default();
    {
        static const rx_ev_t rx[] = { {10u,'x'},{60u,'s'} };
        C.rx = rx; C.n_rx = 2u;
    }
    C.stop_tick = 200u;
    run_scenario(); split_lines();
    CHECK(g_split_ok);
    CHECK_EQ(count_pfx("[ev] "), 2);
    CHECK(nth_pfx("[ev] ", 0) != NULL && strcmp(nth_pfx("[ev] ", 0), "[ev] btn=h_wx st=down src=uart") == 0);
    CHECK(nth_pfx("[ev] ", 1) != NULL && strcmp(nth_pfx("[ev] ", 1), "[ev] btn=h_wx st=act src=uart") == 0);
    CHECK(strncmp(nth_pfx("[ui]", 0), "[ui] scr=home set=42 ", 21) == 0);
    CHECK_EQ(app_log_drops(), 0);
#if !APP_WX_PRODUCT
    CHECK_EQ(count_pfx("[wx]"), 0);                                                 /* APP_WX = 0: 取得のコードが無いので [wx] の行は出ない */
#endif
    cfg_default();                                                                  /* ホーム以外(n で fill へ)の x は無視 */
    {
        static const rx_ev_t rx[] = { {10u,'n'},{100u,'x'} };
        C.rx = rx; C.n_rx = 2u;
    }
    C.stop_tick = 250u;
    run_scenario(); split_lines();
    CHECK_EQ(count_pfx("[ev] btn=h_wx"), 0);
    cfg_default();                                                                  /* デモ中の x は無視 */
    {
        static const rx_ev_t rx[] = { {10u,'d'},{100u,'x'} };
        C.rx = rx; C.n_rx = 2u;
    }
    C.stop_tick = 250u;
    run_scenario(); split_lines();
    CHECK_EQ(count_pfx("[ev] btn=h_wx"), 0);

#if APP_WX_PRODUCT
    /* ================================================================ M23 製品への組み込み(APP_WX = 1。wifi_at.c は偽物) */
    TEST("M23a wx_init は [init] ×4 と [perf] の後に 1 回(ログの順序)、wx_poll はメインループの毎周回(ms は 10ms 刻みの tick x 10 で戻らない)");
    cfg_default();
    C.stop_tick = 300u;
    run_scenario(); split_lines();
    CHECK_EQ(g_wx_init_n, 1);
    CHECK(strncmp(g_trace, "LCDINIT;TOUCHINIT;WXINIT;", 25) == 0);
    CHECK(strncmp(g_ln[5], "[perf] ", 7) == 0);
    CHECK_EQ(g_tx_at_wx_init, 6);                                                   /* [boot] [init]x4 [perf] を積んだ後 */
    CHECK(g_wx_poll_n >= 300u);                                                     /* 1 tick に 3 周回 */
    CHECK_EQ(g_wx_poll_back, 0);
    CHECK_EQ(g_wx_poll_last_ms, 3000u);                                             /* 最後の tick 300 x 10ms */
    CHECK(g_wx_ui_n >= 299u);                                                       /* 10ms ごと */
    CHECK_EQ(g_wx_state_n, 0);
    CHECK_EQ(app_log_drops(), 0);

    TEST("M23b 表示値: wx_ui の値が ui_set_wx で s_ui.wx に入り、clk_ext = 1 なら時計は外部の値(未同期は 0xFFFF)。値が変わると再描画される");
    cfg_default();
    memset(&g_wx_val, 0, sizeof g_wx_val);
    g_wx_val.clk_ext = 1u; g_wx_val.clock_min = 0xFFFFu; g_wx_val.upd_min = 0xFFFFu; g_wx_val.net = (uint8_t)UI_NET_OFF;
    C.stop_tick = 100u;
    run_scenario();
    CHECK_EQ(s_ui.wx.clk_ext, 1);
    CHECK_EQ(s_ui.wx.net, UI_NET_OFF);
    CHECK_EQ(s_ui.v.clock_min, 0xFFFF);                                              /* 旧時計(21:45)ではなく未同期 */
    CHECK_EQ(g_flip_ok, 1);
    g_wx_val.net = (uint8_t)UI_NET_ON; g_wx_val.have = 1u; g_wx_val.kind = 4u; g_wx_val.temp_x10 = 123;
    g_wx_val.clock_min = (uint16_t)(13u * 60u + 24u); g_wx_val.upd_min = (uint16_t)(13u * 60u + 20u); g_wx_val.month = 11u; g_wx_val.day = 25u; g_wx_val.wday = 3u;
    cfg_default(); C.stop_tick = 100u;
    run_scenario();
    CHECK_EQ(s_ui.wx.net, UI_NET_ON);
    CHECK_EQ(s_ui.wx.temp_x10, 123);
    CHECK_EQ(s_ui.wx.kind, 4);
    CHECK_EQ(s_ui.wx.month, 11);
    CHECK_EQ(s_ui.wx.wday, 3);
    CHECK_EQ(s_ui.v.clock_min, 13 * 60 + 24);
    memset(&g_wx_val, 0, sizeof g_wx_val);
    g_wx_val.clk_ext = 1u; g_wx_val.clock_min = 0xFFFFu; g_wx_val.upd_min = 0xFFFFu;

    TEST("M23c 更新ボタン: UART x(src=uart)と実タッチ(src=touch)の BTN_ACT で wx_request が 1 回ずつ。ほかのボタン・ボタンの外・デモ中の x では呼ばない");
    cfg_default();
    {
        static const rx_ev_t rx[] = { {10u,'x'} };
        static const press_t pr[] = { {100u, 103u, 250u, 172u, 1u}, {200u, 203u, 69u, 224u, 1u} };   /* h_wx の中、h_fill(画面が変わる) */
        C.rx = rx; C.n_rx = 1u; C.press = pr; C.n_press = 2u;
    }
    C.stop_tick = 300u;
    g_wx_req_ret = WX_REQ_OK;
    run_scenario(); split_lines();
    CHECK_EQ(count_pfx("[ev] btn=h_wx st=act"), 2);
    CHECK_EQ(g_wx_req_n, 2);
    CHECK_EQ(g_wx_req_src[0], WX_SRC_UART);
    CHECK_EQ(g_wx_req_src[1], WX_SRC_TOUCH);
    CHECK(g_wx_req_ms[0] >= 100u && g_wx_req_ms[0] <= 300u);                         /* x(tick 10)の act は約 160ms 後 */
    CHECK(g_wx_req_ms[1] >= 1030u && g_wx_req_ms[1] <= 1300u);                       /* タッチ(tick 100)の act は 90ms 以上後 */
    CHECK_EQ(count_pfx("[ev] btn=h_fill st=act"), 1);                                /* h_fill では呼ばれない(計 2 回のまま) */
    cfg_default();                                                                  /* ボタンの外・デモ中の x */
    {
        static const rx_ev_t rx[] = { {10u,'d'},{100u,'x'} };
        static const press_t pr[] = { {400u, 410u, 400u, 100u, 1u} };
        C.rx = rx; C.n_rx = 2u; C.press = pr; C.n_press = 1u;
    }
    C.stop_tick = 500u;
    run_scenario();
    CHECK_EQ(g_wx_req_n, 0);

    TEST("M23d s: [st] [ui] の次に wx_log_state を 1 回(行の順序)");
    cfg_default();
    { static const rx_ev_t rx[] = { {20u,'s'} }; C.rx = rx; C.n_rx = 1u; }
    C.stop_tick = 100u;
    run_scenario(); split_lines();
    CHECK_EQ(g_wx_state_n, 1);
    CHECK_EQ(count_pfx("[wx] net=on busy=0 have=0"), 1);
    CHECK_EQ(find_idx("[wx] net=on busy=0 have=0", 0), find_idx("[ui]", 0) + 1);
    CHECK_EQ(field(nth_pfx("[wx] net=on busy=0 have=0", 0), "age="), 200);

    TEST("M23e x を 5 回: 仮想タップは 1 tick に 1 文字・押下中は無視。wx_request は act の数と同じ。ログは drop=0、[hb] は途切れない");
    cfg_default();
    { static const rx_ev_t rx[] = { {10u,'x'},{50u,'x'},{110u,'x'},{150u,'x'},{210u,'x'} }; C.rx = rx; C.n_rx = 5u; }
    C.stop_tick = 400u;
    run_scenario(); split_lines();
    CHECK_EQ(g_wx_req_n, (unsigned)count_pfx("[ev] btn=h_wx st=act"));
    CHECK(g_wx_req_n >= 2u);
    CHECK_EQ(app_log_drops(), 0);
    CHECK_EQ(count_pfx("[hb] "), 4);
#endif

    TEST_REPORT("test_app_main");
}
