/*
 * speed_view.c - 仪表盘数据刷新（移植自 AWTK speed_view）
 *
 * 涉及 UI 控件：scr->speed_digit[3] / progress_arc / pointer / speed_unit / driving_mode / gear_P/D/R
 * 各控件由 home_page.c::home_speed_view_create() 创建，本文件只做数据写入。
 */

#include "speed_view.h"
#include "lvgl.h"
#include "aic_ui.h"
#include "vehicle_argument.h"
#include "vehicle_data.h"   /* vehicle_get_data_speed */
#include <stddef.h>
#include "definition.h"

bool g_stop_state = true;

/* =========================================================================
 * 数字位图拆位（AWTK image_value 等价物）
 * ========================================================================= */
static void speed_digits_set(int speed)
{
    home_page_t *page = home_page_get(&ui_manager);
    if (!page || !page->speed_digit[0]) return;
    if (speed < 0)   speed = 0;
    if (speed > 999) speed = 999;

    int digits[3] = { speed / 100, (speed / 10) % 10, speed % 10 };
    int num   = (speed >= 100) ? 3 : (speed >= 10) ? 2 : 1;
    int first = 3 - num;

    lv_coord_t base_x = lv_obj_get_x(page->speed_digit[0]);
    lv_coord_t dw     = lv_obj_get_width(page->speed_digit[0]);
    lv_coord_t start  = base_x + (lv_coord_t)(3 - num) * dw / 2;

    for (int i = 0; i < 3; i++) {
        if (i < first) {
            lv_obj_add_flag(page->speed_digit[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            char path[80];
            lv_snprintf(path, sizeof(path), "L:/rodata/lvgl_data/image/speed/number_%d.png", digits[i]);
            lv_img_set_src(page->speed_digit[i], path);
            lv_obj_set_x(page->speed_digit[i], start + (lv_coord_t)(i - first) * dw);
            lv_obj_remove_flag(page->speed_digit[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* 速度弧按值分段配色（arc range 0..270）
 * 0-44 绿 0x41FC72 / 45-180 青 0x33F6CA / 181-270 红 0xFC0B0B */
static lv_color_t speed_arc_color(int cur)
{
    if (cur <= 44)     return lv_color_hex(0x41FC72);
    if (cur <= 180)    return lv_color_hex(0x33F6CA);
    return lv_color_hex(0xFC0B0B);
}

static const char *halo_img[DRV_MODE_MAX] = {
    "L:/rodata/lvgl_data/image/test/bg_halo_0.png",
    "L:/rodata/lvgl_data/image/test/bg_halo_1.png",
    "L:/rodata/lvgl_data/image/test/bg_halo_2.png",
};
static const char *drv_mode_img[DRV_MODE_MAX] = {
    "L:/rodata/lvgl_data/image/test/drv_mode_0.png",
    "L:/rodata/lvgl_data/image/test/drv_mode_1.png",
    "L:/rodata/lvgl_data/image/test/drv_mode_2.png",
};

/* =========================================================================
 * 公开接口
 * ========================================================================= */
void home_refresh_speed(uint32_t speed)
{
    /* 去重按换算后的显示值：切单位后即使车速不变也要重绘数字位图 */
    static int lastShown = -1;
    int cur = (int)speed;
    if (MPH == vehicle_get_param_unit()) cur = (int)(cur * KM_CONVERT_MILE);
    if (cur == lastShown) return;
    lastShown = cur;

    if (speed != 0 && g_stop_state) home_refresh_stop_state(false);

    home_page_t *page = home_page_get(&ui_manager);
    if (!page || !page->speed_view) return;

    /* 1. 速度大数字位图（number_X.png，3 位居中） */
    speed_digits_set(cur);
}

void home_refresh_speed_power(uint32_t pct)
{
    static int last = -1;
    if (pct == last) return;
    if (pct < POWER_MIN)   pct = POWER_MIN;
    if (pct > POWER_MAX)   pct = POWER_MAX;
    last = pct;

    // printf(" home_refresh_speed_power pct = %d\n" , pct);
    home_page_t *page = home_page_get(&ui_manager);
    if (!page || !page->speed_view) return;
    /* 2. 圆弧进度（原生 lv_arc）
     *    value 走 range(0..200) 归一化,arc 内部按 angles(0,270) 映射,叠加 rotation=135 起点
     *    业务值 0..200 km/h 视觉上落在 135°..405° (8 点钟到 4 点钟方向) */
    if (page->progress_arc) {
        int cur;
        // if(pct >= 30000)
        {
            cur = (int)(((int32_t)pct - POWER_MIN) * 0.01);
            lv_arc_set_value(page->progress_arc, cur);

            /* 按值变色：主弧 + 光晕弧同步换色，变色时才 set */
            // static lv_color_t last_arc_color;
            // static bool last_arc_color_valid = false;
            // lv_color_t arc_c = speed_arc_color(cur);
            // if (!last_arc_color_valid ||
            //     lv_color_to_u32(last_arc_color) != lv_color_to_u32(arc_c)) {
            //     last_arc_color = arc_c;
            //     last_arc_color_valid = true;
            //     lv_obj_set_style_arc_color(page->progress_arc, arc_c, LV_PART_INDICATOR);
            // }
            // lv_arc_set_value(page->progress_arc_re, 0);
        }
        // else
        // {
        //     cur = (int)((30000 - (int32_t)pct) * 0.01);
        //     lv_arc_set_value(page->progress_arc, 0);
        //     lv_arc_set_value(page->progress_arc_re, cur);
        // }
    }

    /* 3. 指针旋转
     *    公式：angle = (speed / 200) * 270 - 135  (-135..+135 度) */
    if (page->pointer) {
        int ang_x10, cur;
        cur = (int)(((int32_t)pct - 30000) * 0.01);
        ang_x10 = cur - 90;
        lv_img_set_angle(page->pointer, ang_x10 * 10);
    }
}

void home_refresh_stop_state(bool istop)
{
    home_page_t *page = home_page_get(&ui_manager);
    if (!page) return;
    // if (page->icon_ready) {
    //     if (istop) lv_obj_remove_flag(page->icon_ready, LV_OBJ_FLAG_HIDDEN);
    //     else       lv_obj_add_flag(page->icon_ready, LV_OBJ_FLAG_HIDDEN);
    // }
    // if (page->speed_lab) {
    //     if (istop) lv_obj_remove_flag(page->speed_lab, LV_OBJ_FLAG_HIDDEN);
    //     else       lv_obj_add_flag(page->speed_lab, LV_OBJ_FLAG_HIDDEN);
    // }
    g_stop_state = istop;
}

void home_refresh_unit(unit_e unit)
{
    home_page_t *page = home_page_get(&ui_manager);
    if (!page || !page->speed_unit) return;
    if (unit == KM_H)
        lv_img_set_src(page->speed_unit, "L:/rodata/lvgl_data/image/test/unit_km_h.png");
    else
        lv_img_set_src(page->speed_unit, "L:/rodata/lvgl_data/image/test/unit_mph.png");
}

/* 速度单位文本：主界面为 kmh_lab label（page->speed_unit 图片位未创建，见 home_page.c） */
void home_refresh_speed_unit(unit_e unit)
{
    home_page_t *page = home_page_get(&ui_manager);
    if (!page || !page->kmh_lab) return;
    lv_label_set_text(page->kmh_lab, (unit == KM_H) ? "km/h" : "mph");
}

void home_refresh_drv_mode(drv_mode_e m)
{
    if (m >= DRV_MODE_MAX) return;
    home_page_t *page = home_page_get(&ui_manager);
    if (!page) return;
    if (page->bg_halo)      lv_img_set_src(page->bg_halo,      halo_img[m]);
    if (page->driving_mode) lv_img_set_src(page->driving_mode, drv_mode_img[m]);
}

void home_refresh_gear(gear_e g)
{
    if ((int)g < 0 || g >= GEAR_MAX)
        g = GEAR_P;   /* CAN 数据非法(负值/超界)时默认显示驻车 */
    home_page_t *page = home_page_get(&ui_manager);
    if (!page) return;

    /* 下标 = VEH_GEAR_POSITION 原始值: raw0/1→gear_P(驻车), raw2→gear_D, raw3→gear_R;
     * 注意 index0 和 index1 指向同一个 gear_P 对象, 所以不能逐位"不是 i 就隐藏",
     * 必须全部隐藏后再单独显示目标 — 否则 g=0 时 i=0 解锁 P, 紧接着 i=1 又锁掉 P */
    lv_obj_t *gears[GEAR_MAX] = { page->gear_P, page->gear_P, page->gear_D, page->gear_R };
    for (int i = 0; i < GEAR_MAX; i++) {
        if (gears[i]) lv_obj_add_flag(gears[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (gears[g]) lv_obj_remove_flag(gears[g], LV_OBJ_FLAG_HIDDEN);

    /* 驻车就绪(RDY): 不为 P 挡(raw 0/1)且车速为 0 时点亮 icon_ready，否则隐藏 */
    if (page->icon_ready) {
        bool ready = (g != 0 && g != 1 && (vehicle_get_data_speed() == 0));
        if (ready) lv_obj_remove_flag(page->icon_ready, LV_OBJ_FLAG_HIDDEN);
        else       lv_obj_add_flag(page->icon_ready, LV_OBJ_FLAG_HIDDEN);
    }
}

/* =========================================================================
 * 累加器（logic.c 50ms 周期驱动），速度表与功率表共用同一套平滑算法
 * ========================================================================= */
typedef struct {
    int previous;
    int current;
    int data;
    int accumulated;
} data_accumulator_t;

static data_accumulator_t acc = {
    .current     = 0,
    .accumulated = 1,
    .previous    = 0,
};

/* 功率表额外一个累加器实例，算法与速度表完全一致（大跳变分 5 步逼近） */
static data_accumulator_t power_acc = {
    .current     = 0,
    .accumulated = 1,
    .previous    = 0,
};

static void run(data_accumulator_t *a, int step)
{
    int target = a->data;
    if (a->previous != target) {
        int diff = abs(a->previous - target);
        if (diff > step) a->accumulated = diff / 5;
        else           a->accumulated = 1;
        a->previous = target;
    }
    int inc = a->accumulated;
    if (a->current < target)      a->current = (a->current + inc > target) ? target : a->current + inc;
    else if (a->current > target) a->current = (a->current - inc < target) ? target : a->current - inc;
}

static inline bool should_update(int target, int current)
{
    return (target <= 2 || target > current || (target + 2) < current);
}

void speed_view_run(int target)
{
    acc.data = target;
    if (should_update(acc.data, acc.current)) run(&acc, 20);
}

int speed_view_current(void) { return acc.current; }

void speed_view_set_current(int value) { acc.current = value; }

void power_view_run(int target)
{
    power_acc.data = target;
    if (should_update(power_acc.data, power_acc.current)) run(&power_acc, 2700);
}

int power_view_current(void) { return power_acc.current; }

void power_view_set_current(int value) { power_acc.current = value; }
