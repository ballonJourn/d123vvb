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

#define SPEED_MAX (199)        //最大速度
#define RPM_MAX (199)          //最大转速
#define POWER_MAX (100)        //最大功率
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

typedef enum {
    GEAR_N   ,
    GEAR_D   ,
    GEAR_R   ,
    GEAR_MAX ,
}gear_e;


#endif