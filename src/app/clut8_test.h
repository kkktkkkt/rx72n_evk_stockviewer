/* clut8_test.h - CLUT8 表示の実機実験(docs/design.md §19)。APP_CLUT8_TEST = 1 の時だけ実体がある(clut8_sw.h) */
#ifndef CLUT8_TEST_H
#define CLUT8_TEST_H

#include "platform.h"
#include "r_glcdc_rx_if.h"

/* lcd_build_cfg が作った cfg の GR2 を CLUT8(行幅 512、面A、パレットA)に書き換える。lcd_init() の中(R_GLCDC_Open の前)から呼ばれる。
 * パレットAは clut8_test_main が lcd_init の前に作る */
void clut8_test_patch_cfg(glcdc_cfg_t *c);

/* 実験の本体。戻らない。app_main() の先頭から呼ぶ */
void clut8_test_main(void);

#endif /* CLUT8_TEST_H */
