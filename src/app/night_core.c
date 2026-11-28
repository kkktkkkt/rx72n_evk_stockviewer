/* night_core.c - 夜間のバックライト消灯の論理(docs/design.md §14)。APP_NIGHT = 0 では中身が消える */
#include "night_core.h"

#if APP_NIGHT

static const char *const k_mode[3]  = { "on", "dim", "off" };
static const char *const k_cause[7] = { "boot", "night", "day", "touch", "timeout", "uart", "state" };

bool night_in_window(uint16_t clock_min, uint16_t start_min, uint16_t end_min)
{
    if ((clock_min > 1439u) || (start_min == end_min)) {
        return false;
    }
    if (start_min < end_min) {
        return (clock_min >= start_min) && (clock_min < end_min);
    }
    return (clock_min >= start_min) || (clock_min < end_min);
}

void night_init(night_t *n)
{
    n->mode    = (uint8_t)NIGHT_ON;
    n->cause   = (uint8_t)NIGHT_C_BOOT;
    n->force   = 0u;
    n->awake   = 0u;
    n->swallow = 0u;
    n->uart    = 0u;
    n->t_wake  = 0u;
}

bool night_update(night_t *n, uint32_t now_ms, uint16_t clock_min)
{
    uint8_t base, want, cause;
    bool    timeout = false;

    if ((n->awake != 0u) && ((uint32_t)(now_ms - n->t_wake) >= (uint32_t)APP_NIGHT_WAKE_MS)) {
        n->awake = 0u;
        timeout  = true;
    }
    if (n->force == 1u) {
        base = (uint8_t)NIGHT_OFF;
    } else if (n->force == 3u) {
        base = (uint8_t)NIGHT_ON;
    } else {
        base = night_in_window(clock_min, (uint16_t)APP_NIGHT_START_MIN, (uint16_t)APP_NIGHT_END_MIN) ? (uint8_t)NIGHT_OFF : (uint8_t)NIGHT_ON;
    }
    want = (n->awake != 0u) ? (uint8_t)NIGHT_ON : base;

    if (n->awake != 0u) {
        cause = (uint8_t)NIGHT_C_TOUCH;
    } else if (n->uart != 0u) {
        cause = (uint8_t)NIGHT_C_UART;
    } else if (timeout && (want == (uint8_t)NIGHT_OFF)) {
        cause = (uint8_t)NIGHT_C_TIMEOUT;
    } else {
        cause = (want == (uint8_t)NIGHT_OFF) ? (uint8_t)NIGHT_C_NIGHT : (uint8_t)NIGHT_C_DAY;
    }
    n->uart = 0u;
    if (want == n->mode) {
        if (timeout) {
            n->cause = cause;            /* 点灯のまま窓が終わっていた(day)。ログは出さない */
        }
        return false;
    }
    n->mode  = want;
    n->cause = cause;
    return true;
}

bool night_filter(night_t *n, bool pressed, uint32_t now_ms)
{
    if (!pressed) {
        n->swallow = 0u;
        return true;
    }
    if ((n->mode != (uint8_t)NIGHT_ON) && (n->awake == 0u)) {
        n->awake   = 1u;
        n->swallow = 1u;
    }
    if (n->awake != 0u) {
        n->t_wake = now_ms;              /* 触っている間は消えない */
    }
    return n->swallow == 0u;
}

void night_force_next(night_t *n)
{
    n->force = (n->force == 0u) ? 1u : ((n->force == 1u) ? 3u : 0u);
    n->uart  = 1u;
}

uint8_t night_level(const night_t *n)
{
    return (n->mode == (uint8_t)NIGHT_OFF) ? 0u : (uint8_t)NIGHT_LEVEL_ON;
}

const char *night_mode_name(uint8_t mode)
{
    return (mode < 3u) ? k_mode[mode] : "?";
}

const char *night_cause_name(uint8_t cause)
{
    return (cause < 7u) ? k_cause[cause] : "?";
}

#endif /* APP_NIGHT */
