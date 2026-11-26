#include "instrument.h"
#include "setting_menu.h"
#include "template.h"
#include "aic_ui.h"

/* ===== 翻页状态（选项表索引，页/槽位由它换算） ===== */
static int item_idx = 0;

/* 每页固定 INSTRUMENT_SLOT_NUM 槽（= tpl3 槽数），窗口在表上滚动，
 * up/down 到表尾/表头首尾相接 */
#define INSTRUMENT_SLOT_NUM   3

/* 选项表：一维，表顺序即翻页顺序。尺寸取 instrument_item_e 的项数，项数与枚举
 * 不符时编译期即报错；顺序必须与枚举值一致 —— set_page_key 用枚举值直接索引
 * L3 路由表（setting_entry_level_3[cat][item]）。 */
static const instrument_item_e item_list[INSTRUMENT_ITEM_NUM] = {
    INSTRUMENT_LANGUAGE, INSTRUMENT_BRIGHTNESS, INSTRUMENT_TIME,
    INSTRUMENT_UNIT,     INSTRUMENT_TCS,        INSTRUMENT_FACTORY_RESET,
};

/* 项显示名：TCS 按需求保持英文缩写字面量，其余走翻译表 */
static const char *instrument_item_name(instrument_item_e item)
{
    switch (item) {
    case INSTRUMENT_LANGUAGE:      return aic_get_translation(language_settings);
    case INSTRUMENT_BRIGHTNESS:    return aic_get_translation(brightness_settings);
    case INSTRUMENT_TIME:          return aic_get_translation(time_format);
    case INSTRUMENT_UNIT:          return aic_get_translation(unit_switch);
    case INSTRUMENT_TCS:           return "TCS switch";
    case INSTRUMENT_FACTORY_RESET: return aic_get_translation(restore_factory);
    default:                       return "";
    }
}

instrument_item_e get_l2_instrument_index(void)
{
    return item_list[item_idx];
}

/* 刷新当前页：窗口内各槽位选中态 + 项文字（模板控件）；表中已无对应项的槽隐藏 */
static void instrument_refresh_page(void)
{
    int page = item_idx / INSTRUMENT_SLOT_NUM;
    for (int s = 0; s < INSTRUMENT_SLOT_NUM; s++) {
        int i = page * INSTRUMENT_SLOT_NUM + s;
        if (i >= INSTRUMENT_ITEM_NUM) {
            tpl3_set_slot(s, NULL, false);
            continue;
        }
        tpl3_set_slot(s, instrument_item_name(item_list[i]), true);
        tpl3_set_card_pressed(s, i == item_idx);
    }
}

void instrument_init_at(instrument_item_e item)
{
    setting_page_t *scr = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_2);
    lv_obj_add_flag(scr->pop_layer_l1, LV_OBJ_FLAG_HIDDEN);

    tpl3_activate(aic_get_translation(instrument_settings_title),
                  LVGL_IMAGE_PATH(setting/icon_menu_instrument.png));
    tpl3_set_label_mode(false, true);   /* 左对齐 + arrow */

    /* 定位到表中对应索引；不在表中（含被注释掉的项）则停在首项 */
    item_idx = 0;
    for (int i = 0; i < INSTRUMENT_ITEM_NUM; i++) {
        if (item_list[i] == item) {
            item_idx = i;
            break;
        }
    }
    instrument_refresh_page();
}

/* 语言切换刷新：保留翻页/焦点位置，只重跑激活与填字 */
void instrument_relang(void)
{
    tpl3_activate(aic_get_translation(instrument_settings_title),
                  LVGL_IMAGE_PATH(setting/icon_menu_instrument.png));
    tpl3_set_label_mode(false, true);
    instrument_refresh_page();
}

/* 从 L1 下钻 / 从 L3 返回共用入口：以当前焦点项重入，保留上次焦点
 * （本页退出到 L1 时由 instrument_destroy() 复位到首项） */
void instrument_init(void)
{
    instrument_init_at(get_l2_instrument_index());
}

void instrument_destroy(void)
{
    item_idx = 0;
}

/* ===== 按键处理（在选项表上循环移动，首尾相接） ===== */
static void instrument_move(int step)
{
    item_idx = (item_idx + step + INSTRUMENT_ITEM_NUM) % INSTRUMENT_ITEM_NUM;
    instrument_refresh_page();
}

static void instrument_deal_down(void)
{
    instrument_move(1);
}

static void instrument_deal_up(void)
{
    instrument_move(-1);
}

static void instrument_deal_set(void)
{
    /* L3 升级由 menu_manager_click 经 entry_level_3 表判定 */
}

static void instrument_deal_back(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_1);
    lv_obj_remove_flag(page->pop_layer_l1, LV_OBJ_FLAG_HIDDEN);
    setting_l1_set_focused_item(get_l1_index());
    tpl3_hide();
    instrument_destroy();
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = instrument_deal_up,
    [CLICK_SHORT_DOWN] = instrument_deal_down,
    [CLICK_SHORT_SET]  = instrument_deal_set,
    [CLICK_SHORT_BACK] = instrument_deal_back,
};

void on_instrument_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}
