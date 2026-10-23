#ifndef REF_MILEAGE_CALC_H_
#define REF_MILEAGE_CALC_H_

#include <stdbool.h>
#include <stdint.h>

#define REF_MILE_PACK_NUM   (3)     ///< 电池包数: 1 号固定包 + 2/3 号换电包

/**
 * @brief 启动续航里程计算线程 (由 home_logic_init 调用)
 */
void ref_mileage_calc_init(void);

/* 数据变化 → UI 刷新标志位 (仿 mileage_calc 的 get/set_mileage_state) */
bool get_ref_mileage_state(void);
void set_ref_mileage_state(bool state);

/**
 * @brief 三包续航里程总和
 * @return 单位 0.1km
 */
uint32_t ref_mileage_total_x10(void);

/**
 * @brief 续航里程是否可用
 * @return false: 三包 SOC 全部 ≤5% (含通信超时按 0 计), UI 应显示 "--km"
 */
bool ref_mileage_display_valid(void);

/**
 * @brief 续航计算是否已就绪 (开机初值或首个计算结果已建立)
 * @return false: 尚未 load 完成, 落盘方不得以当前值写 0 覆盖上次续航
 */
bool ref_mileage_ready(void);

#endif