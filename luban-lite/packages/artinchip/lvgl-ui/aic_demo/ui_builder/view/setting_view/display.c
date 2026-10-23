#include "display.h"
#include "setting_menu.h"
#include "template.h"
#include "vehicle_argument.h"
#include "ui_theme.h"
#include "set_page_key.h"
#include "brightness.h"

/* ===== 静态状态 ===== */
static int focus_slot = 0;   /* 0=Daytime, 1=Night, 2=Auto */

#define DISPLAY_ITEM_NUM  3

/* slot → 主题枚举（值与 logic.c/ui_theme.c 约定一致） */
static const display_option_e slot_to_option[DISPLAY_ITEM_NUM] = {
    DIAPLAY_DAY_OPTION,    /* slot 0: Daytime */
    DIAPLAY_NIGHT_OPTION,  /* slot 1: Night   */
    DIAPLAY_AUTO_OPTION,   /* slot 2: Auto    */
};
/* slot → 持久化 param 值 */
static const uint8_t slot_to_param[DISPLAY_ITEM_NUM] = {
    VEHICLE_DISPLAY_MODE_DAY,
    VEHICLE_DISPLAY_MODE_NIGHT,
    VEHICLE_DISPLAY_MODE_AUTO,
};

/* 项显示名——走翻译表 */
static const key_id_t item_key[DISPLAY_ITEM_NUM] = {
    daytime,
    night,
    auto_TXT,
};

/* 刷新：各槽位选中态 + 项文字（模板控件） */
static void display_refresh(void)
{
    for (int s = 0; s < DISPLAY_ITEM_NUM; s++) {
        tpl3_set_slot(s, aic_get_translation(item_key[s]), true);
        tpl3_set_card_pressed(s, s == focus_slot);
    }
}

void display_init(void)
{
    set_current_menu_level(MENU_LEVEL_4);

    tpl3_activate(aic_get_translation(scene_title), NULL);
    tpl3_set_label_mode(false, true);   /* 左对齐 + arrow */

    /* 取已存主题 param（0=白天,1=黑夜,2=自动），值与槽位序号对齐 */
    uint8_t value = vehicle_get_param_display();
    focus_slot = (value < DISPLAY_ITEM_NUM) ? (int)value : 0;
    display_refresh();
}

/* 语言切换刷新：保留焦点位置 */
void display_relang(void)
{
    tpl3_activate(aic_get_translation(scene_title), NULL);
    tpl3_set_label_mode(false, true);
    display_refresh();
}

void display_destroy(void)
{
    focus_slot = 0;
}

/* ===== 按键处理（3项环形，表驱动） ===== */
static void display_deal_up(void)
{
    focus_slot = (focus_slot - 1 + DISPLAY_ITEM_NUM) % DISPLAY_ITEM_NUM;
    display_refresh();
}

static void display_deal_down(void)
{
    focus_slot = (focus_slot + 1) % DISPLAY_ITEM_NUM;
    display_refresh();
}

static void display_deal_set(void)
{
    /* 持久化 param + 即时主题刷新（枚举值与 logic.c/ui_theme.c 约定一致） */
    vehicle_set_param_display(slot_to_param[focus_slot]);
    global_refresh_display((int)slot_to_option[focus_slot]);
}

static void display_deal_back(void)
{
    set_active_l4_handler(NULL);
    set_active_l4_lang_refresh(NULL);
    tpl3_hide();
    display_destroy();
    brightness_init();   /* 回 L3，重配 tpl3 为 brightness */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = display_deal_up,
    [CLICK_SHORT_DOWN] = display_deal_down,
    [CLICK_SHORT_SET]  = display_deal_set,
    [CLICK_SHORT_BACK] = display_deal_back,
};

void on_display_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}
