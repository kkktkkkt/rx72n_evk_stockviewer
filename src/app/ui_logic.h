/* ui_logic.h - 製品デモの状態機械(標準Cのみ。ホストでテストする。docs/design.md v2 §3〜§6, §9)
 * 入力: 10ms ごとのタッチ標本(NULL = 標本なし)、UART コマンド。出力: 状態、表示キー、イベント(UART の [ev] 用)。
 * 時刻はすべて呼び出し側が渡す ms(10ms 単位でよい)。ハードに触らない。 */
#ifndef UI_LOGIC_H
#define UI_LOGIC_H

#include <stdint.h>
#include <stdbool.h>
#include "nav_logic.h"      /* nav_sample_t */
#include "ui_assets.h"

/* ---- 時間・範囲(design §3〜§6) ---- */
#define UI_SET_MIN         (35u)
#define UI_SET_MAX         (48u)
#define UI_SET_INIT        (42u)
#define UI_BATH_INIT_X10   (400u)    /* 浴槽温度の初期値 / 湯はり完了後 */
#define UI_REHEAT_START_X10 (380u)
#define UI_TOUCH_DOWN_N    (2u)      /* 押下確定の連続標本数 */
#define UI_TOUCH_UP_N      (3u)      /* 離し確定の連続標本数 */
#define UI_PRESS_MIN_MS    (90u)     /* 押下から実行までの最短 */
#define UI_HOLD_MAX_MS     (3000u)   /* 押しっぱなしの強制実行 */
#define UI_FLASH_MS        (60u)
#define UI_TR_MS           (250u)    /* スライド */
#define UI_VT_HOLD_MS      (150u)    /* 仮想タッチを押している時間 */
#define UI_SPIN_MS         (150u)    /* 更新アイコンが取得中に 90 度回る間隔(design §22.8) */
#define UI_BAR_END_MS      (640u)    /* 使用量の棒が伸び終わるまで(40ms x 6 + 400) */
#define UI_DEMO_END_MS     (33000u)
#define UI_DEMO_SPEED      (4u)
#define UI_EV_Q            (8u)

/* タッチ座標の変換 flags(board_pins.h の TOUCH_SWAP_XY / FLIP_X / FLIP_Y) */
#define UI_TF_SWAP_XY      (1u)
#define UI_TF_FLIP_X       (2u)
#define UI_TF_FLIP_Y       (4u)

typedef enum { UI_SRC_TOUCH = 0, UI_SRC_UART, UI_SRC_DEMO } ui_src_t;
typedef enum { UI_PH_NONE = 0, UI_PH_PRESS, UI_PH_FLASH } ui_phase_t;
typedef enum { UI_CMD_NEXT = 0, UI_CMD_PREV, UI_CMD_PLUS, UI_CMD_MINUS, UI_CMD_DEMO, UI_CMD_WX } ui_cmd_t;

typedef enum { UI_EV_NONE = 0, UI_EV_SCR, UI_EV_BTN_DOWN, UI_EV_BTN_ACT, UI_EV_SET, UI_EV_DONE, UI_EV_DEMO } ui_ev_type_t;
enum { UI_DONE_FILL = 0, UI_DONE_REHEAT = 1 };
enum { UI_DEMO_START = 0, UI_DEMO_END = 1, UI_DEMO_ABORT = 2 };

/* イベント。SCR: a = 行き先, b = 元の画面。BTN_*: a = ボタン番号。SET: a = 設定温度。DONE / DEMO: a = 種類 */
typedef struct { uint8_t type, a, b, src; } ui_ev_t;

/* Wi-Fi・天気・日付の表示用の値(design §22.8)。取得そのものは別(wx_core)。ui_set_wx() で渡すか、gdb / ホスト試験が s_ui.wx に直接書く。
 * 描画(ui_render)はどの値でも安全に描く(範囲外は丸める)。パディングを作らない(表示キーを memcmp で比べるため。rsv は 0) */
enum { UI_NET_OFF = 0, UI_NET_CONN, UI_NET_ON, UI_NET_ERR };      /* Wi-Fi アイコンの 4 状態(スプライトの順) */
typedef struct {
    uint8_t  net;           /* UI_NET_* */
    uint8_t  busy;          /* 1 = 接続中・取得中(更新アイコンが回る) */
    uint8_t  have;          /* 1 = 気温・天気を一度でも取得した(0 の間は気温 --.-・アイコン「不明」・更新時刻 --:--) */
    uint8_t  kind;          /* 天気アイコン 0..7(sun partly cloud fog rain snow thunder unknown。wx_core.h の wx_kind_t) */
    uint8_t  stale;         /* 1 = 30 分を超えて古い(気温を灰色にする) */
    uint8_t  clk_ext;       /* 1 = 時計・日付は外部(wx)の値。0 = 旧デモ時計(21:45 起点。日付 11月21日(土)) */
    uint8_t  month, day, wday;   /* 日付(clk_ext = 1 の時。month = 0 は未同期 = 空白)。wday 0 = 日曜 */
    uint8_t  rsv;
    int16_t  temp_x10;      /* 気温(0.1℃) */
    uint16_t upd_min;       /* 最後の成功の時刻(分 0..1439。0xFFFF = なし) */
    uint16_t clock_min;     /* 外部の時計(分 0..1439。0xFFFF = 未同期。clk_ext = 1 の時) */
} ui_wx_t;

/* 1画面ぶんの表示に使う値(遷移中に古い画面を止めて描くための写しにも使う) */
typedef struct {
    uint8_t  scr;
    uint8_t  set;           /* 設定温度(℃) */
    uint16_t bath_x10;      /* 浴槽温度(0.1℃) */
    uint16_t fill_x10;      /* 水位(0.1%) */
    uint16_t flow_x10;      /* 流量(0.1 L/分) */
    uint16_t clock_min;     /* 時計(0..1439 分) */
    uint16_t bar_ms;        /* 使用量の棒の経過 ms(UI_BAR_END_MS で頭打ち) */
} ui_vals_t;

/* 表示キー(§7.4): 画面に描かれる内容だけを写したもの。前回描画した時とバイト比較する */
typedef struct { uint8_t scr; uint16_t w[3]; } ui_kv_t;
typedef struct {
    ui_kv_t  cur, old;
    ui_wx_t  wx;
    uint16_t dx;
    uint8_t  tr_on, tr_fwd, fb_btn, fb_phase, demo, spin;
} ui_key_t;

typedef struct {
    ui_vals_t v;                    /* 今の画面の値 */
    ui_vals_t ov;                   /* 遷移中の古い画面の値(動き始めの写し) */
    uint32_t  enter_ms;             /* 今の画面に入った時刻 */
    uint32_t  tr_start;
    uint16_t  tr_dx;                /* スライド量 0..480 */
    uint8_t   tr_on, tr_fwd;        /* 遷移中か / 前へ進む(右から入る)か */
    uint8_t   speed, demo;          /* 湯はり・追い焚きの倍率(1 / 4)、デモ中 */
    ui_wx_t   wx;                   /* Wi-Fi・天気・日付(画面に依らない。古い画面もスライド中は今の値で描く) */
    uint8_t   spin;                 /* 更新アイコンの回転 0..3(wx.busy の間だけ UI_SPIN_MS ごとに進む) */
    uint8_t   fill_sent, reheat_sent;
    /* 押下表示 */
    uint8_t   fb_btn, fb_phase, fb_src, fb_rel;
    uint32_t  fb_t_down, fb_t_flash;
    /* 実タッチ */
    uint8_t   ts, tcnt, flags;
    uint16_t  x_max, y_max;
    /* 仮想タッチ(デモ・UART の + -) */
    uint8_t   vt_on, vt_src;
    uint32_t  vt_rel_at;
    /* デモ */
    uint32_t  demo_t0;
    uint8_t   demo_i;
    /* イベント待ち行列 */
    ui_ev_t   ev[UI_EV_Q];
    uint8_t   ev_h, ev_t, ev_n;
    uint16_t  ev_lost;
    /* 表示キー */
    ui_key_t  key, drawn;
    uint8_t   drawn_valid;
} ui_t;

/* x_max / y_max = タッチ生座標の X / Y 軸の最大値。flags = UI_TF_* */
void ui_init(ui_t *ui, uint32_t now_ms, uint16_t x_max, uint16_t y_max, uint8_t flags);

/* 10ms ごとに呼ぶ。real = タッチ標本(NULL = 標本なし。状態を変えない) */
void ui_update(ui_t *ui, const nav_sample_t *real, uint32_t now_ms);

/* Wi-Fi・天気・日付の表示値を渡す(毎周期でよい。変わった時だけ再描画される) */
void ui_set_wx(ui_t *ui, const ui_wx_t *wx);

/* UART コマンド(n p + - d x)。受け付けない状況では何もしない。UI_CMD_WX = 更新ボタン h_wx の仮想タップ(ホームだけ。取得は app_main が BTN_ACT を見て行う) */
void ui_cmd(ui_t *ui, ui_cmd_t cmd, uint32_t now_ms);

bool ui_event_pop(ui_t *ui, ui_ev_t *out);

bool ui_view_changed(const ui_t *ui);   /* 最後の ui_update の表示キーが、前回描画したものと違うか */
void ui_view_mark(ui_t *ui);            /* 今の表示キーを「描画済み」にする */

/* ---- 式(§2.4, §2.5, §3.4, §5.1, §4.2)。描画とテストが同じものを使う ---- */
uint16_t ui_fill_x10(uint32_t e_ms, uint32_t speed);                 /* 水位 */
uint16_t ui_bath_x10(uint32_t e_ms, uint32_t speed, uint8_t set);    /* 追い焚き中の浴槽温度 */
uint16_t ui_flow_x10(uint32_t e_ms);
uint16_t ui_clock_min(uint32_t now_ms);
uint16_t ui_fill_remain_min(uint16_t fill_x10);
uint16_t ui_reheat_remain_min(uint16_t bath_x10, uint8_t set);
uint16_t ui_fill_w(uint16_t fill_x10);                               /* 水位バーの幅(最小幅を含まない) */
uint16_t ui_reheat_w(uint16_t bath_x10, uint8_t set);                /* 追い焚きバーの幅 0..432 */
uint16_t ui_bar_h(uint8_t i, uint32_t e_ms);                         /* 使用量の棒 i の高さ */
uint16_t ui_slide_dx(uint32_t e_ms);                                 /* スライド量(e=0 で 0、250 で 480) */
void     ui_touch_map(const ui_t *ui, uint16_t raw_x, uint16_t raw_y, int32_t *px, int32_t *py);

#endif /* UI_LOGIC_H */
