#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include "battery_module.h"
#include "app_log.h"
#include "usr_hal_psadc.h"
#ifdef AIC_SYSCFG_DRV
#include "hal_syscfg.h"
#endif
#include "vehicle_param.h"
#include "dev_config.h"
#include "usr_hal_pwm.h"
#include "mw_usbhost.h"
#if MW_LIGHT_SENSOR_ENABLE
#include "display_mode.h"
#endif
#if MW_BT_TPMS_ENABLE
#include "bt_tpms.h"
#endif
#if MW_BT_MODULE_ENABLE
#include "bt_module.h"
#endif

#if MW_BATTERY_MODULE_ENABLE

///< 定义在 LVGL 端口层 lv_rt_thread_port.c (该头文件不在应用层 CPPPATH 内)
///< 返回 RT_TRUE: LVGL 已确认停住; RT_FALSE: 超时未确认
extern rt_bool_t lvgl_pm_pause(void);
///< 定义在 power_manage.c. POWER_MANAGE_ENABLE=0 时 power_manage.h 不导出
///< 该声明, 但函数体在宏外, 符号始终存在. 复位前清 framebuffer 用.
extern void power_pre_reset_cleanup(void);

///< 栈要容纳 bt_module_sleep() 里的 printf 与 LittleFS 无关的纯 IO 操作,
///< 2048 太紧(参见 pm_thread 的 4096)
#define BATTERY_MODULE_STACK_SIZE (4096)
#define BATTERY_MODULE_PRIORITY   (13)

///< 采样周期: 只用于保护判定, 比原 2s 快, 让低压掉电能在 2s 内动作
#define BATTERY_SAMPLE_PERIOD_MS      (500)

///< 防抖窗口: 从第一帧满足条件开始, 窗口内(含窗口两端)每一帧都必须满足,
///< 任一帧不满足就清零重新计时. 500ms 采样, [t0, t0+2000ms] 共 5 帧
#define BATTERY_PROTECT_DEBOUNCE_MS   (2000)
#define BATTERY_PROTECT_CONFIRM_CNT   (BATTERY_PROTECT_DEBOUNCE_MS / BATTERY_SAMPLE_PERIOD_MS + 1)

///< VEH_VOLTAGE_BATTERY 上报周期保持原 2s(4 个采样周期)不变
#define BATTERY_REPORT_PERIOD_MS      (2000)
#define BATTERY_REPORT_CNT            (BATTERY_REPORT_PERIOD_MS / BATTERY_SAMPLE_PERIOD_MS)

///< 开机稳定期: 上电时大电容充电、背光点亮、WiFi/BT 启动会造成数秒压降,
///< 期间只采样上报, 不推进保护/恢复防抖. 稳定期结束才开始正式判定
#define BATTERY_BOOT_SETTLE_MS        (5000)

///< 开机自检只抓极端值(远离上电抖动带): <=6.5V 或 >=19V 立即不显示,
///< 6.5~7.8V / 18~19V 这一圈交给稳定期后的正式判定
#define BATTERY_BOOT_EXTREME_LOW_MV   (6500)
#define BATTERY_BOOT_EXTREME_HIGH_MV  (19000)

///< 调试日志: 开机后该窗口内每帧打印电压, 保护态则一直打印.
///< 用于确认"保护后 8.2V"是持续值还是保护卸载造成的假回升.
///< 定位完改成 0, 只保留保护态日志
#define BATTERY_DEBUG_LOG_MS          (0)

///< 保护/恢复阈值, 单位 mV.
///< 低压: <=7.8V 保护, >=8.4V 恢复.
///< 恢复阈值不能贴着 8.0V: 保护动作会关屏/断 BT-WiFi/USB, 负载骤降后
///< 电池电压会真实回升(实测带载 7.6V -> 卸载 8.0V), 若 8.0V 就恢复,
///< 重启加载后又跌回 7.6V, 会无限循环. 8.4V 对应的带载电压约 8.0V.
///< 高压: >=17.0V 保护, <=16.8V 恢复(卸载会使电压更高, 不存在误恢复)
///< 电压过高会导致ADC模块失效，目前阈值为17.9V
#define BATTERY_LOW_PROTECT_MV        (7800)
#define BATTERY_LOW_RECOVER_MV        (8400)
#define BATTERY_HIGH_PROTECT_MV       (17000)
#define BATTERY_HIGH_RECOVER_MV       (16900)

#define BATTERY_ADC_CHANNEL         PSADC_CH_AUX6
#define BATTERY_ADC_SAMPLE_NUM      (10)    //复合滤波采样次数
#define BATTERY_ADC_MIN_SAMPLE_NUM  (3)     //去极值至少需要 3 个有效样本
#define BATTERY_REF_VOLTAGE_ACCURACY (10000) //eFuse 参考电压精度：V × 10000
#define BATTERY_DEFAULT_REF_VOLTAGE  (2.5f)  //eFuse 读取失败时的兜底参考电压(V)
#define BATTERY_VOLTAGE_RATIO        (7.0f)  //硬件电阻分压比
#define BATTERY_VOLTAGE_COMPENSATION (0.35f) //电压补偿

//系统参考电压，单位同 eFuse 精度(V × 10000)；本平台由 LDO25 经 eFuse 校准得到，每颗芯片不同
static int battery_ref_voltage = 0;

static rt_thread_t g_battery_tid = RT_NULL;

typedef enum {
    BATTERY_PROTECT_NONE = 0,
    BATTERY_PROTECT_LOW,        ///< 低压保护: 等待 >=8.4V
    BATTERY_PROTECT_HIGH,       ///< 高压保护: 等待 <=16.9V
} battery_protect_t;

static volatile battery_protect_t g_protect_state = BATTERY_PROTECT_NONE;
///< 保护动作(停 LVGL/关屏/断外设)只执行一次
static bool g_protect_applied = false;
///< 稳定期起点(初始化完成时刻)
static rt_tick_t g_boot_tick = 0;

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

///< 读取一次电压, 单位 mV. 返回 false 表示本次采样无效.
///< 直接由 battery_voltage_conversion() 的浮点结果截断, 不做四舍五入:
///< 四舍五入会把 7.7999V 进位成 7800mV, 在阈值边界上造成误判
static bool battery_read_voltage_mv(int32_t *mv) {
    float voltage = battery_voltage_conversion();

    if (voltage <= 0.0f)
        return false;

    *mv = (int32_t)(voltage * 1000.0f);
    return true;
}

///< 关面板 + 关背光. 背光是应用层 usr_backlight 用 hal_pwm 直接配的,
///< BSP 的 panel_backlight_disable() 只管 PD.14, 必须自己关通道.
///< 该函数不做 LVGL pause, 可在 LVGL 线程内调用
static void battery_screen_off(void) {
    rt_device_t fb = rt_device_find("aicfb");

    if (fb != RT_NULL)
        rt_device_control(fb, RTGRAPHIC_CTRL_POWEROFF, RT_NULL);

#if MW_BL_PW_ENABLE
    hal_pwm_disable(LCD_PWM_CH);
#endif
}

///< 保护态动作: 停 LVGL -> 关屏 -> 断可软件断电的外设.
///< 恢复走复位重启, 因此这里不需要任何逆操作.
static void battery_protect_apply(void) {
    ///< 必须先停 LVGL: 线程仍在 flush 时 POWEROFF 会关 DE 时钟.
    ///< 超时未确认时只关背光, 不碰面板/DE 时钟(与 power_manage 处理一致)
    if (lvgl_pm_pause()) {
        battery_screen_off();
    } else {
        rt_log_error("battery protect: lvgl pause timeout, skip panel power off");
#if MW_BL_PW_ENABLE
        hal_pwm_disable(LCD_PWM_CH);
#endif
    }

    ///< 光感线程会按环境光重新使能背光, 屏幕已关就不再需要它
#if MW_LIGHT_SENSOR_ENABLE
    display_mode_sleep();
#endif

    ///< 断 BT/WiFi 模块供电(PD.2) + 使能脚(PD.3) + 关 UART2;
    ///< 胎压依赖蓝牙栈, 一起停掉
#if MW_BT_TPMS_ENABLE
    bt_tpms_sleep();
#endif
#if MW_BT_MODULE_ENABLE
    bt_module_sleep();
#endif

#if MW_CHEERYUSB_ENABLE
    ///< USB 5V(PD.6)
    mw_usbhost_sleep();
#endif
}

///< 电压恢复: 清 framebuffer 后复位重启, 整套 init 重跑.
///< 清屏是必要的: rt_hw_cpu_reset 走看门狗复位, PSRAM 保留, 最后一帧
///< 会透过开机动画的透明区显示成残影
static void battery_protect_recover(int32_t mv) {
    rt_log_info("[BAT] voltage %d mV recovered, cpu reset", mv);
    rt_kprintf("[BAT] voltage recovered, restart\n");

    power_pre_reset_cleanup();
    rt_thread_mdelay(10);   ///< 让串口日志先出完
    rt_hw_cpu_reset();      ///< 不返回
}

static void battery_module_thread(void *param) {
    int confirm = 0;
    int report_cnt = 0;

    for (;;) {
        rt_thread_mdelay(BATTERY_SAMPLE_PERIOD_MS);

        int32_t mv;
        ///< 无效采样视为"本帧不满足": 既不参与保护/恢复判定, 也清掉防抖
        ///< 计数, 同时不推进上报周期
        if (!battery_read_voltage_mv(&mv)) {
            confirm = 0;
            rt_kprintf("[BAT] t=%lu sample invalid\n",
                        (unsigned long)rt_tick_get());
            continue;
        }
#if BATTERY_DEBUG_LOG_MS
        bool log_now = (g_protect_state != BATTERY_PROTECT_NONE) ||
                       (rt_tick_get() - g_boot_tick <
                            rt_tick_from_millisecond(BATTERY_DEBUG_LOG_MS));

        if (log_now) {
            const char *state = (g_protect_state == BATTERY_PROTECT_LOW)  ? "LOW"
                                : (g_protect_state == BATTERY_PROTECT_HIGH) ? "HIGH"
                                                                            : "NONE";
            rt_kprintf("[BAT] t=%lu v=%ld mV state=%s confirm=%d/%d\n",
                       (unsigned long)rt_tick_get(), (long)mv, state,
                       confirm, BATTERY_PROTECT_CONFIRM_CNT);
        }
#endif
        ///< VEH_VOLTAGE_BATTERY 为 0.1V 分辨率, 四舍五入.
        ///< 判定用 500ms 采样值, 上报仍按 2s 周期(每 4 个有效采样写一次)
        if (++report_cnt >= BATTERY_REPORT_CNT) {
            report_cnt = 0;
            vehicle_set_data(VEH_VOLTAGE_BATTERY, (mv + 50) / 100);
        }

        ///< 开机稳定期内只上报, 不判定. 已因极端值进入保护态的例外:
        ///< 那种情况要照常执行关断动作并等待恢复
        if (g_protect_state == BATTERY_PROTECT_NONE &&
            rt_tick_get() - g_boot_tick <
                rt_tick_from_millisecond(BATTERY_BOOT_SETTLE_MS)) {
            confirm = 0;
            continue;
        }

        if (g_protect_state == BATTERY_PROTECT_NONE) {
            if (mv <= BATTERY_LOW_PROTECT_MV || mv >= BATTERY_HIGH_PROTECT_MV) {
                if (++confirm < BATTERY_PROTECT_CONFIRM_CNT)
                    continue;

                confirm = 0;
                g_protect_state = (mv >= BATTERY_HIGH_PROTECT_MV)
                                  ? BATTERY_PROTECT_HIGH : BATTERY_PROTECT_LOW;
                rt_log_error("[BAT] voltage %d mV over range, protect on", mv);
            } else {
                confirm = 0;
            }
        }

        if (g_protect_state != BATTERY_PROTECT_NONE) {
            if (!g_protect_applied) {
                g_protect_applied = true;
                battery_protect_apply();
            }

            bool recovered = (g_protect_state == BATTERY_PROTECT_LOW)
                             ? (mv >= BATTERY_LOW_RECOVER_MV)
                             : (mv <= BATTERY_HIGH_RECOVER_MV);

            if (recovered) {
                if (++confirm >= BATTERY_PROTECT_CONFIRM_CNT)
                    battery_protect_recover(mv);   ///< 不返回
            } else {
                confirm = 0;
            }

            ///< 兜底: 保护期间任何线程重新使能背光都再关掉一次
#if MW_BL_PW_ENABLE
            hal_pwm_disable(LCD_PWM_CH);
#endif
        }
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

    ///< 稳定期从初始化完成开始计时, 上电上升沿期间不做保护判定
    g_boot_tick = rt_tick_get();

    ///< 开机自检: ADC 刚使能, 等 100ms 再采, 避免读到未稳定的值.
    ///< 上电期间电压会抖动, 这里只抓极端值; 6.5~7.8V / 18~19V 留给
    ///< 稳定期结束后的正式判定
    rt_thread_mdelay(100);
    int32_t boot_mv;
    if (battery_read_voltage_mv(&boot_mv) &&
        (boot_mv <= BATTERY_BOOT_EXTREME_LOW_MV ||
         boot_mv >= BATTERY_BOOT_EXTREME_HIGH_MV)) {
        g_protect_state = (boot_mv >= BATTERY_BOOT_EXTREME_HIGH_MV)
                          ? BATTERY_PROTECT_HIGH : BATTERY_PROTECT_LOW;
        rt_log_error("[BAT] boot voltage %d mV extreme, hold screen off", boot_mv);

        ///< 光感线程此时已启动, 先删掉, 否则它可能把背光重新点亮
#if MW_LIGHT_SENSOR_ENABLE
        display_mode_sleep();
#endif
        battery_screen_off();

        ///< 采样线程必须继续跑, 电压恢复后才能发现并复位重启
        g_battery_tid = rt_thread_create("battery_module", battery_module_thread, RT_NULL,
                                         BATTERY_MODULE_STACK_SIZE, BATTERY_MODULE_PRIORITY, 10);
        if (g_battery_tid == RT_NULL) {
            rt_log_error("Failed to create battery_module thread!");
            return -1;
        }
        rt_thread_startup(g_battery_tid);

        ///< 返回非 0 让 mw_init 中止后续 CAN/按键/蓝牙初始化:
        ///< 保护期间不需要点亮这些外设, 恢复由复位重启重跑整套 init
        return -1;
    }

    g_battery_tid = rt_thread_create("battery_module", battery_module_thread, RT_NULL,
                                     BATTERY_MODULE_STACK_SIZE, BATTERY_MODULE_PRIORITY, 10);
    if (g_battery_tid == RT_NULL) {
        rt_log_error("Failed to create battery_module thread!");
        return -1;
    }
    rt_thread_startup(g_battery_tid);

    rt_kprintf("battery module init success!\n");

    return 0;
}

///< 下电低功耗: 删电池采样线程. 唤醒走复位重启, 不需要恢复现场
void battery_module_sleep(void) {
    if (g_battery_tid != RT_NULL) {
        rt_thread_delete(g_battery_tid);
        g_battery_tid = RT_NULL;
    }
}

bool battery_is_voltage_protected(void) {
    return g_protect_state != BATTERY_PROTECT_NONE;
}

void battery_protect_screen_off(void) {
    battery_screen_off();
}

#endif /* MW_BATTERY_MODULE_ENABLE */
