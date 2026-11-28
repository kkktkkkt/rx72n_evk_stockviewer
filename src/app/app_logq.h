/* app_logq.h - UART送信の行キュー(標準Cのみ。FITを include しない)
 * メインループ専用。割り込みから触らない(排他制御なし)。仕様: docs/design-uart.md §5.2 */
#ifndef APP_LOGQ_H
#define APP_LOGQ_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "app_logfmt.h"

#define APPLOG_Q_LINES  (8u)

typedef struct {
    char     line[APPLOG_Q_LINES][APPLOG_LINE_MAX];
    uint16_t len[APPLOG_Q_LINES];
    uint8_t  head;              /* 次に書く */
    uint8_t  tail;              /* 先頭 */
    uint8_t  count;
    uint32_t drops;             /* 破棄累計(0xFFFFFFFF で飽和) */
} applog_q_t;

void applog_q_init(applog_q_t *q);

/* 満杯・len == 0・len >= APPLOG_LINE_MAX・引数不正は false(破棄累計 +1) */
bool applog_q_push(applog_q_t *q, const char *line, size_t len);

/* 先頭の行。空なら NULL(*len = 0)。len は NULL でもよい */
const char *applog_q_peek(const applog_q_t *q, size_t *len);

/* 先頭を捨てる。空なら何もしない */
void applog_q_pop(applog_q_t *q);

/* 整形失敗・送信エラーによる破棄を数える(飽和) */
void applog_q_note_drop(applog_q_t *q);

uint32_t applog_q_drops(const applog_q_t *q);
size_t   applog_q_count(const applog_q_t *q);

#endif /* APP_LOGQ_H */
