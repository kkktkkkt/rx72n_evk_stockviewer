/* test_lcd_panel.c - src/app/lcd_panel.c のホスト検証(静的レビュー項目 (b)(c) の自動化)
 *
 *  - FIT の実ヘッダ(src/smc_gen)と実 platform.h を使い、lcd_panel.c をそのままホストでコンパイルする。
 *  - R_GPIO_* / R_GLCDC_* / R_BSP_SoftwareDelay / memcpy は記録用の偽物に差し替え、呼び出し順を検査する。
 *  - R_GLCDC_Open に渡された glcdc_cfg_t を、r_glcdc_rx_config.h の既定値マクロ(LCD_CH0_*)と比較する。
 *  - 同じ cfg を、FIT の実ソース r_glcdc_private.c の r_glcdc_open_param_check() に通す
 *    (実機の R_GLCDC_Open が最初に行うパラメータ検査と同じ関数)。
 *
 * ビルド: tests/host/run.sh の S6 を参照(RX 用GCCではなくホストGCC。実機で動くことの証明ではない)。 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "test_framework.h"

/* GLCDC のレジスタ(GR2VEN)を偽物の構造体に差し替える(ホストでは実アドレスを読めない) */
#include "platform.h"
static struct st_glcdc g_fake_glcdc;
#undef GLCDC
#define GLCDC g_fake_glcdc
#include "../../src/app/lcd_panel.c"
#include "r_glcdc_private.h"

/* ---- 記録 ---- */
enum { EV_WRITE = 1, EV_DIR, EV_PINSET, EV_OPEN, EV_START, EV_DELAY };
typedef struct { int type; uint32_t a; uint32_t b; } ev_t;
static ev_t     g_ev[64];
static unsigned g_nev;
static glcdc_cfg_t g_open_cfg;
static glcdc_err_t g_open_ret  = GLCDC_SUCCESS;
static glcdc_err_t g_start_ret = GLCDC_SUCCESS;
static uint32_t g_err_code, g_err_detail;
static glcdc_err_t g_bc_ret = GLCDC_SUCCESS;
static glcdc_frame_layer_t g_bc_frame;
static const uint32_t *g_bc_base;
static unsigned g_bc_calls;
static glcdc_status_t g_status;
static glcdc_err_t g_status_ret = GLCDC_SUCCESS;
static unsigned g_clr_calls;
static glcdc_detect_cfg_t g_clr_arg;

static void ev(int type, uint32_t a, uint32_t b)
{
    if (g_nev < 64u) { g_ev[g_nev].type = type; g_ev[g_nev].a = a; g_ev[g_nev].b = b; }
    g_nev++;
}
static void ev_reset(void)
{
    g_nev = 0u; g_open_ret = GLCDC_SUCCESS; g_start_ret = GLCDC_SUCCESS;
    g_err_code = 0u; g_err_detail = 0u;
}

void R_GPIO_PinWrite(gpio_port_pin_t pin, gpio_level_t level) { ev(EV_WRITE, (uint32_t)pin, (uint32_t)level); }
void R_GPIO_PinDirectionSet(gpio_port_pin_t pin, gpio_dir_t dir) { ev(EV_DIR, (uint32_t)pin, (uint32_t)dir); }
void R_GLCDC_PinSet() { ev(EV_PINSET, 0u, 0u); }
glcdc_err_t R_GLCDC_Open(glcdc_cfg_t * const p_cfg)
{
    ev(EV_OPEN, 0u, 0u);
    g_open_cfg = *p_cfg;
    return g_open_ret;
}
glcdc_err_t R_GLCDC_Control(glcdc_control_cmd_t cmd, void const * const p_args)
{
    (void)p_args;
    if (cmd == GLCDC_CMD_CLR_DETECTED_STATUS) { g_clr_calls++; g_clr_arg = *(const glcdc_detect_cfg_t *)p_args; return GLCDC_SUCCESS; }
    ev(EV_START, (uint32_t)cmd, 0u);
    return g_start_ret;
}
bool R_BSP_SoftwareDelay(uint32_t delay, bsp_delay_units_t units) { ev(EV_DELAY, delay, (uint32_t)units); return true; }
void app_debug_set_err(uint32_t code, uint32_t detail) { g_err_code = code; g_err_detail = detail; }

glcdc_ctrl_t g_ctrl_blk;   /* r_glcdc_private.c が extern 参照する(本物は r_glcdc_rx.c) */

/* r_glcdc_private.c の中で、このテストでは呼ばれない関数が参照するもの。ホストの MinGW(PE/COFF)は
 * --gc-sections で未参照関数を除去しないため、リンクを通すために空の定義を置く */
void R_BSP_RegisterProtectEnable(bsp_reg_protect_t regs_to_protect) { (void)regs_to_protect; }
void R_BSP_RegisterProtectDisable(bsp_reg_protect_t regs_to_unprotect) { (void)regs_to_unprotect; }
bsp_int_err_t R_BSP_InterruptControl(bsp_int_src_t vector, bsp_int_cmd_t cmd, void *pdata) { (void)vector; (void)cmd; (void)pdata; return BSP_INT_SUCCESS; }
void r_glcdc_interrupt_setting(glcdc_interrupt_cfg_t const * const p_interrupt) { (void)p_interrupt; }

/* R_GLCDC_Open が検査の前に g_ctrl_blk に入れる値を同じに作る(r_glcdc_rx.c:R_GLCDC_Open と同じ式) */
static glcdc_err_t param_check(const glcdc_cfg_t *c)
{
    memset(&g_ctrl_blk, 0, sizeof(g_ctrl_blk));
    g_ctrl_blk.active_start_pos.x = (int16_t)(c->output.htiming.back_porch + c->output.htiming.sync_width);
    g_ctrl_blk.active_start_pos.y = (int16_t)(c->output.vtiming.back_porch + c->output.vtiming.sync_width);
    g_ctrl_blk.hsize = c->output.htiming.display_cyc;
    g_ctrl_blk.vsize = c->output.vtiming.display_cyc;
    g_ctrl_blk.graphics_read_enable[GLCDC_FRAME_LAYER_1] = (NULL != c->input[GLCDC_FRAME_LAYER_1].p_base);
    g_ctrl_blk.graphics_read_enable[GLCDC_FRAME_LAYER_2] = (NULL != c->input[GLCDC_FRAME_LAYER_2].p_base);
    return r_glcdc_open_param_check(c);
}

#define LVL_OFF_BL   ((LCD_BL_ON_LEVEL == GPIO_LEVEL_HIGH) ? GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH)
#define LVL_OFF_DISP ((LCD_DISP_ON_LEVEL == GPIO_LEVEL_HIGH) ? GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH)

_Static_assert(GLCDC_CFG_CONFIGURATION_MODE == 0, "設計 §6: MODE=0");
_Static_assert(LCD_FB_BYTES == 512u * 272u, "FB = 139,264 バイト(CLUT8、行幅 512)");
_Static_assert((LCD_FB0_ADDR % 64u) == 0u, "面0 は 64 バイト境界");
_Static_assert((LCD_FB1_ADDR % 64u) == 0u, "面1 は 64 バイト境界");
_Static_assert(LCD_FB1_ADDR == LCD_FB0_ADDR + 139264u, "面1 = 面0 + 139,264 = 0x00822000");
_Static_assert((LCD_LINE_BYTES % 64u) == 0u && LCD_LINE_BYTES >= LCD_WIDTH, "行オフセットは 64 の倍数で表示幅以上");
_Static_assert(LCD_FB0_ADDR >= 0x00800000u && (LCD_FB1_ADDR + LCD_FB_BYTES) <= 0x00880000u, "2面は RAM2(0x00800000-0x0087FFFF)内");

static void test_pins(void)
{
    TEST("ピン割り当て(回路図: BL=P67, DISP=PB3, タッチRST=P66) と極性");
    CHECK_EQ(PIN_LCD_BACKLIGHT, 0x0607);
    CHECK_EQ(PIN_TOUCH_RST,     0x0606);
    CHECK_EQ(PIN_LCD_DISP,      0x0B03);
    CHECK_EQ(LCD_BL_ON_LEVEL, GPIO_LEVEL_HIGH);        /* schematic-findings §3: High で点灯 */
    CHECK_EQ(TOUCH_SCI_IIC_CH, 6);
    CHECK_EQ(LCD_DISP_TO_BL_MS, 20);
}

static void test_init_order(void)
{
    TEST("(c) lcd_init の順序: GPIO安全状態 -> PinSet -> Open -> START -> DISP -> 待ち -> BL(v2: 画像コピーなし)");
    ev_reset();
    CHECK(lcd_init());
    CHECK_EQ(g_nev, 12);
    if (g_nev == 12u) {
        CHECK_EQ(g_ev[0].type, EV_WRITE);  CHECK_EQ(g_ev[0].a, PIN_LCD_BACKLIGHT); CHECK_EQ(g_ev[0].b, LVL_OFF_BL);
        CHECK_EQ(g_ev[1].type, EV_WRITE);  CHECK_EQ(g_ev[1].a, PIN_LCD_DISP);      CHECK_EQ(g_ev[1].b, LVL_OFF_DISP);
        CHECK_EQ(g_ev[2].type, EV_WRITE);  CHECK_EQ(g_ev[2].a, PIN_TOUCH_RST);     CHECK_EQ(g_ev[2].b, TOUCH_RST_ACTIVE_LEVEL);
        CHECK_EQ(g_ev[3].type, EV_DIR);    CHECK_EQ(g_ev[3].a, PIN_LCD_BACKLIGHT); CHECK_EQ(g_ev[3].b, GPIO_DIRECTION_OUTPUT);
        CHECK_EQ(g_ev[4].type, EV_DIR);    CHECK_EQ(g_ev[4].a, PIN_LCD_DISP);      CHECK_EQ(g_ev[4].b, GPIO_DIRECTION_OUTPUT);
        CHECK_EQ(g_ev[5].type, EV_DIR);    CHECK_EQ(g_ev[5].a, PIN_TOUCH_RST);     CHECK_EQ(g_ev[5].b, GPIO_DIRECTION_OUTPUT);
        CHECK_EQ(g_ev[6].type, EV_PINSET);
        CHECK_EQ(g_ev[7].type, EV_OPEN);
        CHECK_EQ(g_ev[8].type, EV_START);  CHECK_EQ(g_ev[8].a, GLCDC_CMD_START_DISPLAY);
        CHECK_EQ(g_ev[9].type, EV_WRITE);  CHECK_EQ(g_ev[9].a, PIN_LCD_DISP);      CHECK_EQ(g_ev[9].b, LCD_DISP_ON_LEVEL);
        CHECK_EQ(g_ev[10].type, EV_DELAY); CHECK_EQ(g_ev[10].a, 20); CHECK_EQ(g_ev[10].b, BSP_DELAY_MILLISECS);
        CHECK_EQ(g_ev[11].type, EV_WRITE); CHECK_EQ(g_ev[11].a, PIN_LCD_BACKLIGHT); CHECK_EQ(g_ev[11].b, LCD_BL_ON_LEVEL);
    }
    /* 出力にする前に出力値を書いている(ピンのばたつき防止) */
    if (g_nev >= 6u) {
        CHECK(g_ev[0].type == EV_WRITE && g_ev[3].type == EV_DIR);
    }
}

static void test_init_failures(void)
{
    unsigned i;
    int bl_on_writes = 0;
    TEST("lcd_init 失敗時: Open 失敗 / START 失敗 では BL を点けず false、err_code に記録");
    ev_reset();
    g_open_ret = GLCDC_ERR_INVALID_TIMING_SETTING;
    CHECK(!lcd_init());
    CHECK_EQ(g_err_code, APP_ERR_LCD_OPEN);
    CHECK_EQ(g_err_detail, GLCDC_ERR_INVALID_TIMING_SETTING);
    for (i = 0u; i < g_nev; i++) {
        if (g_ev[i].type == EV_START) { bl_on_writes += 100; }
        if (g_ev[i].type == EV_WRITE && g_ev[i].a == PIN_LCD_BACKLIGHT && g_ev[i].b == LCD_BL_ON_LEVEL) { bl_on_writes++; }
    }
    CHECK_EQ(bl_on_writes, 0);

    ev_reset();
    g_start_ret = GLCDC_ERR_INVALID_MODE;
    CHECK(!lcd_init());
    CHECK_EQ(g_err_code, APP_ERR_LCD_START);
    bl_on_writes = 0;
    for (i = 0u; i < g_nev; i++) {
        if (g_ev[i].type == EV_WRITE && g_ev[i].a == PIN_LCD_BACKLIGHT && g_ev[i].b == LCD_BL_ON_LEVEL) { bl_on_writes++; }
        if (g_ev[i].type == EV_WRITE && g_ev[i].a == PIN_LCD_DISP && g_ev[i].b == LCD_DISP_ON_LEVEL) { bl_on_writes += 10; }
    }
    CHECK_EQ(bl_on_writes, 0);
}

#define PB(x) ((uint32_t)(uintptr_t)(x))

/* ---- 2面と入れ替え(v2): R_GLCDC_BufferChange / GetStatus / Control の偽物 ---- */
static char g_seq[64];                 /* GLCDC の書き込み系 API の呼び出し順: C = ClutUpdate_NoReflect(成功)、c = 同(失敗・反映待ち)、B = BufferChange、U = ClutUpdate(反映つき。呼んではいけない) */
static unsigned g_seq_n;
static void seq_add(char c) { if (g_seq_n < sizeof(g_seq) - 1u) { g_seq[g_seq_n++] = c; g_seq[g_seq_n] = '\0'; } }
static glcdc_err_t g_clut_ret = GLCDC_SUCCESS;
static unsigned g_clut_calls;
static glcdc_frame_layer_t g_clut_frame;
static glcdc_clut_cfg_t g_clut_cfg;

glcdc_err_t R_GLCDC_BufferChange(glcdc_frame_layer_t frame, uint32_t const * const p_base)
{
    g_bc_calls++; g_bc_frame = frame; g_bc_base = p_base;
    seq_add('B');
    return g_bc_ret;
}
glcdc_err_t R_GLCDC_ClutUpdate_NoReflect(glcdc_frame_layer_t frame, glcdc_clut_cfg_t const * const p_clut_cfg)
{
    g_clut_calls++; g_clut_frame = frame; g_clut_cfg = *p_clut_cfg;
    seq_add((g_clut_ret == GLCDC_SUCCESS) ? 'C' : 'c');
    return g_clut_ret;
}
glcdc_err_t R_GLCDC_ClutUpdate(glcdc_frame_layer_t frame, glcdc_clut_cfg_t const * const p_clut_cfg)
{
    (void)frame; (void)p_clut_cfg;
    seq_add('U');
    return GLCDC_SUCCESS;
}
glcdc_err_t R_GLCDC_GetStatus(glcdc_status_t * const p_status)
{
    *p_status = g_status;
    return g_status_ret;
}

static void test_flip(void)
{
    TEST("lcd_fb: 面0 = 0x00800000、面1 = 0x00822000(+139,264)。範囲外の番号は面1");
    CHECK_EQ(PB(lcd_fb(0u)), 0x00800000u);
    CHECK_EQ(PB(lcd_fb(1u)), 0x00822000u);
    CHECK_EQ(PB(lcd_fb(2u)), 0x00822000u);

    TEST("lcd_flip: GR2 の BufferChange を面のアドレスで呼ぶ。成功 = OK、反映待ち = BUSY(エラー記録なし)、他の失敗 = FAIL(APP_ERR_LCD_FLIP を記録)");
    ev_reset(); g_bc_calls = 0u; g_bc_ret = GLCDC_SUCCESS;
    CHECK_EQ(lcd_flip(1u), LCD_FLIP_OK);
    CHECK_EQ(g_bc_calls, 1);
    CHECK_EQ(g_bc_frame, GLCDC_FRAME_LAYER_2);
    CHECK_EQ(PB(g_bc_base), 0x00822000u);
    CHECK_EQ(lcd_flip(0u), LCD_FLIP_OK);
    CHECK_EQ(PB(g_bc_base), 0x00800000u);
    g_bc_ret = GLCDC_ERR_INVALID_UPDATE_TIMING;
    CHECK_EQ(lcd_flip(1u), LCD_FLIP_BUSY);
    CHECK_EQ(g_err_code, 0);
    g_bc_ret = GLCDC_ERR_NOT_OPEN;
    CHECK_EQ(lcd_flip(1u), LCD_FLIP_FAIL);
    CHECK_EQ(g_err_code, APP_ERR_LCD_FLIP);
    CHECK_EQ(g_err_detail, GLCDC_ERR_NOT_OPEN);
    g_bc_ret = GLCDC_SUCCESS;
    /* 渡すアドレスは R_GLCDC_BufferChange の検査(64バイト境界)を通る */
    CHECK_EQ(PB(lcd_fb(0u)) & 63u, 0);
    CHECK_EQ(PB(lcd_fb(1u)) & 63u, 0);

    TEST("lcd_flip_busy: GLCDC.GR2VEN.VEN を読む");
    g_fake_glcdc.GR2VEN.BIT.VEN = 0u;
    CHECK(!lcd_flip_busy());
    g_fake_glcdc.GR2VEN.BIT.VEN = 1u;
    CHECK(lcd_flip_busy());
    g_fake_glcdc.GR2VEN.BIT.VEN = 0u;

    TEST("lcd_underflow_poll: 検出があれば 1 を返して GR2 の検出だけクリア。無ければ 0(クリアしない)。取得失敗は 0");
    memset(&g_status, 0, sizeof(g_status));
    g_status.state_gr2uf = GLCDC_NOT_DETECTED;
    g_clr_calls = 0u;
    CHECK_EQ(lcd_underflow_poll(), 0);
    CHECK_EQ(g_clr_calls, 0);
    g_status.state_gr2uf = GLCDC_DETECTED;
    CHECK_EQ(lcd_underflow_poll(), 1);
    CHECK_EQ(g_clr_calls, 1);
    CHECK_EQ(g_clr_arg.gr2uf_detect, 1);
    CHECK_EQ(g_clr_arg.gr1uf_detect, 0);
    CHECK_EQ(g_clr_arg.vpos_detect, 0);
    g_status_ret = GLCDC_ERR_NOT_OPEN;
    CHECK_EQ(lcd_underflow_poll(), 0);
    g_status_ret = GLCDC_SUCCESS;
}


static void test_cfg_vs_fit_defaults(void)
{
    const glcdc_cfg_t *c = &g_open_cfg;
    TEST("(b) R_GLCDC_Open に渡す cfg と r_glcdc_rx_config.h の既定値(LCD_CH0_*)の一致");
    ev_reset();
    CHECK(lcd_init());
    CHECK_EQ(c->output.htiming.front_porch, LCD_CH0_W_HFP);
    CHECK_EQ(c->output.htiming.back_porch,  LCD_CH0_W_HBP);
    CHECK_EQ(c->output.htiming.display_cyc, LCD_CH0_DISP_HW);
    CHECK_EQ(c->output.htiming.sync_width,  LCD_CH0_W_HSYNC);
    CHECK_EQ(c->output.vtiming.front_porch, LCD_CH0_W_VFP);
    CHECK_EQ(c->output.vtiming.back_porch,  LCD_CH0_W_VBP);
    CHECK_EQ(c->output.vtiming.display_cyc, LCD_CH0_DISP_VW);
    CHECK_EQ(c->output.vtiming.sync_width,  LCD_CH0_W_VSYNC);
    CHECK_EQ(c->output.format,      LCD_CH0_OUT_FORMAT);
    CHECK_EQ(c->output.endian,      LCD_CH0_OUT_ENDIAN);
    CHECK_EQ(c->output.color_order, LCD_CH0_OUT_COLOR_ORDER);
    CHECK_EQ(c->output.sync_edge,   LCD_CH0_OUT_EDGE);
    CHECK_EQ(c->output.bg_color.argb, LCD_CH0_OUT_BG_COLOR);
    CHECK_EQ(c->output.tcon_hsync,  LCD_CH0_TCON_PIN_HSYNC);
    CHECK_EQ(c->output.tcon_vsync,  LCD_CH0_TCON_PIN_VSYNC);
    CHECK_EQ(c->output.tcon_de,     LCD_CH0_TCON_PIN_DE);
    CHECK_EQ(c->output.hsync_polarity,       LCD_CH0_TCON_POL_HSYNC);
    CHECK_EQ(c->output.vsync_polarity,       LCD_CH0_TCON_POL_VSYNC);
    CHECK_EQ(c->output.data_enable_polarity, LCD_CH0_TCON_POL_DE);
    CHECK_EQ(c->output.clock_div_ratio,      LCD_CH0_OUT_CLK_DIV_RATIO);
    CHECK_EQ(c->output.clksrc, GLCDC_CLK_SRC_INTERNAL);

    CHECK_EQ(PB(c->input[GLCDC_FRAME_LAYER_2].p_base), LCD_CH0_IN_GR2_PBASE);
    CHECK_EQ(c->input[GLCDC_FRAME_LAYER_2].hsize,  LCD_CH0_IN_GR2_HSIZE);
    CHECK_EQ(c->input[GLCDC_FRAME_LAYER_2].vsize,  LCD_CH0_IN_GR2_VSIZE);
    /* offset / format は FIT 既定(RGB565・960)から CLUT8・512 に変えた(設計 §20.5)。test_cfg_vs_design で検査する */
    CHECK_EQ(c->input[GLCDC_FRAME_LAYER_2].coordinate.x, LCD_CH0_IN_GR2_COORD_X);
    CHECK_EQ(c->input[GLCDC_FRAME_LAYER_2].coordinate.y, LCD_CH0_IN_GR2_COORD_Y);
    CHECK_EQ(c->input[GLCDC_FRAME_LAYER_2].frame_edge,   LCD_CH0_IN_GR2_FRAME_EDGE);
    CHECK_EQ(c->blend[GLCDC_FRAME_LAYER_2].visible,       LCD_CH0_BLEND_GR2_VISIBLE);
    CHECK_EQ(c->blend[GLCDC_FRAME_LAYER_2].blend_control, LCD_CH0_BLEND_GR2_BLEND_CONTROL);
    CHECK_EQ(c->blend[GLCDC_FRAME_LAYER_2].fixed_blend_value, LCD_CH0_BLEND_GR2_FIXED_BLEND_VALUE);
    CHECK_EQ(c->chromakey[GLCDC_FRAME_LAYER_2].enable, LCD_CH0_CHROMAKEY_GR2_ENABLE);
    /* clut は FIT 既定(無効)から有効に変えた(設計 §20.5)。test_cfg_vs_design で検査する */

    CHECK(c->input[GLCDC_FRAME_LAYER_1].p_base == NULL);       /* GR1 は使わない */
    CHECK_EQ(c->input[GLCDC_FRAME_LAYER_1].hsize, LCD_CH0_IN_GR1_HSIZE);

    CHECK_EQ(c->detection.vpos_detect,  0);
    CHECK_EQ(c->detection.gr1uf_detect, 0);
    CHECK_EQ(c->detection.gr2uf_detect, 1);                    /* v2: GR2 のアンダーフローだけ検出(割り込みは使わない) */
    CHECK_EQ(c->interrupt.vpos_enable,  0);
    CHECK_EQ(c->interrupt.gr1uf_enable, 0);
    CHECK_EQ(c->interrupt.gr2uf_enable, 0);
    CHECK(c->p_callback == NULL);

    /* FIT既定値からの意図的な差(実装メモ §6-3): 補正を全部 無効にしている */
    CHECK_EQ(c->output.brightness.enable, 0);
    CHECK_EQ(c->output.contrast.enable,   0);
    CHECK_EQ(c->output.gamma.enable,      0);
    CHECK_EQ(c->output.dithering.dithering_on, 0);
    CHECK_EQ(IMGC_BRIGHT_OUTCTL_ACTIVE, 1);   /* 参考: FIT既定では有効 */
}

static void test_cfg_vs_design(void)
{
    const glcdc_cfg_t *c = &g_open_cfg;
    TEST("(b) cfg と設計 §3/§6 の値(HFP17/HBP62/HSYNC25, VFP8/VBP7/VSYNC1, ÷24, 出力 RGB565, Low-active sync, DE High)。GR2 = CLUT8(H-D6)");
    ev_reset();
    CHECK(lcd_init());
    CHECK_EQ(c->output.htiming.front_porch, 17);
    CHECK_EQ(c->output.htiming.back_porch, 62);
    CHECK_EQ(c->output.htiming.sync_width, 25);
    CHECK_EQ(c->output.htiming.display_cyc, 480);
    CHECK_EQ(c->output.vtiming.front_porch, 8);
    CHECK_EQ(c->output.vtiming.back_porch, 7);
    CHECK_EQ(c->output.vtiming.sync_width, 1);
    CHECK_EQ(c->output.vtiming.display_cyc, 272);
    CHECK_EQ(c->output.clock_div_ratio, GLCDC_PANEL_CLK_DIVISOR_24);
    CHECK_EQ(c->output.hsync_polarity, GLCDC_SIGNAL_POLARITY_LOACTIVE);
    CHECK_EQ(c->output.vsync_polarity, GLCDC_SIGNAL_POLARITY_LOACTIVE);
    CHECK_EQ(c->output.data_enable_polarity, GLCDC_SIGNAL_POLARITY_HIACTIVE);
    CHECK_EQ(c->output.sync_edge, GLCDC_SIGNAL_SYNC_EDGE_RISING);
    CHECK_EQ(c->output.format, GLCDC_OUT_FORMAT_16BITS_RGB565);
    /* H-D6: GR2 は CLUT8(8bpp)。行幅 512・表示 480 x 272、面0 = 0x00800000、パレットは ui_pal の 256 エントリー(Open の時に1回) */
    CHECK_EQ(c->input[GLCDC_FRAME_LAYER_2].format, GLCDC_IN_FORMAT_CLUT8);
    CHECK_EQ(PB(c->input[GLCDC_FRAME_LAYER_2].p_base), 0x00800000u);
    CHECK_EQ(c->input[GLCDC_FRAME_LAYER_2].offset, 512);
    CHECK_EQ(c->input[GLCDC_FRAME_LAYER_2].hsize, 480);
    CHECK_EQ(c->input[GLCDC_FRAME_LAYER_2].vsize, 272);
    CHECK_EQ(c->clut[GLCDC_FRAME_LAYER_2].enable, 1);
    CHECK(c->clut[GLCDC_FRAME_LAYER_2].p_base == ui_pal);
    CHECK_EQ(c->clut[GLCDC_FRAME_LAYER_2].start, 0);
    CHECK_EQ(c->clut[GLCDC_FRAME_LAYER_2].size, 256);
    CHECK_EQ(PB(lcd_fb(0u)) & 63u, 0);
    CHECK_EQ(PB(lcd_fb(1u)) & 63u, 0);
    CHECK_EQ(c->clut[GLCDC_FRAME_LAYER_1].enable, 0);          /* GR1 は使わない */
    /* 総サイクル: 横 584 クロック x 縦 288 ライン、ドット 10MHz -> フレーム周波数 */
    {
        unsigned ht = c->output.htiming.front_porch + c->output.htiming.sync_width + c->output.htiming.display_cyc + c->output.htiming.back_porch;
        unsigned vt = c->output.vtiming.front_porch + c->output.vtiming.sync_width + c->output.vtiming.display_cyc + c->output.vtiming.back_porch;
        double fps = 240.0e6 / 24.0 / (double)ht / (double)vt;
        printf("  参考: 総 %u x %u, ドットクロック 240MHz/24=10MHz -> %.2f Hz\n", ht, vt, fps);
        CHECK_EQ(ht, 584);
        CHECK_EQ(vt, 288);
        CHECK(fps > 55.0 && fps < 65.0);
    }
}

static void test_fit_param_check(void)
{
    glcdc_cfg_t c;
    TEST("(b) 実 FIT の r_glcdc_open_param_check() に cfg を通す(正常) / 壊した cfg は検出される(対照)");
    ev_reset();
    CHECK(lcd_init());
    c = g_open_cfg;
    CHECK_EQ(param_check(&c), GLCDC_SUCCESS);

    /* 対照実験: 検査が本当に効いていること */
    c = g_open_cfg; c.input[GLCDC_FRAME_LAYER_2].offset = 513;
    CHECK_EQ(param_check(&c), GLCDC_ERR_INVALID_ALIGNMENT);
    c = g_open_cfg; c.input[GLCDC_FRAME_LAYER_2].offset = 480;      /* 64 の倍数でない(CLUT8 の 480 は不可。実験 §19 のとおり) */
    CHECK_EQ(param_check(&c), GLCDC_ERR_INVALID_ALIGNMENT);
    c = g_open_cfg; c.input[GLCDC_FRAME_LAYER_2].p_base = (uint32_t *)(uintptr_t)0x00800020u;
    CHECK_EQ(param_check(&c), GLCDC_ERR_INVALID_ALIGNMENT);
    c = g_open_cfg; c.input[GLCDC_FRAME_LAYER_2].p_base = (uint32_t *)(uintptr_t)0x00800040u;
    CHECK_EQ(param_check(&c), GLCDC_SUCCESS);
    c = g_open_cfg; c.input[GLCDC_FRAME_LAYER_2].p_base = (uint32_t *)(uintptr_t)LCD_FB1_ADDR;   /* 面1 も実 FIT の検査を通る */
    CHECK_EQ(param_check(&c), GLCDC_SUCCESS);
    c = g_open_cfg; c.input[GLCDC_FRAME_LAYER_2].hsize = 481;
    CHECK(param_check(&c) != GLCDC_SUCCESS);
    c = g_open_cfg; c.output.htiming.front_porch = 2;
    CHECK_EQ(param_check(&c), GLCDC_ERR_INVALID_TIMING_SETTING);
    c = g_open_cfg; c.output.vtiming.back_porch = 0;
    CHECK_EQ(param_check(&c), GLCDC_ERR_INVALID_TIMING_SETTING);
    c = g_open_cfg; c.output.brightness.enable = true; c.output.brightness.r = 1024;
    CHECK_EQ(param_check(&c), GLCDC_ERR_INVALID_ARG);
}

static void seq_reset(void) { g_seq_n = 0u; g_seq[0] = '\0'; g_bc_calls = 0u; g_clut_calls = 0u; g_err_code = 0u; g_err_detail = 0u; }

static void test_flip_pal(void)
{
    static uint32_t pal[256];
    lcd_flip_t r;

    TEST("H-C9 lcd_flip_pal (i): パレットが変わる入れ替えで ClutUpdate_NoReflect がちょうど 1 回、その直後に BufferChange(GR2・面のアドレス)。ClutUpdate(反映つき)は呼ばない");
    seq_reset(); g_clut_ret = GLCDC_SUCCESS; g_bc_ret = GLCDC_SUCCESS;
    r = lcd_flip_pal(1u, pal);
    CHECK_EQ(r, LCD_FLIP_OK);
    CHECK(strcmp(g_seq, "CB") == 0);
    CHECK_EQ(g_clut_calls, 1);
    CHECK_EQ(g_bc_calls, 1);
    CHECK_EQ(g_clut_frame, GLCDC_FRAME_LAYER_2);
    CHECK_EQ(g_clut_cfg.enable, 1);
    CHECK(g_clut_cfg.p_base == pal);
    CHECK_EQ(g_clut_cfg.start, 0);
    CHECK_EQ(g_clut_cfg.size, 256);
    CHECK_EQ(g_bc_frame, GLCDC_FRAME_LAYER_2);
    CHECK_EQ(PB(g_bc_base), 0x00822000u);
    CHECK_EQ(g_err_code, 0);
    seq_reset();
    CHECK_EQ(lcd_flip_pal(0u, pal), LCD_FLIP_OK);
    CHECK(strcmp(g_seq, "CB") == 0);
    CHECK_EQ(PB(g_bc_base), 0x00800000u);

    TEST("H-C9 (ii) 同じパレットなら lcd_flip だけ(BufferChange だけで NoReflect を呼ばない)");
    seq_reset();
    CHECK_EQ(lcd_flip(1u), LCD_FLIP_OK);
    CHECK(strcmp(g_seq, "B") == 0);

    TEST("H-C9 (iii) NoReflect が反映待ち(INVALID_UPDATE_TIMING)なら BufferChange を呼ばず BUSY(何も記録しない)。やり直しても NoReflect は成功まで 1 回ずつ");
    seq_reset(); g_clut_ret = GLCDC_ERR_INVALID_UPDATE_TIMING;
    CHECK_EQ(lcd_flip_pal(1u, pal), LCD_FLIP_BUSY);
    CHECK(strcmp(g_seq, "c") == 0);
    CHECK_EQ(g_err_code, 0);
    CHECK_EQ(lcd_flip_pal(1u, pal), LCD_FLIP_BUSY);                 /* 再試行(NoReflect は成功していないので、2 回目ではない) */
    CHECK(strcmp(g_seq, "cc") == 0);
    g_clut_ret = GLCDC_SUCCESS;
    CHECK_EQ(lcd_flip_pal(1u, pal), LCD_FLIP_OK);
    CHECK(strcmp(g_seq, "ccCB") == 0);                              /* 成功した 1 回の後に BufferChange が 1 回 */

    TEST("H-C9 (iv) NoReflect の失敗(反映待ち以外)= APP_ERR_LCD_CLUT を記録して FAIL。BufferChange は呼ばない");
    seq_reset(); g_clut_ret = GLCDC_ERR_INVALID_CLUT_ACCESS;
    CHECK_EQ(lcd_flip_pal(1u, pal), LCD_FLIP_FAIL);
    CHECK(strcmp(g_seq, "c") == 0);
    CHECK_EQ(g_err_code, APP_ERR_LCD_CLUT);
    CHECK_EQ(g_err_detail, GLCDC_ERR_INVALID_CLUT_ACCESS);
    CHECK_EQ(APP_ERR_LCD_CLUT, 11);
    seq_reset(); g_clut_ret = GLCDC_ERR_NOT_OPEN;
    CHECK_EQ(lcd_flip_pal(1u, pal), LCD_FLIP_FAIL);
    CHECK_EQ(g_err_code, APP_ERR_LCD_CLUT);

    TEST("H-C9 (v) NoReflect 成功のあと BufferChange が失敗(反映待ちを含む)= APP_ERR_LCD_FLIP を記録して FAIL(BUSY にしない。再試行すると NoReflect が 2 回になる)");
    seq_reset(); g_clut_ret = GLCDC_SUCCESS; g_bc_ret = GLCDC_ERR_INVALID_UPDATE_TIMING;
    CHECK_EQ(lcd_flip_pal(1u, pal), LCD_FLIP_FAIL);
    CHECK(strcmp(g_seq, "CB") == 0);
    CHECK_EQ(g_err_code, APP_ERR_LCD_FLIP);
    CHECK_EQ(g_err_detail, GLCDC_ERR_INVALID_UPDATE_TIMING);
    seq_reset(); g_bc_ret = GLCDC_ERR_NOT_OPEN;
    CHECK_EQ(lcd_flip_pal(1u, pal), LCD_FLIP_FAIL);
    CHECK_EQ(g_err_code, APP_ERR_LCD_FLIP);
    CHECK_EQ(g_err_detail, GLCDC_ERR_NOT_OPEN);
    g_bc_ret = GLCDC_SUCCESS;

    TEST("H-C9 (vi) ClutUpdate(反映つき。VEN が立って続く BufferChange が BUSY になる)を、どの経路でも一度も呼ばない");
    CHECK(strchr(g_seq, 'U') == NULL);
    seq_reset();
    (void)lcd_flip_pal(1u, pal); (void)lcd_flip(0u); g_clut_ret = GLCDC_ERR_INVALID_UPDATE_TIMING; (void)lcd_flip_pal(1u, pal);
    g_clut_ret = GLCDC_ERR_INVALID_CLUT_ACCESS; (void)lcd_flip_pal(1u, pal); g_clut_ret = GLCDC_SUCCESS;
    CHECK(strchr(g_seq, 'U') == NULL);
    CHECK(strcmp(g_seq, "CBBcc") == 0);                             /* 成功した NoReflect(C)は必ず直後に BufferChange(B)。失敗・BUSY(c)の後には B が続かない */
    CHECK(strstr(g_seq, "CC") == NULL);                             /* 成功した NoReflect が続けて 2 回になる経路は無い */
}

int main(void)
{
    test_pins();
    test_init_order();
    test_init_failures();
    test_flip();
    test_flip_pal();
    test_cfg_vs_fit_defaults();
    test_cfg_vs_design();
    test_fit_param_check();
    TEST_REPORT("test_lcd_panel");
}
