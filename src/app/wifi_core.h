/* wifi_core.h - Wi-Fi(ESP32 AT)試作の論理部(標準Cのみ。FIT・platform.h を include しない。ホストで試験する)
 * 仕様: docs/design.md §21.4(Wi-Fi)・§5(株価の取得)、docs/design-uart.md §12。wifi_core.c は APP_WIFI_ON(APP_WIFI_TEST か APP_WX)= 0 の時は中身が消える。
 *
 * 使い方(wifi_at.c から):
 *   static wifi_core_t g;
 *   wifi_core_init(&g, &ops, WIFI_SSID, WIFI_PASS);   // 秘密が無ければ NULL, NULL(join 以降を skip)
 *   wifi_core_start(&g, WIFI_RANGE_ALL, ms);          // d / n / p(取得は wifi_core_set_symbol の後に WIFI_RANGE_WX。1 回 = 1 銘柄。結果は wifi_core_wx_seq / wifi_core_wx)
 *   毎周回: 受信バイトごとに wifi_core_rx(&g, b, ms)、続けて wifi_core_poll(&g, ms)
 *   ops.en / ops.send / ops.log は、状態機械が必要な時に呼ぶ(下)。ms = 単調増加の ms(10ms tick x 10 でよい。32bit 一周に対応)。 */
#ifndef WIFI_CORE_H
#define WIFI_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "wx_core.h"             /* wx_result_t(取得の結果)。wx_core も標準 C */

#define WIFI_LINE_MAX   (160u)   /* 受信1行の最大バイト数。超えた行は捨てる */
#define WIFI_VAL_MAX    (40u)    /* [wifiv] の val の最大文字数 */
#define WIFI_CMD_MAX    (224u)   /* 送信コマンド(\r\n 込み)の最大。AT+CWJAP の最長は 204 + 2 */
#define WIFI_LOG_MAX    (96u)    /* [wifi] / [wifiv] 1行の最大(最長 約 80) */

typedef enum {                   /* 値はそのまま [hb] の scr(0 = 待機) */
    WIFI_STEP_NONE = 0,
    WIFI_STEP_RESET,
    WIFI_STEP_BOOT,
    WIFI_STEP_AT,
    WIFI_STEP_ECHO,
    WIFI_STEP_GMR,
    WIFI_STEP_MODE,
    WIFI_STEP_QAP,               /* AT+CWQAP(前回の接続を切る) */
    WIFI_STEP_LAPOPT,            /* AT+CWLAPOPT(CWLAP の項目をそろえる) */
    WIFI_STEP_SCAN,              /* AT+CWLAP(自分の SSID が見えるかだけを集計。診断) */
    WIFI_STEP_JOIN,
    WIFI_STEP_IP,
    WIFI_STEP_DNS,
    WIFI_STEP_PING_IP,
    WIFI_STEP_PING_DNS,
    WIFI_STEP_WX_MUX,            /* 15: AT+CIPMUX=0(株価の取得。docs/design.md §5.7) */
    WIFI_STEP_WX_CLOSE,          /* 16: AT+CIPCLOSE(前回の接続の掃除。ERROR でも成功) */
    WIFI_STEP_WX_SSLCONF,        /* 17: AT+CIPSSLCCONF=0,0,0(認証なし。専用モード APP_WIFI_TEST の z = WX_TGT_YAHOO_SSL_NOAUTH の時だけ。製品の範囲には入れない。ERROR でも先へ。番号は変えない) */
    WIFI_STEP_WX_TCP,            /* 18: AT+CIPSTART="TCP"|"SSL",... → CONNECT と OK(接続先は wifi_core_set_target) */
    WIFI_STEP_WX_SEND,           /* 19: AT+CIPSEND=<n> → OK と > → 要求 → SEND OK */
    WIFI_STEP_WX_RECV,           /* 20: +IPD の連続 → CLOSED。解析して成功・失敗 */
    WIFI_STEP_END                /* 個数(21)。名前の表の大きさ。[hb] の scr は 0〜20 */
} wifi_step_t;

typedef enum { WIFI_RES_START = 0, WIFI_RES_OK, WIFI_RES_FAIL, WIFI_RES_TIMEOUT, WIFI_RES_SKIP, WIFI_RES_END } wifi_res_t;
typedef enum { WIFI_KEY_VER = 0, WIFI_KEY_SDK, WIFI_KEY_IP, WIFI_KEY_GW, WIFI_KEY_MASK, WIFI_KEY_DNS, WIFI_KEY_PING,
               WIFI_KEY_SCAN, WIFI_KEY_AP, WIFI_KEY_OTH, WIFI_KEY_END } wifi_key_t;
typedef enum { WIFI_STATE_IDLE = 0, WIFI_STATE_RUN, WIFI_STATE_DONE, WIFI_STATE_END } wifi_state_t;

/* 実行の範囲(UART コマンド d / n / p / x)。中身は step のビットマスク */
typedef enum {
    WIFI_RANGE_ALL = 0,          /* reset 〜 ping_dns(d) */
    WIFI_RANGE_BOOT,             /* reset 〜 at(n) */
    WIFI_RANGE_AT,               /* at だけ(p。リセットなし) */
    WIFI_RANGE_CONNECT,          /* 製品の接続: reset boot at echo mode qap join ip(gmr lapopt scan dns ping を省く) */
    WIFI_RANGE_WX                /* 取得 1 回(1 銘柄。wifi_core_set_symbol): wx_mux 〜 wx_recv(接続先は wifi_core_set_target。専用モードの z の時だけ wx_sslconf が入る) */
} wifi_range_t;

/* ---- ログ行の整形(UART。秘密を引数に取らない) ----
 * 戻り値 = 書いたバイト数(NULを除く、\r\n を含む)。失敗(バッファ不足・引数不正・値が長すぎる)は 0 で、buf が使えるなら buf[0] = '\0'。
 * 書式は design-uart.md §12.1。val は 0x21〜0x7E 以外を '_' にする。 */
size_t wifi_fmt_step(char *buf, size_t size, wifi_step_t step, wifi_res_t res, uint32_t try_n, uint32_t ms, uint32_t code);
size_t wifi_fmt_result(char *buf, size_t size, bool pass, wifi_step_t stage, uint32_t ping_ip, uint32_t ping_dns);
size_t wifi_fmt_state(char *buf, size_t size, wifi_state_t st, wifi_step_t step, uint32_t bytes);
size_t wifi_fmt_val(char *buf, size_t size, wifi_key_t key, const char *val);
size_t wifi_fmt_help(char *buf, size_t size);               /* "[help] d=run n=boot p=at s=state ?=help\r\n" */
const char *wifi_step_name(wifi_step_t step);               /* "none" "reset" ... 範囲外は NULL */

/* ---- AT コマンドの組み立て ----
 * `AT+CWJAP="<ssid>","<pwd>"`(\r\n なし)を buf に書く。" , \ の前に \ を付ける。戻り値 = 長さ。
 * 失敗(溢れる・ssid が NULL か空・pwd が NULL・制御文字(< 0x20)を含む)は 0 で buf[0] = '\0'。パスワードが空は可(オープンな AP)。 */
size_t wifi_build_cwjap(char *buf, size_t size, const char *ssid, const char *pwd);

/* ---- 受信行の分類と値の取り出し ---- */
typedef enum {
    WIFI_L_OTHER = 0,            /* 上以外(busy p... SEND OK 空行など)。無視 */
    WIFI_L_ECHO,                 /* AT / AT+... / ATE0。無視。ログに出さない */
    WIFI_L_OK,
    WIFI_L_ERROR,
    WIFI_L_FAIL,
    WIFI_L_READY,
    WIFI_L_CONNECTED,            /* WIFI CONNECTED */
    WIFI_L_GOT_IP,               /* WIFI GOT IP */
    WIFI_L_DISCONNECT,           /* WIFI DISCONNECT */
    WIFI_L_CWJAP,                /* +CWJAP:<n>  num = n */
    WIFI_L_PING,                 /* +PING:<n>  num = n(ms) */
    WIFI_L_PING_TIMEOUT,         /* +PING:TIMEOUT */
    WIFI_L_DOMAIN,               /* +CIPDOMAIN:"a.b.c.d"(引用符は任意)  text = IPv4 */
    WIFI_L_STA_IP,               /* +CIFSR:STAIP,"a.b.c.d"(引用符は任意)または +CIPSTA:ip:"a.b.c.d"(2.x)  text = IPv4 */
    WIFI_L_STA_GW,               /* +CIPSTA:gateway:"..." */
    WIFI_L_STA_MASK,             /* +CIPSTA:netmask:"..." */
    WIFI_L_AT_VER,               /* AT version:x.y...  num = メジャー、num2 = マイナー(数字が無ければ 0)、text = 続く最大 40 文字 */
    WIFI_L_SDK_VER,              /* SDK version:...  text = 続く最大 40 文字 */
    WIFI_L_CWLAP,                /* +CWLAP:... 周囲の AP の1行。分類だけ(text は空)。中身は wifi_parse_cwlap が見る */
    WIFI_L_CONNECT,              /* CONNECT / <n>,CONNECT(TCP がつながった) */
    WIFI_L_CLOSED,               /* CLOSED / <n>,CLOSED */
    WIFI_L_SEND_OK,              /* SEND OK */
    WIFI_L_SEND_FAIL,            /* SEND FAIL */
    WIFI_L_PROMPT,               /* >(AT+CIPSEND の入力待ち。改行で終わらないので、wx_send の間は > を受けた時点で 1 行として扱う) */
    WIFI_L_DNS_FAIL              /* DNS Fail */
} wifi_line_t;

typedef struct {
    wifi_line_t kind;
    uint32_t    num;
    uint32_t    num2;
    char        text[WIFI_VAL_MAX + 1u];   /* 0x21〜0x7E 以外は '_' */
} wifi_ev_t;

/* line は NUL 終端の1行(\r\n なし)。行全体の一致で分類する(+PING:OK や BOOKS を OK にしない)。壊れた行は OTHER。ev は全て書き直す */
void wifi_parse_line(const char *line, wifi_ev_t *ev);

/* ---- AT+CWLAP の1行(+CWLAP:(<ecn>,"<ssid>",<rssi>,"<mac>",<ch>)。括弧は任意、後ろの追加項目は無視)----
 * SSID・MAC は読んで比べるだけで、どこにも保存しない。match: 0 = 違う、1 = exact(バイト一致)、
 * 2 = near(exact ではないが、大小文字・空白(0x20, U+3000)・全角英数記号(U+FF01〜FF5E)をそろえたら一致)。
 * my_ssid が NULL か空なら match は常に 0。読めない行(引用符なし・数字なし・rssi が正・SSID が 32 バイト超・途中で切れた)は false で ap は 0 */
typedef struct {
    uint32_t ecn;
    int32_t  rssi;
    uint32_t ch;
    uint8_t  match;
} wifi_ap_t;

bool wifi_parse_cwlap(const char *line, const char *my_ssid, wifi_ap_t *ap);

/* line の「形」を out へ(最大 WIFI_VAL_MAX 文字 + NUL)。英字 -> 'a'、数字 -> '9'、他の 0x21〜0x7E はそのまま、それ以外(空白・日本語・制御文字)は '_'。
 * 戻り値 = 長さ。out が使えないなら 0 */
size_t wifi_shape(char *out, size_t size, const char *line);

/* ---- 状態機械 ---- */
#define WIFI_SEND_OK    (0)     /* 送信キューに入れた */
#define WIFI_SEND_BUSY   (1)     /* 今は送れない。次の poll で同じものをもう一度渡す */
#define WIFI_SEND_FAIL   (2)     /* 送れない(キューに入りきらない等)。step は fail(code=101) */

typedef struct {
    void (*en)(int level);                        /* ESP32 の EN ピン。0 = Low(リセット)、1 = High */
    int  (*send)(const char *data, size_t n);     /* data は \r\n 込みのコマンド。WIFI_SEND_*。data は次の呼び出しまで有効とは限らないので、その場で送信キューへ写す */
    void (*log)(const char *line, size_t n);      /* 整形済みの1行(\r\n 込み)。app_log_line() へ渡す。line は戻ったら使えない */
} wifi_ops_t;

/* 内部の状態。メンバーを直接触らない(試験の観測用に bad_lines だけ読んでよい) */
typedef struct {
    const wifi_ops_t *ops;
    const char *ssid;
    const char *pass;
    uint32_t    rx_bytes;
    uint32_t    bad_lines;                        /* 捨てた行(長すぎる・NUL を含む)の累計 */
    uint32_t    t0;
    uint32_t    ping_ms;
    uint32_t    ping_ip;
    uint32_t    ping_dns;
    uint32_t    fail_code;                        /* 失敗時の code: join の +CWJAP:<n>、ping の 200 / 201 */
    uint16_t    scan_total;                       /* scan の集計(各 999 で頭打ち。SSID・MAC は持たない) */
    uint16_t    scan_found;
    uint16_t    scan_near;
    uint16_t    scan_bad;
    uint16_t    ap_ch;                            /* 自分の AP(exact)のうち rssi が最大の1件 */
    int16_t     ap_rssi;
    uint8_t     ap_ecn;
    uint8_t     oth_n;                            /* この step で出した oth の行数(最大 3) */
    uint16_t    quiet_ms;
    uint16_t    cmdlen;
    uint8_t     llen;
    uint8_t     lbad;
    uint8_t     phase;
    uint8_t     after;
    uint8_t     step;
    uint8_t     try_n;
    uint8_t     done;
    uint8_t     ping_seen;
    uint32_t    mask;                             /* この実行に含める step のビット(1u << step) */
    /* 株価の取得(WX step)。応答の生の本文は wx_buf にだけ置き、解析のあと消す */
    uint32_t    wx_t0;                            /* 取得の開始(wx_mux)の時刻 */
    uint32_t    wx_first_ms;                      /* 最初のデータを受けた時刻 */
    uint32_t    wx_seq;                           /* 取得の結果が出るたびに +1 */
    uint32_t    done_seq;                         /* 実行が終わるたびに +1 */
    uint16_t    wx_len;
    uint16_t    ipd_left;                         /* データモード: 残りのバイト数 */
    uint8_t     wx_flags;
    uint8_t     target;                           /* wx_target_t。0 = YAHOO_SSL(既定。製品)。wifi_core_set_target */
    uint8_t     sym;                              /* 取得する銘柄 0〜2(wifi_core_set_symbol。既定 0) */
    uint8_t     res_pass;
    uint8_t     res_stage;
    uint8_t     badap_n;                          /* APP_STK_BADAP: join に入った回数(0 のビルドでは使わない) */
    uint8_t     badap_on;                         /* APP_STK_BADAP: 今回の接続はダミーで join する */
    uint8_t     quiet;                            /* 1 = 製品: res=start・res=ok と key=oth の行を出さない(wifi_core_set_quiet) */
    wx_result_t wx;
    char        wx_buf[WX_BUF_SIZE];
    char        line[WIFI_LINE_MAX + 1u];
    char        cmd[WIFI_CMD_MAX];
    char        out[WIFI_LOG_MAX];
    wifi_ev_t   ev;
} wifi_core_t;

/* 初期化。ops は必須(3つとも非 NULL)。ssid / pass は NULL か、呼び出し側が保持する文字列(コピーしない。ssid は scan の照合と join の組み立て、pass は join の組み立ての時だけ読む)。
 * どちらかが NULL なら、join を [wifi] step=join res=skip にして、そこで result=pass で終える */
void wifi_core_init(wifi_core_t *c, const wifi_ops_t *ops, const char *ssid, const char *pass);

/* 製品(APP_WX_PRODUCT)用: [wifi] step=... の res=start と res=ok、[wifiv] key=oth を出さない(失敗・時間切れ・skip と result= は出す。
 * ログキュー 8 行の drop を避ける。design.md §5.9)。既定は出す(専用モード)。init の後に呼ぶ */
void wifi_core_set_quiet(wifi_core_t *c, bool quiet);

/* 取得(WIFI_RANGE_WX)の接続先を選ぶ。次の wifi_core_start から有効。実行中・範囲外は無視。init の既定は WX_TGT_YAHOO_SSL(製品) */
void wifi_core_set_target(wifi_core_t *c, wx_target_t t);

/* 取得(WIFI_RANGE_WX)の銘柄(0 = 6723.T、1 = ^N225、2 = ^GSPC)を選ぶ。次の wifi_core_start から有効。実行中・範囲外(3 以上)は無視。init の既定は 0。
 * 結果の wx_result_t.sym にこの値が入る */
void wifi_core_set_symbol(wifi_core_t *c, uint8_t sym);

/* 実行を始める。実行中なら何もせず false(d n p の無視)。始めると [wifi] step=<最初> res=start を log へ出す */
bool wifi_core_start(wifi_core_t *c, wifi_range_t range, uint32_t now_ms);

/* 受信1バイト。\r \n \r\n のどれでも行の終わり。空行は捨てる。WIFI_LINE_MAX を超える行・NUL を含む行は丸ごと捨てる(bad_lines +1)。
 * 行の処理は待ち中の step の時だけ。それ以外の時の行は捨てる */
void wifi_core_rx(wifi_core_t *c, uint8_t b, uint32_t now_ms);

/* 時間を進める(毎周回。10ms ごとでよい)。タイムアウト・再試行・EN・送信を処理する */
void wifi_core_poll(wifi_core_t *c, uint32_t now_ms);

/* 取得の結果(1 銘柄 = 1 回)。wifi_core_wx_seq が増えたら wifi_core_wx(c) が新しい結果(失敗を含む)。本文は持たない(数値だけ) */
uint32_t            wifi_core_wx_seq(const wifi_core_t *c);
const wx_result_t  *wifi_core_wx(const wifi_core_t *c);
/* 実行の結果。done_seq は 1 回の実行が終わるたびに +1。pass = 全 step が成功(続行可の失敗を含む)、fail_stage = 打ち切った step(pass の時は NONE) */
uint32_t     wifi_core_done_seq(const wifi_core_t *c);
bool         wifi_core_pass(const wifi_core_t *c);
wifi_step_t  wifi_core_fail_stage(const wifi_core_t *c);

wifi_state_t wifi_core_state(const wifi_core_t *c);    /* idle = まだ始めていない、run、done = 1回以上終わった */
wifi_step_t  wifi_core_step(const wifi_core_t *c);     /* 実行中の step。待機・終了後は NONE([hb] の scr) */
uint32_t     wifi_core_bytes(const wifi_core_t *c);    /* ESP32 から受けたバイト数の累計([hb] の frm、[wifi] state の bytes) */

#endif /* WIFI_CORE_H */
