#include <rtthread.h>

#include "sw_version.h"
#include "setting_menu.h"
#include "meter_view.h"
#include "template.h"
#include "aic_lang.h"
#include "vehicle_data.h"
#include "vehicle_upgrade.h"

/* 版本行 = 翻译后的"版本号" + ": " + 中间层版本串（g_soc_version 最长 63 字符） */
#define SW_VERSION_TEXT_MAX  96

/* 隐藏的升级入口：2s 内连续按 SET 5 次，检测 U 盘升级文件并复位进升级 */
#define UPGRADE_SET_CLICK_COUNT       5
#define UPGRADE_SET_CLICK_WINDOW_MS   4000

static uint8_t   s_set_click_cnt;
static rt_tick_t s_set_click_start;

/* 按当前语言重设字体并重填标题与版本号（泰文走独立 c_thai_*，换语言必须重设字体） */
static void sw_version_refresh(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    char text[SW_VERSION_TEXT_MAX];

    if (!page->pop_layer_l3_sw_version)
        return;

    lv_obj_set_style_text_font(page->pop_title_l3_sw_version, aic_get_multi_lang_font(18), LV_PART_MAIN);
    lv_label_set_text(page->pop_title_l3_sw_version, aic_get_translation(sw_version));

    lv_snprintf(text, sizeof(text), "%s: %s",
                aic_get_translation(board_version), veicle_get_data_version());
    lv_obj_set_style_text_font(page->pop_label_l3_sw_version, aic_get_multi_lang_font(18), LV_PART_MAIN);
    lv_label_set_text(page->pop_label_l3_sw_version, text);
}

static void sw_version_reset_set_click(void)
{
    s_set_click_cnt = 0;
}

void sw_version_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    sw_version_build();
    set_current_menu_level(MENU_LEVEL_3);
    tpl3_hide();   /* 隐 L2 meter view 模板层 */
    lv_obj_remove_flag(page->pop_layer_l3_sw_version, LV_OBJ_FLAG_HIDDEN);
    sw_version_refresh();
    sw_version_reset_set_click();
}

void sw_version_relang(void)
{
    sw_version_refresh();
}

void sw_version_destroy(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (page->pop_layer_l3_sw_version)
        lv_obj_add_flag(page->pop_layer_l3_sw_version, LV_OBJ_FLAG_HIDDEN);
    sw_version_reset_set_click();
}

/* ===== 按键处理 ===== */
static void sw_version_deal_back(void)
{
    sw_version_destroy();
    meter_view_init();   /* 回 L2，重配 tpl3 为 meter view */
}

/*
 * SET 连击：窗口期内累计到 5 次后检测 U 盘。
 * 只有 U 盘已挂载且根目录存在 bootcfg.txt 才复位；复位带 BL_UPGRADE reason，
 * bootloader 据此进入 U 盘升级（普通 reboot 不再触发升级）。
 */
static void sw_version_deal_set(void)
{
    rt_tick_t now = rt_tick_get();
    rt_tick_t window = rt_tick_from_millisecond(UPGRADE_SET_CLICK_WINDOW_MS);

    if (s_set_click_cnt == 0 || (now - s_set_click_start) > window) {
        s_set_click_start = now;
        s_set_click_cnt = 1;
        return;
    }

    if (++s_set_click_cnt < UPGRADE_SET_CLICK_COUNT)
        return;

    sw_version_reset_set_click();

    if (vehicle_has_upgrade_file())
        vehicle_reboot_for_upgrade();
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_BACK] = sw_version_deal_back,
    [CLICK_SHORT_SET]  = sw_version_deal_set,
};

void on_sw_version_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}
