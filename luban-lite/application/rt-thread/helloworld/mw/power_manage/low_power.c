/*
 * low_power.c — 低功耗状态管理(阶段1: IDLE 档跑通闭环)
 *
 * 目标: 进入低功耗后只保留 CAN 接收作为唤醒源, 收到总线活动即复位重启。
 *
 * 闭环:
 *   can_sleep_test sleep
 *     -> low_power_enter()
 *          -> mw_can_light_sleep()   停 CAN 业务线程, 保留设备+中断,
 *                                    控制器进 Sleep Mode, INTEN 只留 WAKEUP
 *          -> 释放 NONE            让 PM 进入 LIGHT(降频/关 PLL/tickless)
 *     -> idle 线程进入休眠(aic_pm_enter_light_sleep)
 *   [总线活动] -> CAN 唤醒中断 -> aic_can_wakeup_notify() (ISR)
 *     -> 释放信号量 -> low_power 线程 -> rt_hw_cpu_reset()
 *
 * 说明:
 *   - 唤醒后直接复位重启, 不需要恢复任何业务状态(整套 init 会重跑)
 *   - 本阶段收发器保持 Normal 模式, 因此收发器自身仍是 mA 级功耗;
 *     真正的 μA 级待机要等确认收发器 standby 下 RXD 行为后再做
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <drivers/pm.h>
#include "app_log.h"
#include "low_power.h"
#include "power_manage.h"
#include "mw_can.h"
#include "mw_can_tx.h"
#include "aic_hal_can.h"

#if POWER_MANAGE_ENABLE

#define LP_THREAD_STACK     2048
#define LP_THREAD_PRIO      10      /* 高于 idle, 保证唤醒后能立刻跑 */

static struct rt_semaphore  s_wake_sem;
static volatile rt_bool_t   s_active = RT_FALSE;
static rt_bool_t            s_inited = RT_FALSE;

/* 唤醒现场记录(调试用): 判断"真被总线活动唤醒"还是"进休眠瞬间的虚假唤醒" */
static volatile rt_tick_t   s_enter_tick;
static volatile rt_tick_t   s_wake_tick;
static volatile u32         s_wake_intr;
static volatile u32         s_wake_stat;

rt_bool_t low_power_is_active(void)
{
    return s_active;
}

/* 覆盖 drv_can.c 里的弱符号, 由 CAN 驱动的唤醒中断(CAN_EVENT_WAKEUP)调用。
 * 运行在 ISR 上下文, 只做寄存器取样 + 信号量释放, 不做任何耗时操作。
 * 注意: 本函数由 hal_can_error_handle() 调用, 驱动的 writel(int_status, INTR)
 * 在其之后, 所以此刻 INTR 里的标志还没被清掉, 抓到的才是真现场。 */
void aic_can_wakeup_notify(void)
{
    if (!s_active)
        return;                     /* 非低功耗态的唤醒中断(如误触发), 忽略 */

    s_wake_tick = rt_tick_get();
    s_wake_intr = readl(CAN0_BASE + CAN_INTR_REG);
    s_wake_stat = readl(CAN0_BASE + CAN_STAT_REG);

    rt_sem_release(&s_wake_sem);
}

static void low_power_thread(void *parameter)
{
    while (1) {
        rt_sem_take(&s_wake_sem, RT_WAITING_FOREVER);

        if (!s_active)
            continue;

        /* 唤醒后第一件事: 锁住 PM, 禁止系统再进休眠.
         * 关键原因: 看门狗是 PM 设备, 进 LIGHT/DEEP 时 aic_wdt_suspend() 会
         * hal_clk_disable(CLK_WDT)(drv_wdt.c:252) 把看门狗时钟关掉。复位流程里
         * 有 aicos_msleep, 期间若 idle 线程又把系统送进休眠, 看门狗就停止计数,
         * 要等下次被定时器唤醒才恢复 —— 实测复位被拖到 36 秒。
         * 锁成 NONE 后 PM 选档恒为 NONE, 系统全程保持运行, 看门狗正常计时。 */
        rt_pm_module_request(PM_POWER_ID, PM_SLEEP_MODE_NONE);

        rt_kprintf("[LP] CAN wakeup -> cpu reset\n");
        rt_kprintf("[LP]   进休眠后 %u ms 被唤醒 | 唤醒时 INTR=0x%08x STAT=0x%08x"
                   " | 当前 MODE=0x%08x RXERR=%u TXERR=%u\n",
                   (unsigned)(s_wake_tick - s_enter_tick),
                   (unsigned)s_wake_intr, (unsigned)s_wake_stat,
                   (unsigned)readl(CAN0_BASE + CAN_MODE_REG),
                   (unsigned)(readl(CAN0_BASE + CAN_RXERR_REG) & 0xFF),
                   (unsigned)(readl(CAN0_BASE + CAN_TXERR_REG) & 0xFF));
        /* 清 framebuffer: WDT 复位会保留 PSRAM, 最后一帧会透过开机动画显示成残影 */
        power_pre_reset_cleanup();

        rt_thread_mdelay(10);       /* 让串口日志先出完 */
        rt_hw_cpu_reset();         /* 不返回 */
    }
}

int low_power_init(void)
{
    rt_thread_t tid;

    if (s_inited)
        return 0;

    rt_sem_init(&s_wake_sem, "lp_wake", 0, RT_IPC_FLAG_PRIO);

    tid = rt_thread_create("low_power", low_power_thread, RT_NULL,
                           LP_THREAD_STACK, LP_THREAD_PRIO, 10);
    if (tid == RT_NULL) {
        rt_log_error("create low_power thread failed!\n");
        return -1;
    }
    rt_thread_startup(tid);

    s_inited = RT_TRUE;
    return 0;
}

#if MW_POWER_SLEEP_LOG_ENABLE
static rt_timer_t s_diag_timer = RT_NULL;

static void lp_diag_timer_cb(void *param)
{
    rt_tick_t next = rt_timer_next_timeout_tick();

    rt_kprintf("[LP] diag: pm_mode=%d (1=IDLE 2=LIGHT), next_timer_in=%d tick\n",
               rt_pm_get_sleep_mode(), (int)(next - rt_tick_get()));
}

///< 诊断: LIGHT 下 UART0 的时钟会被设备 suspend 关掉, 要把控制台标记成
///< 唤醒源才能看到休眠期间的日志; 再挂一个 3s 一次性定时器打印 PM 档位
static void lp_diag_start(void)
{
    rt_device_t cons = rt_device_find(RT_CONSOLE_DEVICE_NAME);

    if (cons != RT_NULL)
        rt_device_wakeup_enable(cons, RT_TRUE);

    if (s_diag_timer == RT_NULL) {
        s_diag_timer = rt_timer_create("lp_diag", lp_diag_timer_cb, RT_NULL,
                                       rt_tick_from_millisecond(3000),
                                       RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);
    }
    if (s_diag_timer != RT_NULL)
        rt_timer_start(s_diag_timer);
}
#endif

int low_power_enter(void)
{
    if (s_active) {
        rt_kprintf("[LP] already in low power\n");
        return 0;
    }

    rt_kprintf("[LP] entering low power, CAN/IGN are wake sources\n");

    /* 1. 必须先置标记: mw_can_light_sleep() 内部一旦把控制器切到 Sleep Mode,
     *    繁忙总线上的活动会立刻触发唤醒中断。若标记晚于该动作, 唤醒中断
     *    会被 aic_can_wakeup_notify() 当成"非低功耗态"丢弃, 导致永远不重启。 */
    s_active = RT_TRUE;
    s_enter_tick = rt_tick_get();

    /* 2. CAN 轻休眠: 保留设备与 CAN0_IRQn, 控制器进 Sleep Mode */
    mw_can_light_sleep();

    /* 3. 释放 PM 框架初始化时默认申请的 NONE 模式, 让系统进入 LIGHT 档:
     *    CPU/总线切 24M OSC、关 CPU PLL 和显示 PLL、tickless(1000/s -> 事件驱动),
     *    并发起各设备 suspend.
     *    aicfb 的 PM 回调已按 power_on 加了守卫: 本流程已 POWEROFF 关屏,
     *    PM 不会再把面板/DE 时钟打开, 因此不会再出现"唤醒一次亮一次".
     *    CAN 已在 mw_can_light_sleep() 里标记为唤醒源, 它的时钟不会被 suspend 掉.
     *    (若步骤2期间已被唤醒, low_power 线程会并发走复位流程, 这里不冲突) */
#if MW_POWER_SLEEP_LOG_ENABLE
    lp_diag_start();
#endif
    rt_pm_module_release(PM_POWER_ID, PM_SLEEP_MODE_NONE);

    return 0;
}

#endif /* POWER_MANAGE_ENABLE */
