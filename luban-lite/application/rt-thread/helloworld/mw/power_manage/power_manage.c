#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <aic_core.h>
#include <aic_hal_clk.h>
#include <aic_hal_gpio.h>
#include "app_log.h"
#include "power_manage.h"
#include "ign_check.h"
#include "set_param.h"
#include "mile_param.h"
#include "vehicle_param.h"
#include "low_power.h"
#include "dev_config.h"
#include "usr_hal_pwm.h"
#include "gpio_light.h"
#include "gpio_key.h"
#include "can_key.h"
#include "display_mode.h"
#include "battery_module.h"
#include "mw_wdt.h"
#include "bt_module.h"
#include "bt_tpms.h"
#include "usr_hal_psadc.h"
#include "mw_usbhost.h"

///< 定义在 LVGL 端口层 lv_rt_thread_port.c (该头文件不在应用层 CPPPATH 内)
///< 返回 RT_TRUE: LVGL 已确认停住; RT_FALSE: 超时未确认
extern rt_bool_t lvgl_pm_pause(void);
///< 定义在 ui_builder/logic, 同上
extern void mileage_calc_sleep(void);
extern void ref_mileage_calc_sleep(void);

#if POWER_MANAGE_ENABLE

///< 下电保存要跑 LittleFS 写 + low_power_enter(删 CAN 线程 + printf),
///< 2048 不够, 留足余量
#define PM_THREAD_STACK   4096
#define PM_THREAD_PRIO    30

struct rt_event usr_pm_event;

///< 把引脚切回 GPIO 输入高阻(关闭内部上下拉), 既不再输出, 也不主动灌/拉电流.
///< d13x 的 GPIO 功能号是 1(见 pinmux.c 的 {1, PIN_PULL_DIS, ...}), 不是 0
static void pm_pin_to_highz(const char *name)
{
    int pin;
    unsigned int g, p;

    pin = hal_gpio_name2pin(name);
    if (pin < 0)
        return;

    g = GPIO_GROUP(pin);
    p = GPIO_GROUP_PIN(pin);

    hal_gpio_set_func(g, p, 1);
    hal_gpio_set_bias_pull(g, p, PIN_PULL_DIS);
    hal_gpio_direction_input(g, p);
}

///< 把与"已断电外设"相连的 SoC 引脚放开为高阻, 切断经外部 IO 保护二极管的
///< 倒灌路径. 必须在对应外设已断电/关时钟之后调用(dev_sleep_mode 第 4、5 步).
///< 注意: 电源开关(PD.2)、背光使能(PD.14)、面板 STBYB/RST/SLEEP(PD.15~17)、
///< CAN STB(PE.15) 必须保持驱动电平, 不能放进这里.
static void dev_pins_to_highz(void)
{
#ifdef AIC_USING_UART2
    ///< 蓝牙模块已断电: TX 仍在输出、RX 内部上拉, 都会经模块 IO 倒灌;
    ///< 自动流控模式下 CTS/RTS 也接在该模块上(见 pinmux.c)
    static const char *uart2_pins[] = { "PD.4", "PD.5", "PA.2", "PA.3" };
    for (unsigned int i = 0; i < ARRAY_SIZE(uart2_pins); i++)
        pm_pin_to_highz(uart2_pins[i]);
#endif

#ifdef AIC_USING_SDMC0
    ///< WiFi SDIO: 控制器已 assert reset, pad 若不放开, pinmux 里的上拉会把
    ///< 线拉到 3.3V, 经断电模块的 IO 倒灌
    static const char *sdio0_pins[] = {
        "PB.6", "PB.7", "PB.8", "PB.9", "PB.10", "PB.11",
    };
    for (unsigned int i = 0; i < ARRAY_SIZE(sdio0_pins); i++)
        pm_pin_to_highz(sdio0_pins[i]);
#endif

#ifdef AIC_LVDS_LINK_0
    ///< LVDS 输出: DE 时钟已关, 保持最后电平没有意义, 放开可减小驱动与漏电
    static const char *lvds_pins[] = {
        "PD.18", "PD.19", "PD.20", "PD.21", "PD.22",
        "PD.23", "PD.24", "PD.25", "PD.26", "PD.27",
    };
    for (unsigned int i = 0; i < ARRAY_SIZE(lvds_pins); i++)
        pm_pin_to_highz(lvds_pins[i]);
#endif
}

///< 下电低功耗第一步: 停显示渲染 → 关屏 → 停轮询/采样线程.
///< 之后由 low_power_enter() 把 CAN 切到唤醒源并放行 PM.
///< 唤醒走 rt_hw_cpu_reset 复位重启, 这里不需要任何恢复动作.
static void dev_sleep_mode(void) {
    rt_device_t fb;
    rt_bool_t lvgl_paused;

    ///< 0. 先停看门狗: 下面会停掉 CAN/LVGL 两个心跳点, 不停狗的话喂狗线程
    ///<    会在心跳超时(12s)后主动停喂, 15s 内触发硬件复位
    mw_wdt_sleep();
    ///< 1. 先让 LVGL 线程停下(带确认). 没确认就不能动显示硬件
    lvgl_paused = lvgl_pm_pause();

    ///< 2. 停掉所有周期轮询/采样线程, 否则 CPU 仍按最小周期被唤醒.
    ///<    放在关屏之前: display_mode 线程会按光感调背光, 若在关屏之后才删,
    ///<    它可能在关屏和删除之间把刚关掉的背光 PWM 重新使能
#if GPIO_LIGHT_ENABLE
    light_gpio_sleep();
#endif
#if MW_IO_KEY_ENABLE
    gpio_key_sleep();
#endif
#if MW_CAN_IO_KEY_ENABLE
    can_key_sleep();
#endif
#if MW_LIGHT_SENSOR_ENABLE
    display_mode_sleep();
#endif
#if MW_BATTERY_MODULE_ENABLE
    battery_module_sleep();
#endif
    mileage_calc_sleep();
    ref_mileage_calc_sleep();

    ///< 3. 关 panel / 背光使能 GPIO(PD.14) / DE 时钟.
    ///<    必须在 LVGL 确认停下之后: POWEROFF 会关 DE 时钟, 若 LVGL 仍在
    ///<    flush, 它随后写 DE 寄存器可能挂住总线, 等 VSYNC 则是死等.
    ///<    超时未确认时跳过关屏, 保留在异常态直到下一次复位
    if (lvgl_paused) {
        fb = rt_device_find("aicfb");
        if (fb != RT_NULL) {
            if (rt_device_control(fb, RTGRAPHIC_CTRL_POWEROFF, RT_NULL) != RT_EOK) {
                rt_log_error("screen power off failed!\n");
            }
        } else {
            rt_log_error("aicfb device not found, screen off skipped\n");
        }

#if MW_BL_PW_ENABLE
        ///< BSP 的 panel_backlight_disable() 只拉低 PD.14: 背光 PWM 是应用层
        ///< usr_backlight 用 hal_pwm 直接配的, rtconfig.h 里没有
        ///< AIC_PWM_BACKLIGHT_CHANNEL, 驱动侧不会代关, 这里补一刀
        hal_pwm_disable(LCD_PWM_CH);
#endif
    } else {
        rt_log_error("lvgl pause timeout, skip screen off\n");
    }

    ///< 4. 蓝牙胎压 + 蓝牙/WiFi 链路: 休眠期间不做胎压监控, 整条链路停掉.
    ///<    bt_module_sleep() 还会关 UART2 并断开 AIC8800 供电(PD.3)
#if MW_BT_TPMS_ENABLE
    bt_tpms_sleep();
#endif
#if MW_BT_MODULE_ENABLE
    bt_module_sleep();
#endif

    ///< 5. 关剩余外设. 放在最后: 前面所有业务线程已停, 落盘也已完成.
    ///<    注意 I2C0(EEPROM) 和 QSPI0(NOR) 的时钟**不关**: 里程参数的 trip
    ///<    清零是异步队列落盘、tpms 的 JSON 写也可能在收尾, 关时钟只能依赖
    ///<    线程优先级恰好把队列排空; 一旦不成立就是丢数据或传输挂死,
    ///<    而这两个时钟的静态电流很小, 不值得冒这个险
#if MW_BL_PW_ENABLE
    hal_clk_disable(CLK_PWMCS);     ///< 背光通道已在上面 disable, 这里关模块时钟
#endif
#if MW_LIGHT_SENSOR_ENABLE || MW_BATTERY_MODULE_ENABLE
    ///< 电池/光感共用 PSADC, 两个采样线程已删, 关 ADC
    psadc_channel_disable(PSADC_CH_AUX6);
#endif
#if MW_CHEERYUSB_ENABLE
    ///< USB 5V: 控制器未初始化, 但 5V 使能脚一直是高
    mw_usbhost_sleep();
#endif

    ///< 6. 与已断电外设相连的引脚放开为高阻, 防止经 IO 保护二极管倒灌
    dev_pins_to_highz();
}

///< 下电后的低功耗动作. 必须在 pm_thread 里执行:
///< low_power_enter() 会删 CAN 业务线程, 不能在 CAN 线程自身上调用.
static void power_off_enter_low_power(void) {
#if POWER_LOW_SLEEP_ENABLE
    rt_pm_module_release(PM_POWER_ID, PM_SLEEP_MODE_NONE);
    rt_pm_module_request(PM_POWER_ID, PM_SLEEP_MODE_IDLE);
#elif POWER_CAN_LOW_SLEEP_ENABLE
    if (low_power_enter() != 0) {
        rt_log_info("[CAN-TEST] 进入低功耗失败\n");
        return;
    }
#endif
}

static void pm_thread(void *parameter) {
    rt_uint32_t event;

    while (1) {
        if (rt_event_recv(&usr_pm_event,
                          PM_EVENT_IGN_OFF | PM_EVENT_IGN_ON | PM_EVENT_WAKEUP,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER,
                          &event) != RT_EOK) {
            continue;
        }

        rt_log_info("PM recv event: 0x%08lx\n", (unsigned long)event);

        if (event & PM_EVENT_IGN_OFF) {
            ///< 统一下电入口(幂等) + 进低功耗, 全部在大栈线程执行
            power_off_request();
            ///< 先停显示与周期线程, 再让 CAN 成为唯一唤醒源并放行 PM
            dev_sleep_mode();
            power_off_enter_low_power();
        }

        if (event & PM_EVENT_IGN_ON) {
#if POWER_LOW_SLEEP_ENABLE
            if (rt_pm_get_sleep_mode() != PM_SLEEP_MODE_NONE) {
                rt_pm_module_request(PM_POWER_ID, PM_SLEEP_MODE_NONE);
            }
#elif POWER_CAN_LOW_SLEEP_ENABLE
            ///< 低功耗态下 IGN ON: 与 CAN 唤醒一致, 复位重启(整套 init 会重跑).
            ///< 非低功耗态的 IGN ON(含开机首次扫描/行驶中状态不变)不做任何事.
            if (low_power_is_active()) {
                rt_kprintf("PM: IGN ON in low power -> cpu reset\n");
                ///< 清 framebuffer, 避免 WDT 复位保留的残影透过开机动画显示
                power_pre_reset_cleanup();
                ///< rt_hw_cpu_reset 走看门狗(设超时->等复位), 期间锁 NONE
                ///< 保证系统不再进休眠
                rt_pm_module_request(PM_POWER_ID, PM_SLEEP_MODE_NONE);
                rt_thread_mdelay(10);   ///< 让串口日志先出完
                rt_hw_cpu_reset();
            }
#endif
        }
    }
}

int power_manage_init(void) {
    rt_err_t ret;
    rt_thread_t thread;

    ///< 事件对象和 pm_thread 必须先于 IGN 检测线程就绪:
    ///< IGN 线程首次扫描就会投递事件
    ret = rt_event_init(&usr_pm_event, "usr_pm_event", RT_IPC_FLAG_PRIO);
    if (ret != RT_EOK) {
        rt_log_error("Failed to initialize power manage event!\n");
        return -1;
    }

    thread = rt_thread_create("pm_thread", pm_thread, RT_NULL,
                              PM_THREAD_STACK, PM_THREAD_PRIO, 10);
    if (thread == RT_NULL) {
        rt_log_error("Failed to create power manage thread!\n");
        return -1;
    }
    rt_thread_startup(thread);

    if (ign_check_init() != 0) {
        rt_log_error("Failed to initialize IGN check module!\n");
        return -1;
    }

#if POWER_CAN_LOW_SLEEP_ENABLE
    if (low_power_init() != 0) {
        rt_log_error("low power init error!\n");
        return -1;
    }
#endif

    return 0;
}

#endif

///< 复位前清理. rt_hw_cpu_reset() 实际是看门狗复位, PSRAM 内容会保留,
///< 包括 framebuffer 里的最后一帧; 下次开机会透过开机动画的透明区看到残影.
///< 这里在复位前把 framebuffer 清零并刷 cache, 让 PSRAM 物理上保存的就是 0.
///< 放在 POWER_MANAGE_ENABLE 之外: 电池高低压保护的恢复重启也要用它
void power_pre_reset_cleanup(void) {
    struct rt_device_graphic_info info;
    rt_device_t fb;
    rt_uint32_t size;

    fb = rt_device_find("aicfb");
    if (fb == RT_NULL) {
        return;
    }

    if (rt_device_control(fb, RTGRAPHIC_CTRL_GET_INFO, &info) != RT_EOK ||
        info.framebuffer == RT_NULL || info.smem_len == 0) {
        rt_log_error("pre-reset: no fb info, skip cleanup\n");
        return;
    }

    ///< GET_INFO 返回的 smem_len 是单块 buffer 大小; PAN_DISPLAY 下驱动按
    ///< 2 块分配(当前未启用 AIC_FB_ROTATE_EN), 两块都要清
    size = info.smem_len;
#ifdef AIC_PAN_DISPLAY
    size *= 2;
#endif

    rt_memset(info.framebuffer, 0, size);
    ///< CPU 写完必须刷 cache, 否则 PSRAM 自刷新保存的还是 cache 里的旧值
    aicos_dcache_clean_invalid_range(info.framebuffer, size);

    rt_kprintf("PM: pre-reset cleanup, fb cleared %u bytes\n", size);
}

static volatile bool s_power_off_pending = false;

///< 下电清零里程: 是否清零由设置页"里程清零方式"(USR_PARAM_RESET_TYPE)决定,
///< 仅 RESET_TYPE_AFTER_POWER_OFF 时把 trip 写 0 并置"里程已变"标志, 与下电来源无关。
///< 走 force 路径 —— 此时闸门已置位, 普通 set_mile_param 会被自己挡掉。
///< 只写里程参数 + 标志, 不碰 LVGL —— 显示刷新由 UI 线程按标志重读
static void power_off_clear_trip(void) {
    uint8_t reset_type = RESET_TYPE_BY_BUTTON;
    uint32_t trip = 0;

    if (!get_usr_param(USR_PARAM_RESET_TYPE, &reset_type)) {
        rt_log_error("Get reset_type error!\n");
        return;
    }

    if (reset_type != RESET_TYPE_AFTER_POWER_OFF) {
        return;
    }

    if (!set_mile_param_force(MILE_PARAM_TRIP, &trip)) {
        rt_log_error("Clear trip data error!\n");
        return;
    }

    ///< 1:trip 已变 (ui_builder 侧据此重读并刷新显示)
    vehicle_set_data(VEH_MILEAGE_CHANGE_MSG, 1);
    rt_log_info("trip cleared on power off\n");
}

bool power_is_off_pending(void) {
    return s_power_off_pending;
}

void power_off_request(void) {
    if (s_power_off_pending) {
        ///< 幂等: 周期报文重复触发 / 多源并发下电只会生效一次
        return;
    }

    ///< 先置闸门, 封住此后所有非下电触发的落盘, 避免写入被断电截断
    s_power_off_pending = true;

    ///< 是否清零由 USR_PARAM_RESET_TYPE 决定
    power_off_clear_trip();

    if (shutdown_save_usr_param() == 0) {
        rt_log_info("dev will power off!\n");
    } else {
        rt_log_error("power off save usr param failed!\n");
    }
}