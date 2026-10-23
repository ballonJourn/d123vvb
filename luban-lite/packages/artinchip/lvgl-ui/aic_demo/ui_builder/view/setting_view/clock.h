#ifndef SETTING_CLOCK__H_
#define SETTING_CLOCK__H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

typedef enum {
    CLOCK_H_1_OPTION     ,
    CLOCK_H_2_OPTION     ,
    CLOCK_M_1_OPTION     ,
    CLOCK_M_2_OPTION     ,

    CLOCK_OPTION_NUM_MAX ,
}clock_option_e ;


void clock_init() ;

void refresh_clock(int min ,int sec);

void get_label_clock(int32_t *min , int32_t *sec);

void on_clock_deal_short_key(click_e key) ;

void clock_view_clean_state() ;

void clock_view_set_focused_item(clock_option_e focusedIndex) ;


/// @brief 三级页面
void clock_option_init() ;

void on_clock_option_deal_short_key(click_e key) ;


#endif