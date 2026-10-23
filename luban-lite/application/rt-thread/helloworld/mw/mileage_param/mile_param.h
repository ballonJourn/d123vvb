#ifndef __MILE_PARAM_H__
#define __MILE_PARAM_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __HCN_MILE_PARAM_H__
#define __HCN_MILE_PARAM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "dev_config.h"

#if MILEAGE_PARAM_ENABLE

#define E2PROM_MAGIC_NUM    (0x44556677)  ///< magic num

/**
 * @brief 用户里程参数结构体
 */
typedef struct {
    uint32_t odo;           ///< 总里程，单位为m
    uint32_t trip;          ///< 小计里程A 单位为m
    uint32_t ride_time;      ///< 骑行时间a: 单位秒
    uint16_t range;          ///< 续航里程a km
    uint16_t avg_elec_cons;  ///< 平均电耗kw/100km
    uint32_t all_elec_cons;  ///< 累计电耗kw
    uint32_t erase_counter;  ///< 里程相关信息擦除次数:次
    uint32_t nfc_uid1;       ///< NFC UID 1
    uint32_t nfc_uid2;       ///< NFC UID 2
    uint32_t wireless_key1;  ///< WIRELESS KEY1
    uint32_t wireless_key2; ///< WIRELESS KEY2
    uint32_t reserve;      ///< 预留20字节
    uint32_t checksum;       ///< 检验位
} mile_param_t;  ///< 48字节

///< 数据结构48字节，分3个区域，每个区域7个槽位，该款eeprom擦写次数200w次，总计1400w次
///< 符合每一百米保持一次要求，覆盖999999km

/**
 * @brief 用户里程参数枚举
 */
typedef enum {
    MILE_PARAM_ODO = 0x10,
    MILE_PARAM_TRIP,
    MILE_PARAM_RIDE_TIME,
    MILE_PARAM_AVG_ELEC_CONS,
    MILE_PARAM_RANGE,
    MILE_PARAM_NFC_UID1,
    MILE_PARAM_NFC_UID2,
    MILE_PARAM_WIRELESS_KEY1,
    MILE_PARAM_WIRELESS_KEY2,
} mile_param_handle_e;

bool set_mile_param(mile_param_handle_e id, void *param);
bool get_mile_param(mile_param_handle_e id, void *param);
bool get_recovery_mile_param(void);

int mile_param_init(void);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __HCN_MILE_PARAM_H__

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __MILE_PARAM_H__