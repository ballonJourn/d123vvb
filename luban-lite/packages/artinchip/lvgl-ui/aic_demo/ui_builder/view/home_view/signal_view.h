#ifndef HOME_SIGNAL_VIEW_H
#define HOME_SIGNAL_VIEW_H
#include "ui_objects.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum home_signal_com{
    ICON_MW            ,
    ICON_TC            ,
    ICON_TW             ,
    ICON_HIGH_BEAM      ,
    ICON_LEFT           ,
    ICON_READY          ,
    ICON_RIGHT          ,
    ICON_NEAR_BEAM      ,
    ICON_ABS            ,
    ICON_ECU            ,
    ICON_TCS            ,
    ICON_BRAKE          ,
    ICON_CC_STATUS      ,
    ICON_VCU            ,
    ICON_BATTERY        ,
    ICON_CHARGE         ,   /* 充电提示：home_battery_view_create 中创建，默认隐藏 */

    ICON_SIGNAL_NUM_MAX ,
}signal_e;


typedef enum {
    GMS_LEVEL_0 ,
    GMS_LEVEL_1 ,
    GMS_LEVEL_2 ,
    GMS_LEVEL_3 ,
}gms_level_e ;

void home_signal_view_init();

void home_refresh_signal(signal_e icon , int32_t state) ;

void home_refresh_GMS_level(gms_level_e level );
void home_refresh_TCS(int status);

void home_refresh_signal_visible(bool visible );

#endif