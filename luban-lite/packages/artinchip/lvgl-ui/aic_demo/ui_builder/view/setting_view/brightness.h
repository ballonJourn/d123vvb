#ifndef BRIGHTNESS_H__
#define BRIGHTNESS_H__

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"


void brightness_init() ;

void on_brightness_deal_short_key(click_e key) ;

void brightness_view_clean_state() ;

void brightness_view_set_focused_item(int focusedIndex) ;

#endif