/*
 * mw_usbhost.h
 *
 * U 盘/USB 存储挂载支持（基于 CherryUSB host MSC class）。
 *
 * 强符号 override：实现 usbh_msc_run/stop，覆盖 cherryusb 自带
 * demo/usbh_msc_template.c 的 __WEAK 同名符号。插入 U 盘后自动注册
 * RT-Thread 块设备 "udisk" 并挂载到 /udisk；拔出时反向清理。
 *
 * 初始化（mutex + USB 5V 使能）由 mw_usbhost.c 里的 INIT_COMPONENT_EXPORT
 * 自动完成，早于 usbh_init()（INIT_ENV_EXPORT），不需要在 mw_init() 里调用。
 *
 */
#ifndef MW_USBHOST_H
#define MW_USBHOST_H

#include "dev_config.h"

#if MW_CHEERYUSB_ENABLE
/**
 * @brief 下电低功耗: 关闭 USB 5V 使能脚
 * @note  仅断电, 不影响已挂载 U 盘的数据一致性(下电前落盘已完成)
 */
void mw_usbhost_sleep(void);
#endif

#endif /* MW_USBHOST_H */
