
#include "signal_view.h"
#include <stdio.h>
#include <stddef.h>

static lv_obj_t* home_signal_widget[ICON_SIGNAL_NUM_MAX] = { NULL };
typedef struct {
    signal_e signal;
    size_t offset;
} member_info_t;

static member_info_t member_info[] = {                                 
    {ICON_GMS , offsetof(home_page_t, icon_GMS)},
    {ICON_GPS , offsetof(home_page_t, icon_gps)},
    {ICON_BT , offsetof(home_page_t, icon_bt)},
    {ICON_HIGH_BEAM , offsetof(home_page_t, icon_high_beam)},
    {ICON_LEFT , offsetof(home_page_t, icon_left)},
    {ICON_READY , offsetof(home_page_t, icon_ready)},
    {ICON_RIGHT , offsetof(home_page_t, icon_right)},
    {ICON_AUTO_BEAM , offsetof(home_page_t, icon_near_beam)},
    {ICON_ABS , offsetof(home_page_t, icon_abs)},
    {ICON_ECU, offsetof(home_page_t, icon_ecu)},
    {ICON_TCS , offsetof(home_page_t, icon_tcs)},
    {ICON_BRAKE , offsetof(home_page_t, icon_brake)},
};

lv_obj_t* get_screen_member(signal_e signal) {
    home_page_t *page = home_page_get(&ui_manager);
    for (int i = 0; i < sizeof(member_info) / sizeof(member_info[0]); i++) {
        if (signal == member_info[i].signal) {
            return *(lv_obj_t**)((char*)page + member_info[i].offset);
        }
    }
    return NULL;
}

void home_signal_view_init()
{
    for (size_t i = 0; i < ICON_SIGNAL_NUM_MAX; i++){
        home_signal_widget[i] = get_screen_member(i);
    }
    return  ;
}


void home_refresh_signal(signal_e icon , bool visible) 
{
    if (home_signal_widget[icon])
    {
        bool is_hidden = lv_obj_has_flag(home_signal_widget[icon], LV_OBJ_FLAG_HIDDEN);

        if (visible == !is_hidden) {
            return;
        }

        if (visible) {
            lv_obj_remove_flag(home_signal_widget[icon], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(home_signal_widget[icon], LV_OBJ_FLAG_HIDDEN);
        }
        
    }
    
    return  ;
}


void home_refresh_GMS_level(gms_level_e level )
{
    if(level > GMS_LEVEL_3)
        return ;
    
    char format[64] ;
    lv_snprintf(format , sizeof(format) -1 , "top_signal_%d_n" , (int)level);

    if (home_signal_widget[ICON_GMS])
    {
       lv_image_set_src(home_signal_widget[ICON_GMS] , format) ;
    }
    
    return  ;
}


void home_refresh_signal_visible(bool visible )
{
    for (size_t i = 0; i < ICON_SIGNAL_NUM_MAX; i++){
        // if (i == ICON_GMS || i == ICON_GPS )   //不参与自检图表
        //     continue;
        if (home_signal_widget[i])
        {
            if (visible) {
                lv_obj_remove_flag(home_signal_widget[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(home_signal_widget[i], LV_OBJ_FLAG_HIDDEN);
            }
            
        }

    }
    return  ;
}

