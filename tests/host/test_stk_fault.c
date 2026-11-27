/* test_stk_fault.c - APP_STK_FAULT(docs/design.md §7.4。受入 U-6・U-C7 の失敗の模擬)。wx_core.c だけをリンクする(標準 C)。
 *   ビルド: -DAPP_STK_FAULT=<0..4>。n(1〜3)= 銘柄 n-1 の要求のパスだけ壊す(銘柄コードの後ろに "ZZ")。4 = 天気(取得 ID 3)のパスだけ壊す(/v1/forecastZZ)。0 = 壊さない(製品)。
 *   期待は、壊れていない取得は固定の文字列(長さ 144 / 145 / 145 / 195)、壊れた取得は "ZZ" が入っただけ(長さ +2)。 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "wx_core.h"
#include "test_framework.h"

static const char *const K_HEAD[3] = { "GET /v8/finance/chart/6723.T", "GET /v8/finance/chart/%5EN225", "GET /v8/finance/chart/%5EGSPC" };
#define TAIL "?range=2d&interval=1d HTTP/1.0\r\nHost: query1.finance.yahoo.com\r\nUser-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n"
#define WX_TAIL "?latitude=35.68&longitude=139.76&current=temperature_2m,weather_code&timezone=Asia%2FTokyo HTTP/1.0\r\nHost: api.open-meteo.com\r\nUser-Agent: rx72n-stockview/1\r\nConnection: close\r\n\r\n"

int main(void)
{
    char buf[WX_BUF_SIZE];
    char exp[256];
    uint8_t sym;

    TEST("APP_STK_FAULT: 壊すのは銘柄 FAULT-1(4 なら天気)の要求だけ。ほかの取得・製品(0)は固定の文字列どおり");
    printf("  APP_STK_FAULT = %d\n", (int)APP_STK_FAULT);
    for (sym = 0u; sym < (uint8_t)WX_NSYM; sym++) {
        size_t n, want;
        int broken = ((unsigned)APP_STK_FAULT == ((unsigned)sym + 1u));

        (void)snprintf(exp, sizeof exp, "%s%s" TAIL, K_HEAD[sym], broken ? "ZZ" : "");
        want = strlen(exp);
        n = wx_build_request(buf, sizeof buf, sym);
        CHECK_EQ(n, want);
        CHECK(strcmp(buf, exp) == 0);
        if (!broken) {
            CHECK_EQ(n, (sym == 0u) ? 144 : 145);            /* 設計 §5.9 */
        }
        CHECK_EQ(broken, strstr(buf, "ZZ?range=") != NULL);
    }
    {                                                       /* 天気(取得 ID 3。Open-Meteo の固定の要求。195 バイト) */
        size_t n, want;
        int broken = ((unsigned)APP_STK_FAULT == 4u);

        (void)snprintf(exp, sizeof exp, "GET /v1/forecast%s%s", broken ? "ZZ" : "", WX_TAIL);
        want = strlen(exp);
        n = wx_build_request(buf, sizeof buf, (uint8_t)WX_WEATHER);
        CHECK_EQ(n, want);
        CHECK(strcmp(buf, exp) == 0);
        CHECK_EQ(n, broken ? 197 : 195);
        CHECK_EQ(broken, strstr(buf, "ZZ?latitude=") != NULL);
        CHECK_EQ(wx_build_request_t(buf, sizeof buf, WX_TGT_METEO_TCP, (uint8_t)WX_WEATHER), want);
    }
    TEST_REPORT("test_stk_fault");
}
