#include "home_page_key.h"
#include "window_manager.h"
#include "ui_objects.h"
#include "mileage_calc.h"

void home_manager_click(const click_e click)
{
    switch (click)
    {
        case CLICK_SHORT_SET:
            window_manager_set_target(WINDOW_MENU) ;
            break;
        case CLICK_SHORT_UP:
            /* UP key on the home page is the DVR page entry point. */
            window_manager_set_target(WINDOW_DEVICE) ;
            break;
        case CLICK_LONG_SET:
            window_manager_set_target(WINDOW_MOBILE) ;
            break;
        case CLICK_LONG_BACK:
            mileage_clear_trip();
            break;
        default:
            break;
    }
}



