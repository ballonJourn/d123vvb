#ifndef __DEV_CONFIG_H__
#define __DEV_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include "rtconfig.h"

///< 型号相关配置
#define APP_VERSION_HEAD    "HCN_BB001_01_"
#define MANUFACTURER_NAME   "HCN"  
#define CUSTMOER_NAME       "HCN"
#define CAR_MODEL           "bb001"
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
#define MILEAGE_PARAM_ENABLE 0

///< PWM背光使能
#define MW_BL_PW_ENABLE   0     

#if MW_BL_PW_ENABLE
#ifndef AIC_PWM_BACKLIGHT_CHANNEL
#define AIC_PWM_BACKLIGHT_CHANNEL 1
#endif
#define LCD_PWM_CH  AIC_PWM_BACKLIGHT_CHANNEL
#endif

///< can功能使能
#define MW_CAN_ENABLE  1

///< adc按键使能
#define MW_ADC_KEY_ENABLE   0

#if MW_ADC_KEY_ENABLE
#define MW_ADC_KEY_CHANNEL  (3)
#endif

///< 手机互联使能
#define  MW_CARLINK_EC_ENABLE 1

///< io灯光使能
#define GPIO_LIGHT_ENABLE 0

#define AVG_CONSUMPTION_ENABLE 0

#define POWER_MANAGE_ENABLE 0

#define WIRELESS_UNLOCK_ENABLE 0

#define SPEED_PWM_ENABLE 0

#define ENABLE_BD_USB_DVR_FUNC 1

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __DEV_CONFIG_H__