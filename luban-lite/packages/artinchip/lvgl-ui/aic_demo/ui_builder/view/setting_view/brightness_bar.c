#include "brightness_bar.h"
#include "setting_menu.h"
#include "aic_lang.h"
#include "vehicle_argument.h"
#include "set_page_key.h"
#include "brightness.h"
#include "template.h"

/* ===== 静态状态 ===== */
static int level = 0;        /* 手动亮度等级 0-4，对应 0/25/50/75/100% */
static bool is_auto = false; /* 是否处于自动亮度模式 */

#define BRIGHTNESS_LEVEL_MAX  5
#define BRIGHTNESS_BAR_MIN    0
#define BRIGHTNESS_BAR_MAX    100
#define BRIGHTNESS_LEVEL_MID  2   /* 自动模式的进度条中值（50%） */

/* 自动状态图标：_n 未启用（暗淡） / _p 已启用（明亮） */
#define ICON_AUTO_OFF  LVGL_IMAGE_PATH(setting/btn_auto_brightness_n.png)
#define ICON_AUTO_ON   LVGL_IMAGE_PATH(setting/btn_auto_brightness_p.png)

/* brightness_bar_build() 由各分辨率 setting_page.c 提供（布局随分辨率切换） */

/* 同步进度条与自动状态图标 */
static void brightness_bar_sync_view(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);

    if (page->pop_bar_l4_brightness_bar)
        lv_bar_set_value(page->pop_bar_l4_brightness_bar, level * 25, LV_ANIM_OFF);

    if (page->pop_icon_l4_auto_status)
        lv_img_set_src(page->pop_icon_l4_auto_status, is_auto ? ICON_AUTO_ON : ICON_AUTO_OFF);
}

void brightness_bar_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    brightness_bar_build();
    set_current_menu_level(MENU_LEVEL_4);
    tpl3_hide();   /* 隐 L3 brightness 模板层 */
    lv_obj_remove_flag(page->pop_layer_l4_brightness_bar, LV_OBJ_FLAG_HIDDEN);

    /* 读已存亮度：自动模式进度条落到中值，手动模式为 param-1（param 1..5） */
    uint8_t value = vehicle_get_param_brightness();
    is_auto = vehicle_get_brightness_is_auto();
    if (is_auto)
        level = BRIGHTNESS_LEVEL_MID;
    else
        level = (value >= 1 && value <= BRIGHTNESS_LEVEL_MAX) ? (int)value - 1 : 0;

    brightness_bar_sync_view();
}

/* 语言切换刷新：该层标题由 brightness_bar_build() 建（懒加载），未建则无可刷 */
void brightness_bar_relang(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (!page->pop_title_l4_brightness_bar)
        return;
    lv_obj_set_style_text_font(page->pop_title_l4_brightness_bar,
                               aic_get_multi_lang_font(18), LV_PART_MAIN);
    lv_label_set_text(page->pop_title_l4_brightness_bar,
                      aic_get_translation(brightness_title));
}

void brightness_bar_destroy(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (page->pop_layer_l4_brightness_bar) {
        lv_obj_delete(page->pop_layer_l4_brightness_bar);
        page->pop_layer_l4_brightness_bar = NULL;
        page->pop_top_bg_l4_brightness_bar = NULL;
        page->pop_title_l4_brightness_bar = NULL;
        page->pop_bar_l4_brightness_bar = NULL;
        page->pop_icon_l4_brightness_bar = NULL;
        page->pop_icon_l4_night = NULL;
        page->pop_icon_l4_auto_status = NULL;
    }
    level = 0;
    is_auto = false;
}

/* 刷新进度条并落盘为手动档（调档即退出自动模式） */
static void brightness_bar_refresh(void)
{
    is_auto = false;
    brightness_bar_sync_view();
    vehicle_set_param_brightness((uint8_t)(level + 1));
}

/* ===== 按键处理（表驱动） ===== */
static void brightness_bar_deal_up(void)
{
    level = (level + 1) % BRIGHTNESS_LEVEL_MAX;
    brightness_bar_refresh();
}

static void brightness_bar_deal_down(void)
{
    level = (level - 1 + BRIGHTNESS_LEVEL_MAX) % BRIGHTNESS_LEVEL_MAX;
    brightness_bar_refresh();
}

/* SET：切换自动亮度。退出自动时以进度条当前值作为手动亮度 */
static void brightness_bar_deal_set(void)
{
    if (is_auto) {
        is_auto = false;
        vehicle_set_param_brightness((uint8_t)(level + 1));
    } else {
        is_auto = true;
        vehicle_set_brightness_auto();
    }

    brightness_bar_sync_view();
}

static void brightness_bar_deal_back(void)
{
    set_active_l4_handler(NULL);
    set_active_l4_lang_refresh(NULL);
    brightness_bar_destroy();
    brightness_init();   /* 回 L3，重配 tpl3 为 brightness */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = brightness_bar_deal_up,
    [CLICK_SHORT_DOWN] = brightness_bar_deal_down,
    [CLICK_SHORT_SET]  = brightness_bar_deal_set,
    [CLICK_SHORT_BACK] = brightness_bar_deal_back,
};

void on_brightness_bar_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}
