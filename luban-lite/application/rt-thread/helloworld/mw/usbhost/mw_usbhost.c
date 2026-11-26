/*
 * mw_usbhost.c
 *
 * 强符号 override：覆盖 cherryusb 自带 demo/usbh_msc_template.c 的
 * __WEAK usbh_msc_run / usbh_msc_stop。U 盘插入后自动注册 RT-Thread
 * 块设备 "udisk" 并挂载到 /udisk；拔出时反向清理。
 *
 * 关键约束：
 * - usbh_msc_run  在 cherryusb 枚举线程里被调用（持 hport mutex），必须非阻塞。
 * - usbh_msc_stop 在 cherryusb 断开线程里被调用（持 hport mutex），必须非阻塞。
 * - 所有阻塞操作（scsi_init、读 MBR、dfs_mount/unmount）都在单实例
 *   worker 线程里串行执行：
 *     · 插入事件 → 静默 500ms 后挂载（合并快速插拔的多条事件）；
 *     · 拔出事件 → 立即卸载（先等 DVR 线程关闭 elene，再 unmount）。
 *
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 * SPDX-License-Identifier: Apache-2.0
 */
#include "dev_config.h"

#if MW_CHEERYUSB_ENABLE

#include <rtthread.h>
#include <dfs_fs.h>
#include <rtdevice.h>

#include "usbh_core.h"
#include "usbh_msc.h"
#include "usb_osal.h"
#include "mw_usbhost.h"

#define DEV_NAME  "udisk"
#define MNT_NAME  "/udisk"

///< USB 5V 使能脚（高有效），必须在 usbh_init 之前拉高
#define MW_USB_VBUS_PIN "PD.6"

/* 插入后等待链路稳定的防抖动窗口；拔出不延时，立即卸载 */
#define USBH_MOUNT_DELAY_MS   500
/* 卸载前等待 DVR 线程收尾的上限。必须大于 MSC 单次传输超时
 * （CONFIG_USBHOST_MSC_TIMEOUT = 1500ms），否则会在 DVR 还持有 elene
 * 时强制卸载，释放 FATFS 后仍被 f_read/f_close 使用。 */
#define USBH_STOP_WAIT_MS     3000

static struct rt_device   s_udisk_dev;
static struct usbh_msc   *s_active_msc;      /* 已挂载、供块设备 IO 使用 */
static rt_off_t           s_part_offset;     /* 第一个分区的 LBA 偏移 */
static rt_off_t           s_part_size;       /* 分区大小，0 = 整盘 */
static rt_mutex_t         s_lock;
static rt_bool_t          s_dev_registered;
static rt_bool_t          s_mounted;         /* /udisk 当前已挂载 */

/* 事件驱动的挂载/卸载 worker：插入/拔出回调只更新时间戳状态并唤醒它，
 * 所有 DFS / USB 阻塞操作都在 worker 线程里串行执行。 */
static rt_sem_t           s_event_sem;
static volatile struct usbh_msc *s_pending_msc;  /* 最近一次插入的 MSC */
static volatile rt_bool_t s_removed;             /* 最近一次事件是拔出 */
static volatile rt_uint32_t s_stop_seq;          /* 拔出事件计数 */
static rt_uint32_t        s_mounted_stop_seq;    /* 本次挂载时的 stop_seq */

/* -------------------- RT 块设备 ops -------------------- */

/* 拔盘开始后（usbh_hubport_release 会把 connected 置 false，早于 kill URB /
 * class_free）不再提交新的 URB，避免 DVR 线程在已断开的端口上又排一次
 * 最长 1.5s 的传输，拖住卸载时机。 */
static rt_bool_t udisk_link_alive(struct usbh_msc *msc)
{
    if (msc == RT_NULL || msc->hport == RT_NULL || !msc->hport->connected)
        return RT_FALSE;
    return RT_TRUE;
}

static rt_size_t udisk_read(rt_device_t dev, rt_off_t pos, void *buf, rt_size_t size)
{
    struct usbh_msc *msc = s_active_msc;
    int ret;
    if (!udisk_link_alive(msc))
        return 0;
    ret = usbh_msc_scsi_read10(msc, s_part_offset + pos, (rt_uint8_t *)buf, size);
    return (ret == 0) ? size : 0;
}

static rt_size_t udisk_write(rt_device_t dev, rt_off_t pos, const void *buf, rt_size_t size)
{
    struct usbh_msc *msc = s_active_msc;
    int ret;
    if (!udisk_link_alive(msc))
        return 0;
    ret = usbh_msc_scsi_write10(msc, s_part_offset + pos, (rt_uint8_t *)buf, size);
    return (ret == 0) ? size : 0;
}

static rt_err_t udisk_control(rt_device_t dev, int cmd, void *args)
{
    struct usbh_msc *msc = s_active_msc;
    if (!udisk_link_alive(msc))
        return -RT_ERROR;
    if (cmd == RT_DEVICE_CTRL_BLK_GETGEOME) {
        struct rt_device_blk_geometry *geo = (struct rt_device_blk_geometry *)args;
        if (geo == RT_NULL)
            return -RT_ERROR;
        geo->bytes_per_sector = msc->blocksize;
        geo->block_size       = msc->blocksize;
        geo->sector_count     = s_part_size ? s_part_size : msc->blocknum;
        return RT_EOK;
    }
    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops udisk_ops = {
    RT_NULL,        /* init    */
    RT_NULL,        /* open    */
    RT_NULL,        /* close   */
    udisk_read,
    udisk_write,
    udisk_control,
};
#endif

/* -------------------- worker 线程：挂载 / 卸载 -------------------- */

/*
 * 卸载：等 DVR 线程收尾后 unmount + 注销块设备。
 * 只在 worker 线程里调用，保证挂载/卸载永远串行、不会互相踩。
 */
static void udisk_do_unmount(void)
{
#if ENABLE_BD_USB_DVR_FUNC
    /* DVR 线程可能仍在读 /udisk/elene。必须等它走完 deinit（关闭文件、
     * 释放缓冲）再 unmount，否则 FatFs 的 FATFS 块会被释放后仍被使用
     * （UAF 写坏 heap_sys 空闲链表）。 */
    extern uint8_t dvr_usb_is_stopped(void);
    int i;

    for (i = 0; i < (USBH_STOP_WAIT_MS / 10) && !dvr_usb_is_stopped(); i++) {
        rt_thread_mdelay(10);
    }
    if (!dvr_usb_is_stopped()) {
        /* 不能强拆：保持挂载，等 DVR 线程真正退出后由下一次事件重试。
         * 期间块设备 IO 已经返回 0，不会再有新的 USB 传输。 */
        rt_kprintf("udisk: dvr still running, defer unmount\n");
        return;
    }
#endif

    if (s_mounted) {
        dfs_unmount(MNT_NAME);
        s_mounted = RT_FALSE;
        rt_kprintf("udisk: unmount %s\n", MNT_NAME);
    }

    rt_mutex_take(s_lock, RT_WAITING_FOREVER);
    if (s_dev_registered) {
        rt_device_unregister(&s_udisk_dev);
        s_dev_registered = RT_FALSE;
    }
    s_active_msc  = RT_NULL;
    s_part_offset = 0;
    s_part_size   = 0;
    rt_mutex_release(s_lock);
}

/*
 * 挂载：scsi_init → 读 MBR → 解析分区 → 注册块设备 → dfs_mount。
 * 每一步之后检查 s_stop_seq：拔出事件在任何阶段到来都立即放弃，
 * 避免在已断开的设备上继续注册/挂载。
 */
static void udisk_do_mount(struct usbh_msc *msc)
{
    struct dfs_partition part = {0};
    rt_uint8_t       *sector = RT_NULL;
    rt_uint32_t       stop_seq;
    int               ret;

    if (msc == RT_NULL) {
        return;
    }
    stop_seq = s_stop_seq;

    /* 1. SCSI init：TEST UNIT READY / REQUEST SENSE / INQUIRY / READ CAPACITY(10)
     *    成功后 msc->blocksize / msc->blocknum 已就绪。 */
    ret = usbh_msc_scsi_init(msc);
    if (ret < 0) {
        rt_kprintf("udisk: scsi_init failed: %d\n", ret);
        return;
    }
    if (s_stop_seq != stop_seq) {
        rt_kprintf("udisk: aborted by unplug (scsi_init)\n");
        return;
    }

    /* 2. 读 MBR 第一扇区 */
    sector = (rt_uint8_t *)rt_malloc(512);
    if (sector == RT_NULL) {
        rt_kprintf("udisk: alloc sector buf failed\n");
        return;
    }
    ret = usbh_msc_scsi_read10(msc, 0, sector, 1);
    if (ret < 0) {
        rt_kprintf("udisk: read MBR failed: %d\n", ret);
        rt_free(sector);
        return;
    }

    /* 3. 解析 MBR / GPT 找第一个有效分区；无分区则用整盘。
     *    本工程未集成 GPT 解析器，遇到 GPT 保护 MBR (type=0xee) 也按整盘处理。 */
    if (dfs_filesystem_get_partition(&part, sector, 0) != RT_EOK
        || part.type == 0xee) {
        part.offset = 0;
        part.size   = msc->blocknum;
    }
    rt_free(sector);
    if (s_stop_seq != stop_seq) {
        rt_kprintf("udisk: aborted by unplug (mbr)\n");
        return;
    }

    /* 4. 短临界区：注册块设备 udisk。dfs_mount / 后续 IO 都在锁外执行。 */
    rt_mutex_take(s_lock, RT_WAITING_FOREVER);
    if (s_stop_seq != stop_seq) {
        rt_mutex_release(s_lock);
        rt_kprintf("udisk: aborted by unplug (register)\n");
        return;
    }
    s_active_msc  = msc;
    s_part_offset = part.offset;
    s_part_size   = part.size;

    s_udisk_dev.type = RT_Device_Class_Block;
#ifdef RT_USING_DEVICE_OPS
    s_udisk_dev.ops = &udisk_ops;
#else
    s_udisk_dev.read    = udisk_read;
    s_udisk_dev.write   = udisk_write;
    s_udisk_dev.control = udisk_control;
#endif
    s_udisk_dev.user_data = RT_NULL;

    ret = rt_device_register(&s_udisk_dev, DEV_NAME,
        RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_REMOVABLE | RT_DEVICE_FLAG_STANDALONE);
    if (ret != RT_EOK) {
        s_active_msc  = RT_NULL;
        s_part_offset = 0;
        s_part_size   = 0;
        rt_mutex_release(s_lock);
        rt_kprintf("udisk: rt_device_register failed: %d\n", ret);
        return;
    }
    s_dev_registered = RT_TRUE;
    rt_mutex_release(s_lock);

    /* 5. 锁外 dfs_mount：可能触发 udisk_read 读 superblock，耗时不可控。 */
    ret = dfs_mount(DEV_NAME, MNT_NAME, "elm", 0, 0);
    if (ret != 0) {
        rt_kprintf("udisk: mount %s failed: %d\n", MNT_NAME, ret);
        udisk_do_unmount();
        return;
    }

    s_mounted = RT_TRUE;
    s_mounted_stop_seq = stop_seq;
    rt_kprintf("udisk: mount %s successfully\n", MNT_NAME);

#if ENABLE_BD_USB_DVR_FUNC
    /* 挂载期间设备又被拔出：不要拉起 DVR，worker 下一轮会立即卸载 */
    if (s_stop_seq == stop_seq) {
        extern void dvr_usb_eval_and_apply(bool state);
        dvr_usb_eval_and_apply(true);
    }
#endif
}

/*
 * 单实例事件 worker：
 *  - 拔出事件 → 立即卸载（不做延时）；
 *  - 插入事件 → 静默 USBH_MOUNT_DELAY_MS 后再挂载（合并快速插拔的多条事件）。
 */
static void usbh_udisk_worker(void *param)
{
    while (1) {
        rt_sem_take(s_event_sem, RT_WAITING_FOREVER);

        /* 已挂载的会话在挂载之后发生过拔出 → 先卸载 */
        if (s_mounted && s_stop_seq != s_mounted_stop_seq) {
            udisk_do_unmount();
        }

        /* 最近一次事件是拔出：保持卸载状态 */
        if (s_removed) {
            continue;
        }

        /* 插入防抖：事件持续到来就不断推迟挂载 */
        while (rt_sem_take(s_event_sem,
                           rt_tick_from_millisecond(USBH_MOUNT_DELAY_MS)) == RT_EOK) {
            if (s_removed) {
                break;
            }
        }
        if (s_removed || s_mounted) {
            continue;
        }

        udisk_do_mount((struct usbh_msc *)s_pending_msc);
    }
}

/* -------------------- 强符号：覆盖 cherryusb __WEAK -------------------- */

void usbh_msc_run(struct usbh_msc *msc_class)
{
    if (msc_class == RT_NULL) {
        return;
    }
    /* 枚举线程回调，持 hport mutex：只登记最新设备并唤醒 worker，不阻塞。 */
    s_pending_msc = msc_class;
    s_removed = RT_FALSE;
    if (s_event_sem) {
        rt_sem_release(s_event_sem);
    }
}

void usbh_msc_stop(struct usbh_msc *msc_class)
{
    if (msc_class == RT_NULL) {
        return;
    }

#if ENABLE_BD_USB_DVR_FUNC
    /* 拔盘第一时间停 DVR 预览并让 dvr 线程退出。非阻塞：此函数运行在
     * cherryusb 断开线程并持有 hport mutex，不能等待线程收尾。 */
    extern void dvr_usb_eval_and_apply(bool state);
    dvr_usb_eval_and_apply(false);
#endif

    /*
     * 本函数运行在 cherryusb disconnect 线程并持有 hport mutex：只做非阻塞
     * 状态清理，unmount/unregister 全部交给 worker 线程（那里可以安全等待
     * DVR 线程收尾，也不会和 USB 断开流程互等）。
     */
    if (rt_mutex_take(s_lock, rt_tick_from_millisecond(200)) == RT_EOK) {
        s_active_msc  = RT_NULL;
        s_part_offset = 0;
        s_part_size   = 0;
        rt_mutex_release(s_lock);
    } else {
        /* 锁被 worker 短时间占用时直接强制摘掉，避免拖住断开流程 */
        rt_kprintf("udisk: stop lock timeout, force detach msc\n");
        s_active_msc  = RT_NULL;
        s_part_offset = 0;
        s_part_size   = 0;
    }

    /* 标记拔出，唤醒 worker：它会立即（不防抖）卸载 /udisk */
    s_removed = RT_TRUE;
    s_stop_seq++;
    if (s_event_sem) {
        rt_sem_release(s_event_sem);
    }
}

/* -------------------- 早初始化（INIT 阶段 4） -------------------- */

/*
 * PD.6 = USB 5V 使能（高有效）。
 * 必须早于 usbh_init()（INIT_ENV_EXPORT，阶段 5）拉高，否则开机即插着的 U 盘
 * 在 host 开始枚举时还没供电，会漏检。
 * PIN 驱动在 INIT_BOARD_EXPORT（阶段 1）注册、pinmux 在 rt_hw_board_init 已完成，
 * 所以放在阶段 4 安全。
 * mutex 也在这里建：U 盘枚举可能在 ENV(5)~APP(6) 之间就完成，不能等到 APP 阶段。
 */
static int mw_usbhost_early_init(void)
{
    rt_thread_t worker;

    s_lock = rt_mutex_create("usbh_lock", RT_IPC_FLAG_PRIO);
    s_event_sem = rt_sem_create("usbh_evt", 0, RT_IPC_FLAG_PRIO);
    if (s_lock == RT_NULL || s_event_sem == RT_NULL) {
        rt_kprintf("udisk: create ipc failed\n");
        return -RT_ENOMEM;
    }

    /* 挂载/卸载 worker 必须早于 U 盘枚举完成就绪（枚举可能在
     * INIT_ENV(5)~INIT_APP(6) 之间结束）。 */
    worker = rt_thread_create("usbh_udisk", usbh_udisk_worker, RT_NULL,
                              4096, CONFIG_USBHOST_PSC_PRIO + 1, 10);
    if (worker == RT_NULL) {
        rt_kprintf("udisk: create worker failed\n");
        return -RT_ENOMEM;
    }
    rt_thread_startup(worker);

    {
        rt_base_t pin;

        pin = rt_pin_get(MW_USB_VBUS_PIN);
        if (pin < 0) {
            rt_kprintf("udisk: get vbus pin %s failed\n", MW_USB_VBUS_PIN);
            return -RT_ERROR;
        }
        rt_pin_mode(pin, PIN_MODE_OUTPUT);
        rt_pin_write(pin, PIN_HIGH);        /* ACT_HIGH */
    }
    return RT_EOK;
}
INIT_COMPONENT_EXPORT(mw_usbhost_early_init);

/* 下电低功耗: 断开 USB 5V.
 * usbh_initialize() 只在 msh 命令里调用, 正常情况下控制器根本没初始化,
 * 但 5V 使能脚从 INIT_COMPONENT 起一直是高, 休眠期间白白给 U 盘口供电. */
void mw_usbhost_sleep(void)
{
    rt_base_t pin = rt_pin_get(MW_USB_VBUS_PIN);

    if (pin >= 0)
        rt_pin_write(pin, PIN_LOW);
}

#endif