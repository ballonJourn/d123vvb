#include <rtthread.h>
#include <rtdevice.h>
#include "display_mode.h"
#include "set_param.h"
#include "vehicle_param.h"
#include "light_sensor.h"
#include "dev_state.h"
#include "usr_backlight.h"
#include "app_log.h"

#ifdef MW_LIGHT_SENSOR_ENABLE

#define DISPLAY_MODE_PERIOD (50)
#define DISPLAY_MODE_STACK_SIZE (2048)
#define DISPLAY_MODE_PRIORITY   (30)
#define LIGHT_SENSOR_GET_DATA_INTERVAL (3)

#define DISPLAY_OPTION_DAY    1
#define DISPLAY_OPTION_NIGHT  2

typedef struct {
    int arr_index;
    int sample_interval;
    uint8_t is_first_display;
    uint8_t sensor_level;
    uint8_t cur_display_mode;
} display_param_t;

static display_param_t display = {0, 0, true, 0, 0xFF};

static uint16_t lg_ref[LIGHT_SENSOR_ARR_LEVEL];
static uint16_t lg_value[LIGHT_SENSOR_ARR_SIZE];

static void light_sensor_ref_init(void) {
    lg_ref[5] = 4060;
    lg_ref[4] = 2400;
    lg_ref[3] = 1500;
    lg_ref[2] = 700;
    lg_ref[1] = 100;
    lg_ref[0] = 0;
}

static void check_auto_backlight_level(void) {
    static uint8_t cur_level = 0;

    if (!get_recovery_usr_param() ||
        get_check_self_state() < CHECK_SELF_STATE_SUCCESS) {
        return;
    }

    if (!get_usr_param(USR_PARAM_BRIGHTNESS_LEVEL, &cur_level)) {
        rt_log_error("Get usr param backlight failed!");
        return;
    }

    if (cur_level == BACKLIGHT_LEVEL_AUTO) {
        // rt_log_info("BACKLIGHT_LEVEL_AUTO!!! sensor_level: %d\n", display.sensor_level);
        if (display.sensor_level > 1) {
            set_backlight_level(BACKLIGHT_LEVEL_5);
        } else {
            set_backlight_level(BACKLIGHT_LEVEL_1);
        }
    }
}

static void check_display_mode(void) {
    uint8_t display_mode = 0;

    if (!get_recovery_usr_param()) {
        return;
    }

    if (!get_usr_param(USR_PARAM_THEME, &display_mode)) {
        rt_log_error("Get usr param display mode failed!");
        return;
    }

    if (display_mode == AUTO_MODE) {
        int target_display = display.sensor_level > 1 ? DAY_MODE : NIGHT_MODE;
        if (display.cur_display_mode != target_display) {
            display.cur_display_mode = target_display;
            vehicle_set_data(VEH_CUR_DISPALY_MODE, display.cur_display_mode);
            rt_log_info("Current display mode: %s %d",
                   display.cur_display_mode == DAY_MODE ? "Day" : "Night",
                   display.cur_display_mode);
        }
    } else {
        if (display.cur_display_mode != display_mode) {
            display.cur_display_mode = display_mode;
            vehicle_set_data(VEH_CUR_DISPALY_MODE, display.cur_display_mode);
        }
    }
}

static void check_sensor_level(void) {
    if (is_acc_start() && display.is_first_display) {
        display.is_first_display = false;
        if (lg_value[0] >= lg_ref[1]) {
            display.sensor_level = 2;
        } else {
            display.sensor_level = 1;
        }
    } else {
        for (int i = 0; i < LIGHT_SENSOR_ARR_LEVEL - 1; i++) {
            int success_count = 0;

            for (int j = 0; j < LIGHT_SENSOR_ARR_SIZE; j++) {
                if (lg_value[j] > lg_ref[i] && lg_value[j] <= lg_ref[i + 1]) {
                    success_count++;
                } else {
                    break;
                }

                if (success_count == LIGHT_SENSOR_ARR_LEVEL - 1) {
                    display.sensor_level = i + 1;
                }
            }
        }
    }

    check_display_mode();
    check_auto_backlight_level();
}

static void check_light_sensor(void) {
    if ((display.sample_interval % LIGHT_SENSOR_GET_DATA_INTERVAL) == 0) {
        if (display.arr_index < LIGHT_SENSOR_ARR_SIZE) {
            lg_value[display.arr_index] = (uint16_t)get_light_sensor_value();
            display.arr_index++;
        }

        if (display.arr_index >= LIGHT_SENSOR_ARR_SIZE) {
            display.arr_index = 0;
        }

        check_sensor_level();
    }
    display.sample_interval++;
}

static void display_mode_thread(void *param) {
    light_sensor_ref_init();

    for (;;) {
        check_light_sensor();
        rt_thread_mdelay(DISPLAY_MODE_PERIOD);
    }
}

int display_mode_init(void) {
    rt_thread_t thread;

    /* 使能 PSADC 通道采样（psadc 设备经 INIT_DEVICE_EXPORT 已注册）*/
    light_sensor_init();

    thread = rt_thread_create("display_mode", display_mode_thread, RT_NULL,
                              DISPLAY_MODE_STACK_SIZE, DISPLAY_MODE_PRIORITY, 10);
    if (thread == RT_NULL) {
        rt_log_error("Failed to create display mode thread!");
        return -1;
    }
    rt_thread_startup(thread);

    rt_kprintf("display mode init success!\n");
    return 0;
}

#endif