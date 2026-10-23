
#include <rtthread.h>
#include <rtdevice.h>
#include "mile_param.h"
#include "set_param.h"
#include "vehicle_param.h"
#include "ign_check.h"
#include "power_manage.h"
#include "app_log.h"

#if POWER_MANAGE_ENABLE

#define IGN_SCAN_MS         100   
#define IGN_DEBOUNCE_MS     1000  
#define IGN_DEBOUNCE_COUNT   (IGN_DEBOUNCE_MS / IGN_SCAN_MS)  

#define IGN_ACTIVE_HIGH      1    
#if IGN_ACTIVE_HIGH
    #define IGN_ON_LEVEL      PIN_HIGH
    #define IGN_OFF_LEVEL     PIN_LOW
#else
    #define IGN_ON_LEVEL      PIN_LOW
    #define IGN_OFF_LEVEL     PIN_HIGH
#endif

#define IGN_TASK_STACK_SIZE  1024
#define IGN_TASK_PRIORITY    12   

static uint32_t power_pin = 0;
static uint32_t ign_det_pin = 0;
static uint32_t ign_power_ctrl = 0;

static ign_state_t g_ign_state = IGN_STATE_UNKNOWN;  
static bool is_power_on = false;                               

#if POWER_LOW_SLEEP_ENABLE
extern struct rt_event usr_pm_event;
#endif

static void ign_power_ctrl_off(void) {
    if (ign_power_ctrl == 0) {
        rt_log_error("ign power ctrl pin not init\n");
        return;
    }

    rt_pin_mode(ign_power_ctrl, PIN_MODE_OUTPUT);
    rt_pin_write(ign_power_ctrl, PIN_LOW);
}

static void power_enable(bool enable) {
    if (power_pin == 0) {
        rt_log_error("Power pin not init\n");
        return;
    }

    rt_pin_mode(power_pin, PIN_MODE_OUTPUT);
    rt_pin_write(power_pin, enable ? PIN_HIGH : PIN_LOW);
    is_power_on = enable;
}

///< 下电清零: 清零方式为"下电清零"时, 把 trip 写 0 并置"里程已变"标志。
///< 只写里程参数 + 标志, 不碰 LVGL —— 显示刷新由 UI 线程(50ms 定时器)按标志重读,
///< 本线程栈只有 1024 字节, 且此后系统随即休眠
static void reset_trip_on_power_off(void) {
    uint8_t reset_type = RESET_TYPE_BY_BUTTON;
    uint32_t trip = 0;

    if (!get_usr_param(USR_PARAM_RESET_TYPE, &reset_type)) {
        rt_log_error("Get reset_type error!\n");
        return;
    }

    if (reset_type != RESET_TYPE_AFTER_POWER_OFF) {
        return;
    }

    if (!set_mile_param(MILE_PARAM_TRIP, &trip)) {
        rt_log_error("Clear trip data error!\n");
        return;
    }

    ///< 1:trip a 已变 (ui_builder 侧据此重读并刷新显示)
    vehicle_set_data(VEH_MILEAGE_CHANGE_MSG, 1);
    rt_log_info("trip cleared on power off\n");
}

static void on_ign_state_changed(ign_state_t new_state) {
    if (new_state == IGN_STATE_ON) {
        if (!is_power_on) {
            //power_enable(true);
        }

#if POWER_LOW_SLEEP_ENABLE
         rt_event_send(&usr_pm_event, PM_EVENT_IGN_ON);
#endif

    } else if (new_state == IGN_STATE_OFF) {
        if (is_power_on) {
            ///< 下电清零: 先清零 trip 再落盘用户参数
            reset_trip_on_power_off();

            ///< 保存设置参数和里程参数
            if (shutdown_save_usr_param() == 0) {
                rt_log_info("dev will power off!\n");
                //ign_power_ctrl_off();
            } else {
                rt_log_info("try 2 times save set param failed, dev will power off after 300ms!\n");
                rt_thread_mdelay(300);
                //ign_power_ctrl_off();
            }
#if POWER_LOW_SLEEP_ENABLE
            rt_event_send(&usr_pm_event, PM_EVENT_IGN_OFF);
#endif
        }
    }
}

static void ign_scan_thread(void *param) {
    rt_tick_t last_tick = rt_tick_get();
    int pin_level;
    bool current_ign_on;
    static uint8_t debounce_counter = 0;                         

    while (1) {
        ///< 精确延时 100ms
        rt_thread_delay_until(&last_tick, rt_tick_from_millisecond(IGN_SCAN_MS));

        pin_level = rt_pin_read(ign_det_pin);
        if (pin_level == PIN_LOW)
            current_ign_on = (IGN_ON_LEVEL == PIN_LOW);
        else
            current_ign_on = (IGN_ON_LEVEL == PIN_HIGH);

        if (g_ign_state == IGN_STATE_UNKNOWN) {
            g_ign_state = current_ign_on ? IGN_STATE_ON : IGN_STATE_OFF;
            on_ign_state_changed(g_ign_state);
            debounce_counter = 0;
            continue;
        }

        bool stable_on = (g_ign_state == IGN_STATE_ON);
        if (current_ign_on == stable_on) {
            debounce_counter = 0;
        } else {
            debounce_counter++;
            if (debounce_counter >= IGN_DEBOUNCE_COUNT) {
                g_ign_state = current_ign_on ? IGN_STATE_ON : IGN_STATE_OFF;
                on_ign_state_changed(g_ign_state);
                debounce_counter = 0;
            }
        }
    }
}

static void power_pin_init(void) {
    power_pin = rt_pin_get(POWER_CTRL_PIN);
    if (power_pin == 0) {
        rt_log_error("Failed to get power pin %s\n", POWER_CTRL_PIN);
        return;
    }

    power_enable(true);
}

static void ign_power_ctrl_init(void) {
    ign_power_ctrl = rt_pin_get(IGN_POWER_CTRL);
    if (ign_power_ctrl == 0) {
        rt_log_error("Failed to get ign power ctrl pin %s\n", IGN_POWER_CTRL);
        return;
    }
}

static void ign_det_init(void) {
    ign_det_pin = rt_pin_get(IGN_DET_PIN);
    if (ign_det_pin == 0) {
        rt_log_error("Failed to get ign pin %s\n", IGN_DET_PIN);
        return;
    }
    rt_pin_mode(ign_det_pin, PIN_MODE_INPUT);
}

int ign_check_init(void) {
    ign_det_init();
    ign_power_ctrl_init();
    rt_thread_t ign_task = RT_NULL;
    ign_task = rt_thread_create("ign_det",
                                ign_scan_thread,
                                RT_NULL,
                                IGN_TASK_STACK_SIZE,
                                IGN_TASK_PRIORITY,
                                10);
    if (ign_task == RT_NULL) {
        rt_log_error("Failed to create IGN det task\n");
        return -1;
    }
    rt_thread_startup(ign_task);

    rt_log_info("IGN check module init success!\n");

    return 0;
}

ign_state_t get_ign_state(void) {
    return g_ign_state;
}

#endif