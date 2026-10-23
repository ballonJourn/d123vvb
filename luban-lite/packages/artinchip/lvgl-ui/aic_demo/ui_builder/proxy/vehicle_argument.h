
#ifndef VEHICLE_ARGUMENT__H
#define VEHICLE_ARGUMENT__H

#include <stdint.h>
#include <stdbool.h>

/* 亮度等级取值（vehicle_get/set_param_brightness）：
 *   VEHICLE_BRIGHTNESS_AUTO = 自动，背光交中间层光感接管
 *   1..5                    = 手动档位 */
#define VEHICLE_BRIGHTNESS_AUTO   (0)

/* 显示主题取值（vehicle_get/set_param_display）：
 *   0 白天 / 1 黑夜 / 2 自动（自动时由中间层按光感决定实际主题） */
#define VEHICLE_DISPLAY_MODE_DAY    (0)
#define VEHICLE_DISPLAY_MODE_NIGHT  (1)
#define VEHICLE_DISPLAY_MODE_AUTO   (2)

/* 小计里程清零方式取值（vehicle_get/set_param_reset_type）：
 *   0 = 按键清零：只在主页长按 BACK 时清零
 *   1 = 下电清零：只在 IGN OFF（下电）时清零，长按 BACK 不响应 */
#define VEHICLE_RESET_BY_BUTTON        (0)
#define VEHICLE_RESET_AFTER_POWER_OFF  (1)

/**
 * @brief  读取usr param准备状态
 * @param  none
 * @return false:参数未准备好 true:已准备好
 */
bool vehicle_get_param_recovery();

//语言
uint8_t vehicle_get_param_language() ;

void vehicle_set_param_language(uint8_t value) ;


//单位
uint8_t vehicle_get_param_unit();

void vehicle_set_param_unit(uint8_t value);


//亮度等级  取值见 VEHICLE_BRIGHTNESS_AUTO / 1..5 手动档
uint8_t vehicle_get_param_brightness() ;

void vehicle_set_param_brightness(uint8_t value) ;

/**
 * @brief  亮度是否处于自动模式（背光由中间层光感接管）
 * @return true:自动 false:手动
 */
bool vehicle_get_brightness_is_auto() ;

/**
 * @brief  进入亮度自动模式
 */
void vehicle_set_brightness_auto() ;


//主题  取值见 VEHICLE_DISPLAY_MODE_*
uint8_t vehicle_get_param_display();

void vehicle_set_param_display(uint8_t value) ;

/**
 * @brief  当前实际生效的显示模式（主题为自动时取中间层光感结果）
 * @return VEHICLE_DISPLAY_MODE_DAY / VEHICLE_DISPLAY_MODE_NIGHT
 */
uint8_t vehicle_get_cur_display_mode() ;


//蓝牙开关
uint8_t vehicle_get_param_bluetooth();

void vehicle_set_param_bluetooth(uint8_t value);


//小计里程清零方式  取值见 VEHICLE_RESET_BY_BUTTON / VEHICLE_RESET_AFTER_POWER_OFF
uint8_t vehicle_get_param_reset_type() ;

void vehicle_set_param_reset_type(uint8_t value) ;

#endif