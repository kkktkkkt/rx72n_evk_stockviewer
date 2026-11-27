/* stk_core.h - 株価ビューの数値(前日比・騰落率・整形・市場の状態)と表示用データ(標準Cのみ。docs/design.md §4・§5.4)
 * 整数だけ(float / double を使わない)。FIT・platform.h を include しない。ホストで試験する。 */
#ifndef STK_CORE_H
#define STK_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* dir = 色・帯の添字(設計 §3.2)。上昇 = 0(赤)、下落 = 1(緑)、変化なし・値なし = 2(灰) */
enum { STK_UP = 0, STK_DOWN = 1, STK_FLAT = 2 };
enum { STK_NET_OFF = 0, STK_NET_CONN, STK_NET_ON, STK_NET_ERR };
enum { STK_MKT_NONE = 0, STK_MKT_TRADING, STK_MKT_CLOSED };
enum { STK_KIND_YEN = 0, STK_KIND_INDEX = 1 };      /* 銘柄の種類(§4.2)。YEN = 現在値・金額を整数円で出す */

#define STK_ROWS       (3u)          /* 0 = 6723.T(円)、1 = ^N225、2 = ^GSPC */
#define STK_NONE16     (0xFFFFu)     /* clock_min / upd_min の「なし」 */
#define STK_X100_MAX   (99999999)    /* 価格・前日の終値・前日比の絶対値の上限(×100。999,999.99)*/
#define STK_BP_MAX     (99999)       /* 騰落率の絶対値の上限(0.01% 単位。999.99%) */
#define STK_FRESH_S    (1800u)       /* 最新の足がこれ以内(秒)なら「取引中」(§5.4) */

typedef struct {
    uint8_t  have;             /* 値を持っている */
    uint8_t  stale;            /* その銘柄の最後の成功から 30 分以上 */
    uint8_t  fail;             /* 取得周期の終わりに失敗が残っている(値がなければ「取得失敗」) */
    uint8_t  mkt;              /* STK_MKT_* */
    int32_t  price_x100;
    int32_t  diff_x100;
    int32_t  pct_bp;           /* 0.01% = 1 */
} stk_row_t;

/* 表示キーとして memcmp で比べるので、パディングを作らない(rsv2 は 4 バイト境界にそろえるための明示の穴埋め。設計 §4.4 に無い) */
typedef struct {
    uint8_t   net;             /* STK_NET_* */
    uint8_t   busy;            /* 取得中 */
    uint8_t   have_any;        /* 1 銘柄でも取れた */
    uint8_t   stale_all;       /* 最新の成功から 30 分以上 */
    uint8_t   partial;         /* 取得周期の終わりに失敗が残っている */
    uint8_t   rsv;             /* 0 */
    uint16_t  clock_min;       /* 時計(0..1439。STK_NONE16 = 未同期) */
    uint16_t  upd_min;         /* 更新時刻(同。STK_NONE16 = なし) */
    uint16_t  rsv2;            /* 0 */
    stk_row_t row[STK_ROWS];
} stk_view_t;

/* ---- 前日比・騰落率(§4.2) ---- */
typedef struct {
    int32_t diff_x100;         /* price - prev */
    int32_t pct_bp;            /* round_half_away(diff * 10000 / prev)。0.01% = 1 */
    uint8_t dir;               /* diff > 0: STK_UP、< 0: STK_DOWN、= 0: STK_FLAT(pct が 0 に丸まっても diff ≠ 0 なら UP / DOWN のまま) */
} stk_calc_t;

/* price_x100・prev_x100 は 1〜STK_X100_MAX。外れる・|pct_bp| > STK_BP_MAX・out が NULL は false(out は 0・STK_FLAT)。int64 は割り算の 1 か所だけ */
bool stk_calc(int32_t price_x100, int32_t prev_x100, stk_calc_t *out);

/* ---- 整形(NUL 終端。戻り値 = 長さ。失敗(範囲外・size 不足・kind / dir が不正)は 0 で buf[0] = '\0'。buf が NULL か size = 0 なら何も書かず 0) ----
 * 記号は \x01 = ▲、\x02 = ▼、\x03 = −(U+2212)。ui_render のグリフ表がこの 3 つを読む(§3.6) */
size_t stk_fmt_price(char *buf, size_t size, int32_t price_x100, uint8_t kind);   /* YEN: 2,433(整数円。0.5 円は切り上げ)/ INDEX: 57,953.84。0〜STK_X100_MAX */
size_t stk_fmt_pct(char *buf, size_t size, int32_t pct_bp, uint8_t dir);          /* +2.31% / \x03 + 0.25% / FLAT: 0.00%。|pct_bp| ≦ STK_BP_MAX。符号は dir で決める(pct の符号は見ない) */
size_t stk_fmt_amt(char *buf, size_t size, int32_t diff_x100, uint8_t kind);      /* \x01 55 / \x02 18.31 / 0 は YEN: 0、INDEX: 0.00(矢印なし)。|diff| ≦ STK_X100_MAX。YEN は 0.5 円を絶対値で四捨五入 */
uint8_t stk_row_kind(uint8_t row);                                                /* 行 → STK_KIND_*(0 = 円、それ以外 = 指数) */

/* ---- 市場の状態(§5.4)。now_utc / mtime / rs / re はすべて UTC 秒。synced = 時計が同期済みか。
 * 未同期 → NONE。TRADING = rs ≦ now < re かつ mtime ≧ rs かつ now − mtime ≦ STK_FRESH_S(mtime が now より先でもよい)。それ以外は CLOSED。
 * 32bit の差で比べる(秒の値が 32bit で巻き戻ってもよい。区間 re − rs は 0 より大きく 2^31 未満) */
uint8_t stk_market(bool synced, uint32_t now_utc, uint32_t mtime, uint32_t rs, uint32_t re);

#endif /* STK_CORE_H */
