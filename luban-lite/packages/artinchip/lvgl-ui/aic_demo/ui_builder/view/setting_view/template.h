#ifndef TEMPLATE_H_
#define TEMPLATE_H_

#include <stdbool.h>

/* ===== 可复用卡片模板（setting_page_create 时预建，常驻 hidden） =====
 * tpl3：3槽位（pop_menu3 + label LEFT + arrow）
 * tpl4：4槽位（pop_menu4 + label LEFT，无 arrow）
 * 各列表页 init 时 activate + set_slot/set_label_mode，refresh 时 set_card_pressed，
 * destroy 时 hide（不删模板控件，常驻复用）。 */

/* ---- per-resolution 布局（定义于各分辨率 setting_page.c）----
 * 标题/label 对齐随分辨率切换；template.c 仅调用，不含坐标。 */
void tpl3_layout_title(bool has_icon);
void tpl4_layout_title(bool has_icon);
void tpl3_layout_label(int slot, bool center);
void tpl4_layout_label(int slot, bool center);
/* 字号同样随分辨率：把 tpl3/tpl4/tpl_yn 的标题与槽位 label 字体按当前语言重设
 * （未构建的控件安全跳过） */
void tpl_layout_font(void);

/* ---- 3槽位模板 ---- */
/* 显 tpl3 + 隐 tpl4，设标题文字；icon_src=NULL 时隐图标位 */
void tpl3_activate(const char *title, const char *icon_src);
/* 设 slot 文字 + 整槽显隐（visible=false 隐卡片/label/arrow） */
void tpl3_set_slot(int slot, const char *text, bool visible);
/* 设所有 slot label 对齐（center=true 居中/false 左对齐）+ arrow 显隐 */
void tpl3_set_label_mode(bool center, bool show_arrow);
/* 切 slot n/p 选中态 + label opa(选中255/未选178) */
void tpl3_set_card_pressed(int slot, bool pressed);
/* 隐 tpl3 */
void tpl3_hide(void);

/* ---- 4槽位模板 ---- */
void tpl4_activate(const char *title, const char *icon_src);
/* 设 slot 文字 + 整槽显隐（visible=false 隐卡片/label/arrow） */
void tpl4_set_slot(int slot, const char *text, bool visible);
/* 设所有 slot label 对齐（center=true 居中/false 左对齐）+ arrow 显隐 */
void tpl4_set_label_mode(bool center, bool show_arrow);
/* 切 slot n/p 选中态 + label opa(选中255/未选178) */
void tpl4_set_card_pressed(int slot, bool pressed);
/* 隐 tpl4 */
void tpl4_hide(void);

/* ---- YES/NO 按钮模板（498×110 组合图，切 src） ---- */
/* 显 tpl_yn + 隐 tpl3/tpl4，设标题（无图标，居中） */
void tpl_yn_activate(const char *title);
/* 切换选中：yes=true 显 pop_btn_yes_n，false 显 pop_btn_no_n */
void tpl_yn_set_focus(bool yes);
/* 隐 tpl_yn */
void tpl_yn_hide(void);

#endif
