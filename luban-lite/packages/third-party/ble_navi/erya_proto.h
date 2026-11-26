/*
 * erya_proto.h - 尔雅 BLE 导航协议 Ver 1.2 (2026-09-25), device (仪表) side.
 * Used by the "领骑摩托" phone app to project navigation onto the dashboard.
 *
 * GATT: characteristics 0xAAA1..0xAAAA (16-bit UUIDs).
 *   AAA5  device -> phone : "<MAC>&<model key>" (initial "0"), phone polls reads
 *         phone -> device : phone type, 0x00 Android / 0x01 iOS
 *   AAA4  device -> phone : "-3" = device supports BLE connection only
 *   AAA6  device -> phone : EY frame {"fun":"pic","w","h","q","type"}
 *   AAA7  phone -> device : EB frame {"fun":"pic","num","sum","size"} + JPEG piece
 *   AAA8  phone -> device : EY frame {"fun":"navi","type","data":{...}}
 * Seen from the 领骑摩托 app, not in Ver 1.2 (phone -> device on AAA6):
 *         EY {"fun":"time","data":{"timeZone":"8","timestamp":"<UTC s>"}}
 *         EY {"fun":"activate","type":"<n>"}   (2 seen with an empty key)
 * Framing (AAA6..AAA8):
 *   "EY" | u32 BE n | n bytes UTF-8 JSON
 *   "EB" | u32 BE n | u32 BE m | n bytes JSON | m bytes binary
 *
 * This file is platform neutral (no RT-Thread / NimBLE): the host test in
 * tools/ runs it unchanged.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ERYA_PROTO_H
#define ERYA_PROTO_H

#include <stdint.h>
#include <stddef.h>
#include "chronos_nav.h"   /* struct nav_state */

#ifdef __cplusplus
extern "C" {
#endif

#define ERYA_UUID_AAA1 0xAAA1
#define ERYA_UUID_AAA4 0xAAA4
#define ERYA_UUID_AAA5 0xAAA5
#define ERYA_UUID_AAA6 0xAAA6
#define ERYA_UUID_AAA7 0xAAA7
#define ERYA_UUID_AAA8 0xAAA8
#define ERYA_NUM_CHR   10      /* AAA1..AAAA */

#define ERYA_JSON_MAX  1024    /* longest JSON part accepted */
#define ERYA_VAL_MAX   160     /* readable characteristic value */

enum erya_phone { ERYA_PHONE_UNKNOWN = -1, ERYA_PHONE_ANDROID = 0, ERYA_PHONE_IOS = 1 };

/* one byte-stream parser per written characteristic (AAA7, AAA8) */
struct erya_stream {
    uint8_t  hdr[10];
    uint8_t  hdr_len;          /* bytes of hdr collected                   */
    uint8_t  is_bin;           /* "EB" frame                               */
    uint32_t json_len, bin_len;
    uint32_t json_got, bin_got;
    char     json[ERYA_JSON_MAX + 1];
    uint8_t  skipping_json;    /* JSON too long: consume and drop          */
};

struct erya_stats {
    uint32_t frames_ey, frames_eb, bad_magic, json_err, json_too_long;
    uint32_t pics_ok, pics_lost, pics_too_big, pics_alloc_fail, pkts;
    uint32_t navi_msgs, bytes_rx, last_pic_bytes, last_pic_ms;
    uint32_t time_msgs, activate_msgs, unknown_fun;
};

struct erya_ops {
    /* send a value change to the phone: notify characteristic AAAx */
    int   (*notify)(void *ctx, uint16_t uuid16, const uint8_t *b, uint16_t n);
    void *(*alloc)(void *ctx, size_t n);
    void  (*free)(void *ctx, void *p);
    uint32_t (*now_ms)(void *ctx);
    /* a complete JPEG; ownership of buf passes to the callee */
    void  (*on_picture)(void *ctx, uint8_t *buf, uint32_t len);
    void  (*on_navi)(void *ctx, const struct nav_state *ns);
    /* undocumented app messages on AAA6 (may be NULL) */
    void  (*on_time)(void *ctx, uint32_t utc, int tz_hours);
    void  (*on_activate)(void *ctx, int type);
    void  *ctx;
};

struct erya_cfg {
    char     mac[13];          /* 12 hex digits, upper case, MSB first     */
    char     model_key[64];    /* issued by 尔雅 after the business deal    */
    uint16_t pic_w, pic_h, pic_q;
    char     pic_type[8];      /* "420" or "422"                           */
    uint32_t max_pic_bytes;
};

struct erya {
    struct erya_cfg   cfg;
    struct erya_ops   ops;
    struct erya_stream s6, s7, s8;
    int      phone;            /* enum erya_phone                          */
    uint8_t  connected, mac_published;
    /* picture being assembled */
    uint8_t *pic;
    uint32_t pic_size, pic_got, pic_t0;
    int32_t  pic_next;         /* next expected "num", -1 = wait for num 0 */
    uint32_t pic_sum;
    uint32_t pkt_bin_left;     /* binary bytes of the current EB that go to pic */
    uint8_t  pkt_accept;       /* current EB belongs to the picture         */
    uint32_t pkt_num;
    struct nav_state nav;
    int      activate;         /* last "activate" type, -1 = none yet      */
    uint32_t time_utc;         /* last "time" message, 0 = none yet        */
    int      time_tz;
    struct erya_stats st;
};

void erya_init(struct erya *e, const struct erya_cfg *cfg, const struct erya_ops *ops);
/* Change the picture format asked from the phone and tell it on AAA6 (it also
 * answers the next read). 0 ok; -1 and nothing changed when out of range:
 * w, h 16..2048 and multiples of 16 (the JPEG MCU), q 120..200, type "420"/"422".
 * Whether the app takes a changed format in the middle of a connection is the
 * app's business: look at the size of the next pictures. */
int erya_set_pic(struct erya *e, uint16_t w, uint16_t h, uint16_t q, const char *type);
void erya_on_connect(struct erya *e);
void erya_on_disconnect(struct erya *e);
/* a phone write to characteristic uuid16 */
void erya_on_write(struct erya *e, uint16_t uuid16, const uint8_t *b, uint16_t n);
/* the phone subscribed to notifications of uuid16 */
void erya_on_subscribe(struct erya *e, uint16_t uuid16);
/* current readable value of uuid16 -> out (returns length) */
uint16_t erya_read(struct erya *e, uint16_t uuid16, uint8_t *out, uint16_t cap);
/* build an "EY" frame around json into out; returns length or 0 */
uint32_t erya_frame_ey(const char *json, uint8_t *out, uint32_t cap);

/* JSON hooks: implemented on top of cJSON (erya_json.c) */
int erya_json_pic(const char *json, uint32_t *num, uint32_t *sum, uint32_t *size);
int erya_json_navi(const char *json, struct nav_state *ns);
/* "fun" of an EY message into out; returns 0 if present */
int erya_json_fun(const char *json, char *out, size_t cap);
int erya_json_time(const char *json, uint32_t *utc, int *tz_hours);
int erya_json_activate(const char *json, int *type);

#ifdef __cplusplus
}
#endif
#endif
