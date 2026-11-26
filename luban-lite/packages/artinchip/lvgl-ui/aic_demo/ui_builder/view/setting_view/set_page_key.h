#ifndef MENU_MANAGER_H_
#define MENU_MANAGER_H_

#include "button_manager.h"
#include <stdbool.h>
void menu_manager_click(const click_e);

void set_page_deal_key_down ();
void set_page_deal_key_up   ();
void set_page_deal_key_set  ();
void set_page_deal_key_back ();

bool check_clicked_isVaild(int menu_index);
int get_current_menu_index();

/* 注册/清空当前 L4 按键处理器（L3 进入 L4 时设置，L4 退出时清空） */
void set_active_l4_handler(void (*handler)(click_e));

/* 注册/清空当前 L4 语言刷新处理器（与 set_active_l4_handler 成对） */
void set_active_l4_lang_refresh(void (*handler)(void));

/* 语言切换时按当前层重填文案（保留焦点位置），由 setting_page 层调用 */
void menu_manager_lang_refresh(void);
#endif  // MENU_MANAGER_H_
