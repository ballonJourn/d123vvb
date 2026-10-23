#ifndef VEHICLE_MILE__H
#define VEHICLE_MILE__H

#include <stdint.h>
#include <stdbool.h>

/* 续航参考里程数组长度 (与中间层 set_param.h 的 REF_MILE_ARR_NUM 一致,
 * 二者不一致时 vehicle_mile.c 的编译期断言会直接报错) */
#define VEHICLE_REF_MILE_ARR_LEN   (10)

/**
 * @brief  读取usr mile准备状态
 * @mile  none
 * @return false:参数未准备好 true:已准备好
 */

bool vehicle_get_mile_recovery();

//odo
uint32_t vehicle_get_mile_odo();
void vehicle_set_mile_odo(double value) ;


//tripA
uint32_t vehicle_get_mile_tripA();
void vehicle_set_mile_tripA(double value) ;


//tripB
uint32_t vehicle_get_mile_tripB();
void vehicle_set_mile_tripB(double value) ;


int32_t vehicle_get_mile_changed() ;

/* ---- 续航里程 (RemainMile) ---- */

/**
 * @brief  读取某电池包的续航参考里程数组 (每 1% SOC 的参考里程)
 * @param  pack  1:固定包 2/3:换电包
 * @param  arr   输出缓冲, 长度 REF_MILE_ARR_NUM (uint8_t, 单位 0.01km)
 * @return false:参数未就绪(缓冲内容未定义) true:成功
 */
bool vehicle_get_ref_mile_arr(uint8_t pack, uint8_t *arr);

/**
 * @brief  写入某电池包的续航参考里程数组 (只改 RAM, 由 IGN OFF 统一落盘)
 */
bool vehicle_set_ref_mile_arr(uint8_t pack, const uint8_t *arr);

/**
 * @brief  发布续航里程 (整数 km) 到中间层, 0x453 会周期广播
 */
void vehicle_set_mile_endurance(uint16_t km);

/**
 * @brief  保存续航里程 (整数 km) 到 mile_param, 由 mileage_calc 的保存点统一落盘
 * @note   与 vehicle_set_mile_endurance 的区别: 后者只发 0x453 不落 EEPROM
 */
void vehicle_set_mile_range(uint16_t km);

/**
 * @brief  读取上次落盘的续航里程 (整数 km), 供开机做续航初值
 * @param  km  输出缓冲
 * @return false:参数未就绪 true:成功
 */
bool vehicle_get_mile_range(uint16_t *km);

/**
 * @brief  发布 1 号包当前 SOC 窗口内累计里程 (0.01km), 供 0x455 调试帧使用
 */
void vehicle_set_ref_mile_count(uint16_t centikm);

#endif