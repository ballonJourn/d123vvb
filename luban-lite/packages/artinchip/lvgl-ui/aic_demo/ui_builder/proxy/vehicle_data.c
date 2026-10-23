
#include <stddef.h>
#include <stdbool.h>
#include "vehicle_data.h"
#include "vehicle_param.h"
#include "usr_utils.h"

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
    {ICON_MW         , VEH_DTC_CODE     } ,
    {ICON_TW         , VEH_LIGHT_TIRE           } ,
    {ICON_TC          , VEH_LIGHT_GPS           } ,
    {ICON_HIGH_BEAM   , VEH_LIGHT_HIGH_BEAM     } ,
    {ICON_NEAR_BEAM   , VEH_LIGHT_LOW_BEAM       } ,
    {ICON_LEFT        , VEH_INDICATOR_TURN_LEFT } ,
    {ICON_RIGHT       , VEH_INDICATOR_TURN_RIGHT} ,
    {ICON_ECU     , VEH_LIGHT_ENGINE_FAULT  } ,
    {ICON_TCS    , VEH_TCS_WARNING         } ,
    {ICON_ABS    , VEH_LIGHT_ABS           } ,
    {ICON_BRAKE       , VEH_LIGHT_BRAKE         } ,
    {ICON_CC_STATUS   , VEH_CRUISE_MAIN_SWITCH       } ,
    {ICON_VCU         , VEH_MCU_DTC            } ,
    {ICON_BATTERY     , VEH_BAT_DTC_P1   } ,
    {ICON_CHARGE      , VEH_CHARGER_STATUS} ,
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
    /* gear_e 数值即 VEH_GEAR_POSITION 原始编码, 直接透传; 非法值 clamp */
    int32_t raw = vehicle_get_data(VEH_GEAR_POSITION);
    return (raw < 0 || raw >= GEAR_MAX) ? GEAR_MAX : raw;
#endif

    return 0 ;

}


int32_t vehicle_get_data_power() 
{

#if !ON_PC_CACLE
    int32_t power = vehicle_get_data(VEH_TRAM_POWR);
    if(power < POWER_MIN)
        power = POWER_MIN;
    return power > POWER_MAX ? POWER_MAX : power;
#endif

    return 30000 ; 
}

int32_t vehicle_get_data_drv_mode() 
{
#if !ON_PC_CACLE
    int32_t drv_mode = vehicle_get_data(VEH_DRIVE_MODE);
    return drv_mode > DRV_MODE_S ? VEH_INVALID_VALUE : drvModeMaps[drv_mode] ;
#endif

    return 0 ;
}

///< 驾驶模式索引：直接透传 CAN VEH_DRIVE_MODE 原始编码 (0 normal / 1 eco / 2 sport)，
///< 与 mode_index_e 的顺序一致，供 home_refresh_mode_index 使用。
///< 不要改用 vehicle_get_data_drv_mode()：那个返回 drv_mode_e，其 0/1 顺序与
///< mode_index_e 相反，会把 normal / eco 显示颠倒。
int32_t vehicle_get_data_mode_index()
{
#if !ON_PC_CACLE
    return vehicle_get_data(VEH_DRIVE_MODE);
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

///< 各电池包 SOC(%)：pack 1/2/3 对应 BMS P1/P2/P3
int32_t vehicle_get_data_bat_soc(uint8_t pack)
{
#if !ON_PC_CACLE
    switch (pack) {
        case 1: return vehicle_get_data(VEH_BAT_SOC_P1);
        case 2: return vehicle_get_data(VEH_BAT_SOC_P2);
        case 3: return vehicle_get_data(VEH_BAT_SOC_P3);
    }
#endif
    return 0;
}

///< 故障码查询: BMS1/2/3 取各包 DTC 码, MCU 取 8bit 故障位图
int32_t vehicle_get_fault_code(veh_fault_item_e item)
{
#if !ON_PC_CACLE
    switch (item) {
        case VEH_FAULT_BMS1: return vehicle_get_data(VEH_BAT_DTC_P1);
        case VEH_FAULT_BMS2: return vehicle_get_data(VEH_BAT_DTC_P2);
        case VEH_FAULT_BMS3: return vehicle_get_data(VEH_BAT_DTC_P3);
        case VEH_FAULT_MCU:  return vehicle_get_data(VEH_MCU_DTC);
        default: break;
    }
#endif
    return 0;
}

///< CAN 弹窗消息码: 0 无, 1..N 对应 pop_tip_code_e
int32_t vehicle_get_can_popup_msg(void)
{
#if !ON_PC_CACLE
    return vehicle_get_data(VEH_CAN_POPUP_MSG);
#endif
    return 0;
}

void vehicle_set_can_popup_msg(int32_t code)
{
#if !ON_PC_CACLE
    vehicle_set_data(VEH_CAN_POPUP_MSG, code);
#endif
}

int32_t vehicle_get_battery_voltage(void)
{
#if !ON_PC_CACLE
    return vehicle_get_data(VEH_VOLTAGE_BATTERY);
#endif
    return 0;
}

const char* veicle_get_data_version()
{
#if !ON_PC_CACLE
    return get_soc_version();
#endif
    return " ";
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