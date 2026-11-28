/* app_logq.c - 標準Cのみ。FITのヘッダを include しない。 */
#include <string.h>
#include "app_logq.h"

void applog_q_init(applog_q_t *q)
{
    if (q == NULL) {
        return;
    }
    q->head  = 0u;
    q->tail  = 0u;
    q->count = 0u;
    q->drops = 0u;
    memset(q->len, 0, sizeof(q->len));
}

void applog_q_note_drop(applog_q_t *q)
{
    if ((q != NULL) && (q->drops != 0xFFFFFFFFu)) {
        q->drops++;
    }
}

bool applog_q_push(applog_q_t *q, const char *line, size_t len)
{
    if (q == NULL) {
        return false;
    }
    if ((line == NULL) || (len == 0u) || (len >= APPLOG_LINE_MAX) || (q->count >= APPLOG_Q_LINES)) {
        applog_q_note_drop(q);
        return false;
    }
    memcpy(q->line[q->head], line, len);
    q->line[q->head][len] = '\0';
    q->len[q->head]       = (uint16_t)len;
    q->head = (uint8_t)((q->head + 1u) % APPLOG_Q_LINES);
    q->count++;
    return true;
}

const char *applog_q_peek(const applog_q_t *q, size_t *len)
{
    if ((q == NULL) || (q->count == 0u)) {
        if (len != NULL) {
            *len = 0u;
        }
        return NULL;
    }
    if (len != NULL) {
        *len = q->len[q->tail];
    }
    return q->line[q->tail];
}

void applog_q_pop(applog_q_t *q)
{
    if ((q == NULL) || (q->count == 0u)) {
        return;
    }
    q->tail = (uint8_t)((q->tail + 1u) % APPLOG_Q_LINES);
    q->count--;
}

uint32_t applog_q_drops(const applog_q_t *q)
{
    return (q == NULL) ? 0u : q->drops;
}

size_t applog_q_count(const applog_q_t *q)
{
    return (q == NULL) ? 0u : (size_t)q->count;
}
