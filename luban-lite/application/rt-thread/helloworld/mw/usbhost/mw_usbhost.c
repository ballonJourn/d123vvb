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
 * - 所有阻塞操作（scsi_init、读 MBR、dfs_mount/unmount）放在独立 thread
 *   或 s_lock 临界区外。
 *
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <dfs_fs.h>
#include <rtdevice.h>

#include "usbh_core.h"
#include "usbh_msc.h"
#include "usb_osal.h"
#include "mw_usbhost.h"

#define DEV_NAME  "udisk"
#define MNT_NAME  "/udisk"

static struct rt_device   s_udisk_dev;
static struct usbh_msc   *s_active_msc;     /* 当前活跃 MSC（单实例） */
static rt_off_t           s_part_offset;    /* 第一个分区的 LBA 偏移 */
static rt_off_t           s_part_size;      /* 分区大小，0 = 整盘 */
static rt_mutex_t         s_lock;
static rt_bool_t          s_dev_registered;

/* -------------------- RT 块设备 ops -------------------- */

static rt_size_t udisk_read(rt_device_t dev, rt_off_t pos, void *buf, rt_size_t size)
{
    struct usbh_msc *msc = s_active_msc;
    int ret;
    if (msc == RT_NULL)
        return 0;
    ret = usbh_msc_scsi_read10(msc, s_part_offset + pos, (rt_uint8_t *)buf, size);
    return (ret == 0) ? size : 0;
}

static rt_size_t udisk_write(rt_device_t dev, rt_off_t pos, const void *buf, rt_size_t size)
{
    struct usbh_msc *msc = s_active_msc;
    int ret;
    if (msc == RT_NULL)
        return 0;
    ret = usbh_msc_scsi_write10(msc, s_part_offset + pos, (rt_uint8_t *)buf, size);
    return (ret == 0) ? size : 0;
}

static rt_err_t udisk_control(rt_device_t dev, int cmd, void *args)
{
    struct usbh_msc *msc = s_active_msc;
    if (msc == RT_NULL)
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

/* -------------------- 异步 mount thread -------------------- */

/*
 * 完整流程：scsi_init → 读 MBR → 解析分区 → 注册块设备 → dfs_mount
 * 在独立 RT-Thread thread context 执行（cherryusb 枚举线程不会等它）。
 */
static void usbh_udisk_mount_thread(void *param)
{
    struct usbh_msc  *msc = (struct usbh_msc *)param;
    struct dfs_partition part = {0};
    rt_uint8_t       *sector = RT_NULL;
    int               ret;

    /* 1. SCSI init：TEST UNIT READY / REQUEST SENSE / INQUIRY / READ CAPACITY(10)
     *    成功后 msc->blocksize / msc->blocknum 已就绪。 */
    ret = usbh_msc_scsi_init(msc);
    if (ret < 0) {
        rt_kprintf("udisk: scsi_init failed: %d\n", ret);
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

    /* 4. 短临界区：注册块设备 udisk。mount thread 全程只在注册这一步
     *    持 s_lock，dfs_mount / 后续 IO 都在锁外执行，避免与 stop 死锁。 */
    rt_mutex_take(s_lock, RT_WAITING_FOREVER);
    if (s_active_msc != msc) {
        /* usbh_msc_stop 已被调用，放弃挂载。 */
        rt_mutex_release(s_lock);
        rt_kprintf("udisk: skipped (msc already removed)\n");
        return;
    }
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
        rt_mutex_release(s_lock);
        rt_kprintf("udisk: rt_device_register failed: %d\n", ret);
        return;
    }
    s_dev_registered = RT_TRUE;
    rt_mutex_release(s_lock);

    /* 5. 锁外 dfs_mount：可能触发 udisk_read 读 superblock，耗时不可控。 */
    ret = dfs_mount(DEV_NAME, MNT_NAME, "elm", 0, 0);
    if (ret == 0) {
        rt_kprintf("udisk: mount %s successfully\n", MNT_NAME);
        extern void dvr_usb_eval_and_apply(bool state);
        dvr_usb_eval_and_apply(true);
    } else {
        rt_kprintf("udisk: mount %s failed: %d\n", MNT_NAME, ret);
        rt_mutex_take(s_lock, RT_WAITING_FOREVER);
        if (s_dev_registered) {
            rt_device_unregister(&s_udisk_dev);
            s_dev_registered = RT_FALSE;
        }
        rt_mutex_release(s_lock);
    }
}

/* -------------------- 强符号：覆盖 cherryusb __WEAK -------------------- */

void usbh_msc_run(struct usbh_msc *msc_class)
{
    if (msc_class == RT_NULL) {
        return;
    }
    /* 1. 记下当前 msc_class（持锁短临界区） */
    rt_mutex_take(s_lock, RT_WAITING_FOREVER);
    s_active_msc = msc_class;
    rt_mutex_release(s_lock);

    /* 2. 起独立 thread 做 mount，立刻返回。 */
    usb_osal_thread_create("usbh_udisk_mnt", 2048,
        CONFIG_USBHOST_PSC_PRIO + 1,
        usbh_udisk_mount_thread, msc_class);
}

void usbh_msc_stop(struct usbh_msc *msc_class)
{
    if (msc_class == RT_NULL) {
        return;
    }
    rt_mutex_take(s_lock, RT_WAITING_FOREVER);
    s_active_msc = RT_NULL;
    s_part_offset = 0;
    s_part_size   = 0;
    if (s_dev_registered) {
        dfs_unmount(MNT_NAME);
        rt_device_unregister(&s_udisk_dev);
        s_dev_registered = RT_FALSE;
    }
    rt_mutex_release(s_lock);
}

/* -------------------- 自动初始化 -------------------- */

int mw_usbhost_init(void)
{
    s_lock = rt_mutex_create("usbh_lock", RT_IPC_FLAG_PRIO);
    return s_lock ? 0 : -RT_ENOMEM;
}