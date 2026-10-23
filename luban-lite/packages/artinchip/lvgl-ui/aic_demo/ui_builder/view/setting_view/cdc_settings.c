#include "cdc_settings.h"
#include "setting_menu.h"
#include "template.h"
#include "set_page_key.h"
#include "cdc_custom.h"
#include "instrument.h"

/* ===== 静态状态 ===== */
static cdc_item_e focus_slot = CDC_CUSTOM;

/* 项显示名 */
static const char *item_name[CDC_ITEM_NUM] = {
    "Custom",
    "Leisure",
    "Comfortable",
    "Sports",
};

/* 刷新：各槽位选中态 + 项文字（模板控件） */
static void cdc_refresh(void)
{
    for (int s = 0; s < CDC_ITEM_NUM; s++)
        tpl4_set_card_pressed(s, s == (int)focus_slot);
}

void cdc_settings_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_3);

    tpl4_activate("CDC REGULATION", NULL);            /* 无图标 → 标题居中 */
    tpl4_set_label_mode(false, true);                 /* 左对齐 + arrow */
    for (int s = 0; s < CDC_ITEM_NUM; s++)
        tpl4_set_slot(s, item_name[s], true);

    /* 焦点不复位：本入口与"从 L4(cdc_custom) 返回"共用，
     * 复位留给 cdc_settings_destroy()（本页退出到 L2 时调用） */
    cdc_refresh();
}

void cdc_settings_destroy(void)
{
    focus_slot = CDC_CUSTOM;
}

/* ===== 按键处理（环形移动，表驱动） ===== */
static void cdc_deal_up(void)
{
    focus_slot = (cdc_item_e)((focus_slot - 1 + CDC_ITEM_NUM) % CDC_ITEM_NUM);
    cdc_refresh();
}

static void cdc_deal_down(void)
{
    focus_slot = (cdc_item_e)((focus_slot + 1) % CDC_ITEM_NUM);
    cdc_refresh();
}

static void cdc_deal_set(void)
{
    /* 按 focus 分派 L4，并注册 L4 按键处理器 */
    if (focus_slot == CDC_CUSTOM) {
        set_active_l4_handler(on_cdc_custom_deal_short_key);
        cdc_custom_init();
    }
    /* 其他档位（Leisure / Comfortable / Sports）暂无 L4，占位 */
}

static void cdc_deal_back(void)
{
    tpl4_hide();
    cdc_settings_destroy();
    instrument_init();   /* 回 L2，重配 tpl3 为 instrument */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = cdc_deal_up,
    [CLICK_SHORT_DOWN] = cdc_deal_down,
    [CLICK_SHORT_SET]  = cdc_deal_set,
    [CLICK_SHORT_BACK] = cdc_deal_back,
};

void on_cdc_settings_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}