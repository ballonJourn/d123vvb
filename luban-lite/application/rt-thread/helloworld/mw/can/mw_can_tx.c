#include "mw_can_tx.h"
#include <rtthread.h>
#include <string.h>
#include "rtdevice.h"
#include "mw_can.h"
#include "usr_utils.h"

#if MW_CAN_ENABLE

//#define CAN_TX_ENABLE

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

static void tx_450_msg_fun(rt_device_t can, void *param);
int mq_can_send_msg(struct rt_can_msg * msg);

static can_tx_cycle_t can_tx_cycle_msg[] = {
    // id,     start, cycle,     function,          param
    {0x450, 5, 100, tx_450_msg_fun, NULL},
};

static const uint8_t can_tx_cycle_count = 
    sizeof(can_tx_cycle_msg) / sizeof(can_tx_cycle_msg[0]);

static void tx_450_msg_fun(rt_device_t can, void *param) {
    if (can == NULL) {
        rt_kprintf("can dev write data failed!\n");
    }

    static uint8_t tcs_on_of_status = 0;
    struct rt_can_msg txmsg = {0};
    txmsg.ide = 0;
    txmsg.rtr = 0;
    txmsg.len = 8;
    txmsg.id = 0x450;

    tcs_on_of_status = !tcs_on_of_status;
    txmsg.data[0] = tcs_on_of_status;

#if 0
    rt_size_t  size;
    size = rt_device_write(can_dev, 0, &txmsg, sizeof(txmsg));
    if (size != sizeof(txmsg)) {
        rt_kprintf("can dev write data failed!\n");
    }
#else
    mq_can_send_msg(&txmsg);
#endif
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
            rt_kprintf("can dev write data failed!\n");
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
        rt_free(msg);
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
    rt_thread_t thread;
    thread = rt_thread_create("can_cycle", can_tx_thread, RT_NULL,
                                  1024, CAN_TX_THREAD_PRIO + 1, 10);
    if (thread != RT_NULL) {
        rt_thread_startup(thread);
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
    rt_thread_t thread;
    thread = rt_thread_create("can_send", can_send_thread, RT_NULL,
                                  1024, CAN_TX_THREAD_PRIO, 10);
    if (thread != RT_NULL) {
        rt_thread_startup(thread);
    } else {
        rt_kprintf("create can_tx thread failed!\n");
        return -1;
    }

    can_tx_cycle_init();
#endif
#endif

    return 0;
}

#endif