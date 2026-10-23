#include "link_page_key.h"
#include "window_manager.h"
#include "ui_objects.h"
#include "link_logic.h"
void phone_manager_click(const click_e click)
{
    switch (click)
    {
        case CLICK_SHORT_BACK:
            on_phone_destory();
            window_manager_set_target(WINDOW_MAIN) ;
            break;
        case CLICK_SHORT_UP:
            break;
        case CLICK_LONG_SET:
            break;

        default:
            break;
    }
}



