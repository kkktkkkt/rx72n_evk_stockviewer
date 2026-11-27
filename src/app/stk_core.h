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
#define STK_MINUTES_MAX (1439u)      /* clock_min / upd_min の有効な最大(23:59)。1440〜0xFFFE は範囲外 = 「なし」と同じ表示 */
#define STK_X100_MAX   (99999999)    /* 価格・前日の終値・前日比の絶対値の上限(×100。999,999.99)*/
#define STK_BP_MAX     (99999)       /* 騰落率の絶対値の上限(0.01% 単位。999.99%) */
#define STK_FRESH_S    (7200u)       /* データ(mtime)の鮮度の上限(秒)。取引時間帯の中でこれ以内なら「取引中」(§5.4。設計の WX_FRESH_S) */
#define STK_OPEN_GRACE_S (1800u)     /* 寄り付き(rs)から、今日の最初の足が届くまでの猶予(秒)。mtime < rs でも「取引中」(設計の WX_OPEN_GRACE_S) */
#define STK_LUNCH_S    (41400u)      /* 日本株の昼休み(JST の 0 時からの秒)の始まり 11:30 */
#define STK_LUNCH_E    (45000u)      /* 終わり 12:30 */

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

/* ---- 銘柄表(§4.2・§5.4) ---- */
typedef struct {
    uint8_t  kind;             /* STK_KIND_* */
    uint32_t lunch_s;          /* 昼休みの始まり(JST の 0 時からの秒)。lunch_s = lunch_e は昼休みなし */
    uint32_t lunch_e;
} stk_sym_t;

const stk_sym_t *stk_sym(uint8_t row);                       /* 行 0 = 6723.T、1 = ^N225(どちらも昼休み 11:30〜12:30)、2 = ^GSPC(昼休みなし)。範囲外は NULL */
uint8_t stk_row_kind(uint8_t row);                           /* 行 → STK_KIND_*(0 = 円、それ以外 = 指数) */

/* ---- 前日比・騰落率(§4.2) ---- */
typedef struct {
    int32_t diff_x100;         /* price - prev */
    int32_t pct_bp;            /* round_half_away(diff * 10000 / prev)。0.01% = 1 */
    uint8_t dir;               /* 丸めた pct_bp の符号: > 0 STK_UP、< 0 STK_DOWN、= 0 STK_FLAT(diff ≠ 0 でも 0.00% に丸まれば FLAT) */
} stk_calc_t;

/* price_x100・prev_x100 は 1〜STK_X100_MAX。外れる・|pct_bp| > STK_BP_MAX・out が NULL は false(out は 0・STK_FLAT)。int64 は割り算の 1 か所だけ */
bool stk_calc(int32_t price_x100, int32_t prev_x100, stk_calc_t *out);

/* ---- 整形(NUL 終端。戻り値 = 長さ。失敗(範囲外・size 不足・kind / dir が不正)は 0 で buf[0] = '\0'。buf が NULL か size = 0 なら何も書かず 0) ----
 * 記号は \x01 = ▲、\x02 = ▼、\x03 = −(U+2212)。ui_render のグリフ表がこの 3 つを読む(§3.6) */
size_t stk_fmt_price(char *buf, size_t size, int32_t price_x100, uint8_t kind);   /* YEN: 2,433(整数円。0.5 円は切り上げ)/ INDEX: 57,953.84。0〜STK_X100_MAX */
size_t stk_fmt_pct(char *buf, size_t size, int32_t pct_bp);                       /* +2.31% / \x03 + 0.25% / 0 は 0.00%(+0.00% −0.00% は出ない)。符号は pct_bp から。|pct_bp| ≦ STK_BP_MAX */
/* 丸めた金額 A(YEN: |diff| を 0.5 円で四捨五入した整数、INDEX: 小数 2 桁)。矢印 \x01 / \x02 は dir が UP / DOWN で A ≠ 0 のときだけ(符号 + − は付けない)。
 * A = 0 は YEN: 0、INDEX: 0.00。|diff| ≦ STK_X100_MAX、kind / dir は範囲内 */
size_t stk_fmt_amt(char *buf, size_t size, int32_t diff_x100, uint8_t kind, uint8_t dir);

/* ---- 市場の状態(§5.4)。now_jst = ソフト時計の JST の通算秒(now_utc = now_jst − 32,400)。mtime / rs / re は UTC 秒。
 * synced = 時計が同期済みか(設計の署名に無い引数。未同期 → NONE)。sym が NULL も NONE。
 * TRADING = 現在が取引時間帯(rs ≦ now_utc < re)で、昼休みでなく(JST の時刻が sym->lunch_s 以上 lunch_e 未満)、
 *           かつ(data_ok = mtime ≧ rs かつ now_utc − mtime ≦ STK_FRESH_S)または(open_grace = mtime < rs かつ now_utc − rs ≦ STK_OPEN_GRACE_S)。それ以外は CLOSED。
 * 32bit の差で比べる(秒が 32bit で巻き戻ってもよい。区間 re − rs は 0 より大きく 2^31 未満。mtime が now より先でもデータは新しいと見なす) */
uint8_t stk_market(const stk_sym_t *sym, bool synced, uint32_t now_jst, uint32_t mtime, uint32_t rs, uint32_t re);

#endif /* STK_CORE_H */
