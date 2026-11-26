#include "button_manager.h"
#include "window_manager.h"
#include "aic_ui.h"
#include "set_page_key.h"
#include "home_page_key.h"
#include "link_page_key.h"
#include "selfcheck.h"
#include "key_common.h"
#include "dev_config.h"

typedef enum {
  BUTTON_STATE_CALL,
  BUTTONG_STATE_NON_CALL,
} button_state_e;

typedef enum {
  BUTTON_ID_SHORT_UP   = 17,
  BUTTON_ID_SHORT_DOWN = 18,
  BUTTON_ID_SHORT_SET  = 10,
  BUTTON_ID_SHORT_BACK = 8
} button_id_e;

static click_e current_click        = CLICK_END;
static button_state_e current_state = BUTTONG_STATE_NON_CALL;

void button_manager_set_click(click_e click) 
{
    if (checkself_get_state() != CHECK_STATE_FINISHED /*|| get_demonstration_state()*/ ) 
        return  ;

    current_click = click;
}

click_e button_manager_get_click() {
    return current_click;
}

static void handle_non_call_state() {
    window_e window = window_manager_get();
    switch (window) {
    case WINDOW_MAIN:
        home_manager_click(current_click) ;
        break;
    case WINDOW_MENU:
        menu_manager_click(current_click);
        break;
    case WINDOW_MOBILE:
        phone_manager_click(current_click);
        break;
    default:
        break;
    }
}

static void handle_call_state() 
{

}

void button_manager_handle() 
{
    if (current_state == BUTTON_STATE_CALL)
        handle_call_state();
    else
        handle_non_call_state();
}

void button_manager_refresh() 
{
    if (button_manager_get_click() == CLICK_END)
        return;

    current_state = BUTTONG_STATE_NON_CALL;
    button_manager_handle();
    button_manager_set_click(CLICK_END);
}

void button_manager_event(uint8_t id) 
{
    if (checkself_get_state() != CHECK_STATE_FINISHED /*|| get_demonstration_state()*/ ) 
        return  ;

    if (current_click != CLICK_END)
        return;

#if MW_ADC_KEY_ENABLE || MW_IO_KEY_ENABLE || MW_CAN_IO_KEY_ENABLE
    switch (id) 
    {
      case UP_KEY_SHORT_PR:
        current_click = CLICK_SHORT_UP;
        break;
      case MODE_KEY_SHORT_PR:
        current_click = CLICK_SHORT_DOWN;
        break;
      case SET_KEY_SHORT_PR:
        current_click = CLICK_SHORT_SET;
        break;
      case BACK_KEY_SHORT_PR:
        current_click = CLICK_SHORT_BACK;
        break;
      case BACK_KEY_LONG_PR:
        current_click = CLICK_LONG_BACK;
        break;
      case SET_KEY_LONG_PR :
        current_click = CLICK_LONG_SET;
        break;
      case HOME_KEY_SHORT_PR:
        current_click = CLICK_SHORT_HOME;
        break;
      default:
        break;
    }
#endif

    // LV_LOG_WARN("Event ID = %u processed.", id);
}
