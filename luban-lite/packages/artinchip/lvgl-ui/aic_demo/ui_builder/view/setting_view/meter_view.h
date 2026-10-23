#ifndef METER_VIEW_H_
#define METER_VIEW_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L2 Meter view 分类下的三项列表 */
typedef enum {
    METER_TIRE    = 0,   /* Check Tire Pressure */
    METER_FAULTS,        /* View Faults */
    METER_VERSION,       /* View Software Version */
    METER_ITEM_NUM,
} meter_item_e;

/* 进入 L2：懒加载构建 Meter view 列表控件 + 切层 + 默认聚焦 */
void meter_view_init(void);

/* 按键处理（up/down 环形移动，set 占位 L3，back 回 L1） */
void on_meter_view_deal_short_key(click_e key);

/* 销毁 L2 控件并复位状态（退出 L2 / 关弹窗时调用） */
void meter_view_destroy(void);

/* 当前选中项 */
meter_item_e get_l2_meter_index(void);

/* 语言切换刷新：保留焦点位置 */
void meter_view_relang(void);

#endif
