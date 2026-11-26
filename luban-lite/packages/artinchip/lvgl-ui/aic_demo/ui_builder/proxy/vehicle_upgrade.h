#ifndef VEHICLE_UPGRADE_H
#define VEHICLE_UPGRADE_H

#include <stdbool.h>

/* U 盘根目录是否存在升级配置 bootcfg.txt。
 * 判据与 bootloader 保持一致：bootloader 也是以能否打开 bootcfg.txt 为准。 */
bool vehicle_has_upgrade_file(void);

/* 写入 REBOOT_REASON_BL_UPGRADE 后复位，由 bootloader 接管 U 盘升级。 */
void vehicle_reboot_for_upgrade(void);

#endif /* VEHICLE_UPGRADE_H */
