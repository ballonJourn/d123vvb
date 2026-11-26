/*
 * chronos_nav.c - see chronos_nav.h
 * Logic mirrors chronos-esp32 src/ChronosESP32.cpp (v1.9.1, commit 9cb9269):
 *   onWrite()           -> chronos_nav_on_write()
 *   dataReceived() 0xEF -> nav text, 0xEE -> nav icon part
 *   sendInfo()/sendBattery() -> chronos_nav_on_subscribe()
 * Verified against the original C++ by tools/sim/chronos_diff (differential test).
 *
 * Original: Copyright (c) 2023 Felix Biego, MIT License
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "chronos_nav.h"
#include "bnp_proto.h"

#define CS_VERSION_MAJOR 1
#define CS_VERSION_MINOR 9
#define CS_VERSION_PATCH 1

static void set_str(char *dst, const char *s)
{
    size_t n = strlen(s);
    if (n > CHRONOS_STR_MAX - 1)
        n = CHRONOS_STR_MAX - 1;
    memmove(dst, s, n);
    dst[n] = 0;
}

/* copy NUL-terminated field starting at *i (bounded by len), advance past NUL */
static void take_str(char *dst, const uint8_t *d, int len, int *i)
{
    int n = 0;
    while (*i < len && d[*i] != 0) {
        if (n < CHRONOS_STR_MAX - 1)
            dst[n++] = (char)d[*i];
        (*i)++;
    }
    dst[n] = 0;
    (*i)++;
}

static void nav_reset(struct nav_state *n, int disabled)
{
    /* exact strings from ChronosESP32.cpp so UI behaves the same */
    n->active = disabled ? 1 : 0;
    set_str(n->title, "Chronos");
    set_str(n->duration, disabled ? "Disabled" : "Inactive");
    set_str(n->distance, "");
    set_str(n->speed, "");
    set_str(n->eta, "Navigation");
    set_str(n->directions, disabled ? "Check Chronos app settings" : "Start navigation on Google maps");
    n->has_icon = 0;
    n->is_navigation = 0;
    n->icon_crc = 0xFFFFFFFF;
}

void chronos_nav_init(struct chronos_nav *c)
{
    c->length = 0;
    memset(c->data, 0, sizeof(c->data));
    memset(&c->nav, 0, sizeof(c->nav));
    nav_reset(&c->nav, 0);
    if (!c->screen_conf)
        c->screen_conf = 7; /* CS_240x240_128_CTF, library default */
    if (!c->battery)
        c->battery = 100;
}

static void emit(struct chronos_nav *c, enum chronos_evt e, int arg)
{
    if (c->on_event)
        c->on_event(c->ctx, e, arg, &c->nav);
}

static void data_received(struct chronos_nav *c)
{
    const uint8_t *d = c->data;
    int len = c->length;
    if (len > CHRONOS_DATA_SIZE)
        len = CHRONOS_DATA_SIZE;
    if (d[0] != 0xAB || len < 6)
        return;

    switch (d[4]) {
    case 0xEE: /* navigation icon part: [6]=pos [7..10]=crc BE [11..106]=96 bytes */
        if (d[3] == 0xFE && len >= 11 + 96) {
            uint8_t pos = d[6];
            if (pos < 3) {
                uint32_t crc = ((uint32_t)d[7] << 24) | ((uint32_t)d[8] << 16) |
                               ((uint32_t)d[9] << 8) | d[10];
                memcpy(&c->nav.icon[96 * pos], &d[11], 96);
                c->nav.icon_parts |= (uint8_t)(1u << pos);
                (void)crc; /* app sends the crc of the whole icon; kept in icon_crc via 0xEF */
                emit(c, CHRONOS_EVT_NAV_ICON, pos);
            }
        }
        break;
    case 0xEF:
        if (d[3] != 0xFE)
            break;
        if (d[5] == 0x00) {
            nav_reset(&c->nav, 0);
        } else if (d[5] == 0xFF) {
            nav_reset(&c->nav, 1);
        } else if (d[5] == 0x80 && len >= 12) {
            struct nav_state *n = &c->nav;
            n->active = 1;
            n->has_icon = d[6] == 1;
            n->is_navigation = d[7] == 1;
            uint32_t crc = ((uint32_t)d[8] << 24) | ((uint32_t)d[9] << 16) |
                           ((uint32_t)d[10] << 8) | d[11];
            if (crc != n->icon_crc)
                n->icon_parts = 0; /* new icon coming */
            n->icon_crc = crc;
            int i = 12;
            take_str(n->title, d, len, &i);
            take_str(n->duration, d, len, &i);
            take_str(n->distance, d, len, &i);
            take_str(n->eta, d, len, &i);
            take_str(n->directions, d, len, &i);
            take_str(n->speed, d, len, &i);
        }
        emit(c, CHRONOS_EVT_NAV_DATA, c->nav.active);
        break;
    default:
        break; /* other Chronos features (notifications, music...) not ported */
    }
}

void chronos_nav_on_write(struct chronos_nav *c, const uint8_t *p, int len)
{
    if (len <= 0)
        return;
    if ((p[0] == 0xAB || p[0] == 0xEA) && len >= 4 && (p[3] == 0xFE || p[3] == 0xFF)) {
        c->length = p[1] * 256 + p[2] + 3;
        int n = len < CHRONOS_DATA_SIZE ? len : CHRONOS_DATA_SIZE;
        memcpy(c->data, p, n);
        if (c->length <= len)
            data_received(c);
    } else {
        int j = 20 + (p[0] * 19);
        /* original copies len bytes from p+1 (reads one past the end) and has
         * no bounds check; here: copy len-1 bytes, bounded. */
        for (int i = 0; i < len - 1; i++) {
            if (j + i >= CHRONOS_DATA_SIZE)
                return; /* would overflow -> drop, original would corrupt memory */
            c->data[j + i] = p[i + 1];
        }
        if (c->length <= len + j - 1)
            data_received(c);
    }
}

void chronos_nav_on_subscribe(struct chronos_nav *c)
{
    /* ChronosESP32::sendInfo() + sendBattery(); the original defers these by
     * an _infoTimer, caller should call this ~ a few hundred ms after CCCD. */
    uint8_t info[20] = { 0xab, 0x00, 0x11, 0xff, 0x92, 0xc0, CS_VERSION_MAJOR,
                         (CS_VERSION_MINOR * 10 + CS_VERSION_PATCH), 0x00, 0xfb, 0x1e, 0x40,
                         0xc0, 0x0e, 0x32, 0x28, 0x00, 0xe2, 0, 0x80 };
    info[18] = c->screen_conf;
    uint8_t bat[8] = { 0xAB, 0x00, 0x05, 0xFF, 0x91, 0x80, 0x00, c->battery };
    if (c->notify) {
        c->notify(c->ctx, info, sizeof(info));
        c->notify(c->ctx, bat, sizeof(bat));
    }
}

void chronos_nav_on_disconnect(struct chronos_nav *c)
{
    /* ChronosESP32::onDisconnect: if navigation active -> inactive */
    if (c->nav.active) {
        c->nav.active = 0;
        emit(c, CHRONOS_EVT_NAV_DATA, 0);
    }
}

void nav_state_from_bnp_tlv(struct nav_state *ns, const uint8_t *t, uint16_t len)
{
    memset(ns, 0, sizeof(*ns));
    ns->active = 1;
    ns->is_navigation = 1;
    uint16_t i = 0;
    while (i + 2 <= len) {
        uint8_t tag = t[i], l = t[i + 1];
        const uint8_t *v = t + i + 2;
        if (i + 2 + l > len)
            break;
        char *dst = NULL;
        switch (tag) {
        case BNP_NAV_ROAD:     dst = ns->directions; break;
        case BNP_NAV_DIST:     dst = ns->title; set_str(ns->distance, ""); break;
        case BNP_NAV_ETA:      dst = ns->eta; break;
        case BNP_NAV_DURATION: dst = ns->duration; break;
        case BNP_NAV_SPEED:    dst = ns->speed; break;
        case BNP_NAV_TURN:     if (l) ns->turn = v[0]; break;
        case BNP_NAV_ACTIVE:   if (l) ns->active = v[0]; break;
        }
        if (dst) {
            uint16_t n = l < CHRONOS_STR_MAX - 1 ? l : CHRONOS_STR_MAX - 1;
            memcpy(dst, v, n);
            dst[n] = 0;
        }
        i += 2 + l;
    }
    /* BNP "distance to next turn" is shown where Chronos shows title */
    if (ns->title[0])
        set_str(ns->distance, ns->title);
}
