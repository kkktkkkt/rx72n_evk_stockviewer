/* wx_core.h - 天気・気温の取得と時刻同期の論理部(標準Cのみ。FIT・platform.h を include しない。ホストで試験する)
 * 仕様: docs/design.md §22.2〜§22.7、docs/design-uart.md §13。wx_core.c は APP_WIFI_ON = 0 の時は中身が消える(wifi_sw.h)。
 * 応答の本文は、ここから外へ出さない(出すのは数値と名前だけ)。 */
#ifndef WX_CORE_H
#define WX_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define WX_BUF_SIZE      (3072u)     /* 応答バッファ。Yahoo の応答 最大 約 1,960 バイトの 1.57 倍(design.md §5.8)。天気は 629 バイト */
#define WX_REQ_LEN       (190u)      /* 天気(WX_TGT_METEO)の要求の長さ(固定。実測と同じ) */
#define WX_RANGE_STR     "2d"        /* Yahoo の chart API の range(design.md §5.3) */
#define WX_UA            "rx72n-stockview/1"
#define WX_SYNC_BIAS_MS  (700u)      /* 最初のデータを受けた ms から引いて Date: の秒の頭とする。実機で PC と比べて調整する 1 定数 */
#define WX_INTERVAL_MS   (600000u)   /* 定期取得(前回の取得開始から) */
#define WX_MIN_GAP_MS    (5000u)     /* 更新要求を受け付ける最小の間隔(取得開始から) */
#define WX_RETRY_MS      (20000u)    /* 失敗後のやり直し */
#define WX_RATE_MS       (300000u)   /* 429 の後 */
#define WX_BOOT_WAIT_MS  (3000u)
#define WX_STALE_S       (1800u)

/* 天気アイコンの種類(WMO コードの分類。design.md §22.5) */
typedef enum { WX_KIND_SUN = 0, WX_KIND_PARTLY, WX_KIND_CLOUD, WX_KIND_FOG, WX_KIND_RAIN, WX_KIND_SNOW, WX_KIND_THUNDER, WX_KIND_UNKNOWN, WX_KIND_END } wx_kind_t;
typedef enum { WX_SRC_BOOT = 0, WX_SRC_TIMER, WX_SRC_TOUCH, WX_SRC_UART, WX_SRC_END } wx_src_t;

/* 取得の結果。err: 0 = なし、1 TCP/SSL 接続 / 2 送信 / 3 応答なし・途切れ / 4 HTTP の状態が 200 でない / 5 キー・値が無い・読めない / 6 範囲外 / 7 バッファ超過 / 8 join 失敗 / 9 ESP32 が再起動(取得中に ready) */
typedef struct {
    uint8_t  ok;             /* 1 = 気温・天気が取れた(err = 0) */
    uint8_t  err;
    uint8_t  have_date;      /* 1 = Date: が読めた(jst_s が有効。天気が失敗でも 1 になりうる) */
    uint8_t  closed;         /* 1 = CLOSED を受けて終わった */
    uint8_t  wmo;
    uint8_t  kind;           /* wx_kind_t */
    int16_t  temp_x10;       /* 0.1℃。-600〜600 */
    uint16_t http;           /* 状態行の数字。読めなければ 0 */
    uint16_t len;            /* バッファに溜めた応答のバイト数(wifi_core が入れる。WX_BUF_SIZE で頭打ち。本文は持たない) */
    uint16_t obs;            /* current.time の HHMM(JST。照合用)。無ければ 0 */
    uint32_t jst_s;          /* Date: + 9 時間。1970-01-01 00:00:00 からの通算秒(JST の壁時計) */
    uint32_t ms;             /* 取得の開始から結果まで(wifi_core が入れる) */
    uint32_t first_ms;       /* 最初のデータ(+IPD の最初のバイト)を受けた時刻(wifi_core が入れる。時刻同期用) */
} wx_result_t;

typedef struct { uint16_t year; uint8_t month, day, wday, hour, min, sec; } wx_date_t;   /* wday: 0 = 日曜 */

/* ---- 接続先(Phase 0 の TLS 検証。design.md §5.5.2。専用モード APP_WIFI_TEST の t y g z x が選ぶ。Phase 3 で Yahoo の SSL だけにする)----
 * METEO = 天気(今までどおり。既定)/ YAHOO_TCP = t(平文 80)/ YAHOO_SSL = y / GOOGLE_SSL = g / YAHOO_SSL_NOAUTH = z(y の前に AT+CIPSSLCCONF=0,0,0) */
typedef enum { WX_TGT_METEO = 0, WX_TGT_YAHOO_TCP, WX_TGT_YAHOO_SSL, WX_TGT_GOOGLE_SSL, WX_TGT_YAHOO_SSL_NOAUTH, WX_TGT_END } wx_target_t;

/* ---- 要求 ---- */
/* AT+CIPSTART="<TCP|SSL>","<host>",<port>\r\n を buf へ(NUL 終端)。戻り値 = 長さ。範囲外の t・size 不足は 0 で buf[0] = '\0' */
size_t wx_build_cipstart(char *buf, size_t size, wx_target_t t);
bool   wx_target_ssl(wx_target_t t);              /* SSL の接続先か(範囲外は false) */

/* 固定の要求(HTTP/1.0)を buf へ(NUL 終端)。戻り値 = 長さ。size 不足・t が範囲外・(Yahoo で)sym が 0〜2 以外は 0 で buf[0] = '\0'。
 * METEO は天気の固定の要求(WX_REQ_LEN。sym は無視)。YAHOO_* は design.md §5.1(sym = 0: 6723.T / 1: ^N225 / 2: ^GSPC)。GOOGLE_SSL は GET / */
size_t wx_build_request(char *buf, size_t size, wx_target_t t, uint8_t sym);

/* ---- 応答の解析 ----
 * buf は NUL 終端でなくてよい(len で止まる)。成功 = 状態 200 かつ気温と天気コードが範囲内で読めた。
 * 失敗しても http / have_date / jst_s は分かった範囲で入る。out の ms / first_ms / closed は触らない */
bool wx_parse_http(const char *buf, size_t len, wx_result_t *out);
const char *wx_kind_name(uint8_t kind);          /* "sun" ... "unknown"。範囲外は NULL */
uint8_t wx_kind_of(uint32_t wmo);                /* WMO コード → wx_kind_t(範囲外は UNKNOWN) */

/* ---- 時刻 ---- */
/* s = Date: の値(" Wed, 25 Nov 2026 04:24:13 GMT" の形。先頭の空白は可)。utc_s = 1970-01-01 からの通算秒。範囲外・壊れた形は false */
bool wx_parse_date(const char *s, size_t len, uint32_t *utc_s);
void wx_date_fields(uint32_t secs, wx_date_t *out);            /* 通算秒 → 年月日・曜日・時分秒 */

typedef struct { uint32_t base_s; uint32_t base_ms; uint8_t synced; } wx_clock_t;
void wx_clock_init(wx_clock_t *k);
void wx_clock_sync(wx_clock_t *k, uint32_t jst_s, uint32_t first_ms);   /* base_ms = first_ms - WX_SYNC_BIAS_MS */
bool wx_clock_now(const wx_clock_t *k, uint32_t now_ms, uint32_t *jst_s);   /* 未同期なら false。now_ms は 32bit で巻き戻ってよい(差で使う。再同期の間隔 49 日未満) */
uint16_t wx_clock_min(const wx_clock_t *k, uint32_t now_ms);   /* 時 * 60 + 分。未同期は 0xFFFF */

/* ---- ログ行(\r\n 込み。戻り値 = 書いたバイト数。失敗は 0 で buf[0] = '\0')。書式は design-uart.md §13.1 ----
 * 本文・SSID・パスワードを引数に取らない */
size_t wx_fmt_ok(char *buf, size_t size, wx_src_t src, const wx_result_t *r);
size_t wx_fmt_fail(char *buf, size_t size, wx_src_t src, const wx_result_t *r);

typedef struct {
    uint8_t  net;            /* wx_net_t */
    uint8_t  busy;
    uint8_t  have;           /* 一度でも取得できた */
    uint8_t  kind;
    int16_t  temp_x10;
    int32_t  age_s;          /* 最後の成功からの秒。未取得は -1 */
    uint8_t  synced;
    uint32_t jst_s;          /* synced の時のソフト時計 */
} wx_view_t;

typedef enum { WX_NET_OFF = 0, WX_NET_CONN, WX_NET_ON, WX_NET_ERR } wx_net_t;
typedef enum { WX_REQ_OK = 0, WX_REQ_IGNORED, WX_REQ_BUSY, WX_REQ_OFF } wx_req_t;
size_t wx_fmt_req(char *buf, size_t size, wx_req_t req, wx_src_t src);       /* req = WX_REQ_OK は 0。src は touch / uart だけ */
size_t wx_fmt_state(char *buf, size_t size, const wx_view_t *v);

/* ---- 取得の予定表(ブロックしない状態機械。wifi_core を呼ばない。呼び出し側が wx_act_t を実行して done を返す。design.md §22.7) ----
 * 使い方: 毎周回 act = wx_sched_poll(s, now)。CONNECT なら wifi_core の WIFI_RANGE_CONNECT、FETCH なら WIFI_RANGE_WX を始め、
 * 終わったら wx_sched_done(s, act, ok, http, now)。更新要求(ボタン・UART x)は wx_sched_request */
typedef enum { WX_ACT_NONE = 0, WX_ACT_CONNECT, WX_ACT_FETCH } wx_act_t;
typedef enum { WX_S_OFF = 0, WX_S_BOOT_WAIT, WX_S_CONNECT, WX_S_FETCH, WX_S_IDLE, WX_S_RETRY } wx_sstate_t;

typedef struct {
    uint8_t  state;
    uint8_t  net;            /* wx_net_t */
    uint8_t  started;        /* poll が act を返し、done を待っている */
    uint8_t  want;           /* RETRY の後の動き(wx_act_t) */
    uint8_t  src;            /* 今の(または次の)取得の wx_src_t */
    uint8_t  fetch_fail;     /* 連続の取得失敗(0〜2) */
    uint8_t  conn_fail;      /* 連続の接続失敗 */
    uint8_t  have;           /* 一度でも成功した */
    uint32_t t_due;          /* BOOT_WAIT / RETRY の期限 */
    uint32_t t_fetch0;       /* 最後の取得開始 */
} wx_sched_t;

void       wx_sched_init(wx_sched_t *s, bool enabled, uint32_t now_ms);   /* enabled = false なら OFF(何もしない) */
wx_act_t   wx_sched_poll(wx_sched_t *s, uint32_t now_ms);
void       wx_sched_done(wx_sched_t *s, wx_act_t act, bool ok, uint16_t http, uint32_t now_ms);
wx_req_t   wx_sched_request(wx_sched_t *s, wx_src_t src, uint32_t now_ms);
wx_src_t   wx_sched_src(const wx_sched_t *s);
wx_net_t   wx_sched_net(const wx_sched_t *s);
bool       wx_sched_busy(const wx_sched_t *s);

#endif /* WX_CORE_H */
