#ifndef TCS_SWITCH_H_
#define TCS_SWITCH_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L3 TCS switch：YES/NO 按钮切换确认（YES=ON, NO=OFF） */
void tcs_switch_init(void);
void on_tcs_switch_deal_short_key(click_e key);
void tcs_switch_destroy(void);

/* 语言切换刷新：标题为字面量，只恢复 YES/NO 选中态 */
void tcs_switch_relang(void);

#endif
