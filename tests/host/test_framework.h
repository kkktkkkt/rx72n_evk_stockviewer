/* test_framework.h - 最小のテスト枠(ホスト専用)。標準Cのみ。 */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>

static int g_tests_run;
static int g_tests_failed;

#define TEST(name) printf("[%s]\n", (name))

#define CHECK(cond) \
    do { \
        g_tests_run++; \
        if (!(cond)) { \
            g_tests_failed++; \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        long long va_ = (long long)(a); \
        long long vb_ = (long long)(b); \
        g_tests_run++; \
        if (va_ != vb_) { \
            g_tests_failed++; \
            printf("  FAIL %s:%d: %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); \
        } \
    } while (0)

/* 終了コード: 失敗があれば 1 */
#define TEST_REPORT(suite) \
    do { \
        printf("%s: %d checks, %d failed\n", (suite), g_tests_run, g_tests_failed); \
        return (g_tests_failed == 0) ? 0 : 1; \
    } while (0)

#endif /* TEST_FRAMEWORK_H */
