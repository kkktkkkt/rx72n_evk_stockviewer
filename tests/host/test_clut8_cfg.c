/* test_clut8_cfg.c - src/app/clut8_test.c と lcd_panel.c のホスト試験(docs/design.md §19.3.6 の H-C8-6 と、段階 0〜5 の模擬走行)
 *  - H-C8-6: lcd_init が R_GLCDC_Open に渡す cfg の GR2 が CLUT8 / offset 512 / hsize 480 / パレットA(256個)になり、
 *    実 FIT の r_glcdc_open_param_check を通る。offset = 480 は GLCDC_ERR_INVALID_ALIGNMENT。面A・面B の実アドレスも通る。
 *  - 模擬走行: GLCDC のレジスタ(GR2VEN / GR2CLUTINT.SEL / CLUT0・1)と R_GLCDC_ClutUpdate_NoReflect / BufferChange を
 *    「VEN = 1 の次の垂直同期でだけ 面と SEL が反映される」モデルの偽物にし、UART の n で段階 1〜5 を流す。
 *    各垂直同期で「表示中の面とパレットの組」が同じ絵になる組か(組み違いが出ないか)、API の順序、[clut] 行の時刻・値を検査する。
 *    実機の GLCDC・実時間・LCD は試験できない(このモデルは設計 §19 の前提 = HW §51.2.9 の記述を仮定している)。
 * ビルド: tests/host/run.sh の S18 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <setjmp.h>

#include "test_framework.h"

#include "platform.h"
static volatile struct st_glcdc g_fake_glcdc;
static struct st_ofsm           g_fake_ofsm;
#undef GLCDC
#define GLCDC g_fake_glcdc
#undef OFSM
#define OFSM g_fake_ofsm

static uint8_t g_ram[2u * 139264u + 64u] __attribute__((aligned(64)));   /* 面A・面B の代わり(RAM2 はホストに無い) */
#define LCD_C8_FB_A_ADDR ((uintptr_t)g_ram)

#include "../../src/app/lcd_panel.c"
#include "../../src/app/clut8_test.c"
#include "r_glcdc_private.h"

/* ---------------------------------------------------------------- 偽物 */
#define VSYNC_US   (16820u)          /* 59.46 Hz */
#define POLL_US    (500u)            /* app_log_poll 1回 = 模擬時間 0.5ms */

static uint64_t g_us, g_stop_us;
static uint32_t g_ticks, g_vs;
static bool     g_timer_on;
static jmp_buf  g_jb;

typedef struct { uint64_t us; int step; char text[256]; } line_t;
static line_t   g_lines[4096];
static unsigned g_nlines;

typedef struct { unsigned ms; char c; } rx_t;
static const rx_t *g_rx; static unsigned g_nrx, g_rxi;

static glcdc_cfg_t g_open_cfg;
static glcdc_err_t g_open_ret = GLCDC_SUCCESS;
static const uint32_t *g_pending;           /* BufferChange が受けた面(VEN = 1 の間) */
static char     g_disp_face = 'a';          /* 表示中の面 */
static unsigned g_disp_set;                 /* 表示中のパレットのセット */
static unsigned g_nr_while_ven, g_bc_while_ven, g_bad_clut_args, g_inconsistent, g_vsyncs;
static unsigned g_nr_n, g_bc_n;
typedef struct { uint64_t us; char face; unsigned set; } chg_t;
static chg_t    g_chg[256]; static unsigned g_nchg;
static char     g_seq[6][8192]; static unsigned g_seqn[6];     /* 段階ごとの API 呼び出しの並び(N = NoReflect、B = BufferChange) */

void R_GPIO_PinWrite(gpio_port_pin_t pin, gpio_level_t level) { (void)pin; (void)level; }
void R_GPIO_PinDirectionSet(gpio_port_pin_t pin, gpio_dir_t dir) { (void)pin; (void)dir; }
void R_GLCDC_PinSet() { }
bool R_BSP_SoftwareDelay(uint32_t delay, bsp_delay_units_t units) { (void)delay; (void)units; return true; }
void app_debug_set_err(uint32_t code, uint32_t detail) { g_app_dbg.err_code = code; g_app_dbg.err_detail = detail; }
volatile app_dbg_t g_app_dbg;
void app_debug_init(void) { memset((void *)&g_app_dbg, 0, sizeof(g_app_dbg)); }
void app_log_init(void) { }
uint32_t app_log_drops(void) { return 0u; }
uint32_t app_log_rx_event(void) { return 0u; }

glcdc_ctrl_t g_ctrl_blk;
void R_BSP_RegisterProtectEnable(bsp_reg_protect_t r) { (void)r; }
void R_BSP_RegisterProtectDisable(bsp_reg_protect_t r) { (void)r; }
bsp_int_err_t R_BSP_InterruptControl(bsp_int_src_t v, bsp_int_cmd_t c, void *p) { (void)v; (void)c; (void)p; return BSP_INT_SUCCESS; }
void r_glcdc_interrupt_setting(glcdc_interrupt_cfg_t const * const p) { (void)p; }

static void seq_add(char c)
{
    unsigned s = (g_app_dbg.scr < 6u) ? g_app_dbg.scr : 0u;
    if (g_seqn[s] < (sizeof(g_seq[s]) - 1u)) { g_seq[s][g_seqn[s]++] = c; g_seq[s][g_seqn[s]] = '\0'; }
}

/* FIT の r_glcdc_clut_update と同じ動作: SEL が示さない方のセットへ全部書き、SEL をそのセットにする */
static glcdc_err_t clut_write_model(glcdc_clut_cfg_t const *c, bool via_open)
{
    unsigned i, target = 1u - (unsigned)g_fake_glcdc.GR2CLUTINT.BIT.SEL;
    if (!c->enable) { return GLCDC_SUCCESS; }
    if ((c->start != 0u) || (c->size != 256u)) { g_bad_clut_args++; }
    for (i = c->start; i < (unsigned)(c->start + c->size); i++) {
        if (target == 0u) { g_fake_glcdc.GR2CLUT0[i].LONG = c->p_base[i]; } else { g_fake_glcdc.GR2CLUT1[i].LONG = c->p_base[i]; }
    }
    g_fake_glcdc.GR2CLUTINT.BIT.SEL = target;
    if (via_open) { g_disp_set = target; }
    return GLCDC_SUCCESS;
}

glcdc_err_t R_GLCDC_Open(glcdc_cfg_t * const p_cfg)
{
    g_open_cfg = *p_cfg;
    if (g_open_ret != GLCDC_SUCCESS) { return g_open_ret; }
    clut_write_model(&p_cfg->clut[GLCDC_FRAME_LAYER_2], true);
    g_disp_face = 'a';
    return GLCDC_SUCCESS;
}
glcdc_err_t R_GLCDC_Control(glcdc_control_cmd_t cmd, void const * const p_args) { (void)cmd; (void)p_args; return GLCDC_SUCCESS; }
glcdc_err_t R_GLCDC_GetStatus(glcdc_status_t * const p_status) { memset(p_status, 0, sizeof(*p_status)); p_status->state_gr2uf = GLCDC_NOT_DETECTED; return GLCDC_SUCCESS; }
glcdc_err_t R_GLCDC_ClutUpdate_NoReflect(glcdc_frame_layer_t frame, glcdc_clut_cfg_t const * const p_clut_cfg)
{
    g_nr_n++; seq_add('N');
    if (frame != GLCDC_FRAME_LAYER_2) { g_bad_clut_args++; }
    if (g_fake_glcdc.GR2VEN.BIT.VEN != 0u) { g_nr_while_ven++; return GLCDC_ERR_INVALID_UPDATE_TIMING; }
    return clut_write_model(p_clut_cfg, false);
}
glcdc_err_t R_GLCDC_BufferChange(glcdc_frame_layer_t frame, uint32_t const * const p_base)
{
    g_bc_n++; seq_add('B');
    if (frame != GLCDC_FRAME_LAYER_2) { g_bad_clut_args++; }
    if (g_fake_glcdc.GR2VEN.BIT.VEN != 0u) { g_bc_while_ven++; return GLCDC_ERR_INVALID_UPDATE_TIMING; }
    g_pending = p_base;
    g_fake_glcdc.GR2VEN.BIT.VEN = 1u;
    return GLCDC_SUCCESS;
}
bool R_CMT_CreatePeriodic(uint32_t frequency_hz, void (*callback)(void *pdata), uint32_t *channel)
{
    (void)callback; *channel = 0u;
    CHECK_EQ(frequency_hz, 100);
    g_timer_on = true;
    return true;
}
void app_log_line(const char *s, size_t n)
{
    if ((n == 0u) || (g_nlines >= 4096u)) { return; }
    g_lines[g_nlines].us = g_us;
    g_lines[g_nlines].step = (int)g_app_dbg.scr;
    memcpy(g_lines[g_nlines].text, s, n);
    g_lines[g_nlines].text[n] = '\0';
    g_nlines++;
}
int app_log_getc(void)
{
    if ((g_rxi < g_nrx) && (g_us >= ((uint64_t)g_rx[g_rxi].ms * 1000u))) { return (int)(unsigned char)g_rx[g_rxi++].c; }
    return -1;
}

/* 垂直同期: VEN = 1 なら 面と SEL を同時に反映して VEN = 0。そのあと「表示中の面とセットが同じ絵か」を検査 */
static void vsync(void)
{
    uint32_t bg;
    g_vsyncs++;
    if (g_fake_glcdc.GR2VEN.BIT.VEN != 0u) {
        char f = (g_pending == (const uint32_t *)(uintptr_t)LCD_C8_FB_A_ADDR) ? 'a' : 'b';
        unsigned set = (unsigned)g_fake_glcdc.GR2CLUTINT.BIT.SEL;
        if ((f != g_disp_face) || (set != g_disp_set)) {
            if (g_nchg < 256u) { g_chg[g_nchg].us = g_us; g_chg[g_nchg].face = f; g_chg[g_nchg].set = set; g_nchg++; }
        }
        g_disp_face = f; g_disp_set = set;
        g_fake_glcdc.GR2VEN.BIT.VEN = 0u;
    }
    /* 背景(面A は idx 0、面B は idx 255)の色が黒でなければ、面とパレットが組み違い */
    bg = (g_disp_set == 0u) ? g_fake_glcdc.GR2CLUT0[(g_disp_face == 'a') ? 0 : 255].LONG
                            : g_fake_glcdc.GR2CLUT1[(g_disp_face == 'a') ? 0 : 255].LONG;
    if (bg != 0xFF000000u) { g_inconsistent++; }
}

void app_log_poll(void)
{
    g_us += POLL_US;
    if (g_timer_on && ((g_us / 10000u) > g_ticks)) { g_ticks++; tick_callback(NULL); }
    if ((g_us / VSYNC_US) > g_vs) { g_vs++; vsync(); }
    if (g_us >= g_stop_us) { longjmp(g_jb, 1); }
}

/* ---------------------------------------------------------------- 実行と解析 */
static const rx_t k_script[] = {
    { 100, 's' }, { 200, '?' }, { 300, 'x' }, { 400, '+' },                    /* 段階0 で s ? と無視される文字 */
    { 1000, 'n' },                                                             /* 段階1(7000 ms) */
    { 9000, 'n' },                                                             /* 段階2(11000 ms) */
    { 22000, 'n' },                                                            /* 段階3(12000 ms) */
    { 35000, 'n' },                                                            /* 段階4(9000 ms) */
    { 46000, 'n' },                                                            /* 段階5(5500 ms) */
    { 56000, 'n' },                                                            /* 5 の次は 0 */
    { 57000, 'p' },                                                            /* 0 の前は 5 */
    { 58000, 'd' }, { 59000, 's' },                                            /* 段階5 をやり直す */
};

static unsigned long field(const char *line, const char *key)
{
    char        k[16];
    const char *p;
    snprintf(k, sizeof(k), " %s", key);           /* "ven=" の中の "n=" に当たらないよう、前に空白を付ける */
    p = strstr(line, k);
    return (p != NULL) ? strtoul(p + strlen(k), NULL, 10) : 0xDEADBEEFul;
}
static bool has_ev(const char *line, int step, const char *ev)
{
    char a[40], b[40];
    snprintf(a, sizeof(a), "[clut] step=%d ", step);
    snprintf(b, sizeof(b), " ev=%s ", ev);
    return (strncmp(line, a, strlen(a)) == 0) && (strstr(line, b) != NULL);
}
/* step の ev の nth 番目(0 始まり)の行番号。無ければ -1。from 以降だけ見る */
static int find(int step, const char *ev, unsigned nth, unsigned from)
{
    unsigned i;
    for (i = from; i < g_nlines; i++) { if (has_ev(g_lines[i].text, step, ev)) { if (nth == 0u) { return (int)i; } nth--; } }
    return -1;
}
static long ms_between(int a, int b) { return (long)((long long)(g_lines[b].us - g_lines[a].us) / 1000); }
static const char *fb(int i) { return strstr(g_lines[i].text, "fb=") + 3; }
static const char *pl(int i) { return strstr(g_lines[i].text, "pal=") + 4; }
#define F(i, k) field(g_lines[i].text, k)

static void test_cfg(void)
{
    glcdc_cfg_t c;
    const glcdc_input_cfg_t *in;
    TEST("H-C8-6 lcd_init -> clut8_test_patch_cfg: GR2 = CLUT8、offset 512、hsize 480 / vsize 272、パレットA(start 0、size 256)。GR1・出力・ブレンド・検出は変わらない");
    memset((void *)&g_fake_glcdc, 0, sizeof(g_fake_glcdc));
    c8pat_pal(s_pal_a, false, false);
    CHECK(lcd_init());
    in = &g_open_cfg.input[GLCDC_FRAME_LAYER_2];
    CHECK_EQ((long long)(uintptr_t)in->p_base, (long long)(uintptr_t)g_ram);
    CHECK_EQ(in->format, GLCDC_IN_FORMAT_CLUT8);
    CHECK_EQ(in->offset, 512);
    CHECK_EQ(in->hsize, 480);
    CHECK_EQ(in->vsize, 272);
    CHECK_EQ(g_open_cfg.clut[GLCDC_FRAME_LAYER_2].enable, 1);
    CHECK(g_open_cfg.clut[GLCDC_FRAME_LAYER_2].p_base == s_pal_a);
    CHECK_EQ(g_open_cfg.clut[GLCDC_FRAME_LAYER_2].start, 0);
    CHECK_EQ(g_open_cfg.clut[GLCDC_FRAME_LAYER_2].size, 256);
    CHECK(g_open_cfg.input[GLCDC_FRAME_LAYER_1].p_base == NULL);
    CHECK_EQ(g_open_cfg.blend[GLCDC_FRAME_LAYER_2].blend_control, GLCDC_BLEND_CONTROL_NONE);
    CHECK_EQ(g_open_cfg.blend[GLCDC_FRAME_LAYER_2].visible, 1);
    CHECK_EQ(g_open_cfg.detection.gr2uf_detect, 1);
    CHECK_EQ(g_open_cfg.output.format, GLCDC_OUT_FORMAT_16BITS_RGB565);

    TEST("H-C8-6 実 FIT の r_glcdc_open_param_check: 実アドレス(面A 0x00800000、面B 0x00822000)で通る。offset = 480 は ALIGNMENT");
    c = g_open_cfg;
    c.input[GLCDC_FRAME_LAYER_2].p_base = (uint32_t *)(uintptr_t)0x00800000u;
    memset(&g_ctrl_blk, 0, sizeof(g_ctrl_blk));
    g_ctrl_blk.active_start_pos.x = (int16_t)(c.output.htiming.back_porch + c.output.htiming.sync_width);
    g_ctrl_blk.active_start_pos.y = (int16_t)(c.output.vtiming.back_porch + c.output.vtiming.sync_width);
    g_ctrl_blk.hsize = c.output.htiming.display_cyc; g_ctrl_blk.vsize = c.output.vtiming.display_cyc;
    g_ctrl_blk.graphics_read_enable[GLCDC_FRAME_LAYER_2] = true;
    CHECK_EQ(r_glcdc_open_param_check(&c), GLCDC_SUCCESS);
    c.input[GLCDC_FRAME_LAYER_2].p_base = (uint32_t *)(uintptr_t)0x00822000u;
    CHECK_EQ(r_glcdc_open_param_check(&c), GLCDC_SUCCESS);
    CHECK_EQ(LCD_FB0_ADDR + LCD_C8_FB_BYTES, 0x00822000u);
    CHECK_EQ(LCD_C8_FB_BYTES, 139264);
    c.input[GLCDC_FRAME_LAYER_2].offset = 480;
    CHECK_EQ(r_glcdc_open_param_check(&c), GLCDC_ERR_INVALID_ALIGNMENT);
    c = g_open_cfg; c.input[GLCDC_FRAME_LAYER_2].p_base = (uint32_t *)(uintptr_t)0x00800000u;
    c.clut[GLCDC_FRAME_LAYER_2].size = 257;
    CHECK(r_glcdc_open_param_check(&c) != GLCDC_SUCCESS);   /* 対照: 検査が効いている(start + size > 256) */
}

static void test_run(void)
{
    int a, b, c, i, st[6];
    unsigned k, n_clut_lines;
    long dt;

    TEST("模擬走行(UART の n p d s ? で 段階 0 → 1 → … → 5 → 0 → 5 → 5)");
    memset((void *)&g_fake_glcdc, 0, sizeof(g_fake_glcdc));
    g_fake_ofsm.MDE.LONG = 0xFFFFFFFFu;
    g_us = 0u; g_ticks = 0u; g_vs = 0u; g_timer_on = false; g_nlines = 0u; g_rx = k_script; g_nrx = sizeof(k_script) / sizeof(k_script[0]); g_rxi = 0u;
    g_nr_while_ven = g_bc_while_ven = g_bad_clut_args = g_inconsistent = g_vsyncs = g_nr_n = g_bc_n = g_nchg = 0u;
    memset(g_seqn, 0, sizeof(g_seqn)); memset(g_seq, 0, sizeof(g_seq));
    g_stop_us = 61000ull * 1000ull;
    if (setjmp(g_jb) == 0) { clut8_test_main(); }
    printf("  模擬 %llu ms、垂直同期 %u 回、ログ %u 行、NoReflect %u 回、BufferChange %u 回\n", (unsigned long long)(g_us / 1000u), g_vsyncs, g_nlines, g_nr_n, g_bc_n);
    for (i = 0; i < (int)g_nlines && i < 12; i++) { printf("  %6llu %s", (unsigned long long)(g_lines[i].us / 1000u), g_lines[i].text); }

    TEST("UC0 起動の順序と値");
    CHECK(strncmp(g_lines[0].text, "[boot] ", 7) == 0);
    CHECK(strncmp(g_lines[1].text, "[init] stage=lcd ok=1 err=0 detail=0", 36) == 0);
    CHECK(strncmp(g_lines[2].text, "[init] stage=timer ok=1", 23) == 0);
    CHECK(strncmp(g_lines[3].text, "[init] stage=loop ok=1", 22) == 0);
    a = find(0, "start", 0, 0);
    CHECK(a == 4);
    if (a >= 0) { CHECK(F(a, "rc=") == 0 && F(a, "ven=") == 0 && F(a, "uf=") == 0 && strncmp(fb(a), "a ", 2) == 0 && strncmp(pl(a), "a ", 2) == 0); }
    CHECK_EQ(g_c8_mde, 7);
    CHECK_EQ(g_app_dbg.err_code, 0);

    TEST("コマンド: s(ev=state + [st])と ?(help)。x と + は無視(x は applog_cmd_parse が APPLOG_CMD_WX を返すようになったが、CLUT8 実験は対象外の文字として無視する)");
    a = find(0, "state", 0, 0);
    CHECK(a >= 0);
    if (a >= 0) { CHECK(strncmp(g_lines[a + 1].text, "[st] tick=", 10) == 0); CHECK(strstr(g_lines[a + 1].text, " scr=0 ") != NULL); }
    for (k = 0u, b = 0; k < g_nlines; k++) { if (strcmp(g_lines[k].text, "[help] n=next p=prev d=again s=state ?=help\r\n") == 0) { b++; } }
    CHECK_EQ(b, 1);

    TEST("UC1 段階1: flip が6行、fb = b a b a b a、pal が fb と同じ、sel が毎回反転、rc = 0、ven = 1、間隔 1000 ± 30 ms、7000 ms 後に done");
    a = find(1, "start", 0, 0); CHECK(a >= 0);
    st[1] = a;
    for (k = 0u; k < 6u; k++) {
        b = find(1, "flip", k, 0);
        CHECK(b >= 0);
        if (b < 0) { continue; }
        CHECK_EQ(F(b, "n="), k + 1u);
        CHECK(*fb(b) == (((k & 1u) == 0u) ? 'b' : 'a'));
        CHECK(*pl(b) == *fb(b));
        CHECK_EQ(F(b, "rc="), 0);
        CHECK_EQ(F(b, "ven="), 1);
        CHECK_EQ(F(b, "sel="), (F(a, "sel=") + k + 1u) & 1u);
        dt = ms_between(st[1], b);
        CHECK(labs(dt - (long)(1000u * (k + 1u))) <= 30);
    }
    CHECK(find(1, "flip", 6, 0) < 0);
    b = find(1, "done", 0, 0); CHECK(b >= 0);
    if (b >= 0) { CHECK(labs(ms_between(st[1], b) - 7000) <= 30); CHECK(*fb(b) == 'a' && *pl(b) == 'a'); }
    CHECK(strncmp(g_seq[1], "NBNBNBNBNBNBNB", 15) == 0 && g_seqn[1] == 14u);       /* settle + 6 flip、どれも NoReflect の直後に BufferChange */

    TEST("UC2 段階2: 1000 ms に nr(pal = b、fb = a、sel 反転、ven = 0)、3000 ms 後に commit(fb = b)、3000 ms 後に nr(pal = a)、3000 ms 後に commit(fb = a)、1000 ms 後に done。3秒間 表示は変わらない");
    a = find(2, "start", 0, 0); CHECK(a >= 0);
    st[2] = a;
    {
        int nr1 = find(2, "nr", 0, 0), c1 = find(2, "commit", 0, 0), nr2 = find(2, "nr", 1, 0), c2 = find(2, "commit", 1, 0), d = find(2, "done", 0, 0);
        CHECK(nr1 >= 0 && c1 >= 0 && nr2 >= 0 && c2 >= 0 && d >= 0);
        if (nr1 >= 0 && c1 >= 0 && nr2 >= 0 && c2 >= 0 && d >= 0) {
            CHECK(labs(ms_between(a, nr1) - 1000) <= 30);
            CHECK(labs(ms_between(nr1, c1) - 3000) <= 30);
            CHECK(labs(ms_between(c1, nr2) - 3000) <= 30);
            CHECK(labs(ms_between(nr2, c2) - 3000) <= 30);
            CHECK(labs(ms_between(c2, d) - 1000) <= 30);
            CHECK(*pl(nr1) == 'b' && *fb(nr1) == 'a' && F(nr1, "ven=") == 0 && F(nr1, "sel=") != F(a, "sel="));
            CHECK(*pl(c1) == 'b' && *fb(c1) == 'b' && F(c1, "ven=") == 1);
            CHECK(*pl(nr2) == 'a' && *fb(nr2) == 'b' && F(nr2, "ven=") == 0);
            CHECK(*pl(c2) == 'a' && *fb(c2) == 'a');
            CHECK_EQ(F(nr1, "rc=") + F(c1, "rc=") + F(nr2, "rc=") + F(c2, "rc="), 0);
            /* 表示が変わったのは commit の後(垂直同期 1 回以内)だけ: 段階2 の間に 2 回 */
            for (k = 0u, b = 0; k < g_nchg; k++) {
                if ((g_chg[k].us > g_lines[a].us) && (g_chg[k].us <= g_lines[d].us)) {   /* start の前の settle の反映(同時刻)は除く */
                    b++;
                    CHECK((g_chg[k].us >= g_lines[c1].us && g_chg[k].us <= g_lines[c1].us + 40000u) ||
                          (g_chg[k].us >= g_lines[c2].us && g_chg[k].us <= g_lines[c2].us + 40000u));
                }
            }
            CHECK_EQ(b, 2);
        }
    }
    CHECK(strncmp(g_seq[2], "NBNBNB", 6) == 0 && g_seqn[2] == 6u);

    TEST("UC3 段階3: 11000 ms 後に stress_end(n = 500〜600、m = 0)、1000 ms 後に done、fb = a pal = a。毎回 NoReflect の直後に BufferChange");
    a = find(3, "start", 0, 0); CHECK(a >= 0);
    b = find(3, "stress_end", 0, 0); c = find(3, "done", 0, 0);
    CHECK(b >= 0 && c >= 0);
    if (a >= 0 && b >= 0 && c >= 0) {
        CHECK(labs(ms_between(a, b) - 11000) <= 60);
        printf("  段階3: n = %lu m = %lu\n", F(b, "n="), F(b, "m="));
        CHECK(F(b, "n=") >= 500u && F(b, "n=") <= 600u);
        CHECK_EQ(F(b, "m="), 0);
        CHECK_EQ(F(b, "rc="), 0);
        CHECK(labs(ms_between(b, c) - 1000) <= 60);
        CHECK(*fb(c) == 'a' && *pl(c) == 'a');
        CHECK(!(g_seqn[3] & 1u));
        for (k = 0u; (k + 1u) < g_seqn[3]; k += 2u) { if (g_seq[3][k] != 'N' || g_seq[3][k + 1u] != 'B') { CHECK(0); break; } }
        CHECK_EQ(g_seqn[3], 2u * (F(b, "n=") + 2u));                     /* settle + n flip + settle */
        CHECK_EQ(g_app_dbg.err_code, 0);
    }
    for (k = 0u, n_clut_lines = 0u; k < g_nlines; k++) { if (has_ev(g_lines[k].text, 3, "flip") || has_ev(g_lines[k].text, 3, "nr") || has_ev(g_lines[k].text, 3, "commit")) { n_clut_lines++; } }
    CHECK_EQ(n_clut_lines, 0);                                            /* 段階3の途中では [clut] を出さない */

    TEST("UC4 段階4: wr_unused の n = 1 - sel、wr_inuse の n = sel、2行の sel が同じ。bench の n = 2000。done の fb = a pal = a");
    a = find(4, "start", 0, 0); b = find(4, "wr_unused", 0, 0); c = find(4, "wr_inuse", 0, 0);
    CHECK(a >= 0 && b >= 0 && c >= 0);
    if (a >= 0 && b >= 0 && c >= 0) {
        int back = find(4, "wr_back", 0, 0), bn = find(4, "bench", 0, 0), d = find(4, "done", 0, 0);
        CHECK_EQ(F(b, "n="), 1u - F(b, "sel="));
        CHECK_EQ(F(c, "n="), F(c, "sel="));
        CHECK_EQ(F(b, "sel="), F(c, "sel="));
        CHECK(labs(ms_between(a, b) - 1000) <= 30);
        CHECK(labs(ms_between(b, c) - 3000) <= 30);
        CHECK(back >= 0 && bn >= 0 && d >= 0);
        if (back >= 0 && bn >= 0 && d >= 0) {
            CHECK(labs(ms_between(a, back) - 7000) <= 30);
            CHECK_EQ(F(bn, "n="), 2000);
            printf("  bench: m = %lu ms(模擬の時間なので意味は無い)\n", F(bn, "m="));
            CHECK(labs(ms_between(a, d) - 9000) <= 60);
            CHECK(*fb(d) == 'a' && *pl(d) == 'a');
            CHECK_EQ(F(b, "rc=") + F(c, "rc=") + F(bn, "rc="), 0);
        }
        CHECK(strncmp(g_seq[4], "NB", 2) == 0);
        {   /* NoReflect: settle 1 + bench 2000 + settle 1 */
            unsigned nn = 0u; for (k = 0u; k < g_seqn[4]; k++) { if (g_seq[4][k] == 'N') { nn++; } }
            CHECK_EQ(nn, 2002);
        }
    }

    TEST("UC5 段階5: nr1 の sel = 開始時の反転、nr2 の sel = 開始時の sel(落とし穴が SEL の値で見える)、どちらも ven = 0 rc = 0。settle の後 fb = a pal = a、sel は開始時の反転");
    a = find(5, "start", 0, 0); b = find(5, "nr1", 0, 0); c = find(5, "nr2", 0, 0);
    CHECK(a >= 0 && b >= 0 && c >= 0);
    if (a >= 0 && b >= 0 && c >= 0) {
        int s2 = find(5, "settle", 0, 0), d = find(5, "done", 0, 0);
        CHECK_EQ(F(b, "sel="), 1u - F(a, "sel="));
        CHECK_EQ(F(c, "sel="), F(a, "sel="));
        CHECK(F(b, "ven=") == 0 && F(c, "ven=") == 0 && F(b, "rc=") == 0 && F(c, "rc=") == 0);
        CHECK(labs(ms_between(b, c) - 500) <= 30);
        CHECK(s2 >= 0 && d >= 0);
        if (s2 >= 0 && d >= 0) {
            CHECK(*fb(s2) == 'a' && *pl(s2) == 'a');
            CHECK_EQ(F(s2, "sel="), 1u - F(a, "sel="));
            CHECK(labs(ms_between(a, d) - 5500) <= 60);
        }
        CHECK(strncmp(g_seq[5], "NBNNNB", 6) == 0);
    }

    TEST("段階の移動: 5 の次は 0(n)、0 の前は 5(p)、d で段階5 をやり直す。n の直後に新しい段階の ev=start");
    CHECK(find(0, "start", 1, 0) >= 0);                                   /* 56000 の n で 0 に戻る */
    CHECK(find(5, "start", 1, 0) >= 0);                                   /* 57000 の p で 5 */
    CHECK(find(5, "start", 2, 0) >= 0);                                   /* 58000 の d で 5 をやり直す */
    b = find(5, "state", 0, 0);
    CHECK(b >= 0);
    if (b >= 0) { CHECK(strstr(g_lines[b + 1].text, " scr=5 ") != NULL); }

    TEST("全体: 垂直同期ごとの 面とパレットの組み違いが 0、NoReflect / BufferChange の VEN = 1 での呼び出しが 0、パレットの引数は常に start 0 size 256、uf = 0、1段階あたり [clut] 9行以内");
    CHECK_EQ(g_inconsistent, 0);
    CHECK_EQ(g_nr_while_ven, 0);
    CHECK_EQ(g_bc_while_ven, 0);
    CHECK_EQ(g_bad_clut_args, 0);
    CHECK_EQ(g_app_dbg.uf, 0);
    for (k = 0u; k < g_nlines; k++) { if (strncmp(g_lines[k].text, "[clut]", 6) == 0) { CHECK_EQ(F((int)k, "rc="), 0); CHECK_EQ(F((int)k, "uf="), 0); } }
    CHECK(g_nlines < 4000u);
}

int main(void)
{
    test_cfg();
    test_run();
    TEST_REPORT("test_clut8_cfg");
}
