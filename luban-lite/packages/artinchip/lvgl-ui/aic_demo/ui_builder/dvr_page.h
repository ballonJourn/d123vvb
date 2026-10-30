#ifndef DVR_PAGE_H
#define DVR_PAGE_H

#include "lvgl/lvgl.h"
#include "button_manager.h"  /* click_e, for dvr_manager_click() below */

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *dvr_page_create(void);

void dvr_page_destroy(void);

/* Key-click adapter: routes button_manager's click_e events into
 * dvr_view.c's sub-page navigation (see dvr_page.c for details). */
void dvr_manager_click(click_e click);

#ifdef __cplusplus
}
#endif

#endif
