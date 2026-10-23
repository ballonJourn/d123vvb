
#ifndef HOME_MILEAGE_VIEW_H
#define HOME_MILEAGE_VIEW_H

#include "definition.h"
#include "ui_objects.h"
#include <stdbool.h>
#include <stdio.h>

void home_refresh_trip(double trip) ;

void home_refresh_odo(double odo) ;

void home_refresh_mileage_unit(unit_e unit) ;

/**
 * @brief 刷新续航里程 (range_lab, 数值与单位同一 label)
 * @param value 续航里程(已换算到 unit 对应单位); 传负数表示无效, 显示 "--km"/"--mile"
 * @param unit  当前单位 (KM_H/MPH)
 */
void home_refresh_range(int32_t value, unit_e unit) ;


#endif //HOME_MILEAGE_VIEW_H