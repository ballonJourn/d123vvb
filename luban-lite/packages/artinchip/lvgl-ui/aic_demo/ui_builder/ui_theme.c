/*
 * ui_theme.c - 白天/黑夜主题切换实现
 *
 * 设计要点：
 *   - 文字颜色按角色分组，每个角色一对昼夜色（见 s_text_day/s_text_night），
 *     每个角色一个共享 lv_style_t，控件按角色 add_style。
 *   - 切换主题时遍历各角色，lv_style_set_text_color 改属性 +
 *     lv_obj_report_style_change 通知所有引用控件重绘，各角色独立、互不影响。
 *   - 主页常驻原地刷新；子页靠 auto_del 重建按新主题创建。
 */

#include "ui_theme.h"
#include "display.h"            /* display_option_e: DIAPLAY_NIGHT_OPTION 等 */
#include "vehicle_argument.h"   /* vehicle_get_param_display */
#include "ui_objects.h"         /* home_page_refresh_theme */
#include <stdio.h>

/* 文字角色昼夜色对表（0xRRGGBB），扩展角色时在此各加一行 */
static const uint32_t s_text_day[UI_THEME_TEXT_ROLE_NUM]   = {
    [UI_THEME_TEXT_PRIMARY]   = 0xffffff,   /* 白 */
    [UI_THEME_TEXT_SECONDARY] = 0x7d7d7d,   /* 灰 */
    [UI_THEME_TEXT_ACCENT]    = 0xff5500,   /* 橙 */
};
static const uint32_t s_text_night[UI_THEME_TEXT_ROLE_NUM] = {
    [UI_THEME_TEXT_PRIMARY]   = 0x222222,   /* 深灰 */
    [UI_THEME_TEXT_SECONDARY] = 0xcfcfcf,   /* 浅灰 */
    [UI_THEME_TEXT_ACCENT]    = 0xffaa00,   /* 亮橙 */
};

static lv_style_t s_style_text[UI_THEME_TEXT_ROLE_NUM];
static lv_style_t s_style_bg;
static bool       s_inited   = false;
static ui_theme_e  g_ui_theme = UI_THEME_DAY;

static void apply_style_values(ui_theme_e t)
{
    for (int i = 0; i < UI_THEME_TEXT_ROLE_NUM; i++) {
        uint32_t c = (t == UI_THEME_NIGHT) ? s_text_night[i] : s_text_day[i];
        lv_style_set_text_color(&s_style_text[i], lv_color_hex(c));
    }

    if (t == UI_THEME_NIGHT)
        lv_style_set_bg_color(&s_style_bg, lv_color_hex(0x000000));
    else
        lv_style_set_bg_color(&s_style_bg, lv_color_hex(0xfcfcfc));
}

/* display_option → 实际生效主题：自动(0)时取中间层按光感算出的模式 */
static ui_theme_e resolve_theme(int display_option)
{
    if (display_option == DIAPLAY_AUTO_OPTION)
        return (vehicle_get_cur_display_mode() == VEHICLE_DISPLAY_MODE_NIGHT)
               ? UI_THEME_NIGHT : UI_THEME_DAY;

    return (display_option == DIAPLAY_NIGHT_OPTION) ? UI_THEME_NIGHT : UI_THEME_DAY;
}

ui_theme_e ui_theme_get(void)
{
    /* proxy 已解析自动模式：返回值即实际生效的白天/黑夜 */
    if (vehicle_get_cur_display_mode() == VEHICLE_DISPLAY_MODE_NIGHT)
        return UI_THEME_NIGHT;
    return UI_THEME_DAY;
}

const char *ui_theme_image_path(const char *rel)
{
    static char buf[128];
    snprintf(buf, sizeof(buf), "L:/rodata/lvgl_data/image/%s/%s",
             g_ui_theme == UI_THEME_NIGHT ? "night" : "day", rel);
    return buf;
}

void ui_theme_init(void)
{
    if (s_inited)
        return;
    for (int i = 0; i < UI_THEME_TEXT_ROLE_NUM; i++)
        lv_style_init(&s_style_text[i]);
    lv_style_init(&s_style_bg);
    g_ui_theme = ui_theme_get();
    apply_style_values(g_ui_theme);
    s_inited = true;
}

const lv_style_t *ui_theme_style_text(ui_theme_text_role_e role)
{
    if (role >= UI_THEME_TEXT_ROLE_NUM)
        role = UI_THEME_TEXT_PRIMARY;
    return &s_style_text[role];
}

const lv_style_t *ui_theme_style_bg(void) { return &s_style_bg; }

void ui_theme_apply(int display_option)
{
    ui_theme_e t = resolve_theme(display_option);
    if (s_inited && t == g_ui_theme)
        return;
    g_ui_theme = t;

    /* (a) 颜色：改共享 style 属性，触发所有引用控件重绘 */
    apply_style_values(t);
    for (int i = 0; i < UI_THEME_TEXT_ROLE_NUM; i++)
        lv_obj_report_style_change(&s_style_text[i]);
    lv_obj_report_style_change(&s_style_bg);

    home_page_refresh_theme();

    /* 子页不处理：auto_del=true，下次进入 *_page_create 读 g_ui_theme 重建 */
}

void global_refresh_display(int display_option)
{
    ui_theme_apply(display_option);
}
