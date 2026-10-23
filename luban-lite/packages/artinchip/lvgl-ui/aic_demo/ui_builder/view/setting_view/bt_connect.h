#ifndef BT_CONNECT__H_
#define BT_CONNECT__H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

typedef enum {
    BT_CONNECT_ON_OPTION   ,
    BT_CONNECT_OFF_OPTION  ,

    BT_CONNECT_OPTION_MAX  ,
}bt_option_e ;


void refresh_bt_connect_state(bt_option_e state) ;

void refresh_bt_name(const char* name);

void refresh_bt_phone_info(const char* phoneName);

void bt_connect_init() ;

void on_bt_connect_deal_short_key(click_e key) ;

void bt_connect_view_clean_state() ;

void bt_connect_view_set_focused_item(bt_option_e focusedIndex) ;

#endif