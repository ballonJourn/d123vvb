#include "language.h"
#include <stdio.h>
#include "vehicle_argument.h"
#include "setting_menu.h"
#include "definition.h"
#include "aic_lang.h"

static lv_obj_t* set_language_widget[LANGUAGE_OPTION_MAX] = { NULL };

static language_option_e option = LANGUAGE_CHINESE_OPTION ;

static void setting_menu_view_deal_set()
{
    printf("language setting_menu_view_deal_set \n") ;

    vehicle_set_param_language(option);

    aic_set_language(option);
    setting_page_update_current_language() ;
    return  ;
}

static void setting_menu_view_deal_back()
{
    set_current_menu_level(MENU_LEVEL_1);
    language_view_clean_state() ;

    return  ;
}

static void setting_menu_view_deal_up()
{
    option =  (option - 1 + LANGUAGE_OPTION_MAX) % LANGUAGE_OPTION_MAX ;
    language_view_set_focused_item(option) ;

    return  ;
}

static void setting_menu_view_deal_down()
{
    option =  (option + 1 ) % LANGUAGE_OPTION_MAX ;
    language_view_set_focused_item(option) ;

    return  ;
}


static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = setting_menu_view_deal_up  ,
    [CLICK_SHORT_DOWN] = setting_menu_view_deal_down,
    [CLICK_SHORT_SET]  = setting_menu_view_deal_set ,
    [CLICK_SHORT_BACK] = setting_menu_view_deal_back,
};


void language_init()
{
    setting_page_t *page = setting_page_get(&ui_manager);
    VIEW_MEMBER_VALID(page, language_option_open) ;
    VIEW_MEMBER_VALID(page, language_option_en) ;
    set_language_widget[LANGUAGE_CHINESE_OPTION] = language_option_open ;
    set_language_widget[LANGUAGE_ENGLISH_OPTION] = language_option_en;
 
    ///< 语言 0:中文 1:英文,预留位
    uint8_t value = vehicle_get_param_bluetooth();
    option = value ;
    language_view_set_focused_item(value) ;
    return  ;
}

void on_language_deal_short_key(click_e key)
{
    printf("on_bt_conn_deal_short_key = %d \n" ,key) ;
    if (key < sizeof(short_click) / sizeof(short_click_deal) 
        && short_click[key]) {
        short_click[key]();
    }

    return  ;
}

void language_view_set_focused_item(language_option_e focusedIndex)
{
    if (focusedIndex > LANGUAGE_ENGLISH_OPTION)
        return ;
    
    for (size_t i = 0; i < LANGUAGE_OPTION_MAX ; i++)
    {
        if (set_language_widget[i]){
            if (i == focusedIndex)
                lv_obj_set_style_bg_image_src(set_language_widget[i] , LVGL_IMAGE_PATH(setting/setting_selected.png), 0) ;
            else
                lv_obj_remove_local_style_prop(set_language_widget[i] , LV_STYLE_BG_IMAGE_SRC , 0);
        }
    }

    option = focusedIndex ;
    
    return ;
}

void language_view_clean_state()
{
    for (size_t i = 0; i < LANGUAGE_OPTION_MAX ; i++)
    {
        if (set_language_widget[i])
            lv_obj_remove_local_style_prop(set_language_widget[i] , LV_STYLE_BG_IMAGE_SRC , 0);
    }

    return ;
}
