#ifndef __SET_PARAM_H__
#define __SET_PARAM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "dev_config.h"
#include "uart_tpms.h"

#define pads(index, num)   uint8_t pads##index[num]
#define MAX_UUID_LEN        (20)

/* 蓝牙胎压传感器配对信息, 见 mw/bt_tpms/ */
#define BT_TPMS_WHEEL_NUM   (2)     ///< 蓝牙胎压车轮数 (两轮: 前/后)
#define BT_TPMS_MAC_LEN     (6)     ///< 传感器 MAC 字节数

/* 续航里程参考数组: 每包 10 个"每 1% SOC 的参考里程", 8bit × 0.01km 编码 */
#define REF_MILE_PACK_NUM     (3)      ///< 电池包数 (1 固定包 + 2 换电包)
#define REF_MILE_ARR_NUM      (10)     ///< 每包数组长度
#define REF_MILE_ARR_DEFAULT  (100)    ///< 默认参考里程 1.00km (100 × 0.01km)

typedef struct {
    uint16_t year;             ///< 年    
    uint8_t mon;               ///< 月
    uint8_t day;              ///< 日
} maintain_date_t;

typedef struct {
    maintain_date_t last_maintain_date;  ///< 上次保养日期 年月日
    uint16_t maintain_days;     ///< 维修天数，默认365天，可设置
    uint8_t is_sync_time;    ///< 是否同步过互联网时间 0:未同步 1:同步过
    uint8_t reserve;        ///< 预留1字节
} maintain_time_t;

typedef struct {
    uint16_t maintain_count;        ///< 保养次数
    uint16_t cur_maintain_mileage;  ///< 当前段保养距离 1000Km, 5000Km等，可设置
    uint32_t last_maintain_total_mileage; ///< 上次保养总里程
} maintain_mileage_t;
typedef struct {
    uint8_t update_type;         ///< 0:无 1:usb  2:ota
    uint16_t update_mcu_len;
    uint8_t reserve; 
} update_param_t; ///<8 Bytes

/**
 * @brief 保养信息结构体
 * @note 分为里程保养和时间保养
 */
typedef struct {
    maintain_mileage_t maintain_mile; ///< 里程保养
    maintain_time_t maintain_time;  ///< 时间保养
} maintain_info_t;

typedef struct {
    uint32_t ride_time_a;  ///< 骑行时间a,单位s
    uint32_t ride_time_b;  ///< 骑行时间b,单位s 
} ride_param_t;

typedef struct {
    uint32_t mile_format :1;  ///< 里程格式 0:公制 1：英制
    uint32_t mile_display :2; ///< 里程显示模式 0：显示ODO 1:trip_a 2:trip_b
    uint32_t theme :1;        ///< 显示主题 0：白天 1:黑夜 2:自动
    uint32_t brightness :3;  ///< 亮度等级 0:自动 1-5级
    uint32_t language   :3; ///< 语言 0:英文 1:中文,预留位
    uint32_t bt_switch :1;  ///< 蓝牙开关 0：关闭 1:打开
    uint32_t uuid_active_staus :1; ///< uuid激活状态 0:未激活 1:已激活
    uint32_t start_src :1;       ///< 启动源 0:bat 1:ign
    uint32_t tcs_switch:1;       ///< tcs开关 0:关闭 1:开启
    uint32_t temp_unit :1;      ///< 温度单位 0:摄氏度 1:华氏度、
    uint32_t time_format :1;   ///< 时间格式 0:12小时制 1:24小时制
    uint32_t tpms_unit:1;      ///< 胎压单位 0:KPA 1:BAR 2:PSI
    uint32_t auto_headlight :1; ///< 自动大灯状态 0:关闭 1:打开
    uint32_t phone_type :1;   ///< 手机类型 0:安卓  1:ios
    uint32_t sys_log :1;     ///< 系统日志控制开关 0:关闭 1:开启
    uint32_t drive_mode :1;  ///< 驾驶模式 0：eco经济模式  1:驾驶模式
    uint32_t reset_type :1;  ///< 里程清零方式 0:按键清零(主页长按 BACK) 1:下电清零(IGN OFF)
    uint32_t resume :9;      ///< 预留
    uint32_t usr_reserve;   ///< 用户设置参数预留 4字节
} usr_setting_t;

typedef struct {
    maintain_info_t maintain_info;
    usr_setting_t usr_set;
    ride_param_t ride_info;
    tpms_param_t tpms[MAX_WHEEL_POS_NUM];
    char carlink_uuid[MAX_UUID_LEN];
    update_param_t mcu_update;
    ///< 三包续航参考里程数组, 8bit × 0.01km; 占用原预留区 36 字节中的 30 字节,
    ///< 结构体总大小由此保持不变 (92 + 30 + 6 = 128 字节)
    uint8_t ref_mile_arr[REF_MILE_PACK_NUM][REF_MILE_ARR_NUM];
    pads(1, 6);         ///< 预留6个字节
    ///< 蓝牙胎压传感器 MAC (索引 0=前轮/左前, 1=后轮/左后), 全 0 表示未配对
    uint8_t bt_tpms_mac[BT_TPMS_WHEEL_NUM][BT_TPMS_MAC_LEN];
} usr_param_t;

///< usr_set.reset_type 取值（小计里程清零方式）
#define RESET_TYPE_BY_BUTTON        (0)   ///< 按键清零：主页长按 BACK 时清零
#define RESET_TYPE_AFTER_POWER_OFF  (1)   ///< 下电清零：IGN OFF 时清零

/**
 * @brief usr param enum
 */
typedef enum {
    USR_PARAM_NONE,

    /* 里程保养参数 */
    USR_PARAM_MAINTAIN_COUNTS,
    USR_PARAM_CUR_MAINTAIN_MILEAGE,
    USR_PARAM_LAST_MAINTAIN_MILEAGE,

    /* 里程保养参数 */
    USR_PARAM_MAINTAIN_DAYS,
    USR_PARAM_MAINTAIN_DATE,
    USR_PARAM_MAINTAIN_IS_SYNC_TIME,

    /* 仪表设置参数 */
    USR_PARAM_MILE_FORMAT,
    USR_PARAM_MILE_DISPLAY,
    USR_PARAM_THEME,
    USR_PARAM_BRIGHTNESS_LEVEL,
    USR_PARAM_LANGUAGE,
    USR_PARAM_BT_SWITCH,
    USR_PARAM_UUID_REGISTER,
    USR_PARAM_METER_START_SRC,
    USR_PARAM_TCS_SWITCH,
    USR_PARAM_TEMP_UNIT,
    USR_PARAM_TIME_FORMAT,
    USR_PARAM_TPMS_PRESSURE_UNIT,
    USR_PARAM_AUTO_HEAD_LIGHT,
    USR_PARAM_PHONE_TYPE,
    USR_PARAM_SYS_LOG,
    USR_PARAM_DRIVE_MODE,
    USR_PARAM_RESET_TYPE,     ///< 里程清零方式 0:按键清零 1:下电清零

    /* 手机互联uuid */
    USR_PARAM_EC_UUID,

    /* 骑行时间 */
    USR_PARAM_RIDE_TIME_A,
    USR_PARAM_RIDE_TIME_B,
    
    /* 胎压信息 */
    USR_PARAM_LEFT_FRONT_TIRE_INFO,
    USR_PARAM_RIGHT_FRONT_TIRE_INFO,
    USR_PARAM_LEFT_REAR_TIRE_INFO,
    USR_PARAM_RIGHT_REAR_TIRE_INFO,

    /* mcu更新文件长度 */
    USR_PARAM_MCU_UPDATE_LEN,
    USR_PARAM_MCU_UPDATE_TYPE,

    USR_PARAM_START_SRC,

    /* 续航参考里程数组 (三包各 10 个, 8bit × 0.01km) */
    USR_PARAM_REF_MILE_ARR_P1,
    USR_PARAM_REF_MILE_ARR_P2,
    USR_PARAM_REF_MILE_ARR_P3,

    /* 蓝牙胎压传感器 MAC (2 轮 × 6 字节, 见 mw/bt_tpms/) */
    USR_PARAM_BT_TPMS_MAC,
} usr_param_handle_e;

/**
 * @brief  读取usr param准备状态
 * @param  none
 * @return false:参数未准备好 true:已准备好
 */
bool get_recovery_usr_param(void);

/**
 * @brief  读取usr param参数
 * @param  id 用户参数句柄
 * @param  param 获取用户参数指针变量，根据不同类型参数，传入对应类型指针
 * @return false:读取失败 true:读取成功
 */
bool get_usr_param(usr_param_handle_e id, void *param);

/**
 * @brief  设置usr param参数
 * @param  id 用户参数句柄
 * @param  param 设置用户参数指针变量，根据不同类型参数，传入对应类型指针
 * @return false:设置失败  true:设置成功
 */
bool set_usr_param(usr_param_handle_e id, void *param);

/**
 * @brief  保存usr param参数
 * @param  none
 * @return 0:保存成功  -1:保存失败
 */
int save_usr_param(void);

/**
 * @brief  关机前保存用户参数: 启动源置 1 并落盘
 *         (IGN/NFC/无线钥匙关机通用)
 * @return 0:保存成功  -1:保存失败
 */
int shutdown_save_usr_param(void);

/**
 * @brief  用户参数初始化
 * @param  none
 * @return 0:初始化成功  -1:初始化失败
 */
int usr_param_init(void);

bool is_acc_start(void);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // SET_PARAM_H__