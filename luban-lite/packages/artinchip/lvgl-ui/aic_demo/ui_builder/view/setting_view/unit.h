#ifndef UNIT__H_
#define UNIT__H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

typedef enum {
    UNIT_KM_OPTION  ,
    UNIT_MILE_OPTION  ,

    UNIT_OPTION_MAX      ,
}unit_option_e ;


void unit_init() ;

void on_unit_deal_short_key(click_e key) ;

void unit_view_clean_state() ;

void unit_view_set_focused_item(unit_option_e focusedIndex) ;

#endif