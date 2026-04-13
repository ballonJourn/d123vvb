#include "vehicle_mile.h"
#include "vehicle_data.h"
#include "vehicle_param.h"

#undef ON_PC_CACLE
#define ON_PC_CACLE 1

static double odo_value = 0.0  ;
static double tripA = 0.0 ;
static double tripB = 0.0 ;

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
    if (get_hcn_mile_param(HCN_MILE_PARAM_ODO, &value) )
        return value ;
#endif
    return odo_value ;

}

void vehicle_set_mile_odo(double value) 
{
    uint32_t __value = (uint32_t)value ;
#if !ON_PC_CACLE
    set_hcn_mile_param(HCN_MILE_PARAM_ODO, &__value) ;
#endif

    odo_value = value ;
    return ;
}

//tripA
uint32_t vehicle_get_mile_tripA()
{
    uint32_t value ;
#if !ON_PC_CACLE
    if (get_hcn_mile_param(HCN_MILE_PARAM_TRIP_A, &value) )
        return value ;
#endif
    return tripA ;

}

void vehicle_set_mile_tripA(double value) 
{
    uint32_t __value = (uint32_t)value ;
#if !ON_PC_CACLE
    set_hcn_mile_param(HCN_MILE_PARAM_TRIP_A, &__value) ;
#endif
    tripA = value ;
    return ;
}

void vehicle_save_mile_tripA(void) 
{
    uint32_t trip_data = 0;
#if !ON_PC_CACLE
    if (get_hcn_mile_param(HCN_MILE_PARAM_TRIP_A, &trip_data)) {
        if (send_mcu_set_trip_a(trip_data) != 0) {
            hcn_log_error("Save trip a data error!\r\n");
        }
    } else {
        hcn_log_error("Get trip a param error!\r\n");
    }
#endif
}

//tripB
uint32_t vehicle_get_mile_tripB()
{
    uint32_t value ;
#if !ON_PC_CACLE
    if (get_hcn_mile_param(HCN_MILE_PARAM_TRIP_B, &value) )
        return value ;
#endif
    return tripB ;

}

void vehicle_set_mile_tripB(double value) 
{
    uint32_t __value = (uint32_t)value ;
#if !ON_PC_CACLE
    set_hcn_mile_param(HCN_MILE_PARAM_TRIP_B, &__value) ;
#endif
    tripB = value ;
    return ;
}


void vehicle_save_odo_mileage(void) 
{
    uint32_t odo_data = 0;
#if !ON_PC_CACLE
    if (get_hcn_mile_param(HCN_MILE_PARAM_ODO, &odo_data)) {
        if (send_mcu_set_odo(odo_data) != 0) {
            hcn_log_error("Save odo data error!\r\n");
        }
    } else {
        hcn_log_error("Get odo param error!\r\n");
    }
#endif
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
