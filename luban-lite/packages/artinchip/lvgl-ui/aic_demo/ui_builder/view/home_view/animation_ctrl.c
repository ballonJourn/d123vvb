#include "animation_ctrl.h"
#include "lvgl.h"
#include "ui_objects.h"
#include <stdio.h>
#include "definition.h"
#include "aic_ui.h"
#include <string.h>
#include <dirent.h>
#include "ui_util.h"
#include "selfcheck.h"

#define ANI_IMAGE_COUNT 20
#define ANI_IMAGE_PATH  256
#define FRAME_RATE  20

lv_timer_t *anim_timer = NULL ;

cartoon_state_e current_theme = CARTOON_STYLE_1 ;
cartoon_theme_e current_state = CARTOON_STATE_START   ;
static int current_image_index = 0;
static int max_image_count = 0 ;
static int len = 0 ;
static const char* state_path[] = {"cartoon_start/" , "cartoon_running/"} ;
static const char* theme_path[] = {"style_1/" , "style_2/" , "style_3/" } ;
static char  path[ANI_IMAGE_PATH] = {0};

static void on_animation_timer(struct _lv_timer_t * timer)
{
    // if (checkself_get_state() != CHECK_STATE_FINISHED /*|| get_demonstration_state()*/ )
    //     return  ;

    home_page_t *page = home_page_get(&ui_manager) ;
    /* 新 home_page (1024x600) 没有 cartoon_image 控件 —— 静默 return，
     * 避免每 50ms 一条 ERROR 噪音 */
    if (!page || !page->cartoon_image) return;

    lv_snprintf(path + len, ANI_IMAGE_PATH - len - 1, "%03d.png" , current_image_index);
    // LV_LOG_INFO("imagepath[len] = %c path - 5 = %s\n" ,path[len -1], path + len - 5) ;
    lv_image_set_src(page->cartoon_image , path) ;

    current_image_index = (current_image_index + 1) % max_image_count ;
}

void animation_init() 
{
    len = lv_snprintf(path , ANI_IMAGE_PATH - 1 , LVGL_IMAGE_PATH(%s%s),\
    state_path[CARTOON_STATE_START], theme_path[CARTOON_STYLE_1] ) ;

    max_image_count = count_png_files(path, 0) ;
    current_image_index = 0 ;
    // len = strlen(path) ;

    if ((max_image_count == 0 )|| (max_image_count == -1))
    {
        LV_LOG_ERROR("not found *.png picture\n max_image_count = %d" ,max_image_count);
        return ;
    }
    

    if (anim_timer == NULL)
        anim_timer = lv_timer_create(on_animation_timer , (uint32_t)(1000 / FRAME_RATE) , NULL) ;

    return ;
}


void animation_resume()
{
    if (anim_timer == NULL)
        return ;
    
    lv_timer_resume(anim_timer) ;
}


void animation_stop() 
{
    if (anim_timer == NULL)
        return ;
    
    lv_timer_pause(anim_timer) ;
    
    current_image_index = 0 ;
    return ;
}
 
void animation_set_theme(cartoon_theme_e theme) 
{
    if (theme > CARTOON_STYLE_3)
        return ;
    
    current_theme = theme ;
    animation_init() ;
}
 
void animation_set_state(cartoon_state_e state)
{
    if (state > CARTOON_STATE_RUNNING)
        return ;

    current_state = state ;
    animation_init() ;
}

/* =========================================================================
 * 移植自 AWTK home_view 的 move/breath 动画
 *   move_speed_out/in: speed_view x 切换 (303↔586)
 *   move_power_out/in: power_view x 切换 (688↔1024)
 *   move_dock_out/in:  dock_slider width 切换 (336↔648)
 *   转向灯 breath: 4s ease_in_out 来回
 * ========================================================================= */
static void anim_x_cb(void *var, int32_t v) { lv_obj_set_x((lv_obj_t *)var, v); }
static void anim_w_cb(void *var, int32_t v) { lv_obj_set_width((lv_obj_t *)var, v); }
static void anim_opa_cb(void *var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

void home_anim_move_x(void *obj, int32_t from, int32_t to, uint32_t ms)
{
    if (!obj) return;
    lv_anim_t a = {0};
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_start(&a);
}

void home_anim_move_w(void *obj, int32_t from, int32_t to, uint32_t ms)
{
    if (!obj) return;
    lv_anim_t a = {0};
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_exec_cb(&a, anim_w_cb);
    lv_anim_start(&a);
}

void home_anim_breath(void *obj, uint32_t period_ms)
{
    if (!obj) return;
    lv_anim_t a = {0};
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, period_ms / 2);
    lv_anim_set_playback_duration(&a, period_ms / 2);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_start(&a);
}