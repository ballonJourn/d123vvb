/*
 * disp_sink.h - put received navigation frames on the D133 screen
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef DISP_SINK_H
#define DISP_SINK_H

#include <stdint.h>
#include "ble_img_rx.h"
#include "chronos_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

int  disp_sink_init(void);
/* queue a CRC-checked frame; returns 0 if accepted (ble_navi_release() follows) */
int  disp_sink_submit(const struct bir_frame *f);
/* same, but f->data was allocated by the caller (aicos_malloc_align MEM_CMA)
 * and is freed by the sink after display, also when the queue is full */
int  disp_sink_submit_owned(const struct bir_frame *f);
/* navigation text/state (BNP NAV_TEXT or Chronos 0xEF) */
void disp_sink_nav(const struct nav_state *ns);
/* chronos 48x48 1bpp icon, all 3 parts received */
void disp_sink_turn_icon_1bpp(const uint8_t *bits, uint16_t w, uint16_t h);
void disp_sink_dump_stats(void);
/* BLE_NAVI_DISP_LVGL: call once from the LVGL thread (lv_user_gui_init) */
void ble_navi_lv_attach(void);

/* weak hook: replace with an LVGL label update in your UI; default prints */
void disp_ui_nav_update(const struct nav_state *ns);

#ifdef __cplusplus
}
#endif
#endif
