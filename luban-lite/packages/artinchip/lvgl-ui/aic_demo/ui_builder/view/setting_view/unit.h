#ifndef UNIT__H_
#define UNIT__H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L3 公英制选项：数值与 vehicle_get/set_param_unit() 的 0=公制 / 1=英制 对齐 */
typedef enum {
    UNIT_KM_OPTION = 0,      /* Kilometer */
    UNIT_MILE_OPTION,        /* Mile      */

    UNIT_OPTION_MAX,
} unit_option_e;

/* 进入 L3：激活 tpl3 模板 + 切层 + 聚焦当前生效单位 */
void unit_init(void);

/* 按键处理（up/down 2项环形，set 保存单位并刷新主界面，back 回 L2） */
void on_unit_deal_short_key(click_e key);

/* 复位状态（退出弹窗时调用） */
void unit_destroy(void);

/* 语言切换刷新：保留焦点位置 */
void unit_relang(void);

#endif