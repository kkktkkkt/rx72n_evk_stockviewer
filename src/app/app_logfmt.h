/* app_logfmt.h - UARTログの行組み立てとコマンド解釈(標準Cのみ。FITを include しない)
 * 仕様: docs/design-uart.md(最優先。製品デモは §10)。ホストでテストできる。 */
#ifndef APP_LOGFMT_H
#define APP_LOGFMT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define APPLOG_LINE_MAX   (256u)    /* 1行の最大バイト数(\r\n と NUL を含む) */

typedef struct {
    uint32_t tick;              /* 10ms ごとに増える */
    uint32_t err_code;
    uint32_t err_detail;
    uint32_t i2c_fail_total;
    uint32_t drop;              /* UART送信の破棄累計(UART自身の値) */
    uint32_t frames;            /* 表示の入れ替え完了回数の累計 */
    uint32_t uf;                /* GLCDC アンダーフロー検出の累計 */
    uint16_t raw_x;
    uint16_t raw_y;
    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
    uint8_t  raw_points;
    uint8_t  scr;               /* 画面番号 0..4 */
    uint8_t  demo;              /* デモ中 = 1 */
    uint8_t  i2c_fail_streak;
    uint8_t  found_count;
} applog_state_t;

typedef enum {
    APPLOG_CMD_NONE = 0,
    APPLOG_CMD_NEXT,            /* 'n' 専用モード(APP_WIFI_TEST)だけ。製品は無視 */
    APPLOG_CMD_PREV,            /* 'p' 同上 */
    APPLOG_CMD_STATE,           /* 's' */
    APPLOG_CMD_HELP,            /* '?' */
    APPLOG_CMD_DEMO,            /* 'd' 同上 */
    APPLOG_CMD_WX,              /* 'x' = 更新(製品はタップと同じ。専用モードは取得 1 回) */
    APPLOG_CMD_BL               /* 'b' = バックライトの強制の切り替え(試験用の隠しコマンド。docs/design.md §14) */
} applog_cmd_t;

/* すべての applog_fmt_*:
 *   戻り値 = 書いたバイト数(NULを除く、\r\n を含む)。失敗は 0。
 *   失敗(バッファ不足・引数不正)のとき、buf が有効で size > 0 なら buf[0] = '\0'。部分行は残さない。 */

/* "[boot] fw=rx72n_gcc build=<id> ch=<ch> baud=<baud>\r\n"
 * build は 0x21〜0x7E 以外(空白含む)を '_' にして31文字で切る。NULL は "unknown"。 */
size_t applog_fmt_boot(char *buf, size_t size, const char *build, uint8_t ch, uint32_t baud);

/* "[init] stage=<stage> ok=<0|1> err=<err> detail=<detail>\r\n"(渡された値をそのまま出す) */
size_t applog_fmt_init(char *buf, size_t size, const char *stage, bool ok, uint32_t err, uint32_t detail);

/* "[<tag>] tick=.. scr=.. demo=.. frm=.. uf=.. err=.. detail=.. pts=.. x=.. y=.. i2c_fail=.. streak=.. found=.. drop=..\r\n"
 * with_range が真なら末尾に " xmin=.. xmax=.. ymin=.. ymax=.." を付ける。tag は "hb" / "st" */
size_t applog_fmt_state(char *buf, size_t size, const char *tag, const applog_state_t *s, bool with_range);

/* "[scan] n=<count> a0=0x38 a1=0x..\r\n"  count は 4 を超えたら 4。count > 0 のとき addr が必要 */
size_t applog_fmt_scan(char *buf, size_t size, uint8_t count, const uint8_t *addr);

/* "[ev] btn=tap src=<src>\r\n"(タップ 1 回。src は "touch" / "uart"。プログラム内の定数だけを渡す。NULL は "?"。docs/design.md §7.5) */
size_t applog_fmt_ev_tap(char *buf, size_t size, const char *src);

/* "[perf] n=<n> copy_us=<..> home_us=<..> slide_us=<..>\r\n" */
size_t applog_fmt_perf(char *buf, size_t size, uint32_t n, uint32_t copy_us, uint32_t home_us, uint32_t slide_us);

/* "[scr] to=<stock|clock> pal=<0..8> clk=<hhmm|none> kind=<名前|none> temp=<-99..99|none> tick=<n>\r\n"(docs/design-uart.md §14.1。入れ替えを受け付けるたびに 1 行)
 * to_clock = 0 なら pal を 0 にそろえる(株価)。clk_min が 1440 以上は clk=none。have = 0 は kind=none temp=none。
 * pal > 8、have = 1 での kind > 7・temp が -99〜99 の外は失敗(0・buf[0] = '\0')。kind の名前は clk_core.h の clk_kind_name と同じ(app_logfmt.c は標準 C だけで自己完結させるため、ここにも持つ) */
size_t applog_fmt_scr(char *buf, size_t size, bool to_clock, uint8_t pal, uint16_t clk_min, uint8_t kind, bool have, int8_t temp, uint32_t tick);

/* "[bl] mode=<on|dim|off> cause=<boot|night|day|touch|timeout|uart|state> pct=<0..100> clk=<hhmm|none> tick=<n> + CRLF(docs/design.md §14.4)
 * mode・cause は下の名前だけ(それ以外は失敗)。pct は mode と整合(on = 100、off = 0、dim = 1〜99。違えば失敗)。clk_min が 1440 以上は clk=none */
size_t applog_fmt_bl(char *buf, size_t size, const char *mode, const char *cause, uint8_t pct, uint16_t clk_min, uint32_t tick);

/* "[err] code=<code> detail=<detail>\r\n" */
size_t applog_fmt_err(char *buf, size_t size, uint32_t code, uint32_t detail);

/* "[help] s=state x=update ?=help\r\n"(製品の UART コマンド。docs/design.md §7.3) */
size_t applog_fmt_help(char *buf, size_t size);

/* 受信1バイト -> コマンド。'n' 'p' 's' '?' 'd' 'x' 'b' 以外(-1、範囲外を含む)は NONE */
applog_cmd_t applog_cmd_parse(int ch);

/* (uint32_t)(now - last) >= period なら 1。32bit一周でも成立 */
int applog_due(uint32_t now, uint32_t last, uint32_t period);

/* (code, detail) が最後に出した値と違えば 1 */
int applog_err_changed(uint32_t code, uint32_t detail, uint32_t last_code, uint32_t last_detail);

#endif /* APP_LOGFMT_H */
