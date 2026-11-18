/*
 * disp_sink.h - put received navigation frames on the D133 screen
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef DISP_SINK_H
#define DISP_SINK_H

#include <stdint.h>
#include "ble_img_rx.h"

#ifdef __cplusplus
extern "C" {
#endif

int  disp_sink_init(void);
/* queue a CRC-checked frame; returns 0 if accepted (blenavi_release() follows) */
int  disp_sink_submit(const struct bir_frame *f);
/* navigation text, from chronos-esp32 Navigation or BNP NAV_TEXT */
struct blenavi_nav {
    int         active;
    const char *title;       /* distance to next turn */
    const char *directions;  /* road name / instruction */
    const char *eta, *duration, *distance, *speed;
    uint8_t     turn;        /* BNP maneuver code, 0 = none */
};
void disp_sink_nav(const struct blenavi_nav *ns);
/* chronos-esp32 48x48 1bpp turn icon (Navigation icon) */
void disp_sink_turn_icon_1bpp(const uint8_t *bits, uint16_t w, uint16_t h);
void disp_sink_dump_stats(void);

/* weak hook: replace with an LVGL label update in your UI; default prints */
void disp_ui_nav_update(const struct blenavi_nav *ns);

#ifdef __cplusplus
}
#endif
#endif
