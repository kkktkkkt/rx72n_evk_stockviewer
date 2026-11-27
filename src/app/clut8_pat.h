/* clut8_pat.h - CLUT8 実験(docs/design.md §19)のテスト画像・パレット・[clut] 行(標準Cのみ。FITを include しない) */
#ifndef CLUT8_PAT_H
#define CLUT8_PAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define C8_W            (480u)
#define C8_H            (272u)
#define C8_STRIDE       (512u)                 /* 面の行幅(バイト) */
#define C8_FB_BYTES     (C8_STRIDE * C8_H)     /* 139,264 */
#define C8_PAL_N        (256u)

/* パレットA の番号(§19.2) */
#define C8_IDX_BLACK    (0u)
#define C8_IDX_WHITE    (1u)
#define C8_IDX_REMAIN   (9u)                   /* 右の余り(x = 480〜511)。LCD に見えてはいけない */
#define C8_IDX_MARKER   (10u)
#define C8_IDX_RAMP0    (16u)                  /* 灰の階段 16〜255 */

/* 領域(§19.2。x, y は画素。範囲は両端を含む) */
#define C8_RAMP_Y0      (8u)
#define C8_RAMP_Y1      (39u)
#define C8_BOX_N        (8u)
#define C8_BOX_X0       (8u)                   /* k 番目の箱の左端 = C8_BOX_X0 + C8_BOX_PITCH * k */
#define C8_BOX_PITCH    (58u)
#define C8_BOX_W        (54u)
#define C8_BOX_Y0       (48u)
#define C8_BOX_Y1       (111u)
#define C8_GRID_Y0      (120u)
#define C8_GRID_PITCH   (16u)
#define C8_MARK_X0      (432u)
#define C8_MARK_X1      (471u)
#define C8_MARK_Y0      (240u)
#define C8_MARK_Y1      (263u)
#define C8_FRAME_T      (2u)

/* 箱の値(左から)。赤・緑・青・黄・シアン・マゼンタ・白・灰 */
extern const uint8_t c8pat_box_idx[C8_BOX_N];

/* 面(512 x 272、8bit)を作る。b = true なら全バイトを 255 - v にした面B */
void c8pat_face(uint8_t *fb, bool b);

/* パレット(0xAARRGGBB)を作る。b = true は逆順(pal[255 - i] = palA[i])。
 * marker_green は b = true の時だけ有効: マーカー(pal[245])を緑にする(同じ絵ではなくなる) */
void c8pat_pal(uint32_t pal[C8_PAL_N], bool b, bool marker_green);

/* "[clut] step=.. ev=.. fb=.. pal=.. sel=.. ven=.. rc=.. n=.. m=.. uf=..\r\n"(design-uart.md §11.1)。
 * 戻り値 = 書いたバイト数(\r\n 込み)。失敗(引数不正・バッファ不足)は 0 で buf[0] = '\0'。
 * ev は §11.2 の名前だけ、fb / pal は 'a' か 'b'、step <= 5、sel / ven <= 1 */
size_t c8pat_fmt_clut(char *buf, size_t size, uint8_t step, const char *ev, char fb, char pal,
                      uint8_t sel, uint8_t ven, uint32_t rc, uint32_t n, uint32_t m, uint32_t uf);

#endif /* CLUT8_PAT_H */
