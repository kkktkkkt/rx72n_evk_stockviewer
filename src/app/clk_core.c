/* clk_core.c - 時計・天気画面の表示値と整形(標準Cのみ。docs/design.md §13.3・§13.4・§13.8) */
#include <stddef.h>
#include "clk_core.h"
#include "clk_assets.h"          /* 字番号の定数(CLK_G124_* など)だけを使う。データは参照しない */

static const uint8_t k_wday_g20[7] = CLK_WDAY_G20;      /* wday 0..6(日月火水木金土)の F20 の字番号 */

uint8_t clk_kind_of(uint32_t wmo)
{
    switch (wmo) {
    case 0u: case 1u:                                                    return (uint8_t)CLK_SUN;
    case 2u:                                                             return (uint8_t)CLK_PARTLY;
    case 3u:                                                             return (uint8_t)CLK_CLOUD;
    case 45u: case 48u:                                                  return (uint8_t)CLK_FOG;
    case 51u: case 53u: case 55u: case 56u: case 57u:
    case 61u: case 63u: case 65u: case 66u: case 67u:
    case 80u: case 81u: case 82u:                                        return (uint8_t)CLK_RAIN;
    case 71u: case 73u: case 75u: case 77u: case 85u: case 86u:          return (uint8_t)CLK_SNOW;
    case 95u: case 96u: case 99u:                                        return (uint8_t)CLK_THUNDER;
    default:                                                             return (uint8_t)CLK_UNKNOWN;
    }
}

const char *clk_kind_name(uint8_t kind)
{
    static const char *const names[CLK_KINDS] = { "sun", "partly", "cloud", "fog", "rain", "snow", "thunder", "unknown" };

    return (kind < (uint8_t)CLK_KINDS) ? names[kind] : NULL;
}

int8_t clk_temp_int(int16_t t10)
{
    int32_t a = (t10 < 0) ? -(int32_t)t10 : (int32_t)t10;

    a = (a + 5) / 10;
    if (a > CLK_TEMP_MAX) {
        a = CLK_TEMP_MAX;
    }
    return (int8_t)((t10 < 0) ? -a : a);                 /* a = 0 なら 0(符号なし) */
}

void clk_view_make(clk_view_t *v, const clk_slot_t *slot, uint32_t now_ms, uint16_t clock_min, const clk_date_t *date)
{
    if (v == NULL) {
        return;
    }
    v->clock_min = ((clock_min > (uint16_t)CLK_MINUTES_MAX) ? (uint16_t)CLK_CLOCK_NONE : clock_min);
    v->kind = (uint8_t)CLK_UNKNOWN;
    v->have = 0u;
    v->temp = 0;
    if ((slot != NULL) && (slot->have != 0u) && (slot->kind < (uint8_t)CLK_KINDS) &&
        (((uint32_t)(now_ms - slot->ok_ms) / 1000u) < (uint32_t)CLK_WX_STALE_S)) {
        v->kind = slot->kind;
        v->have = 1u;
        v->temp = clk_temp_int(slot->temp_x10);
    }
    v->mon = 0u;
    v->day = 0u;
    v->wday = 0u;
    if ((date != NULL) && (date->mon >= 1u) && (date->mon <= 12u) && (date->day >= 1u) && (date->day <= 31u) && (date->wday <= 6u)) {
        v->mon = date->mon;
        v->day = date->day;
        v->wday = date->wday;
    }
}

/* 1〜2 桁の 10 進数(0 詰めなし)を ids に足す。戻り値 = 足した字数。数字の字番号は数字そのもの */
static uint8_t put_dec(uint8_t *ids, uint8_t n)
{
    if (n >= 10u) {
        ids[0] = (uint8_t)(n / 10u);
        ids[1] = (uint8_t)(n % 10u);
        return 2u;
    }
    ids[0] = n;
    return 1u;
}

uint8_t clk_seq_time(uint8_t out[CLK_SEQ_TIME_MAX], const clk_view_t *v)
{
    uint16_t m = v->clock_min;

    if (m > (uint16_t)CLK_MINUTES_MAX) {
        out[0] = (uint8_t)CLK_G124_DASH; out[1] = (uint8_t)CLK_G124_DASH; out[2] = (uint8_t)CLK_G124_COLON;
        out[3] = (uint8_t)CLK_G124_DASH; out[4] = (uint8_t)CLK_G124_DASH;
        return 5u;
    }
    out[0] = (uint8_t)((m / 60u) / 10u);
    out[1] = (uint8_t)((m / 60u) % 10u);
    out[2] = (uint8_t)CLK_G124_COLON;
    out[3] = (uint8_t)((m % 60u) / 10u);
    out[4] = (uint8_t)((m % 60u) % 10u);
    return 5u;
}

uint8_t clk_seq_date(uint8_t out[CLK_SEQ_DATE_MAX], const clk_view_t *v)
{
    uint8_t n = 0u;

    if ((v->mon >= 1u) && (v->mon <= 12u) && (v->day >= 1u) && (v->day <= 31u) && (v->wday <= 6u)) {
        n = (uint8_t)(n + put_dec(&out[n], v->mon));
        out[n++] = (uint8_t)CLK_G20_MON;
        n = (uint8_t)(n + put_dec(&out[n], v->day));
        out[n++] = (uint8_t)CLK_G20_DAY;
        out[n++] = (uint8_t)CLK_G20_PAREN_L;
        out[n++] = k_wday_g20[v->wday];
        out[n++] = (uint8_t)CLK_G20_PAREN_R;
        out[n++] = (uint8_t)CLK_G20_SPACE;
    }
    out[n++] = (uint8_t)CLK_G20_TOKYO0;
    out[n++] = (uint8_t)CLK_G20_TOKYO1;
    out[n++] = (uint8_t)CLK_G20_TOKYO2;
    return n;
}

uint8_t clk_seq_temp(uint8_t out[CLK_SEQ_TEMP_MAX], const clk_view_t *v)
{
    uint8_t n = 0u;
    int32_t t;

    if (v->have == 0u) {
        out[0] = (uint8_t)CLK_G72_DASH; out[1] = (uint8_t)CLK_G72_DASH; out[2] = (uint8_t)CLK_G72_DEG;
        return 3u;
    }
    t = v->temp;
    if (t < 0) {
        out[n++] = (uint8_t)CLK_G72_MINUS;
        t = -t;
    }
    if (t > CLK_TEMP_MAX) {
        t = CLK_TEMP_MAX;
    }
    n = (uint8_t)(n + put_dec(&out[n], (uint8_t)t));
    out[n++] = (uint8_t)CLK_G72_DEG;
    return n;
}
