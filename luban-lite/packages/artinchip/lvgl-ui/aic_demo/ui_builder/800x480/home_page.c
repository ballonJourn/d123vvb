/*
 * home_page.c - 800×480 主页（按 MasterGo 设计稿重做）
 *
 * 视觉层级（800×480 画布）：
 *   scr->obj
 *   ├─ wallpaper         (0,0,800,480)    home/wallpaper.png
 *   ├─ bottom_bg         (0,272,800,208)  home/bottom_bg.png
 *   ├─ top_bar           (0,0,800,60)     时钟 + BAT + 左右转向灯
 *   ├─ speed_view        (203,29,412,412) 表盘 + 速度大数字 + READY/kmh + 档位
 *   ├─ mode_view         (32,101,194,240) 模式盘前景 4 条 ECO/NORMAL/SPORT/SETTING
 *   ├─ mode_dial         (120,45,144,346) home/icon_mode_dial.png
 *   ├─ battery_arc1  (598,85)         home/icon_battery_border_full.png（边框，顶层）
 *   │  └─ arc1_fg                       home/icon_battery_arc_full.png（满弧填充+A8 mask，底层）
 *   ├─ battery_arc2  (654,85)         home/icon_battery_border_mid.png（边框，顶层）
 *   │  └─ arc2_fg                       home/icon_battery_arc_mid.png（满弧填充+A8 mask，底层）
 *   ├─ battery_arc3  (709,85)         home/icon_battery_border_low.png（边框，顶层）
 *   │  └─ arc3_fg                       home/icon_battery_arc_low.png（满弧填充+A8 mask，底层）
 *   ├─ dial_pointer_*     电池指针
 *   ├─ logo/range/电池进度条 bat1/2/3   home/icon_battery_empty.png（背景，内腔透明）+ 纯色指示器
 *   └─ mileage_cont      (0,410,800,70)   底部 TRIP/ODO/电量/指示灯
 *
 * 注：dock 启动器子页（info/navi/music/.../dvr）与 dock_icon 栏按设计稿废弃，
 *   成员保留不创建（NULL），view 层 NULL-check 静默 return。
 */

#include "ui_objects.h"
#include "aic_ui.h"
#include "ui_util.h"
#include "aic_lang.h"
#include "ui_theme.h"
#include "home_view_interface.h"
#include "mode_view.h"         /* home_refresh_mode_index / MODE_* */
#include "animation_ctrl.h"    /* home_anim_move_x / home_anim_breath */
#include "power_view.h"        /* home_refresh_power (logic 引用) */
#include "home_page.h"

/* 顶部状态图标坐标（800×480，仅左右转向灯参与 signal 反射） */

static void home_top_bar_create(home_page_t *scr)
{
    scr->top_bar = lv_obj_create(scr->obj);
    lv_obj_set_size(scr->top_bar, 800, 60);
    lv_obj_set_pos(scr->top_bar, 0, 0);
    lv_obj_set_scrollbar_mode(scr->top_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(scr->top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scr->top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(scr->top_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 时钟 time_view (66, 12, 101, 32) */
    scr->time_view = lv_obj_create(scr->top_bar);
    lv_obj_set_size(scr->time_view, 200, 32);
    lv_obj_set_pos(scr->time_view, 540, 12);
    lv_obj_set_scrollbar_mode(scr->time_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(scr->time_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scr->time_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(scr->time_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(scr->time_view, LV_OBJ_FLAG_HIDDEN);

    scr->hour = lv_label_create(scr->time_view);
    lv_label_set_text(scr->hour, "00");
    lv_obj_set_size(scr->hour, 60, 32);
    lv_obj_set_pos(scr->hour, 0, 4);
    lv_obj_set_style_text_align(scr->hour, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(scr->hour, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->hour, aic_get_lang_font(SPEEDLABEL_TTF, 32, true), LV_PART_MAIN | LV_STATE_DEFAULT);

    scr->colon = lv_label_create(scr->time_view);
    lv_label_set_text(scr->colon, ":");
    lv_obj_set_size(scr->colon, 15, 32);
    lv_obj_set_pos(scr->colon, 65, 0);
    lv_obj_set_style_text_align(scr->colon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(scr->colon, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->colon, aic_get_lang_font(SPEEDLABEL_TTF, 32, true), LV_PART_MAIN | LV_STATE_DEFAULT);

    scr->min = lv_label_create(scr->time_view);
    lv_label_set_text(scr->min, "00");
    lv_obj_set_size(scr->min, 60, 32);
    lv_obj_set_pos(scr->min, 80, 4);
    lv_obj_set_style_text_align(scr->min, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(scr->min, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->min, aic_get_lang_font(SPEEDLABEL_TTF, 32, true), LV_PART_MAIN | LV_STATE_DEFAULT);

    scr->ampm = lv_label_create(scr->time_view);
    lv_label_set_text(scr->ampm, "AM");
    lv_obj_set_size(scr->ampm, 34, 16);
    lv_obj_set_pos(scr->ampm, 130, 6);
    lv_obj_set_style_text_align(scr->ampm, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(scr->ampm, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->ampm, aic_get_multi_lang_font(22), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* BAT:--V，文本随后由 home_refresh_battery_voltage 刷新 */
    scr->bat_lab = lv_label_create(scr->top_bar);
    lv_label_set_text(scr->bat_lab, "BAT:--V");
    lv_obj_set_pos(scr->bat_lab, 66, 20);
    lv_obj_set_style_text_color(scr->bat_lab, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->bat_lab, aic_get_multi_lang_font(16), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 左转向灯（前景 icon_left，参与 signal 反射）top_left_p.png */
    scr->icon_left = lv_img_create(scr->top_bar);
    lv_img_set_src(scr->icon_left, LVGL_IMAGE_PATH(home/top_left_n.png));
    lv_obj_set_pos(scr->icon_left, 10, 6);

    /* 右转向灯（前景 icon_right）top_right_p.png */
    scr->icon_right = lv_img_create(scr->top_bar);
    lv_img_set_src(scr->icon_right, LVGL_IMAGE_PATH(home/top_right_n.png));
    lv_obj_set_pos(scr->icon_right, 742, 6);

    /* 其余 signal icon（GMS/gps/bt/high_beam/ready/near_beam/abs/ecu/tcs/brake）
     * 设计稿 status_bar 未展示，不创建（NULL），signal_view NULL-check 安全 */
}

static void home_speed_view_create(home_page_t *scr)
{
    /* 仪表容器 (203, 29, 412, 412) */
    scr->speed_view = lv_obj_create(scr->obj);
    lv_obj_set_size(scr->speed_view, 412, 412);
    lv_obj_set_pos(scr->speed_view, 203, 29);
    lv_obj_set_scrollbar_mode(scr->speed_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(scr->speed_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scr->speed_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(scr->speed_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 表盘底图 dashboard_n.png 380×380，居中于 412 容器 */
    scr->bg_cricle = lv_img_create(scr->speed_view);
    lv_img_set_src(scr->bg_cricle, LVGL_IMAGE_PATH(home/dashboard_n.png));
    lv_obj_set_pos(scr->bg_cricle, 16, 16);

    /* 速度大数字位图（"主题字"区域 94,148 215×93，3 位 number_X.png 72×100 居中） */
    const int DIGIT_W = 72, DIGIT_H = 100;
    const int DIGIT_BASE_X = 94 + (215 - 3 * DIGIT_W) / 2;   /* 3 位区域左起，居中于 215 */
    const int DIGIT_BASE_Y = 148 + (93 - DIGIT_H) / 2;
    for (int i = 0; i < 3; i++) {
        scr->speed_digit[i] = lv_img_create(scr->speed_view);
        lv_img_set_src(scr->speed_digit[i], "L:/rodata/lvgl_data/image/speed/number_0.png");
        lv_obj_set_pos(scr->speed_digit[i], DIGIT_BASE_X + i * DIGIT_W, DIGIT_BASE_Y);
        lv_obj_set_size(scr->speed_digit[i], DIGIT_W, DIGIT_H);
    }
    /* 初始值 0：仅显示个位，居中（speed_digits_set 会重排） */
    lv_obj_add_flag(scr->speed_digit[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scr->speed_digit[1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(scr->speed_digit[2], DIGIT_BASE_X + DIGIT_W);

    /* km/h (181,258,40,16) */
    scr->kmh_lab = lv_label_create(scr->speed_view);
    lv_label_set_text(scr->kmh_lab, "km/h");
    lv_obj_set_size(scr->kmh_lab, 40, 16);
    lv_obj_set_pos(scr->kmh_lab, 181, 258);
    lv_obj_set_style_text_align(scr->kmh_lab, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(scr->kmh_lab, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->kmh_lab, aic_get_lang_font(DEFAULT_TTF, 16, true), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 进度弧 lv_arc（dashboard_progress_n.png 作 arc_image，380×380 叠在 dashboard_n.png 上） */
    static lv_style_t style_arc_bg;
    static lv_style_t style_arc_fg;
    static bool style_arc_inited = false;
    if (!style_arc_inited) {
        /* 背景槽:透明,不显示,避免遮挡 PNG 视觉 */
        lv_style_init(&style_arc_bg);
        lv_style_set_arc_width(&style_arc_bg, 0);
        lv_style_set_arc_opa(&style_arc_bg, LV_OPA_0);

        lv_style_init(&style_arc_fg);
        lv_style_set_arc_width(&style_arc_fg, 65);
        lv_style_set_arc_rounded(&style_arc_fg, 0);
        lv_style_set_arc_image_src(&style_arc_fg, LVGL_IMAGE_PATH(home/dashboard_progress_n.png));
        style_arc_inited = true;
    }

    scr->progress_arc = lv_arc_create(scr->speed_view);
    lv_obj_set_size(scr->progress_arc, 380, 380);
    lv_obj_set_pos(scr->progress_arc, 16, 16);
    lv_obj_set_align(scr->progress_arc, LV_ALIGN_TOP_LEFT);
    lv_obj_clear_flag(scr->progress_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(scr->progress_arc, NULL, LV_PART_KNOB);

    lv_arc_set_rotation(scr->progress_arc, 135);
    lv_arc_set_bg_angles(scr->progress_arc, 0, 270);
    lv_arc_set_range(scr->progress_arc, 0, 270);
    lv_arc_set_value(scr->progress_arc, 0);

    lv_obj_add_style(scr->progress_arc, &style_arc_bg, LV_PART_MAIN);
    lv_obj_add_style(scr->progress_arc, &style_arc_fg, LV_PART_INDICATOR);

    scr->progress_arc_re = lv_arc_create(scr->speed_view);
    lv_obj_set_size(scr->progress_arc_re, 380, 380);
    lv_obj_set_pos(scr->progress_arc_re, 16, 16);
    lv_obj_set_align(scr->progress_arc_re, LV_ALIGN_TOP_LEFT);
    lv_obj_clear_flag(scr->progress_arc_re, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(scr->progress_arc_re, NULL, LV_PART_KNOB);

    lv_arc_set_rotation(scr->progress_arc_re, 270);
    lv_arc_set_bg_angles(scr->progress_arc_re, 0, 270);
    lv_arc_set_range(scr->progress_arc_re, 0, 270);
    lv_arc_set_value(scr->progress_arc_re, 0);
    lv_arc_set_mode(scr->progress_arc_re, LV_ARC_MODE_REVERSE);

    lv_obj_add_style(scr->progress_arc_re, &style_arc_bg, LV_PART_MAIN);
    lv_obj_add_style(scr->progress_arc_re, &style_arc_fg, LV_PART_INDICATOR);

    lv_obj_add_flag(scr->progress_arc_re, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *bg_scale = lv_img_create(scr->speed_view);
    lv_img_set_src(bg_scale, LVGL_IMAGE_PATH(home/dashboard_scale.png));
    lv_obj_set_pos(bg_scale, 45, 46);

    scr->pointer = lv_image_create(scr->speed_view);
    lv_image_set_src(scr->pointer, LVGL_IMAGE_PATH(home/dashboard_pointer_n.png));
    /* 关键：设置旋转中心为图片的旋转轴心（指针尾部） */
    lv_image_set_pivot(scr->pointer, 10, 190);
    /* 放置到仪表盘圆心位置 */
    lv_obj_set_pos(scr->pointer, 196, 16);
    lv_img_set_angle(scr->pointer, -1350);
    /* bottom_bg 与 gear_view 移至 home_page_create 控制层级（表盘 < bottom_bg < gear） */
}

static void home_gear_view_create(home_page_t *scr)
{
    /* 档位栏 (365,335,70,84) - scr->obj 子，在 bottom_bg 之上；三档同位 HIDDEN 切换 */
    scr->gear_view = lv_obj_create(scr->obj);
    lv_obj_set_size(scr->gear_view, 70, 84);
    lv_obj_set_pos(scr->gear_view, 365, 335);
    lv_obj_set_scrollbar_mode(scr->gear_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(scr->gear_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scr->gear_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(scr->gear_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    scr->gear_R = lv_img_create(scr->gear_view);
    lv_img_set_src(scr->gear_R, LVGL_IMAGE_PATH(home/gear_r_r.png));
    lv_obj_set_pos(scr->gear_R, 0, 0);
    lv_obj_add_flag(scr->gear_R, LV_OBJ_FLAG_HIDDEN);

    scr->gear_D = lv_img_create(scr->gear_view);
    lv_img_set_src(scr->gear_D, LVGL_IMAGE_PATH(home/gear_r_d.png));
    lv_obj_set_pos(scr->gear_D, 0, 0);
    // lv_obj_add_flag(scr->gear_D, LV_OBJ_FLAG_HIDDEN);

    scr->gear_P = lv_img_create(scr->gear_view);
    lv_img_set_src(scr->gear_P, LVGL_IMAGE_PATH(home/gear_r_p.png));
    lv_obj_set_pos(scr->gear_P, 0, 0);
    lv_obj_add_flag(scr->gear_P, LV_OBJ_FLAG_HIDDEN);
}

static void home_mode_view_create(home_page_t *scr)
{
    /* 模式盘背景 icon_mode_dial.png (120,45,144,346) */
    scr->mode_dial = lv_img_create(scr->obj);
    lv_img_set_src(scr->mode_dial, LVGL_IMAGE_PATH(home/icon_mode_dial.png));
    lv_obj_set_pos(scr->mode_dial, 120, 45);

    /* 模式条容器 (32,101,194,240) - 4 条 194×60，垂直间隔 0 */
    scr->mode_view = lv_obj_create(scr->obj);
    lv_obj_set_size(scr->mode_view, 194, 240);
    lv_obj_set_pos(scr->mode_view, 32, 101);
    lv_obj_set_scrollbar_mode(scr->mode_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(scr->mode_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scr->mode_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(scr->mode_view, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 4 条模式图，单控件切换 n/p 图片表示选中态（由 home_refresh_mode_index 切换 src） */
    scr->mode_normal = lv_img_create(scr->mode_view);
    lv_img_set_src(scr->mode_normal, LVGL_IMAGE_PATH(home/drive_normal_n.png));
    lv_obj_set_pos(scr->mode_normal, 0, 0);
    
    scr->mode_eco = lv_img_create(scr->mode_view);
    lv_img_set_src(scr->mode_eco, LVGL_IMAGE_PATH(home/drive_eco_n.png));
    lv_obj_set_pos(scr->mode_eco, 0, 60);

    scr->mode_sport = lv_img_create(scr->mode_view);
    lv_img_set_src(scr->mode_sport, LVGL_IMAGE_PATH(home/drive_sport_n.png));
    lv_obj_set_pos(scr->mode_sport, 0, 120);

    scr->mode_setting = lv_img_create(scr->mode_view);
    lv_img_set_src(scr->mode_setting, LVGL_IMAGE_PATH(home/drive_setting_n.png));
    lv_obj_set_pos(scr->mode_setting, 0, 180);
}

static void home_battery_view_create(home_page_t *scr)
{
    /* 电量1/2/3 弧，三段统一用「A8 角度 mask」方案：满弧 PNG 弧外透明，
     *   挂 bitmap_mask_src=A8 mask buffer，由 lv_draw_sw_mask_angle 预渲染。
     *   SOC 增长→mask 角度增大→露出更多满弧，不盖背景；顶层各叠一张边框 PNG。
     *   几何/圆心/扫角见 home_page.h（三弧同构：start 336°, sweep 24°）。
     *   位置 x=598/654/709（中心 624/680/736，对齐旧弧中心）、顶 y=85
     *   （弧中点≈212，贴合指针 y≈209-215）。位置/指针 TUNE 上板微调。 */

    /* --- arc1: 满弧填充(底) + 边框(顶) --- */
    lv_obj_t *cont1 = lv_obj_create(scr->obj);
    lv_obj_set_size(cont1, 52, 224);
    lv_obj_set_pos(cont1, 598, 117);
    lv_obj_set_style_bg_opa(cont1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(cont1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cont1, LV_OBJ_FLAG_CLICKABLE);

    scr->arc1_fg = lv_img_create(cont1);
    lv_img_set_src(scr->arc1_fg, LVGL_IMAGE_PATH(home/icon_battery_arc_full.png));
    lv_obj_set_pos(scr->arc1_fg, 0, 0);
    lv_image_set_bitmap_map_src(scr->arc1_fg, &arc1_mask_dsc);
    /* battery_arc_draw_cb：LV_EVENT_DRAW_MAIN(PREPROCESS) 时按最新 SOC
     * 调用 lv_draw_sw_mask_angle_init/apply 实时重算 mask，绘制即用 */
    lv_obj_add_event_cb(scr->arc1_fg, battery_arc_draw_cb,
                        LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS, (void *)(intptr_t)0);

    scr->battery_arc1 = lv_img_create(cont1);   /* 边框，顶层 */
    lv_img_set_src(scr->battery_arc1, LVGL_IMAGE_PATH(home/icon_battery_border_full.png));
    lv_obj_set_pos(scr->battery_arc1, 0, 0);

    /* --- arc2: 满弧填充(底) + 边框(顶)，同 arc1 --- */
    lv_obj_t *cont2 = lv_obj_create(scr->obj);
    lv_obj_set_size(cont2, 53, 244);
    lv_obj_set_pos(cont2, 654, 97);
    lv_obj_set_style_bg_opa(cont2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(cont2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cont2, LV_OBJ_FLAG_CLICKABLE);

    scr->arc2_fg = lv_img_create(cont2);
    lv_img_set_src(scr->arc2_fg, LVGL_IMAGE_PATH(home/icon_battery_arc_mid.png));
    lv_obj_set_pos(scr->arc2_fg, 0, 0);
    lv_image_set_bitmap_map_src(scr->arc2_fg, &arc2_mask_dsc);
    lv_obj_add_event_cb(scr->arc2_fg, battery_arc_draw_cb,
                        LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS, (void *)(intptr_t)1);

    scr->battery_arc2 = lv_img_create(cont2);   /* 边框，顶层 */
    lv_img_set_src(scr->battery_arc2, LVGL_IMAGE_PATH(home/icon_battery_border_mid.png));
    lv_obj_set_pos(scr->battery_arc2, 0, 0);

    /* --- arc3: 满弧填充(底) + 边框(顶)，同 arc1 --- */
    lv_obj_t *cont3 = lv_obj_create(scr->obj);
    lv_obj_set_size(cont3, 54, 260);
    lv_obj_set_pos(cont3, 710, 81);
    lv_obj_set_style_bg_opa(cont3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(cont3, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cont3, LV_OBJ_FLAG_CLICKABLE);

    scr->arc3_fg = lv_img_create(cont3);
    lv_img_set_src(scr->arc3_fg, LVGL_IMAGE_PATH(home/icon_battery_arc_low.png));
    lv_obj_set_pos(scr->arc3_fg, 0, 0);
    lv_image_set_bitmap_map_src(scr->arc3_fg, &arc3_mask_dsc);
    lv_obj_add_event_cb(scr->arc3_fg, battery_arc_draw_cb,
                        LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS, (void *)(intptr_t)2);

    scr->battery_arc3 = lv_img_create(cont3);   /* 边框，顶层 */
    lv_img_set_src(scr->battery_arc3, LVGL_IMAGE_PATH(home/icon_battery_border_low.png));
    lv_obj_set_pos(scr->battery_arc3, 0, 0);

    /* 三弧同构：start 336°, sweep 24°（见 home_page.h） */
    scr->bat_rot[0] = 336;   scr->bat_sweep[0] = 24;
    scr->bat_rot[1] = 336;   scr->bat_sweep[1] = 24;
    scr->bat_rot[2] = 336;   scr->bat_sweep[2] = 24;

    /* 电池指针 icon_dial_pointer.png (36×16)，各随对应包 SOC 旋转 */
    scr->dial_pointer_main = lv_img_create(scr->obj);
    lv_img_set_src(scr->dial_pointer_main, LVGL_IMAGE_PATH(home/icon_dial_pointer.png));
    lv_obj_set_pos(scr->dial_pointer_main, 613, 215);
    lv_img_set_pivot(scr->dial_pointer_main, 18, 8);
    lv_obj_add_flag(scr->dial_pointer_main, LV_OBJ_FLAG_HIDDEN);

    scr->dial_pointer_sub = lv_img_create(scr->obj);
    lv_img_set_src(scr->dial_pointer_sub, LVGL_IMAGE_PATH(home/icon_dial_pointer.png));
    lv_obj_set_pos(scr->dial_pointer_sub, 669, 209);
    lv_img_set_pivot(scr->dial_pointer_sub, 18, 8);
    lv_obj_add_flag(scr->dial_pointer_sub, LV_OBJ_FLAG_HIDDEN);

    scr->dial_pointer_third = lv_img_create(scr->obj);
    lv_img_set_src(scr->dial_pointer_third, LVGL_IMAGE_PATH(home/icon_dial_pointer.png));
    lv_obj_set_pos(scr->dial_pointer_third, 725, 209);   /* 镜像 main/sub 递增，TUNE */
    lv_img_set_pivot(scr->dial_pointer_third, 18, 8);
    lv_obj_add_flag(scr->dial_pointer_third, LV_OBJ_FLAG_HIDDEN);

    /* 指针固定在电量弧 50% 位置（仅图片控件，不参与刷新） */
    lv_img_set_angle(scr->dial_pointer_main,  (scr->bat_rot[0] + scr->bat_sweep[0] / 2) * 10);
    lv_img_set_angle(scr->dial_pointer_sub,   (scr->bat_rot[1] + scr->bat_sweep[1] / 2) * 10);
    lv_img_set_angle(scr->dial_pointer_third, (scr->bat_rot[2] + scr->bat_sweep[2] / 2) * 10);

    /* 电池1/2/3 进度条（底部，bottom_bg 之上）30×20 @(617/673/729,345)：
     * 背景 = icon_battery_empty.png（空电池边框+电极帽，内腔透明；由 icon_battery_full.png
     *        抠掉彩色内腔得到——饱和度高 max-min>=40 的像素置透明，灰色边框原样保留），
     * 指示器 = 纯色，颜色与数值由 power_view.c::home_refresh_battery 按 SOC 档位刷新。
     * padding 内缩到内腔（BAT_BAR_PAD_*），使指示器正好填在内腔里。 */
    static const lv_coord_t bat_x[3] = { 617, 673, 729 };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *bat = lv_bar_create(scr->obj);
        lv_obj_set_size(bat, BAT_BAR_W, BAT_BAR_H);
        lv_obj_set_pos(bat, bat_x[i], 345);
        lv_bar_set_range(bat, 0, 100);
        lv_bar_set_value(bat, 0, LV_ANIM_OFF);

        /* 背景只留空框图：底色/边框/圆角全关，padding 对齐内腔 */
        lv_obj_set_style_bg_opa(bat, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_image_src(bat, LVGL_IMAGE_PATH(home/icon_battery_empty.png),
                                      LV_PART_MAIN);
        lv_obj_set_style_border_width(bat, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bat, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_left(bat, BAT_BAR_PAD_L, LV_PART_MAIN);
        lv_obj_set_style_pad_right(bat, BAT_BAR_PAD_R, LV_PART_MAIN);
        lv_obj_set_style_pad_top(bat, BAT_BAR_PAD_T, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(bat, BAT_BAR_PAD_B, LV_PART_MAIN);

        lv_obj_set_style_radius(bat, 2, LV_PART_INDICATOR);
        scr->bat_bar[i] = bat;
    }

    /* 充电提示 icon_charge.png 32×20 (575,377)
     * 默认隐藏：VEH_CHARGER_STATUS 为 1 时由 signal_view 显示，为 0 时隐藏 */
    scr->icon_charge = lv_img_create(scr->obj);
    lv_img_set_src(scr->icon_charge, LVGL_IMAGE_PATH(home/icon_charge.png));
    lv_obj_set_pos(scr->icon_charge, 572, 375);
    lv_obj_add_flag(scr->icon_charge, LV_OBJ_FLAG_HIDDEN);
}

static void home_mileage_bottom_create(home_page_t *scr)
{
    /* Zion logo (352,402,114,48) */
    lv_obj_t *logo = lv_img_create(scr->obj);
    lv_img_set_src(logo, LVGL_IMAGE_PATH(home/icon_logo_zion.png));
    lv_obj_set_pos(logo, 352, 402);

    /* 续航 205km (722,374) */
    scr->range_lab = lv_label_create(scr->obj);
    lv_label_set_text(scr->range_lab, "205km");
    lv_obj_set_size(scr->range_lab, 180, 26);
    lv_obj_set_pos(scr->range_lab, 620, 374);
    lv_obj_set_style_text_align(scr->range_lab, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(scr->range_lab, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->range_lab, aic_get_lang_font(DEFAULT_TTF, 24, true), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* mileage_cont (0,410,800,70) - TRIP/ODO/电量文字层 */
    scr->mileage_cont = lv_obj_create(scr->obj);
    lv_obj_set_size(scr->mileage_cont, 800, 70);
    lv_obj_set_pos(scr->mileage_cont, 0, 410);
    lv_obj_set_scrollbar_mode(scr->mileage_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(scr->mileage_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scr->mileage_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(scr->mileage_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* TRIP 块：左 (12,36)/(48,29) */
    scr->tirp_lab = lv_label_create(scr->mileage_cont);
    lv_label_set_text(scr->tirp_lab, aic_get_translation(trip));
    lv_obj_set_size(scr->tirp_lab, 44, 18);
    lv_obj_set_pos(scr->tirp_lab, 12, 36);
    lv_obj_set_style_text_color(scr->tirp_lab, lv_color_hex(0x7d7d7d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->tirp_lab, aic_get_multi_lang_font(18), LV_PART_MAIN | LV_STATE_DEFAULT);

    scr->trip_label = lv_label_create(scr->mileage_cont);
    lv_label_set_text(scr->trip_label, aic_get_translation(num999));
    lv_obj_set_size(scr->trip_label, 69, 26);
    lv_obj_set_pos(scr->trip_label, 56, 29);
    lv_obj_set_style_text_color(scr->trip_label, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(scr->trip_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->trip_label, aic_get_multi_lang_font(24), LV_PART_MAIN | LV_STATE_DEFAULT);

    scr->trip_unit = lv_label_create(scr->mileage_cont);
    lv_label_set_text(scr->trip_unit, aic_get_translation(km));
    lv_obj_set_size(scr->trip_unit, 42, 18);
    lv_obj_set_pos(scr->trip_unit, 131, 36);
    lv_obj_set_style_text_align(scr->trip_unit, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(scr->trip_unit, lv_color_hex(0x7d7d7d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->trip_unit, aic_get_multi_lang_font(18), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ODO 块：右 (639,36)/(673,29) */
    scr->odo_lab = lv_label_create(scr->mileage_cont);
    lv_label_set_text(scr->odo_lab, aic_get_translation(odo));
    lv_obj_set_size(scr->odo_lab, 44, 18);
    lv_obj_set_pos(scr->odo_lab, 618, 36);
    lv_obj_set_style_text_color(scr->odo_lab, lv_color_hex(0x7d7d7d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->odo_lab, aic_get_multi_lang_font(18), LV_PART_MAIN | LV_STATE_DEFAULT);

    scr->odo_label = lv_label_create(scr->mileage_cont);
    lv_label_set_text(scr->odo_label, aic_get_translation(num));
    lv_obj_set_size(scr->odo_label, 88, 26);
    lv_obj_set_pos(scr->odo_label, 662, 29);
    lv_obj_set_style_text_color(scr->odo_label, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(scr->odo_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->odo_label, aic_get_multi_lang_font(24), LV_PART_MAIN | LV_STATE_DEFAULT);

    scr->odo_unit = lv_label_create(scr->mileage_cont);
    lv_label_set_text(scr->odo_unit, aic_get_translation(km));
    lv_obj_set_size(scr->odo_unit, 42, 18);
    lv_obj_set_pos(scr->odo_unit, 756, 36);
    lv_obj_set_style_text_align(scr->odo_unit, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(scr->odo_unit, lv_color_hex(0x7d7d7d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->odo_unit, aic_get_multi_lang_font(18), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 电量块：中间 electrical（保留 view 层刷新接口） */
    scr->electrical_percentage = lv_label_create(scr->mileage_cont);
    lv_label_set_text(scr->electrical_percentage, aic_get_translation(hour));
    lv_obj_set_size(scr->electrical_percentage, 60, 27);
    lv_obj_set_pos(scr->electrical_percentage, 360, 29);
    lv_obj_set_style_text_color(scr->electrical_percentage, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(scr->electrical_percentage, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->electrical_percentage, aic_get_multi_lang_font(36), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(scr->electrical_percentage, LV_OBJ_FLAG_HIDDEN);

    scr->electrical_bar = lv_bar_create(scr->mileage_cont);
    lv_bar_set_mode(scr->electrical_bar, LV_BAR_MODE_NORMAL);
    lv_bar_set_range(scr->electrical_bar, 0, 100);
    lv_bar_set_value(scr->electrical_bar, 50, LV_ANIM_OFF);
    lv_obj_set_size(scr->electrical_bar, 120, 12);
    lv_obj_set_pos(scr->electrical_bar, 425, 36);
    lv_obj_set_style_bg_color(scr->electrical_bar, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr->electrical_bar, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(scr->electrical_bar, lv_color_hex(0x00ff55), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_add_flag(scr->electrical_bar, LV_OBJ_FLAG_HIDDEN);

    scr->electrical_value = lv_label_create(scr->mileage_cont);
    lv_label_set_text(scr->electrical_value, aic_get_translation(hour));
    lv_obj_set_size(scr->electrical_value, 50, 27);
    lv_obj_set_pos(scr->electrical_value, 550, 29);
    lv_obj_set_style_text_color(scr->electrical_value, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(scr->electrical_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->electrical_value, aic_get_multi_lang_font(36), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(scr->electrical_value, LV_OBJ_FLAG_HIDDEN);

    scr->electrical_unit = lv_label_create(scr->mileage_cont);
    lv_label_set_text(scr->electrical_unit, aic_get_translation(km));
    lv_obj_set_size(scr->electrical_unit, 32, 18);
    lv_obj_set_pos(scr->electrical_unit, 600, 36);
    lv_obj_set_style_text_color(scr->electrical_unit, lv_color_hex(0x7d7d7d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(scr->electrical_unit, aic_get_multi_lang_font(18), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(scr->electrical_unit, LV_OBJ_FLAG_HIDDEN);
}

static void home_signal_create(home_page_t *scr)
{
    // Init scr->icon_MW (hidden, keep signal_view interface)
    scr->icon_MW = lv_img_create(scr->obj);
    lv_img_set_src(scr->icon_MW, LVGL_IMAGE_PATH(light/icon_main_warning.png));
    lv_img_set_pivot(scr->icon_MW, 50, 50);
    lv_img_set_angle(scr->icon_MW, 0);
    lv_obj_set_pos(scr->icon_MW, 29, 355);
    lv_obj_add_flag(scr->icon_MW, LV_OBJ_FLAG_HIDDEN);

    // Init scr->icon_TC
    scr->icon_TC = lv_img_create(scr->obj);
    lv_img_set_src(scr->icon_TC, LVGL_IMAGE_PATH(light/icon_tc_off.png));
    lv_img_set_pivot(scr->icon_TC, 50, 50);
    lv_img_set_angle(scr->icon_TC, 0);
    lv_obj_set_pos(scr->icon_TC, 81, 355);
    lv_obj_add_flag(scr->icon_TC, LV_OBJ_FLAG_HIDDEN);

    // Init scr->icon_temp_warning
    scr->icon_temp_warning = lv_img_create(scr->obj);
    lv_img_set_src(scr->icon_temp_warning, LVGL_IMAGE_PATH(light/icon_tpms_warning.png));
    lv_img_set_pivot(scr->icon_temp_warning, 50, 50);
    lv_img_set_angle(scr->icon_temp_warning, 0);
    lv_obj_set_pos(scr->icon_temp_warning, 133, 355);
    lv_obj_add_flag(scr->icon_temp_warning, LV_OBJ_FLAG_HIDDEN);

    // Init scr->icon_cc_status
    scr->icon_cc_status = lv_img_create(scr->obj);
    lv_img_set_src(scr->icon_cc_status, LVGL_IMAGE_PATH(light/icon_cc_active.png));
    lv_img_set_pivot(scr->icon_cc_status, 50, 50);
    lv_img_set_angle(scr->icon_cc_status, 0);
    lv_obj_set_pos(scr->icon_cc_status, 185, 355);
    lv_obj_add_flag(scr->icon_cc_status, LV_OBJ_FLAG_HIDDEN);

    // Init scr->icon_vcu
    scr->icon_vcu = lv_img_create(scr->obj);
    lv_img_set_src(scr->icon_vcu, LVGL_IMAGE_PATH(light/icon_vcu_fault.png));
    lv_img_set_pivot(scr->icon_vcu, 50, 50);
    lv_img_set_angle(scr->icon_vcu, 0);
    lv_obj_set_pos(scr->icon_vcu, 170, 428);
    lv_obj_add_flag(scr->icon_vcu, LV_OBJ_FLAG_HIDDEN);

    // Init scr->icon_high_beam
    scr->icon_high_beam = lv_img_create(scr->obj);
    lv_img_set_src(scr->icon_high_beam, LVGL_IMAGE_PATH(light/icon_highbeam.png));
    lv_img_set_pivot(scr->icon_high_beam, 50, 50);
    lv_img_set_angle(scr->icon_high_beam, 0);
    lv_obj_set_pos(scr->icon_high_beam, 216, 428);
    lv_obj_add_flag(scr->icon_high_beam, LV_OBJ_FLAG_HIDDEN);

    // Init scr->icon_near_beam
    scr->icon_near_beam = lv_img_create(scr->obj);
    lv_img_set_src(scr->icon_near_beam, LVGL_IMAGE_PATH(light/icon_lowbeam.png));
    lv_img_set_pivot(scr->icon_near_beam, 50, 50);
    lv_img_set_angle(scr->icon_near_beam, 0);
    lv_obj_set_pos(scr->icon_near_beam, 262, 428);
    lv_obj_add_flag(scr->icon_near_beam, LV_OBJ_FLAG_HIDDEN);
    
    // Init scr->icon_ready
    scr->icon_ready = lv_img_create(scr->obj);
    lv_img_set_src(scr->icon_ready, LVGL_IMAGE_PATH(light/icon_ready.png));
    lv_img_set_pivot(scr->icon_ready, 50, 50);
    lv_img_set_angle(scr->icon_ready, 0);
    lv_obj_set_pos(scr->icon_ready, 343, 138);

    // Init scr->icon_battery
    scr->icon_battery = lv_img_create(scr->obj);
    lv_img_set_src(scr->icon_battery, LVGL_IMAGE_PATH(light/icon_battery_fault.png));
    lv_img_set_pivot(scr->icon_battery, 50, 50);
    lv_img_set_angle(scr->icon_battery, 0);
    lv_obj_set_pos(scr->icon_battery, 524, 428);
    lv_obj_add_flag(scr->icon_battery, LV_OBJ_FLAG_HIDDEN);

    // Init scr->icon_abs
    scr->icon_abs = lv_img_create(scr->obj);
    lv_img_set_src(scr->icon_abs, LVGL_IMAGE_PATH(light/icon_abs_fault.png));
    lv_img_set_pivot(scr->icon_abs, 50, 50);
    lv_img_set_angle(scr->icon_abs, 0);
    lv_obj_set_pos(scr->icon_abs, 569, 428);
    lv_obj_add_flag(scr->icon_abs, LV_OBJ_FLAG_HIDDEN);
}

void home_page_create(ui_manager_t *ui)
{
    home_page_t *scr = home_page_get(ui);

    if (!ui->auto_del && scr->obj) {
        home_page_update_current_language();
        return;
    }

    /* screen root */
    scr->obj = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(scr->obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(scr->obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);

    scr->wallpaper = lv_img_create(scr->obj);
    lv_img_set_src(scr->wallpaper, ui_theme_image_path("wallpaper.png"));
    lv_obj_set_pos(scr->wallpaper, 0, 0);

    /* 层级（从下到上）：表盘 → bottom_bg → gear → battery → mode → logo → top_bar */
    /* 1. 表盘（dashboard_n + 速度数字 + progress_arc） */
    home_speed_view_create(scr);
    /* 2. 底部背景（表盘之上） */
    lv_obj_t *bottom_bg = lv_img_create(scr->obj);
    lv_img_set_src(bottom_bg, LVGL_IMAGE_PATH(home/bottom_bg.png));
    lv_obj_set_pos(bottom_bg, 0, 272);
    /* 3. 档位（bottom_bg 之上） */
    home_gear_view_create(scr);
    /* 4. 电池/模式/底部(含 logo)/顶部 */
    home_battery_view_create(scr);
    home_mode_view_create(scr);
    home_mileage_bottom_create(scr);
    home_top_bar_create(scr);

    home_signal_create(scr);
    home_signal_view_init();

    /* 默认 ECO 模式选中 */
    home_refresh_mode_index(MODE_NORMAL);
}

void home_page_update_language(lang_type_t lang)
{
    aic_set_language(lang);
    home_page_update_current_language();
}

void home_page_update_current_language(void)
{
    home_page_t *scr = home_page_get(&ui_manager);
    if (!scr->obj) return;

#define SET_LANG_FONT(member, size)                                                      \
    do {                                                                                 \
        if (scr->member)                                                                 \
            lv_obj_set_style_text_font(scr->member, aic_get_multi_lang_font(size),       \
                                       LV_PART_MAIN | LV_STATE_DEFAULT);                 \
    } while (0)

    /* ① 纯静态标签：字体 + 文案（主界面 TRIP/ODO 保持英文缩写，仍走词条便于后续可译） */
    SET_LANG_FONT(tirp_lab, 18);
    if (scr->tirp_lab)
        lv_label_set_text(scr->tirp_lab, aic_get_translation(trip));
    SET_LANG_FONT(odo_lab, 18);
    if (scr->odo_lab)
        lv_label_set_text(scr->odo_lab, aic_get_translation(odo));

    /* ② 实时值控件：只重设字体，绝不写文本
     * （trip_label/odo_label 由 mileage_view 的 home_refresh_trip/odo 写，
     *   electrical_* 由 electrical_view 写；写占位串会把实时值冲掉） */
    SET_LANG_FONT(trip_label, 24);
    SET_LANG_FONT(odo_label, 24);
    SET_LANG_FONT(electrical_percentage, 36);
    SET_LANG_FONT(electrical_value, 36);

    /* ③ 单位标签：只重设字体。文本由 home_refresh_mileage_unit/electric 按当前
     * 公/英制写 km/mile，此处若写 aic_get_translation(km) 会把英制打回 km */
    SET_LANG_FONT(trip_unit, 18);
    SET_LANG_FONT(odo_unit, 18);
    SET_LANG_FONT(electrical_unit, 18);

#undef SET_LANG_FONT
}

void home_page_refresh_theme(void)
{
    home_page_t *scr = home_page_get(&ui_manager);
    if (!scr->obj)
        return;
    lv_img_set_src(scr->wallpaper, ui_theme_image_path("wallpaper.png"));
    (void)scr;
}
