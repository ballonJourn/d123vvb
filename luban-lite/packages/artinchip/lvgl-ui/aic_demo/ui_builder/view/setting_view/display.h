#ifndef DISPLAY__H_
#define DISPLAY__H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

typedef enum {
    DIAPLAY_AUTO_OPTION   ,
    DIAPLAY_DAY_OPTION    ,
    DIAPLAY_NIGHT_OPTION  ,
    DIAPLAY_OPTION_MAX    ,
}display_option_e ;


void display_init() ;

void on_display_deal_short_key(click_e key) ;

void display_view_clean_state() ;

void display_view_set_focused_item(display_option_e focusedIndex) ;

#endif