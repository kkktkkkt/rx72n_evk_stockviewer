/* ui_logic.h - 株価ビューの画面の状態(標準Cのみ。ホストでテストする。docs/design.md §6)
 * タップ検出(画面のどこでも「更新」)と、表示キー(stk_view_t)だけを持つ。画面遷移・ボタン・デモは無い。
 * 時刻はすべて呼び出し側が渡す ms。ハードに触らない。 */
#ifndef UI_LOGIC_H
#define UI_LOGIC_H

#include <stdint.h>
#include <stdbool.h>
#include "nav_logic.h"      /* nav_sample_t */
#include "stk_core.h"

#define UI_TAP_DOWN_N        (2u)      /* 押下確定の連続標本数 */
#define UI_TAP_UP_N          (3u)      /* 離し確定の連続標本数(離しの確定でイベント) */
#define UI_TAP_HOLD_MAX_MS   (3000u)   /* これを超える押しっぱなしはタップにしない */
#define UI_EV_Q              (8u)

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

bool ui_view_changed(const ui_t *ui);   /* 表示値が、前回描画したものと違うか(memcmp) */
void ui_view_mark(ui_t *ui);            /* 今の表示値を「描画済み」にする */

#endif /* UI_LOGIC_H */
