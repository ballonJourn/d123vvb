/*
 * ble_img_rx.h - transport independent BNP frame reassembly
 * (bitmap of received chunks, selective NACK, resume, CRC32 check)
 *
 * No BLE / RTOS dependency: the same file is built for D133 and for the
 * host simulator in tools/sim.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BLE_IMG_RX_H
#define BLE_IMG_RX_H

#include <stddef.h>
#include <stdint.h>
#include "bnp_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

struct bir_frame {             /* a fully received + CRC-checked frame */
    uint16_t frame_id;
    uint8_t  fmt, layer;
    uint16_t width, height, x, y;
    uint32_t len;
    uint8_t *data;             /* owned by rx engine until bir_release() */
    uint32_t rx_ms;
};

struct bir_ops {
    /* send a CTRL notification to the central */
    int  (*notify)(void *ctx, const uint8_t *buf, uint16_t len);
    /* large buffer alloc; on D133 use CMA/uncached-capable memory for VE */
    void*(*alloc)(void *ctx, size_t len);
    void (*free)(void *ctx, void *p);
    uint32_t (*now_ms)(void *ctx);
    /* complete frame ready; return 0 to accept (call bir_release later) */
    int  (*on_frame)(void *ctx, const struct bir_frame *f);
    /* NAV_TEXT TLV blob, already length-checked */
    void (*on_nav_text)(void *ctx, const uint8_t *tlv, uint16_t len);
    void *ctx;
};

struct bir_stats {
    uint32_t frames_ok, frames_crc_err, resumes, aborts;
    uint32_t chunks_rx, chunks_dup, chunks_bad;
    uint32_t bytes_rx;
};

#define BIR_NUM_LAYERS 2

struct bir_slot {
    int       active;          /* START seen, not yet committed */
    int       handed_off;      /* given to on_frame, awaiting release */
    struct bnp_start hdr;
    uint8_t  *buf;
    uint32_t  buf_cap;
    uint8_t  *bitmap;
    uint16_t  n_chunks, have, contig;
    uint32_t  t_start;
    uint32_t  dup, bad;
};

struct bir {
    struct bir_ops ops;
    uint32_t max_frame;        /* largest accepted total_len */
    uint16_t max_chunk;        /* att_mtu - 3 - BNP_DATA_HDR_LEN */
    uint16_t max_notify;       /* att_mtu - 3 */
    uint16_t status_every;     /* auto STATUS notify every N new chunks, 0=off */
    uint16_t since_status;
    struct bir_slot slot[BIR_NUM_LAYERS];
    int      data_slot;        /* slot that DATA writes go to (last START) */
    struct bir_stats st;
};

void bir_init(struct bir *r, const struct bir_ops *ops, uint32_t max_frame);
void bir_set_mtu(struct bir *r, uint16_t att_mtu);
/* feed one write on CTRL / DATA characteristic */
void bir_on_ctrl(struct bir *r, const uint8_t *buf, uint16_t len);
void bir_on_data(struct bir *r, const uint8_t *buf, uint16_t len);
/* link dropped: keep partial frames for resume */
void bir_on_disconnect(struct bir *r);
/* display done with frame of this layer */
void bir_release(struct bir *r, uint8_t layer);
void bir_deinit(struct bir *r);

/* build STATUS for a slot into out (ranges trimmed to fit cap), returns len */
uint16_t bir_build_status(struct bir *r, int slot, uint8_t *out, uint16_t cap);

uint32_t bnp_crc32(uint32_t crc, const uint8_t *p, size_t n);

#ifdef __cplusplus
}
#endif
#endif
