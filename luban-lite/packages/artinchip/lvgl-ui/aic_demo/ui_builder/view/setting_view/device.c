#include "device.h"
#include <stdio.h>
#include "vehicle_argument.h"
#include "definition.h"

void refresh_sn(const char* str)
{
    setting_page_t *page = setting_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page , label_37) ;
    lv_label_set_text(label_37 , str) ;
}

void refresh_ver(const char* str)
{
    setting_page_t *page = setting_page_get(&ui_manager) ;
    VIEW_MEMBER_VALID(page , label_39) ;
    lv_label_set_text(label_39 , str) ;
}

// void refresh_mcu(const char* str)
// {
//     // setting_page_t *page = setting_page_get(&ui_manager) ;
//     //no mcu 
//     // VIEW_MEMBER_VALID(page , label_39) ;
//     // lv_label_set_text(label_39 , str) ;
// }