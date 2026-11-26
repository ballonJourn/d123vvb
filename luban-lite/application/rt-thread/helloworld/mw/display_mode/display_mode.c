#include <rtthread.h>
#include <rtdevice.h>
#include "display_mode.h"
#include "set_param.h"
#include "vehicle_param.h"
#include "light_sensor.h"
#include "dev_state.h"
#include "usr_backlight.h"
#include "app_log.h"

#if MW_LIGHT_SENSOR_ENABLE

#define DISPLAY_MODE_PERIOD (50)
#define DISPLAY_MODE_STACK_SIZE (2048)
#define DISPLAY_MODE_PRIORITY   (30)
#define LIGHT_SENSOR_GET_DATA_INTERVAL (3)

#define DISPLAY_OPTION_DAY    1
#define DISPLAY_OPTION_NIGHT  2

#define DISPLAY_MSH_ENABLE 0

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

static rt_thread_t g_display_mode_tid = RT_NULL;

int display_mode_init(void) {
    /* 使能 PSADC 通道采样（psadc 设备经 INIT_DEVICE_EXPORT 已注册）*/
    light_sensor_init();

    g_display_mode_tid = rt_thread_create("display_mode", display_mode_thread, RT_NULL,
                                          DISPLAY_MODE_STACK_SIZE, DISPLAY_MODE_PRIORITY, 10);
    if (g_display_mode_tid == RT_NULL) {
        rt_log_error("Failed to create display mode thread!");
        return -1;
    }
    rt_thread_startup(g_display_mode_tid);

    rt_kprintf("display mode init success!\n");
    return 0;
}

///< 下电低功耗: 删光感采样线程(屏幕已关, 不再需要调背光/主题).
///< 唤醒走复位重启, 不需要恢复现场
void display_mode_sleep(void) {
    if (g_display_mode_tid != RT_NULL) {
        rt_thread_delete(g_display_mode_tid);
        g_display_mode_tid = RT_NULL;
    }
}

#if DISPLAY_MSH_ENABLE

#ifdef RT_USING_FINSH
#include <finsh.h>
#include <stdlib.h>

int display_mode_set_light_ref(int index, uint16_t value) {
    if (index < 0 || index >= LIGHT_SENSOR_ARR_LEVEL) {
        rt_log_error("Light ref index %d out of range [0, %d]",
                     index, LIGHT_SENSOR_ARR_LEVEL - 1);
        return -1;
    }

    /* lg_ref 必须严格递增，否则 check_sensor_level() 的区间判定失效 */
    if ((index > 0 && value <= lg_ref[index - 1]) ||
        (index < LIGHT_SENSOR_ARR_LEVEL - 1 && value >= lg_ref[index + 1])) {
        rt_log_error("Light ref[%d] = %d breaks ascending order", index, (int)value);
        return -1;
    }

    lg_ref[index] = value;
    return 0;
}

int display_mode_get_light_ref(uint16_t *buf, int len) {
    int count;

    if (buf == RT_NULL || len <= 0) {
        return -1;
    }

    count = len < LIGHT_SENSOR_ARR_LEVEL ? len : LIGHT_SENSOR_ARR_LEVEL;
    for (int i = 0; i < count; i++) {
        buf[i] = lg_ref[i];
    }

    return count;
}

static void light_ref_dump(void) {
    rt_kprintf("ref[0..%d] =", LIGHT_SENSOR_ARR_LEVEL - 1);
    for (int i = 0; i < LIGHT_SENSOR_ARR_LEVEL; i++) {
        rt_kprintf(" %d", lg_ref[i]);
    }
    rt_kprintf("\n");

    rt_kprintf("lg_value   =");
    for (int i = 0; i < LIGHT_SENSOR_ARR_SIZE; i++) {
        rt_kprintf(" %d", lg_value[i]);
    }
    rt_kprintf("\n");

    rt_kprintf("level      = %d\n", display.sensor_level);
}

/* 全量设置：先整体校验再写入，避免逐条设置时中间态破坏递增 */
static int light_ref_set_all(char **argv) {
    uint16_t vals[LIGHT_SENSOR_ARR_LEVEL];

    for (int i = 0; i < LIGHT_SENSOR_ARR_LEVEL; i++) {
        int v = atoi(argv[i + 1]);

        if (v < 0 || v > 0xFFFF) {
            rt_kprintf("value %d out of range [0, 65535]\n", v);
            return -1;
        }
        if (i > 0 && v <= vals[i - 1]) {
            rt_kprintf("values must be strictly ascending, ref[%d] = %d\n", i, v);
            return -1;
        }
        vals[i] = (uint16_t)v;
    }

    for (int i = 0; i < LIGHT_SENSOR_ARR_LEVEL; i++) {
        lg_ref[i] = vals[i];
    }

    return 0;
}

static void lg_ref_cmd(int argc, char **argv) {
    if (argc == 1) {
        light_ref_dump();
        return;
    }

    if (argc == 3) {
        int index = atoi(argv[1]);
        int value = atoi(argv[2]);

        if (value < 0 || value > 0xFFFF) {
            rt_kprintf("value %d out of range [0, 65535]\n", value);
            return;
        }
        if (display_mode_set_light_ref(index, (uint16_t)value) != 0) {
            rt_kprintf("set ref[%d] = %d failed: index out of range "
                       "or breaks ascending order\n", index, value);
            return;
        }
        rt_kprintf("ref[%d] = %d\n", index, value);
        return;
    }

    if (argc == LIGHT_SENSOR_ARR_LEVEL + 1) {
        if (light_ref_set_all(argv) == 0) {
            light_ref_dump();
        }
        return;
    }

    rt_kprintf("usage:\n"
               "  lg_ref                      show thresholds / samples / level\n"
               "  lg_ref <index> <value>      set one, index 0 ~ %d\n"
               "  lg_ref <v0> ... <v%d>  set all, strictly ascending\n",
               LIGHT_SENSOR_ARR_LEVEL - 1, LIGHT_SENSOR_ARR_LEVEL - 1);
}
MSH_CMD_EXPORT_ALIAS(lg_ref_cmd, lg_ref, tune light sensor level thresholds);

#endif /* RT_USING_FINSH */
#endif /* DISPLAY_MSH_ENABLE */

#endif