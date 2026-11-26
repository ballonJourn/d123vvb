#ifndef __LIGHT_SENSOR_H__
#define __LIGHT_SENSOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

void light_sensor_init(void);
uint32_t get_light_sensor_value (void);

#ifdef __cplusplus
}
#endif

#endif //__LIGHT_SENSOR_H__