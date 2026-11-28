/* stk_core.c - 株価ビューの数値(前日比・騰落率・整形・市場の状態)。標準Cのみ。FIT・platform.h を include しない。docs/design.md §4・§5.4
 * 整数だけ。float / double を使わない(printf の浮動小数点も使わない)。 */
#include <string.h>
#include "stk_core.h"

#define HALF_RANGE   (0x80000000u)       /* 32bit の差を「負」と見なす境 */
#define STK_JST_OFFSET_S (32400u)        /* JST = UTC + 9 時間 */

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
    out->dir       = (out->pct_bp > 0) ? (uint8_t)STK_UP : ((out->pct_bp < 0) ? (uint8_t)STK_DOWN : (uint8_t)STK_FLAT);   /* 丸めた騰落率の符号(§4.2) */
    return true;
}

/* ---- 銘柄表 ---- */

static const stk_sym_t k_sym[STK_ROWS] = {
    { (uint8_t)STK_KIND_YEN,   STK_LUNCH_S, STK_LUNCH_E },     /* 6723.T */
    { (uint8_t)STK_KIND_INDEX, STK_LUNCH_S, STK_LUNCH_E },     /* ^N225 */
    { (uint8_t)STK_KIND_INDEX, 0u,          0u }               /* ^GSPC(昼休みなし) */
};

const stk_sym_t *stk_sym(uint8_t row)
{
    return (row < (uint8_t)STK_ROWS) ? &k_sym[row] : NULL;
}

uint8_t stk_row_kind(uint8_t row)
{
    return (row == 0u) ? (uint8_t)STK_KIND_YEN : (uint8_t)STK_KIND_INDEX;
}

/* ---- 整形 ---- */

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

size_t stk_fmt_pct(char *buf, size_t size, int32_t pct_bp)
{
    char     t[12];
    size_t   n = 0u;

    if (!arg_ok(buf, size) || (pct_bp > STK_BP_MAX) || (pct_bp < -STK_BP_MAX)) {
        return 0u;
    }
    if (pct_bp != 0) {                                                 /* 0 は符号なしの 0.00%(+0.00% −0.00% は出さない。§4.2) */
        t[n++] = (pct_bp > 0) ? '+' : '\x03';
    }
    n += put_x100(&t[n], abs32(pct_bp));
    t[n++] = '%';
    return emit(buf, size, t, n);
}

size_t stk_fmt_amt(char *buf, size_t size, int32_t diff_x100, uint8_t kind, uint8_t dir)
{
    char     t[16];
    size_t   n = 0u;
    uint32_t a;
    uint32_t amt;

    if (!arg_ok(buf, size) || (diff_x100 > STK_X100_MAX) || (diff_x100 < -STK_X100_MAX) || (kind > (uint8_t)STK_KIND_INDEX) || (dir > (uint8_t)STK_FLAT)) {
        return 0u;
    }
    a   = abs32(diff_x100);
    amt = (kind == (uint8_t)STK_KIND_YEN) ? ((a + 50u) / 100u) : a;   /* 丸めた金額 A(YEN は整数円) */
    if ((amt != 0u) && (dir != (uint8_t)STK_FLAT)) {                   /* 矢印は方向があって A ≠ 0 のときだけ */
        t[n++] = (dir == (uint8_t)STK_UP) ? '\x01' : '\x02';
    }
    n += (kind == (uint8_t)STK_KIND_YEN) ? put_commas(&t[n], amt) : put_x100(&t[n], amt);
    return emit(buf, size, t, n);
}

/* ---- 市場の状態 ---- */

uint8_t stk_market(const stk_sym_t *sym, bool synced, uint32_t now_jst, uint32_t mtime, uint32_t rs, uint32_t re)
{
    uint32_t now_utc = now_jst - STK_JST_OFFSET_S;
    uint32_t span    = re - rs;
    uint32_t tod;
    uint32_t age;

    if ((!synced) || (sym == NULL)) {
        return (uint8_t)STK_MKT_NONE;
    }
    if ((span == 0u) || (span >= HALF_RANGE) || ((uint32_t)(now_utc - rs) >= span)) {
        return (uint8_t)STK_MKT_CLOSED;                                /* 区間が不正、または now が [rs, re) の外 */
    }
    tod = now_jst % 86400u;
    if ((sym->lunch_s != sym->lunch_e) && (tod >= sym->lunch_s) && (tod < sym->lunch_e)) {
        return (uint8_t)STK_MKT_CLOSED;                                /* 昼休み */
    }
    if ((uint32_t)(mtime - rs) < HALF_RANGE) {                         /* data_ok: mtime ≧ rs かつ 2 時間以内(mtime が先なら新しい) */
        age = now_utc - mtime;
        return ((age >= HALF_RANGE) || (age <= STK_FRESH_S)) ? (uint8_t)STK_MKT_TRADING : (uint8_t)STK_MKT_CLOSED;
    }
    return ((uint32_t)(now_utc - rs) <= STK_OPEN_GRACE_S) ? (uint8_t)STK_MKT_TRADING : (uint8_t)STK_MKT_CLOSED;   /* open_grace */
}

/* ---- 表示用データの組み立て ---- */

void stk_view_make(stk_view_t *v, uint8_t net, uint8_t busy, uint8_t fail_mask, const stk_slot_t slot[STK_ROWS],
                   uint32_t now_ms, uint16_t clock_min, uint16_t upd_min)
{
    uint8_t  i;
    uint32_t age_s;
    uint32_t newest = 0u;          /* have の行のうち、最後の成功からの秒が最小のもの */
    bool     any    = false;

    if ((v == NULL) || (slot == NULL)) {
        return;
    }
    memset(v, 0, sizeof *v);
    v->net       = net;
    v->busy      = (busy != 0u) ? 1u : 0u;
    v->partial   = ((fail_mask & 7u) != 0u) ? 1u : 0u;
    v->clock_min = clock_min;
    v->upd_min   = upd_min;
    for (i = 0u; i < (uint8_t)STK_ROWS; i++) {
        stk_row_t *r = &v->row[i];

        r->fail = (((fail_mask >> i) & 1u) != 0u) ? 1u : 0u;
        if (slot[i].have == 0u) {
            continue;
        }
        age_s         = (now_ms - slot[i].ok_ms) / 1000u;
        r->have       = 1u;
        r->stale      = (age_s >= STK_STALE_S) ? 1u : 0u;
        r->mkt        = slot[i].mkt;
        r->price_x100 = slot[i].price_x100;
        r->diff_x100  = slot[i].diff_x100;
        r->pct_bp     = slot[i].pct_bp;
        if (!any || (age_s < newest)) {
            newest = age_s;
            any    = true;
        }
    }
    v->have_any  = any ? 1u : 0u;
    v->stale_all = (any && (newest >= STK_STALE_S)) ? 1u : 0u;
}
