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

/* 反映待ち(lcd_flip が受け付けてから、まだ表示面が変わっていない)なら true */
bool lcd_flip_busy(void);

/* GR2 アンダーフローを検出していれば 1 を返して検出をクリア。無ければ 0。呼ぶのは1秒に1回程度 */
uint32_t lcd_underflow_poll(void);

#endif /* LCD_PANEL_H */
