#include <rtthread.h>
#include "rtdevice.h"
#include "aic_core.h"
#include "aic_hal_gpio.h"
#include "dev_state.h"
#include "vehicle_param.h"
#include "app_log.h"
#include "gpio_light.h"

#if GPIO_LIGHT_ENABLE

///< io 刷新周期
#define VEH_IO_CHECK_PERIOD (100)

///< gpio探测脚
#define VEH_LEFT_TURN_DET_GPIO  ("PD.7")
#define VEH_RIGHT_TURN_DET_GPIO ("PD.6")
#define VEH_HEAD_LIGHT_DET_GPIO ("PD.5")
#define VEH_DAY_RUN_LIGHT_GPIO  ("PD.4")
#ifndef MW_CARLINK_EC_ENABLE
#define VEH_GB_T_DET_GPIO       ("PB.11")
#endif

typedef struct {
    const char *pin_name;
    uint32_t pin;
} gpio_ctrl_t;

#ifdef GPIO_FILTER_ENABLE

///< gpio过滤时间值
#define GPIO_FILTER_TIME (300)

///< gpio过滤计数值
#define GPIO_FILTER_CNT(period) (GPIO_FILTER_TIME / period)

static uint8_t gpio_last_value[6] = {0};
static uint8_t gpio_count[6] = {0};
#endif

static gpio_ctrl_t pin_ctrl[5] = {
    [0] = {VEH_LEFT_TURN_DET_GPIO, 0},
    [1] = {VEH_RIGHT_TURN_DET_GPIO, 0},
    [2] = {VEH_HEAD_LIGHT_DET_GPIO, 0},
    [3] = {VEH_DAY_RUN_LIGHT_GPIO, 0},
#ifndef MW_CARLINK_EC_ENABLE
    [4] = {VEH_GB_T_DET_GPIO, 0},
#endif
};

static void set_frame_light(uint32_t pin, int value) {
     if (pin == pin_ctrl[0].pin) {
        vehicle_set_data(VEH_INDICATOR_TURN_LEFT, !value);
     } else if (pin == pin_ctrl[1].pin) {
        vehicle_set_data(VEH_INDICATOR_TURN_RIGHT, !value);
     } else if (pin == pin_ctrl[2].pin) {
        vehicle_set_data(VEH_LIGHT_HIGH_BEAM, !value);
     } else if (pin == pin_ctrl[3].pin) {
        vehicle_set_data(VEH_AUTO_HEADLIGH, !value);
     } 
#ifndef MW_CARLINK_EC_ENABLE
     else if (pin == pin_ctrl[4].pin) {
        vehicle_set_data(VEH_LIGHT_GB, !value);
     }  
#endif
}

/**
 * @brief  检测外框灯
 * @param  pin IO编号
 * @param  io_index 过滤io序号
 * @param  is_filter 是否需要进行io过滤
 * @return none
 */
static void check_frame_light(uint32_t pin, int io_index, bool is_filter) {
    int value = rt_pin_read(pin);

    if (is_filter) {
        if (gpio_last_value[io_index] == value) {
            if (gpio_count[io_index] >= GPIO_FILTER_CNT(VEH_IO_CHECK_PERIOD)) {
                if (get_check_self_state() == CHECK_SELF_STATE_SUCCESS) {
                    set_frame_light(pin, value);
                }
            } else {
                gpio_count[io_index] += 1;
            }
        } else {
            gpio_last_value[io_index] = value;
            gpio_count[io_index] = 1;
        }
    } else {
        if (get_check_self_state() != CHECK_SELF_STATE_SUCCESS) {
            return;
        }

        set_frame_light(pin, value);
    }
}

static void scan_frame_light(void) {
      for (uint8_t i = 0; i < 5; i++) {
#ifdef MW_CARLINK_EC_ENABLE
        if (i == 4) {
            break;
        }
#endif
        check_frame_light(pin_ctrl[i].pin, i, false);
    }
}

static void set_light_gpio_input(void) {
    for (uint8_t i = 0; i < 5; i++) {
#ifdef MW_CARLINK_EC_ENABLE
        if (i == 4) {
            break;
        }
#endif
        pin_ctrl[i].pin = rt_pin_get(pin_ctrl[i].pin_name);
        rt_pin_mode(pin_ctrl[i].pin, PIN_MODE_INPUT);
    }
}

static void gpio_control_thread(void *parameter) {
    set_light_gpio_input();

    while (1) {
        rt_thread_mdelay(VEH_IO_CHECK_PERIOD);
        scan_frame_light();
    }
}

int light_gpio_init(void) {
    rt_thread_t thread;

    thread = rt_thread_create("gpio_control_thread", 
                           gpio_control_thread, 
                           RT_NULL, 1024, 
                           12, 10);
    if (thread != RT_NULL) {
        rt_thread_startup(thread);
    } else {
        rt_log_error("create can_rx thread failed!\n");
        return -1;
    }

    return 0;
}

#endif
