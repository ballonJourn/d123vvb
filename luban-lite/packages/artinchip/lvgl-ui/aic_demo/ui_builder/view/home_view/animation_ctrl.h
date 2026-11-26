#ifndef CARTOON_CTRL_H
#define CARTOON_CTRL_H

typedef enum {

    CARTOON_STATE_START   ,
    CARTOON_STATE_RUNNING ,

    CARTOON_STATE_MAX  ,

}cartoon_state_e;


typedef enum animation_theme{

    CARTOON_STYLE_1    ,
    CARTOON_STYLE_2    ,
    CARTOON_STYLE_3    ,

    CARTOON_STYLE_MAX  ,

}cartoon_theme_e;

void animation_init() ;

void animation_stop() ;

void animation_resume();

void animation_set_theme(cartoon_theme_e theme) ;

void animation_set_state(cartoon_state_e state) ;

/* ===== 移植自 AWTK home_view 的 4 套 move 动画 + 转灯呼吸 ===== */
#include <stdint.h>

/* 移动 obj 的 x 坐标 from → to，duration ms，linear 路径 */
void home_anim_move_x(void *obj, int32_t from, int32_t to, uint32_t ms);

/* 改变 obj 的 width  from → to，duration ms，linear 路径 */
void home_anim_move_w(void *obj, int32_t from, int32_t to, uint32_t ms);

/* 透明度呼吸（0..255 来回），period_ms 完整周期，ease_in_out 路径 */
void home_anim_breath(void *obj, uint32_t period_ms);


#endif