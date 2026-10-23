/*
 * Copyright (C) 2020-2025 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc: jpeg/png decode demo
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <dirent.h>
#include <inttypes.h>
#include "aic_osal.h"
#include "mpp_fb.h"
#include "mpp_mem.h"
#include "frame_allocator.h"
#include "mpp_decoder.h"
#include "mpp_mem.h"
#include "mpp_log.h"
#include "mpp_ge.h"
#include "lvgl/lvgl.h"
#include "ECTiny.h"
#include "dev_config.h"
#include "dev_state.h"
#include "vehicle_param.h"

#if MW_CARLINK_EC_ENABLE
#include "lvgl/lvgl.h"                    /* 包含基础类型和函数 */
#include "libs/qrcode/lv_qrcode.h"   /* 包含二维码头文件 */
static lv_obj_t *g_root_obj = NULL;


#define SAVE_VIDEO_DATA_TO_FILE 0
#define EC_VIDEO_AREA 0
#define USE_USR_QR 1

#define EC_AP_TEST_SSID   "wifi_ECTinyPlus"
#define EC_AP_TEST_PASSWORD   "12345678"
#define EC_RT_WLAN_DEVICE_AP_NAME  "wlan1"  
#define EC_FRAME_SIZE 60*1024 

static IECCallBack *mListener = NULL;
static IECVideoPlayer *mVideoPlayer = NULL;
static ECConfig*   mConfig = NULL;
static ECMirrorConfig* mMirrorConfig = NULL;

static volatile bool mEcinit = false;


static struct aicfb_screeninfo g_screen_info = {0};

static struct mpp_fb *mMppfb = NULL;
static bool mPlay = false;
static struct mpp_decoder* mDec = NULL;
static rt_mutex_t video_mutex = NULL;

static uint64_t mLastTime = 0;
static int mFps = 0;
static uint64_t  timeDifference = 0;
static uint64_t  FRAME_INTERVAL_US = 66; // 25 FPS = 40ms
static uint32_t packSize = 0;
static uint16_t g_video_width = CARLINK_EC_WIDTH;
static uint16_t g_video_height = CARLINK_EC_HEIGHT;
static uint16_t g_video_x = 0;
static uint16_t g_video_y = 0;

static char carlink_url[256] = {0};

const char *get_carlink_url(void) {
    return carlink_url;
}

#if !USE_USR_QR
static void lv_qrcode_ui_init(void)
{   

    printf("lv_qrcode_ui_init begin()\n");
    lv_color_t bg_color = lv_color_white();
    lv_color_t fg_color = lv_color_black();
    lv_obj_t * qr = NULL;
    
    g_root_obj = lv_scr_act();
    qr = lv_qrcode_create(g_root_obj);
    lv_qrcode_set_dark_color(qr, fg_color);
    lv_qrcode_set_light_color(qr, bg_color);
    lv_qrcode_set_size(qr,200);

    // 生成二维码 url
    ECQRInfo info = {0};
    strcpy(info.ssid,EC_AP_TEST_SSID);
    strcpy(info.pwd,EC_AP_TEST_PASSWORD);
    strcpy(info.ip,"192.168.169.1");
    info.action=EC_QR_ACTION_WIFI_AP_MODE_CUSTOMIZED | EC_QR_ACTION_WIFI_AP_MODE_ANDROID;
    const char* url = EC_generateQRCodeUrl(&info);
    printf("url:%s\n",url);
    
    /*Set data*/
    lv_qrcode_update(qr, url, strlen(url));
    lv_obj_center(qr);
    //lv_obj_center(qr);

    /*Add a border with bg_color*/
    lv_obj_set_style_border_color(qr, bg_color, 0);
    lv_obj_set_style_border_width(qr, 5, 0);

    printf("lv_qrcode_ui_init end()\n");

}
#endif

static long long get_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000ll + tv.tv_usec;
}

static uint64_t get_now_ms_modern(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec + 500000) / 1000000;
}


#ifdef AIC_CHIP_D13X
#define USE_VE_FILL_FB
#endif

#define FILE_MAX_NUM 128
struct file_list {
    char *file_path[FILE_MAX_NUM];
    int file_num;
};


#ifdef USE_VE_FILL_FB
struct ext_frame_allocator {
    struct frame_allocator allocator;
    dma_addr_t             buf;
    struct mpp_rect        rect;
};

static int frame_buf_alloc(struct frame_allocator *p, struct mpp_frame* frame,
                           int stride, int height, enum mpp_pixel_format format)
{
    struct ext_frame_allocator *ext = (struct ext_frame_allocator *)p;
    struct aicfb_screeninfo *info = &g_screen_info;
    u32 offset_x, offset_y = ext->rect.y;
    u32 offset = 0;

    if (format > MPP_FMT_BGRA_4444) {
        loge("The decode format 0x%x is not RGB!\n", format);
        return -1;
    }

    frame->buf.format = info->format;
    frame->buf.size.width = info->width;
    frame->buf.size.height = info->height;
    frame->buf.stride[0] = info->stride;
    frame->buf.buf_type = MPP_PHY_ADDR;

    if (offset_y + height > info->height)
        offset_y = info->height - height;

    if (ext->rect.x * (info->bits_per_pixel / 8) + stride > info->stride)
        offset_x = info->stride - stride;
    else
        offset_x = ext->rect.x * (info->bits_per_pixel / 8);

    offset = info->stride * offset_y + offset_x;
    if (offset % 8) {
        logi("The offset of (%d, %d) is not 8-byte aligned\n",
             ext->rect.x, ext->rect.y);
        offset = offset - offset % 8;
    }

    frame->buf.phy_addr[0] = (unsigned long)(ext->buf + offset);
    return 0;
}

static int frame_buf_free(struct frame_allocator *p, struct mpp_frame *frame)
{
    // Use the UI layer framebuffer directly, do not need to free
    return 0;
}

static int allocator_close(struct frame_allocator *p)
{
    struct ext_frame_allocator *ext = (struct ext_frame_allocator*)p;
    mpp_free(ext);
    return 0;
}

static struct alloc_ops def_ops = {
    .alloc_frame_buffer = frame_buf_alloc,
    .free_frame_buffer = frame_buf_free,
    .close_allocator = allocator_close,
};

static struct frame_allocator *allocator_open(u8 *buf, u32 x, u32 y)
{
    struct ext_frame_allocator *ext = (struct ext_frame_allocator*)mpp_alloc(sizeof(struct ext_frame_allocator));
    if (ext == NULL) {
        return NULL;
    }

    memset(ext, 0, sizeof(struct ext_frame_allocator));
    ext->allocator.ops = &def_ops;
    ext->buf = (dma_addr_t)buf;
    ext->rect.x = min(x, g_screen_info.width);
    ext->rect.y = min(y, g_screen_info.height);
    return &ext->allocator;
}
#endif

static void print_help(char *program)
{
    printf("Usage: %s [options]\n", program);
    printf("\t -i, --input: \t\t input stream file name\n");
    printf("\t -r, --rotate: \t\t enable clockwise rotate(0/90/180/270)\n");
    printf("\t -s, --scale: \t\t enable scale(1- 1/2 scale; 2- 1/4 scale; 3- 1/8 scale)\n");
    printf("\t -l, --flip: \t\t enable flip(1-horizontal flip; 2-vertical flip; 3-ver & hor flip)\n");
    printf("\t -f, --format: \t\t output format:rgba8888/bgra8888/argb8888/abgr8888/bgr565/rgb565\n");
    printf("\t -a, --address: \t read image file from a buffer address\n");
    printf("\t -z, --size: \t\t the size of the image file in buffer\n");
    printf("\t -h, --help: \t\t print help info\n");
}

static int get_file_size(int fd, char* path)
{
    struct stat st;
    stat(path, &st);

    logi("mode: %"PRIu32", size: %ld", st.st_mode, st.st_size);

	return st.st_size;
}

static int ge_bitblt(struct ge_bitblt *blt)
{
    int ret = 0;
    struct mpp_ge *ge = mpp_ge_open();

    if (!ge) {
        loge("open ge device error\n");
    }

    ret = mpp_ge_bitblt(ge, blt);
    if (ret < 0) {
        loge("bitblt task failed\n");
        return ret;
    }

    ret = mpp_ge_emit(ge);
    if (ret < 0) {
        loge("emit task failed\n");
        return ret;
    }

    ret = mpp_ge_sync(ge);
    if (ret < 0) {
        loge("ge sync fail\n");
        return ret;
    }

    mpp_ge_close(ge);

    return 0;
}

static u32 *g_vlayer_addr = NULL;
#ifdef AIC_PAN_DISPLAY
static u32 g_fb_buf_index = 0;
#endif

static struct aicfb_screeninfo *get_screen_info(void)
{
    struct mpp_fb *fb = NULL;
    int ret;

    if (g_screen_info.width)
        return &g_screen_info;

    fb = mpp_fb_open();
    if (!fb)
        return NULL;

    ret = mpp_fb_ioctl(fb, AICFB_GET_SCREENINFO, &g_screen_info);
    if (ret) {
        loge("get screen info failed\n");
        return NULL;
    }

    mpp_fb_close(fb);
    return &g_screen_info;
}

static void ui_layer_buf_sync(void)
{
#ifdef AIC_PAN_DISPLAY
    struct ge_bitblt blt = {0};
    struct aicfb_screeninfo *info = NULL;
    uint8_t *src_buf = NULL, *dst_buf = NULL;

    info = get_screen_info();
    if (info == NULL)
        return;

    if (g_fb_buf_index) {
        src_buf = info->framebuffer + info->smem_len;
        dst_buf = info->framebuffer;
    } else {
        src_buf = info->framebuffer;
        dst_buf = info->framebuffer + info->smem_len;
    }

    blt.src_buf.buf_type    = MPP_PHY_ADDR;
    blt.src_buf.phy_addr[0] = (u32)(long)src_buf;
    blt.src_buf.format      = info->format;
    blt.src_buf.stride[0]   = info->stride;
    blt.src_buf.size.width  = info->width;
    blt.src_buf.size.height = info->height;
    blt.dst_buf.buf_type    = MPP_PHY_ADDR;
    blt.dst_buf.phy_addr[0] = (u32)(long)dst_buf;
    blt.dst_buf.format      = info->format;
    blt.dst_buf.stride[0]   = info->stride;
    blt.dst_buf.size.width  = info->width;
    blt.dst_buf.size.height = info->height;
    ge_bitblt(&blt);
#endif
}

static void render_frame(struct mpp_fb* fb, struct mpp_frame* frame,
                         u32 offset_x, u32 offset_y, u32 width, u32 height,
                         u32 layer_id)
{
    struct ge_bitblt blt;
    struct aicfb_screeninfo *info = NULL;
    struct aicfb_layer_data layer = {0};
#ifdef AIC_PAN_DISPLAY
    u32 disp_buf_addr;
    u32 fb0_buf_addr;
#endif
    u32 dst_buf_addr;

    info = get_screen_info();
    if (info == NULL)
        return;

    if (layer_id == AICFB_LAYER_TYPE_UI) {
        layer.layer_id = AICFB_LAYER_TYPE_UI;
        layer.rect_id = 0;
        if (mpp_fb_ioctl(fb, AICFB_GET_LAYER_CONFIG, &layer) < 0) {
            loge("get ui layer config failed\n");
            return;
        }

#ifdef AIC_PAN_DISPLAY
        disp_buf_addr = (u32)layer.buf.phy_addr[0];
        fb0_buf_addr = (u32)(unsigned long)info->framebuffer;
        /* Switch the double-buffer */
        if (disp_buf_addr == fb0_buf_addr) {
            dst_buf_addr = fb0_buf_addr + info->smem_len;
            g_fb_buf_index = 1;
        } else {
            dst_buf_addr = fb0_buf_addr;
            g_fb_buf_index = 0;
        }
#else
        dst_buf_addr = layer.buf.phy_addr[0];
#endif
    } else {
        layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
        if (mpp_fb_ioctl(fb, AICFB_GET_LAYER_CONFIG, &layer) < 0) {
            loge("get video layer config failed\n");
            return;
        }
        dst_buf_addr = layer.buf.phy_addr[0];
    }
    //pr_info("Render to buf 0x%08x\n", dst_buf_addr);

    memset(&blt, 0, sizeof(struct ge_bitblt));
    memcpy(&blt.src_buf, &frame->buf, sizeof(struct mpp_buf));

    blt.dst_buf.buf_type = MPP_PHY_ADDR;
    blt.dst_buf.phy_addr[0] = dst_buf_addr;
    blt.dst_buf.format = info->format;
    blt.dst_buf.stride[0] = info->stride;
    blt.dst_buf.size.width = info->width;
    blt.dst_buf.size.height = info->height;
    blt.dst_buf.crop_en = 1;

    blt.dst_buf.crop.x = offset_x;
    blt.dst_buf.crop.y = offset_y;
    blt.dst_buf.crop.width = blt.src_buf.crop.width;
    blt.dst_buf.crop.height = blt.src_buf.crop.height;

    //logi("phy_addr: %x, stride: %d", blt.src_buf.phy_addr[0], blt.src_buf.stride[0]);
    //logi("width: %d, height: %d, format: %d", blt.src_buf.size.width, blt.src_buf.size.height, blt.src_buf.format);

    ge_bitblt(&blt);

#ifdef AIC_PAN_DISPLAY
    if (layer_id == AICFB_LAYER_TYPE_UI) {
        /* display at last */
        mpp_fb_ioctl(fb, AICFB_PAN_DISPLAY, &g_fb_buf_index);
        mpp_fb_ioctl(fb, AICFB_WAIT_FOR_VSYNC, 0);
        ui_layer_buf_sync();
    }
#endif
}

static int parse_rotation(char *str)
{
    if (!strcmp(optarg, "90"))
        return MPP_ROTATION_90;

    if (!strcmp(optarg, "180"))
        return MPP_ROTATION_180;

    if (!strcmp(optarg, "270"))
        return MPP_ROTATION_270;

    return MPP_ROTATION_0;
}

static int parse_flip(char *str)
{
    if (!strcmp(optarg, "1"))
        return MPP_FLIP_H;

    if (!strcmp(optarg, "2"))
        return MPP_FLIP_V;

    if (!strcmp(optarg, "3"))
        return MPP_FLIP_H | MPP_FLIP_V;

    return 0;
}

static int str_to_format(char *str)
{
    /* str to format table struct */
    struct StrToFormat {
        char *str;
        int format;
    };

    /* format conversion */
    static int table_size = 0;
    static struct StrToFormat table[] = {
        {"argb8888", MPP_FMT_ARGB_8888},
        {"abgr8888", MPP_FMT_ABGR_8888},
        {"rgba8888", MPP_FMT_RGBA_8888},
        {"bgra8888", MPP_FMT_BGRA_8888},
        {"xrgb8888", MPP_FMT_XRGB_8888},
        {"xbgr8888", MPP_FMT_XBGR_8888},
        {"rgbx8888", MPP_FMT_RGBX_8888},
        {"bgrx8888", MPP_FMT_BGRX_8888},
        {"argb4444", MPP_FMT_ARGB_4444},
        {"abgr4444", MPP_FMT_ABGR_4444},
        {"rgba4444", MPP_FMT_RGBA_4444},
        {"bgra4444", MPP_FMT_BGRA_4444},
        {"rgb565", MPP_FMT_RGB_565},
        {"bgr565", MPP_FMT_BGR_565},
        {"argb1555", MPP_FMT_ARGB_1555},
        {"abgr1555", MPP_FMT_ABGR_1555},
        {"rgba5551", MPP_FMT_RGBA_5551},
        {"bgra5551", MPP_FMT_BGRA_5551},
        {"rgb888", MPP_FMT_RGB_888},
        {"bgr888", MPP_FMT_BGR_888},
        {"yuv420", MPP_FMT_YUV420P},
        {"nv12", MPP_FMT_NV12},
        {"nv21", MPP_FMT_NV21},
        {"yuv422", MPP_FMT_YUV422P},
        {"nv16", MPP_FMT_NV16},
        {"nv61", MPP_FMT_NV61},
        {"yuyv", MPP_FMT_YUYV},
        {"yvyu", MPP_FMT_YVYU},
        {"uyvy", MPP_FMT_UYVY},
        {"vyuy", MPP_FMT_VYUY},
        {"yuv400", MPP_FMT_YUV400},
        {"yuv444", MPP_FMT_YUV444P},
    };
    int i = 0;

    table_size = sizeof(table) / sizeof(table[0]);
    for (i = 0; i < table_size; i++) {
        if (!strncmp(str, table[i].str, strlen(table[i].str)))
            return table[i].format;
    }
    return -1;
}

static int parse_format(char *str)
{
    return str_to_format(str);
}

// static void pic_test(const char *name)
// {   
//     int out_format = MPP_FMT_ABGR_8888;
//     int ret = 0;
//     int file_len = 0, r_len = 0;
//     int input_fd = 0;
//     int type = MPP_CODEC_VIDEO_DECODER_MJPEG;
//     int rot_flip_flag = 0;
//     int ver_scale = 0;
//     int hor_scale = 0;
//     struct mpp_fb *fb = NULL;
//     char *ptr = NULL;

//     optind = 0;
//     int c;

//     ptr = strrchr(name, '.');

//     if (!strcmp(ptr, ".jpg")) {
//         type = MPP_CODEC_VIDEO_DECODER_MJPEG;
//     }
//     if (!strcmp(ptr, ".png")) {
//         type = MPP_CODEC_VIDEO_DECODER_PNG;
//     }
//     if (!strcmp(ptr, ".aicp")) {
//         type = MPP_CODEC_VIDEO_DECODER_AICP;
//     }
//     printf("decode type: 0x%02X\n", type);
//     printf("optarg: %s\n", name);
//     input_fd = open(name, O_RDONLY);
//     if(input_fd < 0) {
//         printf("open file(%s) failed, %d\n", name, input_fd);
//         return;
//     }

//     file_len = get_file_size(input_fd, name);
//     printf("Read image from %s, length %d\n", name, file_len);


//     fb = mpp_fb_open();

//     // 1. create mpp_decoder
//     struct mpp_decoder* dec = mpp_decoder_create(type);

//     struct decode_config config;
//     config.bitstream_buffer_size = (file_len + 1023) & (~1023);
//     config.extra_frame_num = 0;
//     config.packet_count = 1;
//     config.pix_fmt = out_format;

// #ifdef AIC_VE_DRV_V10
//     if(type == MPP_CODEC_VIDEO_DECODER_MJPEG)
//         config.pix_fmt = MPP_FMT_NV12;
// #endif

//     if(rot_flip_flag) {
//         printf("rot_flip_flag: %d\n", rot_flip_flag);
//         mpp_decoder_control(dec, MPP_DEC_INIT_CMD_SET_ROT_FLIP_FLAG, &rot_flip_flag);
//     }

//     if(ver_scale || hor_scale) {
//         struct mpp_scale_ratio scale;
//         scale.hor_scale = hor_scale;
//         scale.ver_scale = ver_scale;
//         mpp_decoder_control(dec, MPP_DEC_INIT_CMD_SET_SCALE, &scale);
//     }

//     // 2. init mpp_decoder
//     mpp_decoder_init(dec, &config);

//     // 3. get an empty packet from mpp_decoder
//     struct mpp_packet packet;
//     memset(&packet, 0, sizeof(struct mpp_packet));
//     mpp_decoder_get_packet(dec, &packet, file_len);

//     // 4. copy data to packet
//     if (input_fd > 0) {
//         r_len = read(input_fd, packet.data, file_len);
//     } else {
//         memcpy(packet.data, ptr, file_len);
//         r_len = file_len;
//         printf("Read image from 0x%lx, length %d\n", (long)ptr, file_len);
//     }
//     packet.size = r_len;
//     packet.flag = PACKET_FLAG_EOS;
//     printf("read len: %d, file_len: %d\n", r_len, file_len);

//     // 5. put the packet to mpp_decoder
//     mpp_decoder_put_packet(dec, &packet);

//     // 6. decode
//     //time_start(mpp_decoder_decode);
//     ret = mpp_decoder_decode(dec);
//     if(ret < 0) {
//         loge("decode error");
//         goto out;
//     }
//     //time_end(mpp_decoder_decode);

//     // 7. get a decoded frame
//     struct mpp_frame frame;
//     memset(&frame, 0, sizeof(struct mpp_frame));
//     mpp_decoder_get_frame(dec, &frame);

//     // 8. Render the data to Framebuffer
//     render_frame(fb, &frame, 0, 0, 0, 0, AICFB_LAYER_TYPE_UI);

//     // 9. return this frame
//     mpp_decoder_put_frame(dec, &frame);

// out:
//     // 10. destroy mpp_decoder
//     mpp_decoder_destory(dec);

//     if (fb)
//         mpp_fb_close(fb);

//     if(input_fd > 0)
//         close(input_fd);
// }

static int read_dir(char* path, struct file_list *files)
{
    char* ptr = NULL;
    int file_path_len = 0;
    struct dirent* dir_file = NULL;

    DIR* dir = opendir(path);
    if (dir == NULL) {
        loge("read dir failed");
        return -1;
    }
    printf("path: %s\n", path);
    while((dir_file = readdir(dir))) {
        ptr = strrchr(dir_file->d_name, '.');
        if (ptr == NULL)
            continue;
        if (strcmp(ptr, ".png") != 0)
            continue;

        file_path_len = 0;
        file_path_len += strlen(path);
        file_path_len += 1;
        file_path_len += strlen(dir_file->d_name);
        logd("file_path_len:%d\n",file_path_len);
        if (file_path_len > FILE_MAX_NUM-1) {
            loge("%s too long \n",dir_file->d_name);
            continue;
        }
        files->file_path[files->file_num] = (char *)mpp_alloc(file_path_len+1);
        files->file_path[files->file_num][file_path_len] = '\0';
        strcpy(files->file_path[files->file_num], path);
        strcat(files->file_path[files->file_num], "/");
        strcat(files->file_path[files->file_num], dir_file->d_name);
        logd("i: %d, filename: %s", files->file_num, files->file_path[files->file_num]);
        files->file_num ++;
    }
    closedir(dir);
    return 0;
}

static void onECConnectStatus(ECConnectedStatus status, ECTransportType type) {
    printf("onECConnectStatus status:%d, type:%d\n", status, type);
    switch (status) {
        case EC_CONNECT_STATUS_DEVICE_ATTACHED: {
            break;
        }
        case EC_CONNECT_STATUS_DEVICE_DEATTACHED: {
            break;
        }
        case EC_CONNECT_STATUS_CONNECTING: {
            break;
        }
        case EC_CONNECT_STATUS_CONNECT_FAILED: {
            break;
        }
        case EC_CONNECT_STATUS_CONNECT_SUCCEED: {
            // 开始投屏
            printf("start mirror\n");
            EC_startMirror();
            
            // 查询时间
            //EC_queryTime();

            break;
        }
        case EC_CONNECT_STATUS_DISCONNECTED: {
            printf("stop mirror\n");
            break;
        }
        case EC_CONNECT_STATUS_APP_EXIT: {
            printf("exit app\n");
            break;
        }
        case EC_CONNECT_STATUS_INTERRUPTED_BY_APP: {
            printf("interrupted by app\n");
            break;
        }
    }
}

static void onMirrorStatus(ECMirrorStatus status)
{
    printf("onMirrorStatus status:%d\n", status);
}

static void onECStatusMessage(ECStatusMessage status)
{
    printf("onECStatusMessage status:%d\n", status);
}

static void onCallAction(ECCallType type, const char *name, const char *number)
{
    printf("onCallAction type:%d, name:%s, number:%s\n", type, name, number);
}

static void onMirrorInfoChanged(const ECVideoInfo *info)
{
    printf("onMirrorInfoChanged\n");
}

static void onOTACheckResult(const ECOTAInfo *info, int count)
{
    printf("onOTACheckResult\n");
}

static void onOTAData(unsigned char *data, unsigned int len)
{
    printf("onOTAData\n");
}

static void onPhoneAppHUD(const ECNavigationHudInfo *data)
{
    printf("[demo]onPhoneAppHUD begin \n");
    printf("[demo]onPhoneAppHUD end \n");
}

static void onQueryTime(const char* dateTime, unsigned int dateTimeLen) {
    printf("onQueryTime dateTime=[%s],dateTimeLen=%d \n", dateTime, dateTimeLen);
}

static void videoStart(int width, int height)
{
    printf("videoStart,width:%d,height:%d\n",width,height);   
    rt_mutex_take(video_mutex, RT_WAITING_FOREVER);
    if(mPlay){
        rt_mutex_release(video_mutex);
        return;
    }

    int len = EC_FRAME_SIZE*1.1;
    int type = MPP_CODEC_VIDEO_DECODER_MJPEG;
    int rot_flip_flag = 0;
    int ver_scale = 0;
    int hor_scale = 0;
    struct mpp_fb *fb = NULL;
    int out_format = MPP_FMT_ABGR_8888;
    mMppfb = mpp_fb_open();

        // 1. create mpp_decoder
    mDec = mpp_decoder_create(type);

    struct decode_config config;
    config.bitstream_buffer_size = (len + 1023) & (~1023);
    config.extra_frame_num = 0;
    config.packet_count = 1;
    config.pix_fmt = out_format;

#ifdef AIC_VE_DRV_V10
    if(type == MPP_CODEC_VIDEO_DECODER_MJPEG)
        config.pix_fmt = MPP_FMT_NV12;
#endif

    if(rot_flip_flag) {
        printf("rot_flip_flag: %d\n", rot_flip_flag);
        mpp_decoder_control(mDec, MPP_DEC_INIT_CMD_SET_ROT_FLIP_FLAG, &rot_flip_flag);
    }

    if(ver_scale || hor_scale) {
        struct mpp_scale_ratio scale;
        scale.hor_scale = hor_scale;
        scale.ver_scale = ver_scale;
        mpp_decoder_control(mDec, MPP_DEC_INIT_CMD_SET_SCALE, &scale);
    }

    // 2. init mpp_decoder
    mpp_decoder_init(mDec, &config);
    mPlay = true;
    rt_mutex_release(video_mutex);
}

static void videoPlay(const void *data, unsigned int len)
{   
    rt_mutex_take(video_mutex, RT_WAITING_FOREVER);
    if(!mPlay){
        rt_mutex_release(video_mutex);
        return;
    }
    static uint64_t mLastFrameTime = 0;
    static uint64_t mAccumulatedTime = 0;
    
    // 统一使用微秒时间函数
    uint64_t mCurrentTime = get_now_ms_modern();
    
    // 首帧保护
    if (mLastFrameTime == 0) {
        mLastFrameTime =  get_now_ms_modern();
    }
    
    // // // 计算真实帧间隔
    // uint64_t deltaTime = mCurrentTime - mLastFrameTime;
    
    // // 高精度延迟（微秒级）
    // if (deltaTime < FRAME_INTERVAL_US) {
    //     // 将微秒转换为毫秒，但注意精度损失
    //     uint32_t delay_ms = (FRAME_INTERVAL_US - deltaTime); 
    //     if (delay_ms > 0) {
    //         rt_thread_mdelay(delay_ms);
    //         //printf("[VideoPlayer]>>>>>>>delay_ms = %d\n", delay_ms);
    //     }
    //     // 重新获取当前时间
    //     mCurrentTime = get_now_ms_modern();
    // }
    
    // FPS统计
    mFps++;
    packSize = packSize + len;
    mAccumulatedTime += (mCurrentTime - mLastFrameTime);
    if (mAccumulatedTime >= 1000) { // 1秒 = 1,000,000微秒
        printf("[VideoPlayer]>>>>>>>FPS = %d,packSize:%d\n", mFps,packSize);
        mFps = 0;
        packSize = 0;
        mAccumulatedTime = 0;
    }
    
    mLastFrameTime = get_now_ms_modern();
    int ret = 0;
    int rot_flip_flag = 0;
    int ver_scale = 0;
    int hor_scale = 0;

    // 3. get an empty packet from mpp_decoder
    struct mpp_packet packet;
    memset(&packet, 0, sizeof(struct mpp_packet));
    mpp_decoder_get_packet(mDec, &packet, len);

    // 4. copy data to packet
    // if (input_fd > 0) {
    //     r_len = read(input_fd, packet.data, file_len);
    // } else {
    //     memcpy(packet.data, ptr, file_len);
    //     r_len = file_len;
    //     printf("Read image from 0x%lx, length %d\n", (long)ptr, file_len);
    // }

    memcpy(packet.data, data, len);
    packet.size = len;
    packet.flag = PACKET_FLAG_EOS;
    //printf("read len: %d, file_len: %d\n", r_len, file_len);

    // 5. put the packet to mpp_decoder
    mpp_decoder_put_packet(mDec, &packet);

    // 6. decode
    //time_start(mpp_decoder_decode);
    ret = mpp_decoder_decode(mDec);
    if(ret < 0) {
        loge("decode error");
        rt_mutex_release(video_mutex);
        return;
    }
    //time_end(mpp_decoder_decode);

    // 7. get a decoded frame
    struct mpp_frame frame;
    memset(&frame, 0, sizeof(struct mpp_frame));
    mpp_decoder_get_frame(mDec, &frame);

    // 8. Render the data to Framebuffer
    render_frame(mMppfb, &frame, g_video_x, g_video_y, 0, 0, AICFB_LAYER_TYPE_UI);

    vehicle_set_data(VEH_CARLINK_CONNECTED, 1);

    // 9. return this frame
    mpp_decoder_put_frame(mDec, &frame);
    rt_mutex_release(video_mutex);
}

static void videoStop()
{  
    printf("videoStop\n"); 
    rt_mutex_take(video_mutex, RT_WAITING_FOREVER);
    if(!mPlay){
        rt_mutex_release(video_mutex);
        return;
    }
    if(mDec)
        mpp_decoder_destory(mDec);
    if(mMppfb)
        mpp_fb_close(mMppfb);
    mPlay = false;
    vehicle_set_data(VEH_CARLINK_CONNECTED, 0);
    rt_mutex_release(video_mutex);
}

static void ec_init()
{
    printf("------------------begin------------------\n");

    // 互联回调函数设置
    mListener = malloc(sizeof(IECCallBack));
    memset(mListener, 0, sizeof(IECCallBack));
    mListener->onECConnectStatus = onECConnectStatus;
    mListener->onMirrorStatus = onMirrorStatus;
    mListener->onECStatusMessage = onECStatusMessage;
    mListener->onCallAction = onCallAction;
    mListener->onMirrorInfoChanged = onMirrorInfoChanged;
    mListener->onOTACheckResult = onOTACheckResult;
    mListener->onOTAData = onOTAData;
    mListener->onPhoneAppHUD = onPhoneAppHUD;
    mListener->onQueryTime = onQueryTime;

    mVideoPlayer = (IECVideoPlayer*)malloc(sizeof(IECVideoPlayer));
    memset(mVideoPlayer, 0, sizeof(IECVideoPlayer));
    mVideoPlayer->start = videoStart;
    mVideoPlayer->play = videoPlay;
    mVideoPlayer->stop = videoStop;

    mConfig = (ECConfig*)malloc(sizeof(ECConfig));
    memset(mConfig, 0, sizeof(ECConfig));
    snprintf(mConfig->uuid, sizeof(mConfig->uuid), "%s", "HNCX04XXXXXX000001");
    sprintf(mConfig->versionName, "%s", ECSDK_VERSION);
    sprintf(mConfig->mediaModel, "%s", "mediaModel");
    sprintf(mConfig->carBrand, "%s", "carbit");
    sprintf(mConfig->carModel, "%s", "fcm362k");
    sprintf(mConfig->carConfig, "%s", "Plus");
    mConfig->supportBTCall = 0;
    mConfig->supportConnect = EC_SUPPORT_CONNECT_ANDROID_WIFI | EC_SUPPORT_CONNECT_IPHONE_WIFI;
    mConfig->supportFunction = EC_SUPPORT_FUNCTION_WIFI_QR_CONNECT;
    mConfig->disableShowCallInfo = 0;
    mConfig->disablePageInRVMap = 0;
    mConfig->socketTimeoutPeriod = 10;
    mConfig->enableDpi = 1;
    mConfig->dpi = 170;

    mMirrorConfig = (ECMirrorConfig*)malloc(sizeof(ECMirrorConfig));
    memset(mMirrorConfig, 0, sizeof(ECMirrorConfig));
    mMirrorConfig->screenDirection = EC_SCREEN_DIRECTION_HORIZONTAL;
    mMirrorConfig->width = g_video_width;
    mMirrorConfig->height = g_video_height;
    mMirrorConfig->type = EC_VIDEO_TYPE_JPEG;
    mMirrorConfig->quality = EC_FRAME_SIZE;
    mMirrorConfig->screenPhysicsWidth = 15.5;
    mMirrorConfig->screenPhysicsHeight = 8.7;
    // mMirrorConfig->mirrorFeature = EC_MIRROR_FEATURE_HUD_ELEMENT_SIMPLE;
#if EC_VIDEO_AREA
    // 异形屏参数设置
    ECVideoView view = {
        1,
        {1, 0,0,1024,600},
        {1,0,0,1024,600}
    };
    mMirrorConfig->viewConfig = view;
#endif

    // 设置日志
    printf("------------------begin------------------\n");
    EC_setLogInfo(EC_LOG_LEVEL_ERROR);
    // ECTiny 初始化
    EC_initialize(mConfig, mMirrorConfig, mVideoPlayer, mListener);
    if (video_mutex == NULL) {
        video_mutex = rt_mutex_create("video_lock", RT_IPC_FLAG_PRIO);
        if (video_mutex == NULL) {
            loge("Failed to create video mutex!");
            return;
        }
    }
    rt_thread_mdelay(1000);

#if !USE_USR_QR
    lv_async_call(lv_qrcode_ui_init, NULL);
#else
    // 生成二维码 url
    ECQRInfo info = {0};
    strcpy(info.ssid,EC_AP_TEST_SSID);
    strcpy(info.pwd,EC_AP_TEST_PASSWORD);
    strcpy(info.ip,"192.168.169.1");
    info.action = EC_QR_ACTION_WIFI_AP_MODE_CUSTOMIZED | EC_QR_ACTION_WIFI_AP_MODE_ANDROID;

    const char* url = EC_generateQRCodeUrl(&info);
    if (url == NULL) {
        loge("Failed to generate QR code URL!");
        return;
    }

    printf("genetate url:%s\n",url);
    memset(carlink_url, 0, sizeof(carlink_url));
    strncpy(carlink_url, url, sizeof(carlink_url) - 1);
    vehicle_set_data(VEH_CARLINK_URL_STATUS, 1);
#endif

    while(mEcinit){
        rt_thread_mdelay(2000);
    }
    return;
}

/* 注册客户端连接/断开事件 */
static void ap_event_handler(int event, struct rt_wlan_buff *buff, void *parameter) {
    switch (event) {
        case RT_WLAN_EVT_AP_ASSOCIATED:
            printf("Client connected\n");
            break;
        case RT_WLAN_EVT_AP_DISASSOCIATED:
            printf("Client disconnected\n");
            break;
    }
}

#if 0
static int aic_wifi_init(char *dev_name, char *set_mode)
{
    rt_err_t err = RT_EOK;
    rt_wlan_mode_t mode;
    char device[16] = {0};

    if (strncmp("sta", set_mode, 3) == 0) {
        mode = RT_WLAN_STATION;
    } else if (strncmp("ap", set_mode, 2) == 0) {
        mode = RT_WLAN_AP;
    } else if (strncmp("none", set_mode, 4) == 0) {
        mode = RT_WLAN_NONE;
    } else {
        return -1;
    }

    // if the device name is not set, set the default value.
    if (dev_name == NULL) {
        if (mode == RT_WLAN_STATION)
            snprintf(device, sizeof(device), "%s", RT_WLAN_DEVICE_STA_NAME);
        else if (mode == RT_WLAN_AP)
            snprintf(device, sizeof(device), "%s", RT_WLAN_DEVICE_AP_NAME);
    } else {
        snprintf(device, sizeof(device), "%s", dev_name);
    }

    err = rt_wlan_set_mode((const char*)device, mode);
    if (err != RT_EOK) {
        printf("wifi init err! errcode:%d\n", err);
        return -1;
    }

    return 0;
}
#endif

void set_carlink_display_info(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    g_video_x		= x;
    g_video_y		= y;
    g_video_width	= w;
    g_video_height	= h;
}

void start_wifi_ap()
{   
    printf("enter start ap ....\r\n");
    int ret ;
#if 0
    // = aic_wifi_init(EC_RT_WLAN_DEVICE_AP_NAME, "ap");
    // if (ret < 0) {
    //     printf("WiFi initialization failed!\n");
    // }
#endif

    //保持调用和wifi 命令一样的接口验证
    rt_wlan_set_mode(EC_RT_WLAN_DEVICE_AP_NAME, RT_WLAN_AP);

    ret = rt_wlan_start_ap(EC_AP_TEST_SSID, EC_AP_TEST_PASSWORD);
    if (ret < 0) {
        printf("WiFi start ap failed!\n");
    }

    printf("WiFi start ap end\n");
    rt_thread_mdelay(200);
    /* 在AP启动后注册 */
    rt_wlan_register_event_handler(RT_WLAN_EVT_AP_ASSOCIATED, ap_event_handler, RT_NULL);
    rt_thread_mdelay(3000);
    if(!mEcinit){
        mEcinit = true;
        ec_init();
    }
}

static void canlink_start_thread(void *param) {
    while (get_boot_animation_status() < ANIMATION_STATE_END) {
        rt_thread_mdelay(100);
    }   
    rt_thread_mdelay(200);
    start_wifi_ap();
}

void start_carlink(void) {
    rt_thread_t thread;
    set_carlink_display_info(0, 0, 1024, 600);

    thread = rt_thread_create("canlink_start_thread", canlink_start_thread, RT_NULL,
                                  8192, 12, 10);
    if (thread != RT_NULL) {
        rt_thread_startup(thread);
    } else {
        rt_kprintf("create carlink thread failed!\n");
        return;
    }
}

#endif