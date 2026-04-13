#include "mileage_view.h"
#include "lvgl.h"
#include "aic_ui.h"

void home_refresh_trip(double trip) 
{
    home_page_t *page = home_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, trip_label) 

    char mileage_value_str[16] = " " ;      
    
    double trip_km = trip / 1000.0;
    double truncated = (int)(trip_km * 10) / 10.0;

    snprintf(mileage_value_str, sizeof(mileage_value_str) , "%.1f" , truncated );
    lv_label_set_text(trip_label, mileage_value_str) ;

    return  ;
}

void home_refresh_odo(double odo) 
{
    home_page_t *page = home_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, odo_label) 

    char mileage_value_str[16] = " " ;
    lv_snprintf(mileage_value_str, sizeof(mileage_value_str) , "%d" ,  (int)(odo / 1000) );     
    
    lv_label_set_text(odo_label, mileage_value_str);
    
    return  ;
}

void home_refresh_mileage_unit(unit_e unit) 
{
    home_page_t *page = home_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, odo_unit) 
    VIEW_MEMBER_VALID(page, trip_unit) 
    lv_label_set_text(odo_unit , (unit == KM_H) ? "km" : "mile" );
    lv_label_set_text(trip_unit, (unit == KM_H) ? "km" : "mile" );
    return  ;
}