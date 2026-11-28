/* app_debug.h - デバッガで見る状態変数と、UARTログ(設計 §10)
 * UARTログは既定で有効(SCI2 = CN8、115200bps)。無効にするには APP_LOG_ENABLE を 0 にする。
 * 出力形式とコマンドは docs/implementation-notes.md。 */
#ifndef APP_DEBUG_H
#define APP_DEBUG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- UARTログ。無効時(0)は r_sci_rx を一切呼ばない ---- */
#ifndef APP_LOG_ENABLE
#define APP_LOG_ENABLE   (1)
#endif
#ifndef APP_LOG_SCI_CH
#define APP_LOG_SCI_CH   (2)     /* 2 = CN8(P13/P12)。1 または 2 のみ対応 */
#endif
#ifndef APP_LOG_BAUD
#define APP_LOG_BAUD     (115200u)
#endif

#define APP_DBG_FOUND_MAX (4u)

/* err_code の値 */
#define APP_ERR_NONE         (0u)
#define APP_ERR_LCD_OPEN     (1u)   /* R_GLCDC_Open 失敗。err_detail = glcdc_err_t */
#define APP_ERR_LCD_START    (2u)   /* START_DISPLAY 失敗。err_detail = glcdc_err_t */
#define APP_ERR_TOUCH_OPEN   (3u)   /* R_SCI_IIC_Open 失敗。err_detail = sci_iic_return_t */
#define APP_ERR_TIMER        (4u)   /* R_CMT_CreatePeriodic 失敗 */
#define APP_ERR_IIC_TIMEOUT  (5u)   /* I2C完了待ちタイムアウト(最新1回) */
#define APP_ERR_IIC_START    (6u)   /* MasterReceive 呼び出し失敗。err_detail = sci_iic_return_t */
#define APP_ERR_UART_RX      (7u)   /* UART受信異常。[err] 行にだけ出す(g_app_dbg.err_code には書かない) */
#define APP_ERR_LCD_FLIP     (8u)   /* R_GLCDC_BufferChange 失敗(反映待ち以外)。err_detail = glcdc_err_t */
#define APP_ERR_WIFI_OPEN    (9u)   /* Wi-Fi 試作(wifi_at.c): SCI1 を開けない。err_detail = sci_err_t。[err] 行にだけ出す */
#define APP_ERR_WIFI_RX      (10u)  /* Wi-Fi 試作: SCI1 の受信異常。err_detail = sci_cb_evt_t。[err] 行にだけ出す */
#define APP_ERR_LCD_CLUT     (11u)  /* R_GLCDC_ClutUpdate_NoReflect 失敗(反映待ち以外)。err_detail = glcdc_err_t(lcd_flip_pal)。[err] 行にも出る */

typedef struct {
    uint32_t tick;              /* 10ms ごとに増える */
    uint32_t err_code;          /* APP_ERR_* */
    uint32_t err_detail;
    uint32_t i2c_fail_total;    /* I2C失敗の累計 */
    uint16_t raw_x;             /* 最新の生座標 */
    uint16_t raw_y;
    uint16_t x_min;             /* 触れた範囲(B8用。四隅を触ると範囲が分かる) */
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
    uint32_t frames;            /* 表示の入れ替え完了回数の累計 */
    uint32_t uf;                /* GLCDC アンダーフロー検出の累計 */
    uint8_t  raw_points;
    uint8_t  scr;               /* 画面番号 0..4(UI_SCR_*) */
    uint8_t  demo;              /* デモ中 = 1 */
    uint8_t  i2c_fail_streak;   /* 連続失敗数 */
    uint8_t  found_count;       /* アドレススキャンで応答した数 */
    uint8_t  found_addr[APP_DBG_FOUND_MAX];
} app_dbg_t;

extern volatile app_dbg_t g_app_dbg;

void app_debug_init(void);
void app_debug_set_err(uint32_t code, uint32_t detail);
void app_debug_touch(const uint16_t x, const uint16_t y, const uint8_t points);

/* UARTログ API(仕様 docs/design-uart.md)。すべてメインループ専用。割り込みから呼ばない。 */
#if APP_LOG_ENABLE
void     app_log_init(void);                     /* SCIを開く。失敗しても続行(出力なし) */
void     app_log_line(const char *s, size_t n);  /* 組み立て済みの1行をキューへ。n == 0(整形失敗)、満杯は破棄して drop +1 */
void     app_log_poll(void);                     /* キュー先頭を送信。BUSYなら残して即戻る(待たない) */
int      app_log_getc(void);                     /* 受信1バイト。無ければ -1 */
uint32_t app_log_drops(void);                    /* 破棄の累計 */
uint32_t app_log_rx_event(void);                 /* 受信異常のイベント値(sci_cb_evt_t)。未読が無ければ 0。読むと既読になる */
#else
#define app_log_init()        ((void)0)
#define app_log_line(s, n)    ((void)(s), (void)(n))
#define app_log_poll()        ((void)0)
#define app_log_getc()        (-1)
#define app_log_drops()       (0u)
#define app_log_rx_event()    (0u)
#endif

#endif /* APP_DEBUG_H */
