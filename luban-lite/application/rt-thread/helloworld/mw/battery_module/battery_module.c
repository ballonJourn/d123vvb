#include <rtthread.h>
#include "battery_module.h"
#include "app_log.h"
#include "usr_hal_psadc.h"
#ifdef AIC_SYSCFG_DRV
#include "hal_syscfg.h"
#endif
#include "vehicle_param.h"

#define BATTERY_MODULE_STACK_SIZE (2048)
#define BATTERY_MODULE_PRIORITY   (13)

#define BATTERY_ADC_CHANNEL         PSADC_CH_AUX6
#define BATTERY_ADC_SAMPLE_NUM      (10)    //复合滤波采样次数
#define BATTERY_ADC_MIN_SAMPLE_NUM  (3)     //去极值至少需要 3 个有效样本
#define BATTERY_REF_VOLTAGE_ACCURACY (10000) //eFuse 参考电压精度：V × 10000
#define BATTERY_DEFAULT_REF_VOLTAGE  (2.5f)  //eFuse 读取失败时的兜底参考电压(V)
#define BATTERY_VOLTAGE_RATIO        (7.0f)  //硬件电阻分压比
#define BATTERY_VOLTAGE_COMPENSATION (0.20f) //电压补偿

//系统参考电压，单位同 eFuse 精度(V × 10000)；本平台由 LDO25 经 eFuse 校准得到，每颗芯片不同
static int battery_ref_voltage = 0;

/*
 * 中值 + 平均复合滤波：
 * 连续采样 N 次并保持数组升序，丢弃读失败的样本（psadc_get_channel_value
 * 读超时返回 0，与合法采样值无法区分，只能丢弃）；
 * 再去掉最小和最大各一个，对剩余样本求平均。
 * 返回 0 表示本次采样无效。
 */
static uint32_t battery_adc_filtered(void) {
    uint32_t sample[BATTERY_ADC_SAMPLE_NUM] = {0};
    uint32_t sum = 0;
    int valid = 0;

    for (int i = 0; i < BATTERY_ADC_SAMPLE_NUM; i++) {
        uint32_t value = psadc_get_channel_value(BATTERY_ADC_CHANNEL);
        int j;

        if (value == 0) {
            rt_log_warn("battery adc sample invalid, dropped");
            continue;
        }

        //插入排序，保证 sample[0..valid-1] 升序，便于直接去掉首尾
        for (j = valid; j > 0 && sample[j - 1] > value; j--)
            sample[j] = sample[j - 1];
        sample[j] = value;
        valid++;
    }

    if (valid < BATTERY_ADC_MIN_SAMPLE_NUM) {
        rt_log_error("battery adc valid sample num(%d) is not enough", valid);
        return 0;
    }

    for (int i = 1; i < valid - 1; i++)
        sum += sample[i];

    // rt_log_debug("battery adc valid:%d filtered:%u", valid, sum / (valid - 2));

    return sum / (valid - 2);
}

static float battery_voltage_conversion(void) {
    uint32_t adc_value = battery_adc_filtered();

    //采样无效时返回 0 作为哨兵值
    if (adc_value == 0)
        return 0.0f;

    return BATTERY_VOLTAGE_RATIO
           * adc_2_voltage(battery_ref_voltage / (float)BATTERY_REF_VOLTAGE_ACCURACY, adc_value)
           + BATTERY_VOLTAGE_COMPENSATION;
}

static void battery_module_thread(void *param) {
    for (;;) {
        rt_thread_mdelay(2000);

        float battery_voltage_value = battery_voltage_conversion();
        // printf("battery_voltage_value = %.2f v\n", battery_voltage_value);

        //VEH_VOLTAGE_BATTERY 为 0.1V 分辨率，四舍五入；采样无效时转换函数返回 0，
        //加 0.5 后仍取整为 0，与 vehicle_param 用 0 表示"无数据"的惯例一致
        vehicle_set_data(VEH_VOLTAGE_BATTERY, (int32_t)(battery_voltage_value * 10.0f + 0.5f));
    }
}

int battery_module_init(void) {
    if (psadc_channel_enable(BATTERY_ADC_CHANNEL) != 0) {
        rt_log_error("battery_module_init adc channel failed");
        return -1;
    }

    battery_ref_voltage = 0;
#ifdef AIC_SYSCFG_DRV
    battery_ref_voltage = hal_syscfg_read_ldo_cfg();
#endif
    if (!battery_ref_voltage) {
        rt_log_warn("Failed to obtain reference voltage through eFuse");
        battery_ref_voltage = (int)(BATTERY_DEFAULT_REF_VOLTAGE * BATTERY_REF_VOLTAGE_ACCURACY);
    }
    rt_log_info("battery reference voltage: %d.%04d V",
                battery_ref_voltage / BATTERY_REF_VOLTAGE_ACCURACY,
                battery_ref_voltage % BATTERY_REF_VOLTAGE_ACCURACY);

    rt_thread_t thread;

    thread = rt_thread_create("battery_module", battery_module_thread, RT_NULL,
                              BATTERY_MODULE_STACK_SIZE, BATTERY_MODULE_PRIORITY, 10);
    if (thread == RT_NULL) {
        rt_log_error("Failed to create battery_module thread!");
        return -1;
    }
    rt_thread_startup(thread);

    rt_kprintf("battery module init success!\n");

    return 0;
}