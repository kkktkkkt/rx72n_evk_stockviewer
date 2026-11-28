/* test_app_debug.c - src/app/app_debug.c の UARTログ部(app_log_*)を、r_sci_rx の偽物(tests/stubs/fake_sci.h)で試験する。
 *   - 実 platform.h と FIT の実ヘッダを使い、app_debug.c をそのまま #include してコンパイルする(S6 と同じ流儀)。
 *   - 試験できるのは「アプリ側のキュー・再送・破棄・受信の読み方・コールバック」。実際の送信時間・割り込みは試験できない。
 * 作成: 2026-11-20 テスト担当 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "test_framework.h"

#include "../../src/app/app_debug.c"
#include "../stubs/fake_sci.h"

static void reset_all(void)
{
    fake_sci_reset();
    memset((void *)&g_app_dbg, 0, sizeof(g_app_dbg));
    s_log_tried = false;
    s_log_ready = false;
    s_log_hdl   = NULL;
    applog_q_init(&s_q);
    s_rx_ev_val = 0u; s_rx_ev_seq = 0u; s_rx_ev_seen = 0u;
}

/* i 番目の試験行: "L<i>-" + 'a'..  で n バイト(\r\n 終わり) */
static size_t mk(char *dst, unsigned i, size_t n)
{
    size_t k;
    for (k = 0u; k + 2u < n; k++) { dst[k] = (char)('a' + (int)((i + k) % 26u)); }
    dst[n - 2u] = '\r'; dst[n - 1u] = '\n';
    return n;
}

int main(void)
{
    char ln[APPLOG_LINE_MAX + 8];
    unsigned i;

    TEST("D1 初期化: PinSet が Open より前、引数(ch=2, 115200, 8N1, 優先度2, コールバックあり)");
    reset_all();
    app_log_init();
    CHECK_EQ(g_fs.open_calls, 1);
    CHECK_EQ(g_fs.pinset_calls, 1);
    CHECK_EQ(g_fs.pinset_at_open, 1);
    CHECK_EQ(g_fs.open_chan, 2);
    CHECK_EQ(g_fs.open_mode, SCI_MODE_ASYNC);
    CHECK_EQ(g_fs.open_cfg.async.baud_rate, 115200);
    CHECK_EQ(g_fs.open_cfg.async.data_size, SCI_DATA_8BIT);
    CHECK_EQ(g_fs.open_cfg.async.parity_en, SCI_PARITY_OFF);
    CHECK_EQ(g_fs.open_cfg.async.stop_bits, SCI_STOPBITS_1);
    CHECK_EQ(g_fs.open_cfg.async.clk_src, SCI_CLK_INT);
    CHECK_EQ(g_fs.open_cfg.async.int_priority, 2);          /* 設計 §4.4: 3 → 2 */
    CHECK(g_fs.open_cb_set);
    app_log_init();                                          /* 2回目は何もしない */
    CHECK_EQ(g_fs.open_calls, 1);
    CHECK_EQ(g_fs.pinset_calls, 1);
    CHECK_EQ(APP_LOG_ENABLE, 1);                             /* 既定で有効 */

    TEST("D2 init 前に app_log_line を呼んでも自動で init される");
    reset_all();
    mk(ln, 0u, 20u);
    app_log_line(ln, 20u);
    CHECK_EQ(g_fs.open_calls, 1);
    CHECK_EQ(g_fs.nsend, 1);
    CHECK_EQ(g_fs.tx_len, 20);
    CHECK(memcmp(g_fs.tx, ln, 20u) == 0);

    TEST("D3 Open 失敗: 無音で続行。drop は数えない。再度 Open しない");
    reset_all();
    g_fs.open_ret = SCI_ERR_BAD_CHAN;
    app_log_init();
    CHECK(!s_log_ready);
    mk(ln, 1u, 30u);
    app_log_line(ln, 30u);
    app_log_line(ln, 0u);
    app_log_poll();
    CHECK_EQ(g_fs.send_calls, 0);
    CHECK_EQ(app_log_drops(), 0);
    CHECK_EQ(app_log_getc(), -1);
    CHECK_EQ(g_fs.recv_calls, 0);
    CHECK_EQ(g_fs.open_calls, 1);
    app_log_line(ln, 30u);
    CHECK_EQ(g_fs.open_calls, 1);

    TEST("D4 init 前の poll / 空キューの poll は R_SCI_Send を呼ばない");
    reset_all();
    app_log_poll();
    CHECK_EQ(g_fs.send_calls, 0);
    app_log_init();
    for (i = 0u; i < 10u; i++) { app_log_poll(); }
    CHECK_EQ(g_fs.send_calls, 0);

    TEST("D5 送信中(BUSY)の保持・満杯での破棄・再送の順序");
    reset_all();
    app_log_init();
    {
        char all[16][APPLOG_LINE_MAX];
        size_t lens[16];
        size_t total = 0u;
        /* 行0: 即送信(以後 tx_idle=false)。行1〜8: キューに8行まで保持。行9: 満杯で破棄 */
        for (i = 0u; i < 10u; i++) {
            lens[i] = 20u + i * 7u;
            mk(all[i], i, lens[i]);
            app_log_line(all[i], lens[i]);
        }
        CHECK_EQ(g_fs.nsend, 1);                              /* 行0だけ送信済み */
        CHECK_EQ(g_fs.tx_len, lens[0]);
        CHECK_EQ(applog_q_count(&s_q), 8);                    /* 行1〜8を保持 */
        CHECK_EQ(app_log_drops(), 1);                         /* 行9を破棄(新しい行を捨てる) */
        /* BUSY の間は何度 poll しても減らない・送らない・捨てない */
        for (i = 0u; i < 1000u; i++) { app_log_poll(); }
        CHECK_EQ(g_fs.nsend, 1);
        CHECK_EQ(applog_q_count(&s_q), 8);
        CHECK_EQ(app_log_drops(), 1);
        CHECK(g_fs.send_busy_returns >= 1000u);
        /* 送信完了のたびに先頭から順に送られる */
        for (i = 1u; i <= 8u; i++) {
            fake_sci_tx_done();
            app_log_poll();
            CHECK_EQ(g_fs.nsend, i + 1u);
            CHECK_EQ(g_fs.send_len[i], lens[i]);
        }
        CHECK_EQ(applog_q_count(&s_q), 0);
        for (i = 0u; i <= 8u; i++) { total += lens[i]; }
        CHECK_EQ(g_fs.tx_len, total);
        {   /* 内容が順序どおり・破損なし */
            size_t off = 0u; int same = 1;
            for (i = 0u; i <= 8u; i++) { if (memcmp(&g_fs.tx[off], all[i], lens[i]) != 0) { same = 0; } off += lens[i]; }
            CHECK(same);
        }
        CHECK_EQ(app_log_drops(), 1);                         /* 送信自体の失敗は無いので増えない */
        /* 空いた後は新しい行がすぐ送られる */
        fake_sci_tx_done();
        app_log_line(all[9], lens[9]);
        CHECK_EQ(g_fs.nsend, 10);
        CHECK_EQ(g_fs.send_len[9], lens[9]);
    }

    TEST("D6 BUSY で待っている行の中身が、後続の push・poll で壊れない");
    reset_all();
    app_log_init();
    {
        char a[40], b[60], c[50];
        mk(a, 0u, 40u); mk(b, 5u, 60u); mk(c, 9u, 50u);
        app_log_line(a, 40u);          /* 送信中になる */
        app_log_line(b, 60u);          /* 待ち */
        memset(b, 'Q', sizeof(b));     /* 呼び出し側のバッファを破壊しても、キュー内は影響を受けない */
        app_log_line(c, 50u);
        fake_sci_tx_done(); app_log_poll();
        fake_sci_tx_done(); app_log_poll();
        {
            char eb[60];
            mk(eb, 5u, 60u);
            CHECK_EQ(g_fs.send_len[1], 60);
            CHECK(memcmp(&g_fs.tx[40], eb, 60u) == 0);
        }
    }

    TEST("D7 BUSY 以外の送信エラー: その行を捨てて drop+1、次の行へ進む");
    reset_all();
    app_log_init();
    {
        char a[30], b[31];
        mk(a, 0u, 30u); mk(b, 1u, 31u);
        g_fs.send_force = SCI_ERR_INSUFFICIENT_SPACE;
        app_log_line(a, 30u);                                  /* push → poll → エラー → 捨てる */
        CHECK_EQ(applog_q_count(&s_q), 0);
        CHECK_EQ(app_log_drops(), 1);
        CHECK_EQ(g_fs.tx_len, 0);
        g_fs.send_force = SCI_ERR_NULL_PTR;
        app_log_line(a, 30u);
        CHECK_EQ(app_log_drops(), 2);
        g_fs.send_force = SCI_SUCCESS;                         /* 回復したら普通に送れる */
        app_log_line(b, 31u);
        CHECK_EQ(g_fs.tx_len, 31);
        CHECK_EQ(app_log_drops(), 2);
        /* BUSY を返す間は数えない */
        g_fs.tx_idle = true; g_fs.send_force = SCI_ERR_XCVR_BUSY;
        app_log_line(a, 30u);
        CHECK_EQ(app_log_drops(), 2);
        CHECK_EQ(applog_q_count(&s_q), 1);
        g_fs.send_force = SCI_SUCCESS;
        app_log_poll();
        CHECK_EQ(applog_q_count(&s_q), 0);
        CHECK_EQ(app_log_drops(), 2);
    }
    {   /* 長すぎる行は R_SCI_Send に渡らない前に push で拒否される(上限 255) */
        reset_all();
        app_log_init();
        mk(ln, 3u, APPLOG_LINE_MAX - 1u);
        app_log_line(ln, APPLOG_LINE_MAX - 1u);
        CHECK_EQ(g_fs.nsend, 1);
        CHECK_EQ(g_fs.send_len[0], APPLOG_LINE_MAX - 1u);      /* 255 バイトはそのまま通る */
        fake_sci_tx_done();
        app_log_line(ln, APPLOG_LINE_MAX);
        CHECK_EQ(g_fs.nsend, 1);
        CHECK_EQ(app_log_drops(), 1);
        app_log_line(ln, 0u);                                  /* 整形失敗(n==0)も drop */
        CHECK_EQ(app_log_drops(), 2);
        app_log_line(NULL, 10u);
        CHECK_EQ(app_log_drops(), 3);
        app_log_line(ln, (size_t)-1);
        CHECK_EQ(app_log_drops(), 4);
        CHECK_EQ(g_fs.nsend, 1);
    }

    TEST("D8 多数周期: 1万行を送受(毎回 tx_done)しても順序・内容・件数が保たれる(キューの循環)");
    reset_all();
    g_fs.auto_idle = false;
    app_log_init();
    {
        unsigned k, bad = 0u;
        size_t off = 0u;
        for (k = 0u; k < 10000u; k++) {
            size_t n = 3u + (size_t)((k * 7u) % (APPLOG_LINE_MAX - 4u));
            mk(ln, k, n);
            fake_sci_tx_done();
            g_fs.tx_len = 0u;                                  /* 1行ごとに検査して先頭へ戻す */
            app_log_line(ln, n);
            if ((g_fs.tx_len != n) || (memcmp(g_fs.tx, ln, n) != 0)) { bad++; }
            (void)off;
        }
        CHECK_EQ(bad, 0);
        CHECK_EQ(app_log_drops(), 0);
        CHECK_EQ(applog_q_count(&s_q), 0);
    }

    TEST("D9 app_log_getc: 0〜255 を非負で返す(0xFF が -1 にならない)、空は -1");
    reset_all();
    app_log_init();
    CHECK_EQ(app_log_getc(), -1);
    {
        static const uint8_t vals[] = { 0x00u, 0x01u, 'n', 'p', 's', '?', 0x0Au, 0x0Du, 0x7Fu, 0x80u, 0xFEu, 0xFFu };
        size_t k;
        for (k = 0u; k < sizeof(vals); k++) { fake_sci_rx_put(vals[k]); }
        for (k = 0u; k < sizeof(vals); k++) { CHECK_EQ(app_log_getc(), (int)vals[k]); }
        CHECK_EQ(app_log_getc(), -1);
    }
    /* R_SCI_Receive が INSUFFICIENT_DATA 以外のエラーでも -1 */
    reset_all(); app_log_init();
    g_fs.rx_w = 0u;
    CHECK_EQ(app_log_getc(), -1);
    CHECK_EQ(g_fs.recv_calls, 1);

    TEST("D10 コールバック: 異常イベントの値だけを書く(受信イベント値の確認つき)");
    reset_all();
    app_log_init();
    CHECK_EQ(SCI_EVT_RXBUF_OVFL, 2);                            /* 設計 §2.4 の数値の裏取り(r_sci_rx_if.h) */
    CHECK_EQ(SCI_EVT_FRAMING_ERR, 3);
    CHECK_EQ(SCI_EVT_PARITY_ERR, 4);
    CHECK_EQ(SCI_EVT_OVFL_ERR, 8);
    CHECK_EQ(app_log_rx_event(), 0);                            /* 何も無ければ 0 */
    {
        sci_cb_args_t a;
        sci_cb_evt_t ign[] = { SCI_EVT_TEI, SCI_EVT_RX_CHAR, SCI_EVT_RX_CHAR_MATCH, SCI_EVT_XFER_DONE, SCI_EVT_XFER_ABORTED,
                               SCI_EVT_RX_SYNC_DONE, SCI_EVT_RX_DONE };
        sci_cb_evt_t err[] = { SCI_EVT_RXBUF_OVFL, SCI_EVT_FRAMING_ERR, SCI_EVT_PARITY_ERR, SCI_EVT_OVFL_ERR };
        unsigned k;
        memset(&a, 0, sizeof(a));
        for (k = 0u; k < sizeof(ign) / sizeof(ign[0]); k++) { a.event = ign[k]; log_cb(&a); }
        CHECK_EQ(app_log_rx_event(), 0);                        /* 無視するイベントでは立たない */
        log_cb(NULL);                                           /* NULL でも落ちない */
        CHECK_EQ(app_log_rx_event(), 0);
        for (k = 0u; k < sizeof(err) / sizeof(err[0]); k++) {
            a.event = err[k]; log_cb(&a);
            CHECK_EQ(app_log_rx_event(), (uint32_t)err[k]);     /* 値が返る */
            CHECK_EQ(app_log_rx_event(), 0);                    /* 読むと既読(2回目は 0) */
        }
        /* 読む前に2回起きたら、最新の値が1回だけ返る */
        a.event = SCI_EVT_FRAMING_ERR; log_cb(&a);
        a.event = SCI_EVT_OVFL_ERR;    log_cb(&a);
        CHECK_EQ(app_log_rx_event(), 8);
        CHECK_EQ(app_log_rx_event(), 0);
        /* 同じ値が連続しても検出できる(seq で見ている) */
        a.event = SCI_EVT_OVFL_ERR; log_cb(&a);
        CHECK_EQ(app_log_rx_event(), 8);
        a.event = SCI_EVT_OVFL_ERR; log_cb(&a);
        CHECK_EQ(app_log_rx_event(), 8);
        /* seq が 32bit で一周しても検出できる */
        s_rx_ev_seq = 0xFFFFFFFFu; s_rx_ev_seen = 0xFFFFFFFFu;
        a.event = SCI_EVT_PARITY_ERR; log_cb(&a);
        CHECK_EQ(s_rx_ev_seq, 0);
        CHECK_EQ(app_log_rx_event(), 4);
    }
    CHECK_EQ(g_app_dbg.err_code, 0);                            /* g_app_dbg.err_code には書かない(設計 §2.4) */

    TEST("D11 drop 累計の飽和(app_log_drops)");
    reset_all();
    app_log_init();
    s_q.drops = 0xFFFFFFFEu;
    app_log_line(ln, 0u);  CHECK_EQ(app_log_drops(), 0xFFFFFFFFu);
    app_log_line(ln, 0u);  CHECK_EQ(app_log_drops(), 0xFFFFFFFFu);

    TEST_REPORT("test_app_debug");
}
