#ifndef __433_WIRELESS_UNLOCK_H__
#define __433_WIRELESS_UNLOCK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "dev_config.h"

#if WIRELESS_UNLOCK_ENABLE

typedef enum {
    RECV_STATE_NONE = 0,
    RECV_STATE_IDLE,
    RECV_STATE_RECV,
    RECV_STATE_CHECK
} recv_state_e;

typedef enum {
    LOW_LEVEL = 0,
    HIGH_LEVEL,
    START_LEVEL,
    MISTAKE_LEVEL,
} level_state_e;

typedef struct {
    uint32_t last_tick;
    uint8_t recv_data[3];
    uint8_t recv_compare[3];
    recv_state_e recv_state;
    uint8_t bit_count;
    uint8_t data_index;
    uint8_t level_state;
    uint8_t data_value;
    uint8_t recv_ok;
    uint8_t check_flag;
    uint16_t low_duration;
} wireless_unlock_t;

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __433_WIRELESS_UNLOCK_H__