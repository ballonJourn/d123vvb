#include "clock.h"
#include <stdio.h>
#include "vehicle_argument.h"
#include "setting_menu.h"
#include "definition.h"
#include <stdlib.h>

char time_value[CLOCK_OPTION_NUM_MAX] = { 0 } ;

static lv_obj_t* set_clock_widget[CLOCK_OPTION_NUM_MAX] = { NULL };

static clock_option_e option = CLOCK_H_1_OPTION ;


void get_label_clock(int32_t *min , int32_t *sec)
{
    if (min == NULL || sec == NULL) return ;

    // lv_label_get_text()atoi()
    int32_t h_1 = 0, h_2 = 0, m_1 = 0 , m_2 = 0;

    if(set_clock_widget[CLOCK_H_1_OPTION]){
       h_1 = atoi(lv_label_get_text(set_clock_widget[CLOCK_H_1_OPTION]) );
    
    }
    if(set_clock_widget[CLOCK_H_2_OPTION]){
       h_2 = atoi(lv_label_get_text(set_clock_widget[CLOCK_H_2_OPTION]));

    }
    if(set_clock_widget[CLOCK_M_1_OPTION]){
       m_1 = atoi(lv_label_get_text(set_clock_widget[CLOCK_M_1_OPTION]));

    }
    if(set_clock_widget[CLOCK_M_2_OPTION]){
       m_2 = atoi(lv_label_get_text(set_clock_widget[CLOCK_M_2_OPTION]));
    }

    *min = h_1 * 10 + h_2;
    *sec = m_1 * 10 + m_2;   

    return ;
}

void refresh_clock(int min ,int sec)
{
    if(set_clock_widget[CLOCK_H_1_OPTION]){
        lv_label_set_text_fmt(set_clock_widget[CLOCK_H_1_OPTION], "%d" ,min / 10);
    
    }
    if(set_clock_widget[CLOCK_H_2_OPTION]){
        lv_label_set_text_fmt(set_clock_widget[CLOCK_H_2_OPTION], "%d" ,min % 10);

    }
    if(set_clock_widget[CLOCK_M_1_OPTION]){
        lv_label_set_text_fmt(set_clock_widget[CLOCK_M_1_OPTION], "%d" ,sec / 10);

    }
    if(set_clock_widget[CLOCK_M_2_OPTION]){
        lv_label_set_text_fmt(set_clock_widget[CLOCK_M_2_OPTION], "%d" ,sec % 10);
    }

    return ;
}

int32_t option_current_value = 0 ;  //记录设置时的时间

static void setting_menu_view_deal_set()
{
    // printf("on_clock setting_menu_view_deal_set \n") ;
    set_current_menu_level(MENU_LEVEL_3);
    clock_option_init();
    if (set_clock_widget[option])
    {
        option_current_value = atoi(lv_label_get_text(set_clock_widget[option] ) );
    }
    
}

static void setting_menu_view_deal_back()
{
    set_current_menu_level(MENU_LEVEL_1);
    clock_view_clean_state() ;
}

static void setting_menu_view_deal_up()
{
    option =  (option - 1 + CLOCK_OPTION_NUM_MAX) % CLOCK_OPTION_NUM_MAX ;
    clock_view_set_focused_item(option) ;
}

static void setting_menu_view_deal_down()
{
    option =  (option + 1 ) % CLOCK_OPTION_NUM_MAX ;
    clock_view_set_focused_item(option) ;
}


static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = setting_menu_view_deal_up  ,
    [CLICK_SHORT_DOWN] = setting_menu_view_deal_down,
    [CLICK_SHORT_SET]  = setting_menu_view_deal_set ,
    [CLICK_SHORT_BACK] = setting_menu_view_deal_back,
};


void clock_init()
{

    setting_page_t *page = setting_page_get(&ui_manager);
    VIEW_MEMBER_VALID(page, clock_h_1) ;
    VIEW_MEMBER_VALID(page, clock_h_2) ;
    VIEW_MEMBER_VALID(page, clock_m_1) ;
    VIEW_MEMBER_VALID(page, clock_m_2) ;
    set_clock_widget[CLOCK_H_1_OPTION] = clock_h_1 ;
    set_clock_widget[CLOCK_H_2_OPTION] = clock_h_2 ;
    set_clock_widget[CLOCK_M_1_OPTION] = clock_m_1 ;
    set_clock_widget[CLOCK_M_2_OPTION] = clock_m_2 ;

    clock_view_set_focused_item(option) ;
    //刷新数据 todo

    return  ;
}

void on_clock_deal_short_key(click_e key)
{
    printf("on_clock_deal_short_key = %d \n" ,key) ;
    if (key < sizeof(short_click) / sizeof(short_click_deal) 
        && short_click[key]) {
        short_click[key]();
    }

    return  ;
}

void clock_view_set_focused_item(clock_option_e focusedIndex)
{
    for (size_t i = 0; i < CLOCK_OPTION_NUM_MAX ; i++)
    {
        if (set_clock_widget[i])
        {
            if (i == focusedIndex)
                lv_obj_set_style_bg_image_src(set_clock_widget[i] , LVGL_IMAGE_PATH(setting/time_selected.png), 0) ;
            else
                lv_obj_remove_local_style_prop(set_clock_widget[i] , LV_STYLE_BG_IMAGE_SRC , 0);

        }
    }

    return ;
}

void clock_view_clean_state()
{
    for (size_t i = 0; i < CLOCK_OPTION_NUM_MAX ; i++)
    {
        if (set_clock_widget[i])
            lv_obj_remove_local_style_prop(set_clock_widget[i] , LV_STYLE_BG_IMAGE_SRC , 0);
    }
}

/// @brief  三级页面功能
static lv_timer_t* timer_clock_Id  ;

void clock_ctrl_end()
{
    if (timer_clock_Id )
    {
        lv_timer_del(timer_clock_Id) ;
        timer_clock_Id = NULL ;
    }
    
    if (set_clock_widget[option])
        lv_obj_remove_flag(set_clock_widget[option] , LV_OBJ_FLAG_HIDDEN);
    
}

void clock_cacle(int direc)
{
    uint32_t option_time = atoi(lv_label_get_text(set_clock_widget[option]));
    uint32_t temp_time   = 0;
    int offset = direc > 0 ? 1 : -1 ;
    
    switch (option)
    {
        case CLOCK_H_1_OPTION:
            temp_time = atoi(lv_label_get_text(set_clock_widget[CLOCK_H_2_OPTION]));
            if (temp_time > 3 )
                option_time = (option_time + offset + 2) % 2 ;
            else
                option_time = (option_time + offset + 3) % 3 ;

            break;
        case CLOCK_H_2_OPTION:
            temp_time = atoi(lv_label_get_text(set_clock_widget[CLOCK_H_1_OPTION]));
            if (temp_time == 2 )
                option_time = (option_time + offset + 4) % 4 ;
            else
                option_time = (option_time + offset + 10) % 10 ;

            break;
        case CLOCK_M_1_OPTION:
            option_time = (option_time + offset + 6) % 6 ;

            break;
        case CLOCK_M_2_OPTION:
            option_time = (option_time + offset + 10) % 10 ;
        
            break;
        
        default:
            break;
    }

    lv_label_set_text_fmt(set_clock_widget[option] ,"%lu",option_time) ;
}

static void setting_option_view_deal_set()
{
    set_current_menu_level(MENU_LEVEL_2);
 
    clock_ctrl_end();

    //to do  set systerm time
    int32_t min = 0, sec = 0 ;
    get_label_clock(&min , &sec);
    // vehicle_set_time(min, sec);
    return ;
}

static void setting_option_view_deal_back()
{
    set_current_menu_level(MENU_LEVEL_2);
    
    clock_ctrl_end();

    if (set_clock_widget[option])
    {
        lv_label_set_text_fmt(set_clock_widget[option] , "%ld" ,option_current_value) ;
    }

    return ;
}

static void setting_option_view_deal_up()
{
    clock_cacle(-1) ;
    return ;

}

static void setting_option_view_deal_down()
{
    clock_cacle(1) ;
    return ;
}


static short_click_deal option_click[] = {
    [CLICK_SHORT_UP]   = setting_option_view_deal_up  ,
    [CLICK_SHORT_DOWN] = setting_option_view_deal_down,
    [CLICK_SHORT_SET]  = setting_option_view_deal_set ,
    [CLICK_SHORT_BACK] = setting_option_view_deal_back,
};

void on_timer_flicker(lv_timer_t* timer)
{
    (void)timer ;
    
    if (set_clock_widget[option])
    {
        if (lv_obj_has_flag(set_clock_widget[option] , LV_OBJ_FLAG_HIDDEN))
            lv_obj_remove_flag(set_clock_widget[option] , LV_OBJ_FLAG_HIDDEN) ;
        else 
            lv_obj_add_flag(set_clock_widget[option] , LV_OBJ_FLAG_HIDDEN) ;
    }
    
    return  ;
}


void clock_option_init()
{
    timer_clock_Id = lv_timer_create(on_timer_flicker , 500 , NULL ) ;
    return  ;
}

void on_clock_option_deal_short_key(click_e key)
{
    printf("on_clock_deal_short_key = %d \n" ,key) ;
    if (key < sizeof(option_click) / sizeof(short_click_deal) 
        && option_click[key]) {
        option_click[key]();
    }

    return  ;
}
