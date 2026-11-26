#include <stdbool.h>
#include <sys/stat.h>
#include <rthw.h>

#include "vehicle_upgrade.h"
#include "aic_reboot_reason.h"
#include "set_param.h"

#define UPGRADE_CFG_PATH "/udisk/bootcfg.txt"

bool vehicle_has_upgrade_file(void)
{
    struct stat st;

    /* U 盘未挂载（未插入）或文件不存在时 stat 均失败 */
    return (stat(UPGRADE_CFG_PATH, &st) == 0);
}

void vehicle_reboot_for_upgrade(void)
{
    /* 先清一次：aic_set_reboot_reason 遇到已有 reason 会直接返回，
     * 必须确保寄存器为空，新的 reason 才能写进去。 */
    aic_clr_reboot_reason();
    aic_set_reboot_reason(REBOOT_REASON_BL_UDISK_UPGRADE);
    restore_factory_usr_param();
    // rt_hw_cpu_reset();
}
