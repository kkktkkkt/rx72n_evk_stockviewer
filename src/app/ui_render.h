/* ui_render.h - ui_t -> 480x272 の 8bpp(CLUT8)の面(標準Cのみ。ホストでテストする。docs/design.md §3.9・§3.13)
 * 面の左 480 列の全画素を毎回書く(クリア不要。右の 32 列は書かない)。描く順は §3.13:
 * ヘッダー帯 → 行の帯 x 3 → フッター帯 → Wi-Fi アイコン → 時計・更新時刻 → 各行の丸と状態の文字 → 現在値・騰落率・金額。
 * 色はすべてパレット(ui_pal)の添字。帯・グリフ・ramp は tools/gen_stock_assets.py が作る。 */
#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <stdint.h>
#include "ui_logic.h"

/* fb: 512 x 272 の uint8_t 配列(行の長さ GFX_STRIDE)。左 480 列だけ書く */
void ui_render(const ui_t *ui, uint8_t *fb);

/* §3.9 の優先順位。表示の決定の試験用に公開 */
uint8_t ui_foot_index(const stk_view_t *v);   /* 0..5 = F0..F5 */
uint8_t ui_icon_index(const stk_view_t *v);   /* 0 = ON、1 = CONN、2 = STALE、3 = OFF */
uint8_t ui_row_dir(const stk_row_t *r);       /* STK_UP / STK_DOWN / STK_FLAT(値なし = FLAT) */

#endif /* UI_RENDER_H */
