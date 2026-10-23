#ifndef __DEFINITION__H
#define __DEFINITION__H

#include "lvgl.h"

#define VIEW_MEMBER_VALID(view_page, member, ...) \
    lv_obj_t *member = NULL; \
    do { \
        if ((view_page) && (view_page)->member) { \
            member = (view_page)->member; \
        } else { \
            LV_LOG_ERROR("%s->%s is NULL ",#view_page, #member); \
            return __VA_ARGS__; \
        } \
    } while (0);

#define SPEED_MAX (150)        //最大速度
#define RPM_MAX (199)          //最大转速
#define POWER_MAX (52500)        //最大功率
#define POWER_MIN (25500)        //最小功率
#define ELECTRI_MAX (100)      //最大续航里程 km
#define ANGLE_MAX (270)        //最大角度
#define KM_CONVERT_MILE  (0.62137f)

typedef enum {
    KM_H     ,
    MPH      ,
    UNIT_MAX ,
}unit_e;

typedef enum {
    DRV_MODE_E   ,
    DRV_MODE_N   ,
    DRV_MODE_S   ,
    DRV_MODE_MAX ,
}drv_mode_e;

/* 挡位枚举: 数值与 CAN 信号 VEH_GEAR_POSITION 的原始编码一致,
 * 使调用处可直接 home_refresh_gear((gear_e)vehicle_get_data(VEH_GEAR_POSITION));
 * 显示目标见 home_refresh_gear / phone imagebuff 按 raw 下标排布 */
typedef enum {
    GEAR_P   = 0,   /* raw 0: 驻车 */
    GEAR_N   = 1,   /* raw 1: 驻车另一编码, 与 GEAR_P 同显示 */
    GEAR_D   = 2,   /* raw 2: 前进挡 */
    GEAR_R   = 3,   /* raw 3: 倒挡 */
    GEAR_MAX ,
}gear_e;


#endif