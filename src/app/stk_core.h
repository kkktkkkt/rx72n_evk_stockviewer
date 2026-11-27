/* stk_core.h - 株価ビューの表示用データ(標準Cのみ。docs/design.md §4.4)
 * 今は型と定数だけ(Phase 1)。stk_calc / stk_fmt_* / stk_market は Phase 2 でここに足す。 */
#ifndef STK_CORE_H
#define STK_CORE_H

#include <stdint.h>

/* dir = 色・帯の添字(設計 §3.2)。上昇 = 0(赤)、下落 = 1(緑)、変化なし・値なし = 2(灰) */
enum { STK_UP = 0, STK_DOWN = 1, STK_FLAT = 2 };
enum { STK_NET_OFF = 0, STK_NET_CONN, STK_NET_ON, STK_NET_ERR };
enum { STK_MKT_NONE = 0, STK_MKT_TRADING, STK_MKT_CLOSED };

#define STK_ROWS    (3u)       /* 0 = 6723.T(円)、1 = ^N225、2 = ^GSPC */
#define STK_NONE16  (0xFFFFu)  /* clock_min / upd_min の「なし」 */

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

#endif /* STK_CORE_H */
