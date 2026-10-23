#include "template.h"
#include "ui_objects.h"
#include "aic_ui.h"

/* ===== 模板卡片背景图（按 slot 索引固定） ===== */
static const char *tpl3_n[3] = {
    LVGL_IMAGE_PATH(setting/pop_menu3_01_n.png),
    LVGL_IMAGE_PATH(setting/pop_menu3_02_n.png),
    LVGL_IMAGE_PATH(setting/pop_menu3_03_n.png),
};
static const char *tpl3_p[3] = {
    LVGL_IMAGE_PATH(setting/pop_menu3_01_p.png),
    LVGL_IMAGE_PATH(setting/pop_menu3_02_p.png),
    LVGL_IMAGE_PATH(setting/pop_menu3_03_p.png),
};
static const char *tpl4_n[4] = {
    LVGL_IMAGE_PATH(setting/pop_menu4_01_n.png),
    LVGL_IMAGE_PATH(setting/pop_menu4_02_n.png),
    LVGL_IMAGE_PATH(setting/pop_menu4_03_n.png),
    LVGL_IMAGE_PATH(setting/pop_menu4_04_n.png),
};
static const char *tpl4_p[4] = {
    LVGL_IMAGE_PATH(setting/pop_menu4_01_p.png),
    LVGL_IMAGE_PATH(setting/pop_menu4_02_p.png),
    LVGL_IMAGE_PATH(setting/pop_menu4_03_p.png),
    LVGL_IMAGE_PATH(setting/pop_menu4_04_p.png),
};

/* ===== 3槽位模板 ===== */
void tpl3_activate(const char *title, const char *icon_src)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (page->pop_tpl4)
        lv_obj_add_flag(page->pop_tpl4, LV_OBJ_FLAG_HIDDEN);
    if (!page->pop_tpl3)
        return;
    lv_obj_remove_flag(page->pop_tpl3, LV_OBJ_FLAG_HIDDEN);

    /* label 字体是构建弹窗时按当时的语言设死的（CH/EN/KO/SG 共用 c_default_*，
     * 泰文走独立的 c_thai_*），切语言只重填文字会缺字 -> 每次 activate 按当前
     * 语言重设一遍（同 pop_tip_show 的处理） */
    tpl_layout_font();

    if (page->pop_tpl3_title) {
        lv_label_set_text(page->pop_tpl3_title, title ? title : "");
        /* 标题对齐随分辨率（有图标右对齐/无图标居中） */
        tpl3_layout_title(icon_src != NULL);
        lv_obj_remove_flag(page->pop_tpl3_title, LV_OBJ_FLAG_HIDDEN);
    }
    if (page->pop_tpl3_icon) {
        if (icon_src) {
            lv_img_set_src(page->pop_tpl3_icon, icon_src);
            lv_obj_remove_flag(page->pop_tpl3_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(page->pop_tpl3_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void tpl3_set_slot(int slot, const char *text, bool visible)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (slot < 0 || slot >= 3 || !page->pop_tpl3_card[slot])
        return;
    if (visible) {
        lv_obj_remove_flag(page->pop_tpl3_card[slot], LV_OBJ_FLAG_HIDDEN);
        if (page->pop_tpl3_card_label[slot])
            lv_label_set_text(page->pop_tpl3_card_label[slot], text ? text : "");
    } else {
        lv_obj_add_flag(page->pop_tpl3_card[slot], LV_OBJ_FLAG_HIDDEN);
    }
}

void tpl3_set_label_mode(bool center, bool show_arrow)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    for (int s = 0; s < 3; s++) {
        if (!page->pop_tpl3_card_label[s])
            continue;
        /* label 对齐随分辨率（center 居中/非 center 左对齐） */
        tpl3_layout_label(s, center);
        if (page->pop_tpl3_card_arrow[s]) {
            if (show_arrow)
                lv_obj_remove_flag(page->pop_tpl3_card_arrow[s], LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(page->pop_tpl3_card_arrow[s], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void tpl3_set_card_pressed(int slot, bool pressed)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (slot < 0 || slot >= 3 || !page->pop_tpl3_card[slot])
        return;
    lv_img_set_src(page->pop_tpl3_card[slot], pressed ? tpl3_p[slot] : tpl3_n[slot]);
    if (page->pop_tpl3_card_label[slot])
        lv_obj_set_style_text_opa(page->pop_tpl3_card_label[slot], pressed ? 255 : 178, LV_PART_MAIN);
}

void tpl3_hide(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (page->pop_tpl3)
        lv_obj_add_flag(page->pop_tpl3, LV_OBJ_FLAG_HIDDEN);
}

/* ===== 4槽位模板 ===== */
void tpl4_activate(const char *title, const char *icon_src)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (page->pop_tpl3)
        lv_obj_add_flag(page->pop_tpl3, LV_OBJ_FLAG_HIDDEN);
    if (!page->pop_tpl4)
        return;
    lv_obj_remove_flag(page->pop_tpl4, LV_OBJ_FLAG_HIDDEN);

    tpl_layout_font();   /* 同 tpl3_activate：按当前语言重设模板 label 字体 */

    if (page->pop_tpl4_title) {
        lv_label_set_text(page->pop_tpl4_title, title ? title : "");
        /* 标题对齐随分辨率（有图标右对齐/无图标居中） */
        tpl4_layout_title(icon_src != NULL);
        lv_obj_remove_flag(page->pop_tpl4_title, LV_OBJ_FLAG_HIDDEN);
    }
    if (page->pop_tpl4_icon) {
        if (icon_src) {
            lv_img_set_src(page->pop_tpl4_icon, icon_src);
            lv_obj_remove_flag(page->pop_tpl4_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(page->pop_tpl4_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void tpl4_set_slot(int slot, const char *text, bool visible)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (slot < 0 || slot >= 4 || !page->pop_tpl4_card[slot])
        return;
    if (visible) {
        lv_obj_remove_flag(page->pop_tpl4_card[slot], LV_OBJ_FLAG_HIDDEN);
        if (page->pop_tpl4_card_label[slot])
            lv_label_set_text(page->pop_tpl4_card_label[slot], text ? text : "");
    } else {
        lv_obj_add_flag(page->pop_tpl4_card[slot], LV_OBJ_FLAG_HIDDEN);
    }
}

void tpl4_set_label_mode(bool center, bool show_arrow)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    for (int s = 0; s < 4; s++) {
        if (!page->pop_tpl4_card_label[s])
            continue;
        /* label 对齐随分辨率（center 居中/非 center 左对齐） */
        tpl4_layout_label(s, center);
        if (page->pop_tpl4_card_arrow[s]) {
            if (show_arrow)
                lv_obj_remove_flag(page->pop_tpl4_card_arrow[s], LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(page->pop_tpl4_card_arrow[s], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void tpl4_set_card_pressed(int slot, bool pressed)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (slot < 0 || slot >= 4 || !page->pop_tpl4_card[slot])
        return;
    lv_img_set_src(page->pop_tpl4_card[slot], pressed ? tpl4_p[slot] : tpl4_n[slot]);
    if (page->pop_tpl4_card_label[slot])
        lv_obj_set_style_text_opa(page->pop_tpl4_card_label[slot], pressed ? 255 : 178, LV_PART_MAIN);
}

void tpl4_hide(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (page->pop_tpl4)
        lv_obj_add_flag(page->pop_tpl4, LV_OBJ_FLAG_HIDDEN);
}

/* ===== YES/NO 按钮模板 ===== */
static const char *yn_yes = LVGL_IMAGE_PATH(setting/pop_btn_yes_n.png);
static const char *yn_no  = LVGL_IMAGE_PATH(setting/pop_btn_no_n.png);

void tpl_yn_activate(const char *title)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    /* 隐卡片模板 */
    if (page->pop_tpl3)
        lv_obj_add_flag(page->pop_tpl3, LV_OBJ_FLAG_HIDDEN);
    if (page->pop_tpl4)
        lv_obj_add_flag(page->pop_tpl4, LV_OBJ_FLAG_HIDDEN);
    if (!page->pop_tpl_yn)
        return;
    lv_obj_remove_flag(page->pop_tpl_yn, LV_OBJ_FLAG_HIDDEN);

    tpl_layout_font();   /* 同 tpl3_activate：按当前语言重设模板 label 字体 */

    if (page->pop_tpl_yn_title) {
        lv_label_set_text(page->pop_tpl_yn_title, title ? title : "");
        lv_obj_remove_flag(page->pop_tpl_yn_title, LV_OBJ_FLAG_HIDDEN);
    }
}

void tpl_yn_set_focus(bool yes)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (page->pop_tpl_yn_btn)
        lv_img_set_src(page->pop_tpl_yn_btn, yes ? yn_yes : yn_no);
}

void tpl_yn_hide(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    if (page->pop_tpl_yn)
        lv_obj_add_flag(page->pop_tpl_yn, LV_OBJ_FLAG_HIDDEN);
}
