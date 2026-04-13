#include "brightness.h"
#include <stdio.h>
#include "vehicle_argument.h"
#include "setting_menu.h"
#include "definition.h"

static int  option = 0 ;
static int  count  = 0 ;

static void setting_menu_view_deal_set()
{
    printf("brightness setting_menu_view_deal_set \n") ;
    vehicle_set_param_brightness(option);
    return  ;
}

static void setting_menu_view_deal_back()
{
    set_current_menu_level(MENU_LEVEL_1);
    brightness_view_clean_state()  ;

    return  ;
}

static void setting_menu_view_deal_up()
{
    if (0 == count)
        return ;
    
    option =  (option - 1 + count) % count ;
    brightness_view_set_focused_item(option) ;

    return  ;
}

static void setting_menu_view_deal_down()
{
    if (0 == count)
        return ;

    option =  (option + 1 ) % count ;
    brightness_view_set_focused_item(option) ;

    return  ;
}


static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = setting_menu_view_deal_up  ,
    [CLICK_SHORT_DOWN] = setting_menu_view_deal_down,
    [CLICK_SHORT_SET]  = setting_menu_view_deal_set ,
    [CLICK_SHORT_BACK] = setting_menu_view_deal_back,
};


void brightness_init()
{
    uint8_t value = vehicle_get_param_brightness() ;
    option = value ;
    brightness_view_set_focused_item(option) ;
    
    //刷新数据 todo
    return  ;
}


void on_brightness_deal_short_key(click_e key)
{
    printf("on_bt_conn_deal_short_key = %d \n" ,key) ;
    if (key < sizeof(short_click) / sizeof(short_click_deal) 
        && short_click[key]) {
        short_click[key]();
    }

    return  ;
}

int prevIndex = 0 ;
int endIndex  = 2 ;

void brightness_view_set_focused_item(int focusedIndex)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    VIEW_MEMBER_VALID(page, brightness_list) ;
    count = lv_obj_get_child_count(brightness_list);

    if (0 == count || focusedIndex > count) 
        return ;

    lv_obj_t *children = NULL ;

    for (size_t i = 0; i < count ; i++)
    {
        children = lv_obj_get_child(brightness_list , i) ;

        if (i == focusedIndex)
            lv_obj_set_style_bg_image_src(children, LVGL_IMAGE_PATH(setting/setting_selected.png), 0) ;
        else
            lv_obj_remove_local_style_prop(children , LV_STYLE_BG_IMAGE_SRC , 0);

    }

    lv_obj_scroll_to_view(lv_obj_get_child(brightness_list , focusedIndex) ,LV_ANIM_OFF);
    option = focusedIndex ;

    return ;
}

void brightness_view_clean_state()
{
    setting_page_t *page = setting_page_get(&ui_manager);
    VIEW_MEMBER_VALID(page, brightness_list) ;
    for (size_t i = 0; i < count ; i++){
        lv_obj_t* children = lv_obj_get_child(brightness_list , i) ;
        if (children)
            lv_obj_remove_local_style_prop(children , LV_STYLE_BG_IMAGE_SRC , 0);
        
    }

    return ;
}
