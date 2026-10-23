#include "ui_objects.h"
#include "aic_ui.h"
#include "aic_lang.h"
#include "setting_menu.h"
#include "set_page_key.h"
#include "template.h"

/* L1 六卡片坐标（2列3行，相对弹窗 root 862×436） */
static const int l1_pos_x[SETTING_CATEGORY_NUM] = {  19, 435,  19, 435,  19, 435 };
static const int l1_pos_y[SETTING_CATEGORY_NUM] = { 104, 104, 212, 212, 320, 320 };

static void setting_pop_layer_style(lv_obj_t *layer)
{
    lv_obj_set_style_bg_opa(layer, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(layer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(layer, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(layer, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(layer, LV_SCROLLBAR_MODE_OFF);
}

void setting_page_create(ui_manager_t *ui)
{
    setting_page_t *scr = setting_page_get(ui);

    /* build-once：已构建则仅重置并显示 */
    if (scr->obj) {
        setting_popup_show();
        return;
    }

    lv_obj_t *home = home_page_get(ui)->obj;
    if (!home) {
        LV_LOG_ERROR("home screen NULL, cannot mount setting popup");
        return;
    }

    /* mask：1024×600 半透明遮罩，父=home */
    scr->mask = lv_img_create(home);
    lv_img_set_src(scr->mask, LVGL_IMAGE_PATH(setting/mask.png));
    lv_obj_set_pos(scr->mask, 0, 0);
    lv_obj_set_size(scr->mask, 1024, 600);
    lv_obj_add_flag(scr->mask, LV_OBJ_FLAG_HIDDEN);

    /* 弹窗根容器 862×436 @(81,82)，父=home */
    scr->obj = lv_obj_create(home);
    lv_obj_set_pos(scr->obj, 81, 82);
    lv_obj_set_size(scr->obj, 862, 436);
    setting_pop_layer_style(scr->obj);
    lv_obj_add_flag(scr->obj, LV_OBJ_FLAG_HIDDEN);

    /* pop_bg 整窗背景 862×436 */
    lv_obj_t *bg = lv_img_create(scr->obj);
    lv_img_set_src(bg, LVGL_IMAGE_PATH(setting/pop_bg.png));
    lv_obj_set_pos(bg, 0, 0);

    /* ===== L1 层 ===== */
    scr->pop_layer_l1 = lv_obj_create(scr->obj);
    lv_obj_set_pos(scr->pop_layer_l1, 0, 0);
    lv_obj_set_size(scr->pop_layer_l1, 862, 436);
    setting_pop_layer_style(scr->pop_layer_l1);

    scr->pop_top_bg_l1 = lv_img_create(scr->pop_layer_l1);
    lv_img_set_src(scr->pop_top_bg_l1, LVGL_IMAGE_PATH(setting/pop_menu_bg.png));
    lv_obj_set_pos(scr->pop_top_bg_l1, 19, 20);

    scr->pop_icon_l1 = lv_img_create(scr->pop_layer_l1);
    lv_img_set_src(scr->pop_icon_l1, LVGL_IMAGE_PATH(setting/icon_menu_settings.png));
    lv_obj_set_pos(scr->pop_icon_l1, 37, 26);

    scr->pop_title_l1 = lv_label_create(scr->pop_layer_l1);
    lv_label_set_text(scr->pop_title_l1, aic_get_translation(set_up));
    lv_obj_set_size(scr->pop_title_l1, LV_SIZE_CONTENT, LV_SIZE_CONTENT);   /* 高度自适应：泰文行高 +3px */
    lv_obj_set_style_text_color(scr->pop_title_l1, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_align(scr->pop_title_l1, LV_ALIGN_TOP_RIGHT, -39, 40);
    lv_obj_set_style_text_align(scr->pop_title_l1, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(scr->pop_title_l1, aic_get_multi_lang_font(18), LV_PART_MAIN);

    for (int i = 0; i < SETTING_CATEGORY_NUM; i++) {
        scr->pop_card_l1[i] = lv_img_create(scr->pop_layer_l1);
        lv_img_set_src(scr->pop_card_l1[i], setting_l1_card_src((setting_category_e)i, false));
        lv_obj_set_pos(scr->pop_card_l1[i], l1_pos_x[i], l1_pos_y[i]);

        /* 卡片右侧文字（右对齐白字，未选 opa0.7），右边界 391 匹配设计 */
        scr->pop_card_label_l1[i] = lv_label_create(scr->pop_card_l1[i]);
        lv_label_set_text(scr->pop_card_label_l1[i], setting_l1_card_name((setting_category_e)i));
        lv_obj_set_pos(scr->pop_card_label_l1[i], 107, 33);
        lv_obj_set_size(scr->pop_card_label_l1[i], 284, LV_SIZE_CONTENT);   /* 高度自适应：泰文行高 +3px */
        lv_obj_set_style_text_align(scr->pop_card_label_l1[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_style_text_color(scr->pop_card_label_l1[i], lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_opa(scr->pop_card_label_l1[i], 178, LV_PART_MAIN);
        lv_obj_set_style_text_font(scr->pop_card_label_l1[i], aic_get_multi_lang_font(16), LV_PART_MAIN);
    }

    /* ===== 可复用卡片模板（预建，常驻 hidden）===== */
    /* tpl3：3槽位（pop_menu3 + label@(24,33)LEFT + arrow@(762,20)） */
    scr->pop_tpl3 = lv_obj_create(scr->obj);
    lv_obj_set_pos(scr->pop_tpl3, 0, 0);
    lv_obj_set_size(scr->pop_tpl3, 862, 436);
    setting_pop_layer_style(scr->pop_tpl3);
    lv_obj_add_flag(scr->pop_tpl3, LV_OBJ_FLAG_HIDDEN);

    scr->pop_tpl3_top = lv_img_create(scr->pop_tpl3);
    lv_img_set_src(scr->pop_tpl3_top, LVGL_IMAGE_PATH(setting/pop_menu_bg.png));
    lv_obj_set_pos(scr->pop_tpl3_top, 19, 20);

    scr->pop_tpl3_icon = lv_img_create(scr->pop_tpl3);
    lv_obj_set_pos(scr->pop_tpl3_icon, 37, 26);
    lv_obj_add_flag(scr->pop_tpl3_icon, LV_OBJ_FLAG_HIDDEN);   /* 默认隐，activate 时按需显 */

    scr->pop_tpl3_title = lv_label_create(scr->pop_tpl3);
    lv_obj_set_size(scr->pop_tpl3_title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(scr->pop_tpl3_title, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_align(scr->pop_tpl3_title, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(scr->pop_tpl3_title, aic_get_multi_lang_font(18), LV_PART_MAIN);

    {
        static const int y3[3] = { 103, 211, 319 };
        static const char *n3[3] = {
            LVGL_IMAGE_PATH(setting/pop_menu3_01_n.png),
            LVGL_IMAGE_PATH(setting/pop_menu3_02_n.png),
            LVGL_IMAGE_PATH(setting/pop_menu3_03_n.png),
        };
        for (int s = 0; s < 3; s++) {
            scr->pop_tpl3_card[s] = lv_img_create(scr->pop_tpl3);
            lv_img_set_src(scr->pop_tpl3_card[s], n3[s]);
            lv_obj_set_pos(scr->pop_tpl3_card[s], 19, y3[s]);

            scr->pop_tpl3_card_label[s] = lv_label_create(scr->pop_tpl3_card[s]);
            lv_obj_set_pos(scr->pop_tpl3_card_label[s], 24, 33);
            lv_obj_set_style_text_align(scr->pop_tpl3_card_label[s], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            lv_obj_set_style_text_color(scr->pop_tpl3_card_label[s], lv_color_hex(0xffffff), LV_PART_MAIN);
            lv_obj_set_style_text_font(scr->pop_tpl3_card_label[s], aic_get_multi_lang_font(16), LV_PART_MAIN);

            scr->pop_tpl3_card_arrow[s] = lv_img_create(scr->pop_tpl3_card[s]);
            lv_img_set_src(scr->pop_tpl3_card_arrow[s], LVGL_IMAGE_PATH(setting/icon_list_arrow_right.png));
            lv_obj_set_pos(scr->pop_tpl3_card_arrow[s], 762, 20);
        }
    }

    /* tpl4：4槽位（pop_menu4 + label@(30,20)LEFT + arrow@(763,7)，由 tpl4_set_label_mode 控制显隐） */
    scr->pop_tpl4 = lv_obj_create(scr->obj);
    lv_obj_set_pos(scr->pop_tpl4, 0, 0);
    lv_obj_set_size(scr->pop_tpl4, 862, 436);
    setting_pop_layer_style(scr->pop_tpl4);
    lv_obj_add_flag(scr->pop_tpl4, LV_OBJ_FLAG_HIDDEN);

    scr->pop_tpl4_top = lv_img_create(scr->pop_tpl4);
    lv_img_set_src(scr->pop_tpl4_top, LVGL_IMAGE_PATH(setting/pop_menu_bg.png));
    lv_obj_set_pos(scr->pop_tpl4_top, 19, 20);

    scr->pop_tpl4_icon = lv_img_create(scr->pop_tpl4);
    lv_obj_set_pos(scr->pop_tpl4_icon, 37, 26);
    lv_obj_add_flag(scr->pop_tpl4_icon, LV_OBJ_FLAG_HIDDEN);

    scr->pop_tpl4_title = lv_label_create(scr->pop_tpl4);
    lv_obj_set_size(scr->pop_tpl4_title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(scr->pop_tpl4_title, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_align(scr->pop_tpl4_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(scr->pop_tpl4_title, aic_get_multi_lang_font(18), LV_PART_MAIN);

    {
        static const int y4[4] = { 104, 186, 268, 350 };
        static const char *n4[4] = {
            LVGL_IMAGE_PATH(setting/pop_menu4_01_n.png),
            LVGL_IMAGE_PATH(setting/pop_menu4_02_n.png),
            LVGL_IMAGE_PATH(setting/pop_menu4_03_n.png),
            LVGL_IMAGE_PATH(setting/pop_menu4_04_n.png),
        };
        for (int s = 0; s < 4; s++) {
            scr->pop_tpl4_card[s] = lv_img_create(scr->pop_tpl4);
            lv_img_set_src(scr->pop_tpl4_card[s], n4[s]);
            lv_obj_set_pos(scr->pop_tpl4_card[s], 19, y4[s]);

            scr->pop_tpl4_card_label[s] = lv_label_create(scr->pop_tpl4_card[s]);
            lv_obj_set_pos(scr->pop_tpl4_card_label[s], 30, 20);
            lv_obj_set_style_text_align(scr->pop_tpl4_card_label[s], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            lv_obj_set_style_text_color(scr->pop_tpl4_card_label[s], lv_color_hex(0xffffff), LV_PART_MAIN);
            lv_obj_set_style_text_font(scr->pop_tpl4_card_label[s], aic_get_multi_lang_font(16), LV_PART_MAIN);

            scr->pop_tpl4_card_arrow[s] = lv_img_create(scr->pop_tpl4_card[s]);
            lv_img_set_src(scr->pop_tpl4_card_arrow[s], LVGL_IMAGE_PATH(setting/icon_list_arrow_right.png));
            lv_obj_set_pos(scr->pop_tpl4_card_arrow[s], 763, 7);
            lv_obj_add_flag(scr->pop_tpl4_card_arrow[s], LV_OBJ_FLAG_HIDDEN);   /* 默认隐，由 set_label_mode 控制 */
        }
    }

    /* tpl_yn：YES/NO 按钮模板 */
    scr->pop_tpl_yn = lv_obj_create(scr->obj);
    lv_obj_set_pos(scr->pop_tpl_yn, 0, 0);
    lv_obj_set_size(scr->pop_tpl_yn, 862, 436);
    setting_pop_layer_style(scr->pop_tpl_yn);
    lv_obj_add_flag(scr->pop_tpl_yn, LV_OBJ_FLAG_HIDDEN);

    scr->pop_tpl_yn_top = lv_img_create(scr->pop_tpl_yn);
    lv_img_set_src(scr->pop_tpl_yn_top, LVGL_IMAGE_PATH(setting/pop_menu_bg.png));
    lv_obj_set_pos(scr->pop_tpl_yn_top, 19, 20);

    scr->pop_tpl_yn_title = lv_label_create(scr->pop_tpl_yn);
    lv_obj_align(scr->pop_tpl_yn_title, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_align(scr->pop_tpl_yn_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(scr->pop_tpl_yn_title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(scr->pop_tpl_yn_title, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_font(scr->pop_tpl_yn_title, aic_get_multi_lang_font(18), LV_PART_MAIN);

    scr->pop_tpl_yn_btn = lv_img_create(scr->pop_tpl_yn);
    lv_img_set_src(scr->pop_tpl_yn_btn, LVGL_IMAGE_PATH(setting/pop_btn_yes_n.png));
    lv_obj_set_pos(scr->pop_tpl_yn_btn, 112, 182);

    /* 首次构建后显示 */
    setting_popup_show();
}

void setting_popup_show(void)
{
    setting_page_t *scr = setting_page_get(&ui_manager);
    if (!scr->obj)
        return;

    set_current_menu_level(MENU_LEVEL_1);
    lv_obj_remove_flag(scr->pop_layer_l1, LV_OBJ_FLAG_HIDDEN);
    if (scr->pop_layer_l3_sw_version)
        lv_obj_add_flag(scr->pop_layer_l3_sw_version, LV_OBJ_FLAG_HIDDEN);
    if (scr->pop_layer_l3_tire_pressure)
        lv_obj_add_flag(scr->pop_layer_l3_tire_pressure, LV_OBJ_FLAG_HIDDEN);
    if (scr->pop_layer_l4_brightness_bar)
        lv_obj_add_flag(scr->pop_layer_l4_brightness_bar, LV_OBJ_FLAG_HIDDEN);
    if (scr->pop_tpl3)
        lv_obj_add_flag(scr->pop_tpl3, LV_OBJ_FLAG_HIDDEN);
    if (scr->pop_tpl4)
        lv_obj_add_flag(scr->pop_tpl4, LV_OBJ_FLAG_HIDDEN);
    if (scr->pop_tpl_yn)
        lv_obj_add_flag(scr->pop_tpl_yn, LV_OBJ_FLAG_HIDDEN);
    setting_l1_view_init();   /* 复位到首个可选卡片，并同步 cruise 卡片状态图 */

    if (scr->mask)
        lv_obj_remove_flag(scr->mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(scr->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(scr->mask);
    lv_obj_move_foreground(scr->obj);
}

void setting_popup_hide(void)
{
    setting_page_t *scr = setting_page_get(&ui_manager);
    if (!scr->obj)
        return;
    lv_obj_add_flag(scr->obj, LV_OBJ_FLAG_HIDDEN);
    if (scr->mask)
        lv_obj_add_flag(scr->mask, LV_OBJ_FLAG_HIDDEN);
    setting_menu_destory();
    /* 释放弹窗图缓存，归还 CMA（按 src 逐个 drop，保留 home 图不影响实时刷新） */
    setting_drop_popup_image_cache();
}

void setting_page_update_language(lang_type_t lang)
{
    aic_set_language(lang);
    setting_page_update_current_language();
}

/* 语言切换后逐控件重填设置弹窗文案（弹窗未构建则安全返回）。
 * ① L1 常驻层：顶部标题 + 六张卡片名；
 * ② 当前模板层（tpl3/tpl4/tpl_yn）：交由登记在路由表里的各模块 relang 重跑激活；
 *    L4 亮度条层的标题由 brightness_bar_relang 负责，同样经 ② 派发。 */
void setting_page_update_current_language(void)
{
    setting_page_t *scr = setting_page_get(&ui_manager);
    if (!scr->obj)
        return;

    setting_l1_refresh_language();
    menu_manager_lang_refresh();
}

/* ===== L4 亮度条布局（1024×600 弹窗 862×436，per-resolution）===== */
void brightness_bar_build(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (page->pop_layer_l4_brightness_bar)
        return;

    page->pop_layer_l4_brightness_bar = lv_obj_create(page->obj);
    lv_obj_set_pos(page->pop_layer_l4_brightness_bar, 0, 0);
    lv_obj_set_size(page->pop_layer_l4_brightness_bar, 862, 436);
    lv_obj_set_style_bg_opa(page->pop_layer_l4_brightness_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(page->pop_layer_l4_brightness_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page->pop_layer_l4_brightness_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(page->pop_layer_l4_brightness_bar, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(page->pop_layer_l4_brightness_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(page->pop_layer_l4_brightness_bar, LV_OBJ_FLAG_HIDDEN);

    /* 顶部黄条 + 标题（19,20,824×64，标题居中） */
    page->pop_top_bg_l4_brightness_bar = lv_img_create(page->pop_layer_l4_brightness_bar);
    lv_img_set_src(page->pop_top_bg_l4_brightness_bar, LVGL_IMAGE_PATH(setting/pop_menu_bg.png));
    lv_obj_set_pos(page->pop_top_bg_l4_brightness_bar, 19, 20);

    page->pop_title_l4_brightness_bar = lv_label_create(page->pop_layer_l4_brightness_bar);
    lv_label_set_text(page->pop_title_l4_brightness_bar, aic_get_translation(brightness_title));
    lv_obj_set_size(page->pop_title_l4_brightness_bar, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(page->pop_title_l4_brightness_bar, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_align(page->pop_title_l4_brightness_bar, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(page->pop_title_l4_brightness_bar, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(page->pop_title_l4_brightness_bar, aic_get_multi_lang_font(18), LV_PART_MAIN);

    /* 进度条（slider_brightness_bg 198,144,466×140）：轨道灰 opa0.4，指示器浅灰白 */
    page->pop_bar_l4_brightness_bar = lv_bar_create(page->pop_layer_l4_brightness_bar);
    lv_bar_set_mode(page->pop_bar_l4_brightness_bar, LV_BAR_MODE_NORMAL);
    lv_bar_set_range(page->pop_bar_l4_brightness_bar, 0, 100);
    lv_bar_set_value(page->pop_bar_l4_brightness_bar, 0, LV_ANIM_OFF);
    lv_obj_set_pos(page->pop_bar_l4_brightness_bar, 198, 144);
    lv_obj_set_size(page->pop_bar_l4_brightness_bar, 466, 140);
    lv_obj_set_style_radius(page->pop_bar_l4_brightness_bar, 48, LV_PART_MAIN);
    lv_obj_set_style_bg_color(page->pop_bar_l4_brightness_bar, lv_color_hex(0x868686), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page->pop_bar_l4_brightness_bar, 102, LV_PART_MAIN);   /* 0.4 * 255 */
    lv_obj_set_style_radius(page->pop_bar_l4_brightness_bar, 48, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(page->pop_bar_l4_brightness_bar, lv_color_hex(0xe9ede9), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(page->pop_bar_l4_brightness_bar, 255, LV_PART_INDICATOR);

    /* 右侧日间图标（698,184,60×60） */
    page->pop_icon_l4_brightness_bar = lv_img_create(page->pop_layer_l4_brightness_bar);
    lv_img_set_src(page->pop_icon_l4_brightness_bar, LVGL_IMAGE_PATH(setting/icon_brightness_day.png));
    lv_obj_set_pos(page->pop_icon_l4_brightness_bar, 698, 184);

    /* 左侧夜间图标（102,184,60×60） */
    page->pop_icon_l4_night = lv_img_create(page->pop_layer_l4_brightness_bar);
    lv_img_set_src(page->pop_icon_l4_night, LVGL_IMAGE_PATH(setting/icon_brightness_night.png));
    lv_obj_set_pos(page->pop_icon_l4_night, 102, 184);

    /* 自动亮度状态按钮（376,342,110×46） */
    page->pop_icon_l4_auto_status = lv_img_create(page->pop_layer_l4_brightness_bar);
    lv_img_set_src(page->pop_icon_l4_auto_status, LVGL_IMAGE_PATH(setting/btn_auto_brightness_n.png));
    lv_obj_set_pos(page->pop_icon_l4_auto_status, 376, 342);
}

/* ===== L3 版本页布局（1024×600 弹窗 862×436，per-resolution）=====
 * 只建控件不填文案，文案由 sw_version.c 的刷新函数负责 */
void sw_version_build(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (page->pop_layer_l3_sw_version)
        return;

    page->pop_layer_l3_sw_version = lv_obj_create(page->obj);
    lv_obj_set_pos(page->pop_layer_l3_sw_version, 0, 0);
    lv_obj_set_size(page->pop_layer_l3_sw_version, 862, 436);
    setting_pop_layer_style(page->pop_layer_l3_sw_version);
    lv_obj_add_flag(page->pop_layer_l3_sw_version, LV_OBJ_FLAG_HIDDEN);

    /* 顶部黄条 + 标题（同 L4 brightness bar：19,20 起，标题水平居中） */
    page->pop_top_l3_sw_version = lv_img_create(page->pop_layer_l3_sw_version);
    lv_img_set_src(page->pop_top_l3_sw_version, LVGL_IMAGE_PATH(setting/pop_menu_bg.png));
    lv_obj_set_pos(page->pop_top_l3_sw_version, 19, 20);

    page->pop_title_l3_sw_version = lv_label_create(page->pop_layer_l3_sw_version);
    lv_obj_set_size(page->pop_title_l3_sw_version, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(page->pop_title_l3_sw_version, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_align(page->pop_title_l3_sw_version, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(page->pop_title_l3_sw_version, LV_ALIGN_TOP_MID, 0, 40);

    /* 版本号文本（弹窗居中） */
    page->pop_label_l3_sw_version = lv_label_create(page->pop_layer_l3_sw_version);
    lv_obj_align(page->pop_label_l3_sw_version, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(page->pop_label_l3_sw_version, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_align(page->pop_label_l3_sw_version, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

/* ===== L3 胎压页布局（1024×600 弹窗 862×436，per-resolution）=====
 * 只建控件不填文案，文案/字体/指示层显隐由 tire_pressure.c 负责。
 * 底图与两张指示层都是 824×312 整幅图，叠在同一坐标；指示层是透明叠加图，
 * 红环由图片自身落在前/后轮位置上。
 *
 * 几何取自 1024×600 自身的设计基准（不是 800×480 的等比放大 —— 两分辨率并非严格等比，
 * 例如亮度条内部 y 就没有按比例走）：
 *   内容区原点 (19,104) —— x 同黄条 19；y 同 l1/menu4 首行 104（且黄条下留白与
 *   弹窗底部留白对称：84..104 = 416..436 = 20）。内容区尺寸 = 底图尺寸 824×312。
 *   两个 label 的框按底图比例从 800×480 版换算（同一帧的等比缩放，×1.2795/×1.2787），
 *   保证外侧对齐边仍落在设计稿的位置上（前轮框左缘 60、后轮框右缘 802 = 862-60 对称）。
 *   字号不随分辨率缩放（见 pop_tip_page.c：两分辨率的 POP_TIP_LABEL_FONT 都是 22），
 *   故胎压值仍用 24。 */
void tire_pressure_build(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (page->pop_layer_l3_tire_pressure)
        return;

    page->pop_layer_l3_tire_pressure = lv_obj_create(page->obj);
    lv_obj_set_pos(page->pop_layer_l3_tire_pressure, 0, 0);
    lv_obj_set_size(page->pop_layer_l3_tire_pressure, 862, 436);
    setting_pop_layer_style(page->pop_layer_l3_tire_pressure);
    lv_obj_add_flag(page->pop_layer_l3_tire_pressure, LV_OBJ_FLAG_HIDDEN);

    /* 顶部黄条 + 标题（同 sw_version：19,20 起，标题水平居中） */
    page->pop_top_l3_tire_pressure = lv_img_create(page->pop_layer_l3_tire_pressure);
    lv_img_set_src(page->pop_top_l3_tire_pressure, LVGL_IMAGE_PATH(setting/pop_menu_bg.png));
    lv_obj_set_pos(page->pop_top_l3_tire_pressure, 19, 20);

    page->pop_title_l3_tire_pressure = lv_label_create(page->pop_layer_l3_tire_pressure);
    lv_obj_set_size(page->pop_title_l3_tire_pressure, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(page->pop_title_l3_tire_pressure, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_align(page->pop_title_l3_tire_pressure, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(page->pop_title_l3_tire_pressure, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(page->pop_title_l3_tire_pressure, aic_get_multi_lang_font(18), LV_PART_MAIN);

    /* 内容区底图（整车渲染，规格文字由 label 另放） */
    page->pop_base_l3_tire_pressure = lv_img_create(page->pop_layer_l3_tire_pressure);
    lv_img_set_src(page->pop_base_l3_tire_pressure, LVGL_IMAGE_PATH(setting/img_tire_n.png));
    lv_obj_set_pos(page->pop_base_l3_tire_pressure, 19, 104);

    /* 未匹配指示层：同坐标叠加，默认隐，显隐由 tire_pressure.c 控制 */
    page->pop_ind_front_l3_tire_pressure = lv_img_create(page->pop_layer_l3_tire_pressure);
    lv_img_set_src(page->pop_ind_front_l3_tire_pressure,
                   LVGL_IMAGE_PATH(setting/setting_tire_pressure_left.png));
    lv_obj_set_pos(page->pop_ind_front_l3_tire_pressure, 19, 104);
    lv_obj_add_flag(page->pop_ind_front_l3_tire_pressure, LV_OBJ_FLAG_HIDDEN);

    page->pop_ind_rear_l3_tire_pressure = lv_img_create(page->pop_layer_l3_tire_pressure);
    lv_img_set_src(page->pop_ind_rear_l3_tire_pressure,
                   LVGL_IMAGE_PATH(setting/setting_tire_pressure_right.png));
    lv_obj_set_pos(page->pop_ind_rear_l3_tire_pressure, 19, 104);
    lv_obj_add_flag(page->pop_ind_rear_l3_tire_pressure, LV_OBJ_FLAG_HIDDEN);

    /* 前/后轮胎压值：贴内容区左右内边距并向外侧对齐，白字 24（字号不随分辨率缩放） */
    page->pop_label_front_l3_tire_pressure = lv_label_create(page->pop_layer_l3_tire_pressure);
    lv_obj_set_pos(page->pop_label_front_l3_tire_pressure, 60, 238);
    lv_obj_set_size(page->pop_label_front_l3_tire_pressure, 155, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(page->pop_label_front_l3_tire_pressure, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_color(page->pop_label_front_l3_tire_pressure, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(page->pop_label_front_l3_tire_pressure, aic_get_multi_lang_font(24), LV_PART_MAIN);

    page->pop_label_rear_l3_tire_pressure = lv_label_create(page->pop_layer_l3_tire_pressure);
    lv_obj_set_pos(page->pop_label_rear_l3_tire_pressure, 645, 238);
    lv_obj_set_size(page->pop_label_rear_l3_tire_pressure, 157, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(page->pop_label_rear_l3_tire_pressure, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(page->pop_label_rear_l3_tire_pressure, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(page->pop_label_rear_l3_tire_pressure, aic_get_multi_lang_font(24), LV_PART_MAIN);

    /* 前/后轮胎温：压力正下方一行，16 号白字作次要信息（单位 C，字体子集无 ℃/°） */
    page->pop_label_front_l3_tire_pressure_temp = lv_label_create(page->pop_layer_l3_tire_pressure);
    lv_obj_set_pos(page->pop_label_front_l3_tire_pressure_temp, 60, 268);
    lv_obj_set_size(page->pop_label_front_l3_tire_pressure_temp, 155, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(page->pop_label_front_l3_tire_pressure_temp, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_color(page->pop_label_front_l3_tire_pressure_temp, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(page->pop_label_front_l3_tire_pressure_temp, aic_get_multi_lang_font(16), LV_PART_MAIN);

    page->pop_label_rear_l3_tire_pressure_temp = lv_label_create(page->pop_layer_l3_tire_pressure);
    lv_obj_set_pos(page->pop_label_rear_l3_tire_pressure_temp, 645, 268);
    lv_obj_set_size(page->pop_label_rear_l3_tire_pressure_temp, 157, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(page->pop_label_rear_l3_tire_pressure_temp, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(page->pop_label_rear_l3_tire_pressure_temp, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(page->pop_label_rear_l3_tire_pressure_temp, aic_get_multi_lang_font(16), LV_PART_MAIN);
}

/* ===== per-resolution 模板布局（template.c 调用，坐标随分辨率）=====
 * tpl3/tpl4 标题：有图标→右对齐（right edge 823），无图标→水平居中；y=40
 * tpl3 label 非居中→(24,33)；tpl4 label 非居中→(30,20) */
void tpl3_layout_title(bool has_icon)
{
    setting_page_t *p = setting_page_get(&ui_manager);
    if (!p->pop_tpl3_title)
        return;
    if (has_icon) {
        lv_obj_align(p->pop_tpl3_title, LV_ALIGN_TOP_RIGHT, -39, 40);
        lv_obj_set_style_text_align(p->pop_tpl3_title, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    } else {
        lv_obj_align(p->pop_tpl3_title, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_set_style_text_align(p->pop_tpl3_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

void tpl3_layout_label(int slot, bool center)
{
    setting_page_t *p = setting_page_get(&ui_manager);
    if (slot < 0 || slot >= 3 || !p->pop_tpl3_card_label[slot])
        return;
    if (center)
        lv_obj_align(p->pop_tpl3_card_label[slot], LV_ALIGN_CENTER, 0, 0);
    else
        lv_obj_align(p->pop_tpl3_card_label[slot], LV_ALIGN_TOP_LEFT, 24, 33);
    lv_obj_set_style_text_align(p->pop_tpl3_card_label[slot],
                                center ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
}

void tpl4_layout_title(bool has_icon)
{
    setting_page_t *p = setting_page_get(&ui_manager);
    if (!p->pop_tpl4_title)
        return;
    if (has_icon) {
        lv_obj_align(p->pop_tpl4_title, LV_ALIGN_TOP_RIGHT, -39, 40);
        lv_obj_set_style_text_align(p->pop_tpl4_title, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    } else {
        lv_obj_align(p->pop_tpl4_title, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_set_style_text_align(p->pop_tpl4_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

void tpl4_layout_label(int slot, bool center)
{
    setting_page_t *p = setting_page_get(&ui_manager);
    if (slot < 0 || slot >= 4 || !p->pop_tpl4_card_label[slot])
        return;
    if (center)
        lv_obj_align(p->pop_tpl4_card_label[slot], LV_ALIGN_CENTER, 0, 0);
    else
        lv_obj_align(p->pop_tpl4_card_label[slot], LV_ALIGN_TOP_LEFT, 30, 20);
    lv_obj_set_style_text_align(p->pop_tpl4_card_label[slot],
                                center ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
}

/* ===== per-resolution 模板 label 字体（字号随分辨率）=====
 * 这些 label 的字体是 setting_page_create() 构建弹窗时按当时的语言设死的，
 * 而 CH/EN/KO/SG 共用 c_default_*、泰文走独立的 c_thai_*：切语言只重填文字不重设
 * 字体的话，泰文会拿 c_default_* 渲染而缺字。故各模板 activate 时都重设一遍。
 * 字号与构建处保持一致：标题 18 / 槽位 label 16 */
void tpl_layout_font(void)
{
    setting_page_t *p = setting_page_get(&ui_manager);

    if (p->pop_tpl3_title)
        lv_obj_set_style_text_font(p->pop_tpl3_title, aic_get_multi_lang_font(18), LV_PART_MAIN);
    if (p->pop_tpl4_title)
        lv_obj_set_style_text_font(p->pop_tpl4_title, aic_get_multi_lang_font(18), LV_PART_MAIN);
    if (p->pop_tpl_yn_title)
        lv_obj_set_style_text_font(p->pop_tpl_yn_title, aic_get_multi_lang_font(18), LV_PART_MAIN);

    for (int s = 0; s < 3; s++) {
        if (p->pop_tpl3_card_label[s])
            lv_obj_set_style_text_font(p->pop_tpl3_card_label[s], aic_get_multi_lang_font(16), LV_PART_MAIN);
    }
    for (int s = 0; s < 4; s++) {
        if (p->pop_tpl4_card_label[s])
            lv_obj_set_style_text_font(p->pop_tpl4_card_label[s], aic_get_multi_lang_font(16), LV_PART_MAIN);
    }
}
