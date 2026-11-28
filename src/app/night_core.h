/* night_core.h - 夜間のバックライト消灯の論理(docs/design.md §14。Phase 7a)。標準 C のみ(FIT・platform.h を include しない)。
 * 窓(既定 23:00〜06:00。日をまたぐ)の中は消灯、外・時刻が未同期は点灯。消灯中のタッチは点灯して 30 秒後に再び消灯し、その最初のタッチは渡さない。
 * APP_NIGHT = 0 では night_core.c の中身が消える。 */
#ifndef NIGHT_CORE_H
#define NIGHT_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "wifi_sw.h"

#ifndef APP_NIGHT_START_MIN
#define APP_NIGHT_START_MIN  (1380u)     /* 23:00 */
#endif
#ifndef APP_NIGHT_END_MIN
#define APP_NIGHT_END_MIN    (360u)      /* 06:00(この分ちょうどは窓の外) */
#endif
#ifndef APP_NIGHT_WAKE_MS
#define APP_NIGHT_WAKE_MS    (30000u)    /* 起こしてから消灯に戻るまで */
#endif
#define NIGHT_LEVEL_ON       (20u)       /* night_level の点灯(lcd_bl_set の LCD_BL_STEPS と同じ) */

typedef enum { NIGHT_ON = 0, NIGHT_DIM, NIGHT_OFF } night_mode_t;     /* NIGHT_DIM は 7b 用(7a では使わない) */
typedef enum { NIGHT_C_BOOT = 0, NIGHT_C_NIGHT, NIGHT_C_DAY, NIGHT_C_TOUCH, NIGHT_C_TIMEOUT, NIGHT_C_UART, NIGHT_C_STATE } night_cause_t;

typedef struct {
    uint8_t  mode;       /* night_mode_t */
    uint8_t  cause;      /* night_cause_t(最後に mode が変わった原因。同じ mode のまま窓が終わったときも更新) */
    uint8_t  force;      /* 0 自動 / 1 消灯 / 3 点灯(2 = 暗くは 7b) */
    uint8_t  awake;      /* タッチで起こしている間 1 */
    uint8_t  swallow;    /* 起こしたタッチの指が離れるまで 1 */
    uint8_t  uart;       /* b を受けた(次の night_update で cause = uart) */
    uint32_t t_wake;     /* 最後のタッチの now_ms */
} night_t;

/* m が 0〜1439 でなければ false(未同期)。start == end は false。start > end は日をまたぐ(m >= start か m < end) */
bool night_in_window(uint16_t clock_min, uint16_t start_min, uint16_t end_min);

void night_init(night_t *n);                                          /* 点灯・cause = boot */

/* 10ms ごと。mode が変わったら true(呼び側がログと lcd_bl_set を行う)。clock_min は 1440 以上 = 未同期 */
bool night_update(night_t *n, uint32_t now_ms, uint16_t clock_min);

/* タッチの標本を ui_update へ渡すか。true = 渡す / false = 渡さない(起こしただけ。離すまで)。標本ごとに呼ぶ(標本なしの周期は呼ばない) */
bool night_filter(night_t *n, bool pressed, uint32_t now_ms);

void night_force_next(night_t *n);                                    /* UART b: 自動 → 消灯 → 点灯 → 自動 */
uint8_t night_level(const night_t *n);                                /* 0(消灯)/ NIGHT_LEVEL_ON(点灯) */
const char *night_mode_name(uint8_t mode);                            /* "on" "dim" "off" */
const char *night_cause_name(uint8_t cause);

#endif /* NIGHT_CORE_H */
