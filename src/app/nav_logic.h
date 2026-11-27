/* nav_logic.h - タッチ標本 -> 画像番号(標準Cのみ。設計 §11) */
#ifndef NAV_LOGIC_H
#define NAV_LOGIC_H

#include <stdint.h>
#include <stdbool.h>

/* 暫定値(実機で調整) */
#define NAV_SAMPLE_MS        (10u)
#define NAV_PRESS_CONFIRM    (2u)    /* 押下確定に必要な連続標本数 */
#define NAV_RELEASE_CONFIRM  (3u)    /* 離し確定に必要な連続標本数 */
#define NAV_MIN_INTERVAL_MS  (300u)  /* 動作から次の押下確定までの最小間隔 */

#define NAV_MODE_HALVES      (0)     /* 左半分=前 / 右半分=次 */
#define NAV_MODE_NEXT_ONLY   (1)     /* どこでも次へ */
#ifndef NAV_MODE
#define NAV_MODE             NAV_MODE_HALVES
#endif

typedef struct {
    bool     pressed;   /* 1点以上触れている */
    uint8_t  points;    /* タッチ点数 */
    uint16_t x;         /* 点1の座標 */
    uint16_t y;
} nav_sample_t;

typedef enum {
    NAV_ST_IDLE = 0,
    NAV_ST_PRESS_PENDING,
    NAV_ST_HELD,
    NAV_ST_RELEASE_PENDING
} nav_state_t;

typedef struct {
    nav_state_t state;
    uint8_t     count;          /* 連続標本カウント */
    uint8_t     image_count;
    uint8_t     index;
    bool        has_action;     /* 一度でも動作したか */
    uint16_t    x_max;
    uint16_t    pend_x;         /* 押下確定待ち中の最新X */
    uint32_t    last_action_ms;
} nav_t;

void    nav_init(nav_t *nav, uint8_t image_count, uint16_t x_max);

/* 画像番号が変わったら true。nav_t 以外の状態は持たない。 */
bool    nav_update(nav_t *nav, const nav_sample_t *s, uint32_t now_ms);

uint8_t nav_index(const nav_t *nav);

/* 画像番号を直接 ±1(端は循環)。dir > 0 で次、dir < 0 で前、0 は何もしない。
 * 番号が変わったら true。画像1枚・NULL は false。
 * 状態機械と300ms間隔規則(last_action_ms 等)には触らない(UART遠隔操作用)。 */
bool    nav_step(nav_t *nav, int8_t dir);

#endif /* NAV_LOGIC_H */
