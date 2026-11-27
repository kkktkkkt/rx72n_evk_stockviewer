/* clut8_ref.c - CLUT8 実験(docs/design.md §19)の基準データをホストで作る。
 * ビルド: gcc -std=gnu99 -DAPP_CLUT8_TEST=1 -I src/app tools/clut8_ref.c src/app/clut8_pat.c -o jobs/c8/clut8_ref.exe
 * 使い方: clut8_ref <出力フォルダ>(既存)。書くファイル: ref_face_a.bin ref_face_b.bin(各 139,264)、
 *   ref_pal_a.bin ref_pal_b.bin(マーカー赤)ref_pal_b_green.bin(各 1,024。リトルエンディアンの 32bit 語 0xAARRGGBB) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clut8_pat.h"

static int put(const char *dir, const char *name, const void *p, size_t n)
{
    char  path[1024];
    FILE *f;
    int   ok;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    ok = (fwrite(p, 1, n, f) == n);
    fclose(f);
    return ok ? 0 : 1;
}

static int put_pal(const char *dir, const char *name, const uint32_t *pal)
{
    uint8_t  b[C8_PAL_N * 4u];
    unsigned i;

    for (i = 0u; i < C8_PAL_N; i++) {            /* ホストの並びに依らずリトルエンディアンで書く */
        b[(i * 4u) + 0u] = (uint8_t)(pal[i]);
        b[(i * 4u) + 1u] = (uint8_t)(pal[i] >> 8);
        b[(i * 4u) + 2u] = (uint8_t)(pal[i] >> 16);
        b[(i * 4u) + 3u] = (uint8_t)(pal[i] >> 24);
    }
    return put(dir, name, b, sizeof(b));
}

int main(int argc, char **argv)
{
    static uint8_t  face[C8_FB_BYTES];
    static uint32_t pal[C8_PAL_N];
    int rc = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: clut8_ref <out-dir>\n");
        return 2;
    }
    c8pat_face(face, false); rc |= put(argv[1], "ref_face_a.bin", face, sizeof(face));
    c8pat_face(face, true);  rc |= put(argv[1], "ref_face_b.bin", face, sizeof(face));
    c8pat_pal(pal, false, false); rc |= put_pal(argv[1], "ref_pal_a.bin", pal);
    c8pat_pal(pal, true, false);  rc |= put_pal(argv[1], "ref_pal_b.bin", pal);
    c8pat_pal(pal, true, true);   rc |= put_pal(argv[1], "ref_pal_b_green.bin", pal);
    return rc;
}
