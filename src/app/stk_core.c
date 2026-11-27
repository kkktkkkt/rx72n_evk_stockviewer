/* stk_core.c - 株価ビューの数値(前日比・騰落率・整形・市場の状態)。標準Cのみ。FIT・platform.h を include しない。docs/design.md §4・§5.4
 * 整数だけ。float / double を使わない(printf の浮動小数点も使わない)。 */
#include <string.h>
#include "stk_core.h"

#define HALF_RANGE   (0x80000000u)       /* 32bit の差を「負」と見なす境 */

/* ---- 整形の部品 ---- */

static bool arg_ok(char *buf, size_t size)
{
    if ((buf == NULL) || (size == 0u)) {
        return false;
    }
    buf[0] = '\0';
    return true;
}

/* tmp[0..n) を buf へ(NUL 終端)。入りきらなければ buf を空にして 0 */
static size_t emit(char *buf, size_t size, const char *tmp, size_t n)
{
    if ((n + 1u) > size) {
        buf[0] = '\0';
        return 0u;
    }
    memcpy(buf, tmp, n);
    buf[n] = '\0';
    return n;
}

static uint32_t abs32(int32_t v)
{
    return (v < 0) ? (0u - (uint32_t)v) : (uint32_t)v;
}

/* n を 3 桁ごとのカンマ区切りで o へ(NUL なし)。戻り値 = 長さ */
static size_t put_commas(char *o, uint32_t n)
{
    char   rev[12];
    size_t len = 0u;
    size_t i;
    size_t k = 0u;

    do {
        rev[len++] = (char)('0' + (n % 10u));
        n /= 10u;
    } while (n != 0u);
    for (i = 0u; i < len; i++) {
        if ((i != 0u) && (((len - i) % 3u) == 0u)) {
            o[k++] = ',';
        }
        o[k++] = rev[len - 1u - i];
    }
    return k;
}

/* v(×100)を「整数部(カンマ区切り).小数 2 桁」で o へ */
static size_t put_x100(char *o, uint32_t v)
{
    size_t k = put_commas(o, v / 100u);

    o[k++] = '.';
    o[k++] = (char)('0' + ((v % 100u) / 10u));
    o[k++] = (char)('0' + (v % 10u));
    return k;
}

/* ---- 前日比・騰落率 ---- */

bool stk_calc(int32_t price_x100, int32_t prev_x100, stk_calc_t *out)
{
    int32_t diff;
    int64_t a;
    int64_t q;

    if (out == NULL) {
        return false;
    }
    out->diff_x100 = 0;
    out->pct_bp    = 0;
    out->dir       = (uint8_t)STK_FLAT;
    if ((price_x100 < 1) || (price_x100 > STK_X100_MAX) || (prev_x100 < 1) || (prev_x100 > STK_X100_MAX)) {
        return false;
    }
    diff = price_x100 - prev_x100;                                     /* |diff| < 1e8。int32 に収まる */
    a    = (diff < 0) ? -(int64_t)diff : (int64_t)diff;
    q    = ((a * 20000) + (int64_t)prev_x100) / ((int64_t)prev_x100 * 2);   /* round_half_away(|diff| * 10000 / prev) */
    if (q > (int64_t)STK_BP_MAX) {
        return false;
    }
    out->diff_x100 = diff;
    out->pct_bp    = (diff < 0) ? (int32_t)(-q) : (int32_t)q;
    out->dir       = (diff > 0) ? (uint8_t)STK_UP : ((diff < 0) ? (uint8_t)STK_DOWN : (uint8_t)STK_FLAT);
    return true;
}

/* ---- 整形 ---- */

uint8_t stk_row_kind(uint8_t row)
{
    return (row == 0u) ? (uint8_t)STK_KIND_YEN : (uint8_t)STK_KIND_INDEX;
}

size_t stk_fmt_price(char *buf, size_t size, int32_t price_x100, uint8_t kind)
{
    char     t[16];
    size_t   n;
    uint32_t p;

    if (!arg_ok(buf, size) || (price_x100 < 0) || (price_x100 > STK_X100_MAX) || (kind > (uint8_t)STK_KIND_INDEX)) {
        return 0u;
    }
    p = (uint32_t)price_x100;
    n = (kind == (uint8_t)STK_KIND_YEN) ? put_commas(t, (p + 50u) / 100u) : put_x100(t, p);
    return emit(buf, size, t, n);
}

size_t stk_fmt_pct(char *buf, size_t size, int32_t pct_bp, uint8_t dir)
{
    char     t[12];
    size_t   n = 0u;
    uint32_t a;

    if (!arg_ok(buf, size) || (dir > (uint8_t)STK_FLAT) || (pct_bp > STK_BP_MAX) || (pct_bp < -STK_BP_MAX)) {
        return 0u;
    }
    a = abs32(pct_bp);
    if (dir == (uint8_t)STK_FLAT) {
        a = 0u;                                                        /* 変化なし: 符号なしの 0.00% */
    } else {
        t[n++] = (dir == (uint8_t)STK_UP) ? '+' : '\x03';
    }
    n += put_x100(&t[n], a);
    t[n++] = '%';
    return emit(buf, size, t, n);
}

size_t stk_fmt_amt(char *buf, size_t size, int32_t diff_x100, uint8_t kind)
{
    char     t[16];
    size_t   n = 0u;
    uint32_t a;

    if (!arg_ok(buf, size) || (diff_x100 > STK_X100_MAX) || (diff_x100 < -STK_X100_MAX) || (kind > (uint8_t)STK_KIND_INDEX)) {
        return 0u;
    }
    a = abs32(diff_x100);
    if (a == 0u) {                                                     /* 変化なし: 矢印なし */
        t[n++] = '0';
        if (kind == (uint8_t)STK_KIND_INDEX) {
            t[n++] = '.';
            t[n++] = '0';
            t[n++] = '0';
        }
        return emit(buf, size, t, n);
    }
    t[n++] = (diff_x100 > 0) ? '\x01' : '\x02';
    n += (kind == (uint8_t)STK_KIND_YEN) ? put_commas(&t[n], (a + 50u) / 100u) : put_x100(&t[n], a);
    return emit(buf, size, t, n);
}

/* ---- 市場の状態 ---- */

uint8_t stk_market(bool synced, uint32_t now_utc, uint32_t mtime, uint32_t rs, uint32_t re)
{
    uint32_t span = re - rs;
    uint32_t age;

    if (!synced) {
        return (uint8_t)STK_MKT_NONE;
    }
    if ((span == 0u) || (span >= HALF_RANGE) || ((uint32_t)(now_utc - rs) >= span)) {
        return (uint8_t)STK_MKT_CLOSED;                                /* 区間が不正、または now が [rs, re) の外 */
    }
    if ((uint32_t)(mtime - rs) >= HALF_RANGE) {
        return (uint8_t)STK_MKT_CLOSED;                                /* mtime < rs(開場前の足) */
    }
    age = now_utc - mtime;
    if ((age < HALF_RANGE) && (age > STK_FRESH_S)) {
        return (uint8_t)STK_MKT_CLOSED;                                /* 最新の足が古い。age が「負」(mtime が先)なら新しいと見なす */
    }
    return (uint8_t)STK_MKT_TRADING;
}
