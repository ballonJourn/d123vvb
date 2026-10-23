#ifndef VEHICLE_SIGNAL_H
#define VEHICLE_SIGNAL_H

#include <stdint.h>
#include "definition.h"
#include "home_view_interface.h"
#define ON_PC_CACLE   0

int32_t vehicle_get_data_signal_lamp(signal_e lamp);

int32_t vehicle_get_data_speed();

int32_t vehicle_get_data_rpm();

int32_t vehicle_get_data_gear();

int32_t vehicle_get_data_power();

int32_t vehicle_get_data_drv_mode() ;

///< 驾驶模式索引 (与 mode_index_e 对应: 0 normal / 1 eco / 2 sport)
int32_t vehicle_get_data_mode_index();

int32_t vehicle_get_data_remain_battary();

///< 各电池包 SOC(%)：pack 1/2/3 对应 BMS P1/P2/P3
int32_t vehicle_get_data_bat_soc(uint8_t pack);

///< 故障项：顺序与 view_faults 的 fault_item_e 一致 (BMS1/BMS2/BMS3/MCU)
typedef enum {
    VEH_FAULT_BMS1 = 0,
    VEH_FAULT_BMS2,
    VEH_FAULT_BMS3,
    VEH_FAULT_MCU,
    VEH_FAULT_ITEM_NUM,
} veh_fault_item_e;

///< 取某故障项当前码值 (0: 无故障)。MCU 为 8bit 故障位图(MCUErrBits)，
///< BMS 为该包 DTC 码 (一/二/三级复用同一槽位, 后到后得)
int32_t vehicle_get_fault_code(veh_fault_item_e item);

///< CAN 弹窗消息码 (0:无, 1..N:pop_tip_code_e)
int32_t vehicle_get_can_popup_msg(void);
void vehicle_set_can_popup_msg(int32_t code);
int32_t vehicle_get_battery_voltage(void);

const char* veicle_get_data_version();

#if  0
int32_t vehicle_get_data_current_display() ;

int32_t vehicle_get_ota_state() ;

const char* veicle_get_data_mcu_ver();

void vehicle_set_ota_page_state(int32_t state);

#endif

#endif