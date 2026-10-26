#ifndef __MW_DVR_H__
#define __MW_DVR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "dev_config.h"

/* RT-Thread DFS — provides struct dfs_fd (file descriptor) and the
 * dfs_file_open / dfs_file_close / dfs_file_read / dfs_file_write /
 * dfs_file_lseek / dfs_file_stat helper family that mw_dvr.c uses for
 * elene capture I/O. The DFS path "/udisk/elene" is resolved by the DFS
 * mount table (the same layer that powers "ls udisk"), independent of
 * any FATFS drive-number assumptions.
 *
 * The bare FATFS API (`ff.h`) is still available for constants like
 * FSIZE_t / FR_OK that are referenced in helper code, but the actual
 * file handle type held by this module is struct dfs_fd, not FIL. */
#include <dfs_file.h>
#include <fcntl.h>

/* LCD / OSD dimensions — fallback defaults when the project does not
 * expose them through any panel config header. 1024x600 matches the
 * project's active panel (see CARLINK_EC_WIDTH/HEIGHT in dev_config.h). */
#ifndef LCD_WIDTH
#define LCD_WIDTH   1024
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT  600
#endif
#ifndef OSD_WIDTH
#define OSD_WIDTH   LCD_WIDTH
#endif
#ifndef OSD_HEIGHT
#define OSD_HEIGHT  LCD_HEIGHT
#endif

#if ENABLE_BD_USB_DVR_FUNC
// USB DVR related defines
#define BD_CTRL_GET_ID          0x00
#define BD_CTRL_REC_START        0x01
#define BD_CTRL_REC_STOP        0x02
#define BD_CTRL_SNAP            0x03
#define BD_CTRL_SOS             0x04
#define BD_CTRL_GET_LIST        0x05
#define BD_CTRL_PB_START        0x06
#define BD_CTRL_PB_PAUSE        0x07    /*播放暂停*/
#define BD_CTRL_PB_STOP         0x08    /*播放停止*/
#define BD_CTRL_GET_STS         0x09
#define BD_CTRL_REC_TICK        0x0A    /*记录仪当前的录像秒数*/
#define BD_CTRL_SET_RES         0x0B    /*记录仪的录像分辨率 - 0:1080P,1:720P - 预留一般是默认不可选*/
#define BD_CTRL_SET_REC_TIME    0x0C    /*循环录像时长 1:1min,2:2min,3:3min,发送 0:(代表获取当前固件的对应设置)*/
#define BD_CTRL_MIC_ON          0x0D    /*MIC开关,0:off,1:on*/
#define BD_CTRL_STAMP_ON        0x0E    /*时间水印开关,0:off,1:on - 预留,默认开*/
#define BD_CTRL_KEY_ON          0x0F    /*预留*/
#define BD_CTRL_OFF_TIME        0x10    /*预留*/
#define BD_CTRL_FORMAT          0x11    /*格式化卡,需要先停止录像*/
#define BD_CTRL_DEFAULT         0x12    /*恢复出厂设置,需先停止录像*/
#define BD_CTRL_SET_TIME        0x13    /*设置时间*/
#define BD_CTRL_DEL_FILE        0x14    /*删除指定index的文件,一般文件列表界面操作*/
#define BD_CTRL_LOCK_FILE       0x15    /*加锁指定index的文件，一般文件列表界面操作*/
#define BD_CTRL_UNLOCK_FILE     0x16    /*解锁指定index的文件，一般文件列表界面操作*/
#define BD_CTRL_TOTAL_TIME      0x17    /*返回指定index录像文件的总时长,一般回放文件时使用*/
#define BD_CTRL_PB_FF           0x1e    /*回放视频快进*/
#define BD_CTRL_PB_FB           0x1f    /*回放视频快退*/
#define BD_CTRL_SENSOR_SEL      0x22   /*多路切换预览显示 0:前路,1:后路*/
#define BD_ANDROID_VIEW_SWITCH  0x9c    /*多路切换预览显示 0:前路,1:后路*/
#define BD_CTRL_GET_TF_CAPACITY  0x38    /* Query TF card total/free capacity (reply: 8B LE: total[0..3] free[4..7]) */

#define BD_MAX_DATA_LEN         (60*1024)
#define BD_FILE_JPG_BIT         0x8000
#define BD_FILE_BACK_BIT        0x4000
#define BD_HEADER_LEN           10
#define BD_JPG_OFFSET           17
/* Path passed to the DFS API (dfs_file_open / dfs_file_stat).
 *
 * This is a DFS-style absolute path. DFS resolves "/udisk" via the
 * filesystem mount table (mw_usbhost.c::usbh_udisk_mount_thread does
 * dfs_mount("udisk", "/udisk", "elm", 0, 0)) and forwards the remaining
 * "elene" component to the elm FATFS driver — same code path that
 * "ls udisk" uses, which is known to list elene correctly. */
#define JPG_FILE_NAME           "/udisk/elene"
#define FIXED_BUF_SIZE          600

// DVR debug print control: 1=enable print, 0=disable print
#define DVR_DEBUG_PRINT         0
// JPEG decode debug print control: 1=enable, 0=disable (default off)
#define JPEG_DEBUG_PRINT        0
// DVR framerate statistics control: 1=enable, 0=disable
#define DVR_FRAMERATE_PRINT     0

typedef struct tag_st_bd_ctrl_if {
    uint16_t header_id;
    uint16_t cmd_id;
    uint16_t cmd_par;
    uint16_t data_len;
    uint16_t checksum;
    uint8_t trans_buf[BD_MAX_DATA_LEN];
} st_bd_ctrl_if_t;

typedef struct tag_dvr_capture {
    struct dfs_fd *h_cap; /* RT-Thread DFS file descriptor. Heap-allocated
                           * dfs_fd whose pointer is also stored in file-private
                           * `dvr_fp` for the global one-fd-at-a-time access pattern
                           * in mw_dvr.c. Note: this used to be FATFS FIL*; switched
                           * to DFS-layer handle so the I/O path matches the DFS mount
                           * resolution that "ls udisk" relies on. */
    uint8_t *cap_blk_buf;
    uint8_t *cap_jpg_buf;
    uint32_t blksize;
    uint64_t file_size;
    st_bd_ctrl_if_t cap_ctrl;
    uint8_t running;
    uint8_t round_flag;
    uint32_t read_times;
    // Decode and display related.
    //
    // Migration notes (was MFC + PXP + dedicated VIDEO OSD, now mpp_decoder +
    // mpp_ge_blit directly into UI layer — same approach as third/carlink):
    //   - mfc_handle  removed: replaced by file-private `dvr_mpp_dec`
    //                     (struct mpp_decoder *, see mw_dvr.c globals).
    //   - yuv_buf     removed: the VE decoder allocates its own internal
    //                     YUV output buffer; caller no longer supplies one.
    //   - dst_buf[2]  removed: render goes straight into the UI layer
    //                     framebuffer via mpp_ge_bitblt. No double buffer.
    //   - dst_buf_index / yuv_buf_size / dst_buf_size: removed (unused).
    uint8_t display_on;
    uint32_t current_jpg_size;  // Store current jpg size for decode
} st_dvr_capture_t;

#endif /* ENABLE_BD_USB_DVR_FUNC */

/* ==========================================================================
 * DVR API function declarations
 *
 * Implemented in mw_dvr.c (DVR control backend). External modules (UI,
 * mw_init.c, etc.) include this header to call DVR control / query /
 * playback / file-list functions. Mirrors the public surface of
 *   dvr_api.h (in dc002/AMT630HV100 SDK)
 * consolidated into this project's helloworld app.
 *
 * The function bodies live inside `#if ENABLE_BD_USB_DVR_FUNC` in
 * mw_dvr.c, but the prototypes are exported unconditionally so callers
 * do not have to define ENABLE_BD_USB_DVR_FUNC just to use the API.
 * Linking fails with undefined symbols if the feature is off.
 * ========================================================================== */

/* ---------- Core command / lifecycle (already public in mw_dvr.c) ---------- */
void     dvr_send_normal_cmd(unsigned short cmd_id, unsigned short cmd_par);
void     dvr_get_file_list(uint8_t mode);
void     dvr_get_status(void);
void     dvr_start_if_elene_exists(void);
void     dvr_usb_eval_and_apply(bool state);

/* ---------- Status getters ---------- */
uint8_t  dvr_get_sd_status(void);
uint8_t  dvr_get_rec_status(void);
uint8_t  dvr_get_lock_status(void);
uint8_t  dvr_get_mic_status(void);
uint8_t  dvr_get_sd_error_status(void);
uint8_t  dvr_get_sd_full_status(void);
uint8_t  dvr_get_view_mode(void);

/* ---------- File list counts (legacy single counter) ---------- */
uint16_t dvr_get_video_list_count(void);
uint16_t dvr_get_photo_list_count(void);

/* ---------- Sensor switch control ---------- */
void     dvr_set_sensor_switch_enable(uint8_t enable);

/* ---------- Preview lifecycle ---------- */
void     dvr_start_preview(void);
void     dvr_stop_preview(void);
uint8_t  dvr_is_device_online(void);

/* ---------- DVR firmware version ----------
 * dvr_api_get_id() sends GET_ID; reply arrives asynchronously.
 * dvr_api_get_version() returns the most recent cached version string.
 */
void     dvr_api_get_id(void);
uint8_t  dvr_api_get_version(char *buf, uint32_t buf_len);

/* ---------- Recording / snap / SOS ---------- */
void     dvr_api_rec_start(void);
void     dvr_api_rec_stop(void);
void     dvr_api_snap(void);
void     dvr_api_sos(void);

/* ---------- File list (fetch + per-bucket cache) ----------
 * mode argument for dvr_api_get_list / dvr_api_pb_start / dvr_api_del_file:
 *   0: front-camera video
 *   1: rear-camera  video
 *   2: front-camera photo
 *   3: rear-camera  photo
 */
void     dvr_api_get_list(uint8_t mode);
uint16_t dvr_api_get_video_list_f(char *out, uint16_t max_entries);
uint16_t dvr_api_get_video_list_r(char *out, uint16_t max_entries);
uint16_t dvr_api_get_photo_list_f(char *out, uint16_t max_entries);
uint16_t dvr_api_get_photo_list_r(char *out, uint16_t max_entries);
uint16_t dvr_api_get_video_list_f_count(void);
uint16_t dvr_api_get_video_list_r_count(void);
uint16_t dvr_api_get_photo_list_f_count(void);
uint16_t dvr_api_get_photo_list_r_count(void);
uint8_t  dvr_api_is_filelist_ready(void);
void     dvr_api_clear_filelist_ready(void);

/* ---------- Playback ---------- */
void     dvr_api_pb_start(uint8_t mode, uint16_t index);
void     dvr_api_pb_pause(void);
void     dvr_api_pb_stop(void);
void     dvr_api_pb_ff(void);
void     dvr_api_pb_fb(void);

/* ---------- File delete / lock / unlock / total-time ---------- */
void     dvr_api_del_file(uint8_t mode, uint16_t index);
void     dvr_api_lock_file(uint16_t index);
void     dvr_api_unlock_file(uint16_t index);
void     dvr_api_get_total_time(uint16_t index);
uint8_t  dvr_api_is_del_ack(void);
void     dvr_api_clear_del_ack(void);

/* ---------- Status / TF card capacity ---------- */
void     dvr_api_get_status(void);
void     dvr_api_get_tf_capacity_query(void);
uint8_t  dvr_api_get_tf_capacity(uint32_t *total_kib, uint32_t *free_kib);

/* ---------- View / resolution / mic / stamp / format / default / time ---------- */
void     dvr_api_view_switch(uint8_t mode);
void     dvr_api_set_res(uint8_t res);
void     dvr_api_set_loop_time(uint8_t time);
void     dvr_api_set_mic(uint8_t onoff);
void     dvr_api_set_stamp(uint8_t onoff);
void     dvr_api_format(void);
void     dvr_api_restore_default(void);
void     dvr_api_set_time(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second);

/* ---------- Misc control ---------- */
void     dvr_api_set_sensor_switch(uint8_t enable);
int      dvr_api_check_exists(void);
int      dvr_api_is_running(void);

/* ---------- Display window (UI layer position / size) ---------- */
void     dvr_api_set_display_window(int32_t x, int32_t y, int32_t width, int32_t height);
void     dvr_api_get_display_window(int32_t *x, int32_t *y, int32_t *width, int32_t *height);
void     dvr_api_reset_display_window(void);

/* ---------- Preview enable / FPS print ---------- */
void     dvr_api_set_preview_enable(uint8_t enable);
uint8_t  dvr_api_get_preview_enable(void);
void     dvr_api_set_fps_print(uint8_t enable);
uint8_t  dvr_api_get_fps_print(void);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __MW_DVR_H__