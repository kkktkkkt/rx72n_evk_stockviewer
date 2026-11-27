/* gfx.h - 480x272 面への描画部品(標準Cのみ。ホストでテストする。設計 §9.1、§20.5)
 * 製品の面は 8bpp(CLUT8): uint8_t の配列で行の長さ GFX_STRIDE(512)。左 480 列だけ描く(右の 32 列は書かない)。gfx_*8 を使う。
 * RGB565 版(gfx_blit565 / gfx_a8 / gfx_fillrect)は uint16_t の配列(行の長さ 480)で、P1 の基準描画とホスト試験用に残す。
 * どの関数も面の外へは1画素も書かない(切り取る)。 */
#ifndef GFX_HEADER_H
#define GFX_HEADER_H

#include <stdint.h>

#define GFX_W  (480)
#define GFX_H  (272)
#define GFX_STRIDE (512)      /* 8bpp の面の行の長さ(バイト) */

/* 不透明コピー: src(行の長さ src_stride)の (sx,sy) から w x h を fb の (dx,dy) へ */
void gfx_blit565(uint16_t *fb, const uint16_t *src, int32_t src_stride,
                 int32_t sx, int32_t sy, int32_t w, int32_t h, int32_t dx, int32_t dy);

/* 濃度つき重ね描き: a8(w x h、行の長さ w)の濃度 0..255 に scale256(0..256)を掛けた割合で color を混ぜる。
 * 濃度は 5bit(33段階)に丸めて 565 のまま計算する。scale256 = 0 なら何も変えない */
void gfx_a8(uint16_t *fb, const uint8_t *a8, int32_t w, int32_t h, int32_t x, int32_t y,
            uint16_t color, uint32_t scale256);

void gfx_fillrect(uint16_t *fb, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);

/* ---- 8bpp(CLUT8)の面 ---- */
/* 不透明コピー: src(行の長さ src_stride)の (sx,sy) から w x h を fb の (dx,dy) へ */
void gfx_blit8(uint8_t *fb, const uint8_t *src, int32_t src_stride,
               int32_t sx, int32_t sy, int32_t w, int32_t h, int32_t dx, int32_t dy);

void gfx_fill8(uint8_t *fb, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t idx);

/* 段階の表で重ね描き: a8(w x h、行の長さ w)の濃度のレベル l = (a + 4) >> 3(0..32)の画素のうち、l != 0 の所を ramp[l] にする。
 * alt >= 0 で、書く前の添字が alt の画素は ramp1[l]、それ以外は ramp0[l](下地が2色の時)。ramp は 33 要素 */
void gfx_a8_ramp(uint8_t *fb, const uint8_t *a8, int32_t w, int32_t h, int32_t x, int32_t y,
                 const uint8_t *ramp0, const uint8_t *ramp1, int32_t alt);

/* 置き換え: a8 >= th の画素の添字 p を map[p] にする(押下の暗転 / 白光。map は 256 要素) */
void gfx_remap8(uint8_t *fb, const uint8_t *a8, int32_t w, int32_t h, int32_t x, int32_t y,
                const uint8_t *map, uint8_t th);

#endif /* GFX_HEADER_H */
