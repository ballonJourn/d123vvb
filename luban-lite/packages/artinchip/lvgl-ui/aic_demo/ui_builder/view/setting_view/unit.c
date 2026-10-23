#include "unit.h"
#include "setting_menu.h"
#include "template.h"
#include "instrument.h"
#include "vehicle_argument.h"
#include "aic_lang.h"
#include "logic.h"

/* ===== 静态状态 ===== */
static unit_option_e focus = UNIT_KM_OPTION;

/* 刷新：各槽位项文字（走翻译表，语言切换后重进本页即生效）+ 选中态 */
static void unit_refresh(void)
{
    tpl3_set_slot(UNIT_KM_OPTION,   aic_get_translation(unit_km),   true);
    tpl3_set_slot(UNIT_MILE_OPTION, aic_get_translation(unit_mile), true);
    tpl3_set_slot(2, NULL, false);   /* 隐藏第3槽 */

    for (int s = 0; s < UNIT_OPTION_MAX; s++)
        tpl3_set_card_pressed(s, s == (int)focus);
}

void unit_init(void)
{
    set_current_menu_level(MENU_LEVEL_3);

    tpl3_activate(aic_get_translation(unit_settings_title), NULL);
    tpl3_set_label_mode(true, false);

    /* 取当前生效单位定位焦点（0=公制 1=英制，与槽位序号对齐） */
    uint8_t value = vehicle_get_param_unit();
    focus = (value < UNIT_OPTION_MAX) ? (unit_option_e)value : UNIT_KM_OPTION;
    unit_refresh();
}

/* 语言切换刷新：保留焦点位置 */
void unit_relang(void)
{
    tpl3_activate(aic_get_translation(unit_settings_title), NULL);
    tpl3_set_label_mode(false, true);
    unit_refresh();
}

void unit_destroy(void)
{
    focus = UNIT_KM_OPTION;
}

/* ===== 按键处理（2项环形，表驱动） ===== */
static void unit_deal_up(void)
{
    focus = (unit_option_e)((focus - 1 + UNIT_OPTION_MAX) % UNIT_OPTION_MAX);
    unit_refresh();
}

static void unit_deal_down(void)
{
    focus = (unit_option_e)((focus + 1) % UNIT_OPTION_MAX);
    unit_refresh();
}

static void unit_deal_set(void)
{
    /* 持久化单位 + 按新单位刷新主界面单位文本与速度数字 */
    vehicle_set_param_unit((uint8_t)focus);
    global_refresh_unit((uint8_t)focus);
}

static void unit_deal_back(void)
{
    tpl3_hide();
    unit_destroy();
    instrument_init_at(INSTRUMENT_UNIT);   /* 回 L2，定位到 unit 项 */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = unit_deal_up,
    [CLICK_SHORT_DOWN] = unit_deal_down,
    [CLICK_SHORT_SET]  = unit_deal_set,
    [CLICK_SHORT_BACK] = unit_deal_back,
};

void on_unit_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}