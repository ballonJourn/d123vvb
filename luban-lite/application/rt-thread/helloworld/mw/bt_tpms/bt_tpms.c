/*
* @note   自 FreeRTOS 工程移植到 RT-Thread:
*         - 业务逻辑(配对状态机/故障判定/阈值)保留
*         - xQueue/xTask/xTimer/pvPortMalloc -> rt_mq/rt_thread/rt_timer/rt_malloc
*         - CKX_PARAM_* + set/get_basic_param_info -> USR_PARAM_* + set/get_usr_param
*         - 数据源改为本工程的蓝牙胎压协议: bt_module 收到 "LP" 指示后经
*           bt_tpms_task_add() 入队, 由 bt_tpms_analysis_data() 解析天城
*           TC.TPMS 广播 (见 /mnt/hgfs/opendir/BLE胎压监测协议.pdf)
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bt_tpms.h"
#include "bt_tpms_failure.h"
#include "bt_module.h"
#include "mw_log.h"

#if MW_BT_TPMS_ENABLE


#define BT_TPMS_LOW_VOLTAGE_VALUE                   (24) //2.4V, 单位 100mV (协议 2.4V)
#define BT_TPMS_PAIE_TIMEOUT_TIME                   (60) //1min超时
#define BT_TPMS_MSG_MAX_LEN                         (256)
#define BT_TPMS_PAIR_TIMER_INTERVAL_PERIOD          (1000) //ms

///< 天城 TC.TPMS 广播
#define TC_TPMS_ADV_NAME                            "TC.TPMS"
#define TC_TPMS_AD_TYPE_NAME_SHORT                  (0x08)
#define TC_TPMS_AD_TYPE_NAME_COMPLETE               (0x09)
#define TC_TPMS_AD_TYPE_MANU                        (0xFF)
#define TC_TPMS_MANU_LEN                            (11)   ///< S + V + T + P(2) + MAC(6)

///< 状态位 S: 仅这三位任一为 1 时才允许配对学习
#define TC_TPMS_STATUS_PAIR_BIT0                    (1 << 0) ///< 充气配对
#define TC_TPMS_STATUS_PAIR_BIT1                    (1 << 1) ///< 漏气(放气配对)
#define TC_TPMS_STATUS_PAIR_BIT6                    (1 << 6) ///< 低频(设备激活配对)
#define TC_TPMS_STATUS_PAIR_MASK                    (TC_TPMS_STATUS_PAIR_BIT0 | \
                                                     TC_TPMS_STATUS_PAIR_BIT1 | \
                                                     TC_TPMS_STATUS_PAIR_BIT6)

///< 传感器上报的是绝压, 先减 14.6psi 大气压得表压, 再换算成 kPa (1 psi = 6.895 kPa)
///< 依据: 协议 PDF 的 "17.5 psi - 14.6 psi = 2.9 psi (show 2.9 psi)";
///<       实测传感器在常压下读数恰为 146 (0.1psi), 即 14.6psi
#define TC_TPMS_PSI10_TO_KPA(p)   ((((p) - 146) * 6895 + 5000) / 10000)

///< 30min 无数据丢失报警 (车速 > 20km/h 起算)
#define BT_TPMS_LOST_SPEED_KMH                      (20)
#define BT_TPMS_LOST_TIMEOUT_TIME                   (30 * 60) //s
#define BT_TPMS_LOST_TIMER_PERIOD                   (1000)    //ms

static rt_timer_t pairTimer = RT_NULL;
static rt_timer_t lostTimer = RT_NULL;
static rt_mq_t btTpmsQueueHandle = RT_NULL;
static int  btTpmsTimeoutCnt = 0;
static int8_t curPairPos = -1;

///< 最近一次配对操作的内部轮位与结果 (只在 bt_tpms 线程里写),
///< 用于让配对接口区分"学到"与"超时但保留旧记录"——后者只看 pairState 分不出来
static int8_t pairOpPos = -1;
static bt_tpms_pair_state_e pairOpResult = BT_TPMS_PAIR_STATE_IDLE;

//static BT_TPMS_PAIR_PROCESS_T  btTpmsPairProces[BT_TIRES_NUMBER];
static BT_TPMS_TIRE_INFO_T btTpmsRealTire[BT_TIRES_NUMBER];

///< 最近一次收到胎压数据的时间 (丢失报警计时)
static rt_tick_t lastDataTick = 0;

///< 上次胎压落盘时刻
static rt_tick_t lastSaveTick = 0;

///< 天城 TC.TPMS 广播解析结果
typedef struct
{
    uint8_t  status;     ///< S 状态位
    uint8_t  volt;       ///< V 电压, 单位 100mV
    int8_t   temp;       ///< T 温度, 单位 1'C
    uint16_t pressure;   ///< P 压力, 单位 0.1psi (大端)
    uint8_t  mac[6];     ///< MAC, 低字节在前
} TC_TPMS_INFO_T;


static void  reset_bt_tpms_mac_addr(uint8_t pos)
{
    ///< 未配对用全 '0' 表示 (is_bt_tpms_paired 依赖该约定),
    ///< 只填 12 个字符并补结束符, 避免 macAddr 丢失 '\0' 后被当字符串越界读
    memset(btTpmsRealTire[pos].macAddr, '0', 12);
    btTpmsRealTire[pos].macAddr[12] = '\0';
}

/**
 * @brief 该轮位是否已记录有效 MAC
 * @note  未配对有两种表示: 空串, 或 reset_bt_tpms_mac_addr() 写入的 12 个 '0'
 */
static bool bt_tpms_mac_is_valid(uint8_t pos)
{
    const char *mac = btTpmsRealTire[pos].macAddr;

    if (mac[0] == '\0')
    {
        return false;
    }

    for (int i = 0; i < 6; i++)
    {
        if (mac[i] != '0')
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief 按 MAC 反查已配对的轮位
 * @return 内部轮位 (FRONT_TIRE/REAR_TIRE), -1 表示没有已配对且匹配的轮位
 */
static int8_t bt_tpms_find_pos_by_mac(const char *macAddr)
{
    if (macAddr == NULL)
    {
        return -1;
    }

    for (int i = 0; i < BT_TIRES_NUMBER; i++)
    {
        if (btTpmsRealTire[i].pairState != 2)
        {
            continue;
        }

        if (strncmp(btTpmsRealTire[i].macAddr, macAddr, 12) == 0)
        {
            return (int8_t)i;
        }
    }

    return -1;
}

static void start_bt_tpms_pair_timer(void)
{
    if(pairTimer != RT_NULL)
    {
        rt_timer_start(pairTimer);
    }
}

static void stop_bt_tpms_pair_timer(void)
{
    if(pairTimer != RT_NULL)
    {
        rt_timer_stop(pairTimer);
    }
}


/**
 * @brief 汇总两轮告警状态写入 VEH_LIGHT_TIRE, 供主界面胎压告警图标显隐
 * @note  告警 = 任一轮未配对 / 任一故障位有效(含加气提示 AERATE) /
 *        已配对轮传感器丢失(30min 无数据)
 */
static void bt_tpms_publish_alarm(void)
{
    uint8_t alarm = 0;

    for (int i = 0; i < BT_TIRES_NUMBER; i++)
    {
        if ((btTpmsRealTire[i].pairState != 2) || (btTpmsRealTire[i].lostSensor != 0))
        {
            alarm = 1;
            break;
        }
    }

    for (uint8_t f = BT_TPMS_FRONT_LOW_PRESSURE; (f < BT_TPMS_FAILURE_END) && (alarm == 0); f++)
    {
        if (bt_tpms_get_failure(f) != 0)
        {
            alarm = 1;
        }
    }

    vehicle_set_data(VEH_LIGHT_TIRE, alarm);
}

/**
 * @brief 把 RAM 里的实时胎压与故障位汇总, 落库到 usr_param 与 vehicle_data
 */
static void bt_tpms_publish_tire(uint8_t pos)
{
    BT_TPMS_TIRE_INFO_T *tire_info = &btTpmsRealTire[pos];
    tpms_param_t tire = {0};
    int iRearOffSet = ((pos == REAR_TIRE) ? 8 : 0);
    bool is_paired = (tire_info->pairState == 2);

    get_usr_param(BT_TPMS_TIRE_INFO_ID(pos), &tire);

    tire.tpms_pressure = (uint16_t)tire_info->tpmsPressure;
    tire.tpms_temp = tire_info->tpmsTemp;

    tire.tpms_state.high_pressure = bt_tpms_get_failure(BT_TPMS_FRONT_HIGH_PRESSURE + iRearOffSet);
    tire.tpms_state.low_pressure  = bt_tpms_get_failure(BT_TPMS_FRONT_LOW_PRESSURE + iRearOffSet);
    tire.tpms_state.high_temp     = bt_tpms_get_failure(BT_TPMS_FRONT_HIGH_TEMP + iRearOffSet);
    tire.tpms_state.air_leak      = bt_tpms_get_failure(BT_TPMS_FRONT_SLOW_AIR_LEAK + iRearOffSet)
                                  | bt_tpms_get_failure(BT_TPMS_FRONT_QUICK_AIR_LEAK + iRearOffSet);
    tire.tpms_state.low_voltage   = bt_tpms_get_failure(BT_TPMS_FRONT_LOW_VOLTAGE + iRearOffSet);
    tire.tpms_state.not_pair      = is_paired;
    tire.tpms_state.invalid_data  = is_paired;
    tire.tpms_state.lost_sensor   = (is_paired ? tire_info->lostSensor : 0);

    set_usr_param(BT_TPMS_TIRE_INFO_ID(pos), &tire);

    vehicle_set_data((pos == FRONT_TIRE) ? VEH_TPMS_FRONT_PRESSURE : VEH_TPMS_REAR_PRESSURE,
                     tire_info->tpmsPressure);
    vehicle_set_data((pos == FRONT_TIRE) ? VEH_TPMS_FRONT_TEMP : VEH_TPMS_REAR_TEMP,
                     tire_info->tpmsTemp);

    bt_tpms_publish_alarm();
}

static void bt_tpms_failure_handler(uint8_t pos, uint8_t type)
{
    int iRearOffSet = 0;
    int iMaxPressure = BT_TPMS_FRONT_MAX_PRESSURE;
    int iMinPressure = BT_TPMS_FRONT_MIN_PRESSURE;
    uint8_t iRet = 0;


    if (pos == 0)
    {
        iRearOffSet = 8;
        iMaxPressure = BT_TPMS_REAR_MAX_PRESSURE;
        iMinPressure = BT_TPMS_REAR_MIN_PRESSURE;
    }

    if (type == INIT_PROCESS)
    {
        ///< 开机恢复: 先把持久化的胎压读回 RAM, 否则 publish_tire 会用 RAM 的 0
        ///< 洗掉存档 (也就实现了"开机显示保存的胎压")
        tpms_param_t tire = {0};

        get_usr_param(BT_TPMS_TIRE_INFO_ID(pos), &tire);
        btTpmsRealTire[pos].tpmsPressure = tire.tpms_pressure;
        btTpmsRealTire[pos].tpmsTemp = tire.tpms_temp;
    }

    if (btTpmsRealTire[pos].tpmsPressure >= iMaxPressure)
    {
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_HIGH_PRESSURE + iRearOffSet, 1);
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_LOW_PRESSURE + iRearOffSet, 0);
    }
    else if(btTpmsRealTire[pos].tpmsPressure >= (iMaxPressure - BT_TPMS_FLOAT_MAX_PRESSURE))
    {
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_LOW_PRESSURE + iRearOffSet, 0);
    }
    else if (btTpmsRealTire[pos].tpmsPressure >= iMinPressure + BT_TPMS_FLOAT_MIN_PRESSURE)
    {
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_HIGH_PRESSURE + iRearOffSet, 0);
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_LOW_PRESSURE + iRearOffSet, 0);
    }
    else if (btTpmsRealTire[pos].tpmsPressure > iMinPressure)
    {
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_HIGH_PRESSURE + iRearOffSet, 0);
    }
    else
    {
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_HIGH_PRESSURE + iRearOffSet, 0);
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_LOW_PRESSURE + iRearOffSet, 1);
    }

    if (btTpmsRealTire[pos].tpmsTemp > BT_TPMS_MAX_TEMP)
    {
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_HIGH_TEMP + iRearOffSet, 1);
    }
    else if (btTpmsRealTire[pos].tpmsTemp < BT_TPMS_NORMAL_TEMP)
    {
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_HIGH_TEMP + iRearOffSet, 0);
    }

    if(btTpmsRealTire[pos].faultStatus == 1)
    {
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_SLOW_AIR_LEAK + iRearOffSet, 1);
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_QUICK_AIR_LEAK + iRearOffSet, 0);
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_AERATE + iRearOffSet, 0);
    }
    else if(btTpmsRealTire[pos].faultStatus == 2)
    {
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_QUICK_AIR_LEAK + iRearOffSet, 1);
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_SLOW_AIR_LEAK + iRearOffSet, 0);
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_AERATE + iRearOffSet, 0);
    }
    else if(btTpmsRealTire[pos].faultStatus == 4)
    {
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_AERATE + iRearOffSet, 1);
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_SLOW_AIR_LEAK + iRearOffSet, 0);
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_QUICK_AIR_LEAK + iRearOffSet, 0);
    }
    else if(btTpmsRealTire[pos].faultStatus == 0)
    {
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_SLOW_AIR_LEAK + iRearOffSet, 0);
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_QUICK_AIR_LEAK + iRearOffSet, 0);
        iRet += bt_tpms_set_failure(BT_TPMS_FRONT_AERATE + iRearOffSet, 0);
    }

    if(type == ANALYSIS_DATA_PROCESS)
    {
        if(btTpmsRealTire[pos].battaryValue < BT_TPMS_LOW_VOLTAGE_VALUE)
        {
            iRet += bt_tpms_set_failure(BT_TPMS_FRONT_LOW_VOLTAGE + iRearOffSet, 1);
        }
        else if(btTpmsRealTire[pos].battaryValue > BT_TPMS_LOW_VOLTAGE_VALUE)
        {
            iRet += bt_tpms_set_failure(BT_TPMS_FRONT_LOW_VOLTAGE + iRearOffSet, 0);
        }
    }

    bt_tpms_failure_start(iRet);

    ///< 实时值与故障位汇总落库 (INIT_PROCESS 时把持久化的值同步到 vehicle_data)
    bt_tpms_publish_tire(pos);
}


static bool is_bt_tpms_paired(char * macAddr)
{
    if(macAddr != NULL)
    {
        ///< 入参是 usr_param 里的二进制 MAC 字节, 未配对的默认值/清除值是全 0x00,
        ///< 不能只判 ASCII '0' 或 0xFF —— 首次上电会把全零误判成已配对
        if(macAddr[0] == 0x00
            && macAddr[1] == 0x00
            && macAddr[2] == 0x00
            && macAddr[3] == 0x00
            && macAddr[4] == 0x00
            && macAddr[5] == 0x00)
        {
            return false;
        }

        if(macAddr[0] == '0'
            && macAddr[1] == '0'
            && macAddr[2] == '0'
            && macAddr[3] == '0'
            && macAddr[4] == '0'
            && macAddr[5] == '0')
         {
            return false;
         }


         if(macAddr[0] == 0xFF
            && macAddr[1] == 0xFF
            && macAddr[2] == 0xFF
            && macAddr[3] == 0xFF
            && macAddr[4] == 0xFF
            && macAddr[5] == 0xFF)
         {
            return false;
         }


         return true;
    }

    return false;
}

void set_bt_tpms_mac_addr(void)
{
    uint8_t posTmp = 0;
    uint8_t macParam[BT_TPMS_WHEEL_NUM][BT_TPMS_MAC_LEN] = {0};

    get_usr_param(USR_PARAM_BT_TPMS_MAC, macParam);

    for (int i = 0; i < BT_TIRES_NUMBER; i++)
    {
        int idx = BT_TPMS_MAC_IDX(i);

        if (!is_bt_tpms_paired((char *)macParam[idx]))
        {
            continue;
        }

        snprintf(btTpmsRealTire[i].macAddr, sizeof(btTpmsRealTire[i].macAddr),
                 "%02X%02X%02X%02X%02X%02X",
                 macParam[idx][0], macParam[idx][1], macParam[idx][2],
                 macParam[idx][3], macParam[idx][4], macParam[idx][5]);
        btTpmsRealTire[i].pairState = 2;
    }

    uint8_t ret = 0;

    ret += bt_tpms_set_failure(BT_TPMS_FRONT_NOT_PAIR, btTpmsRealTire[FRONT_TIRE].pairState != 2);
    ret += bt_tpms_set_failure(BT_TPMS_REAR_NOT_PAIR, btTpmsRealTire[REAR_TIRE].pairState != 2);
    bt_tpms_failure_start(ret);

    (void)posTmp;

    if(btTpmsRealTire[REAR_TIRE].pairState == 2)
    {
        bt_tpms_failure_handler(REAR_TIRE, INIT_PROCESS);
    }

    if(btTpmsRealTire[FRONT_TIRE].pairState == 2)
    {
        bt_tpms_failure_handler(FRONT_TIRE, INIT_PROCESS);
    }

    ///< 开机即反映"未配对"告警 (此时还没有任何数据帧经过 publish_tire)
    bt_tpms_publish_alarm();
}

/**
 * @brief 把 12 位 hex 字符串写入 usr_param 的 MAC 槽位
 */
///< 配对 MAC 立即落盘: save 链路栈深(set_set_param 按值传 140B 结构体 + cJSON_Print
///< 递归 + printf/fprintf ~512B), bt_tpms 线程 2048 栈扛不住, 用一次性线程跑
static volatile bool flushPending = false;  ///< 落盘期间又有 MAC 变化, 需补写一轮
static volatile bool flushRunning = false;  ///< 落盘线程存活标志

static void bt_tpms_flush_thread(void *param)
{
    (void)param;

    do {
        flushPending = false;
        if (save_usr_param() != 0)
        {
            rt_kprintf("bt tpms: save pair mac to json failed!\n");
        }
    } while (flushPending);   ///< 如换轮腾挪会连清旧轮+学新轮两次触发, 补写兜住

    flushRunning = false;
}

static void bt_tpms_flush_pair_data(void)
{
    flushPending = true;

    if (flushRunning)
    {
        return;   ///< 已有线程在写, 靠其循环补写兜住本次变化
    }

    flushRunning = true;

    rt_thread_t tid = rt_thread_create("tpms_sv", bt_tpms_flush_thread, RT_NULL,
                                       4096, 15, 10);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }
    else
    {
        ///< 创建失败: 数据仍在 RAM, IGN OFF 落盘兜底
        flushRunning = false;
    }
}

static void save_bt_tpms_mac(uint8_t pos, const char *macAddr)
{
    uint8_t macParam[BT_TPMS_WHEEL_NUM][BT_TPMS_MAC_LEN] = {0};
    int idx = BT_TPMS_MAC_IDX(pos);

    get_usr_param(USR_PARAM_BT_TPMS_MAC, macParam);

    if (macAddr == NULL || strlen(macAddr) < 12)
    {
        memset(macParam[idx], 0, BT_TPMS_MAC_LEN);
    }
    else
    {
        for (int i = 0; i < BT_TPMS_MAC_LEN; i++)
        {
            char byte_str[3] = { macAddr[2 * i], macAddr[2 * i + 1], '\0' };
            macParam[idx][i] = (uint8_t)strtol(byte_str, NULL, 16);
        }
    }

    set_usr_param(USR_PARAM_BT_TPMS_MAC, macParam);

    ///< MAC 数据变化立即落盘 (学到新 MAC / 换轮腾挪清旧轮 / 用户清除配对都走这里)
    bt_tpms_flush_pair_data();
}

static void clean_bt_tpms_pair_data(uint8_t pos)
{
    //to do
    if(pos != 0 && pos != 1)
    {
        return;
    }
    uint16_t tpmsPressure = 0;

    reset_bt_tpms_mac_addr(pos);
    memset(btTpmsRealTire[pos].btTpmsVer, 0, sizeof(btTpmsRealTire[pos].btTpmsVer));

    save_bt_tpms_mac(pos, NULL);

    tpms_param_t tire = {0};

    get_usr_param(BT_TPMS_TIRE_INFO_ID(pos), &tire);
    tire.tpms_pressure = tpmsPressure;
    tire.tpms_temp = 0;
    set_usr_param(BT_TPMS_TIRE_INFO_ID(pos), &tire);
}

static void bt_tpms_clean_failture(uint8_t pos)
{
    int iRearOffSet = ((pos == 0) ? 8 :0);
    uint8_t iRet = 0;

    iRet += bt_tpms_set_failure(BT_TPMS_FRONT_LOW_PRESSURE + iRearOffSet, 0);
    iRet += bt_tpms_set_failure(BT_TPMS_FRONT_HIGH_PRESSURE + iRearOffSet, 0);
    iRet += bt_tpms_set_failure(BT_TPMS_FRONT_HIGH_TEMP + iRearOffSet, 0);
    iRet += bt_tpms_set_failure(BT_TPMS_FRONT_QUICK_AIR_LEAK + iRearOffSet, 0);
    iRet += bt_tpms_set_failure(BT_TPMS_FRONT_SLOW_AIR_LEAK + iRearOffSet, 0);
    iRet += bt_tpms_set_failure(BT_TPMS_FRONT_AERATE + iRearOffSet, 0);
    iRet += bt_tpms_set_failure(BT_TPMS_FRONT_LOW_VOLTAGE + iRearOffSet, 0);
    bt_tpms_failure_start(iRet);
}

/**
 * @brief 配对学习: 正在配对的轮位收到一帧广播时的判定 (原 is_can_analysis_data)
 * @param macAddr 12 位 hex 字符串(正常字节序)
 * @param status  天城协议的状态位 S, 用于学习条件判定
 * @return true: 学习成功, 该轮 MAC 已落库
 * @note  稳态(非配对中)的数据归属不在这里判, 见 bt_tpms_analysis_data()
 */
static bool bt_tpms_pair_learn_match(const char *macAddr, uint8_t status)
{
    uint8_t otherPos;

    if (macAddr == NULL)
    {
        return false;
    }

    ///< curPairPos 仅在配对期间被赋值, 其余时刻为 -1, 必须先判合法
    if (curPairPos != FRONT_TIRE && curPairPos != REAR_TIRE)
    {
        return false;
    }

    if (btTpmsRealTire[curPairPos].pairState != 1)
    {
        return false;
    }

    ///< 只允许状态位 bit0/bit1/bit6 为 1 时才学习 (放气/充气/手持匹配器激活)
    if ((status & TC_TPMS_STATUS_PAIR_MASK) == 0)
    {
        return false;
    }

    memcpy(btTpmsRealTire[curPairPos].macAddr, macAddr, strlen(macAddr) + 1);
    btTpmsRealTire[curPairPos].macAddr[12] = '\0';
    mw_logd("bt tpms macAddr:%s\n", btTpmsRealTire[curPairPos].macAddr);

    ///< 全 '0' 的 MAC 视为无效传感器, 放弃本次学习
    if (!bt_tpms_mac_is_valid(curPairPos))
    {
        reset_bt_tpms_mac_addr(curPairPos);
        btTpmsRealTire[curPairPos].pairState = 0;
        bt_tpms_clean_failture(curPairPos);

        return false;
    }

    otherPos = (curPairPos == FRONT_TIRE) ? REAR_TIRE : FRONT_TIRE;

    ///< 同一个传感器不能同时配前轮和后轮: 配新的则清掉旧的
    if (strncmp(btTpmsRealTire[curPairPos].macAddr,
                btTpmsRealTire[otherPos].macAddr, 12) == 0)
    {
        reset_bt_tpms_mac_addr(otherPos);
        clean_bt_tpms_pair_data(otherPos);
        btTpmsRealTire[otherPos].pairState = 0;
    }

    btTpmsRealTire[curPairPos].pairState = 2;

    save_bt_tpms_mac(curPairPos, btTpmsRealTire[curPairPos].macAddr);

    return true;
}

BT_TPMS_TIRE_INFO_T* bt_tpms_get_data(uint8_t pos)
{
    if (pos == 0 || pos == 1)
    {
        return &btTpmsRealTire[pos];
    }

    return NULL;
}

///< 配对收尾原因
typedef enum
{
    BT_TPMS_PAIR_END_LEARNED = 0,  ///< 学习成功
    BT_TPMS_PAIR_END_CANCEL,       ///< 应用层主动停止
    BT_TPMS_PAIR_END_TIMEOUT,      ///< 1min 内未学到
} BT_TPMS_PAIR_END_E;

/**
 * @brief 配对流程收尾: 停定时器, 复位 curPairPos, 判定配对状态并落库
 * @param pos    本次配对的轮位
 * @param reason 收尾原因
 * @note  学到即停 / 主动停止 / 超时 三条路径都走这里, 保证 curPairPos 一定被复位
 */
static void bt_tpms_pair_settle(uint8_t pos, BT_TPMS_PAIR_END_E reason)
{
    uint8_t iRet;

    stop_bt_tpms_pair_timer();
    btTpmsTimeoutCnt = 0;
    curPairPos = -1;

    if (reason == BT_TPMS_PAIR_END_LEARNED)
    {
        ///< MAC 已在 bt_tpms_pair_learn_match() 里存放好
        btTpmsRealTire[pos].pairState = 2;
        pairOpResult = BT_TPMS_PAIR_STATE_SUCCESS;
    }
    else
    {
        ///< 没学到: 原有配对记录若在则保留, 否则退回未配对(超时记 3, 与主动取消区分)
        if (bt_tpms_mac_is_valid(pos))
        {
            btTpmsRealTire[pos].pairState = 2;
        }
        else
        {
            btTpmsRealTire[pos].pairState = (reason == BT_TPMS_PAIR_END_TIMEOUT) ? 3 : 0;
        }

        pairOpResult = (reason == BT_TPMS_PAIR_END_TIMEOUT) ? BT_TPMS_PAIR_STATE_TIMEOUT
                                                            : BT_TPMS_PAIR_STATE_IDLE;
    }

    iRet = bt_tpms_set_failure((pos == FRONT_TIRE) ? BT_TPMS_FRONT_NOT_PAIR : BT_TPMS_REAR_NOT_PAIR,
                               btTpmsRealTire[pos].pairState != 2);
    bt_tpms_failure_start(iRet);

    bt_tpms_publish_tire(pos);
}


/**
 * @brief 天城协议状态位 -> 本模块 faultStatus
 * @note  bit1(漏气) -> 1 慢漏气, bit0(充气配对) -> 4 加气;
 *        bit2(低电压) 不在此处体现, 由 battaryValue 独立判定
 */
static uint8_t bt_tpms_fault_from_status(uint8_t status)
{
    if (status & TC_TPMS_STATUS_PAIR_BIT1)
    {
        return 1;
    }

    if (status & TC_TPMS_STATUS_PAIR_BIT0)
    {
        return 4;
    }

    return 0;
}

static int bt_tpms_hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/**
 * @brief hex 文本 -> 字节流
 * @return 字节数, 0 表示非法
 */
static int bt_tpms_hex_to_bytes(const char *hex, int hex_len, uint8_t *out, int out_size)
{
    int n = 0;

    if (hex == NULL || out == NULL || (hex_len & 1))
    {
        return 0;
    }

    for (int i = 0; i < hex_len; i += 2)
    {
        int hi = bt_tpms_hex_val(hex[i]);
        int lo = bt_tpms_hex_val(hex[i + 1]);

        if (hi < 0 || lo < 0 || n >= out_size)
        {
            return 0;
        }

        out[n++] = (uint8_t)((hi << 4) | lo);
    }

    return n;
}

/**
 * @brief 遍历 ADV, 校验设备名并取出 TC.TPMS 厂商段
 * @return true: 是 TC.TPMS 胎压广播且字段完整
 */
static bool bt_tpms_parse_adv(const uint8_t *adv, int adv_len, TC_TPMS_INFO_T *info)
{
    bool name_matched = false;
    int i = 0;

    if (adv == NULL || info == NULL)
    {
        return false;
    }

    while (i + 1 < adv_len)
    {
        uint8_t len = adv[i];
        uint8_t type;
        const uint8_t *data;
        int data_len;

        if (len == 0)
        {
            break;
        }

        if (i + 1 + len > adv_len)
        {
            break;
        }

        type = adv[i + 1];
        data = &adv[i + 2];
        data_len = len - 1;

        if ((type == TC_TPMS_AD_TYPE_NAME_SHORT || type == TC_TPMS_AD_TYPE_NAME_COMPLETE)
            && data_len == (int)strlen(TC_TPMS_ADV_NAME)
            && memcmp(data, TC_TPMS_ADV_NAME, data_len) == 0)
        {
            name_matched = true;
        }
        else if (type == TC_TPMS_AD_TYPE_MANU && data_len >= TC_TPMS_MANU_LEN)
        {
            ///< FF 段: S / V / T / P(大端 2B) / MAC(低字节在前 6B)
            info->status = data[0];
            info->volt = data[1];
            info->temp = (int8_t)data[2];
            info->pressure = (uint16_t)((data[3] << 8) | data[4]);
            memcpy(info->mac, &data[5], 6);
        }

        i += len + 1;
    }

    return name_matched;
}

/**
 * @brief 解析 bt_module 送来的 "LP" 指示
 * @note  格式: LP<evt_type>,<addr_type>,<addr>,<primary_phy>,<secondary_phy>,
 *              <adv_sid>,<tx_power>,<rssi>,<periodic_adv_int>,<data_len>,<data>
 *        例: LP70013,00,C1C2C3C4C5C6,01,00,FF,7F,D4,0000,0B,02010607FF50A6E5133BC1
 */
static bool bt_tpms_parse_lp(const char *lp, TC_TPMS_INFO_T *info)
{
    const char *p;
    const char *field[11] = {0};
    int field_len[11] = {0};
    uint8_t adv[64];
    int adv_len;
    int idx = 0;

    if (lp == NULL || info == NULL)
    {
        return false;
    }

    p = strstr(lp, "LP");
    if (p == NULL)
    {
        return false;
    }
    p += 2;

    ///< 按 ',' 切分, 逐段取到 11 个字段; 最后一段以 '\r'/'\n'/'\0' 结尾
    for (idx = 0; idx < 11 && *p != '\0'; idx++)
    {
        field[idx] = p;

        while (*p != '\0' && *p != ',' && *p != '\r' && *p != '\n')
        {
            p++;
        }

        field_len[idx] = (int)(p - field[idx]);

        if (*p == ',')
        {
            p++;
        }
    }

    if (idx < 11 || field_len[10] <= 0)
    {
        return false;
    }

    adv_len = bt_tpms_hex_to_bytes(field[10], field_len[10], adv, sizeof(adv));
    if (adv_len <= 0)
    {
        return false;
    }

    return bt_tpms_parse_adv(adv, adv_len, info);
}

static void bt_tpms_analysis_data(const char *lp)
{
    ///< 必须零初始化: bt_tpms_parse_adv() 的返回值只表示设备名匹配,
    ///< 若该帧没有 FF 段(或段长不足), tc 不会被填任何字段;
    ///< 零值会让 status=0 在配对条件处被拦下, MAC 全 '0' 也查不到轮位
    TC_TPMS_INFO_T tc = {0};
    char macAddr[BT_TPMS_MAC_ADDR_LEN] = {0};
    int8_t pos;

    if (lp == NULL)
    {
        return;
    }

    if (!bt_tpms_parse_lp(lp, &tc))
    {
        ///< 非 TC.TPMS 广播 (或其他蓝牙设备的 ADV), 静默丢弃
        return;
    }

    rt_kprintf("bt <-- {%s}\n", lp);
    
    ///< ADV 里 MAC 低字节在前 -> 转成正常字节序字符串
    snprintf(macAddr, sizeof(macAddr), "%02X%02X%02X%02X%02X%02X",
             tc.mac[5], tc.mac[4], tc.mac[3], tc.mac[2], tc.mac[1], tc.mac[0]);

    if (bt_tpms_pair_learn_match(macAddr, tc.status))
    {
        ///< 学到即停: 收尾会复位 curPairPos, 所以先把轮位存下来
        pos = curPairPos;
        bt_tpms_pair_settle((uint8_t)pos, BT_TPMS_PAIR_END_LEARNED);
    }
    else
    {
        ///< 稳态: 按 MAC 反查数据归属轮位, 与 curPairPos 无关
        pos = bt_tpms_find_pos_by_mac(macAddr);

        if (pos < 0)
        {
            mw_logd("bt tpms: no paired pos for mac %s\n", macAddr);
            return;
        }
    }

    lastDataTick = rt_tick_get();
    btTpmsRealTire[pos].lostSensor = 0;
    btTpmsRealTire[pos].bootStale = 0;
    btTpmsRealTire[pos].lastRxTick = lastDataTick;

    btTpmsRealTire[pos].tpmsPressure = TC_TPMS_PSI10_TO_KPA(tc.pressure);
    btTpmsRealTire[pos].tpmsTemp = tc.temp;
    btTpmsRealTire[pos].battaryValue = tc.volt;
    btTpmsRealTire[pos].faultStatus = bt_tpms_fault_from_status(tc.status);

    mw_logd("pos:%d  pairState:%d\n", pos, btTpmsRealTire[pos].pairState);
    mw_logd("pressure:%dKpa, temp:%d\n", btTpmsRealTire[pos].tpmsPressure,
            btTpmsRealTire[pos].tpmsTemp);
    mw_logd("faultStatus:%d\n", btTpmsRealTire[pos].faultStatus);

    bt_tpms_failure_handler((uint8_t)pos, ANALYSIS_DATA_PROCESS);
}

static void stop_bt_tpms_pair(uint8_t pos, uint8_t cmd)
{
    if(REAR_TIRE != pos  &&  FRONT_TIRE != pos)
    {
        return;
    }

    if(btTpmsRealTire[pos].pairState != 1)
    {
        return;
    }

    uint8_t pairBuff[2] = {0};
    pairBuff[0] = 0x02;
    pairBuff[1] = pos;

    bt_tpms_task_add(pairBuff,sizeof(pairBuff), cmd);
}

static void set_bt_tpms_pair(uint8_t pos, uint8_t cmd)
{
    uint8_t otherPos;
    uint8_t pairBuff[2] = {0};

    if(REAR_TIRE != pos && FRONT_TIRE != pos)
    {
        return;
    }

    otherPos = (pos == FRONT_TIRE) ? REAR_TIRE : FRONT_TIRE;

    ///< 两轮不能同时学习: 先把另一轮停掉收尾, 再开始本轮
    ///< (原实现在这里是 return, 会把本轮卡在 pairState=1 但没定时器的状态)
    if(btTpmsRealTire[otherPos].pairState == 1)
    {
        stop_bt_tpms_pair(otherPos, BT_TPMS_STOP_PAIR_CMD);
    }

    btTpmsRealTire[pos].pairState = 1;

    pairBuff[0] = 0x01;
    pairBuff[1] = pos;

    bt_tpms_task_add(pairBuff, sizeof(pairBuff), cmd);
}

static void clean_bt_tpms_id(uint8_t pos, uint8_t cmd)
{
    if(REAR_TIRE != pos && FRONT_TIRE != pos)
    {
        return;
    }

    if(btTpmsRealTire[pos].pairState != 2)
    {

        return;
    }

    uint8_t pairBuff[2] = {0};
    pairBuff[0] = 0x03;
    pairBuff[1] = pos;

    bt_tpms_task_add(pairBuff, sizeof(pairBuff), cmd);
}


/**
 * @brief 外部轮位 (tpms_pos_e) -> 内部轮位 (FRONT_TIRE/REAR_TIRE)
 * @return -1: 本工程不支持的轮位
 */
static int8_t bt_tpms_pos_from_slot(tpms_pos_e slot)
{
    if (slot == TPMS_LEFT_FRONT)
    {
        return FRONT_TIRE;
    }

    if (slot == TPMS_LEFT_REAR)
    {
        return REAR_TIRE;
    }

    return -1;
}

int bt_tpms_pair_start(tpms_pos_e slot)
{
    int8_t pos = bt_tpms_pos_from_slot(slot);

    if (pos < 0)
    {
        rt_kprintf("bt tpms pair start: unsupported slot %d\n", (int)slot);
        return -1;
    }

    if (btTpmsRealTire[pos].pairState == 1)
    {
        ///< 该轮已在学习中, 幂等返回
        return 0;
    }

    ///< 扫描是稳态数据的唯一来源(常开), 这里幂等补发一次, 防止被外部关掉过
    bt_module_send_at_cmd("AT#LS61\r\n");

    set_bt_tpms_pair((uint8_t)pos, BT_TPMS_PAIR_CMD);

    return 0;
}

int bt_tpms_pair_stop(tpms_pos_e slot)
{
    int8_t pos = bt_tpms_pos_from_slot(slot);

    if (pos < 0)
    {
        return -1;
    }

    stop_bt_tpms_pair((uint8_t)pos, BT_TPMS_STOP_PAIR_CMD);

    return 0;
}

int bt_tpms_pair_clear(tpms_pos_e slot)
{
    int8_t pos = bt_tpms_pos_from_slot(slot);

    if (pos < 0)
    {
        return -1;
    }

    clean_bt_tpms_id((uint8_t)pos, BT_TPMS_CLEAN_ID_CMD);

    return 0;
}

bt_tpms_pair_state_e bt_tpms_pair_get_state(tpms_pos_e slot)
{
    int8_t pos = bt_tpms_pos_from_slot(slot);

    if (pos < 0)
    {
        return BT_TPMS_PAIR_STATE_IDLE;
    }

    if (btTpmsRealTire[pos].pairState == 1)
    {
        return BT_TPMS_PAIR_STATE_LEARNING;
    }

    ///< 本轮的配对流程刚结束且结果非"空闲"时以流程结果为准
    ///< (超时即使保留了旧记录也要报超时, 否则 UI 分不出学到没学到)
    if ((pos == pairOpPos) && (pairOpResult != BT_TPMS_PAIR_STATE_IDLE))
    {
        return pairOpResult;
    }

    return (btTpmsRealTire[pos].pairState == 2) ? BT_TPMS_PAIR_STATE_SUCCESS
                                                : BT_TPMS_PAIR_STATE_IDLE;
}

static void bt_tpms_pair_process(uint8_t cmd, uint8_t pos)
{
    switch(cmd)
    {
        case BT_TPMS_PAIR_CMD:
            btTpmsRealTire[pos].pairState = 1;
            curPairPos = pos;
            pairOpPos = (int8_t)pos;
            pairOpResult = BT_TPMS_PAIR_STATE_IDLE;
            btTpmsTimeoutCnt = 0;
            mw_logd("pos:%d recv bt tpms pair cmd.\n", pos);
            start_bt_tpms_pair_timer();
            break;

        case BT_TPMS_STOP_PAIR_CMD:
            ///< 收尾会复位 curPairPos, 所以这里传 pos 而不是 curPairPos
            bt_tpms_pair_settle(pos, BT_TPMS_PAIR_END_CANCEL);
            break;

        case BT_TPMS_CLEAN_ID_CMD:
            ///< 清记录与"正在进行的配对"互不相干, 不动定时器与 curPairPos
            btTpmsRealTire[pos].pairState = 0;
            clean_bt_tpms_pair_data(pos);

            if (pairOpPos == (int8_t)pos)
            {
                pairOpResult = BT_TPMS_PAIR_STATE_IDLE;
            }

            bt_tpms_publish_tire(pos);
            break;

        default:
            break;
    }

}

void bt_tpms_task_process(uint8_t *msg)
{
    uint8_t tpmsCmd = msg[BT_TPMS_CMD_DATA_OFFSET];

    switch(tpmsCmd)
    {
        case BT_TPMS_PAIR_CMD:
        case BT_TPMS_STOP_PAIR_CMD:
        case BT_TPMS_CLEAN_ID_CMD:
             bt_tpms_pair_process(tpmsCmd, msg[BT_TPMS_CMD_DATA_OFFSET + 2]);

            break;

        case BT_TPMS_ANALYSIS_DATA_CMD:
            bt_tpms_analysis_data((const char *)&msg[BT_TPMS_CMD_DATA_OFFSET + 1]);
            break;

        default:
            break;
    }

}

void bt_tpms_task_thread(void *param)
{
    uint8_t *msg;

    for (;;)
    {
        if (rt_mq_recv(btTpmsQueueHandle, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
        {
            rt_kprintf("%s[%d] %s: rt_mq_recv error.\n", __FILE__, __LINE__, __FUNCTION__);
            continue;
        }

        bt_tpms_task_process(msg);
        rt_free(msg);
        rt_thread_mdelay(1);
    }
}

int bt_tpms_task_add(uint8_t *data, uint8_t len, uint8_t cmd)
{
    if ((btTpmsQueueHandle == RT_NULL) || data == NULL)
    {
        return 0;
    }

    uint8_t *msg = rt_malloc(BT_TPMS_MSG_MAX_LEN);

    if (msg == NULL)
    {
        rt_kprintf("%s[%d] %s: is NULL.\n", __FILE__, __LINE__, __FUNCTION__);
        return 0;
    }

    memset(msg, 0, BT_TPMS_MSG_MAX_LEN);
    msg[0] = 0x81;
    msg[1] = 0x50;
    msg[2] = cmd;

    memcpy(&msg[3], data, len > BT_TPMS_MSG_MAX_LEN - 3 ? BT_TPMS_MSG_MAX_LEN - 3 : len);

    if (rt_mq_send(btTpmsQueueHandle,
                    &msg,
                     sizeof(msg)) != RT_EOK)
    {
        rt_free(msg);
        rt_kprintf("%s[%d] %s: rt_mq_send error.\n", __FILE__, __LINE__, __FUNCTION__);
        return 0;
    }

    return 1;
}


static void bt_tpms_pair_timer(void *parameter)
{
    btTpmsTimeoutCnt++;

    if(btTpmsTimeoutCnt >= BT_TPMS_PAIE_TIMEOUT_TIME)
    {
        ///< 超时收尾: settle 里已停表并清零计数
        if (curPairPos == FRONT_TIRE || curPairPos == REAR_TIRE)
        {
            bt_tpms_pair_settle((uint8_t)curPairPos, BT_TPMS_PAIR_END_TIMEOUT);
        }
        else
        {
            stop_bt_tpms_pair_timer();
            btTpmsTimeoutCnt = 0;
        }

        return;
    }

    ///< 一次性定时器手动重挂, 凑成周期性
    rt_timer_start(pairTimer);
}

/**
 * @brief 30min 无数据丢失报警 (车速 > 20km/h 起算)
 */
static void bt_tpms_lost_timer(void *parameter)
{
    rt_tick_t now = rt_tick_get();
    rt_tick_t timeout_tick = (rt_tick_t)BT_TPMS_LOST_TIMEOUT_TIME * RT_TICK_PER_SECOND;
    rt_tick_t save_period_tick = (rt_tick_t)BT_TPMS_SAVE_PERIOD_MS * RT_TICK_PER_SECOND / 1000;

    ///< 周期落盘: 已配对轮有新数据(lostTimer 在 bt_tpms 线程之外, 只置标志建线程)
    {
        bool hasNew = false;

        for (int pos = 0; pos < BT_TIRES_NUMBER; pos++)
        {
            if ((btTpmsRealTire[pos].pairState == 2)
                && (btTpmsRealTire[pos].lastRxTick > lastSaveTick))
            {
                hasNew = true;
                break;
            }
        }

        if (hasNew && ((now - lastSaveTick) >= save_period_tick))
        {
            lastSaveTick = now;
            bt_tpms_flush_pair_data();
        }
    }

    ///< 低速不计时, 并把计时起点跟着推后
    if (vehicle_get_data(VEH_SPEED_CURRENT) < BT_TPMS_LOST_SPEED_KMH)
    {
        lastDataTick = now;
        return;
    }

    if ((now - lastDataTick) < timeout_tick)
    {
        return;
    }

    for (int pos = 0; pos < BT_TIRES_NUMBER; pos++)
    {
        if (btTpmsRealTire[pos].pairState != 2)
        {
            continue;
        }

        if (btTpmsRealTire[pos].lostSensor == 0)
        {
            btTpmsRealTire[pos].lostSensor = 1;
            rt_kprintf("bt tpms pos:%d lost sensor!\n", pos);
            bt_tpms_publish_tire(pos);
        }
    }
}

int bt_tpms_task_init(void)
{
    rt_thread_t thread;

    if(btTpmsQueueHandle)
    {
        rt_kprintf("btTpmsQueueHandle is already inited.\n");
        return 0;
    }

    btTpmsQueueHandle = rt_mq_create("bt_tpms_mq", sizeof(uint8_t *), 10, RT_IPC_FLAG_FIFO);
    if (btTpmsQueueHandle == RT_NULL)
    {
        rt_kprintf("%s[%d] %s: create error.\n", __FILE__, __LINE__, __FUNCTION__);
        return -1;
    }

    thread = rt_thread_create("bt_tpms", bt_tpms_task_thread, RT_NULL,
                              BT_TPMS_THREAD_STACK_SIZE, BT_TPMS_THREAD_PRIO,
                              BT_TPMS_THREAD_TICK);
    if (thread == RT_NULL)
    {
        rt_mq_delete(btTpmsQueueHandle);
        btTpmsQueueHandle = RT_NULL;
        rt_kprintf("Create bt tpms thread failed.\n");
        return -1;
    }
    rt_thread_startup(thread);

    if(pairTimer == RT_NULL)
    {
        pairTimer = rt_timer_create("bt_tpms_pair", bt_tpms_pair_timer, RT_NULL,
                                    BT_TPMS_PAIR_TIMER_INTERVAL_PERIOD,
                                    RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);
        if(pairTimer == RT_NULL)
        {
            rt_kprintf("Create bt tpms pair timer failed.\n");
            return -1;
        }
    }

    if(lostTimer == RT_NULL)
    {
        lostTimer = rt_timer_create("bt_tpms_lost", bt_tpms_lost_timer, RT_NULL,
                                    BT_TPMS_LOST_TIMER_PERIOD,
                                    RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
        if(lostTimer == RT_NULL)
        {
            rt_kprintf("Create bt tpms lost timer failed.\n");
            return -1;
        }
        rt_timer_start(lostTimer);
    }

    ///< 上电先按持久化的 MAC 恢复配对状态
    set_bt_tpms_mac_addr();
    lastDataTick = rt_tick_get();

    return 0;
}

#endif /* MW_BT_TPMS_ENABLE */