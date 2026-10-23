#ifndef __MW_CAN_H__
#define __MW_CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include "dev_config.h"

///< CAN 故障弹窗码 — 与 vehicle_param.VEH_CAN_POPUP_MSG 的位对应:
///<   VEH_CAN_POPUP_MSG 为位图, 第 (code-1) 位为 1 表示该故障存在, 全 0 表示无故障。
///<   放在 MW_CAN_ENABLE 门控外, 使 UI (pop_tip_page) 与 mw_can 共用同一份定义。
typedef enum {
    POP_TIP_NONE = 0,
    POP_TIP_SEAT_UNLOCK,        ///< bit0  座椅开锁
    POP_TIP_BATT_LOW,            ///< bit1  低电量
    POP_TIP_CRUISE_FAULT,        ///< bit2  巡航故障
    POP_TIP_NFC_LEARNING,        ///< bit3  NFC 学习中
    POP_TIP_NFC_SUCCESS,         ///< bit4  NFC 识别成功
    POP_TIP_NFC_FAILURE,         ///< bit5  NFC 识别失败
    POP_TIP_TPMS_INIT,           ///< bit6  胎压匹配
    POP_TIP_TPMS_SUCCESS,           ///< bit7  胎压匹配成功
    POP_TIP_TPMS_FAILED,           ///< bit8  胎压匹配失败
    POP_TIP_UPGRADE,             ///< bit9  升级中
    POP_TIP_UPGRADE_FAILED,      ///< bit10  升级失败
    POP_TIP_UPGRADE_SUCCESS,     ///< bit11  升级成功
    POP_TIP_END,
} pop_tip_code_e;

#if MW_CAN_ENABLE

///< 整车CAN网络信号 (xlsx 整车CAN通信网络信号) — 仅仪表相关报文
#define CAN_NFC_MATCH_ACK_ID      0x80   ///< NFC_PC_match_start_ACK (RX)
#define CAN_VCU_DISCHARGER_ID     0x102  ///< VCU_Discharger_Control (RX)
#define CAN_VCU_BROADCAST2_ID     0x108  ///< VCU_Broadcast2 (RX)
#define CAN_VCU_METER1_ID         0x10A  ///< VCU_Meter1 (RX)
#define CAN_VCU_BROADCAST4_ID     0x10D  ///< VCU_Broadcast4 (RX)
#define CAN_MCU_401_ID            0x401  ///< MCU_0x401 (RX)
#define CAN_MCU_403_ID            0x403  ///< MCU_0x403 (RX)
#define CAN_MCU_404_ID            0x404  ///< MCU_0x404 (RX)
#define CAN_MCU_405_ID            0x405  ///< MCU_0x405 (RX)

///< BMS 报文: 每条 3 个 ID 对应 1/2/3 号电池包, 共用同一 parser
#define CAN_BMS1_P1_ID            0x301
#define CAN_BMS1_P2_ID            0x311
#define CAN_BMS1_P3_ID            0x321
#define CAN_BMS3_P1_ID            0x303
#define CAN_BMS3_P2_ID            0x313
#define CAN_BMS3_P3_ID            0x323
#define CAN_BMS4_P1_ID            0x304
#define CAN_BMS4_P2_ID            0x314
#define CAN_BMS4_P3_ID            0x324
#define CAN_BMS5_P1_ID            0x305
#define CAN_BMS5_P2_ID            0x315
#define CAN_BMS5_P3_ID            0x325
#define CAN_BMS6_P1_ID            0x306
#define CAN_BMS6_P2_ID            0x316
#define CAN_BMS6_P3_ID            0x326
#define CAN_BMS7_P1_ID            0x307
#define CAN_BMS7_P2_ID            0x317
#define CAN_BMS7_P3_ID            0x327
#define CAN_BMS11_P1_ID           0x30B
#define CAN_BMS11_P2_ID           0x31B
#define CAN_BMS11_P3_ID           0x32B

#define CAN_ABS1_ID               0xA3   ///< ABS1 (RX)
#define CAN_TCS_ID                0xA4   ///< TCS (RX)
#define CAN_ABS2_ID               0xA5   ///< ABS2 (RX)
#define CAN_TPMS_METER2_ID        0x507  ///< TPMS_Meter2 (RX)
#define CAN_SWITCH_LEFT_ID        0x501  ///< SwitchLeft (RX)
#define CAN_SWITCH_RIGHT_ID       0x502  ///< SwitchRight (RX)
#define CAN_P_CHARGER1_ID         0x430  ///< p_charger1 (RX)

///< 里程/清零指令 (RX): 数据为大端字节序 (Big-Endian), 均带 J1850 CRC-8 校验
#define CAN_SET_TRIP_A            0x776  ///< 设置小计里程A Trip A
#define CAN_SET_TRIP_B            0x777  ///< 设置小计里程B Trip B
#define CAN_SET_ODO               0x778  ///< 设置总里程 ODO
#define CAN_CLEAN_EEPROM          0x779  ///< 清零整车 EEPROM

typedef struct{
    ///< 整车CAN网络信号 — 按 ECU 分组 (同一发送节点的报文共用一个超时戳)
    uint32_t ecu_vcu_rx_timeout;      ///< 0x102/0x108/0x10a/0x10d
    uint32_t ecu_mcu_rx_timeout;      ///< 0x401/0x403/0x404/0x405
    uint32_t ecu_bms_p1_rx_timeout;    ///< 1号电池包 BMS 报文 (0x301/0x303/0x304/0x305/0x306/0x307/0x30b)
    uint32_t ecu_bms_p2_rx_timeout;    ///< 2号电池包 BMS 报文 (0x311/0x313/0x314/0x315/0x316/0x317/0x31b)
    uint32_t ecu_bms_p3_rx_timeout;    ///< 3号电池包 BMS 报文 (0x321/0x323/0x324/0x325/0x326/0x327/0x32b)
    uint32_t ecu_abs_rx_timeout;       ///< 0xA3/0xA5
    uint32_t ecu_switch_l_rx_timeout;  ///< 0x501
    uint32_t ecu_switch_r_rx_timeout;  ///< 0x502
    uint32_t ecu_charger_rx_timeout;   ///< 0x430
    uint32_t ecu_nfc_rx_timeout;       ///< 0x80
    uint32_t ecu_tpms_rx_timeout;      ///< 0x507
} can_rx_timeout;

int mw_can_init(void);

/**
 * @brief  休眠前停止 CAN 收发: 关 RX/TX 中断, 关闭设备, 删除 RX/TX/cycle/timeout 线程
 *         同时把 STB pin 拉低让收发器进 standby, 进一步降功耗
 *         IGN_ON 唤醒走 rt_hw_cpu_reset, 重启后 mw_can_init 会重新拉起
 * @return 无
 */
void mw_can_sleep(void);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __MW_CAN_H__