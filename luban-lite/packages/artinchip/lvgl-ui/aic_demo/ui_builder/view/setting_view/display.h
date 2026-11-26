#ifndef DISPLAY__H_
#define DISPLAY__H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* 主题选项枚举（值与 logic.c / ui_theme.c 约定一致，勿改值：
 *   DIAPLAY_AUTO_OPTION=0 / DIAPLAY_DAY_OPTION=1 / DIAPLAY_NIGHT_OPTION=2
 *   vehicle_param_display 持久化值：0=白天 1=黑夜 2=自动） */
typedef enum {
    DIAPLAY_AUTO_OPTION   = 0,
    DIAPLAY_DAY_OPTION    = 1,
    DIAPLAY_NIGHT_OPTION  = 2,
    DIAPLAY_OPTION_MAX,
} display_option_e;

/* 进入 L4：懒加载构建 scene 控件 + 切层 + 默认聚焦 */
void display_init(void);

/* 按键处理（up/down 2项环形，set 切主题，back 回 L3） */
void on_display_deal_short_key(click_e key);

/* 销毁 L4 控件并复位状态 */
void display_destroy(void);

/* 语言切换刷新：保留焦点位置 */
void display_relang(void);

#endif
