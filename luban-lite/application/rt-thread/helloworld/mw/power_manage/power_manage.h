#ifndef __POWER_MANAGE_H__
#define __POWER_MANAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>
#include <stdbool.h>
#include "dev_config.h"

#if POWER_MANAGE_ENABLE

#define PM_EVENT_IGN_OFF    (1 << 0)  ///< 事件标志：IGN关闭
#define PM_EVENT_IGN_ON     (1 << 1)  ///< 事件标志：IGN开启
#define PM_EVENT_WAKEUP     (1 << 2)  ///< 事件标志：系统被唤醒（可能来自按键或其它唤醒源）

int power_manage_init(void);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __POWER_MANAGE_H__