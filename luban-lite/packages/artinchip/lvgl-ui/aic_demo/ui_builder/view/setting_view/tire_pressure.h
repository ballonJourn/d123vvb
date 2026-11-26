#ifndef TIRE_PRESSURE_H_
#define TIRE_PRESSURE_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L3 胎压检测页（meter view → Check Tire Pressure）
 *
 * 三态：
 *   BROWSE   浏览态：各轮红环按自身匹配状态常显（已匹配的不显示）
 *   SELECT   选择态：SET 进入，默认选中前轮并 500ms 闪烁；UP/DOWN 在未匹配轮间切换
 *   LEARNING 学习态：再按 SET 开始学习，弹 pop_tip "胎压匹配中"，轮询到结果自动收尾
 *                    （UI 侧另有 70s 看门狗，中间层超时未生效时按失败收尾，不会卡住）
 *
 * 画面：底图 img_tire_n.png + 两张 644×244 透明叠加指示层
 *       （setting_tire_pressure_left/right.png，红环分别套在前/后轮上）
 */

/* 进入 L3：懒加载构建本页 + 切层 + 起 500ms 刷新/闪烁定时（浏览态） */
void tire_pressure_init(void);

/* 布局构建（per-resolution，定义在各分辨率 setting_page.c）：创建 L3 层与控件 */
void tire_pressure_build(void);

/* 按键处理：SET 进选择态/开始学习，UP/DOWN 切轮位，BACK 回浏览态或退出 L3 */
void on_tire_pressure_deal_short_key(click_e key);

/* 隐去本页 + 停定时 + 复位状态（层常驻，不删除）；学习中退出会停学习并收掉弹窗 */
void tire_pressure_destroy(void);

/* 语言切换刷新：重填标题、重设字体、重填胎压文本（保留三态与轮位） */
void tire_pressure_relang(void);

#endif