#include "bt_connect.h"
#include <stdio.h>
#include "vehicle_argument.h"
#include "setting_menu.h"
#include "definition.h"
// #include "proxy/bluetooth_data.h"

const char* set_bt_conn_state_str[BT_CONNECT_OPTION_MAX] = {
    "setting_bluebooth_open" , "setting_bluebooth_close"
};

lv_obj_t *set_bt_conn_widget[BT_CONNECT_OPTION_MAX] ;

static bt_option_e option = BT_CONNECT_ON_OPTION ;

static void setting_menu_view_deal_set()
{
    printf("on_bt_conn setting_menu_view_deal_set \n") ;

    // vehicle_set_param_bluetooth(!option);
    // vehicle_set_bluetooth_state(!option);
    return  ;
}

static void setting_menu_view_deal_back()
{
    set_current_menu_level(MENU_LEVEL_1);
    bt_connect_view_clean_state() ;

    return  ;
}

static void setting_menu_view_deal_up()
{
    option =  (option - 1 + BT_CONNECT_OPTION_MAX) % BT_CONNECT_OPTION_MAX ;
    bt_connect_view_set_focused_item(option) ;

    return  ;
}

static void setting_menu_view_deal_down()
{
    option =  (option + 1 ) % BT_CONNECT_OPTION_MAX ;
    bt_connect_view_set_focused_item(option) ;

    return  ;
}


static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = setting_menu_view_deal_up  ,
    [CLICK_SHORT_DOWN] = setting_menu_view_deal_down,
    [CLICK_SHORT_SET]  = setting_menu_view_deal_set ,
    [CLICK_SHORT_BACK] = setting_menu_view_deal_back,
};


void bt_connect_init()
{
    setting_page_t *page = setting_page_get(&ui_manager);
    VIEW_MEMBER_VALID(page, bt_option_open) ;
    VIEW_MEMBER_VALID(page, bt_option_close) ;
    set_bt_conn_widget[BT_CONNECT_ON_OPTION] = bt_option_open ;
    set_bt_conn_widget[BT_CONNECT_OFF_OPTION] = bt_option_close;

    //刷新数据 
    uint8_t value = vehicle_get_param_bluetooth();
    option = (value == 0 ? BT_CONNECT_OFF_OPTION : BT_CONNECT_ON_OPTION);
    bt_connect_view_set_focused_item(option) ;


    return  ;
}

void on_bt_connect_deal_short_key(click_e key)
{
    printf("on_bt_conn_deal_short_key = %d \n" ,key) ;
    if (key < sizeof(short_click) / sizeof(short_click_deal) 
        && short_click[key]) {
        short_click[key]();
    }

    return  ;
}

void bt_connect_view_set_focused_item(bt_option_e focusedIndex)
{
    for (size_t i = 0; i < BT_CONNECT_OPTION_MAX ; i++)
    {
        if (set_bt_conn_widget[i])
        {
            if (i == focusedIndex)
                lv_obj_set_style_bg_image_src(set_bt_conn_widget[i] , LVGL_IMAGE_PATH(setting/setting_selected.png), 0) ;
            else
                lv_obj_remove_local_style_prop(set_bt_conn_widget[i] , LV_STYLE_BG_IMAGE_SRC , 0);

        }

    }
    option = focusedIndex ;

    return ;
}

void bt_connect_view_clean_state()
{
    for (size_t i = 0; i < BT_CONNECT_OPTION_MAX ; i++)
    {
        if (set_bt_conn_widget[i])
            lv_obj_remove_local_style_prop(set_bt_conn_widget[i] , LV_STYLE_BG_IMAGE_SRC , 0);
    }

    return ;
}

void refresh_bt_connect_state(bt_option_e state)
{
    if (state > BT_CONNECT_OFF_OPTION)
        return ;
    setting_page_t *page = setting_page_get(&ui_manager);
    VIEW_MEMBER_VALID(page , bt_status) ;

    lv_image_set_src(bt_status , set_bt_conn_state_str[state]) ;
    
    
    return ;
}

void refresh_bt_name(const char* name)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    VIEW_MEMBER_VALID(page , bt_name) ;
    lv_label_set_text(bt_name , name);
    return ;
}

void refresh_bt_phone_info(const char* phoneName)
{
    // setting_page_t *page = setting_page_get(&ui_manager);
    // VIEW_MEMBER_VALID(page , phoneName) ;
    // lv_label_set_text(phoneName , phoneName);
    return ;
}