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
static reset_trip_slot_e focus_slot = RESET_TRIP_BUTTON_SLOT;   /* 列表焦点槽位 */

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

/* 显示选项列表（init / 切语言共用） */
static void reset_trip_show_list(void)
{
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
    focus_slot = slot_of_value(vehicle_get_param_reset_type());
    reset_trip_show_list();
}

/* 语言切换刷新：槽位文字走翻译表，按新语言重填并保留焦点 */
void reset_trip_relang(void)
{
    reset_trip_show_list();
}

void reset_trip_destroy(void)
{
    focus_slot = RESET_TRIP_BUTTON_SLOT;
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

/* 应用所选项：写入清零方式并落盘（同 language 的 apply，无确认层） */
static void reset_trip_apply_option(void)
{
    vehicle_set_param_reset_type(slot_value[focus_slot]);
    vehicle_save_param();
}

/* ===== 按键处理（列表 2 项环形，表驱动） ===== */
static void reset_trip_deal_up(void)
{
    focus_slot = (reset_trip_slot_e)((focus_slot - 1 + RESET_TRIP_SLOT_NUM) % RESET_TRIP_SLOT_NUM);
    reset_trip_refresh();
}

static void reset_trip_deal_down(void)
{
    focus_slot = (reset_trip_slot_e)((focus_slot + 1) % RESET_TRIP_SLOT_NUM);
    reset_trip_refresh();
}

static void reset_trip_deal_set(void)
{
    /* 焦点槽位淡出淡入提示后即时生效（无下级页、无确认层） */
    setting_page_t *page = setting_page_get(&ui_manager);
    tpl_fade_out_in_then(page->pop_tpl3_card[focus_slot], reset_trip_apply_option);
}

static void reset_trip_deal_back(void)
{
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