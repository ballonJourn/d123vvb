/*
 * BYD 板 (nor-1024x600) 上的 ble_navi 板级接入。
 *
 * 公版 demo88 上 WiFi 由别的模块先拉起, `ble_navi start` 只需等串口出现
 * "rwnx_fdrv_init exit"。本工程里拉起 wlan 的是 bt_module.c (GOC 栈),
 * 打开 LPKG_USING_BLE_NAVI 后它不再编译 (见 dev_config.h), 所以这里接手:
 *
 *   rt_wlan_set_mode(STA) -> aic8800_init -> rwnx_fdrv_init -> aicbt_init
 *   (经 SDIO 下载 BT patch, 把 BT 配到 UART2 1.5 Mbps + 硬件流控)
 *
 * 时序与 bt_module.c 相同: 上电等 4 s, set_mode 后再等 6 s。
 * 用 STA 口, 不和亿连投屏的 AP 口 (wlan1) 抢接口。
 *
 * 供电: PD.2 (BT_WIFI_PWR, 模块总电源) 由 drv_sdmc.c::sys_pwr_init() 拉高;
 * PD.3 (AIC_WIRELESS_PWR_GPIO, 使能) 由 aic8800 驱动的上电时序控制。
 * 不配置 AIC_DEV_AIC8800_BT_RST_GPIO (为空时 plat_port.c 不动任何脚):
 * GOC 栈在本板一直这样工作; 公版的 PD.6 在本板是 USB5V, 不能照抄。
 */
#include <rtthread.h>

#ifdef LPKG_USING_BLE_NAVI

#include <wlan_mgnt.h>

#define BYD_BLE_NAVI_AUTOSTART      1       /* 0: 开机后手动 `ble_navi start` */
#define BYD_WLAN_PWR_ON_DELAY_MS    4000    /* 等 aic8800 SDIO 上电稳定 */
#define BYD_WLAN_UP_DELAY_MS        6000    /* 等 wlan 驱动 + BT patch 下载完成 */

extern int ble_navi_init(void);

/* 覆盖 ble_service.c 里的 RT_WEAK 版本, ble_navi_init() 开头调用 */
int ble_navi_bt_vendor_init(void)
{
    static int s_done;
    rt_err_t ret;

    if (s_done)
        return 0;

    rt_kprintf("[ble_navi_byd] wlan0 up -> AIC8800 BT patch\n");
    ret = rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION);
    if (ret != RT_EOK) {
        rt_kprintf("[ble_navi_byd] rt_wlan_set_mode(%s) failed: %d\n",
                   RT_WLAN_DEVICE_STA_NAME, (int)ret);
        return -1;
    }
    rt_thread_mdelay(BYD_WLAN_UP_DELAY_MS);
    s_done = 1;
    rt_kprintf("[ble_navi_byd] BT controller should be up\n");
    return 0;
}

#if BYD_BLE_NAVI_AUTOSTART
static void ble_navi_byd_entry(void *p)
{
    (void)p;
    rt_thread_mdelay(BYD_WLAN_PWR_ON_DELAY_MS);
    ble_navi_init();
}

static int ble_navi_byd_autostart(void)
{
    rt_thread_t t = rt_thread_create("ble_navi_up", ble_navi_byd_entry, RT_NULL,
                                     4096, 12, 10);
    if (t == RT_NULL) {
        rt_kprintf("[ble_navi_byd] create thread failed\n");
        return -1;
    }
    rt_thread_startup(t);
    return 0;
}
INIT_APP_EXPORT(ble_navi_byd_autostart);
#endif

#endif /* LPKG_USING_BLE_NAVI */
