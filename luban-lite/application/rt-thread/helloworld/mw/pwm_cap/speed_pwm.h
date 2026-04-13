#ifndef __SPEED_PWM_H__
#define __SPEED_PWM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rtthread.h>
#include "dev_config.h"
#include <stdint.h>
#include <stdio.h>

#if SPEED_PWM_ENABLE
int speed_pwm_init(void);
#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __SPEED_PWM_H__