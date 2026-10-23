/*
 * power_view.c - 功率面板数据刷新
 */

#include "power_view.h"
#include "lvgl.h"
#include "lv_draw_sw_mask.h"   /* lv_draw_sw_mask_angle_init/apply/free (CPPPATH 含 sw 目录) */
#include "aic_ui.h"
#include "home_page.h"        /* 各弧布局几何 ARCn_W/H/VX/VY/START/SWEEP（按分辨率） */

/* arc1 A8 角度遮罩：满弧图 icon_battery_arc_full.png，几何/圆心/扫角见 home_page.h。
 *   mask 挂底层 arc1_fg 的 bitmap_mask_src，lv_img 重绘自动 blend；顶层边框 PNG 静态。
 *   buffer 内容由 battery_arc_draw_cb（LV_EVENT_DRAW_MAIN | PREPROCESS）在每次
 *   绘制时按最新 SOC 实时重算，不再预渲染。 */
static uint8_t arc1_mask_buf[ARC1_W * ARC1_H];   /* A8 alpha，常驻 */
const lv_image_dsc_t arc1_mask_dsc = {
    .header = {
        .cf      = LV_COLOR_FORMAT_A8,
        .w       = ARC1_W,
        .h       = ARC1_H,
        .stride  = ARC1_W,
    },
    .data_size = sizeof(arc1_mask_buf),
    .data      = arc1_mask_buf,
};

/* arc2 A8 角度遮罩（2号包）：满弧图 icon_battery_arc_mid.png，几何见 home_page.h。 */
static uint8_t arc2_mask_buf[ARC2_W * ARC2_H];
const lv_image_dsc_t arc2_mask_dsc = {
    .header = {
        .cf      = LV_COLOR_FORMAT_A8,
        .w       = ARC2_W,
        .h       = ARC2_H,
        .stride  = ARC2_W,
    },
    .data_size = sizeof(arc2_mask_buf),
    .data      = arc2_mask_buf,
};

/* arc3 A8 角度遮罩（3号包）：满弧图 icon_battery_arc_low.png，几何见 home_page.h。 */
static uint8_t arc3_mask_buf[ARC3_W * ARC3_H];
const lv_image_dsc_t arc3_mask_dsc = {
    .header = {
        .cf      = LV_COLOR_FORMAT_A8,
        .w       = ARC3_W,
        .h       = ARC3_H,
        .stride  = ARC3_W,
    },
    .data_size = sizeof(arc3_mask_buf),
    .data      = arc3_mask_buf,
};

/* =========================================================================
 * 电池弧角度 mask：SOC 状态 + 各弧几何表 + LV_EVENT_DRAW_MAIN 实时绘制回调
 *   - 业务侧 home_refresh_battery 只写 SOC 并 invalidate，不手动算 mask；
 *   - battery_arc_draw_cb 在 lv_img 默认绘制之前（PREPROCESS）按最新 SOC
 *     调用 lv_draw_sw_mask_angle_init/apply 实时重填 A8 buffer，绘制即用。
 *   - v9 图片外部 mask 仅 bitmap_mask_src（A8 data）一条通道，故 home_page.c
 *     仍把 arcN_mask_dsc 绑定到底层 arcN_fg；本回调只负责 buffer 内容实时化。
 * ========================================================================= */
static int s_arc_soc[3];                         /* 三包 SOC：绘制回调实时读取 */

typedef struct {
    uint8_t *buf;                                /* 对应 A8 mask buffer */
    uint16_t w, h;                               /* buffer 尺寸 */
    int16_t  vx, vy, start, sweep;               /* 圆心/起始角/扫角（PNG-local） */
} battery_arc_cfg_t;

static battery_arc_cfg_t s_arc_cfg[3] = {
    { arc1_mask_buf, ARC1_W, ARC1_H, ARC1_VX, ARC1_VY, ARC1_START, ARC1_SWEEP },
    { arc2_mask_buf, ARC2_W, ARC2_H, ARC2_VX, ARC2_VY, ARC2_START, ARC2_SWEEP },
    { arc3_mask_buf, ARC3_W, ARC3_H, ARC3_VX, ARC3_VY, ARC3_START, ARC3_SWEEP },
};

void battery_arc_draw_cb(lv_event_t * e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);   /* arc1/2/3 → 0/1/2 */
    if (idx < 0 || idx > 2) return;

    battery_arc_cfg_t *a = &s_arc_cfg[idx];

    int soc = s_arc_soc[idx];
    if (soc <= 0)
    {
        soc = 0;
        memset(a->buf, 0, (size_t)a->w * a->h);
        return;
    }
    if (soc >= 100)
    {
        soc = 100;
    }
    memset(a->buf, 0xFF, (size_t)a->w * a->h);

    int start = a->start + (100 - soc) * a->sweep / 100;   /* 0..100 → start..start+sweep */
    lv_draw_sw_mask_angle_param_t param;
    lv_draw_sw_mask_angle_init(&param, a->vx, a->vy, start, 360);

    void *mask_list[2] = { &param, NULL };
    for (int y = 0; y < a->h; y++)
    {
        lv_opa_t *line_buf = a->buf + y * a->w;
        lv_draw_sw_mask_res_t res = lv_draw_sw_mask_apply(mask_list, line_buf, 0, y, a->w);
        if(res == LV_DRAW_SW_MASK_RES_TRANSP)
            memset(line_buf, 0, (size_t)a->w);
    }
    lv_draw_sw_mask_free_param(&param);
}

static void power_digits_set(int pct)
{
    home_page_t *page = home_page_get(&ui_manager);
    if (!page || !page->power_digit[0]) return;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    int digits[3] = { pct / 100, (pct / 10) % 10, pct % 10 };
    int num   = (pct >= 100) ? 3 : (pct >= 10) ? 2 : 1;
    int first = 3 - num;

    lv_coord_t base_x = lv_obj_get_x(page->power_digit[0]);
    lv_coord_t dw     = lv_obj_get_width(page->power_digit[0]);
    lv_coord_t start  = base_x + (lv_coord_t)(3 - num) * dw / 2;

    for (int i = 0; i < 3; i++) {
        if (i < first) {
            lv_obj_add_flag(page->power_digit[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            char path[80];
            lv_snprintf(path, sizeof(path), "L:/rodata/lvgl_data/image/test/power_%d.png", digits[i]);
            lv_img_set_src(page->power_digit[i], path);
            lv_obj_set_x(page->power_digit[i], start + (lv_coord_t)(i - first) * dw);
            lv_obj_remove_flag(page->power_digit[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void home_refresh_power(int pct)
{
    static int last = -1;
    if (pct == last) return;
    last = pct;

    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    home_page_t *page = home_page_get(&ui_manager);
    if (!page) return;

    if (page->power_slider) lv_bar_set_value(page->power_slider, pct, LV_ANIM_OFF);
    power_digits_set(pct);
}

/* =========================================================================
 * 底部电池进度条 bat1/2/3（home_page.c 创建：背景 icon_battery_empty.png，指示器纯色）
 *   - 填充色按 SOC 档位取三张原图标的填充色：
 *       0-50   icon_battery_low.png   黄 (242,230,98)
 *       51-80  icon_battery_mid.png   青 (46,253,253)
 *       81-100 icon_battery_full.png  青绿 (28,250,202)
 *   - 档位不变则不重设样式（set_style 会触发 style 刷新+重绘），数值交给 lv_bar 自己判重
 * ========================================================================= */
#define BAT_BAND_MID_MIN   51
#define BAT_BAND_FULL_MIN  81

static int bat_band_of(int soc)
{
    if (soc < BAT_BAND_MID_MIN)  return 0;
    if (soc < BAT_BAND_FULL_MIN) return 1;
    return 2;
}

/* lv_color_hex 是 static inline，不能做静态初始化，故用函数返回 */
static lv_color_t bat_band_color(int band)
{
    switch (band) {
    case 0:  return lv_color_hex(0xF2E662);   /* icon_battery_low.png  黄 */
    case 1:  return lv_color_hex(0x2EFDFD);   /* icon_battery_mid.png  青 */
    default: return lv_color_hex(0x1CFACA);   /* icon_battery_full.png 青绿 */
    }
}

/* =========================================================================
 * 电量弧刷新（1/2/3 号包各自独立）
 *   - 只写 SOC 状态并 invalidate：mask buffer 内容由 battery_arc_draw_cb 在
 *     每次 LV_EVENT_DRAW_MAIN（PREPROCESS）时按最新 SOC 实时重算，绘制即用。
 *   - invalidate 顶层边框 battery_arcN：与底层 arcN_fg 同区重叠，会触发 cont
 *     内该区域重绘，arcN_fg 的 DRAW_MAIN 回调对新 SOC 重算 mask。
 *   - SOC 不变则跳过，对齐 lv_img_set_angle 值不变不 invalidate 的行为。
 *   - 指针不参与刷新：由 home_page.c 创建时固定在弧 50% 位置（仅图片控件）
 *   - 同一次调用顺带刷新底部电池进度条 bat1/2/3（与各弧同源同值）：进度条不走
 *     上面的去重分支，避免弧的去重把它一起挡掉。
 * ========================================================================= */
void home_refresh_battery(int soc1, int soc2, int soc3)
{
    home_page_t *p = home_page_get(&ui_manager);
    if (!p) return;

    int s[3] = { soc1, soc2, soc3 };
    lv_obj_t *arc[3] = { p->arc1_fg, p->arc2_fg, p->arc3_fg };
    static int last[3] = { -1, -1, -1 };
    static int last_band[3] = { -1, -1, -1 };

    for (int i = 0; i < 3; i++) {
        if (s[i] < 0)   s[i] = 0;
        if (s[i] > 100) s[i] = 100;

        if (p->bat_bar[i]) {
            int band = bat_band_of(s[i]);
            if (band != last_band[i]) {
                last_band[i] = band;
                lv_obj_set_style_bg_color(p->bat_bar[i], bat_band_color(band),
                                          LV_PART_INDICATOR);
            }
            lv_bar_set_value(p->bat_bar[i], s[i], LV_ANIM_OFF);
        }

        if (!arc[i] || s[i] == last[i]) continue;
        last[i] = s[i];
        s_arc_soc[i] = s[i];
        lv_obj_invalidate(arc[i]);
    }
}

/* 顶部 BAT 电压：vol 为 vehicle_param 中 VEH_VOLTAGE_BATTERY 的 0.1V 编码，
 * 0 是 battery_module 采样无效的哨兵值，与"电压为 0"区分开单独显示。
 * 去重前置，有效值与无效值两个分支共用同一个 last。 */
void home_refresh_battery_voltage(int vol)
{
    home_page_t *page = home_page_get(&ui_manager);
    if (!page || !page->bat_lab) return;

    static int last = -1;
    if (vol == last) return;
    last = vol;

    if (vol <= 0) {
        lv_label_set_text(page->bat_lab, "BAT:--V");
    } else {
        lv_label_set_text_fmt(page->bat_lab, "BAT:%d.%dV", vol / 10, vol % 10);
    }
}