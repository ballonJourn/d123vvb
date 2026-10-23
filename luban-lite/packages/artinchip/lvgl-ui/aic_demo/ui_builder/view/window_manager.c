#include "window_manager.h"
#include "ui_init.h"
#include "aic_ui.h"
#include "logic.h"
#include "vehicle_argument.h"

static window_e active_window =  WINDOW_MAIN ;//WINDOW_MAIN;
static lv_obj_t* previous_scr = NULL;

bool window_manager_is_active(window_e window_id) 
{
  return active_window == window_id;
}

void window_manager_set_target(window_e new_window)
{
    if (new_window >= WINDOW_NULL || new_window == active_window)
        return ;

    lv_obj_t * scr = NULL ;

    switch (new_window)
    {
    case WINDOW_MAIN:
        scr = home_page_get(&ui_manager)->obj;
        global_refresh_unit(vehicle_get_param_unit()) ;
        if (active_window == WINDOW_MENU) {
            /* 弹窗叠加在 home 上，home 已是活动屏：仅隐藏弹窗，不切屏不删除 */
            setting_popup_hide();
        } else {
            /* 自 phone 等全屏屏退回：切回 home 并删除上一屏 */
            lv_scr_load(scr);
            if (previous_scr) {
                lv_obj_delete(previous_scr);
                previous_scr = NULL;
                lv_log("lv_obj_delete successed\n");
            }
        }
        break;
    case WINDOW_MENU:
        /* 构建或复用弹窗并显示；home 保持活动屏，弹窗作为 home 子对象显示 */
        setting_page_create(&ui_manager);
        setting_popup_show();
        scr = home_page_get(&ui_manager)->obj;
        if (lv_screen_active() != scr)
            lv_scr_load(scr);
        break;
    case WINDOW_MOBILE:
        phone_page_create(&ui_manager);
        scr = phone_page_get(&ui_manager)->obj ;
        lv_scr_load(scr);
        break;
    case WINDOW_DVR:
    case WINDOW_CP:
    case WINDOW_LINK:
        /* 预留：移植自 AWTK home_view dock 入口；
         * 顶级页面实现未移植，此处只 log 不切换，避免崩溃。 */
        LV_LOG_WARN("Window %d (AWTK reserved) not implemented, stay on current", (int)new_window);
        return;
    default:
        LV_LOG_WARN("Unknown window type: %d", new_window);
        return;
    }

    previous_scr = scr ;
    active_window = new_window;
}

window_e window_manager_get() {
  return active_window;
}
