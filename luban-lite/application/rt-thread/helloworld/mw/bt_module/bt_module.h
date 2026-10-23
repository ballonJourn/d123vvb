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