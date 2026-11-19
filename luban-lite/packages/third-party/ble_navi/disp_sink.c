/*
 * disp_sink.c - received frame -> screen, on a dedicated thread.
 *
 * Decode/blit sequence is the one ArtInChip ships in
 *   packages/artinchip/mpp/mpp_test/pic_dec_test.c (decode_pic + render_frame):
 *   mpp_decoder_create(MJPEG) -> init -> get_packet -> memcpy -> put_packet
 *   -> decode -> get_frame -> GE bitblt into UI layer -> put_frame -> destroy
 * RGB565 / 1bpp frames skip the decoder and go straight to GE.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <string.h>
#include "aic_core.h"
#include "mpp_fb.h"
#include "mpp_ge.h"
#include "mpp_decoder.h"
#include "disp_sink.h"
#include "ble_service.h"

#ifndef BLE_NAVI_TURN_X
#define BLE_NAVI_TURN_X 16
#endif
#ifndef BLE_NAVI_TURN_Y
#define BLE_NAVI_TURN_Y 16
#endif
#ifndef BLE_NAVI_TURN_SCALE
#define BLE_NAVI_TURN_SCALE 2           /* 48x48 icon shown as 96x96 */
#endif
#define DISP_QUEUE_DEPTH 4

struct disp_msg {
    struct bir_frame f;
    uint8_t own_buf;                    /* 1: data allocated here, free after */
};

static rt_mq_t       s_mq;
#ifndef BLE_NAVI_DISP_LVGL
static struct mpp_fb *s_fb;
static struct mpp_ge *s_ge;
#endif
static struct aicfb_screeninfo s_info;
static struct {
    uint32_t frames, jpeg, rgb, icon, drops, errors;
    uint32_t last_decode_ms, last_blit_ms;
} s_st;

static uint32_t now_ms(void) { return rt_tick_get() * 1000 / RT_TICK_PER_SECOND; }

#ifndef BLE_NAVI_DISP_LVGL
/* current visible UI buffer; with AIC_PAN_DISPLAY we draw into the shown one
 * (no page flip) - acceptable for nav updates, see README "tearing" */
static int ui_dst(struct mpp_buf *dst)
{
    struct aicfb_layer_data layer = { 0 };
    layer.layer_id = AICFB_LAYER_TYPE_UI;
    layer.rect_id = 0;
    if (mpp_fb_ioctl(s_fb, AICFB_GET_LAYER_CONFIG, &layer) < 0)
        return -1;
    memset(dst, 0, sizeof(*dst));
    dst->buf_type = MPP_PHY_ADDR;
    dst->phy_addr[0] = layer.buf.phy_addr[0];
    dst->format = s_info.format;
    dst->stride[0] = s_info.stride;
    dst->size.width = s_info.width;
    dst->size.height = s_info.height;
    return 0;
}

static int blit(struct mpp_buf *src, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    struct ge_bitblt blt;
    memset(&blt, 0, sizeof(blt));
    blt.src_buf = *src;
    if (ui_dst(&blt.dst_buf))
        return -1;
    if (x >= s_info.width || y >= s_info.height)
        return -1;
    if (!w) w = src->crop_en ? src->crop.width : src->size.width;
    if (!h) h = src->crop_en ? src->crop.height : src->size.height;
    if (x + w > s_info.width)  w = s_info.width - x;     /* clip, GE scales src->dst */
    if (y + h > s_info.height) h = s_info.height - y;
    blt.dst_buf.crop_en = 1;
    blt.dst_buf.crop.x = x;
    blt.dst_buf.crop.y = y;
    blt.dst_buf.crop.width = w;
    blt.dst_buf.crop.height = h;

    uint32_t t = now_ms();
    int ret = mpp_ge_bitblt(s_ge, &blt);
    if (!ret) ret = mpp_ge_emit(s_ge);
    if (!ret) ret = mpp_ge_sync(s_ge);
    s_st.last_blit_ms = now_ms() - t;
    return ret;
}

static int show_raw(const struct bir_frame *f, enum mpp_pixel_format fmt, uint32_t bpp)
{
    struct mpp_buf src;
    memset(&src, 0, sizeof(src));
    aicos_dcache_clean_range((void *)f->data, f->len);  /* GE reads DRAM */
    src.buf_type = MPP_PHY_ADDR;
    src.phy_addr[0] = (unsigned int)(uintptr_t)f->data; /* D133: VA == PA */
    src.format = fmt;
    src.stride[0] = f->width * bpp;
    src.size.width = f->width;
    src.size.height = f->height;
    return blit(&src, f->x, f->y, f->width, f->height);
}

#endif /* !BLE_NAVI_DISP_LVGL */

/*
 * VE output format for JPEG. D13x GE cannot bitblt from YUV ("bitblt not
 * support yuv format, except src format yuv400"), so let the VE convert:
 * VE V30 (D13x) has yuv2rgb and outputs RGB_565/RGB_888/ARGB_8888 directly
 * (mjpeg_decoder.c parse_pix_fmt), as lvgl_v8/lv_drivers/aic_dec.c does when
 * !AIC_VE_DRV_V10. Same format as the framebuffer -> GE only copies/scales.
 */
static enum mpp_pixel_format jpeg_out_fmt(void)
{
#ifdef AIC_VE_DRV_V10
    return MPP_FMT_NV12;               /* old VE: no yuv2rgb, GE there takes YUV */
#else
    switch (s_info.format) {
    case MPP_FMT_RGB_565: case MPP_FMT_BGR_565:
    case MPP_FMT_RGB_888: case MPP_FMT_BGR_888:
    case MPP_FMT_ARGB_8888: case MPP_FMT_ABGR_8888:
    case MPP_FMT_RGBA_8888: case MPP_FMT_BGRA_8888:
        return (enum mpp_pixel_format)s_info.format;
    default:
        return MPP_FMT_RGB_565;
    }
#endif
}

/* decode a JPEG with the VE, hand the decoded frame to use(), release it */
typedef int (*frame_use_fn)(struct mpp_buf *fb, const struct bir_frame *f);

static int jpeg_decode(const struct bir_frame *f, frame_use_fn use)
{
    if (f->len < 4 || f->data[0] != 0xFF || f->data[1] != 0xD8) {
        rt_kprintf("[disp] not a JPEG (no FFD8), %u bytes\n", f->len);
        return -1;
    }
    struct decode_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.bitstream_buffer_size = (f->len + 1023) & ~1023u;
    cfg.extra_frame_num = 0;
    cfg.packet_count = 1;
    cfg.pix_fmt = jpeg_out_fmt();

    uint32_t t = now_ms();
    struct mpp_decoder *dec = mpp_decoder_create(MPP_CODEC_VIDEO_DECODER_MJPEG);
    if (!dec) {
        rt_kprintf("[disp] mpp_decoder_create(MJPEG) failed\n");
        return -1;
    }
    int ret = -1;
    if (mpp_decoder_init(dec, &cfg) < 0) {
        rt_kprintf("[disp] decoder init failed (out fmt %d, %u bytes)\n", cfg.pix_fmt, f->len);
        goto out;
    }

    struct mpp_packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    if (mpp_decoder_get_packet(dec, &pkt, f->len) < 0) {
        rt_kprintf("[disp] decoder get_packet failed\n");
        goto out;
    }
    memcpy(pkt.data, f->data, f->len);
    pkt.size = f->len;
    pkt.flag = PACKET_FLAG_EOS;
    mpp_decoder_put_packet(dec, &pkt);
    if (mpp_decoder_decode(dec) < 0) {
        rt_kprintf("[disp] JPEG decode failed (baseline JPEG expected)\n");
        goto out;
    }

    struct mpp_frame fr;
    memset(&fr, 0, sizeof(fr));
    if (mpp_decoder_get_frame(dec, &fr) < 0) {
        rt_kprintf("[disp] decoder get_frame failed\n");
        goto out;
    }
    s_st.last_decode_ms = now_ms() - t;
    ret = use(&fr.buf, f);
#ifdef BLE_NAVI_ERYA_LOG
    rt_kprintf("[disp] JPEG %ux%u fmt %d -> (%u,%u) decode %u ms, show %u ms%s\n",
               fr.buf.size.width, fr.buf.size.height, fr.buf.format, f->x, f->y,
               s_st.last_decode_ms, s_st.last_blit_ms, ret ? " SHOW FAILED" : "");
#else
    if (ret)
        rt_kprintf("[disp] showing the decoded frame failed (fmt %d)\n", fr.buf.format);
#endif
    mpp_decoder_put_frame(dec, &fr);
out:
    mpp_decoder_destory(dec);
    return ret;
}

#ifndef BLE_NAVI_DISP_LVGL
/* width/height in the frame = target size on screen (0 = native) */
static int blit_use(struct mpp_buf *fb, const struct bir_frame *f)
{
    return blit(fb, f->x, f->y, f->width, f->height);
}
static int show_jpeg(const struct bir_frame *f) { return jpeg_decode(f, blit_use); }
#endif

/* MSB-first 1bpp -> RGB565 (white on black), nearest-neighbour scale */
static uint8_t *expand_1bpp(const uint8_t *bits, uint16_t w, uint16_t h, int sc, uint32_t *out_len)
{
    uint32_t W = w * sc, H = h * sc, len = W * H * 2;
    uint16_t *px = aicos_malloc_align(MEM_CMA, len, CACHE_LINE_SIZE);
    if (!px)
        return NULL;
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            uint32_t sx = x / sc, sy = y / sc, i = sy * w + sx;
            px[y * W + x] = (bits[i >> 3] & (0x80 >> (i & 7))) ? 0xFFFF : 0x0000;
        }
    *out_len = len;
    return (uint8_t *)px;
}

#ifndef BLE_NAVI_DISP_LVGL
static void handle(struct disp_msg *m)
{
    struct bir_frame *f = &m->f;
    int ret = -1;
    s_st.frames++;
    switch (f->fmt) {
    case BNP_FMT_JPEG:     s_st.jpeg++; ret = show_jpeg(f); break;
    case BNP_FMT_RGB565:   s_st.rgb++;  ret = show_raw(f, MPP_FMT_RGB_565, 2); break;
    case BNP_FMT_ARGB8888: s_st.rgb++;  ret = show_raw(f, MPP_FMT_ARGB_8888, 4); break;
    case BNP_FMT_ICON1BPP: {
        s_st.icon++;
        uint32_t len;
        uint8_t *px = expand_1bpp(f->data, f->width, f->height, BLE_NAVI_TURN_SCALE, &len);
        if (px) {
            struct bir_frame g = *f;
            g.data = px; g.len = len;
            g.width *= BLE_NAVI_TURN_SCALE; g.height *= BLE_NAVI_TURN_SCALE;
            ret = show_raw(&g, MPP_FMT_RGB_565, 2);
            aicos_free_align(MEM_CMA, px);
        }
        break;
    }
    }
    if (ret)
        s_st.errors++;
    if (m->own_buf)
        aicos_free_align(MEM_CMA, f->data);
    else
        ble_navi_release(f->layer);       /* BNP slot may accept next START */
}

static void disp_thread(void *p)
{
    (void)p;
    struct disp_msg m;
    for (;;) {
        if (rt_mq_recv(s_mq, &m, sizeof(m), RT_WAITING_FOREVER) >= 0)
            handle(&m);
    }
}

int disp_sink_init(void)
{
    if (s_mq)
        return 0;
    s_fb = mpp_fb_open();
    if (!s_fb || mpp_fb_ioctl(s_fb, AICFB_GET_SCREENINFO, &s_info))
        return -1;
    s_ge = mpp_ge_open();
    if (!s_ge)
        return -1;
    rt_kprintf("[disp] %ux%u fmt=%d stride=%u\n", s_info.width, s_info.height,
               s_info.format, s_info.stride);
    s_mq = rt_mq_create("disp", sizeof(struct disp_msg), DISP_QUEUE_DEPTH, RT_IPC_FLAG_FIFO);
    rt_thread_t t = rt_thread_create("disp", disp_thread, RT_NULL, 4096, 20, 10);
    if (!s_mq || !t)
        return -1;
    return rt_thread_startup(t);
}

#else /* BLE_NAVI_DISP_LVGL */
/*
 * LVGL mode: the application's LVGL (v9, LV_OS_NONE) owns the framebuffer and
 * also uses the VE for its own images, and all LVGL calls must come from the
 * LVGL thread. So frames are queued here and consumed by an lv_timer created
 * in ble_navi_lv_attach() (called from lv_user_gui_init in the LVGL thread):
 * the VE decodes JPEG -> RGB565 in that thread (no VE contention with
 * lv_mpp_dec), and the result is an lv_image on lv_layer_top() (LVGL composes
 * it; nothing redraws over it).
 */
#include "lvgl.h"

#define LV_POLL_MS        30
#define LV_FRAMES_PER_TICK 1        /* keep the LVGL thread responsive */

struct lv_layer_img {
    lv_obj_t      *img;
    lv_image_dsc_t dsc[2];         /* alternate: LVGL caches by src pointer */
    uint8_t       *buf[2];
    uint32_t       cap[2];
    int            cur;
};
static struct lv_layer_img s_lv[BIR_NUM_LAYERS];
static int s_lv_attached;

static uint8_t *lv_next_buf(struct lv_layer_img *L, uint32_t need, int *idx)
{
    int i = L->cur ^ 1;
    if (L->cap[i] < need) {
        if (L->buf[i])
            aicos_free_align(MEM_CMA, L->buf[i]);
        L->buf[i] = aicos_malloc_align(MEM_CMA, need, CACHE_LINE_SIZE);
        L->cap[i] = L->buf[i] ? need : 0;
    }
    *idx = i;
    return L->buf[i];
}

/* copy RGB565 rows into the layer's spare buffer and show it */
static int lv_show_rgb565(const uint8_t *px, uint32_t src_stride, uint16_t w, uint16_t h,
                          const struct bir_frame *f)
{
    uint8_t layer = f->layer < BIR_NUM_LAYERS ? f->layer : 0;
    struct lv_layer_img *L = &s_lv[layer];
    uint32_t t = now_ms(), stride = (uint32_t)w * 2, need = stride * h;
    int i;
    uint8_t *dst = lv_next_buf(L, need, &i);
    if (!dst) {
        rt_kprintf("[disp] no CMA for %ux%u picture\n", w, h);
        return -1;
    }
    for (uint32_t y = 0; y < h; y++)
        memcpy(dst + y * stride, px + y * src_stride, stride);
    aicos_dcache_clean_range(dst, need);           /* LVGL may draw it with GE */

    lv_image_dsc_t *d = &L->dsc[i];
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = w;
    d->header.h = h;
    d->header.stride = stride;
    d->data_size = need;
    d->data = dst;

    if (!L->img) {
        L->img = lv_image_create(lv_layer_top());
        lv_obj_remove_flag(L->img, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_image_cache_drop(d);
    lv_image_set_src(L->img, d);
    lv_obj_set_pos(L->img, f->x, f->y);
    if (f->width && f->height && (f->width != w || f->height != h)) {
        lv_obj_set_size(L->img, f->width, f->height);
        lv_image_set_inner_align(L->img, LV_IMAGE_ALIGN_STRETCH);
    } else {
        lv_obj_set_size(L->img, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    }
    lv_obj_remove_flag(L->img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(L->img);
    L->cur = i;
    s_st.last_blit_ms = now_ms() - t;
    return 0;
}

static int lv_use_decoded(struct mpp_buf *fb, const struct bir_frame *f)
{
    if (fb->format != MPP_FMT_RGB_565) {
        rt_kprintf("[disp] decoded fmt %d, LVGL mode expects RGB565\n", fb->format);
        return -1;
    }
    uint16_t w = fb->crop_en ? fb->crop.width : fb->size.width;
    uint16_t h = fb->crop_en ? fb->crop.height : fb->size.height;
    const uint8_t *px = (const uint8_t *)(uintptr_t)fb->phy_addr[0];   /* D133: VA == PA */
    aicos_dcache_invalid_range((void *)px, fb->stride[0] * h);          /* VE wrote it */
    return lv_show_rgb565(px, fb->stride[0], w, h, f);
}

static void handle(struct disp_msg *m)
{
    struct bir_frame *f = &m->f;
    int ret = -1;
    s_st.frames++;
    switch (f->fmt) {
    case BNP_FMT_JPEG:
        s_st.jpeg++;
        ret = jpeg_decode(f, lv_use_decoded);
        break;
    case BNP_FMT_RGB565:
        s_st.rgb++;
        ret = lv_show_rgb565(f->data, (uint32_t)f->width * 2, f->width, f->height, f);
        break;
    case BNP_FMT_ICON1BPP: {
        s_st.icon++;
        uint32_t len;
        uint8_t *px = expand_1bpp(f->data, f->width, f->height, BLE_NAVI_TURN_SCALE, &len);
        if (px) {
            uint16_t W = f->width * BLE_NAVI_TURN_SCALE, H = f->height * BLE_NAVI_TURN_SCALE;
            struct bir_frame g = *f;
            g.width = g.height = 0;                        /* already scaled */
            ret = lv_show_rgb565(px, (uint32_t)W * 2, W, H, &g);
            aicos_free_align(MEM_CMA, px);
        }
        break;
    }
    default:
        rt_kprintf("[disp] fmt %u not supported in LVGL mode\n", f->fmt);
        break;
    }
    if (ret)
        s_st.errors++;
    if (m->own_buf)
        aicos_free_align(MEM_CMA, f->data);
    else
        ble_navi_release(f->layer);
}

static void lv_poll(lv_timer_t *t)
{
    (void)t;
    struct disp_msg m;
    for (int n = 0; n < LV_FRAMES_PER_TICK; n++) {
        if (!s_mq || rt_mq_recv(s_mq, &m, sizeof(m), 0) < 0)
            break;
        handle(&m);
    }
}

/* call once from the LVGL thread, e.g. at the end of lv_user_gui_init() */
void ble_navi_lv_attach(void)
{
    if (s_lv_attached)
        return;
    if (!s_mq)
        s_mq = rt_mq_create("disp", sizeof(struct disp_msg), DISP_QUEUE_DEPTH, RT_IPC_FLAG_FIFO);
    lv_timer_create(lv_poll, LV_POLL_MS, NULL);
    s_lv_attached = 1;
    rt_kprintf("[disp] LVGL top layer attached\n");
}

int disp_sink_init(void)
{
#if LV_COLOR_DEPTH == 16
    s_info.format = MPP_FMT_RGB_565;               /* VE output = LVGL RGB565 */
#else
#error "BLE_NAVI_DISP_LVGL: only LV_COLOR_DEPTH 16 is implemented"
#endif
    if (!s_mq)
        s_mq = rt_mq_create("disp", sizeof(struct disp_msg), DISP_QUEUE_DEPTH, RT_IPC_FLAG_FIFO);
    if (!s_mq)
        return -1;
    rt_kprintf("[disp] LVGL mode: pictures on lv_layer_top()%s\n",
               s_lv_attached ? "" : " - waiting for ble_navi_lv_attach() from the LVGL thread");
    return 0;
}
#endif /* BLE_NAVI_DISP_LVGL */

int disp_sink_submit(const struct bir_frame *f)
{
    if (!s_mq) { s_st.drops++; return -1; }
    struct disp_msg m = { .f = *f, .own_buf = 0 };
    if (rt_mq_send(s_mq, &m, sizeof(m)) != RT_EOK) { s_st.drops++; return -1; }
    return 0;
}

int disp_sink_submit_owned(const struct bir_frame *f)
{
    struct disp_msg m = { .f = *f, .own_buf = 1 };
    if (!s_mq || rt_mq_send(s_mq, &m, sizeof(m)) != RT_EOK) {
        aicos_free_align(MEM_CMA, f->data);
        s_st.drops++;
        return -1;
    }
    return 0;
}

void disp_sink_turn_icon_1bpp(const uint8_t *bits, uint16_t w, uint16_t h)
{
    if (!s_mq)
        return;
    uint32_t n = (uint32_t)w * h / 8;
    uint8_t *copy = aicos_malloc_align(MEM_CMA, n, CACHE_LINE_SIZE); /* chronos buffer reused */
    if (!copy) { s_st.drops++; return; }
    memcpy(copy, bits, n);
    struct disp_msg m = { .own_buf = 1 };
    m.f.fmt = BNP_FMT_ICON1BPP; m.f.layer = BNP_LAYER_TURN;
    m.f.width = w; m.f.height = h; m.f.x = BLE_NAVI_TURN_X; m.f.y = BLE_NAVI_TURN_Y;
    m.f.data = copy; m.f.len = n;
    if (rt_mq_send(s_mq, &m, sizeof(m)) != RT_EOK) {
        aicos_free_align(MEM_CMA, copy);
        s_st.drops++;
    }
}

RT_WEAK void disp_ui_nav_update(const struct nav_state *ns)
{
    rt_kprintf("[nav] %s | %s | road: %s | eta %s | %s | turn %u\n",
               ns->active ? "ON" : "off", ns->title, ns->directions, ns->eta,
               ns->duration, ns->turn);
}

void disp_sink_nav(const struct nav_state *ns)
{
    disp_ui_nav_update(ns);
}

void disp_sink_dump_stats(void)
{
    rt_kprintf("disp frames=%u jpeg=%u rgb=%u icon=%u drops=%u err=%u decode=%ums blit=%ums\n",
               s_st.frames, s_st.jpeg, s_st.rgb, s_st.icon, s_st.drops, s_st.errors,
               s_st.last_decode_ms, s_st.last_blit_ms);
}
