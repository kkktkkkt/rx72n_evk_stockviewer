/* wx_core.h - 株価(Yahoo chart API)の取得と時刻同期の論理部(標準Cのみ。FIT・platform.h を include しない。ホストで試験する)
 * 仕様: docs/design.md §4・§5・§7.5。wx_core.c は APP_WIFI_ON = 0 の時は中身が消える(wifi_sw.h)。
 * 応答の本文は、ここから外へ出さない(出すのは数値と名前だけ)。 */
#ifndef WX_CORE_H
#define WX_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "wifi_sw.h"
#include "stk_core.h"

#define WX_BUF_SIZE      (3072u)     /* 応答バッファ。Yahoo の応答 最大 約 1,960 バイトの 1.57 倍(design.md §5.8) */
#define WX_HOST          "query1.finance.yahoo.com"
#define WX_RANGE_STR     "2d"        /* Yahoo の chart API の range(design.md §5.3) */
#define WX_UA            "rx72n-stockview/1"
#define WX_NSYM          (STK_ROWS)  /* 銘柄の数 3(0 = 6723.T、1 = ^N225、2 = ^GSPC) */
#define WX_SYM_ALL       (7u)        /* 3 銘柄のビットマスク */
#define WX_CLOCK_BACK_MAX_S (30u)    /* 同期済みの時計より 30 秒以内の遅れの Date: は採用しない(CDN のキャッシュ応答。design.md §5.9) */
#define WX_SYNC_BIAS_MS  (700u)      /* 最初のデータを受けた ms から引いて Date: の秒の頭とする。実機で PC と比べて調整する 1 定数 */
#define WX_INTERVAL_MS   (600000u)   /* 定期取得(前回の周期の開始から) */
#define WX_MIN_GAP_MS    (5000u)     /* 更新要求を受け付ける最小の間隔(周期の開始から) */
#define WX_RETRY_MS      (20000u)    /* 失敗した銘柄だけのやり直しまで */
#define WX_RETRY_MAX     (2u)        /* やり直しの最大回数(1 周期あたり) */
#define WX_RATE_MS       (300000u)   /* 429 の後 */
#define WX_BOOT_WAIT_MS  (3000u)
#define WX_STALE_S       (STK_STALE_S)       /* 「古い」(1,800。値は stk_core.h) */
#define WX_FRESH_S       (STK_FRESH_S)       /* 取引中のデータの鮮度の上限(7,200。design.md §5.4) */
#define WX_OPEN_GRACE_S  (STK_OPEN_GRACE_S)  /* 寄り付き後の猶予(1,800) */

/* 取得のきっかけ(ログの src=)。RETRY = 失敗した銘柄のやり直し */
typedef enum { WX_SRC_BOOT = 0, WX_SRC_TIMER, WX_SRC_TOUCH, WX_SRC_UART, WX_SRC_RETRY, WX_SRC_END } wx_src_t;

typedef enum { WX_CUR_NONE = 0, WX_CUR_JPY, WX_CUR_USD, WX_CUR_OTHER } wx_cur_t;

/* 取得の結果。err: 0 = なし、1 TCP/SSL 接続 / 2 送信 / 3 応答なし・途切れ / 4 HTTP の状態が 200 でない / 5 キー・値が無い・読めない / 6 範囲外 / 7 バッファ超過 / 8 join 失敗 / 9 ESP32 が再起動(取得中に ready) */
typedef struct {
    uint8_t  ok;             /* 1 = 取れた(err = 0) */
    uint8_t  err;
    uint8_t  have_date;      /* 1 = Date: が読めた(jst_s が有効。失敗した取得でも 1 になりうる) */
    uint8_t  closed;         /* 1 = CLOSED を受けて終わった */
    uint16_t http;           /* 状態行の数字。読めなければ 0 */
    uint16_t len;            /* バッファに溜めた応答のバイト数(wifi_core が入れる。WX_BUF_SIZE で頭打ち。本文は持たない) */
    uint32_t jst_s;          /* Date: + 9 時間。1970-01-01 00:00:00 からの通算秒(JST の壁時計) */
    uint32_t ms;             /* 取得の開始から結果まで(wifi_core が入れる) */
    uint32_t first_ms;       /* 最初のデータ(+IPD の最初のバイト)を受けた時刻(wifi_core が入れる。時刻同期用) */
    /* ---- 株価(wx_parse_http が入れる。design.md §5.2・§5.9)---- */
    uint8_t  sym;            /* 銘柄 0〜2(wifi_core が入れる。解析は触らない) */
    uint8_t  has_bar_prev;   /* 1 = close 配列の「直前の足」から prev_x100 を決めた(0 = meta.chartPreviousClose) */
    uint8_t  cur;            /* wx_cur_t(currency)。キーが無くても失敗にしない */
    int32_t  price_x100;     /* regularMarketPrice(×100。1〜99,999,999) */
    int32_t  prev_x100;      /* 前日の終値(決定後。×100。1〜99,999,999) */
    uint32_t mtime;          /* regularMarketTime(UTC 秒) */
    uint32_t rs;             /* currentTradingPeriod.regular.start(UTC 秒) */
    uint32_t re;             /* 同 end */
} wx_result_t;

typedef struct { uint16_t year; uint8_t month, day, wday, hour, min, sec; } wx_date_t;   /* wday: 0 = 日曜 */

/* ---- 接続先(design.md §5.5.2・§5.9)----
 * 製品は YAHOO_SSL(0)だけ。専用モード(APP_WIFI_TEST)は t y g z の試験用に YAHOO_TCP(t)/ GOOGLE_SSL(g)/ YAHOO_SSL_NOAUTH(z = y の前に AT+CIPSSLCCONF=0,0,0)が増える */
typedef enum {
    WX_TGT_YAHOO_SSL = 0,
#if APP_WIFI_TEST
    WX_TGT_YAHOO_TCP,
    WX_TGT_GOOGLE_SSL,
    WX_TGT_YAHOO_SSL_NOAUTH,
#endif
    WX_TGT_END
} wx_target_t;

/* ---- 要求 ---- */
/* AT+CIPSTART="<TCP|SSL>","<host>",<port>\r\n を buf へ(NUL 終端)。戻り値 = 長さ。範囲外の t・size 不足は 0 で buf[0] = '\0' */
size_t wx_build_cipstart(char *buf, size_t size, wx_target_t t);
bool   wx_target_ssl(wx_target_t t);              /* SSL の接続先か(範囲外は false) */

/* 製品の要求(HTTP/1.0。Yahoo の SSL。design.md §5.1)を buf へ(NUL 終端)。戻り値 = 長さ(sym 0 / 1 / 2 = 144 / 145 / 145)。
 * size 不足・sym が 0〜2 以外・buf が NULL は 0 で(buf が使えれば)buf[0] = '\0' */
size_t wx_build_request(char *buf, size_t size, uint8_t sym);
/* 接続先つき(専用モードの t y g z と wifi_core が使う)。YAHOO_* は上と同じ要求。GOOGLE_SSL は GET /(sym は無視)。t が範囲外は 0 */
size_t wx_build_request_t(char *buf, size_t size, wx_target_t t, uint8_t sym);

/* ---- 応答の解析 ----
 * buf は NUL 終端でなくてよい(len で止まる)。失敗しても http / have_date / jst_s は分かった範囲で入る。
 * out の ms / first_ms / closed / len / sym は触らない。数値だけを取り出す(本文は out に残さない) */

/* 株価(Yahoo chart API の range=2d。design.md §5.2)。成功 = 状態 200 かつ meta の regularMarketPrice・chartPreviousClose・regularMarketTime・
 * currentTradingPeriod.regular の start / end が読め、前日の終値(close 配列の直前の足があればそれ、無ければ chartPreviousClose)と価格が
 * 範囲内(stk_calc が通る)。err: 3 = 状態行まで届かない・途切れ / 4 = 状態が 200 でない(429 を含む)/ 5 = キー・値が無い・読めない / 6 = 範囲外 */
bool wx_parse_http(const char *buf, size_t len, wx_result_t *out);

/* b[0..n) が、最初の { か [ から始まる 1 つの JSON 値として閉じているか(入れ子の深さが 0 に戻り、その後が空白だけ。文字列の中とエスケープは数えない。
 * 設計 §5.2 段 3b。wx_parse_http が使う。ホスト試験のために公開) */
bool stk_json_complete(const char *b, size_t n);

/* ---- 時刻 ---- */
/* s = Date: の値(" Wed, 25 Nov 2026 04:24:13 GMT" の形。先頭の空白は可)。utc_s = 1970-01-01 からの通算秒。範囲外・壊れた形は false */
bool wx_parse_date(const char *s, size_t len, uint32_t *utc_s);
void wx_date_fields(uint32_t secs, wx_date_t *out);            /* 通算秒 → 年月日・曜日・時分秒 */

typedef struct { uint32_t base_s; uint32_t base_ms; uint8_t synced; } wx_clock_t;
void wx_clock_init(wx_clock_t *k);
void wx_clock_sync(wx_clock_t *k, uint32_t jst_s, uint32_t first_ms);   /* base_ms = first_ms - WX_SYNC_BIAS_MS。同期済みで 1〜WX_CLOCK_BACK_MAX_S 秒の遅れは採用しない */
bool wx_clock_now(const wx_clock_t *k, uint32_t now_ms, uint32_t *jst_s);   /* 未同期なら false。now_ms は 32bit で巻き戻ってよい(差で使う。再同期の間隔 49 日未満) */
uint16_t wx_clock_min(const wx_clock_t *k, uint32_t now_ms);   /* 時 * 60 + 分。未同期は 0xFFFF */

/* ---- ログ行(\r\n 込み。戻り値 = 書いたバイト数。失敗は 0 で buf[0] = '\0')。書式は design.md §7.5 の [stk] ----
 * 本文・SSID・パスワードを引数に取らない */
typedef enum { WX_REQ_OK = 0, WX_REQ_IGNORED, WX_REQ_BUSY, WX_REQ_OFF } wx_req_t;
typedef enum { WX_NET_OFF = 0, WX_NET_CONN, WX_NET_ON, WX_NET_ERR } wx_net_t;    /* = STK_NET_* */

/* [stk] res=ok sym=<n> src=<s> p100=<n> c100=<n> bp=<n> mkt=<t|c|n> http=<n> ms=<n> sync=<0|1> jst=<hhmmss|none> len=<n> closed=<0|1>
 * r は ok = 1 の結果(bp は stk_calc で r の価格・前日の終値から計算。計算できない・sym ≧ 3・mkt が不正は 0) */
size_t wx_fmt_ok(char *buf, size_t size, wx_src_t src, const wx_result_t *r, uint8_t mkt);
/* [stk] res=fail sym=<n> src=<s> err=<1..9> http=<n> ms=<n> len=<n>(r->err が 1〜9、sym が 0〜2) */
size_t wx_fmt_fail(char *buf, size_t size, wx_src_t src, const wx_result_t *r);
/* [stk] cycle=<n> ok=<mask> fail=<mask> ms=<n> */
size_t wx_fmt_cycle(char *buf, size_t size, uint32_t n, uint8_t ok_mask, uint8_t fail_mask, uint32_t ms);
/* [stk] req=<ignored|busy|off> src=<touch|uart>(req = WX_REQ_OK は 0。src は touch / uart だけ) */
size_t wx_fmt_req(char *buf, size_t size, wx_req_t req, wx_src_t src);

/* [stk] net=<off|conn|on|err> busy=<0|1> have=<mask> stale=<mask> clk=<hhmmss|none> upd=<hhmm|none> */
typedef struct {
    uint8_t  net;            /* wx_net_t */
    uint8_t  busy;
    uint8_t  have_mask;      /* 銘柄のビット(0〜7) */
    uint8_t  stale_mask;
    uint8_t  synced;         /* 1 の時 jst_s が有効(ソフト時計) */
    uint16_t upd_min;        /* 0〜1439。範囲外(0xFFFF を含む)は none */
    uint32_t jst_s;
} wx_netlog_t;
size_t wx_fmt_net(char *buf, size_t size, const wx_netlog_t *v);

/* [stk] sym=<n> have=<0|1> p100=<n> d100=<n> bp=<n> dir=<u|d|f> mkt=<t|c|n> age=<-1|n>(have = 0 の時は値を 0・dir = f・mkt = n・age = -1 にそろえる) */
typedef struct {
    uint8_t  sym;
    uint8_t  have;
    uint8_t  dir;            /* STK_UP / STK_DOWN / STK_FLAT */
    uint8_t  mkt;            /* STK_MKT_* */
    int32_t  p100;
    int32_t  d100;
    int32_t  bp;
    int32_t  age_s;          /* 最後の成功からの秒。未取得は -1 */
} wx_symlog_t;
size_t wx_fmt_sym(char *buf, size_t size, const wx_symlog_t *v);

/* ---- 取得の予定表(ブロックしない状態機械。wifi_core を呼ばない。呼び出し側が wx_act_t を実行して done を返す。design.md §5.7)----
 * 使い方: 毎周回 act = wx_sched_poll(s, now)。CONNECT なら wifi_core の WIFI_RANGE_CONNECT、FETCH なら wx_sched_sym(s) の銘柄で WIFI_RANGE_WX を 1 回始め、
 * 終わったら wx_sched_done(s, act, ok, http, now)(銘柄ごとに 1 回)。更新要求(タッチ・UART x)は wx_sched_request。
 * 1 周期 = 3 銘柄を順に。失敗した銘柄だけを 20 秒後にやり直す(最大 2 回)。429 は周期を打ち切り、300 秒後に周期をやり直す。 */
typedef enum { WX_ACT_NONE = 0, WX_ACT_CONNECT, WX_ACT_FETCH } wx_act_t;
typedef enum { WX_S_OFF = 0, WX_S_BOOT_WAIT, WX_S_CONNECT, WX_S_FETCH, WX_S_IDLE, WX_S_RETRY } wx_sstate_t;

typedef struct {             /* 終わった周期の記録(wx_sched_cycle)。n = 0 は、まだ終わった周期がない */
    uint32_t n;
    uint32_t ms;             /* 周期の開始から終わりまで(やり直しの待ちを含む) */
    uint8_t  ok;             /* 周期の中で成功した銘柄のビット */
    uint8_t  fail;           /* 終わった時に失敗が残っている銘柄のビット */
} wx_cycle_t;

typedef struct {
    uint8_t  state;
    uint8_t  net;            /* wx_net_t */
    uint8_t  started;        /* poll が act を返し、done を待っている */
    uint8_t  want;           /* RETRY の後の動き(wx_act_t) */
    uint8_t  src;            /* 今の(または次の)取得の wx_src_t */
    uint8_t  sym;            /* 今の(または次の)銘柄 0〜2 */
    uint8_t  todo;           /* 今のパスでまだ取っていない銘柄のビット */
    uint8_t  failed;         /* 失敗が残っている銘柄のビット */
    uint8_t  okmask;         /* 今の周期で成功した銘柄のビット */
    uint8_t  nomask;         /* 今の周期で、応答なし(http = 0)で失敗した銘柄のビット */
    uint8_t  retry_n;        /* 今の周期のやり直しの回数(0〜WX_RETRY_MAX) */
    uint8_t  new_cycle;      /* 次の FETCH で周期を始める */
    uint8_t  rate;           /* RETRY が 429 の待ち */
    uint8_t  conn_fail;      /* 連続の接続失敗 */
    uint8_t  have;           /* 一度でも成功した */
    uint32_t t_due;          /* BOOT_WAIT / RETRY の期限 */
    uint32_t t_fetch0;       /* 今の(最後の)周期の開始 */
    wx_cycle_t cyc;
} wx_sched_t;

void       wx_sched_init(wx_sched_t *s, bool enabled, uint32_t now_ms);   /* enabled = false なら OFF(何もしない) */
wx_act_t   wx_sched_poll(wx_sched_t *s, uint32_t now_ms);
void       wx_sched_done(wx_sched_t *s, wx_act_t act, bool ok, uint16_t http, uint32_t now_ms);
wx_req_t   wx_sched_request(wx_sched_t *s, wx_src_t src, uint32_t now_ms);
wx_src_t   wx_sched_src(const wx_sched_t *s);
wx_net_t   wx_sched_net(const wx_sched_t *s);
bool       wx_sched_busy(const wx_sched_t *s);
uint8_t    wx_sched_sym(const wx_sched_t *s);               /* 今の(poll が WX_ACT_FETCH を返した)銘柄 */
uint8_t    wx_sched_failed_mask(const wx_sched_t *s);       /* 失敗が残っている銘柄のビット(partial の判定用) */
const wx_cycle_t *wx_sched_cycle(const wx_sched_t *s);      /* 最後に終わった周期 */

#endif /* WX_CORE_H */
