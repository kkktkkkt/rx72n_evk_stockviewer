/* clut8_pat.c - 標準Cのみ。FITのヘッダを include しない。 */
#include <stdio.h>
#include <string.h>
#include "clut8_pat.h"
#include "clut8_sw.h"

#if APP_CLUT8_TEST   /* 0 の時は中身を消す(clut8_sw.h) */

const uint8_t c8pat_box_idx[C8_BOX_N] = { 2u, 3u, 4u, 5u, 6u, 7u, 1u, 8u };

static void fill_rect(uint8_t *fb, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1, uint8_t v)
{
    uint32_t x, y;

    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            fb[(y * C8_STRIDE) + x] = v;
        }
    }
}

void c8pat_face(uint8_t *fb, bool b)
{
    uint32_t x, y, k;

    memset(fb, (int)C8_IDX_BLACK, C8_FB_BYTES);
    for (y = C8_RAMP_Y0; y <= C8_RAMP_Y1; y++) {                 /* 灰の階段: 2画素ごとに1段 */
        for (x = 0u; x < C8_W; x++) {
            fb[(y * C8_STRIDE) + x] = (uint8_t)(C8_IDX_RAMP0 + (x / 2u));
        }
    }
    for (k = 0u; k < C8_BOX_N; k++) {                            /* 色の箱 */
        x = C8_BOX_X0 + (C8_BOX_PITCH * k);
        fill_rect(fb, x, x + C8_BOX_W - 1u, C8_BOX_Y0, C8_BOX_Y1, c8pat_box_idx[k]);
    }
    for (y = C8_GRID_Y0; y < C8_H; y++) {                        /* 格子(1画素) */
        for (x = 0u; x < C8_W; x++) {
            if (((x % C8_GRID_PITCH) == 0u) || (((y - C8_GRID_Y0) % C8_GRID_PITCH) == 0u)) {
                fb[(y * C8_STRIDE) + x] = (uint8_t)C8_IDX_WHITE;
            }
        }
    }
    fill_rect(fb, C8_MARK_X0, C8_MARK_X1, C8_MARK_Y0, C8_MARK_Y1, (uint8_t)C8_IDX_MARKER);
    fill_rect(fb, 0u, C8_FRAME_T - 1u, 0u, C8_H - 1u, (uint8_t)C8_IDX_WHITE);                     /* 枠 */
    fill_rect(fb, C8_W - C8_FRAME_T, C8_W - 1u, 0u, C8_H - 1u, (uint8_t)C8_IDX_WHITE);
    fill_rect(fb, 0u, C8_W - 1u, 0u, C8_FRAME_T - 1u, (uint8_t)C8_IDX_WHITE);
    fill_rect(fb, 0u, C8_W - 1u, C8_H - C8_FRAME_T, C8_H - 1u, (uint8_t)C8_IDX_WHITE);
    fill_rect(fb, C8_W, C8_STRIDE - 1u, 0u, C8_H - 1u, (uint8_t)C8_IDX_REMAIN);                   /* 余り */

    if (b) {
        for (x = 0u; x < C8_FB_BYTES; x++) {
            fb[x] = (uint8_t)(255u - fb[x]);
        }
    }
}

static uint32_t argb(uint32_t rgb)
{
    return 0xFF000000u | rgb;
}

/* パレットA の i 番目 */
static uint32_t col_a(uint32_t i)
{
    uint32_t g;

    switch (i) {
    case 1u:  return argb(0xFFFFFFu);
    case 2u:  return argb(0xFF0000u);
    case 3u:  return argb(0x00FF00u);
    case 4u:  return argb(0x0000FFu);
    case 5u:  return argb(0xFFFF00u);
    case 6u:  return argb(0x00FFFFu);
    case 7u:  return argb(0xFF00FFu);
    case 8u:  return argb(0x808080u);
    case 9u:  return argb(0xFF8000u);
    case 10u: return argb(0xFF0000u);
    default:  break;
    }
    if (i < C8_IDX_RAMP0) {
        return argb(0x000000u);                                  /* 0 と 11〜15 は黒 */
    }
    g = ((2u * (i - C8_IDX_RAMP0) * 255u) + 239u) / 478u;        /* (i - 16) * 255 / 239(四捨五入) */
    return argb((g << 16) | (g << 8) | g);
}

void c8pat_pal(uint32_t pal[C8_PAL_N], bool b, bool marker_green)
{
    uint32_t i;

    for (i = 0u; i < C8_PAL_N; i++) {
        pal[b ? (255u - i) : i] = col_a(i);
    }
    if (b && marker_green) {
        pal[255u - C8_IDX_MARKER] = argb(0x00FF00u);
    }
}

/* ---- [clut] 行 ---- */
static const char *const k_ev[] = {
    "start", "flip", "nr", "commit", "stress_end", "wr_unused", "wr_inuse", "wr_back",
    "bench", "nr1", "nr2", "settle", "done", "state"
};

static bool ev_ok(const char *ev)
{
    size_t i;

    if (ev == NULL) {
        return false;
    }
    for (i = 0u; i < (sizeof(k_ev) / sizeof(k_ev[0])); i++) {
        if (strcmp(ev, k_ev[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* snprintf の戻り値 n を位置 *pos に加える。入りきらなければ false */
static bool adv(int n, size_t size, size_t *pos)
{
    if ((n < 0) || ((size_t)n >= (size - *pos))) {
        return false;
    }
    *pos += (size_t)n;
    return true;
}

size_t c8pat_fmt_clut(char *buf, size_t size, uint8_t step, const char *ev, char fb, char pal,
                      uint8_t sel, uint8_t ven, uint32_t rc, uint32_t n, uint32_t m, uint32_t uf)
{
    size_t pos = 0u;

    if ((buf == NULL) || (size == 0u)) {
        return 0u;
    }
    buf[0] = '\0';
    if ((step > 5u) || !ev_ok(ev) || ((fb != 'a') && (fb != 'b')) || ((pal != 'a') && (pal != 'b')) ||
        (sel > 1u) || (ven > 1u)) {
        return 0u;
    }
    /* 引数が多いとスタックを使うので、値を分けて書く(app_logfmt.c と同じ) */
    if (!adv(snprintf(buf, size, "[clut] step=%u ev=%s fb=%c pal=%c", (unsigned)step, ev, fb, pal), size, &pos) ||
        !adv(snprintf(buf + pos, size - pos, " sel=%u ven=%u rc=%lu", (unsigned)sel, (unsigned)ven,
                      (unsigned long)rc), size, &pos) ||
        !adv(snprintf(buf + pos, size - pos, " n=%lu m=%lu uf=%lu\r\n", (unsigned long)n, (unsigned long)m,
                      (unsigned long)uf), size, &pos)) {
        buf[0] = '\0';
        return 0u;
    }
    return pos;
}

#endif /* APP_CLUT8_TEST */
