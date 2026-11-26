#ifndef HOME_SPEED_VIEW_H
#define HOME_SPEED_VIEW_H

#include "definition.h"
#include "ui_objects.h"
#include <stdbool.h>
#include <stdio.h>

/* 速度值刷新（同时刷 digit[3]、dashboard_progress_0 旋转、pointer 旋转） */
void home_refresh_speed(uint32_t speed);

void home_refresh_speed_power(uint32_t pct);

/* 单位切换（km/h vs mph） */
void home_refresh_unit(unit_e unit);

/* 速度单位文本切换（kmh_lab：km/h vs mph） */
void home_refresh_speed_unit(unit_e unit);

/* 停车状态切换 */
void home_refresh_stop_state(bool istop);

/* 驾驶模式（E/N/S）切换 bg_halo + driving_mode */
void home_refresh_drv_mode(drv_mode_e m);

/* 档位（P/D/R）切换 gear_P/D/R 显示 */
void home_refresh_gear(gear_e g);

/* 累加器平滑（logic.c 50ms timer 周期调用） */
void speed_view_run(int target);

/* 取累加器当前值 */
int speed_view_current(void);

/* 强制设置累加器值 */
void speed_view_set_current(int value);

/* 功率表累加器（home_refresh_speed_power 的数据源，模式与速度表相同） */
void power_view_run(int target);

/* 取功率累加器当前值 */
int power_view_current(void);

/* 强制设置功率累加器值 */
void power_view_set_current(int value);

#endif
