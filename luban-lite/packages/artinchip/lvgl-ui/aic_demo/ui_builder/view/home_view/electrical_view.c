
#include "electrical_view.h"
#include "lvgl.h"
#include "aic_ui.h"
#include "ui_objects.h"
#include "vehicle_argument.h"

static uint32_t elec_current_value = 0 ;

static fg_color current_color = NONE ;

void home_refresh_electrical(int mileage) 
{
    static int value = 0 ;
    if (value == mileage)
        return ;
    
    lv_color_t color_buff[] = {
        lv_color_hex(0x000000) ,
        lv_color_hex(0xff0000) ,
        lv_color_hex(0xffff00) ,
        lv_color_hex(0x00ff55) ,
    } ;

    home_page_t *page = home_page_get(&ui_manager) ;

    VIEW_MEMBER_VALID(page, electrical_bar) 
    VIEW_MEMBER_VALID(page, electrical_value) 
    VIEW_MEMBER_VALID(page, electrical_percentage)

    mileage = (mileage > ELECTRI_MAX) ? ELECTRI_MAX : mileage ;
    elec_current_value = mileage ;
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
        lv_obj_set_style_bg_color(electrical_bar, color_buff[_color] , LV_PART_INDICATOR ) ;
        current_color = _color ;
        // printf("elelctrical color changed\n") ;
    }

    lv_bar_set_value(electrical_bar , perent , LV_ANIM_OFF) ;

    lv_label_set_text_fmt(electrical_percentage, "%d%%",perent);
    
    uint32_t temp_value = mileage ;

    if (MPH == vehicle_get_param_unit())
        temp_value  *= KM_CONVERT_MILE ; 

    lv_label_set_text_fmt(electrical_value , "%ld" ,temp_value) ;
    
    value = mileage ;
    
    return  ;
}

void home_refresh_electrical_unit(unit_e unit) 
{
    home_page_t *page = home_page_get(&ui_manager) ;

    VIEW_MEMBER_VALID(page, electrical_unit)
    VIEW_MEMBER_VALID(page, electrical_value) 
    lv_label_set_text(electrical_unit, (unit == KM_H) ? "km" : "mile" );
    
    
    uint32_t temp_value = elec_current_value ;
    // if (MPH == vehicle_get_param_unit())
    // {
    //     temp_value  *= KM_CONVERT_MILE ; 
    // }
    lv_label_set_text_fmt(electrical_value, "%ld" ,temp_value) ;


    return  ;
}
