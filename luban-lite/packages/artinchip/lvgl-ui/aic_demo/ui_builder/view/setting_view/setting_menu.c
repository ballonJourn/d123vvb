#include "setting_menu.h"
#include <stddef.h>
#include "definition.h"
#include "aic_ui.h"
#include "window_manager.h"
#include "set_page_key.h"

static lv_obj_t* setting_menu_widget[SETTING_MENU_NUM_MAX] = { NULL };
static lv_obj_t* foucused_img = NULL;

void setting_menu_view_init()
{
    setting_page_t *page = setting_page_get(&ui_manager);
    VIEW_MEMBER_VALID(page , setting_menu) ;
    int count = lv_obj_get_child_count(setting_menu) ;
    for (int i = 0; i < count; i++)
    {
        setting_menu_widget[i] = lv_obj_get_child(setting_menu , i);
    }
    
    setting_menu_set_focused_item(SETTING_MENU_TMPS) ;
    return  ;
}


void setting_menu_set_focused_item(setting_menu_e item)
{
    if(item > SETTING_MENU_DEVICE)
        return ;

    setting_page_t *page = setting_page_get(&ui_manager);
    // VIEW_MEMBER_VALID(page , setting_menu);
    VIEW_MEMBER_VALID(page , tabView);
    VIEW_MEMBER_VALID(page , setting_line);

    if (foucused_img)
        lv_obj_delete(foucused_img) ;
    
    for (int i = 0; i < SETTING_MENU_NUM_MAX; i++)
    {
        lv_obj_t * menu = setting_menu_widget[i]; 
        if (menu)
        {
            if(i == item){
                lv_obj_set_style_text_color(menu ,lv_color_hex(0xEA9036) , 0 );
                foucused_img = lv_image_create(menu);
                lv_image_set_src(foucused_img , LVGL_IMAGE_PATH(setting/setting_instruction.png)) ;
                lv_obj_set_pos(foucused_img,0,6);
            }else{
                lv_obj_set_style_text_color(menu ,lv_color_hex(0xffffff) , 0 );
            }

        }
        
    }

    if(item == 0)
        lv_obj_add_flag(setting_line , LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(setting_line , LV_OBJ_FLAG_HIDDEN);
    
    lv_tabview_set_active(tabView , item , LV_ANIM_OFF);
    lv_obj_scroll_to_view(setting_menu_widget[item] , LV_ANIM_OFF);

    return ;
}


static setting_menu_e menu_index = SETTING_MENU_TMPS ;

//初始状态为0级别 进去music 、setting为一级  setting进入option选项为二级 时间调整为三级
static menu_level_e current_level = MENU_LEVEL_1 ;   


int get_current_menu_index()
{
    return menu_index ;
}

int get_current_menu_level()
{
    return current_level ;
}

void set_current_menu_level(menu_level_e level) 
{
    if(level > MENU_LEVEL_3)
        return;
    
    current_level = level ;
}

void set_menu_deal_key_down ()
{
    menu_index = ( menu_index + 1 ) % SETTING_MENU_NUM_MAX ;
    setting_menu_set_focused_item(menu_index) ;
}


void set_menu_deal_key_up   ()
{
    menu_index = ( menu_index - 1 + SETTING_MENU_NUM_MAX) % SETTING_MENU_NUM_MAX ;
    setting_menu_set_focused_item( menu_index ) ;
}

void set_menu_deal_key_set  ()
{
    if(check_clicked_isVaild(menu_index))
        current_level = MENU_LEVEL_2  ;
}


void set_menu_deal_key_back ()
{
    // setting_menu_destory();
    window_manager_set_target(WINDOW_MAIN);
    printf("set_menu_deal_key_back to main");
    setting_menu_destory() ;
    
}


static click_handler_t click_handlers[CLICK_END] = {
    [CLICK_SHORT_SET]  = set_menu_deal_key_set,
    [CLICK_SHORT_BACK] = set_menu_deal_key_back,
    [CLICK_SHORT_UP]   = set_menu_deal_key_up,
    [CLICK_SHORT_DOWN] = set_menu_deal_key_down,
};


void setting_menu_destory()
{
    for (size_t i = 0; i < SETTING_MENU_NUM_MAX; i++){
        setting_menu_widget[i] = NULL;
    }

    if(foucused_img)
        lv_obj_delete(foucused_img);

    foucused_img = NULL;
    menu_index = SETTING_MENU_TMPS ;
}

void on_setting_menu_deal_short_click(click_e click) 
{
    if(click < CLICK_END && click_handlers[click])
        click_handlers[click]();
}