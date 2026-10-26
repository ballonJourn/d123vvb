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

int32_t vehicle_get_data_remain_battary();

#if  0
int32_t vehicle_get_data_current_display() ;

int32_t vehicle_get_ota_state() ;

const char* veicle_get_data_version();

const char* veicle_get_data_mcu_ver();

void vehicle_set_ota_page_state(int32_t state);

#endif

#endif