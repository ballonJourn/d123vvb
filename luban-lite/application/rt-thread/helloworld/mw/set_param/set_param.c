#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <rtthread.h>
#include <stdlib.h>
#include "set_param.h"
#include "vehicle_param.h"
#include "json_param_parse.h"
#include "find_file_name.h"
#include "app_log.h"
#include "usr_backlight.h"
#include "power_manage.h"

#define USE_PARAM_PRINTF (1)

static usr_param_t usr_param;
static usr_param_t usr_param_pre = {
    .usr_set = {1},
    .carlink_uuid = {0xff},
};

static bool is_recovery_usr_param = false;
static bool start_by_acc = false;

#if 0
static void set_default_usr_param(usr_param_t * set_param) {
    if (set_param) {
        memset(set_param, 0, sizeof(usr_param_t));
        set_param->maintain_info.maintain_mile.cur_maintain_mileage = 1000;
        set_param->maintain_info.maintain_time.maintain_days = 365;

        set_param->usr_set.mile_format = 0,
        set_param->usr_set.mile_display = 0,
        set_param->usr_set.theme = 0;
        set_param->usr_set.bt_switch = 1;
        set_param->usr_set.brightness = 3;
        set_param->usr_set.language = 0;
        set_param->usr_set.sys_log = 1;
        set_param->usr_set.time_format = 1;
    }
}
#endif

#if USE_PARAM_PRINTF
static void printf_usr_param(usr_param_t * param) {
    if (param) {
        printf("\r\n ...............usr param start......................\r\n");
        printf("maintain_count:%d\r\n",param->maintain_info.maintain_mile.maintain_count);
        printf("maintain_mileage:%lu\r\n",param->maintain_info.maintain_mile.last_maintain_total_mileage);
        printf("cur_maintain_mileage:%d\r\n",param->maintain_info.maintain_mile.cur_maintain_mileage);

        printf("maintain_days:%d\r\n",param->maintain_info.maintain_time.maintain_days);
        printf("is sync time:%d\r\n",param->maintain_info.maintain_time.is_sync_time);
        printf("maintain data:%04d/%02d/%02d\r\n",param->maintain_info.maintain_time.last_maintain_date.year, 
                param->maintain_info.maintain_time.last_maintain_date.mon, 
                param->maintain_info.maintain_time.last_maintain_date.day);

        printf("mile_format :%d\r\n",param->usr_set.mile_format);
        printf("mile display:%d\r\n",param->usr_set.mile_display);
        printf("theme :%d\r\n",param->usr_set.theme);
        printf("brightness level :%d\r\n",param->usr_set.brightness);
        printf("system language :%d\r\n",param->usr_set.language);
        printf("bt_switch:%d\r\n",param->usr_set.bt_switch);
        printf("uuid_active_status:%d\r\n",param->usr_set.uuid_active_staus);
        printf("meter start src:%d\r\n",param->usr_set.start_src);
        printf("tpms Press Unit:%d\r\n",param->usr_set.tpms_unit);
        printf("tcs_switch:%d\r\n",param->usr_set.tcs_switch);
        printf("time_format:%d\r\n",param->usr_set.time_format);
        printf("temp_unit:%d\r\n",param->usr_set.temp_unit);
        printf("phone_type:%d\r\n", param->usr_set.phone_type);
        printf("sys_log:%d\r\n", param->usr_set.sys_log);
        printf("drive_mode:%d\r\n", param->usr_set.drive_mode);
        printf("reset_type:%d\r\n", param->usr_set.reset_type);

        printf("ride_time_a:%lu\r\n", param->ride_info.ride_time_a);     
        printf("ride_time_b:%lu\r\n", param->ride_info.ride_time_b);    
        printf("mcu_update:%d\r\n", param->mcu_update.update_type) ,
        printf("mcu update_len:%d\r\n", param->mcu_update.update_mcu_len);
        for (int i = 0; i < MAX_WHEEL_POS_NUM; i++) {
            printf("tpms_info[%d].id:0x%lx\n", i, param->tpms[i].tpms_id);
            printf("tpms_info[%d].pressure:%d\n", i, param->tpms[i].tpms_pressure);
            printf("tpms_info[%d].temp:%d\n", i, param->tpms[i].tpms_temp);
        }

        printf("\r\nuuid:");
        for(int i = 0; i < 20; i++)
        {
            printf("%c",param->carlink_uuid[i]);
        }
        printf("\r\n");
        printf("\r\n ...............usr param end......................\r\n");
    }
}
#endif

static void check_usr_param(void) {
    if (usr_param.usr_set.mile_format > 1) {
        usr_param.usr_set.mile_format = 0;
    }

    if (usr_param.usr_set.mile_display > 2) {
        usr_param.usr_set.mile_display = 0;
    }

    if (usr_param.usr_set.theme > 2) {
        usr_param.usr_set.theme = 0;
    }

    if (usr_param.usr_set.brightness > 5) {
        usr_param.usr_set.brightness = 3;
    }

    if (usr_param.usr_set.language > 1) {
        usr_param.usr_set.language = 1;
    }

    if (usr_param.usr_set.uuid_active_staus > 1) {
        usr_param.usr_set.uuid_active_staus = 0;
    }

    if (usr_param.usr_set.tpms_unit > 2) {
        usr_param.usr_set.tpms_unit = 0;
    }

    if (usr_param.usr_set.tcs_switch > 1) {
        usr_param.usr_set.tcs_switch = 0;
    }

    if (usr_param.usr_set.time_format > 1) {
        usr_param.usr_set.time_format = 1;
    }

    if (usr_param.usr_set.temp_unit > 1) {
        usr_param.usr_set.temp_unit = 0;
    }

    if (usr_param.usr_set.auto_headlight > 1) {
        usr_param.usr_set.auto_headlight = 0;
    }

    if (usr_param.usr_set.phone_type > 1) {
        usr_param.usr_set.phone_type = 0;
    }

    if (usr_param.usr_set.drive_mode > 1) {
        usr_param.usr_set.drive_mode = 0;
    }

    if (usr_param.usr_set.reset_type > 1) {
        usr_param.usr_set.reset_type = 0;
    }

    if (usr_param.maintain_info.maintain_mile.maintain_count == 0xffff) {
        usr_param.maintain_info.maintain_mile.maintain_count = 0;
    }

    if (usr_param.maintain_info.maintain_mile.cur_maintain_mileage == 0xffff) {
        usr_param.maintain_info.maintain_mile.cur_maintain_mileage = 0;
    }

    if (usr_param.maintain_info.maintain_mile.last_maintain_total_mileage == 0xffffffff) {
        usr_param.maintain_info.maintain_mile.last_maintain_total_mileage = 0;
    }

    if (usr_param.maintain_info.maintain_time.maintain_days == 0xffff) {
        usr_param.maintain_info.maintain_time.maintain_days = 0;
    }

    if (usr_param.maintain_info.maintain_time.is_sync_time > 1) {
        usr_param.maintain_info.maintain_time.is_sync_time = 0;
    }

    
#if USE_PARAM_PRINTF
    printf_usr_param(&usr_param);
#endif

#ifdef DEFAULT_START_UI_THEME
    usr_param.usr_set.theme = 1;
#endif

    vehicle_set_data(VEH_LICENSE_AUTH_STATUS, (int)usr_param.usr_set.uuid_active_staus);
#if MW_BL_PW_ENABLE
    if (usr_param.usr_set.brightness != 0) {
        set_backlight_level(usr_param.usr_set.brightness);
    } else {
        rt_log_info("current platform not support autobacklight\n");
        set_backlight_level(3);
    }
#endif
}

static void check_start_source(uint8_t start_src) {
#if POWER_MANAGE_ENABLE
    if (start_src) {
        start_by_acc = true;
        rt_log_info("\r\nStart by acc\n");
    } else {
        rt_log_info("\r\nStart by bat\n");
        usr_param.usr_set.mile_format = 0;
        usr_param.usr_set.mile_display = 0,
        usr_param.usr_set.language = 0;
        usr_param.usr_set.theme = 1;
        usr_param.usr_set.reset_type = 0;
        usr_param.usr_set.bt_switch = 1;
        usr_param.usr_set.tpms_unit = 0;
        usr_param.usr_set.brightness = 3;
        usr_param.usr_set.auto_headlight = 0;
    }
#else
    start_by_acc = true;
#endif
    
    check_usr_param();
    is_recovery_usr_param = true;
}

///< 落盘主体, 不受下电闸门限制 —— 供 shutdown_save_usr_param() 做最后一次保存
static int save_usr_param_internal(void) {
    if (!is_recovery_usr_param) {
        rt_log_info("Usr param is not get ready!\n");
        return -1;
    }

    if (memcmp(&usr_param_pre, &usr_param, sizeof(usr_param_t)) == 0) {
        rt_log_info("param is same, do not save!\n");
        return 0;
    }

    memcpy(&usr_param_pre, &usr_param, sizeof(usr_param_t));
    set_set_param(usr_param);
    if (!save_set_param_file()) {
        rt_log_error("save usr set param file failed!\n");
        return -1;
    }

    return 0;
}

int save_usr_param(void) {
    ///< 下电中: 丢弃非下电触发的落盘, 避免写入被断电截断
    if (power_is_off_pending()) {
        rt_log_info("power off pending, drop usr param save!\n");
        return 0;
    }

    return save_usr_param_internal();
}

/**
 * @brief  关机前保存用户参数: 启动源置 1(下次开机识别为 ACC 启动)并落盘.
 *         由 power_off_request() 统一下电入口调用, 走不受闸门限制的强制路径.
 * @return 0:保存成功  -1:保存失败(失败时已延时 100ms)
 */
int shutdown_save_usr_param(void)
{
    uint8_t stat_src = 1;
    set_usr_param(USR_PARAM_START_SRC, &stat_src);
    rt_log_info("save usr param..\n");
    if (save_usr_param_internal() != 0) {
        rt_log_info("save usr param failed!\n");
        rt_thread_mdelay(100);
        return -1;
    }
    rt_log_info("save usr param success!\n");

    return 0;
}

bool get_usr_param(usr_param_handle_e id, void *param) {
    if (!get_recovery_usr_param()) {
        rt_log_error("Usr param is not get ready!\n");
        return false;
    }

    if (!param) {
        rt_log_error("Get usr param id:%d, param pointer null!\n", id);
        return false;
    }

    bool status = true;

    switch (id) {
        case USR_PARAM_MAINTAIN_COUNTS:
            *((uint16_t *)param) = usr_param.maintain_info.maintain_mile.maintain_count;
            break;
        
        case USR_PARAM_CUR_MAINTAIN_MILEAGE:
            *((uint16_t *)param) = usr_param.maintain_info.maintain_mile.cur_maintain_mileage;
            break;

        case USR_PARAM_LAST_MAINTAIN_MILEAGE:
            *((uint16_t *)param) = usr_param.maintain_info.maintain_mile.last_maintain_total_mileage;
            break;

        case USR_PARAM_MAINTAIN_DAYS:
            *((uint16_t *)param) = usr_param.maintain_info.maintain_time.maintain_days;
            break;

        case USR_PARAM_MAINTAIN_DATE:
            memcpy(&param, &usr_param.maintain_info.maintain_time.last_maintain_date, 
                        sizeof(maintain_date_t));
            break;

        case USR_PARAM_MAINTAIN_IS_SYNC_TIME:
            *((uint8_t *)param) = usr_param.maintain_info.maintain_time.is_sync_time;
            break;

        case USR_PARAM_MILE_FORMAT:
            *((uint8_t *)param) = usr_param.usr_set.mile_format;
            break;

        case USR_PARAM_MILE_DISPLAY:
            *((uint8_t *)param) = usr_param.usr_set.mile_display;
            break;

        case USR_PARAM_THEME:
            *((uint8_t *)param) = usr_param.usr_set.theme;
            break;

        case USR_PARAM_BRIGHTNESS_LEVEL:
            *((uint8_t *)param) = usr_param.usr_set.brightness;
            break;

        case USR_PARAM_LANGUAGE:
            *((uint8_t *)param) = usr_param.usr_set.language;
            break;

        case USR_PARAM_BT_SWITCH:
            *((uint8_t *)param) = usr_param.usr_set.bt_switch;
            break;

        case USR_PARAM_UUID_REGISTER:
            *((uint8_t *)param) = usr_param.usr_set.uuid_active_staus;
            break;

        case USR_PARAM_METER_START_SRC:
            *((uint8_t *)param) = usr_param.usr_set.start_src;
            break;

        case USR_PARAM_TCS_SWITCH:
            *((uint8_t *)param) = usr_param.usr_set.tcs_switch;
            break;

        case USR_PARAM_TEMP_UNIT:
            *((uint8_t *)param) = usr_param.usr_set.temp_unit;
            break;

        case USR_PARAM_TIME_FORMAT:
            *((uint8_t *)param) = usr_param.usr_set.time_format;
            break;

        case USR_PARAM_TPMS_PRESSURE_UNIT:
            *((uint8_t *)param) = usr_param.usr_set.tpms_unit;
            break;
            
        case USR_PARAM_AUTO_HEAD_LIGHT:
            *((uint8_t *)param) = usr_param.usr_set.auto_headlight;
            break;

        case USR_PARAM_PHONE_TYPE:
            *((uint8_t *)param) = usr_param.usr_set.phone_type;
            break;

        case USR_PARAM_SYS_LOG:
            *((uint8_t *)param) = usr_param.usr_set.sys_log;
            break;

        case USR_PARAM_DRIVE_MODE:
            *((uint8_t *)param) = usr_param.usr_set.drive_mode;
            break;

        case USR_PARAM_RESET_TYPE:
            *((uint8_t *)param) = usr_param.usr_set.reset_type;
            break;

        case USR_PARAM_EC_UUID:
            snprintf((char *)param, sizeof(usr_param.carlink_uuid), 
                    "%s", usr_param.carlink_uuid);
            break;
        
        case USR_PARAM_RIDE_TIME_A:
             *((uint32_t *)param) = usr_param.ride_info.ride_time_a;
            break;

        case USR_PARAM_RIDE_TIME_B:
            *((uint32_t *)param) = usr_param.ride_info.ride_time_b;
            break;
            
        case USR_PARAM_LEFT_FRONT_TIRE_INFO:
            memcpy(param, &usr_param.tpms[TPMS_LEFT_FRONT], sizeof(tpms_param_t));
            break;

        case USR_PARAM_RIGHT_FRONT_TIRE_INFO:
            memcpy(param, &usr_param.tpms[TPMS_RIGHT_FRONT], sizeof(tpms_param_t));
            break;

        case USR_PARAM_LEFT_REAR_TIRE_INFO:
            memcpy(param, &usr_param.tpms[TPMS_LEFT_REAR], sizeof(tpms_param_t));
            break;

        case USR_PARAM_RIGHT_REAR_TIRE_INFO:
            memcpy(param, &usr_param.tpms[TPMS_RIGHT_REAR], sizeof(tpms_param_t));
            break;

        case USR_PARAM_BT_TPMS_MAC:
            memcpy(param, usr_param.bt_tpms_mac, sizeof(usr_param.bt_tpms_mac));
            break;
            
        case USR_PARAM_MCU_UPDATE_LEN:
            *((uint16_t *)param) = usr_param.mcu_update.update_mcu_len;
            break;  

        case USR_PARAM_MCU_UPDATE_TYPE:
            *((uint8_t *)param) = usr_param.mcu_update.update_type;
            break;

        case USR_PARAM_START_SRC:
            *((uint8_t *)param) = usr_param.usr_set.start_src;
            break;

        /* 续航参考里程数组: 传入 uint8_t[REF_MILE_ARR_NUM] 缓冲 */
        case USR_PARAM_REF_MILE_ARR_P1:
        case USR_PARAM_REF_MILE_ARR_P2:
        case USR_PARAM_REF_MILE_ARR_P3:
            memcpy(param, usr_param.ref_mile_arr[id - USR_PARAM_REF_MILE_ARR_P1],
                   REF_MILE_ARR_NUM);
            break;

        default:
            status = false;
            break;
    }

    return status;
}

bool set_usr_param(usr_param_handle_e id, void *param) {
     if (!get_recovery_usr_param()) {
        rt_log_error("Usr param is not get ready!\n");
        return false;
    }

    if (!param) {
        rt_log_error("Set usr param id:%d, param pointer null!\n", id);
        return false;
    }

    bool is_save = false;

    switch (id) {
        case USR_PARAM_MAINTAIN_COUNTS:
            if (usr_param.maintain_info.maintain_mile.maintain_count != *((uint16_t *)param)) {
                usr_param.maintain_info.maintain_mile.maintain_count = *((uint16_t *)param);
                is_save = true;
            }
            break;
        
        case USR_PARAM_CUR_MAINTAIN_MILEAGE:
            if (usr_param.maintain_info.maintain_mile.cur_maintain_mileage != *((uint16_t *)param)) {
                usr_param.maintain_info.maintain_mile.cur_maintain_mileage = *((uint16_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_LAST_MAINTAIN_MILEAGE:
            if (usr_param.maintain_info.maintain_mile.last_maintain_total_mileage != *((uint16_t *)param)) {
                usr_param.maintain_info.maintain_mile.last_maintain_total_mileage = *((uint16_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_MAINTAIN_DAYS:
            if (usr_param.maintain_info.maintain_time.maintain_days != *((uint16_t *)param)) {
                usr_param.maintain_info.maintain_time.maintain_days = *((uint16_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_MAINTAIN_DATE:
            if (memcmp(&usr_param.maintain_info.maintain_time.last_maintain_date, param,
                       sizeof(maintain_date_t)) != 0) {
                memcpy(&usr_param.maintain_info.maintain_time.last_maintain_date, 
                        param, sizeof(maintain_date_t));
                is_save = true;
            }
            break;

        case USR_PARAM_MAINTAIN_IS_SYNC_TIME:
            if (usr_param.maintain_info.maintain_time.is_sync_time != *((uint8_t *)param)) {
                usr_param.maintain_info.maintain_time.is_sync_time = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_MILE_FORMAT:
            if (usr_param.usr_set.mile_format != *((uint8_t *)param)) {
                usr_param.usr_set.mile_format = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_MILE_DISPLAY:
            if (usr_param.usr_set.mile_display != *((uint8_t *)param)) {
                usr_param.usr_set.mile_display = *((uint8_t *)param); 
                is_save = true;
            }
            break;

        case USR_PARAM_THEME:
            if (usr_param.usr_set.theme != *((uint8_t *)param)) {
                usr_param.usr_set.theme = *((uint8_t *)param);  
                #ifdef DEFAULT_START_UI_THEME
                is_save = false;
                #else
                is_save = true;
                #endif
            }
            break;

        case USR_PARAM_BRIGHTNESS_LEVEL:
            if (usr_param.usr_set.brightness != *((uint8_t *)param)) {
                usr_param.usr_set.brightness = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_LANGUAGE:
            if (usr_param.usr_set.language != *((uint8_t *)param)) {
                usr_param.usr_set.language = *((uint8_t *)param); 
                is_save = true;
            }
            break;

        case USR_PARAM_BT_SWITCH:
            if (usr_param.usr_set.bt_switch != *((uint8_t *)param)) {
                usr_param.usr_set.bt_switch = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_UUID_REGISTER:
            if (usr_param.usr_set.uuid_active_staus != *((uint8_t *)param)) {
                usr_param.usr_set.uuid_active_staus = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_METER_START_SRC:
            if (usr_param.usr_set.start_src != *((uint8_t *)param)) {
                usr_param.usr_set.start_src = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_TCS_SWITCH:
            if (usr_param.usr_set.tcs_switch != *((uint8_t *)param)) {
                usr_param.usr_set.tcs_switch = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_TEMP_UNIT:
            if (usr_param.usr_set.temp_unit != *((uint8_t *)param)) {
                usr_param.usr_set.temp_unit = *((uint8_t *)param);  
                is_save = true;
            }
            break;

        case USR_PARAM_TIME_FORMAT:
            if (usr_param.usr_set.time_format != *((uint8_t *)param)) {
                usr_param.usr_set.time_format = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_TPMS_PRESSURE_UNIT:
            if (usr_param.usr_set.tpms_unit != *((uint8_t *)param)) {
                usr_param.usr_set.tpms_unit = *((uint8_t *)param); 
                is_save = true;
            }
            break;
            
        case USR_PARAM_AUTO_HEAD_LIGHT:
            if (usr_param.usr_set.auto_headlight != *((uint8_t *)param)) {
                usr_param.usr_set.auto_headlight = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_PHONE_TYPE:
            if (usr_param.usr_set.phone_type != *((uint8_t *)param)) {
                usr_param.usr_set.phone_type = *((uint8_t *)param); 
                is_save = true;
            }
            break;

        case USR_PARAM_SYS_LOG:
            if (usr_param.usr_set.sys_log != *((uint8_t *)param)) {
                usr_param.usr_set.sys_log = *((uint8_t *)param); 
                is_save = true;
            }
            break;

        case USR_PARAM_DRIVE_MODE:
            if (usr_param.usr_set.drive_mode != *((uint8_t *)param)) {
                usr_param.usr_set.drive_mode = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_RESET_TYPE:
            if (usr_param.usr_set.reset_type != *((uint8_t *)param)) {
                usr_param.usr_set.reset_type = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_EC_UUID:
            if (strncmp((const char *)usr_param.carlink_uuid, ((const char *)param),
                        strlen(((const char *)param))) != 0) {
                memcpy(usr_param.carlink_uuid, param, strlen(((const char *)param)));
                is_save = true;
            }
            break;

        case USR_PARAM_RIDE_TIME_A:
            is_save = false;
            if (usr_param.ride_info.ride_time_a != *((uint32_t *)param)) {
                usr_param.ride_info.ride_time_a = *((uint32_t *)param);  
            }
            break;

        case USR_PARAM_RIDE_TIME_B:
            is_save = false;
            if (usr_param.ride_info.ride_time_b != *((uint32_t *)param)) {
                usr_param.ride_info.ride_time_b = *((uint32_t *)param);  
            }
            break;    

        case USR_PARAM_LEFT_FRONT_TIRE_INFO:
            is_save = false;
            if (memcmp(&usr_param.tpms[TPMS_LEFT_FRONT], param,
                       sizeof(tpms_param_t)) != 0) {
                memcpy(&usr_param.tpms[TPMS_LEFT_FRONT], param,
                       sizeof(tpms_param_t));
            }
            break;
        case USR_PARAM_RIGHT_FRONT_TIRE_INFO:
            is_save = false;
            if (memcmp(&usr_param.tpms[TPMS_RIGHT_FRONT], param,
                       sizeof(tpms_param_t)) != 0) {
                memcpy(&usr_param.tpms[TPMS_RIGHT_FRONT], param,
                       sizeof(tpms_param_t));
            }
            break;

        case USR_PARAM_LEFT_REAR_TIRE_INFO:
            is_save = false;
            if (memcmp(&usr_param.tpms[TPMS_LEFT_REAR], param,
                       sizeof(tpms_param_t)) != 0) {
                memcpy(&usr_param.tpms[TPMS_LEFT_REAR], param,
                       sizeof(tpms_param_t));
            }
            break;

        case USR_PARAM_RIGHT_REAR_TIRE_INFO:
            is_save = false;
            if (memcmp(&usr_param.tpms[TPMS_RIGHT_REAR], param,
                       sizeof(tpms_param_t)) != 0) {
                memcpy(&usr_param.tpms[TPMS_RIGHT_REAR], param,
                       sizeof(tpms_param_t));
            }
            break;

        case USR_PARAM_BT_TPMS_MAC:
            is_save = true;     ///< 配对信息必须落盘
            if (memcmp(usr_param.bt_tpms_mac, param,
                       sizeof(usr_param.bt_tpms_mac)) != 0) {
                memcpy(usr_param.bt_tpms_mac, param,
                       sizeof(usr_param.bt_tpms_mac));
            }
            break;

        case USR_PARAM_MCU_UPDATE_LEN:
            if (usr_param.mcu_update.update_mcu_len != *((uint16_t *)param)) {
                usr_param.mcu_update.update_mcu_len = *((uint16_t *)param);  
                is_save = true;
            }
            break;   

        case USR_PARAM_MCU_UPDATE_TYPE:
            if (usr_param.mcu_update.update_type != *((uint8_t *)param)) {
                usr_param.mcu_update.update_type = *((uint8_t *)param);
                is_save = true;
            }
            break;

        case USR_PARAM_START_SRC:
            if (usr_param.usr_set.start_src != *((uint8_t *)param)) {
                usr_param.usr_set.start_src = *((uint8_t *)param);
                is_save = true;
            }
            break;

        /* 续航参考里程数组: 传入 uint8_t[REF_MILE_ARR_NUM] 缓冲 */
        case USR_PARAM_REF_MILE_ARR_P1:
        case USR_PARAM_REF_MILE_ARR_P2:
        case USR_PARAM_REF_MILE_ARR_P3: {
            uint8_t *dst = usr_param.ref_mile_arr[id - USR_PARAM_REF_MILE_ARR_P1];
            if (memcmp(dst, param, REF_MILE_ARR_NUM) != 0) {
                memcpy(dst, param, REF_MILE_ARR_NUM);
                is_save = true;
            }
            break;
        }

        default:
            is_save = false;
            break;
    }
    
#ifdef PARAM_WEAR_LEVEL_ENABLE
    if (is_save) {
        is_save = false;
        if (save_usr_param() != 0) {
            return false;
        }
    }
#else
    (void)is_save;
#endif

    return true;
}

int usr_param_init(void) {
    if (!read_set_param()) {
        rt_log_error("read json config param failed\n");
        return -1;
    } 

    usr_param = get_set_param();
    usr_param_pre = usr_param;

    if (usr_param.usr_set.start_src == 1) {
        usr_param.usr_set.start_src = 0;
        check_start_source(1);
        if (save_usr_param() != 0) {
            rt_log_error("save start src param failed!\n");
        }
    } else {
        check_start_source(0);
    }

    return 0;
}

bool get_recovery_usr_param(void) {
    return is_recovery_usr_param;
}

bool is_acc_start(void) {
  return start_by_acc;
}

void clean_eeprom_operate(void) {
    printf("Clear eeprom operate...\r\n");
    maintain_info_t maintence_temp;
    memset(&maintence_temp, 0, sizeof(maintain_info_t));
    memcpy(&usr_param.maintain_info, &maintence_temp, sizeof(maintain_info_t));
    if (save_usr_param() != 0) {
        rt_log_info("Clear eeprom save usr param failed!\r\n");
    }
}

/**
 * @brief  恢复出厂设置: 删除落盘的用户参数 JSON 文件, 随后立即复位重启.
 *         只动 JSON, 不碰 EEPROM 里程/保养等其它数据.
 *         重启后 read_set_param() 找不到 /temp 副本会按默认值重建, 故此处无需写默认值.
 * @return -1:失败(未重启)  成功时不返回(已复位)
 */
int restore_factory_usr_param(void) {
    char file_path[128] = {0};
    char *file_name = find_file_name(DEST_CONFIG_FILE_PATH,
                                     CONFIG_FILE_SUFFIX, SET_PARAM_PREFIX);

    if (!file_name || strlen(file_name) == 0) {
        rt_log_error("usr param file not found, reset abort!\n");
        return -1;
    }

    snprintf(file_path, sizeof(file_path), "%s/%s%s",
             DEST_CONFIG_FILE_PATH, file_name, CONFIG_FILE_SUFFIX);

    if (remove(file_path) != 0) {
        rt_log_error("remove %s failed!\n", file_path);
        return -1;
    }
    rt_log_info("usr param %s removed, reboot...\n", file_path);

    rt_thread_mdelay(500);      /* 等 littlefs 把删除提交落盘, 再重启 */
    system("reboot");

    return 0;
}
