#ifndef __JSON_PARAM_PARSE_H__
#define __JSON_PARAM_PARSE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "set_param.h"
#ifdef _WIN32
#include "../../ui_config.h"
#include "lv_conf.h"
#endif

#define SET_PARAM_PREFIX 		"setparam"
#define CONFIG_FILE_SUFFIX		".json"
#ifndef _WIN32
#define CONFIG_SRC_PATH			"/data/set_param"
#define DEST_CONFIG_FILE_PATH 	"/temp"
#else
#define WIN32_ROOT_PATH			LV_FS_WIN32_PATH
#define CONFIG_SRC_PATH			RES_PRFIX"set_param"
#define DEST_CONFIG_FILE_PATH 	RES_PRFIX"temp"
#define WIN32_DEST_CONFIG_PATH	"temp"
#define WIN32_DEST_SRC_PATH		"set_param"
#endif

#define NAME_STR_LEN			(32)

///< 6 main json object
#define SYSTEM_OBJECT    	    "system"
#define TPMS_OBJECT		 	    "tpms"
#define UPDATE_OBJECT    	    "update"
#define MAINTENANCE_OBJECT	    "maintenance"
#define RIDE_INFO_OBJECT        "rideinfo"
#define UUID_INFO_OBJECT        "uuid"
#define RESERVE_OBJECT		    "reserve"
#define RESERVE1_OBJECT		    "reserve1"
#define REF_MILE_OBJECT         "ref_mile"

#define RESERVE_SUB_OBJECT       "reserve"
#define INT_RESERVE_SUB_OBJECT   "int_reserve"

///< ref_mile json object: 三包参考里程数组, 平铺编号键 (8bit × 0.01km)
#define REF_MILE_SUB_FMT         "p%d_ref_mile%d"

///< system json object
#define S_THEME		            "theme"
#define S_MILE_DIS              "mile_display"
#define S_LED_LEVEL	            "led_levl"
#define S_DIS_UNIT              "dis_uint"
#define S_LANGUAGE              "language"
#define S_DIS_MODE              "set_dis_mode"
#define S_LINK_TYPE             "link_type"
#define S_BT_SWITCH             "bt_switch"
#define S_UUID_STATE            "uuid_active"
#define S_START_SCR             "start_scr"
#define S_TCS_SWITCH            "tcs_switch"
#define S_TEMP_UNIT             "temp_unit"
#define S_TIME_FORMAT           "time_foramt"
#define S_TPMS_UNIT             "tpms_unit"
#define S_AUTO_HEADLIGHT        "auto_hedlight"
#define S_SYS_LOG               "sys_log"
#define S_DRIVE_MODE            "drive_mode"
#define S_RESET_TYPE            "reset_type"

///< tpms json object
#define T_L_F_TPMS_ID           "l_front_tpms_id"
#define T_L_R_TPMS_ID           "l_rear_tpms_id"
#define T_R_F_TPMS_ID           "r_front_tpms_id"
#define T_R_R_TPMS_ID           "r_rear_tpms_id"
#define T_L_F_PRESSURE          "l_front_pressure"
#define T_L_R_PRESSURE          "l_rear_pressure"
#define T_R_F_PRESSURE          "r_front_pressure"
#define T_R_R_PRESSURE          "r_rear_pressure"
#define T_L_F_TEMP               "l_front_temp"
#define T_L_R_TEMP               "l_rear_temp"
#define T_R_F_TEMP               "r_front_temp"
#define T_R_R_TEMP               "r_rear_temp"
///< 蓝牙胎压传感器 MAC (12 位 hex 字符串, 如 "AABBCCDDEEFF")
#define T_L_F_MAC               "l_front_mac"
#define T_L_R_MAC               "l_rear_mac"

///< update json object
#define U_UPDATE_MCU            "update_mcu"
#define U_UPDATE_MCU_LEN        "mcu_len"

///< maintenance json object
#define M_COUNT                 "count"
#define M_CUR_MAIN_MI           "cur_main_mileage"
#define M_LAST_MAIN_MI          "last_main_mileage"
#define M_CUR_MAIN_DAYS         "cur_main_days"
#define M_IS_SET_MAIN_TIME      "is_set_main_time"
#define M_LAST_MAIN_DATE        "last_main_date"
#define M_SUB_OBJECT_YEAR       "year"
#define M_SUB_OBJECT_MONTH      "month"
#define M_SUB_OBJECT_DAY        "day"

///< ride info
#define R_RIDE_TIME_A           "ride_time_a"             
#define R_RIDE_TIME_B           "ride_time_b"             

///< carlink uuid
#define C_CARLINK_UUID          "carlink_uuid"

/**
 * @brief 读取设置参数
 * @param wu
 * @return true:成功 false失败
 */
bool read_set_param(void);

/**
 * @brief 获取设置参数指针
 * @param 无
 * @return 设置参数
 */
usr_param_t get_set_param(void);

/**
 * @brief 设置设置参数
 * @param 无
 * @return 无
 */
void set_set_param(usr_param_t param);

/**
 * @brief 保存设置参数文件
 * @param 无
 * @return true:成功  false:失败
 */
bool save_set_param_file(void);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __JSON_PARAM_PARSE_H__