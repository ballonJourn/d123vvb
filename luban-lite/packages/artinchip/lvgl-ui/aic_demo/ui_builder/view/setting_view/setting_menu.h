#ifndef SETTING_MENU_H_
#define SETTING_MENU_H_

#include "ui_init.h"
#include "button_manager.h"

typedef enum menu_level{
    MENU_LEVEL_0 ,
    MENU_LEVEL_1 ,
    MENU_LEVEL_2 ,
    MENU_LEVEL_3 , 
}menu_level_e;

typedef enum setting_menu{
    SETTING_MENU_TMPS       ,
    SETTING_MENU_CONNECT    ,
    SETTING_MENU_LANGUAGE   ,
    SETTING_MENU_BRIGHTNESS ,
    SETTING_MENU_UNIT       ,
    SETTING_MENU_CLOCK      ,
    SETTING_MENU_DISPLAY    ,
    SETTING_MENU_DEVICE     ,

    SETTING_MENU_NUM_MAX    ,
}setting_menu_e;

int get_current_menu_index();

int get_current_menu_level() ;
void set_current_menu_level(menu_level_e s) ;

void setting_menu_view_init();

void on_setting_menu_deal_short_click(click_e click) ;

void setting_menu_set_focused_item(setting_menu_e item) ;

void setting_menu_destory() ;

#endif