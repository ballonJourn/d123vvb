#ifndef __MW_WDT_H__
#define __MW_WDT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rtthread.h>
#include "dev_config.h"

///< 心跳点: 由对应业务线程周期性打点, 停止打点即判定该业务卡死
typedef enum {
    MW_WDT_HB_CAN = 0,      ///< CAN 业务线程 (mw_can.c::can_timeout_thread)
    MW_WDT_HB_LVGL,         ///< LVGL 线程 (logic.c::timer_refresh_50_ms / 开机动画定时器)
    MW_WDT_HB_NUM
} mw_wdt_hb_e;

/**
 * @brief  看门狗模块初始化: 查找 wdt 设备并创建喂狗线程 (幂等)
 * @return 0 成功; -1 失败 (只记日志, 由调用方决定是否阻断整机)
 * @note   线程启动后延时 3s 才使能看门狗, 给系统初始化留余量。
 *         超时 15s, 喂狗周期 5s, 心跳有效期 12s;
 *         任一心跳点超过有效期未打点, 喂狗线程停止喂狗, 由硬件复位。
 *         MW_WDT_ENABLE=0 时 mw_init 不调用本函数, 由 CAN 0x777
 *         (TripB == 111) 按需拉起; 重复调用直接返回, 不会重复建线程。
 */
int mw_wdt_init(void);

/**
 * @brief  心跳打点
 * @param  id 心跳点, 见 mw_wdt_hb_e
 * @note   任意线程/中断上下文可调用, 只有一次 32 位写, 开销可忽略。
 *         某点第一次调用后即被激活, 未激活的点不参与判活。
 */
void mw_wdt_feed(mw_wdt_hb_e id);

/**
 * @brief  暂停看门狗计数
 * @note   当前 POWER_LOW_SLEEP_ENABLE=0 不涉及休眠路径。
 *         将来启用低功耗时, 必须在 mw_can_light_sleep()/mw_can_sleep()
 *         之前调用本函数: 那两个接口会删除 CAN 业务线程, CAN 心跳随之
 *         停止, 不停狗会在 12s 后触发复位。恢复用 mw_wdt_start()。
 */
void mw_wdt_stop(void);

/**
 * @brief  下电低功耗: 停狗并删除喂狗线程
 * @note   必须在删除 CAN/LVGL 业务线程(心跳点)之前调用, 否则喂狗线程会在
 *         心跳超时后主动停喂, 触发硬件复位. 未启动过看门狗时为空操作。
 */
void mw_wdt_sleep(void);

/**
 * @brief  重新启动看门狗 (超时 15s)
 * @return 0 成功; 负值失败
 */
int mw_wdt_start(void);

#ifdef __cplusplus
}
#endif

#endif /* __MW_WDT_H__ */
