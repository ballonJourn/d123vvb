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

/* 续航里程: 数值与单位写在同一个 label, value < 0 表示无效 */
void home_refresh_range(int32_t value, unit_e unit)
{
    home_page_t *page = home_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, range_lab)

    char range_value_str[16] = " " ;
    const char *unit_str = (unit == KM_H) ? "km" : "mile" ;

    if (value < 0) {
        snprintf(range_value_str, sizeof(range_value_str), "--%s", unit_str) ;
    } else {
        snprintf(range_value_str, sizeof(range_value_str), "%d%s",
                 (int)value, unit_str) ;
    }

    lv_label_set_text(range_lab, range_value_str) ;

    return  ;
}