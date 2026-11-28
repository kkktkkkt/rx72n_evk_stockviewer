/* test_stk_indep.c - 株価ビュー Phase 1・2 のテスト担当による独立試験(コーディング担当の試験とは別の観点)。docs/design.md §8 の H-1・H-2・H-3・H-5・H-7・H-8・H-9 の穴を埋める。
 *   A  stk_calc: 絶対値 0.5(= ±0.005%)ちょうどの構成(prev = 20000 x j、diff = (2k+1) x j)と、別の割り算(商と余り)による全域の乱数比較
 *   B  stk_calc → ui_row_dir → stk_fmt_* の連結: 符号(▲ ▼ と + −)が色に依らず必ず付く(H-7)、丸めて 0 になる値の表示(+0.00% / −0.00% / ▲0)、
 *      文字列の長さ ≦ スロットの maxch(生成された ui_slots の値)
 *   C  状態ファイル S0〜S7(設計 §3.12 の表)の数値が、stk_calc の結果と一致する(price - diff = prev から bp が出る)
 *   D  stk_market: 別のモデル(64bit・日の時間割)との乱数比較、東京市場の 1 日(15 分遅れの足・昼休み・寄り付き・引け)、週末、ニューヨーク
 *   E  描画の領域: 描かれた画素が、文字・丸・状態の文字・時計の外へ出ない(区切り線・左バー・隣の行・フッターを壊さない)、
 *      実際に整形された文字列の矩形が互いに重ならない(最悪の幅だけでなく、乱数の値で)、フッターは帯と同一
 *   F  描画の頑健性: 不正な値(範囲外の enum・巨大な数値・clock_min の範囲外)の表示キーを 20,000 個描いても、面の外・右の 32 列・番兵に書かない
 *   G  色: 帯の固定色(バー・下地・区切り線・ヘッダー)が設計 §3.2 の色を RGB565 に丸めた値と厳密に一致、dir → バーの色(上昇 = 赤・下落 = 緑・変化なし = 灰)、
 *      実際にパレットを通った文字の色と下地のコントラスト ≧ 4.5(H-9 を生成物から独立に再計算)
 *   H  解析: 価格・前日の終値の数値を乱数で作った応答(小数の桁数 0〜9、整数部 1〜7 桁)を、別の方法(文字列の桁の演算)で期待値にして wx_parse_http と比較(差分ファズ)、
 *      LF だけの改行、ヘッダー内のキーを読まない、など
 *   I  SUSPECT(S32 から格上げ。設計の修正で解消): close 配列の途中で切れた応答を成功にしない・clock_min の範囲外を時刻にしない。--suspect ならこれだけを走らせる
 * 使い方: test_stk_indep <data_dir>(= tests/host/data) [--suspect]。-DAPP_WIFI_TEST=1 でコンパイルする(wx_core.c は APP_WIFI_ON = 0 では中身が消える)。 */
#define HOSTRENDER_NO_MAIN
#include "../../tools/fb_hostrender.c"
#include <stddef.h>
#include <math.h>
#include <stdbool.h>
#include "wx_core.h"
#include "test_framework.h"

#define UPS "\x01"
#define DNS "\x02"
#define MNS "\x03"

static uint32_t g_rs = 0x5EEDBEEFu;
static uint32_t rnd(void) { g_rs ^= g_rs << 13; g_rs ^= g_rs >> 17; g_rs ^= g_rs << 5; return g_rs; }
static const char *g_dir;

#define NPX ((size_t)GFX_STRIDE * GFX_H)

/* ================================================================ A */
static int64_t ref_bp(int64_t diff, int64_t prev)         /* 別の割り算: 商と余り。絶対値の 0.5 は遠い方へ */
{
    int64_t a = (diff < 0) ? -diff : diff;
    int64_t n = a * 10000;
    int64_t q = n / prev, r = n % prev;

    if (2 * r >= prev) { q++; }
    return (diff < 0) ? -q : q;
}

static int32_t pick(int maxdigits)                         /* 桁数が一様な乱数(1〜maxdigits 桁)。1〜99,999,999 */
{
    int d = 1 + (int)(rnd() % (uint32_t)maxdigits);
    uint32_t m = 1u, i;
    uint32_t v;

    for (i = 0u; i < (uint32_t)d; i++) { m *= 10u; }
    v = 1u + (rnd() % (m - 1u));
    return (int32_t)v;
}

static void t_calc(void)
{
    long bad = 0, n = 0, half = 0;
    int32_t j, k;
    stk_calc_t c;
    long i;

    TEST("A ±0.005%(bp = k + 0.5)ちょうどは遠い方へ: prev = 20000 x j、diff = ±(2k + 1) x j");
    for (j = 1; j <= 4999; j += (j < 60) ? 1 : 41) {
        for (k = 0; k < 40; k++) {
            int32_t prev = 20000 * j;
            int64_t d = (int64_t)(2 * k + 1) * j;
            int sg;
            for (sg = 0; sg < 2; sg++) {
                int64_t price = (sg == 0) ? (int64_t)prev + d : (int64_t)prev - d;
                if ((price < 1) || (price > STK_X100_MAX)) { continue; }
                n++;
                if (!stk_calc((int32_t)price, prev, &c)) { bad++; continue; }
                if (c.pct_bp != ((sg == 0) ? (k + 1) : -(k + 1))) { bad++; }
                if ((c.diff_x100 != ((sg == 0) ? (int32_t)d : (int32_t)-d)) || (c.dir != ((sg == 0) ? STK_UP : STK_DOWN))) { bad++; }
                half++;
            }
        }
    }
    if (bad != 0) { printf("  不一致 %ld / %ld\n", bad, n); }
    CHECK_EQ(bad, 0); CHECK(half > 1000);

    TEST("A 別の割り算(商と余り)との比較 3,000,000 件(桁数が一様な乱数 + prev に近い価格 + 極端な比)");
    bad = 0;
    for (i = 0; i < 3000000L; i++) {
        int32_t prev = pick(8), price;
        int64_t bp;
        switch (rnd() % 4u) {
        case 0:  price = pick(8); break;
        case 1:  price = prev + (int32_t)(rnd() % 61u) - 30; break;                        /* prev に近い */
        case 2:  price = (int32_t)(((int64_t)prev * (9900 + (int64_t)(rnd() % 201u))) / 10000); break;   /* ±1% */
        default: price = (int32_t)(((int64_t)prev * (int64_t)(1 + (rnd() % 30u))) / (int64_t)(1 + (rnd() % 7u))); break;
        }
        if ((price < 1) || (price > STK_X100_MAX)) { continue; }
        bp = ref_bp((int64_t)price - prev, prev);
        if ((bp > STK_BP_MAX) || (bp < -STK_BP_MAX)) {
            if (stk_calc(price, prev, &c) || (c.pct_bp != 0) || (c.diff_x100 != 0) || (c.dir != STK_FLAT)) { bad++; }
        } else {
            if (!stk_calc(price, prev, &c) || (c.pct_bp != (int32_t)bp) || (c.diff_x100 != price - prev)) { bad++; }
            if (c.dir != ((c.pct_bp > 0) ? STK_UP : ((c.pct_bp < 0) ? STK_DOWN : STK_FLAT))) { bad++; }      /* dir は丸めた bp の符号(設計 §4.2) */
            if (((price > prev) && (c.pct_bp < 0)) || ((price < prev) && (c.pct_bp > 0)) || ((price == prev) && (c.pct_bp != 0))) { bad++; }
        }
    }
    CHECK_EQ(bad, 0);
}

/* ================================================================ B */
static void t_pipeline(void)
{
    long bad = 0, n = 0, flat_nz = 0, yen0 = 0, i;
    char sp[32], sc[32], sa[32];

    TEST("B stk_calc → ui_row_dir → stk_fmt_*: 色付き(bp ≠ 0)は + − が必ず付き、金額が 0 でなければ ▲ ▼ も付く。bp = 0 は灰・0.00%・矢印なし(diff ≠ 0 でも)・長さ ≦ maxch(生成された ui_slots)");
    for (i = 0; i < 1500000L; i++) {
        int32_t prev = pick(8), price;
        int row = (int)(rnd() % 3u);
        stk_calc_t c;
        stk_row_t r;
        uint8_t dir, kind = stk_row_kind((uint8_t)row);
        size_t lp, lc, la;
        const char *ssign;
        int amt_zero;

        switch (rnd() % 3u) {
        case 0:  price = prev + (int32_t)(rnd() % 121u) - 60; break;
        case 1:  price = (int32_t)(((int64_t)prev * (9000 + (int64_t)(rnd() % 2001u))) / 10000); break;
        default: price = pick(8); break;
        }
        if ((price < 1) || (price > STK_X100_MAX) || !stk_calc(price, prev, &c)) { continue; }
        memset(&r, 0, sizeof r);
        r.have = 1u; r.price_x100 = price; r.diff_x100 = c.diff_x100; r.pct_bp = c.pct_bp;
        dir = ui_row_dir(&r);
        if (dir != c.dir) { bad++; }
        if (dir != ((c.pct_bp > 0) ? STK_UP : ((c.pct_bp < 0) ? STK_DOWN : STK_FLAT))) { bad++; }
        lp = stk_fmt_price(sp, sizeof sp, price, kind);
        lc = stk_fmt_pct(sc, sizeof sc, c.pct_bp);
        la = stk_fmt_amt(sa, sizeof sa, c.diff_x100, kind, dir);
        n++;
        if ((lp == 0u) || (lc == 0u) || (la == 0u)) { bad++; continue; }
        if ((lp > ui_slots[UI_SL_PRICE0].maxch) || (lc > ui_slots[UI_SL_PCT0].maxch) || (la > ui_slots[UI_SL_AMT0].maxch)) { bad++; }
        if (strchr(sp, ' ') || strchr(sc, ' ') || strchr(sa, ' ')) { bad++; }
        ssign = (dir == STK_UP) ? "+" : MNS;
        {                                                          /* 丸めた金額 A が 0 か(円は 0.5 円未満、指数は 0.00) */
            uint32_t ad = (uint32_t)((c.diff_x100 < 0) ? -c.diff_x100 : c.diff_x100);
            amt_zero = (kind == STK_KIND_YEN) ? (((ad + 50u) / 100u) == 0u) : (ad == 0u);
        }
        if (dir == STK_FLAT) {
            if (strcmp(sc, "0.00%") != 0) { bad++; }
            if (((uint8_t)sa[0] == 1u) || ((uint8_t)sa[0] == 2u)) { bad++; }                       /* 矢印なし */
            if (c.diff_x100 == 0) { if (strcmp(sa, (kind == STK_KIND_YEN) ? "0" : "0.00") != 0) { bad++; } }
            else { flat_nz++; }
        } else {
            if (strncmp(sc, ssign, 1u) != 0) { bad++; }
            if (!((sc[1] >= '0') && (sc[1] <= '9'))) { bad++; }
            if (strcmp(sc + 1, "0.00%") == 0) { bad++; }                                            /* bp ≠ 0 なので 0.00% にならない */
            if (amt_zero) {
                if (strcmp(sa, (kind == STK_KIND_YEN) ? "0" : "0.00") != 0) { bad++; }              /* A = 0 は色付きでも矢印なし */
                yen0++;
            } else {
                if (((uint8_t)sa[0]) != ((dir == STK_UP) ? 1u : 2u)) { bad++; }
                if (!((sa[1] >= '0') && (sa[1] <= '9'))) { bad++; }
            }
        }
        if (strcmp(sc, "0.00%") == 0 && dir != STK_FLAT) { bad++; }          /* 変化なし以外で符号のない 0.00% は出ない */
        if ((strncmp(sc, "+0.00%", 6u) == 0) || (strncmp(sc, MNS "0.00%", 6u) == 0)) { bad++; }   /* +0.00% −0.00% は出ない */
    }
    printf("  確かめた組 %ld。diff ≠ 0 だが 0.00%% に丸まって変化なしになる %ld、色付きで円の金額が 0(矢印なし)になる %ld\n", n, flat_nz, yen0);
    CHECK_EQ(bad, 0); CHECK(flat_nz > 100); CHECK(yen0 > 100);
}

/* ================================================================ C */
static void t_states(void)
{
    unsigned n, r;
    long bad = 0;

    TEST("C 状態ファイル S0〜S7(設計 §3.12)の数値: have の行で、prev = price - diff を stk_calc に通すと、diff・bp・dir が同じ");
    for (n = 0u; n < 8u; n++) {
        char path[512];
        stk_view_t v;

        snprintf(path, sizeof path, "%s/states/S%u.state", g_dir, n);
        CHECK_EQ(hostrender_load(&v, path), 0);
        for (r = 0u; r < 3u; r++) {
            stk_calc_t c;
            const stk_row_t *w = &v.row[r];
            if (w->have == 0u) { continue; }
            if (!stk_calc(w->price_x100, w->price_x100 - w->diff_x100, &c) || (c.diff_x100 != w->diff_x100) || (c.pct_bp != w->pct_bp)) {
                printf("  S%u r%u: price=%ld diff=%ld bp=%ld → calc diff=%ld bp=%ld\n", n, r, (long)w->price_x100, (long)w->diff_x100, (long)w->pct_bp, (long)c.diff_x100, (long)c.pct_bp);
                bad++;
            }
        }
    }
    CHECK_EQ(bad, 0);
}

/* ================================================================ D */
#define JSTOFF (32400u)

/* 64bit のモデル(巻き戻りなし)。設計 §5.4: 取引中 = 取引時間帯 かつ 昼休みでない かつ (data_ok または open_grace)。row 2(^GSPC)は昼休みなし */
static uint8_t ref_market(int row, bool synced, int64_t now_utc, int64_t mt, int64_t rs, int64_t re)
{
    int64_t now_jst = now_utc + 32400, tod = now_jst % 86400;
    int in_sess, lunch, data_ok, grace;

    if (!synced) { return STK_MKT_NONE; }
    in_sess = (now_utc >= rs) && (now_utc < re);
    lunch = (row != 2) && (tod >= 41400) && (tod < 45000);
    data_ok = (mt >= rs) && ((now_utc - mt) <= 7200);
    grace = (mt < rs) && ((now_utc - rs) <= 1800);
    return (in_sess && !lunch && (data_ok || grace)) ? STK_MKT_TRADING : STK_MKT_CLOSED;
}

static uint8_t mkt(int row, uint32_t now_utc, uint32_t mt, uint32_t rs, uint32_t re)
{
    return stk_market(stk_sym((uint8_t)row), true, now_utc + JSTOFF, mt, rs, re);
}

static void t_market(void)
{
    long bad = 0, i;
    const uint32_t RS = 1795737600u;                 /* 2026-11-27 09:00 JST = 00:00 UTC */
    const uint32_t RE = RS + 23400u;                 /* 15:30 JST */
    uint32_t t;
    int row, wk;

    TEST("D 64bit のモデルとの乱数比較 3,000,000 件(各境界の ±2 秒を厚く。昼休み・猶予・鮮度 7200 の境界を含む)");
    for (i = 0; i < 3000000L; i++) {
        int64_t rs, re, now, mt;
        static const int64_t edge[] = { 0, 1, 2, -1, -2, 1800, 1801, 1799, -1800, -1801, 7200, 7201, 7199, -7200, -7201 };
        int r2 = (int)(rnd() % 3u);
        int mode = (int)(rnd() % 3u);

        if (mode == 0) { rs = (int64_t)(rnd() % 2000000000u) + 100000; re = rs + 600 + (int64_t)(rnd() % 90000u); }
        else { rs = (int64_t)(rnd() % 20000u) * 86400 + 86400; re = rs + 23400; }                  /* 東京の時間割(rs は UTC の 0 時 = JST 09:00) */
        switch (rnd() % 5u) {
        case 0:  now = rs + edge[rnd() % 15u]; break;
        case 1:  now = re + edge[rnd() % 15u]; break;
        case 2:  now = rs + ((rnd() & 1u) ? 9000 : 12600) + edge[rnd() % 15u] % 5; break;       /* 昼休みの前後(11:30 / 12:30)の ±2 秒 */
        default: now = rs + (int64_t)(rnd() % (uint32_t)(re - rs + 7200)) - 3600; break;
        }
        switch (rnd() % 4u) {
        case 0:  mt = now - edge[rnd() % 15u]; break;
        case 1:  mt = rs + edge[rnd() % 15u]; break;
        case 2:  mt = rs - 70000; break;
        default: mt = now - (int64_t)(rnd() % 9000u) + 500; break;
        }
        if ((mt < 0) || (now < 0)) { continue; }
        if (mkt(r2, (uint32_t)now, (uint32_t)mt, (uint32_t)rs, (uint32_t)re) != ref_market(r2, true, now, mt, rs, re)) { bad++; }
    }
    CHECK_EQ(bad, 0);

    TEST("D 東京市場の 1 日(足は 15 分遅れ・昼休みは 11:30 で止まる)を 1 秒刻みで: 設計 §5.4 の表。09:00〜11:30 取引中、11:30〜12:30 終値、12:30〜15:30 取引中、ほかは終値。^GSPC(同じ区間を使う)は昼休みなし");
    for (row = 0; row < 3; row++) {
        long fails = 0;
        for (t = 0u; t < 86400u; t++) {                      /* t = UTC の 00:00 からの秒 = JST 09:00 から */
            uint32_t now = RS + t, mt;
            uint32_t feed = (t >= 900u) ? (t - 900u) : 0u;  /* 足の時刻(RS からの秒)。15 分遅れ */
            uint8_t want, got;
            if (t < 900u) { mt = RS - 70000u; }              /* 最初の足がまだ届かない = 前日の足 */
            else if (feed <= 9000u) { mt = RS + feed; }
            else if (feed < 12600u) { mt = RS + 9000u; }     /* 前場の最後の足のまま(昼休みと後場の頭) */
            else if (feed <= 23400u) { mt = RS + feed; }
            else { mt = RS + 23400u; }
            if (t >= 23400u) { want = STK_MKT_CLOSED; }
            else if ((t >= 9000u) && (t < 12600u) && (row != 2)) { want = STK_MKT_CLOSED; }
            else { want = STK_MKT_TRADING; }
            got = mkt(row, now, mt, RS, RE);
            if (got != want) { fails++; if (fails < 4) { printf("  row %d t=%u (JST %02u:%02u:%02u): got %u want %u\n", row, t, 9u + t / 3600u, (t / 60u) % 60u, t % 60u, (unsigned)got, (unsigned)want); } }
        }
        CHECK_EQ(fails, 0);
    }
    /* 代表時刻を文字どおり(設計 §5.4 の表): 09:00:00 取引中(前日の足)/ 09:29:59 取引中 / 11:29:59 取引中 / 11:30:00 終値 / 12:29:59 終値 / 12:30:00 取引中(足は 11:30 のまま)/ 12:44:59 取引中 / 15:29:59 取引中 / 15:30:00 終値 */
    CHECK_EQ(mkt(0, RS, RS - 70000u, RS, RE), STK_MKT_TRADING);
    CHECK_EQ(mkt(0, RS + 1799u, RS - 70000u, RS, RE), STK_MKT_TRADING);
    CHECK_EQ(mkt(0, RS + 9000u - 1u, RS + 8000u, RS, RE), STK_MKT_TRADING);
    CHECK_EQ(mkt(0, RS + 9000u, RS + 8100u, RS, RE), STK_MKT_CLOSED);
    CHECK_EQ(mkt(1, RS + 12600u - 1u, RS + 9000u, RS, RE), STK_MKT_CLOSED);
    CHECK_EQ(mkt(1, RS + 12600u, RS + 9000u, RS, RE), STK_MKT_TRADING);
    CHECK_EQ(mkt(1, RS + 12600u + 899u, RS + 9000u, RS, RE), STK_MKT_TRADING);
    CHECK_EQ(mkt(0, RE - 1u, RE - 1000u, RS, RE), STK_MKT_TRADING);
    CHECK_EQ(mkt(0, RE, RE - 1000u, RS, RE), STK_MKT_CLOSED);
    CHECK_EQ(mkt(0, RS + 5u * 3600u, RS + 5u * 3600u - 7200u, RS, RE), STK_MKT_TRADING);   /* 鮮度 7200 ちょうど */
    CHECK_EQ(mkt(0, RS + 5u * 3600u, RS + 5u * 3600u - 7201u, RS, RE), STK_MKT_CLOSED);

    TEST("D 週末・休日: 区間(rs / re)が直前の取引日のままなら、何日後でも終値。時計が未同期なら NONE");
    for (wk = 1; wk <= 4; wk++) {
        CHECK_EQ(mkt(0, RS + (uint32_t)wk * 86400u + 3600u, RE - 600u, RS, RE), STK_MKT_CLOSED);
        CHECK_EQ(mkt(0, RS + (uint32_t)wk * 86400u + 3600u, RS + (uint32_t)wk * 86400u + 3500u, RS, RE), STK_MKT_CLOSED);   /* 足が新しくても区間の外 */
    }
    CHECK_EQ(stk_market(stk_sym(0), false, RS + JSTOFF + 3600u, RS + 3000u, RS, RE), STK_MKT_NONE);

    TEST("D ニューヨーク(足の遅れなし): 14:30〜21:00 UTC だけ取引中(冬時間 = 09:30〜16:00 EST)");
    {
        const uint32_t GS = 1795617000u, GE = GS + 23400u;
        for (t = 0u; t < 86400u; t += 7u) {
            uint32_t now = GS - 20000u + t;
            uint8_t want = ((now >= GS) && (now < GE)) ? STK_MKT_TRADING : STK_MKT_CLOSED;
            uint32_t mt = (now >= GS) ? ((now < GE) ? now : GE) : (GS - 3600u);
            if (mkt(2, now, mt, GS, GE) != want) { bad++; }
        }
        CHECK_EQ(bad, 0);
    }
}

/* ================================================================ E / F / G: 描画 */
struct box { int x0, y0, x1, y1; };

static int box_overlap(const struct box *a, const struct box *b)
{
    return (a->x0 < b->x1) && (b->x0 < a->x1) && (a->y0 < b->y1) && (b->y0 < a->y1);
}

static int glyph_of(char ch)
{
    static const char map[] = "0123456789,.+\x03%\x01\x02:-";
    const char *q;

    if (ch == '\0') { return -1; }
    q = strchr(map, ch);
    return (q != NULL) ? (int)(q - map) : -1;
}

/* 文字列 s を slot へ描いた時の、グリフの画素箱の外接(描かれる画素の範囲)。空文字列は false */
static bool text_box(uint8_t slot, const char *s, struct box *b)
{
    const ui_slot_t *sl = &ui_slots[slot];
    const ui_glyph_t *gl = ui_glyphs[sl->font];
    int wd = 0, x, first = 1;
    const char *p;

    for (p = s; *p; p++) { int g = glyph_of(*p); if (g >= 0) { wd += gl[g].adv; } }
    x = (sl->align == UI_ALIGN_RIGHT) ? sl->x - wd : ((sl->align == UI_ALIGN_CENTER) ? sl->x - wd / 2 : sl->x);
    for (p = s; *p; p++) {
        int g = glyph_of(*p);
        if (g < 0) { continue; }
        if (gl[g].w != 0u) {
            int x0 = x + gl[g].bx, y0 = sl->y - gl[g].by, x1 = x0 + (int)gl[g].w, y1 = y0 + (int)gl[g].h;
            if (first) { b->x0 = x0; b->y0 = y0; b->x1 = x1; b->y1 = y1; first = 0; }
            else { if (x0 < b->x0) b->x0 = x0; if (y0 < b->y0) b->y0 = y0; if (x1 > b->x1) b->x1 = x1; if (y1 > b->y1) b->y1 = y1; }
        }
        x += gl[g].adv;
    }
    return !first;
}

static struct box shape_box(uint8_t sh, int y0)
{
    struct box b;
    b.x0 = ui_shapes[sh].x; b.y0 = y0 + ui_shapes[sh].y; b.x1 = b.x0 + ui_shapes[sh].w; b.y1 = b.y0 + ui_shapes[sh].h;
    return b;
}

static void render_to(const stk_view_t *v, uint8_t *fb)
{
    ui_t u;
    ui_init(&u, 0u);
    ui_set_view(&u, v);
    ui_render(&u, fb);
}

static void fmt_hm(char *o, uint16_t m)
{
    if (m == STK_NONE16) { strcpy(o, "--:--"); return; }
    o[0] = (char)('0' + (m / 60u) / 10u); o[1] = (char)('0' + (m / 60u) % 10u); o[2] = ':'; o[3] = (char)('0' + (m % 60u) / 10u); o[4] = (char)('0' + (m % 60u) % 10u); o[5] = '\0';
}

/* v を描いた時の「背景だけの面」(帯・ヘッダー・フッター・アイコン)と、動く文字・丸が触ってよい箱の一覧を作る。boxes に積む */
#define MAXBOX 40
static int build_bg_and_boxes(const stk_view_t *v, uint8_t *bg, struct box *boxes, int *rowbox_end /* [4] 0 = ヘッダーの箱の終わり、1〜3 = 行ごとの箱の終わりの添字 */)
{
    int r, nb = 0, y, x;
    char b[24];
    struct box bx;

    memset(bg, 0, NPX);
    for (y = 0; y < UI_HDR_H; y++) { memcpy(&bg[(size_t)y * GFX_STRIDE], &ui_hdr[(size_t)y * UI_W], UI_W); }
    for (r = 0; r < 3; r++) {
        uint8_t d = ui_row_dir(&v->row[r]);
        for (y = 0; y < UI_ROW_H; y++) { memcpy(&bg[(size_t)(ui_row_y0[r] + y) * GFX_STRIDE], &ui_strip[r][d][(size_t)y * UI_W], UI_W); }
    }
    {
        uint8_t f = ui_foot_index(v);
        for (y = 0; y < UI_FOOT_H; y++) { memcpy(&bg[(size_t)(UI_FOOT_Y + y) * GFX_STRIDE], &ui_foot[f][(size_t)y * UI_W], UI_W); }
    }
    {
        uint8_t ic = ui_icon_index(v);
        for (y = 0; y < UI_WIFI_H; y++) for (x = 0; x < UI_WIFI_W; x++) { bg[(size_t)(UI_WIFI_Y + y) * GFX_STRIDE + (size_t)(UI_WIFI_X + x)] = ui_spr_wifi[ic][y * UI_WIFI_W + x]; }
    }
    fmt_hm(b, v->clock_min); if (text_box(UI_SL_CLOCK, b, &bx)) { boxes[nb++] = bx; }
    fmt_hm(b, v->upd_min);   if (text_box(UI_SL_UPD, b, &bx)) { boxes[nb++] = bx; }
    rowbox_end[0] = nb;
    for (r = 0; r < 3; r++) {
        const stk_row_t *w = &v->row[r];
        int y0 = ui_row_y0[r];
        uint8_t dir = ui_row_dir(w);
        uint8_t kind = stk_row_kind((uint8_t)r);
        if (w->mkt == STK_MKT_TRADING) { boxes[nb++] = shape_box(UI_SH_DOT, y0); boxes[nb++] = shape_box(UI_SH_TRADING, y0); }
        else if (w->mkt == STK_MKT_CLOSED) { boxes[nb++] = shape_box(UI_SH_DOT, y0); boxes[nb++] = shape_box(UI_SH_CLOSED, y0); }
        else if (w->fail) { boxes[nb++] = shape_box(UI_SH_DOT, y0); boxes[nb++] = shape_box(UI_SH_FAIL, y0); }
        if (w->have == 0u) {
            if (text_box((uint8_t)(UI_SL_PRICE0 + r), "---", &bx)) { boxes[nb++] = bx; }
            if (text_box((uint8_t)(UI_SL_PCT0 + r), "---", &bx)) { boxes[nb++] = bx; }
        } else {
            if ((stk_fmt_price(b, sizeof b, w->price_x100, kind) > 0u) && text_box((uint8_t)(UI_SL_PRICE0 + r), b, &bx)) { boxes[nb++] = bx; }
            if ((stk_fmt_pct(b, sizeof b, w->pct_bp) > 0u) && text_box((uint8_t)(UI_SL_PCT0 + r), b, &bx)) { boxes[nb++] = bx; }
            if ((stk_fmt_amt(b, sizeof b, w->diff_x100, kind, dir) > 0u) && text_box((uint8_t)(UI_SL_AMT0 + r), b, &bx)) { boxes[nb++] = bx; }
        }
        rowbox_end[r + 1] = nb;
    }
    return nb;
}

static stk_view_t rand_view(int valid)
{
    stk_view_t v;
    int r;

    memset(&v, 0, sizeof v);
    v.net = (uint8_t)(valid ? rnd() % 4u : rnd() & 0xFFu);
    v.busy = (uint8_t)(rnd() & 1u); v.have_any = (uint8_t)(rnd() & 1u); v.stale_all = (uint8_t)(rnd() & 1u); v.partial = (uint8_t)(rnd() & 1u);
    v.clock_min = (uint16_t)(valid ? ((rnd() & 3u) == 0u ? STK_NONE16 : rnd() % 1440u) : (rnd() & 0xFFFFu));
    v.upd_min = (uint16_t)(valid ? ((rnd() & 3u) == 0u ? STK_NONE16 : rnd() % 1440u) : (rnd() & 0xFFFFu));
    for (r = 0; r < 3; r++) {
        stk_row_t *w = &v.row[r];
        if (valid) {
            int32_t prev = pick(8), price = pick(8);
            stk_calc_t c;
            w->have = (uint8_t)(rnd() & 1u); w->stale = (uint8_t)(rnd() & 1u); w->fail = (uint8_t)(rnd() & 1u); w->mkt = (uint8_t)(rnd() % 3u);
            if ((rnd() & 1u) != 0u) { price = prev + (int32_t)(rnd() % 301u) - 150; if (price < 1) { price = 1; } }
            if (stk_calc(price, prev, &c)) { w->price_x100 = price; w->diff_x100 = c.diff_x100; w->pct_bp = c.pct_bp; }
            else { w->have = 0u; }
        } else {
            w->have = (uint8_t)(rnd() & 0xFFu); w->stale = (uint8_t)(rnd() & 0xFFu); w->fail = (uint8_t)(rnd() & 0xFFu); w->mkt = (uint8_t)(rnd() & 0xFFu);
            w->price_x100 = (int32_t)rnd(); w->diff_x100 = (int32_t)rnd(); w->pct_bp = (int32_t)rnd();
            if ((rnd() & 3u) == 0u) { w->price_x100 = (int32_t)(rnd() % 3u) - 1; }
        }
    }
    return v;
}

static void t_regions(void)
{
    static uint8_t fb[NPX], bg[NPX];
    long bad_out = 0, bad_ov = 0, bad_foot = 0, bad_rows = 0, i;
    int r, x, y;

    TEST("E 描画の領域: 乱数の有効な表示キー 6,000 個。背景(帯・ヘッダー・フッター・アイコン)から変わった画素は、動く文字・丸・状態の文字の箱の中だけ(区切り線・左バー・隣の行・フッターは触らない)");
    for (i = 0; i < 6000; i++) {
        stk_view_t v = rand_view(1);
        struct box boxes[MAXBOX];
        int rowend[4], nb;
        uint8_t dir;

        (void)dir;
        nb = build_bg_and_boxes(&v, bg, boxes, rowend);
        memset(fb, 0xEE, NPX);
        render_to(&v, fb);
        for (y = 0; y < UI_H; y++) {
            for (x = 0; x < UI_W; x++) {
                int k, inside = 0;
                if (fb[(size_t)y * GFX_STRIDE + (size_t)x] == bg[(size_t)y * GFX_STRIDE + (size_t)x]) { continue; }
                for (k = 0; k < nb; k++) { if ((x >= boxes[k].x0) && (x < boxes[k].x1) && (y >= boxes[k].y0) && (y < boxes[k].y1)) { inside = 1; break; } }
                if (!inside) { if (bad_out < 5) { printf("  箱の外の画素 (%d,%d) view#%ld\n", x, y, i); } bad_out++; }
            }
        }
        /* 区切り線(各行の最下行)と左バー(x 0〜5)は背景のまま。フッターは帯と同一(文字を描かない) */
        for (r = 0; r < 3; r++) {
            for (x = 0; x < UI_W; x++) { if (fb[(size_t)(ui_row_y0[r] + UI_ROW_H - 1) * GFX_STRIDE + (size_t)x] != bg[(size_t)(ui_row_y0[r] + UI_ROW_H - 1) * GFX_STRIDE + (size_t)x]) { bad_rows++; } }
            for (y = 0; y < UI_ROW_H; y++) for (x = 0; x < 6; x++) { if (fb[(size_t)(ui_row_y0[r] + y) * GFX_STRIDE + (size_t)x] != bg[(size_t)(ui_row_y0[r] + y) * GFX_STRIDE + (size_t)x]) { bad_rows++; } }
        }
        for (y = UI_FOOT_Y; y < UI_H; y++) for (x = 0; x < UI_W; x++) { if (fb[(size_t)y * GFX_STRIDE + (size_t)x] != bg[(size_t)y * GFX_STRIDE + (size_t)x]) { bad_foot++; } }
        /* 同じ行の中の箱(価格・騰落率・金額・丸・状態の文字)が互いに重ならない */
        {
            int a = rowend[0], p, q;
            for (r = 0; r < 3; r++) {
                for (p = a; p < rowend[r + 1]; p++) for (q = p + 1; q < rowend[r + 1]; q++) {
                    if (box_overlap(&boxes[p], &boxes[q])) { if (bad_ov < 5) { printf("  重なり view#%ld row%d: (%d,%d,%d,%d) と (%d,%d,%d,%d)\n", i, r, boxes[p].x0, boxes[p].y0, boxes[p].x1, boxes[p].y1, boxes[q].x0, boxes[q].y0, boxes[q].x1, boxes[q].y1); } bad_ov++; }
                }
                for (p = a; p < rowend[r + 1]; p++) {     /* 行の中に収まる(区切り線の y = Y0 + 73 を含まない)・x が 6〜479 */
                    if ((boxes[p].y0 < ui_row_y0[r]) || (boxes[p].y1 > ui_row_y0[r] + UI_ROW_H - 1) || (boxes[p].x0 < 6) || (boxes[p].x1 > UI_W)) { bad_ov++; }
                }
                a = rowend[r + 1];
            }
        }
    }
    printf("  箱の外の画素 %ld、重なり・はみ出し %ld、区切り線・左バーの変化 %ld、フッターの変化 %ld\n", bad_out, bad_ov, bad_rows, bad_foot);
    CHECK_EQ(bad_out, 0); CHECK_EQ(bad_ov, 0); CHECK_EQ(bad_rows, 0); CHECK_EQ(bad_foot, 0);
}

static void t_garbage(void)
{
    static uint8_t g_mem[1024u + NPX + 1024u];
    long bad = 0, i;
    size_t k, r, c;

    TEST("F 描画の頑健性: 不正な値(範囲外の enum・巨大な数・clock_min の範囲外)の表示キー 20,000 個で、前後の番兵・右の 32 列・面の外に書かない");
    for (i = 0; i < 20000; i++) {
        stk_view_t v = rand_view((int)(i & 1));

        memset(g_mem, 0xA5, sizeof g_mem);
        for (r = 0u; r < GFX_H; r++) { memset(&g_mem[1024u + r * GFX_STRIDE], 0x00, GFX_W); }
        render_to(&v, &g_mem[1024u]);
        for (k = 0u; k < 1024u; k++) { if ((g_mem[k] != 0xA5u) || (g_mem[1024u + NPX + k] != 0xA5u)) { bad++; break; } }
        for (r = 0u; r < GFX_H; r++) for (c = GFX_W; c < GFX_STRIDE; c++) { if (g_mem[1024u + r * GFX_STRIDE + c] != 0xA5u) { bad++; r = GFX_H; break; } }
    }
    CHECK_EQ(bad, 0);
}

static uint32_t q565(uint32_t rgb)                 /* RGB888 → RGB565(最も近い段)→ RGB888(上位ビットの複製)。生成された ui_pal と同じ規約 */
{
    uint32_t r = (rgb >> 16) & 255u, g = (rgb >> 8) & 255u, b = rgb & 255u;
    uint32_t rr = (r * 31u + 127u) / 255u, gg = (g * 63u + 127u) / 255u, bb = (b * 31u + 127u) / 255u;
    uint32_t r8 = (rr << 3) | (rr >> 2), g8 = (gg << 2) | (gg >> 4), b8 = (bb << 3) | (bb >> 2);
    return 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
}

static double lin(uint32_t c8)
{
    double s = (double)c8 / 255.0;
    return (s <= 0.03928) ? (s / 12.92) : pow((s + 0.055) / 1.055, 2.4);
}

static double lum(uint32_t argb)
{
    return 0.2126 * lin((argb >> 16) & 255u) + 0.7152 * lin((argb >> 8) & 255u) + 0.0722 * lin(argb & 255u);
}

static double contrast(uint32_t a, uint32_t b)
{
    double la = lum(a), lb = lum(b), hi = (la > lb) ? la : lb, lo = (la > lb) ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

static void t_colors(void)
{
    static const uint32_t BAR[3] = { 0xD23B3Bu, 0x1F9D57u, 0x556080u }, BG[3] = { 0x191522u, 0x0C1C24u, 0x101727u };
    const uint32_t SEP = 0x1F2D4Du, HDR = 0x16213Au;
    int r, d, x, y;
    long bad = 0;

    TEST("G 帯の固定色: バー・下地・区切り線・ヘッダー・フッターの地が、設計 §3.2 の色を RGB565 に丸めた値と厳密に一致。dir → 帯(上昇 = 赤・下落 = 緑・変化なし = 灰)");
    for (r = 0; r < 3; r++) {
        for (d = 0; d < 3; d++) {
            const uint8_t *s = ui_strip[r][d];
            for (y = 0; y < UI_ROW_H - 1; y++) {
                for (x = 0; x < 6; x++) { if (ui_pal[s[y * UI_W + x]] != q565(BAR[d])) { bad++; } }                     /* 左バー x 0〜5 */
            }
            for (x = 0; x < UI_W; x++) { if (ui_pal[s[(UI_ROW_H - 1) * UI_W + x]] != q565(SEP)) { bad++; } }          /* 区切り線(バーの列も) */
            if (ui_pal[s[(UI_ROW_H - 2) * UI_W + 300]] != q565(BG[d])) { bad++; }                                       /* 下地(文字のない所) */
            if (ui_pal[s[3 * UI_W + 470]] != q565(BG[d])) { bad++; }
        }
    }
    for (y = 0; y < UI_HDR_H; y++) { if (ui_pal[ui_hdr[y * UI_W + 300]] != q565(HDR)) { bad++; } if (ui_pal[ui_hdr[y * UI_W + 3]] != q565(HDR)) { bad++; } }
    {
        int f;
        for (f = 0; f < (int)UI_FOOT_N; f++) { for (y = 0; y < UI_FOOT_H; y++) { if (ui_pal[ui_foot[f][y * UI_W + 300]] != q565(HDR)) { bad++; } } }
    }
    CHECK_EQ(bad, 0);
    {
        stk_view_t v;
        static uint8_t fb[NPX];
        memset(&v, 0, sizeof v);
        v.net = STK_NET_ON; v.have_any = 1u; v.clock_min = 555u; v.upd_min = 552u;
        v.row[0].have = 1u; v.row[0].price_x100 = 1000; v.row[0].diff_x100 = 5; v.row[0].pct_bp = 5;
        v.row[1].have = 1u; v.row[1].price_x100 = 1000; v.row[1].diff_x100 = -5; v.row[1].pct_bp = -5;
        v.row[2].have = 1u; v.row[2].price_x100 = 1000;
        render_to(&v, fb);
        for (r = 0; r < 3; r++) {
            uint8_t want = (r == 0) ? STK_UP : ((r == 1) ? STK_DOWN : STK_FLAT);
            CHECK_EQ(ui_pal[fb[(size_t)(ui_row_y0[r] + 30) * GFX_STRIDE + 2u]], q565(BAR[want]));
            CHECK_EQ(ui_pal[fb[(size_t)(ui_row_y0[r] + 66) * GFX_STRIDE + 470u]], q565(BG[want]));
        }
    }

    TEST("G パレットを通った実際の色(ramp の最大の段 32 = 前景)と、その下地のコントラスト: 文字 ≧ 4.5、丸(図形)≧ 3。設計 §3.2 の前景との差 ≦ 1 段");
    {
        static const struct { int kind; uint32_t fg[3]; double need; const char *name; } K[] = {
            { UI_R_PRICE, { 0xF4F7FFu, 0xF4F7FFu, 0xF4F7FFu }, 4.5, "PRICE" },
            { UI_R_STALE, { 0x7D8AA6u, 0x7D8AA6u, 0x7D8AA6u }, 4.5, "STALE" },
            { UI_R_ACC,   { 0xFF6B6Bu, 0x41D17Au, 0xC9D3EAu }, 4.5, "ACC" },
            { UI_R_DOTG,  { 0x5FD38Du, 0x5FD38Du, 0x5FD38Du }, 3.0, "DOTG" },
            { UI_R_SUB,   { 0x8DA0C4u, 0x8DA0C4u, 0x8DA0C4u }, 4.5, "SUB" },
        };
        size_t k;
        int t;
        for (k = 0u; k < sizeof K / sizeof K[0]; k++) {
            for (t = 0; t < 3; t++) {
                uint32_t fg = ui_pal[ui_ramp[K[k].kind][t][32]];
                uint32_t bgc = ui_pal[ui_strip[0][t][3 * UI_W + 470]];
                double cr = contrast(fg, bgc);
                uint32_t want = q565(K[k].fg[t]);
                int dr = (int)((fg >> 16) & 255u) - (int)((want >> 16) & 255u), dg = (int)((fg >> 8) & 255u) - (int)((want >> 8) & 255u), db = (int)(fg & 255u) - (int)(want & 255u);
                dr = abs(dr); dg = abs(dg); db = abs(db);
                if ((cr < K[k].need) || (dr > 10) || (dg > 6) || (db > 10)) { printf("  %s tint%d: contrast %.2f 前景 %08X 期待 %08X\n", K[k].name, t, cr, (unsigned)fg, (unsigned)want); bad++; }
            }
        }
        {
            uint32_t hb = ui_pal[ui_hdr[3 * UI_W + 3]];
            if (contrast(ui_pal[ui_ramp_hdr[UI_RH_CLOCK][32]], hb) < 4.5) { bad++; }
            if (contrast(ui_pal[ui_ramp_hdr[UI_RH_UPD][32]], hb) < 4.5) { bad++; }
        }
        CHECK_EQ(bad, 0);
    }
}

/* ================================================================ H 解析 */
#define H200 "HTTP/1.0 200 OK\r\ncontent-type: application/json;charset=utf-8\r\ndate: Fri, 27 Nov 2026 04:04:13 GMT\r\n\r\n"

static size_t build_resp(char *b, size_t cap, const char *hdr, const char *price, const char *prev, const char *close)
{
    return (size_t)snprintf(b, cap, "%s{\"chart\":{\"result\":[{\"meta\":{\"currency\":\"JPY\",\"symbol\":\"X\",\"regularMarketTime\":1795751335,\"regularMarketPrice\":%s,"
                            "\"chartPreviousClose\":%s,\"currentTradingPeriod\":{\"pre\":{\"start\":1,\"end\":2},\"regular\":{\"timezone\":\"JST\",\"start\":1795737600,\"end\":1795761000,"
                            "\"gmtoffset\":32400},\"post\":{\"start\":3,\"end\":4}}},\"timestamp\":[1,2],\"indicators\":{\"quote\":[{\"close\":%s}],\"adjclose\":[{\"adjclose\":[7,8]}]}}],\"error\":null}}",
                            hdr, price, prev, close);
}

/* 10 進の文字列 → ×100(小数 3 桁目を四捨五入)。別の方法: 文字列の桁を直接読む(wx_core.c と同じ式を使わない)。整数部が 0 桁・8 桁以上は false */
static bool ref_x100(const char *s, int32_t *out)
{
    const char *p = s;
    int neg = 0;
    int64_t ip = 0;
    int digs = 0, f1 = 0, f2 = 0, f3 = 0, nf = 0;
    int64_t v;

    if (*p == '-') { neg = 1; p++; }
    for (; (*p >= '0') && (*p <= '9'); p++) { ip = ip * 10 + (*p - '0'); digs++; }
    if (*p == '.') {
        p++;
        for (; (*p >= '0') && (*p <= '9'); p++) { if (nf == 0) f1 = *p - '0'; else if (nf == 1) f2 = *p - '0'; else if (nf == 2) f3 = *p - '0'; nf++; }
    }
    if ((digs == 0) || (digs > 7) || (*p != '\0')) { return false; }
    v = ip * 100 + f1 * 10 + f2 + ((f3 >= 5) ? 1 : 0);
    *out = (int32_t)(neg ? -v : v);
    return true;
}

static void rand_num(char *o, size_t cap)
{
    int id = 1 + (int)(rnd() % 7u), fd = (int)(rnd() % 10u), i;
    size_t k = 0u;

    (void)cap;
    if ((rnd() % 40u) == 0u) { o[k++] = '-'; }
    for (i = 0; i < id; i++) { o[k++] = (char)('0' + ((i == 0 && id > 1) ? 1u + rnd() % 9u : rnd() % 10u)); }
    if (fd > 0) {
        o[k++] = '.';
        for (i = 0; i < fd; i++) { o[k++] = (char)('0' + (rnd() % 10u)); }
    } else if ((rnd() & 1u) != 0u) {
        o[k++] = '.'; o[k++] = '0';
    }
    o[k] = '\0';
}

static void t_parse_fuzz(void)
{
    long bad = 0, ok_n = 0, i;
    char b[2048], ps[40], vs[40];

    TEST("H 数値の差分ファズ 300,000 件: 価格・直前の終値を乱数の文字列(整数部 1〜7 桁、小数 0〜9 桁、まれに負)で作り、別の方法で期待値を出して wx_parse_http と比べる");
    for (i = 0; i < 300000L; i++) {
        size_t n;
        wx_result_t r;
        bool ok, want_ok;
        int32_t p = 0, q = 0;
        bool pv, qv;
        stk_calc_t c;

        rand_num(ps, sizeof ps); rand_num(vs, sizeof vs);
        n = build_resp(b, sizeof b, H200, ps, vs, "[null,null]");
        ok = wx_parse_http(b, n, &r);
        pv = ref_x100(ps, &p); qv = ref_x100(vs, &q);
        want_ok = pv && qv && (p >= 1) && (q >= 1) && stk_calc(p, q, &c);
        if (ok != want_ok) { if (bad < 5) { printf("  price=%s prev=%s: ok=%d want=%d err=%u\n", ps, vs, (int)ok, (int)want_ok, (unsigned)r.err); } bad++; continue; }
        if (ok) {
            ok_n++;
            if ((r.price_x100 != p) || (r.prev_x100 != q) || (r.has_bar_prev != 0u)) { bad++; }
        } else {
            if (r.err != ((!pv || !qv) ? 5u : 6u)) { if (bad < 5) { printf("  price=%s prev=%s: err=%u\n", ps, vs, (unsigned)r.err); } bad++; }
        }
        /* 直前の足があるとき(close 配列の最後から 2 番目)は、それが prev になる */
        n = build_resp(b, sizeof b, H200, ps, "999.99", "[null,5.5,7]");
        ok = wx_parse_http(b, n, &r);
        if (ok != (pv && (p >= 1) && stk_calc(p, 550, &c))) { bad++; }
        else if (ok && ((r.prev_x100 != 550) || (r.has_bar_prev != 1u))) { bad++; }
    }
    printf("  成功 %ld 件(残りは範囲外・読めない)\n", ok_n);
    CHECK_EQ(bad, 0); CHECK(ok_n > 20000);

    TEST("H LF だけの改行・ヘッダー部のキーは読まない・本文の先頭に空白なし");
    {
        wx_result_t r;
        size_t n;
        char tmp[2048];
        size_t k, m = 0u;

        n = build_resp(b, sizeof b, H200, "2564.5", "2433.0", "[2433.0,2564.5]");
        for (k = 0u; k < n; k++) { if (b[k] != '\r') { tmp[m++] = b[k]; } }               /* CR を全部消す = LF だけ */
        /* 設計 §5.2 段 3: 空行(CRLF CRLF)が無ければ「ヘッダーの途中で切れた」= err 3。LF だけの応答は受けない(Yahoo は CRLF。以前は成功にしていた) */
        CHECK(!wx_parse_http(tmp, m, &r)); CHECK_EQ(r.err, 3); CHECK_EQ(r.http, 200); CHECK_EQ(r.have_date, 1);
        n = build_resp(b, sizeof b, "HTTP/1.0 200 OK\r\nx-note: \"regularMarketPrice\":1.5,\"chartPreviousClose\":1.5\r\ndate: Fri, 27 Nov 2026 04:04:13 GMT\r\n\r\n", "2564.5", "2433.0", "[2433.0,2564.5]");
        CHECK(wx_parse_http(b, n, &r)); CHECK_EQ(r.price_x100, 256450);                   /* ヘッダーの偽のキーを読まない */
    }
}

/* ================================================================ 不具合の可能性(現在の実装で落ちるものを含む) */
static void t_suspect(void)
{
    char b[4096], t[4096];
    size_t n, i;
    wx_result_t full, r;
    static const char *const FILES[3] = { "yahoo_GSPC_2d.http", "yahoo_6723T_2d.http", "yahoo_N225_2d.http" };
    int f;

    TEST("SUSPECT-1 close 配列の途中で切れた応答を成功にしない(切れたまま成功にすると、chartPreviousClose = 2 営業日前の終値で前日比が誤る。設計 §5.3・F10)");
    for (f = 0; f < 3; f++) {
        char path[600];
        FILE *fp;
        size_t cl, bracket, bad = 0u, ex = 0u;
        const char *cp;

        snprintf(path, sizeof path, "%s/%s", g_dir, FILES[f]);
        fp = fopen(path, "rb");
        if (fp == NULL) { printf("  開けない: %s\n", path); CHECK(0); continue; }
        n = fread(b, 1u, sizeof b - 1u, fp); fclose(fp); b[n] = '\0';
        {                                                          /* CRLF にそろえる */
            size_t k, m = 0u;
            for (k = 0u; k < n; k++) { if (b[k] == '\r') { continue; } if (b[k] == '\n') { t[m++] = '\r'; } t[m++] = b[k]; }
            memcpy(b, t, m); n = m; b[n] = '\0';
        }
        CHECK(wx_parse_http(b, n, &full));
        cp = strstr(strstr(b, "\"indicators\""), "\"close\":[");
        cl = (size_t)(cp - b) + 9u;
        bracket = (size_t)(strchr(cp, ']') - b);
        for (i = cl; i <= bracket; i++) {                          /* 配列の途中まで(] の手前)で終わる応答 */
            bool ok = wx_parse_http(b, i, &r);
            if (ok && (r.has_bar_prev == 0u) && (full.has_bar_prev == 1u) && (r.prev_x100 != full.prev_x100)) { bad++; if (ex == 0u) { ex = i; } }
        }
        if (bad != 0u) {
            wx_parse_http(b, ex, &r);
            printf("  %s: close 配列の途中(長さ %u〜%u)で切れた応答 %u 通りが成功し、prev = %ld(全体では %ld)で騰落率が誤る\n", FILES[f], (unsigned)cl, (unsigned)bracket, (unsigned)bad,
                   (long)r.prev_x100, (long)full.prev_x100);
        }
        CHECK_EQ(bad, 0);
    }

    TEST("SUSPECT-2 clock_min / upd_min が 1440 以上(0xFFFF 以外)でも、時計に 24:00 以上の時刻を出さない(設計 §4.4 は 0〜1439)");
    {
        stk_view_t v;
        static uint8_t fa[NPX], fb2[NPX];
        memset(&v, 0, sizeof v);
        v.net = STK_NET_ON; v.have_any = 1u; v.clock_min = 1500u; v.upd_min = STK_NONE16;
        render_to(&v, fa);
        v.clock_min = STK_NONE16;
        render_to(&v, fb2);
        CHECK(memcmp(fa, fb2, NPX) == 0);                          /* 範囲外は「なし」と同じ表示(--:--)になるべき */
    }
}

int main(int argc, char **argv)
{
    g_dir = (argc > 1) ? argv[1] : "tests/host/data";
    if ((argc > 2) && (strcmp(argv[2], "--suspect") == 0)) {
        t_suspect();
        TEST_REPORT("test_stk_indep --suspect");
    }
    t_calc();
    t_pipeline();
    t_states();
    t_market();
    t_regions();
    t_garbage();
    t_colors();
    t_parse_fuzz();
    t_suspect();                                                   /* S32 から格上げ(設計の修正で 0 になった)。--suspect でも単独で走る */
    TEST_REPORT("test_stk_indep");
}
