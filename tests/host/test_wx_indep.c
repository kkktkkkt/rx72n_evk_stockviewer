/* test_wx_indep.c - 株価の取得・時刻同期(docs/design.md §5・§7.5)の独立試験(テスト担当が追加。コーディング担当の試験と重ならない観点)。標準C + ホストの OS(ガードページ)。
 *   A  ガードページ: wx_parse_date に、データの末尾(または先頭)をアクセス禁止ページに接して渡す。範囲外を 1 バイトでも読めば異常終了する
 *      (Yahoo の応答の解析 wx_parse_http の同じ検査は test_wx_stock.c)
 *   B  日付の全日検査: 2020-01-01〜2099-12-31 の全日 x 3 時刻を、独立に作った暦(1 日ずつ数える)と突き合わせる。Date: の往復・JST の繰り上がり・存在しない日の拒否
 *   C  ソフト時計: 32bit の ms の巻き戻りを含む乱数検査(64bit の基準と比べる)・日またぎ
 *   D  取得の予定表(wx_sched。3 銘柄・やり直し・429・全部失敗): 乱数の走行を、時刻の起点を 0 と 0xFFFF0000(走行の途中で ms が折り返す)で 2 回行い、行動の列が一致すること + 時間と順序の規則
 *   E  wifi_core の WX step: 乱数のバイト列(+IPD・> ・CLOSED・ready・本文の目印を混ぜる)を流し、範囲外・固まり・秘密/本文/Date: のログ混入がないこと。銘柄ごとの要求
 * -DAPP_WX=1 でコンパイルする。 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "wx_core.h"
#include "wifi_core.h"
#include "test_framework.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#define PG (4096u)

/* ---------------------------------------------------------------- 乱数(xorshift。再現できる) */
static uint32_t g_rs = 0x12345678u;
static uint32_t rnd(void)
{
    g_rs ^= g_rs << 13; g_rs ^= g_rs >> 17; g_rs ^= g_rs << 5;
    return g_rs;
}
static uint32_t rn(uint32_t n) { return rnd() % n; }

/* ---------------------------------------------------------------- A ガードページ */
static unsigned char *g_pg;   /* [禁止][データ][禁止] */

static int guard_init(void)
{
#ifdef _WIN32
    DWORD o;
    g_pg = (unsigned char *)VirtualAlloc(NULL, 3u * PG, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_pg == NULL) { return 0; }
    VirtualProtect(g_pg, PG, PAGE_NOACCESS, &o);
    VirtualProtect(g_pg + 2u * PG, PG, PAGE_NOACCESS, &o);
#else
    g_pg = (unsigned char *)mmap(NULL, 3u * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_pg == MAP_FAILED) { g_pg = NULL; return 0; }
    mprotect(g_pg, PG, PROT_NONE);
    mprotect(g_pg + 2u * PG, PG, PROT_NONE);
#endif
    return 1;
}
static const char *place(const void *src, size_t n, int at_end)
{
    unsigned char *d = at_end ? (g_pg + 2u * PG - n) : (g_pg + PG);
    if (n > 0u) { memcpy(d, src, n); }
    return (const char *)d;
}

static int g_inv_bad;   /* D・E の不変条件の違反(E の feed も数える) */

static void t_guard(void)
{
    size_t      len, i;
    int         at;
    static const char poison[] = { '0', '9', '-', '.', '"', ':', ',', '}', '\r', '\n', ' ', 'e', 'n', 'u', 'l', '\0', (char)0xFF, 'D', 'T', '{' };

    if (!guard_init()) { printf("  (ガードページを作れない: SKIP)\n"); return; }
    TEST("A ガードページ: wx_parse_date を 1 文字ずつ切る・壊す(返す値は 2020〜2099 の範囲)");
    {
        static const char D0[] = " Wed, 25 Nov 2026 04:24:13 GMT";
        char   m[40];
        size_t n0 = sizeof D0 - 1u, n;
        uint32_t u = 0u;
        int bad = 0;
        for (len = 0u; len <= n0; len++) {
            for (at = 0; at < 2; at++) {
                bool ok = wx_parse_date(place(D0, len, at), len, &u);
                if (ok && len < n0) { bad++; }   /* GMT が欠けたら成功にしない */
            }
        }
        CHECK_EQ(bad, 0);
        bad = 0;
        for (i = 0u; i < 60000u; i++) {
            uint32_t k;
            memcpy(m, D0, n0); n = n0;
            for (k = 0u; k < 1u + rn(3u); k++) { m[rn((uint32_t)n)] = poison[rn(sizeof poison)]; }
            if ((rnd() & 3u) == 0u) { n = rn((uint32_t)n + 1u); }
            if (wx_parse_date(place(m, n, (int)(rnd() & 1u)), n, &u)) {
                wx_date_t d;
                wx_date_fields(u, &d);
                if (!(u >= 1577836800u && u <= 4102444799u && d.month >= 1u && d.month <= 12u && d.day >= 1u && d.day <= 31u && d.hour < 24u)) { bad++; }
            }
        }
        CHECK_EQ(bad, 0);
    }
}

/* ---------------------------------------------------------------- B 日付の全日検査 */
#define NDAYS (47482u)     /* 1970-01-01 から 2099-12-31 まで */
static uint16_t ry[NDAYS]; static uint8_t rm[NDAYS], rd[NDAYS], rw[NDAYS];

static unsigned dim_ref(unsigned y, unsigned m)
{
    static const unsigned MD[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int leap = ((y % 4u == 0u) && (y % 100u != 0u)) || (y % 400u == 0u);
    return (m == 2u && leap) ? 29u : MD[m - 1u];
}

static void t_dates(void)
{
    static const char *const WD[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const MN[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const uint32_t TOD[3] = { 0u, 45296u, 86399u };
    unsigned y = 1970u, m = 1u, d = 1u, w = 4u;
    uint32_t day, secs, j;
    unsigned t;
    wx_date_t f;
    char     b[48];
    uint32_t u;
    int      bad = 0, badj = 0, badp = 0;

    TEST("B1 日付の全日検査: 1970〜2099 の全 47482 日 x 3 時刻の wx_date_fields が、1 日ずつ数えた暦と一致。2020 以降は Date: の往復も一致");
    for (day = 0u; day < NDAYS; day++) {
        ry[day] = (uint16_t)y; rm[day] = (uint8_t)m; rd[day] = (uint8_t)d; rw[day] = (uint8_t)w;
        d++; w = (w + 1u) % 7u;
        if (d > dim_ref(y, m)) { d = 1u; m++; if (m > 12u) { m = 1u; y++; } }
    }
    CHECK_EQ(ry[NDAYS - 1u], 2099); CHECK_EQ(rm[NDAYS - 1u], 12); CHECK_EQ(rd[NDAYS - 1u], 31);
    for (day = 0u; day < NDAYS; day++) {
        for (t = 0u; t < 3u; t++) {
            secs = day * 86400u + TOD[t];
            wx_date_fields(secs, &f);
            if (f.year != ry[day] || f.month != rm[day] || f.day != rd[day] || f.wday != rw[day] || f.hour != TOD[t] / 3600u
                || f.min != (TOD[t] % 3600u) / 60u || f.sec != TOD[t] % 60u) { bad++; }
            if (ry[day] >= 2020u) {
                (void)snprintf(b, sizeof b, " %s, %02u %s %04u %02u:%02u:%02u GMT", WD[rw[day]], (unsigned)rd[day], MN[rm[day] - 1u], (unsigned)ry[day],
                               (unsigned)(TOD[t] / 3600u), (unsigned)((TOD[t] % 3600u) / 60u), (unsigned)(TOD[t] % 60u));
                if (!wx_parse_date(b, strlen(b), &u) || (u != secs)) { badp++; }
            }
            if (day + 1u < NDAYS) {                                /* JST(+9h)の繰り上がり */
                j = secs + 32400u;
                wx_date_fields(j, &f);
                {
                    uint32_t dj = j / 86400u;
                    if (f.year != ry[dj] || f.month != rm[dj] || f.day != rd[dj] || f.wday != rw[dj] || f.hour != (j % 86400u) / 3600u) { badj++; }
                }
            }
        }
    }
    CHECK_EQ(bad, 0); CHECK_EQ(badp, 0); CHECK_EQ(badj, 0);
    TEST("B2 存在しない日の拒否: 2020〜2099 の全月で 日 = 日数 + 1(2 月の 29 / 30 / 31 を含む)・日 = 0 を拒否、最終日は受理");
    {
        unsigned yy, mm, dd;
        int bad_r = 0, bad_a = 0;
        for (yy = 2020u; yy <= 2099u; yy++) {
            for (mm = 1u; mm <= 12u; mm++) {
                dd = dim_ref(yy, mm);
                (void)snprintf(b, sizeof b, " Mon, %02u %s %04u 12:00:00 GMT", dd, MN[mm - 1u], yy);
                if (!wx_parse_date(b, strlen(b), &u)) { bad_a++; }
                (void)snprintf(b, sizeof b, " Mon, %02u %s %04u 12:00:00 GMT", dd + 1u, MN[mm - 1u], yy);
                if (wx_parse_date(b, strlen(b), &u)) { bad_r++; }
                (void)snprintf(b, sizeof b, " Mon, 00 %s %04u 12:00:00 GMT", MN[mm - 1u], yy);
                if (wx_parse_date(b, strlen(b), &u)) { bad_r++; }
            }
        }
        CHECK_EQ(bad_a, 0); CHECK_EQ(bad_r, 0);
    }
}

/* ---------------------------------------------------------------- C ソフト時計 */
static void t_clock(void)
{
    wx_clock_t k;
    uint32_t   i, bs, first, base_ms, now, got = 0u;
    uint64_t   el;
    int        bad = 0, badm = 0;
    wx_date_t  d;

    TEST("C1 ソフト時計: 乱数 200000 回。基準時刻・基準 ms(32bit の端を含む)・経過(0〜40 日)で、64bit の計算と一致");
    for (i = 0u; i < 200000u; i++) {
        bs = 1700000000u + rn(2000000000u);
        first = (i & 1u) ? (0xFFFFFF00u + rn(512u)) : rnd();            /* 半分は 32bit の端 */
        base_ms = first - WX_SYNC_BIAS_MS;
        el = ((uint64_t)rnd() * 1u) % (40ull * 86400ull * 1000ull);
        now = (uint32_t)(base_ms + (uint32_t)el);
        wx_clock_init(&k);
        if (wx_clock_now(&k, now, &got)) { bad++; }                      /* 未同期 */
        if (wx_clock_min(&k, now) != 0xFFFFu) { badm++; }
        wx_clock_sync(&k, bs, first);
        if (!wx_clock_now(&k, now, &got) || (got != (uint32_t)(bs + (uint32_t)(el / 1000u)))) { bad++; }
        if (wx_clock_min(&k, now) != (uint16_t)((((uint64_t)bs + el / 1000u) % 86400u) / 60u)) { badm++; }
    }
    CHECK_EQ(bad, 0); CHECK_EQ(badm, 0);
    TEST("C2 日またぎ: 23:59:50 JST に同期して 9 秒後は 23:59、11 秒後は 翌日の 00:00(日・曜日が進む)。月末・年末も");
    {
        uint32_t u0;
        /* 2026-12-31 23:59:50 JST = UTC 14:59:50 */
        CHECK(wx_parse_date(" Thu, 31 Dec 2026 14:59:50 GMT", 30u, &u0));
        wx_clock_sync(&k, u0 + 32400u, 100000u + WX_SYNC_BIAS_MS);
        CHECK_EQ(wx_clock_min(&k, 100000u + 9000u), 23 * 60 + 59);
        CHECK_EQ(wx_clock_min(&k, 100000u + 11000u), 0);
        CHECK(wx_clock_now(&k, 100000u + 11000u, &got));
        wx_date_fields(got, &d);
        CHECK_EQ(d.year, 2027); CHECK_EQ(d.month, 1); CHECK_EQ(d.day, 1); CHECK_EQ(d.wday, 5);
        /* 2028-02-28 23:59:50 JST(うるう年)→ 2 月 29 日 */
        CHECK(wx_parse_date(" Mon, 28 Feb 2028 14:59:50 GMT", 30u, &u0));
        wx_clock_sync(&k, u0 + 32400u, 5000u + WX_SYNC_BIAS_MS);
        CHECK(wx_clock_now(&k, 5000u + 15000u, &got));
        wx_date_fields(got, &d);
        CHECK_EQ(d.month, 2); CHECK_EQ(d.day, 29); CHECK_EQ(d.hour, 0);
    }
}

/* ---------------------------------------------------------------- D 取得の予定表 */
/* -DAPP_WX_WEATHER=1 のビルドでは、周期に天気(取得 ID 3)が最初に加わる(設計 §13.6。取る順 3 → 0 → 1 → 2。天気の 429 は普通の失敗)。モデルは「取る順」と「周期のマスク」だけを差し替える */
#if APP_WX_WEATHER
#define M_ALL  (0x0Fu)
#define M_N    (4)
static const uint8_t M_ORDER[4] = { 3u, 0u, 1u, 2u };
#else
#define M_ALL  (WX_SYM_ALL)
#define M_N    (3)
static const uint8_t M_ORDER[3] = { 0u, 1u, 2u };
#endif
static int m_pos(uint8_t sym)                      /* 取る順の中の位置(範囲外は -1) */
{
    int i;
    for (i = 0; i < M_N; i++) { if (M_ORDER[i] == sym) { return i; } }
    return -1;
}
static uint8_t m_first(uint8_t mask)               /* mask の中で最初に取る取得 ID(空は 255) */
{
    int i;
    for (i = 0; i < M_N; i++) { if (((mask >> M_ORDER[i]) & 1u) != 0u) { return M_ORDER[i]; } }
    return 255u;
}
typedef struct { uint32_t t; uint8_t act, src, sym, ok; } evt_t;
#define MAXEV (60000)
static evt_t g_ev[2][MAXEV]; static int g_nev[2];
static int g_req_bad, g_time_bad, g_fetch_n, g_conn_n, g_timer_n, g_retry_n, g_retry_pass_n, g_429_n, g_req_ok_n, g_ign_n, g_busy_n, g_cyc_n, g_reconn_n, g_partial_n;

/* 3 銘柄の取得の予定表(design.md §5.7)を、乱数の成否・乱数の更新要求・乱数の所要時間で走らせ、独立のモデルと突き合わせる:
 *   - 動きの列(時刻・種類・銘柄・きっかけ)が、時刻の起点 0 と 0xFFFF0000(走行の途中で 32bit の ms が折り返す)で完全に一致
 *   - 取得の順序: 周期の始まりは銘柄 0・昇順、やり直しは直前に失敗した銘柄だけ・昇順・1 周期あたり最大 2 パス
 *   - 時間: 定期の周期は開始から 600,000ms、やり直しは直前のパスの終わりから 20,000ms、429 の後は 300,000ms、再接続の失敗の後は 60 / 120 / 300 秒(各 +20ms 以内)
 *   - 更新要求の返値(状態から決まる)、二重に始めない、始めていない動きの完了通知は無視
 *   - 周期の記録: ok と fail が重ならず、合わせて 7 */
static void sched_run(int idx, uint32_t off, uint32_t req_den, uint32_t hours)
{
    wx_sched_t s;
    uint32_t   t, rel, tick, done_at = 0u, last_c0 = 0u, last_done = 0u;
    uint32_t   total = hours * 3600u * 100u;   /* 10ms 刻み */
    wx_act_t   act = WX_ACT_NONE;
    uint8_t    plan_ok = 0u;
    uint16_t   plan_http = 0u;
    uint8_t    act_sym = 0u;
    uint32_t   cyc_seen = 0u;
    int        have_c0 = 0, req_since = 1;     /* 直前の完了のあとに、状態を変える更新要求があった(時間の規則を確かめない) */
    int        last_was_429 = 0, last_was_fetch = 0, conn_fail_run = 0, prev_conn_fail = 0, retry_pass = 0, in_pass_sym = -1;
    uint8_t    failed_model = 0u, todo_model = 0u;
    uint32_t   down_until = 0u;
    const uint32_t k_cw[3] = { 60000u, 120000u, 300000u };

    g_rs = 0xC0FFEEu;                          /* 2 回の走行で同じ乱数 */
    g_nev[idx] = 0;
    wx_sched_init(&s, true, off);
    for (tick = 0u; tick < total; tick++) {
        t   = off + tick * 10u;
        rel = tick * 10u;
        if (act != WX_ACT_NONE && (uint32_t)(t - done_at) < 0x80000000u) {          /* 実行中の動きの完了 */
            wx_act_t a = act;
            act = WX_ACT_NONE;
            wx_sched_done(&s, a, plan_ok != 0u, plan_http, t);
            last_done = rel;
            if (a == WX_ACT_FETCH) {
                uint8_t bit = (uint8_t)(1u << act_sym);
                last_was_fetch = 1; last_was_429 = (!plan_ok && plan_http == 429u && act_sym < (uint8_t)WX_NSYM); prev_conn_fail = 0;      /* 打ち切るのは株価の 429 だけ */
                todo_model = (uint8_t)(todo_model & (uint8_t)~bit);
                if (plan_ok) { failed_model = (uint8_t)(failed_model & (uint8_t)~bit); } else { failed_model = (uint8_t)(failed_model | bit); }
                if (last_was_429) { failed_model = (uint8_t)(failed_model | todo_model); todo_model = 0u; }          /* 打ち切り: 取っていない銘柄も失敗扱い */
                if (wx_sched_failed_mask(&s) != (uint8_t)(failed_model & WX_SYM_ALL)) { g_inv_bad++; }      /* partial 用は株価の 3 ビットだけ */
            } else {
                last_was_fetch = 0; last_was_429 = 0;
                if (plan_ok) { conn_fail_run = 0; prev_conn_fail = 0; } else { conn_fail_run++; prev_conn_fail = 1; }
            }
            req_since = 0;
            if (s.cyc.n != cyc_seen) {                                              /* 周期が終わった */
                cyc_seen = s.cyc.n; g_cyc_n++;
                if (((s.cyc.ok | s.cyc.fail) != WX_SYM_ALL) || ((s.cyc.ok & s.cyc.fail) != 0u) || s.cyc.fail != (uint8_t)(failed_model & WX_SYM_ALL)) { g_inv_bad++; }
                if (s.cyc.fail != 0u) { g_partial_n++; }
                if (s.cyc.ms != (t - s.t_fetch0)) { g_inv_bad++; }
            }
            if (g_nev[idx] < MAXEV) { g_ev[idx][g_nev[idx]].t = rel; g_ev[idx][g_nev[idx]].act = (uint8_t)(10u + (uint8_t)a); g_ev[idx][g_nev[idx]].src = 0u; g_ev[idx][g_nev[idx]].sym = act_sym; g_ev[idx][g_nev[idx]].ok = plan_ok; g_nev[idx]++; }
        }
        if (rn(req_den) == 0u) {                                                      /* 乱数の更新要求 */
            wx_src_t src = (rnd() & 1u) ? WX_SRC_TOUCH : WX_SRC_UART;
            uint8_t  st  = s.state, want = s.want, rate = s.rate;
            uint32_t gap = t - s.t_fetch0;
            wx_req_t r   = wx_sched_request(&s, src, t);
            switch (st) {
            case WX_S_OFF:   if (r != WX_REQ_OFF) { g_req_bad++; } break;
            case WX_S_IDLE:
                if (gap < WX_MIN_GAP_MS) { if (r != WX_REQ_IGNORED) { g_req_bad++; } g_ign_n++; }
                else { if (r != WX_REQ_OK) { g_req_bad++; } g_req_ok_n++; }
                break;
            case WX_S_RETRY:
                if (want != (uint8_t)WX_ACT_FETCH) { if (r != WX_REQ_OK) { g_req_bad++; } g_req_ok_n++; }
                else if (rate != 0u) { if (r != WX_REQ_IGNORED) { g_req_bad++; } g_ign_n++; }
                else { if (r != WX_REQ_BUSY) { g_req_bad++; } g_busy_n++; }
                break;
            default:         if (r != WX_REQ_BUSY) { g_req_bad++; } g_busy_n++; break;
            }
            if (r == WX_REQ_OK) { req_since = 1; }
            if (g_nev[idx] < MAXEV) { g_ev[idx][g_nev[idx]].t = rel; g_ev[idx][g_nev[idx]].act = (uint8_t)(20u + (uint8_t)r); g_ev[idx][g_nev[idx]].src = (uint8_t)src; g_ev[idx][g_nev[idx]].sym = 0u; g_ev[idx][g_nev[idx]].ok = 0u; g_nev[idx]++; }
        }
        if (rn(5000u) == 0u) {                                                        /* 始めていない動き・別の動きの完了通知は無視 */
            uint8_t  sv_state = s.state, sv_net = s.net, sv_started = s.started, sv_todo = s.todo, sv_failed = s.failed, sv_cf = s.conn_fail, sv_ok = s.okmask;
            uint32_t sv_n = s.cyc.n;
            wx_act_t wrong = (act == WX_ACT_FETCH) ? WX_ACT_CONNECT : WX_ACT_FETCH;
            wx_sched_done(&s, wrong, true, 200u, t);
            wx_sched_done(&s, WX_ACT_NONE, true, 200u, t);
            if (act == WX_ACT_NONE) { wx_sched_done(&s, WX_ACT_FETCH, true, 200u, t); }   /* 動きが無い時の完了は、どちらも無視 */
            if (s.state != sv_state || s.net != sv_net || s.started != sv_started || s.todo != sv_todo || s.failed != sv_failed || s.conn_fail != sv_cf || s.okmask != sv_ok || s.cyc.n != sv_n) { g_inv_bad++; }
        }
        {
            wx_act_t a = wx_sched_poll(&s, t);
            if (a != WX_ACT_NONE) {
                uint32_t x = rn(100u);
                if (act != WX_ACT_NONE) { g_inv_bad++; }                              /* 二重に始めない */
                act = a;
                done_at   = t + 500u + rn(7000u);
                plan_ok   = (x < 70u) ? 1u : 0u;
                plan_http = plan_ok ? 200u : ((x < 73u) ? 429u : ((x < 85u) ? 500u : 0u));
                if (a == WX_ACT_CONNECT) { plan_http = 0u; plan_ok = (x < 60u) ? 1u : 0u; }
                if (rn(150u) == 0u) { down_until = t + 30000u + rn(150000u); }                 /* 通信が落ちている間(応答なしの失敗が続く) */
                if ((int32_t)(down_until - t) > 0) { plan_ok = 0u; plan_http = 0u; }
                if (a == WX_ACT_FETCH) {
                    uint8_t  sym = wx_sched_sym(&s);
                    wx_src_t src = wx_sched_src(&s);
                    uint32_t gap_done = rel - last_done;
                    act_sym = sym;
                    g_fetch_n++;
                    if (m_pos(sym) < 0) { g_inv_bad++; }
                    if (src == WX_SRC_RETRY) {                                         /* やり直し: 直前に失敗した銘柄だけ・昇順・1 周期 2 パスまで */
                        if (gap_done >= 5000u) {                                       /* 新しいパス: 直前のパスの終わりから 20 秒 */
                            retry_pass++; g_retry_pass_n++; in_pass_sym = -1;
                            if (!last_was_fetch || gap_done < WX_RETRY_MS || gap_done > WX_RETRY_MS + 20u) { g_time_bad++; }
                            if (retry_pass > (int)WX_RETRY_MAX) { g_time_bad++; }
                            todo_model = failed_model;
                        } else if (!last_was_fetch || gap_done > 20u) { g_time_bad++; }  /* パスの途中は直ちに次の銘柄 */
                        if (m_pos(sym) <= in_pass_sym) { g_time_bad++; }               /* 取る順(昇順) */
                        if (!(todo_model & (uint8_t)(1u << sym)) || (m_first(todo_model) != sym)) { g_time_bad++; }   /* 残りの失敗の中で最初に取る取得 ID */
                        in_pass_sym = m_pos(sym);
                        g_retry_n++;
                    } else if (in_pass_sym >= 0 && last_was_fetch && !last_was_429 && gap_done <= 20u && m_pos(sym) == in_pass_sym + 1 && todo_model != 0u) {
                        in_pass_sym = m_pos(sym);                                      /* 周期の途中: 直ちに次の取得 */
                        if (!(todo_model & (uint8_t)(1u << sym))) { g_time_bad++; }
                    } else {                                                          /* 周期の始まり(常に銘柄 0 から) */
                        if (sym != M_ORDER[0]) { g_time_bad++; }                      /* 周期の最初の取得(天気ありは天気、なしは銘柄 0) */
                        if (!last_was_fetch) {                                         /* 接続が済んだら直ちに */
                            if (gap_done > 20u && !req_since) { g_time_bad++; }
                        } else if (last_was_429) {                                     /* 429 の後 300 秒 */
                            if (gap_done < WX_RATE_MS || gap_done > WX_RATE_MS + 20u) { g_time_bad++; }
                            g_429_n++;
                        } else if (src == WX_SRC_TIMER) {                              /* 定期: 前の周期の開始から 10 分 */
                            uint32_t d0 = rel - last_c0;
                            if (!have_c0 || d0 < WX_INTERVAL_MS || d0 > WX_INTERVAL_MS + 20u) { g_time_bad++; }
                            g_timer_n++;
                        } else if (src == WX_SRC_TOUCH || src == WX_SRC_UART) {        /* 更新要求: 前の周期の開始から 5 秒以上 */
                            if (!have_c0 || (rel - last_c0) < WX_MIN_GAP_MS) { g_time_bad++; }
                        } else {
                            g_time_bad++;                                              /* 起動のきっかけは接続の後だけ */
                        }
                        last_c0 = rel; have_c0 = 1; retry_pass = 0; in_pass_sym = 0;
                        failed_model = 0u; todo_model = (uint8_t)(M_ALL & (uint8_t)~(1u << M_ORDER[0]));
                        if (s.t_fetch0 != t || wx_sched_failed_mask(&s) != 0u) { g_inv_bad++; }
                    }
                } else {
                    g_conn_n++;
                    if (prev_conn_fail && !req_since) {
                        uint32_t want = k_cw[(conn_fail_run - 1 < 2) ? (conn_fail_run - 1) : 2], sd = rel - last_done;
                        if (sd < want || sd > want + 20u) { g_time_bad++; }
                    } else if (!prev_conn_fail && last_was_fetch && !req_since) {
                        g_reconn_n++;                                                  /* 全部失敗の後の再接続(直ちに。オフライン表示のまま) */
                        if (s.net != (uint8_t)WX_NET_ERR || (rel - last_done) > 20u) { g_time_bad++; }
                    }
                }
                if (g_nev[idx] < MAXEV) { g_ev[idx][g_nev[idx]].t = rel; g_ev[idx][g_nev[idx]].act = (uint8_t)a; g_ev[idx][g_nev[idx]].src = s.src; g_ev[idx][g_nev[idx]].sym = s.sym; g_ev[idx][g_nev[idx]].ok = 0u; g_nev[idx]++; }
            }
        }
        if (s.net > 3u || s.state > 5u || s.retry_n > WX_RETRY_MAX || (s.todo & ~M_ALL) != 0u || (s.failed & ~M_ALL) != 0u
            || (s.started != 0u && s.state != WX_S_CONNECT && s.state != WX_S_FETCH)) { g_inv_bad++; }
    }
}

static void t_sched(void)
{
    int i, same = 1;
    wx_sched_t o;

    TEST("D1 予定表(3 銘柄): 3 時間と 24 時間の乱数走行を、時刻の起点 0 と 0xFFFF0000(途中で ms が折り返す)で 2 回。動きの列が完全に一致、要求の返値・二重開始・範囲外の完了通知・周期の記録・取得の順序・時間の規則(10 分・5 秒・20 秒・300 秒・60/120/300 秒)");
    for (i = 0; i < 2; i++) {
        const uint32_t den = (i == 0) ? 2000u : 400000u;      /* 平均 20 秒に 1 回(要求の返値・無視・busy)/ 平均 4000 秒に 1 回(定期・やり直しの時間の規則) */
        const uint32_t hrs = (i == 0) ? 3u : 24u;
        int j;
        same = 1;
        sched_run(0, 0u, den, hrs);
        sched_run(1, 0xFFFF0000u, den, hrs);
        CHECK_EQ(g_nev[0], g_nev[1]);
        CHECK(g_nev[0] < MAXEV);
        for (j = 0; j < g_nev[0] && j < g_nev[1]; j++) {
            if (g_ev[0][j].t != g_ev[1][j].t || g_ev[0][j].act != g_ev[1][j].act || g_ev[0][j].src != g_ev[1][j].src || g_ev[0][j].sym != g_ev[1][j].sym) { same = 0; printf("  差: #%d t=%u/%u act=%u/%u\n", j, g_ev[0][j].t, g_ev[1][j].t, g_ev[0][j].act, g_ev[1][j].act); break; }
        }
        CHECK(same);
    }
    printf("  (参考) 取得 %d 回・接続 %d 回・周期 %d(失敗が残った %d)・定期 %d・やり直し %d 回(パス %d)・429 の後 %d・全部失敗の再接続 %d・要求 OK %d / 無視 %d / busy %d\n",
           g_fetch_n, g_conn_n, g_cyc_n, g_partial_n, g_timer_n, g_retry_n, g_retry_pass_n, g_429_n, g_reconn_n, g_req_ok_n, g_ign_n, g_busy_n);
    CHECK_EQ(g_req_bad, 0); CHECK_EQ(g_inv_bad, 0); CHECK_EQ(g_time_bad, 0);
    CHECK(g_timer_n > 50); CHECK(g_retry_n > 100); CHECK(g_retry_pass_n > 40); CHECK(g_429_n > 3); CHECK(g_ign_n > 0); CHECK(g_busy_n > 0); CHECK(g_partial_n > 20); CHECK(g_reconn_n > 0);
    TEST("D2 OFF: 何をしても ESP32 に触れる動きを返さない(net=off 固定)");
    wx_sched_init(&o, false, 0u);
    for (i = 0; i < 100000; i++) {
        CHECK_EQ(wx_sched_poll(&o, (uint32_t)i * 10u), WX_ACT_NONE);
        if ((i % 1000) == 0) { CHECK_EQ(wx_sched_request(&o, WX_SRC_UART, (uint32_t)i * 10u), WX_REQ_OFF); }
    }
    CHECK_EQ(wx_sched_net(&o), WX_NET_OFF); CHECK(!wx_sched_busy(&o));
}

/* ---------------------------------------------------------------- E wifi_core の WX step の乱数 */
static char g_log[200000]; static size_t g_logn;
static int g_sent_n, g_sent_unknown, g_busy_rate;
static char g_req_expect[3][256];
static unsigned g_req_len[3];
static unsigned g_sym_now;
static void op_en(int level) { (void)level; }
static int op_send(const char *data, size_t n)
{
    static const char *const OK[] = { "AT+CIPMUX=0\r\n", "AT+CIPCLOSE\r\n", "AT+CIPSTART=\"SSL\",\"query1.finance.yahoo.com\",443\r\n", "AT+CIPSEND=144\r\n", "AT+CIPSEND=145\r\n" };
    unsigned k; int known = 0;
    if (g_busy_rate > 0 && (int)rn(100u) < g_busy_rate) { return WIFI_SEND_BUSY; }
    for (k = 0u; k < 5u; k++) { if (n == strlen(OK[k]) && memcmp(data, OK[k], n) == 0) { known = 1; } }
    if ((n == g_req_len[g_sym_now]) && memcmp(data, g_req_expect[g_sym_now], n) == 0) { known = 1; }       /* 今の銘柄の要求だけ(他の銘柄の要求は送らない) */
    if (!known) { g_sent_unknown++; }
    g_sent_n++;
    return WIFI_SEND_OK;
}
static void op_log(const char *line, size_t n)
{
    if (g_logn + n < sizeof g_log) { memcpy(g_log + g_logn, line, n); g_logn += n; g_log[g_logn] = '\0'; }
}
static const wifi_ops_t k_ops = { op_en, op_send, op_log };
static wifi_core_t g_c;

static void feed(const char *s, size_t n, uint32_t now)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        wifi_core_rx(&g_c, (uint8_t)s[i], now);
        if (g_c.wx_len > WX_BUF_SIZE || g_c.ipd_left > 2048u || g_c.llen > WIFI_LINE_MAX) { g_inv_bad += 1000; }
    }
}

static void t_core_fuzz(void)
{
    static const char *const TOK[] = {
        "OK\r\n", "ERROR\r\n", "CONNECT\r\n", "0,CONNECT\r\n", "CLOSED\r\n", "0,CLOSED\r\n", ">", "> ", "SEND OK\r\n", "SEND FAIL\r\n", "DNS Fail\r\n", "ready\r\n",
        "+IPD,12:", "+IPD,1230,1.2.3.4,80:", "+IPD,3000:", "+IPD,:", "+IPD,0:", "+IPD,99999:", "+IPD,5", "+IPD", ":", "\r\n", "busy p...\r\n", "Recv 144 bytes\r\n",
        "SECRETBODY", "Date: Wed, 25 Nov 2026 04:24:13 GMT\r\n", "HTTP/1.0 200 OK\r\n", "\"regularMarketPrice\":2433.0,\"chartPreviousClose\":2378.0", "{\"chart\":{", "\x01\xFF\x7F"
    };
    unsigned it, iter_fail = 0u, nonzero = 0u, finished = 0u, completed_wx = 0u, ok_wx = 0u, reboot_wx = 0u;
    uint32_t t0, now;
    unsigned tick;
    char     tmp[300];
    unsigned k;

    TEST("E1 wifi_core WX step の乱数 3000 回(各 40 秒。銘柄 0 / 1 / 2 を順に): バッファ・データモードの範囲、固まらない、送信は既知のコマンドと今の銘柄の要求だけ、ログに SECRETBODY・Date:・regularMarket が出ない、結果の sym が選んだ銘柄、終了後に本文バッファが 0");
    for (k = 0u; k < 3u; k++) {
        g_req_len[k] = (unsigned)wx_build_request(g_req_expect[k], sizeof g_req_expect[k], (uint8_t)k);
        CHECK(g_req_len[k] >= 144u);
    }
    for (it = 0u; it < 3000u; it++) {
        uint32_t seq0;
        wifi_core_init(&g_c, &k_ops, "SSID_X", "PASS_X");
        wifi_core_set_quiet(&g_c, (it & 1u) != 0u);
        g_sym_now = it % 3u;
        wifi_core_set_symbol(&g_c, (uint8_t)g_sym_now);
        g_logn = 0u; g_log[0] = '\0';
        g_busy_rate = (it % 3u == 0u) ? 40 : 0;
        t0 = (it & 2u) ? (0xFFFFF000u + rn(0x800u)) : rn(100000u);
        CHECK(wifi_core_start(&g_c, WIFI_RANGE_WX, t0));
        seq0 = wifi_core_done_seq(&g_c);
        for (tick = 0u; tick < 4000u; tick++) {
            now = t0 + tick * 10u;
            wifi_core_poll(&g_c, now);
            if ((it % 4u) != 3u || tick < 5u) {
                /* 乱数のトークン列 */
                unsigned nt = rn(4u);
                for (k = 0u; k < nt; k++) {
                    const char *tk = TOK[rn(sizeof TOK / sizeof TOK[0])];
                    feed(tk, strlen(tk), now);
                    if ((rnd() & 7u) == 0u) { uint8_t b = (uint8_t)rnd(); feed((const char *)&b, 1u, now); }
                }
                if ((rnd() & 15u) == 0u) {              /* 長い本文(データモードの中) */
                    size_t n = 1u + rn(300u), q;
                    feed("+IPD,300:", 9u, now);
                    for (q = 0u; q < n; q++) { tmp[q % sizeof tmp] = (char)rnd(); }
                    feed(tmp, n < sizeof tmp ? n : sizeof tmp, now);
                }
            } else if (tick < 60u) {                    /* 「ほぼ正常な会話」: 本物に近い順序を、時々崩す */
                static const char *const SCR[] = { "OK\r\n", "OK\r\n", "CONNECT\r\nOK\r\n", "OK\r\n>" , "Recv 144 bytes\r\nSEND OK\r\n", "+IPD,200:" };
                if (tick % 8u == 3u) {
                    unsigned si = (tick / 8u) % 6u;
                    feed(SCR[si], strlen(SCR[si]), now);
                    if (si == 5u) { size_t q; for (q = 0u; q < 200u; q++) { tmp[q] = "SECRETBODY Date: regularMarket OK\r\nCLOSED\r\n"[q % 41u]; } feed(tmp, 200u, now); feed("CLOSED\r\n", 8u, now); }
                }
            }
            if (g_c.step == WIFI_STEP_NONE && wifi_core_state(&g_c) != WIFI_STATE_RUN) { break; }
        }
        if (wifi_core_state(&g_c) == WIFI_STATE_RUN) { iter_fail++; } else { finished++; }
        if (wifi_core_done_seq(&g_c) != seq0 + 1u) { iter_fail++; }
        if (wifi_core_wx_seq(&g_c) > 0u) {
            completed_wx++;
            if (wifi_core_wx(&g_c)->sym != g_sym_now) { iter_fail++; }                  /* 結果の銘柄 = 選んだ銘柄(失敗でも) */
            if (wifi_core_wx(&g_c)->ok != 0u) { ok_wx++; if (wifi_core_wx(&g_c)->err != 0u) { iter_fail++; } }
            if (wifi_core_wx(&g_c)->err == 9u) { reboot_wx++; }
        }
        { size_t q; for (q = 0u; q < WX_BUF_SIZE; q++) { if (g_c.wx_buf[q] != 0) { nonzero++; break; } } }
        if (strstr(g_log, "SECRETBODY") || strstr(g_log, "Date") || strstr(g_log, "regularMarket") || strstr(g_log, "SSID_X") || strstr(g_log, "PASS_X")) { iter_fail += 100u; }
    }
    CHECK_EQ(iter_fail, 0u); CHECK_EQ(nonzero, 0u); CHECK_EQ(g_inv_bad, 0); CHECK_EQ(g_sent_unknown, 0);
    CHECK_EQ(finished, 3000u);
    printf("  (参考) 取得の結果が出た走行 %u / 3000(成功 %u・ready による err=9 が %u)、送信 %d 回\n", completed_wx, ok_wx, reboot_wx, g_sent_n);
    CHECK(completed_wx > 100u);
    CHECK(reboot_wx > 0u);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    t_guard();
    t_dates();
    t_clock();
    t_sched();
    t_core_fuzz();
    TEST_REPORT("test_wx_indep");
}
