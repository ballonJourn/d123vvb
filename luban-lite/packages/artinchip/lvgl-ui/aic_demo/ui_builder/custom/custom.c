#include "custom.h"
#include "lvgl.h"
#include "home_view_interface.h"
#include "vehicle_data.h"
#include "home_view_interface.h"
#include "button_manager.h"
#include "logic.h"
#include "window_manager.h"



#if defined(TOUCH_SCREEN)
typedef struct 
{
    lv_obj_t *btn_up  ;
    lv_obj_t *btn_down;
    lv_obj_t *btn_set ;
    lv_obj_t *btn_back;
}virtual_button_t;
static virtual_button_t vir_buttons = {0};


void on_btn_clicked(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)e->user_data;
    if (LV_EVENT_CLICKED == lv_event_get_code(e) )
    {
        if (obj == vir_buttons.btn_up)
        {
            button_manager_set_click(CLICK_SHORT_UP);
        }
        else if(obj == vir_buttons.btn_down)
        {
            button_manager_set_click(CLICK_SHORT_DOWN);
        }
        else if(obj == vir_buttons.btn_set)
        {
            button_manager_set_click(CLICK_SHORT_SET);
        }
        else if(obj == vir_buttons.btn_back)
        {
            button_manager_set_click(CLICK_SHORT_BACK );
        }
    }else if (LV_EVENT_LONG_PRESSED == lv_event_get_code(e))
    {
        lv_log("buttons long press!");
        if(obj == vir_buttons.btn_set)
            button_manager_set_click(CLICK_LONG_SET);
    }
    
}

void virtual_button_init()
{
    vir_buttons.btn_up   = lv_btn_create(lv_layer_top()) ;
    vir_buttons.btn_down = lv_btn_create(lv_layer_top()) ;
    vir_buttons.btn_set  = lv_btn_create(lv_layer_top()) ;
    vir_buttons.btn_back = lv_btn_create(lv_layer_top()) ;

    lv_obj_align(vir_buttons.btn_up , LV_ALIGN_TOP_MID ,0,0);
    lv_obj_align(vir_buttons.btn_down , LV_ALIGN_BOTTOM_MID ,0,0);
    lv_obj_align(vir_buttons.btn_back , LV_ALIGN_LEFT_MID ,0,0);
    lv_obj_align(vir_buttons.btn_set , LV_ALIGN_RIGHT_MID ,0,0);

    lv_obj_set_style_bg_color(vir_buttons.btn_up , lv_color_hex(0x5a5913) ,    LV_STATE_DEFAULT | LV_PART_MAIN);
    lv_obj_set_style_bg_color(vir_buttons.btn_down , lv_color_hex(0x5a5913) ,  LV_STATE_DEFAULT | LV_PART_MAIN);
    lv_obj_set_style_bg_color(vir_buttons.btn_set , lv_color_hex(0x5a5913) ,   LV_STATE_DEFAULT | LV_PART_MAIN);
    lv_obj_set_style_bg_color(vir_buttons.btn_back , lv_color_hex(0x5a5913) ,  LV_STATE_DEFAULT | LV_PART_MAIN);

    lv_obj_add_event_cb(vir_buttons.btn_up , on_btn_clicked ,LV_EVENT_ALL ,vir_buttons.btn_up );
    lv_obj_add_event_cb(vir_buttons.btn_down , on_btn_clicked ,LV_EVENT_ALL ,vir_buttons.btn_down );
    lv_obj_add_event_cb(vir_buttons.btn_set , on_btn_clicked ,LV_EVENT_ALL ,vir_buttons.btn_set );
    lv_obj_add_event_cb(vir_buttons.btn_back , on_btn_clicked ,LV_EVENT_ALL ,vir_buttons.btn_back );

}
#endif 

void custom_init()
{
    /* 开机动画已提前到 ui_init() 播放，动画播完由 boot_animation_finish_cb()
     * 回调进入 custom_init()，此处不再重复启动动画 */

    /* Add your codes here */
    home_logic_init();

}

