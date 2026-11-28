/* touch_decode.h - I2C生バイト列 -> nav_sample_t(標準Cのみ。設計 §9.2) */
#ifndef TOUCH_DECODE_H
#define TOUCH_DECODE_H

#include <stdint.h>
#include <stdbool.h>
#include "nav_logic.h"

#define TOUCH_MAX_POINTS (5u)

/* raw: レジスタ 0x02 から読んだ5バイト
 *   raw[0]=タッチ数(下位4bit) raw[1]=XH raw[2]=XL raw[3]=YH raw[4]=YL
 * 異常(点数 > 5、NULL)は false を返し、*out を「触れていない」にする。 */
bool touch_decode(const uint8_t raw[5], nav_sample_t *out);

#endif /* TOUCH_DECODE_H */
