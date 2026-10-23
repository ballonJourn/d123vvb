#include "link_view.h"
#include "link_view.h"
#include "vehicle_argument.h"
#include "electrical_view.h"
#include "definition.h"
#include "vehicle_mile.h"
#include "aic_ui.h"
#include "vehicle_argument.h"
#include "vehicle_data.h"

static uint32_t current_value = 0 ;

static fg_color current_color = NONE ;

char *imagebuff[] = {"gear_n.png" , "gear_d.png" , "gear_r.png"} ;

void link_view_init()
{
    phone_refresh_electrical(0) ;  
    phone_refresh_electrical_unit(vehicle_get_param_unit()) ;  
    phone_refresh_speed(0);    
    phone_refresh_unit(vehicle_get_param_unit() ); 

    phone_refresh_mileage();

    // extern char *get_qr_string();
    // char *string = get_qr_string();
    // lv_log("link_view_init qr = %s len = %d ",string , lv_strlen(string)) ;
    // phone_refresh_qr(string) ;
}

void phone_refresh_electrical(int mileage) 
{
    lv_color_t color_buff[] = {
        lv_color_hex(0x000000) ,
        lv_color_hex(0xff0000) ,
        lv_color_hex(0xffff00) ,
        lv_color_hex(0x00ff55) ,
    } ;

    phone_page_t *page = phone_page_get(&ui_manager) ;

    VIEW_MEMBER_VALID(page, elec_bar) 
    VIEW_MEMBER_VALID(page, elec_value) 
    VIEW_MEMBER_VALID(page, elec_percent) 

    mileage = (mileage > ELECTRI_MAX) ? ELECTRI_MAX : mileage ;
    current_value = mileage ;
    float step = 100.0f / ELECTRI_MAX  ;
    int perent = (int)(step * mileage) ;

    fg_color _color = NONE ;
    if ( 0 <= perent && perent <= 10 )
        _color = RED ;
    else if( 10 < perent && perent <= 20 )
        _color = YELLOW ;
    else 
        _color = GREEN ;

    if (current_color != _color)
    {
        lv_obj_set_style_bg_color(elec_bar, color_buff[_color] , LV_PART_INDICATOR ) ;
        
        current_color = _color ;
        // printf("elelctrical color changed\n") ;
    }

    lv_bar_set_value(elec_bar , perent , LV_ANIM_OFF) ;

    lv_label_set_text_fmt(elec_percent, "%d%%",perent);
    
    
    uint32_t temp_value = mileage ;

    if (MPH == vehicle_get_param_unit())
        temp_value  *= KM_CONVERT_MILE ; 
    lv_label_set_text_fmt(elec_value , "%ld" ,temp_value) ;
    
    return  ;
}

void phone_refresh_electrical_unit(unit_e unit) 
{
    phone_page_t *page = phone_page_get(&ui_manager) ;

    VIEW_MEMBER_VALID(page, elec_unit)
    VIEW_MEMBER_VALID(page, elec_value) 
    lv_label_set_text(elec_unit, (unit == KM_H) ? "km" : "mile" );
    
    
    uint32_t temp_value = current_value ;
    if (MPH == vehicle_get_param_unit())
        temp_value  *= KM_CONVERT_MILE ; 
    
    lv_label_set_text_fmt(elec_value, "%ld" ,temp_value) ;

    return  ;
}


void phone_refresh_speed(uint32_t speed_value)
{
    phone_page_t *page = phone_page_get(&ui_manager) ;

    VIEW_MEMBER_VALID(page, speed) 

    if (MPH == vehicle_get_param_unit())
        speed_value  *= KM_CONVERT_MILE ; 

    lv_label_set_text_fmt(speed, "%ld" , speed_value) ;
    
    return ;
}

void phone_refresh_unit(unit_e unit)
{
    phone_page_t *page = phone_page_get(&ui_manager) ;

    VIEW_MEMBER_VALID(page, speed_unit) 
    VIEW_MEMBER_VALID(page, trip_unit) 
    VIEW_MEMBER_VALID(page, odo_unit) 
    lv_label_set_text_fmt(speed_unit , "%s" , unit == KM_H ? "km/h" : "mph") ;
    lv_label_set_text_fmt(trip_unit , "%s" , unit == KM_H ? "km" : "mile") ;
    lv_label_set_text_fmt(odo_unit , "%s" , unit == KM_H ? "km" : "mile") ;

   return  ;
}

void phone_refresh_trip(double trip) 
{
    phone_page_t *page = phone_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, trip_label) 

    char mileage_value_str[16] = " " ;      
    
    double trip_km = trip / 1000.0;
    double truncated = (int)(trip_km * 10) / 10.0;

    snprintf(mileage_value_str, sizeof(mileage_value_str) , "%.1f" , truncated );
    lv_label_set_text(trip_label, mileage_value_str) ;

    return  ;
}

void phone_refresh_odo(double odo) 
{
    phone_page_t *page = phone_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, odo_label) 

    char mileage_value_str[16] = " " ;
    lv_snprintf(mileage_value_str, sizeof(mileage_value_str) , "%d" ,  (int)(odo / 1000) );     
    
    lv_label_set_text(odo_label, mileage_value_str);
    
    return  ;
}

void phone_refresh_qr(const char* string)
{
    phone_page_t *page = phone_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, qr_code) 

    lv_qrcode_update(qr_code , string , lv_strlen(string));
}

void phone_refresh_gear(gear_e gear)
{
    if(gear > GEAR_R)
        return ;

    phone_page_t *page = phone_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, gear_image) 
    char imge[128] ;
    lv_snprintf(imge , sizeof(imge) -1 , LVGL_IMAGE_PATH(phone/%s) , imagebuff[gear]) ;
    lv_img_set_src(gear_image , imge) ;

}

void phone_refresh_link_state(bool state)
{
    phone_page_t *page = phone_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page, qr_cont) 

    bool visible = lv_obj_has_flag(qr_cont , LV_OBJ_FLAG_HIDDEN);
    if (state == visible)
        return ;

    if (state) {
        lv_obj_add_flag(qr_cont, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(qr_cont, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_invalidate(page->obj);
}


void phone_refresh_mileage()
{
    uint32_t u32_odo   = vehicle_get_mile_odo()  ;
    uint32_t u32_tripA = vehicle_get_mile_tripA();
    if (MPH == vehicle_get_param_unit())
    {
        u32_odo   *= KM_CONVERT_MILE ; 
        u32_tripA *= KM_CONVERT_MILE ;
    }
    phone_refresh_trip ((double)u32_odo) ;
    phone_refresh_odo((double)u32_tripA) ;
}
