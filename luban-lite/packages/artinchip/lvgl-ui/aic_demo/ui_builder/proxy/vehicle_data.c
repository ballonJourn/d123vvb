
#include <stddef.h>
#include <stdbool.h>
#include "vehicle_data.h"
#include "vehicle_param.h"

#if !ON_PC_CACLE
// #include "common/version/hcn_version.h"
// #include "uart_communicate/hcn_uart_parse_cmd.h"
#endif

typedef struct {
  signal_e signal_lamp;
  veh_data_e veh_data;
} Signal_Lamp_Mapping_t;

#define VEH_INVALID_VALUE INT32_MIN

static const Signal_Lamp_Mapping_t signalMaps[] = {
    {ICON_GMS         , VEH_BT_PHONE_SIGNAL     } ,
    {ICON_BT         , VEH_BT_CONNECTED_STATUS } ,
    {ICON_GPS          , VEH_LIGHT_GPS           } ,
    {ICON_HIGH_BEAM   , VEH_LIGHT_HIGH_BEAM     } ,
    {ICON_LEFT        , VEH_INDICATOR_TURN_LEFT } ,
    {ICON_READY       , VEH_LIGHT_READY         } ,
    {ICON_RIGHT       , VEH_INDICATOR_TURN_RIGHT} ,
    {ICON_AUTO_BEAM   , VEH_AUTO_HEADLIGH       } ,
    {ICON_ECU     , VEH_LIGHT_ENGINE_FAULT  } ,
    {ICON_TCS    , VEH_TCS_WARNING         } ,               
    {ICON_ABS    , VEH_LIGHT_ABS           } ,
    {ICON_BRAKE       , VEH_LIGHT_BRAKE         } ,
};

static const int drvModeMaps[] = {
    [0] = DRV_MODE_N ,
    [1] = DRV_MODE_E ,
    [2] = DRV_MODE_S ,
};



int32_t vehicle_get_data_signal_lamp(signal_e lamp)
{
    int count = sizeof(signalMaps) / sizeof(Signal_Lamp_Mapping_t) ;
    for (size_t i = 0; i < count; i++){
        if (signalMaps[i].signal_lamp == lamp){
            #if  ON_PC_CACLE == 0 
            return vehicle_get_data(signalMaps[i].veh_data);
            #endif
        }
    }
    return VEH_INVALID_VALUE;
}


int32_t vehicle_get_data_speed()
{
#if !ON_PC_CACLE
    int32_t speed = vehicle_get_data(VEH_SPEED_CURRENT) ;
    return speed > SPEED_MAX ? SPEED_MAX : speed;
#endif

    return 0 ; 
}


int32_t vehicle_get_data_rpm()
{
#if !ON_PC_CACLE
    int32_t rpm = vehicle_get_data(VEH_SPEED_ENGINE);
    return rpm > RPM_MAX ? RPM_MAX : rpm;
#endif

    return 0 ; 
}


int32_t vehicle_get_data_gear() 
{
#if !ON_PC_CACLE
    int32_t gear_id = vehicle_get_data(VEH_GEAR_POSITION);
    return (gear_id > GEAR_MAX) ?  GEAR_N : gear_id ;
#endif 

    return 0 ; 

}


int32_t vehicle_get_data_power() 
{

#if !ON_PC_CACLE
    int32_t power = vehicle_get_data(VEH_TRAM_POWR);
    return power > POWER_MAX ? POWER_MAX : power;
#endif

    return 0 ; 
}

int32_t vehicle_get_data_drv_mode() 
{
#if !ON_PC_CACLE
    int32_t drv_mode = vehicle_get_data(VEH_DRIVE_MODE);
    return drv_mode > DRV_MODE_S ? VEH_INVALID_VALUE : drvModeMaps[drv_mode] ;
#endif

    return 0 ; 
}

int32_t vehicle_get_data_remain_battary() 
{
#if !ON_PC_CACLE
    int32_t remain_battary = vehicle_get_data(VEH_TRAM_REMAIN_BATTARY);
    return remain_battary;
#endif

    return 0 ; 
}

#if 0 //暂未开启

//mode  0：day 1:night
int32_t vehicle_get_data_current_display() 
{
#if !ON_PC_CACLE
    int32_t value = vehicle_get_data(VEH_CUR_DISPALY_MODE);
    return ( value == 0 ) ? DIAPLAY_DAY_OPTION : DIAPLAY_NIGHT_OPTION ;
#endif

    return 0 ; 
}


const char* veicle_get_data_version()
{
#if !ON_PC_CACLE
    return get_soc_version();
#endif
    return " ";
}

const char* veicle_get_data_mcu_ver()
{
#if !ON_PC_CACLE
    return get_mcu_version();
#endif
    return " ";
}

/////< 0:not enter  1:enter ota page  2:exit ota page
void vehicle_set_ota_page_state(int32_t state)
{
#if !ON_PC_CACLE
    vehicle_set_data(VEH_ENTER_OTA_PAGE_STATE , state);
#endif
}


int32_t vehicle_get_ota_state()
{
#if !ON_PC_CACLE
    int32_t ota_state = vehicle_get_data(VEH_OTA_START_STATUS);
    return ota_state ;
#endif   
    return 0 ;
}

#endif