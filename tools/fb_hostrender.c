/* fb_hostrender.c - 実機から読んだ状態(s_ui)を、ファームと同じ ui_render で PC 上に描いて、8bpp(CLUT8)の面を .c8 にする(tools/fb2png.py が使う)。
 * 使い方: fb_hostrender <state.txt> <out.c8> [--pal <out.pal>]
 *   state.txt は "キー=整数" の行(キー: v.scr v.set v.bath v.fill v.flow v.clock v.bar、ov.* 同、tr_on tr_fwd tr_dx demo fb_btn fb_phase、
 *   天気・Wi-Fi(design §22.8): wx.net wx.busy wx.have wx.kind wx.temp wx.stale wx.clkext wx.upd wx.clock wx.month wx.day wx.wday wx.spin。
 *   wx.* と無いキーは ui_init の既定(OFFLINE・未取得・旧デモ時計)= 天気が無い従来の状態ファイルのまま描ける)。
 *   out.c8 = 512 x 272 x 8bit = 139,264 バイト(行幅 GFX_STRIDE。右の 32 列は 0 のまま。比較は x < 480 だけ)。
 *   --pal = ui_pal(256 x 32bit、リトルエンディアンの 0xAARRGGBB。1,024 バイト)。実機の GLCDC に書いたものと同じ。
 * ビルド: gcc -std=gnu99 -O1 -I src/app tools/fb_hostrender.c src/app/ui_render.c src/app/ui_logic.c src/app/gfx.c src/app/ui_bg_*.c src/app/ui_gfx_data.c src/app/ui_layout.c -o jobs/fb/fb_hostrender.exe */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ui_logic.h"
#include "ui_render.h"
#include "gfx.h"

static uint8_t g_fb[(size_t)GFX_STRIDE * GFX_H];

static void setv(ui_vals_t *v, const char *k, long n)
{
    if (!strcmp(k, "scr")) v->scr = (uint8_t)n;
    else if (!strcmp(k, "set")) v->set = (uint8_t)n;
    else if (!strcmp(k, "bath")) v->bath_x10 = (uint16_t)n;
    else if (!strcmp(k, "fill")) v->fill_x10 = (uint16_t)n;
    else if (!strcmp(k, "flow")) v->flow_x10 = (uint16_t)n;
    else if (!strcmp(k, "clock")) v->clock_min = (uint16_t)n;
    else if (!strcmp(k, "bar")) v->bar_ms = (uint16_t)n;
}

static void setwx(ui_t *u, const char *k, long n)
{
    if (!strcmp(k, "net")) u->wx.net = (uint8_t)n;
    else if (!strcmp(k, "busy")) u->wx.busy = (uint8_t)n;
    else if (!strcmp(k, "have")) u->wx.have = (uint8_t)n;
    else if (!strcmp(k, "kind")) u->wx.kind = (uint8_t)n;
    else if (!strcmp(k, "temp")) u->wx.temp_x10 = (int16_t)n;
    else if (!strcmp(k, "stale")) u->wx.stale = (uint8_t)n;
    else if (!strcmp(k, "clkext")) u->wx.clk_ext = (uint8_t)n;
    else if (!strcmp(k, "upd")) u->wx.upd_min = (uint16_t)n;
    else if (!strcmp(k, "clock")) u->wx.clock_min = (uint16_t)n;
    else if (!strcmp(k, "month")) u->wx.month = (uint8_t)n;
    else if (!strcmp(k, "day")) u->wx.day = (uint8_t)n;
    else if (!strcmp(k, "wday")) u->wx.wday = (uint8_t)n;
    else if (!strcmp(k, "spin")) u->spin = (uint8_t)n;
}

static int write_file(const char *path, const void *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) { perror(path); return 1; }
    ok = (fwrite(p, 1, n, f) == n);
    fclose(f);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    ui_t u;
    char line[256];
    FILE *f;
    const char *pal_out = NULL;
    int i;

    if (argc < 3) { fprintf(stderr, "usage: fb_hostrender state.txt out.c8 [--pal out.pal]\n"); return 2; }
    for (i = 3; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--pal")) { pal_out = argv[i + 1]; }
    }
    memset(&u, 0, sizeof(u));
    ui_init(&u, 0u, 479u, 271u, 0u);
    f = fopen(argv[1], "r");
    if (!f) { perror("state"); return 2; }
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        long n;
        if (!eq) continue;
        *eq = 0; n = strtol(eq + 1, NULL, 10);
        if (!strncmp(line, "v.", 2)) setv(&u.v, line + 2, n);
        else if (!strncmp(line, "ov.", 3)) setv(&u.ov, line + 3, n);
        else if (!strncmp(line, "wx.", 3)) setwx(&u, line + 3, n);
        else if (!strcmp(line, "tr_on")) u.tr_on = (uint8_t)n;
        else if (!strcmp(line, "tr_fwd")) u.tr_fwd = (uint8_t)n;
        else if (!strcmp(line, "tr_dx")) u.tr_dx = (uint16_t)n;
        else if (!strcmp(line, "demo")) u.demo = (uint8_t)n;
        else if (!strcmp(line, "fb_btn")) u.fb_btn = (uint8_t)n;
        else if (!strcmp(line, "fb_phase")) u.fb_phase = (uint8_t)n;
    }
    fclose(f);
    ui_render(&u, g_fb);
    if (write_file(argv[2], g_fb, sizeof(g_fb)) != 0) { return 2; }
    if (pal_out != NULL) {
        uint8_t b[UI_PAL_N * 4u];
        unsigned k;
        for (k = 0u; k < UI_PAL_N; k++) {       /* ホストの並びに依らずリトルエンディアンで書く */
            b[k * 4u + 0u] = (uint8_t)(ui_pal[k]);
            b[k * 4u + 1u] = (uint8_t)(ui_pal[k] >> 8);
            b[k * 4u + 2u] = (uint8_t)(ui_pal[k] >> 16);
            b[k * 4u + 3u] = (uint8_t)(ui_pal[k] >> 24);
        }
        if (write_file(pal_out, b, sizeof(b)) != 0) { return 2; }
    }
    return 0;
}
