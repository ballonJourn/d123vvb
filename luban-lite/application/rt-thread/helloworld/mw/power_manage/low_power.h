#ifndef __LOW_POWER_H__
#define __LOW_POWER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rtthread.h>
#include "dev_config.h"

#if POWER_MANAGE_ENABLE

/**
 * @brief  低功耗模块初始化: 建唤醒信号量与处理线程
 * @return 0 成功, 非 0 失败
 * @note   需在 PM 框架初始化之后调用
 */
int low_power_init(void);

/**
 * @brief  进入低功耗状态
 * @return 0 已进入, 非 0 失败
 * @note   执行顺序:
 *           1. CAN 轻休眠(停业务线程, 保留设备+中断, 控制器进 Sleep Mode)
 *           2. 标记低功耗态, 此后 CAN 唤醒中断会触发复位重启
 *           3. 释放 PM_SLEEP_MODE_NONE, idle 线程随即进入休眠
 *         本函数**不阻塞**——休眠发生在 idle 线程里, 唤醒后由
 *         low_power 线程统一走 rt_hw_cpu_reset() 重启, 不返回。
 */
int low_power_enter(void);

/**
 * @brief  当前是否处于低功耗态
 */
rt_bool_t low_power_is_active(void);

#endif /* POWER_MANAGE_ENABLE */

#ifdef __cplusplus
}
#endif //__cplusplus

#endif /* __LOW_POWER_H__ */
