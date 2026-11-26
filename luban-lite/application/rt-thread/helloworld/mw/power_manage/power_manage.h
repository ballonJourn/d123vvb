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

///< 电源管理事件: 检测线程(小栈)只投递, pm_thread(大栈)统一执行下电/低功耗
extern struct rt_event usr_pm_event;

int power_manage_init(void);

/**
 * @brief  复位前清理 framebuffer
 * @note   rt_hw_cpu_reset() 走看门狗复位, PSRAM 内容(含 framebuffer 最后一帧)
 *         会保留, 下次开机会透过开机动画的透明区显示成残影.
 *         CAN 唤醒和 IGN ON 唤醒的复位流程里都要调用.
 */
void power_pre_reset_cleanup(void);

#endif

/**
 * @brief  系统是否已进入下电流程
 * @return true:已进入 false:正常
 * @note   作为"下电中不再写参数"的闸门, 由 set_param / mile_param 的落盘入口查询.
 *         置位后除下电流程自身(走 force 路径)外, 任何落盘请求都会被丢弃
 */
bool power_is_off_pending(void);

/**
 * @brief  发起下电: 全系统唯一的下电入口, 幂等(重复调用只生效一次)
 * @note   执行顺序: 置闸门 -> 下电清零里程(是否清零由设置页"里程清零方式"
 *         USR_PARAM_RESET_TYPE 决定) -> 最后一次落盘.
 *         调用方不得再自行落盘或延时; 实际断电由 MCU 负责
 */
void power_off_request(void);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __POWER_MANAGE_H__