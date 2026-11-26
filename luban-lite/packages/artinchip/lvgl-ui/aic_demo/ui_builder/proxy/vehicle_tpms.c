#include "vehicle_tpms.h"
#include "vehicle_data.h"
#include "dev_config.h"
#include "bt_tpms.h"   /* 中间层：蓝牙胎压配对接口（仅 proxy 层可引用） */

///< 取值必须与中间层一致（不一致时编译报错，避免两处定义漂移）
typedef char vehicle_tpms_state_check[
    (VEHICLE_TPMS_PAIR_IDLE     == BT_TPMS_PAIR_STATE_IDLE &&
     VEHICLE_TPMS_PAIR_LEARNING == BT_TPMS_PAIR_STATE_LEARNING &&
     VEHICLE_TPMS_PAIR_SUCCESS  == BT_TPMS_PAIR_STATE_SUCCESS &&
     VEHICLE_TPMS_PAIR_TIMEOUT  == BT_TPMS_PAIR_STATE_TIMEOUT) ? 1 : -1];

typedef char vehicle_tpms_slot_check[
    (VEHICLE_TPMS_LEFT_FRONT == TPMS_LEFT_FRONT &&
     VEHICLE_TPMS_LEFT_REAR  == TPMS_LEFT_REAR) ? 1 : -1];

int32_t vehicle_tpms_pair_start(uint8_t slot)
{
#if !ON_PC_CACLE && MW_BT_TPMS_ENABLE
    return (int32_t)bt_tpms_pair_start((tpms_pos_e)slot);
#else
    return -1;
#endif
}

int32_t vehicle_tpms_pair_stop(uint8_t slot)
{
#if !ON_PC_CACLE && MW_BT_TPMS_ENABLE
    return (int32_t)bt_tpms_pair_stop((tpms_pos_e)slot);
#else
    return -1;
#endif
}

int32_t vehicle_tpms_pair_clear(uint8_t slot)
{
#if !ON_PC_CACLE && MW_BT_TPMS_ENABLE
    return (int32_t)bt_tpms_pair_clear((tpms_pos_e)slot);
#else
    return -1;
#endif
}

uint8_t vehicle_tpms_pair_get_state(uint8_t slot)
{
#if !ON_PC_CACLE && MW_BT_TPMS_ENABLE
    return (uint8_t)bt_tpms_pair_get_state((tpms_pos_e)slot);
#else
    return VEHICLE_TPMS_PAIR_IDLE;
#endif
}

int32_t vehicle_tpms_get_pressure(uint8_t slot)
{
#if !ON_PC_CACLE && MW_BT_TPMS_ENABLE
    /* bt_tpms_get_data() 的下标是 FRONT_TIRE(1)/REAR_TIRE(0)，与配对接口用的
     * tpms_pos_e 是两套约定，这里做一次显式映射 */
    uint8_t pos;
    BT_TPMS_TIRE_INFO_T *info;

    if (slot == VEHICLE_TPMS_LEFT_FRONT)
        pos = FRONT_TIRE;
    else if (slot == VEHICLE_TPMS_LEFT_REAR)
        pos = REAR_TIRE;
    else
        return -1;

    info = bt_tpms_get_data(pos);
    if (!info || info->pairState != 2 || info->lostSensor || info->bootStale)
        return -1;

    return (int32_t)info->tpmsPressure;
#else
    (void)slot;
    return -1;
#endif
}

int32_t vehicle_tpms_get_temp(uint8_t slot)
{
#if !ON_PC_CACLE && MW_BT_TPMS_ENABLE
    uint8_t pos;
    BT_TPMS_TIRE_INFO_T *info;

    if (slot == VEHICLE_TPMS_LEFT_FRONT)
        pos = FRONT_TIRE;
    else if (slot == VEHICLE_TPMS_LEFT_REAR)
        pos = REAR_TIRE;
    else
        return -1;

    info = bt_tpms_get_data(pos);
    if (!info || info->pairState != 2 || info->lostSensor || info->bootStale)
        return -1;

    return (int32_t)info->tpmsTemp;
#else
    (void)slot;
    return -1;
#endif
}