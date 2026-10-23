#include "meter_view.h"
#include "setting_menu.h"
#include "template.h"
#include "aic_ui.h"

/* ===== 静态状态 ===== */
static meter_item_e focus_slot = METER_TIRE;

/* 项显示名——走翻译表 */
static const key_id_t item_key[METER_ITEM_NUM] = {
    tire_pressure,
    view_faults,
    sw_version,
};

meter_item_e get_l2_meter_index(void) { return focus_slot; }

/* 刷新：各槽位选中态 + 项文字（模板控件） */
static void meter_view_refresh(void)
{
    for (int s = 0; s < METER_ITEM_NUM; s++) {
        tpl3_set_slot(s, aic_get_translation(item_key[s]), true);
        tpl3_set_card_pressed(s, s == (int)focus_slot);
    }
}

void meter_view_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_2);
    lv_obj_add_flag(page->pop_layer_l1, LV_OBJ_FLAG_HIDDEN);

    tpl3_activate(aic_get_translation(meter_view),
                  LVGL_IMAGE_PATH(setting/icon_menu_meterview.png));
    tpl3_set_label_mode(false, true);   /* 左对齐 + arrow */

    /* 焦点不复位：本入口与"从 L3(view_faults / sw_version) 返回"共用，
     * 复位留给 meter_view_destroy()（本页退出到 L1 时调用） */
    meter_view_refresh();
}

/* 语言切换刷新：保留焦点位置 */
void meter_view_relang(void)
{
    tpl3_activate(aic_get_translation(meter_view),
                  LVGL_IMAGE_PATH(setting/icon_menu_meterview.png));
    tpl3_set_label_mode(false, true);
    meter_view_refresh();
}

void meter_view_destroy(void)
{
    focus_slot = METER_TIRE;
}

/* ===== 按键处理（环形移动，表驱动） ===== */
static void meter_view_deal_up(void)
{
    focus_slot = (meter_item_e)((focus_slot - 1 + METER_ITEM_NUM) % METER_ITEM_NUM);
    meter_view_refresh();
}

static void meter_view_deal_down(void)
{
    focus_slot = (meter_item_e)((focus_slot + 1) % METER_ITEM_NUM);
    meter_view_refresh();
}

static void meter_view_deal_set(void)
{
    /* L3 升级由 menu_manager_click 经 entry_level_3_meter 表判定 */
}

static void meter_view_deal_back(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_1);
    lv_obj_remove_flag(page->pop_layer_l1, LV_OBJ_FLAG_HIDDEN);
    setting_l1_set_focused_item(get_l1_index());
    tpl3_hide();
    meter_view_destroy();
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = meter_view_deal_up,
    [CLICK_SHORT_DOWN] = meter_view_deal_down,
    [CLICK_SHORT_SET]  = meter_view_deal_set,
    [CLICK_SHORT_BACK] = meter_view_deal_back,
};

void on_meter_view_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}
