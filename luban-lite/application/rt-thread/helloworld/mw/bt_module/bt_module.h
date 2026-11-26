#ifndef __BT_MODULE_H__
#define __BT_MODULE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rtthread.h>
#include <stdint.h>
#include "dev_config.h"

#if MW_BT_MODULE_ENABLE

int bt_module_init(void);

/**
 * @brief 下电低功耗: 删除蓝牙栈/队列线程, 关 UART2, 断开 BT/WiFi 模块供电
 * @note  硬件侧: PD.2(BT_WIFI_PWR)拉低断电 + PD.3(使能)拉低 + SDMC0 保持复位;
 *        仅在下电休眠路径调用, 唤醒走复位重启, 由 boot 重新拉起 WiFi/BT
 */
void bt_module_sleep(void);

/*!
    \brief: 上层向蓝牙栈下发一条 AT 指令, 形如 "AT#CA\r\n"
    \note:  响应/指示通过栈内部的 rx_queue 异步上报, 需自行解析
*/
rt_err_t bt_module_send_at_cmd(const char *atcmd);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __BT_MODULE_H__