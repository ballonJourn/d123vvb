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
    focus_yes = true;
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

/* ===== 确认事件（淡出淡入动画结束后执行） ===== */
/* 0x451 TCS_Command: 0无 1开 2关；确认后均返回 L2（重配 tpl3 为 instrument） */
static void tcs_switch_focus_yes(void)
{
    can_tx_set_tcs_command(1);
    tpl_yn_hide();
    tcs_switch_destroy();
    instrument_init();
}

static void tcs_switch_focus_no(void)
{
    can_tx_set_tcs_command(2);
    tpl_yn_hide();
    tcs_switch_destroy();
    instrument_init();
}

/* ===== 按键处理（表驱动） =====
 * 弹窗期间 UP/DOWN 不响应；SET=YES(开)、BACK=NO(关)，均为"切状态 → 淡出淡入 → 执行事件" */
static void tcs_switch_deal_set(void)
{
    focus_yes = true;
    tpl_yn_confirm(true, tcs_switch_focus_yes);
}

static void tcs_switch_deal_back(void)
{
    focus_yes = false;
    tpl_yn_confirm(false, tcs_switch_focus_no);
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_SET]  = tcs_switch_deal_set,
    [CLICK_SHORT_BACK] = tcs_switch_deal_back,
};

void on_tcs_switch_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}
