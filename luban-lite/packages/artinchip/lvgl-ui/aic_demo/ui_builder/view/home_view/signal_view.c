
#include "signal_view.h"
#include <stdio.h>
#include <stddef.h>

/* TCS 闪烁：2Hz = 500ms 周期，半周期 250ms 切换一次显隐 */
#define TCS_BLINK_HALF_PERIOD_MS  250

static lv_obj_t* home_signal_widget[ICON_SIGNAL_NUM_MAX] = { NULL };
typedef struct {
    signal_e signal;
    size_t offset;
} member_info_t;

static member_info_t member_info[] = {
    /* 左右转向灯：home_top_bar_create 中创建 */
    {ICON_LEFT     , offsetof(home_page_t, icon_left)},
    {ICON_RIGHT    , offsetof(home_page_t, icon_right)},
    /* 其余信号灯：home_signal_create 中创建 */
    {ICON_MW       , offsetof(home_page_t, icon_MW)},
    {ICON_TCS       , offsetof(home_page_t, icon_TC)},
    {ICON_TW       , offsetof(home_page_t, icon_temp_warning)},
    {ICON_CC_STATUS, offsetof(home_page_t, icon_cc_status)},
    {ICON_VCU      , offsetof(home_page_t, icon_vcu)},
    {ICON_HIGH_BEAM, offsetof(home_page_t, icon_high_beam)},
    {ICON_NEAR_BEAM, offsetof(home_page_t, icon_near_beam)},
    {ICON_READY    , offsetof(home_page_t, icon_ready)},
    {ICON_BATTERY  , offsetof(home_page_t, icon_battery)},
    {ICON_ABS      , offsetof(home_page_t, icon_abs)},
    /* 充电提示：home_battery_view_create 中创建，默认隐藏，按 VEH_CHARGER_STATUS 显隐 */
    {ICON_CHARGE   , offsetof(home_page_t, icon_charge)},
    /* 已移除映射：ICON_ECU/TCS/BRAKE 对应字段在 home_signal_create 中未创建 */
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


void home_refresh_signal(signal_e icon , int32_t state)
{
    if (!home_signal_widget[icon])
        return;

    /* 左右转向灯：常驻显示，激活(打转向/自检)切 _p、平时切 _n（不隐藏） */
    if (icon == ICON_LEFT) {
        lv_image_set_src(home_signal_widget[icon], state ?
            "L:/rodata/lvgl_data/image/home/top_left_p.png" :
            "L:/rodata/lvgl_data/image/home/top_left_n.png");
        return;
    }
    if (icon == ICON_RIGHT) {
        lv_image_set_src(home_signal_widget[icon], state ?
            "L:/rodata/lvgl_data/image/home/top_right_p.png" :
            "L:/rodata/lvgl_data/image/home/top_right_n.png");
        return;
    }
    if (icon == ICON_TCS) {
        home_refresh_TCS(state);
        return;
    }

    /* 其余信号：切 HIDDEN */
    bool visible = state;
    bool is_hidden = lv_obj_has_flag(home_signal_widget[icon], LV_OBJ_FLAG_HIDDEN);
    if (visible == !is_hidden)
        return;
    if (visible)
        lv_obj_remove_flag(home_signal_widget[icon], LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(home_signal_widget[icon], LV_OBJ_FLAG_HIDDEN);
}


void home_refresh_GMS_level(gms_level_e level )
{
    if(level > GMS_LEVEL_3)
        return ;
    
    char format[96] ;
    lv_snprintf(format, sizeof(format) - 1,
                "L:/rodata/lvgl_data/image/test/top_signal_%d_n.png", (int)level);

    if (home_signal_widget[ICON_MW])
    {
       lv_image_set_src(home_signal_widget[ICON_MW] , format) ;
    }
    
    return  ;
}

void home_refresh_TCS(int status)
{
    lv_obj_t *w = home_signal_widget[ICON_TCS];
    if (!w)
        return;

    switch (status)
    {
    case 0:  /* 隐藏 */
        lv_obj_add_flag(w, LV_OBJ_FLAG_HIDDEN);
        break;
    case 1:  /* 2Hz 闪烁 */
        if ((lv_tick_get() / TCS_BLINK_HALF_PERIOD_MS) % 2 == 0)
            lv_obj_remove_flag(w, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(w, LV_OBJ_FLAG_HIDDEN);
        break;
    case 2:  /* 常亮 */
        lv_obj_remove_flag(w, LV_OBJ_FLAG_HIDDEN);
        break;
    default:
        break;
    }
    return;
}


void home_refresh_signal_visible(bool visible )
{
    /* 自检闪烁：复用 home_refresh_signal，使左右转向灯切 _p/_n，其余切 HIDDEN */
    for (size_t i = 0; i < ICON_SIGNAL_NUM_MAX; i++){
        home_refresh_signal(i, visible ? 2 : 0);
    }
    return  ;
}

