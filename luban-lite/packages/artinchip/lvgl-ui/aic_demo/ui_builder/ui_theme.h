/*
 * ui_theme.h - 白天/黑夜主题切换
 *
 * 复用 set_param 的 theme 参数（0 白天 / 1 黑夜 / 2 自动）。
 * 提供全局主题状态、按主题拼图的 ui_theme_image_path()、共享 lv_style_t
 * （运行时改属性触发重绘）以及 global_refresh_display() 入口。
 *
 * 图片路径约定：
 *   - LVGL_IMAGE_PATH(rel)        : 昼夜共用的图（保持原样）
 *   - ui_theme_image_path(rel)    : 需主题切换的图，返回 image/{day|night}/<rel>
 *
 * 文字颜色：按"角色"分组，每个角色有独立的昼夜色对（见 ui_theme.c 颜色表）。
 *   lv_obj_add_style(label, ui_theme_style_text(UI_THEME_TEXT_PRIMARY), ...)
 * 不同角色互不影响；扩展新色对只需在 ui_theme_text_role_e 与颜色表各加一行。
 *
 * 哪些控件纳入昼夜切换由调用方自行决定。
 */
#ifndef UI_THEME_H
#define UI_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef enum {
    UI_THEME_DAY   = 0,
    UI_THEME_NIGHT = 1,
} ui_theme_e;

/* 文字角色：每个角色对应一对独立的昼夜色，按需在末尾追加 */
typedef enum {
    UI_THEME_TEXT_PRIMARY = 0,    /* 主文字（如仪表数值） */
    UI_THEME_TEXT_SECONDARY,      /* 次要标签（如 TRIP/ODO 标签） */
    UI_THEME_TEXT_ACCENT,         /* 强调色字（如橙色提示） */
    UI_THEME_TEXT_ROLE_NUM,
} ui_theme_text_role_e;

/* 初始化共享 style 并按持久化 theme 设定当前主题（在 home_page_create 之前调用） */
void ui_theme_init(void);

/* 当前生效主题（theme==2 自动暂当白天） */
ui_theme_e ui_theme_get(void);

/* 按当前主题返回 image/day|night/<rel> 的完整 LVGL 路径（静态 buffer，即时消费） */
const char *ui_theme_image_path(const char *rel);

/* 共享 style 访问 */
const lv_style_t *ui_theme_style_text(ui_theme_text_role_e role);  /* 文字（按角色） */
const lv_style_t *ui_theme_style_bg(void);                        /* 背景（单套） */

/* 切换主题：改共享 style 属性 + 刷新主页已纳入主题的控件（display_option_e 取值） */
void ui_theme_apply(int display_option);

/* 与既有调用点（display.c / logic.c）兼容的别名 */
void global_refresh_display(int display_option);

#ifdef __cplusplus
}
#endif

#endif /* UI_THEME_H */
