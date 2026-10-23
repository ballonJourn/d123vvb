#include "restore_factory.h"
#include "setting_menu.h"
#include "template.h"
#include "instrument.h"

/* ===== 静态状态 ===== */
static bool focus_yes = false;   /* true=YES 选中, false=NO 选中 */

void restore_factory_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_3);

    tpl_yn_activate(aic_get_translation(restore_factory_title));
    focus_yes = false;
    tpl_yn_set_focus(focus_yes);
}

/* 语言切换刷新：保留 YES/NO 选中态 */
void restore_factory_relang(void)
{
    tpl_yn_activate(aic_get_translation(restore_factory_title));
    tpl_yn_set_focus(focus_yes);
}

void restore_factory_destroy(void)
{
    focus_yes = false;
}

/* ===== 按键处理（表驱动） ===== */
static void restore_factory_deal_up(void)
{
    focus_yes = !focus_yes;
    tpl_yn_set_focus(focus_yes);
}

static void restore_factory_deal_down(void)
{
    focus_yes = !focus_yes;
    tpl_yn_set_focus(focus_yes);
}

static void restore_factory_deal_set(void)
{
    if (focus_yes) {
        /* TODO: 执行恢复出厂设置 */
    }
    /* YES/NO 确认后均返回 L2 */
    tpl_yn_hide();
    restore_factory_destroy();
    instrument_init();
}

static void restore_factory_deal_back(void)
{
    tpl_yn_hide();
    restore_factory_destroy();
    instrument_init();   /* 回 L2，重配 tpl3 为 instrument */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = restore_factory_deal_up,
    [CLICK_SHORT_DOWN] = restore_factory_deal_down,
    [CLICK_SHORT_SET]  = restore_factory_deal_set,
    [CLICK_SHORT_BACK] = restore_factory_deal_back,
};

void on_restore_factory_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}
