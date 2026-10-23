#ifndef POWER_DOWN_H_
#define POWER_DOWN_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L2 Power down the entire vehicle：YES/NO 确认，YES 发 0x451 Power_Down=1，NO 无动作 */
void power_down_init(void);
void on_power_down_deal_short_key(click_e key);
void power_down_destroy(void);

/* 语言切换刷新：保留 YES/NO 选中态 */
void power_down_relang(void);

#endif