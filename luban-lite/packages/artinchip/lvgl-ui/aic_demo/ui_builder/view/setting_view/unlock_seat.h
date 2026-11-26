#ifndef UNLOCK_SEAT_H_
#define UNLOCK_SEAT_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L2 Unlock the seat：YES/NO 确认，YES 发 0x451 Bat_Seat=1，NO 无动作 */
void unlock_seat_init(void);
void on_unlock_seat_deal_short_key(click_e key);
void unlock_seat_destroy(void);

/* 语言切换刷新：保留 YES/NO 选中态 */
void unlock_seat_relang(void);

#endif