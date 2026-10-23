#ifndef __BT_TPMS_FAILURE_H__
#define __BT_TPMS_FAILURE_H__


#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>


void bt_tpms_failure_start(uint8_t iRet);
uint8_t bt_tpms_get_cur_failure(void);
uint8_t bt_tpms_get_failure(uint8_t tpmsFault);
uint8_t bt_tpms_set_failure(uint8_t tpmsFault, uint8_t value);


#ifdef __cplusplus

}
#endif

#endif