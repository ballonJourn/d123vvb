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
        break;
    case WINDOW_MENU:
        setting_page_create(&ui_manager);
        scr = setting_page_get(&ui_manager)->obj ;
        break;
    case WINDOW_MOBILE:
        phone_page_create(&ui_manager);
        scr = phone_page_get(&ui_manager)->obj ;
        break;
    default:
        LV_LOG_WARN("Unknown window type: %d", new_window);
        return;
    }
    
    if (scr) {
      lv_scr_load(scr);
    }

    if (new_window == WINDOW_MAIN && active_window != WINDOW_MAIN) {
        if (previous_scr)
        {
            lv_obj_delete(previous_scr) ;
            previous_scr = NULL ;
            lv_log("lv_obj_delete successed\n") ;
        }
    }

    previous_scr = scr ;
    active_window = new_window;
}

window_e window_manager_get() {
  return active_window;
}
