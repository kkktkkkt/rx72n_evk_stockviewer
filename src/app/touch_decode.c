/* touch_decode.c - 標準Cのみ。FT5x06系のレジスタ配置を仮定(未確認)。 */
#include "touch_decode.h"

bool touch_decode(const uint8_t raw[5], nav_sample_t *out)
{
    uint8_t n;

    if (out == 0) {
        return false;
    }
    out->pressed = false;
    out->points  = 0u;
    out->x       = 0u;
    out->y       = 0u;

    if (raw == 0) {
        return false;
    }
    n = (uint8_t)(raw[0] & 0x0Fu);
    if (n > TOUCH_MAX_POINTS) {
        return false;                 /* 読み取りノイズ: 異常 */
    }
    if (n == 0u) {
        return true;                  /* 触れていない */
    }
    out->pressed = true;
    out->points  = n;
    out->x = (uint16_t)((((uint16_t)raw[1] & 0x0Fu) << 8) | raw[2]);
    out->y = (uint16_t)((((uint16_t)raw[3] & 0x0Fu) << 8) | raw[4]);
    return true;
}
