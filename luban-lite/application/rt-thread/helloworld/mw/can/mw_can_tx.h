#ifndef __MW_CAN_TX_H__
#define __MW_CAN_TX_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rtthread.h>
#include <string.h>
#include "rtdevice.h"
#include "dev_config.h"

#if MW_CAN_ENABLE
int can_tx_init(rt_device_t can);
void can_tx_sleep(void);   /* 由 mw_can_sleep 内部调用, 不对外暴露给上层 */

///< 整车CAN网络信号 (xlsx) — ifactive 触发型 TX 的请求接口
///< 0x451 整帧事件触发: cmd=TCS_Command(0无 1开 2关), bat=Bat_Seat(0无 1开), down=Power_Down(0无 1开)
void can_tx_set_tcs_command(uint8_t cmd);
void can_tx_set_bat_seat(uint8_t cmd);
void can_tx_set_power_down(uint8_t cmd);
///< act: 0x452 胎压匹配动作码, 0空闲 1前轮 2后轮 3前后 4中断
void can_tx_set_tpms_action(uint8_t act);
#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __MW_CAN_TX_H__