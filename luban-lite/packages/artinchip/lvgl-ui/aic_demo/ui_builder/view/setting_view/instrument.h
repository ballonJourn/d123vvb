#ifndef INSTRUMENT_H_
#define INSTRUMENT_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L2 仪器设置（INSTRUMENT SETTINGS）分类下的列表项（可扩展） */
typedef enum {
    INSTRUMENT_LANGUAGE   = 0,   /* Language settings */
    INSTRUMENT_BRIGHTNESS,       /* Brightness settings */
    INSTRUMENT_TIME,             /* Time format switching */
    INSTRUMENT_UNIT,             /* Switch between Imperial and Metric systems */
    // INSTRUMENT_CDC,              /* CDC Settings */
    INSTRUMENT_TCS,              /* TCS switch */
    INSTRUMENT_FACTORY_RESET,    /* Restore factory settings */
    INSTRUMENT_ITEM_NUM,
} instrument_item_e;

/* 进入 L2：构建 INSTRUMENT 列表控件 + 切层，并聚焦指定项（不在表中则停在首项） */
void instrument_init_at(instrument_item_e item);

/* 进入 L2 首页首槽 */
void instrument_init(void);

/* 按键处理（up/down 在选项表内循环移动，set 占位 L3，back 回 L1） */
void on_instrument_deal_short_key(click_e key);

/* 销毁 L2 控件并复位状态（退出 L2 / 关弹窗时调用，节省资源） */
void instrument_destroy(void);

/* 当前选中项（供 set_page_key 路由 L3 用） */
instrument_item_e get_l2_instrument_index(void);

/* 语言切换刷新：保留翻页/焦点位置，重跑激活与填字 */
void instrument_relang(void);

#endif
