/* touch_ft5x.h - 静電容量タッチIC(回路図: FT5206)。SCI6 簡易I2C。ハード依存(設計 §9) */
#ifndef TOUCH_FT5X_H
#define TOUCH_FT5X_H

#include <stdint.h>
#include <stdbool.h>
#include "nav_logic.h"

/* RSTピン制御 -> r_sci_iic_rx (CH6) を開く。失敗時 false(画像表示は続ける)。
 * RST解除後に約300ms待つ(ブロッキング)。 */
bool touch_init(void);

/* 1回読む(ブロッキング、最大約20msのタイムアウト付き)。
 * 成功: true(触れていない場合も true)。I2C失敗・異常値: false。 */
bool touch_read(nav_sample_t *out);

#endif /* TOUCH_FT5X_H */
