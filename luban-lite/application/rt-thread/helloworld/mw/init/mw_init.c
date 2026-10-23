#include <rtthread.h>
#include <stdio.h>
#include "dev_state.h"
#include "mw_log.h"
#include "set_param.h"
#include "mw_can.h"
#include "mw_init.h"
#include "adc_key.h"
#include "gpio_light.h"
#include "gpio_key.h"
#include "can_key.h"
#include "mile_param.h"
#include "power_manage.h"
#include "433_wireless_unlock.h"
#include "speed_pwm.h"
#include "display_mode.h"
#include "usr_utils.h"
#include "bt_module.h"
#include "bt_tpms.h"

#define LVDS_RST     "PD.16"   
#define LVDS_STBYB   "PD.15"   
#define LVDS_BK_PIN  "PD.14"
#define WIFI_BT_ON_PIN  "PD.3"  

static uint32_t lvds_rst_pin = 0;
static uint32_t lvds_stbyb_pin = 0;
static uint32_t bk_pin = 0;
static uint32_t wifi_bt_pin = 0;

static void lvds_power_ctrl(void) {
    lvds_rst_pin = rt_pin_get(LVDS_RST);
    if (lvds_rst_pin == 0) {
        mw_loge("Failed to get lvds rst pin %s\n", LVDS_RST);
        return;
    }
    rt_pin_mode(lvds_rst_pin, PIN_MODE_OUTPUT);
    rt_pin_write(lvds_rst_pin, PIN_HIGH);
    rt_thread_mdelay(10);
    lvds_stbyb_pin = rt_pin_get(LVDS_STBYB);
    if (lvds_stbyb_pin == 0) {
        mw_loge("Failed to get lvds stbyb pin %s\n", LVDS_STBYB);
        return;
    }
    rt_pin_mode(lvds_stbyb_pin, PIN_MODE_OUTPUT);
    rt_pin_write(lvds_stbyb_pin, PIN_HIGH);
    rt_thread_mdelay(100);
    bk_pin = rt_pin_get(LVDS_BK_PIN);
    if (bk_pin == 0) {
        mw_loge("Failed to get lvds stbyb pin %s\n", LVDS_BK_PIN);
        return;
    }
    rt_pin_mode(bk_pin, PIN_MODE_OUTPUT);
    rt_pin_write(bk_pin, PIN_HIGH);
}

static void wifi_bt_on(void)
{
    wifi_bt_pin = rt_pin_get(WIFI_BT_ON_PIN);
    if (wifi_bt_pin == 0) {
        mw_loge("Failed to get WIFI_BT_ON_PIN %s\n", WIFI_BT_ON_PIN);
        return;
    }
    rt_pin_mode(wifi_bt_pin, PIN_MODE_OUTPUT);
    rt_pin_write(wifi_bt_pin, PIN_HIGH);
}

int mw_init(void) {
    // lvds_power_ctrl();
    // wifi_bt_on();
    soc_version_init();

#if POWER_MANAGE_ENABLE
    if (power_manage_init() != 0) {
        mw_loge("power manage init error!\n");
        return -1;
    }
#endif

#if MW_SET_PARAM_ENABLE
    if (usr_param_init() != 0) {
        mw_loge("usr param init error!\n");
        return -1;
    }
#endif

#if MW_LIGHT_SENSOR_ENABLE
    if (display_mode_init() != 0) {
        mw_loge("mw display mode init error!\n");
        goto init_error;
    }

    if (battery_module_init() != 0) {
        mw_loge("mw battery_module_init init error!\n");
        goto init_error;
    }
#endif

#if MILEAGE_PARAM_ENABLE
    if (mile_param_init() != 0) {
        mw_loge("mile param init error!\n");
        return -1;
    } 
#endif

#if MW_CAN_ENABLE
    if (mw_can_init() != 0) {
        mw_loge("mw can init error!\n");
        return -1;
    }
#endif

#if MW_ADC_KEY_ENABLE
    if (adc_key_init() != 0) {
        mw_loge("mw adc key init error!\n");
        return -1;
    }
#endif

#if MW_IO_KEY_ENABLE
    if (gpio_key_init() != 0) {
        mw_loge("mw io key init error!\n");
        goto init_error;
    }
#endif

#if MW_CAN_IO_KEY_ENABLE
    if (can_key_init() != 0) {
        mw_loge("mw can io key init error!\n");
        return -1;
    }
#endif

#if GPIO_LIGHT_ENABLE
    if (light_gpio_init() != 0) {
        mw_loge("mw light gpio init error!\n");
        return -1;
    }
#endif

#if WIRELESS_UNLOCK_ENABLE
    if (wireless_unlock_init() != 0) {
        mw_loge("wireless unlock init error!\n");
        return -1;
    }
#endif

#if SPEED_PWM_ENABLE
    if (speed_pwm_init() != 0) {
        mw_loge("speed pwm init error!\n");
        return -1;
    }
#endif

#if MW_CARLINK_EC_ENABLE
    extern void start_carlink(void);
    start_carlink();
#endif

#if MW_BT_MODULE_ENABLE
    /* 蓝牙非必要功能, 起不来只记日志, 不阻断整机初始化 */
    if (bt_module_init() != 0) {
        mw_loge("mw bt module init error!\n");
    }
#endif

#if MW_BT_TPMS_ENABLE
    /* 蓝牙胎压: 只起解析线程/定时器并恢复配对状态, 不依赖蓝牙栈就绪 */
    if (bt_tpms_task_init() != 0) {
        mw_loge("mw bt tpms init error!\n");
    }
#endif

    return 0;
init_error:
    return -1;
}

INIT_APP_EXPORT(mw_init);
