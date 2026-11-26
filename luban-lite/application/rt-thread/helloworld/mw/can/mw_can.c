#include <rtthread.h>
#include "rtdevice.h"
#include "mw_can.h"
#include "usr_utils.h"
#include "mw_can_tx.h"
#include "rtconfig.h"
#include "vehicle_param.h"
#include "dev_config.h"
#include "app_log.h"
#include "dev_state.h"
#include "aic_core.h"
#include "aic_hal_gpio.h"
#include "aic_hal_can.h"
#include "mile_param.h"
#include "set_param.h"
#include "power_manage.h"
#include "mw_wdt.h"

// #define CAN_RX_DEBUG

#if MW_CAN_ENABLE

#define GET_DATA_BITS(data,offset,mask) ((data >> offset) & mask)
///< 弹窗码位掩码: code 取 pop_tip_code_e 枚举值 (1-based),
///<   与 UI 侧 POP_TIP_BIT(code)=1<<(code-1) 一致
#define POP_TIP_MASK(code)          (1u << ((code) - 1))
#define POP_TIP_BIT(code, status)   ((status) ? POP_TIP_MASK(code) : 0u)
///< 整车CAN网络信号均为 intel(小端)字节序, 提供跨字节小端提取辅助
#define LE_U8(buf, byte)        (buf[byte])
#define LE_U16(buf, byte)        ((uint16_t)(buf[byte]) | ((uint16_t)(buf[byte+1]) << 8))
#define LE_U24(buf, byte)        ((uint32_t)(buf[byte]) | ((uint32_t)(buf[byte+1]) << 8) \
                                  | ((uint32_t)(buf[byte+2]) << 16))
///< 大端(Motorola)字节序提取辅助: 低地址为高位字节
#define BE_U8(buf, byte)        (buf[byte])
#define BE_U16(buf, byte)        (((uint16_t)(buf[byte]) << 8) | (uint16_t)(buf[byte+1]))
#define BE_U24(buf, byte)        (((uint32_t)(buf[byte]) << 16) | ((uint32_t)(buf[byte+1]) << 8) \
                                  | (uint32_t)(buf[byte+2]))
#define CAN_RX_THREAD_PRIO  (14)
#define MW_CAN_DEVICE_NAME    "can0"
#define MW_CAN_STB_PIN        "PE.15"

static can_rx_timeout rx_timeout = {0};

static rt_device_t can_dev;
static struct rt_semaphore can_rx_sem;
static rt_thread_t g_can_rx_tid = RT_NULL;
static rt_thread_t g_can_timeout_tid = RT_NULL;
static uint32_t g_can_stb_pin = 0;
static rt_bool_t g_can_rx_sem_inited = RT_FALSE;

///< ===========================================================================
///< VEH_DTC_CODE (ICON_MW 故障图标) 汇总
///<   10a/403/BMS5/BMS6/a5/430 任一路报故障则置 1, 全部无故障才置 0 (字段非 0 即故障)。
///<   各来源按位独立记忆, 避免后到的正常报文把先到的故障覆盖掉;
///<   BMS5/BMS6 三块电池包 (P1/P2/P3) 各自独立。
///< ===========================================================================
typedef enum {
    DTC_SRC_10A = 0,   ///< VCU_DTC_Code@0x10A
    DTC_SRC_403,       ///< MCUErrBits@0x403
    DTC_SRC_BMS5_P1,   ///< BMS5@0x305
    DTC_SRC_BMS5_P2,   ///< BMS5@0x315
    DTC_SRC_BMS5_P3,   ///< BMS5@0x325
    DTC_SRC_BMS6_P1,   ///< BMS6@0x306
    DTC_SRC_BMS6_P2,   ///< BMS6@0x316
    DTC_SRC_BMS6_P3,   ///< BMS6@0x326
    DTC_SRC_A5,        ///< ABS_FPC_DTC@0xA5
    DTC_SRC_430,       ///< DTC_Code@0x430
    DTC_SRC_93,       ///< NFC_DTC@0x93
    DTC_SRC_MAX,
} dtc_src_e;

///< 各来源故障状态位图, 任一位为 1 则 VEH_DTC_CODE=1
static uint32_t s_dtc_fault_mask = 0;

static void dtc_code_update(dtc_src_e src, int fault)
{
    ///< rx 线程 (置位) 与 can_timeout 线程 (超时清位) 都会做读-改-写,
    ///< 低优先级的清位若在 RMW 中途被抢占, 会用旧值覆盖刚置上的故障位.
    ///< 用调度锁保证 RMW 原子 (同 can_popup_msg_apply).
    rt_enter_critical();
    uint32_t old_mask = s_dtc_fault_mask;
    if (fault)
        s_dtc_fault_mask |= (1u << src);
    else
        s_dtc_fault_mask &= ~(1u << src);

    /* 超时清理每 100ms 都会调用, 掩码未变时不重复写槽位 */
    if (s_dtc_fault_mask != old_mask)
        vehicle_set_data(VEH_DTC_CODE, s_dtc_fault_mask ? 1 : 0);
    rt_exit_critical();
}

///< 双源车速仲裁 (0x405 MCU / 0xA3 ABS):
///<   两源各自缓存最近一帧车速, 只有都在 SPEED_SRC_VALID_MS 内收到, 且差值
///<   不超过 SPEED_MATCH_TOLERANCE 时才写入 VEH_SPEED_CURRENT, 否则写 0.
///<   单源缺失/超时、两源矛盾都按"不一致"处理.
#define SPEED_MATCH_TOLERANCE (2)      ///< km/h, 容忍取整与采样时差
#define SPEED_SRC_VALID_MS    (2000)   ///< 车速超时清零 2s

///< CAN 超时清零阈值: 故障/警告灯 3s, 功率及普通数据 1s
#define CAN_TIMEOUT_LAMP_MS   (3000)
#define CAN_TIMEOUT_DATA_MS   (1000)

static int32_t   s_speed_mcu = 1;     ///< -1: 无有效值
static rt_tick_t s_speed_mcu_tick = 0;
static rt_tick_t s_over_speed_tick = 0;

static void speed_current_arbitrate(void)
{
    rt_tick_t now = rt_tick_get();
    rt_tick_t valid_ticks = rt_tick_from_millisecond(SPEED_SRC_VALID_MS);

    if ((now - s_speed_mcu_tick) > valid_ticks) s_speed_mcu = -1;

    if (s_speed_mcu < 0) {
        vehicle_set_data(VEH_SPEED_CURRENT, 0);
        return;
    }
}

static rt_err_t can_rx_callback(rt_device_t dev, rt_size_t size) {
    /* 开机动画期间 (IDLE/RUNNING) 屏蔽 CAN 接收:
     *   1) 避免 CAN ISR / rx_thread 抢占 PNG 解码, 导致动画前几帧卡死或花屏
     *   2) 同时抑制噪声帧触发 "can_ecu_xxx dlc[%d] or msg buff err!" 误报
     *   3) 动画结束 (ANIMATION_STATE_END) 后恢复正常接收.
     * 读取一个 enum 静态变量, 在 ISR 上下文里安全且开销极小. */
    if (get_boot_animation_status() != ANIMATION_STATE_END) {
        return RT_EOK;
    }

    rt_sem_release(&can_rx_sem);

    return RT_EOK;
}

///< VEH_CAN_POPUP_MSG 位图更新 (先清后置).
///<   该位图由 CAN rx 线程 (prio 14) 与 can_timeout 线程 (prio 15) 并发做读-改-写;
///<   低优先级的超时清理在 RMW 中途被 rx 线程抢占时, 会用旧值覆盖刚置上的位
///<   (弹窗位丢失或卡住). 用调度锁保证 RMW 期间不发生线程切换.
///<   set 与 clr 允许重叠, 便于做"三选一"赋值.
static void can_popup_msg_apply(uint32_t set_mask, uint32_t clr_mask)
{
    rt_enter_critical();
    uint32_t m = (uint32_t)vehicle_get_data(VEH_CAN_POPUP_MSG);
    m = (m & ~clr_mask) | set_mask;
    vehicle_set_data(VEH_CAN_POPUP_MSG, (int32_t)m);
    rt_exit_critical();
}

static void timeout_clear_can_data(void) {
    uint32_t current_tick = rt_tick_get();

    ///< 整车CAN网络信号超时清零 — 按 ECU 分组, 仅清该 ECU 独占或主要提供的槽位.
    ///< 阈值两档: 故障/警告灯 3s (CAN_TIMEOUT_LAMP_MS), 车速/功率及普通数据 1s (CAN_TIMEOUT_DATA_MS).
    if ((current_tick - rx_timeout.ecu_vcu_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_DATA_MS)) {
        if (vehicle_get_data(VEH_WORK_MODE) > 0) vehicle_set_data(VEH_WORK_MODE, 0);
        if (vehicle_get_data(VEH_VEHICLE_TYPE) > 0) vehicle_set_data(VEH_VEHICLE_TYPE, 0);
        if (vehicle_get_data(VEH_HEAT_HANDLE) > 0) vehicle_set_data(VEH_HEAT_HANDLE, 0);
        if (vehicle_get_data(VEH_SEAT_STATE) > 0) vehicle_set_data(VEH_SEAT_STATE, 0);
        if (vehicle_get_data(VEH_EMERGENCY_SHUTDOWN) > 0) vehicle_set_data(VEH_EMERGENCY_SHUTDOWN, 0);
        if (vehicle_get_data(VEH_DISCHARGE_BAT_NUM) > 0) vehicle_set_data(VEH_DISCHARGE_BAT_NUM, 0);
        ///< VCU 超时: 同步清座椅开锁/巡航故障弹窗位
        can_popup_msg_apply(0, POP_TIP_MASK(POP_TIP_SEAT_UNLOCK) |
                               POP_TIP_MASK(POP_TIP_CRUISE_FAULT));
    }

    ///< 0x108 灯状态单独判超时: 0x102/0x10A/0x10D 仍在发送时组时间戳会持续刷新,
    ///< 只靠 VCU 组时间戳会让远/近光、转向灯常亮
    if ((current_tick - rx_timeout.ecu_vcu_108_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_DATA_MS)) {
        if (vehicle_get_data(VEH_LIGHT_HIGH_BEAM) > 0) vehicle_set_data(VEH_LIGHT_HIGH_BEAM, 0);
        if (vehicle_get_data(VEH_LIGHT_LOW_BEAM) > 0) vehicle_set_data(VEH_LIGHT_LOW_BEAM, 0);
        if (vehicle_get_data(VEH_INDICATOR_TURN_LEFT) > 0) vehicle_set_data(VEH_INDICATOR_TURN_LEFT, 0);
        if (vehicle_get_data(VEH_INDICATOR_TURN_RIGHT) > 0) vehicle_set_data(VEH_INDICATOR_TURN_RIGHT, 0);
    }
    ///< 0x10A: VCU_DTC_Code 为 MW 故障灯聚合来源之一, 超时清对应聚合位
    if ((current_tick - rx_timeout.ecu_vcu_10a_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_LAMP_MS)) {
        dtc_code_update(DTC_SRC_10A, 0);
        can_popup_msg_apply(0, POP_TIP_MASK(POP_TIP_BATT_LOW));
    }
    ///< 0x10D: 巡航主开关 (主界面巡航图标 / 设置页巡航卡片)
    if ((current_tick - rx_timeout.ecu_vcu_10d_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_DATA_MS)) {
        if (vehicle_get_data(VEH_CRUISE_MAIN_SWITCH) > 0) vehicle_set_data(VEH_CRUISE_MAIN_SWITCH, 0);
    }

    if ((current_tick - rx_timeout.ecu_mcu_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_DATA_MS)) {
        ///< MCU 普通数据 (功率/挡位/驾驶模式/回馈功率), 1s
        if (vehicle_get_data(VEH_MCU_SLIDE_FEED) != 0) vehicle_set_data(VEH_MCU_SLIDE_FEED, 0);
        if (vehicle_get_data(VEH_MCU_BRAKE_FEED) != 0) vehicle_set_data(VEH_MCU_BRAKE_FEED, 0);
        if (vehicle_get_data(VEH_GEAR_POSITION) > 0) vehicle_set_data(VEH_GEAR_POSITION, 0);
        if (vehicle_get_data(VEH_DRIVE_MODE) > 0) vehicle_set_data(VEH_DRIVE_MODE, 0);
        if (vehicle_get_data(VEH_TRAM_POWR) != 0) vehicle_set_data(VEH_TRAM_POWR, 0);
    }
    ///< 0x403 故障灯/故障等级 3s 超时: 与 1s 的功率/挡位分开判
    if ((current_tick - rx_timeout.ecu_mcu_403_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_LAMP_MS)) {
        if (vehicle_get_data(VEH_MCU_DTC) > 0) vehicle_set_data(VEH_MCU_DTC, 0);
        if (vehicle_get_data(VEH_MCU_FAULT_LEVEL) > 0) vehicle_set_data(VEH_MCU_FAULT_LEVEL, 0);
        ///< MCUErrBits 为 MW 故障灯聚合来源之一, 超时清对应聚合位
        dtc_code_update(DTC_SRC_403, 0);
    }

    ///< ABS 报警灯: 0xA3/0xA4/0xA5 断连 3s 后置 1 常亮提示.
    ///<   ecu_abs_rx_timeout 为 0 表示本上电周期从未收到过 ABS 组报文 (静态零初始化),
    ///<   此时直接视为断连, 不再依赖 current_tick 是否已超过 3s.
    if ((rx_timeout.ecu_abs_rx_timeout == 0) ||
        ((current_tick - rx_timeout.ecu_abs_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_LAMP_MS))) {
        if (vehicle_get_data(VEH_LIGHT_ABS) != 1) vehicle_set_data(VEH_LIGHT_ABS, 1);
    }
    ///< 0xA4 单独判超时: 0xA3/0xA5 仍在发送时组时间戳会持续刷新, TCS 灯会一直闪/亮
    if ((current_tick - rx_timeout.ecu_tcs_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_LAMP_MS)) {
        if (vehicle_get_data(VEH_TCS_WARNING) != 0) vehicle_set_data(VEH_TCS_WARNING, 0);
    }
    ///< 0xA5: ABS_FPC_DTC 为 MW 故障灯聚合来源之一, 超时清对应聚合位
    if ((current_tick - rx_timeout.ecu_abs_a5_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_LAMP_MS)) {
        dtc_code_update(DTC_SRC_A5, 0);
    }

    ///< 车速: 双源仲裁 — 任一源超时 / 两源不一致都会置 0
    speed_current_arbitrate();


    if ((current_tick - rx_timeout.ecu_bms_p1_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_DATA_MS)) {
        if (vehicle_get_data(VEH_BAT_MAX_TEMP_P1) != 0) vehicle_set_data(VEH_BAT_MAX_TEMP_P1, 0);
        if (vehicle_get_data(VEH_BAT_VOLTAGE_P1) > 0) vehicle_set_data(VEH_BAT_VOLTAGE_P1, 0);
        if (vehicle_get_data(VEH_BAT_P_VOLT_P1) > 0) vehicle_set_data(VEH_BAT_P_VOLT_P1, 0);
        if (vehicle_get_data(VEH_BAT_SOH_P1) > 0) vehicle_set_data(VEH_BAT_SOH_P1, 0);
        if (vehicle_get_data(VEH_BAT_SOC_P1) > 0) vehicle_set_data(VEH_BAT_SOC_P1, 0);
        if (vehicle_get_data(VEH_BAT_WORK_STATUS_P1) > 0) vehicle_set_data(VEH_BAT_WORK_STATUS_P1, 0);
    }
    if ((current_tick - rx_timeout.ecu_bms_p2_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_DATA_MS)) {
        if (vehicle_get_data(VEH_BAT_MAX_TEMP_P2) != 0) vehicle_set_data(VEH_BAT_MAX_TEMP_P2, 0);
        if (vehicle_get_data(VEH_BAT_VOLTAGE_P2) > 0) vehicle_set_data(VEH_BAT_VOLTAGE_P2, 0);
        if (vehicle_get_data(VEH_BAT_P_VOLT_P2) > 0) vehicle_set_data(VEH_BAT_P_VOLT_P2, 0);
        if (vehicle_get_data(VEH_BAT_SOH_P2) > 0) vehicle_set_data(VEH_BAT_SOH_P2, 0);
        if (vehicle_get_data(VEH_BAT_SOC_P2) > 0) vehicle_set_data(VEH_BAT_SOC_P2, 0);
        if (vehicle_get_data(VEH_BAT_WORK_STATUS_P2) > 0) vehicle_set_data(VEH_BAT_WORK_STATUS_P2, 0);
    }
    if ((current_tick - rx_timeout.ecu_bms_p3_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_DATA_MS)) {
        if (vehicle_get_data(VEH_BAT_MAX_TEMP_P3) != 0) vehicle_set_data(VEH_BAT_MAX_TEMP_P3, 0);
        if (vehicle_get_data(VEH_BAT_VOLTAGE_P3) > 0) vehicle_set_data(VEH_BAT_VOLTAGE_P3, 0);
        if (vehicle_get_data(VEH_BAT_P_VOLT_P3) > 0) vehicle_set_data(VEH_BAT_P_VOLT_P3, 0);
        if (vehicle_get_data(VEH_BAT_SOH_P3) > 0) vehicle_set_data(VEH_BAT_SOH_P3, 0);
        if (vehicle_get_data(VEH_BAT_SOC_P3) > 0) vehicle_set_data(VEH_BAT_SOC_P3, 0);
        if (vehicle_get_data(VEH_BAT_WORK_STATUS_P3) > 0) vehicle_set_data(VEH_BAT_WORK_STATUS_P3, 0);
    }

    ///< 各包 DTC 故障灯 3s 超时: 与 1s 的 SOC/温度/电压分开判
    if ((current_tick - rx_timeout.ecu_bms_p1_dtc_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_LAMP_MS)) {
        if (vehicle_get_data(VEH_BAT_DTC_P1) > 0) vehicle_set_data(VEH_BAT_DTC_P1, 0);
        ///< BMS5/BMS6 的 DTC 为 MW 故障灯聚合来源之一
        dtc_code_update(DTC_SRC_BMS5_P1, 0);
        dtc_code_update(DTC_SRC_BMS6_P1, 0);
    }
    if ((current_tick - rx_timeout.ecu_bms_p2_dtc_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_LAMP_MS)) {
        if (vehicle_get_data(VEH_BAT_DTC_P2) > 0) vehicle_set_data(VEH_BAT_DTC_P2, 0);
        dtc_code_update(DTC_SRC_BMS5_P2, 0);
        dtc_code_update(DTC_SRC_BMS6_P2, 0);
    }
    if ((current_tick - rx_timeout.ecu_bms_p3_dtc_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_LAMP_MS)) {
        if (vehicle_get_data(VEH_BAT_DTC_P3) > 0) vehicle_set_data(VEH_BAT_DTC_P3, 0);
        dtc_code_update(DTC_SRC_BMS5_P3, 0);
        dtc_code_update(DTC_SRC_BMS6_P3, 0);
    }

    if ((current_tick - rx_timeout.ecu_switch_l_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_DATA_MS)) {
        if (vehicle_get_data(VEH_HORN) > 0) vehicle_set_data(VEH_HORN, 0);
        if (vehicle_get_data(VEH_BTN_WHEEL) > 0) vehicle_set_data(VEH_BTN_WHEEL, 0);
        if (vehicle_get_data(VEH_ROCKER_LEFT) > 0) vehicle_set_data(VEH_ROCKER_LEFT, 0);
        if (vehicle_get_data(VEH_ROCKER_RIGHT) > 0) vehicle_set_data(VEH_ROCKER_RIGHT, 0);
        if (vehicle_get_data(VEH_ROLLER_DIRE) > 0) vehicle_set_data(VEH_ROLLER_DIRE, 0);
        if (vehicle_get_data(VEH_ROLLER_LEN) > 0) vehicle_set_data(VEH_ROLLER_LEN, 0);
    }
    ///< 0x502 按键值不落槽位, 无超时清零内容 (ecu_switch_r_rx_timeout 仍随报文刷新)
    if ((current_tick - rx_timeout.ecu_charger_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_DATA_MS)) {
        if (vehicle_get_data(VEH_CHARGER_CURRENT) > 0) vehicle_set_data(VEH_CHARGER_CURRENT, 0);
        if (vehicle_get_data(VEH_CHARGER_VOLTAGE) > 0) vehicle_set_data(VEH_CHARGER_VOLTAGE, 0);
    }
    ///< 充电状态灯/充电器 DTC 3s 超时: 与 1s 的电流电压分开判
    if ((current_tick - rx_timeout.ecu_charger_lamp_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_LAMP_MS)) {
        if (vehicle_get_data(VEH_CHARGER_STATUS) > 0) vehicle_set_data(VEH_CHARGER_STATUS, 0);
        if (vehicle_get_data(VEH_CHARGER_DTC) > 0) vehicle_set_data(VEH_CHARGER_DTC, 0);
        ///< DTC_Code 为 MW 故障灯聚合来源之一
        dtc_code_update(DTC_SRC_430, 0);
    }
    if ((current_tick - rx_timeout.ecu_nfc_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_DATA_MS)) {
        dtc_code_update(DTC_SRC_93, 0);
        // if (vehicle_get_data(VEH_NFC_MATCH_STATE) > 0) vehicle_set_data(VEH_NFC_MATCH_STATE, 0);
        // ///< 同步清 NFC 弹窗位, 使位图与 VEH_NFC_MATCH_STATE 保持一致
        // can_popup_msg_apply(0, POP_TIP_MASK(POP_TIP_NFC_LEARNING) |
        //                        POP_TIP_MASK(POP_TIP_NFC_SUCCESS) |
        //                        POP_TIP_MASK(POP_TIP_NFC_FAILURE));
    }
    if ((current_tick - rx_timeout.ecu_tpms_rx_timeout) > rt_tick_from_millisecond(CAN_TIMEOUT_DATA_MS)) {
        if (vehicle_get_data(VEH_TPMS_MATCH_STATE) > 0) vehicle_set_data(VEH_TPMS_MATCH_STATE, 0);
    }
}

///< J1850 CRC-8 校验: 多项式 0x1D (x8+x4+x3+x2+1), 初值 0xFF, XOROUT 0xFF
///< 用于 0x776/0x777/0x778/0x779 里程/清零指令, 校验字节为第 8 字节 (buf[7])
#define CRC8_MAGIC_NUM (0x1D)

static uint8_t can_j1850_crc8(const uint8_t *data, uint8_t length) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ CRC8_MAGIC_NUM;
            } else {
                crc <<= 1;
            }
        }
        crc &= 0xFF;
    }
    crc ^= 0xFF;
    return crc;
}

///< ===========================================================================
///< 整车CAN网络信号 (xlsx 整车CAN通信网络信号) 解析函数
///< 仅仪表相关报文; 信号均为 intel(小端)字节序
///< 心跳类信号(VCU_Life/MCUAliveCounter/BMS_life/各 Live_code)只刷新 rx_timeout
///< 注: 0x776~0x779 里程/清零指令为大端字节序, 见解析函数前的注释
///< ===========================================================================

static int parse_can_msg_93_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_80 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_nfc_rx_timeout = rt_tick_get();
    int data = LE_U8(buf, 7);  ///< NFC_DTC_Code 8bit@byte7
    dtc_code_update(DTC_SRC_93, data != 0);

    return 0;
}

static int parse_can_msg_80_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_80 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    int data = GET_DATA_BITS(buf[1], 0, 0x03);  ///< match_success 2bit@byte1
    vehicle_set_data(VEH_NFC_MATCH_STATE, data);

    ///< 三种匹配状态互斥: 先清三位, 再置命中的一位
    can_popup_msg_apply(POP_TIP_BIT(POP_TIP_NFC_LEARNING, data == 1) |
                        POP_TIP_BIT(POP_TIP_NFC_FAILURE, data == 2) |
                        POP_TIP_BIT(POP_TIP_NFC_SUCCESS, data == 3),
                        POP_TIP_MASK(POP_TIP_NFC_LEARNING) |
                        POP_TIP_MASK(POP_TIP_NFC_FAILURE) |
                        POP_TIP_MASK(POP_TIP_NFC_SUCCESS));

    return 0;
}

static int parse_can_msg_102_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_102 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_vcu_rx_timeout = rt_tick_get();
    int data = GET_DATA_BITS(buf[0], 0, 0x07);  ///< Battery_Number 3bit@byte0
    vehicle_set_data(VEH_DISCHARGE_BAT_NUM, data);
    return 0;
}

static int parse_can_msg_108_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_108 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_vcu_rx_timeout = rt_tick_get();
    rx_timeout.ecu_vcu_108_rx_timeout = rx_timeout.ecu_vcu_rx_timeout;
    int data;

    data = GET_DATA_BITS(buf[0], 0, 0x07);  ///< Work_Mode 3bit
    vehicle_set_data(VEH_WORK_MODE, data);
    if (data == 0) {
        ///< VCU 通知下电: 交给统一下电入口. 该入口幂等, 周期报文重复触发只生效一次,
        ///< 不做阻塞延时 —— 保存是同步的, 断电由 MCU 负责
        power_off_request();
    }

    data = GET_DATA_BITS(buf[0], 4, 0x0F);  ///< Vehicle_Type 4bit
    vehicle_set_data(VEH_VEHICLE_TYPE, data);

    ///< VCU_Life 8bit@byte1 — 仅心跳, 不写参数

    data = GET_DATA_BITS(buf[2], 0, 0x01);  ///< High_Beam_Drive
    vehicle_set_data(VEH_LIGHT_HIGH_BEAM, data);

    data = GET_DATA_BITS(buf[2], 1, 0x01);  ///< Low_Beam_Drive
    vehicle_set_data(VEH_LIGHT_LOW_BEAM, data);

    data = GET_DATA_BITS(buf[2], 2, 0x01);  ///< Left_Signal_Drive
    vehicle_set_data(VEH_INDICATOR_TURN_LEFT, data);

    data = GET_DATA_BITS(buf[2], 3, 0x01);  ///< Right_Signal_Drive
    vehicle_set_data(VEH_INDICATOR_TURN_RIGHT, data);

    data = GET_DATA_BITS(buf[2], 4, 0x01);  ///< Double_Flashing_Drive
    vehicle_set_data(VEH_LIGHT_GB, data);

    data = GET_DATA_BITS(buf[2], 6, 0x01);  ///< Hot_Handle_Drive
    vehicle_set_data(VEH_HEAT_HANDLE, data);

    data = GET_DATA_BITS(buf[7], 0, 0x01);  ///< Seat_State
    vehicle_set_data(VEH_SEAT_STATE, data);

    can_popup_msg_apply(POP_TIP_BIT(POP_TIP_SEAT_UNLOCK, data == 1),
                        POP_TIP_MASK(POP_TIP_SEAT_UNLOCK));

    return 0;
}

static int parse_can_msg_10a_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_10a dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_vcu_rx_timeout = rt_tick_get();
    rx_timeout.ecu_vcu_10a_rx_timeout = rx_timeout.ecu_vcu_rx_timeout;
    int data = LE_U8(buf, 7);  ///< VCU_DTC_Code 8bit@byte7
    dtc_code_update(DTC_SRC_10A, data != 0);

    can_popup_msg_apply(POP_TIP_BIT(POP_TIP_BATT_LOW, data == 1),
                        POP_TIP_MASK(POP_TIP_BATT_LOW));
    return 0;
}

static int parse_can_msg_10d_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_10d dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_vcu_rx_timeout = rt_tick_get();
    rx_timeout.ecu_vcu_10d_rx_timeout = rx_timeout.ecu_vcu_rx_timeout;
    int data;

    data = GET_DATA_BITS(buf[0], 2, 0x01);  ///< VCUEmShutDown
    vehicle_set_data(VEH_EMERGENCY_SHUTDOWN, data);

    data = GET_DATA_BITS(buf[0], 3, 0x01);  ///< Cruise_Icon
    vehicle_set_data(VEH_CRUISE_MAIN_SWITCH, data);

    data = GET_DATA_BITS(buf[0], 4, 0x03);  ///< Cruise_Active
    vehicle_set_data(VEH_CRUISE_STATUS, data);

    can_popup_msg_apply(POP_TIP_BIT(POP_TIP_CRUISE_FAULT, data == 2),
                        POP_TIP_MASK(POP_TIP_CRUISE_FAULT));

    data = (int)LE_U16(buf, 1);  ///< Cruise_Speed 16bit@byte1, km/h
    vehicle_set_data(VEH_CRUISE_CONTROL_SPEED, data);

    ///< MCUPhaseU/V_AC_Current: xlsx 中 StartBit 与 Cruise_Speed 冲突(均 8),
    ///< 属表格定义瑕疵, 此处暂不解析, 避免覆盖 Cruise_Speed.
    return 0;
}

static int parse_can_msg_401_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_401 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_mcu_rx_timeout = rt_tick_get();
    int raw = LE_U16(buf, 6);  ///< MCU_MotorPower 16bit@byte6
    int data = raw;
    vehicle_set_data(VEH_TRAM_POWR, data);
    return 0;
}

static int parse_can_msg_403_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_403 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_mcu_rx_timeout = rt_tick_get();
    rx_timeout.ecu_mcu_403_rx_timeout = rx_timeout.ecu_mcu_rx_timeout;
    int data;

    data = LE_U8(buf, 0);  ///< MCUErrBits
    vehicle_set_data(VEH_MCU_DTC, data);
    dtc_code_update(DTC_SRC_403, data != 0);

    data = GET_DATA_BITS(buf[1], 0, 0x0F);  ///< MCUFaultLevl 4bit@byte1
    vehicle_set_data(VEH_MCU_FAULT_LEVEL, data);

    ///< MCUAliveCounter3 8bit@byte7 — 仅心跳
    return 0;
}

static int parse_can_msg_404_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_404 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_mcu_rx_timeout = rt_tick_get();
    int data;

    ///< MCUGearSts: xlsx 0:P 1:R 2:D 3:D 7:D -> 项目 0:P 1:D 2:D 3:R
    data = GET_DATA_BITS(buf[2], 0, 0x07);
    int gear, mode;
    switch (data) {
        case 0:  gear = 0; break;
        case 1:  gear = 3; break;
        case 2:  gear = 2; break;
        case 3:  gear = 2; break;
        case 4:  gear = 2; break;
        default: gear = 0; break;
    }
    vehicle_set_data(VEH_GEAR_POSITION, gear);

    switch (data) {
        case 0:
        case 1:  mode = 0; break;  ///< SETTING
        case 2:  mode = 2; break;  ///< ECO
        case 3:  mode = 1; break;  ///< Normal
        case 4:  mode = 3; break;  ///< SPORT
        default: mode = 0; break;  ///< SETTING
    }
    vehicle_set_data(VEH_DRIVE_MODE, mode);

    data = (int)(LE_U16(buf, 3) * 0.001 - 30);  ///< MCUSlideFeedPower, res0.001 offset-30
    vehicle_set_data(VEH_MCU_SLIDE_FEED, data);

    data = (int)(LE_U16(buf, 5) * 0.001 - 30);  ///< MCUBrakeFeedPower
    vehicle_set_data(VEH_MCU_BRAKE_FEED, data);

    ///< MCUAliveCounter4 8bit@byte7 — 仅心跳
    return 0;
}

static int parse_can_msg_405_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_405 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rt_tick_t now = rt_tick_get();
    s_speed_mcu_tick = now;
    int data = (int)(LE_U16(buf, 6) * 0.01);  ///< Speed 16bit@byte6, km/h
    if(data > 180)  //超过最大车速150 20%以上不计入正常数据，若连续异常则超时清零
    {
        if(s_speed_mcu != 0)
        {
            s_speed_mcu = 0;
            s_over_speed_tick = now;
            vehicle_set_data(VEH_SPEED_CURRENT, 150);
        }
        else
        {
            rt_tick_t valid_ticks = rt_tick_from_millisecond(SPEED_SRC_VALID_MS);
            if ((now - s_over_speed_tick) > valid_ticks)
                vehicle_set_data(VEH_SPEED_CURRENT, 0);
        }
        return 0;
    }
    s_speed_mcu = 1;
    s_over_speed_tick = now;
    vehicle_set_data(VEH_SPEED_CURRENT, data);
    return 0;
}

///< ===========================================================================
///< BMS 报文 — 按电池包区分, 每个包一个独立 parser, 各自写入该包专属槽位.
///< 同一包的 7 条报文(BMS1/3/4/5/6/7/11)信号布局不同, parser 内按报文 ID 分发.
///< BMS6 DTC_Num_Level3: xlsx 标 16bit@StartBit56 但 8 字节帧仅余 byte7 共 8bit,
///< 属表格溢出瑕疵, 按 8bit 取低字节.
///< ===========================================================================

///< 1 号电池包 parser (ID 集: 0x301/0x303/0x304/0x305/0x306/0x307/0x30b)
static int parse_can_msg_bms_p1(uint32_t id, uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_bms_p1 id[0x%x] dlc[%d] err!\r\n", (unsigned int)id, size);
        return -1;
    }
    rx_timeout.ecu_bms_p1_rx_timeout = rt_tick_get();
    int data;

    switch (id) {
        case CAN_BMS1_P1_ID:  ///< BMS1 最高温度
            data = (int)LE_U8(buf, 2) - 40;
            vehicle_set_data(VEH_BAT_MAX_TEMP_P1, data);
            break;
        case CAN_BMS3_P1_ID:  ///< BMS3 电压/P+/SOH
            data = (int)(LE_U16(buf, 3) * 0.1);
            vehicle_set_data(VEH_BAT_VOLTAGE_P1, data);
            data = (int)(LE_U16(buf, 5) * 0.1);
            vehicle_set_data(VEH_BAT_P_VOLT_P1, data);
            data = LE_U8(buf, 7);
            vehicle_set_data(VEH_BAT_SOH_P1, data);
            break;
        case CAN_BMS7_P1_ID:  ///< BMS7 SOC
            data = LE_U8(buf, 4);
            vehicle_set_data(VEH_BAT_SOC_P1, data);
            break;
        case CAN_BMS11_P1_ID:  ///< BMS11 工作状态 (BMS_life@byte7 仅心跳)
            data = GET_DATA_BITS(buf[0], 2, 0x03);
            vehicle_set_data(VEH_BAT_WORK_STATUS_P1, data);
            break;
        case CAN_BMS4_P1_ID:  ///< BMS4/5/6 一/二/三级 DTC, 同包复用同一槽位
            data = LE_U8(buf, 7);
            break;
        case CAN_BMS5_P1_ID:
            data = LE_U8(buf, 7);
            rx_timeout.ecu_bms_p1_dtc_rx_timeout = rx_timeout.ecu_bms_p1_rx_timeout;
            dtc_code_update(DTC_SRC_BMS5_P1, data != 0);
            break;
        case CAN_BMS6_P1_ID:
            data = LE_U16(buf, 6);
            vehicle_set_data(VEH_BAT_DTC_P1, data);
            rx_timeout.ecu_bms_p1_dtc_rx_timeout = rx_timeout.ecu_bms_p1_rx_timeout;
            dtc_code_update(DTC_SRC_BMS6_P1, data != 0);
            break;
        default:
            break;
    }
    return 0;
}

///< 2 号电池包 parser (ID 集: 0x311/0x313/0x314/0x315/0x316/0x317/0x31b)
static int parse_can_msg_bms_p2(uint32_t id, uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_bms_p2 id[0x%x] dlc[%d] err!\r\n", (unsigned int)id, size);
        return -1;
    }
    rx_timeout.ecu_bms_p2_rx_timeout = rt_tick_get();
    int data;

    switch (id) {
        case CAN_BMS1_P2_ID:
            data = (int)LE_U8(buf, 2) - 40;
            vehicle_set_data(VEH_BAT_MAX_TEMP_P2, data);
            break;
        case CAN_BMS3_P2_ID:
            data = (int)(LE_U16(buf, 3) * 0.1);
            vehicle_set_data(VEH_BAT_VOLTAGE_P2, data);
            data = (int)(LE_U16(buf, 5) * 0.1);
            vehicle_set_data(VEH_BAT_P_VOLT_P2, data);
            data = LE_U8(buf, 7);
            vehicle_set_data(VEH_BAT_SOH_P2, data);
            break;
        case CAN_BMS7_P2_ID:
            data = LE_U8(buf, 4);
            vehicle_set_data(VEH_BAT_SOC_P2, data);
            break;
        case CAN_BMS11_P2_ID:
            data = GET_DATA_BITS(buf[0], 2, 0x03);
            vehicle_set_data(VEH_BAT_WORK_STATUS_P2, data);
            break;
        case CAN_BMS4_P2_ID:
            data = LE_U8(buf, 7);
            break;
        case CAN_BMS5_P2_ID:
            data = LE_U8(buf, 7);
            rx_timeout.ecu_bms_p2_dtc_rx_timeout = rx_timeout.ecu_bms_p2_rx_timeout;
            dtc_code_update(DTC_SRC_BMS5_P2, data != 0);
            break;
        case CAN_BMS6_P2_ID:
            data = LE_U16(buf, 6);
            vehicle_set_data(VEH_BAT_DTC_P2, data);
            rx_timeout.ecu_bms_p2_dtc_rx_timeout = rx_timeout.ecu_bms_p2_rx_timeout;
            dtc_code_update(DTC_SRC_BMS6_P2, data != 0);
            break;
        default:
            break;
    }
    return 0;
}

///< 3 号电池包 parser (ID 集: 0x321/0x323/0x324/0x325/0x326/0x327/0x32b)
static int parse_can_msg_bms_p3(uint32_t id, uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_bms_p3 id[0x%x] dlc[%d] err!\r\n", (unsigned int)id, size);
        return -1;
    }
    rx_timeout.ecu_bms_p3_rx_timeout = rt_tick_get();
    int data;

    switch (id) {
        case CAN_BMS1_P3_ID:
            data = (int)LE_U8(buf, 2) - 40;
            vehicle_set_data(VEH_BAT_MAX_TEMP_P3, data);
            break;
        case CAN_BMS3_P3_ID:
            data = (int)(LE_U16(buf, 3) * 0.1);
            vehicle_set_data(VEH_BAT_VOLTAGE_P3, data);
            data = (int)(LE_U16(buf, 5) * 0.1);
            vehicle_set_data(VEH_BAT_P_VOLT_P3, data);
            data = LE_U8(buf, 7);
            vehicle_set_data(VEH_BAT_SOH_P3, data);
            break;
        case CAN_BMS7_P3_ID:
            data = LE_U8(buf, 4);
            vehicle_set_data(VEH_BAT_SOC_P3, data);
            break;
        case CAN_BMS11_P3_ID:
            data = GET_DATA_BITS(buf[0], 2, 0x03);
            vehicle_set_data(VEH_BAT_WORK_STATUS_P3, data);
            break;
        case CAN_BMS4_P3_ID:
            data = LE_U8(buf, 7);
            break;
        case CAN_BMS5_P3_ID:
            data = LE_U8(buf, 7);
            rx_timeout.ecu_bms_p3_dtc_rx_timeout = rx_timeout.ecu_bms_p3_rx_timeout;
            dtc_code_update(DTC_SRC_BMS5_P3, data != 0);
            break;
        case CAN_BMS6_P3_ID:
            data = LE_U16(buf, 6);
            vehicle_set_data(VEH_BAT_DTC_P3, data);
            rx_timeout.ecu_bms_p3_dtc_rx_timeout = rx_timeout.ecu_bms_p3_rx_timeout;
            dtc_code_update(DTC_SRC_BMS6_P3, data != 0);
            break;
        default:
            break;
    }
    return 0;
}

static int parse_can_msg_a3_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_a3 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_abs_rx_timeout = rt_tick_get();
    int data;

    data = GET_DATA_BITS(buf[3], 3, 0x01);  ///< ABS_ABSAlarm 1bit@byte3 bit3
    vehicle_set_data(VEH_LIGHT_ABS, data);
    return 0;
}

static int parse_can_msg_a4_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_a4 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_abs_rx_timeout = rt_tick_get();
    rx_timeout.ecu_tcs_rx_timeout = rx_timeout.ecu_abs_rx_timeout;

    int data = LE_U8(buf, 2);  ///< TCS_Control
    vehicle_set_data(VEH_TCS_CONTROL, data);

    int index = LE_U8(buf, 6);  ///< TCS_WorkStatus
    vehicle_set_data(VEH_TCS_WORKSTATUS, index);

    if(index == 0 && data == 0)
        vehicle_set_data(VEH_TCS_WARNING, 0);
    else if(index == 0 && data == 1)
        vehicle_set_data(VEH_TCS_WARNING, 2);
    else
        vehicle_set_data(VEH_TCS_WARNING, (index == 1 || index == 2));

    return 0;
}

static int parse_can_msg_a5_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_a5 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_abs_rx_timeout = rt_tick_get();
    rx_timeout.ecu_abs_a5_rx_timeout = rx_timeout.ecu_abs_rx_timeout;
    int data = LE_U8(buf, 7);  ///< ABS_FPC_DTC
    dtc_code_update(DTC_SRC_A5, data != 0);
    return 0;
}

static int parse_can_msg_507_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_507 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_tpms_rx_timeout = rt_tick_get();
    int data = GET_DATA_BITS(buf[0], 0, 0x07);  ///< 胎压匹配状态码 3bit@byte0
    vehicle_set_data(VEH_TPMS_MATCH_STATE, data);
    return 0;
}

///< SwitchLeft/Right: xlsx StartByte 列(如 2.1) 才是真实字节位置(StartBit 列误填0)
///< 字节序: byte = (StartByte 整数部分) - 1, StartByte 小数部分为字节内位号(1-based)
///< 0x501 仅落"已有槽位"的信号(转向/喇叭/滚轮/滚轮方向与长度)及新增摇杆槽位;
///< HighBeam_Btn(1:近光 2:远光) 与 Cruise/Reverse(1:巡航短按 2:倒车长按) 的按键值域
///< 与既有槽位语义(灯状态 0/1) 不一致, 按约定只解析不存储, 待 UI 侧明确用法后再落槽位
static int parse_can_msg_501_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_501 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_switch_l_rx_timeout = rt_tick_get();
    int data;
    uint8_t byte6, byte7;

    ///< Live_code@byte0 — 仅心跳

    data = LE_U8(buf, 1);  ///< TurnRight_Btn 0:空闲 1:触发
    // vehicle_set_data(VEH_INDICATOR_TURN_RIGHT, data);

    data = LE_U8(buf, 2);  ///< TurnLeft_Btn 0:空闲 1:触发
    // vehicle_set_data(VEH_INDICATOR_TURN_LEFT, data);

    ///< HighBeam_Btn@byte3 0:空闲 1:近光 2:远光 — 按键值, 不落 VEH_LIGHT_*_BEAM 灯状态槽位
    data = LE_U8(buf, 3);

    data = LE_U8(buf, 4);  ///< Horn_Btn 0:空闲 1:触发
    // vehicle_set_data(VEH_HORN, data);

    ///< Cruise/Reverse@byte5 0:空闲 1:定速巡航(短按) 2:倒车(长按) — 按键值, 不落槽位
    data = LE_U8(buf, 5);

    byte6 = LE_U8(buf, 6);
    data = GET_DATA_BITS(byte6, 0, 0x01);  ///< Wheel_Btn@byte6 bit0 0:空闲 1:触发
    vehicle_set_data(VEH_BTN_WHEEL, data);

    data = GET_DATA_BITS(byte6, 1, 0x01);  ///< Rocker_Left@byte6 bit1 0:空闲 1:触发
    vehicle_set_data(VEH_ROCKER_LEFT, data);

    data = GET_DATA_BITS(byte6, 2, 0x01);  ///< Rocker_Right@byte6 bit2 0:空闲 1:触发
    vehicle_set_data(VEH_ROCKER_RIGHT, data);

    byte7 = LE_U8(buf, 7);
    data = GET_DATA_BITS(byte7, 0, 0x03);  ///< Roller_Dire@byte7 bit0-1 0:空闲 1:下滑 2:上滑
    vehicle_set_data(VEH_ROLLER_DIRE, data);

    data = GET_DATA_BITS(byte7, 2, 0x03);  ///< Roller_Len@byte7 bit2-3 0:空闲 1:一页 2:两页
    vehicle_set_data(VEH_ROLLER_LEN, data);

#ifdef CAN_RX_DEBUG
    rt_kprintf("can_501: TurnR[%d] TurnL[%d] HighBeam[%d] Horn[%d] Cruise/Rev[%d] "
               "Wheel[%d] RockerL[%d] RockerR[%d] RollerDire[%d] RollerLen[%d]\r\n",
               LE_U8(buf, 1), LE_U8(buf, 2), high_beam_btn, LE_U8(buf, 4), cruise_reverse,
               GET_DATA_BITS(byte6, 0, 0x01), GET_DATA_BITS(byte6, 1, 0x01),
               GET_DATA_BITS(byte6, 2, 0x01), GET_DATA_BITS(byte7, 0, 0x03),
               GET_DATA_BITS(byte7, 2, 0x03));
#endif
    return 0;
}

static int parse_can_msg_502_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_502 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_switch_r_rx_timeout = rt_tick_get();

    ///< Live_code@byte0 — 仅心跳
    ///< 新表 0x502 仅剩 4 个按键信号, 且均为按键值语义(0:空闲 1/2:触发动作),
    ///< 与既有槽位(VEH_BTN_*/VEH_LIGHT_GB/VEH_IGNITION_STATUS, 0/1 状态语义)不一致;
    ///< ReverseGear_Btn/Wheel_Btn/Roller_* 在新表中已移入 0x501。
    ///< 按约定本条报文只解析不存储, 故无槽位写入, 超时清零相应为空。
    int double_flash_btn = LE_U8(buf, 1);  ///< DoubleFlash_Btn 0:空闲 1:触发
    int mode_btn         = LE_U8(buf, 2);  ///< Mode_Btn 0:空闲 1:触发
    int parking_btn      = LE_U8(buf, 3);  ///< Parking_Btn 0:空闲 1:解P(短按) 2:驻车(长按)
    int power_btn        = LE_U8(buf, 4);  ///< Power_Btn 0:空闲 1:电源键 2:开坐垫锁(短按两次)
    (void)double_flash_btn;
    (void)mode_btn;
    (void)parking_btn;
    (void)power_btn;

#ifdef CAN_RX_DEBUG
    rt_kprintf("can_502: DoubleFlash[%d] Mode[%d] Parking[%d] Power[%d]\r\n",
               double_flash_btn, mode_btn, parking_btn, power_btn);
#endif
    return 0;
}

static int parse_can_msg_430_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_430 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_charger_rx_timeout = rt_tick_get();
    rx_timeout.ecu_charger_lamp_rx_timeout = rx_timeout.ecu_charger_rx_timeout;
    int data;

    ///< p_charger_heartBeat@byte0 — 仅心跳

    data = GET_DATA_BITS(buf[1], 0, 0x03);  ///< p_charger_status 2bit@byte1
    vehicle_set_data(VEH_CHARGER_STATUS, data);

    data = (int)(LE_U16(buf, 2) * 0.1);  ///< p_ChargerCurrent A
    vehicle_set_data(VEH_CHARGER_CURRENT, data);

    data = (int)(LE_U16(buf, 4) * 0.1);  ///< p_ChargerVlot V
    vehicle_set_data(VEH_CHARGER_VOLTAGE, data);

    data = LE_U8(buf, 6);  ///< DTC_Code
    vehicle_set_data(VEH_CHARGER_DTC, data);
    dtc_code_update(DTC_SRC_430, data != 0);
    return 0;
}

///< ===========================================================================
///< 里程/清零指令 (0x776/0x777/0x778/0x779, RX)
///< 大端字节序: 各字段数据在 buf 中按高字节在前排列, 与 0xNNN 整车信号的小端相反
///< 公共约定: DLC>=8, 第 8 字节 (buf[7]) 为前 7 字节的 J1850 CRC-8;
///<           连续 3 帧完全相同才执行写入/清零动作 (防抖动)
///< ===========================================================================

///< 0x776 设置小计里程A: buf[0..3] = TripA(m), 大端
static int parse_can_msg_776_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_SUCCESS) {
        return 0;
    }

    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_776 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }

    uint8_t crc_chceck;
    static uint8_t rx_cnt = 0;
    static uint8_t rx_buf[8] = {0};

    crc_chceck = can_j1850_crc8(buf, 7);
    if (crc_chceck != buf[7]) {
        rt_log_error("can_ecu_776 Error checksum = 0x%x, buf[7] = 0x%x !\r\n", \
                    crc_chceck, buf[7]);
        return -1;
    }

    if (rx_cnt == 0) {
        memcpy(rx_buf, buf, 8);
        rx_cnt++;
    } else if (rx_cnt == 1) {
        if (memcmp(rx_buf, buf, 8) == 0) {
            rx_cnt++;
        } else {
            memcpy(rx_buf, buf, 8);
            rx_cnt = 1;
        }
    } else if (rx_cnt == 2) {
        if (memcmp(rx_buf, buf, 8) == 0) {
            rx_cnt++;
        } else {
            memcpy(rx_buf, buf, 8);
            rx_cnt = 1;
        }
    }

    if (rx_cnt == 3) {
        rx_cnt = 0;
        uint32_t trip_a;
        uint32_t tmp = 0;

        if (!get_mile_param(MILE_PARAM_TRIP, &tmp)) {
            rt_log_error("Get MILE_PARAM_TRIP error!\r\n");
            return -1;
        }

        trip_a = (((uint32_t) buf[0]) << 24 ) |  (((uint32_t) buf[1]) << 16 ) | \
                (((uint32_t) buf[2]) << 8 ) |(((uint32_t) buf[3]) << 0);
        if (trip_a != tmp) {
            set_mile_param(MILE_PARAM_TRIP, &trip_a);
            vehicle_set_data(VEH_MILEAGE_CHANGE_MSG, 1);
        }
    }

    return 0;
}

///< 0x777 设置小计里程B: buf[0..3] = TripB(m), 大端
///< 本平台不支持修改 Trip B, 仅打印提示
static int parse_can_msg_777_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_SUCCESS) {
        return 0;
    }

    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_777 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }

    uint8_t crc_chceck;
    static uint8_t rx_cnt = 0;
    static uint8_t rx_buf[8] = {0};

    crc_chceck = can_j1850_crc8(buf, 7);
    if (crc_chceck != buf[7]) {
        rt_log_error("can_ecu_777 Error checksum = 0x%x, buf[7] = 0x%x !\r\n", \
                    crc_chceck, buf[7]);
        return -1;
    }

    /* 0x777 命令解析 (单帧即可触发, 不走去抖):
     *   TripB == 111: 拉起看门狗线程. MW_WDT_ENABLE=0 时上电不拉起,
     *                 靠这笔报文启动; MW_WDT_ENABLE=1 时幂等返回.
     *   TripB == 753: 测试后门, 主动访问未映射地址触发 store access fault. */
    {
        uint32_t trip_b_cmd = (((uint32_t) buf[0]) << 24 ) |  (((uint32_t) buf[1]) << 16 ) | \
                              (((uint32_t) buf[2]) << 8 ) |(((uint32_t) buf[3]) << 0);

        if (trip_b_cmd == 111) {
            rt_kprintf("CAN 0x777 trip=111, start watchdog\n");
            mw_wdt_init();
        }

#if CAN_777_TEST_TRIGGER_ENABLE
        /* D13x 有效区域只有 0x3000_0000~0x3013_FFFF、0x4000_0000~0x43FF_FFFF、
         * 0x6000_0000~0x7FFF_FFFF 及外设段, 0xDEAD_0000 无映射.
         * trap_c() 打印异常现场后 while(1) 死循环 (未开 AIC_BACKTRACE_DEBUG),
         * 喂狗线程停止, 15s 后看门狗复位.
         * 仅用于验证看门狗, 量产/验证完成后把 CAN_777_TEST_TRIGGER_ENABLE 置 0. */
        if (trip_b_cmd == 753) {
            rt_kprintf("CAN 0x777 trip=753, trigger access fault\n");
            *(volatile uint32_t *)0xDEAD0000 = 0x5A5A5A5A;
        }
#endif
    }

    if (rx_cnt == 0) {
        memcpy(rx_buf, buf, 8);
        rx_cnt++;
    } else if (rx_cnt == 1) {
        if (memcmp(rx_buf, buf, 8) == 0) {
            rx_cnt++;
        } else {
            memcpy(rx_buf, buf, 8);
            rx_cnt = 1;
        }
    } else if (rx_cnt == 2) {
        if (memcmp(rx_buf, buf, 8) == 0) {
            rx_cnt++;
        } else {
            memcpy(rx_buf, buf, 8);
            rx_cnt = 1;
        }
    }

    if (rx_cnt == 3) {
        rx_cnt = 0;
        uint32_t trip_b;
        uint32_t tmp = 0;

        trip_b = (((uint32_t) buf[0]) << 24 ) |  (((uint32_t) buf[1]) << 16 ) | \
                (((uint32_t) buf[2]) << 8 ) |(((uint32_t) buf[3]) << 0);
        if (trip_b != tmp) {
            rt_log_info("Platform not supported change trip B\n");
        }
    }

    return 0;
}

///< 0x778 设置总里程: buf[0..3] = ODO(m), 大端
static int parse_can_msg_778_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_SUCCESS) {
        return 0;
    }

    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_778 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }

    uint8_t crc_chceck;
    static uint8_t rx_cnt = 0;
    static uint8_t rx_buf[8] = {0};

    crc_chceck = can_j1850_crc8(buf, 7);
    if (crc_chceck != buf[7]) {
        rt_log_error("can_ecu_778 Error checksum = 0x%x, buf[7] = 0x%x !\r\n", \
                    crc_chceck, buf[7]);
        return -1;
    }

    if (rx_cnt == 0) {
        memcpy(rx_buf, buf, 8);
        rx_cnt++;
    } else if (rx_cnt == 1) {
        if (memcmp(rx_buf, buf, 8) == 0) {
            rx_cnt++;
        } else {
            memcpy(rx_buf, buf, 8);
            rx_cnt = 1;
        }
    } else if (rx_cnt == 2) {
        if (memcmp(rx_buf, buf, 8) == 0) {
            rx_cnt++;
        } else {
            memcpy(rx_buf, buf, 8);
            rx_cnt = 1;
        }
    }

    if (rx_cnt == 3) {
        rx_cnt = 0;
        uint32_t odo;
        uint32_t tmp = 0;

        if (!get_mile_param(MILE_PARAM_ODO, &tmp)) {
            rt_log_error("Get MILE_PARAM_ODO error!\r\n");
            return -1;
        }

        odo = (((uint32_t) buf[0]) << 24 ) |  (((uint32_t) buf[1]) << 16 ) | \
                (((uint32_t) buf[2]) << 8 ) |(((uint32_t) buf[3]) << 0);
        if (odo != tmp) {
            set_mile_param(MILE_PARAM_ODO, &odo);
            vehicle_set_data(VEH_MILEAGE_CHANGE_MSG, 3);
        }
    }

    return 0;
}

///< 0x779 清零整车 EEPROM: buf[0..6] = 特定口令序列 (0xa1 0xb1 ... 0xee)
static int parse_can_msg_779_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_SUCCESS) {
        return 0;
    }

    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_779 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }

    uint8_t crc_chceck;
    static uint8_t rx_cnt = 0;
    static uint8_t rx_buf[8] = {0};

    crc_chceck = can_j1850_crc8(buf, 7);
    if (crc_chceck != buf[7]) {
        rt_log_error("can_ecu_779 Error checksum = 0x%x, buf[7] = 0x%x !\r\n", \
                    crc_chceck, buf[7]);
        return -1;
    }

    if (rx_cnt == 0) {
        memcpy(rx_buf, buf, 8);
        rx_cnt++;
    } else if (rx_cnt == 1) {
        if (memcmp(rx_buf, buf, 8) == 0) {
            rx_cnt++;
        } else {
            memcpy(rx_buf, buf, 8);
            rx_cnt = 1;
        }
    } else if (rx_cnt == 2) {
        if (memcmp(rx_buf, buf, 8) == 0) {
            rx_cnt++;
        } else {
            memcpy(rx_buf, buf, 8);
            rx_cnt = 1;
        }
    }

    if (rx_cnt == 3) {
        if ((buf[0] == 0xa1) && (buf[1] == 0xb1)
            && (buf[2] == 0xc1) && (buf[3] == 0xd1)
            && (buf[4] == 0xe1) && (buf[5] == 0xf1)
            && (buf[6] == 0xee)) {
            rt_log_info("start clear e2prom...\n");
            extern void clean_eeprom_operate(void);
            clean_eeprom_operate();
            extern void clear_e2mprom(void);
            clear_e2mprom();
        }
    }

    return 0;
}

static void can_msg_process(struct rt_can_msg * msg) {
    if (msg == NULL) {
        return;
    }

#ifdef CAN_RX_DEBUG
    hex_config_data_print(__FUNCTION__, ":recv(0x)", msg->data, msg->len);
#endif

    switch (msg->id) {
        ///< 整车CAN网络信号 (xlsx)
        case CAN_NFC_DTC_ID:
            parse_can_msg_93_msg(msg->data, msg->len);
            break;
        case CAN_NFC_MATCH_ACK_ID:
            parse_can_msg_80_msg(msg->data, msg->len);
            break;
        case CAN_VCU_DISCHARGER_ID:
            parse_can_msg_102_msg(msg->data, msg->len);
            break;
        case CAN_VCU_BROADCAST2_ID:
            parse_can_msg_108_msg(msg->data, msg->len);
            break;
        case CAN_VCU_METER1_ID:
            parse_can_msg_10a_msg(msg->data, msg->len);
            break;
        case CAN_VCU_BROADCAST4_ID:
            parse_can_msg_10d_msg(msg->data, msg->len);
            break;
        case CAN_MCU_401_ID:
            parse_can_msg_401_msg(msg->data, msg->len);
            break;
        case CAN_MCU_403_ID:
            parse_can_msg_403_msg(msg->data, msg->len);
            break;
        case CAN_MCU_404_ID:
            parse_can_msg_404_msg(msg->data, msg->len);
            break;
        case CAN_MCU_405_ID:
            parse_can_msg_405_msg(msg->data, msg->len);
            break;

        case CAN_BMS1_P1_ID:
        case CAN_BMS3_P1_ID:
        case CAN_BMS4_P1_ID:
        case CAN_BMS5_P1_ID:
        case CAN_BMS6_P1_ID:
        case CAN_BMS7_P1_ID:
        case CAN_BMS11_P1_ID:
            parse_can_msg_bms_p1(msg->id, msg->data, msg->len);
            break;
        case CAN_BMS1_P2_ID:
        case CAN_BMS3_P2_ID:
        case CAN_BMS4_P2_ID:
        case CAN_BMS5_P2_ID:
        case CAN_BMS6_P2_ID:
        case CAN_BMS7_P2_ID:
        case CAN_BMS11_P2_ID:
            parse_can_msg_bms_p2(msg->id, msg->data, msg->len);
            break;
        case CAN_BMS1_P3_ID:
        case CAN_BMS3_P3_ID:
        case CAN_BMS4_P3_ID:
        case CAN_BMS5_P3_ID:
        case CAN_BMS6_P3_ID:
        case CAN_BMS7_P3_ID:
        case CAN_BMS11_P3_ID:
            parse_can_msg_bms_p3(msg->id, msg->data, msg->len);
            break;

        case CAN_ABS1_ID:
            parse_can_msg_a3_msg(msg->data, msg->len);
            break;
        case CAN_TCS_ID:
            parse_can_msg_a4_msg(msg->data, msg->len);
            break;
        case CAN_ABS2_ID:
            parse_can_msg_a5_msg(msg->data, msg->len);
            break;
        case CAN_TPMS_METER2_ID:
            parse_can_msg_507_msg(msg->data, msg->len);
            break;
        case CAN_SWITCH_LEFT_ID:
            parse_can_msg_501_msg(msg->data, msg->len);
            break;
        case CAN_SWITCH_RIGHT_ID:
            parse_can_msg_502_msg(msg->data, msg->len);
            break;
        case CAN_P_CHARGER1_ID:
            parse_can_msg_430_msg(msg->data, msg->len);
            break;

        case CAN_SET_TRIP_A:
            parse_can_msg_776_msg(msg->data, msg->len);
            break;
        case CAN_SET_TRIP_B:
            parse_can_msg_777_msg(msg->data, msg->len);
            break;
        case CAN_SET_ODO:
            parse_can_msg_778_msg(msg->data, msg->len);
            break;
        case CAN_CLEAN_EEPROM:
            parse_can_msg_779_msg(msg->data, msg->len);
            break;

        default:
            break;
    }
}

static void can_rx_thread(void *param) {
    rt_size_t size;
    struct rt_can_msg rxmsg = {0};

    rt_kprintf("Can rx thread success!.\n");

    while (1) {
        rt_sem_take(&can_rx_sem, RT_WAITING_FOREVER);
        rxmsg.hdr = -1;
        size = rt_device_read(can_dev, 0, &rxmsg, sizeof(rxmsg));
        if (!size) {
            // rt_kprintf("CAN read error\n");
            continue;
        }
        can_msg_process(&rxmsg);
    }
}

static void can_timeout_thread(void *param) {
    while (1) {
        /* CAN 业务心跳: 不依赖总线报文, 只证明该线程仍在周期运行.
         * 放在动画门控之外, 开机动画期间也能保持有效. */
        mw_wdt_feed(MW_WDT_HB_CAN);

        /* 动画期间不做超时清零, 否则因为没有真实 CAN 输入, vehicle data 会被
         * 一直清成 0, 与 can_rx_callback 抑制保持一致. 动画结束后恢复. */
        if (get_boot_animation_status() == ANIMATION_STATE_END) {
            timeout_clear_can_data();
        }
        rt_thread_mdelay(100);
    }
}

static void can_timeout_enable(void) {
    g_can_timeout_tid = rt_thread_create("can_timeout", can_timeout_thread, RT_NULL,
                                  1024, CAN_RX_THREAD_PRIO + 1, 10);
    if (g_can_timeout_tid != RT_NULL) {
        rt_thread_startup(g_can_timeout_tid);
    } else {
        rt_kprintf("create can_timeout thread failed!\n");
        return;
    }
}

static void can_rx_enable(void) {
    /* 先 init sem, 再 set rx_indicate, 最后 enable 中断: 避免中断在 sem 初始化
     * 前就 release can_rx_sem(未初始化的 sem). */
    rt_sem_init(&can_rx_sem, "usr_rx_sem", 0, RT_IPC_FLAG_PRIO);
    g_can_rx_sem_inited = RT_TRUE;
    rt_device_set_rx_indicate(can_dev, can_rx_callback);
    rt_device_control(can_dev, RT_DEVICE_CTRL_SET_INT, NULL);

    g_can_rx_tid = rt_thread_create("can_rx", can_rx_thread, RT_NULL,
                                  4096, CAN_RX_THREAD_PRIO, 10);
    if (g_can_rx_tid != RT_NULL) {
        rt_thread_startup(g_can_rx_tid);
    } else {
        rt_kprintf("create can_rx thread failed!\n");
        return;
    }
}

int mw_can_init(void) {
    rt_err_t ret = 0;

    ///< can stan by
    g_can_stb_pin = rt_pin_get(MW_CAN_STB_PIN);
    rt_pin_mode(g_can_stb_pin, PIN_MODE_OUTPUT);
    rt_pin_write(g_can_stb_pin, PIN_LOW);

    can_dev = rt_device_find(MW_CAN_DEVICE_NAME);
    if (!can_dev) {
        rt_kprintf("Can not find %s dev\n", MW_CAN_DEVICE_NAME);
        return -1;
    }

    ret = rt_device_open(can_dev,
                        RT_DEVICE_FLAG_INT_TX | RT_DEVICE_FLAG_INT_RX);
    if (ret) {
        rt_kprintf("%s open failed!\n", MW_CAN_DEVICE_NAME);
        return -1;
    }

    ///< 设置为500kbps波特率
    ret = rt_device_control(can_dev, RT_CAN_CMD_SET_BAUD, (void *)CAN500kBaud);
    if (ret) {
        rt_kprintf("set %s baud failed\n", MW_CAN_DEVICE_NAME);
        ret = -1;
        rt_device_close(can_dev);
        goto __exit;
    }

    can_rx_enable();
    can_timeout_enable();
    can_tx_init(can_dev);

    rt_kprintf("can init success!\n");

__exit:
    return ret;
}

/* 休眠前清理 CAN 全部资源:
 *   1. 先关 RX 中断 + close 设备, 阻断后续中断 / sem release
 *   2. delete RX/timeout 线程 (RX 线程阻塞在 rt_sem_take, delete 安全)
 *   3. detach can_rx_sem
 *   4. 关 TX 通道 (can_tx_sleep 删 send/cycle 线程 + tx_mq)
 *   5. STB pin 拉高让收发器进 standby (PE.15 = 0)
 * 唤醒走 rt_hw_cpu_reset 复位, 重启后 mw_can_init 会重新拉起整套资源.
 */
void mw_can_sleep(void) {
    /* 1. 先摘 rx_indicate, 阻断新的 sem release (不在此 close, 见步骤 3) */
    if (can_dev) {
        rt_device_set_rx_indicate(can_dev, RT_NULL);
    }

    /* 2. delete RX/timeout 线程 — RX 线程阻在 sem_take 上, timeout 阻在 mdelay 上,
     *    都没有持锁, delete 安全 */
    if (g_can_rx_tid != RT_NULL) {
        rt_thread_delete(g_can_rx_tid);
        g_can_rx_tid = RT_NULL;
    }
    if (g_can_timeout_tid != RT_NULL) {
        rt_thread_delete(g_can_timeout_tid);
        g_can_timeout_tid = RT_NULL;
    }

    /* 3. 再 close 设备 (此时 RX 线程已删, 不会读已关闭设备 -> 不会读到 0) */
    if (can_dev) {
        rt_device_close(can_dev);
    }

    /* 4. detach RX sem (init 路径用的是 rt_sem_init, 不是 create) */
    if (g_can_rx_sem_inited) {
        rt_sem_detach(&can_rx_sem);
        g_can_rx_sem_inited = RT_FALSE;
    }

    /* 5. TX 通道: send/cycle 线程 + tx_mq */
    can_tx_sleep();

    /* 6. STB pin 拉高让 CAN 收发器进 standby (典型功耗 mA → μA 级) */
    if (g_can_stb_pin != 0) {
        rt_pin_write(g_can_stb_pin, PIN_HIGH);
    }

    can_dev = RT_NULL;
    rt_kprintf("can sleep: rx/tx threads deleted, dev closed, STB low\n");
}

/* CAN 轻休眠: 为"CAN 总线唤醒"准备的版本, 与 mw_can_sleep() 的区别是
 * **不关设备**——rt_device_close() 会走 RT_DEVICE_CTRL_CLR_INT,
 * 最终 hal_can_disable_interrupt() 把 CAN_INTEN 写 0, 唤醒中断也被关掉。
 * 这里只停业务线程, 保留设备打开 + CAN0_IRQn 使能, 然后把控制器切到
 * Sleep Mode 并只留 WAKEUP 中断, 等总线活动把它唤醒。
 *
 * 唤醒后走 rt_hw_cpu_reset() 重启, 不需要在此还原任何东西。 */
void mw_can_light_sleep(void) {
    u32 base = CAN0_BASE;
    u32 mode;

    /* 1. 摘 rx_indicate, 阻断后续 sem release */
    if (can_dev) {
        rt_device_set_rx_indicate(can_dev, RT_NULL);
    }

    /* 2. 删 RX/timeout 线程 (都阻塞在 sem / mdelay 上, delete 安全) */
    if (g_can_rx_tid != RT_NULL) {
        rt_thread_delete(g_can_rx_tid);
        g_can_rx_tid = RT_NULL;
    }
    if (g_can_timeout_tid != RT_NULL) {
        rt_thread_delete(g_can_timeout_tid);
        g_can_timeout_tid = RT_NULL;
    }

    /* 3. detach RX sem */
    if (g_can_rx_sem_inited) {
        rt_sem_detach(&can_rx_sem);
        g_can_rx_sem_inited = RT_FALSE;
    }

    /* 4. 停 TX 通道 (cycle/send/tcs 线程 + tx_mq) */
    can_tx_sleep();

    /* 5. 标记为唤醒源: 否则 PM 框架的 aic_can_suspend() 会
     *    hal_clk_disable(CLK_CAN0), 控制器没时钟就收不到总线活动 */
    if (can_dev) {
        rt_device_wakeup_enable(can_dev, RT_TRUE);
    }

    /* 6. 进休眠前先"清场": 中止挂起的发送 + 复位控制器清错误状态.
     *    台架上 D133 是总线上唯一节点时, 周期报文无人 ACK 会把发送错误
     *    计数 TEC 顶到 128 进入 error-passive; 带着挂起发送/错误状态进休眠,
     *    控制器会立刻被唤醒(实测进休眠后 0ms 就被叫醒)。
     *    can_tx_sleep() 只能删线程, 删不掉已经写进控制器的 TXREQ。 */
    {
        u32 tec_before = readl(base + CAN_TXERR_REG) & 0xFF;
        u32 stat_before = readl(base + CAN_STAT_REG);

        writel(CAN_MCR_ABORTREQ, base + CAN_MCR_REG);   /* 中止发送 */
        rt_thread_mdelay(2);

        writel(readl(base + CAN_MODE_REG) | CAN_MODE_RST, base + CAN_MODE_REG);
        rt_thread_mdelay(2);
        writel(readl(base + CAN_MODE_REG) & ~CAN_MODE_RST, base + CAN_MODE_REG);
        rt_thread_mdelay(2);

        rt_kprintf("can light sleep: clear state TEC %u->%u STAT 0x%08x->0x%08x\n",
                   (unsigned)tec_before,
                   (unsigned)(readl(base + CAN_TXERR_REG) & 0xFF),
                   (unsigned)stat_before,
                   (unsigned)readl(base + CAN_STAT_REG));
    }

    /* 7. 中断掩码收窄到只剩唤醒位. 驱动 hal_can_enable_interrupt() 写的是
     *    0xFF, 错误中断(ERRB/ERRP/ERRW/OVF)在休眠期间会被总线噪声触发,
     *    造成误唤醒; 而 INTEN 同时门控 INTR 标志, 不开 WAKEUP 位就看不到唤醒标志 */
    writel(CAN_INTEN_WAKEUP, base + CAN_INTEN_REG);
    writel(0xFF, base + CAN_INTR_REG);          /* 写1清全部标志 */

    /* 8. 控制器进 Sleep Mode. 直接写即可, 不需要先复位模式
     *    (已实测: c1 直接写生效, 只是繁忙总线上会被立刻唤醒) */
    mode = readl(base + CAN_MODE_REG);
    writel((mode & ~CAN_MODE_MASK) | CAN_MODE_SLEEP, base + CAN_MODE_REG);

    /* 9. STB pin 拉高让 CAN 收发器进 standby */
    if (g_can_stb_pin != 0) {
        rt_pin_write(g_can_stb_pin, PIN_HIGH);
    }

    rt_kprintf("can light sleep: threads stopped, dev kept open, "
               "controller in sleep mode (INTEN=WAKEUP)\n");
}

#endif