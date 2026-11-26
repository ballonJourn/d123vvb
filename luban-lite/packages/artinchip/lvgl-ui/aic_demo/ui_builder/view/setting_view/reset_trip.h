#ifndef RESET_TRIP_H_
#define RESET_TRIP_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L2 Reset trip mileage：两个互斥的小计里程清零方式
 *   槽0 "Reset after power off" → VEHICLE_RESET_AFTER_POWER_OFF(1)：IGN OFF 时清零
 *   槽1 "Reset by button"       → VEHICLE_RESET_BY_BUTTON(0)      ：主页长按 BACK 清零
 * SET 对焦点槽位播放淡出淡入后即时写入并落盘（无确认层，取值见 proxy/vehicle_argument.h） */
void reset_trip_init(void);
void on_reset_trip_deal_short_key(click_e key);
void reset_trip_destroy(void);

/* 语言切换刷新：按新语言重填槽位文案，保留焦点 */
void reset_trip_relang(void);

#endif