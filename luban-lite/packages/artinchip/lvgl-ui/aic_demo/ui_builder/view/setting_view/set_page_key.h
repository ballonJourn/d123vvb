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
#endif  // MENU_MANAGER_H_
