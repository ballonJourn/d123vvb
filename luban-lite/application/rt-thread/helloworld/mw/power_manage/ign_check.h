#ifndef __IGN_CHECK_H__
#define __IGN_CHECK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>
#include <stdbool.h>
#include "dev_config.h"

#if POWER_MANAGE_ENABLE

#define POWER_CTRL_PIN  "PD.0"   
#define IGN_DET_PIN     "PC.0"  

typedef enum {
    IGN_STATE_UNKNOWN = 0,
    IGN_STATE_OFF,          ///< ACC 关闭 */
    IGN_STATE_ON            ///< ACC 开启 */
} ign_state_t;

int ign_check_init(void);
ign_state_t get_ign_state(void);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __IGN_CHECK_H__