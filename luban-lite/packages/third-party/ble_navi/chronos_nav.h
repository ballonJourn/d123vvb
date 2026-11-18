/*
 * chronos_nav.h - C port of the navigation subset of fbiego/chronos-esp32
 * (ChronosESP32::onWrite reassembly + dataReceived cases 0xEF / 0xEE,
 * sendInfo / sendBattery handshake) so the existing "Chronos" Android app
 * (Google Maps / OsmAnd notification bridge) can drive the D133 directly.
 *
 * Original: Copyright (c) 2023 Felix Biego, MIT License
 * Port    : SPDX-License-Identifier: MIT
 */
#ifndef CHRONOS_NAV_H
#define CHRONOS_NAV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NUS-style UUIDs used by Chronos (same as Nordic UART Service) */
#define CHRONOS_SVC_UUID_STR "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHRONOS_ICON_SIZE    48
#define CHRONOS_ICON_BYTES   (CHRONOS_ICON_SIZE * CHRONOS_ICON_SIZE / 8)
#define CHRONOS_DATA_SIZE    512
#define CHRONOS_STR_MAX      96

struct nav_state {
    uint8_t  active, is_navigation, has_icon;
    char     distance[CHRONOS_STR_MAX];
    char     duration[CHRONOS_STR_MAX];
    char     eta[CHRONOS_STR_MAX];
    char     title[CHRONOS_STR_MAX];      /* distance to next point */
    char     directions[CHRONOS_STR_MAX]; /* road name / instruction */
    char     speed[CHRONOS_STR_MAX];
    uint8_t  icon[CHRONOS_ICON_BYTES];    /* 48x48, 1bpp, MSB first   */
    uint32_t icon_crc;
    uint8_t  icon_parts;                  /* bitmask of 3 x 96B parts */
    uint8_t  turn;                        /* BNP_NAV_TURN, 0 = none   */
};

enum chronos_evt {
    CHRONOS_EVT_NAV_DATA = 1, /* text fields changed      */
    CHRONOS_EVT_NAV_ICON = 2, /* icon part (arg) received */
};

struct chronos_nav {
    int      length;
    uint8_t  data[CHRONOS_DATA_SIZE];
    struct nav_state nav;
    uint8_t  screen_conf;     /* ChronosScreen enum value reported to app */
    uint8_t  battery;
    /* hooks */
    int  (*notify)(void *ctx, const uint8_t *b, uint16_t n); /* TX char */
    void (*on_event)(void *ctx, enum chronos_evt e, int arg, const struct nav_state *ns);
    void *ctx;
};

void chronos_nav_init(struct chronos_nav *c);
/* RX characteristic write (6e400002) */
void chronos_nav_on_write(struct chronos_nav *c, const uint8_t *p, int len);
/* TX notify enabled -> after ~CS info timer the lib sends info + battery */
void chronos_nav_on_subscribe(struct chronos_nav *c);
void chronos_nav_on_disconnect(struct chronos_nav *c);

/* fill nav_state from a BNP NAV_TEXT TLV list (shared display path) */
void nav_state_from_bnp_tlv(struct nav_state *ns, const uint8_t *tlv, uint16_t len);

#ifdef __cplusplus
}
#endif
#endif
