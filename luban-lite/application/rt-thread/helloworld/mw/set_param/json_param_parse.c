#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "dev_config.h"
#include "mw_log.h"
#include "json_param_parse.h"
#include "cJSON.h"
#include "json_utils.h"
#include "set_param.h"
#include "find_file_name.h"

#define return_if_fail(p, main_object, sub_object) 				   \
  if (!(p)) {                               	   				   \
    mw_loge("Load [%s]:%s failed!\n", main_object, sub_object);    \
    return;                                        				   \
}

#define return_value_if_fail(p, main_object, sub_object, value)   \
  if (!(p)) {                                                     \
    mw_loge("Load [%s]:%s failed!\n", main_object, sub_object);   \
    return (value);                                               \
}

static FILE *param_file = NULL;
static char *file_data = NULL;
static long file_length = 0;
static usr_param_t set_param;
static char car_model[NAME_STR_LEN] = {0};
static char param_ver[NAME_STR_LEN] = {0};
static char src_path[128] = {0};

static char * open_param_file(const char * file_path, const char *mode) {
    if (file_path && mode) {
        param_file =  fopen(file_path, mode);
        if (param_file == NULL) {
            mw_loge("Error opening %s\n", file_path);
            return NULL;
        }

        fseek(param_file, 0, SEEK_END);
        file_length = ftell(param_file);
        fseek(param_file, 0, SEEK_SET);

        if (file_data == NULL) {
            file_data = (char *)malloc(file_length + 1);
            if (file_data) {
                fread(file_data, 1, file_length, param_file);
                file_data[file_length] = '\0';
                fclose(param_file);
                param_file = NULL;
                return file_data;
            }
        } else {
			mw_loge("file_data is not null\n");
			return file_data;
		}
    }

    return NULL;
}

static bool read_json_data(char *data) {
    if (data == NULL) {
        mw_loge("Parse json data pointer null\n");
        return false;
    }

    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            mw_loge("Json parse Error before: %s\n", error_ptr);
        }
        free(data);
        cJSON_Delete(json);
        return false;
    }

	char str[32];
	int i;
	
	//system
    cJSON *main_object = cJSON_GetObjectItemCaseSensitive(json, SYSTEM_OBJECT);
	if (cJSON_IsObject(main_object)) {
		uint8_t idata = 0;
		
		cJSON *temp = cJSON_GetObjectItemCaseSensitive(main_object, S_THEME);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_THEME, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.theme = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_MILE_DIS);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_MILE_DIS, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.mile_display = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_LED_LEVEL);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_LED_LEVEL, false);
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.brightness = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_DIS_UNIT);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_DIS_UNIT, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.mile_format = idata;
		
		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_LANGUAGE);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_LANGUAGE, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.language = idata;

#if 0
		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_DIS_MODE);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_DIS_MODE, false);
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.sys.set_display_mode = idata;		
#endif
		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_LINK_TYPE);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_LINK_TYPE, false);
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.phone_type = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_BT_SWITCH);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_BT_SWITCH, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.language = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_BT_SWITCH);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_BT_SWITCH, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.bt_switch = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_UUID_STATE);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_UUID_STATE, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.uuid_active_staus = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_START_SCR);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_START_SCR, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.start_src = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_TCS_SWITCH);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_TCS_SWITCH, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.tcs_switch = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_TEMP_UNIT);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_TEMP_UNIT, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.temp_unit = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_TIME_FORMAT);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_TIME_FORMAT, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.time_format = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_TPMS_UNIT);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_TPMS_UNIT, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.tpms_unit = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_AUTO_HEADLIGHT);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_AUTO_HEADLIGHT, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.auto_headlight = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_SYS_LOG);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_SYS_LOG, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.sys_log = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, S_DRIVE_MODE);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, S_DRIVE_MODE, false);		
		idata = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.usr_set.drive_mode = idata;

		memset(str, 0, sizeof(str));
		snprintf(str, sizeof(str), "%s%d", INT_RESERVE_SUB_OBJECT, 10);
		temp = cJSON_GetObjectItemCaseSensitive(main_object, str);
		return_value_if_fail(temp != NULL, SYSTEM_OBJECT, str, false);
		set_param.usr_set.usr_reserve = cJSON_GetNumberIntValue(temp);	
	} else {
		return_value_if_fail(main_object != NULL, SYSTEM_OBJECT, "main_json", false);	
	}
	
	//tpms
	main_object = cJSON_GetObjectItemCaseSensitive(json, TPMS_OBJECT);
	if (cJSON_IsObject(main_object)) {
		uint16_t data = 0;
		uint32_t uin32_data = 0;
		int8_t tpms_data = 0;

		cJSON *temp = cJSON_GetObjectItemCaseSensitive(main_object, T_L_F_TPMS_ID);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_L_F_TPMS_ID, false);
		uin32_data = (uint32_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[0].tpms_id = uin32_data;	

		temp = cJSON_GetObjectItemCaseSensitive(main_object, T_L_R_TPMS_ID);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_L_R_TPMS_ID, false);
		uin32_data = (uint32_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[1].tpms_id = uin32_data;	

		temp = cJSON_GetObjectItemCaseSensitive(main_object, T_R_F_TPMS_ID);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_R_F_TPMS_ID, false);
		uin32_data = (uint32_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[2].tpms_id = uin32_data;	

		temp = cJSON_GetObjectItemCaseSensitive(main_object, T_R_R_TPMS_ID);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_R_R_TPMS_ID, false);
		uin32_data = (uint32_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[3].tpms_id = uin32_data;	

		temp = cJSON_GetObjectItemCaseSensitive(main_object, T_L_F_PRESSURE);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_L_F_PRESSURE, false);
		data = (uint16_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[0].tpms_pressure = data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, T_L_R_PRESSURE);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_L_R_PRESSURE, false);
		data = (uint16_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[1].tpms_pressure = data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, T_R_F_PRESSURE);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_R_F_PRESSURE, false);
		data = (uint16_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[2].tpms_pressure = data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, T_R_R_PRESSURE);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_R_R_PRESSURE, false);
		data = (uint16_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[2].tpms_pressure = data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, T_L_F_TEMP);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_L_F_TEMP, false);
		tpms_data = (int8_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[0].tpms_temp = tpms_data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, T_L_R_TEMP);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_L_R_TEMP, false);
		tpms_data = (int8_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[1].tpms_temp = tpms_data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, T_R_F_TEMP);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_R_F_TEMP, false);
		tpms_data = (int8_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[2].tpms_temp = tpms_data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, T_R_R_TEMP);
		return_value_if_fail(temp != NULL, TPMS_OBJECT, T_R_R_TEMP, false);
		data = (int8_t)cJSON_GetNumberIntValue(temp);
		set_param.tpms[3].tpms_temp = data;
	} else {
		return_value_if_fail(main_object != NULL, TPMS_OBJECT, "main_json", false);
	}
	
	///< update
	main_object = cJSON_GetObjectItemCaseSensitive(json, UPDATE_OBJECT);
	if (cJSON_IsObject(main_object)) {
		uint8_t idata = 0;

		cJSON *temp = cJSON_GetObjectItemCaseSensitive(main_object, U_UPDATE_MCU);
		return_value_if_fail(temp != NULL, UPDATE_OBJECT, U_UPDATE_MCU, false);
		idata  = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.mcu_update.update_type = idata;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, U_UPDATE_MCU_LEN);
		return_value_if_fail(temp != NULL, UPDATE_OBJECT, U_UPDATE_MCU_LEN, false);
		idata  = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.mcu_update.update_mcu_len = idata;

		memset(str, 0, sizeof(str));
		snprintf(str, sizeof(str), "%s%d", RESERVE_SUB_OBJECT, 1);
		temp = cJSON_GetObjectItemCaseSensitive(main_object, str);
		return_value_if_fail(temp != NULL, UPDATE_OBJECT, str, false);
		idata  = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.mcu_update.reserve = idata;
	} else {
		return_value_if_fail(main_object != NULL, UPDATE_OBJECT, "main_json", false);
	}
	
	///< maintenance
	main_object = cJSON_GetObjectItemCaseSensitive(json, MAINTENANCE_OBJECT);
	if (cJSON_IsObject(main_object)) {
		uint16_t main_data = 0;

		cJSON *temp = cJSON_GetObjectItemCaseSensitive(main_object, M_COUNT);
		return_value_if_fail(temp != NULL, MAINTENANCE_OBJECT, M_COUNT, false);
		main_data  = (uint16_t)cJSON_GetNumberIntValue(temp);
		set_param.maintain_info.maintain_mile.maintain_count = main_data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, M_CUR_MAIN_MI);
		return_value_if_fail(temp != NULL, MAINTENANCE_OBJECT, M_CUR_MAIN_MI, false);
		main_data  = (uint16_t)cJSON_GetNumberIntValue(temp);
		set_param.maintain_info.maintain_mile.cur_maintain_mileage = main_data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, M_LAST_MAIN_MI);
		return_value_if_fail(temp != NULL, MAINTENANCE_OBJECT, M_LAST_MAIN_MI, false);
		set_param.maintain_info.maintain_mile.last_maintain_total_mileage = \
					(uint32_t)cJSON_GetNumberIntValue(temp);

		temp = cJSON_GetObjectItemCaseSensitive(main_object, M_CUR_MAIN_DAYS);
		return_value_if_fail(temp != NULL, MAINTENANCE_OBJECT, M_CUR_MAIN_DAYS, false);
		main_data  = (uint16_t)cJSON_GetNumberIntValue(temp);
		set_param.maintain_info.maintain_time.maintain_days = main_data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, M_IS_SET_MAIN_TIME);
		return_value_if_fail(temp != NULL, MAINTENANCE_OBJECT, M_IS_SET_MAIN_TIME, false);
		main_data  = (uint8_t)cJSON_GetNumberIntValue(temp);
		set_param.maintain_info.maintain_time.is_sync_time = main_data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, M_LAST_MAIN_DATE);
		if (cJSON_IsObject(temp)) {
			cJSON *tmp = cJSON_GetObjectItemCaseSensitive(temp, M_SUB_OBJECT_YEAR);
			return_value_if_fail(tmp != NULL, M_LAST_MAIN_DATE, M_SUB_OBJECT_YEAR, false);
			main_data  = (uint16_t)cJSON_GetNumberIntValue(tmp);
			set_param.maintain_info.maintain_time.last_maintain_date.year = \
																main_data;
		
			tmp = cJSON_GetObjectItemCaseSensitive(temp, M_SUB_OBJECT_MONTH);
			return_value_if_fail(tmp != NULL, M_LAST_MAIN_DATE, M_SUB_OBJECT_MONTH, false);
			main_data  = (uint16_t)cJSON_GetNumberIntValue(tmp);
			set_param.maintain_info.maintain_time.last_maintain_date.mon = \
																main_data;
			
			tmp = cJSON_GetObjectItemCaseSensitive(temp, M_SUB_OBJECT_DAY);
			return_value_if_fail(tmp != NULL, M_LAST_MAIN_DATE, M_SUB_OBJECT_DAY, false);
			main_data  = (uint16_t)cJSON_GetNumberIntValue(tmp);
			set_param.maintain_info.maintain_time.last_maintain_date.day = \
																main_data;
		} else {
			return_value_if_fail(temp != NULL, MAINTENANCE_OBJECT, M_LAST_MAIN_DATE, false);
		}
	} else {
		return_value_if_fail(main_object != NULL, MAINTENANCE_OBJECT, "main_json", false);
	}
	
	///< ride_info
	main_object = cJSON_GetObjectItemCaseSensitive(json, RIDE_INFO_OBJECT);
	if (cJSON_IsObject(main_object)) {
		uint32_t data = 0;

		cJSON *temp = cJSON_GetObjectItemCaseSensitive(main_object, R_RIDE_TIME_A);
		return_value_if_fail(temp != NULL, RIDE_INFO_OBJECT, R_RIDE_TIME_A, false);
		data  = (uint32_t)cJSON_GetNumberIntValue(temp);
		set_param.ride_info.ride_time_a = data;

		temp = cJSON_GetObjectItemCaseSensitive(main_object, R_RIDE_TIME_B);
		return_value_if_fail(temp != NULL, RIDE_INFO_OBJECT, R_RIDE_TIME_B, false);
		data  = (uint32_t)cJSON_GetNumberIntValue(temp);
		set_param.ride_info.ride_time_b = data;
	} else {
		return_value_if_fail(main_object != NULL, RIDE_INFO_OBJECT, "main_json", false);
	}
	
	///< uuid
	main_object = cJSON_GetObjectItemCaseSensitive(json, UUID_INFO_OBJECT);
	if (cJSON_IsObject(main_object)) {
		const char *str = NULL;
		str = json_utils_get_string(main_object, C_CARLINK_UUID);
		return_value_if_fail(str != NULL, UUID_INFO_OBJECT, C_CARLINK_UUID, false);
		snprintf(set_param.carlink_uuid, sizeof(set_param.carlink_uuid), "%s", str);
	} else {
		return_value_if_fail(main_object != NULL, UUID_INFO_OBJECT, "main_json", false);
	}

	///< reserve
	main_object = cJSON_GetObjectItemCaseSensitive(json, RESERVE_OBJECT);
	if (cJSON_IsObject(main_object)) {
		cJSON *temp;
		uint8_t idata = 0;

		for (i = 0; i < 8; i++) {
			memset(str, 0, sizeof(str));
			snprintf(str, sizeof(str), "%s%d", RESERVE_SUB_OBJECT, i);
			temp = cJSON_GetObjectItemCaseSensitive(main_object, str);
			return_value_if_fail(temp != NULL, RESERVE_OBJECT, str, false);
			idata = (uint8_t)cJSON_GetNumberIntValue(temp);
			set_param.pads1[i] = idata;
		}
	} else {
		return_value_if_fail(main_object != NULL, RESERVE_OBJECT, "main_json", false);
	}
	
	///< reserve1
	main_object = cJSON_GetObjectItemCaseSensitive(json, RESERVE1_OBJECT);
	if (cJSON_IsObject(main_object)) {
		cJSON *temp;
		uint8_t idata = 0;
		for (i = 0; i < 6; i++) {
			memset(str, 0, sizeof(str));
			snprintf(str, sizeof(str), "%s%d", INT_RESERVE_SUB_OBJECT, i);

			temp = cJSON_GetObjectItemCaseSensitive(main_object, str);
			return_value_if_fail(temp != NULL, RESERVE_OBJECT, str, false);
			idata = (uint8_t)cJSON_GetNumberIntValue(temp);
			set_param.pads1[8 + i] = idata;
		}
	} else {
		return_value_if_fail(main_object != NULL, RESERVE1_OBJECT, "main_json", false);
	}

    cJSON_Delete(json);
    free(data);
	if (file_data != NULL) {
		file_data = NULL;
	}

	return true;
}

static void set_default_param(usr_param_t *param) {
	if (param) {
		memset(param, 0, sizeof(usr_param_t));
        param->maintain_info.maintain_mile.cur_maintain_mileage = 1000;
        param->maintain_info.maintain_time.maintain_days = 365;

        param->usr_set.mile_format = 0,
        param->usr_set.mile_display = 0,
        param->usr_set.theme = 0;
        param->usr_set.bt_switch = 1;
        param->usr_set.brightness = 3;
        param->usr_set.language = 0;
        param->usr_set.sys_log = 0;
        param->usr_set.time_format = 1;
		snprintf(param->carlink_uuid, 20, "%s", " ");
	}
}

static bool write_json_data(char *data, const char *file_path) {
	if (!data || !file_path) {
        mw_loge("Parse json data pointer null\n");
        return false;
    }

    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            mw_loge("Write Json parse Error before: %s\n", error_ptr);
        }

        free(data);
		data = NULL;
        cJSON_Delete(json);

        return false;
    }

	///< system
	cJSON *item = cJSON_GetObjectItemCaseSensitive(json, SYSTEM_OBJECT);
	cJSON *writeTmp;
	if (cJSON_IsObject(item)) {
		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_THEME);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.theme);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_MILE_DIS);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.mile_display);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_LED_LEVEL);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.brightness);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_DIS_UNIT);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.mile_format);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_LANGUAGE);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.language);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_LINK_TYPE);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.phone_type);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_BT_SWITCH);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.bt_switch);
		}
	
		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_UUID_STATE);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.uuid_active_staus);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_START_SCR);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.start_src);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_TCS_SWITCH);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.tcs_switch);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_TEMP_UNIT);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.temp_unit);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_TIME_FORMAT);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.time_format);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_TPMS_UNIT);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.tpms_unit);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_AUTO_HEADLIGHT);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.auto_headlight);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_SYS_LOG);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.sys_log);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, S_DRIVE_MODE);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.usr_set.drive_mode);
		}
		///< reserve0 int_reserve0
		///< to do
	}
	
	///< tpms
	item = cJSON_GetObjectItemCaseSensitive(json, TPMS_OBJECT);
	if (cJSON_IsObject(item)) {
		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_L_F_TPMS_ID);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[0].tpms_id);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_L_R_TPMS_ID);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[1].tpms_id);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_R_F_TPMS_ID);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[2].tpms_id);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_R_R_TPMS_ID);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[3].tpms_id);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_L_F_PRESSURE);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[0].tpms_pressure);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_L_R_PRESSURE);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[1].tpms_pressure);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_R_F_PRESSURE);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[2].tpms_pressure);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_R_R_PRESSURE);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[3].tpms_pressure);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_L_F_TEMP);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[0].tpms_temp);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_L_R_TEMP);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[1].tpms_temp);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_R_F_TEMP);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[2].tpms_temp);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, T_R_R_TEMP);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.tpms[3].tpms_temp);
		}
		
		// reserve0 int_reserve0
		// to do
	}

	///< update
	item = cJSON_GetObjectItemCaseSensitive(json, UPDATE_OBJECT);
	if (cJSON_IsObject(item)) {
		writeTmp = cJSON_GetObjectItemCaseSensitive(item, U_UPDATE_MCU);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.mcu_update.update_type);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, U_UPDATE_MCU_LEN);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.mcu_update.update_mcu_len);
		}
		// reserve0 int_reserve0
		// to do
	}

	///< maintenance
	item = cJSON_GetObjectItemCaseSensitive(json, MAINTENANCE_OBJECT);
	if (cJSON_IsObject(item)) {
		writeTmp = cJSON_GetObjectItemCaseSensitive(item, M_COUNT);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.maintain_info.maintain_mile.maintain_count);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, M_CUR_MAIN_MI);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.maintain_info.maintain_mile.cur_maintain_mileage);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, M_LAST_MAIN_MI);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.maintain_info.maintain_mile.last_maintain_total_mileage);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, M_CUR_MAIN_DAYS);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.maintain_info.maintain_time.maintain_days);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, M_IS_SET_MAIN_TIME);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.maintain_info.maintain_time.is_sync_time);
		}
	
		writeTmp = cJSON_GetObjectItemCaseSensitive(item, M_LAST_MAIN_DATE);
		if (cJSON_IsObject(writeTmp)) {
			cJSON *writeTmp1 = cJSON_GetObjectItemCaseSensitive(writeTmp, M_SUB_OBJECT_YEAR);
			if (cJSON_IsNumber(writeTmp1)) {
				cJSON_SetIntValue(writeTmp1, set_param.maintain_info.maintain_time.last_maintain_date.year);
			}

			writeTmp1 = cJSON_GetObjectItemCaseSensitive(writeTmp, M_SUB_OBJECT_MONTH);
			if (cJSON_IsNumber(writeTmp1)) {
				cJSON_SetIntValue(writeTmp1, set_param.maintain_info.maintain_time.last_maintain_date.mon);
			}

			writeTmp1 = cJSON_GetObjectItemCaseSensitive(writeTmp, M_SUB_OBJECT_DAY);
			if (cJSON_IsNumber(writeTmp1)) {
				cJSON_SetIntValue(writeTmp1, set_param.maintain_info.maintain_time.last_maintain_date.day);
			}
		}
	
		// reserve0 int_reserve0
		// to do
	}

	///< ride info
	item = cJSON_GetObjectItemCaseSensitive(json, RIDE_INFO_OBJECT);
	if (cJSON_IsObject(item)) {
		writeTmp = cJSON_GetObjectItemCaseSensitive(item, R_RIDE_TIME_A);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.ride_info.ride_time_a);
		}

		writeTmp = cJSON_GetObjectItemCaseSensitive(item, R_RIDE_TIME_B);
		if (cJSON_IsNumber(writeTmp)) {
			cJSON_SetIntValue(writeTmp, set_param.ride_info.ride_time_b);
		}
	}

	///< uuid
	char str[20] = {0};
	snprintf(str, sizeof(str), "%s", set_param.carlink_uuid);
	item = cJSON_GetObjectItemCaseSensitive(json, UUID_INFO_OBJECT);
	json_utils_modify_string(item, C_CARLINK_UUID, str);

	char *jsonString = cJSON_Print(json);
	param_file = fopen(file_path, "w");
    if (param_file != NULL) {
        fprintf(param_file, "%s", jsonString);
        fclose(param_file);
		param_file = NULL;
    }
	
	printf("Save set param success\n");

    // clean
    free(jsonString);
    cJSON_Delete(json);
	free(data);
	if (file_data != NULL) {
		file_data = NULL;
	}

	return true;
}

static bool is_src_config_exist(void) {
	char *file_name = NULL;
	char str[128] = {0};
	char name_spilt[3][64] = {0};

	file_name = find_file_name(CONFIG_SRC_PATH, CONFIG_FILE_SUFFIX, SET_PARAM_PREFIX);
	if (!file_name) {
		mw_loge("file name pointer is null\n");
		return false;
	}

	if ((strlen(file_name) == 0)) {
		mw_loge("Get config file failed\n");
		return false;
	}

	if (strstr(file_name, CAR_MODEL)) {
		snprintf(str, sizeof(str), "%s", file_name);
		if (str_split(str, "_", name_spilt, 
			sizeof(name_spilt) / sizeof(name_spilt[0])) >=3) {
			memcpy(car_model, name_spilt[1], strlen(name_spilt[1]));
			memcpy(param_ver, name_spilt[2], strlen(name_spilt[2]));	
			printf("src config car mode:%s  param_ver:%s\n", car_model, param_ver);

			memset(str, 0, sizeof(str));
#ifndef _WIN32
			snprintf(str, sizeof(str), "%s/%s_%s_%s%s", CONFIG_SRC_PATH,
				SET_PARAM_PREFIX, car_model, param_ver, CONFIG_FILE_SUFFIX);

#else
			snprintf(str, sizeof(str), "%s\\%s\\%s_%s_%s%s", WIN32_ROOT_PATH,
			WIN32_DEST_SRC_PATH,SET_PARAM_PREFIX, 
			car_model, param_ver, CONFIG_FILE_SUFFIX);	
#endif
			memcpy(src_path, str, strlen(str));
			printf("src config path:%s\n", src_path);

			return true;
		}
	} else {
		mw_loge("has not mate config file\n");
	}

	return false;
} 

static bool is_dest_config_exist(void) {
	char *file_name = NULL;
	file_name = find_file_name(DEST_CONFIG_FILE_PATH, 
		CONFIG_FILE_SUFFIX, SET_PARAM_PREFIX);
	if (!file_name) {
		mw_loge("file name pointer is null\r\n");
		return false;
	}

	if (strlen(file_name) > 0) {
		if (strstr(file_name, CAR_MODEL)) {
			return true;
		}
	} else {
		mw_loge("can not find dest config file\r\n");
	}

	return false;
}

static bool read_src_config(void) {
	char *file_name = NULL;
	char str[256] = {0};
	char cmd_str[256] = {0};
	char name_spilt[3][64] = {0};
	bool is_mv_config = false;
	bool state = false;
	int ret = -1;
	uint8_t split_num = 0;

	file_name = find_file_name(DEST_CONFIG_FILE_PATH, 
		CONFIG_FILE_SUFFIX, SET_PARAM_PREFIX);
	if (file_name) {
		if (strlen(file_name) > 0) {
			if (strstr(file_name, CAR_MODEL)) {
				snprintf(str, sizeof(str), "%s", file_name);

				split_num = str_split(str, "_", name_spilt, 
					sizeof(name_spilt) / sizeof(name_spilt[0]));
				if (split_num >=3) {
					///< car_modell is equal? param_ver is equal?
					memset(str, 0, sizeof(str));
	#ifndef _WIN32
					snprintf(str, sizeof(str), "%s/%s_%s_%s%s", 
						DEST_CONFIG_FILE_PATH, SET_PARAM_PREFIX, 
						car_model, param_ver, CONFIG_FILE_SUFFIX);
	#else
					snprintf(str, sizeof(str), "%s\\%s\\%s_%s_%s%s", WIN32_ROOT_PATH,
						WIN32_DEST_CONFIG_PATH, SET_PARAM_PREFIX, 
						car_model, param_ver, CONFIG_FILE_SUFFIX);
	#endif

					if ((strcmp(name_spilt[1], car_model) != 0)
						|| (strcmp(name_spilt[2], param_ver) != 0)) {				
	#ifndef _WIN32		
						snprintf(cmd_str, sizeof(cmd_str), 
							"rm %s/%s_%s_%s%s",DEST_CONFIG_FILE_PATH, 
							SET_PARAM_PREFIX, name_spilt[1],
							name_spilt[2], CONFIG_FILE_SUFFIX);
	#else
						snprintf(cmd_str, sizeof(cmd_str), 
							"rm %s\\%s\\%s_%s_%s%s", WIN32_ROOT_PATH
							WIN32_DEST_CONFIG_PATH, 
							SET_PARAM_PREFIX, name_spilt[1],
							name_spilt[2], CONFIG_FILE_SUFFIX);
	#endif
						system(cmd_str);

						memset(cmd_str, 0, sizeof(cmd_str));
	#ifndef _WIN32
						snprintf(cmd_str, sizeof(cmd_str), "cp  %s %s",src_path, str);
	#else
						snprintf(cmd_str, sizeof(cmd_str), "copy  %s %s",src_path, str);	
	#endif
						system(cmd_str);
						printf("car model or param ver is diffrent, mv config file...\n");
					} else if ((strcmp(name_spilt[1], car_model) == 0) 
						&& (strcmp(name_spilt[2], param_ver) == 0)) {
						printf("dest src config same, read dest\n");
	#ifndef _WIN32
						snprintf(cmd_str, sizeof(cmd_str), "rm %s", src_path);	
						printf("sys tem str:%s\r\n", cmd_str);
						system(cmd_str);
	#endif
					}

					printf("path str:%s\r\n", str);
					if (read_json_data(open_param_file(str, "r"))) {
						printf("Type2: load config json param success\n");
						is_mv_config = true;
						state = true;
					}
				}
			} else {
				///< delete other model config json, Ensure 
				///< that there is only one setParam_xxx_xxx.json only
				memset(str, 0, sizeof(str));
	#ifndef _WIN32
				snprintf(str, sizeof(str), "rm %s/%s.json", 
					DEST_CONFIG_FILE_PATH, file_name);
	#else
				snprintf(str, sizeof(str), "rm %s\\%s\\%s.json", WIN32_ROOT_PATH
					WIN32_DEST_CONFIG_PATH, file_name);
	#endif
				printf("2: Delete other model config:%s\n", str);
				system(str);
			}
		}
	}
	
	if (!is_mv_config) {
		is_mv_config = true;
		memset(str, 0, sizeof(str));
#ifndef _WIN32
		snprintf(str, sizeof(str), "%s/%s_%s_%s%s",
			DEST_CONFIG_FILE_PATH, SET_PARAM_PREFIX, 
			car_model, param_ver, CONFIG_FILE_SUFFIX);

		//snprintf(str, sizeof(str), "%s", DEST_CONFIG_FILE_PATH);
		memset(cmd_str, 0, sizeof(cmd_str));
		snprintf(cmd_str, sizeof(cmd_str), "cp %s %s", src_path, str);
#else
		snprintf(str, sizeof(str), "%s\\%s\\%s_%s_%s%s",
			WIN32_ROOT_PATH, WIN32_DEST_CONFIG_PATH, SET_PARAM_PREFIX, 
			car_model, param_ver, CONFIG_FILE_SUFFIX);

		memset(cmd_str, 0, sizeof(cmd_str));
		snprintf(cmd_str, sizeof(cmd_str), "copy %s %s", src_path, str);

#endif
		printf("cmd_str:%s\r\n", cmd_str);
		ret = system(cmd_str);
		printf("ret = %d\r\n", ret);
		if (ret == 0) {
			printf("Mv %s config json success\n", str);
			if (read_json_data(open_param_file(str, "r"))) {
				printf("Type1: load config json param success\n");
				state = true;
			}
		} else {
			printf("Mv %s config json failed!\n", str);
		}
	}

	return state;
}

static bool read_dest_config(void) {
	bool state = false;
	char *file_name = NULL;
	char str[256] = {0};
	char name_spilt[3][64] = {0};

	file_name = find_file_name(DEST_CONFIG_FILE_PATH, 
		CONFIG_FILE_SUFFIX, SET_PARAM_PREFIX);
	if (!file_name) {
		return false;
	}

	if (strlen(file_name) > 0 ) {
		if (strstr(file_name, CAR_MODEL)) {
			snprintf(str, sizeof(str), "%s", file_name);
			if (str_split(str, "_", name_spilt, 
				sizeof(name_spilt) / sizeof(name_spilt[0])) >=3) {
				if ((strcmp(name_spilt[1], CAR_MODEL) == 0)
					&& (strcmp(name_spilt[2], CONFIG_PARAM_VER) == 0)) {
					memset(str, 0, sizeof(str));
#ifndef _WIN32
					snprintf(str, sizeof(str), "%s/%s_%s_%s%s",
						DEST_CONFIG_FILE_PATH, SET_PARAM_PREFIX,
						name_spilt[1], name_spilt[2], CONFIG_FILE_SUFFIX);
#else
					snprintf(str, sizeof(str), "%s\\%s\\%s_%s_%s%s",WIN32_ROOT_PATH,
						WIN32_DEST_CONFIG_PATH, SET_PARAM_PREFIX,
						name_spilt[1], name_spilt[2], CONFIG_FILE_SUFFIX);	

#endif
					printf("Dest set param config:%s\n", str);
					
					if (read_json_data(open_param_file(str, "r"))) {
						printf("Type4: load config json param success\n");
						state = true;
					}
				}	
			}
		} else {
			memset(str, 0, sizeof(str));
#ifndef _WIN32
			snprintf(str, sizeof(str), "rm %s/%s.json",
				DEST_CONFIG_FILE_PATH, file_name);
#else
			snprintf(str, sizeof(str), "rm %s\\%s\\%s.json",
				WIN32_ROOT_PATH, WIN32_DEST_CONFIG_PATH, file_name);

#endif
			system(str);
			printf("4:Delete other model config:%s\n", str);
		}
	}

	return state;
}

static void set_default_config(void) {
	if (!param_file) {
		mw_loge("Json config file is not open\n");
		return;
	}

	int i = 0;
	char str_temp[32] = {0};

	cJSON *json = cJSON_CreateObject();
	if (!json) {
		mw_loge("create json object failed\n");
		return;
	}

	///< system
	cJSON *sec_json = cJSON_CreateObject();
	cJSON_AddNumberToObject(sec_json, S_THEME, set_param.usr_set.theme);
	cJSON_AddNumberToObject(sec_json, S_MILE_DIS, set_param.usr_set.mile_display);
	cJSON_AddNumberToObject(sec_json, S_LED_LEVEL, set_param.usr_set.brightness);
	cJSON_AddNumberToObject(sec_json, S_DIS_UNIT, set_param.usr_set.mile_format);
	cJSON_AddNumberToObject(sec_json, S_LANGUAGE, set_param.usr_set.language);
	cJSON_AddNumberToObject(sec_json, S_LINK_TYPE, set_param.usr_set.phone_type);
	cJSON_AddNumberToObject(sec_json, S_BT_SWITCH, set_param.usr_set.bt_switch);
	cJSON_AddNumberToObject(sec_json, S_UUID_STATE, set_param.usr_set.uuid_active_staus);
	cJSON_AddNumberToObject(sec_json, S_START_SCR, set_param.usr_set.start_src);
	cJSON_AddNumberToObject(sec_json, S_TCS_SWITCH, set_param.usr_set.tcs_switch);
	cJSON_AddNumberToObject(sec_json, S_TEMP_UNIT, set_param.usr_set.temp_unit);
	cJSON_AddNumberToObject(sec_json, S_TIME_FORMAT, set_param.usr_set.time_format);
	cJSON_AddNumberToObject(sec_json, S_TPMS_UNIT, set_param.usr_set.tpms_unit);
	cJSON_AddNumberToObject(sec_json, S_AUTO_HEADLIGHT, set_param.usr_set.auto_headlight);
	cJSON_AddNumberToObject(sec_json, S_SYS_LOG, set_param.usr_set.sys_log);
	cJSON_AddNumberToObject(sec_json, S_DRIVE_MODE, set_param.usr_set.drive_mode);

	for (i = 0; i < 11; i++) {
		memset(str_temp, 0, sizeof(str_temp));
		snprintf(str_temp, sizeof(str_temp), "%s%d", INT_RESERVE_SUB_OBJECT, i);
		cJSON_AddNumberToObject(sec_json, str_temp, 0);
	}

	cJSON_AddItemToObject(json, SYSTEM_OBJECT, sec_json);

	///< tpms
	sec_json = cJSON_CreateObject();
	cJSON_AddNumberToObject(sec_json, T_L_F_TPMS_ID, set_param.tpms[0].tpms_id);
	cJSON_AddNumberToObject(sec_json, T_L_R_TPMS_ID, set_param.tpms[1].tpms_id);
	cJSON_AddNumberToObject(sec_json, T_R_F_TPMS_ID, set_param.tpms[2].tpms_id);
	cJSON_AddNumberToObject(sec_json, T_R_R_TPMS_ID, set_param.tpms[3].tpms_id);
	cJSON_AddNumberToObject(sec_json, T_L_F_PRESSURE, set_param.tpms[0].tpms_pressure);
	cJSON_AddNumberToObject(sec_json, T_L_R_PRESSURE, set_param.tpms[1].tpms_pressure);
	cJSON_AddNumberToObject(sec_json, T_R_F_PRESSURE, set_param.tpms[2].tpms_pressure);
	cJSON_AddNumberToObject(sec_json, T_R_R_PRESSURE, set_param.tpms[3].tpms_pressure);
	cJSON_AddNumberToObject(sec_json, T_L_F_TEMP, set_param.tpms[0].tpms_temp);
	cJSON_AddNumberToObject(sec_json, T_L_R_TEMP, set_param.tpms[1].tpms_temp);
	cJSON_AddNumberToObject(sec_json, T_R_F_TEMP, set_param.tpms[2].tpms_temp);
	cJSON_AddNumberToObject(sec_json, T_R_R_TEMP, set_param.tpms[3].tpms_temp);

	for (i = 0; i < 4; i++) {
		memset(str_temp, 0, sizeof(str_temp));
		snprintf(str_temp, sizeof(str_temp), "%s%d", RESERVE_SUB_OBJECT, i);
		cJSON_AddNumberToObject(sec_json, str_temp, 0);
	}

	for (i = 0; i < 4; i++) {
		memset(str_temp, 0, sizeof(str_temp));
		snprintf(str_temp, sizeof(str_temp), "%s%d", INT_RESERVE_SUB_OBJECT, i);
		cJSON_AddNumberToObject(sec_json, str_temp, 0);
	}
	cJSON_AddItemToObject(json, TPMS_OBJECT, sec_json);
	
	///< update
	sec_json = cJSON_CreateObject();
	cJSON_AddNumberToObject(sec_json, U_UPDATE_MCU, set_param.mcu_update.update_type);
	cJSON_AddNumberToObject(sec_json, U_UPDATE_MCU_LEN, set_param.mcu_update.update_mcu_len);

	memset(str_temp, 0, sizeof(str_temp));
	snprintf(str_temp, sizeof(str_temp), "%s%d", RESERVE_SUB_OBJECT, 1);
	cJSON_AddNumberToObject(sec_json, str_temp, 0);
	cJSON_AddItemToObject(json, UPDATE_OBJECT, sec_json);

	///< maintenance
	sec_json = cJSON_CreateObject();
	cJSON_AddNumberToObject(sec_json, M_COUNT, set_param.maintain_info.maintain_mile.maintain_count);
	cJSON_AddNumberToObject(sec_json, M_CUR_MAIN_MI, set_param.maintain_info.maintain_mile.cur_maintain_mileage);
	cJSON_AddNumberToObject(sec_json, M_LAST_MAIN_MI, set_param.maintain_info.maintain_mile.last_maintain_total_mileage);
	cJSON_AddNumberToObject(sec_json, M_CUR_MAIN_DAYS, set_param.maintain_info.maintain_time.maintain_days);
	cJSON_AddNumberToObject(sec_json, M_IS_SET_MAIN_TIME, set_param.maintain_info.maintain_time.is_sync_time);
	cJSON *third_json = cJSON_CreateObject();
	cJSON_AddNumberToObject(third_json, M_SUB_OBJECT_YEAR, set_param.maintain_info.maintain_time.last_maintain_date.year);
	cJSON_AddNumberToObject(third_json, M_SUB_OBJECT_MONTH, set_param.maintain_info.maintain_time.last_maintain_date.mon);
	cJSON_AddNumberToObject(third_json, M_SUB_OBJECT_DAY, set_param.maintain_info.maintain_time.last_maintain_date.day);
	cJSON_AddItemToObject(sec_json, M_LAST_MAIN_DATE, third_json);
	memset(str_temp, 0, sizeof(str_temp));
	snprintf(str_temp, sizeof(str_temp), "%s%d", INT_RESERVE_SUB_OBJECT, 0);
	cJSON_AddNumberToObject(sec_json, str_temp, 0);
	cJSON_AddItemToObject(json, MAINTENANCE_OBJECT, sec_json);

	///< ride info
	sec_json = cJSON_CreateObject();
	cJSON_AddNumberToObject(sec_json, R_RIDE_TIME_A, set_param.ride_info.ride_time_a);
	cJSON_AddNumberToObject(sec_json, R_RIDE_TIME_B, set_param.ride_info.ride_time_b);
	cJSON_AddItemToObject(json, RIDE_INFO_OBJECT, sec_json);

	///< uuid
	sec_json = cJSON_CreateObject();
	cJSON_AddStringToObject(sec_json, C_CARLINK_UUID, set_param.carlink_uuid);
	memset(str_temp, 0, sizeof(str_temp));
	snprintf(str_temp, sizeof(str_temp), "%s%d", INT_RESERVE_SUB_OBJECT, 0);
	cJSON_AddNumberToObject(sec_json, str_temp, 0);
	cJSON_AddItemToObject(json, UUID_INFO_OBJECT, sec_json);

	///< reserve
	sec_json = cJSON_CreateObject();
	for (i = 0; i < 8; i++) {
		memset(str_temp, 0, sizeof(str_temp));
		snprintf(str_temp, sizeof(str_temp), "%s%d", RESERVE_SUB_OBJECT, i);
		cJSON_AddNumberToObject(sec_json, str_temp, 0);
	}
	cJSON_AddItemToObject(json, RESERVE_OBJECT, sec_json);

	///< reserve1
	sec_json = cJSON_CreateObject();
	for (i = 0; i < 6; i++) {
		memset(str_temp, 0, sizeof(str_temp));
		snprintf(str_temp, sizeof(str_temp), "%s%d", INT_RESERVE_SUB_OBJECT, i);
		cJSON_AddNumberToObject(sec_json, str_temp, 0);
	}
	cJSON_AddItemToObject(json, RESERVE1_OBJECT, sec_json);

	char *json_str = cJSON_Print(json);
	fprintf(param_file, "%s", json_str);
	fclose(param_file);
	param_file = NULL;

    free(json_str);
    cJSON_Delete(json);
}

static bool read_default_config(void) {
	char str[128] = {0};

#ifndef _WIN32
	snprintf(str, sizeof(str), "%s/%s_%s_%s%s",
		DEST_CONFIG_FILE_PATH, SET_PARAM_PREFIX,
		CAR_MODEL, CONFIG_PARAM_VER, CONFIG_FILE_SUFFIX);
#else
	snprintf(str, sizeof(str), "%s\\%s\\%s_%s_%s%s", WIN32_ROOT_PATH,
		WIN32_DEST_CONFIG_PATH, SET_PARAM_PREFIX,
		CAR_MODEL, CONFIG_PARAM_VER, CONFIG_FILE_SUFFIX);

#endif
	if (!param_file) {
		param_file = fopen(str, "w+");
		if (!param_file) {
			mw_loge("open %s failed\n", str);
			return false;
		} 
	}

	set_default_param(&set_param);
	set_default_config();
	printf("Type3: load config json param success\n");

	return true;
}

bool read_set_param(void) {
    bool src_read = false;
    bool dest_read = false;

	src_read = is_src_config_exist();
	dest_read = is_dest_config_exist();
	if (src_read) {
		if (!read_src_config()) {
			mw_loge("usr load src setparam json error\n");
			return false;
		}
	} else if (!src_read && dest_read) {
		if (!read_dest_config()) {
			mw_loge("usr load dest setparam json erro\n");
			return false;
		}
	} else if (!src_read && !dest_read) {
		if (!read_default_config()) {
			mw_loge("usr load default setparam json erro\n");
			return false;
		}
	} 

	return true;
}

usr_param_t get_set_param(void) {
	return set_param;
}

void set_set_param(usr_param_t param) {
	set_param = param;
}

bool save_set_param_file(void) {
	char file_path[128] = {0};
	char *file_name = NULL;
	
	file_name = find_file_name(DEST_CONFIG_FILE_PATH, 
		CONFIG_FILE_SUFFIX, SET_PARAM_PREFIX);
	if (!file_name) {
		return false;
	}

	if (strlen(file_name) > 0) {
		if (strstr(file_name, CAR_MODEL)) {
#ifndef _WIN32
			snprintf(file_path, sizeof(file_path), "%s/%s%s",
        		DEST_CONFIG_FILE_PATH, file_name, CONFIG_FILE_SUFFIX);
#else
			snprintf(file_path, sizeof(file_path), "\\%s\\%s%s",WIN32_ROOT_PATH,
        		WIN32_DEST_CONFIG_PATH, file_name, CONFIG_FILE_SUFFIX);
#endif
			return write_json_data(open_param_file(file_path, "r"), file_path);
		} else {
			mw_loge("cofig param file not mate car model\n");
		}
	} else {
		mw_loge("usr has not param config file\n");
	}

    return false;
}
