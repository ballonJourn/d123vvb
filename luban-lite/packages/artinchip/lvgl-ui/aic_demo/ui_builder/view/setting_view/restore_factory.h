#ifndef RESTORE_FACTORY_H_
#define RESTORE_FACTORY_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L3 Restore factory settings：YES/NO 按钮切换确认 */
void restore_factory_init(void);
void on_restore_factory_deal_short_key(click_e key);
void restore_factory_destroy(void);

/* 语言切换刷新：保留 YES/NO 选中态 */
void restore_factory_relang(void);

#endif
