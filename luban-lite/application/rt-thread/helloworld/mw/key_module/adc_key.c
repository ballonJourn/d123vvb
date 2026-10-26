
#include <rtthread.h>
#include <string.h>
#include "rtdevice.h"
#include "app_log.h"
#include "adc_key.h"
#include "dev_state.h"
#include "key_common.h"
#include <time.h>
#include "aic_core.h"
#include "hal_rtp.h"
#include "touch.h"
#include "hal_adcim.h"
#include "aic_drv_gpio.h"
#include "float.h"

#if MW_ADC_KEY_ENABLE

#define KEYAD_NUM	                    (4)
#define AIC_ADC_KEY_DEFAULT_VOLTAGE    (3.3)
#define AIC_ADC_KEY_VOLTAGE_ACCURACY   (10000)
#define AIC_ADC_KEY_CHAN_MAX            (4)

static int keyadc_arr[] = {497, 1351, 2240, 3340};
static uint8_t key_map[KEYAD_NUM] = {
    UP_KEY_PRESS,
    DOWN_KEY_PRESS,
    ENTER_KEY_PRESS,
    BACK_KEY_PRESS,
};

static  float def_voltage = AIC_ADC_KEY_DEFAULT_VOLTAGE;
static rt_device_t g_adc_dev = RT_NULL;

static void adc_key_scan(int cal_param) {
    if (get_check_self_state() < CHECK_SELF_STATE_SUCCESS) {
        return;
    }

    (void)cal_param;

    struct aic_rtp_adc_info adc_info = {0};
    adc_info.ch = MW_ADC_KEY_CHANNEL;
#ifdef AIC_RTP_DRV
    rt_device_control(g_adc_dev, RT_TOUCH_CTRL_GET_ADC, (void *)&adc_info);
#endif
    rt_log_info("adc key value:%d\r\n", adc_info.data);
    rt_log_info("def v:%.3f, arr:%d, map:%d\r\n", def_voltage, keyadc_arr[0], key_map[0]);
}

static void adc_key_thread(void * parameter) {
    g_adc_dev = rt_device_find(AIC_RTP_NAME);
    int cal_param = 0;
    if (!g_adc_dev) {
        rt_log_error("Failed to find %s device\n", AIC_RTP_NAME);
        return;
    }

    cal_param = hal_adcim_auto_calibration();
    rt_device_control(g_adc_dev, RT_TOUCH_CTRL_ENABLE_INT, RT_NULL);

    for (;;) {
        rt_thread_mdelay(10);
        adc_key_scan(cal_param);
    }
}

int adc_key_init(void) {
    rt_thread_t thread;

    thread = rt_thread_create("adc_key_thread", adc_key_thread, 
                            RT_NULL, 1024, 14, 10);
    if (thread != RT_NULL) {
        rt_thread_startup(thread);
    } else {
        rt_kprintf("create adc key thread failed!\n");
        return -1;
    }

    return 0;
}

#endif