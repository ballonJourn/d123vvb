#ifndef WINDOW_MANAGER_H_
#define WINDOW_MANAGER_H_

#include "lvgl.h"
#include <stdbool.h>

typedef enum {
    WINDOW_MAIN,
    WINDOW_MENU,
    WINDOW_MOBILE,
    WINDOW_DEVICE,
    WINDOW_NULL,
} window_e;

void window_manager_set_target(window_e e);
window_e window_manager_get();
bool window_manager_is_active(window_e id);



#endif  // WINDOW_MANAGER_H_
