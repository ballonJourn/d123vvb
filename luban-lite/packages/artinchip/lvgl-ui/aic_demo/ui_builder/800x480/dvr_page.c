/*
 * Copyright (C) 2026 ArtInChip Technology Co., Ltd.
 *
 * DVR 预览页（800×480）
 *
 * 页面只包含一个纯黑全屏根对象，不放任何子控件。DVR 中间层把解码后的
 * JPEG 通过 GE 直接 blit 到 UI layer 的 framebuffer，不经过 LVGL：
 *   - 切入本页时 LVGL 渲染一次黑色底，覆盖 home 残留；
 *   - 之后页面没有 dirty，LVGL 不再重绘，DVR 帧才能持续显示；
 *   - 退出时切回 home，LVGL 全屏重绘覆盖 DVR 最后一帧。
 */

#include "ui_objects.h"

void dvr_page_create(ui_manager_t *ui)
{
    dvr_page_t *scr = dvr_page_get(ui);

    if (!ui->auto_del && scr->obj)
        return;

    scr->obj = lv_obj_create(NULL);
    lv_obj_remove_flag(scr->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr->obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr->obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scr->obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(scr->obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}
