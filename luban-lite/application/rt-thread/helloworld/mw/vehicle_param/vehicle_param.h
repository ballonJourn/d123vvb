#ifndef __VEHICLE_PARAM_H__
#define __VEHICLE_PARAM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ///< Battery and Fuel Data
    VEH_VOLTAGE_BATTERY = 0x00,    ///< Battery voltage, precision 0.1
    VEH_FUEL_CONSUMPTION_A,        ///< Fuel consumption A, precision 0.1
    VEH_FUEL_CONSUMPTION_B,        ///< Fuel consumption B, precision 0.1
    VEH_FUEL_CONSUMPTION_INSTANT,  ///< Instant fuel consumption, precision 0.001

    ///< Speed Data
    VEH_SPEED_AVG_A,    ///< Average speed A (km/h), precision 0.1
    VEH_SPEED_AVG_B,    ///< Average speed B (km/h), precision 0.1
    VEH_SPEED_CURRENT,  ///< Current vehicle speed (km/h)
    VEH_SPEED_ENGINE,   ///< Engine speed （rpm）

    ///< Power
    VEH_TRAM_POWR,              ///< Tram powr, unit:KW
    VEH_TRAM_REMAIN_BATTARY,    ///< Tram remain,percentage(0-100)

    ///< Mileage Data
    VEH_MILEAGE_ENDURANCE_A,    ///< Endurance mileage A (km), 整数精度: 0x453 原值直取
    VEH_MILEAGE_ENDURANCE_B,    ///< Endurance mileage B (km), 整数精度
    VEG_AVG_FUEL_CONSUMPTION_A, ///< Average fuel consumption A, precision 0.1
    VEG_AVG_FUEL_CONSUMPTION_B, ///<Average fuel consumption B, precision 0.1
    VEH_MILEAGE_TOTAL,          ///< Total mileage (m),precision 1
    VEH_MILEAGE_SUB_A,          ///< Subtotal mileage A (km), precision 0.1
    VEH_MILEAGE_SUB_B,          ///< Subtotal mileage B (km), precision 0.1

    ///< Temperature Data
    VEH_TEMP_WATER,        ///< Water temperature (℃ ), precision 0.1
    VEH_TEMP_WATER_LEVEL,  ///< Coolant temperature level, 0-5 levels, 0:open circuit 
    VEH_TEMP_ENGINE_TEMP,  ///< engine temp(℃),precision 0.1

    ///< Light and Indicator Status
    VEH_LIGHT_HIGH_BEAM,        ///< High beam, 0: off, 1: on
    VEH_LIGHT_LOW_BEAM,         ///< Low beam, 0: off, 1: on
    VEH_INDICATOR_TURN_LEFT,    ///< Left turn signal, 0: off, 1: on
    VEH_INDICATOR_TURN_RIGHT,   ///< Right turn signal, 0: off, 1: on
    VEH_LIGHT_LOCATION,         ///< Location LED status, 0: off, 1: on
    VEH_LIGHT_OIL_PRESSURE,     ///< Oil pressure lamp, 0: off, 1: on
    VEH_LIGHT_ENGINE_FAULT,     ///< Engine fault light, 0: off, 1: on
    VEH_LIGHT_TIRE,             ///< Tire alarm 0: off, 1: on
    VEH_LIGHT_AUTO_START_STOP,  ///< 0: off, 1: on
    VEH_LIGHT_ABS,              ///< 0: off, no show , 1: ABS on
    VEH_LIGHT_WIFI,             ///< 0: off, 1: on
    VEH_AUTO_HEADLIGH,          ///< 0：off, 1:on
    VEH_LIGHT_BRAKE,            ///< 0：off, 1:on
    VEH_LIGHT_READY,            ///< 0：off, 1:on
    VEH_LIGHT_GPS,             ///< 0：off, 1:on
    VEH_LIGHT_GB,             ///< 0：off, 1:on

    ///< Gear and Oil Data
    VEH_GEAR_POSITION,  ///< Gear position, 0/1: Park 2: Drive 3: Reverse
    VEH_OIL_LEVEL,      ///< Oil level, scale value: 0-5
    VEH_OIL_RESISTANCE,  ///< Oil resistance 0x0:short circuit  0xFFF:open circuit;

    ///< Ignition and Side Stand Status
    VEH_IGNITION_SIGNAL,  ///< Ignition electric signal, 0: OFF, 1: ON
    VEH_IGNITION_STATUS,  ///< Ignition status, 0: OFF, 1: ON
    VEH_TILT_SWITCH,      ///< Veh tilt switch 0: OFF, 1: ON
    VEH_SIDE_STAND,       ///< Side stand status, 0: not deployed, 1: deployed

    ///< Maintenance Reminder
    VEH_MAINT_REMINDER,  ///< Maintenance reminder, 0: hidden, 1: required

    ///< WIFI
    VEH_CARLINK_CONNECTED,  ///< 0:disconnect  1:connect
    VEH_CARLINK_URL_STATUS,  ///< carlink url status 0: no url 1: has url
 
    ///< Bluetooth and Phone Status
    VEH_BT_DEV_STATE,  ///< Bluetooth device state, >=1: powered on and working
    VEH_BT_SWITCH_STATUS,     ///< Bluetooth switch status, 0: off, 1: on
    VEH_BT_CONNECTED_STATUS,  ///< Bluetooth connection status, 0: disconnected,
                              ///< 1: connected
    VEH_BT_CONECTED_REMOTE_DEV, ///< Bluetooth connection remote bt dev: 0: disconnected, 1: connected
    VEH_BT_PHONEBOOK_STATE,  ///< Phonebook state, 0: not downloaded, 1:
                             ///< downloading, 2: completed
    VEH_BT_PHONEBOOK_COUNT,  ///< Phonebook contact count
    VEH_BT_CALLLOG_STATE,  ///< Call log state, 0: not downloaded, 1: downloading,
                           ///< 2: completed
    VEH_BT_CALLLOG_COUNT,  ///< Call log count
    VEH_BT_CALL_STATE,     ///< Bluetooth call state, 0: unsupported, 1-6: various
                           ///< states
    VEH_BT_PHONE_SIGNAL,   ///< Phone signal level, 0-5
    VEH_BT_PHONE_BATTERY,  ///< Phone battery level, 0-5

    ///< Weather and Navigation
    VEH_QUETY_WEATHER_STATUS, ///< Query weather status 0: no 1: yes
    VEH_WEATHER_TYPE,  ///< Weather type, 0: none, 1-7: various weather types
    VEH_ENV_TEMP,   ///< Environmental temperature (℃)
    VEH_NAV_MODE,      ///< Navigation mode, 0: none, 1: basic, 2: full screen
    VEH_EASY_NAV_STATUS,    ///< Navigation status, 0: easy navi off, 1: easy navi on

    ///< Miscellaneous
    VEH_LICENSE_AUTH_STATUS,  ///< License authentication status, 0: not registered, 1:
                            ///< registered
    VEH_ACC_OFF_STATUS,  ///< ACC off status, 0: connected, 1: disconnected

    ///< Traction Control System (TCS)
    VEH_TCS_ENABLED,  ///< TCS allow status, 0: not allowed, 1: allowed
    VEH_TCS_MODE,     ///< TCS mode, 0:3 levels 0x01:2 levles 0x02：1 level 0x03：0FF
    VEH_TCS_SLASH_WARING, ///< TCS slash warning lamp 0x00:off 1:on
    VEH_TCS_WARNING,   ///< TCS warning lamp, 0: 熄灭, 1: 闪烁， 2：常亮
    VEH_TCS_SYS_STATE,  ///< TCS system state 0x0:init 0x01:Normal 0x02:Fault 0x03：Diagnostic
    VEH_TCS_CONTROL,   ///< TCS降扭请求标志位, 0: Deactive, 1: Active
    VEH_TCS_WORKSTATUS,  ///< TCS工作状态 0x0:开启 0x01:关闭 0x02:故障

    ///< ABS
    VEH_ABS_MODE,       ///< 0:3 levels 0x01:2 levles 0x02：1 level 0x03：0FF
    VEH_ABS_ENALE,      ///< abs allow status, 0: not allowed, 1: allowed
    VEH_DRIVE_MODE,     ///< drive mode:0x0:normal 0x01:eco 0x02:sport 

    ///< Launch Control
    VEH_LAUNCH_CONTROL_STATUS,  ///< Launch control status, 0: OFF, 1: ON
    VEH_LAUNCH_CONTROL_SWITCH,  ///< Launch control switch, 0: OFF, 1: ON

    ///< altitude and compass
    VEH_ALTITUDE_INFO,          ///< Altitude unit:m  precision 0.1
    VEH_COMPASS,                ///< 0:north 1:west 2:south 3：east
    VEH_COMPASS_COURSE,         ///< angle  precision 0.1
    
    ///< clean odo status
    VEH_CLEAR_ODO,             ///< clear odo status:0:no  1:yes
    VEH_TBOX_SET_MILE,         ///< Tbox set odo or trip: 0:No 1:yes

    ///< display info
    VEH_CUR_DISPALY_MODE,    ///< auto display mode  0：day 1:night

    ///< Cruise infp 
    VEH_CRUISE_MAIN_SWITCH, ///< Cruise main switch 0: OFF, 1: ON
    VEH_CRUISE_STATUS,      ///< Cruise status 0: OFF, 1: ON
    VEH_CRUISE_CONTROL_SPEED, ///< Cruise control speed Km/h
    
    ///< enter ota page state
    VEH_ENTER_OTA_PAGE_STATE,  ///< 0:not enter  1:enter ota page  2:exit ota page
    VEH_OTA_START_STATUS,      ///< 0:not start  1:starting  2:Startup successful

    VEH_MILEAGE_CHANGE_MSG, ///< 0:not change, 1:tip a 2:trip b 3:odo

    ///< 整车CAN网络信号 (xlsx 整车CAN通信网络信号) — 新增槽位
    VEH_NFC_MATCH_STATE,      ///< NFC 读卡器匹配状态 0:空闲 1:学卡中 2:失败 3:成功
    VEH_DISCHARGE_BAT_NUM,    ///< 放电电池包号 bitmap
    VEH_WORK_MODE,            ///< VCU 工作模式 0:下电 1:上电 3:空闲 4:自下电 5:上电充电 6:下电充电 7:蓄电池充电
    VEH_VEHICLE_TYPE,         ///< 车型编号 0:初始 1:XT1_A 2:XT1_B 3:XT1_MAX 4:XT2 5:XT3
    VEH_HEAT_HANDLE,          ///< 手把加热 0:off 1:on
    VEH_SEAT_STATE,           ///< 坐垫锁状态 0:闭合 1:开
    VEH_DTC_CODE,             ///< 通用 DTC 故障码 (VCU/ABS 等复用)
    VEH_EMERGENCY_SHUTDOWN,   ///< VCU 紧急断电 0:不断电 1:断电
    VEH_MCU_DTC,              ///< MCU 故障码
    VEH_MCU_FAULT_LEVEL,      ///< MCU 故障等级 0:Normal 1~5:Level1~5
    VEH_MCU_SLIDE_FEED,       ///< MCU 滑行回馈功率 (Kw), precision 0.001, offset -30
    VEH_MCU_BRAKE_FEED,       ///< MCU 制动回馈功率 (Kw), precision 0.001, offset -30
    ///< BMS 电池包信号 — 按包区分 (P1/P2/P3 对应 1/2/3 号电池包)
    VEH_BAT_MAX_TEMP_P1,      ///< 1号包最高温度 (℃), offset -40
    VEH_BAT_MAX_TEMP_P2,      ///< 2号包最高温度 (℃), offset -40
    VEH_BAT_MAX_TEMP_P3,      ///< 3号包最高温度 (℃), offset -40
    VEH_BAT_VOLTAGE_P1,       ///< 1号包总电压 (V), precision 0.1
    VEH_BAT_VOLTAGE_P2,       ///< 2号包总电压 (V), precision 0.1
    VEH_BAT_VOLTAGE_P3,       ///< 3号包总电压 (V), precision 0.1
    VEH_BAT_P_VOLT_P1,        ///< 1号包 P+ 电压 (V), precision 0.1
    VEH_BAT_P_VOLT_P2,        ///< 2号包 P+ 电压 (V), precision 0.1
    VEH_BAT_P_VOLT_P3,        ///< 3号包 P+ 电压 (V), precision 0.1
    VEH_BAT_SOH_P1,           ///< 1号包 SOH (%) 0-100
    VEH_BAT_SOH_P2,           ///< 2号包 SOH (%) 0-100
    VEH_BAT_SOH_P3,           ///< 3号包 SOH (%) 0-100
    VEH_BAT_SOC_P1,           ///< 1号包 SOC (%) 0-100
    VEH_BAT_SOC_P2,           ///< 2号包 SOC (%) 0-100
    VEH_BAT_SOC_P3,           ///< 3号包 SOC (%) 0-100
    VEH_BAT_WORK_STATUS_P1,   ///< 1号包工作状态 0:空闲 1:放电 2:充电 3:下电
    VEH_BAT_WORK_STATUS_P2,   ///< 2号包工作状态 0:空闲 1:放电 2:充电 3:下电
    VEH_BAT_WORK_STATUS_P3,   ///< 3号包工作状态 0:空闲 1:放电 2:充电 3:下电
    VEH_BAT_DTC_P1,           ///< 1号包 DTC 故障码 (一/二/三级复用, 后到后得)
    VEH_BAT_DTC_P2,           ///< 2号包 DTC 故障码
    VEH_BAT_DTC_P3,           ///< 3号包 DTC 故障码
    VEH_TPMS_MATCH_STATE,     ///< 胎压匹配状态码 0~7
    VEH_HORN,                 ///< 喇叭按键 0:空闲 1:触发
    VEH_BTN_WHEEL,            ///< 滚轮按键 0:空闲 1:触发
    VEH_ROLLER_DIRE,          ///< 滚轮转动方向 0:空闲 1:下滑 2:上滑
    VEH_ROLLER_LEN,           ///< 滚轮转动长度 0:空闲 1:一页 2:两页
    VEH_BTN_REVERSE,          ///< 倒车按键 0:空闲 1:触发
    VEH_BTN_MODE,             ///< Mode 按键 0:空闲 1:触发
    VEH_CHARGER_STATUS,       ///< 充电器状态 0:禁止 1:准备 2:充电中
    VEH_CHARGER_CURRENT,      ///< 实际充电电流 (A), precision 0.1
    VEH_CHARGER_VOLTAGE,      ///< 实际充电电压 (V), precision 0.1
    VEH_CHARGER_DTC,          ///< 充电器故障码

    VEH_CAN_POPUP_MSG,        ///< CAN 弹窗消息码, 0:无, 1..N:对应 pop_tip_code_e

    ///< 续航里程调试 (0x455 CountMile_Sum, 1号包当前 SOC 窗口内累计里程, 8bit 无法承载故用 16bit, 单位 0.01km)
    VEH_REF_MILE_COUNT_MILE,

    ///< 0x501 SwitchLeft 摇杆按键 (新表 byte6 bit1/bit2)
    VEH_ROCKER_LEFT,          ///< 摇杆向左 0:空闲 1:触发
    VEH_ROCKER_RIGHT,         ///< 摇杆向右 0:空闲 1:触发

    ///< 蓝牙胎压数值 (两轮: 前=左前, 后=左后)
    VEH_TPMS_FRONT_PRESSURE,  ///< 前轮胎压 (kPa)
    VEH_TPMS_FRONT_TEMP,      ///< 前轮胎温 (℃)
    VEH_TPMS_REAR_PRESSURE,   ///< 后轮胎压 (kPa)
    VEH_TPMS_REAR_TEMP,       ///< 后轮胎温 (℃)

    VEH_DATA_END,
} veh_data_e;

void vehicle_set_data(veh_data_e id, int32_t value);
int32_t vehicle_get_data(veh_data_e id);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __VEHICLE_PARAM_H__