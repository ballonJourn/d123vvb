#include "light_sensor.h"
#include "app_log.h"
#include "usr_hal_psadc.h"

void light_sensor_init(void) {
    if (psadc_channel_enable(PSADC_CH_AUX7) != 0) {
        rt_log_error("Light sensor adc channel init failed");
        return;
    } else {
        printf("light sensor success\n");
    }
}

uint32_t get_light_sensor_value (void) {
    return psadc_get_channel_value(PSADC_CH_AUX7);
}