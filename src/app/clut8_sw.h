/* clut8_sw.h - CLUT8 実験(docs/design.md §19)のビルドスイッチ。標準Cのみ(FIT を include しない)。
 * 0(既定)= 製品と同じ。clut8_pat.c / clut8_test.c は中身が消える(ROM・コードの差 0。リンクに --gc-sections が無いため、消さないと残る)。
 * 1 = 実験ファーム。切り替えは board_pins.h ではなくここ(または -DAPP_CLUT8_TEST=1)。 */
#ifndef CLUT8_SW_H
#define CLUT8_SW_H

#ifndef APP_CLUT8_TEST
#define APP_CLUT8_TEST       (0)
#endif

#endif /* CLUT8_SW_H */
