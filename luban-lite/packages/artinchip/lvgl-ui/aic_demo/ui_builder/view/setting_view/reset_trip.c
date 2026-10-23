#include "reset_trip.h"
#include "setting_menu.h"
#include "template.h"
#include "vehicle_argument.h"
#include "aic_lang.h"

/* 槽位与取值的对应：槽0=下电清零(1)、槽1=按键清零(0)。
 * 槽位顺序由需求固定（下电在前），勿按取值大小重排 */
typedef enum {
    RESET_TRIP_POWEROFF_SLOT = 0,   /* Reset after power off */
    RESET_TRIP_BUTTON_SLOT,         /* Reset by button */
    RESET_TRIP_SLOT_NUM,
} reset_trip_slot_e;

static const key_id_t slot_key[RESET_TRIP_SLOT_NUM] = {
    reset_after_power_off,
    reset_by_button,
};

static const uint8_t slot_value[RESET_TRIP_SLOT_NUM] = {
    VEHICLE_RESET_AFTER_POWER_OFF,
    VEHICLE_RESET_BY_BUTTON,
};

/* ===== 静态状态 ===== */
static reset_trip_slot_e focus_slot  = RESET_TRIP_BUTTON_SLOT;  /* 列表焦点槽位 */
static bool             confirm_yes  = false;                   /* tpl_yn 选中态 */
static bool             in_confirm   = false;                   /* true=当前显示 YES/NO 确认层 */

/* 取值→槽位；取值越界时回退按键清零（与中间层 check_usr_param 的兜底一致） */
static reset_trip_slot_e slot_of_value(uint8_t value)
{
    for (int s = 0; s < RESET_TRIP_SLOT_NUM; s++) {
        if (slot_value[s] == value)
            return (reset_trip_slot_e)s;
    }
    return RESET_TRIP_BUTTON_SLOT;
}

/* 刷新选项列表：槽位文字（走翻译表）+ 选中态，并隐掉第 3 槽 */
static void reset_trip_refresh(void)
{
    for (int s = 0; s < RESET_TRIP_SLOT_NUM; s++) {
        tpl3_set_slot(s, aic_get_translation(slot_key[s]), true);
        tpl3_set_card_pressed(s, s == (int)focus_slot);
    }
    tpl3_set_slot(2, NULL, false);   /* 隐藏第3槽 */
}

/* 显示选项列表（init / 退出确认层共用）。
 * tpl3_activate 不会自动隐 tpl_yn，从确认层退回须显式隐 */
static void reset_trip_show_list(void)
{
    in_confirm = false;
    tpl_yn_hide();
    tpl3_activate(aic_get_translation(reset_trip), NULL);
    tpl3_set_label_mode(true, false);   /* 居中 + 无 arrow（选项即终值，无下级页） */
    reset_trip_refresh();
}

void reset_trip_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_2);
    lv_obj_add_flag(page->pop_layer_l1, LV_OBJ_FLAG_HIDDEN);

    /* 取当前生效的清零方式定位焦点 */
    focus_slot  = slot_of_value(vehicle_get_param_reset_type());
    confirm_yes = false;
    reset_trip_show_list();
}

/* 语言切换刷新：确认层标题是选中项文本，须与新语言一起重取 */
void reset_trip_relang(void)
{
    if (in_confirm) {
        tpl_yn_activate(aic_get_translation(slot_key[focus_slot]));
        tpl_yn_set_focus(confirm_yes);
        return;
    }
    reset_trip_show_list();
}

void reset_trip_destroy(void)
{
    focus_slot  = RESET_TRIP_BUTTON_SLOT;
    confirm_yes = false;
    in_confirm  = false;
}

/* 回 L1：恢复卡片层并复位本页状态 */
static void reset_trip_back_to_l1(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_1);
    lv_obj_remove_flag(page->pop_layer_l1, LV_OBJ_FLAG_HIDDEN);
    setting_l1_set_focused_item(get_l1_index());
    tpl3_hide();
    reset_trip_destroy();
}

/* ===== 按键处理（列表 2 项环形 / 确认层 YES-NO，表驱动） ===== */
static void reset_trip_deal_up(void)
{
    if (in_confirm) {
        confirm_yes = !confirm_yes;
        tpl_yn_set_focus(confirm_yes);
        return;
    }
    focus_slot = (reset_trip_slot_e)((focus_slot - 1 + RESET_TRIP_SLOT_NUM) % RESET_TRIP_SLOT_NUM);
    reset_trip_refresh();
}

static void reset_trip_deal_down(void)
{
    if (in_confirm) {
        confirm_yes = !confirm_yes;
        tpl_yn_set_focus(confirm_yes);
        return;
    }
    focus_slot = (reset_trip_slot_e)((focus_slot + 1) % RESET_TRIP_SLOT_NUM);
    reset_trip_refresh();
}

static void reset_trip_deal_set(void)
{
    if (in_confirm) {
        /* YES=按所选项写入清零方式，NO=不写入；两者都退回选项列表，
         * 焦点留在所选项（列表焦点即当前生效值的指示） */
        if (confirm_yes)
            vehicle_set_param_reset_type(slot_value[focus_slot]);
        reset_trip_show_list();
        return;
    }

    /* 进 YES/NO 最后确认，默认聚焦 NO（同 restore_factory / power_down），
     * 标题用所选项文本，用户能看清确认的是哪一项 */
    in_confirm  = true;
    confirm_yes = false;
    tpl_yn_activate(aic_get_translation(slot_key[focus_slot]));
    tpl_yn_set_focus(confirm_yes);
}

static void reset_trip_deal_back(void)
{
    if (in_confirm) {
        reset_trip_show_list();   /* 确认层 BACK = 取消，退回列表 */
        return;
    }
    reset_trip_back_to_l1();
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = reset_trip_deal_up,
    [CLICK_SHORT_DOWN] = reset_trip_deal_down,
    [CLICK_SHORT_SET]  = reset_trip_deal_set,
    [CLICK_SHORT_BACK] = reset_trip_deal_back,
};

void on_reset_trip_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}