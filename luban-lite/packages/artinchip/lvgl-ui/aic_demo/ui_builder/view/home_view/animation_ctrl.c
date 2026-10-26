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
    VIEW_MEMBER_VALID(page, cartoon_image) 

    lv_snprintf(path + len, ANI_IMAGE_PATH - len - 1, "%03d.png" , current_image_index);
    // LV_LOG_INFO("imagepath[len] = %c path - 5 = %s\n" ,path[len -1], path + len - 5) ;
    lv_image_set_src(cartoon_image , path) ;

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