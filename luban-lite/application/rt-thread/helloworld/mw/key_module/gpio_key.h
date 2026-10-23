#ifndef __GPIO_KEY_H__
#define __GPIO_KEY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "dev_config.h"

#if MW_IO_KEY_ENABLE

#define KEY_UP        (1)
#define KEY_DOWN      (0)

///< 按键引脚名(字符串形式, 按当前规则由 rt_pin_get() 解析)
#define KEY_UP_GPIO     ("PC.8")
#define KEY_MODE_GPIO   ("PC.9")
#define KEY_SET_GPIO    ("PC.10")
#define KEY_BACK_GPIO   ("PC.11")

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
 * @brief  gpio 按键初始化
 * @param  none
 * @return 0:成功 -1:失败 
 */
int gpio_key_init(void);


#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif