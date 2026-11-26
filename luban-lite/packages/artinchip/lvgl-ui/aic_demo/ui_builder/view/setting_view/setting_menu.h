#ifndef SETTING_MENU_H_
#define SETTING_MENU_H_

#include "ui_init.h"
#include "button_manager.h"
#include <stdbool.h>

/* 四级菜单。L3/L4 本次占位，entry 表保留结构但 view_init=NULL。 */
typedef enum menu_level {
    MENU_LEVEL_0 = 0,
    MENU_LEVEL_1,   /* 六大分类入口 */
    MENU_LEVEL_2,   /* 分类内项目列表（本次仅 INSTRUMENT，封装于 instrument.c） */
    MENU_LEVEL_3,   /* 选项页（占位） */
    MENU_LEVEL_4,   /* 逐位调整（占位） */
} menu_level_e;

/* L1 六大分类（2×3 网格，按行线性索引）。
 * CATEGORY_CRUISE 无 L2 可进，不参与上下键焦点循环：其卡片仅按 VEH_CRUISE_MAIN_SWITCH 显示 n/p。 */
typedef enum setting_category {
    CATEGORY_CRUISE     = 0,   /* Cruise Control            (15,82)   */
    CATEGORY_UNLOCK     = 1,   /* Unlock the seat           (341,82)  */
    CATEGORY_MILEAGE    = 2,   /* Reset trip mileage        (15,168)  */
    CATEGORY_POWEROFF   = 3,   /* Power down the vehicle    (341,168) */
    CATEGORY_INSTRUMENT = 4,   /* Instrument Settings —— 唯一可进 L2 (15,254) */
    CATEGORY_METERVIEW  = 5,   /* Meter view                (341,254) */
    SETTING_CATEGORY_NUM,
} setting_category_e;

/* level / index 访问 */
menu_level_e get_current_menu_level(void);
void         set_current_menu_level(menu_level_e level);
setting_category_e get_l1_index(void);
int  get_current_menu_index(void);   /* 兼容旧声明，返回 l1_index */

/* L1 卡片图片 src 查询（构建与切换共用） */
const char *setting_l1_card_src(setting_category_e item, bool pressed);

/* L1 卡片文字名查询 */
const char *setting_l1_card_name(setting_category_e item);

/* L1 */
void setting_l1_view_init(void);
void setting_l1_set_focused_item(setting_category_e item);
void on_setting_l1_deal_short_click(click_e click);

/* L1 常驻层文案刷新（语言切换时调用） */
void setting_l1_refresh_language(void);

/* 生命周期 */
void setting_menu_destory(void);

/* 退出弹窗时释放弹窗图缓存（按 src 逐个 drop，保留 home 图不影响实时刷新） */
void setting_drop_popup_image_cache(void);

#endif
