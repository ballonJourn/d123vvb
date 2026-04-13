#ifndef LINK_VIEW_H__
#define LINK_VIEW_H__

#include "ui_init.h"
#include "aic_ui.h"
#include "definition.h"

void link_view_init();

void phone_refresh_electrical(int mileage) ;

void phone_refresh_electrical_unit(unit_e unit) ;

void phone_refresh_speed(uint32_t speed_value);

void phone_refresh_unit(unit_e unit);

void phone_refresh_trip(double trip) ;

void phone_refresh_odo(double odo) ;

void phone_refresh_qr(const char* string);

void phone_refresh_gear(gear_e gear);

void phone_refresh_link_state(bool state);

void phone_refresh_mileage();

#endif
