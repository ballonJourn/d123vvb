#ifndef RESET_TRIP_H_
#define RESET_TRIP_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L2 Reset trip mileage：两个互斥的小计里程清零方式
 *   槽0 "Reset after power off" → VEHICLE_RESET_AFTER_POWER_OFF(1)：IGN OFF 时清零
 *   槽1 "Reset by button"       → VEHICLE_RESET_BY_BUTTON(0)      ：主页长按 BACK 清零
 * SET 先进 YES/NO 确认层，YES 才写参数（取值详见 proxy/vehicle_argument.h） */
void reset_trip_init(void);
void on_reset_trip_deal_short_key(click_e key);
void reset_trip_destroy(void);

/* 语言切换刷新：按当前所在层（YES/NO 确认层 / 选项列表）重填文案，保留焦点 */
void reset_trip_relang(void);

#endif