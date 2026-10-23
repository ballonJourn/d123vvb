#include "clock_view.h"
#include "lvgl.h"
#include "ui_objects.h"
#include "definition.h"

void home_refresh_clock_min(int _min)
{
    home_page_t *page = home_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, min) 

    lv_label_set_text_fmt(min , "%02d" , _min) ;
}

void home_refresh_clock_hour(int _hour)
{
    home_page_t *page = home_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, hour) 

    lv_label_set_text_fmt(hour , "%02d" , _hour) ;
} 

void home_refresh_clock_colon(bool visiable)
{
    home_page_t *page = home_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, colon)

    if (visiable) 
        lv_obj_remove_flag(colon , LV_OBJ_FLAG_HIDDEN) ;
    else 
        lv_obj_add_flag(colon , LV_OBJ_FLAG_HIDDEN) ;
    
} 