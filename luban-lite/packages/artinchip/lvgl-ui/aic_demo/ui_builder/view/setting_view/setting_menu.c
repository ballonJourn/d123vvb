#include "setting_menu.h"
#include <stddef.h>
#include "aic_ui.h"
#include "window_manager.h"
#include "instrument.h"
#include "meter_view.h"
#include "language.h"
#include "unit.h"
#include "view_faults.h"
#include "sw_version.h"
#include "tire_pressure.h"
#include "brightness.h"
#include "display.h"
#include "brightness_bar.h"
#include "restore_factory.h"
#include "reset_trip.h"
#include "unlock_seat.h"
#include "power_down.h"
#include "cdc_settings.h"
#include "cdc_custom.h"
#include "template.h"
#include "set_page_key.h"
#include "vehicle_data.h"   /* proxy 层：cruise 卡片状态取 ICON_CC_STATUS */

/* LVGL 图片缓存清理（定义于 lvgl 核心，按 src 释放指定图缓存，NULL 释放全部）。
 * 退出弹窗时按弹窗 src 逐个释放，保留 home 图缓存不影响实时刷新。 */
extern void lv_image_cache_drop(const void *src);

/* ===== 静态状态 ===== */
static setting_category_e l1_index = CATEGORY_UNLOCK;
static menu_level_e      current_level = MENU_LEVEL_1;

/* ===== L1 卡片图片 src 表（构建 + 切换共用） ===== */
static const char *l1_card_n[SETTING_CATEGORY_NUM] = {
    LVGL_IMAGE_PATH(setting/icon_cruise_n.png),
    LVGL_IMAGE_PATH(setting/icon_unlock_n.png),
    LVGL_IMAGE_PATH(setting/icon_mileage_n.png),
    LVGL_IMAGE_PATH(setting/icon_switch_n.png),
    LVGL_IMAGE_PATH(setting/icon_instrument_n.png),
    LVGL_IMAGE_PATH(setting/icon_meterview_n.png),
};
static const char *l1_card_p[SETTING_CATEGORY_NUM] = {
    LVGL_IMAGE_PATH(setting/icon_cruise_p.png),
    LVGL_IMAGE_PATH(setting/icon_unlock_p.png),
    LVGL_IMAGE_PATH(setting/icon_mileage_p.png),
    LVGL_IMAGE_PATH(setting/icon_switch_p.png),
    LVGL_IMAGE_PATH(setting/icon_instrument_p.png),
    LVGL_IMAGE_PATH(setting/icon_meterview_p.png),
};

/* L1 六分类卡片文字（设计稿右侧白字）——走翻译表 */
static const key_id_t l1_card_key[SETTING_CATEGORY_NUM] = {
    cruise_control,
    unlock_seat,
    reset_trip,
    power_down,
    instrument_settings,
    meter_view,
};

const char *setting_l1_card_src(setting_category_e item, bool pressed)
{
    if (item >= SETTING_CATEGORY_NUM)
        return NULL;
    return pressed ? l1_card_p[item] : l1_card_n[item];
}

const char *setting_l1_card_name(setting_category_e item)
{
    if (item >= SETTING_CATEGORY_NUM)
        return "";
    return aic_get_translation(l1_card_key[item]);
}

/* cruise 卡片不参与焦点循环（它没有 L2 可进，仅作状态显示），
 * 上下键在其上跳过，只在剩余五张卡片间循环 */
static bool l1_is_selectable(setting_category_e item)
{
    return item != CATEGORY_CRUISE;
}

/* 从 cur 沿 dir(+1/-1) 找下一个可选中卡片 */
static setting_category_e l1_next_selectable(setting_category_e cur, int dir)
{
    do {
        cur = (setting_category_e)((cur + dir + SETTING_CATEGORY_NUM) % SETTING_CATEGORY_NUM);
    } while (!l1_is_selectable(cur));
    return cur;
}

/* cruise 卡片 n/p：由 VEH_CRUISE_MAIN_SWITCH 驱动（1: ON → _p；0/无效 → _n） */
static bool l1_cruise_is_on(void)
{
    return vehicle_get_data_signal_lamp(ICON_CC_STATUS) == 1;
}

/* ===== level / index 访问 ===== */
menu_level_e get_current_menu_level(void)         { return current_level; }
void set_current_menu_level(menu_level_e level)   { if (level <= MENU_LEVEL_4) current_level = level; }
setting_category_e get_l1_index(void)             { return l1_index; }
int get_current_menu_index(void)                  { return (int)l1_index; } /* 兼容旧声明 */

/* ===== L1 ===== */
void setting_l1_view_init(void)
{
    setting_l1_set_focused_item(CATEGORY_UNLOCK);   /* cruise 不可选，焦点落在首个可选项 */
}

/* L1 常驻层文案刷新：顶部标题 + 六张卡片名。语言切换时调用（哪怕当前在 L2/L3，
 * 也要一并刷新，保证 BACK 回 L1 时文案已正确） */
void setting_l1_refresh_language(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (!page->obj)
        return;

    if (page->pop_title_l1) {
        lv_obj_set_style_text_font(page->pop_title_l1, aic_get_multi_lang_font(18), LV_PART_MAIN);
        lv_label_set_text(page->pop_title_l1, aic_get_translation(set_up));
    }
    for (int i = 0; i < SETTING_CATEGORY_NUM; i++) {
        if (!page->pop_card_label_l1[i])
            continue;
        lv_obj_set_style_text_font(page->pop_card_label_l1[i], aic_get_multi_lang_font(16), LV_PART_MAIN);
        lv_label_set_text(page->pop_card_label_l1[i], setting_l1_card_name((setting_category_e)i));
    }
    setting_l1_set_focused_item(get_l1_index());   /* 恢复 n/p 选中态（幂等） */
}

void setting_l1_set_focused_item(setting_category_e item)
{
    if (item >= SETTING_CATEGORY_NUM)
        return;

    setting_page_t *page = setting_page_get(&ui_manager);
    if (!page->pop_card_l1[0])
        return;

    for (int i = 0; i < SETTING_CATEGORY_NUM; i++) {
        if (!page->pop_card_l1[i])
            continue;
        /* cruise 卡片的高亮不表示焦点，而表示巡航已开启（VEH_CRUISE_MAIN_SWITCH） */
        bool pressed = ((setting_category_e)i == CATEGORY_CRUISE)
                       ? l1_cruise_is_on()
                       : (i == (int)item);
        lv_img_set_src(page->pop_card_l1[i],
                       setting_l1_card_src((setting_category_e)i, pressed));
        if (page->pop_card_label_l1[i])
            lv_obj_set_style_text_opa(page->pop_card_label_l1[i], i == (int)item ? 255 : 178, LV_PART_MAIN);
    }
    l1_index = item;
}

void on_setting_l1_deal_short_click(click_e click)
{
    switch (click) {
    case CLICK_SHORT_UP:
        l1_index = l1_next_selectable(l1_index, -1);
        setting_l1_set_focused_item(l1_index);
        break;
    case CLICK_SHORT_DOWN:
        l1_index = l1_next_selectable(l1_index, +1);
        setting_l1_set_focused_item(l1_index);
        break;
    case CLICK_SHORT_SET:
        /* 升级由 menu_manager_click 经 entry_level_2 表判定，此处无动作 */
        break;
    case CLICK_SHORT_BACK:
        window_manager_set_target(WINDOW_MAIN);
        break;
    default:
        break;
    }
}

/* ===== 生命周期 ===== */
void setting_menu_destory(void)
{
    l1_index = CATEGORY_UNLOCK;
    current_level = MENU_LEVEL_1;
    tpl3_hide();
    tpl4_hide();
    tpl_yn_hide();
    instrument_destroy();   /* 销毁 L2 控件并复位状态 */
    meter_view_destroy();  /* 销毁 L2 Meter view 控件并复位状态 */
    language_destroy();    /* 销毁 L3 language 控件并复位状态 */
    unit_destroy();        /* 复位 L3 unit 状态 */
    view_faults_destroy(); /* 销毁 L3 View Faults 控件并复位状态 */
    sw_version_destroy();  /* 隐去 L3 sw_version 页并复位状态 */
    tire_pressure_destroy(); /* 隐去 L3 tire pressure 页并复位状态 */
    brightness_destroy();  /* 销毁 L3 brightness 控件并复位状态 */
    display_destroy();     /* 销毁 L4 scene 控件并复位状态 */
    brightness_bar_destroy(); /* 销毁 L4 brightness bar 控件并复位状态 */
    restore_factory_destroy(); /* 销毁 L3 restore factory 控件并复位状态 */
    reset_trip_destroy();      /* 复位 L2 reset trip 状态 */
    unlock_seat_destroy();     /* 复位 L2 Unlock the seat 状态 */
    power_down_destroy();      /* 复位 L2 Power down 状态 */
    cdc_settings_destroy();    /* 销毁 L3 CDC settings 控件并复位状态 */
    cdc_custom_destroy();      /* 销毁 L4 CDC custom 控件并复位状态 */
    set_active_l4_handler(NULL);
    set_active_l4_lang_refresh(NULL);
}

/* ===== 弹窗图片 src 表（退出弹窗时逐个 drop 释放 CMA，保留 home 图） ===== */
static const char *popup_image_src[] = {
    /* 背景 */
    LVGL_IMAGE_PATH(setting/mask.png),
    LVGL_IMAGE_PATH(setting/pop_bg.png),
    LVGL_IMAGE_PATH(setting/pop_menu_bg.png),
    /* L1 顶部图标 */
    LVGL_IMAGE_PATH(setting/icon_menu_settings.png),
    LVGL_IMAGE_PATH(setting/icon_menu_instrument.png),
    LVGL_IMAGE_PATH(setting/icon_menu_meterview.png),
    /* L1 六卡片 n/p */
    LVGL_IMAGE_PATH(setting/icon_cruise_n.png),    LVGL_IMAGE_PATH(setting/icon_cruise_p.png),
    LVGL_IMAGE_PATH(setting/icon_unlock_n.png),   LVGL_IMAGE_PATH(setting/icon_unlock_p.png),
    LVGL_IMAGE_PATH(setting/icon_mileage_n.png),   LVGL_IMAGE_PATH(setting/icon_mileage_p.png),
    LVGL_IMAGE_PATH(setting/icon_switch_n.png),   LVGL_IMAGE_PATH(setting/icon_switch_p.png),
    LVGL_IMAGE_PATH(setting/icon_instrument_n.png),LVGL_IMAGE_PATH(setting/icon_instrument_p.png),
    LVGL_IMAGE_PATH(setting/icon_meterview_n.png),LVGL_IMAGE_PATH(setting/icon_meterview_p.png),
    /* L2/L3/L4 卡片箭头 */
    LVGL_IMAGE_PATH(setting/icon_list_arrow_right.png),
    /* L2 instrument / L3 language / L3 brightness / L4 scene 卡片 */
    LVGL_IMAGE_PATH(setting/pop_menu3_01_n.png),  LVGL_IMAGE_PATH(setting/pop_menu3_01_p.png),
    LVGL_IMAGE_PATH(setting/pop_menu3_02_n.png),  LVGL_IMAGE_PATH(setting/pop_menu3_02_p.png),
    LVGL_IMAGE_PATH(setting/pop_menu3_03_n.png),  LVGL_IMAGE_PATH(setting/pop_menu3_03_p.png),
    /* L3 view_faults 卡片 */
    LVGL_IMAGE_PATH(setting/pop_menu4_01_n.png),  LVGL_IMAGE_PATH(setting/pop_menu4_01_p.png),
    LVGL_IMAGE_PATH(setting/pop_menu4_02_n.png),  LVGL_IMAGE_PATH(setting/pop_menu4_02_p.png),
    LVGL_IMAGE_PATH(setting/pop_menu4_03_n.png),  LVGL_IMAGE_PATH(setting/pop_menu4_03_p.png),
    LVGL_IMAGE_PATH(setting/pop_menu4_04_n.png),  LVGL_IMAGE_PATH(setting/pop_menu4_04_p.png),
    /* L4 brightness bar 图标 */
    LVGL_IMAGE_PATH(setting/icon_brightness_day.png),
    /* L3 restore factory YES/NO 按钮组合图 */
    LVGL_IMAGE_PATH(setting/pop_btn_yes_n.png),
    LVGL_IMAGE_PATH(setting/pop_btn_no_n.png),
    /* L3 tire pressure 底图 + 前/后轮未匹配指示层 */
    LVGL_IMAGE_PATH(setting/img_tire_n.png),
    LVGL_IMAGE_PATH(setting/setting_tire_pressure_left.png),
    LVGL_IMAGE_PATH(setting/setting_tire_pressure_right.png),
};

void setting_drop_popup_image_cache(void)
{
    for (int i = 0; i < (int)(sizeof(popup_image_src) / sizeof(popup_image_src[0])); i++)
        lv_image_cache_drop(popup_image_src[i]);
}
