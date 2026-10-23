
#include <rtthread.h>
#include <rtdevice.h>
#include <stdbool.h>
#include "gpio_key.h"
#include "key_common.h"
#include "dev_config.h"
#include "app_log.h"

#if MW_IO_KEY_ENABLE

#define SCAN_KEY_THREAD_INTERVAL_PERIOD (20)   // 扫描周期，单位 ms

#define SCAN_KEY_DEFIBRATE_TIME (60)           // 消抖时间 60ms
#define GPIO_KEY_LONG_PRESS_TIME (2500)        // 长按 2500ms
#define GPIO_KEY_SUPER_LONG_PRESS_TIME (8000)  // 超长按 8000ms

#define SCAN_KEY_DEFIBRATE_INDEX \
    (SCAN_KEY_DEFIBRATE_TIME / SCAN_KEY_THREAD_INTERVAL_PERIOD)
#define GPIO_KEY_LONG_PRESS_INDEX \
    (GPIO_KEY_LONG_PRESS_TIME / SCAN_KEY_THREAD_INTERVAL_PERIOD)
#define GPIO_KEY_SUPER_LONG_PRESS_INDEX \
    (GPIO_KEY_SUPER_LONG_PRESS_TIME / SCAN_KEY_THREAD_INTERVAL_PERIOD)

typedef struct {
    uint32_t defibrate_time;
    uint32_t scan_key_time;
    bool is_real_key_press;
    bool is_long_press;
} scan_key_time_t;

static scan_key_time_t scan_key = {0, 0, false, false};
static uint8_t key_up_flag = 1;
static uint8_t key_status = NO_KEY_PRESS;

///< 由 rt_pin_get() 解析按键引脚名得到的引脚编号, 供 rt_pin_read() 读取电平
static uint32_t key_up_pin   = 0xFFFFFFFF;
static uint32_t key_mode_pin = 0xFFFFFFFF;
static uint32_t key_set_pin  = 0xFFFFFFFF;
static uint32_t key_back_pin = 0xFFFFFFFF;

#define UP_KEY    rt_pin_read(key_up_pin)
#define DOWN_KEY  rt_pin_read(key_mode_pin)
#define ENTER_KEY rt_pin_read(key_set_pin)
#define BACK_KEY  rt_pin_read(key_back_pin)

///< 按键引脚配置为输入, 遵循当前 gpio_light.c 的规则
static void configure_key_gpio_input(void) {
    key_up_pin   = rt_pin_get(KEY_UP_GPIO);
    key_mode_pin = rt_pin_get(KEY_MODE_GPIO);
    key_set_pin  = rt_pin_get(KEY_SET_GPIO);
    key_back_pin = rt_pin_get(KEY_BACK_GPIO);

    rt_pin_mode(key_up_pin, PIN_MODE_INPUT);
    rt_pin_mode(key_mode_pin, PIN_MODE_INPUT);
    rt_pin_mode(key_set_pin, PIN_MODE_INPUT);
    rt_pin_mode(key_back_pin, PIN_MODE_INPUT);
}

static void check_key_status(uint8_t key_status, uint8_t mode) {
    uint8_t key_value = 0;

    switch (key_status) {
        case UP_KEY_PRESS:
            key_value = mode ? UP_KEY_LONG_PR : UP_KEY_SHORT_PR;
            // rt_kprintf("%s\n", mode ? "up key long press" : "up key short press");
            send_key_event(key_value);
            break;

        case DOWN_KEY_PRESS:
            key_value = mode ? MODE_KEY_LONG_PR : MODE_KEY_SHORT_PR;
            // rt_kprintf("%s\n",
            //            mode ? "down key long press" : "down key short press");
            send_key_event(key_value);
            break;

        case ENTER_KEY_PRESS:
            if (mode == SHORT_PRESS) {
                key_value = SET_KEY_SHORT_PR;
                // rt_kprintf("enter key short press\n");
            } else if (mode == LONG_PRESS) {
                key_value = SET_KEY_LONG_PR;
                // rt_kprintf("enter key long press\n");
            } else if (mode == SUPER_LONG_PRESS) {
                key_value = SET_KEY_SUPER_LONG_PR;
                // rt_kprintf("enter key super long press\n");
            }
            send_key_event(key_value);
            break;

        case BACK_KEY_PRESS:
            key_value = mode ? BACK_KEY_LONG_PR : BACK_KEY_SHORT_PR;
            // rt_kprintf("%s\n",
            //            mode ? "back key long press" : "back key short press");
            send_key_event(key_value);
            break;

        default:
            break;
    }
}

static void scan_gpio_key(void) {
    if (key_up_flag && (!UP_KEY || !DOWN_KEY || !ENTER_KEY || !BACK_KEY)) {
        if (scan_key.defibrate_time <= SCAN_KEY_DEFIBRATE_INDEX - 1) {
            scan_key.defibrate_time++;
        }

        if (scan_key.defibrate_time >= SCAN_KEY_DEFIBRATE_INDEX) {
            key_up_flag = 0;
            scan_key.is_real_key_press = true;
        }
    }

    if (scan_key.is_real_key_press &&
        (!UP_KEY || !DOWN_KEY || !ENTER_KEY || !BACK_KEY)) {
        scan_key.scan_key_time++;
        if (!UP_KEY) {
            key_status = UP_KEY_PRESS;
        } else if (!DOWN_KEY) {
            key_status = DOWN_KEY_PRESS;
        } else if (!ENTER_KEY) {
            key_status = ENTER_KEY_PRESS;
        } else if (!BACK_KEY) {
            key_status = BACK_KEY_PRESS;
        }

        if ((scan_key.scan_key_time >= GPIO_KEY_LONG_PRESS_INDEX) &&
            (!scan_key.is_long_press)) {
            scan_key.is_long_press = true;

            // enter 键特殊处理，不置 real_key_press 标志
            if (key_status != ENTER_KEY_PRESS) {
                scan_key.is_real_key_press = false;
            } 
            check_key_status(key_status, LONG_PRESS);
            key_status = NO_KEY_PRESS;
        }

        // enter 键的超长按
        if (key_status == ENTER_KEY_PRESS && 
            (scan_key.scan_key_time >= GPIO_KEY_SUPER_LONG_PRESS_INDEX)
            && (scan_key.is_long_press)) {
            scan_key.is_long_press = false;
            scan_key.is_real_key_press = false;
            check_key_status(key_status, SUPER_LONG_PRESS); 
            key_status = NO_KEY_PRESS;
        }
    }

    if (UP_KEY && DOWN_KEY && ENTER_KEY && BACK_KEY) {
        key_up_flag = 1;
        scan_key.defibrate_time = 0;
        scan_key.is_long_press = false;
        scan_key.is_real_key_press = false;

        // 处理 enter 键的释放逻辑
        if (key_status == ENTER_KEY_PRESS) {
             if (scan_key.scan_key_time < GPIO_KEY_LONG_PRESS_INDEX) {
                check_key_status(key_status, SHORT_PRESS);
             }
        }

        if (key_status != ENTER_KEY_PRESS) {
            if (scan_key.scan_key_time < GPIO_KEY_LONG_PRESS_INDEX) {
                if (key_status > NO_KEY_PRESS && key_status <= BACK_KEY_PRESS) {
                    check_key_status(key_status, SHORT_PRESS);
                }
            }
        }
     
        scan_key.scan_key_time = 0;
        key_status = NO_KEY_PRESS;
    }
}

static void gpio_key_thread(void *param) {
    configure_key_gpio_input();
    for (;;) {
        scan_gpio_key();
        rt_thread_mdelay(SCAN_KEY_THREAD_INTERVAL_PERIOD);
    }
}

int gpio_key_init(void) {
    rt_thread_t tid;

    tid = rt_thread_create("gpio_key",
                           gpio_key_thread,
                           RT_NULL,
                           2048,          // 堆栈大小，可根据编译情况调整
                           20,            // 优先级，数值越小优先级越高
                           10);           // 时间片
    if (tid != RT_NULL) {
        rt_thread_startup(tid);
    } else {
        rt_log_error("Create gpio_key_thread failed.\n");
        return -1;
    }

    rt_log_info("gpio key init ok!\n");

    return 0;
}

#endif