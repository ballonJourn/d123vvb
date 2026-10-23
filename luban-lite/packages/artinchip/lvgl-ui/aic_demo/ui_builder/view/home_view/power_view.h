#ifndef HOME_POWER_VIEW_H
#define HOME_POWER_VIEW_H

#include "ui_objects.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* arc1/arc2/arc3 A8 角度遮罩：满弧图 PNG 的 per-pixel alpha mask buffer。
 * home_page.c 创建时把 arcN_mask_dsc 挂到底层 arcN_fg 的 bitmap_mask_src，
 * lv_img 重绘自动应用；mask buffer 的内容由 battery_arc_draw_cb 在每次
 * LV_EVENT_DRAW_MAIN（PREPROCESS）时按最新 SOC 实时重算，不再预渲染。 */
extern const lv_image_dsc_t arc1_mask_dsc;   /* icon_battery_arc_full.png  66×286 */
extern const lv_image_dsc_t arc2_mask_dsc;   /* icon_battery_arc_mid.png   67×312 */
extern const lv_image_dsc_t arc3_mask_dsc;   /* icon_battery_arc_low.png   69×332 */

/* 电池弧进度绘制回调：以 lv_event_get_user_data(e) 序号 0/1/2 区分各弧，
 * 在绘制时按当前 SOC 调用 lv_draw_sw_mask_angle_init + lv_draw_sw_mask_apply
 * 实时重填对应 A8 mask buffer。注册：LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS。 */
void battery_arc_draw_cb(lv_event_t * e);

/* 功率百分比刷新（同时刷 digit[3] 和 power_slider） */
void home_refresh_power(int pct);

/* 电量弧 + 电池指针 + 底部电池进度条刷新
 * （1/2/3 号包 SOC 各自独立，数据来自 BMS P1/P2/P3；进度条填充色按 SOC 档位切：
 *  0-50 黄 / 51-80 青 / 81-100 青绿） */
void home_refresh_battery(int soc1, int soc2, int soc3);

void home_refresh_battery_voltage(int vol);

#endif
