#ifndef __DEV_CONFIG_H__
#define __DEV_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include "rtconfig.h"

///< 型号相关配置
#define APP_VERSION_HEAD    "HCN_BE001_01_"
#define MANUFACTURER_NAME   "HCN"  
#define CUSTMOER_NAME       "HCN"
#define CAR_MODEL           "be001"
#define CONFIG_PARAM_VER    "v001"
#define UI_VERSION          "V1.0.0"
#define DATA_SRC_PATH       "/data"
#define USE_CUSTOM_UI       (1)

///< 手机互联宽度和高度设置
#define CARLINK_EC_WIDTH    (1024)
#define CARLINK_EC_HEIGHT   (600)

///< 设置参数使能
#define MW_SET_PARAM_ENABLE 1

///< 里程参数使能
#define MILEAGE_PARAM_ENABLE 1

///< PWM背光使能
#define MW_BL_PW_ENABLE   1

///< 光感调节使能
#define MW_LIGHT_SENSOR_ENABLE 1   

#if MW_BL_PW_ENABLE
#ifndef AIC_PWM_BACKLIGHT_CHANNEL
#define AIC_PWM_BACKLIGHT_CHANNEL 7
#endif
#define LCD_PWM_CH  AIC_PWM_BACKLIGHT_CHANNEL
#endif

///< can功能使能
#define MW_CAN_ENABLE  1
#if MW_CAN_ENABLE
#define MW_CAN_IO_KEY_ENABLE 1
#endif

///< adc按键使能
#define MW_ADC_KEY_ENABLE   0
#define MW_IO_KEY_ENABLE    1

#if MW_ADC_KEY_ENABLE
#define MW_ADC_KEY_CHANNEL  (3)
#endif

#if MW_ADC_KEY_ENABLE && MW_IO_KEY_ENABLE
#error "can not enable io key and adc key!"
#endif

///< 手机互联使能
#define  MW_CARLINK_EC_ENABLE 0

///< 蓝牙模块使能
#define MW_BT_MODULE_ENABLE 1
#if MW_BT_MODULE_ENABLE
#define MW_BT_TPMS_ENABLE   1
#endif

///< io灯光使能
#define GPIO_LIGHT_ENABLE 1

#define AVG_CONSUMPTION_ENABLE 0

#define POWER_MANAGE_ENABLE 1
#if POWER_MANAGE_ENABLE
#define POWER_LOW_SLEEP_ENABLE 0
#endif

#define WIRELESS_UNLOCK_ENABLE 0

#define SPEED_PWM_ENABLE 0

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __DEV_CONFIG_H__