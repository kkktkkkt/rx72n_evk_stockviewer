/* clut8_test.c - CLUT8 表示の実機実験(docs/design.md §19、UART は docs/design-uart.md §11)
 * APP_CLUT8_TEST = 0 の時は中身が消える。段階 0〜5 を、UART の n / p / d で進める(自動では進まない)。
 * 面A = 0x00800000、面B = 0x00822000(512 x 272、8bit)。パレットA / B は RAM の uint32_t[256]。 */
#include "board_pins.h"

#if APP_CLUT8_TEST

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "platform.h"
#include "r_cmt_rx_if.h"
#include "r_glcdc_rx_if.h"
#include "app_debug.h"
#include "app_logfmt.h"
#include "lcd_panel.h"
#include "clut8_pat.h"
#include "clut8_test.h"

#define C8_TICK_HZ        (100u)
#define C8_TICK_MS        (1000u / C8_TICK_HZ)     /* 10ms */
#define C8_HB_TICKS       (C8_TICK_HZ)             /* [hb] は1秒ごと */
#define C8_STEPS          (6u)
#define C8_VEN_WAIT_TICKS (10u)                    /* VEN = 0 を待つ最大(約 100ms) */
#define C8_BC_RETRY       (1000u)                  /* BufferChange の再試行の上限 */
#define C8_BENCH_N        (2000u)
#define C8_PH_END         (0xFFu)                  /* 段階の終わり(何もしない) */

#define C8_YELLOW         (0xFFFFFF00u)
#define C8_BLUE           (0xFF0000FFu)
#define C8_RED            (0xFFFF0000u)

typedef enum { FL_OK = 0, FL_BUSY, FL_FAIL } fl_t;

static const char k_help[] = "[help] n=next p=prev d=again s=state ?=help\r\n";

static volatile uint32_t s_tick;                   /* 10ms ごとに +1(CMT割り込み) */
volatile uint32_t g_c8_mde;                        /* OFSM.MDE[2:0](111b = リトルエンディアン)。gdb で見る */

static uint32_t s_pal_a[C8_PAL_N];                 /* パレットA */
static uint32_t s_pal_b[C8_PAL_N];                 /* パレットB(段階5では P1) */
static uint32_t s_pal_c[C8_PAL_N];                 /* 段階5の P2 */

static bool     s_lcd_ok;
static char     s_fb  = 'a';                       /* 最後に BufferChange へ渡した面 */
static char     s_pal = 'a';                       /* 最後に NoReflect へ渡したパレット */
static uint32_t s_rc;                              /* 出来事の中で最初に出た 0 でない glcdc_err_t */
static uint8_t  s_step;
static uint8_t  s_phase;                           /* 段階の中の進み具合 */
static uint32_t s_t0;                              /* 段階に入った時の s_tick */
static uint32_t s_n;
static uint32_t s_m;
static bool     s_plane_b;                         /* 段階3の負荷: 次に書く面 */

static char           s_line[APPLOG_LINE_MAX];
static applog_state_t s_st;

static void tick_callback(void *pdata)
{
    (void)pdata;
    s_tick++;
}

static uint32_t elapsed_ms(void)
{
    return (s_tick - s_t0) * C8_TICK_MS;
}

static uint8_t reg_sel(void)
{
    return (uint8_t)GLCDC.GR2CLUTINT.BIT.SEL;
}

static uint8_t reg_ven(void)
{
    return (GLCDC.GR2VEN.BIT.VEN != 0u) ? 1u : 0u;
}

static volatile uint32_t *plane(char id)
{
    return (volatile uint32_t *)(uintptr_t)((id == 'a') ? LCD_C8_FB_A_ADDR : LCD_C8_FB_B_ADDR);
}

/* ---- ログ ---- */
static void emit_line(size_t n)
{
    app_log_line(s_line, n);
}

static void ev(const char *name, uint32_t n, uint32_t m)
{
    emit_line(c8pat_fmt_clut(s_line, sizeof(s_line), s_step, name, s_fb, s_pal, reg_sel(), reg_ven(), s_rc, n, m,
                             g_app_dbg.uf));
}

static void log_state(const char *tag, bool with_range)
{
    s_st.tick            = g_app_dbg.tick;
    s_st.err_code        = g_app_dbg.err_code;
    s_st.err_detail      = g_app_dbg.err_detail;
    s_st.i2c_fail_total  = g_app_dbg.i2c_fail_total;
    s_st.drop            = app_log_drops();
    s_st.frames          = g_app_dbg.frames;
    s_st.uf              = g_app_dbg.uf;
    s_st.raw_x           = g_app_dbg.raw_x;
    s_st.raw_y           = g_app_dbg.raw_y;
    s_st.x_min           = g_app_dbg.x_min;
    s_st.x_max           = g_app_dbg.x_max;
    s_st.y_min           = g_app_dbg.y_min;
    s_st.y_max           = g_app_dbg.y_max;
    s_st.raw_points      = g_app_dbg.raw_points;
    s_st.scr             = g_app_dbg.scr;
    s_st.demo            = g_app_dbg.demo;
    s_st.i2c_fail_streak = g_app_dbg.i2c_fail_streak;
    s_st.found_count     = g_app_dbg.found_count;
    emit_line(applog_fmt_state(s_line, sizeof(s_line), tag, &s_st, with_range));
}

static void log_init(const char *stage, bool ok)
{
    emit_line(applog_fmt_init(s_line, sizeof(s_line), stage, ok,
                              ok ? 0u : g_app_dbg.err_code, ok ? 0u : g_app_dbg.err_detail));
}

/* ---- GLCDC の操作(設計 §19.3.3) ---- */
static void note_rc(glcdc_err_t e)
{
    if ((s_rc == 0u) && (e != GLCDC_SUCCESS)) {
        s_rc = (uint32_t)e;
    }
}

/* パレットを未使用のセットへ書いて SEL を切り替える(VEN は立てない)。常に start = 0、size = 256 */
static glcdc_err_t pal_set(uint32_t *pal, char id)
{
    glcdc_clut_cfg_t c;
    glcdc_err_t      e;

    c.enable = true;
    c.p_base = pal;
    c.start  = 0u;
    c.size   = (uint16_t)C8_PAL_N;
    e = R_GLCDC_ClutUpdate_NoReflect(GLCDC_FRAME_LAYER_2, &c);
    if (e == GLCDC_SUCCESS) {
        s_pal = id;
    }
    note_rc(e);
    return e;
}

/* 面を切り替える(VEN = 1。次の垂直同期で FLM2 と SEL が同時に反映される) */
static glcdc_err_t face_set(char id)
{
    glcdc_err_t e = R_GLCDC_BufferChange(GLCDC_FRAME_LAYER_2, (uint32_t const *)plane(id));

    if (e == GLCDC_SUCCESS) {
        s_fb = id;
        g_app_dbg.frames++;
    }
    note_rc(e);
    return e;
}

/* 面とパレットを同時に切り替える。VEN = 1 なら何もせず BUSY(NoReflect だけ成功する形を作らない) */
static fl_t flip(char face, uint32_t *pal, char pid)
{
    glcdc_err_t e = GLCDC_ERR_INVALID_UPDATE_TIMING;
    uint32_t    i;

    if (reg_ven() != 0u) {
        return FL_BUSY;
    }
    if (pal_set(pal, pid) != GLCDC_SUCCESS) {
        return FL_FAIL;
    }
    for (i = 0u; (i < C8_BC_RETRY) && (e == GLCDC_ERR_INVALID_UPDATE_TIMING); i++) {
        e = face_set(face);          /* 失敗したら BufferChange だけ再試行(NoReflect を呼び直さない) */
    }
    return (e == GLCDC_SUCCESS) ? FL_OK : FL_FAIL;
}

static bool wait_ven0(void)
{
    uint32_t t0 = s_tick;

    while (reg_ven() != 0u) {
        app_log_poll();
        if ((uint32_t)(s_tick - t0) > C8_VEN_WAIT_TICKS) {
            return false;
        }
    }
    return true;
}

/* VEN = 0 を待つ -> (面A、パレットA)へ flip -> VEN = 0 を待つ */
static bool settle(void)
{
    bool ok = wait_ven0();

    if (ok) {
        ok = (flip('a', s_pal_a, 'a') == FL_OK);
    }
    if (ok) {
        ok = wait_ven0();
    }
    return ok;
}

static void clut_write(uint8_t set, uint32_t v)
{
    if (set == 0u) {
        GLCDC.GR2CLUT0[2].LONG = v;
    } else {
        GLCDC.GR2CLUT1[2].LONG = v;
    }
}

/* 段階3の負荷: 面A・面B を交互に、全語を同じ値で書き戻す(内容は変えない) */
static void stress_pass(void)
{
    volatile uint32_t *p = plane(s_plane_b ? 'b' : 'a');
    uint32_t           i;

    s_plane_b = !s_plane_b;
    for (i = 0u; i < (C8_FB_BYTES / 4u); i++) {
        p[i] = p[i];
    }
}

/* ---- 段階 ---- */
static void step_enter(uint8_t step)
{
    s_step  = step;
    s_phase = 0u;
    s_n     = 0u;
    s_m     = 0u;
    s_rc    = 0u;
    g_app_dbg.scr = step;
    c8pat_pal(s_pal_b, true, step != 3u);            /* 段階3だけ マーカー赤(同じ絵)。ほかは緑 */
    if (step == 5u) {                                /* P1 = パレットA の idx 2 を黄、P2 = 青 */
        memcpy(s_pal_b, s_pal_a, sizeof(s_pal_a));
        memcpy(s_pal_c, s_pal_a, sizeof(s_pal_a));
        s_pal_b[2] = C8_YELLOW;
        s_pal_c[2] = C8_BLUE;
    }
    (void)settle();                                  /* 失敗は rc に残る */
    s_t0 = s_tick;
    ev("start", 0u, 0u);
}

static void end_step(void)
{
    ev("done", 0u, 0u);
    s_phase = C8_PH_END;
}

/* 段階1: 1000, 2000, ..., 6000 ms に flip(B, A, B, A, B, A)。7000 ms に done */
static void run1(uint32_t t)
{
    bool to_b = ((s_phase & 1u) == 0u);

    if (s_phase < 6u) {
        if (t < (1000u * ((uint32_t)s_phase + 1u))) {
            return;
        }
        s_rc = 0u;
        if ((to_b ? flip('b', s_pal_b, 'b') : flip('a', s_pal_a, 'a')) == FL_BUSY) {
            return;
        }
        s_phase++;
        ev("flip", s_phase, 0u);
    } else if ((s_phase == 6u) && (t >= 7000u)) {
        end_step();
    }
}

/* 段階2: 1000 NoReflect(B)だけ / 4000 BufferChange(B)/ 7000 NoReflect(A)/ 10000 BufferChange(A)/ 11000 done */
static void run2(uint32_t t)
{
    static const uint16_t k_at[4] = { 1000u, 4000u, 7000u, 10000u };

    if (s_phase < 4u) {
        if (t < k_at[s_phase]) {
            return;
        }
        s_rc = 0u;
        switch (s_phase) {
        case 0u:  (void)pal_set(s_pal_b, 'b'); break;
        case 1u:  (void)face_set('b');         break;
        case 2u:  (void)pal_set(s_pal_a, 'a'); break;
        default:  (void)face_set('a');         break;
        }
        ev(((s_phase & 1u) == 0u) ? "nr" : "commit", 0u, 0u);
        s_phase++;
    } else if ((s_phase == 4u) && (t >= 11000u)) {
        end_step();
    }
}

/* 段階3: 1000〜11000 ms は垂直同期ごとに flip + 負荷。11000 stress_end -> settle。12000 done */
static void run3(uint32_t t)
{
    bool to_b;
    fl_t r;

    if (s_phase == 0u) {
        if ((t >= 1000u) && (t < 11000u)) {
            to_b = ((s_n & 1u) == 0u);
            r = to_b ? flip('b', s_pal_b, 'b') : flip('a', s_pal_a, 'a');
            if (r == FL_OK) {
                s_n++;
            } else if (r == FL_FAIL) {
                s_m++;
            }
            stress_pass();
        } else if (t >= 11000u) {
            ev("stress_end", s_n, s_m);
            s_rc = 0u;
            (void)settle();
            s_phase = 1u;
        }
    } else if ((s_phase == 1u) && (t >= 12000u)) {
        end_step();
    }
}

/* 段階4: 1000 未使用セットの idx 2 を黄 / 4000 使用中セットを青 / 7000 両方赤 / 8000 NoReflect x2000 + settle / 9000 done */
static void run4(uint32_t t)
{
    static const uint16_t k_at[4] = { 1000u, 4000u, 7000u, 8000u };
    uint8_t  set;
    uint32_t t0, i;

    if (s_phase < 4u) {
        if (t < k_at[s_phase]) {
            return;
        }
        s_rc = 0u;
        switch (s_phase) {
        case 0u:
            set = (uint8_t)(1u - reg_sel());
            clut_write(set, C8_YELLOW);
            ev("wr_unused", set, 0u);
            break;
        case 1u:
            set = reg_sel();
            clut_write(set, C8_BLUE);
            ev("wr_inuse", set, 0u);
            break;
        case 2u:
            clut_write(0u, C8_RED);
            clut_write(1u, C8_RED);
            ev("wr_back", 0u, 0u);
            break;
        default:
            t0 = s_tick;
            for (i = 0u; i < C8_BENCH_N; i++) {
                (void)pal_set(s_pal_a, 'a');         /* 内容は同じなので絵は変わらない */
            }
            ev("bench", C8_BENCH_N, (s_tick - t0) * C8_TICK_MS);
            s_rc = 0u;
            (void)settle();
            break;
        }
        s_phase++;
    } else if ((s_phase == 4u) && (t >= 9000u)) {
        end_step();
    }
}

/* 段階5: 1000 NoReflect(P1)/ 1500 NoReflect(P2。VEN なしで2回目)/ 4500 settle / 5500 done */
static void run5(uint32_t t)
{
    static const uint16_t k_at[3] = { 1000u, 1500u, 4500u };

    if (s_phase < 3u) {
        if (t < k_at[s_phase]) {
            return;
        }
        s_rc = 0u;
        switch (s_phase) {
        case 0u:  (void)pal_set(s_pal_b, 'a'); ev("nr1", 0u, 0u); break;
        case 1u:  (void)pal_set(s_pal_c, 'a'); ev("nr2", 0u, 0u); break;
        default:  (void)settle();              ev("settle", 0u, 0u); break;
        }
        s_phase++;
    } else if ((s_phase == 3u) && (t >= 5500u)) {
        end_step();
    }
}

static void step_run(void)
{
    uint32_t t = elapsed_ms();

    switch (s_step) {
    case 1u:  run1(t); break;
    case 2u:  run2(t); break;
    case 3u:  run3(t); break;
    case 4u:  run4(t); break;
    case 5u:  run5(t); break;
    default:  break;                                 /* 段階0: 静止 */
    }
}

/* 受信コマンド(1 tick に1文字): n p d は段階の移動、s ? は応答。それ以外(+ - を含む)は無視 */
static void uart_command(void)
{
    int ch = app_log_getc();

    if (ch < 0) {
        return;
    }
    switch (applog_cmd_parse(ch)) {
    case APPLOG_CMD_NEXT:  if (s_lcd_ok) { step_enter((uint8_t)((s_step + 1u) % C8_STEPS)); } break;
    case APPLOG_CMD_PREV:  if (s_lcd_ok) { step_enter((uint8_t)((s_step + C8_STEPS - 1u) % C8_STEPS)); } break;
    case APPLOG_CMD_DEMO:  if (s_lcd_ok) { step_enter(s_step); } break;
    case APPLOG_CMD_STATE:
        ev("state", 0u, 0u);
        log_state("st", true);
        break;
    case APPLOG_CMD_HELP:
        app_log_line(k_help, sizeof(k_help) - 1u);
        break;
    default:
        break;
    }
}

void clut8_test_patch_cfg(glcdc_cfg_t *c)
{
    glcdc_input_cfg_t *in = &c->input[GLCDC_FRAME_LAYER_2];
    glcdc_clut_cfg_t  *cl = &c->clut[GLCDC_FRAME_LAYER_2];

    in->p_base = (uint32_t *)(uintptr_t)LCD_C8_FB_A_ADDR;
    in->format = GLCDC_IN_FORMAT_CLUT8;
    in->offset = (int32_t)LCD_C8_STRIDE;              /* hsize = 480、vsize = 272 のまま */
    cl->enable = true;
    cl->p_base = s_pal_a;
    cl->start  = 0u;
    cl->size   = (uint16_t)C8_PAL_N;
}

void clut8_test_main(void)
{
    uint32_t cmt_ch;
    uint32_t last = 0u;
    uint32_t last_hb = 0u;
    uint32_t now;

    g_c8_mde = (uint32_t)OFSM.MDE.BIT.MDE;
    app_debug_init();
    app_log_init();
    emit_line(applog_fmt_boot(s_line, sizeof(s_line), __DATE__ " " __TIME__,
                              (uint8_t)APP_LOG_SCI_CH, (uint32_t)APP_LOG_BAUD));

    /* 面とパレットは GLCDC を起動する前に作る(RAM2 は起動前に書ける) */
    c8pat_face((uint8_t *)(uintptr_t)LCD_C8_FB_A_ADDR, false);
    c8pat_face((uint8_t *)(uintptr_t)LCD_C8_FB_B_ADDR, true);
    c8pat_pal(s_pal_a, false, false);                /* パレットB は段階に入る時に作る */

    s_lcd_ok = lcd_init();
    if (s_lcd_ok) {
        (void)lcd_underflow_poll();                  /* 起動時の1回は捨てる(design.md §8.6) */
    }
    log_init("lcd", s_lcd_ok);

    if (!R_CMT_CreatePeriodic(C8_TICK_HZ, tick_callback, &cmt_ch)) {
        app_debug_set_err(APP_ERR_TIMER, 0u);
        log_init("timer", false);
        for (;;) {
            app_log_poll();
        }
    }
    log_init("timer", true);
    log_init("loop", true);
    if (s_lcd_ok) {
        step_enter(0u);
    }

    for (;;) {
        app_log_poll();
        now = s_tick;
        if (now != last) {                           /* 10ms ごと */
            last = now;
            g_app_dbg.tick = now;
            if (applog_due(now, last_hb, C8_HB_TICKS) != 0) {
                last_hb = now;
                log_state("hb", false);
            }
            uart_command();
            if (s_lcd_ok) {
                g_app_dbg.uf += lcd_underflow_poll();
            }
        }
        if (s_lcd_ok) {
            step_run();
        }
    }
}

#endif /* APP_CLUT8_TEST */
