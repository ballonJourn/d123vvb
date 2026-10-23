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
    focus_yes = false;
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

/* ===== 按键处理（表驱动） ===== */
static void unlock_seat_deal_up(void)
{
    focus_yes = !focus_yes;
    tpl_yn_set_focus(focus_yes);
}

static void unlock_seat_deal_down(void)
{
    focus_yes = !focus_yes;
    tpl_yn_set_focus(focus_yes);
}

static void unlock_seat_deal_set(void)
{
    /* YES=发送 0x451 Bat_Seat=1(开座), NO=无动作 */
    if (focus_yes)
        can_tx_set_bat_seat(1);
    unlock_seat_back_to_l1();
}

static void unlock_seat_deal_back(void)
{
    unlock_seat_back_to_l1();
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = unlock_seat_deal_up,
    [CLICK_SHORT_DOWN] = unlock_seat_deal_down,
    [CLICK_SHORT_SET]  = unlock_seat_deal_set,
    [CLICK_SHORT_BACK] = unlock_seat_deal_back,
};

void on_unlock_seat_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}