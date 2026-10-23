#ifndef __USR_HAL_PSADC_H__
#define __USR_HAL_PSADC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

typedef enum 
{
    PSADC_CH_AUX0 = 0,
    PSADC_CH_AUX1 = 1,
    PSADC_CH_AUX2 = 2,
    PSADC_CH_AUX3 = 3,
    PSADC_CH_AUX4 = 4,
    PSADC_CH_AUX5 = 5,
    PSADC_CH_AUX6 = 6,
    PSADC_CH_AUX7 = 7,
    PSADC_CH_AUX8 = 8,
    PSADC_CH_AUX9 = 9,
    PSADC_CH_AUX10 = 10,
    PSADC_CH_AUX11 = 11,
    PSADC_CH_AUX12 = 12,
    PSADC_CH_AUX13 = 13,
    PSADC_CH_AUX14 = 14,
    PSADC_CH_AUX15 = 15
} psadc_channel;

int psadc_channel_disable(uint32_t channel);

int psadc_channel_enable(uint32_t channel);

float adc_2_voltage(float st_voltage, uint32_t adc_value);

uint32_t psadc_get_channel_value(uint32_t channel);

#ifdef __cplusplus
}
#endif

#endif //__USR_HAL_PSADC_H__