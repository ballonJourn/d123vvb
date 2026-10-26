
#include "ui_objects.h"
#include "aic_ui.h"
#include "ui_util.h"
#include "aic_lang.h"
#include "definition.h"
#include "dev_state.h"

#if !defined(AIC_STARTUP_UI_SHOW)

#define FRAME_RATE  20
static int current_image_index = 0;
static int max_image_count = 0 ;
static int len = 0 ;
static char *path_suffix = "start_animation/";
static char  path[256] = {0};

lv_timer_t *boot_anim_timer = NULL ;

static void on_boot_animation_timer(struct _lv_timer_t * timer)
{
    lv_obj_t *start_img = (lv_obj_t *)timer->user_data ; 
    if(NULL == start_img )
        return ;

    lv_snprintf(path + len, sizeof(path) - len - 1, "%02d.jpg" , current_image_index);
    lv_image_set_src(start_img , path) ;
    current_image_index++ ;

    if(current_image_index == max_image_count)
    {
        lv_timer_delete(boot_anim_timer) ;
        boot_anim_timer = NULL ;

        lv_obj_delete(start_img) ;
        set_boot_animation_status(ANIMATION_STATE_END) ;
    }
    
    
}

void boot_animation_init() 
{

    len = lv_snprintf(path , sizeof(path) - 1 , LVGL_IMAGE_PATH(%s), path_suffix );
    max_image_count = count_png_files(path, 1) ;
    current_image_index = 0 ;

    if ((max_image_count == 0 )|| (max_image_count == -1))
    {
        LV_LOG_ERROR("not found *.png picture max_image_count = %d\n" ,max_image_count);
        return ;
    }

    if (boot_anim_timer == NULL)
    {
        home_page_t *page = home_page_get(&ui_manager) ;
        lv_obj_t *start_img = lv_image_create(page->obj);
        boot_anim_timer = lv_timer_create(on_boot_animation_timer , (uint32_t)(1000 / FRAME_RATE) , (void *)start_img) ;
        lv_timer_ready(boot_anim_timer);
    }

    return ;
}

#endif