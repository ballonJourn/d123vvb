#include "cdc_custom.h"
#include "setting_menu.h"
#include "template.h"
#include "set_page_key.h"
#include "cdc_settings.h"

/* ===== 静态状态 ===== */
static int focus_slot = 0;   /* 0=Previous, 1=After */

/* 项显示名 */
static const char *item_name[2] = {
    "Previous: (Number)",
    "After: (Number)",
};

/* 刷新：各槽位选中态 + 项文字（模板控件，隐藏第3槽） */
static void cdc_custom_refresh(void)
{
    for (int s = 0; s < 2; s++) {
        tpl3_set_slot(s, item_name[s], true);
        tpl3_set_card_pressed(s, s == focus_slot);
    }
    tpl3_set_slot(2, NULL, false);   /* 隐藏第3槽 */
}

void cdc_custom_init(void)
{
    set_current_menu_level(MENU_LEVEL_4);
    /* L3 cdc_settings 共用 tpl4 模板（无独立 layer），tpl3_activate 已隐 tpl4 */

    tpl3_activate("CUSTOM", NULL);
    tpl3_set_label_mode(false, false);   /* 左对齐 + 无 arrow */

    focus_slot = 0;
    cdc_custom_refresh();
}

void cdc_custom_destroy(void)
{
    focus_slot = 0;
}

/* ===== 按键处理（2项环形，表驱动） ===== */
static void cdc_custom_deal_up(void)
{
    focus_slot = (focus_slot - 1 + 2) % 2;
    cdc_custom_refresh();
}

static void cdc_custom_deal_down(void)
{
    focus_slot = (focus_slot + 1) % 2;
    cdc_custom_refresh();
}

static void cdc_custom_deal_set(void)
{
    /* 占位：仅切换高亮，预留数值调整接口 */
}

static void cdc_custom_deal_back(void)
{
    set_active_l4_handler(NULL);
    tpl3_hide();
    cdc_custom_destroy();
    cdc_settings_init();   /* 回 L3，重配 tpl4 为 cdc settings */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = cdc_custom_deal_up,
    [CLICK_SHORT_DOWN] = cdc_custom_deal_down,
    [CLICK_SHORT_SET]  = cdc_custom_deal_set,
    [CLICK_SHORT_BACK] = cdc_custom_deal_back,
};

void on_cdc_custom_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}