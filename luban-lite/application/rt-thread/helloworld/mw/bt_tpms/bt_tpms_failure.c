#include <rtthread.h>
#include <stdbool.h>
#include "bt_tpms_failure.h"
#include "bt_tpms.h"
#include "mw_log.h"

#if MW_BT_TPMS_ENABLE
#define BT_TPMS_DEBUG


#define BT_TPMS_FAILURE_TIMER_PERIOD (2000) //ms

static rt_timer_t switchTimer = RT_NULL;
static uint8_t iLastFailure = BT_TPMS_RECENT_STABLE_STATE;
static uint8_t btTpmsFailureArr[BT_TPMS_FAILURE_END] = {0};


#ifdef BT_TPMS_DEBUG
static void bt_tpms_fault_printf(uint8_t iPos)
{
    switch (iPos)
    {
        case BT_TPMS_FRONT_LOW_PRESSURE:
            mw_logd("BT_TPMS_FRONT_LOW_PRESSURE.\n");
            break;

        case BT_TPMS_FRONT_HIGH_PRESSURE:
            mw_logd("BT_TPMS_FRONT_HIGH_PRESSURE.\n");
            break;

        case BT_TPMS_FRONT_HIGH_TEMP:
            mw_logd("BT_TPMS_FRONT_HIGH_TEMP.\n");
            break;

        case BT_TPMS_FRONT_QUICK_AIR_LEAK:
            mw_logd("BT_TPMS_FRONT_QUICK_AIR_LEAK.\n");
            break;

        case BT_TPMS_FRONT_SLOW_AIR_LEAK:
            mw_logd("BT_TPMS_FRONT_SLOW_AIR_LEAK.\n");
            break;

        case BT_TPMS_FRONT_AERATE:
            mw_logd("BT_TPMS_FRONT_AERATE.\n");
            break;

        case BT_TPMS_FRONT_LOW_VOLTAGE:
            mw_logd("BT_TPMS_FRONT_LOW_VOLTAGE.\n");
            break;

        case BT_TPMS_FRONT_NOT_PAIR:
            mw_logd("BT_TPMS_FRONT_NOT_PAIR.\n");
            break;

        case BT_TPMS_REAR_LOW_PRESSURE:
            mw_logd("BT_TPMS_REAR_LOW_PRESSURE.\n");
            break;

        case BT_TPMS_REAR_HIGH_PRESSURE:
            mw_logd("BT_TPMS_REAR_HIGH_PRESSURE.\n");
            break;

        case BT_TPMS_REAR_HIGH_TEMP:
            mw_logd("BT_TPMS_REAR_HIGH_TEMP.\n");
            break;

        case BT_TPMS_REAR_QUICK_AIR_LEAK:
            mw_logd("BT_TPMS_REAR_QUICK_AIR_LEAK.\n");
            break;

        case BT_TPMS_REAR_SLOW_AIR_LEAK:
            mw_logd("BT_TPMS_REAR_SLOW_AIR_LEAK.\n");
            break;

        case BT_TPMS_REAR_AERATE:
            mw_logd("BT_TPMS_REAR_AERATE.\n");
            break;

        case BT_TPMS_REAR_LOW_VOLTAGE:
            mw_logd("BT_TPMS_REAR_LOW_VOLTAGE.\n");
            break;

        case BT_TPMS_REAR_NOT_PAIR:
             mw_logd("BT_TPMS_REAR_NOT_PAIR.\n");
             break;

        default:
            break;

    }
}
#endif /* BT_TPMS_DEBUG */


static void bt_tpms_failure_switch(void *parameter)
{
    int iPos = iLastFailure + 1;
    if (iPos > BT_TPMS_REAR_NOT_PAIR)
    {
        iPos = BT_TPMS_FRONT_LOW_PRESSURE;
    }
    bool findFailure = false;
    for (; iPos <= BT_TPMS_REAR_NOT_PAIR; iPos++)
    {
        if (btTpmsFailureArr[iPos] != 0)
        {
            findFailure = true;
            break;
        }
    }
    if (!findFailure)
    {
        for (iPos = BT_TPMS_FRONT_LOW_PRESSURE; iPos <= iLastFailure; iPos++)
        {
            if (btTpmsFailureArr[iPos] != 0)
            {
                findFailure = true;
                break;
            }
        }
    }
    if (findFailure)
    {
        if (iLastFailure != iPos)
        {
            iLastFailure = iPos;
            #ifdef BT_TPMS_DEBUG
            bt_tpms_fault_printf(iPos);
            #endif
            rt_timer_start(switchTimer);
        }
    }
    else
    {
        iLastFailure = BT_TPMS_RECENT_STABLE_STATE;
        mw_logd("BT_TPMS_RECENT_STABLE_STATE");
    }
}

void bt_tpms_failure_start(uint8_t iRet)
{
    if (iRet > 0)
    {
        if (switchTimer == RT_NULL)
        {
            switchTimer = rt_timer_create("bt_tpms_fault", bt_tpms_failure_switch, RT_NULL,
                                          BT_TPMS_FAILURE_TIMER_PERIOD,
                                          RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);
            if (switchTimer == RT_NULL)
            {
                mw_loge("create bt tpms failure timer failed!\n");
                return;
            }
        }
        rt_timer_start(switchTimer);
    }
}

uint8_t bt_tpms_get_cur_failure(void)
{
    return iLastFailure;
}



uint8_t bt_tpms_get_failure(uint8_t tpmsFault)
{
    if (tpmsFault >= BT_TPMS_RECENT_STABLE_STATE
        && tpmsFault < BT_TPMS_FAILURE_END)
    {
        return btTpmsFailureArr[tpmsFault];
    }
    return 0;
}

uint8_t bt_tpms_set_failure(uint8_t tpmsFault, uint8_t value)
{
    if (tpmsFault > BT_TPMS_RECENT_STABLE_STATE
        && tpmsFault < BT_TPMS_FAILURE_END)
    {
        if (btTpmsFailureArr[tpmsFault] != value)
        {
            btTpmsFailureArr[tpmsFault] = value;
            return 1;
        }
    }
    return 0;
}

#endif /* MW_BT_TPMS_ENABLE */