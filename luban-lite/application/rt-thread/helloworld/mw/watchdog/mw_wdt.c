#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/watchdog.h>
#include "mw_wdt.h"

///< 注意: 本文件不按 MW_WDT_ENABLE 裁剪. 该宏只决定 mw_init 是否"上电即拉起",
///< 模块实现必须始终存在, 否则 CAN 0x777 TripB=111 在宏为 0 时无法拉起看门狗.

#define MW_WDT_DEV_NAME         "wdt"
#define MW_WDT_TIMEOUT_S        15
#define MW_WDT_FEED_PERIOD_MS   5000
#define MW_WDT_HB_EXPIRE_MS     12000
#define MW_WDT_START_DELAY_MS   3000
#define MW_WDT_THREAD_STACK     2048
#define MW_WDT_THREAD_PRIO      25

static const char *const s_hb_name[MW_WDT_HB_NUM] = {
    [MW_WDT_HB_CAN]  = "can",
    [MW_WDT_HB_LVGL] = "lvgl",
};

///< 最近一次打点 tick + 1; 0 表示该点尚未激活, 不参与判活。
///< 单字写入天然原子, 无需加锁。
static volatile rt_tick_t s_hb_tick[MW_WDT_HB_NUM];
static rt_device_t s_wdt_dev = RT_NULL;
static rt_thread_t s_feed_tid = RT_NULL;
static volatile rt_bool_t s_started = RT_FALSE;

void mw_wdt_feed(mw_wdt_hb_e id)
{
    if ((rt_uint32_t)id >= MW_WDT_HB_NUM)
        return;

    s_hb_tick[id] = rt_tick_get() + 1;
}

static int wdt_check_heartbeat(void)
{
    rt_tick_t now = rt_tick_get();
    rt_tick_t expire = rt_tick_from_millisecond(MW_WDT_HB_EXPIRE_MS);
    rt_uint32_t i;

    for (i = 0; i < MW_WDT_HB_NUM; i++) {
        rt_tick_t stamp = s_hb_tick[i];

        if (stamp == 0)
            continue;

        if ((now + 1 - stamp) > expire) {
            rt_kprintf("[WDT] heartbeat '%s' lost, %u ms no update\n",
                       s_hb_name[i],
                       (unsigned)((now + 1 - stamp) * 1000 / RT_TICK_PER_SECOND));
            return -1;
        }
    }

    return 0;
}

int mw_wdt_start(void)
{
    rt_uint16_t timeout = MW_WDT_TIMEOUT_S;
    rt_err_t ret;

    if (s_wdt_dev == RT_NULL)
        return -RT_ERROR;

    if (s_started)
        return 0;

    /* 驱动按 uint16_t 解引用, 传参类型必须一致 */
    ret = rt_device_control(s_wdt_dev, RT_DEVICE_CTRL_WDT_SET_TIMEOUT, &timeout);
    if (ret != RT_EOK) {
        rt_kprintf("[WDT] set timeout failed: %d\n", ret);
        return -RT_ERROR;
    }

    ret = rt_device_control(s_wdt_dev, RT_DEVICE_CTRL_WDT_START, RT_NULL);
    if (ret != RT_EOK) {
        rt_kprintf("[WDT] start failed: %d\n", ret);
        return -RT_ERROR;
    }

    s_started = RT_TRUE;
    return 0;
}

void mw_wdt_stop(void)
{
    if (s_wdt_dev == RT_NULL || !s_started)
        return;

    rt_device_control(s_wdt_dev, RT_DEVICE_CTRL_WDT_STOP, RT_NULL);
    s_started = RT_FALSE;
}

///< 下电低功耗: 停狗 + 删喂狗线程.
///< 必须在下电流程删除 CAN/LVGL 业务线程之前调用: 那两个心跳点停掉后,
///< 喂狗线程会在心跳超时(12s)后主动停喂, 15s 内触发硬件复位.
void mw_wdt_sleep(void)
{
    mw_wdt_stop();

    if (s_feed_tid != RT_NULL) {
        rt_thread_delete(s_feed_tid);
        s_feed_tid = RT_NULL;
    }
}

static void wdt_feed_thread(void *param)
{
    (void)param;

    ///< 等业务线程与 UI 起来后再使能, 避免初始化耗时被误判为卡死
    rt_thread_mdelay(MW_WDT_START_DELAY_MS);

    if (mw_wdt_start() != 0) {
        rt_kprintf("[WDT] start failed, watchdog disabled\n");
        return;
    }

    rt_kprintf("[WDT] started, timeout %d s\n", MW_WDT_TIMEOUT_S);

    while (1) {
        rt_thread_mdelay(MW_WDT_FEED_PERIOD_MS);

        if (!s_started)
            continue;

        if (wdt_check_heartbeat() != 0) {
            ///< 故意不再喂狗: 交给硬件超时复位, 线程直接退出
            rt_kprintf("[WDT] stop feeding, system will reboot\n");
            return;
        }

        rt_device_control(s_wdt_dev, RT_DEVICE_CTRL_WDT_KEEPALIVE, RT_NULL);
    }
}

int mw_wdt_init(void)
{
    rt_thread_t tid;

    /* 幂等: 上电已拉起(MW_WDT_ENABLE=1)或此前已被 CAN 报文拉起时直接返回 */
    if (s_feed_tid != RT_NULL)
        return 0;

    s_wdt_dev = rt_device_find(MW_WDT_DEV_NAME);
    if (s_wdt_dev == RT_NULL) {
        rt_kprintf("[WDT] can not find '%s'\n", MW_WDT_DEV_NAME);
        return -1;
    }

    if (rt_device_init(s_wdt_dev) != RT_EOK) {
        rt_kprintf("[WDT] device init failed\n");
        s_wdt_dev = RT_NULL;
        return -1;
    }

    tid = rt_thread_create("wdt_feed", wdt_feed_thread, RT_NULL,
                           MW_WDT_THREAD_STACK, MW_WDT_THREAD_PRIO, 10);
    if (tid == RT_NULL) {
        rt_kprintf("[WDT] create thread failed\n");
        return -1;
    }

    s_feed_tid = tid;
    rt_thread_startup(tid);
    return 0;
}
