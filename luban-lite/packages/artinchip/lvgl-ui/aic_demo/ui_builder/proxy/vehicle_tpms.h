#ifndef VEHICLE_TPMS_H
#define VEHICLE_TPMS_H

#include <stdint.h>

/* 轮位取值（vehicle_tpms_pair_* 的 slot 参数）：
 *   与中间层 tpms_pos_e 的左前/左后一致，本工程两轮只用这两个 */
#define VEHICLE_TPMS_LEFT_FRONT  (0)
#define VEHICLE_TPMS_LEFT_REAR   (2)

/* 配对状态取值（vehicle_tpms_pair_get_state）：
 *   IDLE     未配对，且当前没有进行中的配对
 *   LEARNING 学习中，等待传感器上报
 *   SUCCESS  该轮已配对
 *   TIMEOUT  本次配对超时（原有配对记录若存在则保留） */
#define VEHICLE_TPMS_PAIR_IDLE      (0)
#define VEHICLE_TPMS_PAIR_LEARNING  (1)
#define VEHICLE_TPMS_PAIR_SUCCESS   (2)
#define VEHICLE_TPMS_PAIR_TIMEOUT   (3)

/**
 * @brief  发起某轮蓝牙胎压配对学习
 * @param  slot 轮位，取值见 VEHICLE_TPMS_LEFT_FRONT / VEHICLE_TPMS_LEFT_REAR
 * @return 0:已受理 -1:轮位不支持
 * @note   配对是被动的：本函数只决定"学哪一轮"，实际学习要等传感器侧
 *         充气/放气/手持激活后其广播状态位置位。下发后用
 *         vehicle_tpms_pair_get_state() 轮询结果。
 */
int32_t vehicle_tpms_pair_start(uint8_t slot);

/**
 * @brief  提前结束配对学习
 * @return 0:已受理 -1:轮位不支持
 * @note   原有配对记录保留
 */
int32_t vehicle_tpms_pair_stop(uint8_t slot);

/**
 * @brief  清除该轮配对记录
 * @return 0:已受理 -1:轮位不支持
 */
int32_t vehicle_tpms_pair_clear(uint8_t slot);

/**
 * @brief  取配对状态
 * @return 取值见 VEHICLE_TPMS_PAIR_*
 */
uint8_t vehicle_tpms_pair_get_state(uint8_t slot);

/**
 * @brief  取某轮实时胎压
 * @param  slot 轮位，取值见 VEHICLE_TPMS_LEFT_FRONT / VEHICLE_TPMS_LEFT_REAR
 * @return 胎压 kPa（>=0）；-1 表示无有效数据 —— 该轮未学习，或传感器已丢失
 *         （30min 无上报）。UI 侧据此显示 "--"。
 */
int32_t vehicle_tpms_get_pressure(uint8_t slot);

/**
 * @brief  取某轮实时胎温
 * @param  slot 轮位，取值见 VEHICLE_TPMS_LEFT_FRONT / VEHICLE_TPMS_LEFT_REAR
 * @return 胎温 ℃；-1 表示无有效数据（条件同 get_pressure）。UI 侧据此显示 "--"。
 */
int32_t vehicle_tpms_get_temp(uint8_t slot);

#endif