/* tests/stubs/fake_sci.h - r_sci_rx の記録用の偽物(ホスト試験専用。製品ではない)。
 * 使い方: 実 platform.h と FIT の実ヘッダ(r_sci_rx_if.h / r_sci_rx_pinset.h)を通した状態で、
 *         テストの .c から1回だけ include する(関数の実体を定義する)。
 * 模している範囲(r_sci_rx.c を読んで合わせた。実機の動作の証明ではない):
 *   - R_SCI_Send: 送信中(tx_idle=false)は SCI_ERR_XCVR_BUSY(SCI_CFG_USE_CIRCULAR_BUFFER=0 の実装と同じ)。
 *                 length が送信バッファ(CH2=800)を超えると SCI_ERR_INSUFFICIENT_SPACE。成功したら中身を即コピー。
 *                 成功後は tx_idle=false になり、fake_sci_tx_done() を呼ぶまで次の Send は BUSY(auto_idle=true なら即 true)。
 *   - R_SCI_Receive: 受信FIFOが空なら SCI_ERR_INSUFFICIENT_DATA。
 * 模していない範囲: 割り込み、TEI、受信バッファあふれ(SCI_EVT_*)、実際の送信時間。 */
#ifndef FAKE_SCI_H
#define FAKE_SCI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
/* 注意: platform.h は呼び出し側(実 platform.h)が先に include 済みであること。tests/stubs/platform.h とは混ぜない */
#include "r_sci_rx_if.h"
#include "r_sci_rx_pinset.h"

#define FAKE_TX_BUFSIZ  (800u)      /* SCI_CFG_CH2_TX_BUFSIZ */
#define FAKE_LOG_MAX    (65536u)

typedef struct {
    /* R_SCI_Open */
    unsigned    open_calls;
    uint8_t     open_chan;
    sci_mode_t  open_mode;
    sci_cfg_t   open_cfg;
    bool        open_cb_set;
    sci_err_t   open_ret;               /* 既定 SCI_SUCCESS */
    unsigned    pinset_calls;           /* R_SCI_PinSet_SCI2 */
    unsigned    pinset_at_open;         /* Open 時点での pinset_calls(Open より前に呼ばれたか) */
    /* R_SCI_Send */
    unsigned    send_calls;
    unsigned    send_busy_returns;
    unsigned    send_ok;
    sci_err_t   send_force;             /* SCI_SUCCESS 以外ならそれを返す(状態は変えない) */
    bool        tx_idle;
    bool        auto_idle;              /* true: 成功後も tx_idle のまま(即座に送り終わる回線) */
    uint8_t     tx[FAKE_LOG_MAX];       /* 成功した Send のバイトを連結 */
    size_t      tx_len;
    uint16_t    send_len[1024];         /* 成功した Send の length */
    unsigned    nsend;
    /* R_SCI_Receive */
    uint8_t     rx[4096];
    size_t      rx_r, rx_w;
    unsigned    recv_calls;
    void      (*recv_hook)(void);       /* R_SCI_Receive の先頭で呼ぶ */
    void      (*send_hook)(const uint8_t *p, uint16_t len);   /* Send 成功時に呼ぶ(順序の記録用) */
} fake_sci_t;

static fake_sci_t g_fs;
static char       g_fs_handle;          /* sci_hdl_t の指す先(中身は使わない) */

static void fake_sci_reset(void)
{
    memset(&g_fs, 0, sizeof(g_fs));
    g_fs.open_ret = SCI_SUCCESS;
    g_fs.send_force = SCI_SUCCESS;
    g_fs.tx_idle  = true;
}
static void fake_sci_tx_done(void) { g_fs.tx_idle = true; }
static void fake_sci_rx_put(uint8_t c) { if (g_fs.rx_w < sizeof(g_fs.rx)) { g_fs.rx[g_fs.rx_w++] = c; } }

void R_SCI_PinSet_SCI2(void) { g_fs.pinset_calls++; }
void R_SCI_PinSet_SCI1(void) { g_fs.pinset_calls++; }

sci_err_t R_SCI_Open(uint8_t const chan, sci_mode_t const mode, sci_cfg_t * const p_cfg,
                     void (* const p_callback)(void *p_args), sci_hdl_t * const p_hdl)
{
    g_fs.open_calls++;
    g_fs.open_chan = chan;
    g_fs.open_mode = mode;
    g_fs.pinset_at_open = g_fs.pinset_calls;
    if (p_cfg != NULL) { g_fs.open_cfg = *p_cfg; }
    g_fs.open_cb_set = (p_callback != NULL);
    if (g_fs.open_ret != SCI_SUCCESS) { return g_fs.open_ret; }
    if (p_hdl != NULL) { *p_hdl = (sci_hdl_t)&g_fs_handle; }
    return SCI_SUCCESS;
}

sci_err_t R_SCI_Send(sci_hdl_t const hdl, uint8_t *p_src, uint16_t const length)
{
    g_fs.send_calls++;
    if ((hdl == NULL) || (p_src == NULL)) { return SCI_ERR_NULL_PTR; }
    if (g_fs.send_force != SCI_SUCCESS) { return g_fs.send_force; }
    if (!g_fs.tx_idle) { g_fs.send_busy_returns++; return SCI_ERR_XCVR_BUSY; }
    if (length > FAKE_TX_BUFSIZ) { return SCI_ERR_INSUFFICIENT_SPACE; }
    if ((g_fs.tx_len + length) <= FAKE_LOG_MAX) {
        memcpy(&g_fs.tx[g_fs.tx_len], p_src, length);
        g_fs.tx_len += length;
    }
    if (g_fs.nsend < 1024u) { g_fs.send_len[g_fs.nsend] = length; }
    g_fs.nsend++;
    g_fs.send_ok++;
    if (g_fs.send_hook != NULL) { g_fs.send_hook(p_src, length); }
    g_fs.tx_idle = g_fs.auto_idle;
    return SCI_SUCCESS;
}

sci_err_t R_SCI_Receive(sci_hdl_t const hdl, uint8_t *p_dst, uint16_t const length)
{
    g_fs.recv_calls++;
    if (g_fs.recv_hook != NULL) { g_fs.recv_hook(); }
    if ((hdl == NULL) || (p_dst == NULL)) { return SCI_ERR_NULL_PTR; }
    if ((g_fs.rx_w - g_fs.rx_r) < length) { return SCI_ERR_INSUFFICIENT_DATA; }
    memcpy(p_dst, &g_fs.rx[g_fs.rx_r], length);
    g_fs.rx_r += length;
    return SCI_SUCCESS;
}

#endif /* FAKE_SCI_H */
