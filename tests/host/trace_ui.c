/* trace_ui.c - 実際の app_main.c(製品)を偽の周辺で動かし、design-uart.md §10.8 の UT10〜UT19 と同じ操作をしたときの
 * UART 出力(本物の applog_fmt_* が作る行)をファイルに書く。judge_ui.py の入力の元(= 設計の正規表現と製品の書式の突き合わせ)。
 *   使い方: trace_ui <出力フォルダ>   → utNN.log(受信そのまま、CRLF)と utNN.stamp.log(serial-read.ps1 -Stamp 形式)
 * 注意: 時刻は模擬(10ms の tick)。実機の遅延・割り込み・LCD は含まない。 */
#include <stdio.h>
#define main test_app_main_main
#include "test_app_main.c"
#undef main

#define MAXTL 4096
static unsigned    t_n;
static uint32_t    t_tick[MAXTL];
static char        t_txt[MAXTL][300];

static void trace_hook(const uint8_t *p, uint16_t len)
{
    if ((t_n < MAXTL) && (len < sizeof(t_txt[0]))) {
        t_tick[t_n] = s_tick;
        memcpy(t_txt[t_n], p, len);
        t_txt[t_n][len] = '\0';
        t_n++;
    }
}

static void stamp(char *o, uint32_t ms)
{
    uint32_t t = 12u * 3600000u + ms;
    sprintf(o, "%02u:%02u:%02u.%03u", (unsigned)(t / 3600000u % 24u), (unsigned)(t / 60000u % 60u), (unsigned)(t / 1000u % 60u), (unsigned)(t % 1000u));
}

/* 1 つの場面を走らせ、dir/name.log と dir/name.stamp.log を書く。rx の各文字は「# tx 'c'」として時刻つきで残す(serial-read.ps1 と同じ) */
static void emit_files(const char *dir, const char *name, const rx_ev_t *rx, unsigned n_rx)
{
    char path[512], st[32];
    FILE *f;
    unsigned i, k = 0u, j = 0u;

    snprintf(path, sizeof(path), "%s/%s.log", dir, name);
    f = fopen(path, "wb");
    if (f == NULL) { printf("  書けない: %s\n", path); g_tests_failed++; return; }
    fwrite(g_fs.tx, 1u, g_fs.tx_len, f);
    fclose(f);

    snprintf(path, sizeof(path), "%s/%s.stamp.log", dir, name);
    f = fopen(path, "wb");
    if (f == NULL) { g_tests_failed++; return; }
    /* 受信文字(tx マーカー)と装置の行を時刻順に混ぜる。同じ時刻なら tx マーカーが先 */
    while ((i = j) < t_n || k < n_rx) {
        uint32_t tl = (j < t_n) ? t_tick[j] : 0xFFFFFFFFu;
        uint32_t tr = (k < n_rx) ? rx[k].tick : 0xFFFFFFFFu;
        if (tr <= tl) {
            stamp(st, tr * 10u);
            fprintf(f, "%s # tx '%c'\r\n", st, (char)rx[k].byte);
            k++;
        } else {
            stamp(st, tl * 10u);
            fprintf(f, "%s %s", st, t_txt[i]);          /* 行は \r\n で終わっている */
            j++;
        }
    }
    fclose(f);
}

static void scenario(const char *dir, const char *name, const rx_ev_t *rx, unsigned n_rx,
                     const press_t *pr, unsigned n_pr, uint32_t stop_tick)
{
    cfg_default();
    C.rx = rx; C.n_rx = n_rx; C.press = pr; C.n_press = n_pr; C.stop_tick = stop_tick;
    t_n = 0u;
    g_send_hook_user = trace_hook;
    run_scenario();
    g_send_hook_user = NULL;
    emit_files(dir, name, rx, n_rx);
    printf("  %s: %u 行, %u バイト\n", name, t_n, (unsigned)g_fs.tx_len);
}

static void seq(rx_ev_t *o, unsigned *n, const char *chars, uint32_t at_tick, uint32_t gap_ticks)
{
    unsigned i;
    *n = 0u;
    for (i = 0u; chars[i] != '\0'; i++) { o[i].tick = at_tick + gap_ticks * i; o[i].byte = (uint8_t)chars[i]; (*n)++; }
}

static press_t tap(uint8_t btn, uint32_t t0)
{
    press_t p;
    p.t0 = t0; p.t1 = t0 + 5u;
    p.x = (uint16_t)(ui_btn_defs[btn].vis.x + ui_btn_defs[btn].vis.w / 2);
    p.y = (uint16_t)(ui_btn_defs[btn].vis.y + ui_btn_defs[btn].vis.h / 2);
    p.pts = 1u;
    return p;
}

int main(int argc, char **argv)
{
    static rx_ev_t rx[128];
    static press_t pr[8];
    unsigned n;
    const char *dir = (argc > 1) ? argv[1] : ".";

    /* tx の時刻は serial-read.ps1 の -TxAtSec 3 = 起動後 3 秒の相当(tick 300)。ジョブは「リセット済み・走行中」から受信を始めるが、
     * ここでは起動から記録する(UT18 のため [boot] から入る) */
    seq(rx, &n, "nnnnn", 300u, 70u);              scenario(dir, "ut10", rx, n, NULL, 0u, 1400u);
    seq(rx, &n, "ppppp", 300u, 70u);              scenario(dir, "ut11", rx, n, NULL, 0u, 1400u);
    seq(rx, &n, "++++++++--s", 300u, 50u);        scenario(dir, "ut12", rx, n, NULL, 0u, 1600u);
    seq(rx, &n, "d", 300u, 0u);                   scenario(dir, "ut13", rx, n, NULL, 0u, 4200u);
    seq(rx, &n, "dxxxxxxxd", 300u, 70u);          scenario(dir, "ut14", rx, n, NULL, 0u, 1600u);
    seq(rx, &n, "d", 300u, 0u);
    pr[0].t0 = 1400u; pr[0].t1 = 1405u; pr[0].x = 100u; pr[0].y = 100u; pr[0].pts = 1u;   /* デモ開始の 11 秒後(湯はりの途中) */
                                                  scenario(dir, "ut15", rx, n, pr, 1u, 3000u);
    seq(rx, &n, "nn", 300u, 70u);                 scenario(dir, "ut16", rx, n, NULL, 0u, 3000u);
    {
        unsigned i;
        n = 0u;
        for (i = 0u; i < 60u; i++) { rx[i].tick = 300u; rx[i].byte = '+'; n++; }
    }                                             scenario(dir, "ut17", rx, n, NULL, 0u, 1400u);
    n = 0u;                                       scenario(dir, "ut18", rx, n, NULL, 0u, 2000u);
    /* UT19: ホームで「＋」「湯はり」→ 湯はりで「ホーム」→ ホームの隅(ボタン外)を押す */
    n = 0u;
    pr[0] = tap(UI_BTN_H_UP, 400u);
    pr[1] = tap(UI_BTN_H_FILL, 600u);
    pr[2] = tap(UI_BTN_F_HOME, 900u);
    pr[3].t0 = 1200u; pr[3].t1 = 1205u; pr[3].x = 3u; pr[3].y = 120u; pr[3].pts = 1u;
    pr[4].t0 = 1300u; pr[4].t1 = 1305u; pr[4].x = 476u; pr[4].y = 120u; pr[4].pts = 1u;
                                                  scenario(dir, "ut19", rx, n, pr, 5u, 2000u);
    TEST_REPORT("trace_ui");
}
