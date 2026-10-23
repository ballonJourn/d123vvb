#include "mw_can_tx.h"
#include <rtthread.h>
#include <string.h>
#include "rtdevice.h"
#include "mw_can.h"
#include "usr_utils.h"
#include "mile_param.h"
#include "vehicle_param.h"
#include "set_param.h"      /* 续航参考里程数组 (usr_param 的 ref_mile_arr) */

#if MW_CAN_ENABLE

#define CAN_TX_ENABLE

#ifdef CAN_TX_ENABLE

#define CAN_TX_THREAD_PRIO (16)
#define CAN_TX_QUEUE_LEN (20)

typedef void (*tx_fun_handle)(rt_device_t can, void *param);

typedef struct {
    uint32_t id;
    uint64_t start;
    uint32_t cycle;
    tx_fun_handle fun;
    void *param;
} can_tx_cycle_t;

static rt_device_t can_dev;
static rt_mq_t can_tx_mq;
static rt_thread_t g_can_send_tid = RT_NULL;
static rt_thread_t g_can_cycle_tid = RT_NULL;
static rt_thread_t g_tcs_tx_tid = RT_NULL;
static rt_sem_t g_tcs_tx_sem = RT_NULL;

static void tx_451_msg_fun(rt_device_t can, void *param);
static void tx_452_msg_fun(rt_device_t can, void *param);
static void tx_453_msg_fun(rt_device_t can, void *param);
static void tx_454_msg_fun(rt_device_t can, void *param);
static void tx_455_msg_fun(rt_device_t can, void *param);
static void tx_456_msg_fun(rt_device_t can, void *param);
static void tcs_tx_thread(void *param);
int mq_can_send_msg(struct rt_can_msg * msg);

///< ifactive 触发型报文的待发指令 (0=无动作)
static volatile uint8_t s_tcs_command = 0;   ///< 0x451 TCS_Command: 0无 1开 2关
static volatile uint8_t s_bat_seat = 0;   ///< 0x451 Bat_Seat: 0无 1开
static volatile uint8_t s_power_down = 0;   ///< 0x451 Power_Down: 0无 1开
static volatile uint8_t s_tpms_action = 0;   ///< 0x452 胎压匹配动作码 0~4

static can_tx_cycle_t can_tx_cycle_msg[] = {
    // id,     start, cycle,     function,          param
    {0x453, 205, 200, tx_453_msg_fun, NULL},  ///< Display_Total_mileage 周期发送
    {0x454, 255, 200, tx_454_msg_fun, NULL},  ///< Display_mileage_Debug1 周期发送
    {0x455, 305, 200, tx_455_msg_fun, NULL},  ///< Display_mileage_Debug2 周期发送
    {0x456, 355, 200, tx_456_msg_fun, NULL},  ///< Display_mileage_Debug3 周期发送
};

static const uint8_t can_tx_cycle_count =
    sizeof(can_tx_cycle_msg) / sizeof(can_tx_cycle_msg[0]);

///< 0x453 Display_Total_mileage: 仪表->VCU, 周期 200ms
///  byte0-2 总里程(24bit LE, 取自 mile_param MILE_PARAM_ODO, 单位 m)
///  byte3-4 续航里程(16bit LE, 取自 VEH_MILEAGE_ENDURANCE_A, 单位 km)
static void tx_453_msg_fun(rt_device_t can, void *param) {
    struct rt_can_msg txmsg = {0};
    txmsg.ide = 0;
    txmsg.rtr = 0;
    txmsg.len = 8;
    txmsg.id = 0x453;

    uint32_t odo = 0;
    get_mile_param(MILE_PARAM_ODO, &odo);
    odo &= 0xFFFFFF;  ///< 24bit
    txmsg.data[0] = (uint8_t)(odo & 0xFF);
    txmsg.data[1] = (uint8_t)((odo >> 8) & 0xFF);
    txmsg.data[2] = (uint8_t)((odo >> 16) & 0xFF);

    uint16_t range = (uint16_t)vehicle_get_data(VEH_MILEAGE_ENDURANCE_A);
    txmsg.data[3] = (uint8_t)(range & 0xFF);
    txmsg.data[4] = (uint8_t)((range >> 8) & 0xFF);

    mq_can_send_msg(&txmsg);
}

///< 0x454~0x456 三帧调试信号取 1 号包(固定包, 0x307)的数据:
///< 该组信号只需一份数组, 固定包常驻而换电包会插拔, 基准更稳
#define REF_MILE_DEBUG_PACK (1)

///< 读取指定包的续航参考里程数组 (8bit × 0.01km)
///< 用户参数未就绪时填 0 并返回 false, 与整帧占位行为一致
static bool ref_mile_arr_read(uint8_t pack, uint8_t *arr) {
    if (!get_usr_param((usr_param_handle_e)(USR_PARAM_REF_MILE_ARR_P1 + pack - 1), arr)) {
        memset(arr, 0, REF_MILE_ARR_NUM);
        return false;
    }
    return true;
}

///< 数组均值 (0.01km 编码, 四舍五入)
static uint8_t ref_mile_arr_avg(const uint8_t *arr) {
    uint16_t sum = 0;
    uint8_t i;

    for (i = 0; i < REF_MILE_ARR_NUM; i++) {
        sum += arr[i];
    }

    return (uint8_t)((sum + REF_MILE_ARR_NUM / 2) / REF_MILE_ARR_NUM);
}

///< 0x454 Display_mileage_Debug1: 仪表->VCU, 周期 200ms
///  byte0 avg_RefMile_k2: 平均值, 8bit, 单位 0.01Km
///  数据源: 1号包续航参考里程数组 (set_param 的 ref_mile_arr)
static void tx_454_msg_fun(rt_device_t can, void *param) {
    struct rt_can_msg txmsg = {0};
    uint8_t arr[REF_MILE_ARR_NUM] = {0};

    txmsg.ide = 0;
    txmsg.rtr = 0;
    txmsg.len = 8;
    txmsg.id = 0x454;

    ref_mile_arr_read(REF_MILE_DEBUG_PACK, arr);
    txmsg.data[0] = ref_mile_arr_avg(arr);

    mq_can_send_msg(&txmsg);
}

///< 0x455 Display_mileage_Debug2: 仪表->VCU, 周期 200ms
///  byte0-1 CountMile_Sum: 单个SOC内的计算里程, 16bit LE, 单位 0.01Km
///  byte2   avg_RefMile:   平均参考里程, 8bit, 单位 0.01Km
///  byte3-7 RefMile1~5:    参考里程 1~5, 各 8bit, 单位 0.01Km
///  CountMile_Sum 取自 VEH_REF_MILE_COUNT_MILE (纯运行时量, 不经 JSON)
static void tx_455_msg_fun(rt_device_t can, void *param) {
    struct rt_can_msg txmsg = {0};
    uint8_t arr[REF_MILE_ARR_NUM] = {0};
    uint16_t count_mile;

    txmsg.ide = 0;
    txmsg.rtr = 0;
    txmsg.len = 8;
    txmsg.id = 0x455;

    ref_mile_arr_read(REF_MILE_DEBUG_PACK, arr);
    count_mile = (uint16_t)vehicle_get_data(VEH_REF_MILE_COUNT_MILE);

    txmsg.data[0] = (uint8_t)(count_mile & 0xFF);
    txmsg.data[1] = (uint8_t)((count_mile >> 8) & 0xFF);
    txmsg.data[2] = ref_mile_arr_avg(arr);
    txmsg.data[3] = arr[0];
    txmsg.data[4] = arr[1];
    txmsg.data[5] = arr[2];
    txmsg.data[6] = arr[3];
    txmsg.data[7] = arr[4];

    mq_can_send_msg(&txmsg);
}

///< 0x456 Display_mileage_Debug3: 仪表->VCU, 周期 200ms
///  byte0-4 RefMile6~10: 参考里程 6~10, 各 8bit, 单位 0.01Km
///  byte5-7 未定义, 填 0
static void tx_456_msg_fun(rt_device_t can, void *param) {
    struct rt_can_msg txmsg = {0};
    uint8_t arr[REF_MILE_ARR_NUM] = {0};

    txmsg.ide = 0;
    txmsg.rtr = 0;
    txmsg.len = 8;
    txmsg.id = 0x456;

    ref_mile_arr_read(REF_MILE_DEBUG_PACK, arr);
    txmsg.data[0] = arr[5];
    txmsg.data[1] = arr[6];
    txmsg.data[2] = arr[7];
    txmsg.data[3] = arr[8];
    txmsg.data[4] = arr[9];

    mq_can_send_msg(&txmsg);
}

///< 0x451 Meter_TCS: 仪表->TCS, ifactive, 触发后 10ms 连发 3 帧
///  byte0 TCS_Command: 0无动作 1打开 2关闭
///  byte1 Bat_Seat: 0无 1开
///  byte2 Power_Down: 0无 1开
static void tx_451_msg_fun(rt_device_t can, void *param) {
    uint8_t tcs, seat, down;

    /* 快照与清零原子化, 防止 setter 在新值写入的窗口内被清零抹掉;
     * ifactive 事件型: 每次触发只发本次事件, 发完自复位, 不残留旧值 */
    rt_enter_critical();
    tcs  = s_tcs_command;
    seat = s_bat_seat;
    down = s_power_down;
    s_tcs_command = s_bat_seat = s_power_down = 0;
    rt_exit_critical();

    if (tcs == 0 && seat == 0 && down == 0) {
        return;   ///< ifactive: 无事件不发帧
    }

    struct rt_can_msg txmsg = {0};
    txmsg.ide = 0;
    txmsg.rtr = 0;
    txmsg.len = 8;
    txmsg.id = 0x451;
    txmsg.data[0] = tcs;
    txmsg.data[1] = seat;
    txmsg.data[2] = down;

    ///< 10ms 间隔连发 3 帧
    for (uint8_t i = 0; i < 3; i++) {
        mq_can_send_msg(&txmsg);
        rt_thread_mdelay(10);
    }
}

///< 0x452 Meter_TPMS: 仪表->胎压, ifactive, 触发后连发 3 帧
///  byte0 匹配动作码 3bit: 0空闲 1前轮 2后轮 3前后 4中断
static void tx_452_msg_fun(rt_device_t can, void *param) {
    uint8_t act = s_tpms_action;
    if (act == 0) {
        return;
    }
    struct rt_can_msg txmsg = {0};
    txmsg.ide = 0;
    txmsg.rtr = 0;
    txmsg.len = 8;
    txmsg.id = 0x452;
    txmsg.data[0] = act & 0x07;

    for (uint8_t i = 0; i < 3; i++) {
        mq_can_send_msg(&txmsg);
        rt_thread_mdelay(10);
    }
    s_tpms_action = 0;
}

///< 触发 setter: 上层调用以请求发送 0x451/0x452
///< 0x451 已从周期轮询表注销, 改为事件驱动: 置标志后唤醒 tcs_tx 线程连发 3 帧
void can_tx_set_tcs_command(uint8_t cmd) {
    s_tcs_command = cmd;
    if (g_tcs_tx_sem)
        rt_sem_release(g_tcs_tx_sem);
}

void can_tx_set_bat_seat(uint8_t cmd) {
    s_bat_seat = cmd;
    if (g_tcs_tx_sem)
        rt_sem_release(g_tcs_tx_sem);
}

void can_tx_set_power_down(uint8_t cmd) {
    s_power_down = cmd;
    if (g_tcs_tx_sem)
        rt_sem_release(g_tcs_tx_sem);
}

void can_tx_set_tpms_action(uint8_t act) { s_tpms_action = act; }

///< 0x451 触发发送线程: 睡在信号量上, 被 can_tx_set_tcs/bat_seat/power_down 唤醒后连发 3 帧
static void tcs_tx_thread(void *param) {
    while (1) {
        if (rt_sem_take(g_tcs_tx_sem, RT_WAITING_FOREVER) != RT_EOK)
            continue;
        /* 复用现有发送逻辑: 快照三信号 -> 清标志 -> 3×mq 入队(10ms) */
        tx_451_msg_fun(can_dev, NULL);
    }
}

static void can_send_thread(void *param) {
    struct rt_can_msg *msg;
    rt_size_t  size;

    while (1) {
        if (rt_mq_recv(can_tx_mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK) {
            rt_kprintf("recv can_tx_mq error\n");
            continue;
        }

        size = rt_device_write(can_dev, 0, msg, sizeof(struct rt_can_msg));
        if (size != sizeof(struct rt_can_msg)) {
            // rt_kprintf("can dev write data failed!\n");
            if (msg) {
                rt_free(msg);
                msg = NULL;
            }
            continue;
        }

        rt_free(msg);
        rt_thread_mdelay(1);
    }

    rt_device_close(can_dev);
}

static void can_tx_cycle_msg_proc(void) {
    static uint32_t last_execution_time = 0;
    uint32_t current_time = rt_tick_get();
    
    if (current_time == last_execution_time) {
        return;
    }
    
    last_execution_time = current_time;
    for (uint8_t i = 0; i < can_tx_cycle_count; i++) {
        can_tx_cycle_t *p = &can_tx_cycle_msg[i];
        if (p) {
            if (current_time >= p->start) {
                p->start = current_time + p->cycle; 
                p->fun(can_dev, p->param);
            }
        }
    }
}

int mq_can_send_msg(struct rt_can_msg * msg) {
    if (msg == NULL || can_tx_mq == NULL) {
        return -1;
    }

    struct rt_can_msg *can_send_msg = 
        (struct rt_can_msg *)rt_malloc(sizeof(struct rt_can_msg));
    if (can_send_msg == NULL) {
        rt_kprintf("rt malloc can send msg failed!\r\n");
        return -1;
    }

    memcpy(can_send_msg, msg, sizeof(struct rt_can_msg));
    if (rt_mq_send_wait(can_tx_mq, &can_send_msg, sizeof(can_send_msg), 20) != RT_EOK) {
        rt_kprintf("mq_can_send_msg error\n");
        /* 入队失败要释放本次 rt_malloc 的堆块; msg 是调用者传入的指针
         * (多为函数内栈上结构体), 释放它会毁堆并泄漏 can_send_msg */
        rt_free(can_send_msg);
        return -1;
    }

    return 0;
}

static void can_tx_thread(void *param) {
    rt_thread_mdelay(5000);
#if 0
    static uint32_t tick = 0;
    tick = rt_tick_get();
#endif
      while (1) {
#if  0
        if (rt_tick_get() - tick <= rt_tick_from_millisecond(10000)) {
            continue;
        }
#endif
        can_tx_cycle_msg_proc();
        rt_thread_mdelay(1);
    }
}

int can_tx_cycle_init(void) {
    g_can_cycle_tid = rt_thread_create("can_cycle", can_tx_thread, RT_NULL,
                                  1024, CAN_TX_THREAD_PRIO + 1, 10);
    if (g_can_cycle_tid != RT_NULL) {
        rt_thread_startup(g_can_cycle_tid);
    } else {
        rt_kprintf("create cycle thread failed!\n");
        return -1;
    }

    return 0;
}

#endif

int can_tx_init(rt_device_t can) {
    if (can == NULL) {
        return -1;
    }

#ifdef CAN_TX_ENABLE
    can_dev = can;

    ///< 设置can发送中断使能
    rt_device_control(can_dev, RT_DEVICE_CTRL_SET_INT, NULL);

    can_tx_mq = rt_mq_create("can_tx_mq", sizeof(int),
            CAN_TX_QUEUE_LEN, RT_IPC_FLAG_PRIO);
    if (can_tx_mq == NULL) {
        rt_kprintf("create can_tx_mq failed\n");
        return -1;
    }

#if 1
    g_can_send_tid = rt_thread_create("can_send", can_send_thread, RT_NULL,
                                  1024, CAN_TX_THREAD_PRIO, 10);
    if (g_can_send_tid != RT_NULL) {
        rt_thread_startup(g_can_send_tid);
    } else {
        rt_kprintf("create can_tx thread failed!\n");
        return -1;
    }

    can_tx_cycle_init();

    ///< TCS 触发发送: 事件驱动, 替代已注销的 0x451 周期轮询
    g_tcs_tx_sem = rt_sem_create("tcs_tx", 0, RT_IPC_FLAG_PRIO);
    if (g_tcs_tx_sem == NULL) {
        rt_kprintf("create tcs_tx_sem failed\n");
        return -1;
    }
    g_tcs_tx_tid = rt_thread_create("tcs_tx", tcs_tx_thread, RT_NULL,
                                 1024, CAN_TX_THREAD_PRIO + 1, 10);
    if (g_tcs_tx_tid != RT_NULL) {
        rt_thread_startup(g_tcs_tx_tid);
    } else {
        rt_kprintf("create tcs_tx thread failed!\n");
        return -1;
    }
#endif
#endif

    return 0;
}

#ifdef CAN_TX_ENABLE
/* 休眠前清理 TX 通道:
 *   - cycle 线程: 循环里只 mdelay, delete 安全
 *   - send 线程: 阻塞在 rt_mq_recv(WAITING_FOREVER) 上, delete 安全
 *   - tcs_tx 线程: 阻塞在 rt_sem_take(WAITING_FOREVER) 上, delete 安全; 信号量随后删除
 *   - tx_mq 删除前先 reset 把残留消息释放, 防止 send 线程刚好抢到一条 msg
 *     还没 free 就被 delete (此处先 delete 线程再 delete mq, 顺序保证)
 *   - can_dev 不在这里关, 由 mw_can_sleep 统一关
 */
void can_tx_sleep(void) {
    if (g_can_cycle_tid != RT_NULL) {
        rt_thread_delete(g_can_cycle_tid);
        g_can_cycle_tid = RT_NULL;
    }
    if (g_can_send_tid != RT_NULL) {
        rt_thread_delete(g_can_send_tid);
        g_can_send_tid = RT_NULL;
    }
    if (g_tcs_tx_tid != RT_NULL) {
        rt_thread_delete(g_tcs_tx_tid);
        g_tcs_tx_tid = RT_NULL;
    }
    if (g_tcs_tx_sem != NULL) {
        rt_sem_delete(g_tcs_tx_sem);
        g_tcs_tx_sem = RT_NULL;
    }

    /* 清空残留 msg 指针 (rt_malloc 出来的), 防止内存泄漏 */
    if (can_tx_mq != NULL) {
        struct rt_can_msg *msg;
        while (rt_mq_recv(can_tx_mq, &msg, sizeof(msg), 0) == RT_EOK) {
            if (msg) rt_free(msg);
        }
        rt_mq_delete(can_tx_mq);
        can_tx_mq = NULL;
    }

    can_dev = NULL;
}
#else
void can_tx_sleep(void) { /* nothing */ }
#endif

#endif