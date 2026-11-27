/* board_pins.h - ピン・極性・定数の集約(設計 §8, §9)
 * 極性と待ち時間は「推定」。実機で合わなければここだけ直す。 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "platform.h"
#include "r_gpio_rx_if.h"

/* ---- LCD ---- */
#define LCD_WIDTH            (480u)
#define LCD_HEIGHT           (272u)
#define LCD_FB0_ADDR         (0x00800000u)                  /* RAM2 先頭: 面0 */

/* ---- CLUT8 表示(docs/design.md §19, §20)。製品の面は 8bpp・行幅 512。2面で 278,528 バイト(RAM2 の空き 245,760) ---- */
#include "clut8_sw.h"                                      /* APP_CLUT8_TEST(既定 0。1 で実験ファーム: 製品の画面は出ない)はここで定義 */
#include "wifi_sw.h"                                       /* APP_WIFI_TEST(既定 0。1 で Wi-Fi 試作ファーム: 製品の画面は出ない)はここで定義 */
#define LCD_C8_STRIDE        (512u)                          /* CLUT8 の行幅(64の倍数で表示幅以上の最小値) */
#define LCD_C8_FB_BYTES      (LCD_C8_STRIDE * LCD_HEIGHT)    /* 139,264 */
#ifndef LCD_C8_FB_A_ADDR                                     /* ホスト試験だけが差し替える */
#define LCD_C8_FB_A_ADDR     (LCD_FB0_ADDR)                  /* 面A = 0x00800000 */
#endif
#define LCD_C8_FB_B_ADDR     (LCD_C8_FB_A_ADDR + LCD_C8_FB_BYTES)   /* 面B = 0x00822000(64バイト境界) */

#define LCD_LINE_BYTES       (LCD_C8_STRIDE)                 /* 512(1画素 = 1バイト) */
#define LCD_FB_BYTES         (LCD_C8_FB_BYTES)
#define LCD_FB1_ADDR         (LCD_C8_FB_B_ADDR)

/* ---- GPIO(r_gpio_rx の定義名。144ピン用ヘッダで存在確認済み) ---- */
#define PIN_LCD_BACKLIGHT    (GPIO_PORT_6_PIN_7)   /* P67 */
#define PIN_LCD_DISP         (GPIO_PORT_B_PIN_3)   /* PB3 */
#define PIN_TOUCH_RST        (GPIO_PORT_6_PIN_6)   /* P66 */
#define PIN_WIFI_EN          (GPIO_PORT_4_PIN_3)   /* P43(ピン137): ESP32 の EN。Low = リセット、High = 起動(docs/design.md §21.1) */

/* アクティブレベル(推定。B1/B9 で確定する) */
#define LCD_BL_ON_LEVEL      (GPIO_LEVEL_HIGH)     /* 回路図: P67=High で点灯とする */
#define LCD_DISP_ON_LEVEL    (GPIO_LEVEL_HIGH)     /* 未確認 */
#define TOUCH_RST_ACTIVE_LEVEL (GPIO_LEVEL_LOW)    /* 未確認(FT5x06系は Low でリセット) */

/* 待ち時間(推定) */
#define LCD_DISP_TO_BL_MS    (20u)    /* DISP有効 -> バックライト点灯 */
#define TOUCH_RST_LOW_MS     (10u)    /* リセット保持 */
#define TOUCH_BOOT_MS        (300u)   /* リセット解除 -> 通信開始 */

/* ---- タッチ ---- */
#define TOUCH_SCI_IIC_CH     (6u)     /* SCI6 簡易I2C (P33=SSCL6, P32=SSDA6) */
#define TOUCH_I2C_ADDR       (0x38u)  /* 7bit。FT5x06系の既定値(未確認) */
#define TOUCH_REG_START      (0x02u)  /* タッチ数のレジスタ */
#define TOUCH_RAW_BYTES      (5u)
#define TOUCH_X_MAX          (479u)   /* 仮値。V0(四隅を触る)で確定 */
#define TOUCH_Y_MAX          (271u)   /* 仮値。V0 で確定 */
#define TOUCH_SWAP_XY        (0)      /* 1 = 生 X/Y を入れ替える(生 Y 軸が画面の横方向)。V0 で確定 */
#define TOUCH_FLIP_X         (0)      /* 1 = 画面の左右を反転 */
#define TOUCH_FLIP_Y         (0)      /* 1 = 画面の上下を反転 */
#define TOUCH_WAIT_US_STEP   (10u)    /* 完了待ちの刻み */
#define TOUCH_WAIT_STEPS     (2000u)  /* 最大 約20ms */
#define TOUCH_FAIL_SCAN_AT   (10u)    /* 連続失敗でアドレススキャン */


/* ---- 計測(design §15 R2): 起動時に [perf] を1回出す。0 で無効 ---- */
#ifndef APP_PERF_BENCH
#define APP_PERF_BENCH       (1)
#endif
#define APP_PERF_N           (50u)    /* 各測定の繰り返し回数 */

#endif /* BOARD_PINS_H */
