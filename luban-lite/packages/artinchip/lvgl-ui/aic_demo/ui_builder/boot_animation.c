
#include <stdbool.h>
#include "ui_objects.h"
#include "aic_ui.h"
#include "ui_util.h"
#include "aic_lang.h"
#include "definition.h"
#include "dev_state.h"
#include "boot_animation.h"

#if !defined(AIC_STARTUP_UI_SHOW)

#define FRAME_RATE  20
/* 播放到该帧时提前加载主界面：主界面创建/解码耗时用动画剩余帧的播放时间
 * 消化，播完直接切到已就绪的主界面，避免切屏瞬间加载导致闪烁。
 * 动画总帧数不足该值时会回退到最后一帧前触发。 */
#define BOOT_ANIM_PRELOAD_FRAME  40
static int current_image_index = 0;
static int max_image_count = 0 ;
/* 实际触发提前加载主界面的帧号 */
static int preload_frame = 0 ;
/* 是否已提前加载主界面（防止重复触发） */
static bool home_preloaded = false ;
static int len = 0 ;
static char *path_suffix = "start_animation/";
static char  path[256] = {0};

/* 动画每帧完整路径(副本),动画结束后据此释放 lv_image_cache 中的解码缓存 */
static char **anim_frame_paths = NULL;

/* 独立全屏动画界面：动画播放期间只解码动画帧，主界面在第 preload_frame
 * 帧起并行加载但延后切屏，由 boot_animation_preload_cb()/finish_cb() 衔接 */
static lv_obj_t *start_screen = NULL;

lv_timer_t *boot_anim_timer = NULL ;

static void on_boot_animation_timer(struct _lv_timer_t * timer)
{
    int i;
    lv_obj_t *start_img = (lv_obj_t *)timer->user_data ;
    if(NULL == start_img )
        return ;

    lv_snprintf(path + len, sizeof(path) - len - 1, "%03d.jpg" , current_image_index);
    lv_image_set_src(start_img , path) ;

    /* 保存当前帧完整路径副本,供动画结束后释放图片缓存 */
    if(anim_frame_paths != NULL && anim_frame_paths[current_image_index] == NULL) {
        anim_frame_paths[current_image_index] = lv_strdup(path) ;
        if(anim_frame_paths[current_image_index] == NULL) {
            LV_LOG_ERROR("lv_strdup failed: %s\n", path) ;
        }
    }
    current_image_index++ ;

    /* 播到约定帧时提前创建主界面（不切屏），主界面图片解码与动画剩余帧
     * 并行，动画播完时主界面已就绪，切屏不闪烁 */
    if(!home_preloaded && current_image_index >= preload_frame) {
        home_preloaded = true ;
        boot_animation_preload_cb() ;
    }

    if(current_image_index == max_image_count)
    {
        /* 释放开机动画图片缓存,腾出空间给后续 UI */
        for(i = 0 ; i < max_image_count ; i++) {
            if(anim_frame_paths[i] != NULL) {
                lv_image_cache_drop(anim_frame_paths[i]) ;
                lv_free(anim_frame_paths[i]) ;
                anim_frame_paths[i] = NULL ;
            }
        }
        lv_free(anim_frame_paths) ;
        anim_frame_paths = NULL ;

        lv_timer_delete(boot_anim_timer) ;
        boot_anim_timer = NULL ;

        lv_obj_delete(start_img) ;
        if(start_screen != NULL) {
            lv_obj_delete(start_screen) ;
            start_screen = NULL ;
        }
        set_boot_animation_status(ANIMATION_STATE_END) ;
        boot_animation_finish_cb() ;
    }


}

void boot_animation_init()
{

    /* 播放前先切到独立黑底全屏，保证动画期间缓存只放动画帧 */
    start_screen = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(start_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(start_screen, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_style_bg_color(start_screen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_scr_load(start_screen);

    len = lv_snprintf(path , sizeof(path) - 1 , LVGL_IMAGE_PATH(%s), path_suffix );
    max_image_count = count_png_files(path, 1) ;
    current_image_index = 0 ;

    if ((max_image_count == 0 )|| (max_image_count == -1))
    {
        LV_LOG_ERROR("not found *.jpg picture max_image_count = %d\n" ,max_image_count);
        lv_obj_delete(start_screen) ;
        start_screen = NULL ;
        set_boot_animation_status(ANIMATION_STATE_END) ;
        /* 无动画资源也要继续创建主界面，否则系统一直黑屏 */
        if(!home_preloaded) {
            home_preloaded = true ;
            boot_animation_preload_cb() ;
        }
        boot_animation_finish_cb() ;
        return ;
    }

    /* 提前加载主界面的帧号：动画足够长时固定 40 帧；帧数较少时留最后一帧
     * 为切屏提前量，保证总有时间预加载主界面 */
    preload_frame = (max_image_count > BOOT_ANIM_PRELOAD_FRAME)
                        ? BOOT_ANIM_PRELOAD_FRAME
                        : (max_image_count > 1 ? max_image_count - 1 : 0) ;
    home_preloaded = false ;

    /* 防止重复调用时上次数组未释放 */
    if(anim_frame_paths == NULL) {
        anim_frame_paths = lv_malloc(sizeof(char *) * max_image_count) ;
        if(anim_frame_paths == NULL) {
            LV_LOG_ERROR("lv_malloc anim_frame_paths failed\n") ;
            lv_obj_delete(start_screen) ;
            start_screen = NULL ;
            set_boot_animation_status(ANIMATION_STATE_END) ;
            if(!home_preloaded) {
                home_preloaded = true ;
                boot_animation_preload_cb() ;
            }
            boot_animation_finish_cb() ;
            return ;
        }
        lv_memset(anim_frame_paths, 0, sizeof(char *) * max_image_count) ;
    }

    if (boot_anim_timer == NULL)
    {
        lv_obj_t *start_img = lv_image_create(start_screen);
        boot_anim_timer = lv_timer_create(on_boot_animation_timer , (uint32_t)(1000 / FRAME_RATE) , (void *)start_img) ;
        lv_timer_ready(boot_anim_timer);
    }

    return ;
}

#endif