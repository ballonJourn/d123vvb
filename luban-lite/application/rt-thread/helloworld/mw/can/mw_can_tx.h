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
#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __MW_CAN_TX_H__