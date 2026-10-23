#include <stdlib.h>
#include <string.h>
#include <rtthread.h>
#include <rtdevice.h>
#include "usr_hal_psadc.h"
#include "app_log.h"

#define AIC_PSADC_NAME              "psadc"
#define AIC_PSADC_ADC_MAX_VAL        0XFFF   //12bit ==>4095
#define AIC_PSADC_USE_CHANNEL        (2)     //已使能节点数：本平台为 PSADC6 / PSADC7
#define AIC_PSADC_START_CHANNEL_ID   (6)     //从通道 6 开始
#define AIC_PSADC_QC_MODE            0       //QC 队列类型

static rt_adc_device_t psadc_dev;
static int enable_chan_cnt = 0;
static uint32_t adc_values[AIC_PSADC_USE_CHANNEL];

/* 通道合法性：须落在已使能范围内 [START, START + USE_CHANNEL) */
static int channel_is_valid(uint32_t channel) {
    return (channel >= AIC_PSADC_START_CHANNEL_ID
            && channel < AIC_PSADC_START_CHANNEL_ID + AIC_PSADC_USE_CHANNEL);
}

float adc_2_voltage(float st_voltage, uint32_t adc_value) {
    return (float)adc_value * st_voltage / AIC_PSADC_ADC_MAX_VAL;
}

int psadc_channel_enable(uint32_t channel) {
    if (!channel_is_valid(channel)) {
        rt_log_error("Invalid channel %d", channel);
        return -1;
    }

    if (!psadc_dev) {
        psadc_dev = (rt_adc_device_t)rt_device_find(AIC_PSADC_NAME);
        if (!psadc_dev) {
            rt_log_error("Failed to open %s device", AIC_PSADC_NAME);
            return -1;
        }
    }

    rt_adc_enable(psadc_dev, AIC_PSADC_QC_MODE);
    /* 每次启用都刷新节点数，避免依赖调用顺序 */
    enable_chan_cnt = rt_adc_control(psadc_dev, RT_ADC_CMD_GET_CHAN_COUNT, NULL);
    if (enable_chan_cnt != AIC_PSADC_USE_CHANNEL) {
        rt_log_error("Enabled channel num(%d) mismatch, expected %d",
                     enable_chan_cnt, AIC_PSADC_USE_CHANNEL);
        return -1;
    }

    return 0;
}

int psadc_channel_disable(uint32_t channel) {
    if (!channel_is_valid(channel)) {
        rt_log_error("Invalid channel %d", channel);
        return -1;
    }

    if (!psadc_dev) {
        psadc_dev = (rt_adc_device_t)rt_device_find(AIC_PSADC_NAME);
        if (!psadc_dev) {
            rt_log_error("Failed to open %s device", AIC_PSADC_NAME);
            return -1;
        }
    }

    /* drv_psadc 的 enabled() 把参数当作 QC 队列类型，必须传 0，与 enable 一致 */
    rt_adc_disable(psadc_dev, AIC_PSADC_QC_MODE);

    return 0;
}

uint32_t psadc_get_channel_value(uint32_t channel) {
    if (!channel_is_valid(channel)) {
        rt_log_error("Invalid channel %d", channel);
        return 0;
    }
    if (AIC_PSADC_USE_CHANNEL != enable_chan_cnt) {
        rt_log_error("Channel num error");
        return 0;
    }

    int ret = 0;
    uint32_t data_temp[AIC_PSADC_USE_CHANNEL] = {0};

    ret = rt_adc_control(psadc_dev, RT_ADC_CMD_GET_VALUES_POLL,
                             (void *)data_temp);
    if (ret < 0) {
        rt_log_error("Read timeout");
        return 0;
    }

    for (int i = 0; i < enable_chan_cnt; i++) {
        adc_values[i] = data_temp[i];
        // printf("adc_values:%7d chanel:%d ", adc_values[i], i + AIC_PSADC_START_CHANNEL_ID);
    }
    // printf("\n");
    return adc_values[channel - AIC_PSADC_START_CHANNEL_ID];
}