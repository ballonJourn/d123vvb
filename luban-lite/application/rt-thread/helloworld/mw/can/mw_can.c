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
#include "mile_param.h"

// #define CAN_RX_DEBUG

#if MW_CAN_ENABLE

#define GET_DATA_BITS(data,offset,mask) ((data >> offset) & mask)
///< 位标志操作: status 非 0 将 data 的第 (code-1) 位置 1, 为 0 则清 0 (就地修改 data)
///<   code 取 pop_tip_code_e 枚举值 (1-based), 与 UI 侧 POP_TIP_BIT(code)=1<<(code-1) 一致
#define BIT_FLAG(data, code, status) \
    do { if (status) (data) |= (1u << ((code) - 1)); else (data) &= ~(1u << ((code) - 1)); } while (0)
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

static void timeout_clear_can_data(void) {
    uint32_t current_tick = rt_tick_get();

    ///< 整车CAN网络信号超时清零 — 按 ECU 分组, 仅清该 ECU 独占或主要提供的槽位.
    ///< VEH_SPEED_CURRENT 由 MCU(0x405) 与 ABS(0xA3) 共同提供, 仅当两者都超时才清零,
    ///  避免单源静默时误清另一源的速度.
    if ((current_tick - rx_timeout.ecu_vcu_rx_timeout) > rt_tick_from_millisecond(1000)) {
        if (vehicle_get_data(VEH_WORK_MODE) > 0) vehicle_set_data(VEH_WORK_MODE, 0);
        if (vehicle_get_data(VEH_VEHICLE_TYPE) > 0) vehicle_set_data(VEH_VEHICLE_TYPE, 0);
        if (vehicle_get_data(VEH_HEAT_HANDLE) > 0) vehicle_set_data(VEH_HEAT_HANDLE, 0);
        if (vehicle_get_data(VEH_SEAT_STATE) > 0) vehicle_set_data(VEH_SEAT_STATE, 0);
        if (vehicle_get_data(VEH_EMERGENCY_SHUTDOWN) > 0) vehicle_set_data(VEH_EMERGENCY_SHUTDOWN, 0);
        if (vehicle_get_data(VEH_DISCHARGE_BAT_NUM) > 0) vehicle_set_data(VEH_DISCHARGE_BAT_NUM, 0);
        ///< VCU 超时: 同步清座椅开锁弹窗位
        {
            uint32_t m = (uint32_t)vehicle_get_data(VEH_CAN_POPUP_MSG);
            BIT_FLAG(m, POP_TIP_SEAT_UNLOCK, 0);
            BIT_FLAG(m, POP_TIP_CRUISE_FAULT, 0);
            vehicle_set_data(VEH_CAN_POPUP_MSG, (int32_t)m);
        }
    }

    if ((current_tick - rx_timeout.ecu_mcu_rx_timeout) > rt_tick_from_millisecond(1000)) {
        ///< MCU 独占
        if (vehicle_get_data(VEH_MCU_DTC) > 0) vehicle_set_data(VEH_MCU_DTC, 0);
        if (vehicle_get_data(VEH_MCU_FAULT_LEVEL) > 0) vehicle_set_data(VEH_MCU_FAULT_LEVEL, 0);
        if (vehicle_get_data(VEH_MCU_SLIDE_FEED) != 0) vehicle_set_data(VEH_MCU_SLIDE_FEED, 0);
        if (vehicle_get_data(VEH_MCU_BRAKE_FEED) != 0) vehicle_set_data(VEH_MCU_BRAKE_FEED, 0);
        ///< MCU 主要提供 (挡位/驾驶模式/功率)
        if (vehicle_get_data(VEH_GEAR_POSITION) > 0) vehicle_set_data(VEH_GEAR_POSITION, 0);
        if (vehicle_get_data(VEH_DRIVE_MODE) > 0) vehicle_set_data(VEH_DRIVE_MODE, 0);
        if (vehicle_get_data(VEH_TRAM_POWR) != 0) vehicle_set_data(VEH_TRAM_POWR, 0);
    }

    if ((current_tick - rx_timeout.ecu_abs_rx_timeout) > rt_tick_from_millisecond(1000)) {
        ///< ABS 独占 (ABS 报警灯)
        if (vehicle_get_data(VEH_LIGHT_ABS) > 0) vehicle_set_data(VEH_LIGHT_ABS, 0);
    }

    ///< 车速: MCU(0x405) 与 ABS(0xA3) 双源, 两者都超时才清零
    if (((current_tick - rx_timeout.ecu_mcu_rx_timeout) > rt_tick_from_millisecond(1000)) &&
        ((current_tick - rx_timeout.ecu_abs_rx_timeout) > rt_tick_from_millisecond(1000))) {
        if (vehicle_get_data(VEH_SPEED_CURRENT) > 0) vehicle_set_data(VEH_SPEED_CURRENT, 0);
    }


    if ((current_tick - rx_timeout.ecu_bms_p1_rx_timeout) > rt_tick_from_millisecond(1000)) {
        if (vehicle_get_data(VEH_BAT_MAX_TEMP_P1) != 0) vehicle_set_data(VEH_BAT_MAX_TEMP_P1, 0);
        if (vehicle_get_data(VEH_BAT_VOLTAGE_P1) > 0) vehicle_set_data(VEH_BAT_VOLTAGE_P1, 0);
        if (vehicle_get_data(VEH_BAT_P_VOLT_P1) > 0) vehicle_set_data(VEH_BAT_P_VOLT_P1, 0);
        if (vehicle_get_data(VEH_BAT_SOH_P1) > 0) vehicle_set_data(VEH_BAT_SOH_P1, 0);
        if (vehicle_get_data(VEH_BAT_SOC_P1) > 0) vehicle_set_data(VEH_BAT_SOC_P1, 0);
        if (vehicle_get_data(VEH_BAT_WORK_STATUS_P1) > 0) vehicle_set_data(VEH_BAT_WORK_STATUS_P1, 0);
        if (vehicle_get_data(VEH_BAT_DTC_P1) > 0) vehicle_set_data(VEH_BAT_DTC_P1, 0);
        ///< BMS P1 超时: 同步清低电量弹窗位
        {
            uint32_t m = (uint32_t)vehicle_get_data(VEH_CAN_POPUP_MSG);
            BIT_FLAG(m, POP_TIP_BATT_LOW, 0);
            vehicle_set_data(VEH_CAN_POPUP_MSG, (int32_t)m);
        }
    }
    if ((current_tick - rx_timeout.ecu_bms_p2_rx_timeout) > rt_tick_from_millisecond(1000)) {
        if (vehicle_get_data(VEH_BAT_MAX_TEMP_P2) != 0) vehicle_set_data(VEH_BAT_MAX_TEMP_P2, 0);
        if (vehicle_get_data(VEH_BAT_VOLTAGE_P2) > 0) vehicle_set_data(VEH_BAT_VOLTAGE_P2, 0);
        if (vehicle_get_data(VEH_BAT_P_VOLT_P2) > 0) vehicle_set_data(VEH_BAT_P_VOLT_P2, 0);
        if (vehicle_get_data(VEH_BAT_SOH_P2) > 0) vehicle_set_data(VEH_BAT_SOH_P2, 0);
        if (vehicle_get_data(VEH_BAT_SOC_P2) > 0) vehicle_set_data(VEH_BAT_SOC_P2, 0);
        if (vehicle_get_data(VEH_BAT_WORK_STATUS_P2) > 0) vehicle_set_data(VEH_BAT_WORK_STATUS_P2, 0);
        if (vehicle_get_data(VEH_BAT_DTC_P2) > 0) vehicle_set_data(VEH_BAT_DTC_P2, 0);
    }
    if ((current_tick - rx_timeout.ecu_bms_p3_rx_timeout) > rt_tick_from_millisecond(1000)) {
        if (vehicle_get_data(VEH_BAT_MAX_TEMP_P3) != 0) vehicle_set_data(VEH_BAT_MAX_TEMP_P3, 0);
        if (vehicle_get_data(VEH_BAT_VOLTAGE_P3) > 0) vehicle_set_data(VEH_BAT_VOLTAGE_P3, 0);
        if (vehicle_get_data(VEH_BAT_P_VOLT_P3) > 0) vehicle_set_data(VEH_BAT_P_VOLT_P3, 0);
        if (vehicle_get_data(VEH_BAT_SOH_P3) > 0) vehicle_set_data(VEH_BAT_SOH_P3, 0);
        if (vehicle_get_data(VEH_BAT_SOC_P3) > 0) vehicle_set_data(VEH_BAT_SOC_P3, 0);
        if (vehicle_get_data(VEH_BAT_WORK_STATUS_P3) > 0) vehicle_set_data(VEH_BAT_WORK_STATUS_P3, 0);
        if (vehicle_get_data(VEH_BAT_DTC_P3) > 0) vehicle_set_data(VEH_BAT_DTC_P3, 0);
    }

    if ((current_tick - rx_timeout.ecu_switch_l_rx_timeout) > rt_tick_from_millisecond(1000)) {
        if (vehicle_get_data(VEH_HORN) > 0) vehicle_set_data(VEH_HORN, 0);
        if (vehicle_get_data(VEH_BTN_WHEEL) > 0) vehicle_set_data(VEH_BTN_WHEEL, 0);
        if (vehicle_get_data(VEH_ROCKER_LEFT) > 0) vehicle_set_data(VEH_ROCKER_LEFT, 0);
        if (vehicle_get_data(VEH_ROCKER_RIGHT) > 0) vehicle_set_data(VEH_ROCKER_RIGHT, 0);
        if (vehicle_get_data(VEH_ROLLER_DIRE) > 0) vehicle_set_data(VEH_ROLLER_DIRE, 0);
        if (vehicle_get_data(VEH_ROLLER_LEN) > 0) vehicle_set_data(VEH_ROLLER_LEN, 0);
    }
    ///< 0x502 按键值不落槽位, 无超时清零内容 (ecu_switch_r_rx_timeout 仍随报文刷新)
    if ((current_tick - rx_timeout.ecu_charger_rx_timeout) > rt_tick_from_millisecond(1000)) {
        ///< 充电状态改由 GPIO(PE.17) 检测, 避免被 CAN 超时清零覆盖
        // if (vehicle_get_data(VEH_CHARGER_STATUS) > 0) vehicle_set_data(VEH_CHARGER_STATUS, 0);
        if (vehicle_get_data(VEH_CHARGER_CURRENT) > 0) vehicle_set_data(VEH_CHARGER_CURRENT, 0);
        if (vehicle_get_data(VEH_CHARGER_VOLTAGE) > 0) vehicle_set_data(VEH_CHARGER_VOLTAGE, 0);
        if (vehicle_get_data(VEH_CHARGER_DTC) > 0) vehicle_set_data(VEH_CHARGER_DTC, 0);
    }
    if ((current_tick - rx_timeout.ecu_nfc_rx_timeout) > rt_tick_from_millisecond(1000)) {
        if (vehicle_get_data(VEH_NFC_MATCH_STATE) > 0) vehicle_set_data(VEH_NFC_MATCH_STATE, 0);
        ///< 同步清 NFC 弹窗位, 使位图与 VEH_NFC_MATCH_STATE 保持一致
        {
            uint32_t m = (uint32_t)vehicle_get_data(VEH_CAN_POPUP_MSG);
            BIT_FLAG(m, POP_TIP_NFC_LEARNING, 0);
            BIT_FLAG(m, POP_TIP_NFC_SUCCESS, 0);
            BIT_FLAG(m, POP_TIP_NFC_FAILURE, 0);
            vehicle_set_data(VEH_CAN_POPUP_MSG, (int32_t)m);
        }
    }
    if ((current_tick - rx_timeout.ecu_tpms_rx_timeout) > rt_tick_from_millisecond(1000)) {
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

static int parse_can_msg_80_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_80 dlc[%d] or msg buff err!\r\n", size);
        return -1;
    }
    rx_timeout.ecu_nfc_rx_timeout = rt_tick_get();
    int data = GET_DATA_BITS(buf[1], 0, 0x03);  ///< match_success 2bit@byte1
    vehicle_set_data(VEH_NFC_MATCH_STATE, data);

    uint32_t m = (uint32_t)vehicle_get_data(VEH_CAN_POPUP_MSG);
    BIT_FLAG(m, POP_TIP_NFC_LEARNING, (data == 1));
    BIT_FLAG(m, POP_TIP_NFC_FAILURE, (data == 2));
    BIT_FLAG(m, POP_TIP_NFC_SUCCESS, (data == 3));
    vehicle_set_data(VEH_CAN_POPUP_MSG, (int32_t)m);

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
    int data;

    data = GET_DATA_BITS(buf[0], 0, 0x07);  ///< Work_Mode 3bit
    vehicle_set_data(VEH_WORK_MODE, data);

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

    uint32_t m = (uint32_t)vehicle_get_data(VEH_CAN_POPUP_MSG);
    BIT_FLAG(m, POP_TIP_SEAT_UNLOCK, (data == 1));
    vehicle_set_data(VEH_CAN_POPUP_MSG, (int32_t)m);

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
    int data = LE_U8(buf, 7);  ///< VCU_DTC_Code 8bit@byte7
    vehicle_set_data(VEH_DTC_CODE, data);
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
    int data;

    data = GET_DATA_BITS(buf[0], 2, 0x01);  ///< VCUEmShutDown
    vehicle_set_data(VEH_EMERGENCY_SHUTDOWN, data);

    data = GET_DATA_BITS(buf[0], 3, 0x01);  ///< Cruise_Icon
    vehicle_set_data(VEH_CRUISE_MAIN_SWITCH, data);

    data = GET_DATA_BITS(buf[0], 4, 0x03);  ///< Cruise_Active
    vehicle_set_data(VEH_CRUISE_STATUS, data);

    uint32_t m = (uint32_t)vehicle_get_data(VEH_CAN_POPUP_MSG);
    BIT_FLAG(m, POP_TIP_CRUISE_FAULT, (data == 2));
    vehicle_set_data(VEH_CAN_POPUP_MSG, (int32_t)m);

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
    int data;

    data = LE_U8(buf, 0);  ///< MCUErrBits
    vehicle_set_data(VEH_MCU_DTC, data);

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

    ///< MCUGearSts: xlsx 0:P 1:N 2:D 3:R 7:Invalid -> 项目 0:neutral 1:D 2:R
    data = GET_DATA_BITS(buf[2], 0, 0x07);
    vehicle_set_data(VEH_GEAR_POSITION, data);

    ///< MCUDriveSts: xlsx 0:ECO 1:Normal 2:SPORT 3:Invalid -> 项目 0:normal 1:eco 2:sport
    int mode = GET_DATA_BITS(buf[2], 4, 0x03);
    switch (mode) {
        case 0:  data = 1; break;  ///< ECO
        case 2:  data = 2; break;  ///< SPORT
        default: data = 0; break; ///< Normal/Invalid -> normal
    }
    vehicle_set_data(VEH_DRIVE_MODE, data);

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
    rx_timeout.ecu_mcu_rx_timeout = rt_tick_get();
    int data = (int)(LE_U16(buf, 6) * 0.01);  ///< Speed 16bit@byte6, km/h
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
            ///< 示例: 1 号包 SOC<20 置位 BATT_LOW, 否则清位, 阈值按实车调整
            {
                uint32_t m = (uint32_t)vehicle_get_data(VEH_CAN_POPUP_MSG);
                BIT_FLAG(m, POP_TIP_BATT_LOW, (data > 0 && data < 20));
                vehicle_set_data(VEH_CAN_POPUP_MSG, (int32_t)m);
            }
            break;
        case CAN_BMS11_P1_ID:  ///< BMS11 工作状态 (BMS_life@byte7 仅心跳)
            data = GET_DATA_BITS(buf[0], 2, 0x03);
            vehicle_set_data(VEH_BAT_WORK_STATUS_P1, data);
            break;
        case CAN_BMS4_P1_ID:  ///< BMS4/5/6 一/二/三级 DTC, 同包复用同一槽位
        case CAN_BMS5_P1_ID:
        case CAN_BMS6_P1_ID:
            data = LE_U8(buf, 7);
            vehicle_set_data(VEH_BAT_DTC_P1, data);
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
        case CAN_BMS5_P2_ID:
        case CAN_BMS6_P2_ID:
            data = LE_U8(buf, 7);
            vehicle_set_data(VEH_BAT_DTC_P2, data);
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
        case CAN_BMS5_P3_ID:
        case CAN_BMS6_P3_ID:
            data = LE_U8(buf, 7);
            vehicle_set_data(VEH_BAT_DTC_P3, data);
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

    data = (int)(LE_U16(buf, 6) * 0.01);  ///< ABS_VehSpd km/h
    vehicle_set_data(VEH_SPEED_CURRENT, data);
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

    int data = LE_U8(buf, 2);  ///< TCS_Control
    vehicle_set_data(VEH_TCS_CONTROL, data);

    int index = LE_U8(buf, 6);  ///< TCS_WorkStatus
    vehicle_set_data(VEH_TCS_WORKSTATUS, index);

    if(index == 0 && data == 0)
        vehicle_set_data(VEH_TCS_WARNING, 0);
    else if(index == 1 && data == 1)
        vehicle_set_data(VEH_TCS_WARNING, 2);
    else
        vehicle_set_data(VEH_TCS_WARNING, 1);

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
    int data = LE_U8(buf, 7);  ///< ABS_FPC_DTC
    vehicle_set_data(VEH_DTC_CODE, data);
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
    int data;

    ///< p_charger_heartBeat@byte0 — 仅心跳

    ///< p_charger_status 改由 GPIO(PE.17) 检测, 此处不再解析
    // data = GET_DATA_BITS(buf[1], 0, 0x03);  ///< p_charger_status 2bit@byte1
    // vehicle_set_data(VEH_CHARGER_STATUS, data);

    data = (int)(LE_U16(buf, 2) * 0.1);  ///< p_ChargerCurrent A
    vehicle_set_data(VEH_CHARGER_CURRENT, data);

    data = (int)(LE_U16(buf, 4) * 0.1);  ///< p_ChargerVlot V
    vehicle_set_data(VEH_CHARGER_VOLTAGE, data);

    data = LE_U8(buf, 6);  ///< DTC_Code
    vehicle_set_data(VEH_CHARGER_DTC, data);
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
            rt_kprintf("CAN read error\n");
            continue;
        }
        can_msg_process(&rxmsg);
        rt_thread_mdelay(2);
    }
}

static void can_timeout_thread(void *param) {
    while (1) {
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

#endif