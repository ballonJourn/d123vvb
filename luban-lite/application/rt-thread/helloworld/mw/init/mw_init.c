#include <rtthread.h>
#include <stdio.h>
#include "dev_state.h"
#include "mw_log.h"
#include "set_param.h"
#include "mw_can.h"
#include "mw_init.h"
#include "adc_key.h"
#include "gpio_light.h"
#include "mile_param.h"
#include "power_manage.h"
#include "433_wireless_unlock.h"
#include "speed_pwm.h"

int mw_init(void) {
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

    return 0;
}

INIT_APP_EXPORT(mw_init);
