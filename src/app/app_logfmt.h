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

/* [ui] 行(s コマンドの時だけ)の値。文字列はプログラム内の定数だけを渡す */
typedef struct {
    const char *scr;            /* home fill reheat usage standby */
    const char *btn;            /* ボタン名 または none */
    const char *ph;             /* none press flash */
    uint16_t    set, bath10, fill10, flow10;
    uint8_t     tr;
} applog_ui_t;

typedef enum {
    APPLOG_CMD_NONE = 0,
    APPLOG_CMD_NEXT,            /* 'n' */
    APPLOG_CMD_PREV,            /* 'p' */
    APPLOG_CMD_STATE,           /* 's' */
    APPLOG_CMD_HELP,            /* '?' */
    APPLOG_CMD_DEMO,            /* 'd' */
    APPLOG_CMD_PLUS,            /* '+' */
    APPLOG_CMD_MINUS,           /* '-' */
    APPLOG_CMD_WX               /* 'x' = 更新ボタン h_wx の仮想タップ(専用モードでは天気の取得) */
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

/* "[ev] img=<index> src=<src>\r\n"  src は "touch" / "uart"(P2 ギャラリー用) */
size_t applog_fmt_ev(char *buf, size_t size, uint8_t index, const char *src);

/* 製品デモの [ev](design-uart.md §10.3)。文字列引数はプログラム内の定数。src が NULL なら " src=" を付けない */
size_t applog_fmt_ev_scr(char *buf, size_t size, const char *to, const char *from, const char *src);  /* [ev] scr=<to> from=<from> src=<src> */
size_t applog_fmt_ev_btn(char *buf, size_t size, const char *name, const char *st, const char *src);  /* [ev] btn=<name> st=<down|act> src=<src> */
size_t applog_fmt_ev_set(char *buf, size_t size, uint32_t n, const char *src);                        /* [ev] set=<n> src=<src> */
size_t applog_fmt_ev_done(char *buf, size_t size, const char *what);                                  /* [ev] done=<fill|reheat> */
size_t applog_fmt_ev_demo(char *buf, size_t size, const char *what, const char *src);                 /* [ev] demo=<start|end|abort> [src=..] */

/* "[ui] scr=.. set=.. bath10=.. fill10=.. flow10=.. tr=.. btn=.. ph=..\r\n" */
size_t applog_fmt_ui(char *buf, size_t size, const applog_ui_t *u);

/* "[perf] n=<n> copy_us=<..> home_us=<..> slide_us=<..>\r\n" */
size_t applog_fmt_perf(char *buf, size_t size, uint32_t n, uint32_t copy_us, uint32_t home_us, uint32_t slide_us);

/* "[err] code=<code> detail=<detail>\r\n" */
size_t applog_fmt_err(char *buf, size_t size, uint32_t code, uint32_t detail);

/* "[help] n=next p=prev s=state d=demo +=up -=down x=wx ?=help\r\n" */
size_t applog_fmt_help(char *buf, size_t size);

/* 受信1バイト -> コマンド。'n' 'p' 's' '?' 'd' '+' '-' 'x' 以外(-1、範囲外を含む)は NONE */
applog_cmd_t applog_cmd_parse(int ch);

/* (uint32_t)(now - last) >= period なら 1。32bit一周でも成立 */
int applog_due(uint32_t now, uint32_t last, uint32_t period);

/* (code, detail) が最後に出した値と違えば 1 */
int applog_err_changed(uint32_t code, uint32_t detail, uint32_t last_code, uint32_t last_detail);

#endif /* APP_LOGFMT_H */
