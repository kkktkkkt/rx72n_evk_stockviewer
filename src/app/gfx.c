/* gfx.c - 標準Cのみ(FITを include しない) */
#include <string.h>
#include "gfx.h"

/* 描く範囲 [*x, *x+*w) x [*y, *y+*h) を画面に切る。skx/sky は左・上で切った量。空なら 0 を返す(int32 の加算あふれなし) */
static int clip(int32_t *x, int32_t *y, int32_t *w, int32_t *h, int32_t *skx, int32_t *sky)
{
    int32_t x0, y0, rw, rh;

    if ((*w <= 0) || (*h <= 0) || (*x >= GFX_W) || (*y >= GFX_H) || (*x <= -*w) || (*y <= -*h)) {
        return 0;
    }
    x0 = (*x < 0) ? 0 : *x;
    y0 = (*y < 0) ? 0 : *y;
    *skx = x0 - *x;
    *sky = y0 - *y;
    rw = *w - *skx;
    rh = *h - *sky;
    *w = (rw > (GFX_W - x0)) ? (GFX_W - x0) : rw;
    *h = (rh > (GFX_H - y0)) ? (GFX_H - y0) : rh;
    *x = x0;
    *y = y0;
    return 1;
}

void gfx_blit565(uint16_t *fb, const uint16_t *src, int32_t src_stride,
                 int32_t sx, int32_t sy, int32_t w, int32_t h, int32_t dx, int32_t dy)
{
    int32_t skx, sky, r;

    if ((fb == NULL) || (src == NULL) || !clip(&dx, &dy, &w, &h, &skx, &sky)) {
        return;
    }
    sx += skx;
    sy += sky;
    for (r = 0; r < h; r++) {
        memcpy(&fb[(dy + r) * GFX_W + dx], &src[(sy + r) * src_stride + sx], (size_t)w * 2u);
    }
}

void gfx_fillrect(uint16_t *fb, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    int32_t skx, sky, r, c;
    uint16_t *p;

    if ((fb == NULL) || !clip(&x, &y, &w, &h, &skx, &sky)) {
        return;
    }
    for (r = 0; r < h; r++) {
        p = &fb[(y + r) * GFX_W + x];
        for (c = 0; c < w; c++) {
            p[c] = color;
        }
    }
}

/* bg と fg を 565 のまま混ぜる。a5 = 0..32(fg の割合 a5/32)。G を 21bit 目へ広げ、3つの色を1回の乗算で計算する */
static uint16_t mix565(uint16_t bg, uint16_t fg, uint32_t a5)
{
    uint32_t b = ((uint32_t)bg | ((uint32_t)bg << 16)) & 0x07E0F81Fu;
    uint32_t f = ((uint32_t)fg | ((uint32_t)fg << 16)) & 0x07E0F81Fu;
    uint32_t m = ((((f - b) * a5) >> 5) + b) & 0x07E0F81Fu;

    return (uint16_t)(m | (m >> 16));
}

void gfx_a8(uint16_t *fb, const uint8_t *a8, int32_t w, int32_t h, int32_t x, int32_t y,
            uint16_t color, uint32_t scale256)
{
    int32_t skx, sky, r, c, stride = w;
    const uint8_t *s;
    uint16_t *p;
    uint32_t a;

    if ((fb == NULL) || (a8 == NULL) || (scale256 == 0u) || !clip(&x, &y, &w, &h, &skx, &sky)) {
        return;
    }
    if (scale256 > 256u) {
        scale256 = 256u;
    }
    for (r = 0; r < h; r++) {
        s = &a8[(sky + r) * stride + skx];
        p = &fb[(y + r) * GFX_W + x];
        for (c = 0; c < w; c++) {
            if (s[c] != 0u) {
                a = (((uint32_t)s[c] * scale256) >> 8);     /* 0..255 */
                p[c] = mix565(p[c], color, (a + 4u) >> 3);  /* 0..32 */
            }
        }
    }
}

/* ---- 8bpp(CLUT8)の面(行の長さ GFX_STRIDE)---- */
void gfx_blit8(uint8_t *fb, const uint8_t *src, int32_t src_stride,
               int32_t sx, int32_t sy, int32_t w, int32_t h, int32_t dx, int32_t dy)
{
    int32_t skx, sky, r;

    if ((fb == NULL) || (src == NULL) || !clip(&dx, &dy, &w, &h, &skx, &sky)) {
        return;
    }
    sx += skx;
    sy += sky;
    for (r = 0; r < h; r++) {
        memcpy(&fb[(dy + r) * GFX_STRIDE + dx], &src[(sy + r) * src_stride + sx], (size_t)w);
    }
}

void gfx_fill8(uint8_t *fb, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t idx)
{
    int32_t skx, sky, r;

    if ((fb == NULL) || !clip(&x, &y, &w, &h, &skx, &sky)) {
        return;
    }
    for (r = 0; r < h; r++) {
        memset(&fb[(y + r) * GFX_STRIDE + x], idx, (size_t)w);
    }
}

void gfx_a8_ramp(uint8_t *fb, const uint8_t *a8, int32_t w, int32_t h, int32_t x, int32_t y,
                 const uint8_t *ramp0, const uint8_t *ramp1, int32_t alt)
{
    int32_t skx, sky, r, c, stride = w;
    const uint8_t *s;
    uint8_t *p;
    uint32_t l;

    if ((fb == NULL) || (a8 == NULL) || (ramp0 == NULL) || ((alt >= 0) && (ramp1 == NULL)) || !clip(&x, &y, &w, &h, &skx, &sky)) {
        return;
    }
    for (r = 0; r < h; r++) {
        s = &a8[(sky + r) * stride + skx];
        p = &fb[(y + r) * GFX_STRIDE + x];
        for (c = 0; c < w; c++) {
            l = ((uint32_t)s[c] + 4u) >> 3;                 /* 0..32 */
            if (l != 0u) {
                p[c] = ((alt >= 0) && ((int32_t)p[c] == alt)) ? ramp1[l] : ramp0[l];
            }
        }
    }
}

void gfx_remap8(uint8_t *fb, const uint8_t *a8, int32_t w, int32_t h, int32_t x, int32_t y,
                const uint8_t *map, uint8_t th)
{
    int32_t skx, sky, r, c, stride = w;
    const uint8_t *s;
    uint8_t *p;

    if ((fb == NULL) || (a8 == NULL) || (map == NULL) || !clip(&x, &y, &w, &h, &skx, &sky)) {
        return;
    }
    for (r = 0; r < h; r++) {
        s = &a8[(sky + r) * stride + skx];
        p = &fb[(y + r) * GFX_STRIDE + x];
        for (c = 0; c < w; c++) {
            if (s[c] >= th) {
                p[c] = map[p[c]];
            }
        }
    }
}
