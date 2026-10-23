/*
 * mode_view.c - 驾驶模式选择栏（n/p 图切换）
 *
 * 4 种模式（与 mode_view.h 中 mode_index_e 一一对应）：
 *   MODE_ECO / MODE_NORMAL / MODE_SPORT / MODE_SETTING
 *
 * 单控件切换：选中项 src=_p，其余 src=_n。
 */

#include "mode_view.h"
#include "lvgl.h"
#include "aic_ui.h"

static int g_mode_idx = MODE_NORMAL;

void home_refresh_mode_index(int idx)
{
    if (idx < 0 || idx >= MODE_MAX) return;
    g_mode_idx = idx;

    home_page_t *page = home_page_get(&ui_manager);
    if (!page) return;

    /* 单控件切换 n/p 图片：选中项 src=_p，其余 src=_n */
    static const char *n_paths[MODE_MAX] = {
        "L:/rodata/lvgl_data/image/home/drive_normal_n.png",
        "L:/rodata/lvgl_data/image/home/drive_eco_n.png",
        "L:/rodata/lvgl_data/image/home/drive_sport_n.png",
    };
    static const char *p_paths[MODE_MAX] = {
        "L:/rodata/lvgl_data/image/home/drive_normal_p.png",
        "L:/rodata/lvgl_data/image/home/drive_eco_p.png",
        "L:/rodata/lvgl_data/image/home/drive_sport_p.png",
    };
    lv_obj_t *m[MODE_MAX] = {
        page->mode_normal, page->mode_eco, page->mode_sport
    };
    for (int i = 0; i < MODE_MAX; i++) {
        if (m[i]) lv_img_set_src(m[i], (i == idx) ? p_paths[i] : n_paths[i]);
    }
}

int home_mode_index_get(void) { return g_mode_idx; }