#ifndef VIEW_FAULTS_H_
#define VIEW_FAULTS_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"
#include "vehicle_data.h"

/* L3 View Faults 故障列表四项（取值沿用 proxy 的 veh_fault_item_e，
 * 供 vehicle_get_fault_code 直接查询，勿单独改数字） */
typedef enum {
    FAULT_BMS1 = VEH_FAULT_BMS1,
    FAULT_BMS2 = VEH_FAULT_BMS2,
    FAULT_BMS3 = VEH_FAULT_BMS3,
    FAULT_MCU  = VEH_FAULT_MCU,
    FAULT_ITEM_NUM = VEH_FAULT_ITEM_NUM,
} fault_item_e;

/* 进入 L3：懒加载构建 View Faults 控件 + 切层 + 默认聚焦 */
void view_faults_init(void);

/* 按键处理（up/down 环形移动；set 进故障详情态，详情态内 back 回列表，列表态 back 回 L2） */
void on_view_faults_deal_short_key(click_e key);

/* 销毁 L3 控件并复位状态（退出 L3 / 关弹窗时调用） */
void view_faults_destroy(void);

/* 语言切换刷新：保留焦点位置（列表项为器件缩写，仅标题随语言变化） */
void view_faults_relang(void);

#endif
