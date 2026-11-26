#ifndef __GPIO_LIGHT_H__
#define __GPIO_LIGHT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include "dev_config.h"

#if GPIO_LIGHT_ENABLE

#define GPIO_FILTER_ENABLE

typedef enum {
    FRAME_LIGHT_OFF,
    FRAME_LIGHT_ON,
} led_state_e;

int light_gpio_init(void);

/**
 * @brief  下电低功耗: 删除 IO 轮询线程
 */
void light_gpio_sleep(void);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __GPIO_LIGHT_H__