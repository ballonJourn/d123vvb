#include <math.h>
#include <rtthread.h>
#include <rtdevice.h>
#include "app_log.h"
#include "usr_hal_pwm.h"

int hal_pwm_config(int id, uint32_t duty_ns, uint32_t period_ns) {
    struct rt_device_pwm *pwm_dev = RT_NULL;
    pwm_dev = (struct rt_device_pwm *)rt_device_find("epwm");

    if (pwm_dev == NULL) {
        rt_log_error("can't find pwm device!\n");
        return -RT_ERROR;
    }

    rt_pwm_set(pwm_dev, id, period_ns, duty_ns);
    rt_pwm_enable(pwm_dev, id);

    return 0;
}

void hal_pwm_enable(int id) {
    (void)id;
}

void hal_pwm_disable(int id) {
    (void)id;
}
