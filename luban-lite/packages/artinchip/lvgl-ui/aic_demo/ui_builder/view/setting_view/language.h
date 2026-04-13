#ifndef LANGUAGE__H_
#define LANGUAGE__H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

typedef enum {
    LANGUAGE_CHINESE_OPTION  ,
    LANGUAGE_ENGLISH_OPTION  ,

    LANGUAGE_OPTION_MAX      ,
}language_option_e ;


void language_init() ;

void on_language_deal_short_key(click_e key) ;

void language_view_clean_state() ;

void language_view_set_focused_item(language_option_e focusedIndex) ;



#endif