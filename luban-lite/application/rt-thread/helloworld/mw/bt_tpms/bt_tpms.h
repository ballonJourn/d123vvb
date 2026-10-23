/*
* @file   bt_tpms.h
* @brief  蓝牙胎压 (BLE TPMS)
* @note   自 FreeRTOS 工程移植: 业务逻辑(配对状态机/故障判定/阈值)保留,
*         RTOS 接口换成 RT-Thread, 数据来源改为本工程的蓝牙胎压协议
*         —— bt_module 收到 "LP" 指示后经 bt_tpms_task_add() 入队,
*         由 bt_tpms_analysis_data() 解析天城 TC.TPMS 广播
*
*         配对模型是被动的: 扫描常开(见 bt_module.c), 传感器侧充气/放气/
*         手持激活后其广播状态位置位, 设备侧做 MAC 白名单比对。应用层只
*         能"决定学哪一轮", 用 bt_tpms_pair_*() 系列接口。
*/

#ifndef __BT_TPMS_H__
#define __BT_TPMS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rtthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "set_param.h"
#include "vehicle_param.h"
#include "uart_tpms.h"   /* tpms_pos_e: 对外配对接口的轮位参数 */

#define FRONT_TIRE    (1)
#define REAR_TIRE     (0)
#define BT_TPMS_CMD_DATA_OFFSET             (2)

#define BT_TPMS_PAIR_CMD                   (0x30) //胎压配对指令
#define BT_TPMS_PAIR_LEARNING              (0x31) //胎压正在学习中
#define BT_TPMS_STOP_PAIR_CMD              (0x32) //停止配对指令
#define BT_TPMS_CLEAN_ID_CMD               (0x33) //清除ID指令
#define BT_TPMS_PAIR_TIMEOUT               (0x40) //胎压配对超时
#define BT_TPMS_ANALYSIS_DATA_CMD          (0x50) //胎压数据解析指令

#define BT_TIRES_NUMBER                    (2)
#define BT_TPMS_FRONT_MAX_PRESSURE         (250) // 前轮胎气压上限   >= 290
#define BT_TPMS_REAR_MAX_PRESSURE          (280) // 后轮胎气压上限   >= 340
#define BT_TPMS_FLOAT_MAX_PRESSURE         (11)  // 解除高压策略下浮动 -11
#define BT_TPMS_FRONT_MIN_PRESSURE         (180) // 前轮胎气压下限 <= 220
#define BT_TPMS_REAR_MIN_PRESSURE          (200) // 后轮胎气压下限 <= 240
#define BT_TPMS_FLOAT_MIN_PRESSURE         (10)  // 解除低压策略上浮动 +10
#define BT_TPMS_MAX_TEMP                   (80)  // 高温上限
#define BT_TPMS_NORMAL_TEMP                (79)  // 解除高温

///< 已配对轮有新数据时, 胎压周期落盘间隔 (注意 NOR flash 磨损: 量产建议拉长或改值变化才写)
#define BT_TPMS_SAVE_PERIOD_MS             (4000)


typedef enum
{
    BT_TPMS_RECENT_STABLE_STATE = 0,  //胎压近期平稳状态
    BT_TPMS_FRONT_LOW_PRESSURE,       //前轮低压
    BT_TPMS_FRONT_HIGH_PRESSURE,      //前轮高压
    BT_TPMS_FRONT_HIGH_TEMP,          //前轮高温
    BT_TPMS_FRONT_QUICK_AIR_LEAK,     //前轮快漏气
    BT_TPMS_FRONT_SLOW_AIR_LEAK,      //前轮慢漏气
    BT_TPMS_FRONT_AERATE,             //前轮加气
    BT_TPMS_FRONT_LOW_VOLTAGE,        //前轮电量低
    BT_TPMS_FRONT_NOT_PAIR,           //前轮未学习
    BT_TPMS_REAR_LOW_PRESSURE,        //后轮低压
    BT_TPMS_REAR_HIGH_PRESSURE,       //后轮高压
    BT_TPMS_REAR_HIGH_TEMP,           //后轮高温
    BT_TPMS_REAR_QUICK_AIR_LEAK,      //后轮快漏气
    BT_TPMS_REAR_SLOW_AIR_LEAK,       //后轮慢漏气
    BT_TPMS_REAR_AERATE,              //前轮加气
    BT_TPMS_REAR_LOW_VOLTAGE,         //后轮电量低
    BT_TPMS_REAR_NOT_PAIR,            //后轮未学习
    BT_TPMS_FAILURE_END
}BT_TPMS_FAULT;

enum
{
    INIT_PROCESS,
    ANALYSIS_DATA_PROCESS,
};


#define BT_TPMS_MAC_ADDR_LEN      (16)

///< 线程参数
#define BT_TPMS_THREAD_STACK_SIZE (2048)
#define BT_TPMS_THREAD_PRIO       (12)
#define BT_TPMS_THREAD_TICK       (10)

///< 本工程两轮: 前轮 -> 左前槽位, 后轮 -> 左后槽位
#define BT_TPMS_SLOT(pos)         (((pos) == FRONT_TIRE) ? TPMS_LEFT_FRONT : TPMS_LEFT_REAR)
///< 同上, 用于 usr_param 的 get/set 句柄
#define BT_TPMS_TIRE_INFO_ID(pos) (((pos) == FRONT_TIRE) ? USR_PARAM_LEFT_FRONT_TIRE_INFO \
                                                          : USR_PARAM_LEFT_REAR_TIRE_INFO)
///< MAC 在本工程 usr_param_t 里的下标 (0=前/左前, 1=后/左后)
#define BT_TPMS_MAC_IDX(pos)      (((pos) == FRONT_TIRE) ? 0 : 1)

typedef struct
{
    char macAddr[BT_TPMS_MAC_ADDR_LEN];     //蓝牙MAC地址，作为区分标志
    int  tpmsPressure;                      //胎压单位kpa
    int8_t tpmsTemp;                        //胎温,摄氏度，有负值
    uint8_t pairState;                      //配对状态 0：未学习 1：学习中  2：已学习            3：超时
    uint8_t faultStatus;                    //故障状态 0x00:表示近期平稳 0x01:表示慢漏气                 0x02:表示快漏气  0x03:表示加气
    int battaryValue;                       //胎压传感器电压值
    uint8_t lostSensor;                     //30min 无数据丢失标志 0:正常 1:丢失
    uint8_t bootStale;                      //开机 BT_TPMS_BOOT_STALE_TIME 内无新数据 0:正常 1:显示"--"
    rt_tick_t lastRxTick;                   //最近一次收到该轮数据 0:开机以来未收到
    char btTpmsVer[12];
}BT_TPMS_TIRE_INFO_T;

typedef struct
{
  char macAddr[BT_TPMS_MAC_ADDR_LEN];     //蓝牙MAC地址，作为区分标志                            //学习轮位
  uint8_t pairState;                      //配对状态 0：未学习 1：学习中  2：已学习            3：超时
}BT_TPMS_PAIR_PROCESS_T;

///< 配对流程对外状态 (由内部 pairState 导出)
///< 注意: 命名带 STATE 前缀, 避开上面的 BT_TPMS_PAIR_* 配对指令码宏
typedef enum
{
    BT_TPMS_PAIR_STATE_IDLE = 0,  ///< 空闲: 未配对, 且当前没有进行中的配对
    BT_TPMS_PAIR_STATE_LEARNING,  ///< 学习中: 等待传感器上报
    BT_TPMS_PAIR_STATE_SUCCESS,   ///< 学习成功: 已记录该轮传感器 MAC
    BT_TPMS_PAIR_STATE_TIMEOUT,   ///< 超时: 1min 内未学到 (原有配对记录若存在则保留)
} bt_tpms_pair_state_e;

int bt_tpms_task_init(void);
int bt_tpms_task_add(uint8_t *data, uint8_t len, uint8_t cmd);
void set_bt_tpms_mac_addr(void);
BT_TPMS_TIRE_INFO_T* bt_tpms_get_data(uint8_t pos);

/*
 * 配对接口 —— 供应用层调用。
 * pos 取 uart_tpms.h 的 tpms_pos_e, 本工程两轮只用 TPMS_LEFT_FRONT / TPMS_LEFT_REAR。
 * 状态机在 bt_tpms 线程里推进, 应用层轮询 bt_tpms_pair_get_state() 取结果。
 */
int bt_tpms_pair_start(tpms_pos_e pos);   ///< 发起某轮学习; 0:已受理 -1:非法轮位
int bt_tpms_pair_stop(tpms_pos_e pos);    ///< 提前结束学习, 保留原配对记录; 同上
int bt_tpms_pair_clear(tpms_pos_e pos);   ///< 清除该轮配对记录; 同上
bt_tpms_pair_state_e bt_tpms_pair_get_state(tpms_pos_e pos);

#ifdef __cplusplus
}
#endif

#endif