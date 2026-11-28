/* ui_logic.h - 株価ビューの画面の状態(標準Cのみ。ホストでテストする。docs/design.md §6・§13.3)
 * タップ検出(画面のどこでも「更新」)、表示キー(stk_view_t / clk_view_t)、画面の切り替え(株価 ↔ 時計・天気)を持つ。ボタン・デモは無い。
 * 時刻はすべて呼び出し側が渡す ms。ハードに触らない。 */
#ifndef UI_LOGIC_H
#define UI_LOGIC_H

#include <stdint.h>
#include <stdbool.h>
#include "nav_logic.h"      /* nav_sample_t */
#include "stk_core.h"
#include "clk_core.h"

#define UI_TAP_DOWN_N        (2u)      /* 押下確定の連続標本数 */
#define UI_TAP_UP_N          (3u)      /* 離し確定の連続標本数(離しの確定でイベント) */
#define UI_TAP_HOLD_MAX_MS   (3000u)   /* これを超える押しっぱなしはタップにしない */
#define UI_EV_Q              (8u)

/* 画面の表示時間(ms。設計 §13.0)。試験・調整用に -D で上書きできる */
#ifndef APP_DWELL_STOCK_MS
#define APP_DWELL_STOCK_MS   (10000u)
#endif
#ifndef APP_DWELL_CLOCK_MS
#define APP_DWELL_CLOCK_MS   (5000u)
#endif

enum { UI_SCR_STOCK = 0, UI_SCR_CLOCK = 1 };

typedef enum { UI_SRC_TOUCH = 0, UI_SRC_UART } ui_src_t;
typedef enum { UI_EV_NONE = 0, UI_EV_TAP } ui_ev_type_t;

/* イベント。TAP: src = ui_src_t */
typedef struct { uint8_t type, src; } ui_ev_t;

typedef struct {
    uint8_t     ts, tcnt;            /* タップ検出の状態 / 連続標本数 */
    uint32_t    t_down;              /* 押下が確定した時刻 */
    stk_view_t  view;                /* 今の表示値 */
    stk_view_t  drawn;               /* 最後に描いた表示値 */
    uint8_t     drawn_valid;
    uint8_t     screen;              /* UI_SCR_*。起動時は株価 */
    uint8_t     drawn_screen;        /* 最後に描いた画面 */
    uint32_t    t_scr;               /* 今の画面になった時刻(ms) */
    uint32_t    now;                 /* ui_update が最後に受けた now_ms(ui_tap_inject が使う) */
    clk_view_t  cview;               /* 時計・天気画面の表示値。起動時は「時刻未同期・天気なし」 */
    clk_view_t  cdrawn;              /* 最後に描いた時計・天気画面の表示値 */
    ui_ev_t     ev[UI_EV_Q];
    uint8_t     ev_h, ev_t, ev_n;
    uint16_t    ev_lost;
} ui_t;

/* 表示値は「起動中」(net = OFF、時計・更新時刻なし、値なし)にそろえる */
void ui_init(ui_t *ui, uint32_t now_ms);

/* 10ms ごとに呼ぶ。real = タッチ標本(NULL = 標本なし。状態を変えない)。座標は使わない */
void ui_update(ui_t *ui, const nav_sample_t *real, uint32_t now_ms);

/* 表示値を渡す(毎周期でよい。変わった時だけ再描画される) */
void ui_set_view(ui_t *ui, const stk_view_t *view);

/* UART の x(更新)など、タップと同じイベントを起こす */
void ui_tap_inject(ui_t *ui, ui_src_t src);

bool ui_event_pop(ui_t *ui, ui_ev_t *out);

/* 時計・天気画面の表示値を渡す(毎周期でよい。見えている間に変わった時だけ再描画される) */
void ui_set_clk(ui_t *ui, const clk_view_t *view);

/* 10ms ごとに呼ぶ(ui_update の後)。株価 APP_DWELL_STOCK_MS → 時計・天気 APP_DWELL_CLOCK_MS → 株価…。
 * 時計・天気画面が使えない(cview.clock_min > 1439 = 時刻未同期)間は、株価のまま(t_scr を動かさない)。差は 32bit の引き算で比べる */
void ui_screen_step(ui_t *ui, uint32_t now_ms);

/* 画面を決める。t_scr = now_ms(同じ画面なら t_scr だけ更新)。タップ(ev_push)とプレビューが使う */
void ui_set_screen(ui_t *ui, uint8_t scr, uint32_t now_ms);

/* 今の画面のパレット番号: 株価 = 0、時計・天気 = 1 + 天気の種類(have = 0 は CLK_UNKNOWN → 8)。ui_pal_ptr(id) が 256 エントリーを返す */
uint8_t ui_pal_id(const ui_t *ui);

bool ui_view_changed(const ui_t *ui);   /* 画面が変わった、または今の画面の表示値が、前回描画したものと違うか(memcmp)。見えていない画面の値は見ない */
void ui_view_mark(ui_t *ui);            /* 今の画面と表示値を「描画済み」にする */

#endif /* UI_LOGIC_H */
