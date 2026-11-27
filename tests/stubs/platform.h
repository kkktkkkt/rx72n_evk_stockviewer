/* tests/stubs/platform.h - ホスト構文検査(-fsyntax-only)専用の仮 platform.h。製品ではない。
 * FIT の実ヘッダ(src/smc_gen)を include できる最小限だけを定義する。
 * 値・型は src/smc_gen/r_bsp/mcu/all/r_bsp_common.h と r_rx_compiler.h の実物に合わせてある。
 * (実 platform.h を使う経路は run.sh の S6/S7-A。こちらは実 platform.h を使えない環境向けの代替) */
#ifndef STUB_PLATFORM_H
#define STUB_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define BSP_MCU_RX72N          (1)
#define BSP_PACKAGE_PINS       (144)
#define R_BSP_VERSION_MAJOR    (7)
#define R_BSP_VERSION_MINOR    (71)

#define R_BSP_PRAGMA_UNPACK
#define R_BSP_PRAGMA_PACKOPTION
#define R_BSP_ATTRIB_STRUCT_BIT_ORDER_LEFT_4(a, b, c, d)          struct { a; b; c; d; }
#define R_BSP_ATTRIB_STRUCT_BIT_ORDER_LEFT_6(a, b, c, d, e, f)    struct { a; b; c; d; e; f; }
#define R_BSP_VOLATILE_EVENACCESS volatile
#define FIT_NO_PTR   ((void *)0x10000000)
#define FIT_NO_FUNC  ((void *)0x10000000)

/* r_bsp_common.h と同じ */
typedef enum
{
    BSP_DELAY_MICROSECS = 1000000,
    BSP_DELAY_MILLISECS = 1000,
    BSP_DELAY_SECS = 1
} bsp_delay_units_t;
typedef void (*fit_callback_t)(void *p_args);
bool R_BSP_SoftwareDelay(uint32_t delay, bsp_delay_units_t units);

/* GLCDC レジスタの最小の偽物(lcd_panel.c の lcd_flip_busy が GLCDC.GR2VEN.BIT.VEN を読む。実物は iodefine.h) */
typedef struct { union { unsigned long LONG; struct { unsigned long VEN : 1; unsigned long : 31; } BIT; } GR2VEN; } stub_glcdc_t;
extern volatile stub_glcdc_t stub_glcdc;
#define GLCDC stub_glcdc

#endif /* STUB_PLATFORM_H */
