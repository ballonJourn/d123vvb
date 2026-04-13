#ifndef HOME_SPEED_VIEW_H
#define HOME_SPEED_VIEW_H

#include "definition.h"
#include "ui_objects.h"
#include <stdbool.h>
#include <stdio.h>
//设置速度
void home_refresh_speed(uint32_t speed);

//设置单位
void home_refresh_unit(unit_e unit) ;


void home_refresh_stop_state(bool istop) ;


void speed_view_run(int target) ;

int speed_view_current() ;
#endif