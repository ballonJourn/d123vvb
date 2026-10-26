#include <stdio.h>  
#include <stdlib.h>
#include <stdbool.h>
#include "selfcheck.h"
#include "logic.h"
#include "ui_init.h"
#include "aic_ui.h"
#include "home_view_interface.h"
#include "vehicle_data.h"
#include "vehicle_argument.h"
#include "vehicle_mile.h"
#include "mileage_calc.h"
#include "window_manager.h"
#include "button_manager.h"
#include "key_common.h"
#include "dev_config.h"

#define REFRESH_INTERVAL_50_MS   (50)
#define REFRESH_INTERVAL_200_MS  (200)
#define REFRESH_INTERVAL_500_MS  (500)
#define REFRESH_INTERVAL_1000_MS (1000)

static lv_timer_t* timer_array[REFRESH_TIMER_NUM_MAX] = { 0 } ;

void home_logic_init()
{
    // 自检
    selfcheck_init();

    //添加定时器
    home_timer_init();

    mileage_calc_init();
    
#if MW_ADC_KEY_ENABLE
    set_key_event_cb(button_manager_event) ;
#endif

    return  ;
}

void home_timer_init() 
{
    timer_array[REFRESH_TIMER_50_MS ] = lv_timer_create( timer_refresh_50_ms ,   REFRESH_INTERVAL_50_MS ,NULL ) ;
    timer_array[REFRESH_TIMER_200_MS] = lv_timer_create( global_data_init    ,   REFRESH_INTERVAL_200_MS,  NULL ) ;
    timer_array[REFRESH_TIMER_500_MS] = lv_timer_create( timer_refresh_500_ms,   REFRESH_INTERVAL_500_MS ,NULL ) ;

    return  ;
}

void global_refresh_mileage()
{
    // 里程
    uint32_t u32_odo   = vehicle_get_mile_odo()  ;
    uint32_t u32_tripA = vehicle_get_mile_tripA();
    uint32_t u32_tripB = vehicle_get_mile_tripB();
    if (MPH == vehicle_get_param_unit())
    {
        u32_odo   *= KM_CONVERT_MILE ; 
        u32_tripA *= KM_CONVERT_MILE ;
        u32_tripB *= KM_CONVERT_MILE ;
    }
    home_refresh_odo ((double)u32_odo) ;
    home_refresh_trip((double)u32_tripA) ;

    // if()
    return ;
}

void global_refresh_unit(uint8_t unit)
{
    // 速度
    // int32_t value ;
    // value = vehicle_get_data_speed() ;
    // if (MPH == vehicle_get_param_unit())
    //         value *= KM_CONVERT_MILE ; 
    // home_refresh_speed(value) ;
    // home_refresh_unit(unit);

    global_refresh_mileage();
    home_refresh_mileage_unit(unit) ;
    home_refresh_electrical_unit(unit) ;
}

void global_data_init(lv_timer_t *info)
{
    // printf("===================");
    (void)info ;
    
    static bool is_init_usr_param  = false ;
    static bool is_init_mile_param = false ;
    
    if (!is_init_usr_param && (true == vehicle_get_param_recovery()))
    {
        uint8_t value  ;

        // 设置单位 // 设置里程程息 、 剩余里程
        value = vehicle_get_param_unit() ;
        global_refresh_unit(value);
        
        // //显示主题 0：白天 1:黑夜 2:自动
        // value = vehicle_get_param_display();
        // if (0 == value)
        //     global_refresh_display(DIAPLAY_DAY_OPTION);
        // else if(1 == value)
        //     global_refresh_display(DIAPLAY_NIGHT_OPTION);
        // else 
        //     global_refresh_display(vehicle_get_data_current_display()) ;
        
        // refresh_ver(veicle_get_data_version());

        is_init_usr_param = true ; 

        LV_LOG_USER("vehicle_get_param_recovery successed \n");
    }


    if (!is_init_mile_param  && (true == vehicle_get_mile_recovery()) )
    {
        // 里程
        global_refresh_mileage();

        is_init_mile_param = true ;

        LV_LOG_USER("vehicle_get_mile_recovery successed\n" );
    }


    if (is_init_usr_param && is_init_mile_param)
    {
        if(timer_array[REFRESH_TIMER_200_MS])
        {
            lv_timer_delete(timer_array[REFRESH_TIMER_200_MS]) ;
            timer_array[REFRESH_TIMER_200_MS] = NULL ;
        }
        return  ;
    }
    
    return  ;
}


void timer_refresh_500_ms(lv_timer_t *info)
{
    //时间刷新闪烁
    (void)info ;
    
    if (checkself_get_state() != CHECK_STATE_FINISHED /*|| get_demonstration_state()*/ ) 
        return  ;

    if(WINDOW_MAIN != window_manager_get())
        return ;

    static int  clock_min   = 0 ;
    static int  clock_hour   = 0 ;
    static bool clock_colon = true ;
    // static uint8_t display_value  = 0xFF ;
    // static bool update_state = false ;

    int min = 0;//vehicle_get_time_min();
    if (clock_min != min)
    {
        clock_min = min;
        home_refresh_clock_min(min) ;
    }
    
    int hour = 0 ;//vehicle_get_time_min();
    if (clock_hour != hour)
    {
        clock_hour = hour ;
        home_refresh_clock_hour(hour) ;
    }

    clock_colon = !clock_colon ;
    home_refresh_clock_colon(clock_colon) ;


    if (checkself_get_state() != CHECK_STATE_FINISHED ) 
        return  ;

    // bool state = vehicle_get_uptate_state();
    // if (state != update_state)
    // {
    //     if ( true == state ){
    //         navigator_switch_to(UPDATE_PAGE, false) ;
    //         printf("checked update_page open\n");
    //     }else{
    //         navigator_back();
    //         printf("checked update_page close\n");
    //     }
        
    //     update_state = state ;
    // }
    

    
    return  ;
}

void signal_view_update()
{
    for (size_t i = 0; i < ICON_SIGNAL_NUM_MAX; i++)
    {
        int32_t state =  vehicle_get_data_signal_lamp( i )  ;
        home_refresh_signal(i , !!state);
    }

    return ;
}



void timer_refresh_50_ms(lv_timer_t *info)
{
    (void)info ;
    
    if (checkself_get_state() != CHECK_STATE_FINISHED /*|| get_demonstration_state()*/ ) 
        return  ;
    
    button_manager_refresh();

    int32_t value = vehicle_get_data_speed();
    speed_view_run(value) ;

    if(WINDOW_MAIN != window_manager_get())
        return ;

    if(get_mileage_state())
    {
        global_refresh_mileage();
        set_mileage_state(false);
    }

    if (vehicle_get_mile_changed() && (vehicle_get_data_speed() == 0 ) )
        on_mileage_changed();
    
    // bluetooth_view_update();
    
    home_refresh_speed(speed_view_current()) ;

    value = vehicle_get_data_remain_battary();
    home_refresh_electrical( value );
    
    signal_view_update();

    return  ;
}


lv_timer_t* get_timer_ID(timer_type_e timerID)
{
    if(timerID < REFRESH_TIMER_NUM_MAX)
        return timer_array[timerID] ;
    
    return NULL ;
}

void clean_timer_ID(timer_type_e timerID)
{
    if(timerID < REFRESH_TIMER_NUM_MAX)
    {
        if (timer_array[timerID] )
        {
           lv_timer_del(timer_array[timerID]) ;
           timer_array[timerID] = NULL ;
        }
    }

    return ;
}
