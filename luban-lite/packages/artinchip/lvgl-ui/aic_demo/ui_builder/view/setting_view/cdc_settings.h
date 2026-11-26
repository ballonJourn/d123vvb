#ifndef CDC_SETTINGS_H_
#define CDC_SETTINGS_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L3 CDC Settings（INSTRUMENT 下 CDC REGULATION）四项选项 */
typedef enum {
    CDC_CUSTOM      = 0,   /* Custom */
    CDC_LEISURE,           /* Leisure */
    CDC_COMFORTABLE,       /* Comfortable */
    CDC_SPORTS,            /* Sports */
    CDC_ITEM_NUM,
} cdc_item_e;

/* 进入 L3：懒加载构建 CDC 控件 + 切层 + 默认聚焦 */
void cdc_settings_init(void);

/* 按键处理（up/down 环形移动，set 占位无 L4，back 回 L2 instrument） */
void on_cdc_settings_deal_short_key(click_e key);

/* 销毁 L3 控件并复位状态（退出 L3 / 关弹窗时调用） */
void cdc_settings_destroy(void);

#endif