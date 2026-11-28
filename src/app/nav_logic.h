/* nav_logic.h - タッチ標本の型と標本の周期(標準Cのみ。docs/design.md §7.1)
 * 旧 UI の画像切替の論理は削除した。touch_decode.h / touch_ft5x.h / ui_logic.h / app_main.c が使う分だけ残す。 */
#ifndef NAV_LOGIC_H
#define NAV_LOGIC_H

#include <stdint.h>
#include <stdbool.h>

#define NAV_SAMPLE_MS        (10u)   /* タッチの標本の周期(10ms) */

typedef struct {
    bool     pressed;   /* 1点以上触れている */
    uint8_t  points;    /* タッチ点数 */
    uint16_t x;         /* 点1の座標 */
    uint16_t y;
} nav_sample_t;

#endif /* NAV_LOGIC_H */
