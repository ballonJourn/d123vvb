#ifndef BRIGHTNESS_H__
#define BRIGHTNESS_H__

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L3 brightness 两子选项：Scene → L4 主题切换，Brightness → L4 亮度进度条 */
typedef enum {
    BRIGHTNESS_L3_SCENE     = 0,   /* Scene      */
    BRIGHTNESS_L3_BRIGHTNESS,     /* Brightness */
    BRIGHTNESS_L3_ITEM_NUM,
} brightness_l3_item_e;

/* 进入 L3：懒加载构建 brightness 控件 + 切层 + 默认聚焦 Scene */
void brightness_init(void);

/* 按键处理（up/down 2项环形，set 分派 L4，back 回 L2） */
void on_brightness_deal_short_key(click_e key);

/* 销毁 L3 控件并复位状态 */
void brightness_destroy(void);

/* 当前选中子项 */
brightness_l3_item_e get_l3_brightness_focus(void);

/* 语言切换刷新：保留焦点位置 */
void brightness_relang(void);

#endif
