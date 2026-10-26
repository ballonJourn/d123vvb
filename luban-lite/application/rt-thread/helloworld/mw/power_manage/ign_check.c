
#include <rtthread.h>
#include <rtdevice.h>
#include "mile_param.h"
#include "set_param.h"
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

static ign_state_t g_ign_state = IGN_STATE_UNKNOWN;  
static bool is_power_on = false;                               

extern struct rt_event usr_pm_event;

static void power_enable(bool enable) {
    if (power_pin == 0) {
        rt_log_error("Power pin not init\n");
        return;
    }

    rt_pin_mode(power_pin, PIN_MODE_OUTPUT);
    rt_pin_write(power_pin, enable ? PIN_HIGH : PIN_LOW);
    is_power_on = enable;
}

static void on_ign_state_changed(ign_state_t new_state) {
    if (new_state == IGN_STATE_ON) {
        if (!is_power_on) {
            power_enable(true);
        }

         rt_event_send(&usr_pm_event, PM_EVENT_IGN_ON);

    } else if (new_state == IGN_STATE_OFF) {
        if (is_power_on) {
            #if 0
            rt_thread_mdelay(2000);   
            #else
            ///< 保存设置参数和里程参数
            #endif
            //power_enable(false);
            rt_event_send(&usr_pm_event, PM_EVENT_IGN_OFF);
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

static void ign_det_init(void) {
    ign_det_pin = rt_pin_get(IGN_DET_PIN);
    if (ign_det_pin == 0) {
        rt_log_error("Failed to get ign pin %s\n", IGN_DET_PIN);
        return;
    }
    rt_pin_mode(ign_det_pin, PIN_MODE_INPUT);
}

int ign_check_init(void) {
    power_pin_init();
    ign_det_init();

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