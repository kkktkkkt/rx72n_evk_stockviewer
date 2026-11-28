/* test_logq.c - src/app/app_logq.c(UART送信の行キュー)の単体テスト。設計 §5.2 / U-H7。標準Cのみ。
 * 作成: 2026-11-20 テスト担当 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "app_logq.h"
#include "test_framework.h"

static void mkline(char *dst, size_t len, unsigned seed)
{
    size_t i;
    for (i = 0u; i < len; i++) { dst[i] = (char)('A' + (int)((seed + i) % 26u)); }
    dst[len] = '\0';
}

/* 先頭が (len, seed) の行か */
static int front_is(const applog_q_t *q, size_t len, unsigned seed)
{
    char   exp[APPLOG_LINE_MAX + 1];
    size_t n = 99999u;
    const char *p = applog_q_peek(q, &n);
    if ((p == NULL) || (n != len)) { return 0; }
    mkline(exp, len, seed);
    return (memcmp(p, exp, len) == 0) && (p[len] == '\0');
}

int main(void)
{
    applog_q_t q;
    char buf[APPLOG_LINE_MAX + 8];
    unsigned i;

    TEST("Q1 初期状態");
    memset(&q, 0xA5, sizeof(q));
    applog_q_init(&q);
    CHECK_EQ(applog_q_count(&q), 0);
    CHECK_EQ(applog_q_drops(&q), 0);
    {
        size_t n = 77u;
        CHECK(applog_q_peek(&q, &n) == NULL);
        CHECK_EQ(n, 0);
        CHECK(applog_q_peek(&q, NULL) == NULL);      /* len は NULL でもよい */
    }
    applog_q_pop(&q);                                  /* 空の pop は何もしない */
    CHECK_EQ(applog_q_count(&q), 0);
    CHECK_EQ(APPLOG_Q_LINES, 8);

    TEST("Q2 8行まで push 成功、9行目は false で drops==1(U-H7)");
    applog_q_init(&q);
    for (i = 0u; i < 8u; i++) {
        mkline(buf, 10u + i, i);
        CHECK(applog_q_push(&q, buf, 10u + i));
        CHECK_EQ(applog_q_count(&q), i + 1u);
        CHECK_EQ(applog_q_drops(&q), 0);
    }
    mkline(buf, 20u, 99u);
    CHECK(!applog_q_push(&q, buf, 20u));
    CHECK_EQ(applog_q_drops(&q), 1);
    CHECK_EQ(applog_q_count(&q), 8);
    CHECK(!applog_q_push(&q, buf, 20u));
    CHECK_EQ(applog_q_drops(&q), 2);
    /* 古い行が残っている(新しい行を捨てる)、順序は FIFO */
    for (i = 0u; i < 8u; i++) {
        CHECK(front_is(&q, 10u + i, i));
        applog_q_pop(&q);
        CHECK_EQ(applog_q_count(&q), 7u - i);
    }
    CHECK(applog_q_peek(&q, NULL) == NULL);
    CHECK_EQ(applog_q_drops(&q), 2);                   /* pop では drops は増えない */

    TEST("Q3 pop 後に再び push できる(循環)");
    applog_q_init(&q);
    for (i = 0u; i < 8u; i++) { mkline(buf, 5u, i); CHECK(applog_q_push(&q, buf, 5u)); }
    applog_q_pop(&q);
    mkline(buf, 7u, 100u);
    CHECK(applog_q_push(&q, buf, 7u));
    CHECK(!applog_q_push(&q, buf, 7u));
    CHECK_EQ(applog_q_drops(&q), 1);
    for (i = 1u; i < 8u; i++) { CHECK(front_is(&q, 5u, i)); applog_q_pop(&q); }
    CHECK(front_is(&q, 7u, 100u));
    applog_q_pop(&q);
    CHECK_EQ(applog_q_count(&q), 0);

    TEST("Q4 長さの境界: 255 は可、256 / 0 / 巨大 は false(drops+1)");
    applog_q_init(&q);
    mkline(buf, APPLOG_LINE_MAX - 1u, 3u);
    CHECK(applog_q_push(&q, buf, APPLOG_LINE_MAX - 1u));
    CHECK(front_is(&q, APPLOG_LINE_MAX - 1u, 3u));
    applog_q_pop(&q);
    CHECK_EQ(applog_q_drops(&q), 0);
    {
        char big[APPLOG_LINE_MAX + 64];
        memset(big, 'Z', sizeof(big));
        CHECK(!applog_q_push(&q, big, APPLOG_LINE_MAX));
        CHECK_EQ(applog_q_drops(&q), 1);
        CHECK(!applog_q_push(&q, big, APPLOG_LINE_MAX + 1u));
        CHECK_EQ(applog_q_drops(&q), 2);
        CHECK(!applog_q_push(&q, big, (size_t)-1));
        CHECK_EQ(applog_q_drops(&q), 3);
        CHECK(!applog_q_push(&q, big, 0u));
        CHECK_EQ(applog_q_drops(&q), 4);
        CHECK(!applog_q_push(&q, NULL, 5u));
        CHECK_EQ(applog_q_drops(&q), 5);
        CHECK_EQ(applog_q_count(&q), 0);                 /* 失敗では何も入らない */
    }
    CHECK(!applog_q_push(NULL, buf, 5u));                /* q==NULL: 落ちない */

    TEST("Q5 push は len バイトだけコピーして NUL 終端し、隣の行を壊さない");
    applog_q_init(&q);
    {
        char src[16];
        memset(src, 'X', sizeof(src));
        for (i = 0u; i < 8u; i++) {
            mkline(buf, APPLOG_LINE_MAX - 1u, i);        /* 全行を最大長に */
            CHECK(applog_q_push(&q, buf, APPLOG_LINE_MAX - 1u));
        }
        for (i = 0u; i < 8u; i++) { CHECK(front_is(&q, APPLOG_LINE_MAX - 1u, i)); applog_q_pop(&q); }
        /* 長い行の後に短い行: 古い内容が残らない(NUL 終端) */
        mkline(buf, 100u, 1u); CHECK(applog_q_push(&q, buf, 100u)); applog_q_pop(&q);
        mkline(buf, 3u, 2u);
        for (i = 0u; i < 8u; i++) { CHECK(applog_q_push(&q, buf, 3u)); }
        for (i = 0u; i < 8u; i++) {
            const char *p = applog_q_peek(&q, NULL);
            CHECK(p != NULL);
            if (p != NULL) { CHECK_EQ(strlen(p), 3); }
            applog_q_pop(&q);
        }
        (void)src;
    }

    TEST("Q6 drops: note_drop と飽和");
    applog_q_init(&q);
    applog_q_note_drop(&q);
    applog_q_note_drop(&q);
    CHECK_EQ(applog_q_drops(&q), 2);
    q.drops = 0xFFFFFFFDu;
    applog_q_note_drop(&q); CHECK_EQ(applog_q_drops(&q), 0xFFFFFFFEu);
    applog_q_note_drop(&q); CHECK_EQ(applog_q_drops(&q), 0xFFFFFFFFu);
    applog_q_note_drop(&q); CHECK_EQ(applog_q_drops(&q), 0xFFFFFFFFu);   /* 飽和: 0 に戻らない */
    CHECK(!applog_q_push(&q, buf, 0u));
    CHECK_EQ(applog_q_drops(&q), 0xFFFFFFFFu);
    for (i = 0u; i < 8u; i++) { mkline(buf, 4u, i); CHECK(applog_q_push(&q, buf, 4u)); }
    CHECK(!applog_q_push(&q, buf, 4u));                                   /* 満杯での失敗も飽和のまま */
    CHECK_EQ(applog_q_drops(&q), 0xFFFFFFFFu);
    applog_q_init(&q);                                                    /* init で 0 に戻る */
    CHECK_EQ(applog_q_drops(&q), 0);
    CHECK_EQ(applog_q_count(&q), 0);

    TEST("Q7 NULL 引数で落ちない");
    applog_q_init(NULL);
    applog_q_note_drop(NULL);
    applog_q_pop(NULL);
    CHECK_EQ(applog_q_drops(NULL), 0);
    CHECK_EQ(applog_q_count(NULL), 0);
    {
        size_t n = 5u;
        CHECK(applog_q_peek(NULL, &n) == NULL);
        CHECK_EQ(n, 0);
        CHECK(applog_q_peek(NULL, NULL) == NULL);
    }

    TEST("Q8 モデル照合: 乱数列の push/pop/note_drop を単純な配列と比べる(2万回)");
    {
        applog_q_t qq;
        char   model[APPLOG_Q_LINES][APPLOG_LINE_MAX];
        size_t mlen[APPLOG_Q_LINES];
        size_t mhead = 0u, mcount = 0u;
        uint32_t mdrops = 0u, rng = 12345u;
        unsigned k, bad = 0u;

        applog_q_init(&qq);
        for (k = 0u; k < 20000u; k++) {
            unsigned op;
            rng = rng * 1664525u + 1013904223u;
            op = (rng >> 16) % 10u;
            if (op < 5u) {                              /* push (長さ 0..APPLOG_LINE_MAX+2 を含む) */
                size_t len = (size_t)((rng >> 8) % (APPLOG_LINE_MAX + 3u));
                char ln[APPLOG_LINE_MAX + 4];
                bool r, expect_ok = (len != 0u) && (len < APPLOG_LINE_MAX) && (mcount < APPLOG_Q_LINES);
                mkline(ln, len, k);
                r = applog_q_push(&qq, ln, len);
                if (r != expect_ok) { bad++; }
                if (expect_ok) {
                    size_t pos = (mhead + mcount) % APPLOG_Q_LINES;
                    memcpy(model[pos], ln, len); mlen[pos] = len; mcount++;
                } else { mdrops++; }
            } else if (op < 9u) {                       /* pop (先頭を比べてから) */
                size_t n = 0u;
                const char *p = applog_q_peek(&qq, &n);
                if (mcount == 0u) {
                    if (p != NULL) { bad++; }
                } else {
                    if ((p == NULL) || (n != mlen[mhead]) || (memcmp(p, model[mhead], n) != 0) || (p[n] != '\0')) { bad++; }
                    mhead = (mhead + 1u) % APPLOG_Q_LINES; mcount--;
                }
                applog_q_pop(&qq);
            } else {
                applog_q_note_drop(&qq); mdrops++;
            }
            if ((applog_q_count(&qq) != mcount) || (applog_q_drops(&qq) != mdrops)) { bad++; }
        }
        CHECK_EQ(bad, 0);
    }

    TEST_REPORT("test_logq");
}
