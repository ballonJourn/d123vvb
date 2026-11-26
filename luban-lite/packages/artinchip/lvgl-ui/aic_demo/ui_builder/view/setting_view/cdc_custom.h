#ifndef CDC_CUSTOM_H_
#define CDC_CUSTOM_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L4 Custom（CDC Settings → Custom）两项 Previous / After */
/* 进入 L4：复用 tpl3 模板 + 隐藏第3槽 + 默认聚焦 */
void cdc_custom_init(void);

/* 按键处理（up/down 环形移动，set 占位无下级，back 回 L3 cdc_settings） */
void on_cdc_custom_deal_short_key(click_e key);

/* 销毁 L4 控件并复位状态（退出 L4 / 关弹窗时调用） */
void cdc_custom_destroy(void);

#endif