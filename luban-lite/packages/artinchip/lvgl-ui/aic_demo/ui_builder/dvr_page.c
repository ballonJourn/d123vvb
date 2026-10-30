#include "dvr_page.h"        /* own prototypes + click_e (via button_manager.h) —
                               * needed for dvr_manager_click()'s parameter type below */
#include "dvr_view.h"
#include "mw_dvr.h"          /* dvr_api_* backend (see note in dvr_view.c) */
#include "window_manager.h"  /* window_manager_set_target() — DVR exit on top-level BACK */

#define DVR_PREVIEW_MODE_FRONT_REAR  2

static lv_obj_t *dvr_screen = NULL;

static void on_dvr_screen_delete(lv_event_t *e)
{
    (void)e;
    printf("DVR-EXIT: on_dvr_screen_delete enter, preview_enable=%d\n",
           dvr_api_get_preview_enable());

    dvr_view_destroy();

    if (dvr_api_get_preview_enable()) {
        dvr_stop_preview();
    }
    dvr_set_sensor_switch_enable(1);
    dvr_screen = NULL;

    printf("DVR-EXIT: on_dvr_screen_delete done\n");
}

lv_obj_t *dvr_page_create(void)
{
    if (dvr_screen) {
        printf("DVR: dvr_page_create — screen already exists, loading\n");
        lv_screen_load(dvr_screen);
        return dvr_screen;
    }

    dvr_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(dvr_screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dvr_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(dvr_screen, LV_OBJ_FLAG_SCROLLABLE);


    lv_obj_add_event_cb(dvr_screen, on_dvr_screen_delete, LV_EVENT_DELETE, NULL);


    dvr_view_init(dvr_screen);


    dvr_api_set_display_window(0, 0, 1024, 500);


    dvr_api_set_preview_enable(1);


    dvr_api_view_switch(DVR_PREVIEW_MODE_FRONT_REAR);


    dvr_api_get_status();


    dvr_api_get_id();
    dvr_api_get_tf_capacity_query();

    dvr_set_sensor_switch_enable(0);

    printf("DVR: dvr_page_create done, preview on, window=(0,0,1024,500)\n");

    lv_screen_load(dvr_screen);
    return dvr_screen;
}

void dvr_page_destroy(void)
{
    if (dvr_screen) {
        lv_obj_delete(dvr_screen);
        dvr_screen = NULL;
    }
}


/* ---------------------------------------------------------------------
 * DVR key-click adapter, called from button_manager.c's WINDOW_DEVICE
 * case (see view/button_manager.c). Routes the 4-button nav (UP/DOWN/
 * SET/BACK) into dvr_view.c's own sub-page state machine. A BACK press
 * while dvr_view.c is already at its top level (DVR_SUB_MAIN) also exits
 * the DVR screen back to the home page; a BACK press from any deeper
 * sub-page (settings, file list, playback, ...) is swallowed by
 * dvr_page_deal_key_back() to pop one level, same as the original
 * design (dvr_page_deal_key_back() never exits the screen by itself,
 * see dvr_view.c's DVR_SUB_MAIN case: it only stops preview + hides the
 * main view, so the top-level exit decision belongs here).
 * ------------------------------------------------------------------- */
void dvr_manager_click(click_e click)
{
    switch (click) {
    case CLICK_SHORT_UP:
        dvr_page_deal_key_up();
        break;
    case CLICK_SHORT_DOWN:
        dvr_page_deal_key_down();
        break;
    case CLICK_SHORT_SET:
        dvr_page_deal_key_set();
        break;
    case CLICK_SHORT_BACK: {
        bool was_top = (dvr_get_current_sub() == DVR_SUB_MAIN);
        dvr_page_deal_key_back();
        if (was_top) {
            window_manager_set_target(WINDOW_MAIN);
        }
        break;
    }
    default:
        break;
    }
}
