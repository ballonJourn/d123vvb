#include <rtthread.h>
#include <string.h>
#include <aic_osal.h>      /* aicos_malloc / aicos_free / MEM_PSRAM_CMA */
#include "mw_dvr.h"
/* MPP decode + display path (aligned with third/carlink/src/ec_link.c) */
#include "aic_core.h"      /* transitively required by mpp_ge.h */
#include "mpp_fb.h"        /* mpp_fb_open / close / ioctl, AICFB_* */
#include "mpp_ge.h"        /* mpp_ge_open / close / bitblt / emit / sync */
#include "mpp_decoder.h"   /* mpp_decoder_create / init / decode / *_packet / *_frame */
#include "mpp_dec_type.h"  /* MPP_CODEC_VIDEO_DECODER_MJPEG / MPP_FMT_* / PACKET_FLAG_EOS */

#include "vehicle_param.h" /* vehicle_set_data(VEH_DVR_PLAYING, ...) — drives home_page hide/show */

/* File I/O for the elene capture stream uses the RT-Thread DFS layer
 * (dfs_file_open / dfs_file_read / dfs_file_write / dfs_file_lseek /
 * dfs_file_close) because the bare FATFS API path "/udisk/elene" is
 * not DFS-resolvable and the FATFS-level path "0:/elene" depends on a
 * drive-number assumption that is unsafe across the boot order. The
 * DFS path is the same one "ls udisk" uses internally. */

#if ENABLE_BD_USB_DVR_FUNC

// DVR control variables
static st_bd_ctrl_if_t dvr_ctrl_data;
static st_dvr_capture_t dvr_capture;
static rt_thread_t dvr_task_handle = RT_NULL;
static struct dfs_fd *dvr_fp = NULL;  // Global DFS file descriptor for elene, heap-allocated in dvr_capture_init
static uint8_t dvr_need_send_data = 0;  // Flag to indicate data needs to be sent

// DVR status variables
static uint8_t dvr_sd_status = 0;      // 0: no SD, 1: SD present
static uint8_t dvr_rec_status = 0;    // 0: not recording, 1: recording
static uint8_t dvr_lock_status = 0;   // 0: not locked, 1: locked
static uint8_t dvr_mic_status = 0;    // 0: mic off, 1: mic on
static uint8_t dvr_sd_error = 0;      // 0: normal, 1: error
static uint8_t dvr_sd_full = 0;        // 0: not full, 1: full
static uint8_t dvr_sensor_switch_enable = 0;  // enable auto sensor switch
static uint8_t dvr_view_mode = 0;  // 0:front, 1:rear, 2:f+r, 3:r+f, 4:hzh

// TF card capacity cache — stored in KiB to avoid uint32_t overflow on >4GiB cards.
static volatile uint32_t dvr_tf_total_kib = 0;
static volatile uint32_t dvr_tf_free_kib  = 0;
static volatile uint8_t  dvr_tf_capacity_valid = 0;

// Total-time-of-clip cache: BD_CTRL_TOTAL_TIME's reply only carries the
// index it was answering (pctrl->cmd_par) and the length (pctrl->data_len);
// per dvr_get_total_time()'s call site, data_len itself IS the time in
// seconds for that index (not a byte count — see BD_CTRL_TOTAL_TIME's
// printf below, which already reported it this way before this cache
// existed). Cache both so dvr_api_get_total_time_result() can tell the
// caller whether the cached value is actually for the index it asked about.
static volatile uint16_t dvr_total_time_index = 0;
static volatile uint32_t dvr_total_time_sec   = 0;
static volatile uint8_t  dvr_total_time_valid = 0;

// DEL_FILE acknowledgement flag: set to 1 when DVR replies to a DEL_FILE command.
// Cleared by dvr_api_clear_del_ack() before sending DEL, polled by UI after.
static volatile uint8_t  dvr_del_ack_received = 0;

// DVR firmware version string cache (populated by BD_CTRL_GET_ID response).
// NUL-terminated. 32 bytes covers typical firmware version strings; truncated if longer.
#define DVR_VERSION_MAX_LEN  32
static volatile uint8_t  dvr_version_valid = 0;             /* 0=no response yet, 1=have a sample */
static char               dvr_version_buf[DVR_VERSION_MAX_LEN];

// File list related
#define BYTE_PER_FILE  6
static uint16_t dvr_video_list_count = 0;
static uint16_t dvr_photo_list_count = 0;

/*
 * Filename cache: the last fetched file list for each of the four
 * (camera, kind) buckets, populated by dvr_filelist_parser().
 *
 *   is_jpg = cmd_par & 0x8000   (0=video .avi, 1=photo .jpg)
 *   is_rear = cmd_par & 0x4000  (0=front, 1=rear)
 *
 * Names are kept as NUL-terminated strings, e.g. "MOVI0001.avi",
 * "LOCK0007.avi", "PICT0003.jpg".  The arrays are global so other
 * modules can read them via the dvr_api_*_list_* accessors.
 */
#define DVR_NAME_MAX         16   /* incl. trailing NUL */
#define DVR_VIDEO_LIST_MAX   128  /* must stay <= DVR_FETCH_MAX in dvr_view.c; 128 covers a full 744B GET_LIST (124 files) */
#define DVR_PHOTO_LIST_MAX   128  /* must stay <= DVR_FETCH_MAX in dvr_view.c */

static char dvr_video_list_f[DVR_VIDEO_LIST_MAX][DVR_NAME_MAX];
static uint16_t dvr_video_list_f_count = 0;
static char dvr_video_list_r[DVR_VIDEO_LIST_MAX][DVR_NAME_MAX];
static uint16_t dvr_video_list_r_count = 0;
static char dvr_photo_list_f[DVR_PHOTO_LIST_MAX][DVR_NAME_MAX];
static uint16_t dvr_photo_list_f_count = 0;
static char dvr_photo_list_r[DVR_PHOTO_LIST_MAX][DVR_NAME_MAX];
static uint16_t dvr_photo_list_r_count = 0;

// Display window configuration (for external control)
static int32_t dvr_display_x = 0;       // X coordinate of display window
static int32_t dvr_display_y = 0;       // Y coordinate of display window
static int32_t dvr_display_width = LCD_WIDTH;   // Width of display window
static int32_t dvr_display_height = LCD_HEIGHT; // Height of display window

// Preview mode control flag (for skip jpeg decode when UI not in preview)
// Default OFF: plugging in the DVR USB only connects + queries status in the
// background (dvr_usb_task keeps running for that). It must NOT decode/render
// a fullscreen passthrough preview on its own anymore — that used to bypass
// the new ui_builder dvr_page.c UI entirely (direct GE blit onto the UI-layer
// framebuffer, repainted ~every 10ms, which visually overwrote dvr_page.c's
// own dock/buttons even after the UP key switched the active LVGL screen).
// Preview now only turns on via dvr_start_preview() -> dvr_api_set_preview_enable(1),
// i.e. only when the user opens the DVR page (home page's UP key -> dvr_page_create()).
static uint8_t dvr_preview_enable = 0;  // 0: skip jpeg decode, 1: enable jpeg decode

static uint8_t dvr_ui_hidden = 0;

// Framerate statistics
static uint32_t dvr_frame_count = 0;
static uint32_t dvr_frame_count_last = 0;
static uint32_t dvr_last_stat_time = 0;
static uint32_t dvr_fps = 0;

/* --------------------------------------------------------------------------
 * Decode + display handles (CarLink-style mpp_decoder / mpp_fb)
 *
 * Opened once in dvr_capture_init(), closed in dvr_capture_deinit().
 * The decoder owns its own internal YUV output buffer; the fb handle is
 * used only to query AICFB_GET_LAYER_CONFIG so dvr_render_to_ui_layer
 * can build the GE bitblt destination address. No double-buffered RGB565
 * staging buffer (was dst_buf[2]) and no separate PXP scaling pass.
 * -------------------------------------------------------------------------- */
static struct mpp_decoder     *dvr_mpp_dec = NULL;
static struct mpp_fb          *dvr_mpp_fb  = NULL;
static struct aicfb_screeninfo dvr_screen_info = {0};   /* lazy cache, mirrors ec_link.c:57 */
static enum mpp_pixel_format   dvr_dec_pix_fmt = MPP_FMT_ABGR_8888;

#ifdef AIC_PAN_DISPLAY
static u32 dvr_fb_buf_index = 0;
#endif

/* Bitstream buffer size: protocol allows jpg_size up to ~300 KiB
 * (see dvr_capture_get_pic_process's `jpg_size > 300*1024` guard). */
#define DVR_BITSTREAM_BUF_SIZE  (300 * 1024)

/* DVR-EXPERIMENT: hardware downscale of the MJPEG decode output (VE).
 *   0 = off  : decode at the stream size (1024x600). ABGR frame = 2.4 MB, which
 *              mpp_phy_alloc() sends to the PSRAM SW heap (> 1 MB).
 *   1 = 1/2  : 512x300 -> ABGR frame ~0.6 MB (< 1 MB, goes to the CMA heap)
 *   2 = 1/4, 3 = 1/8
 * GE then scales the small frame up to the display window in dvr_render_to_ui_layer().
 * Set to 0 to restore the original behaviour. */
#ifndef DVR_DEC_SCALE_SHIFT
#define DVR_DEC_SCALE_SHIFT  3
#endif

// FPS print toggle (runtime, default ON). Controls whether the per-second
// `DVR: [FPS:xx] frame_count=xx` log line is emitted. Toggled via CLI.
static uint8_t dvr_fps_print_enable = 0;

// File list ready flag: set by dvr_filelist_parser() after USB response,
// cleared by UI thread after reading. This bridges the async gap between
// dvr_api_get_list() (fire) and dvr_file_list_populate() (consume).
static volatile uint8_t dvr_filelist_ready_flag = 0;

// Skip JPEG process flag (for file list operations)
static uint8_t dvr_skip_jpeg_process = 0;

// Auto-record: set to 1 by dvr_usb_task after init. When the first
// GET_STS response arrives with SD=1, rec_start is sent and flag cleared.
// This ensures recording starts only after the DVR chip has initialized
// its SD card, not before the USB elene file is even open.
static uint8_t dvr_auto_rec_pending = 0;

static int dvr_ge_bitblt(struct ge_bitblt *blt)
{
    struct mpp_ge *ge = mpp_ge_open();
    int ret;

    if (!ge) {
        printf("DVR: mpp_ge_open failed\n");
        return -1;
    }
    ret = mpp_ge_bitblt(ge, blt);
    if (ret < 0) {
        printf("DVR: mpp_ge_bitblt failed: %d\n", ret);
        mpp_ge_close(ge);
        return ret;
    }
    ret = mpp_ge_emit(ge);
    if (ret < 0) {
        printf("DVR: mpp_ge_emit failed: %d\n", ret);
        mpp_ge_close(ge);
        return ret;
    }
    ret = mpp_ge_sync(ge);
    if (ret < 0) {
        printf("DVR: mpp_ge_sync failed: %d\n", ret);
        mpp_ge_close(ge);
        return ret;
    }
    mpp_ge_close(ge);
    return 0;
}

static int dvr_render_to_ui_layer(struct mpp_fb *fb, struct mpp_frame *frame,
                                  u32 offset_x, u32 offset_y,
                                  u32 width, u32 height)
{
    struct ge_bitblt blt;
    struct aicfb_layer_data layer = {0};
    u32 dst_buf_addr;
#ifdef AIC_PAN_DISPLAY
    u32 disp_buf_addr, fb0_buf_addr;
#endif

    /* Lazy screeninfo fetch — first call pays open/ioctl cost; later
     * calls just read the cached struct (mirrors ec_link.c:284-304). */
    if (dvr_screen_info.width == 0) {
        if (mpp_fb_ioctl(fb, AICFB_GET_SCREENINFO, &dvr_screen_info)) {
            printf("DVR: AICFB_GET_SCREENINFO failed\n");
            return -1;
        }
    }

    layer.layer_id = AICFB_LAYER_TYPE_UI;
    layer.rect_id  = 0;
    if (mpp_fb_ioctl(fb, AICFB_GET_LAYER_CONFIG, &layer) < 0) {
        printf("DVR: AICFB_GET_LAYER_CONFIG failed\n");
        return -1;
    }

#ifdef AIC_PAN_DISPLAY
    disp_buf_addr = (u32)layer.buf.phy_addr[0];
    fb0_buf_addr  = (u32)(unsigned long)dvr_screen_info.framebuffer;
    if (disp_buf_addr == fb0_buf_addr) {
        dst_buf_addr    = fb0_buf_addr + dvr_screen_info.smem_len;
        dvr_fb_buf_index = 1;
    } else {
        dst_buf_addr    = fb0_buf_addr;
        dvr_fb_buf_index = 0;
    }
#else
    dst_buf_addr = (u32)layer.buf.phy_addr[0];
#endif

    memset(&blt, 0, sizeof(blt));
    memcpy(&blt.src_buf, &frame->buf, sizeof(struct mpp_buf));

    blt.dst_buf.buf_type    = MPP_PHY_ADDR;
    blt.dst_buf.phy_addr[0] = dst_buf_addr;
    blt.dst_buf.format      = dvr_screen_info.format;
    blt.dst_buf.stride[0]   = dvr_screen_info.stride;
    blt.dst_buf.size.width  = dvr_screen_info.width;
    blt.dst_buf.size.height = dvr_screen_info.height;
    blt.dst_buf.crop_en     = 1;
    blt.dst_buf.crop.x      = (int)offset_x;
    blt.dst_buf.crop.y      = (int)offset_y;

#if DVR_DEC_SCALE_SHIFT
    /* decoded frame is smaller than the window: let GE scale it up */
    blt.dst_buf.crop.width  = width;
    blt.dst_buf.crop.height = height;
#else
    if (blt.src_buf.crop.width && blt.src_buf.crop.height) {
        blt.dst_buf.crop.width  = blt.src_buf.crop.width;
        blt.dst_buf.crop.height = blt.src_buf.crop.height;
    } else {
        blt.dst_buf.crop.width  = blt.src_buf.size.width;
        blt.dst_buf.crop.height = blt.src_buf.size.height;
    }
#endif

    if (dvr_ge_bitblt(&blt) < 0)
        return -1;

#ifdef AIC_PAN_DISPLAY
    mpp_fb_ioctl(fb, AICFB_PAN_DISPLAY, &dvr_fb_buf_index);
    mpp_fb_ioctl(fb, AICFB_WAIT_FOR_VSYNC, 0);
    {
        struct ge_bitblt sync_blt = {0};
        u32 sync_src, sync_dst;

        if (dvr_fb_buf_index) {
            sync_src = (u32)(unsigned long)dvr_screen_info.framebuffer
                       + dvr_screen_info.smem_len;
            sync_dst = (u32)(unsigned long)dvr_screen_info.framebuffer;
        } else {
            sync_src = (u32)(unsigned long)dvr_screen_info.framebuffer;
            sync_dst = (u32)(unsigned long)dvr_screen_info.framebuffer
                       + dvr_screen_info.smem_len;
        }

        sync_blt.src_buf.buf_type    = MPP_PHY_ADDR;
        sync_blt.src_buf.phy_addr[0] = sync_src;
        sync_blt.src_buf.format      = dvr_screen_info.format;
        sync_blt.src_buf.stride[0]   = dvr_screen_info.stride;
        sync_blt.src_buf.size.width  = dvr_screen_info.width;
        sync_blt.src_buf.size.height = dvr_screen_info.height;
        sync_blt.dst_buf.buf_type    = MPP_PHY_ADDR;
        sync_blt.dst_buf.phy_addr[0] = sync_dst;
        sync_blt.dst_buf.format      = dvr_screen_info.format;
        sync_blt.dst_buf.stride[0]   = dvr_screen_info.stride;
        sync_blt.dst_buf.size.width  = dvr_screen_info.width;
        sync_blt.dst_buf.size.height = dvr_screen_info.height;
        if (dvr_ge_bitblt(&sync_blt) < 0)
            printf("DVR: ui_layer_buf_sync bitblt failed\n");
    }
#endif

    return 0;
}

static int dvr_decoder_init(void)
{
    struct decode_config config;

    if (dvr_mpp_dec) {
        /* Already initialised; idempotent. Mirrors ec_link.c:753 (mPlay guard). */
        return 0;
    }

    dvr_mpp_fb = mpp_fb_open();
    if (!dvr_mpp_fb) {
        printf("DVR: mpp_fb_open failed\n");
        return -1;
    }

    dvr_mpp_dec = mpp_decoder_create(MPP_CODEC_VIDEO_DECODER_MJPEG);
    if (!dvr_mpp_dec) {
        printf("DVR: mpp_decoder_create failed\n");
        mpp_fb_close(dvr_mpp_fb);
        dvr_mpp_fb = NULL;
        return -1;
    }

    /* CarLink's pix_fmt policy (ec_link.c:764,776-779): ABGR_8888 by default,
     * NV12 when AIC_VE_DRV_V10 is defined for MJPEG. GE does the final
     * down-convert to the panel pixel format in one blit. */
    dvr_dec_pix_fmt = MPP_FMT_ABGR_8888;
#ifdef AIC_VE_DRV_V10
    dvr_dec_pix_fmt = MPP_FMT_NV12;
#endif

    memset(&config, 0, sizeof(config));
    config.bitstream_buffer_size = DVR_BITSTREAM_BUF_SIZE;
    config.extra_frame_num       = 0;
    config.packet_count          = 1;
    config.pix_fmt               = dvr_dec_pix_fmt;

#if DVR_DEC_SCALE_SHIFT
    {
        struct mpp_scale_ratio ratio;
        ratio.hor_scale = DVR_DEC_SCALE_SHIFT;
        ratio.ver_scale = DVR_DEC_SCALE_SHIFT;
        mpp_decoder_control(dvr_mpp_dec, MPP_DEC_INIT_CMD_SET_SCALE, &ratio);
        printf("DVR: decoder hw downscale shift=%d\n", DVR_DEC_SCALE_SHIFT);
    }
#endif

    if (mpp_decoder_init(dvr_mpp_dec, &config) < 0) {
        printf("DVR: mpp_decoder_init failed\n");
        mpp_decoder_destory(dvr_mpp_dec);
        mpp_fb_close(dvr_mpp_fb);
        dvr_mpp_dec = NULL;
        dvr_mpp_fb  = NULL;
        return -1;
    }

    printf("DVR: decoder init ok (pix_fmt=0x%x, bs_buf=%d)\n",
           dvr_dec_pix_fmt, config.bitstream_buffer_size);
    return 0;
}

static void dvr_decoder_deinit(void)
{
    if (dvr_mpp_dec) {
        mpp_decoder_destory(dvr_mpp_dec);
        dvr_mpp_dec = NULL;
    }
    if (dvr_mpp_fb) {
        mpp_fb_close(dvr_mpp_fb);
        dvr_mpp_fb = NULL;
    }
    /* Invalidate the lazy screeninfo cache so the next session re-probes
     * (in case the panel config changed). */
    memset(&dvr_screen_info, 0, sizeof(dvr_screen_info));
}

static void dvr_filelist_parser(uint8_t *buf, int32_t len, uint16_t cmd_par);
static void dvr_close_elene(void);
static void dvr_pb_start(uint8_t mode, uint16_t index);
static void dvr_pb_pause(void);
static void dvr_pb_stop(void);
static void dvr_pb_ff(void);
static void dvr_pb_fb(void);
static void dvr_tf_capacity_parser(uint8_t *buf, int32_t len);

// Send data to DVR via elene file (write 512 bytes aligned)
// Caller should call dvr_seek_for_align() before this function
static void dvr_send_data(void)
{
    if (dvr_fp == NULL) {
        printf("DVR: elene file not opened for write\n");
        return;
    }

    /* DFS write: returns number of bytes actually written, or <0 on
     * error. Must be exactly 512 (matches elene's 512-byte frame size). */
    int bw = dfs_file_write(dvr_fp, &dvr_ctrl_data, 512);
    if (bw != 512) {
        printf("DVR: dfs_file_write ctrl frame failed: bw=%d\n", bw);
        return;
    }

    printf("DVR: Data sent successfully\n");
}

// Send normal command to DVR
// Sets flag and prepares data, actual sending happens in main loop
void dvr_send_normal_cmd(unsigned short cmd_id, unsigned short cmd_par)
{
    st_bd_ctrl_if_t *pctrl = &dvr_ctrl_data;

    pctrl->header_id = 0xaa55;
    pctrl->cmd_id = cmd_id;
    pctrl->cmd_par = cmd_par;
    pctrl->data_len = 0;
    pctrl->checksum = 0;
    dvr_need_send_data = 1;

    printf("DVR-TX>> cmd=0x%02X par=0x%04X frame[AA 55 %02X %02X %02X %02X 00 00 00 00]\n",
           cmd_id, cmd_par,
           cmd_id & 0xFF, (cmd_id >> 8) & 0xFF,
           cmd_par & 0xFF, (cmd_par >> 8) & 0xFF);
}

// Check if elene file exists on USB using DFS stat (the same DFS path
// resolution that "ls udisk" relies on). Using the DFS API instead of
// the bare FATFS f_stat() avoids drive-number assumptions and matches
// the resolution chain that successfully listed elene in the user's
// session log.
static int dvr_check_elene_exist(void)
{
    struct stat sb;
    int rc = dfs_file_stat(JPG_FILE_NAME, &sb);
    if (rc == 0) {
        return 1;
    }
    /* Surface dfs_file_stat error so the operator can distinguish
     * mount-table-not-yet-up vs. file-really-missing. */
    printf("DVR: dfs_file_stat('%s') failed: rc=%d\n", JPG_FILE_NAME, rc);
    return 0;
}

// Initialize DVR capture - open elene file, allocate I/O buffers, init
// the MPP MJPEG decoder + fb handle (file-private). See dvr_decoder_init()
// for the decoder setup; see dvr_render_to_ui_layer() for the render path.
//
// Replaces the old mfc_init / rt_malloc(yuv_buf) / rt_malloc(dst_buf[2])
// triplet with a single mpp_decoder_create call. No double buffer is needed:
// the GE blits the decoded frame directly into the currently-displayed UI
// layer framebuffer.
static int dvr_capture_init(st_dvr_capture_t *cap)
{
    int blksize;
    cap->h_cap = NULL;

    if (dvr_fp != NULL) {
        printf("DVR: elene already opened\n");
        cap->h_cap = dvr_fp;
    } else {
        /* DFS-layer open (matches the same path resolution "ls udisk"
         * uses). Heap-allocate the dfs_fd struct and keep the pointer
         * in dvr_fp for the global one-fd-at-a-time access pattern.
         * O_RDWR maps to FATFS' FA_READ|FA_WRITE; we omit O_CREAT so
         * missing file still surfaces as -ENOENT instead of being
         * silently created (FA_OPEN_EXISTING semantics). */
        dvr_fp = (struct dfs_fd *)rt_malloc(sizeof(struct dfs_fd));
        if (!dvr_fp) {
            printf("DVR: alloc dfs_fd failed\n");
            return -1;
        }
        int rc = dfs_file_open(dvr_fp, JPG_FILE_NAME, O_RDWR);
        if (rc != 0) {
            printf("DVR: dfs_file_open '%s' failed: rc=%d\n", JPG_FILE_NAME, rc);
            rt_free(dvr_fp);
            dvr_fp = NULL;
            return -1;
        }
        cap->h_cap = dvr_fp;
        printf("DVR: elene opened, dvr_fp=0x%lx (size=%lu)\n",
               (unsigned long)(uintptr_t)dvr_fp,
               (unsigned long)dvr_fp->size);
    }

    blksize = 100 * 1024;  // Block size should be 100KB to match elene file format
    cap->blksize = blksize;

    /* cap_blk_buf is ~410KB. The default rt_malloc heap comes from SRAM
     * (__heap_start..__heap_end) and is only a few hundred KB on D13x, so
     * route this large allocation to the 8MB PSRAM software heap (configured
     * via CONFIG_AIC_PSRAM_SW_SIZE=0x800000 in the active defconfig). The
     * matching free in dvr_capture_deinit() uses aicos_free(MEM_PSRAM_CMA, ...). */
    cap->cap_blk_buf = (uint8_t *)aicos_malloc(MEM_PSRAM_CMA,
                                                blksize + 512 + 300 * 1024);  // 100KB block + extra space for jpg_size
    if (!cap->cap_blk_buf) {
        printf("DVR: alloc blk_buf failed\n");
        aicos_free(MEM_PSRAM_CMA, cap->cap_blk_buf);   /* no-op: pointer is NULL here, kept for symmetry with deinit */
        if (dvr_fp) {
            dfs_file_close(dvr_fp);
            rt_free(dvr_fp);
        }
        cap->h_cap = NULL;
        dvr_fp = NULL;
        return -1;
    }

    /* Same MEM_PSRAM_CMA route as cap_blk_buf — 350KB also overflows the
     * default SRAM heap. See comment at cap_blk_buf allocation above. */
    cap->cap_jpg_buf = (uint8_t *)aicos_malloc(MEM_PSRAM_CMA, 350 * 1024);  // 350KB to handle max jpg_size + header
    if (!cap->cap_jpg_buf) {
        printf("DVR: alloc jpg_buf failed\n");
        aicos_free(MEM_PSRAM_CMA, cap->cap_blk_buf);   /* was rt_malloc'd to PSRAM, must free to PSRAM too */
        if (dvr_fp) {
            dfs_file_close(dvr_fp);
            rt_free(dvr_fp);
        }
        cap->h_cap = NULL;
        dvr_fp = NULL;
        return -1;
    }

    // Initialize align seek variables
    cap->round_flag = 0;
    cap->read_times = 0;
    cap->file_size = (uint64_t)dvr_fp->size;  /* total size exposed by DFS layer */
    printf("DVR: file size = %lu\n", (unsigned long)cap->file_size);

    /* Decode + render init: opens mpp_fb + mpp_decoder (VE). */
    if (dvr_decoder_init() != 0) {
        printf("DVR: decoder init failed\n");
        aicos_free(MEM_PSRAM_CMA, cap->cap_blk_buf);
        aicos_free(MEM_PSRAM_CMA, cap->cap_jpg_buf);
        if (dvr_fp) {
            dfs_file_close(dvr_fp);
            rt_free(dvr_fp);
        }
        cap->h_cap = NULL;
        dvr_fp = NULL;
        return -1;
    }

    cap->display_on = 0;
    cap->running = 1;
    printf("DVR: capture init ok, blksize=%d\n", blksize);
    return 0;
}

// Close DVR capture and free resources.
//
// Note: dvr_fp is heap-allocated in dvr_capture_init and must be freed
// here to prevent leaks across multiple USB insertion / removal cycles.
// Next dvr_capture_init() will see dvr_fp == NULL and re-malloc + re-open.
static void dvr_capture_deinit(st_dvr_capture_t *cap)
{
    cap->running = 0;
    cap->h_cap = RT_NULL;
    /* These two large buffers were allocated from MEM_PSRAM_CMA in
     * dvr_capture_init(); freeing them on the default heap would be a
     * TLSF cross-region fault. NULL guards for safety (aicos_free on
     * a NULL pointer would otherwise depend on the underlying impl). */
    if (cap->cap_blk_buf) {
        aicos_free(MEM_PSRAM_CMA, cap->cap_blk_buf);
        cap->cap_blk_buf = RT_NULL;
    }
    if (cap->cap_jpg_buf) {
        aicos_free(MEM_PSRAM_CMA, cap->cap_jpg_buf);
        cap->cap_jpg_buf = RT_NULL;
    }

    dvr_close_elene();  /* dfs_file_close + rt_free dvr_fp if open */

    dvr_decoder_deinit();

    cap->display_on = 0;

    /* Restore LVGL home_page (the DVR pipeline stopped writing to UI layer). */
    if (dvr_ui_hidden) {
        dvr_ui_hidden = 0;
        vehicle_set_data(VEH_DVR_PLAYING, 0);
    }
}

// Close global elene file
static void dvr_close_elene(void)
{
    if (dvr_fp) {
        dfs_file_close(dvr_fp);
        rt_free(dvr_fp);
        dvr_fp = NULL;
        printf("DVR: elene closed\n");
    }
}

// Process received command from DVR
static void dvr_recv_cmd_process(st_bd_ctrl_if_t *pctrl)
{
    switch (pctrl->cmd_id) {
        case BD_CTRL_GET_ID:
            /* Cache the NUL-terminated version string from trans_buf. The
             * string is bounded by data_len (when set) and/or the first NUL
             * we encounter; we always leave one byte for trailing NUL. */
            {
                int32_t copy_len = pctrl->data_len;
                if (copy_len < 0) copy_len = 0;
                if (copy_len > (int32_t)(DVR_VERSION_MAX_LEN - 1)) {
                    copy_len = DVR_VERSION_MAX_LEN - 1;
                }
                /* If DVR sent a long string with no NUL in the first data_len
                 * bytes, force termination by writing a NUL. */
                memcpy(dvr_version_buf, pctrl->trans_buf, (size_t)copy_len);
                dvr_version_buf[copy_len] = '\0';
                dvr_version_valid = 1;
                printf("DVR version: %s\n", dvr_version_buf);
            }
            break;
        case BD_CTRL_GET_STS:
            dvr_sd_status = (pctrl->cmd_par & 0x01) ? 1 : 0;
            dvr_rec_status = (pctrl->cmd_par & 0x02) ? 1 : 0;
            dvr_lock_status = (pctrl->cmd_par & 0x04) ? 1 : 0;
            dvr_sd_error = (pctrl->cmd_par & 0x08) ? 1 : 0;
            dvr_sd_full = (pctrl->cmd_par & 0x10) ? 1 : 0;
            dvr_mic_status = (pctrl->cmd_par & 0x20) ? 1 : 0;
            printf("DVR STATUS: SD=%d Rec=%d Lock=%d Error=%d Full=%d MIC=%d\n",
                   dvr_sd_status, dvr_rec_status, dvr_lock_status,
                   dvr_sd_error, dvr_sd_full, dvr_mic_status);
            /* Auto-record: once SD card is ready and not already recording,
             * start loop recording automatically. Only fires once per
             * USB insertion (dvr_auto_rec_pending is set by task init). */
            if (dvr_auto_rec_pending && dvr_sd_status && !dvr_rec_status) {
                dvr_auto_rec_pending = 0;
                dvr_send_normal_cmd(BD_CTRL_REC_START, 0);
                printf("DVR: auto-rec started (SD ready)\n");
            } else if (dvr_auto_rec_pending && !dvr_sd_status) {
                /* SD not ready yet — re-query after a short delay.
                 * The next FPS tick (~1s) or manual getsts will retry.
                 * Schedule a re-query via the pending-command mechanism. */
                printf("DVR: SD not ready, will retry auto-rec\n");
            } else if (dvr_auto_rec_pending && dvr_rec_status) {
                /* Already recording (e.g. DVR chip auto-started) */
                dvr_auto_rec_pending = 0;
                printf("DVR: already recording, auto-rec skipped\n");
            }
            /* If version is still unknown, piggyback a GET_ID request.
             * GET_STS just arrived so the USB channel is responsive right
             * now — best moment to slip in a GET_ID and catch the reply
             * on the next 2ms read cycle. Only retry while preview is on
             * (USB reads are frequent); stops once version is cached. */
            if (!dvr_version_valid && dvr_preview_enable) {
                dvr_send_normal_cmd(BD_CTRL_GET_ID, 0);
                printf("DVR: version retry (piggyback on GET_STS reply)\n");
            }
            break;
        case BD_CTRL_SENSOR_SEL:
            printf("DVR RECV SENSOR_SEL: cmd_par=0x%02x\n", pctrl->cmd_par);
            break;
        case BD_ANDROID_VIEW_SWITCH:
            printf("DVR RECV VIEW_SWITCH: cmd_par=0x%02x\n", pctrl->cmd_par);
            dvr_view_mode = pctrl->cmd_par & 0x0F;
            break;
        case BD_CTRL_GET_LIST:
            printf("DVR: GET_LIST received, data_len=%d\n", pctrl->data_len);
            dvr_filelist_parser(pctrl->trans_buf, pctrl->data_len, pctrl->cmd_par);
            // Resume JPEG processing after file list response
            dvr_skip_jpeg_process = 0;
            break;
        case BD_CTRL_SET_RES:
            printf("DVR: SET_RES received, res=%d\n", pctrl->cmd_par);
            break;
        case BD_CTRL_SET_REC_TIME:
            printf("DVR: SET_REC_TIME received, time=%d min\n", pctrl->cmd_par);
            break;
        case BD_CTRL_MIC_ON:
            printf("DVR: MIC_ON received, onoff=%d\n", pctrl->cmd_par);
            dvr_mic_status = pctrl->cmd_par;
            break;
        case BD_CTRL_STAMP_ON:
            printf("DVR: STAMP_ON received, onoff=%d\n", pctrl->cmd_par);
            break;
        case BD_CTRL_FORMAT:
            printf("DVR: FORMAT received, status=%d\n", pctrl->cmd_par);
            break;
        case BD_CTRL_DEFAULT:
            printf("DVR: DEFAULT received, status=%d\n", pctrl->cmd_par);
            break;
        case BD_CTRL_SET_TIME:
            printf("DVR: SET_TIME received, status=%d\n", pctrl->cmd_par);
            break;
        case BD_CTRL_DEL_FILE:
            dvr_del_ack_received = 1;
            printf("DVR: DEL_FILE ACK received, par=0x%04X, status=%d\n", pctrl->cmd_par, pctrl->data_len);
            break;
        case BD_CTRL_LOCK_FILE:
            printf("DVR: LOCK_FILE received, index=%d, status=%d\n", pctrl->cmd_par, pctrl->data_len);
            break;
        case BD_CTRL_UNLOCK_FILE:
            printf("DVR: UNLOCK_FILE received, index=%d, status=%d\n", pctrl->cmd_par, pctrl->data_len);
            break;
        case BD_CTRL_TOTAL_TIME:
            dvr_total_time_index = pctrl->cmd_par;
            dvr_total_time_sec   = (uint32_t)pctrl->data_len;
            dvr_total_time_valid = 1;
            printf("DVR: TOTAL_TIME received, index=%d, time=%d sec\n", pctrl->cmd_par, pctrl->data_len);
            break;
        case BD_CTRL_GET_TF_CAPACITY:
            printf("DVR: GET_TF_CAPACITY received, data_len=%d\n", pctrl->data_len);
            dvr_tf_capacity_parser(pctrl->trans_buf, pctrl->data_len);
            break;
        case BD_CTRL_PB_FF:
            printf("DVR: PB_FF received\n");
            break;
        case BD_CTRL_PB_FB:
            printf("DVR: PB_FB received\n");
            break;
        default:
            printf("DVR: unknown cmd 0x%02x\n", pctrl->cmd_id);
            break;
    }
}

// Decode a JPEG (already in cap->cap_jpg_buf, of size cap->current_jpg_size)
// and write the resulting frame into the UI layer at the configured display
// window. Modeled on CarLink's videoPlay() at
// third/carlink/src/ec_link.c:799.
//
// Pipeline:
//   1. mpp_decoder_get_packet -> fill with JPEG bytes -> put_packet
//   2. mpp_decoder_decode (skip on negative, mirrors ec_link.c:874)
//   3. mpp_decoder_get_frame  -> decoded mpp_frame (YUV/NV12, decoder-internal)
//   4. dvr_render_to_ui_layer -> GE blits the decoded frame straight into
//      the UI layer framebuffer at (dvr_display_x, dvr_display_y) with
//      scaling to (dvr_display_width, dvr_display_height). No PXP, no
//      dedicated VIDEO OSD layer.
//   5. mpp_decoder_put_frame
//
// Returns 0 on success, -1 on any decode or render error (caller skips frame).
static int dvr_decode_and_display(st_dvr_capture_t *cap, uint32_t jpg_size)
{
    struct mpp_packet packet;
    struct mpp_frame  frame;
    int ret;

    if (!dvr_mpp_dec || !dvr_mpp_fb) {
        return -1;
    }
    if (jpg_size == 0 || jpg_size > DVR_BITSTREAM_BUF_SIZE) {
        return -1;
    }

#if DVR_DEBUG_PRINT
    printf("DVR: decode jpg_size=%d (cap_jpg_buf=0x%x)\n",
           jpg_size, (uint32_t)cap->cap_jpg_buf);
#endif

    /* 1. Get an empty packet sized for this JPEG and copy bytes in. */
    memset(&packet, 0, sizeof(packet));
    if (mpp_decoder_get_packet(dvr_mpp_dec, &packet, (int)jpg_size) < 0) {
        printf("DVR: mpp_decoder_get_packet failed\n");
        return -1;
    }
    memcpy(packet.data, cap->cap_jpg_buf, jpg_size);
    packet.size = (int)jpg_size;
    packet.flag = PACKET_FLAG_EOS;

    /* 2. Submit packet to the decoder. */
    if (mpp_decoder_put_packet(dvr_mpp_dec, &packet) < 0) {
        printf("DVR: mpp_decoder_put_packet failed\n");
        return -1;
    }

    /* 3. Decode. Negative returns include DEC_NO_READY_PACKET (3),
     *    DEC_NO_EMPTY_FRAME (2), DEC_NO_RENDER_FRAME (1) — transient
     *    "wait" signals. Mirrors CarLink's videoPlay (ec_link.c:874):
     *    if (ret < 0) return; */
    ret = mpp_decoder_decode(dvr_mpp_dec);
    if (ret < 0) {
        printf("DVR: mpp_decoder_decode failed: %d\n", ret);
        return -1;
    }

    /* 4. Pull the decoded frame. */
    memset(&frame, 0, sizeof(frame));
    if (mpp_decoder_get_frame(dvr_mpp_dec, &frame) < 0) {
        printf("DVR: mpp_decoder_get_frame failed\n");
        return -1;
    }

    /* 5. Render directly into the UI layer at the configured window.
     *    GE handles YUV->panel-format conversion AND scaling in one blit. */
    if (dvr_render_to_ui_layer(dvr_mpp_fb, &frame,
                               (u32)dvr_display_x, (u32)dvr_display_y,
                               (u32)dvr_display_width,
                               (u32)dvr_display_height) < 0) {
        /* Return the frame to the decoder even if render failed. */
        mpp_decoder_put_frame(dvr_mpp_dec, &frame);
        printf("DVR: dvr_render_to_ui_layer failed\n");
        return -1;
    }

    /* 6. Return frame to the decoder's free pool. */
    mpp_decoder_put_frame(dvr_mpp_dec, &frame);

    cap->display_on = 1;

    /* First successful frame — DVR is now visibly rendering onto UI layer.
     * Edge-detected (display_on 0→1) to avoid firing every frame. UI is
     * restored in dvr_capture_deinit() and dvr_stop_preview(). */
    if (!dvr_ui_hidden) {
        dvr_ui_hidden = 1;
        vehicle_set_data(VEH_DVR_PLAYING, 1);
    }

    return 0;
}

// Seek with alignment for continuous reading (replaces simple seek to 0)
// Uses the DFS layer (dfs_file_lseek) for the same reason file IO was
// migrated: matches the DFS path resolution. Note: dfs_file_lseek takes
// an absolute byte offset, identical semantics to f_lseek() here.
//
// `consume_increment` gates the read_times++ at function exit. Capture
// path passes 1 (each capture iteration advances by one 300KB-aligned
// frame slot). send_data path passes 0 — it only needs the seek; the
// capture path that follows will do the increment. Without this gate,
// send_data consumes two slots per call, the capture path skips an extra
// 300KB chunk, JPEG boundaries misalign, and the VE decoder hits
// `status error:2, errmb:0` (SOS segment from a different chunk stitched
// into the next JPEG).
static void dvr_seek_for_align(st_dvr_capture_t *cap, int consume_increment)
{
    uint32_t offset;
    uint64_t limit;

    if (cap->round_flag == 0) {
        limit = cap->file_size;
    } else {
        limit = cap->file_size / 3;
    }
    offset = cap->read_times * 300 * 1024;

    if (offset + 300 * 1024 >= limit) {
        cap->round_flag = 1;
        cap->read_times = 0;
        offset = 100 * 300 * 1024;
        /* dfs_file_lseek returns the new position on success (>= 0),
         * or a negative errno on failure. NOT 0-on-success — so check
         * < 0, mirroring dfs_posix.c:777/802. Using != 0 misreads any
         * non-zero success as failure and freezes read_times. */
        if (dfs_file_lseek(cap->h_cap, (off_t)offset) < 0) {
            printf("DVR: dvr_seek_for_align dfs_file_lseek(%lu) failed\n", (unsigned long)offset);
            return;
        }
    } else {
        if (dfs_file_lseek(cap->h_cap, (off_t)offset) < 0) {
            printf("DVR: dvr_seek_for_align dfs_file_lseek(%lu) failed\n", (unsigned long)offset);
            return;
        }
        if (consume_increment) {
            cap->read_times++;
        }
    }
}

// Main DVR capture process - read from elene file and distinguish protocol vs image
// Returns: 0=ok (image/jpeg data ready), -1=fail or command processed
static int dvr_capture_get_pic_process(st_dvr_capture_t *cap)
{
    /* Early exit: skip USB read entirely when not in preview mode.
     * Previously the check was AFTER dfs_file_read (100KB USB read every 2ms),
     * wasting USB bandwidth and CPU even when DVR preview is off.
     * Command frames (0xAA55) are still needed, so we only skip when
     * not in preview AND no pending command send. */
    if (!dvr_preview_enable && !dvr_need_send_data && !dvr_skip_jpeg_process) {
        return -1;
    }

    struct dfs_fd *fp = cap->h_cap;
    size_t nb;
    st_bd_ctrl_if_t *pctrl;
    size_t jpg_size;
#if JPEG_DEBUG_PRINT
    uint32_t read_check_sum;
    uint32_t cnt, check_sum, i;
#endif

    if (!fp || !cap->cap_blk_buf) {
        return -1;
    }

    /* Use aligned seek for continuous reading */
    dvr_seek_for_align(cap, 1);

    /* Read block data (100KB). dfs_file_read returns bytes read; we keep
     * the existing "exactly blksize bytes or fail" semantics. */
    int br = dfs_file_read(fp, cap->cap_blk_buf, cap->blksize);
    nb = (br < 0) ? 0 : (size_t)br;
    if (br < 0 || nb != cap->blksize) {
        printf("DVR: read first block incomplete, expected %lu, got %d (rc=%d)\n",
               (unsigned long)cap->blksize, (int)nb, br);
        return -1;
    }

    pctrl = (st_bd_ctrl_if_t *)cap->cap_blk_buf;

    /* Check if it's a command (header 0xAA55) or image data */
    if (cap->cap_blk_buf[0] == 0x55 && cap->cap_blk_buf[1] == 0xAA) {
        /* It's a command frame */
        printf("DVR-RX<< cmd=0x%02X par=0x%04X data_len=%d frame[%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X]\n",
               pctrl->cmd_id, pctrl->cmd_par, pctrl->data_len,
               cap->cap_blk_buf[0], cap->cap_blk_buf[1],
               cap->cap_blk_buf[2], cap->cap_blk_buf[3],
               cap->cap_blk_buf[4], cap->cap_blk_buf[5],
               cap->cap_blk_buf[6], cap->cap_blk_buf[7],
               cap->cap_blk_buf[8], cap->cap_blk_buf[9]);
        dvr_recv_cmd_process(pctrl);
        return -1;
    }

    /* Skip JPEG processing when waiting for file list response */
    if (dvr_skip_jpeg_process) {
        dfs_file_lseek(fp, 0);
        return -1;
    }

    /* It's image data - extract jpg_size from first 4 bytes */
    jpg_size = *((uint32_t *)cap->cap_blk_buf);
#if JPEG_DEBUG_PRINT
    read_check_sum = *((uint32_t *)(cap->cap_blk_buf + 8));
#endif

#if DVR_DEBUG_PRINT
    printf("DVR: jpg size=0x%x\n", jpg_size);
#endif

    /* Validate jpg_size - must be at least 1024 and at most 300KB */
    if (jpg_size < 1024 || jpg_size > 300 * 1024) {
        #if JPEG_DEBUG_PRINT
        printf("DVR: jpeg size invalid: 0x%x (min=1024, max=300KB)\n", jpg_size);
        #endif
        /* Advance file position by 100KiB to keep frame alignment with the
         * 300KiB-aligned seek loop. Current position is exposed by dfs_fd.pos;
         * SEEK_CUR semantics here ("current + 100KiB") match the original
         * f_lseek(fp, f_tell(fp) + 100KiB) used by LVGL fsdrv. */
        dfs_file_lseek(fp, fp->pos + (off_t)(100 * 1024));
        return -1;
    }

    /* Copy header part to jpeg buffer (skip BD_JPG_OFFSET = 17) */
    nb = cap->blksize - BD_JPG_OFFSET;
    if (nb > 300 * 1024) {
        #if JPEG_DEBUG_PRINT
        printf("DVR: block size too large: %d\n", nb);
        #endif
        return -1;
    }
    memcpy(cap->cap_jpg_buf, cap->cap_blk_buf + BD_JPG_OFFSET, nb);

    /* Check JPEG header (FF D8) */
    if (cap->cap_jpg_buf[0] != 0xFF || cap->cap_jpg_buf[1] != 0xD8) {
        #if JPEG_DEBUG_PRINT
        printf("DVR: invalid JPEG header\n");
        #endif
        dfs_file_lseek(fp, fp->pos + (off_t)(100 * 1024));
        return -1;
    }

    /* Read remaining JPEG data - read into cap_blk_buf + 512 */
    int br2 = dfs_file_read(fp, cap->cap_blk_buf + 512, (uint32_t)jpg_size);
    if (br2 < 0 || (size_t)br2 != jpg_size) {
        #if JPEG_DEBUG_PRINT
        printf("DVR: read remaining JPEG failed, expected %d, got %d (rc=%d)\n",
               jpg_size, (br2 < 0) ? -1 : br2, br2);
        #endif
        return -1;
    }

    /* Copy remaining data after first block (from offset 512 in cap_blk_buf) */
    memcpy(cap->cap_jpg_buf + nb, cap->cap_blk_buf + 512, jpg_size);

#if JPEG_DEBUG_PRINT
    /* Calculate checksum from cap_blk_buf + 512 + i*512 (like usb_detect.c) */
    cnt = (jpg_size - 511) >> 9;
    check_sum = 0;
    for (i = 1; i < cnt; i++) {
        uint32_t val = *((uint32_t *)(cap->cap_blk_buf + 512 + i * 512));
        check_sum += val;
    }

    if ((check_sum & 0xFF) != (read_check_sum & 0xFF)) {
        printf("DVR: checksum warn, size:%d, calc:0x%08x, expected:0x%08x\n",
               (int)jpg_size, (unsigned int)check_sum, (unsigned int)read_check_sum);
        /* Continue processing - checksum mismatch doesn't mean decode will fail */
    }
#endif

    /* Find actual JPEG end by searching backwards for FF D9 */
    uint32_t actual_size = nb + jpg_size;
    int32_t search_start = (int32_t)actual_size - 200;  // Search last 200 bytes
    if (search_start < (int32_t)nb) search_start = nb;
    uint32_t found_pos = 0;

    // Search backwards from (actual_size - 2) to search_start
    for (int32_t i = (int32_t)actual_size - 2; i >= search_start; i--) {
        if (cap->cap_jpg_buf[i] == 0xFF && cap->cap_jpg_buf[i+1] == 0xD9) {
            found_pos = (uint32_t)i;
            break;
        }
    }

    #if JPEG_DEBUG_PRINT
    // Debug: show search range
    printf("DVR: footer search: range[%d to %d], found=%d\n",
           search_start, actual_size - 2, found_pos);
    #endif

    if (found_pos == 0) {
        #if JPEG_DEBUG_PRINT
        printf("DVR: JPEG footer not found, trying last 200 bytes scan...\n");
        #endif
        // Final fallback: search entire buffer for FF D9
        for (uint32_t i = actual_size - 2; i >= nb + 100; i--) {
            if (cap->cap_jpg_buf[i] == 0xFF && cap->cap_jpg_buf[i+1] == 0xD9) {
                found_pos = i;
                #if JPEG_DEBUG_PRINT
                printf("DVR: FF D9 found at pos=%d (deep search)\n", found_pos);
                #endif
                break;
            }
        }
        if (found_pos == 0) {
            #if JPEG_DEBUG_PRINT
            printf("DVR: JPEG footer FF D9 not found in buffer, using size=%d\n", actual_size);
            #endif
        }
    } else {
        actual_size = found_pos + 2;  // Include FF D9
        #if JPEG_DEBUG_PRINT
        printf("DVR: JPEG footer found at pos=%d, actual_size=%d\n", found_pos, actual_size);
        #endif
    }

    cap->current_jpg_size = actual_size;

    /* JPEG data is ready in cap_jpg_buf */
    return 0;
}

// DVR task - continuously reads from elene file
static void dvr_usb_task(void *arg)
{
    st_dvr_capture_t *cap = (st_dvr_capture_t *)arg;

    printf("DVR: task started\n");

    if (dvr_capture_init(cap) != 0) {
        printf("DVR: init failed, task exit\n");
        dvr_task_handle = RT_NULL;
        rt_thread_delete(rt_thread_self());
        return;
    }


    // Send MIC on command once on startup
    // dvr_send_normal_cmd(BD_CTRL_MIC_ON, 0);

    // Note: dvr_get_status() and dvr_get_file_list() are called on demand, not in the main loop

    /* Auto-record: query status after task init; when SD=1 is first
     * seen in dvr_recv_cmd_process(), rec_start is sent automatically.
     * Flag is reset on task exit so the next USB insertion retriggers. */
    dvr_auto_rec_pending = 1;
    dvr_send_normal_cmd(BD_CTRL_GET_STS, 0);
    printf("DVR: init done, querying status for auto-rec\n");

    uint32_t last_switch_time = 0;
    uint32_t current_time;
#if DVR_FRAMERATE_PRINT
    dvr_last_stat_time = rt_tick_get();
    dvr_frame_count = 0;
    dvr_frame_count_last = 0;
    dvr_fps = 0;
#endif
    printf("DVR: sensor switch test started, enable=%d\n", dvr_sensor_switch_enable);
    while (cap->running) {
        // Check if need to send command data
        if (dvr_need_send_data) {
            /* Seek to the 300KB-aligned boundary without consuming a
             * frame slot — the capture path's dvr_seek_for_align(cap, 1)
             * immediately after this block does the increment. This
             * stops send_data from doubling the per-iteration step
             * (which used to land captures on the wrong chunk and
             * corrupt JPEG stream → VE SOS error). */
            dvr_seek_for_align(cap, 0);
            dvr_send_data();
            dvr_need_send_data = 0;
        }

        if (dvr_capture_get_pic_process(cap) == 0) {
            // Decode and display the JPEG
            dvr_decode_and_display(cap, cap->current_jpg_size);
#if DVR_FRAMERATE_PRINT
            dvr_frame_count++;
#endif
        }

#if DVR_FRAMERATE_PRINT
        // Calculate framerate every 1 second
        current_time = rt_tick_get();
        if (current_time - dvr_last_stat_time >= 1000) {
            dvr_fps = dvr_frame_count - dvr_frame_count_last;
            dvr_frame_count_last = dvr_frame_count;
            dvr_last_stat_time = current_time;
            /* FPS print is gated by the runtime flag dvr_fps_print_enable
             * (CLI-controllable). Stats are always computed; only the
             * printf is suppressed when disabled. */
            if (dvr_fps_print_enable) {
                printf("DVR: [FPS:%lu] frame_count=%lu\n",
                       (unsigned long)dvr_fps,
                       (unsigned long)dvr_frame_count);
            }

            /* Auto-rec retry: if SD was not ready at task init, re-query
             * status every FPS tick (~1s) until SD comes up or 30s passes. */
            if (dvr_auto_rec_pending) {
                static uint8_t auto_rec_retry_count = 0;
                if (++auto_rec_retry_count <= 3) {
                    dvr_send_normal_cmd(BD_CTRL_GET_STS, 0);
                } else {
                    dvr_auto_rec_pending = 0;
                    auto_rec_retry_count = 0;
                    printf("DVR: auto-rec gave up after 3 retries (SD not ready)\n");
                }
            }

            /* System health report every 30 seconds */
            static uint32_t diag_interval = 0;
            if (++diag_interval >= 30) {
                diag_interval = 0;
                /* fb queue status */
                extern void fb_diag_get_counts(int*, int*, uint32_t*, uint32_t*);
                int fb_free_n = 0, fb_ready_n = 0;
                uint32_t vsync_n = 0, no_ready_n = 0;
                fb_diag_get_counts(&fb_free_n, &fb_ready_n, &vsync_n, &no_ready_n);
                /* FreeRTOS heap */
                rt_size_t heap_total = 0, heap_used = 0, heap_max = 0;
                rt_memory_info(&heap_total, &heap_used, &heap_max);
                size_t heap_free = (size_t)heap_total - (size_t)heap_used;
                size_t heap_min  = (size_t)heap_total - (size_t)heap_max;
                printf("[DIAG-SYS] tick=%lu fb:free=%d,ready=%d "
                       "vsync=%lu no_rdy=%lu "
                       "heap:cur=%u,min=%u "
                       "dvr_prev=%d\n",
                       (unsigned long)current_time,
                       fb_free_n, fb_ready_n,
                       (unsigned long)vsync_n, (unsigned long)no_ready_n,
                       (unsigned)heap_free, (unsigned)heap_min,
                       (int)dvr_preview_enable);
            }
        }
#endif

        // Switch view mode every 5 seconds (only when previewing —
        // non-preview USB writes take ~450ms per command, wasting bandwidth)
        if (dvr_sensor_switch_enable && dvr_preview_enable) {
            current_time = rt_tick_get();
            if (current_time - last_switch_time >= 5000) {  // 500 ticks = 5 seconds
                dvr_view_mode = (dvr_view_mode + 1) % 5;
                last_switch_time = current_time;
                printf("DVR: [TICK:%lu] switch view mode=%d\n", (unsigned long)last_switch_time, dvr_view_mode);
                // Use send_normal_cmd to set flag, actual send happens in next loop iteration
                dvr_send_normal_cmd(BD_ANDROID_VIEW_SWITCH, dvr_view_mode);
            }
        }

        /* Yield CPU. When not in preview, sleep longer to avoid
         * burning CPU/USB bandwidth on idle polling (was 2ms = 500
         * USB reads/sec of 100KB each, now 50ms = 20/sec idle check). */
        rt_thread_mdelay(dvr_preview_enable ? 2 : 50);
    }

    dvr_capture_deinit(cap);
    dvr_auto_rec_pending = 0;
    dvr_task_handle = RT_NULL;
    printf("DVR: task exited\n");
    rt_thread_delete(rt_thread_self());
}

// Start DVR USB task when elene file is detected
//
// This is the module's intended entry point. Currently no caller in tree;
// once the USB-host / hotplug infrastructure lands, this gets invoked on
// USB mass-storage insertion. Declared non-static so it isn't flagged as
// unused by `-Wunused-function`.
void dvr_start_if_elene_exists(void)
{
    if (dvr_task_handle != RT_NULL) {
        printf("DVR: task already running\n");
        return;
    }

    if (!dvr_check_elene_exist()) {
        printf("DVR: elene not found\n");
        return;
    }

    printf("DVR: elene found, starting task\n");

    dvr_capture.running = 0;
    dvr_capture.h_cap = NULL;
    dvr_capture.cap_blk_buf = NULL;
    dvr_capture.cap_jpg_buf = NULL;

    dvr_task_handle = rt_thread_create("dvr_usb", dvr_usb_task, &dvr_capture,
                                       16384, 15, 10);
    if (dvr_task_handle == RT_NULL) {
        printf("DVR: create task failed\n");
    } else {
        rt_thread_startup(dvr_task_handle);
    }
}

// Request DVR status from device
void dvr_get_status(void)
{
    if (dvr_fp != NULL) {
        dvr_send_normal_cmd(BD_CTRL_GET_STS, 0);
    }
}

// DVR status getter functions
uint8_t dvr_get_sd_status(void) { return dvr_sd_status; }
uint8_t dvr_get_rec_status(void) { return dvr_rec_status; }
uint8_t dvr_get_lock_status(void) { return dvr_lock_status; }
uint8_t dvr_get_mic_status(void) { return dvr_mic_status; }
uint8_t dvr_get_sd_error_status(void) { return dvr_sd_error; }
uint8_t dvr_get_sd_full_status(void) { return dvr_sd_full; }

// DVR sensor switch control functions
void dvr_set_sensor_switch_enable(uint8_t enable) { dvr_sensor_switch_enable = enable; }

// Store one filename in the appropriate (is_jpg, is_rear) bucket.
// Returns 1 if stored, 0 if the bucket is full (caller still counts it).
static uint8_t dvr_filelist_store(uint8_t is_jpg, uint8_t is_rear, const char *name)
{
    if (name == NULL) return 0;

    if (is_jpg) {
        if (is_rear) {
            if (dvr_photo_list_r_count < DVR_PHOTO_LIST_MAX) {
                memcpy(dvr_photo_list_r[dvr_photo_list_r_count], name, DVR_NAME_MAX - 1);
                dvr_photo_list_r[dvr_photo_list_r_count][DVR_NAME_MAX - 1] = '\0';
                dvr_photo_list_r_count++;
                return 1;
            }
        } else {
            if (dvr_photo_list_f_count < DVR_PHOTO_LIST_MAX) {
                memcpy(dvr_photo_list_f[dvr_photo_list_f_count], name, DVR_NAME_MAX - 1);
                dvr_photo_list_f[dvr_photo_list_f_count][DVR_NAME_MAX - 1] = '\0';
                dvr_photo_list_f_count++;
                return 1;
            }
        }
    } else {
        if (is_rear) {
            if (dvr_video_list_r_count < DVR_VIDEO_LIST_MAX) {
                memcpy(dvr_video_list_r[dvr_video_list_r_count], name, DVR_NAME_MAX - 1);
                dvr_video_list_r[dvr_video_list_r_count][DVR_NAME_MAX - 1] = '\0';
                dvr_video_list_r_count++;
                return 1;
            }
        } else {
            if (dvr_video_list_f_count < DVR_VIDEO_LIST_MAX) {
                memcpy(dvr_video_list_f[dvr_video_list_f_count], name, DVR_NAME_MAX - 1);
                dvr_video_list_f[dvr_video_list_f_count][DVR_NAME_MAX - 1] = '\0';
                dvr_video_list_f_count++;
                return 1;
            }
        }
    }
    return 0;
}

// Parse file list from DVR device response
static void dvr_filelist_parser(uint8_t *buf, int32_t len, uint16_t cmd_par)
{
    int32_t cnt;
    uint8_t *ptr8 = buf;
    uint32_t hash, year, mon, day, hour, min, sec, attrib;
    uint16_t name_idx;
    char fname[DVR_NAME_MAX];
    uint8_t is_jpg = (cmd_par & 0x8000) ? 1 : 0;
    uint8_t is_rear = (cmd_par & 0x4000) ? 1 : 0;

    dvr_video_list_count = 0;
    dvr_photo_list_count = 0;

    /* Only the bucket we are filling this pass is cleared; the other
     * three retain their last-known list until refreshed. */
    if (is_jpg) {
        if (is_rear) dvr_photo_list_r_count = 0;
        else         dvr_photo_list_f_count = 0;
    } else {
        if (is_rear) dvr_video_list_r_count = 0;
        else         dvr_video_list_f_count = 0;
    }

    printf("DVR: filelist parser, len=%ld, is_jpg=%d, is_rear=%d\n",
           (long)len, is_jpg, is_rear);

    for (cnt = 0; cnt < (len / BYTE_PER_FILE); cnt++) {
        // File index (little endian)
        name_idx = (*(ptr8 + 1) << 8) | (*ptr8);

        if (name_idx == 0xFFFF) {
            printf("DVR: list end\n");
            break;
        }

        // File modification time hash
        hash = (*(ptr8 + 5) << 24) | (*(ptr8 + 4) << 16) | (*(ptr8 + 3) << 8) | (*(ptr8 + 2));

        year = (hash >> 26) & 0x3F;
        mon = (hash >> 22) & 0x0F;
        day = (hash >> 17) & 0x1F;
        hour = (hash >> 12) & 0x1F;
        min = (hash >> 6) & 0x3F;
        sec = (hash >> 0) & 0x3F;

        year += 2000;
        attrib = sec & 0x01;  // Lock status

        ptr8 += BYTE_PER_FILE;

        if (is_jpg) {
            snprintf(fname, sizeof(fname), "PICT%04d.jpg", name_idx);
            printf("DVR: [PICT%04d.jpg] %04lu_%02lu_%02lu %02lu:%02lu:%02lu%s\n",
                   name_idx, (unsigned long)year, (unsigned long)mon, (unsigned long)day,
                   (unsigned long)hour, (unsigned long)min, (unsigned long)sec,
                   attrib ? " [LOCKED]" : "");
            dvr_photo_list_count++;
        } else {
            snprintf(fname, sizeof(fname), "%s%04d.avi",
                     attrib ? "LOCK" : "MOVI", name_idx);
            printf("DVR: [%s%04d.avi] %04lu_%02lu_%02lu %02lu:%02lu:%02lu%s\n",
                   attrib ? "LOCK" : "MOVI", name_idx, (unsigned long)year,
                   (unsigned long)mon, (unsigned long)day,
                   (unsigned long)hour, (unsigned long)min, (unsigned long)sec,
                   attrib ? " [LOCKED]" : "");
            dvr_video_list_count++;
        }

        dvr_filelist_store(is_jpg, is_rear, fname);
    }

    printf("DVR: file list count - Video: %d, Photo: %d\n",
           dvr_video_list_count, dvr_photo_list_count);

    /* Signal UI thread that fresh data is available */
    dvr_filelist_ready_flag = 1;
}

// Parse TF card capacity response from DVR device.
// Wire format (little-endian, 8 bytes):
//   trans_buf[0..3]  = total KiB  (1024-byte units, NOT bytes)
//   trans_buf[4..7]  = free  KiB  (1024-byte units, NOT bytes)
// Note: the DVR firmware reports sizes in KiB (1024-byte blocks), matching
// the typical SD-card stat convention. We convert to bytes for the public API.
static void dvr_tf_capacity_parser(uint8_t *buf, int32_t len)
{
    uint32_t total_kib = 0;
    uint32_t free_kib  = 0;

    if (buf == NULL) { printf("DVR: GET_TF_CAPACITY: null buffer\n"); return; }

    if (len >= 4) {
        total_kib = ((uint32_t)buf[3] << 24) | ((uint32_t)buf[2] << 16) |
                    ((uint32_t)buf[1] <<  8) |  (uint32_t)buf[0];
    }
    if (len >= 8) {
        free_kib = ((uint32_t)buf[7] << 24) | ((uint32_t)buf[6] << 16) |
                   ((uint32_t)buf[5] <<  8) |  (uint32_t)buf[4];
    }

    dvr_tf_total_kib      = total_kib;
    dvr_tf_free_kib       = free_kib;
    dvr_tf_capacity_valid = 1;

    printf("DVR: TF [KiB] total=%lu free=%lu (%.2f GiB / %.2f GiB)\n",
           (unsigned long)total_kib, (unsigned long)free_kib,
           (double)total_kib / (1024.0*1024.0),
           (double)free_kib / (1024.0*1024.0));
}

// Get file list from DVR device
//   mode 0: front-camera video file list
//   mode 1: rear-camera  video file list
//   mode 2: front-camera photo file list
//   mode 3: rear-camera  photo file list
void dvr_get_file_list(uint8_t mode)
{
    uint16_t par = 0;

    if (mode == 0) {
        par = 0;        // Video file list F
    } else if (mode == 1) {
        par = 0x4000;   // Video file list R
    } else if (mode == 2) {
        par = 0x8000;   // Photo file list F
    } else if (mode == 3) {
        par = 0xC000;   // Photo file list R
    } else {
        printf("error: invalid mode\n");
        return;
    }

    // Set flag to skip JPEG processing during file list response
    dvr_skip_jpeg_process = 1;

    dvr_send_normal_cmd(BD_CTRL_GET_LIST, par);
    printf("DVR: get file list, mode=%s\n", mode == 0 ? "video" : "photo");
}

// Start playback of a specific file by index
//   mode 0: play front-camera video
//   mode 1: play rear-camera  video
//   mode 2: play front-camera photo
//   mode 3: play rear-camera  photo
static void dvr_pb_start(uint8_t mode, uint16_t index)
{
    uint16_t par = index;

    if (mode == 0) {
        par |= 0;        // Video file list F
    } else if (mode == 1) {
        par |= 0x4000;   // Video file list R
    } else if (mode == 2) {
        par |= 0x8000;   // Photo file list F
    } else if (mode == 3) {
        par |= 0xC000;   // Photo file list R
    } else {
        printf("error: invalid file\n");
        return;
    }

    dvr_send_normal_cmd(BD_CTRL_PB_START, par);
    printf("DVR: pb start, mode=%s, index=%d\n", mode == 0 ? "video" : "photo", index);
}

// Pause playback
static void dvr_pb_pause(void)
{
    dvr_send_normal_cmd(BD_CTRL_PB_PAUSE, 0);
    printf("DVR: pb pause\n");
}

// Stop playback
static void dvr_pb_stop(void)
{
    dvr_send_normal_cmd(BD_CTRL_PB_STOP, 0);
    printf("DVR: pb stop\n");
}

// Fast forward playback
static void dvr_pb_ff(void)
{
    dvr_send_normal_cmd(BD_CTRL_PB_FF, 0);
    printf("DVR: pb fast forward\n");
}

// Fast backward playback
static void dvr_pb_fb(void)
{
    dvr_send_normal_cmd(BD_CTRL_PB_FB, 0);
    printf("DVR: pb fast backward\n");
}

// Set recording resolution: 0=1080P, 1=720P
static void dvr_set_resolution(uint8_t res)
{
    dvr_send_normal_cmd(BD_CTRL_SET_RES, res);
    printf("DVR: set resolution: %s\n", res == 0 ? "1080P" : "720P");
}

// Set loop recording time: 1=1min, 2=2min, 3=3min, 0=get current setting
static void dvr_set_loop_time(uint8_t time)
{
    dvr_send_normal_cmd(BD_CTRL_SET_REC_TIME, time);
    printf("DVR: set loop time: %d min\n", time);
}

// Set MIC on/off: 0=off, 1=on
static void dvr_set_mic(uint8_t onoff)
{
    dvr_send_normal_cmd(BD_CTRL_MIC_ON, onoff);
    printf("DVR: set MIC: %s\n", onoff ? "on" : "off");
}

// Set timestamp watermark on/off: 0=off, 1=on
static void dvr_set_stamp(uint8_t onoff)
{
    dvr_send_normal_cmd(BD_CTRL_STAMP_ON, onoff);
    printf("DVR: set stamp: %s\n", onoff ? "on" : "off");
}

// Format SD card (must stop recording first)
static void dvr_format_card(void)
{
    dvr_send_normal_cmd(BD_CTRL_FORMAT, 0);
    printf("DVR: format card\n");
}

// Restore factory defaults (must stop recording first)
static void dvr_restore_default(void)
{
    dvr_send_normal_cmd(BD_CTRL_DEFAULT, 0);
    printf("DVR: restore factory default\n");
}

// Set system time
static void dvr_set_time(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    st_bd_ctrl_if_t *pctrl = &dvr_ctrl_data;
    uint16_t *ptr16 = (uint16_t *)pctrl->trans_buf;

    ptr16[0] = (month << 8) | year;
    ptr16[1] = (hour << 8) | day;
    ptr16[2] = (second << 8) | minute;

    pctrl->header_id = 0xAA55;
    pctrl->cmd_id = BD_CTRL_SET_TIME;
    pctrl->cmd_par = 0;
    pctrl->data_len = 6;
    pctrl->checksum = 0;
    dvr_need_send_data = 1;

    printf("DVR: set time: %04d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hour, minute, second);
}

// Delete file by index
//   mode 0: delete front-camera video
//   mode 1: delete rear-camera  video
//   mode 2: delete front-camera photo
//   mode 3: delete rear-camera  photo
static void dvr_delete_file(uint8_t mode, uint16_t index)
{
    uint16_t par = index;

    if (mode == 0) {
        par |= 0;        // Video file list F
    } else if (mode == 1) {
        par |= 0x4000;   // Video file list R
    } else if (mode == 2) {
        par |= 0x8000;   // Photo file list F
    } else if (mode == 3) {
        par |= 0xC000;   // Photo file list R
    } else {
        printf("error: invalid file\n");
        return;
    }

    dvr_send_normal_cmd(BD_CTRL_DEL_FILE, par);
    printf("DVR: delete file, mode=%s, index=%d\n", mode == 0 ? "video" : "photo", index);
}

// Lock file by index
static void dvr_lock_file(uint16_t index)
{
    dvr_send_normal_cmd(BD_CTRL_LOCK_FILE, index);
    printf("DVR: lock file index: %d\n", index);
}

// Unlock file by index
static void dvr_unlock_file(uint16_t index)
{
    dvr_send_normal_cmd(BD_CTRL_UNLOCK_FILE, index);
    printf("DVR: unlock file index: %d\n", index);
}

// Get total time of file by index
static void dvr_get_total_time(uint16_t index)
{
    dvr_send_normal_cmd(BD_CTRL_TOTAL_TIME, index);
    printf("DVR: get total time for index: %d\n", index);
}

// Get file list counts
uint16_t dvr_get_video_list_count(void) { return dvr_video_list_count; }
uint16_t dvr_get_photo_list_count(void) { return dvr_photo_list_count; }

/*
 * Per-bucket file list accessors.
 *
 * Each "get_list_*" function returns the number of filenames in the
 * corresponding bucket and, if `out` is non-NULL, copies up to
 * `max_entries` names into `out` (each entry is DVR_NAME_MAX bytes
 * wide). The arrays are read-only to the caller.
 *
 * Bucket layout:
 *   F = front camera, R = rear camera
 *   video = .avi (mov/lock), photo = .jpg
 */
static inline void dvr_copy_names(char *out, const char (*src)[DVR_NAME_MAX],
                                  uint16_t count, uint16_t max_entries)
{
    uint16_t i;
    uint16_t n = (count < max_entries) ? count : max_entries;
    for (i = 0; i < n; i++) {
        memcpy(out + (size_t)i * DVR_NAME_MAX, src[i], DVR_NAME_MAX);
    }
}

uint16_t dvr_api_get_video_list_f(char *out, uint16_t max_entries)
{
    if (out && max_entries) dvr_copy_names(out, dvr_video_list_f, dvr_video_list_f_count, max_entries);
    return dvr_video_list_f_count;
}
uint16_t dvr_api_get_video_list_r(char *out, uint16_t max_entries)
{
    if (out && max_entries) dvr_copy_names(out, dvr_video_list_r, dvr_video_list_r_count, max_entries);
    return dvr_video_list_r_count;
}
uint16_t dvr_api_get_photo_list_f(char *out, uint16_t max_entries)
{
    if (out && max_entries) dvr_copy_names(out, dvr_photo_list_f, dvr_photo_list_f_count, max_entries);
    return dvr_photo_list_f_count;
}
uint16_t dvr_api_get_photo_list_r(char *out, uint16_t max_entries)
{
    if (out && max_entries) dvr_copy_names(out, dvr_photo_list_r, dvr_photo_list_r_count, max_entries);
    return dvr_photo_list_r_count;
}

uint16_t dvr_api_get_video_list_f_count(void) { return dvr_video_list_f_count; }
uint16_t dvr_api_get_video_list_r_count(void) { return dvr_video_list_r_count; }
uint16_t dvr_api_get_photo_list_f_count(void) { return dvr_photo_list_f_count; }
uint16_t dvr_api_get_photo_list_r_count(void) { return dvr_photo_list_r_count; }

/* File-list ready flag: set by dvr_filelist_parser() in USB-task context,
 * polled & cleared by UI thread after dvr_api_get_list(). */
uint8_t dvr_api_is_filelist_ready(void)  { return dvr_filelist_ready_flag; }
void    dvr_api_clear_filelist_ready(void){ dvr_filelist_ready_flag = 0; }

// Get view mode
uint8_t dvr_get_view_mode(void) { return dvr_view_mode; }

/**********************
 *  EXTERNAL DVR API FUNCTIONS
 *  These functions provide external access to DVR control commands
 **********************/

// DVR control API - Get DVR version ID (sends GET_ID; reply arrives async)
void dvr_api_get_id(void)
{
    dvr_send_normal_cmd(BD_CTRL_GET_ID, 0);
}

// DVR control API - Read the most recent DVR firmware version string.
//   buf:      caller-provided buffer; NUL-terminated on return.
//   buf_len:  size of buf in bytes (recommend >= DVR_VERSION_MAX_LEN).
//   Returns 1 if a valid version has been received, 0 otherwise.
//   The string is a snapshot — call again after dvr_api_get_id() to refresh.
uint8_t dvr_api_get_version(char *buf, uint32_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return 0;
    }
    if (!dvr_version_valid) {
        buf[0] = '\0';
        return 0;
    }
    /* dvr_version_buf is written once and not concurrently mutated, so a
     * byte-by-byte copy (no locking) is safe and avoids hidden volatile
     * access on every character. */
    uint32_t i;
    for (i = 0; i + 1 < buf_len && dvr_version_buf[i] != '\0'; i++) {
        buf[i] = dvr_version_buf[i];
    }
    buf[i] = '\0';
    return 1;
}

// DVR control API - Check if DEL_FILE ACK has been received
uint8_t dvr_api_is_del_ack(void)
{
    return dvr_del_ack_received;
}

// DVR control API - Clear DEL_FILE ACK flag (call before sending DEL)
void dvr_api_clear_del_ack(void)
{
    dvr_del_ack_received = 0;
}

// DVR control API - Start recording
void dvr_api_rec_start(void)
{
    dvr_send_normal_cmd(BD_CTRL_REC_START, 0);
}

// DVR control API - Stop recording
void dvr_api_rec_stop(void)
{
    dvr_send_normal_cmd(BD_CTRL_REC_STOP, 0);
}

// DVR control API - Take snapshot/photo
void dvr_api_snap(void)
{
    dvr_send_normal_cmd(BD_CTRL_SNAP, 0);
}

// DVR control API - Emergency/SOS recording (lock current file)
void dvr_api_sos(void)
{
    dvr_send_normal_cmd(BD_CTRL_SOS, 0);
}

// DVR control API - Get file list (mode: 0=video, 1=photo)
void dvr_api_get_list(uint8_t mode)
{
    dvr_get_file_list(mode);
}

// DVR control API - Start playback (mode: 0=video, 1=photo, index: file index)
void dvr_api_pb_start(uint8_t mode, uint16_t index)
{
    dvr_pb_start(mode, index);
}

// DVR control API - Pause playback
void dvr_api_pb_pause(void)
{
    dvr_pb_pause();
}

// DVR control API - Stop playback
void dvr_api_pb_stop(void)
{
    dvr_pb_stop();
}

// DVR control API - Get DVR status
void dvr_api_get_status(void)
{
    dvr_get_status();
}

// DVR control API - Send a TF-card capacity query (no payload).
// Reply is parsed asynchronously by dvr_recv_cmd_process(); read back via
// dvr_api_get_tf_capacity() (or check the valid flag it returns).
void dvr_api_get_tf_capacity_query(void)
{
    dvr_send_normal_cmd(BD_CTRL_GET_TF_CAPACITY, 0);
}

// DVR control API - Read the most recent TF card capacity sample.
//   total_bytes / free_bytes are out-params; may be NULL.
//   Returns 1 if at least one valid sample has been received, 0 otherwise.
uint8_t dvr_api_get_tf_capacity(uint32_t *total_kib, uint32_t *free_kib)
{
    if (total_kib) *total_kib = dvr_tf_total_kib;
    if (free_kib)  *free_kib  = dvr_tf_free_kib;
    return dvr_tf_capacity_valid;
}

// DVR control API - View switch (mode: 0=front, 1=rear, 2=f+r, 3=r+f, 4=hzh)
void dvr_api_view_switch(uint8_t mode)
{
    dvr_send_normal_cmd(BD_ANDROID_VIEW_SWITCH, mode);
    dvr_view_mode = mode;
}

// DVR control API - Set resolution (0=1080P, 1=720P)
void dvr_api_set_res(uint8_t res)
{
    dvr_set_resolution(res);
}

// DVR control API - Set loop recording time (1=1min, 2=2min, 3=3min, 0=get current)
void dvr_api_set_loop_time(uint8_t time)
{
    dvr_set_loop_time(time);
}

// DVR control API - Set MIC on/off (0=off, 1=on)
void dvr_api_set_mic(uint8_t onoff)
{
    dvr_set_mic(onoff);
}

// DVR control API - Set timestamp watermark on/off (0=off, 1=on)
void dvr_api_set_stamp(uint8_t onoff)
{
    dvr_set_stamp(onoff);
}

// DVR control API - Format card
void dvr_api_format(void)
{
    dvr_format_card();
}

// DVR control API - Restore factory defaults
void dvr_api_restore_default(void)
{
    dvr_restore_default();
}

// DVR control API - Set system time
void dvr_api_set_time(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    dvr_set_time(year, month, day, hour, minute, second);
}

// DVR control API - Delete file by index (mode: 0=F video, 1=R video, 2=F photo, 3=R photo)
void dvr_api_del_file(uint8_t mode, uint16_t index)
{
    dvr_delete_file(mode, index);
}

// DVR control API - Lock file by index
void dvr_api_lock_file(uint16_t index)
{
    dvr_lock_file(index);
}

// DVR control API - Unlock file by index
void dvr_api_unlock_file(uint16_t index)
{
    dvr_unlock_file(index);
}

// DVR control API - Get total time of file by index
void dvr_api_get_total_time(uint16_t index)
{
    dvr_get_total_time(index);
}

// DVR control API - Read the most recently received TOTAL_TIME reply.
//   index:        the file index to check the cached reply against.
//   seconds_out:  out-param (may be NULL); set to the cached seconds on match.
//   Returns 1 if a reply for THIS index is cached (seconds_out is valid),
//   0 otherwise (no reply yet, or the cached one is for a different index —
//   the reply is async, so a stale value from a previous query must not be
//   mistaken for this one's answer). Caller should have called
//   dvr_api_get_total_time(index) first and polled this afterwards.
uint8_t dvr_api_get_total_time_result(uint16_t index, uint32_t *seconds_out)
{
    if (!dvr_total_time_valid || dvr_total_time_index != index) {
        return 0;
    }
    if (seconds_out) *seconds_out = dvr_total_time_sec;
    return 1;
}

// DVR control API - Fast forward playback
void dvr_api_pb_ff(void)
{
    dvr_pb_ff();
}

// DVR control API - Fast backward playback
void dvr_api_pb_fb(void)
{
    dvr_pb_fb();
}

// DVR control API - Sensor switch enable/disable
void dvr_api_set_sensor_switch(uint8_t enable)
{
    dvr_set_sensor_switch_enable(enable);
}

// DVR control API - Check if elene file exists
int dvr_api_check_exists(void)
{
    return dvr_check_elene_exist();
}

// DVR control API - Check if DVR task is running
int dvr_api_is_running(void)
{
    return (dvr_task_handle != RT_NULL) ? 1 : 0;
}

// DVR control API - Set display window position and size
// x/y: window top-left coordinates
// width/height: window size
// Note: This affects the OSD layer display area, PXP scales content to fit
void dvr_api_set_display_window(int32_t x, int32_t y, int32_t width, int32_t height)
{
    dvr_display_x = x;
    dvr_display_y = y;
    dvr_display_width = width;
    dvr_display_height = height;
    printf("DVR: set display window - x:%ld y:%ld w:%ld h:%ld\n",
           (long)x, (long)y, (long)width, (long)height);
}

// DVR control API - Get display window position and size
void dvr_api_get_display_window(int32_t *x, int32_t *y, int32_t *width, int32_t *height)
{
    if (x) *x = dvr_display_x;
    if (y) *y = dvr_display_y;
    if (width) *width = dvr_display_width;
    if (height) *height = dvr_display_height;
    printf("DVR: get display window - x:%ld y:%ld w:%ld h:%ld\n",
           (long)dvr_display_x, (long)dvr_display_y,
           (long)dvr_display_width, (long)dvr_display_height);
}

// DVR control API - Reset display window to full screen
void dvr_api_reset_display_window(void)
{
    dvr_display_x = 0;
    dvr_display_y = 0;
    dvr_display_width = LCD_WIDTH;
    dvr_display_height = LCD_HEIGHT;
    printf("DVR: reset display window to full screen\n");
}

// DVR control API - Enable/disable preview mode (jpeg decode)
// enable: 0=disable preview (skip jpeg decode), 1=enable preview (run jpeg decode)
//
// With the CarLink-style render path (mpp_ge_bitblt into UI framebuffer),
// there is no VIDEO OSD layer to toggle and no post-render alpha hook to
// register/unregister — the decoder is initialised once in
// dvr_capture_init and torn down in dvr_capture_deinit. This function
// just gates whether dvr_decode_and_display runs.
void dvr_api_set_preview_enable(uint8_t enable)
{
    dvr_preview_enable = enable ? 1 : 0;
    printf("DVR: preview enable set to %d\n", dvr_preview_enable);
}

// DVR control API - Get preview mode status
uint8_t dvr_api_get_preview_enable(void)
{
    return dvr_preview_enable;
}

// DVR control API - Enable/disable the per-second FPS log line.
// enable: 0=suppress `DVR: [FPS:xx]` prints, 1=emit them. Default ON.
void dvr_api_set_fps_print(uint8_t enable)
{
    dvr_fps_print_enable = enable ? 1 : 0;
    printf("DVR: FPS print %s\n", dvr_fps_print_enable ? "ON" : "OFF");
}

// DVR control API - Query whether the FPS log line is enabled.
uint8_t dvr_api_get_fps_print(void)
{
    return dvr_fps_print_enable;
}

// DVR preview lifecycle - called by dvr_page_init() when DVR window opens
void dvr_start_preview(void)
{
    dvr_api_set_display_window(0, 0, 1024, 600);
    dvr_api_set_preview_enable(1);
    printf("DVR: start_preview (window 0,0,1024,600)\n");
}

// DVR preview lifecycle - called by dvr_page BACK key / window close
void dvr_stop_preview(void)
{
    /* 1. Set flag — DVR task's in-flight decode will return without
     *    touching the fb once it sees this. The 80 ms sleep is the
     *    same race-window the old VIDEO-layer code used; it gives the
     *    dvr_usb_task enough time to drop out of dvr_decode_and_display
     *    so the next AWTK repaint of home_page happens onto a quiescent
     *    fb with no DVR writes interleaving. */
    dvr_preview_enable = 0;

    if (dvr_task_handle != RT_NULL) {
        rt_thread_mdelay(80);
    }

    /* Nothing else: no VIDEO layer to disable, no alpha hook to remove,
     * no UI-framebuffer hack to clean up. The next AWTK repaint of
     * home_page overdraws the preview region on its own. */

    /* Restore LVGL home_page (user closed the DVR preview window). */
    if (dvr_ui_hidden) {
        dvr_ui_hidden = 0;
        vehicle_set_data(VEH_DVR_PLAYING, 0);
    }

    printf("DVR: stop_preview done\n");
}

// DVR device online check
uint8_t dvr_is_device_online(void)
{
    return (dvr_task_handle != RT_NULL) ? 1 : 0;
}

/* Idempotent: looks up current MSC instances and starts/stops the DVR task
 * accordingly. Called from the monitor thread context only. */
void dvr_usb_eval_and_apply(bool state)
{
    if (state) {
        int tries = 0;
        const int max_tries = 10;
        while (tries < max_tries) {
            if (dvr_check_elene_exist()) {
                break;
            }
            tries++;
            rt_thread_mdelay((rt_uint32_t)(tries * 100));  /* 100,200,...,1000ms */
        }
        dvr_start_if_elene_exists();  /* final idempotent kick-off */
    } else {
        dvr_capture.running = 0;
    }
}

#endif