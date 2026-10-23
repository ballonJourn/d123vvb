#include "brightness.h"
#include "setting_menu.h"
#include "template.h"
#include "set_page_key.h"
#include "display.h"
#include "brightness_bar.h"
#include "instrument.h"

/* ===== 静态状态 ===== */
static brightness_l3_item_e focus_slot = BRIGHTNESS_L3_SCENE;

/* 项显示名——走翻译表 */
static const key_id_t item_key[BRIGHTNESS_L3_ITEM_NUM] = {
    scene,
    brightness,
};

brightness_l3_item_e get_l3_brightness_focus(void) { return focus_slot; }

/* 刷新：各槽位选中态 + 项文字（模板控件） */
static void brightness_refresh(void)
{
    for (int s = 0; s < BRIGHTNESS_L3_ITEM_NUM; s++) {
        tpl3_set_slot(s, aic_get_translation(item_key[s]), true);
        tpl3_set_card_pressed(s, s == (int)focus_slot);
    }
    tpl3_set_slot(2, NULL, false);   /* 隐藏第3槽 */
}

void brightness_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_3);

    tpl3_activate(aic_get_translation(brightness_settings_title), NULL);
    tpl3_set_label_mode(false, true);   /* 左对齐 + arrow */

    /* 焦点不复位：本入口与"从 L4(scene / brightness bar) 返回"共用，
     * 复位留给 brightness_destroy()（本页退出到 L2 时调用） */
    brightness_refresh();
}

/* 语言切换刷新：保留焦点位置 */
void brightness_relang(void)
{
    tpl3_activate(aic_get_translation(brightness_settings_title), NULL);
    tpl3_set_label_mode(false, true);
    brightness_refresh();
}

void brightness_destroy(void)
{
    focus_slot = BRIGHTNESS_L3_SCENE;
}

/* ===== 按键处理（2项环形，表驱动） ===== */
static void brightness_deal_up(void)
{
    focus_slot = (brightness_l3_item_e)((focus_slot - 1 + BRIGHTNESS_L3_ITEM_NUM) % BRIGHTNESS_L3_ITEM_NUM);
    brightness_refresh();
}

static void brightness_deal_down(void)
{
    focus_slot = (brightness_l3_item_e)((focus_slot + 1) % BRIGHTNESS_L3_ITEM_NUM);
    brightness_refresh();
}

static void brightness_deal_set(void)
{
    /* 按 focus 分派 L4，并注册 L4 按键处理器与语言刷新处理器 */
    if (focus_slot == BRIGHTNESS_L3_SCENE) {
        set_active_l4_handler(on_display_deal_short_key);
        set_active_l4_lang_refresh(display_relang);
        display_init();
    } else {
        set_active_l4_handler(on_brightness_bar_deal_short_key);
        set_active_l4_lang_refresh(brightness_bar_relang);
        brightness_bar_init();
    }
}

static void brightness_deal_back(void)
{
    tpl3_hide();
    brightness_destroy();
    instrument_init();   /* 回 L2，重配 tpl3 为 instrument */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = brightness_deal_up,
    [CLICK_SHORT_DOWN] = brightness_deal_down,
    [CLICK_SHORT_SET]  = brightness_deal_set,
    [CLICK_SHORT_BACK] = brightness_deal_back,
};

void on_brightness_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}
