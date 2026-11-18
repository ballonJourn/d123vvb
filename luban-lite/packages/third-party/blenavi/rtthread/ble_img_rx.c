/*
 * ble_img_rx.c - BNP frame reassembly
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "ble_img_rx.h"

/* ---------- CRC-32/ISO-HDLC (zlib compatible) ---------- */
static uint32_t crc_tab[256];
static int crc_tab_ok;

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[i] = c;
    }
    crc_tab_ok = 1;
}

uint32_t bnp_crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!crc_tab_ok)
        crc_init();
    crc = ~crc;
    while (n--)
        crc = crc_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* ---------- little-endian helpers (no unaligned struct access) ---------- */
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint8_t *wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; return p + 2; }
static inline uint8_t *wr32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; return p + 4;
}

#define BM_GET(bm, i) ((bm)[(i) >> 3] & (1u << ((i) & 7)))
#define BM_SET(bm, i) ((bm)[(i) >> 3] |= (uint8_t)(1u << ((i) & 7)))

static void send_err(struct bir *r, uint8_t op, uint8_t err)
{
    uint8_t b[3] = { BNP_RSP_ERROR, op, err };
    r->ops.notify(r->ops.ctx, b, sizeof(b));
}

static void slot_free(struct bir *r, struct bir_slot *s)
{
    if (s->buf)
        r->ops.free(r->ops.ctx, s->buf);
    if (s->bitmap)
        r->ops.free(r->ops.ctx, s->bitmap);
    memset(s, 0, sizeof(*s));
}

void bir_init(struct bir *r, const struct bir_ops *ops, uint32_t max_frame)
{
    memset(r, 0, sizeof(*r));
    r->ops = *ops;
    r->max_frame = max_frame;
    r->status_every = 0;
    bir_set_mtu(r, 23);
    r->data_slot = -1;
    if (!crc_tab_ok)
        crc_init();
}

void bir_set_mtu(struct bir *r, uint16_t att_mtu)
{
    if (att_mtu < 23)
        att_mtu = 23;
    r->max_chunk = att_mtu - 3 - BNP_DATA_HDR_LEN;
    r->max_notify = att_mtu - 3;
}

void bir_deinit(struct bir *r)
{
    for (int i = 0; i < BIR_NUM_LAYERS; i++)
        slot_free(r, &r->slot[i]);
}

/* walk bitmap, emit up to BNP_MAX_RANGES missing ranges */
static uint8_t build_ranges(const struct bir_slot *s, uint8_t *out, uint16_t cap)
{
    uint8_t n = 0;
    uint16_t i = s->contig;
    while (i < s->n_chunks && n < BNP_MAX_RANGES && cap >= 4) {
        if (BM_GET(s->bitmap, i)) { i++; continue; }
        uint16_t first = i;
        while (i < s->n_chunks && !BM_GET(s->bitmap, i))
            i++;
        out = wr16(out, first);
        out = wr16(out, (uint16_t)(i - first));
        cap -= 4;
        n++;
    }
    return n;
}

uint16_t bir_build_status(struct bir *r, int si, uint8_t *out, uint16_t cap)
{
    struct bir_slot *s = &r->slot[si];
    const uint16_t fixed = sizeof(struct bnp_status);
    if (cap < fixed)
        return 0;
    uint8_t *p = out;
    *p++ = BNP_RSP_STATUS;
    p = wr16(p, s->hdr.frame_id);
    p = wr16(p, s->n_chunks);
    p = wr16(p, s->have);
    p = wr16(p, s->contig);
    p = wr32(p, s->dup);
    p = wr32(p, s->bad);
    uint8_t *nr = p++;
    *nr = s->bitmap ? build_ranges(s, p, cap - fixed) : 0;
    return (uint16_t)(fixed + 4 * *nr);
}

static void notify_status(struct bir *r, int si)
{
    uint8_t b[sizeof(struct bnp_status) + 4 * BNP_MAX_RANGES];
    uint16_t cap = r->max_notify < sizeof(b) ? r->max_notify : sizeof(b);
    uint16_t n = bir_build_status(r, si, b, cap);
    if (n)
        r->ops.notify(r->ops.ctx, b, n);
}

static int find_slot_by_id(struct bir *r, uint16_t id)
{
    for (int i = 0; i < BIR_NUM_LAYERS; i++)
        if ((r->slot[i].active || r->slot[i].handed_off) && r->slot[i].hdr.frame_id == id)
            return i;
    return -1;
}

static void op_start(struct bir *r, const uint8_t *b, uint16_t len)
{
    if (len < sizeof(struct bnp_start)) { send_err(r, BNP_OP_START, BNP_E_LEN); return; }

    struct bnp_start h;
    h.op = b[0];
    h.frame_id = rd16(b + 1);
    h.fmt = b[3];
    h.layer = b[4];
    h.width = rd16(b + 5);
    h.height = rd16(b + 7);
    h.x = rd16(b + 9);
    h.y = rd16(b + 11);
    h.total_len = rd32(b + 13);
    h.crc32 = rd32(b + 17);
    h.chunk_size = rd16(b + 21);

    uint8_t err = BNP_OK;
    if (h.layer >= BIR_NUM_LAYERS || h.fmt > BNP_FMT_ARGB8888)
        err = BNP_E_BAD_FMT;
    else if (h.total_len == 0 || h.total_len > r->max_frame)
        err = BNP_E_TOO_BIG;
    else if (h.fmt == BNP_FMT_RGB565 && h.total_len != (uint32_t)h.width * h.height * 2)
        err = BNP_E_BAD_FMT;

    struct bir_slot *s = &r->slot[h.layer];
    if (!err && s->handed_off)
        err = BNP_E_BUSY; /* display still owns previous buffer */

    if (err) { send_err(r, BNP_OP_START, err); return; }

    uint16_t cs = h.chunk_size ? h.chunk_size : r->max_chunk;
    if (cs > r->max_chunk)
        cs = r->max_chunk;
    uint32_t nch = (h.total_len + cs - 1) / cs;
    if (nch > 0xFFFF) { send_err(r, BNP_OP_START, BNP_E_TOO_BIG); return; }

    int resumed = 0;
    if (s->active && s->hdr.frame_id == h.frame_id && s->hdr.crc32 == h.crc32 &&
        s->hdr.total_len == h.total_len) {
        /* resume: keep buffer & bitmap. chunk size must stay identical,
         * otherwise sequence numbers no longer map to the same bytes. */
        if (cs != s->hdr.chunk_size) {
            if (s->hdr.chunk_size <= r->max_chunk) {
                cs = s->hdr.chunk_size;
                resumed = 1;
            }
        } else {
            resumed = 1;
        }
    }

    if (!resumed) {
        if (s->active)
            r->st.aborts++;
        uint32_t bm_len = (nch + 7) / 8;
        if (s->buf_cap < h.total_len) {
            if (s->buf) r->ops.free(r->ops.ctx, s->buf);
            s->buf = r->ops.alloc(r->ops.ctx, h.total_len);
            s->buf_cap = s->buf ? h.total_len : 0;
        }
        if (s->bitmap) r->ops.free(r->ops.ctx, s->bitmap);
        s->bitmap = r->ops.alloc(r->ops.ctx, bm_len);
        if (!s->buf || !s->bitmap) {
            slot_free(r, s);
            send_err(r, BNP_OP_START, BNP_E_TOO_BIG);
            return;
        }
        memset(s->bitmap, 0, bm_len);
        s->hdr = h;
        s->hdr.chunk_size = cs;
        s->n_chunks = (uint16_t)nch;
        s->have = s->contig = 0;
        s->dup = s->bad = 0;
        s->t_start = r->ops.now_ms(r->ops.ctx);
        s->active = 1;
    } else {
        r->st.resumes++;
    }
    r->data_slot = h.layer;
    r->since_status = 0;

    uint8_t a[sizeof(struct bnp_start_ack)], *p = a;
    *p++ = BNP_RSP_START_ACK;
    *p++ = BNP_OK;
    p = wr16(p, h.frame_id);
    p = wr16(p, cs);
    p = wr16(p, s->n_chunks);
    p = wr16(p, s->have);
    r->ops.notify(r->ops.ctx, a, sizeof(a));
    if (resumed)
        notify_status(r, h.layer);
}

static void op_commit(struct bir *r, const uint8_t *b, uint16_t len)
{
    if (len < 3) { send_err(r, BNP_OP_COMMIT, BNP_E_LEN); return; }
    uint16_t id = rd16(b + 1);
    int si = find_slot_by_id(r, id);
    uint8_t out[sizeof(struct bnp_result) + 4 * BNP_MAX_RANGES], *p = out;

    *p++ = BNP_RSP_RESULT;
    uint8_t *perr = p++;
    p = wr16(p, id);
    uint8_t *pcrc = p; p = wr32(p, 0);
    uint8_t *pms = p;  p = wr32(p, 0);
    uint8_t *pnr = p++;
    *pnr = 0;

    if (si < 0) { *perr = BNP_E_NO_FRAME; r->ops.notify(r->ops.ctx, out, (uint16_t)(p - out)); return; }
    struct bir_slot *s = &r->slot[si];

    if (s->handed_off) { /* duplicate commit after success (lost notify) */
        *perr = BNP_OK;
        wr32(pcrc, s->hdr.crc32);
        r->ops.notify(r->ops.ctx, out, (uint16_t)(p - out));
        return;
    }

    uint32_t ms = r->ops.now_ms(r->ops.ctx) - s->t_start;
    wr32(pms, ms);

    if (s->have < s->n_chunks) {
        *perr = BNP_E_INCOMPLETE;
        uint16_t room = r->max_notify > (uint16_t)(p - out) ? r->max_notify - (uint16_t)(p - out) : 0;
        if (room > 4 * BNP_MAX_RANGES)
            room = 4 * BNP_MAX_RANGES;
        *pnr = build_ranges(s, p, room);
        p += 4 * *pnr;
        r->ops.notify(r->ops.ctx, out, (uint16_t)(p - out));
        return;
    }

    uint32_t crc = bnp_crc32(0, s->buf, s->hdr.total_len);
    wr32(pcrc, crc);
    if (crc != s->hdr.crc32) {
        /* whole frame suspect: we can't localise the bad chunk without
         * per-chunk CRC, so reset bitmap and ask for everything again */
        r->st.frames_crc_err++;
        memset(s->bitmap, 0, (s->n_chunks + 7) / 8);
        s->have = s->contig = 0;
        *perr = BNP_E_CRC;
        r->ops.notify(r->ops.ctx, out, (uint16_t)(p - out));
        return;
    }

    struct bir_frame f = {
        .frame_id = id, .fmt = s->hdr.fmt, .layer = s->hdr.layer,
        .width = s->hdr.width, .height = s->hdr.height,
        .x = s->hdr.x, .y = s->hdr.y,
        .len = s->hdr.total_len, .data = s->buf, .rx_ms = ms,
    };
    s->active = 0;
    s->handed_off = 1;
    r->st.frames_ok++;
    int rc = r->ops.on_frame ? r->ops.on_frame(r->ops.ctx, &f) : -1;
    *perr = rc == 0 ? BNP_OK : BNP_E_DISPLAY;
    if (rc != 0)
        s->handed_off = 0;
    r->ops.notify(r->ops.ctx, out, (uint16_t)(p - out));
}

void bir_on_ctrl(struct bir *r, const uint8_t *b, uint16_t len)
{
    if (len < 1)
        return;
    switch (b[0]) {
    case BNP_OP_START:  op_start(r, b, len); break;
    case BNP_OP_QUERY: {
        if (len < 3) { send_err(r, b[0], BNP_E_LEN); break; }
        int si = find_slot_by_id(r, rd16(b + 1));
        if (si < 0) send_err(r, b[0], BNP_E_NO_FRAME);
        else notify_status(r, si);
        break;
    }
    case BNP_OP_COMMIT: op_commit(r, b, len); break;
    case BNP_OP_ABORT: {
        if (len < 3) break;
        int si = find_slot_by_id(r, rd16(b + 1));
        if (si >= 0 && !r->slot[si].handed_off) {
            slot_free(r, &r->slot[si]);
            r->st.aborts++;
        }
        break;
    }
    case BNP_OP_NAV_TEXT: {
        /* validate TLV bounds before handing it out */
        uint16_t i = 1;
        while (i + 2 <= len) {
            uint16_t l = b[i + 1];
            if (i + 2 + l > len) { send_err(r, b[0], BNP_E_LEN); return; }
            i += 2 + l;
        }
        if (r->ops.on_nav_text)
            r->ops.on_nav_text(r->ops.ctx, b + 1, len - 1);
        break;
    }
    default:
        /* GET_PARAMS / REQ_PHY / REQ_CONN are link-level: bnp_service.cpp */
        break;
    }
}

void bir_on_data(struct bir *r, const uint8_t *b, uint16_t len)
{
    if (len <= BNP_DATA_HDR_LEN || r->data_slot < 0) { r->st.chunks_bad++; return; }
    struct bir_slot *s = &r->slot[r->data_slot];
    uint16_t seq = rd16(b + 1);
    uint16_t pl = len - BNP_DATA_HDR_LEN;

    if (!s->active || b[0] != (uint8_t)s->hdr.frame_id || seq >= s->n_chunks) {
        s->bad++; r->st.chunks_bad++;
        return;
    }
    uint32_t off = (uint32_t)seq * s->hdr.chunk_size;
    uint32_t want = s->hdr.total_len - off;
    if (want > s->hdr.chunk_size)
        want = s->hdr.chunk_size;
    if (pl != want) { s->bad++; r->st.chunks_bad++; return; }

    r->st.chunks_rx++;
    if (BM_GET(s->bitmap, seq)) { s->dup++; r->st.chunks_dup++; return; }

    memcpy(s->buf + off, b + BNP_DATA_HDR_LEN, pl);
    BM_SET(s->bitmap, seq);
    s->have++;
    r->st.bytes_rx += pl;
    while (s->contig < s->n_chunks && BM_GET(s->bitmap, s->contig))
        s->contig++;

    if (r->status_every && ++r->since_status >= r->status_every) {
        r->since_status = 0;
        notify_status(r, r->data_slot);
    }
}

void bir_on_disconnect(struct bir *r)
{
    /* intentionally keep active slots: same frame_id+crc START resumes */
    r->data_slot = -1;
}

void bir_release(struct bir *r, uint8_t layer)
{
    if (layer < BIR_NUM_LAYERS)
        r->slot[layer].handed_off = 0;
}
