#ifndef POP_TIP_PAGE_H
#define POP_TIP_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/*
 * pop_tip 弹窗布局层 — 800×480 几何
 *
 * 只负责控件几何与内容呈现，业务逻辑(位图/ack/5s/码表)见
 * view/pop_tip_view/pop_tip_manager.c。各 UI_RES_DIR 目录同名同接口，
 * 切换分辨率改 SConscript 的 UI_RES_DIR 即可。
 */

///< 按内容显示弹窗: bg/icon 为 LVGL 图 src, text 为已翻译文案(可为 NULL)
void pop_tip_page_show(const char *bg_src, const char *icon_src, const char *text);

///< 只重填文案(不动已上屏的 bg/icon 与显隐), 供"匹配中"点数动画这类逐帧改字用
void pop_tip_page_set_text(const char *text);

///< 隐藏弹窗(释放遮罩图缓存)
void pop_tip_page_hide(void);

///< 弹窗是否正在显示
bool pop_tip_page_is_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* POP_TIP_PAGE_H */