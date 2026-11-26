
#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/pm.h>
#include "ign_check.h"
#include "power_manage.h"
#include "app_log.h"

#if POWER_MANAGE_ENABLE

///< 边沿中断事件驱动: 电平变化才进消抖, 平时不占 CPU/定时器
#define IGN_DEBOUNCE_MS     1000
#define IGN_SAMPLE_MS       10
#define IGN_DEBOUNCE_COUNT  (IGN_DEBOUNCE_MS / IGN_SAMPLE_MS)

#define IGN_ACTIVE_HIGH      0
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
static struct rt_semaphore s_ign_edge;
static volatile uint32_t s_edge_cnt = 0;

static void power_enable(bool enable) {
    if (power_pin == 0) {
        rt_log_error("Power pin not init\n");
        return;
    }

    rt_pin_mode(power_pin, PIN_MODE_OUTPUT);
    rt_pin_write(power_pin, enable ? PIN_HIGH : PIN_LOW);
    is_power_on = enable;
}

///< 本线程栈只有 1024 字节, 这里只做 IO 操作 + 投递事件:
///< 参数落盘/下电清零/进低功耗统一由 pm_thread(4096) 执行
static void on_ign_state_changed(ign_state_t new_state) {
    if (new_state == IGN_STATE_ON) {
        if (!is_power_on) {
            power_enable(true);
        }
        rt_event_send(&usr_pm_event, PM_EVENT_IGN_ON);
    } else if (new_state == IGN_STATE_OFF) {
        if (is_power_on) {
            rt_event_send(&usr_pm_event, PM_EVENT_IGN_OFF);
        }
    }
}

///< 中断里只做最少的事. 不能在 ISR 里读电平打印:
///< 唤醒瞬间 PM 还没 resume 设备, 控制台可能是挂起的
static void ign_irq_handler(void *args) {
    s_edge_cnt++;
    rt_sem_release(&s_ign_edge);
}

static bool ign_level_on(void) {
    return (rt_pin_read(ign_det_pin) == IGN_ON_LEVEL);
}

static void ign_scan_thread(void *param) {
    bool current_on;
    bool stable;
    uint32_t edges;
    int i;

    ///< 上电首扫: 没有边沿可等, 用当前电平确定初始状态
    current_on = ign_level_on();
    g_ign_state = current_on ? IGN_STATE_ON : IGN_STATE_OFF;
    on_ign_state_changed(g_ign_state);

    while (1) {
        rt_sem_take(&s_ign_edge, RT_WAITING_FOREVER);

        current_on = ign_level_on();
        if (current_on == (g_ign_state == IGN_STATE_ON))
            continue;       ///< 边沿抖动后回到原状态, 忽略

        edges = s_edge_cnt;

        ///< LIGHT 下 tickless 定时器是停摆的(PM 只认 rt_lptimer, 本工程没注册),
        ///< rt_thread_mdelay 依赖的 tick 不会前进. 先申请 IDLE 把系统抬到
        ///< "只 WFI、不改时钟、不挂设备" 的档位, 期间 systick 正常走, 消抖
        ///< 延时才可靠; 结束后释放, 系统回到 LIGHT
        rt_pm_module_request(PM_POWER_ID, PM_SLEEP_MODE_IDLE);

        stable = true;
        for (i = 0; i < IGN_DEBOUNCE_COUNT; i++) {
            rt_thread_mdelay(IGN_SAMPLE_MS);
            if (ign_level_on() != current_on) {
                stable = false;
                break;
            }
        }

        rt_pm_module_release(PM_POWER_ID, PM_SLEEP_MODE_IDLE);

        if (!stable) {
            rt_kprintf("[IGN] bounce, edges=%u, ignore\n",
                       (unsigned)(s_edge_cnt - edges));
            continue;
        }

        g_ign_state = current_on ? IGN_STATE_ON : IGN_STATE_OFF;
        rt_kprintf("[IGN] commit %s, edges=%u\n",
                   current_on ? "ON" : "OFF", (unsigned)(s_edge_cnt - edges));
        on_ign_state_changed(g_ign_state);
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

    if (rt_pin_attach_irq(ign_det_pin, PIN_IRQ_MODE_RISING_FALLING,
                          ign_irq_handler, RT_NULL) != RT_EOK) {
        rt_log_error("attach ign irq failed\n");
        return;
    }
    rt_pin_irq_enable(ign_det_pin, PIN_IRQ_ENABLE);

    ///< 必须登记为 PM 唤醒源: 否则每次 suspend, rt_pm_disable_pin_irq_nonwakeup()
    ///< 会把 PD.8 的边沿中断关掉, 休眠中再也不会产生中断
    rt_pm_set_pin_wakeup_source(ign_det_pin);
}

int ign_check_init(void) {
    rt_sem_init(&s_ign_edge, "ign_edge", 0, RT_IPC_FLAG_PRIO);

    ign_det_init();
    power_pin_init();

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
