#ifndef BRIGHTNESS_BAR_H__
#define BRIGHTNESS_BAR_H__

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* 进入 L4：懒加载构建亮度进度条控件 + 切层 */
void brightness_bar_init(void);

/* 布局构建（per-resolution，定义在各分辨率 setting_page.c）：创建 L4 层与控件 */
void brightness_bar_build(void);

/* 按键处理（up/down 调5级进度并转手动，set 切换自动亮度，back 回 L3） */
void on_brightness_bar_deal_short_key(click_e key);

/* 销毁 L4 控件并复位状态 */
void brightness_bar_destroy(void);

/* 语言切换刷新：该层仅标题随语言变化 */
void brightness_bar_relang(void);

#endif
