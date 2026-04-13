#include "display.h"
#include <stdio.h>
#include "vehicle_argument.h"
#include "setting_menu.h"
#include "definition.h"

static lv_obj_t* set_display_widget[DIAPLAY_OPTION_MAX] = { NULL };

static display_option_e option = DIAPLAY_AUTO_OPTION ;

static void setting_menu_view_deal_set()
{
    printf("display setting_menu_view_deal_set = %d\n" , option) ;
    // 设置主题参数 0：白天 1:黑夜 2:自动
    if (option == DIAPLAY_AUTO_OPTION)
        vehicle_set_param_display(2);
    else if(option == DIAPLAY_DAY_OPTION)
        vehicle_set_param_display(0);
    else if (option == DIAPLAY_NIGHT_OPTION)
        vehicle_set_param_display(1);
    
    // global_refresh_display(option);

    return  ;
}

static void setting_menu_view_deal_back()
{
    set_current_menu_level(MENU_LEVEL_1);
    display_view_clean_state() ;

    return  ;
}

static void setting_menu_view_deal_up()
{
    option =  (option - 1 + DIAPLAY_OPTION_MAX) % DIAPLAY_OPTION_MAX ;
    display_view_set_focused_item(option) ;

    return  ;
}

static void setting_menu_view_deal_down()
{
    option =  (option + 1 ) % DIAPLAY_OPTION_MAX ;
    display_view_set_focused_item(option) ;

    return  ;
}


static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = setting_menu_view_deal_up  ,
    [CLICK_SHORT_DOWN] = setting_menu_view_deal_down,
    [CLICK_SHORT_SET]  = setting_menu_view_deal_set ,
    [CLICK_SHORT_BACK] = setting_menu_view_deal_back,
};


void display_init()
{

    setting_page_t *page = setting_page_get(&ui_manager);
    VIEW_MEMBER_VALID(page, display_option_1) ;
    VIEW_MEMBER_VALID(page, display_option_2) ;
    VIEW_MEMBER_VALID(page, display_option_3) ;
    set_display_widget[DIAPLAY_AUTO_OPTION] = display_option_1 ;
    set_display_widget[DIAPLAY_DAY_OPTION]  = display_option_2;
    set_display_widget[DIAPLAY_NIGHT_OPTION] = display_option_3;

    uint8_t value = vehicle_get_param_display();
    if (value == 0)
        option = DIAPLAY_DAY_OPTION ;
    else if(value == 1)
        option = DIAPLAY_NIGHT_OPTION ;
    else if (value == 2)
        option = DIAPLAY_AUTO_OPTION ;
    else
        option = DIAPLAY_AUTO_OPTION ;

    display_view_set_focused_item(option) ;
    //刷新数据 todo

    return  ;
}

void on_display_deal_short_key(click_e key)
{
    printf("on_bt_conn_deal_short_key = %d \n" ,key) ;
    if (key < sizeof(short_click) / sizeof(short_click_deal) 
        && short_click[key]) {
        short_click[key]();
    }

    return  ;
}

void display_view_set_focused_item(display_option_e focusedIndex)
{
    for (size_t i = 0; i < DIAPLAY_OPTION_MAX ; i++){
        if (set_display_widget[i]){
            if (i == focusedIndex)
                lv_obj_set_style_bg_image_src(set_display_widget[i] , LVGL_IMAGE_PATH(setting/setting_selected.png), 0) ;
            else
                lv_obj_remove_local_style_prop(set_display_widget[i] , LV_STYLE_BG_IMAGE_SRC , 0);
        }

    }

    option = focusedIndex ;
    
    return ;
}

void display_view_clean_state()
{
    for (size_t i = 0; i < DIAPLAY_OPTION_MAX ; i++){
        if (set_display_widget[i])
            lv_obj_remove_local_style_prop(set_display_widget[i] , LV_STYLE_BG_IMAGE_SRC , 0);
    }

    return ;
}
