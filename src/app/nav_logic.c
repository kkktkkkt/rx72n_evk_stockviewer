/* nav_logic.c - 標準Cのみ。FITのヘッダを include しない。 */
#include "nav_logic.h"

void nav_init(nav_t *nav, uint8_t image_count, uint16_t x_max)
{
    if (nav == 0) {
        return;
    }
    nav->state          = NAV_ST_IDLE;
    nav->count          = 0u;
    nav->image_count    = (image_count == 0u) ? 1u : image_count;
    nav->index          = 0u;
    nav->has_action     = false;
    nav->x_max          = x_max;
    nav->pend_x         = 0u;
    nav->last_action_ms = 0u;
}

uint8_t nav_index(const nav_t *nav)
{
    return (nav == 0) ? 0u : nav->index;
}

/* 画像番号を ±1 する(端は循環)。計算はここだけ。dir > 0 で次、それ以外は前 */
static void nav_move(nav_t *nav, int dir)
{
    if (dir > 0) {
        nav->index = (uint8_t)((nav->index + 1u) % nav->image_count);
    } else {
        nav->index = (uint8_t)((nav->index + nav->image_count - 1u) % nav->image_count);
    }
}

bool nav_step(nav_t *nav, int8_t dir)
{
    uint8_t old;

    if ((nav == 0) || (dir == 0)) {
        return false;
    }
    old = nav->index;
    nav_move(nav, (int)dir);
    return (nav->index != old);      /* 状態機械・間隔規則には触らない */
}

/* 押下確定時の動作。画像番号が変わったら true */
static bool nav_act(nav_t *nav, uint32_t now_ms)
{
    uint8_t old = nav->index;

    /* 間隔条件。符号なし引き算なので 32bit の一周をまたいでも成立 */
    if (nav->has_action &&
        (uint32_t)(now_ms - nav->last_action_ms) < NAV_MIN_INTERVAL_MS) {
        return false;
    }
    nav->has_action     = true;
    nav->last_action_ms = now_ms;

#if (NAV_MODE == NAV_MODE_HALVES)
    if (nav->pend_x < (uint16_t)(nav->x_max / 2u)) {
        /* 左半分: 前へ(端は循環) */
        nav_move(nav, -1);
    } else {
        nav_move(nav, 1);
    }
#else
    nav_move(nav, 1);
#endif
    return (nav->index != old);
}

bool nav_update(nav_t *nav, const nav_sample_t *s, uint32_t now_ms)
{
    bool changed = false;
    bool touching;
    bool multi;

    if ((nav == 0) || (s == 0)) {
        return false;
    }
    touching = s->pressed && (s->points >= 1u);
    multi    = touching && (s->points >= 2u);

    switch (nav->state) {
    case NAV_ST_IDLE:
        if (touching) {
            if (multi) {
                nav->state = NAV_ST_HELD;          /* 動作しない */
            } else {
                nav->state  = NAV_ST_PRESS_PENDING;
                nav->count  = 1u;
                nav->pend_x = s->x;
            }
        }
        break;

    case NAV_ST_PRESS_PENDING:
        if (!touching) {
            nav->state = NAV_ST_IDLE;
            nav->count = 0u;
        } else if (multi) {
            nav->state = NAV_ST_HELD;              /* 2点以上: 動作しない */
            nav->count = 0u;
        } else {
            nav->pend_x = s->x;
            if (nav->count < 255u) {
                nav->count++;
            }
            if (nav->count >= NAV_PRESS_CONFIRM) {
                changed    = nav_act(nav, now_ms);
                nav->state = NAV_ST_HELD;
                nav->count = 0u;
            }
        }
        break;

    case NAV_ST_HELD:
        if (!touching) {
            nav->state = NAV_ST_RELEASE_PENDING;
            nav->count = 1u;
            if (nav->count >= NAV_RELEASE_CONFIRM) {
                nav->state = NAV_ST_IDLE;
                nav->count = 0u;
            }
        }
        break;

    case NAV_ST_RELEASE_PENDING:
        if (touching) {
            nav->state = NAV_ST_HELD;
            nav->count = 0u;
        } else {
            if (nav->count < 255u) {
                nav->count++;
            }
            if (nav->count >= NAV_RELEASE_CONFIRM) {
                nav->state = NAV_ST_IDLE;
                nav->count = 0u;
            }
        }
        break;

    default:
        nav->state = NAV_ST_IDLE;
        nav->count = 0u;
        break;
    }
    return changed;
}
