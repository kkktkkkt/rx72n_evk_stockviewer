/* fb_hostrender.c - 状態ファイル(docs/design.md §3.12・§13.10)を、ファームと同じ ui_render で PC 上に描いて、8bpp(CLUT8)の面を .c8 にする(tools/c8png.py が使う)。
 * 使い方: fb_hostrender <state.txt> <out.c8> [--pal <out.pal>]
 *   state.txt は "キー=整数" の行(# から行末はコメント。書かれないキーは 0。clock と upd と clk.clock だけ 65535)。
 *   株価のキー: net busy have_any stale_all partial clock upd と r0.* r1.* r2.*(have stale fail mkt price diff bp)。
 *   時計・天気のキー(§13.10): scr(1 = 時計・天気画面)clk.clock clk.have clk.kind clk.temp clk.mon clk.day clk.wday。
 *   out.c8 = 512 x 272 x 8bit = 139,264 バイト(行幅 GFX_STRIDE。右の 32 列は 0 のまま。比較は x < 480 だけ)。
 *   --pal = 画面のパレット(ui_pal_ptr(ui_pal_id)。256 x 32bit、リトルエンディアンの 0xAARRGGBB。1,024 バイト)。株価 = ui_pal、時計・天気 = clk_pal[kind]。実機の GLCDC に書いたものと同じ。
 * ビルド: gcc -std=gnu99 -O1 -I src/app tools/fb_hostrender.c src/app/ui_render.c src/app/ui_clock.c src/app/clk_core.c src/app/ui_logic.c src/app/stk_core.c src/app/gfx.c
 *         src/app/ui_layout.c src/app/ui_gfx_data.c src/app/ui_strips.c src/app/clk_data.c -o <out> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ui_logic.h"
#include "ui_render.h"
#include "ui_assets.h"
#include "clk_assets.h"
#include "gfx.h"

#ifndef HOSTRENDER_NO_MAIN
static uint8_t g_fb[(size_t)GFX_STRIDE * GFX_H];
#endif

static int set_row(stk_row_t *r, const char *k, long n)
{
    if (!strcmp(k, "have")) r->have = (uint8_t)n;
    else if (!strcmp(k, "stale")) r->stale = (uint8_t)n;
    else if (!strcmp(k, "fail")) r->fail = (uint8_t)n;
    else if (!strcmp(k, "mkt")) r->mkt = (uint8_t)n;
    else if (!strcmp(k, "price")) r->price_x100 = (int32_t)n;
    else if (!strcmp(k, "diff")) r->diff_x100 = (int32_t)n;
    else if (!strcmp(k, "bp")) r->pct_bp = (int32_t)n;
    else return 1;
    return 0;
}

/* 読み込み先。c = NULL なら時計・天気のキー(scr・clk.*)は未知のキー(株価の状態ファイルに混ざったら失敗) */
typedef struct { stk_view_t *v; clk_view_t *c; uint8_t *scr; } load_t;

static int set_clk_key(load_t *l, const char *k, long n)
{
    if (l->c == NULL) return 1;
    if (!strcmp(k, "scr")) *l->scr = (uint8_t)n;
    else if (!strcmp(k, "clk.clock")) l->c->clock_min = (uint16_t)n;
    else if (!strcmp(k, "clk.have")) l->c->have = (uint8_t)n;
    else if (!strcmp(k, "clk.kind")) l->c->kind = (uint8_t)n;
    else if (!strcmp(k, "clk.temp")) l->c->temp = (int8_t)n;
    else if (!strcmp(k, "clk.mon")) l->c->mon = (uint8_t)n;
    else if (!strcmp(k, "clk.day")) l->c->day = (uint8_t)n;
    else if (!strcmp(k, "clk.wday")) l->c->wday = (uint8_t)n;
    else return 1;
    return 0;
}

static int set_key(load_t *l, const char *k, long n)
{
    stk_view_t *v = l->v;

    if (!strncmp(k, "clk.", 4) || !strcmp(k, "scr")) return set_clk_key(l, k, n);
    if (!strcmp(k, "net")) v->net = (uint8_t)n;
    else if (!strcmp(k, "busy")) v->busy = (uint8_t)n;
    else if (!strcmp(k, "have_any")) v->have_any = (uint8_t)n;
    else if (!strcmp(k, "stale_all")) v->stale_all = (uint8_t)n;
    else if (!strcmp(k, "partial")) v->partial = (uint8_t)n;
    else if (!strcmp(k, "clock")) v->clock_min = (uint16_t)n;
    else if (!strcmp(k, "upd")) v->upd_min = (uint16_t)n;
    else if ((k[0] == 'r') && (k[1] >= '0') && (k[1] <= '2') && (k[2] == '.')) return set_row(&v->row[k[1] - '0'], k + 3, n);
    else return 1;
    return 0;
}

#ifndef HOSTRENDER_NO_MAIN
static int write_file(const char *path, const void *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) { perror(path); return 1; }
    ok = (fwrite(p, 1, n, f) == n);
    fclose(f);
    return ok ? 0 : 1;
}
#endif

/* 状態ファイルを読む。未知のキーは失敗(綴りの誤りを黙って捨てない)。c = NULL なら株価の状態ファイルだけ */
static int load_state(stk_view_t *v, clk_view_t *c, uint8_t *scr, const char *path)
{
    char line[256];
    FILE *f = fopen(path, "r");
    load_t l;

    memset(v, 0, sizeof(*v));
    v->clock_min = 65535u;
    v->upd_min = 65535u;
    l.v = v; l.c = c; l.scr = scr;
    if (c != NULL) {
        memset(c, 0, sizeof(*c));
        c->clock_min = 65535u;
        *scr = 0u;
    }
    if (!f) { perror(path); return 2; }
    while (fgets(line, sizeof(line), f)) {
        char *eq, *h = strchr(line, '#');
        char *k = line;
        if (h) *h = 0;
        while (*k == ' ' || *k == '\t') k++;
        eq = strchr(k, '=');
        if (!eq) continue;
        *eq = 0;
        if (set_key(&l, k, strtol(eq + 1, NULL, 10))) { fprintf(stderr, "%s: 未知のキー %s\n", path, k); fclose(f); return 2; }
    }
    fclose(f);
    return 0;
}

int hostrender_load(stk_view_t *v, const char *path)
{
    return load_state(v, NULL, NULL, path);
}

/* 株価(S*.state)も時計・天気(C*.state)も読む。scr = 1 なら時計・天気画面にする */
int hostrender_load_ui(ui_t *u, const char *path)
{
    uint8_t scr = 0u;
    int rc = load_state(&u->view, &u->cview, &scr, path);

    if (rc == 0) { ui_set_screen(u, scr, 0u); }
    return rc;
}

#ifndef HOSTRENDER_NO_MAIN
int main(int argc, char **argv)
{
    ui_t u;
    const char *pal_out = NULL;
    int i, rc;

    if (argc < 3) { fprintf(stderr, "usage: fb_hostrender state.txt out.c8 [--pal out.pal]\n"); return 2; }
    for (i = 3; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--pal")) { pal_out = argv[i + 1]; }
    }
    ui_init(&u, 0u);
    rc = hostrender_load_ui(&u, argv[1]);
    if (rc != 0) { return rc; }
    ui_render(&u, g_fb);
    if (write_file(argv[2], g_fb, sizeof(g_fb)) != 0) { return 2; }
    if (pal_out != NULL) {
        uint8_t b[UI_PAL_N * 4u];
        const uint32_t *pal = ui_pal_ptr(ui_pal_id(&u));
        unsigned k;
        for (k = 0u; k < UI_PAL_N; k++) {       /* ホストの並びに依らずリトルエンディアンで書く */
            b[k * 4u + 0u] = (uint8_t)(pal[k]);
            b[k * 4u + 1u] = (uint8_t)(pal[k] >> 8);
            b[k * 4u + 2u] = (uint8_t)(pal[k] >> 16);
            b[k * 4u + 3u] = (uint8_t)(pal[k] >> 24);
        }
        if (write_file(pal_out, b, sizeof(b)) != 0) { return 2; }
    }
    return 0;
}
#endif
