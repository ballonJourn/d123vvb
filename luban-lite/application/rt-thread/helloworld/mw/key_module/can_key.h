#ifndef __CAN_KEY_H__
#define __CAN_KEY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "dev_config.h"

#if MW_CAN_IO_KEY_ENABLE

/**
 * @brief  can 按键初始化
 * @param  none
 * @return 0:成功 -1:失败
 */
int can_key_init(void);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif