/* clk_core.h - 時計・天気画面の表示値と整形(標準Cのみ。FIT を include しない。ホストで試験する。docs/design.md §13.3・§13.4・§13.8)
 * 6c の wx_core.h がこの型(clk_slot_t・CLK_* の種類)を include して使う。 */
#ifndef CLK_CORE_H
#define CLK_CORE_H

#include <stdint.h>

/* 天気の種類の番号(6b・6c・6d の約束。生成物の clk_pal・clk_icon の添字、[wx] の kind= の名前、ui_pal_id はすべてこの番号) */
enum { CLK_SUN = 0, CLK_PARTLY = 1, CLK_CLOUD = 2, CLK_FOG = 3, CLK_RAIN = 4, CLK_SNOW = 5, CLK_THUNDER = 6, CLK_UNKNOWN = 7 };
#define CLK_KINDS        (8)           /* clk_assets.h(生成物)の定義と同じ字面 */
#define CLK_CLOCK_NONE   (0xFFFFu)     /* clock_min の「未同期」(stk_core.h の STK_NONE16 と同じ値) */
#define CLK_MINUTES_MAX  (1439u)       /* clock_min の有効な最大(23:59。stk_core.h の STK_MINUTES_MAX と同じ値) */
#define CLK_TEMP_MAX     (99)          /* 画面に出す気温の整数の範囲 −99〜99 */

/* 天気が「新しい」と見なす最後の成功からの秒数(これ以上は「不明」。§13.5)。試験では -D で縮められる */
#ifndef CLK_WX_STALE_S
#define CLK_WX_STALE_S   (3600u)
#endif

#define CLK_SEQ_TIME_MAX (5u)          /* HH:MM */
#define CLK_SEQ_DATE_MAX (13u)         /* 12月30日(水)　東京都 */
#define CLK_SEQ_TEMP_MAX (4u)          /* −99° */

/* 画面の表示値。memcmp で比べるのでパディングなし(8 バイト) */
typedef struct {
    uint16_t clock_min;                /* 0..1439。CLK_CLOCK_NONE = 未同期。1440..0xFFFE も --:-- */
    uint8_t  kind;                     /* CLK_*。have = 0 のとき CLK_UNKNOWN */
    uint8_t  have;                     /* 1 = 気温と天気がある(新しい) */
    int8_t   temp;                     /* 整数の気温 −99..99。have = 0 のとき 0 */
    uint8_t  mon;                      /* 1..12。0 = 日付なし */
    uint8_t  day;                      /* 1..31 */
    uint8_t  wday;                     /* 0..6(0 = 日曜) */
} clk_view_t;

/* 取得側(wifi_at.c。6d)が保持する、天気の最後の成功 */
typedef struct {
    uint8_t  have;
    uint8_t  kind;
    int16_t  temp_x10;                 /* 0.1℃ */
    uint32_t ok_ms;                    /* 最後の成功の時刻(単調増加の ms。32bit の巻き戻りは差で扱う) */
} clk_slot_t;

typedef struct { uint8_t mon, day, wday; } clk_date_t;

/* WMO の天気コード → CLK_*(§13.6 の表。0..99 以外・表に無いコードは CLK_UNKNOWN) */
uint8_t clk_kind_of(uint32_t wmo);

/* "sun" "partly" "cloud" "fog" "rain" "snow" "thunder" "unknown"。範囲外は NULL */
const char *clk_kind_name(uint8_t kind);

/* 0.1℃ → 整数。絶対値の 0.5 を遠い方へ丸め(|t10| + 5) / 10、結果 0 なら符号なし、±99 に切る */
int8_t clk_temp_int(int16_t t10);

/* 表示値を作る。slot が NULL か have = 0 か、最後の成功から CLK_WX_STALE_S 秒以上なら kind = CLK_UNKNOWN・temp = 0・have = 0。
 * clock_min が 1440..0xFFFE なら 0xFFFF にそろえる。date が NULL か範囲外(mon 1..12・day 1..31・wday 0..6 以外)なら mon = day = wday = 0。v が NULL なら何もしない。v は全部書く */
void clk_view_make(clk_view_t *v, const clk_slot_t *slot, uint32_t now_ms, uint16_t clock_min, const clk_date_t *date);

/* 画面が使う天気の種類(have = 0 や範囲外は CLK_UNKNOWN)。パレット番号 pal_id = 1 + この値 */
static inline uint8_t clk_view_kind(const clk_view_t *v)
{
    return ((v->have != 0u) && (v->kind < (uint8_t)CLK_KINDS)) ? v->kind : (uint8_t)CLK_UNKNOWN;
}

/* 字番号の並び(clk_assets.h の F124・F72・F20 の字番号)を作る。戻り値 = 字数。表示値が範囲外でも out の範囲を超えて書かない */
uint8_t clk_seq_time(uint8_t out[CLK_SEQ_TIME_MAX], const clk_view_t *v);   /* F124: HH:MM(0 詰め)。未同期・範囲外は --:-- */
uint8_t clk_seq_date(uint8_t out[CLK_SEQ_DATE_MAX], const clk_view_t *v);   /* F20: M月D日(曜)　東京都。日付なしは 東京都 だけ */
uint8_t clk_seq_temp(uint8_t out[CLK_SEQ_TEMP_MAX], const clk_view_t *v);   /* F72: 18° −2° 0° --° */

#endif /* CLK_CORE_H */
