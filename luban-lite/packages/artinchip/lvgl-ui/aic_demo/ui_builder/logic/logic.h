
#ifndef _H_LOGIC_H__
#define _H_LOGIC_H__
#include "lvgl.h"
#include "aic_lang.h"

typedef enum
{
    REFRESH_TIMER_50_MS   ,
    REFRESH_TIMER_200_MS  ,
    REFRESH_TIMER_500_MS  ,
    REFRESH_TIMER_1000_MS ,
    //add

    REFRESH_TIMER_NUM_MAX,
}timer_type_e;

void home_timer_init();

void home_logic_init(); 

void timer_refresh_500_ms(lv_timer_t *info) ;

void timer_refresh_50_ms(lv_timer_t *info);

void global_data_init(lv_timer_t *info);

/* 预加载阶段启动 global_data_init 参数恢复定时器（200ms，完成后自删） */
void boot_recovery_timer_start();

void global_refresh_mileage();

void global_refresh_unit(uint8_t unit);

/* 切语言 + 刷新主界面（含实时值/单位），供设置页切语言与开机恢复复用 */
void global_refresh_language(lang_type_t lang);

#endif