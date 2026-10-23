#ifndef HOME_PAGE_H
#define HOME_PAGE_H

/*
 * home_page.h — 800×480 分辨率下电量弧 arc1/arc2/arc3 的布局几何
 *
 * power_view.c 引用本文件的 ARCn_W/H/VX/VY/START/SWEEP 宏，home_page.c 引用
 * BAT_BAR_* 宏；切换分辨率时各 UI_RES_DIR 目录维护一份同名 home_page.h 即可。
 *
 * 圆心与扫角计算法（PNG-local 坐标系，原点=图左上、y 轴向下；各满弧 PNG
 * 弧外 alpha=0，本身不盖背景）：
 *   1) 圆心 y = PNG 中 y 最大的非透明像素 ymax；
 *   2) P1 = x 取最小值 xmin 时，该列最上方（y 最小）非透明像素；
 *   3) P2 = y 取最小值 ymin 时，该行最左侧（x 最小）非透明像素；
 *   4) P1P2 连线与水平线 y=ymax 的交点即圆心 C=(vx, vy=ymax)；
 *   5) P1P2 与水平线的夹角即满弧扫角 sweep；
 *   6) start = 圆心→P1 方向角（C、P1、P2 共线，故与 P1P2 同向，跨 0°）。
 *
 * mask 采用 lv_draw_sw_mask_angle：扇形 [start, start+sweep]（跨 0°）内可见
 * (255)、外遮罩(0)。SOC 0..100 → end 由 start 增至 start+sweep（end≥360 回绕），
 * soc=0 全隐、soc=100 满弧显示。
 *
 * 注：800×480 三张满弧 PNG 同构（斜率一致），故 start/sweep 相同，仅尺寸/圆心不同。
 */

/* arc1: icon_battery_arc_full.png (52×224)
 *   P1=(6,9)、P2=(15,5)，P1P2 与 y=219 交 → C=(-466,219)，夹角≈24.0° */
#define ARC1_W      52
#define ARC1_H      224
#define ARC1_VX     (-466)
#define ARC1_VY     224
#define ARC1_START  335
#define ARC1_SWEEP  25

/* arc2: icon_battery_arc_mid.png (53×244)
 *   P1=(6,11)、P2=(15,7)，P1P2 与 y=239 交 → C=(-507,239)，夹角≈24.0° */
#define ARC2_W      53
#define ARC2_H      244
#define ARC2_VX     (-507)
#define ARC2_VY     244
#define ARC2_START  335
#define ARC2_SWEEP  25

/* arc3: icon_battery_arc_low.png (54×260)
 *   P1=(6,10)、P2=(15,6)，P1P2 与 y=255 交 → C=(-545,255)，夹角≈24.0° */
#define ARC3_W      54
#define ARC3_H      260
#define ARC3_VX     (-545)
#define ARC3_VY     260
#define ARC3_START  335
#define ARC3_SWEEP  25

/* 底部电池进度条 bat1/2/3（lv_bar）几何：尺寸 = 底图 icon_battery_empty.png 尺寸，
 * padding 按电池内腔 bbox 内缩，使指示器正好落在内腔里（不压到灰色边框）。
 * 内腔 = 三张图标 icon_battery_low/mid/full.png 填充区域的并集，
 * 800×480 为 x5..x21 × y6..y13，故 left=5、right=30-1-21=8、top=6、bottom=20-1-13=6。 */
#define BAT_BAR_W       30
#define BAT_BAR_H       20
#define BAT_BAR_PAD_L   5
#define BAT_BAR_PAD_R   8
#define BAT_BAR_PAD_T   6
#define BAT_BAR_PAD_B   6

#define POWER_START_ANGLE 180
#define MAX_POWER_ANGLE 225
#define MIN_POWER_ANGLE 45

#endif /* HOME_PAGE_H */
