#ifndef LANGUAGE_H_
#define LANGUAGE_H_

#include "ui_init.h"
#include "aic_ui.h"
#include "button_manager.h"

/* L3 语言选项（可扩展：新增语言在此追加枚举 + language.c 的 item_key 表） */
typedef enum {
    LANGUAGE_ENGLISH_OPTION = 0,
    LANGUAGE_CHINESE_OPTION,
    LANGUAGE_KOREAN_OPTION,
    LANGUAGE_SINGAPOREAN_OPTION,
    LANGUAGE_THAI_OPTION,
    LANGUAGE_OPTION_MAX,
} language_option_e;

/* 语言选项 → 系统语言类型（两套枚举前两项相反，勿直接强转；开机恢复亦复用） */
lang_type_t language_option_to_lang_type(language_option_e o);

/* 进入 L3：懒加载构建 language 选项控件 + 切层 + 默认聚焦 */
void language_init(void);

/* 按键处理（up/down 在选项表内循环移动，set 应用语言，back 回 L2） */
void on_language_deal_short_key(click_e key);

/* 销毁 L3 控件并复位状态（退出 L3 / 关弹窗时调用） */
void language_destroy(void);

/* 语言切换刷新：保留翻页/焦点位置 */
void language_relang(void);

#endif
