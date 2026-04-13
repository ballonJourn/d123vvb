#ifndef __AVG_CONSUMPTION_H__
#define __AVG_CONSUMPTION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <rtthread.h>
#include "dev_config.h"

#if AVG_CONSUMPTION_ENABLE
/**
 * @brief 初始化平均电耗计算器
 * @details 启动采样定时器，并尝试从Flash加载历史数据。
 * @return int RT_EOK on success, others on failure.
 */
int avg_consumption_init(void);

/**
 * @brief 获取系统累计平均电耗
 * @return double 平均电耗，单位 Wh/km。如果里程为0，返回0。
 */
double avg_consumption_get_sys_avg(void);

/**
 * @brief 获取系统累计总里程
 * @return double 总里程，单位 km。
 */
double avg_consumption_get_sys_distance(void);

/**
 * @brief 获取系统累计总能耗
 * @return double 总能耗，单位 Wh。
 */
double avg_consumption_get_sys_energy(void);

/**
 * @brief 获取系统累计总回收能量
 * @return double 总回收能量，单位 Wh。
 */
double avg_consumption_get_sys_regen_energy(void);

/**
 * @brief 获取里程平均电耗
 * @return double 平均电耗，单位 Wh/km。如果里程为0，返回0。
 */
double avg_consumption_get_trip_avg(void);

/**
 * @brief 获取里程总里程
 * @return double 总里程，单位 km。
 */
double avg_consumption_get_trip_distance(void);

/**
 * @brief 获取旅程总能耗
 * @return double 总能耗，单位 Wh。
 */
double avg_consumption_get_trip_energy(void);

/**
 * @brief 获取旅程总回收能量
 * @return double 总回收能量，单位 Wh。
 */
double avg_consumption_get_trip_regen_energy(void);

/**
 * @brief 复位旅程数据
 */
void avg_consumption_reset_trip(void);

/**
 * @brief 手动触发系统数据保存
 */
void avg_consumption_save_sys_data(void);

/**
 * @brief 根据累积的能量和距离更新平均电耗
 * @details 里程算法在每个计算周期结束后，将该周期内累积的能量和距离传给此函数。
 * @param[in] accumulated_energy_mwh 累积的能量，单位 mWh。
 * @param[in] distance_m 累积的距离，单位 m。
 */
void avg_consumption_update(double accumulated_energy_mwh, double distance_m);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __AVG_CONSUMPTION_H__