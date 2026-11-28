/* lcd_panel.h - LCD(GLCDC + GPIO)。ハード依存(設計 §6〜§8、v2 §7, §9、§20)
 * RAM2 に CLUT8(8bpp、行幅 512)の全画面を2面持つ(面0 / 面1)。パレット(ui_pal)は lcd_init が1回だけ書く。裏面に描いて lcd_flip で入れ替える。 */
#ifndef LCD_PANEL_H
#define LCD_PANEL_H

#include <stdint.h>
#include <stdbool.h>

typedef enum { LCD_FLIP_OK = 0, LCD_FLIP_BUSY, LCD_FLIP_FAIL } lcd_flip_t;

/* GPIO安全状態 -> LCD端子 -> GLCDC起動(GR2 = 面0)-> DISP有効 -> バックライト点灯。
 * 画像はコピーしない(呼ぶ前に lcd_fb(0) に絵を描いておく)。失敗時は false(バックライトは点けない)。理由は g_app_dbg.err_code。 */
bool lcd_init(void);

/* 面 i(0 / 1)の先頭。512 x 272 の uint8_t(左 480 列が表示) */
uint8_t *lcd_fb(uint8_t i);

/* 面 i を次の垂直同期で表示面にする。OK = 受け付けた / BUSY = 前の反映待ち(あとで再試行)/ FAIL = 異常(APP_ERR_LCD_FLIP を記録) */
lcd_flip_t lcd_flip(uint8_t i);

/* パレット pal(256 エントリー、0xFFRRGGBB)と面 i を、同じ垂直同期で入れ替える(docs/design.md §13.2)。
 * R_GLCDC_ClutUpdate_NoReflect を 1 回呼び、成功したら続けて R_GLCDC_BufferChange(1 つの関数に閉じる)。
 * OK = 受け付けた(完了は lcd_flip_busy で見る)/ BUSY = NoReflect が反映待ちで断られた(何も変えていない。あとで再試行してよい)/
 * FAIL = NoReflect の失敗(APP_ERR_LCD_CLUT)、または NoReflect 成功後の BufferChange の失敗(APP_ERR_LCD_FLIP。BUSY もここ。再試行すると NoReflect が 2 回になるため致命)。
 * 表示中のパレットと同じときは呼ばず lcd_flip を使うこと。 */
lcd_flip_t lcd_flip_pal(uint8_t i, const uint32_t *pal);

/* 反映待ち(lcd_flip が受け付けてから、まだ表示面が変わっていない)なら true */
bool lcd_flip_busy(void);

/* GR2 アンダーフローを検出していれば 1 を返して検出をクリア。無ければ 0。呼ぶのは1秒に1回程度 */
uint32_t lcd_underflow_poll(void);

/* バックライトの点灯・消灯(docs/design.md §14)。level 0 = 消灯、1〜20(LCD_BL_STEPS)= 点灯。lcd_init が成功した後に呼ぶ。GLCDC・DISP には触れない */
#define LCD_BL_STEPS (20u)
void lcd_bl_set(uint8_t level);

#endif /* LCD_PANEL_H */
