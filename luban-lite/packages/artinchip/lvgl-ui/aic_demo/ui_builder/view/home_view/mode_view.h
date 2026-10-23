#ifndef HOME_MODE_VIEW_H
#define HOME_MODE_VIEW_H

#include "ui_objects.h"

/* 驾驶模式选择栏 4 项（800×480 设计稿） */
typedef enum {
    MODE_NORMAL = 0,
    MODE_ECO,
    MODE_SPORT,
    MODE_MAX
} mode_index_e;

/* 切换驾驶模式选中态（n/p 图切换） */
void home_refresh_mode_index(int idx);

/* 取当前模式索引 */
int  home_mode_index_get(void);

#endif