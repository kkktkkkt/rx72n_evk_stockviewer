/* wifi_at.c - Wi-Fi(ESP32 AT)のハード側: SCI1、EN(P43)、受信の汲み上げ、取得の予定表の実行、UART コマンド、ログ
 * 仕様: docs/design.md §21.4・§22、docs/design-uart.md §12・§13。論理(状態機械・書式・予定表)は wifi_core.c / wx_core.c(標準C)。
 * 2 つの使い方(どちらも APP_WIFI_ON = 0 の時は中身が消える。wifi_sw.h):
 *   - 専用モード(APP_WIFI_TEST = 1): wifi_test_main()。戻らない。LCD・タッチは動かさない。d n p = 接続の試験、x = 天気の取得(WIFI_RANGE_WX)。
 *   - 製品(APP_WX_PRODUCT = APP_WX かつ APP_WIFI_TEST = 0): app_main のメインループから wx_init() / wx_poll()(ブロックしない)。
 *     接続(WIFI_RANGE_CONNECT)→ 取得(WIFI_RANGE_WX)を wx_sched_*(予定表)が決め、ここが実行して結果を返す。表示用の値は wx_ui()。
 * 応答の本文は、ここからも出さない(出すのは数値と名前だけ)。
 * 秘密: SSID・パスワードは wifi_secret.h(git 管理外)から wifi_core へポインタで渡すだけ。ここからはログ・[err] に出さない。
 *       wifi_secret.h を include するのはこのファイルだけ。無い時は製品は OFFLINE のまま動く(ESP32 に触れない)。 */
#include "wifi_sw.h"

#if APP_WIFI_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "platform.h"
#include "r_cmt_rx_if.h"
#include "r_sci_rx_if.h"
#include "r_sci_rx_pinset.h"
#include "r_gpio_rx_if.h"
#include "board_pins.h"
#include "app_debug.h"
#include "app_logfmt.h"
#include "wifi_core.h"
#include "wx_core.h"
#include "wifi_at.h"

#if !APP_LOG_ENABLE
#error "APP_WIFI_TEST / APP_WX needs the UART log (APP_LOG_ENABLE = 1)"
#endif
#if APP_LOG_SCI_CH != 2
#error "APP_LOG_SCI_CH must be 2 (SCI1 is for the ESP32)"
#endif

/* SSID・パスワード。ホスト試験(WIFI_AT_HOST_TEST)は実物を読まず、試験が WIFI_SSID / WIFI_PASS を定義する */
#if defined(WIFI_AT_HOST_TEST)
/* 試験が定義する */
#elif __has_include("wifi_secret.h")
#include "wifi_secret.h"
#if !defined(WIFI_SSID) || !defined(WIFI_PASS)
#error "wifi_secret.h must define WIFI_SSID and WIFI_PASS"
#endif
#endif
#if defined(WIFI_SSID) && defined(WIFI_PASS)
#define AT_SSID        (WIFI_SSID)
#define AT_PASS        (WIFI_PASS)
#define AT_HAVE_SECRET (1)
#else
#define AT_SSID        ((const char *)NULL)      /* 無い間は join 以降を skip(wifi_core)。製品は ESP32 に触れない(OFFLINE) */
#define AT_PASS        ((const char *)NULL)
#define AT_HAVE_SECRET (0)
#endif

#define WIFI_SCI_CH        (1u)
#define WIFI_INT_PRIORITY  (2u)             /* SCI2(ログ)・SCI6(タッチ)と同じ。SC の 3 は R_SCI_Open で上書き */
#define WIFI_TICK_HZ       (100u)
#define WIFI_TICK_MS       (1000u / WIFI_TICK_HZ)   /* 10ms。wifi_core の時刻はこの刻み(待ち時間の誤差は最大 10ms) */
#define WIFI_RX_PER_LOOP   (64u)            /* 1周回に wifi_core へ渡す受信バイトの上限(長く占有しない) */
#define WIFI_AUTO_TICKS    (200u)           /* APP_WIFI_AUTOSTART: 起動から 2 秒後(タイマ開始 = 起動直後) */
#define WIFI_RXERR_MS      (1000u)          /* [err] code=10 は 1 秒に 1 行まで */

static wifi_core_t s_core;
static wx_clock_t  s_clk;                   /* ソフト時計(Date: + 9 時間。取得のたびに合わせ直す) */
static uint32_t    s_wx_seen;               /* 報告した取得の結果の数(wifi_core_wx_seq) */
static uint32_t    s_wx_ok_ms;              /* 最後の成功の時刻(age 用) */
static int16_t     s_wx_temp;               /* 最後の成功の値(表示・s の状態行用) */
static uint8_t     s_wx_kind;
static uint8_t     s_wx_have;               /* 一度でも成功した */
static sci_hdl_t   s_hdl;
static bool        s_ok;                    /* SCI1 を開けた */
static uint32_t    s_open_err;              /* R_SCI_Open の戻り値(gdb 用) */

/* コールバック(割り込み内)が書く。seq を最後に増やす(1書き手・1読み手でロック不要)。gdb でも読める */
static volatile uint32_t s_rx_ev_val;
static volatile uint32_t s_rx_ev_seq;
static uint32_t          s_rx_ev_seen;
static uint32_t          s_rxerr_ms;
static bool              s_rxerr_sent;

static char           s_line[APPLOG_LINE_MAX];   /* メインループ専用 */

static void sci_cb(void *p_args)
{
    const sci_cb_args_t *a = (const sci_cb_args_t *)p_args;

    if (a == NULL) {
        return;
    }
    if ((a->event == SCI_EVT_RXBUF_OVFL) || (a->event == SCI_EVT_FRAMING_ERR) ||
        (a->event == SCI_EVT_PARITY_ERR) || (a->event == SCI_EVT_OVFL_ERR)) {
        s_rx_ev_val = (uint32_t)a->event;
        s_rx_ev_seq = s_rx_ev_seq + 1u;
    }
    /* SCI_EVT_RX_CHAR などは何もしない(バイトは r_sci_rx の受信キューに入る) */
}

/* ---- wifi_ops_t ---- */
static void op_en(int level)
{
    R_GPIO_PinWrite(PIN_WIFI_EN, (level != 0) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

static int op_send(const char *data, size_t n)
{
    sci_err_t e = R_SCI_Send(s_hdl, (uint8_t *)data, (uint16_t)n);   /* r_sci_rx は送信キューへ写す。data は変更されない */

    if (e == SCI_SUCCESS) {
        return WIFI_SEND_OK;
    }
    return (e == SCI_ERR_XCVR_BUSY) ? WIFI_SEND_BUSY : WIFI_SEND_FAIL;   /* BUSY は捨てず、次の周回で同じものを再度渡される */
}

static void op_log(const char *line, size_t n)
{
    app_log_line(line, n);
}

static const wifi_ops_t k_ops = { op_en, op_send, op_log };

/* ---- ログ ---- */
static void emit(size_t n)
{
    app_log_line(s_line, n);                 /* n == 0(整形失敗)は app_log_line が drop に数える */
}

static void log_rx_error(uint32_t now_ms)
{
    uint32_t seq = s_rx_ev_seq;

    if (seq == s_rx_ev_seen) {
        return;
    }
    if (s_rxerr_sent && (applog_due(now_ms, s_rxerr_ms, WIFI_RXERR_MS) == 0)) {
        return;
    }
    s_rx_ev_seen = seq;
    s_rxerr_ms   = now_ms;
    s_rxerr_sent = true;
    emit(applog_fmt_err(s_line, sizeof(s_line), APP_ERR_WIFI_RX, s_rx_ev_val));
}

/* ESP32 からの受信を wifi_core へ(1周回に最大 WIFI_RX_PER_LOOP バイト) */
static void pump_rx(uint32_t now_ms)
{
    uint8_t b;
    uint8_t i;

    for (i = 0u; i < WIFI_RX_PER_LOOP; i++) {
        if (R_SCI_Receive(s_hdl, &b, 1u) != SCI_SUCCESS) {
            return;
        }
        wifi_core_rx(&s_core, b, now_ms);
    }
}

static void wifi_open(void)
{
    sci_cfg_t cfg;
    sci_err_t e;

    /* EN(P43): 先に High にしてから出力にする(ESP32 を止めない。EN は基板で 3.3V にプルアップ済み) */
    R_GPIO_PinWrite(PIN_WIFI_EN, GPIO_LEVEL_HIGH);
    R_GPIO_PinDirectionSet(PIN_WIFI_EN, GPIO_DIRECTION_OUTPUT);

    R_SCI_PinSet_SCI1();                     /* P30 = RXD1、P26 = TXD1(CTS1# は SC で外した) */
    cfg.async.baud_rate    = WIFI_BAUD;
    cfg.async.clk_src      = SCI_CLK_INT;
    cfg.async.data_size    = SCI_DATA_8BIT;
    cfg.async.parity_en    = SCI_PARITY_OFF;
    cfg.async.parity_type  = 0u;
    cfg.async.stop_bits    = SCI_STOPBITS_1;
    cfg.async.int_priority = WIFI_INT_PRIORITY;
    e = R_SCI_Open(WIFI_SCI_CH, SCI_MODE_ASYNC, &cfg, sci_cb, &s_hdl);
    s_open_err = (uint32_t)e;
    s_ok = (e == SCI_SUCCESS);
}

/* ---- 取得の結果(wifi_core が出した結果を [wx] の行にし、ソフト時計を合わせる。数値だけ。本文は wifi_core が消している) ---- */
static uint8_t  s_res_ok;                   /* 今の動きの中で出た取得の結果(製品の予定表へ返す) */
static uint16_t s_res_http;
#if APP_WX_PRODUCT
static uint16_t s_upd_min;                  /* 最後の成功の時刻(JST の分。0xFFFF = なし) */
#endif
#if APP_WIFI_TEST
static uint8_t  s_wx_last_ok;
#endif

static void wx_report(uint32_t now_ms, wx_src_t src)
{
    uint32_t           seq = wifi_core_wx_seq(&s_core);
    const wx_result_t *r;

    if (seq == s_wx_seen) {
        return;
    }
    s_wx_seen = seq;
    r = wifi_core_wx(&s_core);
    if (r->have_date != 0u) {
        wx_clock_sync(&s_clk, r->jst_s, r->first_ms);
    }
    s_res_ok   = r->ok;
    s_res_http = r->http;
#if APP_WIFI_TEST
    s_wx_last_ok = r->ok;
#endif
    if (r->ok != 0u) {
        s_wx_have  = 1u;
        s_wx_temp  = r->temp_x10;
        s_wx_kind  = r->kind;
        s_wx_ok_ms = now_ms;
#if APP_WX_PRODUCT
        s_upd_min  = wx_clock_min(&s_clk, now_ms);
#endif
        emit(wx_fmt_ok(s_line, sizeof(s_line), src, r));
    } else {
        emit(wx_fmt_fail(s_line, sizeof(s_line), src, r));
    }
}

/* [wx] 状態行(s の応答。製品は net / busy が変わった時も) */
static void wx_state_line(uint32_t now_ms, uint8_t net, uint8_t busy)
{
    wx_view_t v;

    v.net      = net;
    v.busy     = busy;
    v.have     = s_wx_have;
    v.kind     = s_wx_kind;
    v.temp_x10 = s_wx_temp;
    v.age_s    = (s_wx_have != 0u) ? (int32_t)((now_ms - s_wx_ok_ms) / 1000u) : -1;
    v.synced   = wx_clock_now(&s_clk, now_ms, &v.jst_s) ? 1u : 0u;
    emit(wx_fmt_state(s_line, sizeof(s_line), &v));
}

#if APP_WX_PRODUCT
/* ================================================================ 製品: メインループから呼ぶ(ブロックしない) */
static wx_sched_t s_sched;                  /* 取得の予定表。gdb で s_sched.net を書けば、表示だけエラー・OFFLINE にできる */
static uint8_t    s_act;                    /* 実行中の動き(wx_act_t。NONE = なし) */
static uint32_t   s_act_t0;
static uint32_t   s_done_seen;              /* 報告した実行の完了の数(wifi_core_done_seq) */
static uint8_t    s_last_net;               /* 状態行を最後に出した時の net / busy */
static uint8_t    s_last_busy;

static void act_begin(wx_act_t act, uint32_t now_ms)
{
    s_act      = (uint8_t)act;
    s_act_t0   = now_ms;
    s_res_ok   = 0u;
    s_res_http = 0u;
    if (!wifi_core_start(&s_core, (act == WX_ACT_CONNECT) ? WIFI_RANGE_CONNECT : WIFI_RANGE_WX, now_ms)) {
        s_act = (uint8_t)WX_ACT_NONE;       /* 始められない(通常は起きない): 失敗として予定表へ返す */
        wx_sched_done(&s_sched, act, false, 0u, now_ms);
    }
}

/* wifi_core の実行が終わったら、結果を予定表へ返す。接続の失敗は [wx] res=fail err=8 を 1 行 */
static void act_end(uint32_t now_ms)
{
    uint32_t    seq = wifi_core_done_seq(&s_core);
    wx_act_t    act = (wx_act_t)s_act;
    wx_result_t f;
    bool        ok;

    if ((seq == s_done_seen) || (act == WX_ACT_NONE)) {
        s_done_seen = seq;
        return;
    }
    s_done_seen = seq;
    s_act       = (uint8_t)WX_ACT_NONE;
    if (act == WX_ACT_CONNECT) {
        ok = wifi_core_pass(&s_core);
        if (!ok) {
            memset(&f, 0, sizeof f);
            f.err  = 8u;
            f.kind = (uint8_t)WX_KIND_UNKNOWN;
            f.ms   = now_ms - s_act_t0;
            emit(wx_fmt_fail(s_line, sizeof(s_line), wx_sched_src(&s_sched), &f));
        }
        wx_sched_done(&s_sched, WX_ACT_CONNECT, ok, 0u, now_ms);
    } else {
        wx_sched_done(&s_sched, WX_ACT_FETCH, s_res_ok != 0u, s_res_http, now_ms);
    }
}

void wx_init(uint32_t now_ms)
{
    wx_clock_init(&s_clk);
    s_upd_min = 0xFFFFu;
    wifi_core_init(&s_core, &k_ops, AT_SSID, AT_PASS);
    wifi_core_set_quiet(&s_core, true);
    if (AT_HAVE_SECRET != 0) {
        wifi_open();
        if (!s_ok) {
            emit(applog_fmt_err(s_line, sizeof(s_line), APP_ERR_WIFI_OPEN, s_open_err));
        }
    }
    wx_sched_init(&s_sched, s_ok, now_ms);   /* 秘密が無い・SCI1 を開けない = OFF(ESP32 に何もしない) */
    s_last_net  = (uint8_t)wx_sched_net(&s_sched);
    s_last_busy = 0u;
}

void wx_poll(uint32_t now_ms)
{
    wx_act_t act;
    uint8_t  net;
    uint8_t  busy;

    if (!s_ok) {
        return;
    }
    pump_rx(now_ms);
    wifi_core_poll(&s_core, now_ms);
    wx_report(now_ms, wx_sched_src(&s_sched));
    act_end(now_ms);
    act = wx_sched_poll(&s_sched, now_ms);
    if (act != WX_ACT_NONE) {
        act_begin(act, now_ms);
    }
    log_rx_error(now_ms);
    net  = (uint8_t)wx_sched_net(&s_sched);
    busy = wx_sched_busy(&s_sched) ? 1u : 0u;
    if ((net != s_last_net) || (busy != s_last_busy)) {
        s_last_net  = net;
        s_last_busy = busy;
        wx_log_state(now_ms);
    }
}

wx_req_t wx_request(wx_src_t src, uint32_t now_ms)
{
    wx_req_t r = wx_sched_request(&s_sched, src, now_ms);

    if (r != WX_REQ_OK) {
        emit(wx_fmt_req(s_line, sizeof(s_line), r, src));
    }
    return r;
}

void wx_log_state(uint32_t now_ms)
{
    wx_state_line(now_ms, (uint8_t)wx_sched_net(&s_sched), wx_sched_busy(&s_sched) ? 1u : 0u);
}

void wx_ui(ui_wx_t *u, uint32_t now_ms)
{
    wx_date_t d;
    uint32_t  j;

    memset(u, 0, sizeof *u);
    u->net       = (uint8_t)wx_sched_net(&s_sched);                     /* wx_net_t = UI_NET_*(同じ並び) */
    u->busy      = wx_sched_busy(&s_sched) ? 1u : 0u;
    u->have      = s_wx_have;
    u->kind      = s_wx_kind;
    u->temp_x10  = s_wx_temp;
    u->stale     = ((s_wx_have != 0u) && (((now_ms - s_wx_ok_ms) / 1000u) > WX_STALE_S)) ? 1u : 0u;
    u->clk_ext   = 1u;
    u->upd_min   = s_upd_min;
    u->clock_min = wx_clock_min(&s_clk, now_ms);
    if (wx_clock_now(&s_clk, now_ms, &j)) {
        wx_date_fields(j, &d);
        u->month = d.month;
        u->day   = d.day;
        u->wday  = d.wday;
    }
}
#endif /* APP_WX_PRODUCT */

#if APP_WIFI_TEST
/* ================================================================ 専用モード */
#if APP_WIFI_AUTOSTART
static bool        s_auto_done;
#endif
static volatile uint32_t s_tick;            /* 10ms ごとに +1(CMT割り込み) */
static applog_state_t s_st;

static void tick_callback(void *pdata)
{
    (void)pdata;
    s_tick++;
}

static void log_state(const char *tag, bool with_range)
{
    s_st.tick            = g_app_dbg.tick;
    s_st.err_code        = g_app_dbg.err_code;
    s_st.err_detail      = g_app_dbg.err_detail;
    s_st.i2c_fail_total  = 0u;
    s_st.drop            = app_log_drops();
    s_st.frames          = g_app_dbg.frames;     /* frm = ESP32 から受けたバイト数の累計 */
    s_st.uf              = 0u;
    s_st.raw_x           = 0u;
    s_st.raw_y           = 0u;
    s_st.x_min           = 0u;
    s_st.x_max           = 0u;
    s_st.y_min           = 0u;
    s_st.y_max           = 0u;
    s_st.raw_points      = 0u;
    s_st.scr             = g_app_dbg.scr;        /* scr = 今の step 番号(0 = 待機) */
    s_st.demo            = 0u;
    s_st.i2c_fail_streak = 0u;
    s_st.found_count     = 0u;
    emit(applog_fmt_state(s_line, sizeof(s_line), tag, &s_st, with_range));
}

/* s の応答に足す状態行。一度も取得していなければ出さない(UW の判定に影響しない) */
static void ded_state_line(uint32_t now_ms)
{
    if (s_wx_seen == 0u) {
        return;
    }
    wx_state_line(now_ms, (s_wx_last_ok != 0u) ? (uint8_t)WX_NET_ON : (uint8_t)WX_NET_ERR,
                  (wifi_core_state(&s_core) == WIFI_STATE_RUN) ? 1u : 0u);
}

/* ---- 受信コマンド(1 tick に1文字): d n p は実行、x t y g z は取得、s ? は応答。それ以外(+ - を含む)は無視 ---- */
static void start(wifi_range_t range, uint32_t now)
{
    if (s_ok) {
        (void)wifi_core_start(&s_core, range, now * WIFI_TICK_MS);   /* 実行中なら何もしない(無視) */
    }
}

/* TLS の検証(design.md §5.5.2): t = Yahoo 平文 / y = Yahoo SSL / g = Google SSL / z = Yahoo SSL(認証なしの設定つき)。x は最後に選んだ対象でもう一度 */
static void start_target(wx_target_t t, uint32_t now)
{
    if (s_ok) {
        wifi_core_set_target(&s_core, t);    /* 実行中なら無視(下の start も無視される) */
        start(WIFI_RANGE_WX, now);
    }
}

static void uart_command(uint32_t now)
{
    int ch = app_log_getc();

    if (ch < 0) {
        return;
    }
    switch (ch) {
    case 't': start_target(WX_TGT_YAHOO_TCP, now);        return;
    case 'y': start_target(WX_TGT_YAHOO_SSL, now);        return;
    case 'g': start_target(WX_TGT_GOOGLE_SSL, now);       return;
    case 'z': start_target(WX_TGT_YAHOO_SSL_NOAUTH, now); return;
    default:  break;
    }
    switch (applog_cmd_parse(ch)) {
    case APPLOG_CMD_DEMO:  start(WIFI_RANGE_ALL, now);  break;     /* d */
    case APPLOG_CMD_NEXT:  start(WIFI_RANGE_BOOT, now); break;     /* n */
    case APPLOG_CMD_PREV:  start(WIFI_RANGE_AT, now);   break;     /* p */
    case APPLOG_CMD_WX:    start(WIFI_RANGE_WX, now);   break;     /* x */
    case APPLOG_CMD_STATE:                                          /* s */
        emit(wifi_fmt_state(s_line, sizeof(s_line), wifi_core_state(&s_core), wifi_core_step(&s_core), wifi_core_bytes(&s_core)));
        log_state("st", true);
        ded_state_line(now * WIFI_TICK_MS);
        break;
    case APPLOG_CMD_HELP:                                           /* ? */
        emit(wifi_fmt_help(s_line, sizeof(s_line)));
        break;
    default:
        break;
    }
}

/* 10ms ごとの処理 */
static void tick_step(uint32_t now, uint32_t *last_hb)
{
    g_app_dbg.tick   = now;
    g_app_dbg.scr    = (uint8_t)wifi_core_step(&s_core);
    g_app_dbg.frames = wifi_core_bytes(&s_core);
    if (applog_due(now, *last_hb, WIFI_TICK_HZ) != 0) {
        *last_hb = now;
        log_state("hb", false);
    }
    log_rx_error(now * WIFI_TICK_MS);
    uart_command(now);
#if APP_WIFI_AUTOSTART
    if (!s_auto_done && (now >= WIFI_AUTO_TICKS)) {
        s_auto_done = true;
        start(WIFI_RANGE_ALL, now);          /* d と同じ。1回だけ */
    }
#endif
}

void wifi_test_main(void)
{
    uint32_t cmt_ch;
    uint32_t last = 0u;
    uint32_t last_hb = 0u;
    uint32_t now;

    wifi_open();
    wifi_core_init(&s_core, &k_ops, AT_SSID, AT_PASS);
    emit(applog_fmt_init(s_line, sizeof(s_line), "wifi", s_ok, s_ok ? 0u : APP_ERR_WIFI_OPEN, s_ok ? 0u : s_open_err));
    if (!s_ok) {
        emit(applog_fmt_err(s_line, sizeof(s_line), APP_ERR_WIFI_OPEN, s_open_err));
    }

    if (!R_CMT_CreatePeriodic(WIFI_TICK_HZ, tick_callback, &cmt_ch)) {
        app_debug_set_err(APP_ERR_TIMER, 0u);
        emit(applog_fmt_init(s_line, sizeof(s_line), "timer", false, APP_ERR_TIMER, 0u));
        emit(applog_fmt_err(s_line, sizeof(s_line), APP_ERR_TIMER, 0u));
        for (;;) {
            app_log_poll();                  /* タイマが無いと周期が作れない。上の行を出し切るため送信だけ進める */
        }
    }

    for (;;) {
        app_log_poll();                      /* SCI2 の送信キューを進める(非ブロッキング。毎周回) */
        now = s_tick;
        if (s_ok) {
            pump_rx(now * WIFI_TICK_MS);     /* 受信 → 状態機械(時刻は 10ms 刻み) */
            wifi_core_poll(&s_core, now * WIFI_TICK_MS);
            wx_report(now * WIFI_TICK_MS, WX_SRC_UART);
        }
        if (now != last) {                   /* 10ms ごと */
            last = now;
            tick_step(now, &last_hb);
        }
    }
}
#endif /* APP_WIFI_TEST */

#endif /* APP_WIFI_ON */
