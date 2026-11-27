/* lcd_panel.c - 2面(RAM2、CLUT8 = 8bpp)の GLCDC。GLCDC設定は GLCDC_CFG_CONFIGURATION_MODE = 0(コードで glcdc_cfg_t を埋める)
 * 値の出典: FIT既定値 = LVGL公式ポート(RX72N Envision Kit)の設定 (docs/design.md §3)。
 * パネルのデータシートとは未照合。 */
#include <string.h>
#include "platform.h"
#include "r_glcdc_rx_if.h"
#include "r_glcdc_rx_pinset.h"
#include "board_pins.h"
#include "app_debug.h"
#include "ui_assets.h"
#include "lcd_panel.h"
#if APP_CLUT8_TEST
#include "clut8_test.h"
#endif

static glcdc_cfg_t s_cfg;

static void lcd_gpio_init(void)
{
    /* 出力値を先に決めてから出力にする(ピンのばたつき防止) */
    R_GPIO_PinWrite(PIN_LCD_BACKLIGHT, (LCD_BL_ON_LEVEL == GPIO_LEVEL_HIGH) ? GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH);
    R_GPIO_PinWrite(PIN_LCD_DISP,      (LCD_DISP_ON_LEVEL == GPIO_LEVEL_HIGH) ? GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH);
    R_GPIO_PinWrite(PIN_TOUCH_RST,     TOUCH_RST_ACTIVE_LEVEL);
    R_GPIO_PinDirectionSet(PIN_LCD_BACKLIGHT, GPIO_DIRECTION_OUTPUT);
    R_GPIO_PinDirectionSet(PIN_LCD_DISP,      GPIO_DIRECTION_OUTPUT);
    R_GPIO_PinDirectionSet(PIN_TOUCH_RST,     GPIO_DIRECTION_OUTPUT);
}

static void lcd_build_cfg(glcdc_cfg_t *c)
{
    memset(c, 0, sizeof(*c));

    /* ---- 出力タイミング(HFP17/HBP62/HSYNC25, VFP8/VBP7/VSYNC1, ÷24 = 10MHz) ---- */
    c->output.htiming.display_cyc = (uint16_t)LCD_WIDTH;
    c->output.htiming.front_porch = 17u;
    c->output.htiming.back_porch  = 62u;
    c->output.htiming.sync_width  = 25u;
    c->output.vtiming.display_cyc = (uint16_t)LCD_HEIGHT;
    c->output.vtiming.front_porch = 8u;
    c->output.vtiming.back_porch  = 7u;
    c->output.vtiming.sync_width  = 1u;

    c->output.format      = GLCDC_OUT_FORMAT_16BITS_RGB565;
    c->output.endian      = GLCDC_ENDIAN_LITTLE;
    c->output.color_order = GLCDC_COLOR_ORDER_RGB;
    c->output.sync_edge   = GLCDC_SIGNAL_SYNC_EDGE_RISING;
    c->output.bg_color.argb = 0x00000000u;

    /* 補正は使わない(ガンマ表は MODE=0 では公開されない)。 */
    c->output.brightness.enable = false;
    c->output.contrast.enable   = false;
    c->output.gamma.enable      = false;
    c->output.correction_proc_order = GLCDC_BRIGHTNESS_CONTRAST_TO_GAMMA;
    c->output.dithering.dithering_on   = false;
    c->output.dithering.dithering_mode = GLCDC_DITHERING_MODE_TRUNCATE;

    c->output.tcon_hsync = GLCDC_TCON_PIN_2;
    c->output.tcon_vsync = GLCDC_TCON_PIN_0;
    c->output.tcon_de    = GLCDC_TCON_PIN_3;
    c->output.hsync_polarity       = GLCDC_SIGNAL_POLARITY_LOACTIVE;
    c->output.vsync_polarity       = GLCDC_SIGNAL_POLARITY_LOACTIVE;
    c->output.data_enable_polarity = GLCDC_SIGNAL_POLARITY_HIACTIVE;

    c->output.clksrc          = GLCDC_CLK_SRC_INTERNAL;
    c->output.clock_div_ratio = GLCDC_PANEL_CLK_DIVISOR_24;

    /* ---- GR2: フレームバッファ(RAM2)。CLUT8: 1画素 = 1バイト、行幅 512、表示は左 480 列(docs/design.md §20.5) ---- */
    c->input[GLCDC_FRAME_LAYER_2].p_base     = (uint32_t *)LCD_FB0_ADDR;
    c->input[GLCDC_FRAME_LAYER_2].hsize      = (uint16_t)LCD_WIDTH;
    c->input[GLCDC_FRAME_LAYER_2].vsize      = (uint16_t)LCD_HEIGHT;
    c->input[GLCDC_FRAME_LAYER_2].offset     = (int32_t)LCD_LINE_BYTES;
    c->input[GLCDC_FRAME_LAYER_2].format     = GLCDC_IN_FORMAT_CLUT8;
    c->input[GLCDC_FRAME_LAYER_2].frame_edge = false;
    c->input[GLCDC_FRAME_LAYER_2].coordinate.x = 0;
    c->input[GLCDC_FRAME_LAYER_2].coordinate.y = 0;
    c->input[GLCDC_FRAME_LAYER_2].bg_color.argb = 0x00000000u;

    c->blend[GLCDC_FRAME_LAYER_2].visible           = true;
    c->blend[GLCDC_FRAME_LAYER_2].blend_control     = GLCDC_BLEND_CONTROL_NONE;
    c->blend[GLCDC_FRAME_LAYER_2].frame_edge        = false;
    c->blend[GLCDC_FRAME_LAYER_2].fixed_blend_value = 255u;
    c->blend[GLCDC_FRAME_LAYER_2].fade_speed        = 255u;

    c->chromakey[GLCDC_FRAME_LAYER_2].enable = false;
    c->clut[GLCDC_FRAME_LAYER_2].enable      = true;                 /* パレットは Open の時に1回だけ書く(実行中は書き換えない) */
    c->clut[GLCDC_FRAME_LAYER_2].p_base      = (uint32_t *)(uintptr_t)ui_pal;
    c->clut[GLCDC_FRAME_LAYER_2].start       = 0u;
    c->clut[GLCDC_FRAME_LAYER_2].size        = 256u;

    /* ---- GR1: 使わない(p_base = NULL) ---- */
    c->input[GLCDC_FRAME_LAYER_1].p_base = NULL;
    c->input[GLCDC_FRAME_LAYER_1].hsize  = (uint16_t)LCD_WIDTH;
    c->input[GLCDC_FRAME_LAYER_1].vsize  = (uint16_t)LCD_HEIGHT;
    c->input[GLCDC_FRAME_LAYER_1].offset = (int32_t)(LCD_WIDTH * 2u);    /* 使わない面。FIT の検査を通る値のまま */
    c->input[GLCDC_FRAME_LAYER_1].format = GLCDC_IN_FORMAT_16BITS_RGB565;

    c->blend[GLCDC_FRAME_LAYER_1].visible           = true;
    c->blend[GLCDC_FRAME_LAYER_1].blend_control     = GLCDC_BLEND_CONTROL_NONE;
    c->blend[GLCDC_FRAME_LAYER_1].fixed_blend_value = 255u;
    c->blend[GLCDC_FRAME_LAYER_1].fade_speed        = 255u;
    c->clut[GLCDC_FRAME_LAYER_1].enable = false;
    c->clut[GLCDC_FRAME_LAYER_1].size   = 256u;

    /* ---- 割り込み・コールバックは使わない。GR2 のアンダーフローだけ検出する(lcd_underflow_poll で読む) ---- */
    c->detection.vpos_detect  = false;
    c->detection.gr1uf_detect = false;
    c->detection.gr2uf_detect = true;
    c->interrupt.vpos_enable  = false;
    c->interrupt.gr1uf_enable = false;
    c->interrupt.gr2uf_enable = false;
    c->p_callback = NULL;
}

uint8_t *lcd_fb(uint8_t i)
{
    return (uint8_t *)(uintptr_t)((i == 0u) ? LCD_FB0_ADDR : LCD_FB1_ADDR);
}

/* 裏面 i を次の垂直同期で表示面にする。反映待ち(INVALID_UPDATE_TIMING)は BUSY。それ以外の失敗は記録して FAIL */
lcd_flip_t lcd_flip(uint8_t i)
{
    glcdc_err_t err = R_GLCDC_BufferChange(GLCDC_FRAME_LAYER_2, (uint32_t const *)lcd_fb(i));

    if (err == GLCDC_SUCCESS) {
        return LCD_FLIP_OK;
    }
    if (err == GLCDC_ERR_INVALID_UPDATE_TIMING) {
        return LCD_FLIP_BUSY;
    }
    app_debug_set_err(APP_ERR_LCD_FLIP, (uint32_t)err);
    return LCD_FLIP_FAIL;
}

/* 反映待ちの間 VEN = 1(R_GLCDC_BufferChange が INVALID_UPDATE_TIMING を返す条件と同じレジスタ) */
bool lcd_flip_busy(void)
{
    return GLCDC.GR2VEN.BIT.VEN != 0u;
}

/* GR2 アンダーフローの検出があれば 1 を返してクリア。無ければ 0 */
uint32_t lcd_underflow_poll(void)
{
    glcdc_status_t     st;
    glcdc_detect_cfg_t clr;

    if ((R_GLCDC_GetStatus(&st) != GLCDC_SUCCESS) || (st.state_gr2uf != GLCDC_DETECTED)) {
        return 0u;
    }
    clr.vpos_detect  = false;
    clr.gr1uf_detect = false;
    clr.gr2uf_detect = true;
    (void)R_GLCDC_Control(GLCDC_CMD_CLR_DETECTED_STATUS, &clr);
    return 1u;
}

bool lcd_init(void)
{
    glcdc_err_t err;

    /* 1. GPIO: バックライト消灯 / DISP無効 / タッチRSTアクティブ */
    lcd_gpio_init();

    /* 2. (v2 では画像をコピーしない。面0は表示開始前に app_main が ui_render で描いておく) */

    /* 3. LCD端子。自動では呼ばれない(docs/implementation-notes.md 参照) */
    R_GLCDC_PinSet();

    /* 4. GLCDC 起動 */
    lcd_build_cfg(&s_cfg);
#if APP_CLUT8_TEST
    clut8_test_patch_cfg(&s_cfg);    /* GR2 を CLUT8 に(docs/design.md §19.3.2) */
#endif
    err = R_GLCDC_Open(&s_cfg);
    if (err != GLCDC_SUCCESS) {
        app_debug_set_err(APP_ERR_LCD_OPEN, (uint32_t)err);
        return false;
    }
    err = R_GLCDC_Control(GLCDC_CMD_START_DISPLAY, NULL);
    if (err != GLCDC_SUCCESS) {
        app_debug_set_err(APP_ERR_LCD_START, (uint32_t)err);
        return false;
    }

    /* 5. DISP有効 -> 待ち -> バックライト点灯(順序・待ちは推定) */
    R_GPIO_PinWrite(PIN_LCD_DISP, LCD_DISP_ON_LEVEL);
    R_BSP_SoftwareDelay(LCD_DISP_TO_BL_MS, BSP_DELAY_MILLISECS);
    R_GPIO_PinWrite(PIN_LCD_BACKLIGHT, LCD_BL_ON_LEVEL);
    return true;
}
