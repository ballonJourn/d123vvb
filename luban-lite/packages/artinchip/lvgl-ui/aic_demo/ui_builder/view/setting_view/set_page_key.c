#include "set_page_key.h"
#include "button_manager.h"
#include "setting_menu.h"
#include "button_manager.h"
#include "set_view_interface.h"
#include "window_manager.h"

typedef struct {
    void (*view_init)();
    void (*deal_short_key)(click_e);
}setting_entry_t;

static setting_entry_t setting_entry_level_1 = {setting_menu_view_init , on_setting_menu_deal_short_click} ;

static setting_entry_t setting_entry_level_2[SETTING_MENU_NUM_MAX] = {
    [SETTING_MENU_CLOCK]      = { clock_init           , on_clock_deal_short_key          } ,
    [SETTING_MENU_CONNECT]    = { bt_connect_init      , on_bt_connect_deal_short_key     } ,
    [SETTING_MENU_LANGUAGE]   = { language_init        , on_language_deal_short_key       } ,
    [SETTING_MENU_UNIT]       = { unit_init            , on_unit_deal_short_key           } , 
    [SETTING_MENU_DISPLAY]    = { display_init         , on_display_deal_short_key        } ,
    [SETTING_MENU_BRIGHTNESS] = { brightness_init      , on_brightness_deal_short_key     } ,
};

static setting_entry_t setting_entry_level_3[SETTING_MENU_NUM_MAX] = {
    [SETTING_MENU_CLOCK]      = { clock_option_init    , on_clock_option_deal_short_key   } ,
};

bool check_clicked_isVaild(int menu_index)
{
    if (setting_entry_level_2[menu_index].deal_short_key)
        return true ;

    return false ;
}


void menu_manager_click(const click_e click) 
{

    int menu_index = get_current_menu_index();
    int menu_level = get_current_menu_level();
    if (click < CLICK_END )
    {
        switch (menu_level)
        {
            case MENU_LEVEL_0: break;
            case MENU_LEVEL_1:  
                if(setting_entry_level_1.deal_short_key)
                    setting_entry_level_1.deal_short_key(click);

                if (click == CLICK_SHORT_SET)
                {
                    if (setting_entry_level_2[menu_index].view_init)
                        setting_entry_level_2[menu_index].view_init();
                }
                
                break;
            case MENU_LEVEL_2: 
                if (setting_entry_level_2[menu_index].deal_short_key)
                    setting_entry_level_2[menu_index].deal_short_key(click);
                
                break;
            case MENU_LEVEL_3: 
                if (setting_entry_level_3[menu_index].deal_short_key)
                    setting_entry_level_3[menu_index].deal_short_key(click);
                break;
            default:
                break;
        }
    }
    
    return ;
}





