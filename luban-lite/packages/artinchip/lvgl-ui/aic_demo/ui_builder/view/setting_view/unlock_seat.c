#include "unlock_seat.h"
#include "setting_menu.h"
#include "template.h"
#include "mw_can_tx.h"

/* ===== 静态状态 ===== */
static bool focus_yes = false;   /* true=YES 选中, false=NO 选中 */

void unlock_seat_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_2);
    lv_obj_add_flag(page->pop_layer_l1, LV_OBJ_FLAG_HIDDEN);

    tpl_yn_activate(aic_get_translation(unlock_seat));
    focus_yes = true;
    tpl_yn_set_focus(focus_yes);
}

/* 语言切换刷新：保留 YES/NO 选中态 */
void unlock_seat_relang(void)
{
    tpl_yn_activate(aic_get_translation(unlock_seat));
    tpl_yn_set_focus(focus_yes);
}

void unlock_seat_destroy(void)
{
    focus_yes = false;
}

/* 回 L1：隐 yn，恢复 L1 卡片层 */
static void unlock_seat_back_to_l1(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_1);
    lv_obj_remove_flag(page->pop_layer_l1, LV_OBJ_FLAG_HIDDEN);
    setting_l1_set_focused_item(get_l1_index());
    tpl_yn_hide();
    unlock_seat_destroy();
}

/* ===== 确认事件（淡出淡入动画结束后执行） ===== */
static void unlock_seat_focus_yes(void)
{
    /* YES=发送 0x451 Bat_Seat=1(开座) */
    can_tx_set_bat_seat(1);
    unlock_seat_back_to_l1();
}

static void unlock_seat_focus_no(void)
{
    unlock_seat_back_to_l1();
}

/* ===== 按键处理（表驱动） =====
 * 弹窗期间 UP/DOWN 不响应；SET=YES、BACK=NO，均为"切状态 → 淡出淡入 → 执行事件" */
static void unlock_seat_deal_set(void)
{
    focus_yes = true;
    tpl_yn_confirm(true, unlock_seat_focus_yes);
}

static void unlock_seat_deal_back(void)
{
    focus_yes = false;
    tpl_yn_confirm(false, unlock_seat_focus_no);
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_SET]  = unlock_seat_deal_set,
    [CLICK_SHORT_BACK] = unlock_seat_deal_back,
};

void on_unlock_seat_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}