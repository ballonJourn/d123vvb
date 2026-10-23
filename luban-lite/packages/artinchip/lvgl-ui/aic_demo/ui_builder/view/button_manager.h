
#ifndef BUTTON_MANAGER_H_
#define BUTTON_MANAGER_H_

#include "ui_init.h"

typedef enum 
{
    CLICK_SHORT_UP, 
    CLICK_SHORT_DOWN,

    CLICK_SHORT_SET, 
    CLICK_SHORT_BACK,

    CLICK_LONG_SET,
    CLICK_LONG_BACK,

    CLICK_SHORT_HOME,

    CLICK_END,
}click_e ;

typedef void (*click_handler_t)(void);
typedef void (*short_click_deal)(void);

void button_manager_set_click(click_e click);


click_e button_manager_get_click();


void button_manager_handle();


void button_manager_refresh();


void button_manager_event(uint8_t id);


#endif  // BUTTON_MANAGER_H_
