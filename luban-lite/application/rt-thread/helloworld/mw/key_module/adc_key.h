#ifndef __ADC_KEY_H__
#define __ADC_KEY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "dev_config.h"

#if MW_ADC_KEY_ENABLE

/**
 * @brief 按键按下状态枚举
 */
typedef enum {
    SHORT_PRESS,
    LONG_PRESS,
    SUPER_LONG_PRESS,
} key_press_mode_e;

/**
 * @brief 按键种类枚举
 */
typedef enum {
   NO_KEY_PRESS = 0,
   UP_KEY_PRESS = 1,
   DOWN_KEY_PRESS = 2,
   ENTER_KEY_PRESS = 3,
   BACK_KEY_PRESS = 4,
} key_press_e;

/**
 * @brief  adc按键初始化
 * @param  无
 * @return 0:成功 -1:失败 
 */
int adc_key_init(void);
#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __ADC_KEY_H__