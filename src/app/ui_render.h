/* ui_render.h - ui_t -> 480x272 の 8bpp(CLUT8)の面(標準Cのみ。ホストでテストする。設計 §7.2, §9.1, §20.4)
 * 面の左 480 列の全画素を毎回書く(クリア不要。右の 32 列は書かない)。描く順: 背景 → 数字 → バー → アイコン(天気・更新・Wi-Fi。
 * 下地に焼き込み済みの不透明なスプライト)→ 押下の暗転/白光 → デモ中タグ。ホームに日付・気温・天気・更新時刻・更新アイコン、
 * 待機以外の 4 画面のヘッダーに Wi-Fi アイコン(design §22.8)。
 * 色はすべてパレット(ui_pal)の添字。段階の表(ramp)と置き換え表(remap)は tools/gen_ui_assets.py が作る。 */
#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <stdint.h>
#include "ui_logic.h"

/* 押下の暗転 / 離した時の白光: ボタンの形(A8)がこの値以上の画素だけ、添字を ui_remap で置き換える。
 * 暗転 = 黒を 6/32、白光 = 白を 19/32 混ぜた色(P1 の 52/256・154/256 と同じ濃さ。tools/gen_ui_assets.py の DIM_L / FLASH_L) */
#define UI_REMAP_TH     (128u)

/* fb: 512 x 272 の uint8_t 配列(行の長さ GFX_STRIDE)。左 480 列だけ書く */
void ui_render(const ui_t *ui, uint8_t *fb);

#endif /* UI_RENDER_H */
