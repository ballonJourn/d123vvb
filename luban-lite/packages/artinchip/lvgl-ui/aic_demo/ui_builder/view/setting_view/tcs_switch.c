#include "tcs_switch.h"
#include "setting_menu.h"
#include "template.h"
#include "instrument.h"
#include "mw_can_tx.h"

/* ===== 静态状态 ===== */
static bool focus_yes = false;   /* true=YES(ON) 选中, false=NO(OFF) 选中 */

void tcs_switch_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_3);

    tpl_yn_activate("TCS SWITCH");   /* TCS 为缩写，按需求不翻译 */
    focus_yes = false;
    tpl_yn_set_focus(focus_yes);
}

/* 语言切换刷新：标题为字面量，只恢复 YES/NO 选中态 */
void tcs_switch_relang(void)
{
    tpl_yn_activate("TCS SWITCH");
    tpl_yn_set_focus(focus_yes);
}

void tcs_switch_destroy(void)
{
    focus_yes = false;
}

/* ===== 按键处理（表驱动） ===== */
static void tcs_switch_deal_up(void)
{
    focus_yes = !focus_yes;
    tpl_yn_set_focus(focus_yes);
}

static void tcs_switch_deal_down(void)
{
    focus_yes = !focus_yes;
    tpl_yn_set_focus(focus_yes);
}

static void tcs_switch_deal_set(void)
{
    /* YES=打开(1) NO=关闭(2)，对应 0x451 TCS_Command: 0无 1开 2关 */
    can_tx_set_tcs_command(focus_yes ? 1 : 2);
    /* YES/NO 确认后均返回 L2 */
    tpl_yn_hide();
    tcs_switch_destroy();
    instrument_init();
}

static void tcs_switch_deal_back(void)
{
    tpl_yn_hide();
    tcs_switch_destroy();
    instrument_init();   /* 回 L2，重配 tpl3 为 instrument */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = tcs_switch_deal_up,
    [CLICK_SHORT_DOWN] = tcs_switch_deal_down,
    [CLICK_SHORT_SET]  = tcs_switch_deal_set,
    [CLICK_SHORT_BACK] = tcs_switch_deal_back,
};

void on_tcs_switch_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}
