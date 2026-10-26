#include "unit.h"
#include <stdio.h>
#include "vehicle_argument.h"
#include "setting_menu.h"
#include "definition.h"
#include "aic_lang.h"
#include "logic.h"

static lv_obj_t* set_unit_widget[UNIT_OPTION_MAX] = { NULL };

static unit_option_e option = UNIT_KM_OPTION ;


static void setting_menu_view_deal_set()
{
    printf("unit setting_menu_view_deal_set \n") ;
    vehicle_set_param_unit(option);
    // global_refresh_unit((uint8_t)option);
    return  ;
}

static void setting_menu_view_deal_back()
{
    set_current_menu_level(MENU_LEVEL_1);
    unit_view_clean_state() ;

    return  ;
}

static void setting_menu_view_deal_up()
{
    option =  (option - 1 + UNIT_OPTION_MAX) % UNIT_OPTION_MAX ;
    unit_view_set_focused_item(option) ;

    return  ;
}

static void setting_menu_view_deal_down()
{
    option =  (option + 1 ) % UNIT_OPTION_MAX ;
    unit_view_set_focused_item(option) ;

    return  ;
}


static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = setting_menu_view_deal_up  ,
    [CLICK_SHORT_DOWN] = setting_menu_view_deal_down,
    [CLICK_SHORT_SET]  = setting_menu_view_deal_set ,
    [CLICK_SHORT_BACK] = setting_menu_view_deal_back,
};


void unit_init()
{

    setting_page_t *page = setting_page_get(&ui_manager);
    VIEW_MEMBER_VALID(page, unit_option_km) ;
    VIEW_MEMBER_VALID(page, unit_option_mile) ;
    set_unit_widget[UNIT_KM_OPTION] = unit_option_km ;
    set_unit_widget[UNIT_MILE_OPTION] = unit_option_mile;

    option = vehicle_get_param_unit();
    unit_view_set_focused_item(option) ;
    //刷新数据 todo
    return  ;
}

void on_unit_deal_short_key(click_e key)
{
    printf("on_bt_conn_deal_short_key = %d \n" ,key) ;
    if (key < sizeof(short_click) / sizeof(short_click_deal) 
        && short_click[key]) {
        short_click[key]();
    }

    return  ;
}

void unit_view_set_focused_item(unit_option_e focusedIndex)
{
    if (focusedIndex > UNIT_MILE_OPTION)
        return ;
    
    for (size_t i = 0; i < UNIT_OPTION_MAX ; i++){
        if (i == focusedIndex)
            lv_obj_set_style_bg_image_src(set_unit_widget[i] , LVGL_IMAGE_PATH(setting/setting_selected.png), 0) ;
        else
            lv_obj_remove_local_style_prop(set_unit_widget[i] , LV_STYLE_BG_IMAGE_SRC , 0);
    }

    option = focusedIndex ;

    return ;
}

void unit_view_clean_state()
{
    for (size_t i = 0; i < UNIT_OPTION_MAX ; i++){
        if (set_unit_widget[i])
            lv_obj_remove_local_style_prop(set_unit_widget[i] , LV_STYLE_BG_IMAGE_SRC , 0);   
    }

    return ;
}
