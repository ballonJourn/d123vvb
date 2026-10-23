/*
 * 蓝牙模块 (GOC BTstack / blueware)
 *
 * 预编译库 third/btstack/lib/libgoc_btstack.a 提供蓝牙协议栈主体, 应用侧的
 * 收发通过 bt_sw_cfg 里的两个 RT-Thread 消息队列完成:
 *
 *   tx_queue : 应用 -> blueware  (AT 指令,       "AT#xx\r\n")
 *   rx_queue : blueware -> 应用  (响应/指示,     "\r\nXX..\r\n")
 *
 * 队列每槽固定 AT_CMD_PAYLOAD_LEN(4096) 字节, 文本放在开头, 后面补 0,
 * 与栈内消费方(btstack_entry.c)的约定一致。
 *
 * AT 指令集与指示码见供应商随库提供的《指令文档.md》, 不在此重复。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <wlan_mgnt.h>
#include "aic_osal.h"
#include "bt_module.h"
#include "mw_log.h"
#include "fscbt_interface.h"

#if MW_BT_TPMS_ENABLE
#include "bt_tpms.h"
#endif

#if MW_BT_MODULE_ENABLE

/* 是否在开机后自动下发一串 AT 指令做通路自检(原 main.c 的 tx demo) */
#define MW_BT_AT_DEMO_ENABLE   0

/* aic8800 的 STA/AP 是同一颗芯片的两个虚拟接口(wlan0/wlan1, 见 rtconfig.h 的
 * RT_WLAN_DEVICE_STA_NAME/AP_NAME)。打开任意一个都会跑 aic8800_init()
 * -> wifi_if_sdio_init(), 而蓝牙控制器(aicbt_init + BT 固件下载)正是在 WiFi
 * 驱动初始化里被拉起的, 所以必须先拉起 wlan, 否则 fscbt_init 拿到的控制器没上电。
 * 用 STA 口是为了不和亿连投屏的 AP 口(wlan1)抢同一个接口。
 *
 * 注意: wlan_init(aic8800_init -> wifi_if_sdio_init)是每设备各跑一次的, 所以
 * MW_BT_MODULE_ENABLE 与 MW_CARLINK_EC_ENABLE 同时打开时, 两个模块都会去注册
 * SDIO 驱动。目前亿连默认关闭; 若要同时开, 需要改成"谁先起来谁负责拉起 wlan,
 * 另一个先探测设备是否已初始化再决定要不要 set_mode"。 */
#define BT_WLAN_PWR_ON_DELAY_MS    4000    /* 等 aic8800 SDIO 上电稳定 */
#define BT_WLAN_UP_DELAY_MS        6000    /* 等 wlan 驱动 + BT 固件加载完成 */

#define BT_STACK_THREAD_NAME       "btStack_goc"
#define BT_STACK_THREAD_STACK      8192
#define BT_STACK_THREAD_PRIO       10
#define BT_STACK_THREAD_TICK       10

#define BT_RX_THREAD_NAME          "bt_rx_demo"
#define BT_TX_THREAD_NAME          "bt_tx_demo"
#define BT_QUEUE_THREAD_STACK      2048
#define BT_QUEUE_THREAD_PRIO       11
#define BT_QUEUE_THREAD_TICK       10

/* 蓝牙控制器挂在 uart2 上, 由 blueware 自己配置 uart, 这里只告诉它设备名 */
#define BT_UART_DEV_NAME           "uart2"

/*
 * 两个 AT 队列的消息池合计 480KB(rx 100 条 + tx 20 条), 用 rt_mq_create()
 * 会从默认堆(heap_sys, 只有 1MB SRAM)里拿, 会把 SRAM 堆吃紧; 这里改用
 * rt_mq_init() 自带池, 池从 heap_psram_sw 申请。
 *
 * 槽大小按 rt_mq_init() 的算法算: (RT_ALIGN(msg_size) + 一个链表指针),
 * 与 rt_mq_create() 完全一致, 所以队列深度不变
 * (见 kernel/rt-thread/src/ipc.c 的 rt_mq_init/rt_mq_create)。
 */
#define BT_MQ_SLOT_SIZE  (RT_ALIGN(AT_CMD_PAYLOAD_LEN, RT_ALIGN_SIZE) + sizeof(void *))
#define BT_TX_POOL_SIZE  (BT_MQ_SLOT_SIZE * AT_CMD_TX_QUEUE_LEN)
#define BT_RX_POOL_SIZE  (BT_MQ_SLOT_SIZE * AT_CMD_RX_QUEUE_LEN)

/* 队列控制块留在 SRAM(访问频繁), 只有消息池放 PSRAM */
static struct rt_messagequeue tx_mq;
static struct rt_messagequeue rx_mq;

static rt_mq_t tx_queue = RT_NULL;
static rt_mq_t rx_queue = RT_NULL;

/* 栈上报 "IS"(栈启动完成)后置 1, 上层才可安全下发指令 */
static volatile int g_bt_stack_up = 0;

/*
 * bt_module_send_at_cmd() - 应用 -> blueware, 走 bt_sw_cfg.tx_queue。
 *
 * 把 "AT#xx\r\n" 按队列槽大小(AT_CMD_PAYLOAD_LEN)补齐后入队。栈自己会去取
 * (scooter_hfp_app.c 里的 AT-command 轮询定时器), 响应异步从 rx_queue 回来。
 */
rt_err_t bt_module_send_at_cmd(const char *atcmd)
{
    rt_err_t ret;
    char *buf = rt_malloc(AT_CMD_PAYLOAD_LEN);

    if (buf == RT_NULL) {
        mw_loge("bt send at cmd: out of memory\n");
        return -RT_ENOMEM;
    }

    rt_memset(buf, 0, AT_CMD_PAYLOAD_LEN);
    rt_strncpy(buf, atcmd, AT_CMD_PAYLOAD_LEN - 1);

    ret = rt_mq_send(tx_queue, buf, AT_CMD_PAYLOAD_LEN);
    rt_free(buf);

    if (ret != RT_EOK) {
        mw_loge("bt --> {%s} FAILED (tx_queue full, %d)\n", atcmd, (int)ret);
        return ret;
    }

    mw_logd("bt --> {%s}\n", atcmd);
    return RT_EOK;
}

/*
 * bt_rx_entry() - blueware -> 应用, 走 bt_sw_cfg.rx_queue。
 *
 * 阻塞在 rx_queue 上, 打印栈上报的每条响应/指示。开机会依次收到
 * IS(栈启动完成)、MW<版本>、MM<名称>、MN<PIN>、MF<自动配置>。
 */
static void bt_rx_entry(void *parameter)
{
    char *buf = rt_malloc(AT_CMD_PAYLOAD_LEN);

    (void)parameter;
    if (buf == RT_NULL) {
        rt_kprintf("bt rx: out of memory\n");
        return;
    }

    while (1) {
        rt_memset(buf, 0, AT_CMD_PAYLOAD_LEN);
        if (rt_mq_recv(rx_queue, buf, AT_CMD_PAYLOAD_LEN, RT_WAITING_FOREVER) != RT_EOK)
            continue;

        buf[AT_CMD_PAYLOAD_LEN - 1] = '\0';
        // rt_kprintf("bt <-- {%s}\n", buf);

        /* 指示 "IS": 蓝牙栈启动完成 */
        if (rt_strstr(buf, "IS") != RT_NULL)
            g_bt_stack_up = 1;

#if MW_BT_TPMS_ENABLE
        /* 指示 "LP": TPMS BLE 扫描上报, 交给 bt_tpms 做协议解析 */
        {
            char *ind = buf;

            while (*ind == '\r' || *ind == '\n')
                ind++;

            if (rt_strncmp(ind, "LP", 2) == 0) {
                rt_size_t len = rt_strlen(ind);

                /* bt_tpms_task_add() 的长度参数是 uint8_t */
                if (len > 255)
                    len = 255;

                bt_tpms_task_add((uint8_t *)ind, (uint8_t)len,
                                 BT_TPMS_ANALYSIS_DATA_CMD);
            }
        }
#endif
    }
}

/*
 * bt_tx_entry() - 等在栈起来后, 下发一串 AT 指令做通路自检。
 * 回复由 bt_rx_entry() 打印。正式版本可关掉 MW_BT_AT_DEMO_ENABLE,
 * 改由业务层调用 bt_module_send_at_cmd()。
 */
static void bt_tx_entry(void *parameter)
{
    (void)parameter;

    while (!g_bt_stack_up)
        rt_thread_mdelay(100);

    mw_logi("bt tx: stack up, starting AT command demo\n");
    rt_thread_mdelay(200);

#if MW_BT_TPMS_ENABLE
    /*
     * 启动 TPMS BLE 扫描。
     * AT#LS5,<interval>,<window>: 扫描间隔/窗口, 单位 0.625ms, 取 64 -> 40ms;
     * 胎压协议建议扫描全开(窗口=间隔)。AT#LS61 开扫描, 之后经 rx_queue 上报 LP。
     */
    bt_module_send_at_cmd("AT#LS5,64,64\r\n");
    rt_thread_mdelay(100);
    bt_module_send_at_cmd("AT#LS61\r\n");
    rt_thread_mdelay(100);
#endif

#if MW_BT_AT_DEMO_ENABLE
    bt_module_send_at_cmd("AT#MY\r\n");   /* 查询固件版本   -> MW... */
    rt_thread_mdelay(100);
    bt_module_send_at_cmd("AT#DF\r\n");   /* 查询本机地址   -> DB... */
    rt_thread_mdelay(100);
    bt_module_send_at_cmd("AT#MX\r\n");   /* 导出配对列表   -> MX/OK */
    rt_thread_mdelay(100);
    bt_module_send_at_cmd("AT#CA\r\n");   /* 进入配对模式   -> II + OK */
#endif

    mw_logi("bt tx: command sequence sent\n");
}

static void bt_queue_threads_start(void)
{
    rt_thread_t rx_thread, tx_thread;

    rx_thread = rt_thread_create(BT_RX_THREAD_NAME, bt_rx_entry, RT_NULL,
                                 BT_QUEUE_THREAD_STACK, BT_QUEUE_THREAD_PRIO,
                                 BT_QUEUE_THREAD_TICK);
    if (rx_thread != RT_NULL)
        rt_thread_startup(rx_thread);
    else
        mw_loge("create bt_rx thread failed!\n");

    tx_thread = rt_thread_create(BT_TX_THREAD_NAME, bt_tx_entry, RT_NULL,
                                 BT_QUEUE_THREAD_STACK, BT_QUEUE_THREAD_PRIO,
                                 BT_QUEUE_THREAD_TICK);
    if (tx_thread != RT_NULL)
        rt_thread_startup(tx_thread);
    else
        mw_loge("create bt_tx thread failed!\n");
}

/*
 * bt_stack_entry() - 蓝牙栈主线程。
 *
 * wlan 拉起和固件加载都比较慢(秒级), 所以初始化不放在 mw_init() 里同步做,
 * 而是整个搬进本线程, 避免阻塞 app 初始化阶段(会拖慢开机动画)。
 */
static void bt_stack_entry(void *parameter)
{
    bt_hw_cfg_t bt_hw_cfg = {0, BT_UART_DEV_NAME, 2, 0, 0, 0};
    bt_sw_cfg_t bt_sw_cfg;
    void *tx_pool, *rx_pool;

    (void)parameter;

    /* aic8800 上电稳定后再拉起 wlan 驱动 */
    rt_thread_mdelay(BT_WLAN_PWR_ON_DELAY_MS);
    if (rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION) != RT_EOK) {
        mw_loge("bt wlan set mode failed!\n");
        return;
    }
    rt_thread_mdelay(BT_WLAN_UP_DELAY_MS);

    /* 消息池从 heap_psram_sw 申请, 不占 SRAM 的 heap_sys */
    tx_pool = aicos_malloc(MEM_PSRAM_SW, BT_TX_POOL_SIZE);
    rx_pool = aicos_malloc(MEM_PSRAM_SW, BT_RX_POOL_SIZE);
    if ((tx_pool == RT_NULL) || (rx_pool == RT_NULL)) {
        mw_loge("bt mq pool alloc error (tx %d, rx %d bytes)\n",
                BT_TX_POOL_SIZE, BT_RX_POOL_SIZE);
        aicos_free(MEM_PSRAM_SW, tx_pool);
        aicos_free(MEM_PSRAM_SW, rx_pool);
        return;
    }

    /* 应用 -> blueware 的 AT 指令队列 */
    rt_mq_init(&tx_mq, "bt_tx_mq", tx_pool, AT_CMD_PAYLOAD_LEN,
               BT_TX_POOL_SIZE, RT_IPC_FLAG_FIFO);
    /* blueware -> 应用的响应队列 */
    rt_mq_init(&rx_mq, "bt_rx_mq", rx_pool, AT_CMD_PAYLOAD_LEN,
               BT_RX_POOL_SIZE, RT_IPC_FLAG_FIFO);
    tx_queue = &tx_mq;
    rx_queue = &rx_mq;

    bt_sw_cfg.tx_queue            = tx_queue;
    bt_sw_cfg.rx_queue            = rx_queue;
    bt_sw_cfg.a2dp_resampler      = 1;
    bt_sw_cfg.debug_mode          = 0;
    bt_sw_cfg.ble_connection_type = BLE_ERYA_CONNECTION;
    bt_sw_cfg.ancs_enable         = 0;
    bt_sw_cfg.absvol_enable       = 0;   /* 1: 使能 AT+ABSVOL */
    bt_sw_cfg.module_type         = FSC_MODULE_BW121;

    /* 注意: 与源工程 main.c 一致 —— 返回 0 视为失败 */
    if (fscbt_init(&bt_hw_cfg, &bt_sw_cfg) == 0) {
        mw_loge("fscbt_init error\n");
        return;
    }

    /* 起队列收发线程: rx 打印 + tx 驱动 */
    bt_queue_threads_start();

    while (1) {
        fscbt_run();
    }
}

int bt_module_init(void)
{
    rt_thread_t bt_thread;

    bt_thread = rt_thread_create(BT_STACK_THREAD_NAME, bt_stack_entry, RT_NULL,
                                 BT_STACK_THREAD_STACK, BT_STACK_THREAD_PRIO,
                                 BT_STACK_THREAD_TICK);
    if (bt_thread == RT_NULL) {
        mw_loge("create bt stack thread failed!\n");
        return -1;
    }
    rt_thread_startup(bt_thread);

    return 0;
}

#endif