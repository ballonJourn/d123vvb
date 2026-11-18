/*
 * bnp_proto.h - BLE Navi Protocol (BNP) v1 wire format
 *
 * Central (phone / PC, Web Bluetooth)  --->  D133 (GATT server)
 *
 * All multi-byte fields are little-endian. Packed, no padding.
 *
 * Service  D1330001-4E41-5649-B1E0-0000D1330000
 *   CTRL   D1330002-...  write (with response) + notify   : commands / replies
 *   DATA   D1330003-...  write without response           : image chunks
 *   STAT   D1330004-...  read + notify                    : link params & progress
 *
 * Design lineage (see README "why these repos"):
 *   - EPD-nRF5    : N x write-without-response then 1 write-with-response barrier,
 *                   device reports usable MTU back to the sender.
 *   - Nordic ITS  : image-info header (length) before data, ble_params_info report
 *                   (mtu, conn interval, tx/rx phy), 2M PHY / DLE 251 / 15ms CI.
 *   - chronos     : separate small "nav" objects (text + 48x48 1bpp icon w/ CRC so
 *                   an unchanged icon is never re-sent).
 *   Added here    : per-frame CRC32, chunk sequence numbers + receive bitmap,
 *                   selective NACK (missing ranges), resume after reconnect.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BNP_PROTO_H
#define BNP_PROTO_H

#include <stdint.h>

#define BNP_VERSION            1

/* 128-bit UUIDs, little-endian byte order as used by NimBLE BLE_UUID128_INIT */
#define BNP_UUID128(x16) \
    0x00, 0x00, 0x33, 0xD1, 0x00, 0x00, 0xE0, 0xB1, \
    0x49, 0x56, 0x41, 0x4E, (x16) & 0xFF, ((x16) >> 8) & 0xFF, 0x33, 0xD1
#define BNP_SVC_UUID16         0x0001
#define BNP_CTRL_UUID16        0x0002
#define BNP_DATA_UUID16        0x0003
#define BNP_STAT_UUID16        0x0004
/* string form for Web Bluetooth / python */
#define BNP_SVC_UUID_STR       "d1330001-4e41-5649-b1e0-0000d1330000"

/* ---------------- CTRL opcodes (central -> device) ---------------- */
enum bnp_op {
    BNP_OP_START      = 0x01, /* struct bnp_start                     */
    BNP_OP_QUERY      = 0x02, /* u16 frame_id -> BNP_RSP_STATUS        */
    BNP_OP_COMMIT     = 0x03, /* u16 frame_id -> BNP_RSP_RESULT        */
    BNP_OP_ABORT      = 0x04, /* u16 frame_id                          */
    BNP_OP_NAV_TEXT   = 0x10, /* TLV list, see bnp_nav_tag             */
    BNP_OP_GET_PARAMS = 0x20, /* -> BNP_RSP_PARAMS                     */
    BNP_OP_REQ_PHY    = 0x21, /* u8 phy mask (1=1M,2=2M,4=coded)       */
    BNP_OP_REQ_CONN   = 0x22, /* u16 min_ci, u16 max_ci (1.25ms), u16 latency, u16 timeout(10ms) */
};

/* ---------------- CTRL replies (device -> central, notify) -------- */
enum bnp_rsp {
    BNP_RSP_START_ACK = 0x81, /* struct bnp_start_ack                  */
    BNP_RSP_STATUS    = 0x82, /* struct bnp_status + ranges            */
    BNP_RSP_RESULT    = 0x83, /* struct bnp_result (+ ranges on NACK)  */
    BNP_RSP_PARAMS    = 0xA0, /* struct bnp_params                     */
    BNP_RSP_ERROR     = 0xEE, /* u8 opcode, u8 err                     */
};

enum bnp_err {
    BNP_OK            = 0,
    BNP_E_BUSY        = 1,
    BNP_E_TOO_BIG     = 2,
    BNP_E_BAD_FMT     = 3,
    BNP_E_NO_FRAME    = 4,
    BNP_E_INCOMPLETE  = 5, /* commit with holes -> ranges follow */
    BNP_E_CRC         = 6,
    BNP_E_DISPLAY     = 7,
    BNP_E_LEN         = 8,
};

enum bnp_fmt {
    BNP_FMT_RGB565    = 0, /* raw, width*height*2 bytes, LE      */
    BNP_FMT_JPEG      = 1, /* baseline JPEG -> VE hw decoder     */
    BNP_FMT_ICON1BPP  = 2, /* chronos-compatible MSB-first 1bpp  */
    BNP_FMT_ARGB8888  = 3,
};

enum bnp_layer {
    BNP_LAYER_MAP     = 0, /* main navigation picture            */
    BNP_LAYER_TURN    = 1, /* turn arrow / maneuver icon         */
};

#if defined(__GNUC__)
#define BNP_PACKED __attribute__((packed))
#else
#define BNP_PACKED
#endif

struct bnp_start {            /* op = 0x01, 25 bytes incl. op */
    uint8_t  op;
    uint16_t frame_id;        /* sender-chosen, same id+crc => resume */
    uint8_t  fmt;             /* enum bnp_fmt   */
    uint8_t  layer;           /* enum bnp_layer */
    uint16_t width;
    uint16_t height;
    uint16_t x, y;            /* placement on screen */
    uint32_t total_len;
    uint32_t crc32;           /* CRC-32/ISO-HDLC of whole payload */
    uint16_t chunk_size;      /* payload bytes per DATA write     */
} BNP_PACKED;

struct bnp_start_ack {        /* 0x81 */
    uint8_t  rsp;
    uint8_t  err;
    uint16_t frame_id;
    uint16_t chunk_size;      /* may be lowered by device          */
    uint16_t n_chunks;
    uint16_t have_chunks;     /* >0 => resumed, send STATUS ranges */
} BNP_PACKED;

/* DATA characteristic write: header + payload */
struct bnp_data_hdr {
    uint8_t  frame_lo;        /* frame_id & 0xFF, cheap stale-filter */
    uint16_t seq;             /* chunk index                         */
} BNP_PACKED;
#define BNP_DATA_HDR_LEN 3

struct bnp_range { uint16_t first, count; } BNP_PACKED;
/* upper bound; actual count also limited by notify size (att_mtu-3) */
#define BNP_MAX_RANGES 56

struct bnp_status {           /* 0x82 */
    uint8_t  rsp;
    uint16_t frame_id;
    uint16_t n_chunks;
    uint16_t have_chunks;
    uint16_t contig;          /* chunks [0, contig) all present */
    uint32_t dup_chunks;      /* stats: duplicates received     */
    uint32_t bad_chunks;      /* stats: out-of-range / stale    */
    uint8_t  n_ranges;        /* missing ranges that follow      */
    /* struct bnp_range ranges[n_ranges]; */
} BNP_PACKED;

struct bnp_result {           /* 0x83 */
    uint8_t  rsp;
    uint8_t  err;
    uint16_t frame_id;
    uint32_t crc_calc;
    uint32_t rx_ms;           /* START->COMMIT time on device   */
    uint8_t  n_ranges;
} BNP_PACKED;

struct bnp_params {           /* 0xA0, cf. Nordic ble_its_ble_params_info_t */
    uint8_t  rsp;
    uint16_t att_mtu;
    uint16_t conn_interval;   /* 1.25 ms units */
    uint16_t conn_latency;
    uint16_t supervision_to;  /* 10 ms units   */
    uint8_t  tx_phy, rx_phy;  /* 1,2,3(coded)  */
    uint16_t max_tx_octets;   /* DLE           */
    uint16_t max_rx_octets;
    uint8_t  backend;         /* 0 nimble, 1 aic vendor */
} BNP_PACKED;

/* NAV_TEXT TLV: [tag u8][len u8][utf8 bytes...]* */
enum bnp_nav_tag {
    BNP_NAV_ROAD      = 1,  /* current/next road name          */
    BNP_NAV_DIST      = 2,  /* "300 m"                         */
    BNP_NAV_ETA       = 3,
    BNP_NAV_DURATION  = 4,
    BNP_NAV_TURN      = 5,  /* 1 byte maneuver code            */
    BNP_NAV_SPEED     = 6,
    BNP_NAV_ACTIVE    = 7,  /* 1 byte 0/1                       */
};

#endif /* BNP_PROTO_H */
