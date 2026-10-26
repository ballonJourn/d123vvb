#ifndef __MW_CAN_H__
#define __MW_CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include "dev_config.h"

#if MW_CAN_ENABLE

#define CAN_ENGINGE_RPM_ID        0x110
#define CAN_DRIVE_MODE_ID         0x111
#define CAN_LIGHT_STATE_ID        0x112
#define CAN_TCS_WARNING_ID        0x120
#define CAN_SPEED_ID              0x122
#define CAN_ABS_WARNING_ID        0x12B
#define CAN_SET_TRIP_A            0x776
#define CAN_SET_TRIP_B            0x777
#define CAN_SET_ODO               0x778
#define CAN_CLEAN_EEPROM          0x779

typedef struct{
    uint32_t ecu_110_rx_timeout;
    uint32_t ecu_111_rx_timeout;
    uint32_t ecu_112_rx_timeout;
    uint32_t ecu_120_rx_timeout;
    uint32_t ecu_122_rx_timeout;
    uint32_t ecu_12b_rx_timeout;
} can_rx_timeout;

int mw_can_init(void);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __MW_CAN_H__