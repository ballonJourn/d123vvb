#include "vehicle_mile.h"
#include "vehicle_data.h"
#include "vehicle_param.h"
#include "mile_param.h"
#include "set_param.h"


///< false:参数未准备好 true:已准备好
bool vehicle_get_mile_recovery()
{
#if !ON_PC_CACLE
    return get_recovery_mile_param() ;
#endif

    return true ;
}

//odo_value
uint32_t vehicle_get_mile_odo()
{
    uint32_t value ;
#if !ON_PC_CACLE
    if (get_mile_param(MILE_PARAM_ODO, &value) )
        return value ;
#endif
    return 0 ;

}

void vehicle_set_mile_odo(double value)
{
    uint32_t __value = (uint32_t)value ;
#if !ON_PC_CACLE
    if(!set_mile_param(MILE_PARAM_ODO, &__value))
        LV_LOG_ERROR("Save odo data error!\r\n");
#endif

    return ;
}

//tripA
uint32_t vehicle_get_mile_tripA()
{
    uint32_t value ;
#if !ON_PC_CACLE
    if (get_mile_param(MILE_PARAM_TRIP, &value) )
        return value ;
#endif
    return 0 ;

}

void vehicle_set_mile_tripA(double value)
{
    uint32_t __value = (uint32_t)value ;
#if !ON_PC_CACLE
    if(!set_mile_param(MILE_PARAM_TRIP, &__value) )
        LV_LOG_ERROR("Save tripA data error!\r\n");
#endif

    return ;
}


//tripB
uint32_t vehicle_get_mile_tripB()
{
    uint32_t value ;
#if !ON_PC_CACLE
    if (get_mile_param(MILE_PARAM_TRIP, &value) )
        return value ;
#endif
    return 0 ;

}

void vehicle_set_mile_tripB(double value)
{
    uint32_t __value = (uint32_t)value ;
#if !ON_PC_CACLE
    set_mile_param(MILE_PARAM_TRIP, &__value) ;
#endif

    return ;
}



int32_t vehicle_get_mile_changed()
{
#if !ON_PC_CACLE
    uint32_t state = 0 ;
    state = vehicle_get_data(VEH_MILEAGE_CHANGE_MSG);
    if (state)
        vehicle_set_data(VEH_MILEAGE_CHANGE_MSG , 0);

    return state;
#endif
    return  0 ;
}

/* ---- 续航里程 (RemainMile): 参考里程数组存于 usr_param 的 ref_mile_arr ---- */

///< 数组长度宏必须与中间层一致 (不一致时编译报错, 避免两处定义漂移)
typedef char vehicle_ref_mile_len_check[
    (REF_MILE_ARR_NUM == VEHICLE_REF_MILE_ARR_LEN) ? 1 : -1];

bool vehicle_get_ref_mile_arr(uint8_t pack, uint8_t *arr)
{
#if !ON_PC_CACLE
    if (pack < 1 || pack > REF_MILE_PACK_NUM || !arr)
        return false ;

    return get_usr_param((usr_param_handle_e)(USR_PARAM_REF_MILE_ARR_P1 + pack - 1),
                         arr) ;
#endif

    return false ;
}

bool vehicle_set_ref_mile_arr(uint8_t pack, const uint8_t *arr)
{
#if !ON_PC_CACLE
    if (pack < 1 || pack > REF_MILE_PACK_NUM || !arr)
        return false ;

    return set_usr_param((usr_param_handle_e)(USR_PARAM_REF_MILE_ARR_P1 + pack - 1),
                         (void *)arr) ;
#endif

    return false ;
}

void vehicle_set_mile_endurance(uint16_t km)
{
#if !ON_PC_CACLE
    vehicle_set_data(VEH_MILEAGE_ENDURANCE_A, (int32_t)km) ;
#endif

    return ;
}

void vehicle_set_mile_range(uint16_t km)
{
#if !ON_PC_CACLE
    if(!set_mile_param(MILE_PARAM_RANGE, &km))
        LV_LOG_ERROR("Save range data error!\r\n");
#endif

    return ;
}

bool vehicle_get_mile_range(uint16_t *km)
{
#if !ON_PC_CACLE
    if (!km)
        return false ;

    return get_mile_param(MILE_PARAM_RANGE, km) ;
#endif

    return false ;
}

void vehicle_set_ref_mile_count(uint16_t centikm)
{
#if !ON_PC_CACLE
    vehicle_set_data(VEH_REF_MILE_COUNT_MILE, (int32_t)centikm) ;
#endif

    return ;
}