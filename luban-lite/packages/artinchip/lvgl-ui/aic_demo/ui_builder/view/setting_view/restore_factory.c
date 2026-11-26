#include "restore_factory.h"
#include "setting_menu.h"
#include "template.h"
#include "instrument.h"
#include "vehicle_argument.h"

/* ===== 静态状态 ===== */
static bool focus_yes = false;   /* true=YES 选中, false=NO 选中 */

void restore_factory_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_3);

    tpl_yn_activate(aic_get_translation(restore_factory_title));
    focus_yes = true;
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

/* ===== 确认事件（淡出淡入动画结束后执行） ===== */
static void restore_factory_focus_yes(void)
{
    /* 删除落盘的用户参数 JSON 并立即重启; 成功时不返回 */
    vehicle_restore_factory();
    /* 仅删除失败时才会走到这里: 退回 L2 */
    tpl_yn_hide();
    restore_factory_destroy();
    instrument_init();
}

static void restore_factory_focus_no(void)
{
    tpl_yn_hide();
    restore_factory_destroy();
    instrument_init();   /* 回 L2，重配 tpl3 为 instrument */
}

/* ===== 按键处理（表驱动） =====
 * 弹窗期间 UP/DOWN 不响应；SET=YES、BACK=NO，均为"切状态 → 淡出淡入 → 执行事件" */
static void restore_factory_deal_set(void)
{
    focus_yes = true;
    tpl_yn_confirm(true, restore_factory_focus_yes);
}

static void restore_factory_deal_back(void)
{
    focus_yes = false;
    tpl_yn_confirm(false, restore_factory_focus_no);
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_SET]  = restore_factory_deal_set,
    [CLICK_SHORT_BACK] = restore_factory_deal_back,
};

void on_restore_factory_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}
