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
#define UI_VERSION          "V1.0.3"
#define DATA_SRC_PATH       "/data"
#define USE_CUSTOM_UI       (1)

///< 手机互联宽度和高度设置
#define CARLINK_EC_WIDTH    (1024)
#define CARLINK_EC_HEIGHT   (600)

///< 设置参数使能
#define MW_SET_PARAM_ENABLE 1

///< 里程参数使能
#define MILEAGE_PARAM_ENABLE 1

#if defined(RT_USING_PWM)
    ///< PWM背光使能(依赖 RT_USING_PWM)
    #define MW_BL_PW_ENABLE 1
#endif

#if defined(AIC_USING_PSADC)
    ///< 光感调节使能(依赖 PSADC)
    #define MW_LIGHT_SENSOR_ENABLE 1
    ///< 电压监控使能(依赖 PSADC)
    #define MW_BATTERY_MODULE_ENABLE 1
#endif

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

#if defined(RT_USING_WIFI)
    ///< 手机互联使能
    #define  MW_CARLINK_EC_ENABLE 0
    ///< 蓝牙模块使能(依赖 RT_USING_WIFI: 蓝牙控制器由 WiFi 驱动上电)
    ///< ble_navi(NimBLE) 与 GOC 蓝牙栈共用 UART2 上同一个 BT 控制器, 只能二选一:
    ///< 打开 LPKG_USING_BLE_NAVI 时 GOC 栈(含蓝牙胎压)不编译、不链接
    #if defined(LPKG_USING_BLE_NAVI)
        #define MW_BT_MODULE_ENABLE 0
    #else
        #define MW_BT_MODULE_ENABLE 1
    #endif
    ///< WiFi 测试命令使能（wifi_join/wifi_leave，配合 iperf 使用）
    #define WIFILESS_TEST_ENABLE 0
#endif

#if MW_BT_MODULE_ENABLE
    #define MW_BT_TPMS_ENABLE   1
#endif

///< io灯光使能
#define GPIO_LIGHT_ENABLE 0

#define AVG_CONSUMPTION_ENABLE 0

#define POWER_MANAGE_ENABLE 0
#if POWER_MANAGE_ENABLE
    #define POWER_LOW_SLEEP_ENABLE 0
    #define MW_POWER_SLEEP_LOG_ENABLE 0
    #define POWER_CAN_LOW_SLEEP_ENABLE 1
#endif

#define WIRELESS_UNLOCK_ENABLE 0

#define SPEED_PWM_ENABLE 0

///< CherryUSB host MSC 使能(依赖 host 驱动与 MSC class)
#if defined(LPKG_CHERRYUSB_HOST) && defined(LPKG_CHERRYUSB_HOST_MSC) && defined(AIC_USB_HOST_EHCI_DRV)
    #define MW_CHEERYUSB_ENABLE 1
#endif

///< USB 行车记录仪（DVR）协议与预览使能(依赖 MPP 解码/GE/显示)
#if defined(AIC_VE_DRV) && defined(AIC_GE_DRV) && defined(AIC_DE_DRV)
    #define ENABLE_BD_USB_DVR_FUNC 1
#endif

///< 看门狗使能（CAN/LVGL 心跳 + 独立喂狗线程）
#define MW_WDT_ENABLE 0

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __DEV_CONFIG_H__