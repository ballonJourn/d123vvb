#ifndef __BATTERY_MODULE_H__
#define __BATTERY_MODULE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

int battery_module_init(void);

/**
 * @brief  下电低功耗: 删除电池采样线程
 */
void battery_module_sleep(void);

/**
 * @brief  当前是否处于高低压保护态
 * @return true: 电压超限, 已关屏关外设, 等待电压恢复后复位重启
 */
bool battery_is_voltage_protected(void);

/**
 * @brief  保护关屏: 面板 POWEROFF + 关背光 PWM
 * @note  可在 LVGL 线程内直接调用(内部不做 LVGL pause), 供开机自检时
 *        在 UI 创建之前抢先把屏关掉
 */
void battery_protect_screen_off(void);

#ifdef __cplusplus
}
#endif

#endif //__BATTERY_MODULE_H__