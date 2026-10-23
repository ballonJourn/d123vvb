/*
 * pop_tip_page.c - pop_tip 弹窗布局（1024×600）
 *
 * 仅布局: 遮罩/根容器/背景/文本/图标的位置与样式。故障码表、位图、ack 记忆、
 * 5s 定时等业务逻辑在 view/pop_tip_view/pop_tip_manager.c。
 */

#include "pop_tip_page.h"
#include "aic_ui.h"
#include "aic_lang.h"

///< 弹窗坐标系沿用 setting_page: 容器 862x436 @(81,82), mask 1024x600 @(0,0)
#define POP_TIP_ROOT_W    862
#define POP_TIP_ROOT_H    436
#define POP_TIP_ROOT_X    81
#define POP_TIP_ROOT_Y    82
#define POP_TIP_MASK_W    1024
#define POP_TIP_MASK_H    600

///< 文本行几何与字号
#define POP_TIP_LABEL_Y        21
#define POP_TIP_LABEL_H        43
#define POP_TIP_LABEL_FONT     22

///< 图标位置
#define POP_TIP_ICON_X    230
#define POP_TIP_ICON_Y    118

typedef struct {
    lv_obj_t *mask;      /* 1024x600 半透明遮罩, 父=lv_layer_top() */
    lv_obj_t *obj;       /* 根容器 862x436 @(81,82), 父=lv_layer_top() */
    lv_obj_t *bg;        /* 弹窗背景图 @(0,0) */
    lv_obj_t *label;     /* 文本 (0,21,862,43) font22 居中 */
    lv_obj_t *icon;      /* 弹出图片 @(230,118) */
    bool visible;
} pop_tip_t;

static pop_tip_t g_pop_tip = {0};

///< 层容器样式归零(透明布局壳), 复刻 setting_page::setting_pop_layer_style
static void pop_tip_layer_style(lv_obj_t *layer)
{
    lv_obj_set_style_bg_opa(layer, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(layer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(layer, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(layer, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(layer, LV_SCROLLBAR_MODE_OFF);
}

///< build-once: 首次调用时创建全部控件
static void pop_tip_create(void)
{
    lv_obj_t *top = lv_layer_top();

    /* mask: 1024x600 半透明遮罩 */
    g_pop_tip.mask = lv_img_create(top);
    lv_img_set_src(g_pop_tip.mask, LVGL_IMAGE_PATH(setting/mask.png));
    lv_obj_set_pos(g_pop_tip.mask, 0, 0);
    lv_obj_set_size(g_pop_tip.mask, POP_TIP_MASK_W, POP_TIP_MASK_H);
    lv_obj_add_flag(g_pop_tip.mask, LV_OBJ_FLAG_HIDDEN);

    /* 根容器 862x436 @(81,82) */
    g_pop_tip.obj = lv_obj_create(top);
    lv_obj_set_pos(g_pop_tip.obj, POP_TIP_ROOT_X, POP_TIP_ROOT_Y);
    lv_obj_set_size(g_pop_tip.obj, POP_TIP_ROOT_W, POP_TIP_ROOT_H);
    pop_tip_layer_style(g_pop_tip.obj);
    lv_obj_add_flag(g_pop_tip.obj, LV_OBJ_FLAG_HIDDEN);

    /* 背景控件 @(0,0), src 由 show 时按故障码设置 */
    g_pop_tip.bg = lv_img_create(g_pop_tip.obj);
    lv_obj_set_pos(g_pop_tip.bg, 0, 0);

    /* 文本控件 (0,21,862,43) font22 上下左右居中 */
    g_pop_tip.label = lv_label_create(g_pop_tip.obj);
    lv_obj_set_pos(g_pop_tip.label, 0, POP_TIP_LABEL_Y);
    lv_obj_set_size(g_pop_tip.label, POP_TIP_ROOT_W, POP_TIP_LABEL_H);
    lv_obj_set_style_text_color(g_pop_tip.label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_align(g_pop_tip.label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    /* 图片控件 @(230,118), src 由 show 时按故障码设置 */
    g_pop_tip.icon = lv_img_create(g_pop_tip.obj);
    lv_obj_set_pos(g_pop_tip.icon, POP_TIP_ICON_X, POP_TIP_ICON_Y);
}

void pop_tip_page_set_text(const char *text)
{
    if (!g_pop_tip.label)
        return;

    /* 字体重设：语言切换会改变字体（泰文走独立 THAI_TTF），
     * label 的字体是 create 时一次性设的，凡是重填文案都按当前语言重设 */
    lv_obj_set_style_text_font(g_pop_tip.label, aic_get_multi_lang_font(POP_TIP_LABEL_FONT), LV_PART_MAIN);
    lv_label_set_text(g_pop_tip.label, text);
}

void pop_tip_page_show(const char *bg_src, const char *icon_src, const char *text)
{
    if (!g_pop_tip.obj)
        pop_tip_create();

    lv_img_set_src(g_pop_tip.bg, bg_src);
    lv_img_set_src(g_pop_tip.icon, icon_src);
    pop_tip_page_set_text(text);

    lv_obj_remove_flag(g_pop_tip.mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_pop_tip.obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_pop_tip.mask);
    lv_obj_move_foreground(g_pop_tip.obj);

    g_pop_tip.visible = true;
}

void pop_tip_page_hide(void)
{
    if (!g_pop_tip.obj)
        return;

    lv_obj_add_flag(g_pop_tip.obj, LV_OBJ_FLAG_HIDDEN);
    if (g_pop_tip.mask)
        lv_obj_add_flag(g_pop_tip.mask, LV_OBJ_FLAG_HIDDEN);

    g_pop_tip.visible = false;
    lv_image_cache_drop(LVGL_IMAGE_PATH(setting/mask.png));
}

bool pop_tip_page_is_visible(void)
{
    return g_pop_tip.visible;
}