#ifndef HOME_ELECTRICAL_VIEW_H
#define HOME_ELECTRICAL_VIEW_H

#include <stdio.h>
#include "definition.h"
typedef enum  {
    NONE ,
    RED ,
    YELLOW ,
    GREEN ,
}fg_color;


void home_refresh_electrical(int mileage);

void home_refresh_electrical_unit(unit_e unit);


#endif