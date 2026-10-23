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
#include "ref_mileage_calc.h"
#include "window_manager.h"
#include "button_manager.h"
#include "key_common.h"
#include "mode_view.h"       /* home_refresh_mode_index */
#include "power_view.h"
#include "dev_config.h"
#include "ui_theme.h"
#include "display.h"
#include "pop_tip_manager.h"
#include "language.h"        /* language_option_to_lang_type（开机语言恢复用） */

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
    ref_mileage_calc_init();
    
#if MW_ADC_KEY_ENABLE || MW_IO_KEY_ENABLE || MW_CAN_IO_KEY_ENABLE
    set_key_event_cb(button_manager_event) ;
#endif

    return  ;
}

void home_timer_init()
{
    timer_array[REFRESH_TIMER_50_MS ] = lv_timer_create( timer_refresh_50_ms ,   REFRESH_INTERVAL_50_MS ,NULL ) ;
    timer_array[REFRESH_TIMER_500_MS] = lv_timer_create( timer_refresh_500_ms,   REFRESH_INTERVAL_500_MS ,NULL ) ;

    return  ;
}

/* global_data_init(参数恢复) 的启动时机由动画结束后提前到开机动画预加载：
 * 主界面创建后即轮询恢复单位/里程参数，用动画剩余帧时间消化。
 * 保留 200ms 轮询 + 完成后自删机制，recovery 未就绪时继续等待，
 * 确保最终能拿到正确保存的参数。由 boot_animation_preload_cb 调用。 */
void boot_recovery_timer_start()
{
    if (timer_array[REFRESH_TIMER_200_MS] == NULL) {
        timer_array[REFRESH_TIMER_200_MS] = lv_timer_create(global_data_init, REFRESH_INTERVAL_200_MS, NULL);
    }
}

/* 续航里程: 无效(三包 SOC 全 ≤5%)时传 -1 让 view 层显示 "--km"。
 * 单位换算在 logic 层做完再传纯数值 (与 odo/trip 同一约定) */
static void global_refresh_range()
{
    unit_e unit = (MPH == vehicle_get_param_unit()) ? MPH : KM_H ;

    if (!ref_mileage_display_valid())
    {
        home_refresh_range(-1, unit) ;
        return ;
    }

    uint32_t u32_km = ref_mileage_total_x10() / 10 ;   // 0.1km → km
    if (MPH == unit)
    {
        u32_km *= KM_CONVERT_MILE ;
    }
    home_refresh_range((int32_t)u32_km, unit) ;

    return ;
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

    // 续航里程: 一并覆盖"数据变化"与"切单位/开机恢复"两条路径
    global_refresh_range() ;

    // if()
    return ;
}

void global_refresh_unit(uint8_t unit)
{
    // 速度：单位文本 + 数字位图（数字按新单位重绘，不依赖车速变化）
    home_refresh_speed_unit(unit);
    home_refresh_speed(speed_view_current());

    global_refresh_mileage();
    home_refresh_mileage_unit(unit) ;
    home_refresh_electrical_unit(unit) ;
}

/* 切语言：先落全局语言（后续字体解析才指向新语言），再刷主界面静态文案与字体，
 * 最后复用 global_refresh_unit 把实时值/单位写回 —— home_page_update_current_language
 * 只重设字体、不写实时值，故必须补这一次，否则 trip/odo 与单位文本不会更新 */
void global_refresh_language(lang_type_t lang)
{
    aic_set_language(lang);
    home_page_update_current_language();
    global_refresh_unit(vehicle_get_param_unit());
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

        // 语言：按已存选项恢复（否则重启后 UI 回英文，与设置页焦点指向的选项不符）。
        // 此处只落语言 + 刷主界面文案字体；单位/实时值由下面的 global_refresh_unit 负责
        aic_set_language(language_option_to_lang_type(vehicle_get_param_language()));
        home_page_update_current_language();

        // 设置单位 // 设置里程程息 、 剩余里程
        value = vehicle_get_param_unit() ;
        global_refresh_unit(value);

        // 显示主题：自动模式取中间层光感算出的实际模式
        global_refresh_display(vehicle_get_cur_display_mode() == VEHICLE_DISPLAY_MODE_NIGHT
                               ? DIAPLAY_NIGHT_OPTION : DIAPLAY_DAY_OPTION);

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
    
    home_refresh_battery_voltage(vehicle_get_battery_voltage());

    return  ;
}

void signal_view_update()
{
    for (size_t i = 0; i < ICON_SIGNAL_NUM_MAX; i++)
    {
        int32_t state =  vehicle_get_data_signal_lamp( i )  ;
        home_refresh_signal(i , state);
    }

    return ;
}



void timer_refresh_50_ms(lv_timer_t *info)
{
    (void)info ;
    
    if (checkself_get_state() != CHECK_STATE_FINISHED /*|| get_demonstration_state()*/ ) 
        return  ;
    
    button_manager_refresh();

    /* CAN 故障弹窗轮询: 在 WINDOW_MAIN 门控之前, 使任何界面都能弹出 */
    pop_tip_poll();

    /* 自动主题跟随光感: 传 AUTO 由 ui_theme_apply 解析实际模式,
     * 内部按已生效主题去重, 故仅在光感跨档时才真正切主题 */
    if (vehicle_get_param_display() == VEHICLE_DISPLAY_MODE_AUTO)
        global_refresh_display(DIAPLAY_AUTO_OPTION);

    int32_t value = vehicle_get_data_speed();
    speed_view_run(value) ;
    power_view_run((int)vehicle_get_data_power());

    if(WINDOW_MAIN != window_manager_get())
        return ;

    if(get_mileage_state())
    {
        global_refresh_mileage();
        set_mileage_state(false);
    }

    if(get_ref_mileage_state())
    {
        global_refresh_range();
        set_ref_mileage_state(false);
    }

    if (vehicle_get_mile_changed() && (vehicle_get_data_speed() == 0 ) )
        on_mileage_changed();
    
    // bluetooth_view_update();
    
    home_refresh_speed(speed_view_current()) ;
    home_refresh_speed_power((uint32_t)power_view_current());

    // value = vehicle_get_data_remain_battary();
    // home_refresh_electrical( value );

    /* 功率百分比（移植自 AWTK home_view） */
    // home_refresh_power((int)vehicle_get_data_power());

    /* 电量弧 + 电池指针：1/2/3 号包 SOC 各自独立刷新（BMS P1/P2/P3） */
    home_refresh_battery(vehicle_get_data_bat_soc(1),
                         vehicle_get_data_bat_soc(2),
                         vehicle_get_data_bat_soc(3));

    signal_view_update();

    /* 驾驶模式选中态跟随 CAN: 取 mode_index_e 索引（0 normal / 1 eco / 2 sport） */
    home_refresh_mode_index(vehicle_get_data_mode_index());

    /* 挡位显示: gear_e 数值即 CAN 原始编码 (raw0/1→P, 2→D, 3→R);
     * 非法值(负/超界)由 home_refresh_gear 兜底, 默认显示 GEAR_P */
    home_refresh_gear((gear_e)vehicle_get_data_gear());

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
