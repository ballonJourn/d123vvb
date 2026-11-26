/*
 * WiFi 连通性测试命令（配合 netutils 版 iperf 使用）
 *
 * bt_module 只调 rt_wlan_set_mode(STA) 拉起 aic8800 和蓝牙固件，
 * 不会关联 AP，所以 wlan0 一直是 LINK_DOWN / 0.0.0.0，iperf 无法工作。
 * 本模块只提供手动连接/断开命令，用于开发阶段的吞吐测试：
 *
 *   wifi_join <ssid> [password]   -- 关联 AP 并等待 DHCP 就绪（<=15s）
 *   wifi_leave                    -- 断开
 *
 * 连接成功后用 ifconfig 确认 IP，再执行 iperf。
 */

#include "dev_config.h"

#if WIFILESS_TEST_ENABLE

#include <rtthread.h>
#include <rtdevice.h>
#include <wlan_mgnt.h>

#define WIFI_JOIN_TIMEOUT_MS    15000
#define WIFI_JOIN_POLL_MS       100

static int wifi_join(int argc, char **argv)
{
    const char *ssid;
    const char *password = RT_NULL;
    rt_err_t ret;
    int waited = 0;

    if (argc < 2) {
        rt_kprintf("Usage: wifi_join <ssid> [password]\n");
        return -1;
    }

    ssid = argv[1];
    if (argc >= 3)
        password = argv[2];

    ret = rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION);
    if (ret != RT_EOK) {
        rt_kprintf("wifi_join: set STA mode failed (%d)\n", ret);
        return -1;
    }

    ret = rt_wlan_connect(ssid, password);
    if (ret != RT_EOK) {
        rt_kprintf("wifi_join: connect request failed (%d)\n", ret);
        return -1;
    }

    /* rt_wlan_connect() 是异步的，这里等协议栈就绪（DHCP 完成） */
    while (!rt_wlan_is_ready() && waited < WIFI_JOIN_TIMEOUT_MS) {
        rt_thread_mdelay(WIFI_JOIN_POLL_MS);
        waited += WIFI_JOIN_POLL_MS;
    }

    if (rt_wlan_is_ready()) {
        rt_kprintf("wifi_join: \"%s\" ready, check IP with ifconfig\n", ssid);
        return 0;
    }

    rt_kprintf("wifi_join: \"%s\" not ready after %d ms\n",
               ssid, WIFI_JOIN_TIMEOUT_MS);
    return -1;
}
MSH_CMD_EXPORT(wifi_join, join AP for iperf: wifi_join <ssid> [password]);

static int wifi_leave(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rt_wlan_disconnect();
    rt_kprintf("wifi_leave: disconnected\n");
    return 0;
}
MSH_CMD_EXPORT(wifi_leave, disconnect from AP);

#endif /* WIFILESS_TEST_ENABLE */
