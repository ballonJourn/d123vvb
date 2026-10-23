/*
 * home_page_key.c - 主页按键分发（800×480 设计稿）
 *
 * 4 键交互：
 *   UP/DOWN    翻驾驶模式选择栏（ECO/NORMAL/SPORT/SETTING 循环）
 *   SET        选中 SETTING 时跳 WINDOW_MENU；其余模式不跳转
 *   BACK       回到 MODE_NORMAL
 *   LONG_SET   跳 WINDOW_MOBILE（手机互联）
 *   LONG_BACK  清 TRIP（仅"按键清零"方式，见 reset_type）
 */

#include "home_page_key.h"
#include "window_manager.h"
#include "ui_objects.h"
#include "mileage_calc.h"
#include "mode_view.h"         /* mode_index_e / home_refresh_mode_index */
#include "vehicle_argument.h"  /* vehicle_get_param_reset_type */

void home_manager_click(const click_e click)
{
    static mode_index_e g_mode_idx = MODE_NORMAL;

    switch (click) {
    case CLICK_SHORT_UP:
        // g_mode_idx = (mode_index_e)((g_mode_idx == MODE_NORMAL) ? (MODE_MAX - 1) : (g_mode_idx - 1));
        // home_refresh_mode_index(g_mode_idx);
        break;

    case CLICK_SHORT_DOWN:
        // g_mode_idx = (mode_index_e)((g_mode_idx + 1) % MODE_MAX);
        // home_refresh_mode_index(g_mode_idx);
        break;

    case CLICK_SHORT_SET:
            window_manager_set_target(WINDOW_MENU);
        break;

    case CLICK_SHORT_BACK:
        // g_mode_idx = MODE_NORMAL;
        // home_refresh_mode_index(g_mode_idx);
        break;

    case CLICK_LONG_SET:
        // window_manager_set_target(WINDOW_MOBILE);
        break;

    case CLICK_LONG_BACK:
        /* 清零方式为"按键清零"才响应；"下电清零"由 mw/power_manage/ign_check.c 在下电时负责 */
        if (vehicle_get_param_reset_type() == VEHICLE_RESET_BY_BUTTON)
            mileage_clear_trip();
        break;

    default:
        break;
    }
}

