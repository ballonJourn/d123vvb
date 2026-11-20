/*
 * erya_proto.c - 尔雅 BLE 导航协议 Ver 1.2, device side engine.
 * See erya_proto.h for the protocol summary.
 *
 * Both frame types are parsed as a byte stream, so one frame may span several
 * ATT writes and one write may carry several frames. The binary part of an
 * "EB" frame is copied straight into the picture buffer (no intermediate copy),
 * so the app may choose any packet size.
 *
 * Picture assembly: packets "num" 0..sum-1 are appended in order; num 0 starts
 * a new picture. A gap or an out-of-order packet drops the picture and the
 * engine waits for the next num 0. When all "size" bytes are in, the JPEG is
 * handed to ops.on_picture (ownership transferred).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <stdio.h>
#include "erya_proto.h"

#define ERYA_JSON_HARD_LIMIT (64u * 1024u)  /* anything longer = lost sync */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint32_t erya_frame_ey(const char *json, uint8_t *out, uint32_t cap)
{
    uint32_t n = (uint32_t)strlen(json);
    if (cap < n + 6)
        return 0;
    out[0] = 'E'; out[1] = 'Y';
    out[2] = n >> 24; out[3] = n >> 16; out[4] = n >> 8; out[5] = n;
    memcpy(out + 6, json, n);
    return n + 6;
}

static uint32_t pic_format_frame(struct erya *e, uint8_t *out, uint32_t cap)
{
    char js[128];
    snprintf(js, sizeof(js), "{\"fun\":\"pic\",\"w\":%u,\"h\":%u,\"q\":%u,\"type\":\"%s\"}",
             e->cfg.pic_w, e->cfg.pic_h, e->cfg.pic_q, e->cfg.pic_type);
    return erya_frame_ey(js, out, cap);
}

static uint16_t mac_key(struct erya *e, char *out, uint16_t cap)
{
    if (!e->connected || !e->mac_published)
        return (uint16_t)snprintf(out, cap, "0");
    int n = snprintf(out, cap, "%s&%s", e->cfg.mac, e->cfg.model_key);
    return n < 0 ? 0 : (uint16_t)(n >= cap ? cap - 1 : n);
}

static void notify(struct erya *e, uint16_t uuid, const uint8_t *b, uint16_t n)
{
    if (e->ops.notify && e->connected)
        e->ops.notify(e->ops.ctx, uuid, b, n);
}

static void send_pic_format(struct erya *e)
{
    uint8_t f[160];
    uint32_t n = pic_format_frame(e, f, sizeof(f));
    if (n)
        notify(e, ERYA_UUID_AAA6, f, (uint16_t)n);
}

static void stream_reset(struct erya_stream *s)
{
    s->hdr_len = 0;
    s->is_bin = 0;
    s->json_len = s->bin_len = s->json_got = s->bin_got = 0;
    s->skipping_json = 0;
}

static void pic_abort(struct erya *e, int lost)
{
    if (lost && e->pic_next >= 0)
        e->st.pics_lost++;
    e->pic_next = -1;
    e->pkt_accept = 0;
}

static void pic_free(struct erya *e)
{
    if (e->pic && e->ops.free)
        e->ops.free(e->ops.ctx, e->pic);
    e->pic = NULL;
    e->pic_size = e->pic_got = 0;
}

void erya_init(struct erya *e, const struct erya_cfg *cfg, const struct erya_ops *ops)
{
    memset(e, 0, sizeof(*e));
    e->cfg = *cfg;
    e->ops = *ops;
    e->phone = ERYA_PHONE_UNKNOWN;
    e->pic_next = -1;
    e->activate = -1;
    stream_reset(&e->s6);
    stream_reset(&e->s7);
    stream_reset(&e->s8);
}

void erya_on_connect(struct erya *e)
{
    e->connected = 1;
    e->mac_published = 1;   /* step 3: device publishes "<mac>&<key>" on AAA5 */
    e->phone = ERYA_PHONE_UNKNOWN;
    stream_reset(&e->s6);
    stream_reset(&e->s7);
    stream_reset(&e->s8);
}

void erya_on_disconnect(struct erya *e)
{
    e->connected = 0;
    e->mac_published = 0;
    pic_abort(e, 1);
    pic_free(e);
    stream_reset(&e->s6);
    stream_reset(&e->s7);
    stream_reset(&e->s8);
}

/* ---------------- picture packets ---------------- */
static void pic_packet_start(struct erya *e, uint32_t num, uint32_t sum, uint32_t size,
                             uint32_t bin_len)
{
    e->st.pkts++;
    e->pkt_accept = 0;
    if (num == 0) {
        if (e->pic_next >= 0)
            e->st.pics_lost++;          /* previous picture never completed */
        e->pic_next = -1;
        if (size == 0 || sum == 0)
            return;
        if (size > e->cfg.max_pic_bytes) {
            e->st.pics_too_big++;
            return;
        }
        if (e->pic && e->pic_size != size)
            pic_free(e);
        if (!e->pic) {
            e->pic = e->ops.alloc ? e->ops.alloc(e->ops.ctx, size) : NULL;
            if (!e->pic) {
                e->st.pics_alloc_fail++;
                return;
            }
        }
        e->pic_size = size;
        e->pic_got = 0;
        e->pic_sum = sum;
        e->pic_next = 0;
        e->pic_t0 = e->ops.now_ms ? e->ops.now_ms(e->ops.ctx) : 0;
    }
    if (e->pic_next < 0 || !e->pic)
        return;                         /* waiting for the next num 0 */
    if (num != (uint32_t)e->pic_next || size != e->pic_size ||
        e->pic_got + bin_len > e->pic_size) {
        pic_abort(e, 1);
        return;
    }
    e->pkt_accept = 1;
    e->pkt_num = num;
    e->pkt_bin_left = bin_len;
}

static void pic_packet_data(struct erya *e, const uint8_t *b, uint32_t n)
{
    if (!e->pkt_accept)
        return;
    memcpy(e->pic + e->pic_got, b, n);
    e->pic_got += n;
}

static void pic_packet_end(struct erya *e)
{
    if (!e->pkt_accept)
        return;
    e->pkt_accept = 0;
    e->pic_next++;
    if ((uint32_t)e->pic_next < e->pic_sum && e->pic_got < e->pic_size)
        return;                         /* more packets to come */
    if (e->pic_got != e->pic_size || (uint32_t)e->pic_next != e->pic_sum) {
        pic_abort(e, 1);
        return;
    }
    uint8_t *buf = e->pic;
    uint32_t len = e->pic_size;
    e->pic = NULL;                      /* ownership -> on_picture */
    e->pic_size = e->pic_got = 0;
    e->pic_next = -1;
    e->st.pics_ok++;
    e->st.last_pic_bytes = len;
    e->st.last_pic_ms = (e->ops.now_ms ? e->ops.now_ms(e->ops.ctx) : 0) - e->pic_t0;
    if (e->ops.on_picture)
        e->ops.on_picture(e->ops.ctx, buf, len);
    else if (e->ops.free)
        e->ops.free(e->ops.ctx, buf);
}

/* ---------------- frames ---------------- */
static void frame_json_done(struct erya *e, struct erya_stream *s)
{
    if (s->skipping_json) {
        e->st.json_too_long++;
        return;
    }
    s->json[s->json_len] = 0;
    if (s->is_bin) {
        uint32_t num, sum, size;
        e->st.frames_eb++;
        if (erya_json_pic(s->json, &num, &sum, &size) == 0)
            pic_packet_start(e, num, sum, size, s->bin_len);
        else
            e->st.json_err++;
        return;
    }
    e->st.frames_ey++;
    char fun[16];
    if (erya_json_fun(s->json, fun, sizeof(fun))) {
        e->st.json_err++;
        return;
    }
    if (!strcmp(fun, "navi")) {
        struct nav_state ns = e->nav;
        if (erya_json_navi(s->json, &ns) == 0) {
            e->nav = ns;
            e->st.navi_msgs++;
            if (e->ops.on_navi)
                e->ops.on_navi(e->ops.ctx, &e->nav);
        } else {
            e->st.json_err++;
        }
    } else if (!strcmp(fun, "time")) {
        uint32_t utc;
        int tz;
        if (erya_json_time(s->json, &utc, &tz) == 0) {
            e->time_utc = utc;
            e->time_tz = tz;
            e->st.time_msgs++;
            if (e->ops.on_time)
                e->ops.on_time(e->ops.ctx, utc, tz);
        } else {
            e->st.json_err++;
        }
    } else if (!strcmp(fun, "activate")) {
        int type;
        if (erya_json_activate(s->json, &type) == 0) {
            e->activate = type;
            e->st.activate_msgs++;
            if (e->ops.on_activate)
                e->ops.on_activate(e->ops.ctx, type);
        } else {
            e->st.json_err++;
        }
    } else {
        e->st.unknown_fun++;
    }
}

static void stream_feed(struct erya *e, struct erya_stream *s, const uint8_t *b, uint32_t n)
{
    while (n) {
        /* --- header --- */
        uint8_t need = s->is_bin ? 10 : 6;
        if (s->hdr_len < 2 || s->hdr_len < need) {
            uint8_t c = *b++;
            n--;
            if (s->hdr_len == 0) {
                if (c != 'E') { e->st.bad_magic++; continue; }
            } else if (s->hdr_len == 1) {
                if (c == 'Y') s->is_bin = 0;
                else if (c == 'B') s->is_bin = 1;
                else { e->st.bad_magic++; s->hdr_len = (c == 'E'); continue; }
            }
            s->hdr[s->hdr_len++] = c;
            need = s->is_bin ? 10 : 6;
            if (s->hdr_len < need)
                continue;
            s->json_len = be32(s->hdr + 2);
            s->bin_len = s->is_bin ? be32(s->hdr + 6) : 0;
            if (s->json_len > ERYA_JSON_HARD_LIMIT ||
                s->bin_len > e->cfg.max_pic_bytes + ERYA_JSON_HARD_LIMIT) {
                e->st.bad_magic++;      /* lost sync: hunt for the next "E" */
                stream_reset(s);
                continue;
            }
            s->json_got = s->bin_got = 0;
            s->skipping_json = s->json_len > ERYA_JSON_MAX;
            if (s->json_len == 0) {
                frame_json_done(e, s);
                if (s->bin_len == 0) { stream_reset(s); }
            }
            continue;
        }
        /* --- JSON part --- */
        if (s->json_got < s->json_len) {
            uint32_t k = s->json_len - s->json_got;
            if (k > n) k = n;
            if (!s->skipping_json)
                memcpy(s->json + s->json_got, b, k);
            s->json_got += k;
            b += k; n -= k;
            if (s->json_got == s->json_len) {
                frame_json_done(e, s);
                if (s->bin_len == 0) {
                    if (s->is_bin) pic_packet_end(e);
                    stream_reset(s);
                }
            }
            continue;
        }
        /* --- binary part (EB) --- */
        uint32_t k = s->bin_len - s->bin_got;
        if (k > n) k = n;
        pic_packet_data(e, b, k);
        s->bin_got += k;
        b += k; n -= k;
        if (s->bin_got == s->bin_len) {
            pic_packet_end(e);
            stream_reset(s);
        }
    }
}

/* ---------------- GATT events ---------------- */
void erya_on_write(struct erya *e, uint16_t uuid, const uint8_t *b, uint16_t n)
{
    e->st.bytes_rx += n;
    switch (uuid) {
    case ERYA_UUID_AAA5:            /* step 4: phone type */
        if (n >= 1) {
            uint8_t t = b[0];
            if (t == '0' || t == '1')
                t -= '0';
            e->phone = (t == 1) ? ERYA_PHONE_IOS : ERYA_PHONE_ANDROID;
            static const uint8_t ble_only[] = "-3";
            notify(e, ERYA_UUID_AAA4, ble_only, 2);
            send_pic_format(e);     /* 手机投屏流程 1: 设置导航图片格式 */
        }
        break;
    case ERYA_UUID_AAA6:            /* app: time / activate (not in Ver 1.2) */
        stream_feed(e, &e->s6, b, n);
        break;
    case ERYA_UUID_AAA7:
        stream_feed(e, &e->s7, b, n);
        break;
    case ERYA_UUID_AAA8:
        stream_feed(e, &e->s8, b, n);
        break;
    default:
        break;
    }
}

void erya_on_subscribe(struct erya *e, uint16_t uuid)
{
    uint8_t v[ERYA_VAL_MAX];
    uint16_t n = erya_read(e, uuid, v, sizeof(v));
    if (n && (uuid == ERYA_UUID_AAA5 || uuid == ERYA_UUID_AAA4 || uuid == ERYA_UUID_AAA6))
        notify(e, uuid, v, n);
}

uint16_t erya_read(struct erya *e, uint16_t uuid, uint8_t *out, uint16_t cap)
{
    switch (uuid) {
    case ERYA_UUID_AAA5:
        return mac_key(e, (char *)out, cap);
    case ERYA_UUID_AAA4:
        if (cap < 2) return 0;
        memcpy(out, "-3", 2);
        return 2;
    case ERYA_UUID_AAA6:
        return (uint16_t)pic_format_frame(e, out, cap);
    default:
        return 0;
    }
}
