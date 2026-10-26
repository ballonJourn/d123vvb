/*
 * mw_usbhost.h
 *
 * U 盘/USB 存储挂载支持（基于 CherryUSB host MSC class）。
 *
 * 强符号 override：实现 usbh_msc_run/stop，覆盖 cherryusb 自带
 * demo/usbh_msc_template.c 的 __WEAK 同名符号。插入 U 盘后自动注册
 * RT-Thread 块设备 "udisk" 并挂载到 /udisk；拔出时反向清理。
 *
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MW_USBHOST_H
#define MW_USBHOST_H

/*
 * 初始化入口：创建 s_lock 互斥量。由 INIT_APP_EXPORT 自动注册，
 * 一般不需要在 mw_init() 里显式调用。
 *
 * 返回 0 成功，负数错误码参见 rtthread.h。
 */
int mw_usbhost_init(void);

#endif /* MW_USBHOST_H */
