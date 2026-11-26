#ifndef HOME_PAGE_H
#define HOME_PAGE_H

/*
 * home_page.h — 1024×600 分辨率下电量弧 arc1/arc2/arc3 的布局几何
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
 * 注：本目录三张满弧 PNG 是 800×480 同名图的等比放大（高比 286/224、312/244、
 * 332/260 均为 ≈1.277），故下面的 VX 直接取 800×480 的 VX × 1.277，START/SWEEP
 * 与 800×480 逐字相同，两分辨率三个弧的夹角因此统一为 24°（= 359 - START）。
 * 不要改用上面第 4 步反推的 VX：角点 P1/P2 受抗锯齿影响可达 ±2px，据此算出的
 * VX 偏右（如 arc1 得 -526），会让 SOC=100 时弧顶被 mask 切掉一截。
 * 三弧 START 也必须取同一个值，否则同一 SOC 下扇形张角不同，露出比例就不一致。
 */

/* arc1: icon_battery_arc_full.png (66×286)，VX = -466 × 1.277 */
#define ARC1_W      66
#define ARC1_H      286
#define ARC1_VX     (-595)
#define ARC1_VY     286
#define ARC1_START  335
#define ARC1_SWEEP  25

/* arc2: icon_battery_arc_mid.png (67×312)，VX = -507 × 1.277 */
#define ARC2_W      67
#define ARC2_H      312
#define ARC2_VX     (-648)
#define ARC2_VY     312
#define ARC2_START  335
#define ARC2_SWEEP  25

/* arc3: icon_battery_arc_low.png (69×332)，VX = -545 × 1.277 */
#define ARC3_W      69
#define ARC3_H      332
#define ARC3_VX     (-696)
#define ARC3_VY     332
#define ARC3_START  335
#define ARC3_SWEEP  25

/* 底部电池进度条 bat1/2/3（lv_bar）几何：尺寸 = 底图 icon_battery_empty.png 尺寸，
 * padding 按电池内腔 bbox 内缩，使指示器正好落在内腔里（不压到灰色边框）。
 * 内腔 = 三张图标 icon_battery_low/mid/full.png 填充区域的并集，
 * 1024×600 为 x7..x28 × y8..y16，故 left=7、right=38-1-28=9、top=8、bottom=26-1-16=9。 */
#define BAT_BAR_W       38
#define BAT_BAR_H       26
#define BAT_BAR_PAD_L   7
#define BAT_BAR_PAD_R   9
#define BAT_BAR_PAD_T   8
#define BAT_BAR_PAD_B   9

#define POWER_START_ANGLE 180
#define MAX_POWER_ANGLE 225
#define MIN_POWER_ANGLE 45

#endif /* HOME_PAGE_H */
