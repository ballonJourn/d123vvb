#include <rtthread.h>
#include <rtdevice.h>
#include "app_log.h"
#include "power_manage.h" 
#include "ign_check.h"

#if POWER_MANAGE_ENABLE

struct rt_event usr_pm_event;
static void pm_thread(void *parameter) {
    rt_uint32_t event;
    rt_uint8_t current_mode;

    while (1) {
        if (rt_event_recv(&usr_pm_event,
                               PM_EVENT_IGN_OFF | PM_EVENT_IGN_ON | PM_EVENT_WAKEUP,
                               RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                               RT_WAITING_FOREVER,
                               &event) == RT_EOK) {

            current_mode = rt_pm_get_sleep_mode();
            if (current_mode != PM_SLEEP_MODE_NONE) {
                if (event & PM_EVENT_IGN_ON) {
                    rt_kprintf("PM: Received IGN ON event.\n");
                    rt_pm_module_request(PM_POWER_ID, PM_SLEEP_MODE_NONE);
                }
            } else {
                if (event & PM_EVENT_IGN_OFF) {
                    rt_kprintf("PM: Received IGN OFF event.\n");
                    rt_pm_module_release(PM_POWER_ID, PM_SLEEP_MODE_NONE);
                }
            }
        } 
    }
}

int power_manage_init(void) {
    rt_err_t ret;
    rt_thread_t thread;

    if (ign_check_init() != 0) {
        rt_log_error("Failed to initialize IGN check module!\n");
        return -1;
    }

    ret = rt_event_init(&usr_pm_event, "usr_pm_event", RT_IPC_FLAG_PRIO);
    if (ret != RT_EOK) {
        rt_log_error("Failed to initialize power manage event!\n");
        return -1;
    }

    thread = rt_thread_create("pm_thread", pm_thread, RT_NULL,
                              2048, 30, 10);
    if (thread != RT_NULL) {
        rt_thread_startup(thread);
    } else{
        rt_log_error("Failed to create power manage thread!\n");
        return -1;
    }

    return 0;
}

#endif
