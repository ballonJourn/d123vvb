/*
 * display_mode.h - 光感自动调节背光等级与 UI 主题的中间层
 *
 * 依赖：light_sensor（光敏采样）+ usr_backlight（背光等级）+ ui_theme（LVGL 主题）
 * 开关：dev_config.h 中 MW_LIGHT_SENSOR_ENABLE
 */
#ifndef __DISPLAY_MODE_H__
#define __DISPLAY_MODE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "dev_config.h"

#ifdef MW_LIGHT_SENSOR_ENABLE

#define LIGHT_SENSOR_ARR_LEVEL  (6)
#define LIGHT_SENSOR_ARR_SIZE   (5)

typedef enum {
    DAY_MODE = 0x00,
    NIGHT_MODE = 0x01,
    AUTO_MODE  = 0x02
} display_mode_e;

/**
 * @brief  显示模式初始化
 * @param  none
 * @return none
 */
int display_mode_init(void);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __DISPLAY_MODE_H__