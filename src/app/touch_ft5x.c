/* touch_ft5x.c - r_sci_iic_rx 2.91 のAPIは src/smc_gen/r_sci_iic_rx/r_sci_iic_rx_if.h で確認済み。
 * レジスタ配置(FT5x06系)とアドレス 0x38 は未確認の仮定。 */
#include <string.h>
#include "platform.h"
#include "r_sci_iic_rx_if.h"
#include "board_pins.h"
#include "app_debug.h"
#include "touch_decode.h"
#include "touch_ft5x.h"

static sci_iic_info_t s_iic;                 /* 転送完了までドライバが保持する(static必須) */
static uint8_t        s_slave;               /* 7bit、シフトしない */
static uint8_t        s_reg;
static uint8_t        s_buf[TOUCH_RAW_BYTES];
static volatile bool  s_done;
static bool           s_open;
static bool           s_scanned;

static void iic_callback(void)
{
    s_done = true;
}

static sci_iic_return_t iic_open(void)
{
    memset(&s_iic, 0, sizeof(s_iic));
    s_iic.ch_no        = (uint8_t)TOUCH_SCI_IIC_CH;
    s_iic.dev_sts      = SCI_IIC_NO_INIT;
    s_iic.callbackfunc = iic_callback;
    return R_SCI_IIC_Open(&s_iic);
}

/* 転送完了(コールバック)を待つ。true = 完了 */
static bool iic_wait(void)
{
    uint32_t n = 0u;

    while (!s_done && (n < TOUCH_WAIT_STEPS)) {
        R_BSP_SoftwareDelay(TOUCH_WAIT_US_STEP, BSP_DELAY_MICROSECS);
        n++;
    }
    return s_done;
}

static sci_iic_ch_dev_status_t iic_status(void)
{
    return *(volatile sci_iic_ch_dev_status_t *)&s_iic.dev_sts;
}

/* タイムアウト・異常後の復旧(ベストエフォート) */
static void iic_recover(void)
{
    (void)R_SCI_IIC_Close(&s_iic);
    (void)iic_open();
}

/* addr から len バイト読む。with_reg = true なら先に reg を送る(再スタート付き)。
 * 戻り値: SCI_IIC_SUCCESS = 転送を開始できた。完了状態は *st、タイムアウトは *timeout */
static sci_iic_return_t iic_read(uint8_t addr, bool with_reg, uint8_t reg, uint8_t *dst,
                                 uint32_t len, sci_iic_ch_dev_status_t *st, bool *timeout)
{
    sci_iic_return_t r;

    s_slave = addr;
    s_reg   = reg;
    s_iic.p_slv_adr  = &s_slave;
    s_iic.p_data1st  = with_reg ? &s_reg : (uint8_t *)FIT_NO_PTR;
    s_iic.cnt1st     = with_reg ? 1u : 0u;
    s_iic.p_data2nd  = dst;
    s_iic.cnt2nd     = len;
    s_iic.callbackfunc = iic_callback;
    s_done = false;

    r = R_SCI_IIC_MasterReceive(&s_iic);
    if (r != SCI_IIC_SUCCESS) {
        return r;
    }
    *timeout = !iic_wait();
    *st = iic_status();
    return SCI_IIC_SUCCESS;
}

/* 0x08〜0x77 に1バイト受信を試し、応答したアドレスを g_app_dbg.found_addr[] に記録 */
static void iic_scan(void)
{
    uint8_t a;
    uint8_t dummy;
    sci_iic_ch_dev_status_t st;
    bool to;

    g_app_dbg.found_count = 0u;
    for (a = 0x08u; a <= 0x77u; a++) {
        st = SCI_IIC_NO_INIT;
        to = false;
        if (iic_read(a, false, 0u, &dummy, 1u, &st, &to) != SCI_IIC_SUCCESS) {
            iic_recover();
            continue;
        }
        if (to) {
            iic_recover();
            continue;
        }
        if ((st == SCI_IIC_FINISH) && (g_app_dbg.found_count < APP_DBG_FOUND_MAX)) {
            g_app_dbg.found_addr[g_app_dbg.found_count] = a;
            g_app_dbg.found_count++;
        }
    }
}

bool touch_init(void)
{
    sci_iic_return_t r;

    /* RST: アクティブ -> 解除 -> 起動待ち(時間は推定) */
    R_GPIO_PinWrite(PIN_TOUCH_RST, TOUCH_RST_ACTIVE_LEVEL);
    R_GPIO_PinDirectionSet(PIN_TOUCH_RST, GPIO_DIRECTION_OUTPUT);
    R_BSP_SoftwareDelay(TOUCH_RST_LOW_MS, BSP_DELAY_MILLISECS);
    R_GPIO_PinWrite(PIN_TOUCH_RST,
                    (TOUCH_RST_ACTIVE_LEVEL == GPIO_LEVEL_HIGH) ? GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH);
    R_BSP_SoftwareDelay(TOUCH_BOOT_MS, BSP_DELAY_MILLISECS);

    r = iic_open();
    if (r != SCI_IIC_SUCCESS) {
        app_debug_set_err(APP_ERR_TOUCH_OPEN, (uint32_t)r);
        s_open = false;
        return false;
    }
    s_open = true;
    return true;
}

static void note_fail(uint32_t code, uint32_t detail)
{
    g_app_dbg.i2c_fail_total++;
    if (g_app_dbg.i2c_fail_streak < 255u) {
        g_app_dbg.i2c_fail_streak++;
    }
    app_debug_set_err(code, detail);

    /* 連続失敗でアドレススキャンを1回だけ(自動採用はしない) */
    if ((g_app_dbg.i2c_fail_streak >= TOUCH_FAIL_SCAN_AT) && !s_scanned) {
        s_scanned = true;
        iic_scan();
    }
}

bool touch_read(nav_sample_t *out)
{
    sci_iic_return_t r;
    sci_iic_ch_dev_status_t st = SCI_IIC_NO_INIT;
    bool to = false;

    if ((out == NULL) || !s_open) {
        return false;
    }

    r = iic_read((uint8_t)TOUCH_I2C_ADDR, true, (uint8_t)TOUCH_REG_START,
                 s_buf, TOUCH_RAW_BYTES, &st, &to);
    if (r != SCI_IIC_SUCCESS) {
        note_fail(APP_ERR_IIC_START, (uint32_t)r);
        iic_recover();
        return false;
    }
    if (to) {
        note_fail(APP_ERR_IIC_TIMEOUT, (uint32_t)st);
        iic_recover();
        return false;
    }
    if (st != SCI_IIC_FINISH) {
        /* NACK(アドレス違い等)や ERROR */
        note_fail(APP_ERR_IIC_START, 0x100u + (uint32_t)st);
        if (st == SCI_IIC_ERROR) {
            iic_recover();
        }
        return false;
    }

    g_app_dbg.i2c_fail_streak = 0u;
    return touch_decode(s_buf, out);
}
