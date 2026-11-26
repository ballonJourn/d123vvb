#ifndef __USR_BACKLIGHT_H__
#define __USR_BACKLIGHT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "dev_config.h"

#if MW_BL_PW_ENABLE

#define PWM_BACKLIGHT_PERION (1000000)  ///< pwm背光周期1MHZ

typedef enum {
   BACKLIGHT_LEVEL_AUTO = 0x00,  ///< 等级自动
   BACKLIGHT_LEVEL_1, 
   BACKLIGHT_LEVEL_2,
   BACKLIGHT_LEVEL_3,
   BACKLIGHT_LEVEL_4,
   BACKLIGHT_LEVEL_5
} led_level_e;

void set_backlight_level(uint8_t level);

/**
 * @brief  当前实际生效的背光档位
 * @return BACKLIGHT_LEVEL_1..5；0 表示尚未设置过一档
 */
uint8_t get_backlight_level(void);

void init_backlight_pwm(void);

/**
 * @brief  init auto backlight
 * @param  level: backlight level
 * @return none
 */
void init_auto_backlight(uint8_t level);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __USR_BACKLIGHT_H__