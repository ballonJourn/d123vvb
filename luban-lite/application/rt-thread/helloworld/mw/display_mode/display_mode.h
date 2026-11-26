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

#if MW_LIGHT_SENSOR_ENABLE

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

/**
 * @brief  下电低功耗: 删除光感采样线程
 * @param  none
 * @return none
 */
void display_mode_sleep(void);

/**
 * @brief  设置单个光感等级参考阈值
 * @param  index 阈值下标，0 ~ LIGHT_SENSOR_ARR_LEVEL-1，越大代表环境越亮
 * @param  value 阈值，必须保持整体严格递增，否则拒绝写入
 * @return 0 成功，-1 下标越界或破坏递增
 */
int display_mode_set_light_ref(int index, uint16_t value);

/**
 * @brief  读取全部光感等级参考阈值
 * @param  buf  输出缓冲
 * @param  len  buf 能容纳的元素个数
 * @return 实际写入个数，参数非法返回 -1
 */
int display_mode_get_light_ref(uint16_t *buf, int len);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __DISPLAY_MODE_H__