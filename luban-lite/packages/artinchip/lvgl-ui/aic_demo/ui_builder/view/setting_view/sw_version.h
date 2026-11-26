#ifndef SW_VERSION_H_
#define SW_VERSION_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* 进入 L3：懒加载构建版本页控件 + 切层 + 填入当前版本号 */
void sw_version_init(void);

/* 布局构建（per-resolution，定义在各分辨率 setting_page.c）：创建 L3 层与控件 */
void sw_version_build(void);

/* 按键处理（只读页：仅 back 回 L2 meter view） */
void on_sw_version_deal_short_key(click_e key);

/* 隐去本页并复位状态（层常驻，不删除） */
void sw_version_destroy(void);

/* 语言切换刷新：重设字体并重填标题 / 版本号文本 */
void sw_version_relang(void);

#endif